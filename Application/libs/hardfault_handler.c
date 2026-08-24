/*
 * hardfault_handler.c
 *
 *  Created on: Sep 20, 2022
 *      Author: fatih.ozcan
 *
 *  Refs:
 *       -Using Cortex-M3/M4/M7 Fault Exceptions (https://www.keil.com/appnotes/files/apnt209.pdf)
 *       -https://wiki.segger.com/Cortex-M_Fault
 */
#include <bsp.h>
#include "hardfault_handler.h"
#include "main.h"


#define SYSHND_CTRL (*(volatile uint32_t *)(0xE000ED24u))  // System Handler Control and State Register
#define NVIC_MFSR   (*(volatile uint8_t  *)(0xE000ED28u))  // Memory Management Fault Status Register
#define NVIC_BFSR   (*(volatile uint8_t  *)(0xE000ED29u))  // Bus Fault Status Register
#define NVIC_UFSR   (*(volatile uint16_t *)(0xE000ED2Au))  // Usage Fault Status Register
#define NVIC_HFSR   (*(volatile uint32_t *)(0xE000ED2Cu))  // Hard Fault Status Register
#define NVIC_DFSR   (*(volatile uint16_t *)(0xE000ED30u))  // Debug Fault Status Register
#define NVIC_BFAR   (*(volatile uint32_t *)(0xE000ED38u))  // Bus Fault Manage Address Register
#define NVIC_AFSR   (*(volatile uint16_t *)(0xE000ED3Cu))  // Auxiliary Fault Status Register

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
    uint32_t UnusedBits2 : 27;
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

/* Auxiliary Bus Fault Status Register (ABFSR) */
typedef union __attribute__((packed))
{
	volatile uint32_t ABFSR;
	struct
	{
		uint32_t ITCM        :1; /* Asynchronous fault on ITCM interface */
		uint32_t DTCM        :1; /* Asynchronous fault on DTCM interface */
		uint32_t AHBP        :1; /* Asynchronous fault on AHBP interface */
		uint32_t AXIM        :1; /* Asynchronous fault on AXIM interface */
		uint32_t EPPB        :4; /* Asynchronous fault on EPPB interface */
		uint32_t UnusedBits  :3;
		uint32_t AXIMTYPE    :2; /* Indicates the type of fault on the AXIM interface. */
		uint32_t UnusedBits2 :19;
	};
}ABFSR_t;


/* BFAR: Data address for a precise BusFault. This register is updated with the address of a location that
produced a BusFault. The BFSR shows the reason for the fault. This field is valid only when
BFSR.BFARVALID is set. */
static uint32_t bfar = 0;

/* MMFAR: Data address for a MemManage fault. This register is updated with the address of a location
that produced a MemManage fault. The MMFSR shows the cause of the fault. This field is valid
only when MMFSR.MMARVALID is set. */
static uint32_t mmfar = 0;

static uint32_t afsr = 0, cfsr = 0;

static MMFSR_t mmsfr;
static HFSR_t hfsr;
static DFSR_t dfsr;
static BFSR_t bfsr;
static UFSR_t ufsr;
static ABFSR_t abfsr;

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

static inline void print_hardfault_status(HFSR_t hfsr)
{
	if(hfsr.VECTBL){
		CSLOG_ERR( " HFSR: VECTTBL\r\n");
	}
	if(hfsr.FORCED){
		CSLOG_ERR( " HFSR: FORCED\r\n");
	}
	if(hfsr.DEBUGEVT){
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

static void print_auxiliary_bus_fault(ABFSR_t abus_fault)
{
	if (abus_fault.ITCM)
		CSLOG_ERR("Auxiliary Bus Fault: ITCM\r\n");
	if (abus_fault.DTCM)
		CSLOG_ERR("Auxiliary Bus Fault: DTCM\r\n");
	if (abus_fault.AHBP)
		CSLOG_ERR("Auxiliary Bus Fault: AHBP\r\n");
	if (abus_fault.AXIM)
		CSLOG_ERR("Auxiliary Bus Fault: AXIM\r\n");
	if (abus_fault.EPPB)
		CSLOG_ERR("Auxiliary Bus Fault: EPPB\r\n");
	if (abus_fault.AXIMTYPE)
		CSLOG_ERR("Auxiliary Bus Fault: AXIMTYPE val: %02b\r\n", abus_fault.AXIMTYPE);
}


void HardFault_Handler_C(StackFrame_t *StackFrame, uint32_t lr_value)
{
	print_stackframe(StackFrame, lr_value);
	CSLOG( " SCB->BFAR  = 0x%08x\r\n", bfar);
	CSLOG( " SCB->MMFAR = 0x%08x\r\n", mmfar);
	CSLOG( " SCB->CFSR  = 0x%08x\r\n", cfsr);
	CSLOG( " SCB->HFSR  = 0x%08x\r\n", hfsr);
	CSLOG( " SCB->DFSR  = 0x%08x\r\n", dfsr);
	CSLOG( " SCB->AFSR  = 0x%08x\r\n", afsr);
	print_hardfault_status((HFSR_t)hfsr);
	print_memfault(mmsfr);
	if (cfsr & 0x0080)
		CSLOG( " MMFAR = 0x%x *\r\n", mmfar);
	print_bus_fault(bfsr);
	if (cfsr & 0x8000)
		CSLOG( " *BFAR = 0x%x *\r\n", bfar);

	print_usage_fault(ufsr);
	print_auxiliary_bus_fault(abfsr);

	//__ASM volatile("BKPT #01");
	 while(1);
}

__attribute__((naked)) void HardFault_Handler(void)
{
    cfsr  = SCB->CFSR;           /* Configurable Fault Status Register */
    hfsr.HFSR  = SCB->HFSR;      /* HardFault Status Register */
    dfsr.DFSR  = SCB->DFSR;      /* Debug Fault Status Register */
    bfsr.BFSR  = NVIC_BFSR;      /* Bus Fault Status Register */
    afsr  = SCB->AFSR;           /* Auxiliary Fault Status Register */
    ufsr.UFSR  = NVIC_UFSR;      /* Usage Fault Status Register */
    abfsr.ABFSR = SCB->AFSR; /* Auxiliary Bus Fault Status Register */

    mmfar = SCB->MMFAR; /* MemManage Fault Address Register */
    bfar  = SCB->BFAR;  /* BusFault Address Register */

//	// Load MSP to R0
//	// R0 is used to keep first argument during function call
//	// We used this functionality to copy MSP to StackFrame struct
//	asm volatile("MRS R0, MSP");
//
//	// Jump to HardFault_Handler C function
//	asm volatile("B HardFault_Handler_");

	asm volatile(
			"TST    LR, #4 \r\n\t\
			ITE    EQ      \r\n\t\
			MRSEQ  R0, MSP \r\n\t\
			MRSNE  R0, PSP \r\n\t\
			MOV    R1, LR  \r\n\t\
			B      HardFault_Handler_C"
			);
}
