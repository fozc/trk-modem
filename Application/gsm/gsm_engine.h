/*
 * gsm_engine.h
 *
 *  Created on: 20 Mar 2018
 *      Author: fozcan
 */

#ifndef GSM_ENGINE_H_
#define GSM_ENGINE_H_

#include <gsm_types.h>

//at_cmd_t at_cmd[] =
//{
//		[ATCMD_AT]      = {"",          gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Cevap OK */
//		[ATCMD_ATE0]    = {"E0",        gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Echo */
//		[ATCMD_ATF]     = {"&F1",       gsm_default_cb, GSM_OK_STR, 3, 3000}, /* Factory reset*/
//		[ATCMD_ATK]     = {"&K0",       gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Flow control off*/
//		[ATCMD_IPR]     = {"+IPR=9600", gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Set and fix baud rate*/
//		[ATCMD_CMEE]     = {"+CMEE=1",   gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Error codes on */
//		[ATCMD_SIMINCFG] = {"#SIMINCFG=6,0", gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Config simpin */
//		[ATCMD_SLED]     = {"#SLED=2", gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Enable and config status LED */
//		[ATCMD_CUSD]     = {"+CUSD=1,*101#",    gsm_CUSD_cb, GSM_OK_STR, 3, 5000}, /* Get subscriber number from network */
//		[ATCMD_CNUM]     = {"+CNUM",    gsm_default_cb, GSM_OK_STR, 3, 1500}, /*  Get subscriber number from simcard*/
//		[ATCMD_FCLASS]    = {"+FCLASS=0",    gsm_default_cb, GSM_OK_STR, 3, 1500},
//		[ATCMD_MSCLASS]    = {"#MSCLASS=1,1",    gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Set GPRS Class */
//		[ATCMD_CREG]    = {"+CREG?",    gsm_CREG_cb, GSM_OK_STR, 3, 1500}, /* Get network state */
//		[ATCMD_CGREG]    = {"+CGREG?",    gsm_CGREG_cb, GSM_OK_STR, 3, 1500}, /* Get gprs network state */
//		[ATCMD_CGDCONT] = {"+CGDCONT",  gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Set APN */
//		[ATCMD_CPIN]    = {"+CPIN",    gsm_default_cb, GSM_OK_STR, 3, 1500},
//		[ATCMD_GET_CPIN]  = {"+CPIN?",    gsm_default_cb, GSM_OK_STR, 3, 1500},
//
//		[ATCMD_SCFGEXT]   = {"#SCFGEXT=1,1,0,0,0,0",    gsm_default_cb, GSM_OK_STR, 3, 1500},
//		[ATCMD_SCFGEXT2]  = {"#SCFGEXT2=1,0,0,0,0,2",    gsm_default_cb, GSM_OK_STR, 3, 1500},
//		[ATCMD_TCPMAXWIN] = {"#TCPMAXWIN=32768",    gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Socket config */
//		[ATCMD_SCFG_1th]  = {"#SCFG=1,1,1024,0,300,256",    gsm_default_cb, GSM_OK_STR, 3, 1500}, /* 1th Socket config */
//		[ATCMD_SCFG_2th]  = {"#SCFG=2,1,1024,0,300,256",    gsm_default_cb, GSM_OK_STR, 3, 1500}, /* 2th Socket config */
//		[ATCMD_FRWL]      = {"#FRWL=1,\"1.1.1.1\",\"0.0.0.0\"",    gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Firewall config. */
//
//		[ATCMD_SL_1]        = {"#SL",    gsm_SL_cd, GSM_OK_STR, 3, 1500}, /* Open socket in listening mode */
//		[ATCMD_SH_1]        = {"#SH",    gsm_SL_cd, GSM_OK_STR, 3, 3500}, /* Close Listener socket */
//		[ATCMD_SA_1]        = {"#SA",    gsm_SD_cd, GSM_OK_STR, 3, 145*1000}, /* Accept client connection */
//		[ATCMD_SS_1]        = {"#SS",    gsm_SS_cd, GSM_OK_STR, 3, 145*1000}, /* Check listner socket status */
//		[ATCMD_SD_2]        = {"#SD",    gsm_SD_cd, GSM_OK_STR, 3, 145*1000}, /* Open socket in dialer mode */
//
//
//		[ATCMD_SSENDEXT_1]  = {"#SSENDEXT=1",    gsm_default_cb, GSM_OK_STR, 3, 2000}, /* set socket to write mode */
//		[ATCMD_SSENDEXT_2]  = {"#SSENDEXT=2",    gsm_default_cb, GSM_OK_STR, 3, 2000}, /* set socket to write mode */
//		[ATCMD_SRECV_1]   = {"#SRECV=1,1024",    gsm_listener_socket_data, GSM_OK_STR, 3, 3000}, /* get socket data */
//		[ATCMD_SRECV_2]   = {"#SRECV=2,1024",    gsm_dialer_socket_data, GSM_OK_STR, 3, 3000}, /* get socket data */
//
//		[ATCMD_SGACT]     = {"#SGACT=1,1",    gsm_SRECV_cd, GSM_OK_STR, 3, 1500}, /* activate gprs contex */
//		[ATCMD_GET_SGACT] = {"#SGACT=?",    gsm_SGACT_cd, GSM_OK_STR, 3, 2000}, /* activate gprs contex */
//
//		[ATCMD_CSQ]  = {"+CSQ", gsm_CSQ_cb, GSM_OK_STR, 3, 1500}, /* Get signal quality */
//        [ATCMD_SIMDET]  = {"#SIMDET=2", gsm_default_cb, GSM_OK_STR, 3, 1500}, /* Set simcard detection mode */
//
//		[ATCMD_NTP]  = {"#NTP", gsm_NTP_cb, GSM_OK_STR, 3, 1500} /* Set simcard detection mode */
//
//};


