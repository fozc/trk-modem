/*
 * hardfault_handler.c
 *
 *  Created on: Sep 20, 2022
 *      Author: fatih.ozcan
 *
 *  Refs:
 *       -Using Cortex-M3/M4/M7 Fault Exceptions (https://www.keil.com/appnotes/files/apnt209.pdf)
 *       -https://wiki.segger.com/Cortex-M_Fault
 *
 *  Structure (bootloader repo ab95b31 ile ayni desen):
 *   - HardFault_Handler is naked and contains BASIC ASSEMBLY ONLY: it
 *     selects the stacked frame (MSP or PSP per EXC_RETURN bit 2) and
 *     tail-branches to the C handler.  GCC does not support C code in
 *     naked functions -- every register access lives in the C handler.
 *   - HardFault_Handler_C captures the fault status registers through
 *     the CMSIS SCB accessors (MMFSR/BFSR/UFSR are sub-fields of CFSR,
 *     no separate bus reads), prints a full dump and halts.
 *   - ABFSR is Cortex-M7-only; it does not exist on the M33 and the
 *     old code read AFSR twice under that name -- removed entirely.
 *   - App-specific: the fault trace is stashed to TAMP backup registers
 *     FIRST (survives the watchdog reset that follows); app_main
 *     persists it to elog at the next boot. Release policy is halt and
 *     let the external watchdog reset the device.
 */
#include <bsp.h>
#include "hardfault_handler.h"
#include "main.h"
#include "rtc.h"
#include "console_logger.h"

typedef struct __attribute__((packed))
{
	volatile uint32_t R0;
	volatile uint32_t R1;
	volatile uint32_t R2;
	volatile uint32_t R3;
	volatile uint32_t R12;
	volatile uint32_t LR;      // Link Register
	volatile uint32_t PC;      // Program Counter
	union
	{
		volatile uint32_t xPSR;
		struct
		{
			uint32_t IPSR:8;   // Interrupt Program Status Register
			uint32_t EPSR:19;  // Execution Program Status Register
			uint32_t APSR:5;   // Application Program Status Register
		};
	};
}StackFrame_t;

/* HardFault Status Register */
typedef union __attribute__((packed))
{
  volatile uint32_t HFSR;
  struct
  {
    uint32_t UnusedBits  : 1;
    uint32_t VECTBL      : 1;      // Indicates hard fault is caused by failed vector fetch
    uint32_t UnusedBits2 : 28;     /* 1+1+28 = 30: FORCED must land on bit 30,
                                    * DEBUGEVT on bit 31 (HFSR layout) */
    uint32_t FORCED      : 1;      // Indicates hard fault is taken because of bus fault/memory management fault/usage fault
    uint32_t DEBUGEVT    : 1;      // Indicates hard fault is triggered by debug event
  };
}HFSR_t;                                // Hard Fault Status Register (0xE000ED2C)

/* Memory Management Fault Status Register */
typedef union __attribute__((packed))
{
  volatile uint8_t MMFSR;
  struct
  {
    uint8_t IACCVIOL    : 1; /* The processor attempted an instruction fetch from a location that does not permit execution */
    uint8_t DACCVIOL    : 1; /* The processor attempted a load or store at a location that does not permit the operation. */
    uint8_t UnusedBits  : 1;
    uint8_t MUNSTKERR   : 1; /* Unstack for an exception return has caused one or more access violations. */
    uint8_t MSTKERR     : 1; /* Stacking for an exception entry has caused one or more access violations. */
    uint8_t MLSPERR     : 1; /* A MemManage fault occurred during floating-point lazy state preservation. */
    uint8_t UnusedBits2 : 1;
    uint8_t MMARVALID   : 1; /* 0: Value in MMAR is not a valid fault address. 1: MMAR holds a valid fault address */
  };
}MMFSR_t;

/* Bus Fault Status Register  */
typedef union __attribute__((packed))
{
  volatile uint8_t BFSR;
  struct
  {
	  uint8_t IBUSERR    : 1; /* instruction bus error */
	  uint8_t PRECISERR  : 1; /* a data bus error has occurred, and the PC value stacked for the exception return points to the instruction*/
	  uint8_t IMPRECISERR: 1; /* a data bus error has occurred, but the return address in the stack frame is not related to the instruction */
	  uint8_t UNSTKERR   : 1; /* unstack for an exception return has caused one or more BusFaults */
	  uint8_t STKERR     : 1; /* stacking for an exception entry has caused one or more BusFaults. */
	  uint8_t LSPERR     : 1; /*  fault occurred during floating-point lazy state preservation */
	  uint8_t UnusedBits : 1;
	  uint8_t BFARVALID  : 1; /* BFAR holds a valid fault address. */
  };
}BFSR_t;

