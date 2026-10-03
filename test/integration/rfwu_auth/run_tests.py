"""Test real RFWU v2 parser/crypto and actual PC protocol helpers.
Host doubles replace only hardware, transport, NVRAM, logging and IPC.
Author: Fatih Ozcan
        fatihozcan@gmail.com
"""
import argparse
import ast
import ctypes
import hashlib
import hmac
import os
import platform
from pathlib import Path
import queue
import shutil
import socket
import struct
import subprocess
import threading
import time
import unittest
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "test/build/rfwu_auth"
KEY = bytes(range(16))  # Public fixture; private product key is never loaded.


def load_pc_helpers():
    tree = ast.parse((ROOT / "tools/fw_update_tcp.py").read_text(encoding="utf-8"))
    names = {"_crc32", "build_packet", "_recv_exact", "recv_packet", "parse_key",
             "compute_auth_token", "compute_file_hash", "build_hello_payload",
             "request_hello_payload", "authenticate_socket"}
    classes = {"_OneShot", "TransferWorker", "StatusWorker", "RebootWorker",
               "SocketWorker"}
    selected = [node for node in tree.body
                if isinstance(node, ast.Assign)
                or isinstance(node, ast.FunctionDef) and node.name in names
                or isinstance(node, ast.ClassDef) and node.name in classes]
    namespace = dict(struct=struct, zlib=zlib, hashlib=hashlib, hmac=hmac,
                     threading=threading, queue=queue, socket=socket, time=time)
    exec(compile(ast.Module(body=selected, type_ignores=[]), "PC helpers", "exec"),
         namespace)
    return namespace


PC = load_pc_helpers()
packet = PC["build_packet"]


class ParserSocket:
    """Feed the production parser, expose its real response as socket bytes."""
    def __init__(self):
        self.pending = b""
        self.commands = []

    def sendall(self, data):
        self.commands.append(data[4])
        feed(data)
        self.pending += response()

    def recv(self, size):
        result, self.pending = self.pending[:size], self.pending[size:]
        return result


def feed(data):
    buf = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
    LIB.rfwu_on_receive(buf, len(data))


def response():
    buf = (ctypes.c_uint8 * 64)()
    length = LIB.fixture_response(buf)
    return bytes(buf[:length])


def reply():
    data = response()
    length = struct.unpack_from("<H", data, 6)[0]
    return data[4], data[12:12 + length]


