"""Negative verdicts for the in-flight overwrite acceptance case."""
import struct
import unittest
from checks import TraceChecks
from cases_wrap import check_consumes


def trace(targets):
    records = [{"event":"rx", "cmd":0x48, "t_ms":100+i,
                "data_hex":struct.pack("<I", n).hex()}
               for i,n in enumerate(targets)]
    return TraceChecks(records + [{"event":"tx", "cmd":0x44,
                                  "kind":"reply", "data_hex":"00"}])


class WrapVerdict(unittest.TestCase):
    def test_retained_suffix_drains_in_order(self):
        self.assertEqual([27, 31, 122],
                         check_consumes(trace([27,31,122]),99,23,122))

    def test_overwritten_future_or_backwards_cursor_fails(self):
        for targets in ([4,122], [126], [27,26,122], [27,31], []):
            with self.subTest(targets=targets), self.assertRaises(AssertionError):
                check_consumes(trace(targets),99,23,122)

    def test_old_completion_cannot_pass_current_window(self):
        with self.assertRaises(AssertionError):
            check_consumes(trace([122]),200,23,122)


if __name__ == "__main__":
    unittest.main()
