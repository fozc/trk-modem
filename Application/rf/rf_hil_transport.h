/*
 * rf_hil_transport.h
 *
 *  Created on:  6 Oct 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Hardware-in-the-loop (HIL) transport switch for the RF hub link.
 *
 * 0 (production): SCP runs on USART3 (PC4/PC5, 230400 8N1) toward the
 *    real Modem RF Hub; the Modbus RTU slave runs on UART4/RS-485.
 *
 * 1 (HIL bench build): SCP is rerouted to UART4 so a PC simulator on
 *    the Modbus RS-485 connector can play the MH role with the existing
 *    bench cabling (USB-RS-485 adapter). The USART3 RF RX dispatch is
 *    compiled out (an attached real hub is fully ignored) and the
 *    Modbus slave is not started. TX drives the RS-485 bus through the
 *    MODBUS_OE (PA15) driver-enable pin via uart_send_buffer_rs485().
 *    Half-duplex echo is harmless: libscp drops frames by DST address
 *    before CRC, so the transmitter never accepts its own frames.
 *
 * Flash a 0 build before production use; a 1 build is a bench image.
 */

#ifndef RF_RF_HIL_TRANSPORT_H_
#define RF_RF_HIL_TRANSPORT_H_

#ifndef RF_SCP_OVER_MODBUS_PORT
#define RF_SCP_OVER_MODBUS_PORT 0
#endif

#endif /* RF_RF_HIL_TRANSPORT_H_ */

/*** end of file ***/
