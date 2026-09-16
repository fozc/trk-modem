/*
 * Mock server for TSB23 Device Configuration (index.html)
 * No dependencies — uses Node built-ins only.
 *
 * Run:   node mock-server.js
 * Open:  http://127.0.0.1:8123
 *
 * Login accepts ANY username / password.
 * All config saves are accepted and logged to the console,
 * with array lengths shown so you can verify the RF save fix
 * (arrays should be [8], not [0]).
 */
const http = require('http');
const fs = require('fs');
const path = require('path');

const HOST = '127.0.0.1';
const PORT = 8123;
// Serve the CURRENT page from web-page/; fall back to a local copy if present.
const HTML_FILE = fs.existsSync(path.join(__dirname, '..', 'web-page', 'index.html'))
  ? path.join(__dirname, '..', 'web-page', 'index.html')
  : path.join(__dirname, 'index.html');

const FEEDER_COUNT = 7;
const arr8 = fn => Array.from({ length: FEEDER_COUNT }, (_, i) => fn(i));

// ---------- send helpers ----------
const send = (res, obj, code = 200) => {
  res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8' });
  res.end(JSON.stringify(obj));
};
const serveHtml = res => {
  fs.readFile(HTML_FILE, (err, data) => {
    if (err) { res.writeHead(500); res.end('index.html not found next to mock-server.js'); return; }
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(data);
  });
};
const readBody = req => new Promise(resolve => { let b = ''; req.on('data', c => b += c); req.on('end', () => resolve(b)); });
// Compact summary of a save payload — arrays shown as [length]
const summarize = b => {
  try {
    const o = JSON.parse(b);
    return JSON.stringify(Object.fromEntries(
      Object.entries(o).map(([k, v]) => [k, Array.isArray(v) ? `[${v.length}]` : (v && typeof v === 'object' ? '{...}' : v)])
    ));
  } catch { return b.slice(0, 120); }
};

// ---------- mock data ----------
const now = () => Math.floor(Date.now() / 1000);

const iecKeys = ['IOA_R_ArizaAkimi','IOA_S_ArizaAkimi','IOA_T_ArizaAkimi','IOA_R_ArizaSuresi','IOA_S_ArizaSuresi','IOA_T_ArizaSuresi','IOA_R_ArizaTuru','IOA_S_ArizaTuru','IOA_T_ArizaTuru','IOA_R_AnlikAkim','IOA_S_AnlikAkim','IOA_T_AnlikAkim','IOA_R_EnerjiVarYok','IOA_S_EnerjiVarYok','IOA_T_EnerjiVarYok','IOA_R_NominalAkimVarYok','IOA_S_NominalAkimVarYok','IOA_T_NominalAkimVarYok','IOA_R_RfhabVarYok','IOA_S_RfhabVarYok','IOA_T_RfhabVarYok'];
const iecHat = { inUse: arr8(i => i < 4 ? 1 : 0) };
iecKeys.forEach((k, j) => { iecHat[k] = arr8(i => 100 + j * 10 + i); });

const iec104 = { success: true, data: {
  Port: 2404, PeriodicSend: 10, T0: 30, T1: 30, T2: 30, T3: 30, K: 64, W: 32,
  OriginatorAddr: 1, CommonAddr: 1, SBOTimeout: 30000, SBO: true,
  AkuUyarisi: 100, ModemReset: 101, Hatlar: iecHat
}};

const modKeys = iecKeys.map(k => k.replace('IOA_', 'ADDR_'));
const modHat = { inUse: arr8(i => i < 4 ? 1 : 0) };
modKeys.forEach((k, j) => { modHat[k] = arr8(i => 100 + j * 10 + i); });

const modbus = { success: true, data: {
  CihazID: 1, BaudRate: 9600, SonHataKodu: 0, SonHataZamani: now(), Hat: modHat
}};

// EUI-64 as 16-char uppercase hex string (spec R2 §6.2 — wire/raw order, UI text field).
// NOTE: built string-wise — a 64-bit constant exceeds Number.MAX_SAFE_INTEGER.
const eui64 = (prefix15, i) => prefix15 + i.toString(16).toUpperCase();

