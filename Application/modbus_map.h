/*
 * modbus_map.h
 *
 *  Created on: Oct 15, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Fixed Modbus RTU register map - single source of all block addresses.
 *
 * The product register map is compile-time fixed. Every block base address,
 * block length and feeder field offset of the Modbus slave is defined here
 * and nowhere else; the block readers resolve their registers from these
 * definitions via MODBUS_REG(). NVRAM stores no register addresses.
 *
 * Feeder field offsets are structure-bound: the feeder block declares pure
 * layout descriptors (structs of uint16_t members that are never
 * instantiated). A field address is derived from its member offset, so
 * inserting or removing a field renumbers the following addresses
 * automatically and a layout can never contain padding or hidden holes.
 * Add new fields only at the tail of a layout so already-documented
 * addresses stay stable.
 *
 * Block summary (logical 40000-based holding references, FC03 unless noted):
 *   40000 + n * 1000   feeder n block, n = 0..3 (RF Fider 1..4 mirror)
 *   49000 .. 49016     system statistics (read-only)
 *   49200 .. 49237     power-board SCP summary (read-only)
 *   49300 .. 49378     BMS telemetry (read-only)
 *   49400 .. 49409     GSM status (read-only)
 *   50000              battery-warning control register (read-only)
 *   50001              modem-reset command register (FC06, value 1)
 *   50002 .. 50003     relay commanded states (FC03/FC06, values 0/1)
 *
 * See MODBUS_REGISTER_MAP.md for the customer-facing description.
 */

#ifndef MODBUS_MAP_H
#define MODBUS_MAP_H

#include <stdint.h>
#include <stddef.h>

/* ---- Block base addresses (logical holding references) ------------------ */

#define MODBUS_MAP_LINE_BASE        40000U

/* Feeder block stride in registers. Fixed so a master can compute every
 * feeder base arithmetically; assert-checked against the layout below. */
#define MODBUS_MAP_LINE_STRIDE      1000U

/* The Modbus map mirrors exactly the four provisioned RF feeders (RF
 * Fider_ID 1..4, RF_ACTIVE_FEEDERS in rf.c). Modbus fider n (n = 1..4) is
 * the RF line whose Fider_ID assignment equals n. */
#define MODBUS_MAP_LINE_COUNT       4U

#define MODBUS_MAP_SYS_BASE         49000U
#define MODBUS_MAP_SYS_REG_COUNT    17U
#define MODBUS_MAP_PWR_BASE         49200U
#define MODBUS_MAP_PWR_REG_COUNT    38U
#define MODBUS_MAP_BMS_BASE         49300U
#define MODBUS_MAP_BMS_REG_COUNT    79U
#define MODBUS_MAP_GSM_BASE         49400U
#define MODBUS_MAP_GSM_REG_COUNT    10U

#define MODBUS_MAP_CTRL_AKU_UYARISI     50000U
#define MODBUS_MAP_CTRL_MODEM_RESET     50001U
#define MODBUS_MAP_CTRL_RELAY1          50002U
#define MODBUS_MAP_CTRL_RELAY2          50003U

/* Logical Modbus address of a layout member. Constant expression, usable as
 * a switch case label; for an array member it yields the address of the
 * first element and the remaining elements follow contiguously. */
#define MODBUS_REG(base, map_t, member) \
    ((uint16_t)((base) + \
                offsetof(map_t, member) / sizeof(uint16_t)))

/* ---- Block bounds (derive only here; never hand-copy a range) ---------- */

#define MODBUS_MAP_LINE_ADDR_FIRST  (MODBUS_MAP_LINE_BASE)
#define MODBUS_MAP_LINE_ADDR_LAST \
    ((uint16_t)(MODBUS_MAP_LINE_BASE + \
                (MODBUS_MAP_LINE_COUNT * MODBUS_MAP_LINE_STRIDE) - 1U))

#define MODBUS_MAP_SYS_ADDR_LAST \
    ((uint16_t)(MODBUS_MAP_SYS_BASE + MODBUS_MAP_SYS_REG_COUNT - 1U))
#define MODBUS_MAP_PWR_ADDR_LAST \
    ((uint16_t)(MODBUS_MAP_PWR_BASE + MODBUS_MAP_PWR_REG_COUNT - 1U))
#define MODBUS_MAP_BMS_ADDR_LAST \
    ((uint16_t)(MODBUS_MAP_BMS_BASE + MODBUS_MAP_BMS_REG_COUNT - 1U))
