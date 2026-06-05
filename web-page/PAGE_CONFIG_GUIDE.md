# Sayfa Yapılandırma Rehberi

Bu döküman, `index_v4.html` dosyasında yeni sekme/sayfa ekleme veya mevcut sayfaları düzenleme için gerekli bilgileri içerir.

## Hızlı Başlangıç

Yeni bir sayfa eklemek için `M.pages` array'ine yeni bir obje ekleyin:

```javascript
{
  id: 'benzersiz_id',        // URL ve cache için benzersiz tanımlayıcı
  title: 'Sayfa Başlığı',    // Menüde ve başlıkta görünecek
  ep: '/api/endpoint',       // API endpoint (null ise API çağrısı yapılmaz)
  rw: true,                  // true: Config (Read/Save), false: Monitor (Refresh)
  refresh: true,             // rw:false ise Refresh butonu göster (opsiyonel)
  sections: [...]            // Sayfa içeriği
}
```

---

## Sayfa Tipleri

### 1. Config Sayfası (rw: true)
Kullanıcının veri girişi yapabildiği sayfalar.

```javascript
{
  id: 'myconfig',
  title: 'My Config',
  ep: '/config/myconfig',
  rw: true,
  sections: [...]
}
```
**Otomatik Butonlar:** `Read` ve `Save`

### 2. Monitor/Status Sayfası (rw: false)
Sadece okuma yapılan sayfalar.

```javascript
{
  id: 'mystatus',
  title: 'My Status',
  ep: '/status/mystatus',
  rw: false,
  refresh: true,  // Refresh butonu için
  sections: [...]
}
```
**Otomatik Butonlar:** `Refresh` (refresh: true ise) veya `Refresh All` (monitor rows varsa)

### 3. Action Sayfası (ep: null)
API endpoint olmadan sadece aksiyon butonları.

```javascript
{
  id: 'actions',
  title: 'Actions',
  ep: null,
  rw: false,
  sections: [{ layout: 'action', actions: [...] }]
}
```

---

## Layout Tipleri

### 1. `grid` - Form Alanları
Config sayfalarında kullanıcı girişi için.

```javascript
{
  lbl: 'General Settings',
  layout: 'grid',
  cols: 3,  // 2, 3, 4 veya 6 sütun
  fields: [
    {k: 'Port', t: 'n', lbl: 'Port', min: 1, max: 65535, req: 1},
    {k: 'Name', t: 't', lbl: 'Name'},
    {k: 'Enable', t: 'c', lbl: 'Enable'},
    {k: 'Status', t: 'n', lbl: 'Status', ro: 1}  // readonly
  ]
}
```

### 2. `table` - Editable Tablo
Config sayfalarında çok satırlı veri girişi için.

```javascript
{
  lbl: 'Lines Configuration',
  layout: 'table',
  tbl: 'Lines',   // JSON'daki key adı ('root' ise üst seviyede)
  rows: 8,        // Satır sayısı
  fields: [
    {k: 'inUse', t: 'c', lbl: 'Use'},
    {k: 'addr', t: 'n', lbl: 'Address', min: 1, max: 65535, req: 1}
  ]
}
```

**JSON Yapısı:**
```json
{
  "Lines": {
    "inUse": [true, false, true, ...],
    "addr": [100, 200, 300, ...]
  }
}
```

### 3. `monitor` - Readonly Tablo

#### A) Basit Key-Value Tablosu (rows yok)
Board Status gibi tek veri seti için.

```javascript
{
  lbl: 'System Status',
  layout: 'monitor',
  fields: [
    {k: 'temp', lbl: 'Temperature', u: '°C'},
    {k: 'voltage', lbl: 'Voltage', u: 'V', dec: 2},
    {k: 'leds', t: 'led', lbl: 'Status LEDs', cnt: 4}
  ]
}
```

**Görünüm:**
```
┌─────────────┬──────────┐
│ Parameter   │ Value    │
├─────────────┼──────────┤
│ Temperature │ 45°C     │
│ Voltage     │ 12.50V   │
│ Status LEDs │ [1][2]...│
└─────────────┴──────────┘
```

#### B) R/S/T Faz Tablosu (rows var)
RF Monitor gibi çoklu satır + faz verileri için.

```javascript
{
  lbl: 'RF Lines Status',
  layout: 'monitor',
  rows: 8,
  fields: [
    {k: 'DEVICEID', lbl: 'Device ID'},
    {k: 'FazAkimi', lbl: 'Phase Current', u: 'A', dec: 1}
  ]
}
```

**Görünüm:**
```
Line 1 (Hat ID: 5, Zone: 2)              [Refresh]
┌─────────────┬────────┬────────┬────────┐
│ Parameter   │ Faz R  │ Faz S  │ Faz T  │
├─────────────┼────────┼────────┼────────┤
│ Device ID   │ 100    │ 101    │ 102    │
│ Phase Current│ 5.2A  │ 5.1A   │ 5.3A   │
└─────────────┴────────┴────────┴────────┘
```

