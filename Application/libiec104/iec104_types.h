/*
 * iec104_types.h
 *
 *  Created on: 26 Tem 2025
 *      Author: fatih
 */

#ifndef IEC104_TYPES_H_
#define IEC104_TYPES_H_

#include <stdint.h>


#pragma pack(1)  // Byte alignment icin

/*
Ilk harf: M=Monitor (veri okuma), C=Control (komut)
Ikinci grup: SP=Single Point, ME=Measured Value, IC=Interrogation Command
Ucucu grup: NA=No time tag, TB/TD/TF=Time tag variants
                            TB: CP56Time2a

Son: Type numarası
*/

// Sabitler - BOYUT HESAPLAMALARI ILE DUZELTME
#define MAX_ASDU_SIZE     249    // Maksimum ASDU boyutu
#define IEC104_START_CHAR 0x68   // Baslangic karakteri

// BOYUT HESAPLAMALARI:
// APCI Header: 6 byte (Start + Length + Control Field)
// ASDU Header: 6 byte (Type + VSQ + COT + CA)
// Kalan alan: 255 - 6 = 249 byte (MAX_ASDU_SIZE)
// Information Objects icin kalan: 249 - 6 = 243 byte

// VSQ teorik limit: 127 object, AMA pratikte paket boyutu limiti var!

// Type'a gore maksimum object sayilari:
#define MAX_TYPE1_OBJECTS    60   // 243 / 4 = 60 (IOA:3 + SIQ:1)
#define MAX_TYPE13_OBJECTS   34   // 243 / 7 = 34 (IOA:3 + Float:4)
#define MAX_TYPE30_OBJECTS   24   // 243 / 10 = 24 (IOA:3 + SIQ:1 + Time:6)
#define MAX_TYPE34_OBJECTS   20   // 243 / 12 = 20 (IOA:3 + Float:4 + Time:5)
#define MAX_TYPE36_OBJECTS   17   // 243 / 14 = 17 (IOA:3 + Float:4 + Time:7)
#define MAX_TYPE45_OBJECTS   60   // 243 / 4 = 60 (IOA:3 + SCO:1)

// Guvenli genel maksimum (en buyuk object boyutuna gore)
#define MAX_OBJECTS_SAFE     17   // En guvenli deger (Type 36 bazinda)

// UYARI: VSQ'da 127'ye kadar tanimlanabilir ama fiziksel paket boyutu
// 255 byte ile sinirlidir! Buyuk object'ler icin cok daha az object sigar.



// Control Field bit yapisi (I-Format icin) Package
typedef struct {
    uint32_t format         : 1;    // 0 = I-Format
    uint32_t send_seq       : 15;   // N(S) - Send Sequence Number (0-32767)
    uint32_t format2        : 1;    // 0 = I-Format
    uint32_t receive_seq    : 15;   // N(R) - Receive Sequence Number (0-32767)
}__attribute__((packed)) i_format_control_t;

// Control Field bit yapisi (S-Format icin) Package
typedef struct {
    uint32_t format         : 2;    // 01 = S-Format
    uint32_t reserved       : 14;   // Reserved (0 olmali)
    uint32_t format2        : 1;    // 0 = S-Format
    uint32_t receive_seq    : 15;   // N(R) - Receive Sequence Number
}__attribute__((packed)) s_format_control_t;

// Control Field bit yapisi (U-Format icin) Package
typedef struct {
    uint32_t format         : 2;    // 11 = U-Format
    uint32_t function_code  : 6;    // Fonksiyon kodu (STARTDT, STOPDT, TESTFR)
    uint32_t reserved       : 24;   // Reserved
}__attribute__((packed)) u_format_control_t;

// IEC-104 APCI (Application Protocol Control Information) Header Package
typedef struct {
    uint8_t  start_char;        // 0x68 - IEC-104 baslangic karakteri
    uint8_t  apdu_length;       // APDU uzunlugu (APCI header haric)
	union
	{
		uint8_t control_field[4];   // Kontrol alani (4 byte)
		i_format_control_t i_frame;
		s_format_control_t s_frame;
		u_format_control_t u_frame;
	};
}__attribute__((packed)) apci_header_t;



 
// VSQ (Variable Structure Qualifier)  
typedef struct {
    uint8_t number_of_objects : 7;  // Information Object/Element sayisi (1-127)
                                     // UYARI: Fiziksel paket boyutu 255 byte ile sinirli!
                                     // Buyuk object'ler icin cok daha az object sigar
    uint8_t sq_bit           : 1;   // 0=Ayrik(tek) IOA, 1=Ardisik(coklu) IOA
}__attribute__((packed)) vsq_t;

