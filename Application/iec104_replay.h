/*
 * iec104_replay.h
 *
 *  Created on: 11 Eyl 2026
 *      Author: fatih
 */

#ifndef IEC104_REPLAY_H_
#define IEC104_REPLAY_H_

#include <stdint.h>
#include <stdbool.h>

/* STARTDT bildirimi: 15 s'lik emniyet sayacini baslatir. Sure dolunca
 * IKM hic sorgulama yapmamis olsa bile replay kendiliginden kalkar
 * (sertname 2.2.4.2: donen baglantida veri otomatik gonderilir).
 * Bu, replay'in tek tetik yoludur. */
void iec104_replay_link_established(void);

/* RF uretici giris noktasi (TRIP_NOTIFY isleyicisi icin): bir ariza
 * olayini 15+15 listelerine ve 3 aylik olay gunlugune yazar; hat aciksa
 * olay aninda spontane gonderir, degilse kayit replay'i bekler.
 * Parametre anlami fault_log_add() ile aynidir. */
void iec104_report_fault_event(float fault_current, uint16_t fault_duration_ms,
                               uint8_t nominal_current_status, uint8_t power_status,
                               uint8_t type, uint8_t feeder_id, uint8_t phase_id);

#endif /* IEC104_REPLAY_H_ */
