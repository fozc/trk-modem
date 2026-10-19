"""Periodic case verdict and configuration restoration regressions."""
import json
import struct
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

import apdu
import cases_periodic


class PeriodicCases(unittest.TestCase):
    def frame(self, ioa=1030, ca=1, oa=7):
        body = bytes((36, 1, 3, oa, ca, 0))
        body += ioa.to_bytes(3, "little") + struct.pack("<f", 12.5)
        body += bytes(1) + bytes((1, 0, 2, 3, 19, 10, 26))
        raw = bytes((0x68, len(body) + 4, 0, 0, 0, 0)) + body
        return (0, "rx", raw, apdu.parse_apdu(raw))

    def test_valid_periodic_measurement_is_counted(self):
        self.assertEqual({1030}, cases_periodic.validate_frame(
            self.frame(), {1030}, set(), 1, 7))

    def test_wrong_address_or_originator_cannot_pass(self):
        for frame in (self.frame(ca=2), self.frame(oa=0), self.frame(ioa=999)):
            with self.assertRaises(AssertionError):
                cases_periodic.validate_frame(frame, {1030}, set(), 1, 7)

    def test_missing_time_or_wrong_type_cannot_pass(self):
        frame = self.frame()
        del frame[3]["asdu"]["objects"][0]["time"]
        with self.assertRaises(AssertionError):
            cases_periodic.validate_frame(frame, {1030}, set(), 1, 7)
        with self.assertRaises(AssertionError):
            cases_periodic.validate_frame(self.frame(), set(), {1030}, 1, 7)

    def test_mutation_requires_opt_in(self):
        ctx = SimpleNamespace(args=SimpleNamespace(periodic_test=False))
        self.assertEqual("SKIP", cases_periodic.p01_periodic(ctx)[0])

    def test_master_invalid_cot_cannot_be_counted_as_success(self):
        frame = (0, "unexpected", b"", {"unexpected": "Umc.INVALID_COT"})
        with self.assertRaises(AssertionError):
            cases_periodic.validate_frame(frame, {1030}, {1040}, 1, 7)

    def test_failed_connection_restores_full_configuration(self):
        saved = {"PeriodicSend": 900, "CommonAddr": 1, "Hatlar": {"inUse": [True]}}
        web = Mock()
        web.request.side_effect = [(200, json.dumps(saved)), (200, "OK"),
                                   (200, "OK"), (200, json.dumps(saved))]
        with tempfile.TemporaryDirectory() as folder:
            ctx = SimpleNamespace(args=SimpleNamespace(periodic_test=True),
                console=object(), expected={"group1": {1030}, "group2": {1040}},
                web=web, outdir=Path(folder), master=Mock(),
                oracle={"lines": [{"in_use": True, "ioa": {
                    "anlik": [1030], "enerji": [1040], "yuk": [], "rf": []}}]})
            ctx.master.connect.return_value = False
            with patch.object(cases_periodic, "wait_listener"):
                self.assertEqual("FAIL", cases_periodic.p01_periodic(ctx)[0])
        self.assertEqual(saved, web.request.call_args_list[-2].args[1])
        self.assertEqual(5, web.request.call_args_list[1].args[1]["PeriodicSend"])


if __name__ == "__main__":
    unittest.main()
