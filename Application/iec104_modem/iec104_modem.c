/**
 * @file iec104_modem.c
 * @brief Product IEC104 adapter; all device mapping and publication policy lives here.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "iec104_modem.h"

#include "breaker.h"
#include "cp56time2a.h"
#include "iec104_config.h"
#include "iec104_util.h"
#include "nvram.h"
#include "power_board_scp.h"
#include "rf.h"
#include "rtc.h"

#include <string.h>

/** Internal data-provider result, never a library callback contract. */
typedef enum
{
    MODEM_POINT_VALUE,
    MODEM_POINT_SKIP,
    MODEM_POINT_END,
    MODEM_POINT_ERROR
} modem_point_result_t;

/** Ordinal category order used by GI enumeration and periodic publication. */
typedef enum
{
    MODEM_LIVE_CURRENT     = 0U,
    MODEM_LIVE_ENERGY      = 1U,
    MODEM_LIVE_LOAD        = 2U,
    MODEM_LIVE_RF          = 3U,
    MODEM_LIVE_TRIP_FAILED = 4U,
    MODEM_LIVE_FIELD_COUNT = 5U
} modem_live_field_t;

/** Select categories independently for each GI group. */
typedef enum
{
    MODEM_FIELDS_CURRENT     = 1U << MODEM_LIVE_CURRENT,
    MODEM_FIELDS_ENERGY      = 1U << MODEM_LIVE_ENERGY,
    MODEM_FIELDS_LOAD        = 1U << MODEM_LIVE_LOAD,
    MODEM_FIELDS_RF          = 1U << MODEM_LIVE_RF,
    MODEM_FIELDS_TRIP_FAILED = 1U << MODEM_LIVE_TRIP_FAILED,
    MODEM_FIELDS_STATUS      = MODEM_FIELDS_ENERGY | MODEM_FIELDS_LOAD | MODEM_FIELDS_RF | MODEM_FIELDS_TRIP_FAILED,
    MODEM_FIELDS_ALL         = MODEM_FIELDS_CURRENT | MODEM_FIELDS_STATUS
} modem_live_fields_t;

/** Product policy only; APCI/ACT_CON/ACT_TERM stay in the server/core. */
typedef struct
{
    uint8_t qoi;
    uint8_t fields;
    bool with_time;
    bool include_battery;
} modem_live_group_t;

/* GI kapsamlarini burada sec: QOI grubu, veri alanlari ve zaman bilgisi uygulamanin kararidir.
 * Yeni bir canli veri grubu icin tabloya satir eklemek yeterlidir; protokol kodu degismez.
 */
static const modem_live_group_t gi_live_groups[] = {
    /* GI 20 uygulama profili: Type 13 akim, Type 1 durum/aku; zaman alani gonderilmez.
     * Kaynakta zaman bilgisi bulunsa bile bu profil zaman icermeyen tipleri secer.
     */
    { .qoi = IEC104_QOI_STATION, .fields = MODEM_FIELDS_ALL, .with_time = false, .include_battery = true },
    /* GI 21: Type 36 currents, with the original RF receive timestamp. */
    { .qoi = IEC104_QOI_GROUP_1, .fields = MODEM_FIELDS_CURRENT, .with_time = true, .include_battery = false },
    /* GI 22: Type 30 status with time; battery stays untimed Type 1. */
    { .qoi = IEC104_QOI_GROUP_2, .fields = MODEM_FIELDS_STATUS, .with_time = true, .include_battery = true }
};

/* Internal publisher profile, never interpreted as a wire QOI command. */
static const modem_live_group_t spontaneous_live_group = { .fields          = MODEM_FIELDS_ALL,
                                                           .with_time       = true,
                                                           .include_battery = false };

/* Periyodik gonderilecek alanlar burada secilir; GI ve spontane yayin kapsamindan bagimsizdir.
 * Zamanlama send_periodic() icindedir; kutuphane veriyi COT=1 ile paketler. */
static const modem_live_group_t periodic_live_group = { .fields = MODEM_FIELDS_CURRENT | MODEM_FIELDS_ENERGY |
                                                                  MODEM_FIELDS_LOAD | MODEM_FIELDS_RF,
                                                        .with_time       = true,
                                                        .include_battery = false };

/** Four fields belonging to one entry in the existing fault list. */
typedef enum
{
    MODEM_FAULT_CURRENT     = 0U,
    MODEM_FAULT_DURATION    = 1U,
    MODEM_FAULT_ENERGY      = 2U,
    MODEM_FAULT_LOAD        = 3U,
    MODEM_FAULT_FIELD_COUNT = 4U
} modem_fault_field_t;

