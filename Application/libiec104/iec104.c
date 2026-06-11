/*
 * iec104.c
 *
 *  Created on: 26 Tem 2025
 *      Author: fatih
 */
#include "iec104.h"
#include "cp56time2a.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "breaker.h"
#include "time_service.h"
#include "bsp.h"
#include "rtc.h"
#include "iec104_util.h"
#include "fault_log.h"
#include "iec104_config.h"
#include "iec104_application.h"

static const uint8_t supported_asdu_types[] = {
    M_DP_TB_1, // 31, Double Point Information
    M_SP_TB_1, // 30, Single Point Information
    M_ME_TF_1, // 36, Measured Value, Normalized Value
    C_SC_NA_1, // 45, Single Command
    C_DC_NA_1, // 46, Double Command
    M_EI_NA_1, // 70, End of Initialization
    C_IC_NA_1, // 100, Interrogation Command
	C_CS_NA_1 // 103, clock synchronization command
};

static uint8_t iec104_rx_buffer[512] = {0};
static uint16_t iec104_rx_buffer_len = 0;

static iec104_io_t iec_io = {0};

static iec104_package_t package = {0};
static bool package_ready = false;
 
static uint16_t receive_sn = 0; //receive_squence_number, scadadan alinan paket sayisi, 15-bit
static uint16_t send_sn = 0;    //send_squence_number, rtu'nun gonderdigi paket sayisi, 15-bit
static uint16_t ack_sn = 0;    // Scada'nin onayladigi son paket numarasi, 15-bit
 
// Gonderilen I-frame paketlere Onay almadan maksimum gonderilecek mesaj sayisi. Doldugunda t1 sure sonunda tekrar gonderim yapilir.
 static uint16_t k_counter = 0; // Henuz SCADA'dan ACK alinmamış I-frame sayisi
//Alinan I-frame paketlere onay gondermeden alinabilecek  maksimum mesaj sayisi. Doldugunda hemen onay gonderilmeli. Eger sayi t2 suresine kadar dolmaz ise t2 suresi sonunda onay gonderilmeli
static uint16_t w_counter = 0; // Henuz ACK gondermedigimiz alinan I-frame sayisi
 
static uint32_t t0_timer = 0;
static uint32_t t1_timer = 0;
static uint32_t t2_timer = 0;
static uint32_t t3_timer = 0;

static uint32_t periodical_send_interval = 0;

static bool wait_for_ack = false;
 
static cp56time2a_t last_clock_sync_time = {0};
static iec104_config_t config = {0};

void iec104_send_periodic_update(void);
void iec104_send_phase_currents(cause_of_transmission_t cause);
void iec104_send_fault_currents(cause_of_transmission_t cause);
void iec104_send_fault_durations(cause_of_transmission_t cause);
void iec104_send_fault_types(cause_of_transmission_t cause);
void iec104_send_energy_states(cause_of_transmission_t cause);
void iec104_send_nominal_current_states(cause_of_transmission_t cause);
void iec104_send_rf_communication_states(cause_of_transmission_t cause);
/* Feeder+phase filtreli alternatifler (henuz kullanilmiyor) */
void iec104_send_phase_currents_for(uint8_t feeder_id, uint8_t phase, cause_of_transmission_t cause);
void iec104_send_fault_currents_for(uint8_t feeder_id, uint8_t phase, cause_of_transmission_t cause);
void iec104_send_fault_durations_for(uint8_t feeder_id, uint8_t phase, cause_of_transmission_t cause);
void iec104_send_fault_types_for(uint8_t feeder_id, uint8_t phase, cause_of_transmission_t cause);
void iec104_send_energy_states_for(uint8_t feeder_id, uint8_t phase, cause_of_transmission_t cause);
void iec104_send_nominal_current_states_for(uint8_t feeder_id, uint8_t phase, cause_of_transmission_t cause);
void iec104_send_rf_communication_states_for(uint8_t feeder_id, uint8_t phase, cause_of_transmission_t cause);

void iec104_tick(void)
{
	if(w_counter > 0){
		t2_timer++;
	}
    t3_timer++;

    if(wait_for_ack){
        t1_timer++;
    }

    periodical_send_interval++;

}

static inline bool iec104_is_i_format(const iec104_package_t *packet) {
    return (packet->frame.apci.control_field[0] & 0x01) == 0;
}

static inline bool iec104_is_s_format(const iec104_package_t *packet) {
    return (packet->frame.apci.control_field[0] & 0x03) == 0x01;
}

static inline bool iec104_is_u_format(const iec104_package_t *packet) {
    return (packet->frame.apci.control_field[0] & 0x03) == 0x03;
}
void iec104_set_sbo_state(bool is_active)
{
	config.is_sbo_active = is_active;
}

bool iec104_get_sbo_state(void)
{
	return config.is_sbo_active;
}
 
void iec104_send(const uint8_t *data, size_t length)
{
	const iec104_package_t * pkt = (const iec104_package_t *)data;

	if(iec104_is_i_format(pkt)){
		wait_for_ack = true;

		if(k_counter >= config.k_max){
			CSLOG("Window full, cannot send more I-frames\r\n");
            //TODO: buffer'a alabiliriz. Ama gerek yok gibi
			//return;
		}
		k_counter++;
        send_sn++;
        if (send_sn > 32767) {
            send_sn = 0;
        }
        w_counter = 0;
        t1_timer = 0;
	}

    if(iec_io.send) {
    	//TODO: Bu fonksiyon kuyruklayip GSM module uzerinden gonderim yapmali
        iec_io.send(data, length);
    }
}

static int send_package(const uint8_t *data, uint16_t length)
{
    CSLOG("Data sended (%d bytes): ", length);
    for (size_t i = 0; i < length && i < length; i++) {
        CSLOG_NODT("%02X ", data[i]);
    }
    CSLOG_NODT("\r\n");

    return 0;
}

static int handle_c_sc_na_1(ioa_3byte_t ioa, uint8_t state)
{
    CSLOG("C_SC_NA_1: IOA=%u, State=%u\r\n", iec104_ioa_3byte_to_uint32(ioa), state);
    return 0;
}

void iec104_set_originator_address(uint8_t address)
{
    config.originator_address = address;
}

void iec104_set_common_address(uint16_t address)
{
    config.common_address = address;
}

static inline i_format_control_t make_iframe_control(uint16_t send_seq, uint16_t receive_seq)
{
    return (i_format_control_t){
        .format = 0x00, // I-Format
        .send_seq = send_seq,
        .format2 = 0x00, // I-Format
        .receive_seq = receive_seq
    };
}

static inline asdu_header_t make_asdu_header(uint8_t type_id, cot_t cot, uint16_t originator_address, uint16_t common_address,
                                             uint8_t vsq_number_of_objects, uint8_t vsq_sq_bit)
{
    return (asdu_header_t){
        .type_id = type_id,
        .cot = cot,
        .originator_address = config.originator_address,
        .common_asdu_address = config.common_address,
        .vsq = {
            .number_of_objects = vsq_number_of_objects, // 1: single object, != 1: multiple objects
            .sq_bit = vsq_sq_bit // 1: Single object, 0: multiple objects, sq=1 ise IOA'lar arsisik tek IOA gonderilir, sq=0 ise her obje için ayri IOA var
        }
    };
}

void iec104_init(const iec104_io_t *io_cfg, const iec104_config_t *iec104_config)
{
	if(io_cfg){
		iec_io = *io_cfg;
	}else{
		iec_io.send = send_package;
	}

    if(iec104_config == NULL){
    	CSLOG("Error: iec104_config is NULL, using default config\r\n");
        config = (iec104_config_t){
            .periodical_send_interval = 60, // seconds
            .t0_max = 30,  //  [1-255 sec, def:30s] Baglanti kurma zaman asimi, RTU'da onemsiz(?) [1-255 saniye]
            .t1_max = 45,  //  [1-255 sec, def:15s] Onay zaman asimi. RTU I-frame gonderdikten sonra baslar, sure sonuna kadar scada'dan onay gelmesi lazim, gelmezse paket kaybolmus denilir, tekrar gonderim yapilir. Belirli sayida tekrar gonderimden sonra  ack alamaz ise bagantiyi koparir
            .t2_max = 30,  //  [1-255 sec, def:10s] Bir istasyonun (RTU veya SCADA) aldigı I-frame paketlerine onay gondermek icin bekleyebilecegi maks sure
            .t3_max = 60,  //  [1-255 sec, def:20s] Idle timeout. Hat bu sure boyunca bosta kalirsa TESTFR gonderimi yaparak baglantiyi test eder. SCADA'da yapabilir.
            .k_max = 64,   //  [1-32767,   def:12 ] Send-window-limit: Max number of unacknowledged I format frames
            .w_max = 32,    // [1-32767,    def:8  ] W, alim penceresi siniri: Max number of unacknowledged I format frames to be received
            .sbo_execute_timeout = 30, // seconds. Time between SBO select and execute command
            .is_sbo_active = 0, // 0: Not active, 1: Active
            .originator_address = 1,
            .common_address = 1,
        };
    }else{
         config = *iec104_config;
    }
   

    // k > w secilmeli
    k_counter = 0;
    w_counter = 0;

    iec104_reset();

    //Tum strcut yapilarinin boyutlarini kontrol et, compile time hatasi ver
	_Static_assert (sizeof(apci_header_t) == 6, "APCI Header size is not 6 bytes " );
	_Static_assert(sizeof(asdu_header_t) == 6, "ASDU Header size is not 7 bytes");
	_Static_assert(sizeof(cot_t) == 1, "Cause of Transmission size is not 1 byte");
	_Static_assert(sizeof(vsq_t) == 1, "Variable Structure Qualifier size is not 1 byte");
    _Static_assert(sizeof(ioa_3byte_t) == 3, "Information Object Address size is not 3 bytes");
	_Static_assert(sizeof(i_format_control_t) == 4, "I-Format Control size is not 4 bytes");
    _Static_assert(sizeof(s_format_control_t) == 4, "S-Format Control size is not 4 bytes");
    _Static_assert(sizeof(u_format_control_t) == 4, "U-Format Control size is not 4 bytes");
    _Static_assert(sizeof(iec104_package_t) == 255, "IEC104 Package size is not 255 bytes ");
 
    //Object yapilarinin boyutlarini kontrol et, compile time hatasi ver
    _Static_assert(sizeof(siq_t) == 1, "Single Point Information with Quality descriptor size is not 1 bytes");
    _Static_assert(sizeof(diq_t) == 1, "Double Point Information with Quality descriptor size is not 1 bytes");
    _Static_assert(sizeof(cp56time2a_t) == 7, "CP56Time2a size is not 7 bytes");
    _Static_assert(sizeof(m_ei_na_1_t) == 4, "M_EI_NA_1 size is not 4 bytes");
    _Static_assert(sizeof(m_sp_na_1_t) == 4, "M_SP_NA_1 size is not 4 bytes");
    _Static_assert(sizeof(m_dp_tb_1_t) == 11, "M_DP_TB_1 size is not 11 bytes");
    _Static_assert(sizeof(m_me_tf_1_t) == 15, "M_ME_TF_1 size is not 15 bytes");
    _Static_assert(sizeof(c_sc_na_1_t) == 4, "C_SC_NA_1 size is not 4 bytes");
}
 

