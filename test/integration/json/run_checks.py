"""Independent semantic, binary32, mutation and timing checks of json.c.

No firmware transport is mocked here: the host ABI bridge calls public
production APIs, including the real resumable reader/writer paths.
"""

from __future__ import annotations

import argparse
import ctypes as ct
from decimal import Decimal, localcontext
import json
import math
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = HERE / "build"
FLAGS = ["-std=c11", "-pedantic", "-Wall", "-Wextra", "-Werror",
         "-Wshadow", "-Wconversion", "-Wdouble-promotion", "-Wformat=2"]


def configuration_checks(cc: str):
    """Compile real C/C++ callers and catch unsupported literal arguments."""
    BUILD.mkdir(parents=True, exist_ok=True)
    compiler = Path(cc)
    cxx = str(compiler.with_name(compiler.name.replace("gcc", "g++")))
    include = ["-I", str(ROOT / "Application/json")]
    source = ROOT / "Application/json/json.c"
    client = HERE / "configuration.c"
    for enabled in [0, 1]:
        options = [f"-DJSON_ENABLE_FLOAT={enabled}", *include]
        target = BUILD / f"configuration_{enabled}.o"
        subprocess.run([cc, *FLAGS, "-O2", *options, "-c", str(source),
                        "-o", str(target)], check=True)
        for language in ["c", "c++"]:
            executable = BUILD / f"configuration_{enabled}_{language.replace('+', 'p')}.exe"
            flags = FLAGS if language == "c" else ["-std=c++20", *FLAGS[1:]]
            command = [cc if language == "c" else cxx, *flags, *options,
                       "-x", language, str(client), "-x", "none", str(target),
                       "-o", str(executable)]
            # Float-disabled builds must also link without libm on Unix.
            if enabled and sys.platform != "win32":
                command += ["-lm"]
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True)

    probes = [
        ('void f(const char *p) { json_span_t s = JSON_SPAN_LITERAL(p); '
         '(void)s; }', []),
        ('void f(void) { char a[4] = "abc"; '
         'json_span_t s = JSON_SPAN_LITERAL(a); (void)s; }', []),
        ('const json_span_t s = JSON_SPAN_INIT((const char *)0);', []),
        ('void f(json_writer_t *w) { (void)json_write_float(w, 1.0F); }',
         ["-DJSON_ENABLE_FLOAT=0"]),
        ('void f(json_writer_t *w) { '
         '(void)json_write_float_fixed(w, 1.0F, 1U); }',
         ["-DJSON_ENABLE_FLOAT=0"]),
        ('void f(json_token_t *t, float *v) { (void)json_token_float(t, v); }',
         ["-DJSON_ENABLE_FLOAT=0"]),
        ('void f(void) {}', ["-DJSON_ENABLE_FLOAT=2"]),
    ]
    for index, (body, defines) in enumerate(probes):
        result = subprocess.run(
            [cc, *FLAGS, *include, *defines, "-x", "c", "-c", "-",
             "-o", str(BUILD / f"invalid_configuration_{index}.o")],
            input='#include "json.h"\n' + body + "\n", text=True,
            capture_output=True)
        assert result.returncode != 0, (index, body)
    print("PASS configuration matrix: C/C++ x float on/off; 7 invalid clients")


class Options(ct.Structure):
    _fields_ = [("input_chunk", ct.c_size_t), ("output_chunk", ct.c_size_t)]