#define MODEM_LIVE_POINTS (MAX_POWER_LINE_COUNT * PHASE_MAX * MODEM_LIVE_FIELD_COUNT)
#define MODEM_FAULT_POINTS (MAX_POWER_LINE_COUNT * PHASE_MAX * TEMPORARY_FAULT_COUNT * MODEM_FAULT_FIELD_COUNT)
#define MODEM_REPLAY_DELAY_MS 15000U

_Static_assert(TEMPORARY_FAULT_COUNT == PERMANENT_FAULT_COUNT, "Fault IOA ranges require equal list capacities");

/** Map the existing product clock model to library host fields, without wire encoding. */
static iec104_time_t source_time(const cp56time2a_t *time)
{
    return (iec104_time_t){ .milliseconds = time->milliseconds,
                            .minute       = time->minute,
                            .hour         = time->hour,
                            .day          = time->day,
                            .day_of_week  = time->dow,
                            .month        = time->month,
                            .year         = time->year,
                            .invalid      = time->iv_bit != 0U,
                            .summer_time  = time->su_bit != 0U };
}

static uint8_t battery_warning(uint32_t now_ms, bool *active)
{
    *active = false;
    power_board_snapshot_t power;
    if (!power_board_get_snapshot(now_ms, &power) || !power.has_summary ||
        ((power.valid_fields & POWER_VALID_ALARMS) == 0U))
    {
        return IEC104_QUALITY_INVALID;
    }
    const uint32_t alarms = (1UL << 0U) | (1UL << 2U) | (1UL << 3U) | (1UL << 4U) | (1UL << 5U) | (1UL << 7U) |
                            (1UL << 8U) | (1UL << 18U) | (1UL << 20U);
    *active = (power.active_alarms & alarms) != 0U;
    return 0U;
}

/** @brief Resolve a live category/phase to its configured host IOA.
 * @param[in] line Feeder IEC104 configuration from the NVRAM RAM image.
 * @param[in] field MODEM_LIVE_* category, excluding FIELD_COUNT.
 * @param[in] phase Zero-based L1/L2/L3 index.
 * @return Configured IOA, or zero for an unknown/unassigned category.
 * @note Performs no Flash read and does not read the measurement value.
 */
static uint32_t live_ioa(const iec104_line_config_t *line, size_t field, size_t phase)
{
    switch (field)
    {
        case MODEM_LIVE_CURRENT:
            return iec104_ioa_3byte_to_uint32(line->anlik_akim[phase]);
        case MODEM_LIVE_ENERGY:
            return iec104_ioa_3byte_to_uint32(line->enerji_varyok[phase]);
        case MODEM_LIVE_LOAD:
            return iec104_ioa_3byte_to_uint32(line->yuk_akimi_varyok[phase]);
        case MODEM_LIVE_RF:
            return iec104_ioa_3byte_to_uint32(line->rf_haberlesme_varyok[phase]);
        case MODEM_LIVE_TRIP_FAILED:
            return iec104_ioa_3byte_to_uint32(line->trip_failed[phase]);
        default:
            return 0U;
    }
}

/** @brief Select RF value/quality/source time; ASDU helpers own the byte layout.
 * @param[in,out] point Descriptor containing the configured IOA.
 * @param[in] data RF phase snapshot containing a LIVE sample.
 * @param[in] field Requested MODEM_LIVE_* category.
 * @param[in] with_time Product group policy; GI 20 deliberately has no timestamp.
 * @return True when the library accepts the selected point.
 */
static bool live_value(iec104_point_t *point, const rf_phase_data_t *data, size_t field, bool with_time)
{
    iec104_time_t time             = source_time(&data->received_time);
    const iec104_time_t *timestamp = with_time ? &time : NULL;
    uint8_t quality                = 0U;
    if (field == MODEM_LIVE_CURRENT)
    {
        quality = (!data->current_valid || !data->is_online) ? IEC104_QUALITY_INVALID : 0U;
        quality = (uint8_t)(quality | (!data->is_online ? IEC104_QUALITY_NOT_TOPICAL : 0U));
        return iec104_point_set_float(point, point->ioa, data->current_valid ? data->live.current_amps : 0.0F, quality,
                                      timestamp);
    }
    bool flag = field == MODEM_LIVE_ENERGY ? ((data->live.flags & 1U) != 0U) :
                field == MODEM_LIVE_LOAD   ? ((data->live.flags & 2U) != 0U) :
                field == MODEM_LIVE_RF     ? data->is_online :
                                             data->trip_failed;
    if ((field == MODEM_LIVE_ENERGY) || (field == MODEM_LIVE_LOAD))
    {
        quality = (!data->is_online || data->uptime_stalled) ? IEC104_QUALITY_INVALID : 0U;
    }
    else if (field == MODEM_LIVE_TRIP_FAILED)
    {
        quality = time.invalid ? IEC104_QUALITY_INVALID : 0U;
    }
    else
    {
        /* RF off is known, even though the source is stale. */
    }
    if (!data->is_online && (field != MODEM_LIVE_TRIP_FAILED))
    {
        quality = (uint8_t)(quality | IEC104_QUALITY_NOT_TOPICAL);
    }
    return iec104_point_set_single(point, point->ioa, flag, quality, timestamp);
}