typedef union __attribute__((packed))
{
  volatile uint32_t DFSR;
  struct
  {
    uint32_t HALTED   : 1;         // Halt requested in NVIC
    uint32_t BKPT     : 1;         // BKPT instruction executed
    uint32_t DWTTRAP  : 1;         // DWT match occurred
    uint32_t VCATCH   : 1;         // Vector fetch occurred
    uint32_t EXTERNAL : 1;         // EDBGRQ signal asserted
  };
}DFSR_t;

/* UsageFault Status Register (UFSR) */
typedef union __attribute__((packed))
{
	volatile uint16_t UFSR;
	struct
	{
		uint16_t UNDEFINSTR :1; /* the processor has attempted to execute an undefined instruction. */
		uint16_t INVSTATE   :1; /* the processor has attempted to execute an instruction that makes illegal use of the Execution Program Status Register (EPSR). */
		uint16_t INVPC      :1; /* the processor has attempted to load an illegal EXC_RETURN value to the PC as a result of an invalid context switch. */
		uint16_t NOCP       :1; /* the processor has attempted to access a coprocessor that does not exist. */
		uint16_t UnusedBits :4;
		uint16_t UNALIGNED  :1; /* the processor has made an unaligned memory access. */
		uint16_t DIVBYZERO  :1; /* the processor has executed an SDIV or UDIV instruction with a divisor of 0 */
		uint16_t UnusedBits2:6;
	};
}UFSR_t;

/* BFAR: Data address for a precise BusFault. This register is updated with the address of a location that
produced a BusFault. The BFSR shows the reason for the fault. This field is valid only when
BFSR.BFARVALID is set. */
static uint32_t bfar = 0;

/* MMFAR: Data address for a MemManage fault. This register is updated with the address of a location that
produced a MemManage fault. The MMFSR shows the cause of the fault. This field is valid
only when MMFSR.MMARVALID is set. */
static uint32_t mmfar = 0;

static uint32_t afsr = 0, cfsr = 0;

static MMFSR_t mmsfr;
static HFSR_t hfsr;
static DFSR_t dfsr;
static BFSR_t bfsr;
static UFSR_t ufsr;

/*
 * Capture the fault status registers.  Runs in normal C context (first
 * thing in the handler) -- never in the naked wrapper, where GCC does
 * not support C code.  MMFSR/BFSR/UFSR are sub-fields of CFSR, so they
 * are extracted from the value already read instead of re-addressing
 * the PPB.
 */
static void hardfault_capture_status(void)
{
	cfsr       = SCB->CFSR;
	hfsr.HFSR  = SCB->HFSR;
	dfsr.DFSR  = SCB->DFSR;
	afsr       = SCB->AFSR;
	mmfar      = SCB->MMFAR;
	bfar       = SCB->BFAR;

	mmsfr.MMFSR = (uint8_t)(cfsr & 0xFFU);
	bfsr.BFSR   = (uint8_t)((cfsr >> 8) & 0xFFU);
	ufsr.UFSR   = (uint16_t)((cfsr >> 16) & 0xFFFFU);
}

static void print_stackframe(StackFrame_t *StackFrame, uint32_t lr_val)
{
	CSLOG_ERR(" R0  : 0x%08x\r\n", StackFrame->R0);
	CSLOG_ERR(" R1  : 0x%08x\r\n", StackFrame->R1);
	CSLOG_ERR("R2  : 0x%08x\r\n", StackFrame->R2);
	CSLOG_ERR("R3  : 0x%08x\r\n", StackFrame->R3);
	CSLOG_ERR("R12 : 0x%08x\r\n", StackFrame->R12);
	CSLOG_ERR("LR  : 0x%08x\r\n", StackFrame->LR);
	CSLOG_ERR("PC  : 0x%08x *\r\n", StackFrame->PC);
	CSLOG_ERR("xPSR: 0x%08x\r\n", StackFrame->xPSR);
	CSLOG_ERR("LR/EXC_RETURN= 0x%08x\r\n", lr_val);
}

static inline void print_hardfault_status(HFSR_t hfsr_val)
{
	if(hfsr_val.VECTBL){
		CSLOG_ERR( " HFSR: VECTTBL\r\n");
	}
	if(hfsr_val.FORCED){
		CSLOG_ERR( " HFSR: FORCED\r\n");
	}
	if(hfsr_val.DEBUGEVT){
		CSLOG_ERR( " HFSR: DEBUGEVT\r\n");
	}
}

static inline void print_memfault(MMFSR_t mmfsr)
{
	if (mmfsr.IACCVIOL)
		CSLOG_ERR("Memory Management Fault: IACCVIOL\r\n");
	if (mmfsr.DACCVIOL)
		CSLOG_ERR("Memory Management Fault: DACCVIOL\r\n");
	if (mmfsr.MUNSTKERR)
		CSLOG_ERR("Memory Management Fault: MUNSTKERR\r\n");
	if (mmfsr.MSTKERR)
		CSLOG_ERR("Memory Management Fault: MSTKERR\r\n");
	if (mmfsr.MLSPERR)
		CSLOG_ERR("Memory Management Fault: MLSPERR\r\n");
	if (mmfsr.MMARVALID)
		CSLOG_ERR("Memory Management Fault: MMARVALID\r\n");
}

