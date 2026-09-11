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

/* GI (genel sorgulama) tamamlandiginda cagirilir: baglanti aciksa ve
 * gonderilmemis olay gunlugu kaydi varsa replay surecini baslatir.
 * Kayit yoksa hicbir sey yapmaz - unsent sayacinin kendisi durumdur,
 * ayri oturum bayragi tasimaz. */
void iec104_replay_start_if_pending(void);

/* RF uretici giris noktasi (TRIP_NOTIFY isleyicisi icin): bir ariza
 * olayini 15+15 listelerine ve 3 aylik olay gunlugune yazar; hat aciksa
 * olay aninda spontane gonderir, degilse kayit replay'i bekler.
 * Parametre anlami fault_log_add() ile aynidir. */
void iec104_report_fault_event(float fault_current, uint16_t fault_duration_ms,
                               uint8_t nominal_current_status, uint8_t power_status,
                               uint8_t type, uint8_t feeder_id, uint8_t phase_id);

#endif /* IEC104_REPLAY_H_ */