/** Read one live/battery position selected entirely by the application's GI or publication policy. */
static modem_point_result_t read_live(iec104_modem_t *modem, const modem_live_group_t *group, size_t index,
                                      iec104_point_t *point)
{
    if (index >= MODEM_LIVE_POINTS)
    {
        if ((index == MODEM_LIVE_POINTS) && group->include_battery && (modem->battery_ioa != 0U))
        {
            bool active;
            uint8_t quality = battery_warning(modem->now_ms, &active);
            return iec104_point_set_single(point, modem->battery_ioa, active, quality, NULL) ? MODEM_POINT_VALUE :
                                                                                               MODEM_POINT_ERROR;
        }
        return MODEM_POINT_END;
    }
    /* index bir IOA degildir: alan -> fider -> faz siralamasindaki uygulama konumudur.
     * Gercek IOA live_ioa() ile fiderin NVRAM RAM goruntusundeki ayarindan alinir. */
    size_t field             = index / (MAX_POWER_LINE_COUNT * PHASE_MAX);
    size_t feeder            = (index / PHASE_MAX) % MAX_POWER_LINE_COUNT;
    size_t phase             = index % PHASE_MAX;
    const power_line_t *line = breaker_get_power_line_by_idx((uint32_t)feeder);
    if ((line == NULL) || (line->iec104.in_use == 0U) || ((group->fields & (1U << field)) == 0U))
    {
        return MODEM_POINT_SKIP;
    }
    *point = (iec104_point_t){ .ioa = live_ioa(&line->iec104, field, phase) };
    if (point->ioa == 0U)
    {
        return MODEM_POINT_SKIP;
    }
    rf_phase_data_t data;
    bool has_data = rf_get_phase_data(feeder, (phase_id_t)phase, modem->now_ms, &data);
    if (has_data && data.has_live)
    {
        return live_value(point, &data, field, group->with_time) ? MODEM_POINT_VALUE : MODEM_POINT_ERROR;
    }
    iec104_time_t time = { .invalid = true };
    uint8_t quality    = IEC104_QUALITY_INVALID;
    bool flag          = false;
    if ((field == MODEM_LIVE_TRIP_FAILED) && has_data)
    {
        /* A latched alarm may exist before any LIVE packet. */
        cp56time2a_t now = cp56time2a_now();
        time             = source_time(&now);
        quality          = time.invalid ? IEC104_QUALITY_INVALID : 0U;
        flag             = data.trip_failed;
    }
    const iec104_time_t *timestamp = group->with_time ? &time : NULL;
    bool valid = field == MODEM_LIVE_CURRENT ? iec104_point_set_float(point, point->ioa, 0.0F, quality, timestamp) :
                                               iec104_point_set_single(point, point->ioa, flag, quality, timestamp);
    return valid ? MODEM_POINT_VALUE : MODEM_POINT_ERROR;
}

/** @brief Select the existing temporary/permanent fault-list IOA getter.
 * @param[in] feeder Zero-based feeder index.
 * @param[in] phase Zero-based phase index.
 * @param[in] permanent_fault Select permanent instead of temporary addresses.
 * @param[in] record List position, zero=newest; replay uses position zero.
 * @param[in] field MODEM_FAULT_* element, excluding FIELD_COUNT.
 * @return Host IOA computed by the existing configuration helper.
 * @note Reads the NVRAM RAM mapping; does not read/consume the stored event.
 */
static uint32_t fault_ioa(uint32_t feeder, uint8_t phase, bool permanent_fault, uint8_t record, uint8_t field)
{
    typedef ioa_3byte_t (*ioa_getter_t)(uint32_t, uint8_t, uint8_t);

    static const ioa_getter_t temporary[MODEM_FAULT_FIELD_COUNT] = {
        [MODEM_FAULT_CURRENT]  = iec104_get_feeder_temporary_fault_ariza_akimi_ioa,
        [MODEM_FAULT_DURATION] = iec104_get_feeder_temporary_fault_ariza_suresi_ioa,
        [MODEM_FAULT_ENERGY]   = iec104_get_feeder_temporary_fault_enerji_varyok_ioa,
        [MODEM_FAULT_LOAD]     = iec104_get_feeder_temporary_fault_yuk_akimi_varyok_ioa
    };
    static const ioa_getter_t permanent[MODEM_FAULT_FIELD_COUNT] = {
        [MODEM_FAULT_CURRENT]  = iec104_get_feeder_permanent_fault_ariza_akimi_ioa,
        [MODEM_FAULT_DURATION] = iec104_get_feeder_permanent_fault_ariza_suresi_ioa,
        [MODEM_FAULT_ENERGY]   = iec104_get_feeder_permanent_fault_enerji_varyok_ioa,
        [MODEM_FAULT_LOAD]     = iec104_get_feeder_permanent_fault_yuk_akimi_varyok_ioa
    };

    ioa_getter_t getter = permanent_fault ? permanent[field] : temporary[field];

    return iec104_ioa_3byte_to_uint32(getter(feeder, phase, record));
}

