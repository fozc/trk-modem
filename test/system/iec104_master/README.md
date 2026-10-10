# IEC-104 Master Kabul Süiti (c104) — test/system/iec104_master

Cihazın IEC-104 dinleyicisini (GSM üzerinden, varsayılan port 2404)
bağımsız bir master stack'i ile uçtan uca doğrular. Master tarafı
[c104](https://pypi.org/project/c104/) 2.2.1 (lib60870-C v2 çekirdeği,
GPLv3) — kütüphane **repoya girmez**, bench-lokal venv'e kurulur;
kurulu değilse süit SKIP döner. Negatif/fault kapsaması bu süitte
değil, Ceedling'dedir (`test/iec104/test_iec104_protocol_scenario.c`).

## Kurulum (bir kez)

```bash
# Python 3.13 (wheel'ler 3.13'te bitiyor; sistem 3.14 olabilir)
py -3.13 -m venv test/system/iec104_master/.venv
test/system/iec104_master/.venv/Scripts/python.exe -m pip install \
    --only-binary=:all: c104==2.2.1 "pyserial>=3.5,<4"
```

`pyserial` yalnız `--console` kullanılıyorsa gerekir (konsol yardımcısı
`tools/hwtest/serial_io.py` üzerinden import eder).

`.venv` gitignored'dır; silinmesi süiti bozmaz (SKIP döner).

**Ortam notları:**
- venv bağımsız bir kurulum DEĞİLDİR; `pyvenv.cfg` üzerinden base
  Python 3.13'e bağlıdır — 3.13 kaldırılırsa venv çalışmaz
  ([venv belgesi](https://docs.python.org/3/library/venv.html)).
- 3.13 per-user kurulumu kullanıcı PATH'ine üç giriş ekler; bunlar
  süit için gereksizdir (venv mutlak yol, `py -3.13` kayıt defteri
  etiketiyle çözülür) ve istenirse kullanıcı PATH'inden silinebilir:
  `...\Python313\Scripts\`, `...\Python313\`, `...\Python\Launcher\`.
- Donanımsız doğrulama: `python test_apdu_selftest.py` (13 golden;
  negatif onay P/N biti dahil) — integration sarmalayıcısı bunu her
  koşuda önce çalıştırır.

## Kullanım

```bash
# vaka listesi (venv gerekmez)
python test/system/iec104_master/run_suite.py --list

# tam süit (hedef açıkça verilir; varsayılan IP YOK)
python test/system/iec104_master/run_suite.py --host 188.59.76.59

# konsollu + kabul modu + opt-in mutasyonlar
python test/system/iec104_master/run_suite.py --host 188.59.76.59 \
    --console COM16 --require-device --setup-feeders \
    --clock-sync --mutate-eventlog

# merkez koşudan (wrapper): hedef yoksa SKIP
IEC104_DEVICE_HOST=188.59.76.59 ruby test/run_all.rb integration
```

Cihaz IP'si konsoldan `gsm status` ile bulunur. Süit tek istemcili
dinleyiciye sahip olduğu için çalışırken başka 104 istemcisi
bağlanmamalıdır.

## Vaka kataloğu

| Vaka | Doğrulanan | Koşul |
|---|---|---|
| i01_startdt_mei | STARTDT_ACT→CON→M_EI_NA_1(70) COT=4 CA IOA=0 | cihaz |
| i02_gi_station | QOI=20: ACT_CON→tam IOA kümesi→ACT_TERM; kalite beklentisi bench RF durumuna göre | aktif hat |
| i03_gi_groups | QOI=21/22 kesin küme; QOI=23/24 ACT_CON/TERM + arıza IOA bölgesi | aktif hat |
| i04_clock_sync | C_CS_NA_1 ACT_CON + konsol zaman damgasıyla apply | `--clock-sync` + `--console` |
| i05_testfr_idle | cihaz t3 TESTFR_ACT → master CON → GI canlılığı (master t3'ü cihazdan büyük seçilir) | cihaz |
| i06_replay_reconnect | sentetik kayıt replay: içerik + en-yeni-önce sıra + unsent tükenmesi | `--mutate-eventlog` + `--console` |
| i07_replay_retention | replay ortasında link kesintisi: kayıt kaybı yok (de5604c), pencere 2 en-yeni-önce | `--mutate-eventlog` + `--console` |

## Sonuç ve çıkış kodları

- Özet: `PASS=x FAIL=y SKIP=z DEFERRED=w` + verdict satırı
  (`ACCEPTANCE PASS` / `PARTIAL (SKIP/DEFERRED var)` / `FAIL`).
- FAIL → exit 1. `--require-device` ile SKIP/DEFERRED de exit 1.
- Hedef verilmediyse SKIP + exit 0 (merkezi koşu bozulmaz).

## Kanıt

`test/build/iec104-master/<zaman>/`: `apdu.jsonl` (her APDU: yön, ham
hex, monotonic zaman, ASDU çözümü; U/S çerçeveler dahil),
`manifest.json` (git commit, c104/py sürümü, cihaz oracle anlık
görüntüsü: port/CA/T0-T3/K/W/satır IOA haritası, vaka sonuçları),
`console.log` (konsol verildiyse).

## Oracle ve sınırlar

- Beklenen IOA kümesi cihazın **canlı yapılandırmasından** üretilir
  (web `/config/iec104` Hatlar bölümü + `/config/rf`); doc tabloları
  tek başına oracle değildir (bir kısmı eski).
- Kalite semantiği `iec104_process.c` read_anlik_akim'a göre: hub
  offline iken akım değerleri IV zorunludur (NT seçimlik); online
  iken kalite dağılımı kanıt olarak kaydedilir.
- `--setup-feeders` RF store'u fider 1,2/bölge 1 ile doldurur;
  `--mutate-eventlog` `iec104evtlog test N` ile Flash'a kayıt yazar;
  `--clock-sync` cihaz RTC'sini host saatine çeker. Üçü de bilinçli
  bench mutasyonudur, varsayılan kapalıdır.
- 2404'e inbound erişim yoksa otomatik port değişikliği yapılmaz:
  teşhisle başarısız kabul edilir (operatör filtresi raporlanır).
- Oracle bir yazılım stack'idir (lib60870); gerçek SCADA donanımı ve
  standart uygunluk (conformance) testi kapsam dışıdır.