// COT (Cause of Transmission) bit 
typedef struct {
    uint8_t cause       : 6;   // iletim nedeni (1-63)
    uint8_t pn_bit      : 1;   // P/N bit (0=Positive, 1=Negative)
    uint8_t test_bit    : 1;   // T bit (0=Normal, 1=Test)
}__attribute__((packed)) cot_t;
 
// ASDU (Application Service Data Unit)Data unit identifier block - ASDU header
typedef struct {
    uint8_t type_id;  // Type Identification - Veri tipi (1-127)
    vsq_t vsq;     // Variable Structure Qualifier
    cot_t cot;     // Cause of Transmission - iletim nedeni
    uint8_t originator_address;   // oa, Originator Address (0-255), (opsiyonel, COT'da T biti 1 ise), master adresi(scada?)
    uint16_t common_asdu_address; // ca, Common Address - Ortak adres (Station address) (slave adresi, 0-65535 ?)
}__attribute__((packed)) asdu_header_t;


// Information Object Address (3 byte) Package
typedef struct {
    uint8_t  ioa_low;      
    uint8_t  ioa_mid;   
    uint8_t  ioa_high;  
}__attribute__((packed)) ioa_3byte_t;

// Information Object Address (2 byte - bazi implementasyonlarda) Package
typedef struct {
    uint16_t ioa;          // IOA (16-bit)
}__attribute__((packed)) ioa_2byte_t;

// CP56Time2a - 7 byte zaman damgasi Package
typedef struct {
    uint16_t milliseconds : 16;     // Milisaniye (0-59999)
    uint8_t  minute      : 6;  // Dakika (0-59)
    uint8_t  res1        : 1;  // Reserved
    uint8_t  iv_bit      : 1;  // IV bit (Invalid) (0=valid, 1=invalid time)
    uint8_t  hour        : 5;  // Saat (0-23)
    uint8_t  res2        : 2;  // Reserved
    uint8_t  su_bit      : 1;  // SU bit (Summer time) (0=standard, 1=summer time)
    uint8_t  day         : 5;  // Gun (1-31)
    uint8_t  dow         : 3;  // Day of week (1-7) (1=Mon..7=Sun, 0=not used)
    uint8_t  month       : 4;  // Ay (1-12)
    uint8_t  res3        : 4;  // Reserved
    uint8_t  year        : 7;  // Yil (0-99, from 2000)
    uint8_t  res4        : 1;  // Reserved
}__attribute__((packed)) cp56time2a_t;


// CP24Time2a - 3 byte zaman damgasi Package
typedef struct {
    uint16_t milliseconds;     // Milisaniye (0-59999)
    uint8_t  minute      : 6;  // Dakika (0-59)
    uint8_t  iv_bit      : 1;  // IV bit (Invalid)
    uint8_t  res1        : 1;  // Reserved
}__attribute__((packed)) cp24time2a_t;

// Quality Descriptor for measured values Package
typedef struct {
    uint8_t overflow     : 1;  // OV - Overflow
    uint8_t reserved     : 3;  // Reserved
    uint8_t blocked      : 1;  // BL - Blocked, bloke edilmis
    uint8_t substituted  : 1;  // SB - Substituted, yerine konulmus
    uint8_t not_topical  : 1;  // NT - Not topical, guncel degil
    uint8_t invalid      : 1;  // IV - Invalid, gecersiz     
}__attribute__((packed)) qds_t;

// Quality Descriptor for single points Package
typedef struct {
    uint8_t reserved     : 4;  // Reserved
    uint8_t blocked      : 1;  // BL - Blocked
    uint8_t substituted  : 1;  // SB - Substituted
    uint8_t not_topical  : 1;  // NT - Not topical
    uint8_t invalid      : 1;  // IV - Invalid
}__attribute__((packed)) qdp_t;

 
typedef struct {
    uint8_t  spi         : 1;  // Single Point Information (0=OFF, 1=ON)
    uint8_t  reserved    : 3;  // Reserved
    uint8_t  blocked     : 1;  // BL - Blocked
    uint8_t  substituted : 1;  // SB - Substituted
    uint8_t  not_topical : 1;  // NT - Not topical
    uint8_t  invalid     : 1;  // IV - Invalid
}__attribute__((packed)) siq_t;  // Single Point Information with Quality