enum
{
	GSM_FREE,
	GSM_WAITING_RESPONSE,
	GSM_RESPONSE_READY
};

enum
{
	ATCMD_AT,
	ATCMD_ATE0,
	ATCMD_ATF,
	ATCMD_ATK,
	ATCMD_IPR,
	ATCMD_CMEE,
	ATCMD_GET_CPIN,
	ATCMD_CPIN,
	ATCMD_CREG,
	ATCMD_CGREG,
	ATCMD_SLED,
	ATCMD_SIMINCFG,
	ATCMD_SIMDET,
	ATCMD_CGDCONT,
	ATCMD_CUSD,
	ATCMD_CNUM,
	ATCMD_FCLASS,
	ATCMD_MSCLASS,
	ATCMD_NTP,


	ATCMD_SCFGEXT,
	ATCMD_SCFGEXT2,
	ATCMD_TCPMAXWIN,
	ATCMD_SCFG_1th,
	ATCMD_SCFG_2th,
	ATCMD_FRWL,

	/* listener Socket */
	ATCMD_SL_1,       /* Open listener socket */
	ATCMD_SH_1,       /* Close socket */
	ATCMD_SA_1,       /* Accept connection */
	ATCMD_SS_1,	      /* Socket status */
	ATCMD_SRECV_1,
	ATCMD_SSENDEXT_1,
	/* Dialer Socket */
	ATCMD_SS_2,	      /* Open dialer socket */
	ATCMD_SD_2,
	ATCMD_SH_2,
	ATCMD_SSENDEXT_2,
	ATCMD_SRECV_2,

	ATCMD_SGACT,
	ATCMD_GET_SGACT,

	ATCMD_CSQ,

	ATCMD_END_OF_LIST
};

enum
{
	ATQUERY_AT,
	ATQUERY_FACTORY_RESET,
	ATQUERY_SET_FLOW_CONTROL,
	ATQUERY_SET_BAUDRATE,
	ATQUERY_SET_LOW_BAUDRATE,
	ATQUERY_DISABLE_ECHO,
	ATQUERY_SET_CMEE,
	ATQUERY_SET_SIMDETECT_PIN_CFG,
	ATQUERY_SET_SIMDETECT_MODE,
	ATQUERY_SET_STATUS_LED,
	ATQUERY_GET_MODULE_FW_VERSION,
	ATQUERY_GET_SIMCARD_STATUS,
	ATQUERY_SET_PIN_NO,
	ATQUERY_SET_SERVICE_CLASS,
	ATQUERY_SET_GPRS_CLASS,
	ATQUERY_SET_NETWORK_STATE_RES,
	ATQUERY_GET_VOICE_SMS_NETWORK_STATE,
	ATQUERY_GET_GPRS_NETWORK_STATE,
	ATQUERY_GET_LTE_NETWORK_STATE,
	ATQUERY_GET_PHONE_NUMBER,
	ATQUERY_SET_APN,
	ATQUERY_GET_CCED,


