/**
 * @file rf_json.h
 * @brief GET /config/rf yanit govdesinin ureticisi (tablo-tabanli).
 *
 * Tek alan tanim tablosu + tek emitor. Yanit yalnizca KONFIG icerir
 * (inUse, EUI-64 kimlikleri, 96B blok alanlari, Unassigned kesif
 * listesi); durum/telemetri /monitor/rf uc noktasindir.
 *
 * Bagimliliklar dar tutulmustur (xsnprintf + rf_config/rf_discovery)
 * boylece host-side altin-cikti testi mumkun olur.
 */

#ifndef WEB_SERVER_RF_JSON_H_
#define WEB_SERVER_RF_JSON_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RF config JSON govdesini buf'a yaz.
 *
 * @param[out] buf       Cikis tamponu.
 * @param[in]  buf_size  Tampon boyu.
 * @return Yazilan bayt sayisi (NUL haric). buf_size asilirsa cikti
 *         kirilir ve donus buf_size-1'i gecmez; cagiran denetlemelidir.
 */
int rf_json_config_build(char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_RF_JSON_H_ */
