/*
 * iec104_process.c
 *
 *  Created on: Oct 26, 2025
 *      Author: fatih
 */
#include "iec104_process.h"
#include "iec104.h"
#include "nvram.h"
#include "bsp.h"
#include "contiki.h"
#include "contiki_process.h"
#include "gsm_engine.h"
#include "breaker.h"
#include "iec104_application.h"

static iec104_config_t iec104_config = {0};
static struct timer periodic_send_timer;

/* Slot-based TX buffer: each slot = one TCP segment */
#define IEC104_TX_SLOT_SIZE    1024  /* MTU(1500) - IP/TCP headers(40) */
#define IEC104_TX_SLOT_COUNT   32

typedef struct {
    struct {
        uint8_t  data[IEC104_TX_SLOT_SIZE];
        uint16_t len;
    } slot[IEC104_TX_SLOT_COUNT];

    uint8_t  write_idx;     /* iec104_send() writes here */
    uint8_t  read_idx;      /* iec104_tx_process() reads here */
    uint16_t pending;       /* total bytes waiting across all slots */
} iec104_tx_buffer_t;

static iec104_tx_buffer_t tx = {0};

void tick_timer()
{
	iec104_tick();
}


static void tx_reset(void)
{
	for (uint8_t slot = 0; slot < IEC104_TX_SLOT_COUNT; slot++) {
		tx.slot[slot].len = 0;
	}

	tx.write_idx = 0;
	tx.read_idx = 0;
	tx.pending = 0;
}

void iec104_process_socket_closed_cb(void)
{
	CCSLOG(XCOLOR_RED, "IEC104 Socket Closed by Remote\r\n");
	iec104_application_event_handler(IEC104_APP_EVT_SOCKET_CLOSED);
	tx_reset();
}

static void generate_dummy_test_data(void)
{
	for(uint32_t line_idx = 0; line_idx < MAX_POWER_LINE_COUNT; line_idx++)
	{
		for(uint8_t phase = 0; phase < PHASE_MAX; phase++)
		{
			feeder_data_t dummy_fedeer;
			dummy_fedeer.phase[phase].ariza_akimi = 0.5 + line_idx*10 + phase;
			dummy_fedeer.phase[phase].ariza_suresi = 0.5 + line_idx*10 + phase;
			dummy_fedeer.phase[phase].anlik_akim = 0.5 + line_idx*10 + phase;
			dummy_fedeer.phase[phase].ariza_kalicimi = 1;
			dummy_fedeer.phase[phase].enerji_varyok = 0;
			dummy_fedeer.phase[phase].nominal_akim_varyok = 1;
			dummy_fedeer.phase[phase].rf_haberlesme_varyok = 0;

			// Zaman damgaları için geçerli bir tarih oluştur (örneğin, 2026-03-11 12:23:47)
			cp56time2a_t timestamp = cp56time2a_make(0, 23, line_idx, phase, 4, 3, 26);
			dummy_fedeer.phase[phase].tm_ariza_akimi = timestamp;
			dummy_fedeer.phase[phase].tm_ariza_suresi = timestamp;
			dummy_fedeer.phase[phase].tm_anlik_akim = timestamp;
			dummy_fedeer.phase[phase].tm_ariza_kalicimi = timestamp;
			dummy_fedeer.phase[phase].tm_enerji_varyok = timestamp;
			dummy_fedeer.phase[phase].tm_nominal_akim_varyok = timestamp;
			dummy_fedeer.phase[phase].tm_rf_haberlesme_varyok = timestamp;

			breaker_set_feeder_data(line_idx, &dummy_fedeer);
		}
	}
}


static int iec104_send(const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > IEC104_TX_SLOT_SIZE) {
        CCSLOG(XCOLOR_RED, "IEC104 TX: invalid len %d\r\n", len);
        return -1;
    }

    CSLOG("[RTU -> SCADA][%d]: [", len);
    for (size_t i = 0; i < len; i++) {
        CSLOG_NODT("%02X ", data[i]);
    }
    CSLOG_NODT("]\r\n");

    /* Current slot has room? */
    if (tx.slot[tx.write_idx].len + len <= IEC104_TX_SLOT_SIZE) {
        memcpy(&tx.slot[tx.write_idx].data[tx.slot[tx.write_idx].len], data, len);
        tx.slot[tx.write_idx].len += len;
        tx.pending += len;
        return 0;
    }

    /* Current slot full - advance to next slot */
    uint8_t next = (tx.write_idx + 1) % IEC104_TX_SLOT_COUNT;

    if (tx.slot[next].len > 0) {
        CCSLOG(XCOLOR_RED, "IEC104 TX: all slots full! write=%d read=%d pending=%d\r\n",
               tx.write_idx, tx.read_idx, tx.pending);
        return -1;
    }

    tx.write_idx = next;
    memcpy(tx.slot[tx.write_idx].data, data, len);
    tx.slot[tx.write_idx].len = len;
    tx.pending += len;

    return 0;
}