void iec104_send_end_of_initialization(void)
{
    iec104_package_t pkt;

    const size_t pkt_length = sizeof(apci_header_t) + sizeof(asdu_header_t) + sizeof(m_ei_na_1_t);
    memset(&pkt, 0, pkt_length); // +2 for start byte and length byte

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = pkt_length - 2; // -2 for start byte and length byte
    pkt.frame.apci.i_frame.format = 0x00; // I-Format
    pkt.frame.apci.i_frame.send_seq = send_sn;
    pkt.frame.apci.i_frame.format2 = 0x00;
    pkt.frame.apci.i_frame.receive_seq = receive_sn;

    pkt.frame.asdu_header.type_id = M_EI_NA_1; // End of Initialization
    pkt.frame.asdu_header.cot.cause = COT_INITIALIZED;
    pkt.frame.asdu_header.cot.pn_bit = 0;
    pkt.frame.asdu_header.cot.test_bit = 0;

    pkt.frame.asdu_header.originator_address = config.originator_address;
    pkt.frame.asdu_header.common_asdu_address = config.common_address;
    pkt.frame.asdu_header.vsq.number_of_objects = 1; // Single object
    pkt.frame.asdu_header.vsq.sq_bit = 0;

    pkt.frame.m_ei_na_1.ioa.ioa_high = 0x00; // Information Object Address (genelde 0)
    pkt.frame.m_ei_na_1.ioa.ioa_mid = 0x00; 
    pkt.frame.m_ei_na_1.ioa.ioa_low = 0x00;
    pkt.frame.m_ei_na_1.coi.initialization_cause = 0; // Cause of Initialization (0=Normal, 1=Restart, etc.)

    iec104_send(pkt.data, pkt_length );
}

void iec104_send_general_interrogation_con(iec104_qoi_t qoi, uint8_t is_negative)
{
    iec104_package_t pkt;

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = 14; // ASDU header + 1 object
    pkt.frame.apci.i_frame.format = 0x00; // I-Format
    pkt.frame.apci.i_frame.send_seq = send_sn;
    pkt.frame.apci.i_frame.format2 = 0x00;
    pkt.frame.apci.i_frame.receive_seq = receive_sn;

    pkt.frame.asdu_header.type_id = C_IC_NA_1; // Interrogation Command
    pkt.frame.asdu_header.cot.cause = COT_ACTIVATION_CON;
    pkt.frame.asdu_header.cot.pn_bit = is_negative;       // 0=Positive, 1=Negative
    pkt.frame.asdu_header.cot.test_bit = 0;

    pkt.frame.asdu_header.originator_address = config.originator_address;
    pkt.frame.asdu_header.common_asdu_address = config.common_address;
    pkt.frame.asdu_header.vsq.number_of_objects = 1; // Single object
    pkt.frame.asdu_header.vsq.sq_bit = 0;

    pkt.frame.c_ic_na_1_command.ioa.ioa_high = 0x00; // Information Object Address (genelde 0)
    pkt.frame.c_ic_na_1_command.ioa.ioa_mid = 0x00; 
    pkt.frame.c_ic_na_1_command.ioa.ioa_low = 0x00; 
    pkt.frame.c_ic_na_1_command.qoi = (uint8_t)qoi; // Qualifier of Interrogation (20=Station, 21=Group1, etc.)

    iec104_send(pkt.data, 16);
}

void iec104_send_general_interrogation_term(iec104_qoi_t qoi)
{
    iec104_package_t pkt;

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = 14; // ASDU header + 1 object
    pkt.frame.apci.i_frame.format = 0x00; // I-Format
    pkt.frame.apci.i_frame.send_seq = send_sn;
    pkt.frame.apci.i_frame.format2 = 0x00;
    pkt.frame.apci.i_frame.receive_seq = receive_sn;

    pkt.frame.asdu_header.type_id = C_IC_NA_1; // Interrogation Command
    pkt.frame.asdu_header.cot.cause = COT_ACTIVATION_TERM;
    pkt.frame.asdu_header.cot.pn_bit = 0;
    pkt.frame.asdu_header.cot.test_bit = 0;

    pkt.frame.asdu_header.originator_address = config.originator_address;
    pkt.frame.asdu_header.common_asdu_address = config.common_address;
    pkt.frame.asdu_header.vsq.number_of_objects = 1; // Single object
    pkt.frame.asdu_header.vsq.sq_bit = 0;

    pkt.frame.c_ic_na_1_command.ioa.ioa_high = 0x00; // Information Object Address (genelde 0)
    pkt.frame.c_ic_na_1_command.ioa.ioa_mid = 0x00; 
    pkt.frame.c_ic_na_1_command.ioa.ioa_low = 0x00; 
    pkt.frame.c_ic_na_1_command.qoi = (uint8_t)qoi; // Qualifier of Interrogation (20=Station, 21=Group1, etc.)

    iec104_send(pkt.data, 16);
}

void iec104_interrogation_send_m_sp_tb_1_objects(const ioa_3byte_t *ioas, const siq_t *states)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(m_sp_tb_1_t)*3;

    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(m_sp_tb_1_t)*3 + 10; // ASDU header + 1 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header = make_asdu_header(M_SP_TB_1, (cot_t){.cause = COT_INTERROGATED_STATION, .pn_bit = 0, .test_bit = 0}, config.originator_address, config.common_address, 3, 0);

    m_sp_tb_1_t *m_sp_tb_1 = (m_sp_tb_1_t *)&pkt.data[DATA_START_IDX];

#ifdef IEC104_TEST
    cp56time2a_t timestamp = cp56time2a_make(29295, 17, 15, 14, 4, 8, 25);
#else
    //TODO: timestamp RF'den gelen paket ile eslenebilir
    cp56time2a_t timestamp = cp56time2a_now();
#endif

    for (int i = 0; i < 3; i++) {

        m_sp_tb_1[i].ioa = ioas[i];
        m_sp_tb_1[i].siq = states[i];
        m_sp_tb_1[i].timestamp = timestamp;
    }

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}

void iec104_interrogation_send_m_me_tf_1_objects(const ioa_3byte_t *ioas, const float *values, const qds_t *quality)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(m_me_tf_1_t)*3;

    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(m_me_tf_1_t)*3 + 10; // ASDU header + 1 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header = make_asdu_header(M_ME_TF_1, (cot_t){.cause = COT_INTERROGATED_STATION, .pn_bit = 0, .test_bit = 0}, config.originator_address, config.common_address, 3, 0);

    m_me_tf_1_t *m_me_tf_1 = (m_me_tf_1_t *)&pkt.data[DATA_START_IDX];
#ifdef IEC104_TEST
    cp56time2a_t timestamp = cp56time2a_make(43665, 18, 15, 14, 4, 8, 25);
#else
    cp56time2a_t timestamp = cp56time2a_now();
#endif
    for (int i = 0; i < 3; i++) {
        m_me_tf_1[i].ioa = ioas[i];
        m_me_tf_1[i].value = values[i]; // Normalized value
        m_me_tf_1[i].quality = quality[i]; // Quality descriptor
        m_me_tf_1[i].timestamp = timestamp;
    }

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}

void iec104_interrogation_send_c_sc_na_1_object(ioa_3byte_t ioa, uint8_t state, qualifier_of_command_t qualifier, se_bit_t se_bit)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(c_sc_na_1_t);

    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(c_sc_na_1_t) + 10; // ASDU header + 1 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header = make_asdu_header(C_SC_NA_1, (cot_t){.cause = COT_INTERROGATED_STATION, .pn_bit = 0, .test_bit = 0}, config.originator_address, config.common_address, 1, 1);

    c_sc_na_1_t *c_sc_na_1 = (c_sc_na_1_t *)&pkt.data[DATA_START_IDX];

    c_sc_na_1->ioa = ioa;
    c_sc_na_1->sco.scs = state; // Single Command State
    c_sc_na_1->sco.qu = qualifier; // Qualifier of Command State
    c_sc_na_1->sco.reserved = 0;
    c_sc_na_1->sco.se_bit = se_bit; // SE Bit

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}

void iec104_send_feeder_temporary_faults(cause_of_transmission_t cause);

void iec104_interrogation_send_group1()
{
    CSLOG("Sending all objects for interrogation group 1...\r\n");

    iec104_send_fault_currents(COT_INTERROGATED_GROUP1);
    iec104_send_fault_durations(COT_INTERROGATED_GROUP1);
    iec104_send_phase_currents(COT_INTERROGATED_GROUP1);
}

void iec104_interrogation_send_group2()
{
    CSLOG("Sending all objects for interrogation group 2...\r\n");

    iec104_send_energy_states(COT_INTERROGATED_GROUP2);
    iec104_send_nominal_current_states(COT_INTERROGATED_GROUP2);
    iec104_send_rf_communication_states(COT_INTERROGATED_GROUP2);

    //iec104_process_tx_buffer_dump();
}

void iec104_interrogation_send_group3()
{
    CSLOG("Sending all objects for temporary faults [interrogation group 3]...\r\n");

    //iec104_send_feeder_temporary_faults(COT_INTERROGATED_GROUP3);
 
    iec104_application_event_handler(IEC104_APP_EVT_SEND_TEMP_FAULTS);

    //iec104_process_tx_buffer_dump();
}

void iec104_interrogation_send_group4()
{
	CSLOG("Sending all objects for temporary faults [interrogation group 4]...\r\n");

    iec104_application_event_handler(IEC104_APP_EVT_SEND_PERM_FAULTS);

}

void iec104_interrogation_send_all_objects()//const power_line_t *lines, uint32_t count)
{
	//Cevaplar COT_INTERROGATED_STATION 20 ile gonderilir, 21 grup sorgulama olur sa duruma bakalim!
    CSLOG("Sending all objects for interrogation...\r\n");

    iec104_send_fault_currents(COT_INTERROGATED_STATION);
    iec104_send_fault_durations(COT_INTERROGATED_STATION);
    iec104_send_phase_currents(COT_INTERROGATED_STATION);
    
    iec104_send_energy_states(COT_INTERROGATED_STATION);
    iec104_send_nominal_current_states(COT_INTERROGATED_STATION);
    iec104_send_rf_communication_states(COT_INTERROGATED_STATION);

    //iec104_process_tx_buffer_dump();

    return;

    for(uint32_t i = 0; i < MAX_POWER_LINE_COUNT; i++)
    {
        //const power_line_t *line = &lines[i];
        const power_line_t *line = breaker_get_power_line_by_idx(i);

        if(line == NULL) {
            CSLOG("Line %d is NULL, skipping...\r\n", i);
            continue;
        }

        if(!line->iec104.in_use) {
            CSLOG("Line %d is not in use, skipping...\r\n", i);
            continue;
        }

        //TODO: 140226

        // Send Single Point Information
        siq_t states[3] = {(siq_t){0}, (siq_t){0}, (siq_t){0}};

//        states[0].spi = line->phase[PHASE_R].data.breaker_state;
//        states[1].spi = line->phase[PHASE_S].data.breaker_state;
//        states[2].spi = line->phase[PHASE_T].data.breaker_state;

        //iec104_interrogation_send_m_sp_tb_1_objects(line->iec104.m_sp_tb_1_ioa, states);

//        float values[3] = {line->phase[PHASE_R].data.current,
//                             line->phase[PHASE_S].data.current,
//                             line->phase[PHASE_T].data.current};

        qds_t qualities[3] = {{0},{0},{0}}; // Quality descriptors for each phase
                             
        // Send Measured Value, Normalized Value
        //iec104_interrogation_send_m_me_tf_1_objects(line->iec104.m_me_tf_1_ioa, values, qualities);




#ifdef C_SC_NA_1_ENABLED
        // Send Single Command
        iec104_interrogation_send_c_sc_na_1_object(line->iec104.c_sc_na_1_ioa, line->breaker_state, 0, 0);
#endif
    }
}