	ATQUERY_SOCKET_1_CONFIG_SCFG,
	ATQUERY_SOCKET_2_CONFIG_SCFG,
	ATQUERY_SOCKET_3_CONFIG_SCFG,

	ATQUERY_SOCKET_1_CONFIG_SCFGEXT,
	ATQUERY_SOCKET_2_CONFIG_SCFGEXT,
	ATQUERY_SOCKET_3_CONFIG_SCFGEXT,

	ATQUERY_SOCKET_1_CONFIG_SCFGEXT2,
	ATQUERY_SOCKET_2_CONFIG_SCFGEXT2,
	ATQUERY_SOCKET_3_CONFIG_SCFGEXT2,

	ATQUERY_SOCKET_1_CONFIG_SCFGEXT3,
	ATQUERY_SOCKET_2_CONFIG_SCFGEXT3,
	ATQUERY_SOCKET_3_CONFIG_SCFGEXT3,


	ATQUERY_SET_TCPWINDOW,

	ATQUERY_SET_FIREWALL,
	ATQUERY_ATTACH_GPRS,
	ATQUERY_ACTIVATE_GPRS,
	ATQUERY_GET_GPRS_STATUS,
	ATQUERY_GET_SIGNAL_QUALITY,
	ATQUERY_GET_EXT_SIGNAL_QUALITY,
	ATQUERY_GET_NTP_DATETIME,

	ATQUERY_GET_ALL_SOCKET_STATUS,
	ATQUERY_GET_ALL_SOCKET_INFO,   /* AT#SI — tum soketlerin bilgisi */

	/* IEC104 Socket */
	ATQUERY_GET_IEC104_LISTENER_SOCKET_INFO,
	ATQUERY_OPEN_IEC104_LISTENER_SOCKET,           /* Open listener socket */
	ATQUERY_CLOSE_IEC104_LISTENER_SOCKET,          /* Close socket */
	ATQUERY_ACCEPT_IEC104_LISTENER_CONN,           /* Accept connection */
	ATQUERY_GET_IEC104_LISTENER_SOCKET_STATUS,	    /* Socket status */
	ATQUERY_GET_IEC104_LISTENER_SOCKET_DATA,
	ATQUERY_SET_IEC104_LISTENER_SOCKET_DATA_MODE,
	ATQUERY_SEND_DATA_TO_IEC104_LISTENER_SOCKET,


	/* Trace Socket */
	ATQUERY_GET_TRACE_SOCKET_INFO,
	ATQUERY_OPEN_TRACE_SOCKET,           /* Open listener socket */
	ATQUERY_CLOSE_TRACE_SOCKET,          /* Close socket */
	ATQUERY_ACCEPT_TRACE_CONN,           /* Accept connection */
	ATQUERY_GET_TRACE_SOCKET_STATUS,	 /* Socket status */
	ATQUERY_GET_TRACE_SOCKET_DATA,
	ATQUERY_SET_TRACE_SOCKET_DATA_MODE,
	/* listener Socket */
	ATQUERY_GET_LISTENER_SOCKET_INFO,
	ATQUERY_OPEN_LISTENER_SOCKET,           /* Open listener socket */
	ATQUERY_CLOSE_LISTENER_SOCKET,          /* Close socket */
	ATQUERY_ACCEPT_LISTENER_CONN,           /* Accept connection */
	ATQUERY_GET_LISTENER_SOCKET_STATUS,	    /* Socket status */
	ATQUERY_GET_LISTENER_SOCKET_DATA,
	ATQUERY_SET_LISTENER_SOCKET_DATA_MODE,
	/* Dialer Socket */
	ATQUERY_GET_DIALER_SOCKET_INFO,
	ATQUERY_CONN_TO_SERVER,
	ATQUERY_CLOSE_DIALER_SOCKET,
	ATQUERY_GET_DIALER_SOCKET_STATUS,
	ATQUERY_SET_DIALER_SOCKET_DATA_MODE,
	ATQUERY_GET_DIALER_SOCKET_DATA,

