/*
 * rf_shell.h
 *
 *  Created on: Aug 27, 2026
 *      Author: fatih
 *
 * RF hub shell komutlari: konsoldan RF islemlerini tetikleme.
 *
 * Komutlar:
 *   rf disc    - Kesif kuyruguna sanal cihaz ekle (test icin)
 *   rf status  - Hub ve link durumu ozeti
 *   rf inv     - Envanteri yeniden push et
 */

#ifndef RF_RF_SHELL_H_
#define RF_RF_SHELL_H_

#ifdef __cplusplus
extern "C" {
#endif

/** Shell komutlarini kaydet. app_main'de cagirilir. */
void rf_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_SHELL_H_ */