void iec104_interrogation_handler(const iec104_package_t *pkt)
{
    if(pkt->frame.c_ic_na_1_command.qoi == 20)
    {
        iec104_send_general_interrogation_con(QOI_STATION, 0);
        iec104_interrogation_send_all_objects();
        iec104_send_general_interrogation_term(QOI_STATION);
    }
    else if(pkt->frame.c_ic_na_1_command.qoi == 21)
    {
    	iec104_send_general_interrogation_con(QOI_GROUP_1, 0);
    	iec104_interrogation_send_group1();
		iec104_send_general_interrogation_term(QOI_GROUP_1);
    }
    else if(pkt->frame.c_ic_na_1_command.qoi == 22)
    {
    	iec104_send_general_interrogation_con(QOI_GROUP_2, 0);
    	iec104_interrogation_send_group2();
		iec104_send_general_interrogation_term(QOI_GROUP_2);
    }
    else if(pkt->frame.c_ic_na_1_command.qoi == 23)
    {
    	iec104_interrogation_send_group3();
    }
    else if(pkt->frame.c_ic_na_1_command.qoi == 24)
    {
    	iec104_interrogation_send_group4();
    }
    else
    {
        iec104_send_general_interrogation_con(pkt->frame.c_ic_na_1_command.qoi, 1);
    }
}

#ifdef C_SC_NA_1_ENABLED
void iec104_c_sc_na_1_45_command_handler(const iec104_package_t *pkt)
{
    if (pkt == NULL) {
        CSLOG("Invalid packet\r\n");
        return;
    }
    int result = 0;

    // Process the C_SC_NA_1_45 command
    CSLOG("Processing C_SC_NA_1_45 command\r\n");
    CSLOG("Command Type [%s], Command State [%s]\r\n", pkt->frame.c_sc_na_1.sco.se_bit ? "SELECT" : "EXECUTE",
        pkt->frame.c_sc_na_1.sco.scs == 0 ? "OFF" : (pkt->frame.c_sc_na_1.sco.scs == 1 ? "ON" : "UNKNOWN"));

    if(!breaker_is_ioa_valid(pkt->frame.c_sc_na_1.ioa)){
        CSLOG("C_SC_NA_1 Handler: Invalid IOA %u\r\n", iec104_ioa_3byte_to_uint32(pkt->frame.c_sc_na_1.ioa));
        // Wrong IOA
        iec104_send_C_SC_NA_1((cot_t){.cause = UkIOA, .pn_bit = 1, .test_bit = 0}, 
            pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs, pkt->frame.c_sc_na_1.sco.qu, pkt->frame.c_sc_na_1.sco.se_bit);            
        return;
    }


    if(iec104_get_sbo_state()) 
    {    
        // Select
        if(pkt->frame.c_sc_na_1.sco.se_bit)
        {
            breaker_set_power_line_sbo_state(pkt->frame.c_sc_na_1.ioa, (sbo_state_t){.state = SBO_SELECTED, .value = pkt->frame.c_sc_na_1.sco.scs, .select_time = millis()});
            // Successfully selected, send ACK
            iec104_send_C_SC_NA_1((cot_t){.cause = COT_ACTIVATION_CON, .pn_bit = 0, .test_bit = 0}, 
                pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs, pkt->frame.c_sc_na_1.sco.qu, pkt->frame.c_sc_na_1.sco.se_bit);

            //TODO: Log    
            return;
        }
        else
        {
            sbo_state_t sbo = breaker_get_power_line_sbo_state(pkt->frame.c_sc_na_1.ioa);

            if(!pkt->frame.c_sc_na_1.sco.se_bit && !time_has_elapsed(sbo.select_time, (SBO_SELECT_TIMEOUT*1000)) && sbo.state == SBO_SELECTED && sbo.value == pkt->frame.c_sc_na_1.sco.scs) // Execute
            {
                result = iec_io.c_sc_na_1_cb(pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs); 

                breaker_set_power_line_sbo_state(pkt->frame.c_sc_na_1.ioa, (sbo_state_t){0});
                // Successfully executed command
                iec104_send_C_SC_NA_1((cot_t){.cause = COT_ACTIVATION_CON, .pn_bit = 0, .test_bit = 0}, 
                    pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs, pkt->frame.c_sc_na_1.sco.qu, pkt->frame.c_sc_na_1.sco.se_bit);
                iec104_send_C_SC_NA_1((cot_t){.cause = COT_ACTIVATION_TERM, .pn_bit = 0, .test_bit = 0}, 
                    pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs, pkt->frame.c_sc_na_1.sco.qu, pkt->frame.c_sc_na_1.sco.se_bit);  

                //TODO: Log
            }
            else
            {
                CSLOG("C_SC_NA_1 Handler: SBO not selected or value mismatch, cannot execute\r\n");
                iec104_send_C_SC_NA_1((cot_t){.cause = COT_ACTIVATION_TERM, .pn_bit = 1, .test_bit = 0}, 
                    pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs, pkt->frame.c_sc_na_1.sco.qu, pkt->frame.c_sc_na_1.sco.se_bit);                 
            }            
        } 

        return;
    }

    // Direct Execute
    if(!pkt->frame.c_sc_na_1.sco.se_bit)
    {
        result = iec_io.c_sc_na_1_cb(pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs);

        if (result == 0) 
        {
            // Successfully executed command
            iec104_send_C_SC_NA_1((cot_t){.cause = COT_ACTIVATION_CON, .pn_bit = 0, .test_bit = 0}, 
                pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs, pkt->frame.c_sc_na_1.sco.qu, pkt->frame.c_sc_na_1.sco.se_bit);
            iec104_send_C_SC_NA_1((cot_t){.cause = COT_ACTIVATION_TERM, .pn_bit = 0, .test_bit = 0}, 
                pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs, pkt->frame.c_sc_na_1.sco.qu, pkt->frame.c_sc_na_1.sco.se_bit);            
        } 
        //TODO: Log
    }
    else
    {
        CSLOG("C_SC_NA_1 Handler SE_BIT is SELECT !!\r\n");
        //TODO: SE_BIT SELECT ise ne gonderecegiz?

        iec104_send_C_SC_NA_1((cot_t){.cause = COT_ACTIVATION_CON, .pn_bit = 1, .test_bit = 0}, 
            pkt->frame.c_sc_na_1.ioa, pkt->frame.c_sc_na_1.sco.scs, pkt->frame.c_sc_na_1.sco.qu, pkt->frame.c_sc_na_1.sco.se_bit); 
        //TODO: Log
    }
}
#endif

cp56time2a_t iec104_get_last_clock_sync_time(void)
{
    return last_clock_sync_time;
}

void iec104_c_cs_na_1_command_handler(const iec104_package_t *pkt)
{
    if (pkt == NULL) {
        CSLOG("Invalid packet\r\n");
        return;
    }

    const cp56time2a_t *timestamp = &pkt->frame.c_cs_na_1.timestamp;

    CSLOG("Processing C_CS_NA_1 command\r\n");
    cp56time2a_print(timestamp);


    iec104_package_t pkt_response;

    memcpy(pkt_response.data, pkt->data, pkt->frame.apci.apdu_length + 2); // +2 for start char and length byte
    pkt_response.frame.asdu_header.cot.cause = COT_ACTIVATION_CON;

    iec104_send(pkt_response.data, (pkt_response.frame.apci.apdu_length + 2));

    if(pkt->frame.c_cs_na_1.timestamp.iv_bit){
        CSLOG("Invalid timestamp received!\r\n");
        return;
    }

    last_clock_sync_time = pkt->frame.c_cs_na_1.timestamp;

    /* Decode CP56Time2a and apply to software RTC, hardware RTC and epoch. */
    const bsp_rtc_t decoded = cp56time2a_to_rtc(timestamp);
    const rtc_t new_time = {
        .millisec = decoded.millisec,
        .second   = decoded.second,
        .minute   = decoded.minute,
        .hour     = decoded.hour,
        .day      = decoded.day,
        .month    = decoded.month,
        .year     = decoded.year,
    };
    rtc_sync(&new_time);
}


void iec104_send_test_frame()
{
    static const uint8_t test_frame[] = {0x68, 0x04, (IEC104_TESTFR_ACT << 2) | 0x03, 0x00, 0x00, 0x00};
    iec_io.send(test_frame, sizeof(test_frame));
}

void iec104_process_u_frame(const u_format_control_t *uframe)
{
    static const uint8_t startdt_act_con[] = {0x68, 0x04, (IEC104_STARTDT_CON << 2) | 0x03, 0x00, 0x00, 0x00};
    static const uint8_t stopdt_act_con[] = {0x68, 0x04,  (IEC104_STOPDT_CON << 2) | 0x03, 0x00, 0x00, 0x00};
    static const uint8_t testfr_act_con[] = {0x68, 0x04,  (IEC104_TESTFR_CON << 2) | 0x03, 0x00, 0x00, 0x00};
    static const uint8_t end_of_init[] = {0x68, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x46, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}; // M_EI_NA_1

    if (uframe == NULL) {
        CSLOG("Invalid U-Frame packet\r\n");
        return;
    }

    CSLOG("Processing U-Frame: Function Code: 0x%02X\r\n", uframe->function_code);

    switch (uframe->function_code)
    {
    case IEC104_STARTDT_ACT:
        CSLOG("  StartDT Act received, sending StartDT Con\r\n");
        iec_io.send(startdt_act_con, sizeof(startdt_act_con));
        //iec_io.send(end_of_init, sizeof(end_of_init));
        receive_sn = 0;
        send_sn = 0;
        iec104_send_end_of_initialization();
        break;
    case IEC104_STOPDT_ACT:
        CSLOG("  StopDT Act received, sending StopDT Con\r\n");
        iec_io.send(stopdt_act_con, sizeof(stopdt_act_con));
        //TODO: Baglantiyi kes
        break;
    case IEC104_TESTFR_ACT:
        CSLOG("  TestFR Act received, sending TestFR Con\r\n");
        iec_io.send(testfr_act_con, sizeof(testfr_act_con));
        break;

    default:
    	CSLOG("Unkown U frame function code : 0x%02d \r\n", uframe->function_code);
    	return;
        break;
    }

}

/*
    IEC104_STOPDT_ACT
        
*/
void iec104_send_stopdt_con(void)
{
    const uint8_t stopdt_act[] = {0x68, 0x04, (IEC104_STOPDT_ACT << 2) | 0x03, 0x00, 0x00, 0x00};
    iec_io.send(stopdt_act , sizeof(stopdt_act)); 
}

void iec104_send_stopdt_act(void)
{
    const uint8_t stopdt_con[] = {0x68, 0x04, (IEC104_STOPDT_CON << 2) | 0x03, 0x00, 0x00, 0x00};
    iec_io.send(stopdt_con , sizeof(stopdt_con)); 
}