	ATQUERY_SEND_DATA_TO_DIALER_SOCKET,
	ATQUERY_SEND_DATA_TO_LISTENER_SOCKET,
	ATQUERY_SEND_DATA_TO_TRACE_SOCKET,



	ATQUERY_SET_HTTP_CFG,
	ATQUERY_SET_HTTP_QRY,
	ATWUERY_GET_HTTP_DATA,

	ATQUERY_CFUN,
	ATQUERY_SYSHALT,
	ATQUERY_ICMP,		/* ping support  */
	ATQUERY_SHDN,


	ATQUERY_CMGF, 		/* SMS mode: TEXT, PDU */
	ATQUERY_CMGR,       /* SMS oku */
	ATQUERY_CMGD,       /* SMS sil */

	ATQUERY_CMGS, 		/* */
	ATQUERY_CMGL,       /* SMS read */


	ATQUERY_COPS_STATE,
	ATQUERY_COPS_SEARCH, /* network survey */
	ATQUERY_COPS_MANUEL_REGISTER,
	ATQUERY_COPS_AUTOREGISTER,
	ATQUERY_COPS_DEREGEISTER,


	ATQUERY_WS46, /* GSM teknoloji secimi 2G, 3G, 3G/2G */
	ATQUERY_CIMI, /* Sim kart bilgisi */
	ATQUERY_CGSN, /* IMEI */

	ATQUERY_CGMM, /* Modul ismi */
	ATQUERY_MONI,
	ATQUERY_ENHRST,
	ATQUERY_RFSTS,
	ATQUERY_CEER,
	ATQUERY_CEERNET,
	ATQUERY_CGEREP,
	ATQUERY_SEARCHLIM, /* ARFCN search limit */
	ATQUERY_FPLMN,
	ATQUERY_CCLK,
	ATQUERY_NTIZ,
	ATQUERY_CCID, /* Sim kart uniq id */

	ATQUERY_TELIT_LOG_1_ENABLE,
	ATQUERY_TELIT_LOG_2_ENABLE,



	ATQUERY_LIST_LEN
};


typedef enum
{
	GSM_NO_MSG,
	GSM_OK,
	GSM_RING,
	GSM_SRING,
	GSM_ERROR,
	GSM_NO_CARRIER,
	GSM_NO_SIM,

	GSM_CPIN_READY,
	GSM_CPIN_BUSY,
	GSM_CPIN_SIMPIN,
	GSM_CPIN_SIMPUK,
	GSM_CPIN_PINNO_ERROR,
	GSM_CPIN_SIM_FAILUER,
	GSM_CPIN_NO_SIM,
	GSM_CPIN_ERROR,

	GSM_NO_NETWORK_SERVICE,

	GSM_NETWORK_NOT_SEARCHING, /* 0, 0*/
	GSM_NETWORK_REGISTERED,    /* 0,1 */
	GSM_NETWORK_SEARCHING,     /* 0,2 */
	GSM_NETWORK_DENIED,        /* 0,3 */
	GSM_NETWORK_ROAMING,       /* 0,5 */
	GSM_NETWORK_UNKNOW,
	GSM_NETWORK_ONLY_EMERGENCY,

	GSM_GPRS_NETWORK_REGISTERED,  /* 0,1 */
	GSM_GPRS_NETWORK_SEARCHING,   /* 0,2 */
	GSM_GPRS_NETWORK_DENIED,      /* 0,3 */
	GSM_GPRS_NETWORK_ROAMING,     /* 0,5 */
	GSM_GPRS_NETWORK_UNKNOW,

	GSM_LTE_NETWORK_REGISTERED,  /* 0,1 */
	GSM_LTE_NETWORK_SEARCHING,   /* 0,2 */
	GSM_LTE_NETWORK_DENIED,      /* 0,3 */
	GSM_LTE_NETWORK_ROAMING,     /* 0,5 */
	GSM_LTE_NETWORK_UNKNOW,

	GSM_SOCKET_OPEN,
	GSM_SOCKET_NO_CARRIER,
	GSM_SOCKET_DATA_PENDING,
	GSM_SOCKET_ERR,
	GSM_SOCKET_ERROR_ALREADY_OPEN,
	GSM_SOCKET_ERROR_TIMEOUT,
	GSM_SOCKET_DISCONNECTED_TIMEOUT,


	GSM_CONTEXT_NOT_OPENED,
	GSM_CONTEXT_ALREADY_ACTIVATED,

	GSM_HTTP_CONN_FAIL,
	GSM_SS_BLOCKED_IP,

	GSM_SRECV,

	GSM_SMS_NO_SMS,
	GSM_SMS_REC_UNREAD,
	GSM_SMS_REC_READ,

	GSM_COPS_AUTOMOATIC_MODE,
	GSM_COPS_MANUEL_UNLOCKED_MODE,
	GSM_COPS_DEREGISTER_MODE,
	GSM_COPS_3_MODE,
	GSM_COPS_MANUEL_AUTOMATIC_MODE,
	GSM_COPS_MANUEL_LOCKED_MODE,


	GSM_WRONG_MODULE,

	GSM_TIMEOUT,
	GSM_RESPONSE_OK, //
	GSM_DELAY,
	GSM_UNSUPPORTED_MSG

}gsm_response_t;

