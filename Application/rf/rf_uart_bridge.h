/*
 * rf_uart_bridge.h
 *
 *  Created on: Sep 05, 2026
 *      Author: fatih
 *
 * USART3 (RF modul tarafi) ile LPUART1 (konsol tarafi) arasinda saydam
 * (transparent) bayt koprusu. Kopru aktifken bir portta gelen her bayt
 * degistirilmeden diger porta yazilir; LPUART1 shell/XMODEM isleme ve
 * USART3 RF protokol isleme bypas edilir.
 *
 * Kopru tek yonlu acilir: her reset sonrasi kapali (OFF) baslar, sadece
 * "rf-bridge" shell komutuyla acilir. Kapatma yolu yoktur; reset, hem
 * kopruyi hem kayitli log ayarini geri getirir.
 */

#ifndef RF_RF_UART_BRIDGE_H_
#define RF_RF_UART_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief "rf-bridge" shell komutunu kaydet.
 *
 * Kopru kapali baslar; komut konsolda kullanilabilir olsun diye uygulama
 * acilisinda (app_main) bir kez cagirilir.
 */
void rf_uart_bridge_init(void);

/**
 * @brief Koprunun su anda aktif olup olmadigini bildir.
 *
 * @return true ise baytlar USART3 <-> LPUART1 arasinda tasiniyor.
 *
 * @note ISR baglami dahil guvenli: acquire yuklu atomik okuma.
 */
bool rf_uart_bridge_is_enabled(void);

/**
 * @brief Kopruyu calisma zamaninda ac.
 *
 * @note Tasarim geregi tek yonlu: kapatma yolu yok. Reset varsayilan
 *       (kapali) durumu geri getirir.
 */
void rf_uart_bridge_enable(void);

/**
 * @brief LPUART1'de gelen bir bayti USART3'e ilet.
 *
 * @param[in] data USART3 uzerinden gonderilecek bayt.
 *
 * @note Bloklayici: USART3 TX yazmaci yer acana kadar busy-wait.
 *       LPUART1_IRQHandler'ten cagirilmak uzere tasarlandi.
 */
void rf_uart_bridge_lpuart1_to_usart3(uint8_t data);

/**
 * @brief USART3'te gelen bir bayti LPUART1'e ilet.
 *
 * @param[in] data LPUART1 uzerinden gonderilecek bayt.
 *
 * @note Bloklayici: LPUART1 TX yazmaci yer acana kadar busy-wait.
 *       USART3_IRQHandler'ten cagirilmak uzere tasarlandi.
 */
void rf_uart_bridge_usart3_to_lpuart1(uint8_t data);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_UART_BRIDGE_H_ */