static void print_bus_fault(BFSR_t bus_fault)
{
	if (bus_fault.IBUSERR)
		CSLOG_ERR("Bus Fault: IBUSERR\r\n");
	if (bus_fault.PRECISERR)
		CSLOG_ERR("Bus Fault: PRECISERR\r\n");
	if (bus_fault.IMPRECISERR)
		CSLOG_ERR("Bus Fault: IMPRECISERR\r\n");
	if (bus_fault.UNSTKERR)
		CSLOG_ERR("Bus Fault: UNSTKERR\r\n");
	if (bus_fault.STKERR)
		CSLOG_ERR("Bus Fault: STKERR\r\n");
	if (bus_fault.LSPERR)
		CSLOG_ERR("Bus Fault: LSPERR\r\n");
	if (bus_fault.BFARVALID)
		CSLOG_ERR("Bus Fault: BFARVALID\r\n");
}

static void print_usage_fault(UFSR_t usage_fault)
{
	if (usage_fault.UNDEFINSTR)
		CSLOG_ERR("Usage Fault: UNDEFINSTR\r\n");
	if (usage_fault.INVSTATE)
		CSLOG_ERR("Usage Fault: INVSTATE\r\n");
	if (usage_fault.INVPC)
		CSLOG_ERR("Usage Fault: INVPC\r\n");
	if (usage_fault.NOCP)
		CSLOG_ERR("Usage Fault: NOCP\r\n");
	if (usage_fault.UNALIGNED)
		CSLOG_ERR("Usage Fault: UNALIGNED\r\n");
	if (usage_fault.DIVBYZERO)
		CSLOG_ERR("Usage Fault: DIVBYZERO\r\n");
}

void HardFault_Handler_C(StackFrame_t *StackFrame, uint32_t lr_value)
{
	hardfault_capture_status();

	/* Stash the fault trace in TAMP backup registers FIRST: these survive
	 * the watchdog reset that follows, and app_main persists them to elog
	 * at the next boot. Plain register writes, safe in fault context. */
	rtc_bkpr_write(HF_BKPR_DR_PC, StackFrame->PC);
	rtc_bkpr_write(HF_BKPR_DR_LR, StackFrame->LR);
	rtc_bkpr_write(HF_BKPR_DR_CFSR, cfsr);
	rtc_bkpr_write(HF_BKPR_DR_HFSR, hfsr.HFSR);
	rtc_bkpr_write(HF_BKPR_DR_MAGIC, HF_BKPR_MAGIC);

	/* Fault output must never be silenced by a disabled console logger
	 * (the main loop keeps it off during normal operation). */
	console_logger_set_enabled(true, false);

	print_stackframe(StackFrame, lr_value);
	CSLOG( " SCB->BFAR  = 0x%08x\r\n", bfar);
	CSLOG( " SCB->MMFAR = 0x%08x\r\n", mmfar);
	CSLOG( " SCB->CFSR  = 0x%08x\r\n", cfsr);
	CSLOG( " SCB->HFSR  = 0x%08x\r\n", hfsr.HFSR);
	CSLOG( " SCB->DFSR  = 0x%08x\r\n", dfsr.DFSR);
	CSLOG( " SCB->AFSR  = 0x%08x\r\n", afsr);
	print_hardfault_status(hfsr);
	print_memfault(mmsfr);
	if (cfsr & 0x0080)
		CSLOG( " MMFAR = 0x%x *\r\n", mmfar);
	print_bus_fault(bfsr);
	if (cfsr & 0x8000)
		CSLOG( " *BFAR = 0x%x *\r\n", bfar);

	print_usage_fault(ufsr);

	CSLOG_ERR("\r\n*** HardFault: halting - watchdog will reset ***\r\n");

	/* App policy: no direct NVIC_SystemReset here. The external watchdog
	 * performs the reset, and the stashed trace above reaches elog on the
	 * next boot (app_main elog_log_boot_events). */
	while(1);
}

__attribute__((naked)) void HardFault_Handler(void)
{
	/* Basic assembly only -- GCC does not support C code in naked
	 * functions.  Select the stacked frame per EXC_RETURN bit 2 and
	 * tail-branch to the C handler, which never returns. */
	__asm volatile
	(
		"tst   lr, #4              \r\n"
		"ite   eq                  \r\n"
		"mrseq r0, msp             \r\n"
		"mrsne r0, psp             \r\n"
		"mov   r1, lr              \r\n"
		"b     HardFault_Handler_C \r\n"
	);
}

/*** end of file ***/