**JSON Yapısı:**
```json
[
  {"HatID": 5, "ZoneID": 2, "DEVICEID": [100,101,102], "FazAkimi": [5.2,5.1,5.3]},
  {"HatID": 6, "ZoneID": 2, "DEVICEID": [103,104,105], "FazAkimi": [4.8,4.9,5.0]}
]
```

### 4. `action` - Aksiyon Butonları

```javascript
{
  layout: 'action',
  actions: [
    {
      id: 'reboot',
      lbl: 'Reboot Device',
      ep: '/device/reboot',
      confirm: 1,
      msg: 'Are you sure you want to reboot?'
    }
  ]
}
```

---

## Field Tipleri

| Tip | Açıklama | Örnek |
|-----|----------|-------|
| `n` | Number input | `{k: 'port', t: 'n', lbl: 'Port', min: 1, max: 65535}` |
| `t` | Text input | `{k: 'name', t: 't', lbl: 'Name'}` |
| `c` | Checkbox | `{k: 'enable', t: 'c', lbl: 'Enable'}` |
| `led` | LED gösterge (monitor) | `{k: 'status', t: 'led', lbl: 'LEDs', cnt: 4}` |

---

## Field Özellikleri

| Özellik | Açıklama | Örnek |
|---------|----------|-------|
| `k` | JSON key adı | `'Port'` |
| `t` | Field tipi | `'n'`, `'t'`, `'c'`, `'led'` |
| `lbl` | Görünen etiket | `'Port Number'` |
| `min` | Minimum değer (number) | `1` |
| `max` | Maximum değer (number) | `65535` |
| `req` | Zorunlu alan (1/0) | `1` |
| `ro` | Readonly (1/0) | `1` |
| `u` | Birim (monitor) | `'°C'`, `'V'`, `'A'` |
| `dec` | Ondalık basamak (monitor) | `2` |
| `cnt` | LED sayısı (led tipi) | `4` |

---

## Örnek: Yeni Network Config Sayfası

```javascript
{
  id: 'network',
  title: 'Network Config',
  ep: '/config/network',
  rw: true,
  sections: [
    {
      lbl: 'IP Settings',
      layout: 'grid',
      cols: 3,
      fields: [
        {k: 'ip', t: 't', lbl: 'IP Address', req: 1},
        {k: 'mask', t: 't', lbl: 'Subnet Mask', req: 1},
        {k: 'gateway', t: 't', lbl: 'Gateway', req: 1},
        {k: 'dns1', t: 't', lbl: 'DNS 1'},
        {k: 'dns2', t: 't', lbl: 'DNS 2'},
        {k: 'dhcp', t: 'c', lbl: 'DHCP Enable'}
      ]
    },
    {
      lbl: 'Port Configuration',
      layout: 'table',
      tbl: 'ports',
      rows: 4,
      fields: [
        {k: 'enable', t: 'c', lbl: 'Enable'},
        {k: 'port', t: 'n', lbl: 'Port', min: 1, max: 65535, req: 1},
        {k: 'protocol', t: 't', lbl: 'Protocol'}
      ]
    }
  ]
}
```

---

## Örnek: Yeni Sensor Monitor Sayfası

```javascript
{
  id: 'sensors',
  title: 'Sensor Monitor',
  ep: '/monitor/sensors',
  rw: false,
  refresh: true,
  sections: [
    {
      lbl: 'Sensor Values',
      layout: 'monitor',
      fields: [
        {k: 'temp1', lbl: 'Temperature 1', u: '°C', dec: 1},
        {k: 'temp2', lbl: 'Temperature 2', u: '°C', dec: 1},
        {k: 'humidity', lbl: 'Humidity', u: '%'},
        {k: 'pressure', lbl: 'Pressure', u: 'hPa'},
        {k: 'alarms', t: 'led', lbl: 'Alarms', cnt: 8}
      ]
    }
  ]
}
```

---

## API Endpoint Beklentileri

### GET (Read/Refresh)
```
GET /config/network
Response: { "ip": "192.168.1.1", "mask": "255.255.255.0", ... }
// veya
Response: { "success": true, "data": { ... } }
```

### POST (Save)
```
POST /config/network
Body: { "ip": "192.168.1.1", "mask": "255.255.255.0", ... }
Response: { "success": true }
```

### Monitor Satır Refresh
```
GET /monitor/rf/0  → Line 0 verisi
Response: { "HatID": 5, "DEVICEID": [100,101,102], ... }
```

---

## Validasyon

- `req: 1` olan alanlar boş bırakılamaz
- `min`/`max` değerleri number inputlar için otomatik kontrol edilir
- `inUse` checkbox'ı false olan satırlar validate edilmez

---

## Notlar

1. **Sayfa sırası:** `M.pages` array'indeki sıra menüdeki sıradır
2. **Cache:** Her sayfa verisi `cache[id]` içinde saklanır
3. **tbl: 'root':** Tablo verileri JSON'un en üst seviyesinde olur
4. **LED değerleri:** Array olarak gelir: `[1, 0, 1, 0]` (on/off)