typedef struct {
    uint8_t dpi:2;          // Double Point Information (0=OFF, 1=ON, 2=Indeterminate, 3=Reserved)
    uint8_t reserved:2;    // Reserved
    uint8_t blocked:1;      // BL - Blocked
    uint8_t substituted:1;  // SB - Substituted
    uint8_t not_topical:1;  // NT - Not topical
    uint8_t invalid:1;      // IV - Invalid
}__attribute__((packed)) diq_t; // Double Point Information with Quality descriptor


typedef struct {
    uint8_t initialization_cause:7;     //Cause of Initialization (0: Local power switch on, 1: Local manuel reset, 2: Remote reset, 3-127:Reserved)
    uint8_t reserved:1;
}__attribute__((packed)) coi_t;


typedef struct {
    ioa_3byte_t  ioa;   // Information Object Address
    coi_t coi;   // Cause of Initialization
}__attribute__((packed)) m_ei_na_1_t; // End of Initialization - Type 70




 

// Single Point Information Package
typedef struct {
    ioa_3byte_t ioa;   // Information Object Address
    siq_t       siq;   // Single Point + Quality
}__attribute__((packed)) m_sp_na_1_t;  // M_SP_NA_1 Type 1 - Single Point Information

// Single Point Information with Time Tag Package
typedef struct {
    ioa_3byte_t  ioa;   // Information Object Address
    siq_t        siq;   // Single Point + Quality
    cp56time2a_t timestamp; // 7-byte zaman damgasi
}__attribute__((packed)) m_sp_tb_1_t;  // M_SP_TB_1 Type 30 - Single Point Information

// Double Point Information with Time Tag Package
typedef struct {
    ioa_3byte_t  ioa;   // Information Object Address
    diq_t        diq;   // Double Point + Quality
    cp56time2a_t timestamp; // 7-byte zaman damgasi
}__attribute__((packed)) m_dp_tb_1_t;  // M_DP_TB_1 Type 31 - Double Point Information

// Measured Value (Float) with Time Tag Package
typedef struct {
    ioa_3byte_t   ioa;       // Information Object Address (3 bytes)
    float         value;     // IEEE 754 float (4 bytes)
    qds_t         quality;   // Quality Descriptor (1 byte)
    cp56time2a_t  timestamp; // Full timestamp (7 bytes)
}__attribute__((packed)) m_me_tf_1_t;        // Total: 15 bytes


typedef enum
{
    QOC_NO_PULSE_DEF = 0x00, // No additional definition
    QOC_SHORT_PULSE = 0x01, // Short Pulse Duration
    QOC_LONG_PULSE = 0x02, // Long Pulse Duration
    QOC_PERSISTENT_OUTPUT = 0x03, // Persistent Output
    // 4-31 reserved for future use
 
}qualifier_of_command_t; // Qualifier of Command (0-31)

typedef enum {
    SCO_CLOSE_ON_TRUE = 1,  // Open/Off command (0=ON, 1=OFF)
    SCO_OPEN_OFF_FALSE = 0, // Open/Off command (0=OFF, 1=ON)
} sco_command_state_t;


typedef enum {

    SE_EXECUTE = 0, // Execute command 0
    SE_SELECT = 1,  // Select command 1
}se_bit_t; // S/E bit (0=Execute, 1=Select)

// Single Command Object Package
typedef struct {
    uint8_t scs          : 1;  // Single Command State (0=OFF, 1=ON)
    uint8_t reserved     : 1;  // Reserved
    uint8_t qu           : 5;  // Qualifier of Command (0-31)
    uint8_t se_bit       : 1;  // S/E bit (0=Execute, 1=Select (daha sonra execute edilecek))
}__attribute__((packed)) sco_t;

 

//Single Command Package
typedef struct {
    ioa_3byte_t ioa; // Information Object Address
    sco_t       sco; // Single Command Object
}__attribute__((packed)) c_sc_na_1_t;  // Type 45: C_SC_NA_1 - Single Command

 
 
