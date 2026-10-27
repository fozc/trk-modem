"""Regression checks for the production embedded-web build pipeline."""
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CONVERTER = ROOT / "web-page/tools/html_to_c.py"
spec = importlib.util.spec_from_file_location(
    "web_minifier", ROOT / "web-page/tools/minify_html.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EmbeddedWebMinifierTest(unittest.TestCase):
    def test_generated_etag_tracks_exact_bytes_and_encoding(self):
        spec = importlib.util.spec_from_file_location("html_to_c", CONVERTER)
        converter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(converter)
        tags = set()
        with tempfile.TemporaryDirectory() as folder:
            for compression, flag in ((None, 0), ("gzip", 1), ("br", 2)):
                for payload in (b"abc", b"abd", b"", b"\x00\xff\r\n"):
                    content = (payload if compression else
                               payload.decode("latin1"))
                    expected_bytes = (payload if compression else
                                      content.encode("utf-8"))
                    converter.write_c_array(content, "fixture", folder,
                                            "fixture.html", compression)
                    source = (Path(folder) / "fixture_html.c").read_text()
                    literal = re.search(
                        r'\.etag = ("(?:\\.|[^"\\])*")', source)[1]
                    tag = json.loads(literal)
                    expected = '"' + hashlib.sha256(
                        bytes([flag]) + expected_bytes).hexdigest() + '"'
                    self.assertEqual(expected, tag)
                    tags.add(tag)
        self.assertEqual(12, len(tags))

    def test_dynamic_responses_fit_queue_and_static_sizes_match(self):
        server = (ROOT / "Application/gsm/gsm_http_server.c").read_text()
        queue_kib = int(re.search(
            r"#define GSM_HTTP_SERVER_TX_BUFFER_SIZE \((\d+)U \* 1024U\)",
            server)[1])
        reserve = int(re.search(
            r"#define GSM_HTTP_SERVER_HEADER_RESERVE (\d+)U", server)[1])
        capacity = queue_kib * 1024 - 1 - reserve
        http_header = (ROOT / "Application/web-server/http_server.h").read_text()
        dynamic_size = int(re.search(
            r"#define HTTP_TX_BUFFER_SIZE (\d+)", http_header)[1])
        self.assertLessEqual(dynamic_size, capacity)
        for name in ("index", "fw_update", "loading"):
            folder = ROOT / "Application/web-server"
            source = (folder / (name + "_html.c")).read_text()
            array = source.split("};")[0]
            payload = bytes(int(value, 16) for value in re.findall(
                r"0x([0-9A-Fa-f]{2})", array))
            header = (folder / (name + "_html.h")).read_text()
            size = int(re.search(r"_HTML_DATA_SIZE (\d+)U", header)[1])
            self.assertEqual(size, len(payload))
            self.assertTrue(gzip.decompress(payload))
            literal = re.search(r'\.etag = ("(?:\\.|[^"\\])*")', source)[1]
            self.assertEqual('"' + hashlib.sha256(
                bytes([1]) + payload).hexdigest() + '"', json.loads(literal))

    def test_literals_and_public_handlers_keep_their_behavior(self):
        html = """<!-- outside --><pre>keep  spaces\n next</pre>
<style>.item:before {content: "two  spaces";}</style>
<button onclick="handleResponse({PeriodicSend:9})">Go</button>
<script>
const text = "two  spaces > < <!-- inside -->";
const url = "https://host/path";
const pattern = /[&<>"']/g;
function handleResponse(object) {
    const interval = object.PeriodicSend;
    return `row ${interval}: ${`nested ${text}`}`;
}
const result = {text, url, unicode:"\\u011f", regex:pattern.source,
                rendered:handleResponse({PeriodicSend:9})};
</script>"""
        output = module.SafeHTMLMinifier().minify_html(html)
        self.assertNotIn("<!-- outside -->", output)
        self.assertIn("keep  spaces\n next", output)
        self.assertIn('content:"two  spaces"', output)
        checker = """const fs=require('node:fs'),vm=require('node:vm');
const h=JSON.parse(fs.readFileSync(0,'utf8'));
const s=h.match(/<script>([\\s\\S]*?)<\\/script>/)[1];
process.stdout.write(vm.runInNewContext(s+';JSON.stringify(result)'));
"""
        result = subprocess.run(["node", "-e", checker],
                                input=json.dumps(output), encoding="utf-8",
                                capture_output=True, check=True)
        actual = json.loads(result.stdout)
        self.assertEqual("two  spaces > < <!-- inside -->", actual["text"])
        self.assertEqual("https://host/path", actual["url"])
        self.assertEqual("\u011f", actual["unicode"])
        self.assertEqual('[&<>"\']', actual["regex"])
        self.assertEqual("row 9: nested " + actual["text"], actual["rendered"])

    def test_real_rf_monitor_separates_epoch_diagnostics_and_uncertain_retry(self):
        html = (ROOT / "web-page/index.html").read_text(encoding="utf-8")
        function = re.search(r"function buildRfMonitorTable\(d\)\{.*?^\}",
                             html, re.S | re.M).group(0)
        checker = """const fs=require('node:fs'),vm=require('node:vm');
const code=JSON.parse(fs.readFileSync(0,'utf8'));
const c={LNG:'en',t:x=>x,esc:x=>String(x),fmt:x=>String(x),rfLiveFlag:()=>null};
vm.createContext(c);vm.runInContext(code,c);
const render=e=>c.buildRfMonitorTable({Phases:[],Maintenance:{Epoch:[e]}});
const observed=render({Feeder:1,HasReport:true,ReportPhases:6,Received120:1});
if(!observed.includes('201 phases L2, L3, received 120 L1'))throw Error(observed);
if(!observed.includes('success requires APPLIED'))throw Error(observed);
const unknown=render({Feeder:1,Uncertain:true,Missing120:1,RetryUsed:false});
if(!unknown.includes('Outcome uncertain; automatic retry stopped.'))throw Error(unknown);
if(unknown.includes('One extra refresh pending.'))throw Error(unknown);
const exhausted=render({Feeder:1,Missing120:1,RetryUsed:true});
if(!exhausted.includes('contact Bolatex'))throw Error(exhausted);
const degraded=c.buildRfMonitorTable({Phases:[],Maintenance:{EventStoreDegraded:true}});
if(!degraded.includes('event retrieval is unavailable. Contact Bolatex.'))throw Error(degraded);
"""
        subprocess.run(["node", "-e", checker], input=json.dumps(function),
                       encoding="utf-8", capture_output=True, check=True)

    def test_operator_alarm_ack_requires_explicit_offline_confirmation_and_keeps_snapshot(self):
        html = (ROOT / "web-page/index.html").read_text(encoding="utf-8")
        functions = "\n".join(re.search(r"function " + name + r"\([^\n]*?\)\{.*?^\}",
                                       html, re.S | re.M).group(0)
                              for name in ("buildRfMonitorTable", "ackRfAlarm"))
        checker = """const fs=require('node:fs'),vm=require('node:vm');
const code=JSON.parse(fs.readFileSync(0,'utf8'));
const dialogs=[],sent=[];
const phase={Eui64:'00124B0038C9F1BE',TripFailedAlarm:true,TripFailureLatched:true,
 AlarmRevision:7,Online:false,HasData:true,LiveAgeMs:60000,Live:{TripFailed:true},
 AlarmEvent:{Code:105,Boot:3,Uptime:50}};
const c={LNG:'en',s_role:'admin',loading:false,t:x=>x,esc:x=>String(x),fmt:x=>String(x),
 rfLiveFlag:()=>null,pageCache:{rfmon:[{LineId:1,Phases:[phase]}]},pageReadAt:{'rfmon:0':Date.now()},
 confirmAction:(msg,fn)=>dialogs.push({msg,fn}),showLoad:()=>{},hideLoad:()=>{},toast:()=>{},
 userError:x=>String(x),readRfFeeder:async()=>{},api:async(path,options)=>{
 sent.push({path,body:JSON.parse(options.body)});return {success:true};}};
vm.createContext(c);vm.runInContext(code,c);
if(!c.buildRfMonitorTable(c.pageCache.rfmon[0]).includes('ackRfAlarm(0,0)'))throw Error('missing admin action');
c.s_role='user';
if(c.buildRfMonitorTable(c.pageCache.rfmon[0]).includes('ackRfAlarm('))throw Error('user action visible');
c.ackRfAlarm(0,0);if(dialogs.length)throw Error('user confirmation opened');
c.s_role='admin';c.ackRfAlarm(0,0);
if(dialogs.length!==1||sent.length)throw Error('first confirmation bypassed');
if(!dialogs[0].msg.includes('105, boot 3, uptime 50'))throw Error('event identity missing');
phase.AlarmRevision=8;phase.Eui64='00124B0038C9F1AE';
dialogs.shift().fn();
if(dialogs.length!==1||sent.length)throw Error('offline confirmation bypassed');
(async()=>{await dialogs.shift().fn();
if(sent.length!==1||sent[0].path!=='/alarm/rf/ack')throw Error('wrong request');
if(sent[0].body.Revision!==7||sent[0].body.Eui64!=='00124B0038C9F1BE'||!sent[0].body.ConfirmOffline)
 throw Error('confirmed snapshot replaced');})();
"""
        subprocess.run(["node", "-e", checker], input=json.dumps(functions),
                       encoding="utf-8", capture_output=True, check=True)

    def test_initial_rf_group_does_not_claim_known_applied_settings(self):
        html = (ROOT / "web-page/index.html").read_text(encoding="utf-8")
        function = re.search(r"function buildRfGroupStatus\(group\)\{.*?^\}",
                             html, re.S | re.M).group(0)
        checker = """const fs=require('node:fs'),vm=require('node:vm');
const code=JSON.parse(fs.readFileSync(0,'utf8'));
const c={t:x=>x,esc:x=>String(x),buildRfFailure:()=>''};
vm.createContext(c);vm.runInContext(code,c);
const unknown=c.buildRfGroupStatus({State:'idle',BatchState:'idle',HasReport:false});
if(!unknown.includes('rfAppliedUnknown'))throw Error(unknown);
const applied=c.buildRfGroupStatus({State:'applied',MatchesDesired:true});
if(applied.includes('rfAppliedUnknown'))throw Error(applied);
"""
        subprocess.run(["node", "-e", checker], input=json.dumps(function),
                       encoding="utf-8", capture_output=True, check=True)

    def test_invalid_javascript_is_rejected(self):
        with self.assertRaises(RuntimeError):
            module.SafeHTMLMinifier().minify_html(
                "<script>function broken( {</script>")

    def test_converter_is_reproducible_and_keeps_valid_output_on_failure(self):
        temporary_root = ROOT / "test/build"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root) as directory:
            folder = Path(directory)
            source = folder / "fixture.html"
            source.write_text("<script>function hello(){return 'ok';}</script>",
                              encoding="utf-8")
            command = [sys.executable, str(CONVERTER), str(source),
                       "-o", str(folder), "--minify", "--gzip"]
            first = subprocess.run(command, capture_output=True, check=True)
            generated = folder / "fixture_html.c"
            saved = generated.read_bytes()
            stamp = generated.stat().st_mtime_ns
            second = subprocess.run(command, capture_output=True, check=True)
            self.assertIn(b"Unchanged", second.stdout)
            self.assertEqual(saved, generated.read_bytes())
            self.assertEqual(stamp, generated.stat().st_mtime_ns)
            data = bytes(int(x, 16) for x in re.findall(
                rb"0x([0-9A-Fa-f]{2})", saved))
            self.assertIn(b"function hello", gzip.decompress(data))
            header = (folder / "fixture_html.h").read_text(encoding="utf-8")
            self.assertIn(
                f"#define FIXTURE_HTML_DATA_SIZE {len(data)}U", header)
            source.write_text("<script>const = broken;</script>",
                              encoding="utf-8")
            failed = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(0, failed.returncode)
            self.assertEqual(saved, generated.read_bytes())
            self.assertTrue(first.stdout)


if __name__ == "__main__":
    unittest.main()
