# IEC104 Event Queue - Host-Based Test Suite

Host makinede çalışacak test ortamı ve yapı doğrulama araçları.

**Created:** February 8, 2026  
**Updated:** February 8, 2026

## 📁 Proje Yapısı

```
test/
├── README.md                          # Bu dosya
├── Makefile                           # Linux/macOS build
├── build_and_run.bat                  # Windows simple test script
├── build_and_run_comprehensive.bat    # Windows comprehensive test script
├── iec104_test_simple.c               # Yapı doğrulama testi
├── iec104_test_comprehensive.c        # Kapsamlı fonksiyonel testler
├── mock_flash.c/h                     # Flash memory mock
├── mock_bsp.c / bsp.h                 # BSP mock (console, xprintf)
└── .gitignore                         # Build artifact'ları ignore et
```

## ✅ Test Türleri

### 1. Structure Validation Test (Simple)
Yap ı boyutlarını ve memory kullanımını doğrular:
- Event, sector, queue yapılarının boyutları
- Flash sector alignment kontrolü  
- RAM/Flash memory hesaplamaları  

### 2. Comprehensive Functional Tests
Edge case'ler ve fonksiyonel davranışları test eder:
- **Initialization Tests:** NULL pointer, invalid config handling
- **Basic Operations:** Empty/full queue, add/read events
- **Boundary Conditions:** Capacity limits, overflow behavior
- **Circular Buffer:** Wraparound, multiple cycles
- **Flash Persistence:** Save/load operations
- **CRC Validation:** Data integrity checks
- **Sector Boundaries:** Cross-sector event storage

## 🔧 Derleme ve Çalıştırma

### Windows (MinGW/MSYS2)

**Simple Test:**
```cmd
cd test
build_and_run.bat
```

**Comprehensive Test:**
```cmd
cd test
build_and_run_comprehensive.bat
```

### Linux/macOS  

**Simple Test:**
```bash
cd test/
make simple      # Build
make run-simple  # Build and run
```

**Comprehensive Test:**
```bash
cd test/
make comprehensive  # Build
make run-comp      # Build and run
```

**All Tests:**
```bash
make all         # Build all tests
make clean       # Clean
```

## 📊 Test Çıktısı Örnekleri

### Simple Test Output
```
============================================================
  IEC104 Event Queue - Structure Validation
============================================================

Configuration:
  Event capacity: 100
  Flash sector size: 4096 bytes
  Queue sector size: 4092 bytes
  ...

[PASS] Sector size validation PASSED
```

### Comprehensive Test Output
```
========================================================
  IEC104 Event Queue - Comprehensive Test Suite
========================================================

======== INITIALIZATION TESTS ========

[TEST 01] Initialization with NULL config
  [PASS] Should reject NULL config

...

========================================================
  TEST SUMMARY
========================================================
  Total tests: 18
  Passed: 18
  Failed: 0
========================================================

  ALL TESTS PASSED!
```

## 🐛 Bilinen Düzeltmeler

### v1.1 (Feb 8, 2026)
- ✅ UTF-8 box-drawing karakterler ASCII ile değiştirildi (Windows uyumluluğu)
- ✅ Comprehensive edge case testleri eklendi
- ✅ NULL pointer validasyonları test edildi
- ✅ Circular buffer wraparound testleri eklendi
- ✅ Flash persistence testleri eklendi
- ✅ Capacity ve overflow testleri eklendi

## 📝 Test Kapsam Detayları

| Test Kategorisi | Test Sayısı | Edge Cases |
|----------------|-------------|------------|
| Initialization | 2 | NULL config, invalid I/O |
| Basic Ops | 3 | Empty, single event, read |
| NULL Handling | 2 | NULL event, NULL buffer |
| Capacity | 2 | Fill to max, overflow |
| Circular Buffer | 2 | Wraparound, multiple cycles |
| Sector Boundary | 1 | Cross-sector storage |
| Persistence | 1 | Flash save/load |
| Unsent Tracking | 1 | Unsent count |
| **TOPLAM** | **14+** | **Kapsamlı** |

## 📋 Public API Fonksiyonları

Test edilebilir fonksiyonlar:
```c
bool iec104_event_queue_init(const iec104_event_queue_cfg_t *cfg);
iec104_queue_result_t iec104_event_queue_edd(const iec104_fault_event_t *event);
iec104_queue_result_t iec104_event_queue_read(iec104_fault_event_t *event);
bool iec104_event_queue_is_empty(void);
bool iec104_event_queue_is_full(void);
uint8_t iec104_event_queue_get_count(void);
uint8_t iec104_event_queue_get_unsent_count(void);
iec104_queue_result_t iec104_event_queue_save_to_flash(void);
iec104_queue_result_t iec104_event_queue_load_from_flash(void);
```

## 📝 Mock Komponenetler

### Flash Memory (`mock_flash.c`)
- 16MB simüle edilmiş flash
- read/write/erase operasyonları
- Bounds checking

### BSP (`mock_bsp.c`)
- Console logger emülasyonu
- xprintf redirection
- CSLOG macro desteği

## ⚙️ Gereksinimler

- **Derleyici**: GCC 7+ / MinGW / MSVC 2019+
- **C Standard**: C11
- **OS**: Windows, Linux, macOS

## 🎯 Gelecek Geliştirmeler

- [ ] CRC corruption scenario testleri
- [ ] Flash write failure handling testleri
- [ ] Concurrent access testleri (multi-thread)
- [ ] Performance benchmark testleri
- [ ] Memory leak detection (valgrind)
- [ ] Code coverage analysis (gcov/lcov)

## 📚 Referanslar

- IEC104 Event Queue API: `../iec104_event_queue.h`
- Mock Flash Implementation: `mock_flash.c`
- CRC Implementation: `../../libs/crc.c`
