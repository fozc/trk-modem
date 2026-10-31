#!/usr/bin/env python3
"""Build the real C stack/modem fixture and test it over loopback TCP.

Python supplies only socket I/O and test orchestration. Protocol, product
mapping, command handling and replay run in the compiled production C files.
The SCADA peer is c104 (lib60870); raw-peer cases cover invalid traffic.
"""

from __future__ import annotations

import ctypes
import importlib.util
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test/system/iec104_master"))
import apdu  # noqa: E402 - reuse the existing independent wire decoder

BUILD = ROOT / "test/build/iec104-stack"
SOURCES = [ROOT / "Application/iec104_stack" / name for name in
           ("iec104_asdu.c", "iec104_core.c", "iec104_server.c")]
SOURCES += [ROOT / "Application/iec104_modem/iec104_modem.c"]
SOURCES += [ROOT / "Application/libiec104" / name for name in
            ("iec104_config.c", "iec104_util.c")]
SOURCES += [ROOT / "test/support_sources/iec104_modem_fixture.c"]


def build_library():
    BUILD.mkdir(parents=True, exist_ok=True)
    library = BUILD / ("iec104_stack.dll" if os.name == "nt" else
                       "libiec104_stack.so")
    includes = [ROOT / "test/support", ROOT / "Application"]
    includes += sorted(p for p in (ROOT / "Application").rglob("*")
                       if p.is_dir())
    includes += [ROOT / "contiki-kernel"]
    includes += sorted(p for p in (ROOT / "contiki-kernel").rglob("*")
                       if p.is_dir())
    command = [os.environ.get("CC", "gcc"), "-shared", "-std=c11",
               "-pedantic", "-Wall", "-Wextra", "-Werror", "-Wshadow",
               "-Wconversion", "-Wdouble-promotion", "-Wformat=2", "-O2",
               "-DUNIT_TEST", "-DNO_CONSOLE_LOG", "-DNO_SHELL_LOG"]
    if os.name != "nt":
        command.append("-fPIC")
    command += [f"-I{path}" for path in includes]
    command += [str(path) for path in SOURCES] + ["-o", str(library)]
    subprocess.run(command, check=True, cwd=ROOT)
    return library


class Device:
    """Single owner executes C entry points; socket uses whole-frame writes."""

    def __init__(self, library, window=12):
        self.lib = ctypes.CDLL(str(library))
        byte_pointer = ctypes.POINTER(ctypes.c_uint8)
        self.lib.iec104_fixture_start.argtypes = [ctypes.c_uint8]
        self.lib.iec104_fixture_start.restype = ctypes.c_bool
        self.lib.iec104_fixture_configure_sbo.argtypes = [ctypes.c_uint8, ctypes.c_uint16]
        self.lib.iec104_fixture_configure_sbo.restype = ctypes.c_bool
        self.lib.iec104_fixture_set_period_ms.argtypes = [ctypes.c_uint32]
        self.lib.iec104_fixture_set_period_ms.restype = None
        self.lib.iec104_fixture_feed.argtypes = [byte_pointer, ctypes.c_size_t,
                                               ctypes.c_uint32]
        self.lib.iec104_fixture_feed.restype = ctypes.c_size_t
        self.lib.iec104_fixture_pop.argtypes = [byte_pointer, ctypes.c_size_t]
        self.lib.iec104_fixture_pop.restype = ctypes.c_size_t
        self.lib.iec104_fixture_poll.argtypes = [ctypes.c_uint32]
        self.lib.iec104_fixture_event.argtypes = [ctypes.c_uint8]
        for name in ("poll", "close", "event", "notify_event"):
            getattr(self.lib, "iec104_fixture_" + name).restype = None
        for name in ("marked", "clock_count", "reset_count", "state"):
            function = getattr(self.lib, "iec104_fixture_" + name)
            function.restype = ctypes.c_uint32
            function.argtypes = []
        if not self.lib.iec104_fixture_start(window):
            raise RuntimeError("C modem initialization failed")
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(0.05)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.started = time.monotonic()
        self.offset = 0
        self.error = None
        self.sent = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def invoke(self, name, *args):
        with self.lock:
            return getattr(self.lib, "iec104_fixture_" + name)(*args)

    def now(self):
        return (int((time.monotonic() - self.started) * 1000) + self.offset
                ) & 0xFFFFFFFF

    def run(self):
        connection = None
        try:
            while not self.stop.is_set() and connection is None:
                try:
                    connection, _ = self.listener.accept()
                except socket.timeout:
                    continue
            if connection is None:
                return
            connection.settimeout(0.005)
            buffer = (ctypes.c_uint8 * 255)()
            while not self.stop.is_set():
                try:
                    data = connection.recv(4096)
                    if not data:
                        break
                    raw = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
                    consumed = self.invoke("feed", raw, len(data), self.now())
                    if consumed != len(data) and self.invoke("state") != 0:
                        raise AssertionError("Unconsumed TCP suffix")
                except socket.timeout:
                    pass
                self.invoke("poll", self.now())
                while True:
                    length = self.invoke("pop", buffer, len(buffer))
                    if not length:
                        break
                    frame = bytes(buffer[:length])
                    self.sent.append(frame)
                    connection.sendall(frame)
                if self.invoke("state") == 0:
                    break
        except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
            pass
        except Exception as error:
            self.error = error
        finally:
            self.invoke("close")
            if connection is not None:
                connection.close()

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        self.listener.close()
        if self.thread.is_alive():
            raise AssertionError("C transport thread did not stop")
        if self.error:
            raise self.error


