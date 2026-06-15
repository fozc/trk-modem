/**
 * @file modbus_rtu_slave.h
 * @brief Modbus RTU Slave Implementation for Cortex-M
 * @author Fatih Ozcan
 *         fatihozcan@gmail.com
 * @date 2025-10-17
 *
 * Supports Function Codes:
 *  - FC03: Read Holding Registers
 *  - FC06: Write Single Register
 *
 * The slave core is transport-agnostic: the application provides a system-tick
 * source, a UART write function and per-register read/write callbacks. Received
 * bytes are fed in one-by-one from the UART RX interrupt; frame boundaries are
 * detected by an inter-frame idle gap (T3.5 approximation).
 */

#ifndef MODBUS_RTU_SLAVE_H
#define MODBUS_RTU_SLAVE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Modbus function codes serviced by this slave. */
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03U
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06U

/*
 * Additional standard function codes recognised by the frame parser so that
 * their request boundaries are detected by byte count. These are framed and
 * CRC-checked but not serviced: the slave answers them with an
 * ILLEGAL_FUNCTION exception. Add handlers in modbus_process_frame() to
 * support them.
 */
#define MODBUS_FC_READ_COILS                0x01U
#define MODBUS_FC_READ_DISCRETE_INPUTS      0x02U
#define MODBUS_FC_READ_INPUT_REGISTERS      0x04U
#define MODBUS_FC_WRITE_SINGLE_COIL         0x05U
#define MODBUS_FC_DIAGNOSTICS               0x08U
#define MODBUS_FC_GET_COMM_EVENT_COUNTER    0x0BU
#define MODBUS_FC_GET_COMM_EVENT_LOG        0x0CU
#define MODBUS_FC_WRITE_MULTIPLE_COILS      0x0FU
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10U
#define MODBUS_FC_REPORT_SERVER_ID          0x11U
#define MODBUS_FC_MASK_WRITE_REGISTER       0x16U
#define MODBUS_FC_RW_MULTIPLE_REGISTERS     0x17U

/* Modbus Exception Codes */
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION       0x01U
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS   0x02U
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE     0x03U
#define MODBUS_EXCEPTION_SLAVE_DEVICE_FAILURE   0x04U

/* Slave address space */
#define MODBUS_SLAVE_ID_MIN         1U      /* Lowest valid unicast address    */
#define MODBUS_SLAVE_ID_MAX         247U    /* Highest valid unicast address   */
#define MODBUS_BROADCAST_ADDR       0U      /* Broadcast address (writes only) */
#define MODBUS_DEFAULT_SLAVE_ID     23U

/* Protocol limits */
#define MODBUS_MAX_READ_REGISTERS   125U    /* FC03 max registers per request  */

/* Buffer / timing configuration */
#define MODBUS_BUFFER_SIZE          256U    /* RX/TX frame buffer size (bytes) */
#define MODBUS_TIMEOUT_MS           10U     /* Inter-frame idle gap (T3.5)     */

/**
 * @brief Frame-end (T3.5) detection method.
 *
 * Both methods drive the same internal state machine; only the trigger differs:
 *  - 1 (hardware): a UART receiver-timeout interrupt calls
 *    libmodbusrtu_modbus_rx_timeout_handler(). Baud-independent, sub-millisecond
 *    accurate. Requires UART RTO/IDLE support (e.g. STM32 USART).
 *  - 0 (software): libmodbusrtu_modbus_process() polls the millisecond tick and
 *    declares the frame complete after MODBUS_TIMEOUT_MS of idle. Fully portable,
 *    needs only a 1 ms tick source.
 */
#ifndef MODBUS_USE_HW_RTO
#define MODBUS_USE_HW_RTO           0
#endif

/**
 * @brief Holding-register reference base.
 *
 * Maps a wire (PDU) register address to a logical register reference:
 *   logical = MODBUS_HOLDING_REG_BASE + wire_addr
 *
 * The strict Modbus standard uses 40001 (reference 40001 == PDU address 0).
 * This project stores all holding-register references as 40000-based numbers
 * (see NVRAM / web configuration and the IEC104 mirror), so 40000 is used here
 * to keep a single consistent convention across the firmware. Change this macro
 * if the connected master maps holding registers differently.
 */
#define MODBUS_HOLDING_REG_BASE     40000U

/**
 * @brief Result of a register access callback.
 */
typedef enum
{
    MODBUS_REG_OK = 0,          /**< Access accepted.                            */
    MODBUS_REG_ERR_ADDRESS,     /**< Register address not mapped.                */
    MODBUS_REG_ERR_VALUE        /**< Value out of range / register not writable. */
} modbus_reg_status_t;

/**
 * @brief Read-register callback type (FC03).
 * @param[in]  reg_addr  Resolved logical register reference (e.g. 40030).
 * @param[out] p_value   Destination for the 16-bit register value.
 * @return MODBUS_REG_OK if the address is valid, MODBUS_REG_ERR_ADDRESS otherwise.
 */
typedef modbus_reg_status_t (*modbus_read_reg_callback_t)(uint16_t reg_addr, uint16_t *p_value);

/**
 * @brief Write-register callback type (FC06).
 * @param[in] reg_addr  Resolved logical register reference.
 * @param[in] value     Value to write.
 * @return MODBUS_REG_OK on success, MODBUS_REG_ERR_ADDRESS for an unmapped or
 *         read-only register, MODBUS_REG_ERR_VALUE for an out-of-range value.
 */
typedef modbus_reg_status_t (*modbus_write_reg_callback_t)(uint16_t reg_addr, uint16_t value);

/**
 * @brief Get system tick in milliseconds (platform-specific).
 * @return Current tick in milliseconds.
 */