def build(cc: str, sanitize: bool) -> ct.CDLL:
    BUILD.mkdir(parents=True, exist_ok=True)
    library = BUILD / ("json_checks.dll" if sys.platform == "win32" else
                       "json_checks.so")
    command = [cc, *FLAGS, "-O2", "-g", "-shared", "-I", str(ROOT / "Application/json"),
               str(ROOT / "Application/json/json.c"), str(HERE / "json_bridge.c"),
               "-o", str(library)]
    if sys.platform != "win32":
        command += ["-fPIC", "-lm"]
    if sanitize:
        command += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
    subprocess.run(command, check=True)
    api = ct.CDLL(str(library))
    api.json_test_transcode.argtypes = [ct.c_char_p, ct.c_size_t, ct.c_void_p,
                                      ct.c_size_t, ct.POINTER(Options)]
    api.json_test_transcode.restype = ct.c_int32
    api.json_test_float_read.argtypes = [ct.c_char_p, ct.c_size_t,
                                       ct.POINTER(ct.c_uint32)]
    api.json_test_float_read.restype = ct.c_int
    api.json_test_float_write.argtypes = [ct.c_uint32, ct.c_void_p, ct.c_size_t]
    api.json_test_float_write.restype = ct.c_int32
    api.json_test_validate.argtypes = [ct.c_char_p, ct.c_size_t, ct.c_size_t,
                                     ct.POINTER(ct.c_size_t)]
    api.json_test_validate.restype = ct.c_int
    api.json_test_skip.argtypes = api.json_test_validate.argtypes
    api.json_test_skip.restype = ct.c_int
    api.json_test_float_fixed.argtypes = [ct.c_uint32, ct.c_uint8,
                                         ct.c_void_p, ct.c_size_t]
    api.json_test_float_fixed.restype = ct.c_int32
    return api


def transcode(api, text: bytes, input_chunk=8192, output_chunk=256) -> bytes | None:
    output = ct.create_string_buffer(32768)
    options = Options(input_chunk, output_chunk)
    length = api.json_test_transcode(text, len(text), output, len(output),
                                    ct.byref(options))
    return None if length < 0 else output.raw[:length]


def strict_load(text: bytes):
    def reject(value):
        raise ValueError(value)
    value = json.loads(text.decode("utf-8"), parse_constant=reject)
    # Python accepts unpaired escaped surrogates. The embedded API explicitly
    # requires Unicode scalar values; reject those in the independent oracle.
    json.dumps(value, ensure_ascii=False).encode("utf-8")
    return value


def random_value(rng, depth=0):
    atoms = [None, True, False, rng.randrange(-2147483648, 4294967296),
             rng.uniform(-1e20, 1e20), "", "APN", "a\0b\n\t\\\"",
             "İstanbul € 😀 𝄞", "\x7f\x80\u07ff\u0800\ud7ff\ue000\uffff"]
    if depth >= 4 or rng.randrange(3) == 0:
        return rng.choice(atoms)
    if rng.randrange(2) == 0:
        return [random_value(rng, depth + 1) for _ in range(rng.randrange(6))]
    return {rng.choice(["APN", "x", "", "Ğş😀", "\0", '"\\']) + str(index):
            random_value(rng, depth + 1) for index in range(rng.randrange(6))}


def semantic_checks(api, rng, count):
    corpus = [b'{"x":1,"x":2}', b'0', b'-0', b'1e999',
              b'{"skip":[[],{},[true,false,null]],"APN":"internet"}']
    for _ in range(count):
        text = json.dumps(random_value(rng), ensure_ascii=bool(rng.randrange(2)),
                          separators=rng.choice([(",", ":"), (", ", ": ")]))
        corpus.append(text.encode("utf-8"))
    for text in corpus:
        result = transcode(api, text, rng.randrange(1, 65), rng.randrange(1, 65))
        assert result is not None, repr(text)
        assert strict_load(result) == strict_load(text), (text, result)
    print(f"PASS semantic round-trip: {len(corpus)} documents")
    return corpus


