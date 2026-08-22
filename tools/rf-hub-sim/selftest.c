/*
 * selftest.c
 *
 *  Created on: Aug 22, 2026
 *      Author: fatih
 *
 * Hub simulatoru kendi-kendini-testi: ayni hub kodunu, seri port
 * olmadan, sahte RTU istekleriyle eksersiz eder. `make test` ile
 * calisir; donanim baglantisi olmadan davranis dogrulamasi saglar.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "scp.h"
#include "hub.h"

static unsigned int test_pass = 0U;
static unsigned int test_fail = 0U;

#define TEST_CHECK(cond, name)                                          \
    do                                                                  \
    {                                                                   \
        if ((cond) != 0)                                                \
        {                                                               \
            test_pass++;                                                \
            printf("PASS: %s\r\n", (name));                             \
        }                                                               \
        else                                                            \
        {                                                               \
            test_fail++;                                                \
            printf("FAIL: %s  (%s:%d)\r\n", (name), __FILE__, __LINE__);\
        }                                                               \
    } while (0)

/* ---- yanit yakalama ---- */
#define CAP_MAX 8
static scp_packet_t cap[CAP_MAX];
static unsigned int cap_count;

static void capture_send(const scp_packet_t *pkt, void *user)
{
    (void)user;
    if (cap_count < CAP_MAX)
    {
        cap[cap_count] = *pkt;
        cap_count++;
    }
}

static void cap_clear(void)
{
    cap_count = 0U;
}

static unsigned int send_req(hub_t *hub, uint8_t type, uint8_t cmd,
                             uint8_t seq, const uint8_t *body, uint8_t len)
{
    scp_packet_t req;

    cap_clear();
    (void)memset(&req, 0, sizeof(req));
    req.dst = 0x01U;      /* HUB */
    req.src = 0x02U;      /* RTU */
    req.type = type;
    req.cmd = cmd;
    req.seq = seq;
    req.data_len = len;
    if ((body != NULL) && (len > 0U))
    {
        (void)memcpy(req.data, body, len);
    }
    hub_on_packet(hub, &req);
    return cap_count;
}