typedef enum {
    DCS_NOT_PERMITTED_0 = 0,    // 00 - Not permitted, Belirsiz veya ara durum
    DCS_OFF_OPEN        = 1,    // 01 - OFF/OPEN
    DCS_ON_CLOSE        = 2,    // 10 - ON/CLOSE  
    DCS_NOT_PERMITTED_3 = 3     // 11 - Not permitted, Belirsiz durum
} dco_command_state_t;

// Double Command Object
typedef struct {
    uint8_t dcs     : 2;    // Double Command State (00=Not permitted, 01=OFF/Open, 10=ON/Close, 11=Not permitted)
    uint8_t qu      : 5;    // Command qualifier bits 
    uint8_t se_bit  : 1;    // Select/Execute bit (0=Execute, 1=Select)
}__attribute__((packed)) dco_t; 


// Double Command Package
typedef struct {
    ioa_3byte_t ioa; // Information Object Address
    dco_t       dco; // Double Command Object
}__attribute__((packed)) c_dc_na_1_t;  // Type 46: C_DC_NA_1 - Double Command

typedef struct {
    ioa_3byte_t  ioa;       // Information Object Address
    cp56time2a_t timestamp; // 7-byte zaman damgasi
}__attribute__((packed)) c_cs_na_1_t;  // Type 46: C_CS_NA_1 - Clock Synchronization Command
















typedef struct {
    ioa_3byte_t ioa;   // Information Object Address
    siq_t       siq;   // Single Point + Quality
}__attribute__((packed)) m_sp_na_1_object_t;  // Type 1 - Single Point Information

// Type 13: M_ME_NC_1 - Measured Value (Float) Package
typedef struct {
    ioa_3byte_t ioa;     // Information Object Address
    float               value;   // IEEE 754 float deger
    qds_t       quality; // Quality Descriptor
}__attribute__((packed)) m_me_nc_1_object_t;  // Type 13 - Measured Value Float

// Type 30: M_SP_TB_1 - Single Point with Time Tag Package
typedef struct {
    ioa_3byte_t   ioa;       // Information Object Address
    siq_t         siq;       // Single Point + Quality
    cp24time2a_t  timestamp; // 3-byte zaman damgasi
}__attribute__((packed)) m_sp_tb_1_object_t;  // Type 30 - Single Point with Time Tag

// Type 34: M_ME_TD_1 - Measured Value (Float) with Time Tag Package
typedef struct {
    ioa_3byte_t   ioa;       // Information Object Address
    float                 value;     // IEEE 754 float deger
    qds_t         quality;   // Quality Descriptor
    cp24time2a_t  timestamp; // 3-byte zaman damgasi
}__attribute__((packed)) m_me_td_1_object_t;  // Type 34 - Measured Value Float with Time Tag

// Type 36: M_ME_TF_1 - Measured Value (Float) with Long Time Tag Package
typedef struct {
    ioa_3byte_t   ioa;       // Information Object Address
    float                 value;     // IEEE 754 float deger
    qds_t         quality;   // Quality Descriptor
    cp56time2a_t  timestamp; // 7-byte zaman damgasi
}__attribute__((packed)) m_me_tf_1_object_t;  // Type 36 - Measured Value Float with Long Time Tag

// Single Command Object Package
typedef struct {
    uint8_t scs          : 1;  // Single Command State (0=OFF, 1=ON)
    uint8_t reserved     : 1;  // Reserved
    uint8_t qu           : 5;  // Qualifier of Command (0-31)
    uint8_t se_bit       : 1;  // S/E bit (0=Execute, 1=Select)
}__attribute__((packed)) scoq_t;

// Type 45: C_SC_NA_1 - Single Command Package
typedef struct {
    ioa_3byte_t ioa; // Information Object Address
    sco_t       sco; // Single Command Object
}__attribute__((packed)) c_sc_na_1_command_t;  // Type 45 - Single Command

// Type 100: C_IC_NA_1 - Interrogation Command Package
typedef struct {
    ioa_3byte_t ioa; // Information Object Address (genelde 0)
    uint8_t             qoi; // Qualifier of Interrogation (20=Station, 21=Group1, etc.)
}__attribute__((packed)) c_ic_na_1_command_t;  // Type 100 - Interrogation Command