typedef uint32_t (*modbus_get_tick_ms_t)(void);

/**
 * @brief UART write function (platform-specific).
 * @param[in] data Pointer to data buffer.
 * @param[in] len  Length of data to send.
 */
typedef void (*modbus_uart_write_t)(const uint8_t *data, uint16_t len);

/**
 * @brief Result of libmodbusrtu_modbus_process().
 */
typedef enum
{
    MODBUS_POLL_IDLE = 0,   /**< No frame in progress; nothing to do.        */
    MODBUS_POLL_RECEIVING,  /**< A frame is mid-reception; poll again soon.  */
    MODBUS_POLL_HANDLED     /**< A complete frame was processed this call.   */
} modbus_poll_result_t;

/**
 * @brief Modbus RTU slave instance (context).
 *
 * One instance per physical bus, so several independent slaves can run side by
 * side. Allocate it statically and pass its address to every API call; the
 * library keeps no global state. All fields are private - treat the struct as
 * opaque and use the API.
 */
typedef struct
{
    uint8_t slave_id;                           /**< This slave's address.   */

    /*
     * RX frame buffer shared between the UART RX interrupt (producer) and the
     * main-loop poll (consumer). The hand-off is serialised by the volatile
     * flags below: each side only touches the buffer while the other is idle.
     */
    uint8_t           rx_buffer[MODBUS_BUFFER_SIZE];
    volatile uint16_t rx_count;                 /**< Bytes received so far.  */
    volatile uint32_t last_rx_time;             /**< Tick of the last byte.  */
    volatile bool     frame_ready;              /**< Set by the HW RTO ISR.  */
    volatile bool     busy;                     /**< Poll owns the buffer.   */

    modbus_get_tick_ms_t        get_tick_ms;    /**< Millisecond tick hook.  */
    modbus_uart_write_t         uart_write;     /**< UART transmit hook.     */
    modbus_read_reg_callback_t  read_callback;  /**< FC03 read hook.         */
    modbus_write_reg_callback_t write_callback; /**< FC06 write hook.        */

    volatile uint8_t last_exception_code;       /**< Last exception sent (0=none). */
} modbus_slave_t;

/**
 * @brief Initialize a Modbus RTU slave instance.
 * @param[out] p_ctx             Instance to initialise.
 * @param[in]  slave_id          Modbus slave ID (1-247). Out-of-range values are
 *                               clamped to MODBUS_DEFAULT_SLAVE_ID.
 * @param[in]  get_tick_ms_func  Millisecond tick source.
 * @param[in]  uart_write_func   UART transmit function.
 */
void libmodbusrtu_slave_init(modbus_slave_t *p_ctx,
                             uint8_t slave_id,
                             modbus_get_tick_ms_t get_tick_ms_func,
                             modbus_uart_write_t uart_write_func);

/**
 * @brief Register the read-register callback (FC03).
 * @param[in] p_ctx    Slave instance.
 * @param[in] callback Per-register read function.
 */
void libmodbusrtu_register_read_callback(modbus_slave_t *p_ctx,
                                         modbus_read_reg_callback_t callback);

/**
 * @brief Register the write-register callback (FC06).
 * @param[in] p_ctx    Slave instance.
 * @param[in] callback Per-register write function.
 */
void libmodbusrtu_register_write_callback(modbus_slave_t *p_ctx,
                                          modbus_write_reg_callback_t callback);

/**
 * @brief Set the Modbus slave ID.
 * @param[in] p_ctx Slave instance.
 * @param[in] id    New slave ID (1-247). Out-of-range values are ignored.
 */
void libmodbusrtu_modbus_set_slave_id(modbus_slave_t *p_ctx, uint8_t id);

/**
 * @brief Get the most recent Modbus exception code sent by this slave.
 *
 * Records the standard exception code (e.g. MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS)
 * of the last error response. It is never cleared automatically, so it reflects
 * the last error that occurred since start-up.
 *
 * @param[in] p_ctx Slave instance.
 * @return The last exception code, or 0 if no exception has been raised.
 */
uint8_t libmodbusrtu_modbus_get_last_exception(const modbus_slave_t *p_ctx);

/**
 * @brief Store one received byte - call from the UART RX interrupt.
 *
 * Minimal by design: it only buffers the byte and timestamps it. All framing,
 * CRC checking and parsing happen later in libmodbusrtu_modbus_process().
 *
 * @param[in] p_ctx Slave instance.
 * @param[in] byte  Received byte.
 * @note ISR-safe.
 */
void libmodbusrtu_modbus_rx_byte(modbus_slave_t *p_ctx, uint8_t byte);

#if (MODBUS_USE_HW_RTO == 1)
/**
 * @brief Mark the current frame complete - call from the UART RX-timeout interrupt.
 *
 * The hardware RX timeout fires after the T3.5 idle gap, flagging the buffered
 * frame for the main loop. Only used when MODBUS_USE_HW_RTO is 1.
 *
 * @param[in] p_ctx Slave instance.
 * @note ISR-safe.
 */
void libmodbusrtu_modbus_rx_timeout(modbus_slave_t *p_ctx);
#endif

/**
 * @brief Detect, validate and process a received frame - call from the main loop.
 *
 * A complete frame is recognised either by its length (known function codes) or
 * by the inter-frame idle gap (software mode) / RX-timeout flag (hardware mode).
 *
 * @param[in] p_ctx Slave instance.
 * @return MODBUS_POLL_HANDLED   a full frame was processed this call;
 *         MODBUS_POLL_RECEIVING a frame is still arriving - poll again shortly;
 *         MODBUS_POLL_IDLE      the bus is idle - nothing to do.
 */
modbus_poll_result_t libmodbusrtu_modbus_process(modbus_slave_t *p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_SLAVE_H */