void iec104_process_tx_buffer_dump(void)
{
    CSLOG("[TX BUFFER] pending=%d write=%d read=%d\r\n", tx.pending, tx.write_idx, tx.read_idx);
    for (uint8_t s = 0; s < IEC104_TX_SLOT_COUNT; s++) {
        CSLOG("  [SLOT %d][%d/%d]: [", s, tx.slot[s].len, IEC104_TX_SLOT_SIZE);
        for (uint16_t i = 0; i < tx.slot[s].len; i++) {
            CSLOG_NODT("%02X ", tx.slot[s].data[i]);
        }
        CSLOG_NODT("]\r\n");
    }
}

static void iec104_tx_process(void)
{
    if (tx.pending == 0) {
        return;
    }
    if (gsm_get_tx_state() != GSM_TX_READY) {
        return;
    }

    uint16_t send_len = tx.slot[tx.read_idx].len;
    if (send_len == 0 || send_len > IEC104_TX_SLOT_SIZE) {
        tx.slot[tx.read_idx].len = 0; /* discard corrupt slot */
        return;
    }

    if (gsm_send_to_socket(tx.slot[tx.read_idx].data, send_len, GSM_TX_DIR_IEC104_SOCKET, false, false) == 0)
    {
        CSLOG("[TX SLOT %d SENT][%d] pending=%d\r\n", tx.read_idx, send_len, tx.pending - send_len);

        tx.slot[tx.read_idx].len = 0;
        if (tx.pending >= send_len) {
            tx.pending -= send_len;
        } else {
            tx.pending = 0; /* recover from corrupt counter */
        }

        if (tx.read_idx != tx.write_idx) {
            tx.read_idx = (tx.read_idx + 1) % IEC104_TX_SLOT_COUNT;
        }
    }
}

void iec104_on_data(const uint8_t *data, uint16_t len, void *user)
{
    //printf("Data Received (%d byte): %.*s\n", len, len, data);
    iec104_data_received(data, len);
}

static int handle_c_sc_na_1(ioa_3byte_t ioa, uint8_t state)
{
	CSLOG("C_SC_NA_1: IOA=%u, State=%u\r\n", iec104_ioa_3byte_to_uint32(ioa), state);
    return 0;
}

static bool iec104_periodic_send(void)
{
	if(gsm_get_tx_state() != GSM_TX_READY){
		return false;
	}
// TODO: Implement periodic send functionality
//	const char *test_msg = "This is a test message for IEC 104 periodic send.";
//	const size_t test_msg_len = strlen(test_msg);
//
//	return gsm_send_to_socket(test_msg, test_msg_len, GSM_TX_DIR_DIALER_SOCKET, false, false) == 0;

	return true;
}

static int read_ariza_akimi(uint32_t line_index, uint8_t phase, float *value, qds_t *quality, cp56time2a_t *timestamp)
{
	*value = 0;
	*quality = (qds_t){0};
	*timestamp = (cp56time2a_t){0};

	if(phase >= PHASE_MAX){
		return -1;
	}

	const feeder_data_t *feeder = breaker_get_feeder_data(line_index);
	if(feeder == NULL){
		return -1;
	}

	*value = feeder->phase[phase].ariza_akimi;
	*timestamp = feeder->phase[phase].tm_ariza_akimi;
	*quality = (qds_t){0}; //TODO: Quality bilgisini ekle
	return 0;
}

static int read_ariza_suresi(uint32_t line_index, uint8_t phase, float *value, qds_t *quality, cp56time2a_t *timestamp)
{
	*value = 0;
	*quality = (qds_t){0};
	*timestamp = (cp56time2a_t){0};

	if(phase >= PHASE_MAX){
		return -1;
	}

	const feeder_data_t *feeder = breaker_get_feeder_data(line_index);
	if(feeder == NULL){
		return -1;
	}

	*value = feeder->phase[phase].ariza_suresi;
	*timestamp = feeder->phase[phase].tm_ariza_suresi;
	*quality = (qds_t){0}; //TODO: Quality bilgisini ekle
	return 0;
}
 

static int read_anlik_akim(uint32_t line_index, uint8_t phase, float *value, qds_t *quality, cp56time2a_t *timestamp)
{
	*value = 0;
	*quality = (qds_t){0};
	*timestamp = (cp56time2a_t){0};

	if(phase >= PHASE_MAX){
		return -1;
	}

	const feeder_data_t *feeder = breaker_get_feeder_data(line_index);
	if(feeder == NULL){
		return -1;
	}

	*value = feeder->phase[phase].anlik_akim;
	*timestamp = feeder->phase[phase].tm_anlik_akim;
	*quality = (qds_t){0}; //TODO: Quality bilgisini ekle
	return 0;
}

