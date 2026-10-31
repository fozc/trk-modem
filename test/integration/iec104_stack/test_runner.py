"""Offline checks for portable IEC104 integration dependency discovery."""
import sys
import os
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from pathlib import Path
from unittest.mock import patch

import run_tests as runner


class LibraryBoundary(unittest.TestCase):
    def test_library_compiles_with_only_its_own_include_directory(self):
        """RF/NVRAM/RTC/old stack headers must not be required by the library."""
        compiler = shutil.which(os.environ.get("CC", "gcc"))
        self.assertIsNotNone(compiler, "Native C compiler missing (CC or gcc)")
        folder = runner.ROOT / "Application/iec104_stack"
        sources = sorted(folder.glob("*.c"))
        self.assertEqual({"iec104_asdu.c", "iec104_core.c", "iec104_server.c"},
                         {source.name for source in sources})
        command = [compiler, "-std=c11", "-pedantic", "-Wall", "-Wextra", "-Werror",
                   "-Wshadow", "-Wconversion", "-Wdouble-promotion", "-Wformat=2",
                   "-I" + str(folder), "-fsyntax-only", *map(str, sources)]
        subprocess.run(command, check=True, capture_output=True, text=True)

    def test_full_size_extension_uses_real_builder_and_core_without_project_headers(self):
        compiler = shutil.which(os.environ.get("CC", "gcc"))
        self.assertIsNotNone(compiler, "Native C compiler missing (CC or gcc)")
        folder = runner.ROOT / "Application/iec104_stack"
        # Compile every participant with one consistent capacity, then check a full APDU.
        source = r'''
/**
 * @file capacity.c
 * @brief Generated host harness for maximum ASDU extension capacity.
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "iec104_server.h"
#include <string.h>
static uint8_t frame[255];
static size_t length;
static bool write_frame(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    if (size > sizeof(frame))
    {
        return false;
    }
    memcpy(frame, data, size);
    length = size;
    return true;
}
static bool receive(void *context, const iec104_asdu_t *asdu)
{
    (void)context;
    (void)asdu;
    return true;
}
int main(void)
{
    iec104_point_t point;
    uint8_t value[240];
    for (size_t index = 0U; index < sizeof(value); index++)
    {
        value[index] = (uint8_t)index;
    }
    if (!iec104_point_set_encoded(&point, 0x010203U, 200U, value, sizeof(value)))
    {
        return 1;
    }
    iec104_asdu_t header = { .type = point.type, .cause = 3U, .address = 1U };
    iec104_builder_t builder;
    if (!iec104_builder_begin(&builder, &header, point.value_size) ||
        !iec104_builder_add(&builder, point.ioa, point.value) || (builder.length != 249U)) { return 2; }
    if (iec104_builder_add(&builder, 2U, value) || (builder.length != 249U))
    {
        return 3;
    }
    iec104_core_t core;
    const iec104_core_config_t config = {12U, 8U, 15000U, 10000U, 20000U};
    const iec104_core_io_t io = { .write = write_frame, .receive = receive };
    if (!iec104_core_init(&core, &config, &io))
    {
        return 4;
    }
    iec104_core_open(&core, 0U);
    const uint8_t start[] = {0x68U, 4U, 7U, 0U, 0U, 0U};
    if (iec104_core_process_data(&core, start, sizeof(start), 0U) != sizeof(start))
    {
        return 5;
    }
    if (!iec104_core_send(&core, builder.data, builder.length, 0U))
    {
        return 6;
    }
    if ((length != 255U) || (frame[0] != 0x68U) || (frame[1] != 253U) ||
        (frame[6] != 200U) || (frame[7] != 1U) || (frame[12] != 3U) ||
        (frame[13] != 2U) || (frame[14] != 1U) || (memcmp(&frame[15], value, sizeof(value)) != 0)) { return 7; }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            harness = Path(directory) / "capacity.c"
            harness.write_text(source, encoding="ascii")
            executable = Path(directory) / ("capacity.exe" if os.name == "nt" else "capacity")
            command = [compiler, "-std=c11", "-pedantic", "-Wall", "-Wextra", "-Werror",
                       "-Wshadow", "-Wconversion", "-Wdouble-promotion", "-Wformat=2",
                       "-DIEC104_POINT_VALUE_MAX=240", "-I" + str(folder), str(harness),
                       *map(str, sorted(folder.glob("*.c"))), "-o", str(executable)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], check=True, capture_output=True, text=True)


class InterpreterDiscovery(unittest.TestCase):
    def check_layout(self, relative):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            interpreter = root / "test/system/iec104_master/.venv" / relative
            interpreter.parent.mkdir(parents=True)
            interpreter.touch()
            with patch.object(runner, "ROOT", root), \
                    patch.object(runner.importlib.util, "find_spec", return_value=None), \
                    patch.object(runner.subprocess, "call", return_value=0) as call:
                self.assertEqual(0, runner.main())
                self.assertEqual(str(interpreter), call.call_args.args[0][0])

    def test_windows_venv_is_discovered(self):
        self.check_layout("Scripts/python.exe")

    def test_linux_venv_is_discovered(self):
        self.check_layout("bin/python")

    def test_missing_dependency_fails_with_the_requirements_path(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(runner, "ROOT", Path(directory)), \
                    patch.object(runner.importlib.util, "find_spec", return_value=None):
                with self.assertRaisesRegex(SystemExit, "iec104_stack/requirements.txt"):
                    runner.main()


class ConnectionStartup(unittest.TestCase):
    def test_open_muted_connection_gets_an_explicit_startdt(self):
        events = []

        class Connection:
            protocol_parameters = SimpleNamespace()
            is_connected = False
            is_muted = True

            def add_station(self, address):
                self.address = address

            def connect(self):
                events.append("connect")
                self.is_connected = True

            def unmute(self):
                events.append("unmute")
                self.is_muted = False
                return self.is_connected

        connection = Connection()
        client = SimpleNamespace(add_connection=lambda *a, **k: connection,
                                 start=lambda: events.append("start"))
        module = SimpleNamespace(Client=lambda **k: client,
                                 Init=SimpleNamespace(NONE=0))
        case = runner.StackIntegration()
        case.device = SimpleNamespace(port=1234, sent=[bytes(6) + bytes([70])])
        with patch.dict(sys.modules, {"c104": module}):
            case.connect_c104()
        self.assertEqual(["start", "connect", "unmute"], events)


if __name__ == "__main__":
    unittest.main()
