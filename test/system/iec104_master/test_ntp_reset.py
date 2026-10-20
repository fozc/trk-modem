"""Reject source-only or mismatched physical NTP evidence."""
import unittest
from check_ntp_reset import verify_ntp

GOOD=["AT RX #NTP: 26/12/31,22:00:00-00",
      "NTP: 01/01/27 01:00:00 (tz=3)",
      "27/01/01 01:00:01:123 next device log"]


class NTPEvidence(unittest.TestCase):
    def test_source_offset_and_later_clock_match(self):
        self.assertEqual("2027-01-01T01:00:00",verify_ntp(GOOD,3)["applied"])

    def test_missing_source_or_applied_clock_fails(self):
        for lines in (GOOD[1:],GOOD[:2]):
            with self.subTest(lines=lines), self.assertRaises(AssertionError):
                verify_ntp(lines,3)

    def test_wrong_offset_calendar_or_rejected_sync_fails(self):
        for text in ("\n".join(GOOD).replace("tz=3","tz=0"),
                     "\n".join(GOOD).replace("01:00:00","02:00:00"),
                     "\n".join(GOOD)+"\nRTC rejected the received time"):
            with self.subTest(text=text), self.assertRaises(AssertionError):
                verify_ntp(text.splitlines(),3)


if __name__ == "__main__":
    unittest.main()
