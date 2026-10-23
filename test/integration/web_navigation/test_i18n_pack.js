/* Verify the real build pipeline preserves the dictionary interface. */
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const {parse} = require('../../../web-page/tools/node_modules/acorn');
const {minifyJavascript} = require('../../../web-page/tools/minify_js.cjs');
const {packI18n} = require('../../../web-page/tools/pack_i18n.cjs');

async function compare(source, expression) {
    const output = await minifyJavascript(source);
    const expected = vm.runInNewContext(source + ';JSON.stringify(' +
        expression + ')');
    const actual = vm.runInNewContext(output + ';JSON.stringify(' +
        expression + ')');
    assert.equal(actual, expected);
    return output;
}

function dictionary(html) {
    const script = html.match(/<script[^>]*>([\s\S]*?)<\/script>/i)[1];
    const ast = parse(script, {ecmaVersion: 'latest'});
    for (const statement of ast.body) {
        if (statement.type !== 'VariableDeclaration') continue;
        const declaration = statement.declarations.find(d =>
            d.id.type === 'Identifier' && d.id.name === 'I18N');
        if (declaration) return vm.runInNewContext('JSON.stringify(' +
            script.slice(declaration.init.start, declaration.init.end) + ')');
    }
    assert.fail('I18N initializer missing');
}

(async () => {
    const output = await compare(`
const before=7,I18N={
    en:{empty:'',text:'\\u011f\\n  <b> {n}',rfWait_storage:'Store',sep:'x\\u001fy'},
    tr:{empty:'',text:'TR  <b> {n}',rfWait_storage:'Kayit',sep:'x\\u001fy'}
},after='tail';
let LNG='tr';
function t(k){const d=I18N[LNG];return d[k]===undefined?k:d[k]}
function tf(k,n){return t(k).replace('{n}',n)}
`, `{before,after,en:I18N.en,tr:I18N.tr,keys:Object.keys(I18N.en),
     label:t('rfWait_'+'storage'),unknown:t('missing'),empty:t('empty'),
     formatted:tf('text',5)}`);
    assert.ok(output.includes('Object.fromEntries'));
    await compare(`const I18N={en:{label:'EN'},tr:{label:'TR',extra:'More'}};`,
                  'I18N');
    await compare(`const I18N={en:{a:'A',b:'B'},tr:{b:'BB',a:'AA'}};`,
                  '{en:Object.keys(I18N.en),tr:Object.keys(I18N.tr)}');
    await compare(`const I18N={en:{label:()=> 'EN'},tr:{label:()=> 'TR'}};`,
                  '{en:I18N.en.label(),tr:I18N.tr.label()}');
    await compare(`const I18N={en:{text:'a\\u001db\\u001ec\\u001f'},
                              tr:{text:'d\\u001de\\u001ef\\u001f'}};`, 'I18N');
    const root = path.resolve(__dirname, '../../..');
    for (const page of ['index', 'fw_update']) {
        const source = fs.readFileSync(path.join(root,
            'web-page/' + page + '.html'), 'utf8');
        if (page === 'index') {
            const texts = JSON.parse(dictionary(source));
            assert.deepEqual(Object.keys(texts.en), Object.keys(texts.tr),
                'index translations must have matching keys/order for packing');
            const script = source.match(/<script[^>]*>([\s\S]*?)<\/script>/i)[1];
            assert.notEqual(packI18n(script), script,
                'production dictionary packing must actually run');
        }
        const array = fs.readFileSync(path.join(root,
            'Application/web-server/' + page + '_html.c'), 'utf8');
        const bytes = Buffer.from([...array.split('};')[0].matchAll(
            /0x([0-9a-f]{2})/gi)].map(x => parseInt(x[1], 16)));
        assert.equal(dictionary(zlib.gunzipSync(bytes).toString('utf8')),
                     dictionary(source));
    }
    console.log('PASS: compact I18N - lossless texts, empty/Unicode/separators, ' +
        'public dictionary, dynamic keys, safe skip and complete source/gzip equality');
})().catch(error => {console.error(error);process.exitCode=1;});