static uint16_t rd16(const uint8_t *b)
{
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static uint32_t rd32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static float rdf32(const uint8_t *b)
{
    uint32_t v = rd32(b);
    float f;
    (void)memcpy(&f, &v, sizeof(f));
    return f;
}

static const uint8_t EUI_A[8] =
    {0xA4U, 0x05U, 0x67U, 0x82U, 0x1CU, 0x3BU, 0x9FU, 0x40U};
static const uint8_t EUI_B[8] =
    {0xA4U, 0x05U, 0x67U, 0x82U, 0x1CU, 0x3BU, 0xA0U, 0x50U};

static bool floats_close(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
    {
        d = -d;
    }
    return (d < 0.01f) ? true : false;
}

int main(void)
{
    hub_t hub;
    uint8_t seq = 1U;
    uint8_t body[16];
    uint8_t inv[12];
    uint8_t write_body[104];

    printf("=== rf-hub-sim kendi-kendini-testi ===\r\n\r\n");

    hub_init(&hub, capture_send, NULL, 0U);   /* R0 modu */

    /* --- 1) GET_STATUS --- */
    hub_tick(&hub, 1000U);
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x01U, seq++, NULL, 0U) == 1U,
               "GET_STATUS tek ACK");
    TEST_CHECK((cap[0].type == SCP_TYPE_ACK) && (cap[0].data_len == 25U) &&
               (cap[0].seq == (uint8_t)(seq - 1U)),
               "GET_STATUS ACK 25 B, seq kopya");
    TEST_CHECK(rd32(&cap[0].data[21]) == 0U, "GET_STATUS cycles=0 (henuz)");

    /* --- 2) PING: unicast ACK, broadcast yanitsiz --- */
    TEST_CHECK(send_req(&hub, SCP_TYPE_PING, 0x00U, seq++, NULL, 0U) == 1U,
               "PING unicast -> ACK");
    cap_clear();
    {
        scp_packet_t bp;
        (void)memset(&bp, 0, sizeof(bp));
        bp.dst = SCP_BROADCAST_ADDR;
        bp.src = 0x02U;
        bp.type = SCP_TYPE_PING;
        bp.cmd = 0x00U;
        bp.seq = seq;
        bp.data_len = 0U;
        hub_on_packet(&hub, &bp);
        TEST_CHECK(cap_count == 0U, "broadcast PING -> yanit YOK");
    }

    /* --- 3) TIME_SYNC: once yanlis uzunluk --- */
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x07U, seq++, body, 5U) == 1U,
               "TIME_SYNC LEN=5 -> ERROR");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x02U),
               "TIME_SCROLL yanlis LEN -> ERROR 0x02");

    (void)memset(body, 0, sizeof(body));
    body[2] = 30U;                 /* dakika */
    body[3] = 12U;                 /* saat   */
    body[4] = 21U;                 /* gun    */
    body[5] = 8U;                  /* ay     */
    body[6] = 26U;                 /* yil    */
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x07U, seq++, body, 7U) == 1U,
               "TIME_SYNC gecerli -> ACK");
    TEST_CHECK(cap[0].type == SCP_TYPE_ACK, "TIME_SYNC ACK");

    /* --- 4) INVENTORY_SET x2 + END --- */
    (void)memset(inv, 0, sizeof(inv));
    inv[0] = 1U; inv[1] = 1U; inv[2] = 1U;
    (void)memcpy(&inv[3], EUI_A, 8U);
    inv[11] = 11U;                                 /* channel */
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x04U, seq++, inv, 12U) == 1U,
               "INVENTORY_SET A -> ACK");

    inv[0] = 1U; inv[1] = 1U; inv[2] = 2U;
    (void)memcpy(&inv[3], EUI_B, 8U);
    inv[11] = 12U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x04U, seq++, inv, 12U) == 1U,
               "INVENTORY_SET B -> ACK");

    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x05U, seq++, NULL, 0U) == 1U,
               "INVENTORY_END -> ACK");

    /* --- 5) CFG_READ: bilinmeyen EUI 0x05, bilinen 96 B --- */
    {
        uint8_t unknown[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
        TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x20U, seq++, unknown, 8U) == 1U,
                   "CFG_READ bilinmeyen EUI");
        TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) &&
                   (cap[0].data[0] == 0x05U),
                   "CFG_READ bilinmeyen -> ERROR 0x05");
    }

    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x20U, seq++, EUI_A, 8U) == 1U,
               "CFG_READ A");
    TEST_CHECK((cap[0].type == SCP_TYPE_ACK) && (cap[0].data_len == 96U),
               "CFG_READ A -> 96 B");
    TEST_CHECK((cap[0].data[0] == 1U) && (cap[0].data[1] == 1U) &&
               (cap[0].data[2] == 1U),
               "CFG_READ blogunda zone/fider/phase envanterden");
    TEST_CHECK(floats_close(rdf32(&cap[0].data[3]), 6.0f),
               "CFG_READ default nominal=6.0");

    /* --- 6) 0x05 enjeksiyonu: bir kez hata, sonra basari --- */
    hub.stale_0x20_next = 1U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x20U, seq++, EUI_A, 8U) == 1U,
               "CFG_READ (0x05 enjeksiyonu)");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) &&
               (cap[0].data[0] == 0x05U),
               "enjeksiyon: ERROR 0x05 (tekrar sor)");
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x20U, seq++, EUI_A, 8U) == 1U,
               "CFG_READ tekrar");
    TEST_CHECK(cap[0].type == SCP_TYPE_ACK, "tekrar istek basarili");

    /* --- 7) CFG_WRITE: LEN hatasi, STAGED, COMMIT, APPLIED --- */
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x22U, seq++, write_body, 103U) == 1U,
               "CFG_WRITE LEN=103");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x02U),
               "CFG_WRITE yanlis LEN -> ERROR 0x02");

    (void)memcpy(&write_body[0], EUI_A, 8U);
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x20U, seq++, EUI_A, 8U) == 1U,
               "RMW: mevcut blogu oku");
    (void)memcpy(&write_body[8], cap[0].data, 96U);
    {
        float nominal = 10.0f;
        (void)memcpy(&write_body[8 + 3], &nominal, 4U);  /* Nominal=10 */
    }
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x22U, seq++, write_body, 104U) == 1U,
               "CFG_WRITE 104 B -> ACK");
    TEST_CHECK(cap[0].type == SCP_TYPE_ACK, "CFG_WRITE ACK");

    body[0] = 7U;                                        /* group_id */
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x24U, seq++, body, 1U) == 1U,
               "CFG_COMMIT -> ACK");

    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x28U, seq++, body, 1U) == 1U,
               "CFG_STATUS_GET");
    TEST_CHECK((cap[0].data_len == 8U) && (cap[0].data[1] == HUB_G_DELIVERED),
               "0x28: DELIVERED (uygulama beklemede)");

    hub_tick(&hub, 2500U);                               /* apply gecikmesi */
    TEST_CHECK(cap_count >= 1U, "0x21 bildirimi geldi");
    TEST_CHECK((cap[cap_count - 1U].cmd == 0x21U) &&
               (cap[cap_count - 1U].data[1] == HUB_G_APPLIED),
               "0x21: APPLIED");

    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x20U, seq++, EUI_A, 8U) == 1U,
               "CFG_WRITE sonrasi okuma");
    TEST_CHECK(floats_close(rdf32(&cap[0].data[3]), 10.0f),
               "yeni nominal=10.0 (masked alanlar korunmus mu?)");
    TEST_CHECK((cap[0].data[0] == 1U) && (cap[0].data[1] == 1U),
               "zone/fider cihaz degerinde korundu");

    /* --- 8) COMMIT basarisizlik enjeksiyonu --- */
    hub.commit_fail = 1U;
    hub.commit_fail_reason = 3U;                          /* RANGE */
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x22U, seq++, write_body, 104U) == 1U,
               "ikinci CFG_WRITE");
    body[0] = 8U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x24U, seq++, body, 1U) == 1U,
               "ikinci COMMIT");
    hub_tick(&hub, 5000U);
    TEST_CHECK((cap_count >= 1U) && (cap[cap_count - 1U].cmd == 0x21U) &&
               (cap[cap_count - 1U].data[1] == HUB_G_FAILED) &&
               (cap[cap_count - 1U].data[3] == 3U),
               "0x21: FAILED + sebep RANGE");

    /* --- 9) SET tekrar (idempotent) politikesi --- */
    {
        uint8_t set_seq = seq;
        (void)memset(body, 0, sizeof(body));
        body[2] = 31U;
        TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x07U, set_seq, body, 7U) == 1U,
                   "TIME_SYNC (tekrar testi tabani)");
        TEST_CHECK(cap[0].type == SCP_TYPE_ACK, "ilk SET ACK");
        {
            scp_packet_t first_reply = cap[0];

            /* ayni SEQ, araya istek sokulmadi -> yanit AYNEN tekrar */
            TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x07U, set_seq, body, 7U) == 1U,
                       "DUP SET (ayni SEQ)");
            TEST_CHECK((cap[0].type == first_reply.type) &&
                       (cap[0].seq == first_reply.seq) &&
                       (cap[0].data_len == first_reply.data_len),
                       "DUP SET yaniti birebir ayni");
        }
        /* araya baska istek girince ayni SEQ YENI islenir */
        (void)send_req(&hub, SCP_TYPE_GET, 0x01U, seq++, NULL, 0U);
        TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x07U, set_seq, body, 7U) == 1U,
                   "araya istek girdi -> SET yeniden islendi");
        seq++;
    }

    /* --- 10) Olay halkasi --- */
    hub_add_events(&hub, 3U, 4U);
    TEST_CHECK(hub.pending == 3U, "3 olay eklendi, pending=3");

    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x40U, seq++, NULL, 0U) == 1U,
               "LOG_HEAD R0");
    TEST_CHECK(cap[0].data_len == 8U, "R0: 8 B (tail yok)");
    TEST_CHECK((rd16(&cap[0].data[0]) == 3U) &&
               (rd32(&cap[0].data[4]) == 3U),
               "R0: head=3 total=3");

    hub.r1_mode = 1U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x40U, seq++, NULL, 0U) == 1U,
               "LOG_HEAD R1");
    TEST_CHECK((cap[0].data_len == 10U) && (rd16(&cap[0].data[8]) == 0U),
               "R1: 10 B, tail=0");
    hub.r1_mode = 0U;

    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x42U, seq++, body, 2U) == 1U,
               "LOG_RECORD idx=0");
    TEST_CHECK(cap[0].data_len == 60U, "kayit 60 B");
    body[0] = 100U; body[1] = 0U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x42U, seq++, body, 2U) == 1U,
               "LOG_RECORD idx=100");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x02U),
               "idx=100 -> ERROR 0x02");

    body[0] = 0U; body[1] = 0U; body[2] = 4U; body[3] = 0U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x44U, seq++, body, 4U) == 1U,
               "LOG_RANGE 0..3");
    TEST_CHECK(cap[0].data_len == 240U, "4 kayit = 240 B");
    body[2] = 5U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x44U, seq++, body, 4U) == 1U,
               "LOG_RANGE count=5");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x02U),
               "count=5 -> ERROR 0x02 (tek cerceve limiti)");

    body[0] = 3U; body[1] = 0U;                          /* X=3 (haric) */
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x46U, seq++, body, 2U) == 1U,
               "LOG_CONSUME X=3");
    TEST_CHECK((cap[0].type == SCP_TYPE_ACK) &&
               (rd16(&cap[0].data[2]) == 0U),
               "consume -> left=0 (zil susar)");

    body[0] = 2U; body[1] = 0U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x46U, seq++, body, 2U) == 1U,
               "LOG_CONSUME geriye");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x02U),
               "geriye gitme -> ERROR 0x02");

    body[0] = 5U; body[1] = 0U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x46U, seq++, body, 2U) == 1U,
               "LOG_CONSUME head asimi");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x02U),
               "head'i asma -> ERROR 0x02");

    /* --- 11) Stub, bilinmeyen komut, EPOCH penceresi --- */
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x03U, seq++, NULL, 0U) == 1U,
               "0x03 stub");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x04U),
               "0x03 -> ERROR 0x04");

    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x7FU, seq++, NULL, 0U) == 1U,
               "bilinmeyen GET");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x01U),
               "bilinmeyen -> ERROR 0x01");

    body[0] = 1U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x2AU, seq++, body, 1U) == 1U,
               "EPOCH ilk cagri");
    TEST_CHECK(cap[0].type == SCP_TYPE_ACK, "EPOCH ACK");
    TEST_CHECK(send_req(&hub, SCP_TYPE_SET, 0x2AU, seq++, body, 1U) == 1U,
               "EPOCH pencere ici");
    TEST_CHECK((cap[0].type == SCP_TYPE_ERROR) && (cap[0].data[0] == 0x03U),
               "pencere ici -> ERROR BUSY");

    /* --- 12) Yanit yok enjeksiyonu --- */
    hub.drop_next = 1U;
    TEST_CHECK(send_req(&hub, SCP_TYPE_GET, 0x01U, seq++, NULL, 0U) == 0U,
               "drop enjeksiyonu: yanit YOK");

    printf("\r\n=== rf-hub-sim testleri: %u pass / %u fail ===\r\n",
           test_pass, test_fail);
    return (test_fail == 0U) ? 0 : 1;
}

/*** end of file ***/