/** Select a stored fault field; the library serializes its type and source time. */
static bool fault_value(const fault_log_t *fault, uint8_t field, iec104_point_t *point)
{
    iec104_time_t time = source_time(&fault->tm);
    if (field < MODEM_FAULT_ENERGY)
    {
        float value = field == MODEM_FAULT_CURRENT ? fault_log_current_amps(fault) : (float)fault->fault_duration_ms;
        return iec104_point_set_float(point, point->ioa, value, 0U, &time);
    }
    bool value = field == MODEM_FAULT_ENERGY ? fault->info.power_status != 0U :
                                               fault->info.nominal_current_status != 0U;
    return iec104_point_set_single(point, point->ioa, value, 0U, &time);
}

/** Read one product fault-list position using the existing configured IOA helpers. */
static modem_point_result_t read_fault(uint8_t qoi, size_t index, iec104_point_t *point)
{
    if (index >= MODEM_FAULT_POINTS)
    {
        return MODEM_POINT_END;
    }
    size_t feeder            = index / (PHASE_MAX * TEMPORARY_FAULT_COUNT * MODEM_FAULT_FIELD_COUNT);
    uint8_t phase            = (uint8_t)((index / (TEMPORARY_FAULT_COUNT * MODEM_FAULT_FIELD_COUNT)) % PHASE_MAX);
    uint8_t record           = (uint8_t)((index / MODEM_FAULT_FIELD_COUNT) % TEMPORARY_FAULT_COUNT);
    uint8_t field            = (uint8_t)(index % MODEM_FAULT_FIELD_COUNT);
    const power_line_t *line = breaker_get_power_line_by_idx((uint32_t)feeder);
    bool permanent           = qoi == IEC104_QOI_GROUP_4;
    uint32_t base            = line == NULL ? 0U :
                                              iec104_ioa_3byte_to_uint32(permanent ? line->iec104.permanent_fault :
                                                                                     line->iec104.temporary_fault);
    if ((line == NULL) || (line->iec104.in_use == 0U) || (base == 0U))
    {
        return MODEM_POINT_SKIP;
    }
    uint8_t count = permanent ? fault_log_get_perm_count((uint8_t)feeder, phase) :
                                fault_log_get_temp_count((uint8_t)feeder, phase);
    if (record >= count)
    {
        return MODEM_POINT_SKIP;
    }
    fault_log_t fault;
    fault_log_type_t type = permanent ? FAULT_LOG_TYPE_PERMANENT : FAULT_LOG_TYPE_TEMPORARY;
    if (!fault_log_read_nth((uint8_t)feeder, phase, type, record, &fault) || (fault.info.feeder != feeder) ||
        (fault.info.phase != phase) || (fault.info.type != (uint8_t)type))
    {
        return MODEM_POINT_ERROR;
    }
    *point = (iec104_point_t){ .ioa = fault_ioa((uint32_t)feeder, phase, permanent, record, field) };
    if (point->ioa == 0U)
    {
        return MODEM_POINT_SKIP;
    }
    return fault_value(&fault, field, point) ? MODEM_POINT_VALUE : MODEM_POINT_ERROR;
}

/** @brief GI grubunu dogrular ve her cagrida sinirli bir cevap parcasi hazirlar.
 * @param[in,out] modem Veri kaynaklari ve siradaki noktanin query_index konumu.
 * @param[in] request Ilk GI istegi veya server tarafindan uretilen devam cagrisi.
 * @param[in,out] response Ayni Type ID ile doldurulacak ASDU ve devam bilgisi.
 * @return Uygulama sonucu; COT ve negatif cevap bitini server belirler.
 * @note continuation bir protokol biti veya TCP parcalanmasi degildir. Ilk cagrida
 * false olur; grup kabul edilip konum sifirlanir. ACT_CON sonrasinda server her poll
 * icin en fazla bir kez continuation=true ile siradaki ASDU verisini ister.
 * Her cagrida en fazla 32 konum taranir; boylece Contiki ana dongusu bloke edilmez.
 */
