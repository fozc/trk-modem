/* Parser-based JavaScript minification for embedded web resources. */
const fs = require('node:fs');
const {minify} = require('terser');
const {packI18n} = require('./pack_i18n.cjs');

async function minifyJavascript(source) {
    const result = await minify(packI18n(source), {
        ecma: 2020,
        module: false,
        toplevel: false,
        compress: {passes: 2, sequences: false, unsafe: false, toplevel: false},
        mangle: {toplevel: false, properties: false},
        format: {comments: false, inline_script: true}
    });
    return result.code;
}

module.exports = {minifyJavascript};

if (require.main === module) {
    minifyJavascript(fs.readFileSync(0, 'utf8')).then(code => {
        process.stdout.write(code);
    }).catch(error => {
        console.error('JavaScript minification failed: ' + error.message);
        process.exitCode = 1;
    });
}
