"""Regression checks for the production embedded-web build pipeline."""
import gzip
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