static iec104_request_result_t handle_gi(iec104_modem_t *modem, const iec104_request_t *request,
                                         iec104_response_t *response)
{
    uint8_t qoi                     = request->qualifier;
    bool fault                      = (qoi == IEC104_QOI_GROUP_3) || (qoi == IEC104_QOI_GROUP_4);
    const modem_live_group_t *group = NULL;
    for (size_t index = 0U; index < sizeof(gi_live_groups) / sizeof(gi_live_groups[0]); index++)
    {
        if (gi_live_groups[index].qoi == qoi)
        {
            group = &gi_live_groups[index];
        }
    }
    if (!fault && (group == NULL))
    {
        return IEC104_REQUEST_UNSUPPORTED;
    }
    /* more=true: bu GI icin okunacak baska konumlar var; henuz ACT_TERM gonderme. */
    response->more = true;
    if (!request->continuation)
    {
        modem->query_index = 0U;
        return IEC104_REQUEST_ACCEPTED;
    }
    for (size_t work = 0U; work < 32U; work++)
    {
        iec104_point_t point;
        modem_point_result_t result = fault ? read_fault(qoi, modem->query_index, &point) :
                                              read_live(modem, group, modem->query_index, &point);
        if (result == MODEM_POINT_END)
        {
            /* Tum noktalar bitti; server son veri parcasindan sonra ACT_TERM gonderir. */
            response->more = false;
            break;
        }
        if (result == MODEM_POINT_ERROR)
        {
            return IEC104_REQUEST_ERROR;
        }
        /* Tek ASDU farkli Type ID'leri karistiramaz; doluysa veya tip degisirse burada bitir.
         * Eklenemeyen noktanin index'i ilerlemez; sonraki devam cagrisi ayni noktadan baslar.
         * Server hazir ASDU'yu TX/K penceresi acilana kadar saklar; veri yeniden okunmaz. */
        if (result == MODEM_POINT_VALUE && !iec104_response_add(response, &point))
        {
            return response->asdu.length == 6U ? IEC104_REQUEST_ERROR : IEC104_REQUEST_ACCEPTED;
        }
        modem->query_index++;
    }
    return IEC104_REQUEST_ACCEPTED;
}

static bool apply_clock(const iec104_time_t *time);

/** @brief Cozulmus istegi uygulamanin veri/cihaz servislerine yonlendirir.
 * GI kapsami, IOA anlami ve izin verilen komut degeri burada belirlenir.
 * Donen IEC104_REQUEST_* degeri kabloya yazilmaz; server uygun COT/PN cevabini uretir.
 * Reset burada baslatilmaz: onay cevabi TX tarafina kabul edildikten sonra ayri callback calisir.
 */
static iec104_request_result_t handle_request(void *context, const iec104_request_t *request,
                                              iec104_response_t *response)
{
    iec104_modem_t *modem = context;
    switch (request->asdu.type)
    {
        case IEC104_TYPE_C_IC_NA_1:
            return handle_gi(modem, request, response);
        case IEC104_TYPE_C_CS_NA_1:
            return apply_clock(&request->time) ? IEC104_REQUEST_ACCEPTED : IEC104_REQUEST_REJECTED;
        case IEC104_TYPE_C_RP_NA_1:
            /* IOA/QRP ve SBO izin kontrolunu server yapti; burada yalniz kabul bildirilir. */
            return IEC104_REQUEST_ACCEPTED;
        case IEC104_TYPE_C_SC_NA_1:
            /* Type 45 yalniz yapilandirilmis reset IOA'sinda state=1, QU=0 ile desteklenir.
             * Select/Execute eslesmesi ve timeout server'da; fiziksel eylem cevap sonrasindadir. */
            if ((modem->server.config.reset_ioa == 0U) || (request->ioa != modem->server.config.reset_ioa))
            {
                return IEC104_REQUEST_UNKNOWN_IOA;
            }
            return request->single_command.state && (request->single_command.qualifier == 0U) ?
                       IEC104_REQUEST_ACCEPTED :
                       IEC104_REQUEST_REJECTED;
        default:
            return IEC104_REQUEST_UNSUPPORTED;
    }
}

static bool write_frame(void *context, const uint8_t *frame, size_t length)
{
    iec104_modem_t *modem = context;
    return modem->io.write(modem->io.context, frame, length);
}

static bool apply_clock(const iec104_time_t *time)
{
    rtc_t clock = { .millisec = (uint16_t)(time->milliseconds % 1000U),
                    .second   = (uint8_t)(time->milliseconds / 1000U),
                    .minute   = time->minute,
                    .hour     = time->hour,
                    .day      = time->day,
                    .month    = time->month,
                    .year     = time->year };
    return rtc_sync(&clock);
}

/** @brief Server reset bildirimini disaridan baglanan cihaz servisine aktarir.
 * Cagri yolu: server.io.request_reset -> forward_reset_request -> modem.io.request_reset.
 * Son isaretci init() cagiranin verdigi port callback'idir; bu fonksiyona geri donmez.
 * Server son olumlu cevabi TX'e kabul ettirmistir; bu, SCADA ACK'i alindi demek degildir.
 * Cihaz servisi mevcut gecikmeli kapatma/yeniden baslatma islemini zamanlamalidir.
 */
