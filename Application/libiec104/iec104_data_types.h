/*
 * iec104_data_types.h
 *
 *  Created on: 26 Tem 2025
 *      Author: fatih
 */

#ifndef IEC104_DATA_TYPES_H_
#define IEC104_DATA_TYPES_H_

#include <stdint.h>

/* ASDU types (TypeId) */
#define M_SP_NA_1  1    /* single-point information 								*/
#define M_SP_TA_1  2    /* single-point information with time tag 	 					*/
#define M_DP_NA_1  3    /* double-point information 								*/
#define M_DP_TA_1  4    /* double-point information with time tag 						*/
#define M_ST_NA_1  5    /* step position information 								*/
#define M_ST_TA_1  6    /* step position information with time tag 						*/
#define M_BO_NA_1  7    /* bitstring of 32 bits 								*/
#define M_BO_TA_1  8    /* bitstring of 32 bits with time tag 							*/
#define M_ME_NA_1  9    /* measured value, normalized value 							*/
#define M_ME_TA_1  10    /* measured value, normalized value with time tag 					*/
#define M_ME_NB_1  11    /* measured value, scaled value 							*/
#define M_ME_TB_1  12    /* measured value, scaled value with time tag 						*/
#define M_ME_NC_1  13    /* measured value, short floating point number 					*/
#define M_ME_TC_1  14    /* measured value, short floating point number with time tag 				*/
#define M_IT_NA_1  15    /* integrated totals 									*/
#define M_IT_TA_1  16    /* integrated totals with time tag 							*/
#define M_PS_NA_1  20    /* packed single-point information with status change detection 			*/
#define M_ME_ND_1  21    /* measured value, normalized value without quality descriptor 			*/
#define M_SP_TB_1  30    /* single-point information with time tag CP56Time2a 					*/
#define M_DP_TB_1  31    /* double-point information with time tag CP56Time2a 					*/
#define M_ST_TB_1  32    /* step position information with time tag CP56Time2a 					*/
#define M_BO_TB_1  33    /* bitstring of 32 bit with time tag CP56Time2a 					*/
#define M_ME_TD_1  34    /* measured value, normalized value with time tag CP56Time2a 				*/
#define M_ME_TE_1  35    /* measured value, scaled value with time tag CP56Time2a 				*/
#define M_ME_TF_1  36    /* measured value, short floating point number with time tag CP56Time2a 		*/
#define M_IT_TB_1  37    /* integrated totals with time tag CP56Time2a 						*/
#define M_EP_TD_1  38    /* event of protection equipment with time tag CP56Time2a 				*/
#define M_EP_TE_1  39    /* packed start events of protection equipment with time tag CP56Time2a 		*/
#define M_EP_TF_1  40    /* packed output circuit information of protection equipment with time tag CP56Time2a 	*/
#define S_IT_TC_1  41    /* integrated totals containing time tagged security statistics			*/
#define C_SC_NA_1  45    /* single command 									*/
#define C_DC_NA_1  46    /* double command 									*/
#define C_RC_NA_1  47    /* regulating step command 								*/
#define C_SE_NA_1  48    /* set point command, normalized value 						*/
#define C_SE_NB_1  49    /* set point command, scaled value 							*/
#define C_SE_NC_1  50    /* set point command, short floating point number 					*/
#define C_BO_NA_1  51    /* bitstring of 32 bits 								*/
#define C_SC_TA_1  58    /* single command with time tag CP56Time2a 						*/
#define C_DC_TA_1  59    /* double command with time tag CP56Time2a 						*/
#define C_RC_TA_1  60    /* regulating step command with time tag CP56Time2a 					*/
#define C_SE_TA_1  61    /* set point command, normalized value with time tag CP56Time2a 			*/
#define C_SE_TB_1  62    /* set point command, scaled value with time tag CP56Time2a 				*/
#define C_SE_TC_1  63    /* set point command, short floating-point number with time tag CP56Time2a 		*/
#define C_BO_TA_1  64    /* bitstring of 32 bits with time tag CP56Time2a 					*/
#define M_EI_NA_1  70    /* end of initialization 								*/
#define S_CH_NA_1  81    /* authentication challenge								*/
#define S_RP_NA_1  82    /* authentication reply								*/
#define S_AR_NA_1  83    /* aggressive mode authentication request session key status request			*/
#define S_KR_NA_1  84    /* session key status request								*/
#define S_KS_NA_1  85    /* session key status									*/
#define S_KC_NA_1  86    /* session key change									*/
#define S_ER_NA_1  87    /* authentication error								*/
#define S_US_NA_1  90    /* user status change									*/
#define S_UQ_NA_1  91    /* update key change request								*/
#define S_UR_NA_1  92    /* update key change reply								*/
#define S_UK_NA_1  93    /* update key change symmetric								*/
#define S_UA_NA_1  94    /* update key change asymmetric							*/
#define S_UC_NA_1  95    /* update key change confirmation							*/
#define C_IC_NA_1  100    /* interrogation command 								*/
#define C_CI_NA_1  101    /* counter interrogation command 							*/
#define C_RD_NA_1  102    /* read command 									*/
#define C_CS_NA_1  103    /* clock synchronization command 							*/
#define C_TS_NA_1  104    /* test command */
#define C_RP_NA_1  105    /* reset process command 								*/
#define C_TS_TA_1  107    /* test command with time tag CP56Time2a 						*/
#define P_ME_NA_1  110    /* parameter of measured value, normalized value 					*/
#define P_ME_NB_1  111    /* parameter of measured value, scaled value 						*/
#define P_ME_NC_1  112    /* parameter of measured value, short floating-point number 				*/
#define P_AC_NA_1  113    /* parameter activation 								*/
#define F_FR_NA_1  120    /* file ready 									*/
#define F_SR_NA_1  121    /* section ready 									*/
#define F_SC_NA_1  122    /* call directory, select file, call file, call section 				*/
#define F_LS_NA_1  123    /* last section, last segment 							*/
#define F_AF_NA_1  124    /* ack file, ack section 								*/
#define F_SG_NA_1  125    /* segment 										*/
#define F_DR_TA_1  126    /* directory 										*/
#define F_SC_NB_1  127    /* Query Log - Request archive file 							*/