typedef enum
{
	GSM_TX_DIR_IDLE,
	GSM_TX_DIR_LISTENER_SOCKET,
	GSM_TX_DIR_IEC104_SOCKET,
	GSM_TX_DIR_DIALER_SOCKET
}gsm_tx_direction_t;

typedef enum
{
	GSM_TX_READY,
	GSM_TX_FIRED,
	GSM_TX_SENDING,
	GSM_TX_SENDED,
	GSM_TX_NOT_AVAILABLE
}gsm_tx_states_t;

typedef enum {
	GSM_LISTENER_WEB = 0,
	GSM_LISTENER_IEC104,
	GSM_LISTENER_COUNT
} gsm_listener_id_t;

typedef struct {
	uint8_t            state;
	uint8_t            phase;          /* GSM_INIT_PHASE_SEND / GSM_INIT_PHASE_WAIT_RESPONSE */
	uint8_t            temp_counter;
	volatile uint8_t   no_carrier;
	volatile uint8_t   rx_available;
	volatile uint8_t   new_connection_req;
	uint8_t            send_to_client;
	uint8_t            had_data_activity;
	uint8_t            si_all_zero;
	uint16_t           ack_waiting;
	uint32_t           socket_timer;
	uint32_t           tcp_ack_timer;
	uint32_t           data_check_timer;
} gsm_listener_ctx_t;

typedef enum {
	GSM_INIT_PHASE_SEND,
	GSM_INIT_PHASE_WAIT_RESPONSE
} gsm_init_phase_t;