static void forward_reset_request(void *context)
{
    iec104_modem_t *modem = context;
    modem->io.request_reset(modem->io.context);
}

static void link_event(void *context, iec104_core_event_t event)
{
    iec104_modem_t *modem = context;
    if (event == IEC104_CORE_LINK_STARTED)
    {
        modem->started_ms = modem->now_ms;
    }
    else if (event != IEC104_CORE_LINK_STOPPED)
    {
        modem->replay_loaded  = false;
        modem->replay_waiting = false;
        modem->replay_stage   = 0U;
        (void)iec104_event_log_sync();
        modem->replay_dirty = false;
        if (event != IEC104_CORE_DISCONNECTED)
        {
            modem->io.request_close(modem->io.context);
        }
    }
    else
    {
        /* Preserve publication cursors across STOP/START. */
    }
    if (modem->io.event != NULL)
    {
        modem->io.event(modem->io.context, event);
    }
}

/** @brief Consume the replay record only after its final ASDU is acknowledged.
 * @param[in,out] context Modem instance with the pending journal sequence.
 * @param[in] tag Core ACK tag; must match replay_sequence + 1 (zero is unused).
 * @note Clears the in-flight record and marks journal state dirty for sync.
 * Transport acceptance alone never enters this callback.
 */
static void delivered(void *context, uint32_t tag)
{
    iec104_modem_t *modem = context;
    if (modem->replay_waiting && (tag == (uint32_t)modem->replay_sequence + 1U))
    {
        iec104_event_log_mark_sent(modem->replay_sequence);
        modem->replay_loaded  = false;
        modem->replay_waiting = false;
        modem->replay_stage   = 0U;
        modem->replay_dirty   = true;
    }
}

bool iec104_modem_init(iec104_modem_t *modem, const iec104_modem_io_t *io)
{
    /* Bu getter Flash okumaz: baslangicta yuklenmis NVRAM ayarlarinin RAM goruntusunu verir.
     * Global ayarlar burada kopyalanir; fider IOA'lari read_live/read_fault yolunda cozulur. */
    const iec104_config_t *config = nvram_get_iec104_config();
    if ((modem == NULL) || (io == NULL) || (config == NULL) || (io->write == NULL) || (io->request_close == NULL) ||
        (io->request_reset == NULL) || (config->periodical_send_interval > INT32_MAX / 1000U))
    {
        return false;
    }
    iec104_core_config_t link   = { .k     = config->k_max,
                                    .w     = config->w_max,
                                    .t1_ms = (uint32_t)config->t1_max * 1000U,
                                    .t2_ms = (uint32_t)config->t2_max * 1000U,
                                    .t3_ms = ((uint32_t)config->t3_max + 2U) * 1000U };
    iec104_server_config_t app  = { .common_address = config->common_address,
                                    .originator     = config->originator_address,
                                    .reset_ioa      = iec104_ioa_3byte_to_uint32(config->ioa_modem_reset),
                                    .reset_sbo      = config->is_sbo_active != 0U,
                                    .sbo_timeout_ms = (uint32_t)config->sbo_execute_timeout * 1000U };
    iec104_server_io_t services = { .context        = modem,
                                    .write          = write_frame,
                                    .handle_request = handle_request,
                                    .request_reset  = forward_reset_request,
                                    .event          = link_event,
                                    .delivered      = delivered };
    if (!iec104_server_init(&modem->server, &link, &app, &services))
    {
        return false;
    }
    /* Server context olarak modem'i alir; cihaz portu ise cagiranin kendi context'ini kullanir.
     * Boylece protokol kutuphanesi GSM, RTC ve NVRAM uygulamasini bilmez. */
    modem->io             = *io;
    modem->battery_ioa    = iec104_ioa_3byte_to_uint32(config->ioa_aku_uyarisi);
    modem->period_ms      = config->periodical_send_interval * 1000U;
    modem->replay_loaded  = false;
    modem->replay_waiting = false;
    modem->replay_dirty   = false;
    modem->replay_stage   = 0U;
    modem->replay_ready   = false;
    return true;
}

void iec104_modem_open(iec104_modem_t *modem, uint32_t now_ms)
{
    if (modem != NULL)
    {
        modem->now_ms = now_ms;
        iec104_server_open(&modem->server, now_ms);
        modem->server.query_qoi = 0U;
        modem->query_index      = 0U;
        /* Initial snapshot starts at RF, followed by trip-failure fields. */
        modem->snapshot_index   = MODEM_LIVE_RF * MAX_POWER_LINE_COUNT * PHASE_MAX;
        modem->snapshot_done    = false;
        modem->periodic_pending = false;
        modem->periodic_since   = now_ms;
        modem->battery_sent     = false;
        modem->replay_ready     = false;
    }
}

