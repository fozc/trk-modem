#define CSLOG_MODULE LOG_MOD_IEC104
#include "iec104_application.h"
#include "iec104.h"
#include "nvram.h"
#include "bsp.h"
#include "contiki.h"
#include "contiki_process.h"
#include "breaker.h"
#include "fault_log.h"
#include "gsm_listener_process.h"
#include "reboot.h"
#include "sys/pt-sem.h"

static struct pt_sem g_tx_sem;

/* C_RP_NA_1 reset zinciri: once ACT_CON hatta ciksin, sonra soket kapansin. */
#define IEC104_REBOOT_ACK_FLUSH_MS      3000U
#define IEC104_REBOOT_SOCKET_CLOSE_MS   2000U

void iec104_application_init(void)
{
    /* Semaforu 1 (Binary Mutex olarak) ilklendiriyoruz */
    PT_SEM_INIT(&g_tx_sem, 1);
}

void iec104_application_event_handler(iec104_event_t evt)
{
    if(evt == IEC104_EVT_SEND_TEMP_FAULTS)
    {
        if (!process_is_running(&iec104_send_temporary_faults)) {
            process_start(&iec104_send_temporary_faults, NULL);
        }
    }
    else if(evt == IEC104_EVT_SEND_PERM_FAULTS)
    {
        if (!process_is_running(&iec104_send_permanent_faults)) {
            process_start(&iec104_send_permanent_faults, NULL);
        }
    }
    else if(evt == IEC104_EVT_REBOOT_REQUESTED)
    {
        /* C_RP_NA_1 (reset process, QRP=1) geldi: ACT_CON onayi TX
         * kuyruguna yazildi. Onay hatta ciksin, sonra soket kapansin,
         * en son fiziksel reset -- master t1 zaman asimi yerine temiz
         * kapanma gorur. */
        if (!process_is_running(&iec104_reboot_process)) {
            process_start(&iec104_reboot_process, NULL);
        }
    }
    else if(evt == IEC104_EVT_REQUEST_SOCKET_CLOSE)
    {
        gsm_listener_socket_event_handler(GSM_LISTENER_IEC104, GSM_USER_EVENT_CLOSE_SOCKET);
    }
    else if(evt == IEC104_EVT_SOCKET_CLOSED)
    {
        if(process_is_running(&iec104_send_temporary_faults)){
            process_exit(&iec104_send_temporary_faults);
        }
        if(process_is_running(&iec104_send_permanent_faults)){
            process_exit(&iec104_send_permanent_faults);
        }

        PT_SEM_INIT(&g_tx_sem, 1);
    }
}


PROCESS(iec104_reboot_process, "iec104_reboot_process");
PROCESS_THREAD(iec104_reboot_process, ev, data)
{
    static struct etimer timer;

    PROCESS_BEGIN();

    etimer_set(&timer, IEC104_REBOOT_ACK_FLUSH_MS);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

    gsm_listener_socket_event_handler(GSM_LISTENER_IEC104, GSM_USER_EVENT_CLOSE_SOCKET);

    etimer_set(&timer, IEC104_REBOOT_SOCKET_CLOSE_MS);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

    reboot_system();

    PROCESS_END();
}


PROCESS(iec104_send_temporary_faults, "iec104_send_temporary_faults");
PROCESS_THREAD(iec104_send_temporary_faults, ev, data)
{
    static struct etimer timer;
    static uint8_t feeder_id;
    static phase_id_t phase_id;
    static iec104_fault_emit_state_t emit_state;
    static bool link_lost;

    PROCESS_EXITHANDLER({
        iec104_send_general_interrogation_term(QOI_GROUP_3, link_lost ? 1U : 0U);
        CSLOG("Finished sending temporary fault data for all feeders and phases. Exiting process.\r\n");
        CSLOG("%s EXIT \r\n", PROCESS_CURRENT()->name);

    });

    PROCESS_BEGIN();

    CSLOG("PROCESS %s START \r\n", PROCESS_CURRENT()->name);
    etimer_set(&timer, 100);
    feeder_id = 0;
    link_lost = false;

    PT_SEM_WAIT(&iec104_send_temporary_faults.pt, &g_tx_sem);

    CSLOG("Starting to send temporary fault data for feeders and phases...\r\n");
    iec104_send_general_interrogation_con(QOI_GROUP_3, 0);

    for(feeder_id = 0; (feeder_id < MAX_POWER_LINE_COUNT) && !link_lost; feeder_id++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(feeder_id);
        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for(phase_id = PHASE_L1; (phase_id < PHASE_MAX) && !link_lost; phase_id++)
        {
            emit_state = (iec104_fault_emit_state_t){0};

            while(!iec104_emit_feeder_temporary_faults(feeder_id, phase_id,
                                                      COT_INTERROGATED_GROUP3,
                                                      &emit_state))
            {
                /* Hat dustuyse yayim asla tamamlanamaz; semafor birakilabilsin
                 * diye donguden temiz cikilir. */
                if(!iec104_is_link_active())
                {
                    link_lost = true;
                    break;
                }

                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
                etimer_reset(&timer);
            }
        }
    }

    iec104_send_general_interrogation_term(QOI_GROUP_3, link_lost ? 1U : 0U);

    PT_SEM_SIGNAL(&process_pt, &g_tx_sem);

    PROCESS_END();
}

PROCESS(iec104_send_permanent_faults, "iec104_send_permanent_faults");
PROCESS_THREAD(iec104_send_permanent_faults, ev, data)
{
    static struct etimer timer;
    static uint8_t feeder_id;
    static phase_id_t phase_id;
    static iec104_fault_emit_state_t emit_state;
    static bool link_lost;

    PROCESS_EXITHANDLER({
        iec104_send_general_interrogation_term(QOI_GROUP_4, link_lost ? 1U : 0U);
        CSLOG("Finished sending permanent fault data for all feeders and phases. Exiting process.\r\n");
        CSLOG("%s EXIT \r\n", PROCESS_CURRENT()->name);
    });

    PROCESS_BEGIN();

    CSLOG("PROCESS %s START \r\n", PROCESS_CURRENT()->name);
    etimer_set(&timer, 100);
    feeder_id = 0;
    link_lost = false;

    PT_SEM_WAIT(&iec104_send_permanent_faults.pt, &g_tx_sem);

    CSLOG("Starting to send permanent fault data for feeders and phases...\r\n");
    iec104_send_general_interrogation_con(QOI_GROUP_4, 0);

    for(feeder_id = 0; (feeder_id < MAX_POWER_LINE_COUNT) && !link_lost; feeder_id++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(feeder_id);
        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for(phase_id = PHASE_L1; (phase_id < PHASE_MAX) && !link_lost; phase_id++)
        {
            emit_state = (iec104_fault_emit_state_t){0};

            while(!iec104_emit_feeder_permanent_faults(feeder_id, phase_id,
                                                      COT_INTERROGATED_GROUP4,
                                                      &emit_state))
            {
                /* Hat dustuyse yayim asla tamamlanamaz; semafor birakilabilsin
                 * diye donguden temiz cikilir. */
                if(!iec104_is_link_active())
                {
                    link_lost = true;
                    break;
                }

                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
                etimer_reset(&timer);
            }
        }
    }

    iec104_send_general_interrogation_term(QOI_GROUP_4, link_lost ? 1U : 0U);

    PT_SEM_SIGNAL(&process_pt, &g_tx_sem);

    PROCESS_END();
}
