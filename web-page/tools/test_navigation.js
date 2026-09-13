/* Run with node web-page/tools/test_navigation.js. */
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
        'iec-SBOTimeout': 30000, 'iec-AkuUyarisi': 10000,
        'iec-ModemReset': 10001, 'mod-CihazID': 1, 'mod-BaudRate': 115200
    };
    for (const [id, value] of Object.entries(configFields)) {
        const input = document.getElementById(id);
        input.type = 'number';
        input.value = String(value);
    }
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
    console.log('PASS:', label, '- navigation, faults, languages, all config saves, device cache/failure');

}

(async () => {
    const root = path.resolve(__dirname, '../..');
    await checkPage(fs.readFileSync(path.join(root, 'web-page/index.html'), 'utf8'), 'source');
    const source = fs.readFileSync(path.join(root, 'Application/web-server/index_html.c'), 'utf8');
    const bytes = Buffer.from([...source.split('};')[0].matchAll(/0x([0-9a-f]{2})/gi)]
        .map(match => parseInt(match[1], 16)));
    await checkPage(zlib.gunzipSync(bytes).toString('utf8'), 'embedded');
})().catch(error => { console.error(error); process.exitCode = 1; });