void iec104_send_s_frame(uint16_t receive_seq)
{
	receive_seq &= 0x7FFF; // 15-bits

	iec104_package_t pkt;

	pkt.frame.apci.start_char = IEC104_START_BYTE;
	pkt.frame.apci.apdu_length = 4;
	pkt.frame.apci.s_frame.format = 0x01;
    pkt.frame.apci.s_frame.reserved = 0x00;
    pkt.frame.apci.s_frame.format2 = 0x00;
	pkt.frame.apci.s_frame.receive_seq = receive_seq;

	iec104_send(pkt.data, 6);
}

void on_ack_received(uint16_t nr) 
{
     CSLOG("-> ILK On-ACK Received k-counter[%d] ack-sn[%d] w-counter[%d] send-sn[%d] receive-sn[%d]\r\n", k_counter, ack_sn, w_counter, send_sn, receive_sn);
    // nr, SCADA'nin beklediği frame numarasidir, RSN
    if (nr > ack_sn) 
    {
        uint16_t acked = nr - ack_sn; // kac paket onaylandi
        if (acked <= k_counter) 
        {
            k_counter -= acked;
        } 

        else 
        {
            k_counter = 0;  
            //TODO: Sequence numarasi hatali
            //iec104_send_stopdt_con();
            wait_for_ack = false;
            t1_timer = 0;
            CSLOG("Sequence number error: expected %d, got %d --  k-counter[%d] ack-sn[%d] w-counter[%d] send-sn[%d] receive-sn[%d]\r\n",
                ack_sn + 1, nr, k_counter, ack_sn, w_counter, send_sn, receive_sn);
        }

        ack_sn = nr;
    }

    if(ack_sn == send_sn){
        wait_for_ack = false;
        t1_timer = 0;
        CSLOG("All frames acknowledged\r\n");
    }

    CSLOG("-> SON On-ACK Received k-counter[%d] ack-sn[%d] w-counter[%d] send-sn[%d] receive-sn[%d]\r\n", k_counter, ack_sn, w_counter, send_sn, receive_sn);
}

void iec104_process_s_frame(const s_format_control_t *sframe)
{
    CSLOG("Processing S-Frame: Receive Sequence Number: %d\r\n", sframe->receive_seq);

    on_ack_received(sframe->receive_seq);
}

void iec104_send_i_frame()
{
	i_format_control_t iframe;

	iframe.send_seq = send_sn;
	iframe.receive_seq = receive_sn;

	send_sn = (send_sn + 1) % 0x7FFF;

}


void iec104_process_i_frame(const i_format_control_t *iframe)
{
    CSLOG("Processing I-Frame: Send Sequence Number: %d, Receive Sequence Number: %d\r\n",
           iframe->send_seq, iframe->receive_seq);

    receive_sn = (receive_sn + 1) & 0x7FFF;

    if(k_counter >= config.k_max){
        CSLOG("Sending ACK: %d\r\n", receive_sn);
    	iec104_send_s_frame(receive_sn);
    	k_counter = 0;
    }

    CSLOG("Received IEC-104 package:\r\n");
    CSLOG("  Start Char: 0x%02X\r\n", package.frame.apci.start_char);
    CSLOG("  APDU Length: %d\r\n", package.frame.apci.apdu_length);
    CSLOG("  Control Field: ");
    for (size_t i = 0; i < sizeof(package.frame.apci.control_field); i++) {
        CSLOG_NODT("%02X ", package.frame.apci.control_field[i]);
    }
    CSLOG("\r\n");
    CSLOG("  Type ID: %d (%s - %s)\r\n", package.frame.asdu_header.type_id,
           val_to_str(package.frame.asdu_header.type_id, asdu_types_str),
           val_to_str(package.frame.asdu_header.type_id, asdu_types_desc_str));
    CSLOG("  VSQ: Number of Objects: %d, SQ Bit: %d\r\n",
           package.frame.asdu_header.vsq.number_of_objects,
           package.frame.asdu_header.vsq.sq_bit);
    CSLOG("  COT: Cause: %d - (%s), P/N Bit: %d, T Bit: %d\r\n",
           package.frame.asdu_header.cot.cause,
              val_to_str(package.frame.asdu_header.cot.cause, causetx_desc_str),
           package.frame.asdu_header.cot.pn_bit,
           package.frame.asdu_header.cot.test_bit);
    CSLOG("  Originator Address: %d\r\n", package.frame.asdu_header.originator_address);
    CSLOG("  Common ASDU Address: %d\r\n", package.frame.asdu_header.common_asdu_address);


    bool is_supported = false;
    for (size_t i = 0; i < sizeof(supported_asdu_types) / sizeof(supported_asdu_types[0]); i++)
    {
        if(supported_asdu_types[i] == package.frame.asdu_header.type_id) {
            is_supported = true;
            break;
        }
    }

    on_ack_received(iframe->receive_seq);

    if(++w_counter >= config.w_max){
        iec104_send_s_frame(receive_sn);
        w_counter = 0;
        t1_timer = 0;
    }


    if(!is_supported) {
        CSLOG("Unsupported ASDU type: %d\r\n", package.frame.asdu_header.type_id);

        package.frame.asdu_header.cot.cause = UkTypeId; // (unknown ASDU type identification)
        package.frame.asdu_header.cot.pn_bit = 1; // Set P/N bit to indicate that the package is not processed
        iec104_send(package.data, package.frame.apci.apdu_length + 2);

        return;
    }

    if(package.frame.asdu_header.common_asdu_address != config.common_address) {
        CSLOG("Common ASDU Address mismatch: expected %d, got %d\r\n", config.common_address, package.frame.asdu_header.common_asdu_address);
        package.frame.asdu_header.cot.cause = UkComAdrASDU; // (unknown ASDU type identification)
        package.frame.asdu_header.cot.pn_bit = 1; // Set P/N bit to indicate that the package is not processed
        iec104_send(package.data, package.frame.apci.apdu_length + 2);
        return;
    }

    switch(package.frame.asdu_header.type_id)
    {
        case C_IC_NA_1: // Interrogation Command
            CSLOG("Received C_IC_NA_1 Interrogation Command\r\n");
            iec104_interrogation_handler(&package);
        break;
#ifdef C_SC_NA_1_ENABLED
        case C_SC_NA_1:
            CSLOG("Received C_SC_NA_1  Command\r\n");
            iec104_c_sc_na_1_45_command_handler(&package);
            break;
#endif
        case C_CS_NA_1: // clock synchronization command
            CSLOG("Received C_CS_NA_1 Clock Synchronization Command\r\n");
            iec104_c_cs_na_1_command_handler(&package);
            break;

            default:
                CSLOG("Received ASDU Type: %d (%s - %s)\r\n", package.frame.asdu_header.type_id,
                   val_to_str(package.frame.asdu_header.type_id, asdu_types_str),
                   val_to_str(package.frame.asdu_header.type_id, asdu_types_desc_str));
            break;
    }
}

void iec104_reset(void)
{
    wait_for_ack = false;
    receive_sn = 0;
    send_sn = 0;

    // k > w secilmeli
    k_counter = 0;
    w_counter = 0;

    ack_sn = 0;

    t0_timer = 0;
    t1_timer = 0;
    t2_timer = 0;
    t3_timer = 0;

    package_ready = false;
    memset(&package, 0, sizeof(package));

    memset(iec104_rx_buffer, 0, sizeof(iec104_rx_buffer));
    iec104_rx_buffer_len = 0;
 }

uint16_t iec104_get_receive_sn(void)
{
    return receive_sn;
}

uint16_t iec104_get_send_sn(void)
{
    return send_sn;
}

uint16_t iec104_get_w(void)
{
    return w_counter;
}

uint16_t iec104_get_k(void)
{
    return k_counter;
}

uint16_t iec104_get_acksn(void)
{
    return ack_sn;
}

void iec_set_receive_sn(uint16_t sn)
{
    if(sn < 0x7FFF) {
        receive_sn = sn;
    } else {
        CSLOG("Invalid receive sequence number: %d\r\n", sn);
    }
}

void iec_set_send_sn(uint16_t sn)
{
    if(sn < 0x7FFF) {
        send_sn = sn;
    } else {
        CSLOG("Invalid send sequence number: %d\r\n", sn);
    }
}

void iec104_data_received(const uint8_t *data, uint16_t length)
{
    if(data == NULL || length == 0) {
        CSLOG("IEC104 Received invalid data\r\n");
        return;
    }

	if(length > sizeof(iec104_rx_buffer)){
		CSLOG("IEC104 RX buffer overflow! Data length: %d, Buffer size: %d\r\n", length, sizeof(iec104_rx_buffer));
		return;
	}

	if(iec104_rx_buffer_len + length > sizeof(iec104_rx_buffer)){
		length = sizeof(iec104_rx_buffer) - iec104_rx_buffer_len;
		CSLOG("IEC104 RX buffer overflow on append! Current length: %d, Data length: %d, Buffer size: %d\r\n", iec104_rx_buffer_len, length, sizeof(iec104_rx_buffer));
	}

	memcpy(&iec104_rx_buffer[iec104_rx_buffer_len], data, length);
	iec104_rx_buffer_len += length;

	package_ready = true;
}

void iec104_process_package(void)
{
    uint16_t idx = 0;

    // Minimum frame boyutu: 6 byte (APCI header)
    while(idx + 6 <= iec104_rx_buffer_len)
    {
        // Start byte ara
        if(iec104_rx_buffer[idx] != IEC104_START_BYTE)
        {
            idx++;
            continue;
        }

        uint8_t apdu_length = iec104_rx_buffer[idx + 1];

        // APDU length validation (4-253 arası olmalı)
        if(apdu_length < 4 || apdu_length > 253)
        {
            CSLOG("Invalid APDU length: %d\r\n", apdu_length);
            idx++;
            continue;
        }

        uint16_t frame_length = apdu_length + 2; // +2 for start byte and length byte

        // Tam frame var mi kontrol et
        if(idx + frame_length > iec104_rx_buffer_len)
        {
            // Eksik paket, daha fazla veri bekle
            CSLOG("Incomplete frame, waiting for more data. Have: %d, Need: %d\r\n", 
                  iec104_rx_buffer_len - idx, frame_length);
            break;
        }

        if(frame_length > sizeof(iec104_package_t))
        {
            CSLOG("Frame too large: %d > %d\r\n", frame_length, (int)sizeof(iec104_package_t));
            idx += frame_length;
            continue;
        }

        CSLOG("[SCADA -> RTU][%d bytes]: [", frame_length);
        for (uint16_t i = 0; i < frame_length; i++) {
        	CSLOG_NODT("%02X ", iec104_rx_buffer[idx + i]);
        }
        CSLOG_NODT("]\r\n");


        memcpy(&package, &iec104_rx_buffer[idx], frame_length);

        if(iec104_is_u_format(&package)){
            iec104_process_u_frame(&package.frame.apci.u_frame);
        }else if(iec104_is_s_format(&package)){
            iec104_process_s_frame(&package.frame.apci.s_frame);
        }else if(iec104_is_i_format(&package)){
            iec104_process_i_frame(&package.frame.apci.i_frame);
        }else{
            CSLOG("Unknown or damaged package!\r\n");
        }

        idx += frame_length;
    }

    if(idx > 0 && idx < iec104_rx_buffer_len)
    {
        uint16_t remaining = iec104_rx_buffer_len - idx;
        memmove(iec104_rx_buffer, &iec104_rx_buffer[idx], remaining);
        iec104_rx_buffer_len = remaining;
        CSLOG("Remaining %d bytes in buffer\r\n", remaining);
    }
    else
    {
        iec104_rx_buffer_len = 0;
    }

    package_ready = (iec104_rx_buffer_len >= 6);
}
void libiec104_poll(void)
{
	if(package_ready){
        t3_timer = 0;
		iec104_process_package();
	}

	

	if((k_counter >= config.k_max) || ((k_counter > 0) && (t2_timer >= config.t2_max))){
        CSLOG("Sending ACK: %d\r\n", receive_sn);
		iec104_send_s_frame(receive_sn);
		t2_timer = 0;
		k_counter = 0;
	}
    if(t1_timer > config.t1_max){
        CSLOG("T1 timer expired, resend is necessary\r\n");
        t1_timer = 0;
        //TODO: Implement retransmission logic
        //TODO: Save a log entry for retransmission
        send_sn = ack_sn;
        iec104_send_periodic_update();
    }

    if(t3_timer > config.t3_max){
        CSLOG("T3 timer expired, sending TESTFR\r\n");
        t3_timer = 0;
        iec104_send_test_frame();
    }
    
    if(periodical_send_interval >= config.periodical_send_interval){
        iec104_send_periodic_update();
        periodical_send_interval = 0;
    }
 

}