// Ana ASDU Packet yapisi Package
typedef struct {
    apci_header_t  apci;         // APCI Header (6 byte)
    asdu_header_t  asdu_header;  // ASDU Header (6-7 byte)
    uint8_t                information_objects[MAX_ASDU_SIZE]; // Information Objects (degisken)
}__attribute__((packed)) iec104_asdu_packet_t;


// Genel paket yapisi
typedef union
{
	uint8_t data[255];

	struct
	{
		apci_header_t  apci;        // APCI Header
		asdu_header_t  asdu_header; // ASDU Header

		union
		{
			uint8_t raw_data[MAX_ASDU_SIZE - 6]; // MAX_ASDU_SIZE - asdu_header, asdu_header ustte tanimli!

            m_sp_na_1_t m_sp_na_1; // Type  1: M_SP_NA_1 - Single Point Information
			m_dp_tb_1_t m_dp_tb_1; // Type 31: M_DP_TB_1 - Double Point Information with Time Tag
            c_sc_na_1_t c_sc_na_1; // Type 45: C_SC_NA_1 - Single Command
            m_ei_na_1_t m_ei_na_1; // Type 70: M_EI_NA_1 - End of Initialization
            c_cs_na_1_t c_cs_na_1; // Type 46: C_CS_NA_1 - Clock Synchronization Command

			// Type 100: C_IC_NA_1 - Interrogation Command
			c_ic_na_1_command_t  c_ic_na_1_command;                      // tek object
		};

	}frame;

}__attribute__((packed)) iec104_package_t;


typedef enum 
{
    COT_PERIODIC             = 1,     // Periyodik
    COT_BACKGROUND_SCAN      = 2,     // Arka plan tarama
    COT_SPONTANEOUS          = 3,     // Spontan
    COT_INITIALIZED          = 4,     // Initialized
    COT_REQUEST              = 5,     // Request
    COT_ACTIVATION           = 6,     // Aktivasyon
    COT_ACTIVATION_CON       = 7,     // Aktivasyon onayi
    COT_DEACTIVATION         = 8,     // Deaktivasyon
    COT_DEACTIVATION_CON     = 9,     // Deaktivasyon onayi
    COT_ACTIVATION_TERM      = 10,    // Aktivasyon sonlandirma
    COT_RETURN_INFO_REMOTE   = 11,    // Uzaktan bilgi donusu
    COT_RETURN_INFO_LOCAL    = 12,    // Yerel bilgi donusu
    COT_FILE_TRANSFER        = 13,    // Dosya transferi
    COT_INTERROGATED_STATION = 20,    // Sorgulanan istasyon
	COT_INTERROGATED_GROUP1  = 21,     // Sorgulanan grup 1  formul: gelen COT + 16
	COT_INTERROGATED_GROUP2  = 22,     // Sorgulanan grup 2
	COT_INTERROGATED_GROUP3  = 23,     // Sorgulanan grup 3
	COT_INTERROGATED_GROUP4  = 24      // Sorgulanan grup 3
} cause_of_transmission_t;

typedef enum
{
    // Standart Genel Sorgulama (Station Interrogation)
    QOI_STATION             = 20, // 0x14 - Tüm istasyon icin genel sorgulama

    // Grup Sorgulamalari (Group Interrogations)
    QOI_GROUP_1             = 21, // 0x15
    QOI_GROUP_2             = 22, // 0x16
    QOI_GROUP_3             = 23, // 0x17
    QOI_GROUP_4             = 24, // 0x18
    QOI_GROUP_5             = 25, // 0x19
    QOI_GROUP_6             = 26, // 0x1A
    QOI_GROUP_7             = 27, // 0x1B
    QOI_GROUP_8             = 28, // 0x1C
    QOI_GROUP_9             = 29, // 0x1D
    QOI_GROUP_10            = 30, // 0x1E
    QOI_GROUP_11            = 31, // 0x1F
    QOI_GROUP_12            = 32, // 0x20
    QOI_GROUP_13            = 33, // 0x21
    QOI_GROUP_14            = 34, // 0x22
    QOI_GROUP_15            = 35, // 0x23
    QOI_GROUP_16            = 36, // 0x24

    // Sayac Sorgulamalari (Counter Interrogation) - C_CI_NA_1 icin
    QOI_COUNTER_STATION     = 64, // 0x40 - Tüm sayaclar (Global)
    QOI_COUNTER_GROUP_1     = 65, // 0x41
    QOI_COUNTER_GROUP_2     = 66, // 0x42
    QOI_COUNTER_GROUP_3     = 67, // 0x43
    QOI_COUNTER_GROUP_4     = 68  // 0x44
} iec104_qoi_t;