#define MODBUS_MAP_GSM_ADDR_LAST \
    ((uint16_t)(MODBUS_MAP_GSM_BASE + MODBUS_MAP_GSM_REG_COUNT - 1U))

/* Blocks must stay disjoint and ordered. Adding a block or growing one past
 * its neighbour breaks the build here, not on a customer bus. */
_Static_assert(MODBUS_MAP_LINE_ADDR_LAST < MODBUS_MAP_SYS_BASE,
               "modbus map: feeder blocks overlap system statistics");
_Static_assert(MODBUS_MAP_SYS_ADDR_LAST < MODBUS_MAP_PWR_BASE,
               "modbus map: system statistics overlap power summary");
_Static_assert(MODBUS_MAP_PWR_ADDR_LAST < MODBUS_MAP_BMS_BASE,
               "modbus map: power summary overlaps BMS telemetry");
_Static_assert(MODBUS_MAP_BMS_ADDR_LAST < MODBUS_MAP_GSM_BASE,
               "modbus map: BMS telemetry overlaps GSM status");
_Static_assert(MODBUS_MAP_GSM_ADDR_LAST < MODBUS_MAP_CTRL_AKU_UYARISI,
               "modbus map: GSM status overlaps control registers");
_Static_assert(MODBUS_MAP_CTRL_MODEM_RESET > MODBUS_MAP_CTRL_AKU_UYARISI,
               "modbus map: control registers must stay ordered");
_Static_assert(MODBUS_MAP_CTRL_RELAY1 > MODBUS_MAP_CTRL_MODEM_RESET,
               "modbus map: relay 1 overlaps modem reset");
_Static_assert(MODBUS_MAP_CTRL_RELAY2 > MODBUS_MAP_CTRL_RELAY1,
               "modbus map: relay registers must stay ordered");

/* ---- Feeder block quality bits ------------------------------------------ */

/*
 * Phase quality register (faz[n].kalite). A master may trust a published
 * value only when the relevant bit is set. All-zero quality marks either an
 * undefined feeder or a phase that has never delivered a live sample; the
 * accompanying data registers are zero-filled and must not be read as
 * measurements.
 */
#define MODBUS_MAP_PH_Q_DEFINED         (1U << 0U)  /* feeder provisioned    */
#define MODBUS_MAP_PH_Q_HAS_LIVE        (1U << 1U)  /* live sample received  */
#define MODBUS_MAP_PH_Q_FRESH           (1U << 2U)  /* link fresh (<30 s)    */
#define MODBUS_MAP_PH_Q_CURRENT_VALID   (1U << 3U)  /* RMS usable            */
#define MODBUS_MAP_PH_Q_TRIP_V_VALID    (1U << 4U)  /* trip voltage usable   */
#define MODBUS_MAP_PH_Q_HARVEST_V_VALID (1U << 5U)  /* harvest voltage usable*/
#define MODBUS_MAP_PH_Q_TIME_VALID      (1U << 6U)  /* sample time usable    */

/* Feeder quality register (fider_kalite). */
#define MODBUS_MAP_FD_Q_DEFINED         (1U << 0U)  /* feeder provisioned    */
#define MODBUS_MAP_FD_Q_ANY_LIVE        (1U << 1U)  /* any phase delivered   */

/* ---- Feeder block layouts ----------------------------------------------- */

/* Fault records kept per phase and type. Parity with the IEC104 fault lists
 * (TEMPORARY_FAULT_COUNT / PERMANENT_FAULT_COUNT, libiec104/iec104_config.h);
 * the IEC104 side is the source of record, Modbus only mirrors it. */
#define MODBUS_MAP_FAULT_RECORDS    15U

/* Per-phase live data: the IEC104 information mirror (current, energy, load,
 * link, trip-failure - each with its own readable sample time) plus the
 * SCADA-meaningful subset of the RF live frame. 32-bit values are UINT32
 * unix seconds or FLOAT32, both big-endian high word first (ABCD). */