typedef struct {
	uint8_t  value;
	uint8_t  length;
} td_asdu_length;

// IOA haric obje boyutlari
static const td_asdu_length asdu_length [] = {
	{  M_SP_NA_1,	 1 },
	{  M_SP_TA_1,	 4 },
	{  M_DP_NA_1,	 1 },
	{  M_DP_TA_1,	 4 },
	{  M_ST_NA_1,	 2 },
	{  M_ST_TA_1,	 5 },
	{  M_BO_NA_1,	 5 },
	{  M_BO_TA_1,	 8 },
	{  M_ME_NA_1,	 3 },
	{  M_ME_TA_1,	 6 },
	{  M_ME_NB_1,	 3 },
	{  M_ME_TB_1,	 6 },
	{  M_ME_NC_1,	 5 },
	{  M_ME_TC_1,	 8 },
	{  M_IT_NA_1,	 5 },
	{  M_IT_TA_1,	 8 },
	{  M_PS_NA_1,	 5 },
	{  M_ME_ND_1,	 2 },
	{  M_SP_TB_1,	 8 },
	{  M_DP_TB_1,	 8 },
	{  M_ST_TB_1,	 9 },
	{  M_BO_TB_1,	12 },
	{  M_ME_TD_1,	10 },
	{  M_ME_TE_1,	10 },
	{  M_ME_TF_1,	12 },
	{  M_IT_TB_1,	12 },
	{  M_EP_TD_1,	10 },
	{  M_EP_TE_1,	11 },
	{  M_EP_TF_1,	11 },
	{  S_IT_TC_1,    0 },
	{  C_SC_NA_1,	 1 },
	{  C_DC_NA_1,	 1 },
	{  C_RC_NA_1,	 1 },
	{  C_SE_NA_1,	 3 },
	{  C_SE_NB_1,	 3 },
	{  C_SE_NC_1,	 5 },
	{  C_BO_NA_1,	 4 },
	{  C_SC_TA_1,	 8 },
	{  C_DC_TA_1,	 8 },
	{  C_RC_TA_1,	 8 },
	{  C_SE_TA_1,	10 },
	{  C_SE_TB_1,	10 },
	{  C_SE_TC_1,	12 },
	{  C_BO_TA_1,	11 },
	{  M_EI_NA_1,	 1 },
	{  S_CH_NA_1,    0 },
	{  S_RP_NA_1,    0 },
	{  S_AR_NA_1,    0 },
	{  S_KR_NA_1,    0 },
	{  S_KS_NA_1,    0 },
	{  S_KC_NA_1,    0 },
	{  S_ER_NA_1,    0 },
	{  S_US_NA_1,    0 },
	{  S_UQ_NA_1,    0 },
	{  S_UR_NA_1,    0 },
	{  S_UK_NA_1,    0 },
	{  S_UA_NA_1,    0 },
	{  S_UC_NA_1,    0 },
	{  C_IC_NA_1,	 1 },
	{  C_CI_NA_1,	 1 },
	{  C_RD_NA_1,	 0 },
	{  C_CS_NA_1,	 7 },
	{  C_RP_NA_1,	 1 },
	{  C_TS_TA_1,	 9 },
	{  P_ME_NA_1,	 3 },
	{  P_ME_NB_1,	 3 },
	{  P_ME_NC_1,	 5 },
	{  P_AC_NA_1,	 1 },
	{  F_FR_NA_1,	 6 },
	{  F_SR_NA_1,	 7 },
	{  F_SC_NA_1,	 4 },
	{  F_LS_NA_1,	 5 },
	{  F_AF_NA_1,	 4 },
	{  F_SG_NA_1,	 0 },
	{  F_DR_TA_1,	13 },
	{  F_SC_NB_1,	16 },
	{ 0, 0 }
};