void iec104_modem_close(iec104_modem_t *modem)
{
    if (modem != NULL)
    {
        iec104_server_close(&modem->server);
    }
}

size_t iec104_modem_on_data_received(iec104_modem_t *modem, const uint8_t *data, size_t length, uint32_t now_ms)
{
    if (modem == NULL)
    {
        return 0U;
    }
    /* TCP tek ASDU'ya hizali gelmek zorunda degildir; core parcalari birlestirip APDU'yu dogrular.
     * Cozulen ASDU server'a, dogrulanmis uygulama istegi handle_request()'e ulasir. */
    modem->now_ms = now_ms;
    return iec104_server_process_data(&modem->server, data, length, now_ms);
}

/** @brief Publish one same-type live batch for snapshot/periodic enumeration.
 * @param[in,out] modem Server and current product data sources.
 * @param[in] group Product publication profile.
 * @param[in,out] index Next ordinal; advances on accepted data or skipped points.
 * @param[in] end Exclusive ordinal limit for this publication range.
 * @param[in] periodic True selects COT=1; false selects spontaneous COT=3.
 * @return True once the range is exhausted; false means more work/backpressure,
 * not necessarily a failed send (a partial batch may already be accepted).
 * @note Her cagrida en fazla 32 konum taranir, 16 nokta dogrudan ASDU'ya eklenir.
 * Ara nokta dizisi yoktur; TX reddinde aday cursor yayinlanmaz.
 */
static bool publish_live(iec104_modem_t *modem, const modem_live_group_t *group, size_t *index, size_t end,
                         bool periodic)
{
    iec104_response_t response;
    response.more        = false;
    iec104_asdu_t header = { .cause      = periodic ? IEC104_COT_PERIODIC : IEC104_COT_SPONTANEOUS,
                             .address    = modem->server.config.common_address,
                             .originator = modem->server.config.originator };
    if (!iec104_builder_begin(&response.asdu, &header, 0U))
    {
        return false;
    }
    size_t count  = 0U;
    size_t cursor = *index;
    for (size_t work = 0U; (work < 32U) && (cursor < end); work++)
    {
        iec104_point_t point;
        modem_point_result_t result = read_live(modem, group, cursor, &point);
        if (result == MODEM_POINT_ERROR)
        {
            return false;
        }
        if (result == MODEM_POINT_VALUE)
        {
            /* Ayni GI yardimcisi tip/boyut sinirini korur; sigmayan nokta sonraki parcaya kalir. */
            if (!iec104_response_add(&response, &point))
            {
                if (count == 0U)
                {
                    return false;
                }
                break;
            }
            count++;
            if (count == 16U)
            {
                cursor++;
                break;
            }
        }
        cursor++;
    }
    bool accepted = (count == 0U) || iec104_server_publish_asdu(&modem->server, &response.asdu, 0U);
    if (accepted)
    {
        *index = cursor;
    }
    return *index >= end;
}

/** @brief Encode the current durable record's next homogeneous ASDU point set.
 * @param[in] modem Cached record and current two-frame fault replay stage.
 * @param[out] points Up to two descriptors; alarm records produce one.
 * @param[out] count Number of descriptors on success.
 * @param[out] last True when this set completes the record's ASDU sequence.
 * @return False if the record kind/identity/configuration cannot be represented.
 * @note No send or journal consumption occurs here; the final set gets the tag.
 */
static bool replay_points(iec104_modem_t *modem, iec104_point_t points[2], size_t *count, bool *last)
{
    const iec104_event_record_t *record = &modem->replay_record;
    if (record->kind == IEC104_EVENT_TRIP_FAILURE)
    {
        const iec104_alarm_record_t *alarm = &record->payload.alarm;
        const power_line_t *line           = breaker_get_power_line_by_idx(alarm->feeder);
        if ((line == NULL) || (line->iec104.in_use == 0U) || (alarm->phase >= PHASE_MAX))
        {
            return false;
        }
        iec104_time_t time = source_time(&alarm->time);
        uint32_t ioa       = live_ioa(&line->iec104, MODEM_LIVE_TRIP_FAILED, alarm->phase);
        if (!iec104_point_set_single(&points[0], ioa, alarm->active != 0U, 0U, &time))
        {
            return false;
        }
        *count = 1U;
        *last  = true;
        return points[0].ioa != 0U;
    }
    if (record->kind != IEC104_EVENT_FAULT)
    {
        return false;
    }
    const fault_log_t *fault = &record->payload.fault;
    const power_line_t *line = breaker_get_power_line_by_idx(fault->info.feeder);
    if ((line == NULL) || (line->iec104.in_use == 0U) || (fault->info.phase >= PHASE_MAX) ||
        (iec104_ioa_3byte_to_uint32(fault->info.type != 0U ? line->iec104.permanent_fault :
                                                             line->iec104.temporary_fault) == 0U))
    {
        return false;
    }
    for (uint8_t index = 0U; index < 2U; index++)
    {
        uint8_t field = (uint8_t)(modem->replay_stage * 2U + index);
        points[index] = (iec104_point_t){ .ioa = fault_ioa(fault->info.feeder, fault->info.phase,
                                                           fault->info.type != 0U, 0U, field) };
        if (!fault_value(fault, field, &points[index]))
        {
            return false;
        }
    }
    *count = 2U;
    *last  = modem->replay_stage != 0U;
    return true;
}

