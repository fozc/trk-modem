/* Regression checks for industrial web behavior. Failures return exit code 1.
 * No device access. Production page functions run in the existing VM style.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const zlib = require('node:zlib');

function fixture(html) {
    const elements = new Map();
    const timers = new Map();
    const listeners = new Map();
    let timerId = 0;
    function element() {
        const classes = new Set();
        return {
            value: '', type: 'text', dataset: {}, style: {}, innerHTML: '',
            textContent: '', disabled: false,
            classList: {
                add: value => classes.add(value),
                remove: value => classes.delete(value),
                toggle(value, active) {
                    if (active) classes.add(value);
                    else classes.delete(value);
                }
            },
            closest: () => null, querySelector: () => null,
            querySelectorAll: () => [], addEventListener() {}, focus() {},
            remove() {}, appendChild() {}
        };
    }
    const document = {
        getElementById(id) {
            if (!elements.has(id)) elements.set(id, element());
            return elements.get(id);
        },
        createElement: element, querySelectorAll: () => [],
        querySelector: () => null, addEventListener() {},
        body: element(), documentElement: {}
    };
    const context = vm.createContext({
        document, window: {addEventListener: (name, fn) => listeners.set(name, fn)}, console, AbortController,
        localStorage: {getItem: () => 'tr', setItem() {}},
        sessionStorage: {getItem: () => null, setItem() {}, removeItem() {}},
        setTimeout(callback, delay) {
            timers.set(++timerId, {callback, delay});
            return timerId;
        },
        clearTimeout: id => timers.delete(id),
        fetch: async () => ({ok: true, status: 200,
            json: async () => ({success: true})})
    });
    const script = html.match(/<script[^>]*>([\s\S]*?)<\/script>/i)[1];
    vm.runInContext(script.replace(/init\(\);\s*$/, ''), context);
    vm.runInContext("s_role='admin'", context);
    return {context, elements, timers, document, listeners,
        run: code => vm.runInContext(code, context)};
}

const cases = [
    ['output retention is editable, serialized and rendered in both languages', async f => {
        const checkbox = f.document.getElementById('dev-PreserveOutputStates');
        checkbox.type = 'checkbox';
        for (const enabled of [false, true]) {
            checkbox.checked = enabled;
            assert.equal(f.run('extractDevice().PreserveOutputStates'), enabled);
            for (const language of ['tr', 'en']) {
                f.run(`LNG='${language}'`);
                const html = f.run(`renderDevice({PreserveOutputStates:${enabled}})`);
                assert.ok(html.includes(f.run("t('retainOutputs')")));
                assert.ok(html.includes(f.run("t('retainOutputsHelp')")));
                assert.match(html, enabled
                    ? /id="dev-PreserveOutputStates"[^>]*checked/
                    : /id="dev-PreserveOutputStates"[^>]*type="checkbox" >/);
            }
        }
    }],
    ['IEC104 zero interval help matches disabled periodic sending', async f => {
        for (const [language, expected] of [
            ['en', '0 disables periodic sending'],
            ['tr', '0 degeri periyodik gonderimi kapatir']]) {
            f.run(`LNG='${language}'`);
            const help = f.run("t('iecSboHelp')")
                .normalize('NFD').replace(/[\u0300-\u036f]/g, '')
                .replace(/\u0131/g, 'i');
            assert.ok(help.includes(expected));
        }
    }],
    ['WEB-01 logout clears privileged cached configuration', async f => {
        f.run("s_sessionToken='old';s_role='admin';" +
            "pageCache.device={SimKartAPNSifresi:'audit-secret'};" +
            "pageCache.iec104={Port:2404};");
        await f.run('doLogout()');
        assert.equal(f.run('pageCache.device'), undefined,
            'previous admin device configuration survives logout');
    }],
    ['WEB-01 user login cannot render previous admin data', async f => {
        f.run("s_sessionToken='old';s_role='admin';" +
            "pageCache.device={SimKartAPNSifresi:'audit-secret'};");
        await f.run('doLogout()');
        f.document.getElementById('loginUser').value = 'user';
        f.document.getElementById('loginPass').value = 'fixture';
        f.context.fetch = async () => ({ok: true, status: 200,
            json: async () => ({success: true, token: 'new', role: 'user'})});
        await f.run('doLogin()');
        f.run("switchPageNow('device')");
        assert.ok(!f.document.getElementById('body').innerHTML
            .includes('audit-secret'),
        'user page renders a previous admin APN password without GET');
    }],
    ['WEB-01 expired session clears cached configuration', async f => {
        f.run("s_sessionToken='old';s_role='admin';" +
            "pageCache.device={SimKartAPNSifresi:'audit-secret'};");
        f.context.fetch = async () => ({ok: false, status: 401});
        await assert.rejects(f.run("api('/config/device')"));
        assert.equal(f.run('pageCache.device'), undefined,
            '401 resets role/token but preserves previous admin data');
    }],
    ['WEB-02 fractional Modbus ID is rejected before POST', async f => {
        f.run('pageCache.modbus={CihazID:37,BaudRate:19200,StopBits:1}');
        for (const [id, value] of [['mod-CihazID', '37.9'],
            ['mod-BaudRate', '19200'], ['mod-StopBits', '1']]) {
            const el = f.document.getElementById(id);
            el.type = 'number'; el.value = value;
        }
        let posted = null;
        f.context.fetch = async (url, options) => {
            posted = JSON.parse(options.body);
            return {ok: true, status: 200, json: async () => ({success:true})};
        };
        await f.run("savePage('modbus')");
        assert.equal(posted, null,
            '37.9 was accepted and posted as ' + JSON.stringify(posted));
    }],
    ['WEB-02 exponent notation keeps its numeric value or is rejected',
        async f => {
            f.run('pageCache.modbus={CihazID:37,BaudRate:19200,StopBits:1}');
            for (const [id, value] of [['mod-CihazID', '1e2'],
                ['mod-BaudRate', '19200'], ['mod-StopBits', '1']]) {
                const el = f.document.getElementById(id);
                el.type = 'number'; el.value = value;
            }
            let posted = null;
            f.context.fetch = async (url, options) => {
                posted = JSON.parse(options.body);
                return {ok:true,status:200,json:async()=>({success:true})};
            };
            await f.run("savePage('modbus')");
            assert.ok(posted === null || posted.CihazID === 100,
                '1e2 was validated as 100 but posted as ' +
                JSON.stringify(posted));
        }],
    ['WEB-03 response body remains protected by request timeout', async f => {
        let finishBody;
        f.context.fetch = async () => ({ok:true,status:200,
            json: () => new Promise(resolve => {finishBody = resolve;})});
        const pending = f.run("api('/status/board')");
        await new Promise(resolve => setImmediate(resolve));
        const active = [...f.timers.values()]
            .some(timer => timer.delay === 30000);
        finishBody({});
        await pending;
        assert.ok(active, 'timeout removed when headers arrive, before body');
    }],
    ['WEB-04 NOT_LIVE bitmap identifies the affected group member', async f => {
        const html = f.run("buildRfFailure({State:'failed',HasReport:true," +
            "MHState:4,Reason:1,MemberBitmap:4,WritesAcked:3," +
            "Line:1,Feeder:3})");
        assert.ok(!html.includes(f.run("t('rfNoPhaseDetail')")),
            'bitmap 0x04 identifies member 3 but UI denies useful detail');
        assert.ok(html.includes(f.run("tf('rfFailedMembers',{members:'3'})")),
            'show accepted group member without inventing a phase');
    }],
    ['WEB-05 Modbus dropdown omits server-rejected 230400 baud', async f => {
        // json_config.c validate_baud_rate rejects 230400 (E1 source trace).
        const html = f.run('renderModbus({CihazID:1,BaudRate:19200})');
        assert.ok(!html.includes('value="230400"'),
            'UI offers 230400 but current production parser rejects it');
    }],
    ['logout clears local data before server response arrives', async f => {
        let finish;
        f.run("s_sessionToken='old';pageCache.device={SimKartAPNSifresi:'old'}");
        f.context.fetch = () => new Promise(resolve => {finish = resolve;});
        const pending = f.run('doLogout()');
        assert.equal(f.run('pageCache.device'), undefined);
        finish({ok:true,status:200,json:async()=>({success:true})});
        await pending;
    }],
    ['late body cannot repopulate cache after logout', async f => {
        let finish;
        f.run("s_sessionToken='old'");
        f.context.fetch = async () => ({ok:true,status:200,
            json: () => new Promise(resolve => {finish = resolve;})});
        const pending = f.run("readPage('device')");
        await new Promise(resolve => setImmediate(resolve));
        f.run('clearSession()');
        finish({SimKartAPNSifresi:'old'});
        await pending;
        assert.equal(f.run('pageCache.device'), undefined);
    }],
    ['late 401 cannot log out a newer session', async f => {
        let finish;
        f.run("s_sessionToken='old'");
        f.context.fetch = () => new Promise(resolve => {finish = resolve;});
        const pending = f.run("api('/config/device')");
        f.run("clearSession();s_sessionToken='new';s_role='user'");
        finish({ok:false,status:401});
        await assert.rejects(pending);
        assert.equal(f.run('s_sessionToken'), 'new');
        assert.equal(f.run('s_role'), 'user');
    }],
    ['body timeout aborts the request and releases loading state', async f => {
        f.context.fetch = async (url, options) => ({ok:true,status:200,
            json: () => new Promise((resolve, reject) => {
                options.signal.addEventListener('abort', () =>
                    reject(Object.assign(new Error('aborted'), {name:'AbortError'})));
            })});
        const pending = f.run("readPage('board')");
        await new Promise(resolve => setImmediate(resolve));
        const timer = [...f.timers.values()].find(t => t.delay === 30000);
        assert.ok(timer, 'body must keep the abort deadline');
        timer.callback();
        await pending;
        assert.equal(f.run('loading'), false);
        assert.equal(f.run('pageCache.board'), undefined);
        assert.ok(![...f.timers.values()].some(t => t.delay === 30000));
    }],
    ['user cannot POST settings and readonly controls survive hideLoad', async f => {
        f.run("s_role='user';pageCache.modbus={CihazID:1,BaudRate:19200}");
        let requests = 0;
        f.context.fetch = async () => {requests++;throw new Error('unexpected');};
        await f.run("savePage('modbus')");
        await f.run("savePageData('modbus',{CihazID:2})");
        assert.equal(requests, 0);
        assert.ok(!f.run('renderModbus(pageCache.modbus)').includes('id="save-modbus"'));
        const input = f.document.getElementById('mod-CihazID');
        const assign = f.document.getElementById('assignment');
        f.document.getElementById('body').querySelectorAll = () => [input, assign];
        f.run('hideLoad()');
        assert.equal(input.disabled, true);
        assert.equal(assign.disabled, true);
    }],
    ['RF fractional measurements remain valid and sparse bitmap is explicit', async f => {
        const input = f.document.getElementById('current');
        input.type='number';input.value='0.3';
        assert.equal(f.run("validation('current',true,0.3,5,false)"), true);
        assert.equal(f.run("validation('current',true,0.3,5)"), false);
        for (const language of ['tr', 'en']) {
            f.run(`LNG='${language}'`);
            const html = f.run("buildRfFailure({State:'failed',HasReport:true," +
                "MHState:4,Reason:1,MemberBitmap:5,Line:1,Feeder:1})");
            assert.ok(html.includes(f.run("tf('rfFailedMembers',{members:'1, 3'})")));
            const unknown = f.run("buildRfFailure({State:'failed',HasReport:true," +
                "MHState:4,Reason:1,MemberBitmap:0})");
            assert.ok(unknown.includes(f.run("t('rfNoPhaseDetail')")));
        }
    }],
    ['browser unload warns only while a draft is dirty', async f => {
        let warned = false;
        const event = {preventDefault: () => {warned = true;}};
        const handler = f.listeners.get('beforeunload');
        assert.ok(handler);
        handler(event);
        assert.equal(warned, false);
        f.run("dirtyPages.rf=new Set(['draft'])");
        handler(event);
        assert.equal(warned, true);
        assert.equal(event.returnValue, '');
    }]
];

(async () => {
    const root = path.resolve(__dirname, '../../..');
    const source = fs.readFileSync(path.join(root,
        'web-page/index.html'), 'utf8');
    const embedded = fs.readFileSync(path.join(root,
        'Application/web-server/index_html.c'), 'utf8');
    const bytes = Buffer.from([...embedded.split('};')[0]
        .matchAll(/0x([0-9a-f]{2})/gi)].map(m => parseInt(m[1], 16)));
    let failed = 0;
    for (const [label, html] of [['source', source],
        ['embedded', zlib.gunzipSync(bytes).toString('utf8')]]) {
        for (const [name, test] of cases) {
            try {
                await test(fixture(html));
                console.log('PASS:', label, name);
            } catch (error) {
                failed++;
                console.log('FAIL:', label, name, '-', error.message);
            }
        }
    }
    console.log('Audit:', cases.length * 2, 'checks,', failed, 'failures');
    process.exitCode = failed ? 1 : 0;
})().catch(error => { console.error(error); process.exitCode = 2; });