const rf = { success: true, data: {
  inUse: arr8(i => i < 4 ? 1 : 0),
  /* Feeder 4 (index 3) intentionally unprovisioned: HatID=0 demo badge */
  HatID: arr8(i => i === 3 ? 0 : i + 1), ZoneID: arr8(() => 1),
  R_DEVICEID: arr8(i => eui64('A40567821C3B9F4', i)),
  S_DEVICEID: arr8(i => eui64('A40567821C3BA05', i)),
  T_DEVICEID: arr8(i => eui64('A40567821C3BA14', i)),
  CalismaModu: arr8(() => 1), SistemNominalAkimi: arr8(() => 100),
  SetEdilebilirActirmaEsikAkimi: arr8(() => 150), SetEdilebilirAcmaArizaSayisi: arr8(() => 3),
  ArtimliAkimEsigi: arr8(() => 1000), HatKopukHatBosta: arr8(() => 2),
  OluHatAkimiDogrulamaSuresi: arr8(() => 200), YenilenmeSifirlamaSuresi: arr8(() => 60),
  HatFrekansi: arr8(() => 50),
  /* 12 spec R2 params carried by the contract, not rendered on screen */
  IsSafety: arr8(() => 0.3), ThresholdMs: arr8(() => 60),
  TMemDeadSec: arr8(() => 180), InrushTimerMs: arr8(() => 60),
  InrushMultiplier: arr8(() => 5), SyncTripDelayMs: arr8(() => 100),
  TripPulseDurationMs: arr8(() => 40), TripMode: arr8(() => 0),
  Inrush100HzRatio: arr8(() => 39), ClpEnabled: arr8(() => 0),
  ClpMultiplier: arr8(() => 2), ClpDurationMs: arr8(() => 5000),
  VtripTarget: arr8(() => 32),
  /* 0x14 DISCOVERY_REPORT queue: discovered but unassigned devices */
  Unassigned: [
    { EUI64: 'A40567821C3BEE01', FiderID: 0 },
    { EUI64: 'A40567821C3BEE02', FiderID: 0 }
  ]
}};

const device = { success: true, data: {
  SeriNumarasi: 'TSB23-0001', UretimTarihi: now() - 31536000, LifeTime: '10000h',
  ModemYazilimVeriyonu: '1.2.3', RFYazilimVeriyonu: '2.0.1',
  CihazKoordinati: { MCC: 286, MNC: 2, LAC: 1234, CI: 5678 },
  DevreyeAlinmaZamani: now() - 1000000, Time: now(),
  WebArayuzuPortu: 80, SimKartPin: 1234, SimKartAPN: 'internet',
  SimKartAPNUsername: '', SimKartAPNSifresi: '',
  NtpServer: 'pool.ntp.org', NtpServerPortu: 123, TimeZone: 3, PeriyodikModemResetPeriyodu: 0
}};

const board = { success: true, data: {
  GsmSig: -71, GsmRAT: 'LTE',
  PanelAkimi: 1.2, PanelVoltaji: 18.5,
  BataryaVoltaji: 12.4, BataryaAkimi: 0.5, BatteryTemp: 25, ChargePertance: 80,
  ChargeState: 'Charging', BatterySOC: 82, BatterySOH: 95, Capacity: 10000,
  V19: 1.9, '3V3': 3.3, '3V8': 3.8, '5V': 5.0, OrtamSicakligi: 24,
  HeaterState: 'Off', HeaterPower: 0,
  Temp: 24, TempMax: 35, TempMin: 10, TDIE: 30, TDIEMax: 40, TDIEMin: 20,
  DIN: [1, 0, 1, 0], RLY: [0, 1, 0, 1]
}};

const faults = {
  success: true,
  tc: [2, 1, 0], pc: [1, 0, 0],
  L1T: '2026-08-01 10:00 Overcurrent\n2026-08-02 11:30 Short circuit',
  L2T: '2026-08-03 09:15 Earth fault', L3T: '',
  L1P: '2026-07-15 08:00 Permanent trip', L2P: '', L3P: ''
};

