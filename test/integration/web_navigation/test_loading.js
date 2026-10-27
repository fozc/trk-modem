/* Production loader entry point with HTTP/DOM boundaries replaced. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const zlib = require('node:zlib');
const {Blob} = require('node:buffer');
const root = path.resolve(__dirname, '../../..');

function resource(name) {
    const source = fs.readFileSync(path.join(root,
        `Application/web-server/${name}_html.c`), 'utf8').split('};')[0];
    return Buffer.from([...source.matchAll(/0x([0-9a-f]{2})/gi)]
        .map(match => parseInt(match[1], 16)));
}

function start(html, options = {}) {
    const script = html.match(/<script[^>]*>([\s\S]*?)<\/script>/i)[1];
    const elements = new Map();
    const timers = new Map();
    let nextTimer = 0;
    let streamController;
    let written = '';
    let closed = false;
    let fetches = 0;
    let logins = 0;
    const storage = new Map();
    const window = {};
    const token = options.token ?? '0123456789abcdef0123456789abcdef';
    const cacheStorage = options.cacheStorage ?? new Map();
    const etag = '"' + 'a'.repeat(64) + '"';
    const element = id => {
        if (!elements.has(id)) elements.set(id, {
            hidden: id === 'message' || id === 'retry', textContent: '',
            value: id === 'username' ? 'admin' : id === 'password' ? 'example' : 0,
            max: 1, setAttribute() {}, focus() {}});
        return elements.get(id);
    };
    const context = {
        console, Blob, Response, AbortController, window, atob, btoa,
        localStorage: {
            getItem(key) {if(options.storageDenied)throw new Error('denied');return cacheStorage.get(key) ?? null;},
            setItem(key,value) {if(options.storageDenied||options.quotaFull)throw new Error('quota');cacheStorage.set(key,value);},
            removeItem(key) {cacheStorage.delete(key);}
        },
        sessionStorage: {setItem: (key, value) => storage.set(key, value)},
        DecompressionStream: options.unsupported ? undefined : DecompressionStream,
        document: {getElementById: element, open() {},
            write(text) {written = text;}, close() {closed = true;}},
        setTimeout(callback) {timers.set(++nextTimer, callback);return nextTimer;},
        clearTimeout(id) {timers.delete(id);},
        fetch: async (url, settings) => {
            if (url === '/auth/login') {
                logins++;
                assert.equal(settings.method, 'POST');
                assert.deepEqual(JSON.parse(settings.body),
                    {username: 'admin', password: 'example'});
                if (options.loginTimeout) return new Promise((resolve, reject) => {
                    settings.signal.addEventListener('abort', () =>
                        reject(new DOMException('timeout', 'AbortError')));
                });
                if (options.loginMalformed) return new Response('{');
                if (options.loginNetworkError) throw new TypeError('network');
                return new Response(JSON.stringify(options.loginBody ??
                    {success: true, token, role: 'admin'}),
                    {status: options.loginStatus ?? 200});
            }
            fetches++;
            assert.equal(url, '/index.html.gz?t=' + token);
            assert.equal(settings.cache, 'no-store');
            if (options.expectedTag !== undefined)
                assert.equal(settings.headers['If-None-Match'], options.expectedTag);
            if (options.status === 304)
                return new Response(null, {status: 304,
                    headers: options.headers ?? {ETag: etag}});
            if (options.networkError) throw new Error('network');
            const stream = new ReadableStream({start(controller) {
                streamController = controller;
            }});
            settings.signal.addEventListener('abort', () => {
                streamController.error(new DOMException('timeout', 'AbortError'));
            });
            const headers = options.headers ?? {'Content-Length': String(resource('index').length), ETag: etag};
            return new Response(stream, {status: options.status ?? 200, headers});
        }
    };
    vm.runInNewContext(script, context);
    const submit = () => element('login').onsubmit({preventDefault() {}});
    if (options.autoSubmit !== false) submit();
    return {element, timers, submit, window, storage, cacheStorage, get logins() {return logins;}, get written() {return written;},
        get closed() {return closed;}, get fetches() {return fetches;},
        enqueue(bytes) {streamController.enqueue(new Uint8Array(bytes));},
        end() {streamController.close();}};
}

async function until(predicate) {
    const deadline = Date.now() + 5000;
    while (!predicate()) {
        assert.ok(Date.now() < deadline, 'loader did not reach the expected state');
        await new Promise(resolve => setImmediate(resolve));
    }
}

async function check(html, label) {
    const payload = resource('index');
    assert.ok(payload.length <= 48 * 1024,
        `index gzip is ${payload.length} bytes; loader and cache limit is 48 KiB`);
    const expected = zlib.gunzipSync(payload).toString('utf8');
    assert.ok(!/Downloading|Ana sayfa|Home|Uygulama indiriliyor/.test(html));
    const idle = start(html, {autoSubmit: false});
    assert.equal(idle.fetches, 0);
    assert.equal(idle.logins, 0);
    idle.element('username').value = '';
    await idle.submit();
    assert.equal(idle.logins, 0, 'blank credentials must not issue requests');
    const trial = start(html);
    await until(() => trial.element('progress').max === payload.length);
    assert.equal(trial.element('progress').value, 0);
    const first = Math.floor(payload.length / 3);
    trial.enqueue(payload.subarray(0, first));
    await until(() => trial.element('progress').value === first);
    assert.ok(trial.element('amount').textContent.endsWith('%33'));
    assert.ok(trial.element('amount').textContent.includes('KB'));
    const paused = trial.element('amount').textContent;
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(trial.element('amount').textContent, paused,
        'progress must advance with bytes, never with time');
    assert.equal(trial.written, '');
    for (let offset = first; offset < payload.length; offset += 1024)
        trial.enqueue(payload.subarray(offset, offset + 1024));
    trial.end();
    await until(() => trial.closed);
    assert.equal(trial.written, expected);
    assert.equal(trial.element('progress').value, payload.length);
    assert.equal(trial.timers.size, 0);
    assert.equal(trial.window.pageSession.token, trial.storage.get('sessionToken'));
    assert.equal(trial.window.pageSession.role, 'admin');
    assert.equal(trial.element('password').value, '');
    assert.equal(trial.logins, 1);

    const cacheKey = 'tsb23.pageCache';
    const savedCache = trial.cacheStorage.get(cacheKey);
    assert.ok(savedCache, 'verified gzip must be cached');
    const parsedCache = JSON.parse(savedCache);
    assert.deepEqual(Object.keys(parsedCache).sort(), ['data','etag']);
    assert.equal(Buffer.from(parsedCache.data, 'base64').compare(payload), 0);
    const cached = start(html, {cacheStorage: trial.cacheStorage, status:304,
        token:'fedcba9876543210fedcba9876543210', expectedTag: parsedCache.etag});
    await until(() => cached.closed);
    assert.equal(cached.written, expected);
    assert.equal(cached.element('amount').textContent, 'Önbellekten açılıyor');
    assert.equal(cached.logins, 1, 'cached content still requires a new login');
    assert.equal(cached.timers.size, 0);
    const updated = start(html, {cacheStorage: trial.cacheStorage,
        expectedTag:parsedCache.etag,
        headers:{'Content-Length':String(payload.length), ETag:'"'+'b'.repeat(64)+'"'}});
    await until(() => updated.element('progress').max===payload.length);
    updated.enqueue(payload); updated.end();
    await until(() => updated.closed);
    assert.notEqual(JSON.parse(trial.cacheStorage.get(cacheKey)).etag, parsedCache.etag);
    for (const options of [{quotaFull:true}, {storageDenied:true},
            {cacheStorage:new Map([[cacheKey,'{invalid']]),expectedTag:undefined},
            {cacheStorage:new Map([[cacheKey,JSON.stringify({etag:'invalid',data:'AA=='})]])}]) {
        const uncached=start(html,options);
        await until(()=>uncached.element('progress').max===payload.length);
        uncached.enqueue(payload);uncached.end();
        await until(()=>uncached.closed);
        assert.equal(uncached.written,expected);
    }
    for (const options of [
        {status:304},
        {status:304, cacheStorage:new Map([[cacheKey,savedCache]]),headers:{ETag:'"'+'c'.repeat(64)+'"'}},
        {status:304,corruptCache:true,cacheStorage:new Map([[cacheKey,JSON.stringify({etag:parsedCache.etag,data:'AAAA'})]])}
    ]) {
        const invalid=start(html,options);
        await until(()=>!invalid.element('message').hidden);
        assert.equal(invalid.closed,false);
        assert.equal(invalid.timers.size,0);
        if(options.corruptCache)assert.equal(options.cacheStorage.has(cacheKey),false,
            'corrupt cached gzip must be removed');
    }
    const failedCases = [
        {status: 401}, {status: 503}, {networkError: true}, {unsupported: true},
        {headers: {}}, {headers: {'Content-Length': '1.5'}},
        {headers: {'Content-Length': '999999'}},
        {headers: {'Content-Length': String(payload.length),
                   'Content-Encoding': 'gzip'}}
    ];
    for (const options of failedCases) {
        const failed = start(html, options);
        await until(() => !failed.element('message').hidden);
        assert.equal(failed.written, '');
        assert.ok(failed.element('message').textContent.length > 0);
        assert.equal(failed.timers.size, 0);
        if (options.unsupported) assert.equal(failed.fetches, 0);
    }
    for (const options of [
        {loginStatus: 401, loginBody: {success: false, error: 'Invalid credentials'}},
        {loginStatus: 429, loginBody: {success: false, error: 'Too many attempts, try again later'}},
        {loginNetworkError: true}, {loginMalformed: true},
        {loginBody: {success: true, token: 'invalid', role: 'admin'}},
        {loginBody: {success: true, token: '0123456789abcdef0123456789abcdef', role: 'invalid'}}
    ]) {
        const failed = start(html, options);
        await until(() => !failed.element('message').hidden);
        assert.equal(failed.fetches, 0, 'failed login must not download the page');
        assert.equal(failed.storage.size, 0);
        assert.equal(failed.element('submit').disabled, false);
        assert.equal(failed.window.pageSession, undefined);
        assert.equal(failed.timers.size, 0);
    }
    const stalledLogin = start(html, {loginTimeout: true});
    await until(() => stalledLogin.logins === 1);
    [...stalledLogin.timers.values()][0]();
    await until(() => !stalledLogin.element('message').hidden);
    assert.equal(stalledLogin.fetches, 0);
    assert.equal(stalledLogin.element('submit').disabled, false);
    assert.equal(stalledLogin.timers.size, 0);
    const userLogin = start(html, {loginBody: {success: true,
        token: '0123456789abcdef0123456789abcdef', role: 'user'}});
    await until(() => userLogin.element('progress').max === payload.length);
    userLogin.enqueue(payload);
    userLogin.end();
    await until(() => userLogin.closed);
    assert.equal(userLogin.window.pageSession.role, 'user');
    assert.equal(userLogin.storage.get('sessionRole'), 'user');
    const duplicate = start(html);
    duplicate.submit();
    await until(() => duplicate.element('progress').max === payload.length);
    assert.equal(duplicate.logins, 1, 'double submit must not duplicate login/download');
    assert.equal(duplicate.fetches, 1);
    duplicate.end();
    await until(() => !duplicate.element('retry').hidden);
    for (const failure of ['short', 'long', 'corrupt', 'timeout']) {
        const failed = start(html);
        await until(() => failed.element('progress').max === payload.length);
        if (failure === 'timeout') [...failed.timers.values()][0]();
        else {
            const body = failure === 'short' ? payload.subarray(0, first) :
                failure === 'long' ? Buffer.concat([payload, Buffer.from([0])]) :
                Buffer.from(payload);
            if (failure === 'corrupt') body[body.length - 8] ^= 0x80;
            failed.enqueue(body);
            failed.end();
        }
        await until(() => !failed.element('message').hidden);
        assert.equal(failed.written, '');
        assert.equal(failed.timers.size, 0);
    }
    console.log('PASS:', label,
        '- real byte progress, native gzip, exact HTML, login gate, session handoff, duplicate submit, cache revalidation across logins, storage limits, 22 failure paths');
}

(async () => {
    await check(fs.readFileSync(path.join(root, 'web-page/loading.html'), 'utf8'),
                'source loading page');
    await check(zlib.gunzipSync(resource('loading')).toString('utf8'),
                'embedded loading page');
})().catch(error => {console.error(error);process.exitCode = 1;});