class Protocol(unittest.TestCase):
    def setUp(self):
        LIB.fixture_reset(0xA7E194C3)
        self.firmware = b"F" * 4096

    def value(self, field):
        return LIB.fixture_value(field)

    def challenge(self):
        feed(packet(7))
        cmd, nonce = reply()
        self.assertEqual(cmd, 0x84)
        self.assertEqual(len(nonce), 16)
        return nonce

    def hello(self, nonce, key=KEY, size=None, file_hash=None):
        size = len(self.firmware) if size is None else size
        file_hash = PC["compute_file_hash"](self.firmware) if file_hash is None else file_hash
        tag = PC["compute_auth_token"](key, nonce, size, file_hash)
        return packet(1, struct.pack("<II", size, file_hash) + tag)

    def accepted(self):
        self.assertEqual(reply()[0], 0x81)

    def rejected(self, error=1):
        cmd, data = reply()
        self.assertEqual(cmd, 0x82)
        self.assertEqual(data[0], error)

    def authenticate(self):
        nonce = self.challenge()
        data = self.hello(nonce)
        feed(data)
        self.accepted()
        return data

    def test_commands_require_auth_before_hello(self):
        for cmd in (4, 5, 6):
            feed(packet(cmd))
            self.rejected()
        self.assertEqual(self.value(4), 0)

    def test_data_requires_session(self):
        feed(packet(2, struct.pack("<I", 0) + b"X"))
        self.rejected(6)
        self.assertEqual(self.value(3), 0)

    def test_v1_and_derived_xor_tokens_rejected(self):
        token = (zlib.crc32(struct.pack("<I", 0xA7E194C3)) ^ 0xFFFFFFFF) ^ 4096
        for size in (4096, 8192):
            self.challenge()
            feed(packet(1, struct.pack("<III", size, 123, token ^ 4096 ^ size)))
            self.rejected()
        self.assertEqual(self.value(2), 0)

    def test_hello_without_challenge_rejected(self):
        feed(self.hello(bytes(16)))
        self.rejected()
        self.assertEqual(self.value(2), 0)

    def test_valid_fragmented_hello_accepted(self):
        data = self.hello(self.challenge())
        for byte in data:
            feed(bytes([byte]))
        self.accepted()
        self.assertEqual(self.value(2), 1)

    def test_wrong_key_rejected_and_nonce_consumed(self):
        nonce = self.challenge()
        feed(self.hello(nonce, key=b"X" * 16))
        self.rejected()
        feed(self.hello(nonce))
        self.rejected()
        self.assertEqual(self.value(2), 0)

    def test_same_connection_replay_rejected(self):
        data = self.authenticate()
        feed(data)
        self.rejected()
        self.assertEqual(self.value(2), 1)

    def test_reconnect_replay_rejected(self):
        data = self.authenticate()
        LIB.rfwu_on_disconnect()
        feed(data)
        self.rejected()
        self.assertEqual(self.value(2), 1)
        feed(packet(5))
        self.rejected()
        self.assertEqual(self.value(4), 0)

    def test_old_token_rejected_after_new_challenge(self):
        old_nonce = self.challenge()
        new_nonce = self.challenge()
        self.assertNotEqual(old_nonce, new_nonce)
        feed(self.hello(old_nonce))
        self.rejected()

    def test_size_hash_and_tag_changes_rejected(self):
        for offset in (0, 4, 8, 23):
            data = bytearray(self.hello(self.challenge())[12:-4])
            data[offset] ^= 1
            feed(packet(1, bytes(data)))
            self.rejected()
        self.assertEqual(self.value(2), 0)

    def test_bad_packet_crc_rejected(self):
        data = self.hello(self.challenge())
        feed(data[:-1] + bytes([data[-1] ^ 1]))
        self.rejected(2)
        self.assertEqual(self.value(2), 0)

    def test_rng_error_does_not_issue_nonce_or_touch_flash(self):
        LIB.fixture_rng_failure(True)
        feed(packet(7))
        self.rejected(8)
        feed(self.hello(bytes(16)))
        self.rejected()
        self.assertEqual(self.value(2), 0)
        self.assertEqual(self.value(5), 0)

    def test_nonce_expires_at_60_seconds(self):
        data = self.hello(self.challenge())
        LIB.fixture_advance_tick(60000)
        feed(data)
        self.rejected()

    def test_nonce_timeout_handles_tick_wrap(self):
        LIB.fixture_advance_tick(0xFFFFFF00)
        data = self.hello(self.challenge())
        LIB.fixture_advance_tick(1000)
        feed(data)
        self.accepted()
        nonce = self.challenge()
        LIB.fixture_advance_tick(60000)
        feed(self.hello(nonce))
        self.rejected()

    def test_lock_survives_reconnect_and_expires(self):
        for _ in range(5):
            LIB.rfwu_on_disconnect()
            feed(self.hello(bytes(16)))
            self.rejected()
        LIB.rfwu_on_disconnect()
        feed(packet(7))
        self.rejected()
        LIB.fixture_advance_tick(60000)
        self.authenticate()

    def test_resume_preserves_aligned_progress(self):
        self.authenticate()
        LIB.fixture_progress(4096)
        LIB.rfwu_on_disconnect()
        self.authenticate()
        self.assertEqual(struct.unpack_from("<I", reply()[1])[0], 4096)

    def test_data_duplicate_gap_overflow_and_finish(self):
        self.authenticate()
        data = packet(2, struct.pack("<I", 0) + b"X" * 1024)
        feed(data)
        self.accepted()
        feed(data)
        self.accepted()
        self.assertEqual(self.value(3), 1)
        feed(packet(2, struct.pack("<I", 2048) + b"X"))
        self.rejected(3)
        feed(packet(3, struct.pack("<I", 4096)))
        self.rejected(7)
        self.challenge()
        feed(self.hello(self.challenge(), size=8))
        self.accepted()
        feed(packet(2, struct.pack("<I", 0) + b"X" * 9))
        self.rejected(4)

    def test_authenticated_reboot_and_abort_still_work(self):
        self.authenticate()
        feed(packet(5))
        self.accepted()
        self.assertEqual(self.value(4), 1)
        feed(packet(6))
        self.accepted()
        self.assertEqual(self.value(5), 0)

    def test_pc_socket_authentication_matches_device(self):
        sock = ParserSocket()
        cmd, _ = PC["authenticate_socket"](sock, self.firmware, KEY)
        self.assertEqual(cmd, 0x81)
        self.assertEqual(sock.commands, [7, 1])

    def test_pc_status_worker_authenticates_before_query(self):
        sock = ParserSocket()
        messages = queue.Queue()
        PC["StatusWorker"](sock, messages, self.firmware, KEY, True).run()
        self.assertEqual(sock.commands, [7, 1, 4])
        self.assertTrue(any(item[0] == "status_result" for item in list(messages.queue)))

    def test_pc_transfer_and_force_restart_use_fresh_nonce(self):
        for force in (False, True):
            LIB.fixture_reset(0)
            sock = ParserSocket()
            messages = queue.Queue()
            worker = PC["TransferWorker"](sock, b"F" * 16, KEY, messages,
                                           force_restart=force)
            worker._do_transfer()
            prefix = [7, 1, 6, 7, 1] if force else [7, 1]
            self.assertEqual(sock.commands[:len(prefix)], prefix)
            self.assertEqual(sock.commands[-1], 3)
            self.assertFalse(any(item[0] == "error" for item in list(messages.queue)))

    def test_pc_key_validation_rejects_legacy_and_invalid_keys(self):
        self.assertEqual(PC["parse_key"](KEY.hex()), KEY)
        for value in ("SMAR", "123", "00" * 16, "XX" * 16):
            with self.assertRaises(ValueError):
                PC["parse_key"](value)