/*
// COT (Cause of Transmission) degerleri
#define COT_PERIODIC            1    // Periyodik
#define COT_BACKGROUND_SCAN     2    // Arka plan tarama
#define COT_SPONTANEOUS         3    // Spontan
#define COT_INITIALIZED         4    // Initialized
#define COT_REQUEST             5    // Request
#define COT_ACTIVATION          6    // Aktivasyon
#define COT_ACTIVATION_CON      7    // Aktivasyon onayi
#define COT_DEACTIVATION        8    // Deaktivasyon
#define COT_DEACTIVATION_CON    9    // Deaktivasyon onayi
#define COT_ACTIVATION_TERM     10   // Aktivasyon sonlandirma
#define COT_RETURN_INFO_REMOTE  11   // Uzaktan bilgi donusu
#define COT_RETURN_INFO_LOCAL   12   // Yerel bilgi donusu
#define COT_FILE_TRANSFER       13   // Dosya transferi
#define COT_INTERROGATED_STATION 20  // Sorgulanan istasyon
#define COT_INTERROGATED_GROUP1  21  // Sorgulanan grup 1
*/


// Type ID degerleri ve protokol kisaltmalari
#define TYPE_M_SP_NA_1      1    // Single Point Information
#define TYPE_M_DP_NA_1      3    // Double Point Information
#define TYPE_M_ST_NA_1      5    // Step Position Information
#define TYPE_M_BO_NA_1      7    // Bitstring 32-bit
#define TYPE_M_ME_NA_1      9    // Measured Value, Normalized
#define TYPE_M_ME_NB_1      11   // Measured Value, Scaled
#define TYPE_M_ME_NC_1      13   // Measured Value, Float
#define TYPE_M_IT_NA_1      15   // Integrated Totals
#define TYPE_M_SP_TB_1      30   // Single Point + Time Tag
#define TYPE_M_DP_TB_1      31   // Double Point + Time Tag
#define TYPE_M_ST_TB_1      32   // Step Position + Time Tag
#define TYPE_M_BO_TB_1      33   // Bitstring + Time Tag
#define TYPE_M_ME_TD_1      34   // Measured Float + Time Tag
#define TYPE_M_ME_TE_1      35   // Measured Scaled + Time Tag
#define TYPE_M_ME_TF_1      36   // Measured Float + Long Time Tag
#define TYPE_C_SC_NA_1      45   // Single Command
#define TYPE_C_DC_NA_1      46   // Double Command
#define TYPE_C_RC_NA_1      47   // Regulating Step Command
#define TYPE_C_SE_NC_1      50   // Set Point Float
#define TYPE_C_IC_NA_1      100  // Interrogation Command
#define TYPE_C_CI_NA_1      101  // Counter Interrogation
#define TYPE_C_RD_NA_1      102  // Read Command
#define TYPE_C_CS_NA_1      103  // Clock Synchronization

// Protokol kisaltma aciklamalari:
// M_SP_NA_1: Monitor, Single Point, No time tag, Type 1
// M_ME_NC_1: Monitor, Measured value, Normalized, C=Short floating point, Type 1
// M_SP_TB_1: Monitor, Single Point, Time tag B (3 byte), Type 1
// M_ME_TD_1: Monitor, Measured value, Time tag D, Type 1
// M_ME_TF_1: Monitor, Measured value, Time tag F (7 byte), Type 1
// C_SC_NA_1: Control, Single Command, No time tag, Type 1
// C_IC_NA_1: Control, Interrogation Command, No time tag, Type 1


#define DATA_START_IDX (sizeof(apci_header_t) + sizeof(asdu_header_t)) // 12 olmali

#pragma pack()  // Normal alignment'a don

#endif /* IEC104_TYPES_H_ */
