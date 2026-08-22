/*
 * serial_win.h
 *
 *  Created on: Aug 22, 2026
 *      Author: fatih
 *
 * Win32 seri port erisimi (RF HUB simulatoru tasiyici katmani).
 */

#ifndef HUB_SIM_SERIAL_H_
#define HUB_SIM_SERIAL_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief COM portunu ac ve 8N1 olarak yapilandir.
 *  @return 0 basarili; -1 hata (aciklama stderr'e basilir).
 */
int serial_open(const char *port, uint32_t baud);

/** @brief Portu kapat. */
void serial_close(void);

/** @brief Okuma (50 ms bekleme). @return Okunan bayt sayisi; -1 hata. */
int serial_read(uint8_t *buf, size_t max_len);

/** @brief Yazma. @return Yazilan bayt sayisi; -1 hata. */
int serial_write(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HUB_SIM_SERIAL_H_ */
