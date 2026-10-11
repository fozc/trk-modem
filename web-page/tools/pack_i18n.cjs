/* Compact literal dictionaries in build output; keep source dictionaries. */
const {parse} = require('acorn');

function readDictionary(node) {
    if (node.type !== 'ObjectExpression') return null;
    const entries = [];
    const seen = new Set();
    for (const property of node.properties) {
        if (property.type !== 'Property' || property.computed ||
            property.kind !== 'init' || property.method || property.shorthand)
            return null;
        const key = property.key.name ?? property.key.value;
        if (typeof key !== 'string' || key === '__proto__' || seen.has(key))
            return null;
        seen.add(key);
        entries.push([key, property.value]);
    }
    return entries;
}

function packI18n(source) {
    const ast = parse(source, {ecmaVersion: 'latest', sourceType: 'script'});
    for (const statement of ast.body) {
        if (statement.type !== 'VariableDeclaration') continue;
        const declaration = statement.declarations.find(d =>
            d.id.type === 'Identifier' && d.id.name === 'I18N');
        if (!declaration || !declaration.init) continue;
        const languages = readDictionary(declaration.init);
        if (!languages || languages.length !== 2 ||
            !languages.some(([name]) => name === 'en') ||
            !languages.some(([name]) => name === 'tr')) return source;
        let keys;
        const values = {};
        for (const [language, object] of languages) {
            const entries = readDictionary(object);
            if (!entries || entries.length === 0 || entries.some(([, value]) =>
                value.type !== 'Literal' || typeof value.value !== 'string'))
                return source;
            const names = entries.map(([name]) => name);
            if (keys && JSON.stringify(keys) !== JSON.stringify(names))
                return source;
            keys = names;
            values[language] = entries.map(([, value]) => value.value);
        }
        const allStrings = keys.concat(...Object.values(values));
        const separator = ['\x1f', '\x1e', '\x1d'].find(candidate =>
            allStrings.every(text => !text.includes(candidate)));
        if (!separator) return source;
        const packed = Object.fromEntries(Object.entries(values).map(
            ([language, texts]) => [language, texts.join(separator)]));
        const sep = JSON.stringify(separator);
        const expression = '(()=>{const keys=' +
            JSON.stringify(keys.join(separator)) + '.split(' + sep + ');' +
            'const values=' + JSON.stringify(packed) + ';const result={};' +
            'for(const lang of ' + JSON.stringify(languages.map(([name]) => name)) +
            '){const texts=values[lang].split(' + sep + ');' +
            'result[lang]=Object.fromEntries(keys.map((key,index)=>' +
            '[key,texts[index]]))}return result})();';
        /* Replace only the initializer, including multi-declaration cases. */
        return source.slice(0, declaration.init.start) +
            expression.slice(0, -1) + source.slice(declaration.init.end);
    }
    return source;
}

module.exports = {packI18n};