def wait_until(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError("Condition timed out")


class StackIntegration(unittest.TestCase):
    library = None

    def setUp(self):
        self.device = Device(self.library)
        self.client = None
        self.peer = None

    def tearDown(self):
        if self.client:
            self.client.stop()
        if self.peer:
            self.peer.close()
        self.device.close()

    def connect_c104(self):
        import c104
        self.client = c104.Client(tick_rate_ms=50, command_timeout_ms=3000)
        self.connection = self.client.add_connection(
            "127.0.0.1", self.device.port, init=c104.Init.NONE)
        self.connection.originator_address = 0x3C
        self.connection.protocol_parameters.receive_window_size = 1
        self.connection.protocol_parameters.confirm_interval = 1
        self.connection.add_station(1)
        self.client.start()
        # Drive TCP and STARTDT explicitly. An open-but-muted connection is
        # not a completed transfer startup barrier for the test peer.
        self.connection.connect()
        try:
            wait_until(lambda: self.connection.is_connected, timeout=20)
            if self.connection.is_muted:
                self.assertTrue(self.connection.unmute(), "STARTDT request rejected")
            wait_until(lambda: not self.connection.is_muted, timeout=20)
        except AssertionError as error:
            raise AssertionError(
                f"c104 startup: {self.connection.state}; "
                f"C state={self.device.invoke('state')}; "
                f"frames={[f.hex() for f in self.device.sent]}; "
                f"transport={self.device.error}") from error
        wait_until(lambda: any(f[6:7] == bytes([70]) for f in self.device.sent))

    def raw_connect(self):
        self.peer = socket.create_connection(("127.0.0.1", self.device.port))
        self.peer.settimeout(2)
        self.rx = bytearray()

    def raw_receive(self):
        while len(self.rx) < 2 or len(self.rx) < self.rx[1] + 2:
            data = self.peer.recv(4096)
            if not data:
                raise EOFError("C server closed")
            self.rx.extend(data)
        length = self.rx[1] + 2
        frame = bytes(self.rx[:length])
        del self.rx[:length]
        return frame

    def raw_start(self):
        self.raw_connect()
        self.peer.sendall(bytes.fromhex("680407000000"))
        self.assertEqual(self.raw_receive(), bytes.fromhex("68040b000000"))
        frame = self.raw_receive()
        self.assertEqual(frame[6], 70)
        self.ns = 0
        self.nr = 1
        self.ack()

    def ack(self):
        self.peer.sendall(b"\x68\x04\x01\x00" +
                          struct.pack("<H", self.nr << 1))

    def send_command(self, type_id, body, cause=6, ca=1):
        asdu = bytes([type_id, 1, cause, 0x3C]) + struct.pack("<H", ca) + body
        self.peer.sendall(bytes([0x68, len(asdu) + 4]) +
                          struct.pack("<HH", self.ns << 1, self.nr << 1) + asdu)
        self.ns = (self.ns + 1) & 32767

    def next_asdu(self, predicate):
        for _ in range(128):
            frame = self.raw_receive()
            if frame[2] & 1:
                continue
            self.nr = ((int.from_bytes(frame[2:4], "little") >> 1) + 1) & 32767
            self.ack()
            if predicate(frame):
                return frame
        self.fail("Expected ASDU missing")

    def test_c104_all_product_interrogation_groups(self):
        import c104
        self.connect_c104()
        expected = {20: 106, 21: 21, 22: 85, 23: 4, 24: 4}
        live = lambda bases: {base + feeder * 10 + phase for base in bases
                              for feeder in range(7) for phase in range(3)}
        expected_ioas = {
            20: live((100, 200, 300, 400, 500)) | {10000},
            21: live((100,)), 22: live((200, 300, 400, 500)) | {10000},
            23: set(range(2000, 2004)), 24: set(range(5000, 5004))}
        for qoi, count in expected.items():
            start = len(self.device.sent)
            self.assertTrue(self.connection.interrogation(
                1, qualifier=c104.Qoi(qoi), wait_for_response=True))
            wait_until(lambda: any(len(f) >= 16 and f[6] == 100 and
                       f[8] == 10 and f[15] == qoi
                       for f in self.device.sent[start:]))
            data = [f for f in self.device.sent[start:]
                    if len(f) >= 12 and f[8] == qoi]
            self.assertEqual(sum(f[7] & 127 for f in data), count)
            self.assertTrue(all(f[9] == 0x3C for f in data))
            decoded = [apdu.parse_apdu(f)["asdu"] for f in data]
            ioas = [obj["ioa"] for a in decoded for obj in a["objects"]]
            self.assertEqual(len(ioas), len(set(ioas)), "Duplicate IOA")
            self.assertEqual(set(ioas), expected_ioas[qoi])
            if qoi == 21:
                self.assertEqual(data[0][15:19], struct.pack("<f", -2.5))
                self.assertEqual(data[0][20:27],
                                 bytes.fromhex("6be63917f90a1a"))

    def test_c104_clock_sync_reaches_rtc_service(self):
        self.connect_c104()
        self.assertTrue(self.connection.clock_sync(1, wait_for_response=True))
        self.assertEqual(self.device.invoke("clock_count"), 1)

    def test_c104_receives_real_value_and_quality(self):
        import c104
        self.connect_c104()
        station = self.connection.get_station(1)
        point = station.add_point(io_address=100, type=c104.Type.M_ME_TF_1)
        self.assertTrue(self.connection.interrogation(
            1, qualifier=c104.Qoi.GROUP_1, wait_for_response=True))
        wait_until(lambda: point.value == -2.5)
        self.assertEqual(point.value, -2.5)
        self.assertEqual(int(point.quality), 0)

    def test_c104_acknowledges_two_frame_durable_fault(self):
        import c104
        self.connect_c104()
        self.device.invoke("event", 0)
        self.assertTrue(self.connection.interrogation(
            1, qualifier=c104.Qoi.GROUP_3, wait_for_response=True))
        wait_until(lambda: self.device.invoke("marked") == 1)
        event_frames = [f for f in self.device.sent
                        if len(f) > 12 and f[8] == 3 and f[6] in (36, 30)]
        self.assertTrue(any(f[6] == 36 and f[7] == 2 for f in event_frames))
        self.assertTrue(any(f[6] == 30 and f[7] == 2 for f in event_frames))

    def test_c104_k_one_queries_and_durable_delivery(self):
        import c104
        self.device.close()
        self.device = Device(self.library, window=1)
        self.connect_c104()
        for qoi in (21, 22, 23, 24):
            self.assertTrue(self.connection.interrogation(
                1, qualifier=c104.Qoi(qoi), wait_for_response=True))
        self.device.invoke("event", 0)
        self.device.invoke("notify_event")
        wait_until(lambda: self.device.invoke("marked") == 1)
        sequences = [int.from_bytes(f[2:4], "little") >> 1
                     for f in self.device.sent if not f[2] & 1]
        self.assertEqual(sequences, list(range(len(sequences))))

    def test_raw_k_one_disconnect_before_final_ack_keeps_journal(self):
        self.device.close()
        self.device = Device(self.library, window=1)
        self.raw_start()
        self.next_asdu(lambda f: f[6] == 1 and f[8] == 3)
        self.device.invoke("event", 0)
        self.device.invoke("notify_event")
        first = self.raw_receive()
        self.assertEqual((first[6], first[7], first[8]), (36, 2, 3))
        self.assertEqual(self.device.invoke("marked"), 0)
        self.nr = (int.from_bytes(first[2:4], "little") >> 1) + 1
        self.ack()
        second = self.raw_receive()
        self.assertEqual((second[6], second[7], second[8]), (30, 2, 3))
        self.assertEqual(self.device.invoke("marked"), 0)
        self.peer.close()
        self.peer = None
        wait_until(lambda: self.device.invoke("state") == 0)
        self.assertEqual(self.device.invoke("marked"), 0)

    def test_raw_clock_invalid_and_test_bit_do_not_change_rtc(self):
        self.raw_start()
        body = b"\0\0\0" + bytes.fromhex("6be6b917f90a1a")
        self.send_command(103, body)
        response = self.next_asdu(lambda f: f[6] == 103)
        self.assertEqual(response[8], 0x47)
        self.assertEqual(self.device.invoke("clock_count"), 0)
        self.send_command(105, b"\0\0\0\1", cause=0x86)
        response = self.next_asdu(lambda f: f[6] == 105)
        self.assertEqual(response[8], 0xC7)
        self.assertEqual(self.device.invoke("reset_count"), 0)

    def test_raw_unknown_type_cause_ca_ioa_and_reset(self):
        self.raw_start()
        cases = [(45, b"\1\0\0\1", 6, 1, 47),
                 (100, b"\0\0\0\x14", 3, 1, 45),
                 (100, b"\0\0\0\x14", 6, 2, 46),
                 (100, b"\1\0\0\x14", 6, 1, 47),
                 (105, b"\0\0\0\2", 6, 1, 7)]
        for type_id, body, cause, ca, expected in cases:
            self.send_command(type_id, body, cause, ca)
            response = self.next_asdu(lambda f: f[6] == type_id)
            self.assertEqual(response[8], expected | 0x40)
            self.assertEqual(response[9], 0x3C)
        self.assertEqual(self.device.invoke("reset_count"), 0)
        self.send_command(105, bytes.fromhex("11270001"))
        response = self.next_asdu(lambda f: f[6] == 105)
        self.assertEqual(response[8], 7)
        wait_until(lambda: self.device.invoke("reset_count") == 1)

    def test_raw_bytewise_start_and_coalesced_commands(self):
        self.raw_connect()
        for byte in bytes.fromhex("680407000000"):
            self.peer.sendall(bytes([byte]))
        self.assertEqual(self.raw_receive()[2], 0x0B)
        self.assertEqual(self.raw_receive()[6], 70)
        # Two unsupported commands coalesced in one send, followed by ACK.
        frames = []
        for ns in (0, 1):
            frames.append(bytes.fromhex("680e") + struct.pack("<HH", ns << 1, 2)
                          + bytes.fromhex("2d01063c010001000001"))
        self.peer.sendall(b"".join(frames))
        self.nr = 1
        responses = [self.next_asdu(lambda f: f[6] == 45) for _ in range(2)]
        self.assertEqual([f[8] for f in responses], [0x6F, 0x6F])

    def test_raw_sbo_reset_select_execute_and_direct_bypass_rejected(self):
        self.assertTrue(self.device.invoke("configure_sbo", 1, 2))
        self.raw_start()
        body = bytes.fromhex("112700")
        self.send_command(105, body + b"\1")
        self.assertEqual(self.next_asdu(lambda f: f[6] == 105)[8], 0x47)
        self.send_command(45, body + b"\1")
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 0x47)
        self.assertEqual(self.device.invoke("reset_count"), 0)
        self.send_command(45, body + b"\x81")
        select = self.next_asdu(lambda f: f[6] == 45)
        self.assertEqual((select[8], select[15]), (7, 0x81))
        self.assertEqual(self.device.invoke("reset_count"), 0)
        self.send_command(45, body + b"\1")
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 7)
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 10)
        wait_until(lambda: self.device.invoke("reset_count") == 1)
        self.send_command(45, body + b"\1")
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 0x47)
        self.assertEqual(self.device.invoke("reset_count"), 1)

    def test_c104_reset_uses_standard_select_and_execute_mode(self):
        import c104
        self.assertTrue(self.device.invoke("configure_sbo", 1, 3))
        self.connect_c104()
        station = self.connection.get_station(1)
        point = station.add_point(io_address=10001, type=c104.Type.C_SC_NA_1,
                                  command_mode=c104.CommandMode.SELECT_AND_EXECUTE)
        self.assertIsNotNone(point)
        point.value = True
        self.assertTrue(point.transmit(cause=c104.Cot.ACTIVATION),
                        f"SBO replies={[f.hex() for f in self.device.sent if len(f) == 16 and f[6] == 45]}; "
                        f"reset_count={self.device.invoke('reset_count')}; state={self.connection.state}")
        wait_until(lambda: self.device.invoke("reset_count") == 1)
        replies = [f for f in self.device.sent if len(f) == 16 and f[6] == 45]
        self.assertEqual([f[15] for f in replies], [0x81, 0x01, 0x01])
        self.assertEqual([f[8] for f in replies], [7, 7, 10])

    def test_raw_sbo_cancel_and_timeout_do_not_reset(self):
        self.assertTrue(self.device.invoke("configure_sbo", 1, 1))
        self.raw_start()
        body = bytes.fromhex("112700")
        self.send_command(45, body + b"\x81")
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 7)
        self.send_command(45, body + b"\x81", cause=8)
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 9)
        self.send_command(45, body + b"\1")
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 0x47)
        self.send_command(45, body + b"\x81")
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 7)
        # Advance the same monotonic test clock, then feed an actual execute APDU.
        self.device.offset += 1100
        self.send_command(45, body + b"\1")
        self.assertEqual(self.next_asdu(lambda f: f[6] == 45)[8], 0x47)
        self.assertEqual(self.device.invoke("reset_count"), 0)

    def test_raw_periodic_uses_cot_one_and_only_the_application_profile(self):
        self.raw_start()
        # Drain the initial status/battery publication before enabling periodic data.
        self.next_asdu(lambda f: f[6] == 1 and f[8] == 3)
        self.device.invoke("set_period_ms", 1000)
        self.device.offset += 1100
        ioas = []
        while len(ioas) < 84:
            frame = self.next_asdu(lambda f: f[8] == 1)
            self.assertIn(frame[6], (30, 36))
            ioas.extend(obj["ioa"] for obj in apdu.parse_apdu(frame)["asdu"]["objects"])
        expected = {base + feeder * 10 + phase for base in (100, 200, 300, 400)
                    for feeder in range(7) for phase in range(3)}
        self.assertEqual(len(ioas), len(set(ioas)))
        self.assertEqual(set(ioas), expected)

    def test_raw_future_ack_closes(self):
        self.raw_connect()
        self.peer.sendall(bytes.fromhex("680401000200"))
        wait_until(lambda: self.device.invoke("state") == 0)
        self.assertEqual(self.peer.recv(1), b"")

    def test_raw_bad_apci_closes(self):
        self.raw_connect()
        self.peer.sendall(bytes.fromhex("680407010000"))
        wait_until(lambda: self.device.invoke("state") == 0)
        self.assertEqual(self.peer.recv(1), b"")

    def test_raw_stop_start_retains_tcp_sequence(self):
        self.raw_start()
        # Drain the 42 status objects and initial battery while ACKing.
        self.next_asdu(lambda f: f[6] == 1 and f[8] == 3)
        self.peer.sendall(bytes.fromhex("680413000000"))
        wait_until(lambda: self.device.invoke("state") == 1)
        self.assertEqual(self.raw_receive()[2], 0x23)
        previous = self.nr
        self.peer.sendall(bytes.fromhex("680407000000"))
        self.assertEqual(self.raw_receive()[2], 0x0B)
        self.send_command(45, b"\1\0\0\1")
        response = self.next_asdu(lambda f: f[6] == 45)
        self.assertEqual(int.from_bytes(response[2:4], "little") >> 1, previous)


def main():
    if importlib.util.find_spec("c104") is None:
        environment = ROOT / "test/system/iec104_master/.venv"
        candidates = (environment / "Scripts/python.exe", environment / "bin/python")
        for interpreter in candidates:
            if interpreter.exists() and interpreter.resolve() != Path(sys.executable).resolve():
                return subprocess.call([str(interpreter), str(Path(__file__).resolve())])
        raise SystemExit("c104 required: install test/integration/iec104_stack/requirements.txt")
    if not shutil.which(os.environ.get("CC", "gcc")):
        raise SystemExit("Native C compiler missing (CC or gcc)")
    StackIntegration.library = build_library()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(StackIntegration)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