typedef struct
{
    uint16_t kalite;               /* phase quality, MODBUS_MAP_PH_Q_*      */
    uint16_t anlik_akim[2];        /* FLOAT32 A, SCP Live_Irms              */
    uint16_t anlik_zaman[2];       /* UINT32 unix, last live sample         */
    uint16_t enerji_varyok;        /* 0/1, LIVE Status_Flags bit 0          */
    uint16_t enerji_zaman[2];      /* UINT32 unix                           */
    uint16_t yuk_akimi_varyok;     /* 0/1, LIVE Status_Flags bit 1          */
    uint16_t yuk_zaman[2];         /* UINT32 unix                           */
    uint16_t rf_haberlesme_varyok; /* 0/1, fresh live link                  */
    uint16_t rf_zaman[2];          /* UINT32 unix                           */
    uint16_t trip_failed;          /* 0/1, SCP trip-failure alarm           */
    uint16_t trip_zaman[2];        /* UINT32 unix                           */
    uint16_t trip_gerilimi[2];     /* FLOAT32 V                             */
    uint16_t harvest_gerilimi[2];  /* FLOAT32 V                             */
    uint16_t sicaklik;             /* INT16 two's complement, degC          */
    uint16_t rssi;                 /* INT16 two's complement, RF raw        */
    uint16_t fault_count;          /* AY fault counter                      */
} modbus_phase_map_t;              /* 24 registers                          */

/* One fault-log record mirror. zaman == 0 marks an empty slot (fewer
 * records stored than MODBUS_MAP_FAULT_RECORDS). */
typedef struct
{
    uint16_t ariza_akimi[2];       /* FLOAT32 A, fault current              */
    uint16_t ariza_suresi_ms[2];   /* UINT32 ms, fault duration             */
    uint16_t enerji_varyok;        /* 0/1 at the time of the fault          */
    uint16_t yuk_akimi_varyok;     /* 0/1 at the time of the fault          */
    uint16_t zaman[2];             /* UINT32 unix, 0 = empty slot           */
} modbus_fault_record_map_t;       /* 8 registers                           */

/* One feeder block. Reserved gaps keep the sub-block bases at round offsets
 * and leave room for tail growth without moving documented addresses. */
typedef struct
{
    uint16_t fider_kalite;         /* MODBUS_MAP_FD_Q_*                     */
    uint16_t reserved_1_3[3];
    modbus_phase_map_t faz[3];     /* L1, L2, L3 sub-blocks at +4           */
    uint16_t temp_fault_count[3];  /* valid records per phase               */
    uint16_t perm_fault_count[3];  /* valid records per phase               */
    uint16_t reserved_82_99[18];
    modbus_fault_record_map_t
        temp_fault[3][MODBUS_MAP_FAULT_RECORDS];  /* at +100                */
    uint16_t reserved_460_499[40];
    modbus_fault_record_map_t
        perm_fault[3][MODBUS_MAP_FAULT_RECORDS];  /* at +500                */
    uint16_t reserved_860_999[140];
} modbus_feeder_map_t;

/* Register offsets of feeder sub-structures and members (0-based, inside
 * their own layout). Constant expressions by construction. */
#define MODBUS_MAP_PHASE_REGS \
    (sizeof(modbus_phase_map_t) / sizeof(uint16_t))
#define MODBUS_MAP_RECORD_REGS \
    (sizeof(modbus_fault_record_map_t) / sizeof(uint16_t))
#define MODBUS_MAP_PHASE_OFF(member) \
    (offsetof(modbus_phase_map_t, member) / sizeof(uint16_t))
#define MODBUS_MAP_RECORD_OFF(member) \
    (offsetof(modbus_fault_record_map_t, member) / sizeof(uint16_t))
#define MODBUS_MAP_FEEDER_OFF(member) \
    (offsetof(modbus_feeder_map_t, member) / sizeof(uint16_t))

_Static_assert((sizeof(modbus_feeder_map_t) / sizeof(uint16_t))
                   == MODBUS_MAP_LINE_STRIDE,
               "modbus map: feeder layout must exactly fill the stride");
_Static_assert((3U * MODBUS_MAP_FAULT_RECORDS * MODBUS_MAP_RECORD_REGS)
                   <= (500U - 100U),
               "modbus map: temporary fault region overruns +100..+499");
_Static_assert((MODBUS_MAP_FEEDER_OFF(perm_fault)
                + (3U * MODBUS_MAP_FAULT_RECORDS * MODBUS_MAP_RECORD_REGS))
                   <= MODBUS_MAP_LINE_STRIDE,
               "modbus map: permanent fault region overruns the block");

#endif /* MODBUS_MAP_H */

/*** end of file ***/
