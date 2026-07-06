/*
 * iec104_application.c
 *
 *  Created on: Mar 13, 2026
 *      Author: fatih
 */
#include "iec104_application.h"
#include "iec104.h"
#include "nvram.h"
#include "bsp.h"
#include "contiki.h"
#include "contiki_process.h"
#include "gsm_engine.h"
#include "breaker.h"
#include "fault_log.h"
#include "gsm_listener_process.h"


static uint8_t tx_buff[1024];
static uint16_t tx_len = 0;

void iec104_application_event_handler(iec104_app_event_t evt)
{
	if(evt == IEC104_APP_EVT_SEND_TEMP_FAULTS)
	{
		process_start(&iec104_send_temporary_faults, NULL);
	}
	else if(evt == IEC104_APP_EVT_SEND_PERM_FAULTS)
	{
		process_start(&iec104_send_permanent_faults, NULL);
	}
	else if(evt == IEC104_APP_EVT_REQUEST_SOCKET_CLOSE)
	{
		gsm_listener_socket_event_handler(GSM_LISTENER_IEC104, GSM_USER_EVENT_CLOSE_SOCKET);
	}
	else if(evt == IEC104_APP_EVT_SOCKET_CLOSED)
	{
		if(process_is_running(&iec104_send_temporary_faults)){
			process_exit(&iec104_send_temporary_faults);
		}
		if(process_is_running(&iec104_send_permanent_faults)){
			process_exit(&iec104_send_permanent_faults);
		}
	}
}


PROCESS(iec104_send_temporary_faults, "iec104_send_temporary_faults");
PROCESS_THREAD(iec104_send_temporary_faults, ev, data)
{
	static struct etimer timer;
	static uint8_t feeder_id;
	static phase_id_t phase_id;
	static uint16_t tx_sent_index;


	PROCESS_EXITHANDLER({
		iec104_send_general_interrogation_term(QOI_GROUP_3);
		CSLOG("Finished sending temporary fault data for all feeders and phases. Exiting process.\r\n");
		CSLOG("%s EXIT \r\n", PROCESS_CURRENT()->name);
	});

	PROCESS_BEGIN();

	CSLOG("PROCESS %s START \r\n", PROCESS_CURRENT()->name);
	etimer_set(&timer, 100);
	feeder_id = 0;	
	
	while (process_is_running(&iec104_send_permanent_faults))
	{
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        etimer_reset(&timer);
	}
	
	CSLOG("Starting to send temporary fault data for feeders and phases...\r\n");
    iec104_send_general_interrogation_con(QOI_GROUP_3, 0);

    for(feeder_id = 0; feeder_id < MAX_POWER_LINE_COUNT; feeder_id++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(feeder_id);
        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for(phase_id = PHASE_L1; phase_id < PHASE_MAX; phase_id++)
        {
            tx_len = 0;
            iec104_get_feeder_temporary_faults(tx_buff, &tx_len, sizeof(tx_buff),
                                               feeder_id, phase_id,
                                               COT_INTERROGATED_GROUP3);
            tx_sent_index = 0;

            while(tx_len > 0)
            {
                uint16_t len_to_send = tx_len - tx_sent_index;
                if(len_to_send > 1024) {
                	len_to_send = 1024;
                }

                if (gsm_get_tx_state() == GSM_TX_READY &&
                    gsm_send_to_socket(&tx_buff[tx_sent_index], len_to_send,
                                       GSM_TX_DIR_IEC104_SOCKET, false, false) == 0)
                {
                    tx_len       -= len_to_send;
                    tx_sent_index += len_to_send;
                }
                else
                {
                    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
                    etimer_reset(&timer);
                }
            }
        }
    }

	iec104_send_general_interrogation_term(QOI_GROUP_3);
	PROCESS_END();
}

PROCESS(iec104_send_permanent_faults, "iec104_send_permanent_faults");
PROCESS_THREAD(iec104_send_permanent_faults, ev, data)
{
	static struct etimer timer;
	static uint8_t feeder_id;
	static phase_id_t phase_id;
	static uint16_t tx_sent_index;


	PROCESS_EXITHANDLER({
		iec104_send_general_interrogation_term(QOI_GROUP_4);
		CSLOG("Finished sending permanent fault data for all feeders and phases. Exiting process.\r\n");
		CSLOG("%s EXIT \r\n", PROCESS_CURRENT()->name);
	});

	PROCESS_BEGIN();

	CSLOG("PROCESS %s START \r\n", PROCESS_CURRENT()->name);
	etimer_set(&timer, 100);
	feeder_id = 0;	

	while(process_is_running(&iec104_send_temporary_faults))
	{
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        etimer_reset(&timer);
	}

	CSLOG("Starting to send permanent fault data for feeders and phases...\r\n");

    iec104_send_general_interrogation_con(QOI_GROUP_4, 0);

    for(feeder_id = 0; feeder_id < MAX_POWER_LINE_COUNT; feeder_id++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(feeder_id);
        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for(phase_id = PHASE_L1; phase_id < PHASE_MAX; phase_id++)
        {
            tx_len = 0;
            iec104_get_feeder_permanent_faults(tx_buff, &tx_len, sizeof(tx_buff),
                                               feeder_id, phase_id,
                                               COT_INTERROGATED_GROUP4);
            tx_sent_index = 0;

            while(tx_len > 0)
            {
                uint16_t len_to_send = tx_len - tx_sent_index;
                if(len_to_send > 1024) {
                	len_to_send = 1024;
                }

                if (gsm_get_tx_state() == GSM_TX_READY &&
                    gsm_send_to_socket(&tx_buff[tx_sent_index], len_to_send,
                                       GSM_TX_DIR_IEC104_SOCKET, false, false) == 0)
                {
                    tx_len       -= len_to_send;
                    tx_sent_index += len_to_send;
                }
                else
                {
                    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
                    etimer_reset(&timer);
                }
            }
        }
    }

	iec104_send_general_interrogation_term(QOI_GROUP_4);
	PROCESS_END();
}


void iec104_application_init(void)
{

}