/** @brief Progress one newest-first durable record, respecting final-frame ACK.
 * @param[in,out] modem Cache/stage/waiting state for at most one record.
 * @note Keeps the record/stage on backpressure, waits after the final send,
 * and leaves unrepresentable events unsent. Only delivered() consumes storage.
 */
static void replay(iec104_modem_t *modem)
{
    if (modem->replay_waiting)
    {
        return;
    }
    if (!modem->replay_loaded)
    {
        modem->replay_loaded = iec104_event_log_read_newest_unsent(&modem->replay_record, &modem->replay_sequence);
        if (!modem->replay_loaded)
        {
            if (modem->replay_dirty)
            {
                if (iec104_event_log_sync() == 0)
                {
                    modem->replay_dirty = false;
                }
            }
            return;
        }
    }
    iec104_point_t points[2];
    size_t count = 0U;
    bool last    = false;
    if (!replay_points(modem, points, &count, &last))
    {
        /* Keep unrepresentable records durable; never claim delivery. */
        return;
    }
    uint32_t tag = last ? (uint32_t)modem->replay_sequence + 1U : 0U;
    if (iec104_server_publish(&modem->server, points, count, tag))
    {
        modem->replay_waiting = last;
        modem->replay_stage++;
    }
}

/** @brief Periyodik profili mevcut zaman bilgisiyle parcalar halinde gonderir.
 * period_ms=0 ise kapatilir. Tam tur bitince yeni aralik baslar; TX kabul edilmezse
 * ayni konum korunur. Her poll tum listeyi tek seferde gondermeye calismaz.
 */
static void send_periodic(iec104_modem_t *modem, uint32_t now_ms)
{
    if (modem->period_ms == 0U)
    {
        modem->periodic_pending = false;
        return;
    }
    if (((uint32_t)(now_ms - modem->periodic_since) >= modem->period_ms) && !modem->periodic_pending)
    {
        modem->periodic_pending = true;
        modem->periodic_index   = 0U;
    }
    if (modem->periodic_pending &&
        publish_live(modem, &periodic_live_group, &modem->periodic_index, MODEM_LIVE_POINTS, true))
    {
        modem->periodic_pending = false;
        modem->periodic_since   = now_ms;
    }
}

void iec104_modem_poll(iec104_modem_t *modem, uint32_t now_ms)
{
    if (modem == NULL)
    {
        return;
    }
    modem->now_ms = now_ms;
    /* Once protokol cevaplari/GI ilerler; asagidaki yayinlar bu islerin onune gecmez. */
    iec104_server_poll(&modem->server, now_ms);
    if ((modem->server.core.state != IEC104_CORE_STARTED) || (modem->server.query_state != IEC104_QUERY_IDLE) ||
        modem->server.response_pending || modem->server.initialization_pending)
    {
        return;
    }
    /* Yayin sirasi: ilk durum goruntusu, aku degisimi, kalici olay replay, periyodik veri. */
    if (!modem->snapshot_done)
    {
        modem->snapshot_done = publish_live(modem, &spontaneous_live_group, &modem->snapshot_index, MODEM_LIVE_POINTS,
                                            false);
        return;
    }
    bool battery_active;
    uint8_t battery_quality = battery_warning(now_ms, &battery_active);
    iec104_point_t battery;
    if ((modem->battery_ioa != 0U) && (!modem->battery_sent || (modem->battery_active != battery_active) ||
                                       (modem->battery_quality != battery_quality)))
    {
        if (iec104_point_set_single(&battery, modem->battery_ioa, battery_active, battery_quality, NULL) &&
            iec104_server_publish(&modem->server, &battery, 1U, 0U))
        {
            modem->battery_active  = battery_active;
            modem->battery_quality = battery_quality;
            modem->battery_sent    = true;
        }
        return;
    }
    if (modem->replay_ready || ((uint32_t)(now_ms - modem->started_ms) >= MODEM_REPLAY_DELAY_MS) ||
        (modem->server.query_qoi >= IEC104_QOI_GROUP_3))
    {
        replay(modem);
    }
    send_periodic(modem, now_ms);
}

void iec104_modem_notify_event(iec104_modem_t *modem)
{
    if (modem != NULL)
    {
        modem->replay_ready = true;
    }
}
/*** end of file ***/