const rfMonitor = i => ({ success: true, data: { lines: [{
  DEVICEID: [10 + i, 20 + i, 30 + i], CalismaModu: [1, 1, 1], HatFrekansi: [50, 50, 50],
  SistemSicakligi: [25, 25, 25], SistemDCGerilimi: [12, 12, 12],
  '5Vdc': [5.0, 5.0, 5.0], '3V3dc': [3.3, 3.3, 3.3], ActirmaDCGerilimi: [3, 3, 3],
  FazAkimi: [1.5, 1.5, 1.5], FazHataAkimi: [0, 0, 0],
  AktifArizaSayaci: [0, 0, 0], GecmisAcmaSayisi: [1, 1, 1],
  RSSI: [-70, -71, -72], LQI: [100, 99, 98]
}]}});

// Deterministic system-log fixture: 100 records in the device's dump format,
// newest first. /syslogs slices it by offset/limit like the firmware and
// returns an empty recs for past-the-end offsets (no wrap-around).
const SYSLOGS_ALL = Array.from({ length: 100 }, (_, i) => {
  const n = i + 1;
  const mm = String(Math.floor(n / 60) % 60).padStart(2, '0');
  const ss = String(n % 60).padStart(2, '0');
  return `#${n} TS:2026-08-10 10:${mm}:${ss} LVL:${n % 4} CODE:MOCK_EVENT INFO:mock record ${n}`;
});

// ---------- router ----------
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${HOST}`);
  const p = url.pathname, m = req.method;
  console.log(`${m} ${p}${url.search ? ' ' + url.search : ''}`);

  if ((p === '/' || p === '/index.html') && m === 'GET') return serveHtml(res);

  if (p === '/auth/login'  && m === 'POST') { await readBody(req); return send(res, { success: true, token: 'mock-' + now() }); }
  if (p === '/auth/logout' && m === 'POST') return send(res, { success: true });

  if (p === '/config/iec104'  && m === 'GET') return send(res, iec104);
  if (p === '/config/modbus'  && m === 'GET') return send(res, modbus);
  if (p === '/config/rf'      && m === 'GET') return send(res, rf);
  if (p === '/config/device'  && m === 'GET') return send(res, device);
  if (p === '/status/board'   && m === 'GET') return send(res, board);

  if (p === '/config/iec104' && m === 'POST') { console.log('  SAVE iec104  ', summarize(await readBody(req))); return send(res, { success: true }); }
  if (p === '/config/modbus' && m === 'POST') { console.log('  SAVE modbus  ', summarize(await readBody(req))); return send(res, { success: true }); }
  if (p === '/config/rf'     && m === 'POST') { console.log('  SAVE rf      ', summarize(await readBody(req))); return send(res, { success: true }); }
  if (p === '/config/device' && m === 'POST') { console.log('  SAVE device  ', summarize(await readBody(req))); return send(res, { success: true }); }

  const mon = p.match(/^\/monitor\/rf\/(\d+)$/);
  if (mon && m === 'GET') return send(res, rfMonitor(+mon[1]));

  if (p === '/faults'  && m === 'GET') return send(res, faults);
  if (p === '/syslogs' && m === 'GET') {
    const offset = Math.max(0, parseInt(url.searchParams.get('offset') || '0', 10) || 0);
    const limit = Math.min(100, Math.max(1, parseInt(url.searchParams.get('limit') || '30', 10) || 30));
    const recs = offset >= SYSLOGS_ALL.length ? '' : SYSLOGS_ALL.slice(offset, offset + limit).join('\n');
    return send(res, { success: true, recs, t: SYSLOGS_ALL.length });
  }

  if (p === '/serial' && m === 'POST') { const b = await readBody(req); let tx = ''; try { tx = JSON.parse(b).tx || ''; } catch {} console.log('  TERM tx:', tx); return send(res, { rx: 'mock> received: ' + tx }); }
  if (p === '/serial' && m === 'GET')  return send(res, { rx: '' });

  if (p === '/device/reboot' && m === 'POST') return send(res, { success: true, message: 'Reboot command accepted (mock)' });

  console.log('  -> 404');
  send(res, { success: false, error: 'Not found: ' + p }, 404);
});

server.listen(PORT, HOST, () => {
  console.log('TSB23 mock server running:');
  console.log('  ->  http://' + HOST + ':' + PORT);
  console.log('Login with any username / password. Ctrl+C to stop.\n');
});