typedef struct value_string {
	uint8_t value;          // ASDU type ID
	const char *string;    // ASDU type name
} value_string;

static inline const char *val_to_str(const uint32_t val, const value_string *vs)
{
    const char *ret;
	for (ret = NULL; vs->value != 0; vs++) {
		if (vs->value == val) {
			ret = vs->string;
			break;
		}
	}
	return ret ? ret : "Unknown ASDU Type";
}

static const value_string asdu_types_str [] = {
	{  M_SP_NA_1,		"M_SP_NA_1" },
	{  M_SP_TA_1,		"M_SP_TA_1" },
	{  M_DP_NA_1,		"M_DP_NA_1" },
	{  M_DP_TA_1,		"M_DP_TA_1" },
	{  M_ST_NA_1,		"M_ST_NA_1" },
	{  M_ST_TA_1,		"M_ST_TA_1" },
	{  M_BO_NA_1,		"M_BO_NA_1" },
	{  M_BO_TA_1,		"M_BO_TA_1" },
	{  M_ME_NA_1,		"M_ME_NA_1" },
	{  M_ME_TA_1,		"M_ME_TA_1" },
	{  M_ME_NB_1,		"M_ME_NB_1" },
	{  M_ME_TB_1,		"M_ME_TB_1" },
	{  M_ME_NC_1,		"M_ME_NC_1" },
	{  M_ME_TC_1,		"M_ME_TC_1" },
	{  M_IT_NA_1,		"M_IT_NA_1" },
	{  M_IT_TA_1,		"M_IT_TA_1" },
	{  M_PS_NA_1,		"M_PS_NA_1" },
	{  M_ME_ND_1,		"M_ME_ND_1" },
	{  M_SP_TB_1,		"M_SP_TB_1" },
	{  M_DP_TB_1,		"M_DP_TB_1" },
	{  M_ST_TB_1,		"M_ST_TB_1" },
	{  M_BO_TB_1,		"M_BO_TB_1" },
	{  M_ME_TD_1,		"M_ME_TD_1" },
	{  M_ME_TE_1,		"M_ME_TE_1" },
	{  M_ME_TF_1,		"M_ME_TF_1" },
	{  M_IT_TB_1,		"M_IT_TB_1" },
	{  M_EP_TD_1,		"M_EP_TD_1" },
	{  M_EP_TE_1,		"M_EP_TE_1" },
	{  M_EP_TF_1,		"M_EP_TF_1" },
	{  S_IT_TC_1,		"S_IT_TC_1" },
	{  C_SC_NA_1,		"C_SC_NA_1" },
	{  C_DC_NA_1,		"C_DC_NA_1" },
	{  C_RC_NA_1,		"C_RC_NA_1" },
	{  C_SE_NA_1,		"C_SE_NA_1" },
	{  C_SE_NB_1,		"C_SE_NB_1" },
	{  C_SE_NC_1,		"C_SE_NC_1" },
	{  C_BO_NA_1,		"C_BO_NA_1" },
	{  C_SC_TA_1,		"C_SC_TA_1" },
	{  C_DC_TA_1,		"C_DC_TA_1" },
	{  C_RC_TA_1,		"C_RC_TA_1" },
	{  C_SE_TA_1,		"C_SE_TA_1" },
	{  C_SE_TB_1,		"C_SE_TB_1" },
	{  C_SE_TC_1,		"C_SE_TC_1" },
	{  C_BO_TA_1,		"C_BO_TA_1" },
	{  M_EI_NA_1,		"M_EI_NA_1" },
	{  S_CH_NA_1,		"S_CH_NA_1" },
	{  S_RP_NA_1,		"S_RP_NA_1" },
	{  S_AR_NA_1,		"S_AR_NA_1" },
	{  S_KR_NA_1,		"S_KR_NA_1" },
	{  S_KS_NA_1,		"S_KS_NA_1" },
	{  S_KC_NA_1,		"S_KC_NA_1" },
	{  S_ER_NA_1,		"S_ER_NA_1" },
	{  S_US_NA_1,		"S_US_NA_1" },
	{  S_UQ_NA_1,		"S_UQ_NA_1" },
	{  S_UR_NA_1,		"S_UR_NA_1" },
	{  S_UK_NA_1,		"S_UK_NA_1" },
	{  S_UA_NA_1,		"S_UA_NA_1" },
	{  S_UC_NA_1,		"S_UC_NA_1" },
	{  C_IC_NA_1,		"C_IC_NA_1" },
	{  C_CI_NA_1,		"C_CI_NA_1" },
	{  C_RD_NA_1,		"C_RD_NA_1" },
	{  C_CS_NA_1,		"C_CS_NA_1" },
	{  C_RP_NA_1,		"C_RP_NA_1" },
	{  C_TS_TA_1,		"C_TS_TA_1" },
	{  P_ME_NA_1,		"P_ME_NA_1" },
	{  P_ME_NB_1,		"P_ME_NB_1" },
	{  P_ME_NC_1,		"P_ME_NC_1" },
	{  P_AC_NA_1,		"P_AC_NA_1" },
	{  F_FR_NA_1,		"F_FR_NA_1" },
	{  F_SR_NA_1,		"F_SR_NA_1" },
	{  F_SC_NA_1,		"F_SC_NA_1" },
	{  F_LS_NA_1,		"F_LS_NA_1" },
	{  F_AF_NA_1,		"F_AF_NA_1" },
	{  F_SG_NA_1,		"F_SG_NA_1" },
	{  F_DR_TA_1,		"F_DR_TA_1" },
	{  F_SC_NB_1,		"F_SC_NB_1" },
	{ 0, NULL }
};