static uint8_t get_type_id_length(uint8_t TypeId)
{
	uint8_t ret = 0;
	const td_asdu_length *item;

	item = asdu_length;
	while (item->value)
	{
		if (item->value == TypeId)
		{
			ret = item->length;
			break;
		}
		item++;
	}

	return ret;
}



void iec104_send_M_SP_TB_1_spontan(ioa_3byte_t ioa, uint8_t value, uint8_t quality)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(m_sp_tb_1_t);

//    CSLOG("sizeof(apci_header_t): %zu\r\n", sizeof(apci_header_t));
//    CSLOG("sizeof(asdu_header_t): %zu\r\n", sizeof(asdu_header_t));
//    CSLOG("sizeof(m_dp_tb_1_object_t): %zu\r\n", sizeof(m_dp_tb_1_object_t));
//    CSLOG("TypeId: %d, Length: %zu\r\n", M_DP_TB_1, type_id_length);

    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(m_sp_tb_1_t) + 10; // Object size + ASDU header + 1 object
    pkt.frame.apci.i_frame.format = 0x00; // I-Format
    pkt.frame.apci.i_frame.send_seq = send_sn;
    pkt.frame.apci.i_frame.format2 = 0x00;
    pkt.frame.apci.i_frame.receive_seq = receive_sn;
    pkt.frame.asdu_header.type_id = M_SP_TB_1; // Single Point Information
    pkt.frame.asdu_header.vsq.sq_bit = 0;
    pkt.frame.asdu_header.vsq.number_of_objects = 1; // 1 object 
    pkt.frame.asdu_header.cot.cause = COT_SPONTANEOUS;
    pkt.frame.asdu_header.cot.pn_bit = 0;
    pkt.frame.asdu_header.cot.test_bit = 0;
    pkt.frame.asdu_header.originator_address = config.originator_address;
    pkt.frame.asdu_header.common_asdu_address = config.common_address;

    // Single Point Information Object
    m_sp_tb_1_t *m_sp_tb_1 = (m_sp_tb_1_t *)&pkt.data[DATA_START_IDX];

    m_sp_tb_1->ioa = ioa;
    m_sp_tb_1->siq.spi = (quality << 1) | value;

#ifdef IEC104_TEST   
    m_sp_tb_1->timestamp = cp56time2a_make(44073, 25, 21, 1, 5, 8, 25); 
#else
    m_sp_tb_1->timestamp = cp56time2a_now();
#endif

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}


/* 31	Double-point information with time tag CP56Time2a */
void iec104_send_M_DP_TB_1_spontan(ioa_3byte_t ioa,  uint8_t value, uint8_t quality)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(m_dp_tb_1_t);

//    CSLOG("sizeof(apci_header_t): %zu\r\n", sizeof(apci_header_t));
//    CSLOG("sizeof(asdu_header_t): %zu\r\n", sizeof(asdu_header_t));
//    CSLOG("sizeof(m_dp_tb_1_object_t): %zu\r\n", sizeof(m_dp_tb_1_object_t));
//    CSLOG("TypeId: %d, Length: %zu\r\n", M_DP_TB_1, type_id_length);
    
    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(m_dp_tb_1_t) + 10; // ASDU header + 1 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header.type_id = M_DP_TB_1; // Double Point Information with Time Tag
    pkt.frame.asdu_header.vsq.sq_bit = 0;
    pkt.frame.asdu_header.vsq.number_of_objects = 1; // 1 object 
    pkt.frame.asdu_header.cot.cause = COT_SPONTANEOUS;
    pkt.frame.asdu_header.cot.pn_bit = 0;
    pkt.frame.asdu_header.cot.test_bit = 0;
    pkt.frame.asdu_header.originator_address = config.originator_address;
    pkt.frame.asdu_header.common_asdu_address = config.common_address;
    // Double Point Information Object

    pkt.frame.m_dp_tb_1.ioa = ioa;
    pkt.frame.m_dp_tb_1.diq.dpi = value;
    pkt.frame.m_dp_tb_1.timestamp = cp56time2a_now();

   

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}



void iec104_send_M_ME_TF_1(cot_t cot, ioa_3byte_t ioa, float value, qds_t quality)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(m_me_tf_1_t);

    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(m_me_tf_1_t) + 10; // ASDU header + 1 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header = make_asdu_header(M_ME_TF_1, cot, config.originator_address, config.common_address, 1, 0); // Measured Value with Time Tag

    // Measured Value Object
    m_me_tf_1_t *m_me_tf_1 = (m_me_tf_1_t *)&pkt.data[DATA_START_IDX];

    m_me_tf_1->ioa = ioa;
    m_me_tf_1->value = value;
    m_me_tf_1->quality = quality;

#ifdef IEC104_TEST
    m_me_tf_1->timestamp = cp56time2a_make(19892, 2, 10, 3, 0, 8, 25);
#else
    m_me_tf_1->timestamp = cp56time2a_now();
#endif

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}

void iec104_send_C_SC_NA_1(cot_t cot, ioa_3byte_t ioa, sco_command_state_t scs, qualifier_of_command_t qu, se_bit_t se_bit)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(c_sc_na_1_t);

    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(c_sc_na_1_t) + 10; // ASDU header + 1 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header = make_asdu_header(C_SC_NA_1, cot,  config.originator_address, config.common_address, 1, 0); // Single Command

    // Single Command Object
    c_sc_na_1_t *c_sc_na_1 = (c_sc_na_1_t *)&pkt.data[DATA_START_IDX];

    c_sc_na_1->ioa = ioa;
    c_sc_na_1->sco.scs = scs;
    c_sc_na_1->sco.reserved = 0; // Reserved bit
    c_sc_na_1->sco.qu = qu; // Qualifier of Command
    c_sc_na_1->sco.se_bit = se_bit; // S/E bit

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}

void iec104_send_C_DC_NA_1(cot_t cot, ioa_3byte_t ioa, dco_command_state_t dcs, qualifier_of_command_t qu, se_bit_t se_bit)
{
    iec104_package_t pkt;
    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(c_dc_na_1_t);
    memset(&pkt, 0, type_id_length);
    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(c_dc_na_1_t) + 10; // ASDU header + 1 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header = make_asdu_header(C_DC_NA_1, cot, config.originator_address, config.common_address, 1, 0); // Double Command
    // Double Command Object
    c_dc_na_1_t *c_dc_na_1 = (c_dc_na_1_t *)&pkt.data[DATA_START_IDX];
    c_dc_na_1->ioa = ioa;
    c_dc_na_1->dco.dcs = dcs;
    c_dc_na_1->dco.qu = qu;
    c_dc_na_1->dco.se_bit = se_bit;
    

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}





























//---------->>> APPLICATON <<<------------
//---------->>> APPLICATON <<<------------
//---------->>> APPLICATON <<<------------
//---------->>> APPLICATON <<<------------
//---------->>> APPLICATON <<<------------
//---------->>> APPLICATON <<<------------
//---------->>> APPLICATON <<<------------
//---------->>> APPLICATON <<<------------
//---------->>> APPLICATON <<<------------


#if 0
void iec104_send_phase_currents(const ioa_3byte_t *ioas, const float *currents, qds_t *quality)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(m_me_tf_1_t)*3;

    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(m_me_tf_1_t)*3 + 10; // ASDU header + 3 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header = make_asdu_header(M_ME_TF_1, (cot_t){.cause = COT_SPONTANEOUS, .pn_bit = 0, .test_bit = 0}, config.originator_address, config.common_address, 3, 0); // Measured Value with Time Tag

    // Measured Value Object
    m_me_tf_1_t *m_me_tf_1 = (m_me_tf_1_t *)&pkt.data[DATA_START_IDX];

#ifdef IEC104_TEST
    cp56time2a_t timestamp = cp56time2a_make(43665, 18, 15, 14, 4, 8, 25);
#else
    cp56time2a_t timestamp = cp56time2a_now();
#endif

    for (int i = 0; i < 3; i++) 
    {
        m_me_tf_1[i].ioa = ioas[i];
        m_me_tf_1[i].value = currents[i];
        m_me_tf_1[i].quality = quality[i];
        m_me_tf_1[i].timestamp = timestamp;
    }

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}
 
void iec104_send_currents(const breaker_t *breaker)
{
    if(breaker == NULL) {
        CSLOG("Invalid breaker data\r\n");
        return;
    }

    for(int i = 0; i < MAX_POWER_LINE_COUNT; i++ )
    {
        if(breaker->line[i].iec104.in_use)
        {
            float currents[3];
            qds_t quality[3];

            for(int j = PHASE_R; j < PHASE_MAX; j++)
            {
            	if(iec_io.read_m_me_tf_1(breaker->line[i].iec104.anlik_akim[j], &currents[j], &quality[j]) != 0)
            	{
					CSLOG("Failed to read current for line %d, phase %d\r\n", i, j);
					currents[j] = 0.0f; // Default value on read failure
					quality[j].invalid = 1; // Mark quality as invalid
            	}
			}

            CSLOG("Sending Currents for line %d: I1: %.2f, I2: %.2f, I3: %.2f\r\n",
                   i, currents[0], currents[1], currents[2]);

            iec104_send_phase_currents(breaker->line[i].iec104.m_me_tf_1_ioa, currents, quality);
        }
    }
}
#endif
 
static void send_breaker_states(const ioa_3byte_t *ioas, const siq_t *siqs)
{
    iec104_package_t pkt;

    const size_t type_id_length = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + sizeof(m_sp_tb_1_t)*3;

    memset(&pkt, 0, type_id_length);

    pkt.frame.apci.start_char = IEC104_START_BYTE;
    pkt.frame.apci.apdu_length = sizeof(m_sp_tb_1_t)*3 + 10; // ASDU header + 3 object
    pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
    pkt.frame.asdu_header = make_asdu_header(M_SP_TB_1, (cot_t){.cause = COT_SPONTANEOUS, .pn_bit = 0, .test_bit = 0}, config.originator_address, config.common_address, 3, 0);

    m_sp_tb_1_t *m_sp_tb_1 = (m_sp_tb_1_t *)&pkt.data[DATA_START_IDX];

#ifdef IEC104_TEST
    cp56time2a_t timestamp = cp56time2a_make(29295, 17, 15, 14, 4, 8, 25);
#else
    cp56time2a_t timestamp = cp56time2a_now();
#endif

    for (int i = 0; i < 3; i++) {
        m_sp_tb_1[i].ioa = ioas[i];
        m_sp_tb_1[i].siq = siqs[i]; // Single Point Information with Quality descriptor
        m_sp_tb_1[i].timestamp = timestamp;
    }

    iec104_send((uint8_t *)&pkt, type_id_length + 2); // +2 for start char and length byte
}

