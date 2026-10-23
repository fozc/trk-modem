"""Embed the portable profile contract in the single-file web application."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
page=root/'index.html'
text=page.read_text(encoding='utf-8')
begin='// CONFIG PROFILE BEGIN'
end='// CONFIG PROFILE END'
schema=json.loads((root/'config_profile_schema.json').read_text(encoding='utf-8'))
# Display labels belong to the source contract; validation uses only rules.
for fields in schema.values():
    for rule in fields.values():
        rule.pop('label',None)
code=begin+'\nconst CONFIG_PROFILE_SCHEMA='+json.dumps(schema,separators=(',',':'),ensure_ascii=True)+';\n'+(root/'tools/config_profiles.js').read_text(encoding='utf-8')+'\n'+end
if begin in text:
    start=text.index(begin);stop=text.index(end,start)+len(end)
    text=text[:start]+code+text[stop:]
else:
    text=text.replace('// ---------------- PAGE LOAD / SAVE ----------------',code+'\n\n// ---------------- PAGE LOAD / SAVE ----------------')
page.write_text(text,encoding='utf-8',newline='\n')