typedef struct
{
	uint8_t  tx_buff[2048];
	uint16_t tx_index;
	uint8_t  tx_flag;
	uint8_t  tx_direction;			   /* SMS veya socket'e gonderilecek verinin durumunu tutuar. 0-> Gonderimi hazir 1-> Gonderim suruyor */
	uint8_t  tx_error;                 /* Gonderim hatali olursa 1 olur */
    uint8_t  tx_close_socket_after_tx; /* Tx isleminden sonra soketi kapatmak icin kullanilir. */

	uint16_t trace_index;

	int32_t  (*at_callback)();     /* At komutu cevabi alindiktan sonra parse islemimini yapacak fonk. */
	uint8_t  query_id;              /* Modeme gonderilen at komutun id'sini tutar*/
	uint8_t  query_state;           /* Sorgunun durumunu tutar, cevap beklenior veya sorgu i?in bekliyor . iki durum */

	uint8_t is_module_busy;         /* Modulden cevap bekleniyorsa veya modemden okuma baslatilmissa true olur */
	uint8_t main_state;             /* Modem calisma modu */
	uint8_t init_state;             /* Init modu icinde ne is yaptigini tutar. */
	uint8_t init_phase;             /* GSM_INIT_PHASE_SEND / GSM_INIT_PHASE_WAIT_RESPONSE */
	uint8_t init_step;
	uint8_t const*init_vector;      /* Kurulum asamalarini tutan vektor */
	uint8_t init_vec_len;
	uint8_t init_cereg_state;
	uint8_t init_cgreg_state;

	uint8_t prev_at_cmd[2];         /* modeme gonderilen son iki at cmd */

	uint8_t prev_state;             /* Modemin bir onceki sub statini tutuar. */
	uint8_t module_state;           /* 0-> Modul sebekeye bagli ve islemlere hazir, 1-> Modul henuz kurulum asamasinda */

	uint8_t  temp_counter;          /* state lerde gecici degisken olarak kullanilir  */
	uint8_t  delay_flag;            /* GSM process de gecikme icin  */
	uint32_t delay_timer;           /* GSM process de gecikme icin  */
	uint32_t gsm_network_timer;     /* GSM/GPRS sebekesine baglanti zaman asimi icin kullanilir */

    /* Periyodik islemler */
	uint8_t  periodical_event_state;
	uint8_t  periodical_phase;      /* GSM_INIT_PHASE_SEND / GSM_INIT_PHASE_WAIT_RESPONSE */
	uint8_t  net_check_requested;   /* Listener SM'den gelen GPRS kontrol istegi */
	uint8_t  net_reconnect_retry;   /* GPRS yeniden baglanti deneme sayaci */
	uint32_t signal_quality_timer;  /* Periyodik sinyal seviyesi kontrol zamanlayicisi*/
	uint32_t simcard_state_timer;   /* Simcard durumunu sorgulamak icin */
	uint32_t sms_check_timer;	   /* Yeni sms varmi kontrolu */
	uint32_t network_type_timer;    /* 2G - 3G mi bagliyiz ?*/
	uint32_t socket_info_timer;     /* Periyodik AT#SI sorgu zamanlayicisi */
	uint32_t gprs_check_timer;      /* Periyodik GPRS baglanti kontrol zamanlayicisi (bsp_get_tick bazli) */

	uint32_t module_event_timer;
	uint8_t  modul_event_state;
	uint8_t  module_event_flag;      /* 0-> Event yok, 1-> Yeni event olustu, 2-> eventler isleniyor */

	uint8_t  connect_to_headend;     /* GSM engine'e diger processlerden headend'e soket acmasi icin kullanilir 1-> Soketi kapat/kapatma istegi 2-> servera soket acma istegi  */
	uint8_t  disconnect_from_headend; /* Mevcut baglanti varsa sonlandirmak icin kullanilir */

	/* Dialer soket islemleri */
	uint8_t  dialer_socket_state;
	uint8_t  join_status;           /* Server'a join durumu bilgisi*/
	uint8_t  join_counter;
	uint8_t  ds_temp_counter;
	volatile uint8_t  dialer_socket_no_carrier;
	volatile uint8_t  dialer_socket_rx_available;
	uint32_t join_timer;			   /* process icin kullaniliyor, genel timer */
	uint32_t dialer_socket_timer;   /* Periyodik soket durumu kontrol zamanlayicisi */
	uint32_t dialer_tcp_ack_timer;
	uint16_t dialer_ack_waiting;

    /* listener soket islemleri */
	gsm_listener_ctx_t listener[GSM_LISTENER_COUNT];

	/* AT#SI tum soket bilgileri (connId 1-6) */
	socket_si_info_t si_info[GSM_MODEM_SOCKET_COUNT];

	uint8_t power_saving_mode_state;
	uint8_t  reboot_counter;
	uint8_t no_sim;				  /* no_sim daha oncemkayit yapilmis ise ayni calismada sadece bir defa kayit yap.*/
	uint8_t prev_creg;             /* creg'in bir onceki sonucunu tutar, buradan sebekeye baglanamaz ise bir defa kayit alir */
	uint8_t creg_not_searching_counter; /* not xearching durumunda coklu kayit almasin diye */
	uint8_t f_creg;
}gsm_t;