def mutation_checks(api, rng, corpus, count):
    # Compare acceptance and stable absolute error locations across input
    # fragmentation. Python's decoder is independent of the C state machine.
    accepted = 0
    for index in range(count):
        text = bytearray(rng.choice(corpus))
        position = rng.randrange(len(text) + 1)
        operation = index % 3
        if operation == 0:
            text[position:position] = bytes([rng.randrange(256)])
        elif operation == 1:
            del text[position:position + 1]
        elif position < len(text):
            text[position] = rng.randrange(256)
        text = bytes(text)
        try:
            strict_load(text)
            expected = True
        except (ValueError, UnicodeError, RecursionError):
            expected = False
        accepted += int(expected)
        offsets = []
        for chunk in [1, rng.randrange(2, 33), max(1, len(text))]:
            error_offset = ct.c_size_t()
            error = api.json_test_validate(text, len(text), chunk,
                                           ct.byref(error_offset))
            assert (error == 0) == expected, (index, text, error, expected)
            skip_offset = ct.c_size_t()
            skip_error = api.json_test_skip(text, len(text), chunk,
                                             ct.byref(skip_offset))
            assert skip_error == error and skip_offset.value == error_offset.value, (
                text, chunk, error, skip_error, error_offset.value, skip_offset.value)
            offsets.append(error_offset.value)
        assert len(set(offsets)) == 1, (text, offsets)
    print(f"PASS differential byte mutations: {count} x 3 fragmentations "
          f"({accepted} valid, {count - accepted} invalid)")


def float_checks(api, rng, count):
    # CRT strtof supplies a separately implemented decimal -> binary32 oracle.
    # It is host-test-only; production does not link strtof or touch errno.
    crt = ct.CDLL("ucrtbase" if sys.platform == "win32" else None)
    crt.strtof.argtypes = [ct.c_char_p, ct.c_void_p]
    crt.strtof.restype = ct.c_float
    output = ct.create_string_buffer(48)
    finite_count = 0
    for _ in range(count):
        bits = rng.getrandbits(32)
        if bits & 0x7F800000 == 0x7F800000:
            continue
        finite_count += 1
        value = struct.unpack("<f", struct.pack("<I", bits))[0]
        length = api.json_test_float_write(bits, output, len(output))
        assert length > 0
        text = output.raw[:length]
        actual = struct.unpack("<I", struct.pack("<f", crt.strtof(text, None)))[0]
        assert bits == actual, (hex(bits), text, hex(actual))
        # Nine-digit numeric output must match independently formatted Python.
        assert float(text) == float(format(value, ".9g")), (bits, text, value)
        decoded = ct.c_uint32(0xDEADBEEF)
        assert api.json_test_float_read(text, len(text), ct.byref(decoded)) == 0
        assert decoded.value == bits, (bits, text, decoded.value)

        digits = "".join(str(rng.randrange(10)) for _ in range(rng.randrange(1, 129)))
        text = (rng.choice(["", "-"]) + str(rng.randrange(1, 10)) + "." +
                digits[:127] + "e" + str(rng.randrange(-55, 45))).encode()
        reference = crt.strtof(text, None)
        error = api.json_test_float_read(text, len(text), ct.byref(decoded))
        if not math.isfinite(reference) or reference == 0.0:
            assert error == 9, (text, error, reference)  # JSON_ERROR_RANGE
        else:
            assert error == 0, (text, error, reference)
            assert decoded.value == struct.unpack("<I", struct.pack("<f", reference))[0], text
    print(f"PASS independent binary32 output/input reference: {count} draws, "
          f"{finite_count} finite patterns + decimal cases; nonfinite tested in Unity")


def clinger_checks(api, count):
    rng = random.Random(0x239378)
    crt = ct.CDLL("ucrtbase" if sys.platform == "win32" else None)
    crt.strtof.argtypes = [ct.c_char_p, ct.c_void_p]
    crt.strtof.restype = ct.c_float
    for _ in range(count):
        mantissa = rng.randrange(16777217)
        exponent = rng.randrange(-10, 11)
        text = f"{rng.choice(['', '-'])}{mantissa}e{exponent}".encode()
        reference = crt.strtof(text, None)
        expected = struct.unpack("<I", struct.pack("<f", reference))[0]
        actual = ct.c_uint32(0xDEADBEEF)
        error = api.json_test_float_read(text, len(text), ct.byref(actual))
        assert error == 0 and actual.value == expected, (text, error, expected, actual.value)
    print(f"PASS Clinger bounded input reference: {count} mantissa/exponent cases")


