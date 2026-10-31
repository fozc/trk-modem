/**
 * @file iec104_protocol.h
 * @brief Named wire values for the supported IEC104 protocol profile.
 * Created on: Oct 26, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_PROTOCOL_H
#define IEC104_PROTOCOL_H

/** Standard Type IDs (101/104 plus security extensions), not an implementation claim.
 * See examples/README.md for supported encoders, required handlers and profile restrictions.
 */
typedef enum
{
    IEC104_TYPE_M_SP_NA_1 = 1U,
    IEC104_TYPE_M_SP_TA_1 = 2U,
    IEC104_TYPE_M_DP_NA_1 = 3U,
    IEC104_TYPE_M_DP_TA_1 = 4U,
    IEC104_TYPE_M_ST_NA_1 = 5U,
    IEC104_TYPE_M_ST_TA_1 = 6U,
    IEC104_TYPE_M_BO_NA_1 = 7U,
    IEC104_TYPE_M_BO_TA_1 = 8U,
    IEC104_TYPE_M_ME_NA_1 = 9U,
    IEC104_TYPE_M_ME_TA_1 = 10U,
    IEC104_TYPE_M_ME_NB_1 = 11U,
    IEC104_TYPE_M_ME_TB_1 = 12U,
    IEC104_TYPE_M_ME_NC_1 = 13U,
    IEC104_TYPE_M_ME_TC_1 = 14U,
    IEC104_TYPE_M_IT_NA_1 = 15U,
    IEC104_TYPE_M_IT_TA_1 = 16U,
    IEC104_TYPE_M_EP_TA_1 = 17U,
    IEC104_TYPE_M_EP_TB_1 = 18U,
    IEC104_TYPE_M_EP_TC_1 = 19U,
    IEC104_TYPE_M_PS_NA_1 = 20U,
    IEC104_TYPE_M_ME_ND_1 = 21U,
    IEC104_TYPE_M_SP_TB_1 = 30U,
    IEC104_TYPE_M_DP_TB_1 = 31U,
    IEC104_TYPE_M_ST_TB_1 = 32U,
    IEC104_TYPE_M_BO_TB_1 = 33U,
    IEC104_TYPE_M_ME_TD_1 = 34U,
    IEC104_TYPE_M_ME_TE_1 = 35U,
    IEC104_TYPE_M_ME_TF_1 = 36U,
    IEC104_TYPE_M_IT_TB_1 = 37U,
    IEC104_TYPE_M_EP_TD_1 = 38U,
    IEC104_TYPE_M_EP_TE_1 = 39U,
    IEC104_TYPE_M_EP_TF_1 = 40U,
    IEC104_TYPE_S_IT_TC_1 = 41U,
    IEC104_TYPE_C_SC_NA_1 = 45U,
    IEC104_TYPE_C_DC_NA_1 = 46U,
    IEC104_TYPE_C_RC_NA_1 = 47U,
    IEC104_TYPE_C_SE_NA_1 = 48U,
    IEC104_TYPE_C_SE_NB_1 = 49U,
    IEC104_TYPE_C_SE_NC_1 = 50U,
    IEC104_TYPE_C_BO_NA_1 = 51U,
    IEC104_TYPE_C_SC_TA_1 = 58U,
    IEC104_TYPE_C_DC_TA_1 = 59U,
    IEC104_TYPE_C_RC_TA_1 = 60U,
    IEC104_TYPE_C_SE_TA_1 = 61U,
    IEC104_TYPE_C_SE_TB_1 = 62U,
    IEC104_TYPE_C_SE_TC_1 = 63U,
    IEC104_TYPE_C_BO_TA_1 = 64U,
    IEC104_TYPE_M_EI_NA_1 = 70U,
    IEC104_TYPE_S_CH_NA_1 = 81U,
    IEC104_TYPE_S_RP_NA_1 = 82U,
    IEC104_TYPE_S_AR_NA_1 = 83U,
    IEC104_TYPE_S_KR_NA_1 = 84U,
    IEC104_TYPE_S_KS_NA_1 = 85U,
    IEC104_TYPE_S_KC_NA_1 = 86U,
    IEC104_TYPE_S_ER_NA_1 = 87U,
    IEC104_TYPE_S_US_NA_1 = 90U,
    IEC104_TYPE_S_UQ_NA_1 = 91U,
    IEC104_TYPE_S_UR_NA_1 = 92U,
    IEC104_TYPE_S_UK_NA_1 = 93U,
    IEC104_TYPE_S_UA_NA_1 = 94U,
    IEC104_TYPE_S_UC_NA_1 = 95U,
    IEC104_TYPE_C_IC_NA_1 = 100U,
    IEC104_TYPE_C_CI_NA_1 = 101U,
    IEC104_TYPE_C_RD_NA_1 = 102U,
    IEC104_TYPE_C_CS_NA_1 = 103U,
    IEC104_TYPE_C_TS_NA_1 = 104U,
    IEC104_TYPE_C_RP_NA_1 = 105U,
    IEC104_TYPE_C_CD_NA_1 = 106U,
    IEC104_TYPE_C_TS_TA_1 = 107U,
    IEC104_TYPE_P_ME_NA_1 = 110U,
    IEC104_TYPE_P_ME_NB_1 = 111U,
    IEC104_TYPE_P_ME_NC_1 = 112U,
    IEC104_TYPE_P_AC_NA_1 = 113U,
    IEC104_TYPE_F_FR_NA_1 = 120U,
    IEC104_TYPE_F_SR_NA_1 = 121U,
    IEC104_TYPE_F_SC_NA_1 = 122U,
    IEC104_TYPE_F_LS_NA_1 = 123U,
    IEC104_TYPE_F_AF_NA_1 = 124U,
    IEC104_TYPE_F_SG_NA_1 = 125U,
    IEC104_TYPE_F_DR_TA_1 = 126U,
    IEC104_TYPE_F_SC_NB_1 = 127U,
} iec104_type_id_t;

/** Cause of transmission; P/N and test flags are separate ASDU fields. */
typedef enum
{
    IEC104_COT_PERIODIC         = 1U,
    IEC104_COT_SPONTANEOUS      = 3U,
    IEC104_COT_INITIALIZED      = 4U,
    IEC104_COT_ACTIVATION       = 6U,
    IEC104_COT_ACTIVATION_CON   = 7U,
    IEC104_COT_DEACTIVATION     = 8U,
    IEC104_COT_DEACTIVATION_CON = 9U,
    IEC104_COT_ACTIVATION_TERM  = 10U,
    IEC104_COT_UNKNOWN_TYPE     = 44U,
    IEC104_COT_UNKNOWN_CAUSE    = 45U,
    IEC104_COT_UNKNOWN_ADDRESS  = 46U,
    IEC104_COT_UNKNOWN_IOA      = 47U
} iec104_cause_t;

/** QOI also supplies the COT of the corresponding interrogation data.
 * Group-to-data mapping belongs to the application, not the library.
 */
typedef enum
{
    IEC104_QOI_STATION  = 20U,
    IEC104_QOI_GROUP_1  = 21U,
    IEC104_QOI_GROUP_2  = 22U,
    IEC104_QOI_GROUP_3  = 23U,
    IEC104_QOI_GROUP_4  = 24U,
    IEC104_QOI_GROUP_16 = 36U
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
