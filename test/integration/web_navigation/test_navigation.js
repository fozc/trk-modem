/* Run with node test/integration/web_navigation/test_navigation.js. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const zlib = require('node:zlib');

async function checkPage(html, label) {
    const elements = new Map();
    const menu = [];
    const notices = [];
    function element() {
        const classes = new Set();
        return {
            innerHTML: '', textContent: '', className: '', value: '',
            dataset: {}, style: {},
            classList: {
                add: name => classes.add(name),
                remove: name => classes.delete(name),
                contains: name => classes.has(name),
                toggle(name, active) {
                    if (active) classes.add(name);
                    else classes.delete(name);
                }
            },
            querySelectorAll: () => [],
            querySelector: () => null,
            addEventListener() {}, focus() {},
            remove() { this.removed = true; },
            appendChild(child) {
                notices.push(child);
                if (child.id) elements.set(child.id, child);
            }
        };
    }
    const document = {
        getElementById(id) {
            if (!elements.has(id)) elements.set(id, element());
            return elements.get(id);
        },
        querySelectorAll: selector => selector === '.mi' ? menu : [],
        querySelector: () => null,
        createElement: element,
        addEventListener() {},
        body: element(), documentElement: {}
    };
    const scheduledTimers = [];
    const context = vm.createContext({
        document, console,
        localStorage: {getItem: () => 'tr', setItem() {}},
        sessionStorage: {getItem: () => null, setItem() {}, removeItem() {}},
        setTimeout: (callback, delay) => {scheduledTimers.push({callback, delay});return 1;}, clearTimeout() {}, AbortController,
        fetch: async () => ({ok: true, status: 200, json: async () => ({})})
    });
    const script = html.match(/<script[^>]*>([\s\S]*?)<\/script>/i)[1];
    vm.runInContext(script.replace(/init\(\);\s*$/, ''), context);
    const run = code => vm.runInContext(code, context);
    const body = () => document.getElementById('body').innerHTML;
    for (const id of ['iec104', 'modbus', 'rf', 'rfmon', 'board',
                      'iec-faults', 'syslogs', 'device', 'terminal']) {
        const item = element();
        item.dataset.id = id;
        menu.push(item);
    }
    for (const language of ['tr', 'en']) {
        run(`LNG = '${language}';`);
        const rfMonitor = run(`buildRfMonitorTable({Phases:[
            {HasData:true,Eui64:'00124B0038C9F1CB',Online:true,
             Live:{RSSI:-128,Temp:300,Irms:1.25,VTrip:32,VRec:13,Flags:2},
             TripFailedAlarm:true,TripFailureLatched:true},
            {HasData:false},{HasData:false}]})`);
        assert.ok(rfMonitor.includes('00124B0038C9F1CB'));
        assert.ok(rfMonitor.includes('-128'));
        assert.ok(rfMonitor.includes('300'));
        assert.ok(rfMonitor.includes('1.250'));
        const energyLabel = language === 'tr' ? 'Enerji Var/Yok' : 'Energy Present';
        const loadLabel = language === 'tr' ? 'Yük Akımı Var/Yok' : 'Load Current Present';
        assert.ok(rfMonitor.includes(`<td>${energyLabel}</td><td>0</td>`));
        assert.ok(rfMonitor.includes(`<td>${loadLabel}</td><td>1</td>`));
        for (const phase of [
            '{HasData:true,Online:false,Live:{Flags:3}}',
            '{HasData:true,Online:true,UptimeStalled:true,Live:{Flags:3}}',
            '{HasData:true,Online:true,Live:null}',
            '{HasData:true,Online:true,Live:{Flags:null}}',
            '{HasData:true,Online:true,Live:{Flags:256}}',
            '{HasData:false,Online:true,Live:{Flags:3}}'
        ]) {
            const unavailable = run(`buildRfMonitorTable({Phases:[${phase}]})`);
            assert.ok(unavailable.includes(`<td>${energyLabel}</td><td>—</td>`));
            assert.ok(unavailable.includes(`<td>${loadLabel}</td><td>—</td>`));
        }
        assert.ok(!rfMonitor.includes('5V DC'));
        assert.ok(!rfMonitor.includes('LQI'));
        assert.ok(!rfMonitor.includes('undefined'));
        const countsTable=run(`buildRfMonitorTable({Phases:[],
            FaultCountsSinceStartup:{Permanent:1,Temporary:2,Uncertain:3}})`);
        assert.ok(countsTable.includes(language==='tr'
            ? 'kalıcı 1, geçici 2, belirsiz 3'
            : 'permanent 1, temporary 2, uncertain 3'));
        const maintenance=run(`buildRfMonitorTable({Phases:[
            {HasData:true,Online:true,Live:{Flags:3,FsmError:true}}]})`);
        assert.ok(maintenance.includes(language==='tr'
            ? 'Bakım gerekli' : 'Maintenance required'));
        assert.ok(maintenance.includes(`<td>${energyLabel}</td><td>1</td>`));
        for(const unavailable of ['Online:false','Online:true,UptimeStalled:true']){
            const stale=run(`buildRfMonitorTable({Phases:[
                {HasData:true,${unavailable},Live:{Irms:9.75,Temp:300}}]})`);
            assert.ok(!stale.includes('9.750'));
            assert.ok(!stale.includes('<td>300'));
        }
        assert.ok(rfMonitor.includes(language === 'tr'
              ? 'Açma kondansatörü' : 'Trip capacitor'));
        const applied = run(`buildRfGroupStatus({State:'applied',Line:1,
            GroupId:7,MatchesDesired:true,HasReport:false})`);
        assert.ok(applied.includes(language === 'tr'
            ? 'Uygulandı ve CRC doğrulandı' : 'Applied and CRC verified'));
        const changed = run(`buildRfGroupStatus({State:'applied',Line:1,
            GroupId:7,MatchesDesired:false,HasReport:false})`);
        assert.ok(changed.includes(language === 'tr'
            ? 'istenen ayar değişti' : 'desired settings have changed'));
          const missingPower = run(`renderBoard({BatterySOC:null,
              BatterySOH:null,BatteryTemp:null,ChargePertance:null,
              ChargeState:null,HeaterState:null,HeaterPower:null})`);
          assert.ok(!missingPower.includes('NaN'));
          assert.ok(!missingPower.includes('0.0 %'));
          assert.equal(run('formatChargePhase(2)'), language === 'tr'
              ? 'Sabit akım' : 'Constant current');
          const powerRows = run(`renderBoard({DcVoltaji:12500,
              GirisAkimi:420,GirisGucu:865,AkuGucu:-120,Kaynak:1,
              TelemetriYasi:3,AlarmMaskesi:0x00089001,
              KartDurum:0x27,KartDurum2:0xD8})`);
          assert.ok(powerRows.includes('12500'));
          assert.ok(powerRows.includes('420'));
          assert.ok(powerRows.includes('86.5'));
          assert.ok(powerRows.includes('-12.0'));
          assert.ok(powerRows.includes(language === 'tr'
              ? 'PV (güneş)' : 'PV (solar)'));
          for (const name of language === 'tr'
              ? ['Akü kritik düşük', 'Ölçüm bayat',
                 'Şarj denetleyicisi okunamıyor', 'Kart komutlara kapalı']
              : ['Battery critically low', 'Measurement stale',
                 'Charger unreadable', 'Board not accepting commands']) {
            assert.ok(powerRows.includes(name), name);
          }
          assert.ok(powerRows.includes(language === 'tr'
              ? '4 etkin — 0x00089001' : '4 active — 0x00089001'));
          assert.ok(powerRows.includes('>3 s<'));
          const powerNulls = run(`renderBoard({DcVoltaji:null,
              GirisAkimi:null,GirisGucu:null,AkuGucu:null,Kaynak:null,
              TelemetriYasi:null,AlarmMaskesi:null,KartDurum:null,
              KartDurum2:null})`);
          assert.ok(powerNulls.includes('>-<'));
          assert.ok(!powerNulls.includes('NaN'));
          assert.equal(run('formatAlarmMask(0)'),
              language === 'tr' ? 'Alarm yok' : 'No alarms');
          assert.ok(powerRows.includes(language === 'tr'
              ? 'Şarj denetleyicisi güncel değil' : 'Charger data stale'));
          assert.ok(powerRows.includes(language === 'tr'
              ? 'MH–kart bağlantısı: devre dışı' : 'MH link: disabled'));
          assert.ok(powerRows.includes(language === 'tr'
              ? 'Ayar kaynağı: servis' : 'Setting source: service'));
          assert.ok(powerRows.includes(language === 'tr'
              ? 'Sistem gücü ölçümü: hazırlanıyor'
              : 'System power measurement: initializing'));
          assert.ok(powerRows.includes(language === 'tr'
              ? 'Kart ölçümü güncel değil' : 'Board measurement stale'));
          assert.ok(powerRows.includes(language === 'tr'
              ? 'Ayar doğrulama: reddedildi' : 'Setting verification: rejected'));
          for (let csq = 0; csq <= 31; csq++) {
            const board = run(`renderBoard({GsmSig: ${csq}})`);
            assert.ok(board.includes(run("t('fSignal')")));
            assert.ok(board.includes(`class="v">${csq}</span>`));
            assert.ok(!board.includes('dBm'));
        }
        for (const csq of ['99', '32', '-1', '1.5', 'null',
                           'undefined', 'NaN', '"13"']) {
            const board = run(`renderBoard({GsmSig: ${csq}})`);
            assert.ok(board.includes('class="v">' +
                      run("t('signalUnknown')") + '</span>'));
            assert.ok(!board.includes('dBm'));
        }
        assert.equal(run("t('signalUnknown')"),
                     language === 'tr' ? 'Bilinmiyor' : 'Unknown');
        for (const [kind, raw, expected, rating] of [
            ['rxlev', 0, '< -110 dBm', 'signalVeryWeak'],
            ['rxlev', 1, '-110 … < -109 dBm', 'signalVeryWeak'],
            ['rxlev', 16, '-95 … < -94 dBm', 'signalWeak'],
            ['rxlev', 26, '-85 … < -84 dBm', 'signalMid'],
            ['rxlev', 36, '-75 … < -74 dBm', 'signalStrong'],
            ['rxlev', 51, '-60 … < -59 dBm', 'signalExcellent'],
            ['rxlev', 63, '≥ -48 dBm', 'signalExcellent'],
            ['rscp', 0, '< -120 dBm', 'signalVeryWeak'],
            ['rscp', 96, '≥ -25 dBm', 'signalExcellent'],
            ['rsrp', 0, '< -140 dBm', 'signalVeryWeak'],
            ['rsrp', 1, '-140 … < -139 dBm', 'signalVeryWeak'],
            ['rsrp', 31, '-110 … < -109 dBm', 'signalWeak'],
            ['rsrp', 41, '-100 … < -99 dBm', 'signalMid'],
            ['rsrp', 51, '-90 … < -89 dBm', 'signalStrong'],
            ['rsrp', 61, '-80 … < -79 dBm', 'signalExcellent'],
            ['rsrp', 97, '≥ -44 dBm', 'signalExcellent'],
            ['rsrq', 0, '< -19.5 dB', 'signalVeryWeak'],
            ['rsrq', 1, '-19.5 … < -19 dB', 'signalWeak'],
            ['rsrq', 4, '-18 … < -17.5 dB', 'signalMid'],
            ['rsrq', 10, '-15 … < -14.5 dB', 'signalStrong'],
            ['rsrq', 20, '-10 … < -9.5 dB', 'signalExcellent'],
            ['rsrq', 34, '≥ -3 dB', 'signalExcellent']
        ]) {
            assert.equal(run(`formatSignal(${raw}, '${kind}')`),
                         expected + ' — ' + run(`t('${rating}')`));
        }
        for (const [kind, max] of [['rxlev', 63], ['rscp', 96],
                                   ['rsrp', 97], ['rsrq', 34]]) {
            for (const raw of ['99', '255', '-1', 'null', 'undefined',
                               'NaN', '1.5', String(max + 1)]) {
                assert.equal(run(`formatSignal(${raw}, '${kind}')`),
                             run("t('signalUnknown')"));
            }
        }
        for (const rat of [2, 3, 4]) {
            const key = rat === 4 ? 'GsmCEREG' : 'GsmCGREG';
            for (const reg of [1, 5]) {
                const board = run(`renderBoard({GsmRAT:${rat},${key}:${reg},
                    GsmRxlev:51,GsmRsrp:41,GsmRsrq:20})`);
                assert.ok(board.includes(`${rat}G /`));
                assert.ok(board.includes(run("t('networkRegistered')")));
                assert.ok(board.includes('2G RSSI'));
                assert.ok(board.includes('4G RSRP'));
                assert.ok(board.includes('4G RSRQ'));
                assert.ok(board.includes('-60 … &lt; -59 dBm'));
                assert.ok(board.includes('-100 … &lt; -99 dBm'));
            }
        }
        for (const reg of [0, 2, 3]) {
            assert.ok(run(`formatNetwork({GsmRAT:4,GsmCEREG:${reg},
                           GsmCREG:1,GsmCGREG:5})`)
                      .endsWith(run("t('networkNotRegistered')")));
        }
        for (const reg of ['4', 'undefined', '99']) {
            assert.ok(run(`formatNetwork({GsmRAT:4,GsmCEREG:${reg}})`)
                      .endsWith(run("t('signalUnknown')")));
        }
        assert.equal(run('formatNetwork({GsmRAT:0,GsmCEREG:1})'),
                     run("t('signalUnknown')"));


        for (const page of ['iec104', 'modbus', 'rf', 'board', 'device']) {
            run(`pageCache['${page}'] = {}; switchPageNow('${page}');`);
            assert.ok(body().length > 0, page + ' should render loaded data');
            run(`delete pageCache['${page}'];`);
        }
        for (const data of [{}, {L1T: 'temporary'}, {L2P: 'permanent'},
                            {L1T: 'temporary', L1P: 'permanent'}]) {
            run(`rfFaultCache[0] = ${JSON.stringify(data)}; faultFeeder = 0;`);
            for (const item of menu) {
                run(`switchPageNow('${item.dataset.id}');`);
                run("switchPage('iec-faults');");
                assert.equal(run('curId'), 'iec-faults');
                assert.ok(body().includes('id="faultContent"'));
                assert.equal(document.getElementById('title').textContent,
                             run("t('mFaults')"));
            }
        }
        run('selectFaultFeeder(1);');
        assert.ok(body().includes(run("t('faultsPress')")));
        run('selectFaultFeeder(0);');
        assert.ok(body().includes('temporary'));
    }
    run("switchPageNow('board');");
    const previousBody = body();
    const previousTitle = document.getElementById('title').textContent;
    run("const savedBuildFaults = buildFaults; buildFaults = () => {throw new Error('render failure')};");
    assert.throws(() => run("switchPage('iec-faults');"), /render failure/);
    assert.equal(run('curId'), 'board');
    assert.equal(body(), previousBody);
    assert.equal(document.getElementById('title').textContent, previousTitle);
    assert.ok(menu.find(item => item.dataset.id === 'board').classList.contains('a'));
    run('buildFaults = savedBuildFaults;');
    run("switchPage('iec-faults');");
    await run('loadFaults()');
    {
        assert.equal(run('loading'), false);
        assert.ok(body().includes(run("t('noRecords')")));
        assert.ok(!notices.some(item => item.className === 'toast err'));
    }
    const fields = {
        WebArayuzuPortu: 8080, SimKartPin: 1234, NtpServerPortu: 123,
        TimeZone: 3, PeriyodikModemResetPeriyodu: 12,
        WebIlkVeriZamanAsimi: 30, WebBostaKalmaZamanAsimi: 60,
        IEC104IlkVeriZamanAsimi: 30, IEC104BostaKalmaZamanAsimi: 60
    };
    for (const [key, value] of Object.entries(fields)) {
        const input = document.getElementById('dev-' + key);
        input.type = 'number';
        input.value = String(value);
    }
    document.getElementById('dev-SimKartAPN').value = 'new-apn';
    run("pageCache.device = {SeriNumarasi: 'SERIAL-123', SimKartAPN: 'old-apn', PeriyodikModemResetPeriyodu: 3600};");
    let request;
    context.fetch = async (url, options) => {
        if(!options.body)return {ok:true,status:200,json:async()=>({SaveBlocked:false})};
        request = {url, data: JSON.parse(options.body)};
        return {ok: true, status: 200, json: async () => ({success: true})};
    };
    await run("savePage('device')");
    assert.equal(request.url, '/config/device');
    assert.equal(request.data.PeriyodikModemResetPeriyodu, 43200);
    assert.equal(run('pageCache.device.SimKartAPN'), 'new-apn');
    assert.equal(run('pageCache.device.SeriNumarasi'), 'SERIAL-123');
    run("switchPageNow('board'); switchPage('device');");
    assert.ok(body().includes('value="new-apn"'));
    assert.match(body(), /id="dev-PeriyodikModemResetPeriyodu"[^>]*value="12"/);
    const savedCache = run('JSON.stringify(pageCache.device)');
    document.getElementById('dev-SimKartAPN').value = 'rejected-apn';
    for (const response of [
        {ok: true, status: 200, json: async () => ({success: false, error: 'rejected'})},
        {ok: false, status: 500}
    ]) {
        context.fetch = async () => response;
        await run("savePage('device')");
        assert.equal(run('JSON.stringify(pageCache.device)'), savedCache);
        assert.equal(run('loading'), false);
    }
    const configFields = {
        'iec-Port': 2404, 'iec-PeriodicSend': 60, 'iec-T0': 30,
        'iec-T1': 30, 'iec-T2': 30, 'iec-T3': 30, 'iec-K': 64,
        'iec-W': 32, 'iec-OriginatorAddr': 1, 'iec-CommonAddr': 1,
        'iec-SBOTimeout': 30, 'iec-AkuUyarisi': 10000,
        'iec-ModemReset': 10001, 'mod-CihazID': 1, 'mod-BaudRate': 115200
    };
    for (const [id, value] of Object.entries(configFields)) {
        const input = document.getElementById(id);
        input.type = 'number';
        input.value = String(value);
    }
    assert.match(run("renderIec104({Hatlar:{}})"), /data-key="TemporaryFaultBase"/);
    assert.match(run("renderIec104({Hatlar:{}})"), /data-key="PermanentFaultBase"/);
    const sbo = document.getElementById('iec-SBO');
    sbo.type = 'checkbox';
    sbo.checked = true;
    document.getElementById('iec-OriginatorAddr').value = '0';
    document.getElementById('iec-CommonAddr').value = '300';
    assert.equal(run("validatePage('iec104')"), true);
    document.getElementById('iec-OriginatorAddr').value = '256';
    assert.equal(run("validatePage('iec104')"), false);
    document.getElementById('iec-OriginatorAddr').value = '255';
    document.getElementById('iec-CommonAddr').value = '65535';
    assert.equal(run("validatePage('iec104')"), true);
    document.getElementById('iec-CommonAddr').value = '65536';
    assert.equal(run("validatePage('iec104')"), false);
    document.getElementById('iec-CommonAddr').value = '300';
    document.getElementById('iec-SBOTimeout').value = '301';
    assert.equal(run("validatePage('iec104')"), false);
    sbo.checked = false;
    document.getElementById('iec-SBOTimeout').value = '65535';
    assert.equal(run("validatePage('iec104')"), true);
    document.getElementById('iec-SBOTimeout').value = '65536';
    assert.equal(run("validatePage('iec104')"), false);
    sbo.checked = true;
    document.getElementById('iec-SBOTimeout').value = '30';
    context.fetch = async (url, options) => {
        if(!options.body)return {ok:true,status:200,json:async()=>({SaveBlocked:false})};
        request = {url, data: JSON.parse(options.body)};
        return {ok: true, status: 200, json: async () => ({success: true})};
    };
    for (const page of ['iec104', 'modbus', 'rf']) {
        run(`pageCache['${page}'] = {};`);
        await run(`savePage('${page}')`);
        if(page==='rf')await document.getElementById('okConfirm').onclick();
        assert.equal(request.url, '/config/' + page);
        assert.deepEqual(JSON.parse(run(`JSON.stringify(pageCache['${page}'])`)), request.data);
    }
    const ioaError = 'IOA overlap: Hatlar.IOA_R_AnlikAkim[1] (feeder 2) and Hatlar.TemporaryFaultBase[0] (feeder 1)';
    const iecCache = run('JSON.stringify(pageCache.iec104)');
    context.fetch = async () => ({ok: false, status: 400, text: async () => ioaError});
    await run("savePage('iec104')");
    assert.ok(notices.some(item => item.className === 'toast err' && item.textContent.includes(ioaError)));
    assert.equal(run('JSON.stringify(pageCache.iec104)'), iecCache);
    assert.equal(run('loading'), false);
    // Real Save path: a collision must mark both fields and make no POST.
    const originalQuery = document.querySelector;
    const originalQueryAll = document.querySelectorAll;
    for (const page of ['iec104', 'modbus']) {
        const prefix = page === 'iec104' ? 'iec' : 'mod';
        const makeField = (key, index, value, label) => {
            const input = element(), error = element(), hint = element();
            input.type = 'number'; input.value = String(value);
            input.dataset = {id: prefix, key, i: String(index)};
            input.closest = () => ({querySelector: selector => selector === '.err' ? error : selector === '.register-info' ? hint : {textContent: label}});
            input.error = error; input.hint = hint;
            return input;
        };
        const keyPrefix = page === 'iec104' ? 'IOA' : 'ADDR';
        const a = makeField(keyPrefix + '_R_AnlikAkim', 0, 100, 'Fault current');
        const b = makeField(keyPrefix + '_S_AnlikAkim', 0, 100, 'Fault current');
        const c = makeField(keyPrefix + '_R_AnlikAkim', 1, 200, 'Fault current');
        const active = [element(), element()];
        active.forEach((input, index) => {
            input.type = 'checkbox'; input.checked = true;
            input.dataset = {id: prefix, key: 'inUse', i: String(index)};
        });
        const fields = [a, b, c];
        let fault;
        if (page === 'iec104') {
            fault = makeField('TemporaryFaultBase', 0, 100000, 'Temporary fault');
            fields.push(fault);
        }
        document.querySelectorAll = selector => {
            if (!selector.includes('[data-id="' + prefix + '"]')) return originalQueryAll(selector);
            const index = selector.match(/data-i="(\d+)"/);
            return (selector.includes(':not') ? fields : [...active, ...fields])
                .filter(input => !index || input.dataset.i === index[1]);
        };
        document.querySelector = selector => {
            const index = selector.match(/data-i="(\d+)"/);
            const key = selector.match(/data-key="([^"]+)"/);
            return [...active, ...fields].find(input => index && key && input.dataset.i === index[1] && input.dataset.key === key[1]) || null;
        };
        run(`pageCache['${page}'] = {};`);
        let posts = 0;
        context.fetch = async () => { posts++; return {ok: true, status: 200, json: async () => ({success: true})}; };
        await run(`savePage('${page}')`);
        assert.equal(posts, 0, page + ' duplicate address must block POST');
        assert.equal(a.classList.contains('invalid'), true);
        assert.equal(b.classList.contains('invalid'), true);
        assert.ok(a.error.textContent.includes('L2'));
        assert.ok(b.error.textContent.includes('L1'));
        const rendered = run(page === 'iec104' ? 'renderIec104({Hatlar:{}})' : 'renderModbus({Hat:{}})');
        const allKeys = [...new Set([...rendered.matchAll(/data-id="(?:iec|mod)" data-key="([^"]+)" data-i="0"/g)]
            .map(match => match[1]).filter(key => key !== 'inUse'))];
          assert.equal(allKeys.length, page === 'iec104' ? 17 : 12);
        const originalKey = b.dataset.key;
        // Every actual rendered address field must collide across categories.
        a.value = '1000';
        for (const key of allKeys) {
            b.dataset.key = key;
            b.value = '1000';
            await run(`savePage('${page}')`);
            assert.equal(posts, 0, page + ': ' + key + ' must block POST');
            assert.equal(a.classList.contains('invalid'), true, key);
            assert.equal(b.classList.contains('invalid'), true, key);
            // Also exercise each field on another feeder.
            b.dataset.i = '1';
            b.value = /FaultBase$/.test(key) ? '820' : '1000';
            await run(`savePage('${page}')`);
            assert.equal(posts, 0, page + ': cross-feeder ' + key);
            assert.equal(b.classList.contains('invalid'), true, key);
            b.dataset.i = '0';
        }
        b.dataset.key = originalKey;
        if (page === 'modbus') {
            a.value = '49499';
            b.value = '200';
            c.value = '300';
            await run("savePage('modbus')");
            assert.equal(posts, 0, 'FLOAT32 crossing RF quality must block POST');
            assert.ok(a.error.textContent.includes('49500'));
            assert.equal(a.classList.contains('invalid'), true);
        }
        a.value = '100';
        b.value = '102';
        if (page === 'modbus') {
            assert.ok(rendered.includes('oninput="updateModbusRegisterHint(this)"'));
            for (const key of allKeys) {
                context.hintField = a;
                a.dataset.key = key;
                a.value = '40001';
                run('updateModbusRegisterHint(hintField)');
                const isFloat = /_(AnlikAkim|AnlikAkim)$/.test(key);
                assert.equal(a.hint.textContent, isFloat ?
                    'FLOAT32 · 2 register · 40001–40002' :
                    'UINT16 · 1 register · 40001', key);
                assert.ok(run(`feederField('mod', '${key}', 0, 'Address', 40001)`)
                    .includes(a.hint.textContent), 'initial hint: ' + key);
            }
            a.dataset.key = keyPrefix + '_R_AnlikAkim';
            a.value = '40003';
            run('updateModbusRegisterHint(hintField)');
            assert.equal(a.hint.textContent, 'FLOAT32 · 2 register · 40003–40004');
            a.value = '';
            run('updateModbusRegisterHint(hintField)');
            assert.equal(a.hint.textContent, 'FLOAT32 · 2 register · —');
            a.value = '40001'; b.value = '40002';
            await run("savePage('modbus')");
            assert.equal(posts, 0, '40001/40002 overlap must block POST');
            assert.ok(a.error.textContent.includes('40002'));
            assert.ok(a.error.textContent.includes('L2'));
            assert.ok(b.error.textContent.includes('40002'));
            assert.ok(b.error.textContent.includes('L1'));
            a.value = '100';
            b.value = '101';
            await run("savePage('modbus')");
            assert.equal(posts, 0, 'FLOAT32 low word overlap must block POST');
            b.value = '102';
        }
        c.value = '100';
        await run(`savePage('${page}')`);
        assert.equal(posts, 0, 'cross-feeder duplicate must block POST');
        active[1].checked = false;
        await run(`savePage('${page}')`);
        assert.equal(posts, 1, 'inactive feeder must not block POST');
        active[1].checked = true;
        c.value = '200';
        if (page === 'iec104') {
            a.value = '100179';
            await run("savePage('iec104')");
            assert.equal(posts, 1, 'fault window collision must block POST');
            a.value = '100';
            context.fetch = async () => { posts++; return {ok: false, status: 400, text: async () => ioaError}; };
            await run("savePage('iec104')");
            assert.equal(c.classList.contains('invalid'), true);
            assert.equal(fault.classList.contains('invalid'), true);
            assert.ok(!c.error.textContent.includes('Hatlar.'));
            assert.ok(c.error.textContent.length > 0);
        } else {
            a.value = '65535';
            await run("savePage('modbus')");
            assert.equal(posts, 1, 'FLOAT32 range overflow must block POST');
            a.value = '100';
        }
        document.querySelector = originalQuery;
        document.querySelectorAll = originalQueryAll;
    }
    let rfPosts = 0;
    let rfWarning;
    const rfRequests = [];
    context.fetch = async (url, options = {}) => {
        rfRequests.push(url);
        if (options.method === 'POST') rfPosts++;
        return {ok: true, status: 200, json: async () => options.method === 'POST'
            ? {success: true, warning: rfWarning}
            : {State: 'idle', BatchState: 'idle', SaveBlocked: false}};
    };
    run("pageCache.rf={inUse:[true]};dirtyPages.rf=new Set();loading=false;curId='rf';");
    run("LNG='tr'");
    for(const [key,value] of Object.entries({HatID:1,ZoneID:1,
        SetEdilebilirAcmaArizaSayisi:3,OluHatAkimiDogrulamaSuresi:200,
        YenilenmeSifirlamaSuresi:30,SistemNominalAkimi:6,
        SetEdilebilirActirmaEsikAkimi:13,ArtimliAkimEsigi:1000,
        HatKopukHatBosta:2})){
        const input=document.getElementById('rf-'+key+'-0');
        input.type='number';
        input.value=String(value);
    }
    document.getElementById('rf-inUse-0').checked = true;
    for (const value of [1, 4]) {
        document.getElementById('rf-HatID-0').value = String(value);
        assert.equal(run("validatePage('rf')"), true);
    }
    for (const value of [0, 5, 6, 7]) {
        document.getElementById('rf-HatID-0').value = String(value);
        assert.equal(run("validatePage('rf')"), false,
            'active RF feeder must be in 1..4');
    }
    document.getElementById('rf-inUse-0').checked = false;
    document.getElementById('rf-HatID-0').value = '0';
    assert.equal(run("validatePage('rf')"), true,
        'an inactive row may remain unassigned');
    document.getElementById('rf-inUse-0').checked = true;
    document.getElementById('rf-HatID-0').value = '1';
    for (const value of [1, 2200]) {
        document.getElementById('rf-ArtimliAkimEsigi-0').value = String(value);
        assert.equal(run("validatePage('rf')"), true);
    }
    document.getElementById('rf-ArtimliAkimEsigi-0').value = '2200.1';
    assert.equal(run("validatePage('rf')"), false);
    document.getElementById('rf-ArtimliAkimEsigi-0').value = '1000';
    document.getElementById('rf-SistemNominalAkimi-0').value = '2';
    document.getElementById('rf-SetEdilebilirActirmaEsikAkimi-0').value = '2.4';
    assert.equal(run("validatePage('rf')"), false);
    document.getElementById('rf-SetEdilebilirActirmaEsikAkimi-0').value = '5';
    assert.equal(run("validatePage('rf')"), true);
    document.getElementById('rf-SistemNominalAkimi-0').value = '200';
    document.getElementById('rf-SetEdilebilirActirmaEsikAkimi-0').value = '240';
    assert.equal(run("validatePage('rf')"), true);
    document.getElementById('rf-SistemNominalAkimi-0').value = '6';
    document.getElementById('rf-SetEdilebilirActirmaEsikAkimi-0').value = '13';
    await run("savePage('rf')");
    assert.equal(rfPosts, 0, 'confirmation must precede save');
    assert.ok(notices.at(-1).innerHTML.includes('Satır 1'));
    document.getElementById('cancelConfirm').onclick();
    assert.equal(rfPosts, 0, 'cancel must not save');
    await run("savePage('rf')");
    await document.getElementById('okConfirm').onclick();
    assert.equal(rfPosts, 1);
    assert.ok(rfRequests.includes('/config/rf'));
    assert.ok(!rfRequests.some(url => url.includes('/config/rf/apply/')));
    assert.ok(rfRequests.includes('/status/rf-group'));
    rfWarning = 'backup_failed';
    await run("savePageData('rf',{inUse:[true]})");
    assert.ok(notices.at(-1).textContent.includes('yedek kayıt başarısız'));
    rfWarning = undefined;
    const inventoryProgress = run("buildRfGroupStatus({State:'idle',BatchState:'running',InventoryPending:true})");
    assert.ok(inventoryProgress.includes('Eski kayıtlar boşaltılıyor'));
    assert.equal(run("rfCanAbort({BatchState:'running',InventoryPending:true})"), false);
    const storageStopped = run("buildRfGroupStatus({State:'idle',BatchState:'stopped',StopReason:'storage'})");
    assert.ok(storageStopped.includes('RF uygulaması başlamadı'));
    const rfHtml = run('renderRf(pageCache.rf)');
    assert.match(rfHtml, /id="rf-HatID-0"[^>]*min="0"[^>]*max="4"/);
    assert.ok(!rfHtml.includes('rf-apply-group'));
    assert.ok(!rfHtml.includes('applyRfConfig()'));
    const progress = run("buildRfGroupStatus({State:'failed',BatchState:'stopped',Targets:7,Applied:1,BatchLine:2,GroupStarted:true})");
    assert.ok(progress.includes('Uygulama durdu'));
    assert.ok(progress.includes('Satır 1: Uygulandı'));
    assert.ok(progress.includes('Satır 2: Uygulama başarısız'));
    assert.ok(progress.includes('Satır 3: Uygulanmadı'));
    const stopped = run("buildRfGroupStatus({State:'idle',BatchState:'stopped',Targets:1,BatchLine:1,GroupStarted:false,StopReason:'start_rejected'})");
    assert.ok(stopped.includes('Envanteri, atamaları'));
    const partial = run("buildRfGroupStatus({State:'failed',HasReport:true,Reason:6})");
    assert.ok(partial.includes('Kısmi uygulama'));
    assert.ok(partial.includes('otomatik tekrar yapılmaz'));
    assert.ok(run("renderBoard({BatteryCapacityUnknown:1})").includes('Akü kapasitesi bilinmiyor'));
    assert.ok(!run("renderBoard({BatteryCapacityUnknown:null})").includes('Akü kapasitesi bilinmiyor'));
    context.fetch = async () => ({ok:true,status:200,json:async()=>({SaveBlocked:true,BatchState:'running'})});
    await run("savePage('rf')");
    assert.equal(rfPosts, 2, 'busy operation must not save');
    assert.equal(document.getElementById('save-rf').disabled, true);
    run('hideLoad()');
    assert.equal(document.getElementById('save-rf').disabled, true);
    context.fetch = async (url, options={}) => {
        if (options.method === 'POST') {rfPosts++;return {ok:false,status:500};}
        return {ok:true,status:200,json:async()=>({SaveBlocked:false,BatchState:'idle'})};
    };
    run("dirtyPages.rf.add('unsaved')");
    await run("savePage('rf')");
    await document.getElementById('okConfirm').onclick();
    assert.equal(run("hasDirty('rf')"), true, 'failed save must preserve edits');
    assert.equal(rfPosts, 3);
    // Opening RF and saving must not read application status automatically.
    let automaticStatusReads = 0;
    context.fetch = async url => {
        if (url === '/status/rf-group') automaticStatusReads++;
        return {ok:true,status:200,json:async()=>({success:true})};
    };
    run("renderPage('rf',pageCache.rf)");
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(automaticStatusReads,0,'opening RF must not refresh status');
    await run("savePageData('rf',{inUse:[true]})");
    assert.equal(automaticStatusReads,0,'save must leave status for manual refresh');
    // RF refresh must acknowledge a successful read even if state is unchanged.
    let abortPosts=0;
    context.fetch=async(url,options={})=>{
        if(options.method==='POST'){abortPosts++;return {ok:false,status:409,
            text:async()=> 'RF abort not started: check current group state'};}
        return {ok:true,status:200,json:async()=>({State:'idle',BatchState:'idle',SaveBlocked:false})};
    };
    scheduledTimers.length=0;
    await run('readRfGroupStatus(true)');
    assert.equal(run('loading'), false);
    assert.equal(document.getElementById('requestLoading').removed, true,
        'successful RF status reads must close the loading overlay');
    assert.ok(!scheduledTimers.some(timer=>timer.delay===5000),
        'manual refresh must not schedule another RF status read');
    assert.ok(document.getElementById('rf-status-read').textContent.includes(run("t('rfStatusUpdated')")),
        'successful refresh needs visible feedback');
    assert.equal(document.getElementById('rf-abort').disabled,true,
        'idle has no job to cancel');
    await run('abortRfConfig()');
    assert.equal(abortPosts,0,'idle cancel must not send POST');
    context.fetch=async(url,options={})=>{
        if(options.method==='POST'){abortPosts++;return {ok:false,status:409,
            text:async()=> 'RF abort not started: check current group state'};}
        return {ok:true,status:200,json:async()=>({State:'waiting',BatchState:'running',GroupStarted:true,SaveBlocked:true})};
    };
    await run('abortRfConfig()');
    assert.equal(abortPosts,1);
    assert.ok(notices.some(item=>item.textContent.includes('RF abort not started: check current group state')),
        '409 must preserve the server explanation');
    context.fetch=async()=>({ok:true,status:200,json:async()=>({State:'failed',BatchState:'stopped',GroupStarted:true,Targets:1,BatchLine:1,SaveBlocked:false,StopReason:'group_error'})});
    await run('readRfGroupStatus(true)');
    assert.ok(document.getElementById('rf-group-status').innerHTML.includes(run("t('rfBatchStopped')")));
    assert.equal(document.getElementById('rf-abort').disabled,true);
    assert.equal(document.getElementById('save-rf').disabled,false);

    const lastRfStatus=run('JSON.stringify(pageCache.rfGroup)');
    context.fetch=async()=>({ok:false,status:500});
    await run('readRfGroupStatus(true)');
    assert.equal(run('loading'), false);
    assert.equal(document.getElementById('requestLoading').removed, true,
        'failed RF status reads must close the loading overlay');
    assert.ok(document.getElementById('rf-status-read').textContent.includes('HTTP 500'));
    assert.equal(run('JSON.stringify(pageCache.rfGroup)'),lastRfStatus);
    assert.equal(document.getElementById('rf-status-refresh').disabled,false);
    run('hideLoad()');
    assert.equal(document.getElementById('rf-abort').disabled,true);

    let resolveStatus;
    context.fetch = async () => ({ok: true, status: 200,
        json: () => new Promise(resolve => {resolveStatus = resolve;})});
    const pendingStatus = run('readRfGroupStatus(true)');
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(run('loading'), true);
    const rfLoadingOverlay = document.getElementById('requestLoading');
    assert.equal(rfLoadingOverlay.className, 'loading');
    assert.ok(rfLoadingOverlay.innerHTML.includes('spinner'));
    assert.notEqual(rfLoadingOverlay.removed, true);
    assert.equal(await run('readRfGroupStatus(true)'), null,
        'a pending status read must not start a second request');
    run("pageCache.rf={inUse:[true]};invalidateRfGroupStatus()");
    resolveStatus({State: 'applied', MatchesDesired: true});
    await pendingStatus;
    assert.equal(run('loading'), false);
    assert.equal(rfLoadingOverlay.removed, true,
        'an invalidated RF response must also close its loading overlay');
    assert.equal(run('pageCache.rfGroup'), undefined,
        'a late status response for older desired settings must be ignored');
    assert.equal(document.getElementById('rf-status-refresh').disabled,false,
        'invalidated old reads must not leave refresh locked');
    console.log('PASS:', label, '- navigation, RF confirmed save/sequence, faults, languages, config saves, device cache/failure');

}

(async () => {
    const root = path.resolve(__dirname, '../../..');
    await checkPage(fs.readFileSync(path.join(root, 'web-page/index.html'), 'utf8'), 'source');
    const source = fs.readFileSync(path.join(root, 'Application/web-server/index_html.c'), 'utf8');
    const bytes = Buffer.from([...source.split('};')[0].matchAll(/0x([0-9a-f]{2})/gi)]
        .map(match => parseInt(match[1], 16)));
    await checkPage(zlib.gunzipSync(bytes).toString('utf8'), 'embedded');
})().catch(error => { console.error(error); process.exitCode = 1; });