class Crypto(unittest.TestCase):
    def digest(self, data):
        output = (ctypes.c_uint8 * 32)()
        LIB.sha256(data, len(data), output)
        return bytes(output)

    def mac(self, key, data):
        output = (ctypes.c_uint8 * 32)()
        LIB.hmac_sha256(output, key, len(key), data, len(data))
        return bytes(output)

    def test_sha256_empty_short_and_block_boundaries(self):
        for size in (0, 3, 55, 56, 63, 64, 65, 127, 128, 4096):
            data = b"A" * size
            self.assertEqual(self.digest(data), hashlib.sha256(data).digest())

    def test_rfc4231_sha256_vectors(self):
        vectors = [
            (b"\x0b" * 20, b"Hi There",
             "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
            (b"Jefe", b"what do ya want for nothing?",
             "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
            (b"\xaa" * 20, b"\xdd" * 50,
             "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"),
            (bytes(range(1, 26)), b"\xcd" * 50,
             "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"),
            (b"\xaa" * 131, b"Test Using Larger Than Block-Size Key - Hash Key First",
             "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54")]
        for key, data, expected in vectors:
            self.assertEqual(self.mac(key, data).hex(), expected)

    def test_hmac_empty_long_and_boundary_inputs(self):
        for key_size in (0, 16, 64, 65, 131):
            for data_size in (0, 56, 64, 65, 4096):
                key, data = b"K" * key_size, b"D" * data_size
                self.assertEqual(self.mac(key, data),
                                 hmac.new(key, data, hashlib.sha256).digest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()
    if args.clean:
        if BUILD.resolve().parent != (ROOT / "test/build").resolve():
            raise ValueError("Unexpected build path")
        if BUILD.exists():
            shutil.rmtree(BUILD)
        raise SystemExit(0)
    BUILD.mkdir(parents=True, exist_ok=True)
    library = BUILD / ("rfwu.dll" if os.name == "nt" else "rfwu.so")
    # ctypes libraries must match Python, even when CI wraps gcc with -m32.
    architecture = platform.machine().lower()
    abi_flags = []
    if architecture in ("amd64", "x86_64", "i386", "i686", "x86"):
        abi_flags = ["-m64" if ctypes.sizeof(ctypes.c_void_p) == 8 else "-m32"]
    subprocess.run([args.cc] + abi_flags + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-shared", "-fPIC", "-I", str(HERE),
                    "-I", str(ROOT / "Application/web-server"),
                    "-I", str(ROOT / "Application/libefw"),
                    "-I", str(ROOT / "Application/libs"),
                    str(ROOT / "Application/web-server/raw_tcp_fw_update.c"),
                    str(ROOT / "Application/libefw/efw_crc.c"),
                    str(ROOT / "Application/libs/sha256.c"),
                    str(ROOT / "Application/libs/hmac_sha256.c"),
                    str(HERE / "fixture.c"), "-o", str(library)], check=True)
    LIB = ctypes.CDLL(str(library))
    for name in ("fixture_reset", "fixture_advance_tick", "fixture_progress"):
        getattr(LIB, name).argtypes = [ctypes.c_uint32]
        getattr(LIB, name).restype = None
    LIB.fixture_rng_failure.argtypes = [ctypes.c_bool]
    LIB.fixture_rng_failure.restype = None
    LIB.fixture_value.argtypes = [ctypes.c_uint32]
    LIB.fixture_value.restype = ctypes.c_uint32
    LIB.fixture_response.argtypes = [ctypes.POINTER(ctypes.c_uint8)]
    LIB.fixture_response.restype = ctypes.c_uint32
    LIB.rfwu_on_receive.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
    LIB.rfwu_on_receive.restype = None
    LIB.rfwu_on_disconnect.argtypes = []
    LIB.rfwu_on_disconnect.restype = None
    LIB.sha256.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
                           ctypes.POINTER(ctypes.c_uint8)]
    LIB.sha256.restype = None
    LIB.hmac_sha256.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_char_p,
                                ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
    LIB.hmac_sha256.restype = None
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(case)
                               for case in (Protocol, Crypto))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
