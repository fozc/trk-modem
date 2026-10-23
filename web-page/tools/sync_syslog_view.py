"""Embed the log-window UI in the single-file device page."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
page=root/'index.html'
text=page.read_text(encoding='utf-8')
begin='// SYSLOG VIEW BEGIN'
end='// SYSLOG VIEW END'
code=begin+'\n'+(root/'tools/syslog_view.js').read_text(encoding='utf-8')+'\n'+end
if begin in text:
    start=text.index(begin);stop=text.index(end,start)+len(end)
    text=text[:start]+code+text[stop:]
else:
    start=text.index('function renderLogs()');stop=text.index('\nfunction renderTerminal()',start)
    text=text[:start]+code+text[stop:]
    start=text.index('async function loadSysLogs()',text.index('// ---------------- FAULTS / LOGS'))
    stop=text.index('\n',start)
    text=text[:start]+text[stop:]
page.write_text(text,encoding='utf-8',newline='\n')
