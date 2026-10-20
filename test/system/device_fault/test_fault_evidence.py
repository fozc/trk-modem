"""Host regressions for the physical fault run's evidence verdict."""
import unittest
from run_faults import verify_hardfault

GOOD = """PC  : 0x0801caf2
LR  : 0x0801caf3
SCB->CFSR = 0x00010000
SCB->HFSR = 0x40000000
HARDFAULT pc=0x0801CAF2 lr=0x0801CAF3 cfsr=0x00010000 hfsr=0x40000000
"""


class FaultEvidence(unittest.TestCase):
    def test_full_matching_dump_and_persistent_record_pass(self):
        self.assertEqual(0x0801CAF2, verify_hardfault(GOOD)["pc"])

    def test_every_persistent_register_must_match(self):
        for old in ("0801CAF2", "0801CAF3", "cfsr=0x00010000", "hfsr=0x40000000"):
            with self.subTest(old=old), self.assertRaises(AssertionError):
                verify_hardfault(GOOD.replace(old, old[:-1] + "1"))

    def test_dump_without_persistent_record_fails(self):
        with self.assertRaises(AssertionError):
            verify_hardfault(GOOD.split("HARDFAULT")[0])

    def test_persistent_record_without_exception_dump_fails(self):
        with self.assertRaises(AssertionError):
            verify_hardfault("HARDFAULT" + GOOD.split("HARDFAULT")[1])


if __name__ == "__main__":
    unittest.main()