#if 0
void iec104_send_breaker_states(const breaker_t *breaker)
{
    if(breaker == NULL) {
        CSLOG("Invalid breaker data\r\n");
        return;
    }

    for(int i = 0; i < MAX_POWER_LINE_COUNT; i++ )
    {
        if(breaker->line[i].iec104.in_use)
        {
            siq_t siq[3] = {0};

            for(int j = PHASE_R; j < PHASE_MAX; j++)
            {
				if(iec_io.read_m_sp_tb_1(breaker->line[i].iec104.m_sp_tb_1_ioa[PHASE_R], &siq[0]))
				{
					CSLOG("Failed to read breaker state for line %d, phase %d\r\n", i, j);
					siq[j].invalid = 1; // Mark quality as invalid
				}
            }

            CSLOG("Sending breaker states for line %d: R: %d, S: %d, T: %d\r\n",
                   i, siq[0], siq[1], siq[2]);

            send_breaker_states(breaker->line[i].iec104.m_sp_tb_1_ioa, siq);
        }
    }
}
#endif
 

void iec104_send_phase_currents(cause_of_transmission_t cause)
{
    CSLOG("Sending phase currents\r\n");

    uint8_t active_powerline_count = breaker_get_active_powerline_count();

    if (active_powerline_count == 0)
    {
        CSLOG("No active power lines, skipping\r\n");
        return;
    }
    // Max APDU(255) - 1StartByte - 1LengthByte - 4APCI - 6ASDU header --> 243 byte for objects, payload maks
    // IEC 104 APDU max 253 byte, ASDU header 10 byte
    // Max obje sayisi: (253 - 10) / sizeof(m_me_tf_1_t)
    const uint8_t max_objs_per_packet = (253 - 10) / sizeof(m_me_tf_1_t);

    // (maks 8 hat * 3 faz = 24 obje)
    m_me_tf_1_t objects[MAX_POWER_LINE_COUNT * 3];
    uint8_t obj_count = 0;

    for (int power_line = 0; power_line < MAX_POWER_LINE_COUNT; power_line++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(power_line);

        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for (int phase = 0; phase < 3; phase++)
        {
            float value;
            qds_t quality;
            cp56time2a_t timestamp;

            iec_io.get_anlik_akim(power_line, phase, &value, &quality, &timestamp);

            objects[obj_count].ioa = line->iec104.anlik_akim[phase];
            objects[obj_count].value = value;
            objects[obj_count].quality = quality;
            objects[obj_count].timestamp = timestamp;
            obj_count++;
        }
    }

    // Paketlere bol ve gonder
    uint8_t sent = 0;
    while (sent < obj_count)
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) {
            batch = max_objs_per_packet;
        }

        iec104_package_t pkt;
        const uint8_t apdu_len = (sizeof(m_me_tf_1_t) * batch) + 10; // ASDU header + batch * object size
        const size_t total_len = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + (sizeof(m_me_tf_1_t) * batch); // Start char ve length byte APDU uzunluguna dahil edilmez

        memset(&pkt, 0, total_len);

        pkt.frame.apci.start_char = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header = make_asdu_header(M_ME_TF_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_me_tf_1_t) * batch);

        CSLOG("Sending phase currents: %d objects (sent: %d/%d)\r\n", batch, sent + batch, obj_count);
        iec104_send((uint8_t *)&pkt, total_len + 2);

        sent += batch;
    }
}

void iec104_send_fault_currents(cause_of_transmission_t cause)
{
    CSLOG("Sending fault currents\r\n");

    uint8_t active_powerline_count = breaker_get_active_powerline_count();

    if (active_powerline_count == 0)
    {
        CSLOG("No active power lines, skipping\r\n");
        return;
    }

    // IEC 104 APDU max 253 byte, ASDU header 10 byte
    // Max obje sayisi: (253 - 10) / sizeof(m_me_tf_1_t)
    const uint8_t max_objs_per_packet = (253 - 10) / sizeof(m_me_tf_1_t);

    // (maks 8 hat * 3 faz = 24 obje)
    m_me_tf_1_t objects[MAX_POWER_LINE_COUNT * 3];
    uint8_t obj_count = 0;

    for (int power_line = 0; power_line < MAX_POWER_LINE_COUNT; power_line++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(power_line);

        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for (int phase = 0; phase < 3; phase++)
        {
            float value;
            qds_t quality;
            cp56time2a_t timestamp;

            iec_io.get_ariza_akimi(power_line, phase, &value, &quality, &timestamp);

            objects[obj_count].ioa = line->iec104.ariza_akimi[phase];
            objects[obj_count].value = value;
            objects[obj_count].quality = quality;
            objects[obj_count].timestamp = timestamp;
            obj_count++;
        }
    }

    // Paketlere bol ve gonder
    uint8_t sent = 0;
    while (sent < obj_count)
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) {
            batch = max_objs_per_packet;
        }

        iec104_package_t pkt;
        const uint8_t apdu_len = (sizeof(m_me_tf_1_t) * batch) + 10; // ASDU header + batch * object size
        const size_t total_len = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + (sizeof(m_me_tf_1_t) * batch); // Start char ve length byte APDU uzunluguna dahil edilmez

        memset(&pkt, 0, total_len);

        pkt.frame.apci.start_char = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header = make_asdu_header(M_ME_TF_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_me_tf_1_t) * batch);

        CSLOG("Sending fault currents: %d objects (sent: %d/%d)\r\n", batch, sent + batch, obj_count);
        iec104_send((uint8_t *)&pkt, total_len + 2);

        sent += batch;
    }
}

void iec104_send_fault_durations(cause_of_transmission_t cause)
{
    CSLOG("Sending fault durations\r\n");

    uint8_t active_powerline_count = breaker_get_active_powerline_count();

    if (active_powerline_count == 0)
    {
        CSLOG("No active power lines, skipping\r\n");
        return;
    }

    // IEC 104 APDU max 253 byte, ASDU header 10 byte
    // Max obje sayisi: (253 - 10) / sizeof(m_me_tf_1_t)
    const uint8_t max_objs_per_packet = (253 - 10) / sizeof(m_me_tf_1_t);

    // (maks 8 hat * 3 faz = 24 obje)
    m_me_tf_1_t objects[MAX_POWER_LINE_COUNT * 3];
    uint8_t obj_count = 0;

    for (int power_line = 0; power_line < MAX_POWER_LINE_COUNT; power_line++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(power_line);

        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for (int phase = 0; phase < 3; phase++)
        {
            float value;
            qds_t quality;
            cp56time2a_t timestamp;

            iec_io.get_ariza_suresi(power_line, phase, &value, &quality, &timestamp);

            objects[obj_count].ioa = line->iec104.ariza_suresi[phase];
            objects[obj_count].value = value;
            objects[obj_count].quality = quality;
            objects[obj_count].timestamp = timestamp;
            obj_count++;
        }
    }

    // Paketlere bol ve gonder
    uint8_t sent = 0;
    while (sent < obj_count)
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) {
            batch = max_objs_per_packet;
        }

        iec104_package_t pkt;
        const uint8_t apdu_len = (sizeof(m_me_tf_1_t) * batch) + 10; // ASDU header + batch * object size
        const size_t total_len = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + (sizeof(m_me_tf_1_t) * batch); // Start char ve length byte APDU uzunluguna dahil edilmez

        memset(&pkt, 0, total_len);

        pkt.frame.apci.start_char = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header = make_asdu_header(M_ME_TF_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_me_tf_1_t) * batch);

        CSLOG("Sending fault durations: %d objects (sent: %d/%d)\r\n", batch, sent + batch, obj_count);
        iec104_send((uint8_t *)&pkt, total_len + 2);

        sent += batch;
    }
}

void iec104_send_fault_types(cause_of_transmission_t cause)
{
    CSLOG("Sending fault types\r\n");

    uint8_t active_powerline_count = breaker_get_active_powerline_count();

    if (active_powerline_count == 0)
    {
        CSLOG("No active power lines, skipping\r\n");
        return;
    }

    // IEC 104 APDU max 253 byte, ASDU header 10 byte
    // Max obje sayisi: (253 - 10) / sizeof(m_sp_tb_1_t)
    const uint8_t max_objs_per_packet = (253 - 10) / sizeof(m_sp_tb_1_t);

    // (maks 8 hat * 3 faz = 24 obje)
    m_sp_tb_1_t objects[MAX_POWER_LINE_COUNT * 3];
    uint8_t obj_count = 0;

    for (int power_line = 0; power_line < MAX_POWER_LINE_COUNT; power_line++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(power_line);

        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for (int phase = 0; phase < 3; phase++)
        {
        	siq_t siq;
            cp56time2a_t timestamp;

            iec_io.get_ariza_kalicimi(power_line, phase, &siq, &timestamp);

            objects[obj_count].ioa = line->iec104.ariza_kalicimi[phase];
            objects[obj_count].siq = siq;
            objects[obj_count].timestamp = timestamp;
            obj_count++;
        }
    }

    // Paketlere bol ve gonder
    uint8_t sent = 0;
    while (sent < obj_count)
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) {
            batch = max_objs_per_packet;
        }

        iec104_package_t pkt;
        const uint8_t apdu_len = (sizeof(m_sp_tb_1_t) * batch) + 10; // ASDU header + batch * object size
        const size_t total_len = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + (sizeof(m_sp_tb_1_t) * batch); // Start char ve length byte APDU uzunluguna dahil edilmez

        memset(&pkt, 0, total_len);

        pkt.frame.apci.start_char = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header = make_asdu_header(M_SP_TB_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_sp_tb_1_t) * batch);

        CSLOG("Sending fault types: %d objects (sent: %d/%d)\r\n", batch, sent + batch, obj_count);
        iec104_send((uint8_t *)&pkt, total_len + 2);

        sent += batch;
    }
}

