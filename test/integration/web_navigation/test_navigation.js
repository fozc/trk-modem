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
            addEventListener() {}, focus() {}, remove() {},
            appendChild(child) { notices.push(child); }
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
    const context = vm.createContext({
        document, console,
        localStorage: {getItem: () => 'tr', setItem() {}},
        sessionStorage: {getItem: () => null, setItem() {}, removeItem() {}},
        setTimeout: () => 1, clearTimeout() {}, AbortController,
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
        request = {url, data: JSON.parse(options.body)};
        return {ok: true, status: 200, json: async () => ({success: true})};
    };
    for (const page of ['iec104', 'modbus', 'rf']) {
        run(`pageCache['${page}'] = {};`);
        await run(`savePage('${page}')`);
        assert.equal(request.url, '/config/' + page);
        assert.deepEqual(JSON.parse(run(`JSON.stringify(pageCache['${page}'])`)), request.data);
    }
    const ioaError = 'IOA overlap: Hatlar.IOA_R_ArizaAkimi[1] (feeder 2) and Hatlar.TemporaryFaultBase[0] (feeder 1)';
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
        const a = makeField(keyPrefix + '_R_ArizaAkimi', 0, 100, 'Fault current');
        const b = makeField(keyPrefix + '_S_ArizaAkimi', 0, 100, 'Fault current');
        const c = makeField(keyPrefix + '_R_ArizaAkimi', 1, 200, 'Fault current');
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
        assert.equal(allKeys.length, page === 'iec104' ? 23 : 21);
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
        a.value = '100';
        b.value = '102';
        if (page === 'modbus') {
            assert.ok(rendered.includes('oninput="updateModbusRegisterHint(this)"'));
            for (const key of allKeys) {
                context.hintField = a;
                a.dataset.key = key;
                a.value = '40001';
                run('updateModbusRegisterHint(hintField)');
                const isFloat = /_(ArizaAkimi|AnlikAkim)$/.test(key);
                assert.equal(a.hint.textContent, isFloat ?
                    'FLOAT32 · 2 register · 40001–40002' :
                    'UINT16 · 1 register · 40001', key);
                assert.ok(run(`feederField('mod', '${key}', 0, 'Address', 40001)`)
                    .includes(a.hint.textContent), 'initial hint: ' + key);
            }
            a.dataset.key = keyPrefix + '_R_ArizaAkimi';
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
    console.log('PASS:', label, '- navigation, faults, languages, all config saves, device cache/failure');

}

(async () => {
    const root = path.resolve(__dirname, '../../..');
    await checkPage(fs.readFileSync(path.join(root, 'web-page/index.html'), 'utf8'), 'source');
    const source = fs.readFileSync(path.join(root, 'Application/web-server/index_html.c'), 'utf8');
    const bytes = Buffer.from([...source.split('};')[0].matchAll(/0x([0-9a-f]{2})/gi)]
        .map(match => parseInt(match[1], 16)));
    await checkPage(zlib.gunzipSync(bytes).toString('utf8'), 'embedded');
})().catch(error => { console.error(error); process.exitCode = 1; });