static const value_string asdu_types_desc_str [] = {
	{  M_SP_NA_1,		"single-point information" },
	{  M_SP_TA_1,		"single-point information with time tag" },
	{  M_DP_NA_1,		"double-point information" },
	{  M_DP_TA_1,		"double-point information with time tag" },
	{  M_ST_NA_1,		"step position information" },
	{  M_ST_TA_1,		"step position information with time tag" },
	{  M_BO_NA_1,		"bitstring of 32 bits" },
	{  M_BO_TA_1,		"bitstring of 32 bits with time tag" },
	{  M_ME_NA_1,		"measured value, normalized value" },
	{  M_ME_TA_1,		"measured value, normalized value with time tag" },
	{  M_ME_NB_1,		"measured value, scaled value" },
	{  M_ME_TB_1,		"measured value, scaled value with time tag" },
	{  M_ME_NC_1,		"measured value, short floating point number" },
	{  M_ME_TC_1,		"measured value, short floating point number with time tag" },
	{  M_IT_NA_1,		"integrated totals" },
	{  M_IT_TA_1,		"integrated totals with time tag" },
	{  M_PS_NA_1,		"packed single-point information with status change detection" },
	{  M_ME_ND_1,		"measured value, normalized value without quality descriptor" },
	{  M_SP_TB_1,		"single-point information with time tag CP56Time2a" },
	{  M_DP_TB_1,		"double-point information with time tag CP56Time2a" },
	{  M_ST_TB_1,		"step position information with time tag CP56Time2a" },
	{  M_BO_TB_1,		"bitstring of 32 bit with time tag CP56Time2a" },
	{  M_ME_TD_1,		"measured value, normalized value with time tag CP56Time2a" },
	{  M_ME_TE_1,		"measured value, scaled value with time tag CP56Time2a" },
	{  M_ME_TF_1,		"measured value, short floating point number with time tag CP56Time2a" },
	{  M_IT_TB_1,		"integrated totals with time tag CP56Time2a" },
	{  M_EP_TD_1,		"event of protection equipment with time tag CP56Time2a" },
	{  M_EP_TE_1,		"packed start events of protection equipment with time tag CP56Time2a" },
	{  M_EP_TF_1,		"packed output circuit information of protection equipment with time tag CP56Time2a" },
	{  S_IT_TC_1,		"integrated totals containing time tagged security statistics" },
	{  C_SC_NA_1,		"single command" },
	{  C_DC_NA_1,		"double command" },
	{  C_RC_NA_1,		"regulating step command" },
	{  C_SE_NA_1,		"set point command, normalized value" },
	{  C_SE_NB_1,		"set point command, scaled value" },
	{  C_SE_NC_1,		"set point command, short floating point number" },
	{  C_BO_NA_1,		"bitstring of 32 bits" },
	{  C_SC_TA_1,		"single command with time tag CP56Time2a" },
	{  C_DC_TA_1,		"double command with time tag CP56Time2a" },
	{  C_RC_TA_1,		"regulating step command with time tag CP56Time2a" },
	{  C_SE_TA_1,		"set point command, normalized value with time tag CP56Time2a" },
	{  C_SE_TB_1,		"set point command, scaled value with time tag CP56Time2a" },
	{  C_SE_TC_1,		"set point command, short floating-point number with time tag CP56Time2a" },
	{  C_BO_TA_1,		"bitstring of 32 bits with time tag CP56Time2a" },
	{  M_EI_NA_1,		"end of initialization" },
	{  S_CH_NA_1,		"authentication challenge" },
	{  S_RP_NA_1,		"authentication reply" },
	{  S_AR_NA_1,		"aggressive mode authentication request session key status request" },
	{  S_KR_NA_1,		"session key status request" },
	{  S_KS_NA_1,		"session key status" },
	{  S_KC_NA_1,		"session key change" },
	{  S_ER_NA_1,		"authentication error" },
	{  S_US_NA_1,		"user status change" },
	{  S_UQ_NA_1,		"update key change request" },
	{  S_UR_NA_1,		"update key change reply" },
	{  S_UK_NA_1,		"update key change symmetric" },
	{  S_UA_NA_1,		"update key change asymmetric" },
	{  S_UC_NA_1,		"update key change confirmation" },
	{  C_IC_NA_1,		"interrogation command" },
	{  C_CI_NA_1,		"counter interrogation command" },
	{  C_RD_NA_1,		"read command" },
	{  C_CS_NA_1,		"clock synchronization command" },
	{  C_RP_NA_1,		"reset process command" },
	{  C_TS_TA_1,		"test command with time tag CP56Time2a" },
	{  P_ME_NA_1,		"parameter of measured value, normalized value" },
	{  P_ME_NB_1,		"parameter of measured value, scaled value" },
	{  P_ME_NC_1,		"parameter of measured value, short floating-point number" },
	{  P_AC_NA_1,		"parameter activation" },
	{  F_FR_NA_1,		"file ready" },
	{  F_SR_NA_1,		"section ready" },
	{  F_SC_NA_1,		"call directory, select file, call file, call section" },
	{  F_LS_NA_1,		"last section, last segment" },
	{  F_AF_NA_1,		"ack file, ack section" },
	{  F_SG_NA_1,		"segment" },
	{  F_DR_TA_1,		"directory" },
	{  F_SC_NB_1,		"Query Log - Request archive file" },
	{ 0, NULL }
};