void iec104_send_energy_states(cause_of_transmission_t cause)
{
    CSLOG("Sending energy states\r\n");

    uint8_t active_powerline_count = breaker_get_active_powerline_count();

    if (active_powerline_count == 0)
    {
        CSLOG("No active power lines, skipping\r\n");
        return;
    }

    // IEC 104 APDU max 253 byte, ASDU header 10 byte
    // Max obje sayisi: (253 - 10) / sizeof(m_sp_tb_1_t)
    const uint8_t max_objs_per_packet = (253 - 10) / sizeof(m_sp_tb_1_t);

    // (maks 8 hat * 3 faz = 24 obje)
    m_sp_tb_1_t objects[MAX_POWER_LINE_COUNT * 3];
    uint8_t obj_count = 0;

    for (int power_line = 0; power_line < MAX_POWER_LINE_COUNT; power_line++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(power_line);

        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for (int phase = 0; phase < 3; phase++)
        {
        	siq_t siq;
            cp56time2a_t timestamp;

            iec_io.get_enerji_varyok(power_line, phase, &siq, &timestamp);

            objects[obj_count].ioa = line->iec104.enerji_varyok[phase];
            objects[obj_count].siq = siq;
            objects[obj_count].timestamp = timestamp;
            obj_count++;
        }
    }

    // Paketlere bol ve gonder
    uint8_t sent = 0;
    while (sent < obj_count)
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) {
            batch = max_objs_per_packet;
        }

        iec104_package_t pkt;
        const uint8_t apdu_len = (sizeof(m_sp_tb_1_t) * batch) + 10; // ASDU header + batch * object size
        const size_t total_len = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + (sizeof(m_sp_tb_1_t) * batch); // Start char ve length byte APDU uzunluguna dahil edilmez

        memset(&pkt, 0, total_len);

        pkt.frame.apci.start_char = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header = make_asdu_header(M_SP_TB_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_sp_tb_1_t) * batch);

        CSLOG("Sending energy states: %d objects (sent: %d/%d)\r\n", batch, sent + batch, obj_count);
        iec104_send((uint8_t *)&pkt, total_len + 2);

        sent += batch;
    }
}

void iec104_send_nominal_current_states(cause_of_transmission_t cause)
{
    CSLOG("Sending nominal current states\r\n");

    uint8_t active_powerline_count = breaker_get_active_powerline_count();

    if (active_powerline_count == 0)
    {
        CSLOG("No active power lines, skipping\r\n");
        return;
    }

/*
    |<-- Sabit Header -->|<----------- APCI ----------->|<-------------- ASDU Header ------------->|<------------------- ASDU Veri (Information Objects) -------------->|
    +-------+------------+----+----+----+----+----------+----+-----+-----+------------+------------+-----------+-----------+     +-----------+---------------------------+
    | START |   LENGTH   | I1 | I2 | I3 | I4 | TYPE ID  |VSQ | COT | COT | ASDU ADDR  | ASDU ADDR  |  OBJE 1   |  OBJE 2   | ... |  OBJE 22  | BOŞ / ARTIK ALAN          |
    | (0x68)| (Max 253)  |    |    |    |    | (M_SP_TB)|(22)| (L) | (H) |    (L)     |    (H)     | (11 Byte) | (11 Byte) |     | (11 Byte) | (Max 1 Byte)              |
    +-------+------------+----+----+----+----+----------+----+-----+-----+------------+------------+-----------+-----------+     +-----------+---------------------------+
    | 1 Byte|   1 Byte   |      4 Byte       |  1 Byte  | 1B |  1B |  1B |   1 Byte   |   1 Byte   | <------- 11 Byte ------>     <-- 11 B -->| 243 - (22*11) = 1 Byte   |
    +-------+------------+-------------------+----------+----+-----+-----+------------+------------+-----------------------+     +-----------+---------------------------+
            |                                |<---------------------------------------  Length (Toplam 253 Byte)  ------------------------------------------------------>|
            |<--------------------------------------------------------- APDU (Toplam 255 Byte) ------------------------------------------------------------------------->|
*/
    // Toplam APDU uzunluk siniri (Start byte haric)
    const uint8_t MAX_APDU_LEN = 253;
    // Protokol Overhead (APCI 4 byte + ASDU Header 6 byte)
    const uint8_t PROTOCOL_OVERHEAD = 10;
    // Kullanilabilir alan: 243 byte
    const uint8_t MAX_DATA_SPACE = MAX_APDU_LEN - PROTOCOL_OVERHEAD;
    const uint8_t max_objs_per_packet = MAX_DATA_SPACE / sizeof(m_sp_tb_1_t);

    // (maks 8 hat * 3 faz = 24 obje)
    m_sp_tb_1_t objects[MAX_POWER_LINE_COUNT * 3];
    uint8_t obj_count = 0;

    for (int power_line = 0; power_line < MAX_POWER_LINE_COUNT; power_line++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(power_line);

        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for (int phase = 0; phase < 3; phase++)
        {
        	siq_t siq;
            cp56time2a_t timestamp;

            iec_io.get_nominal_akim_varyok(power_line, phase, &siq, &timestamp);

            objects[obj_count].ioa = line->iec104.nominal_akim_varyok[phase];
            objects[obj_count].siq = siq;
            objects[obj_count].timestamp = timestamp;
            obj_count++;
        }
    }

    // Paketlere bol ve gonder
    uint8_t sent = 0;
    while (sent < obj_count)
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) {
            batch = max_objs_per_packet;
        }

        iec104_package_t pkt = {0};
        uint8_t data_len = batch * sizeof(m_sp_tb_1_t);
        // APDU  = APCI_Kontrol(4) + ASDU_Header(6) + Veri (Max 253)  // Start char ve length byte APDU uzunluguna dahil edilmez
        uint8_t apdu_len = 4 + 6 + data_len;

        pkt.frame.apci.start_char = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header = make_asdu_header(M_SP_TB_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_sp_tb_1_t) * batch);

        CSLOG("Sending nominal current states: %d objects (sent: %d/%d)\r\n", batch, sent + batch, obj_count);
        iec104_send((uint8_t *)&pkt, apdu_len + 2);

        sent += batch;
    }
}

void iec104_send_rf_communication_states(cause_of_transmission_t cause)
{
    CSLOG("Sending RF communication states\r\n");

    uint8_t active_powerline_count = breaker_get_active_powerline_count();

    if (active_powerline_count == 0)
    {
        CSLOG("No active power lines, skipping\r\n");
        return;
    }

    // IEC 104 APDU max 253 byte, ASDU header 10 byte
    // Max obje sayisi: (253 - 10) / sizeof(m_sp_tb_1_t)
    const uint8_t max_objs_per_packet = (253 - 10) / sizeof(m_sp_tb_1_t);

    // (maks 8 hat * 3 faz = 24 obje)
    m_sp_tb_1_t objects[MAX_POWER_LINE_COUNT * 3];
    uint8_t obj_count = 0;

    for (int power_line = 0; power_line < MAX_POWER_LINE_COUNT; power_line++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(power_line);

        if (line == NULL || !line->iec104.in_use) {
            continue;
        }

        for (int phase = 0; phase < 3; phase++)
        {
        	siq_t siq;
            cp56time2a_t timestamp;

            iec_io.get_rf_haberlesme_varyok(power_line, phase, &siq, &timestamp);

            objects[obj_count].ioa = line->iec104.rf_haberlesme_varyok[phase];
            objects[obj_count].siq = siq;
            objects[obj_count].timestamp = timestamp;
            obj_count++;
        }
    }

    // Paketlere bol ve gonder
    uint8_t sent = 0;
    while (sent < obj_count)
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) {
            batch = max_objs_per_packet;
        }

        iec104_package_t pkt;
        const uint8_t apdu_len = (sizeof(m_sp_tb_1_t) * batch) + 10; // ASDU header + batch * object size
        const size_t total_len = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t) + (sizeof(m_sp_tb_1_t) * batch); // Start char ve length byte APDU uzunluguna dahil edilmez

        memset(&pkt, 0, total_len);

        pkt.frame.apci.start_char = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header = make_asdu_header(M_SP_TB_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_sp_tb_1_t) * batch);

        CSLOG("Sending RF communication states: %d objects (sent: %d/%d)\r\n", batch, sent + batch, obj_count);
        iec104_send((uint8_t *)&pkt, total_len + 2);

        sent += batch;
    }
}


/* ---------------------------------------------------------------
 * Fault gonderim engine'leri
 * Tum 8 "iec104_send_feeder_xxx_faults_yyy" public fonksiyonu
 * bu 2 static engine üzerinden calisir.
 * --------------------------------------------------------------- */

typedef enum 
{
    FAULT_ME_FIELD_ARIZA_AKIMI,
    FAULT_ME_FIELD_ARIZA_SURESI,
} fault_me_field_t;

typedef enum 
{
    FAULT_SP_FIELD_NOMINAL_AKIM,
    FAULT_SP_FIELD_ENERJI_VARYOK,
} fault_sp_field_t;

/* Engine: M_ME_TF_1 (olculen float deger + timestamp) */
static void send_fault_me_tf_1(uint8_t feeder_id, phase_id_t phase, fault_log_type_t log_type, fault_me_field_t field,
    cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    uint8_t max_objs_per_packet = (253 - 10) / sizeof(m_me_tf_1_t);
    if (max_objs_per_packet > TEMPORARY_FAULT_COUNT) {
        max_objs_per_packet = TEMPORARY_FAULT_COUNT;
    }

    m_me_tf_1_t objects[TEMPORARY_FAULT_COUNT * 3];
    uint8_t obj_count = 0;

    phase_id_t start_phase = (phase == PHASE_ALL) ? PHASE_L1 : phase;
    phase_id_t end_phase   = (phase == PHASE_ALL) ? PHASE_MAX : (phase_id_t)(phase + 1);

    for (phase_id_t phase_id = start_phase; phase_id < end_phase; phase_id++) 
    {
        uint8_t fault_count = (log_type == FAULT_LOG_TYPE_TEMPORARY)
            ? fault_log_get_temp_count(feeder_id, phase_id)
            : fault_log_get_perm_count(feeder_id, phase_id);

        fault_log_t log;
        for (int index = 0; index < fault_count; index++) 
        {
            if (!fault_log_read_nth(feeder_id, phase_id, log_type, index, &log)) {
                continue;
            }

            ioa_3byte_t ioa;
            if (log_type == FAULT_LOG_TYPE_TEMPORARY) 
            {
                ioa = (field == FAULT_ME_FIELD_ARIZA_AKIMI)
                    ? iec104_get_feeder_temporary_fault_ariza_akimi_ioa(feeder_id, phase_id, index)
                    : iec104_get_feeder_temporary_fault_ariza_suresi_ioa(feeder_id, phase_id, index);
            } 
            else 
            {
                ioa = (field == FAULT_ME_FIELD_ARIZA_AKIMI)
                    ? iec104_get_feeder_permanent_fault_ariza_akimi_ioa(feeder_id, phase_id, index)
                    : iec104_get_feeder_permanent_fault_ariza_suresi_ioa(feeder_id, phase_id, index);
            }

            objects[obj_count].ioa       = ioa;
            objects[obj_count].value     = (field == FAULT_ME_FIELD_ARIZA_AKIMI)
                                           ? log.fault_current
                                           : (float)log.fault_duration_ms;
            objects[obj_count].quality   = (qds_t){0};
            objects[obj_count].timestamp = log.tm;
            obj_count++;
        }
    }

    uint16_t buff_written = 0;
    uint8_t sent = 0;
    while (sent < obj_count)
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) { 
            batch = max_objs_per_packet; 
        }

        iec104_package_t pkt = {0};
        const uint8_t apdu_len  = (sizeof(m_me_tf_1_t) * batch) + 10;
        const size_t  total_len = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t)
                                  + (sizeof(m_me_tf_1_t) * batch);

        pkt.frame.apci.start_char  = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame     = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header      = make_asdu_header(M_ME_TF_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        if (buff != NULL && len != NULL) 
        {
            pkt.frame.apci.i_frame.send_seq++;
            send_sn = (send_sn + 1) % 32768;
        }

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_me_tf_1_t) * batch);
        CSLOG("Sending fault ME_TF_1 (field=%d, type=%d): %d objs (sent %d/%d)\r\n",
              field, log_type, batch, sent + batch, obj_count);

        if (buff != NULL && len != NULL) 
        {
            if (buff_written + total_len + 2 > max_len)
            {
                CSLOG("Not enough buffer space to write the packet\r\n");
                return;
            }
            memcpy(&buff[buff_written], &pkt, total_len + 2);
            buff_written += total_len + 2;
            *len = buff_written;
        } 
        else 
        {
            iec104_send((uint8_t *)&pkt, total_len + 2);
        }
        sent += batch;
    }

    if (len != NULL) { 
        *len = buff_written; 
    }
}