def fixed_float_checks(api, count):
    rng = random.Random(0x239377)
    patterns = [0, 0x80000000, 1, 0x80000001, 0x7F7FFFFF, 0xFF7FFFFF,
                0x3F000000, 0x3FC00000, 0x40200000, 0x3E000000]
    patterns.extend(rng.getrandbits(32) for _ in range(count))
    output = ct.create_string_buffer(64)
    checked = 0
    for bits in patterns:
        value = struct.unpack("<f", struct.pack("<I", bits))[0]
        if not math.isfinite(value):
            continue
        for decimals in range(10):
            expected = format(value, f".{decimals}f").encode()
            length = api.json_test_float_fixed(bits, decimals, output, 64)
            assert length == len(expected) and output.raw[:length] == expected, (
                hex(bits), decimals, expected, length, output.raw)
            checked += 1
    print(f"PASS fixed binary32 output: {checked} cases against Python fixed format")


def numeric_syntax_checks(api, count):
    # Independent lexical oracle; do not route these calls through json_read.
    grammar = re.compile(rb"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?"
                         rb"(?:[eE][+-]?[0-9]+)?")
    rng = random.Random(0x239376)
    seeds = [b"0", b"-0", b"1.25", b"1e2", b"-1.25E-3",
             b"1" * 128, b"1" * 129, b"0." + b"0" * 127,
             b"1e999999", b"1e-999999"]
    alphabet = b"0123456789eE+-. \t\r\n\x00xyz"
    invalid = 0
    for _ in range(count):
        text = bytearray(rng.choice(seeds))
        for _ in range(rng.randrange(1, 4)):
            position = rng.randrange(len(text) + 1)
            operation = rng.randrange(3)
            if operation == 0:
                text[position:position] = bytes([rng.choice(alphabet)])
            elif operation == 1:
                del text[position:position + 1]
            elif position < len(text):
                text[position] = rng.choice(alphabet)
        data = bytes(text)
        result = ct.c_uint32(0xDEADBEEF)
        error = api.json_test_float_read(data, len(data), ct.byref(result))
        if grammar.fullmatch(data) is None:
            assert error == 2 and result.value == 0xDEADBEEF, (data, error)
            invalid += 1
        else:
            mantissa = re.split(rb"[eE]", data)[0]
            digits = sum(ord("0") <= byte <= ord("9") for byte in mantissa)
            if digits > 128:
                assert error == 5, (data, error)  # JSON_ERROR_BUFFER
            else:
                assert error in (0, 9), (data, error)  # success or range
            if error != 0:
                assert result.value == 0xDEADBEEF, (data, error)
    print(f"PASS direct float lexical mutations: {count} cases, "
          f"{invalid} invalid; syntax priority and error target preservation")


def unicode_checks(api):
    count = 0
    # Every Unicode scalar, including noncharacters, NUL, shortest/longest
    # UTF-8 forms and all supplementary-plane surrogate-pair combinations.
    for base in range(0, 0x110000, 512):
        text = "".join(chr(cp) for cp in range(base, min(base + 512, 0x110000))
                       if not 0xD800 <= cp <= 0xDFFF)
        count += len(text)
        for escaped in [False, True]:
            data = json.dumps(text, ensure_ascii=escaped).encode("utf-8")
            result = transcode(api, data, 1, 1)
            assert result is not None and strict_load(result) == text, base
    # Exhaust every possible lead/continuation combination for two-byte UTF-8.
    for lead in range(0x80, 0xE0):
        for tail in range(256):
            data = b'"' + bytes([lead, tail]) + b'"'
            valid = 0xC2 <= lead <= 0xDF and 0x80 <= tail <= 0xBF
            error_offset = ct.c_size_t()
            actual = api.json_test_validate(data, len(data), 1,
                                            ct.byref(error_offset)) == 0
            assert actual == valid, data
    # Every unpaired UTF-16 surrogate must fail, including chunked \u digits.
    for cp in range(0xD800, 0xE000):
        data = ('"\\u%04x"' % cp).encode()
        error_offset = ct.c_size_t()
        assert api.json_test_validate(data, len(data), 1,
                                      ct.byref(error_offset)) != 0
    print(f"PASS Unicode: {count} scalars x 2 encodings, 24576 two-byte forms, "
          "2048 unpaired surrogates (one-byte I/O)")