/* Cause of Transmission (CauseTx) */
#define Per_Cyc         1
#define Back            2
#define Spont           3
#define Init            4
#define Req             5
#define Act             6
#define ActCon          7
#define Deact           8
#define DeactCon        9
#define ActTerm         10
#define Retrem          11
#define Retloc          12
#define File            13
#define Auth            14
#define Seskey          15
#define Usrkey          16
#define Inrogen         20
#define Inro1           21
#define Inro2           22
#define Inro3           23
#define Inro4           24
#define Inro5           25
#define Inro6           26
#define Inro7           27
#define Inro8           28
#define Inro9           29
#define Inro10          30
#define Inro11          31
#define Inro12          32
#define Inro13          33
#define Inro14          34
#define Inro15          35
#define Inro16          36
#define Reqcogen        37
#define Reqco1          38
#define Reqco2          39
#define Reqco3          40
#define Reqco4          41
#define UkTypeId        44
#define UkCauseTx       45
#define UkComAdrASDU    46
#define UkIOA           47

static const value_string causetx_desc_str [] = {
	{ Per_Cyc         ,"Per/Cyc" },
	{ Back            ,"Back" },
	{ Spont           ,"Spont" },
	{ Init            ,"Init" },
	{ Req             ,"Req" },
	{ Act             ,"Act" },
	{ ActCon          ,"ActCon" },
	{ Deact           ,"Deact" },
	{ DeactCon        ,"DeactCon" },
	{ ActTerm         ,"ActTerm" },
	{ Retrem          ,"Retrem" },
	{ Retloc          ,"Retloc" },
	{ File            ,"File" },
	{ Auth            ,"Auth" },
	{ Seskey          ,"Seskey" },
	{ Usrkey          ,"Usrkey" },
	{ Inrogen         ,"Inrogen" },
	{ Inro1           ,"Inro1" },
	{ Inro2           ,"Inro2" },
	{ Inro3           ,"Inro3" },
	{ Inro4           ,"Inro4" },
	{ Inro5           ,"Inro5" },
	{ Inro6           ,"Inro6" },
	{ Inro7           ,"Inro7" },
	{ Inro8           ,"Inro8" },
	{ Inro9           ,"Inro9" },
	{ Inro10          ,"Inro10" },
	{ Inro11          ,"Inro11" },
	{ Inro12          ,"Inro12" },
	{ Inro13          ,"Inro13" },
	{ Inro14          ,"Inro14" },
	{ Inro15          ,"Inro15" },
	{ Inro16          ,"Inro16" },
	{ Reqcogen        ,"Reqcogen" },
	{ Reqco1          ,"Reqco1" },
	{ Reqco2          ,"Reqco2" },
	{ Reqco3          ,"Reqco3" },
	{ Reqco4          ,"Reqco4" },
	{ UkTypeId        ,"UkTypeId" },
	{ UkCauseTx       ,"UkCauseTx" },
	{ UkComAdrASDU    ,"UkComAdrASDU" },
	{ UkIOA           ,"UkIOA" },
	{ 0, NULL }
};

#endif /* IEC104_DATA_TYPES_H_ */
