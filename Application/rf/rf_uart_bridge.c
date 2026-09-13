/*
 * rf_uart_bridge.c
 *
 *  Created on: Sep 05, 2026
 *      Author: fatih
 *
 * USART3 (RF modul tarafi) ile LPUART1 (konsol tarafi) arasinda saydam
 * (transparent) bayt koprusu. Amac: RF modulunu PC'den dogrudan
 * yapilandirabilmek.
 */

#include "rf_uart_bridge.h"

#include "shell.h"
#include "console_logger.h"
#include "main.h"
#include "stm32u3xx_ll_usart.h"
#include "stm32u3xx_ll_lpuart.h"

#include <stdatomic.h>
#include <string.h>

/* ISR paylasimli bayrak: shell baglaminda release ile yazilir (publish),
 * USART3 / LPUART1 kesme baglaminda acquire ile okunur. volatile tek
 * basina senkronizasyon ilkel degil (cortex-m-atomic-isr politikasi).
 * Statik atomikler sifir ile baslar: kopru her reset sonrasi kapali. */
static atomic_bool bridge_enabled;

_Static_assert(__atomic_always_lock_free(sizeof(bridge_enabled), 0),
               "bridge_enabled must be lock-free for ISR access");

/**
 * @brief "rf-bridge" shell komut isleyicisi.
 *
 * Sadece "on" alt komutu vardir; durum sorgusu varsayilan ciktidir.
 */
static int rf_uart_bridge_shell_handler(int argc, char *argv[])
{
    if ((argc >= 2) && (0 == strcmp(argv[1], "on")))
    {
        /* Onayi once yaz: rf_uart_bridge_enable() LPUART1'i saydam veri
         * kanalina cevirir ve konsol log cikisini susturur. */
        SHELL_LOG("[RF-BRIDGE] acildi: USART3 <-> LPUART1 "
                  "saydam (transparent) mod\r\n");
        rf_uart_bridge_enable();
        return 0;
    }

    SHELL_LOG("[RF-BRIDGE] durum: %s\r\n",
              rf_uart_bridge_is_enabled() ? "ON" : "OFF");
    SHELL_LOG("Kullanim: rf-bridge on  - saydam kopruyu ac "
              "(kapatmak icin reset)\r\n");
    return 0;
}

void rf_uart_bridge_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd   = "rf-bridge",
        .desc  = "USART3<->LPUART1 saydam kopru\r\n"
                 "\trf-bridge      - kopru durumunu goster\r\n"
                 "\trf-bridge on   - saydam kopruyu ac (kapatmak icin reset)",
        .level = SHELL_LVL_USER,
        .func  = rf_uart_bridge_shell_handler
    });
}

bool rf_uart_bridge_is_enabled(void)
{
    return atomic_load_explicit(&bridge_enabled, memory_order_acquire);
}

void rf_uart_bridge_enable(void)
{
    /* LPUART1 artik saydam RF trafigi tasiyor: arka plan log cikisi
     * byte akisini bozmasin diye konsol logini sustur. Kalici degil:
     * reset, kayitli log ayarini varsayilan (kapali kopru) ile geri
     * getirir. */
    console_logger_set_enabled(false, false);

    atomic_store_explicit(&bridge_enabled, true, memory_order_release);
}

void rf_uart_bridge_lpuart1_to_usart3(uint8_t data)
{
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(USART3))
    {
        /* USART3 TX yazmacinda yer acmasini bekle (busy-wait). */
    }
    LL_USART_TransmitData8(USART3, data);
}

void rf_uart_bridge_usart3_to_lpuart1(uint8_t data)
{
    while (!LL_LPUART_IsActiveFlag_TXE_TXFNF(LPUART1))
    {
        /* LPUART1 TX yazmacinda yer acmasini bekle (busy-wait). */
    }
    LL_LPUART_TransmitData8(LPUART1, data);
}

/*** end of file ***/
