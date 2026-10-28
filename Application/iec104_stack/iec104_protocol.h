/**
 * @file iec104_protocol.h
 * @brief Named wire values for the supported IEC104 protocol profile.
 * Created on: Oct 26, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_PROTOCOL_H
#define IEC104_PROTOCOL_H

/** Type identification; names follow the IEC information-object names. */
typedef enum
{
    IEC104_TYPE_M_SP_NA_1 = 1U,   /**< Single point, without timestamp. */
    IEC104_TYPE_M_ME_NC_1 = 13U,  /**< Binary32 measurement, untimed. */
    IEC104_TYPE_M_SP_TB_1 = 30U,  /**< Single point with CP56Time2a. */
    IEC104_TYPE_M_DP_TB_1 = 31U,  /**< Double point with CP56Time2a. */
    IEC104_TYPE_M_ME_TF_1 = 36U,  /**< Binary32 with CP56Time2a. */
    IEC104_TYPE_M_EI_NA_1 = 70U,  /**< End of initialization. */
    IEC104_TYPE_C_IC_NA_1 = 100U, /**< Interrogation command. */
    IEC104_TYPE_C_CS_NA_1 = 103U, /**< Clock synchronization. */
    IEC104_TYPE_C_RP_NA_1 = 105U  /**< Reset process command. */
} iec104_type_id_t;

/** Cause of transmission; P/N and test flags are separate ASDU fields. */
typedef enum
{
    IEC104_COT_SPONTANEOUS     = 3U,
    IEC104_COT_INITIALIZED     = 4U,
    IEC104_COT_ACTIVATION      = 6U,
    IEC104_COT_ACTIVATION_CON  = 7U,
    IEC104_COT_ACTIVATION_TERM = 10U,
    IEC104_COT_UNKNOWN_TYPE    = 44U,
    IEC104_COT_UNKNOWN_CAUSE   = 45U,
    IEC104_COT_UNKNOWN_ADDRESS = 46U,
    IEC104_COT_UNKNOWN_IOA     = 47U
} iec104_cause_t;

/** QOI also supplies the COT of the corresponding interrogation data.
 * Group-to-product mapping belongs to iec104_modem.c, not the core.
 */
typedef enum
{
    IEC104_QOI_STATION = 20U,
    IEC104_QOI_GROUP_1 = 21U,
    IEC104_QOI_GROUP_2 = 22U,
    IEC104_QOI_GROUP_3 = 23U,
    IEC104_QOI_GROUP_4 = 24U
} iec104_interrogation_qualifier_t;

/** First APCI control octet, including the I/S/U format bits. */
typedef enum
{
    IEC104_CONTROL_S_ACK       = 0x01U,
    IEC104_CONTROL_STARTDT_ACT = 0x07U,
    IEC104_CONTROL_STARTDT_CON = 0x0BU,
    IEC104_CONTROL_STOPDT_ACT  = 0x13U,
    IEC104_CONTROL_STOPDT_CON  = 0x23U,
    IEC104_CONTROL_TESTFR_ACT  = 0x43U,
    IEC104_CONTROL_TESTFR_CON  = 0x83U
} iec104_control_t;

#define IEC104_RESET_PROCESS_GENERAL 1U
#define IEC104_QUALITY_INVALID 0x80U
#define IEC104_QUALITY_NOT_TOPICAL 0x40U
#define IEC104_CP56_INVALID 0x80U
#define IEC104_CP56_SUMMER_TIME 0x80U

#endif
/*** end of file ***/
