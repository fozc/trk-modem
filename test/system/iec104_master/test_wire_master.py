"""Real socket boundary tests for the negative/withheld-ACK master."""
import socket
import time
import unittest

import apdu
from wire_master import WireMaster


class WireTests(unittest.TestCase):
    def setUp(self):
        self.master = WireMaster("unused", 0, 1, None)
        self.master.sock, self.peer = socket.socketpair()
        self.peer.settimeout(0.1)

    def tearDown(self):
        self.master.stop()
        self.peer.close()

    def test_partial_and_coalesced_frames_are_not_lost(self):
        raw = bytes.fromhex("680e0000000046010400010000000000")
        self.peer.sendall(raw[:5])
        self.assertFalse(self.master.wait(lambda f: f[1] == "rx", 0.01)[0])
        start = time.monotonic()
        self.peer.sendall(raw[5:] + bytes.fromhex("680443000000"))
        self.assertTrue(self.master.wait(lambda f: f[3].get("u") ==
                                        "TESTFR_ACT", 0.1, t0=start)[0])
        data = self.peer.recv(1024)
        self.assertEqual(bytes.fromhex("680401000200680483000000"), data)

    def test_withheld_ack_sends_nothing_until_explicit_ack(self):
        self.master.auto_ack = False
        self.peer.sendall(bytes.fromhex("680e0000000046010400010000000000"))
        self.assertTrue(self.master.wait(lambda f: f[1] == "rx", 0.1)[0])
        with self.assertRaises(socket.timeout):
            self.peer.recv(1024)
        self.master.acknowledge()
        self.assertEqual(1, apdu.parse_apdu(self.peer.recv(1024))["vr"])

    def test_commands_use_own_sequences_and_preserve_requested_fields(self):
        self.master.send_sn = 32767
        self.master.receive_sn = 12
        self.master.command(105, bytes((0, 0, 0, 99)), ca=2)
        frame = apdu.parse_apdu(self.peer.recv(1024))
        self.assertEqual((32767, 12), (frame["vs"], frame["vr"]))
        self.assertEqual(2, frame["asdu"]["ca"])
        self.assertEqual(99, frame["asdu"]["objects"][0]["qrp"])
        self.assertEqual(0, self.master.send_sn)

    def test_reset_rejection_request_does_not_ack_outstanding_fault(self):
        self.master.receive_sn = 4
        self.master.command(105, bytes((0, 0, 0, 2)), acknowledge_sn=1)
        frame = apdu.parse_apdu(self.peer.recv(1024))
        self.assertEqual(1, frame["vr"])
        self.assertEqual(4, self.master.receive_sn)
        self.assertEqual(2, frame["asdu"]["objects"][0]["qrp"])

    def test_bad_prefix_and_out_of_order_data_fail_closed(self):
        self.peer.sendall(bytes.fromhex("680e0200000046010400010000000000"))
        with self.assertRaises(ValueError):
            self.master.wait(lambda f: False, 0.1)

    def test_peer_close_does_not_pass_a_pending_wait(self):
        self.peer.close()
        self.assertFalse(self.master.wait(lambda f: True, 0.1)[0])
        self.assertFalse(self.master.is_connected)


if __name__ == "__main__":
    unittest.main()