typedef struct
{
	uint16_t mobile_network_code; /* mnc */
	uint16_t mobile_country_code; /* mcc */
	uint16_t cell_id; 			 /* ci */
	uint16_t location_area_code;  /* lac */

}gsm_cell_info_t;

void gsm_set_gprs_state				(uint8_t state);
uint8_t   gsm_get_gprs_state				(void);
uint32_t  gsm_get_ip_addr				(void);
void gsm_set_access_technology		(uint8_t val);
uint8_t   gsm_get_access_technology		(void);
void gsm_set_signal_quality			(uint8_t val);
uint8_t   gsm_get_signal_quality			(void);
void gsm_get_cell_info				(gsm_cell_info_t *cell_info);
uint8_t   gsm_get_module_model			(void);
void gsm_reset_web_session_info		(void);
uint32_t  gsm_get_web_client_ip		(void);
void gsm_get_rxtx_counters			(uint32_t *tx, uint32_t *rx);



uint32_t  gsm_engine_send_at_cmd			(uint8_t *cmd, uint16_t cmd_len, uint8_t *res, uint8_t res_len, uint8_t try, uint32_t timeout, int32_t  (*res_callback)());
uint32_t  gsm_engine_send_query			(uint8_t query);
void gsm_engine_trace_socket_set_len(uint16_t len);
uint32_t  gsm_engine_trace_socket_send	(uint8_t socket, const uint8_t *buff, uint16_t len);
uint32_t  gsm_engine_socket_send			(uint8_t socket);
uint32_t  gsm_engine_get_query_res		(void);
uint32_t  gsm_pin_reset_module	    	(void);

void gsm_set_busy					(void);
void gsm_set_free					(void);
bool      gsm_is_busy					(void);
uint32_t  gsm_get_status					(void);
void      gsm_set_main_state                (uint8_t state);
uint8_t   gsm_get_main_state                (void);

/* Listener socket event flags (indexed by gsm_listener_id_t) */
void      gsm_listener_set_no_carrier       (gsm_listener_id_t id, uint8_t v);
uint8_t   gsm_listener_get_no_carrier       (gsm_listener_id_t id);
void      gsm_listener_set_rx_available     (gsm_listener_id_t id, uint8_t v);
uint8_t   gsm_listener_get_rx_available     (gsm_listener_id_t id);
void      gsm_listener_set_new_conn_req     (gsm_listener_id_t id, uint8_t v);
uint8_t   gsm_listener_get_new_conn_req     (gsm_listener_id_t id);
uint8_t   gsm_listener_get_state             (gsm_listener_id_t id);

void gsm_close_hes_connection		(void);
void gsm_connect_to_hes				(void);
uint8_t  gsm_get_dialer_socket_state	(void);
uint8_t  gsm_get_listener_socket_state	(void);

void gsm_set_socket_state(uint8_t socket, uint8_t state);
uint8_t gsm_get_socket_state(uint8_t socket);
void gsm_set_raw_socket_state(uint8_t socket, uint8_t state);
uint8_t gsm_get_raw_socket_state(uint8_t socket);


uint32_t  gsm_get_tx_state			(void);
void      gsm_set_tx_state			(uint8_t state);
void      gsm_set_tx_error			(void);
void      gsm_reset_process_old		(void);
void      gsm_reset_iec104_session_info(void);
void gsm_set_tx_direction		    (uint32_t direction);
uint32_t  gsm_get_tx_direction		(void);
uint32_t gsm_send_to_socket         (const void *buff, uint16_t length, uint32_t socket,
		bool tx_close_socket_after_tx, bool crypto);

uint32_t  gsm_get_tick			    (void);
void gsm_set_delay					(uint32_t d_time);


uint32_t gsm_tx_is_ready(void);

void gsm_set_signal_quality2G(uint8_t val);
void gsm_set_signal_quality4G(uint8_t val);
uint8_t   gsm_get_signal_quality2G(void);
uint8_t   gsm_get_signal_quality4G(void);

const socket_si_info_t *gsm_get_si_info(uint8_t conn_id);


#endif /* GSM_ENGINE_H_ */