static int read_ariza_kalicimi(uint32_t line_index, uint8_t phase, siq_t *value, cp56time2a_t *timestamp)
{
	*value = (siq_t){.invalid = 1};
	*timestamp = (cp56time2a_t){0};

	if(phase >= PHASE_MAX){
		return -1;
	}

	const feeder_data_t *feeder = breaker_get_feeder_data(line_index);
	if(feeder == NULL){
		return -1;
	}

	value->spi = feeder->phase[phase].ariza_kalicimi;
	value->invalid = 0; //TODO: Quality bilgisini ekle
	*timestamp = feeder->phase[phase].tm_ariza_kalicimi;
	return 0;
}

static int read_enerji_varyok(uint32_t line_index, uint8_t phase, siq_t *value, cp56time2a_t *timestamp)
{
	*value = (siq_t){.invalid = 1};
	*timestamp = (cp56time2a_t){0};

	if(phase >= PHASE_MAX){
		return -1;
	}

	const feeder_data_t *feeder = breaker_get_feeder_data(line_index);
	if(feeder == NULL){
		return -1;
	}

	value->spi = feeder->phase[phase].enerji_varyok;
	value->invalid = 0; //TODO: Quality bilgisini ekle
	*timestamp = feeder->phase[phase].tm_enerji_varyok;
	
	return 0;
}

static int read_nominal_akim_varyok(uint32_t line_index, uint8_t phase, siq_t *value, cp56time2a_t *timestamp)
{
	*value = (siq_t){.invalid = 1};
	*timestamp = (cp56time2a_t){0};	

	if(phase >= PHASE_MAX){

		return -1;
	}

	const feeder_data_t *feeder = breaker_get_feeder_data(line_index);
	if(feeder == NULL){
		return -1;
	}

	value->spi = feeder->phase[phase].nominal_akim_varyok;
	value->invalid = 0; //TODO: Quality bilgisini ekle
	*timestamp = feeder->phase[phase].tm_nominal_akim_varyok;
	return 0;
}

static int read_rf_haberlesme_varyok(uint32_t line_index, uint8_t phase, siq_t *value, cp56time2a_t *timestamp)
{
	*value = (siq_t){.invalid = 1};
	*timestamp = (cp56time2a_t){0};	

	if(phase >= PHASE_MAX){
		return -1;
	}

	const feeder_data_t *feeder = breaker_get_feeder_data(line_index);
	if(feeder == NULL){
		return -1;
	}

	value->spi = feeder->phase[phase].rf_haberlesme_varyok;
	value->invalid = 0; //TODO: Quality bilgisini ekle
	*timestamp = feeder->phase[phase].tm_rf_haberlesme_varyok;
	return 0;
}
 

PROCESS(iec104_process, "iec104_process");
PROCESS_THREAD(iec104_process, ev, data)
{
	static struct etimer timer;
	static struct timer  iec104_tick_timer;
	PROCESS_BEGIN();

	  iec104_init(&(iec104_io_t){
	      .send = iec104_send,
	    .get_ariza_akimi = read_ariza_akimi,
	    .get_ariza_suresi = read_ariza_suresi,
	    .get_anlik_akim = read_anlik_akim,
	    .get_ariza_kalicimi = read_ariza_kalicimi,
	    .get_enerji_varyok = read_enerji_varyok,
	    .get_nominal_akim_varyok = read_nominal_akim_varyok,
	    .get_rf_haberlesme_varyok = read_rf_haberlesme_varyok
	  },
	  &iec104_config);

	timer_set(&periodic_send_timer, iec104_config.periodical_send_interval*1000);
	etimer_set(&timer, 100);
	timer_set(&iec104_tick_timer, 1000);
	while (1)
	{
 		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
 		etimer_restart(&timer);

 		if(timer_expired(&iec104_tick_timer))
 		{
 			tick_timer();
 			timer_reset(&iec104_tick_timer);
 		}

 		libiec104_poll();
 		if(timer_expired(&periodic_send_timer))
 		{
 			if(iec104_periodic_send()){
				timer_reset(&periodic_send_timer);
 			}
 		}


 		iec104_tx_process();

	}

	PROCESS_END();
}


void iec104_process_init(void)
{
	iec104_config = *nvram_get_iec104_config();
	generate_dummy_test_data();
	iec104_application_init();

	process_start(&iec104_process, NULL);
}