/* Engine: M_SP_TB_1 (tek-bit durum + timestamp) */
static void send_fault_sp_tb_1(uint8_t feeder_id, phase_id_t phase, fault_log_type_t log_type, fault_sp_field_t field,
    cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    uint8_t max_objs_per_packet = (253 - 10) / sizeof(m_sp_tb_1_t);
    if (max_objs_per_packet > TEMPORARY_FAULT_COUNT) {
        max_objs_per_packet = TEMPORARY_FAULT_COUNT;
    }

    m_sp_tb_1_t objects[TEMPORARY_FAULT_COUNT * 3];
    uint8_t obj_count = 0;

    phase_id_t start_phase = (phase == PHASE_ALL) ? PHASE_L1 : phase;
    phase_id_t end_phase   = (phase == PHASE_ALL) ? PHASE_MAX : (phase_id_t)(phase + 1);

    for (phase_id_t phase_id = start_phase; phase_id < end_phase; phase_id++) 
    {
        uint8_t fault_count = (log_type == FAULT_LOG_TYPE_TEMPORARY)
            ? fault_log_get_temp_count(feeder_id, phase_id)
            : fault_log_get_perm_count(feeder_id, phase_id);

        fault_log_t log;
        for (int index = 0; index < fault_count; index++) 
        {
            if (!fault_log_read_nth(feeder_id, phase_id, log_type, index, &log)) {
                continue;
            }

            ioa_3byte_t ioa;
            if (log_type == FAULT_LOG_TYPE_TEMPORARY)
            {
                ioa = (field == FAULT_SP_FIELD_NOMINAL_AKIM)
                    ? iec104_get_feeder_temporary_fault_nominal_akim_varyok_ioa(feeder_id, phase_id, index)
                    : iec104_get_feeder_temporary_fault_enerji_varyok_ioa(feeder_id, phase_id, index);
            } 
            else 
            {
                ioa = (field == FAULT_SP_FIELD_NOMINAL_AKIM)
                    ? iec104_get_feeder_permanent_fault_nominal_akim_varyok_ioa(feeder_id, phase_id, index)
                    : iec104_get_feeder_permanent_fault_enerji_varyok_ioa(feeder_id, phase_id, index);
            }

            objects[obj_count].ioa       = ioa;
            objects[obj_count].siq       = (siq_t){.spi = (field == FAULT_SP_FIELD_NOMINAL_AKIM)
                                           ? log.info.nominal_current_status
                                           : log.info.power_status};
            objects[obj_count].timestamp = log.tm;
            obj_count++;
        }
    }

    uint16_t buff_written = 0;
    uint8_t sent = 0;
    while (sent < obj_count) 
    {
        uint8_t batch = obj_count - sent;
        if (batch > max_objs_per_packet) { 
            batch = max_objs_per_packet; 
        }

        iec104_package_t pkt = {0};
        const uint8_t apdu_len  = (sizeof(m_sp_tb_1_t) * batch) + 10;
        const size_t  total_len = (sizeof(apci_header_t) - 2) + sizeof(asdu_header_t)
                                  + (sizeof(m_sp_tb_1_t) * batch);

        pkt.frame.apci.start_char  = IEC104_START_BYTE;
        pkt.frame.apci.apdu_length = apdu_len;
        pkt.frame.apci.i_frame     = make_iframe_control(send_sn, receive_sn);
        pkt.frame.asdu_header      = make_asdu_header(M_SP_TB_1,
            (cot_t){.cause = cause, .pn_bit = 0, .test_bit = 0},
            config.originator_address, config.common_address, batch, 0);

        if (buff != NULL && len != NULL) 
        {
            pkt.frame.apci.i_frame.send_seq++;
            send_sn = (send_sn + 1) % 32768;
        }

        memcpy(&pkt.data[DATA_START_IDX], &objects[sent], sizeof(m_sp_tb_1_t) * batch);
        CSLOG("Sending fault SP_TB_1 (field=%d, type=%d): %d objs (sent %d/%d)\r\n",
              field, log_type, batch, sent + batch, obj_count);

        if (buff != NULL && len != NULL) 
        {
            if (buff_written + total_len + 2 > max_len) 
            {
                CSLOG("Not enough buffer space to write the packet\r\n");
                return;
            }
            memcpy(&buff[buff_written], &pkt, total_len + 2);
            buff_written += total_len + 2;
            *len = buff_written;
        }
        else 
        {
            iec104_send((uint8_t *)&pkt, total_len + 2);
        }
        sent += batch;
    }

    if (len != NULL) { *len = buff_written; }
}

/* ---------------------------------------------------------------
 * Public wrapper'lar — temporary
 * --------------------------------------------------------------- */
void iec104_send_feeder_temporary_faults_ariza_akimi(uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    send_fault_me_tf_1(feeder_id, phase, FAULT_LOG_TYPE_TEMPORARY, FAULT_ME_FIELD_ARIZA_AKIMI, cause, buff, len, max_len);
}

void iec104_send_feeder_temporary_faults_ariza_suresi(uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    send_fault_me_tf_1(feeder_id, phase, FAULT_LOG_TYPE_TEMPORARY, FAULT_ME_FIELD_ARIZA_SURESI, cause, buff, len, max_len);
}

void iec104_send_feeder_temporary_faults_nominal_akimvaryok(uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    send_fault_sp_tb_1(feeder_id, phase, FAULT_LOG_TYPE_TEMPORARY, FAULT_SP_FIELD_NOMINAL_AKIM, cause, buff, len, max_len);
}

void iec104_send_feeder_temporary_faults_enerji_varyok(uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    send_fault_sp_tb_1(feeder_id, phase, FAULT_LOG_TYPE_TEMPORARY, FAULT_SP_FIELD_ENERJI_VARYOK, cause, buff, len, max_len);
}

void iec104_send_feeder_temporary_faults(cause_of_transmission_t cause)
{
    for(int feeder_id = 0; feeder_id < MAX_POWER_LINE_COUNT; feeder_id++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(feeder_id);
        if (line == NULL || !line->iec104.in_use) {
            continue;
        }
        iec104_send_feeder_temporary_faults_ariza_akimi(feeder_id, PHASE_ALL, cause, NULL, NULL, 0);
        iec104_send_feeder_temporary_faults_ariza_suresi(feeder_id, PHASE_ALL, cause, NULL, NULL, 0);
        iec104_send_feeder_temporary_faults_nominal_akimvaryok(feeder_id, PHASE_ALL, cause, NULL, NULL, 0);
        iec104_send_feeder_temporary_faults_enerji_varyok(feeder_id, PHASE_ALL, cause, NULL, NULL, 0);
    }
}

void iec104_get_feeder_temporary_faults(uint8_t *buff, uint16_t *len, uint16_t max_len, uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause)
{
    uint16_t written = 0;

    iec104_send_feeder_temporary_faults_ariza_akimi(feeder_id, phase, cause, &buff[*len], &written, max_len - *len); //711 byte 3 faz icin
    *len += written;  written = 0;

    iec104_send_feeder_temporary_faults_ariza_suresi(feeder_id, phase, cause, &buff[*len], &written, max_len - *len); //711 byte 3 faz icin
    *len += written;  written = 0;

    iec104_send_feeder_temporary_faults_nominal_akimvaryok(feeder_id, phase, cause, &buff[*len], &written, max_len - *len); // 531 byte 3 faz icin
    *len += written;  written = 0;

    iec104_send_feeder_temporary_faults_enerji_varyok(feeder_id, phase, cause, &buff[*len], &written, max_len - *len); // 531 byte 3 faz icin
    *len += written;
}

/* ---------------------------------------------------------------
 * Public wrapper'lar — permanent
 * --------------------------------------------------------------- */


void iec104_send_feeder_permenent_faults_ariza_akimi(uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    send_fault_me_tf_1(feeder_id, phase, FAULT_LOG_TYPE_PERMANENT, FAULT_ME_FIELD_ARIZA_AKIMI, cause, buff, len, max_len);
}

void iec104_send_feeder_permenent_faults_ariza_suresi(uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    send_fault_me_tf_1(feeder_id, phase, FAULT_LOG_TYPE_PERMANENT, FAULT_ME_FIELD_ARIZA_SURESI, cause, buff, len, max_len);
}

void iec104_send_feeder_permenent_faults_nominal_akimvaryok(uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    send_fault_sp_tb_1(feeder_id, phase, FAULT_LOG_TYPE_PERMANENT, FAULT_SP_FIELD_NOMINAL_AKIM, cause, buff, len, max_len);
}

void iec104_send_feeder_permenent_faults_enerji_varyok(uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause, uint8_t *buff, uint16_t *len, uint16_t max_len)
{
    send_fault_sp_tb_1(feeder_id, phase, FAULT_LOG_TYPE_PERMANENT, FAULT_SP_FIELD_ENERJI_VARYOK, cause, buff, len, max_len);
}

void iec104_send_feeder_permanent_faults(cause_of_transmission_t cause)
{
    for(int feeder_id = 0; feeder_id < MAX_POWER_LINE_COUNT; feeder_id++)
    {
        const power_line_t *line = breaker_get_power_line_by_idx(feeder_id);
        if(line == NULL || !line->iec104.in_use){ continue; }
        iec104_send_feeder_permenent_faults_ariza_akimi(feeder_id, PHASE_ALL, cause, NULL, NULL, 0);
        iec104_send_feeder_permenent_faults_ariza_suresi(feeder_id, PHASE_ALL, cause, NULL, NULL, 0);
        iec104_send_feeder_permenent_faults_nominal_akimvaryok(feeder_id, PHASE_ALL, cause, NULL, NULL, 0);
        iec104_send_feeder_permenent_faults_enerji_varyok(feeder_id, PHASE_ALL, cause, NULL, NULL, 0);
    }
}

void iec104_get_feeder_permanent_faults(uint8_t *buff, uint16_t *len, uint16_t max_len, uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause)
{
    uint16_t written = 0;

    iec104_send_feeder_permenent_faults_ariza_akimi(feeder_id, phase, cause, &buff[*len], &written, max_len - *len);
    *len += written;  written = 0;

    iec104_send_feeder_permenent_faults_ariza_suresi(feeder_id, phase, cause, &buff[*len], &written, max_len - *len);
    *len += written;  written = 0;

    iec104_send_feeder_permenent_faults_nominal_akimvaryok(feeder_id, phase, cause, &buff[*len], &written, max_len - *len);
    *len += written;  written = 0;

    iec104_send_feeder_permenent_faults_enerji_varyok(feeder_id, phase, cause, &buff[*len], &written, max_len - *len);
    *len += written;
}
















/* ========================================================= */

void iec104_send_periodic_update(void)
{
    CSLOG("Sending periodic update\r\n");
    //iec104_send_all_phase_currents();
    //iec104_send_all_breaker_states();
}