def rounding_checks(api):
    count = 0
    # Exact decimal midpoints plus tiny changes on either side detect double
    # rounding and ties-to-even failures that random input almost never finds.
    with localcontext() as context:
        context.prec = 256
        for exponent in range(255):
            for mantissa in [0, 1, 0x3FFFFF, 0x7FFFFE, 0x7FFFFF]:
                lower = (exponent << 23) | mantissa
                first = Decimal.from_float(struct.unpack("<f", struct.pack("<I", lower))[0])
                second = (Decimal(2) ** 128 if lower == 0x7F7FFFFF else
                          Decimal.from_float(struct.unpack("<f", struct.pack("<I", lower + 1))[0]))
                midpoint = (first + second) / 2
                delta = Decimal(1).scaleb(midpoint.adjusted() - 120)
                cases = [(midpoint - delta, lower),
                         (midpoint, lower + (lower & 1)),
                         (midpoint + delta, lower + 1)]
                for value, expected in cases:
                    for sign in [1, -1]:
                        text = format((value * sign).normalize(), "e").encode()
                        result = ct.c_uint32(0xDEADBEEF)
                        error = api.json_test_float_read(text, len(text), ct.byref(result))
                        if expected in [0, 0x7F800000]:
                            assert error == 9 and result.value == 0xDEADBEEF, text
                        else:
                            wanted = expected | (0x80000000 if sign < 0 else 0)
                            assert error == 0 and result.value == wanted, (text, error, hex(result.value), hex(wanted))
                        count += 1
    print(f"PASS exact decimal rounding thresholds: {count} signed cases")


def benchmark(api):
    text = json.dumps({"APN": "internet", "enabled": True,
                       "items": [{"id": i, "value": 1.25} for i in range(32)]},
                      separators=(",", ":")).encode()
    output = ct.create_string_buffer(8192)
    options = Options(8192, 256)
    count = 20000
    start = time.perf_counter()
    for _ in range(count):
        assert api.json_test_transcode(text, len(text), output, len(output),
                                       ct.byref(options)) > 0
    elapsed = time.perf_counter() - start
    metrics = {"host_transcode_mib_s": round(len(text) * count / elapsed / 2**20, 2),
               "document_bytes": len(text), "iterations": count,
               "host_reader_bytes": api.json_test_reader_size(),
               "host_writer_bytes": api.json_test_writer_size()}
    bits = ct.c_uint32()
    start = time.perf_counter()
    for _ in range(count):
        assert api.json_test_float_read(b"-123.456", 8, ct.byref(bits)) == 0
    metrics["host_float_read_us"] = round((time.perf_counter() - start) * 1e6 / count, 3)
    start = time.perf_counter()
    for _ in range(count):
        assert api.json_test_float_write(0xC2F6E979, output, len(output)) > 0
    metrics["host_float_write_us"] = round((time.perf_counter() - start) * 1e6 / count, 3)
    print(json.dumps(metrics, indent=2))
    (BUILD / "host_metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--count", type=int, default=100000)
    args = parser.parse_args()
    configuration_checks(args.cc)
    rng = random.Random(0x239375)
    api = build(args.cc, args.sanitize)
    corpus = semantic_checks(api, rng, 2000)
    mutation_checks(api, rng, corpus, args.count)
    float_checks(api, rng, args.count)
    numeric_syntax_checks(api, args.count)
    clinger_checks(api, args.count)
    fixed_float_checks(api, args.count)
    rounding_checks(api)
    unicode_checks(api)
    benchmark(api)


if __name__ == "__main__":
    main()
