"""TCP JSON-lines control channel for the RF hub simulator.

Lets the HIL harness (and a human via sim_cmd.py) drive the simulated
hub at runtime: inject trips/events, arm wire faults, flip behaviour
knobs, reboot the hub, query state.

Protocol: one JSON object per line per request; one JSON object per
line per response: {"ok": true, ...} or {"ok": false, "error": "..."}.
"""

import json
import socket
import threading


def make_eui(line, phase):
    """Deterministic test EUI-64: 00:12:4B:00:38:C9:Fx:y (BOLATeX OUI)."""
    return bytes([0x00, 0x12, 0x4B, 0x00, 0x38, 0xC9,
                 0xF0 | (line & 0x0F), (phase & 0x0F) * 16 + 0x0A])


# knobs the control channel may set on the hub model (defensive list)
HUB_KNOBS = (
    "fw_label", "sched_active", "live_period_s", "live_enabled",
    "log_bell_enabled",
    "pwr_enabled", "cfg_delivered_ms", "cfg_applied_ms",
    "cfg_force_fail_reason", "cfg_no_deliver", "cfg_no_apply",
    "cfg_delivered_fail_ms", "cfg_applied_fail_ms",
    "cfg_report_crc_offset", "cfg_mute", "pwr_echo_delay_s", "pwr_result_delay_s",
    "pwr_result_sonuc", "pwr_akibet_delay_s", "pwr_cancel_verified",
    "mh_has_key", "rf_queue_busy", "epoch_duration_ms",
    "aku_kaynak_yok", "telemetry_stale",
)


def dispatch_action(hub, faults, lock, req):
    """Execute one control action. Shared by TCP server and scenarios."""
    action = req.get("do")
    with lock:
        if action == "status":
            return {"ok": True, "state": hub.status()}
        if action == "boot":
            hub.start_boot()
            return {"ok": True}
        if action == "reboot":
            hub.reboot()
            return {"ok": True}
        if action == "ping":
            hub.notify(0x00, b"", note="ping")
            return {"ok": True}
        if action == "add_events":
            hub.add_events(int(req.get("count", 1)),
                           code=int(req.get("code", 1)),
                           line=int(req.get("line", 1)),
                           zone=req.get("zone"),
                           phase=req.get("phase"),
                           fault_count=req.get("fault_count"),
                           boot_counter=int(req.get("boot_counter", 7)),
                           perm_count=req.get("perm_count"),
                           temp_count=req.get("temp_count"),
                           mh_record=bool(req.get("mh_record", False)))
            return {"ok": True, "pending": hub.pending_count()}
        if action == "service_wipe":
            hub.service_wipe()
            return {"ok": True, "total": hub.total}
        if action == "store_format":
            hub.store_format()
            return {"ok": True, "total": hub.total}
        if action == "store_state_loss":
            hub.store_state_loss()
            return {"ok": True, "total": hub.total}
        if action == "advance_ring":
            hub.advance_ring(int(req["records"]))
            return {"ok": True, "total": hub.total,
                    "wrap": hub.wrap_count()}
        if action == "live_mute":
            line = int(req["line"])
            phase = int(req["phase"])
            if req.get("value", True):
                hub.live_muted.add((line, phase))
            else:
                hub.live_muted.discard((line, phase))
            return {"ok": True, "muted": sorted(hub.live_muted)}
        if action == "move_card":
            # R2 card change at the MH side: rebind (line, phase) to a
            # different EUI-64 without a DUT-visible inventory command.
            # The default replacement EUI always comes from line 9 (the
            # unassigned band) so it can never equal the DUT-uploaded EUI
            # of the target position.
            eui = req.get("eui")
            if isinstance(eui, str):
                eui = bytes.fromhex(eui.replace(":", ""))
            eui = bytes(eui or make_eui(9, int(req.get("phase", 1))))
            hub._store_inventory_entry(int(req.get("zone", 1)),
                                       int(req["line"]), int(req["phase"]),
                                       eui, 0)
            return {"ok": True, "eui": eui.hex()}
        if action == "epoch_force_done":
            # Test shortcut: complete a running renewal immediately.
            hub.epoch_duration_ms = 0
            if hub.epoch_due_ms is not None:
                hub.epoch_due_ms = hub.now_ms()
            return {"ok": True}
        if action == "inject_trip":
            hub.inject_trip(int(req["line"]), int(req["phase"]),
                            fault_count=int(req.get("fault_count", 2)),
                            max_current=float(req.get("max_current", 0.0)))
            return {"ok": True}
        if action == "inject_discovery":
            eui = req.get("eui") or make_eui(int(req.get("line", 9)),
                                             int(req.get("phase", 1)))
            if isinstance(eui, str):
                eui = bytes.fromhex(eui.replace(":", ""))
            hub.inject_discovery(bytes(eui), int(req.get("rssi", -40)))
            return {"ok": True, "eui": bytes(eui).hex()}
        if action == "inject_anomaly":
            hub.inject_anomaly(src=int(req.get("src", 0x01)),
                               state=int(req.get("state", 1)),
                               win=int(req.get("win", 5)),
                               total=int(req.get("total", 42)),
                               path=int(req.get("path", 0)))
            return {"ok": True}
        if action == "log_bell":
            hub.send_log_bell()
            return {"ok": True}
        if action == "pwr_summary":
            hub.send_pwr_summary(note="pwr_summary manual")
            return {"ok": True}
        if action == "pwr_alarm":
            kod = int(req["kod"])
            if req.get("level") is not None:
                hub.set_pwr_alarm(kod, bool(req.get("level")))
            else:
                hub.send_pwr_alarm(kod, int(req.get("durum", 1)),
                                   deger0=int(req.get("deger0", 0)),
                                   deger1=int(req.get("deger1", 0)))
            return {"ok": True, "mask": hub.pwr_alarm_mask}
        if action == "son_nefes":
            hub.send_son_nefes(oturum=req.get("oturum"),
                               neden=int(req.get("neden", 1)))
            return {"ok": True}
        if action == "set_knob":
            name = req["name"]
            if name not in HUB_KNOBS:
                raise ValueError("knob not permitted: %s" % name)
            setattr(hub, name, req.get("value"))
            return {"ok": True, "name": name, "value": req.get("value")}
        if action == "set_telemetry":
            hub.telemetry = req.get("value")
            return {"ok": True}
        if action == "fault":
            faults.arm(req["action"], **req.get("args", {}))
            return {"ok": True}
        if action == "faults_clear":
            faults.clear()
            return {"ok": True}
        if action == "mark_bad_slot":
            hub.bad_slots.add(int(req["index"]))
            return {"ok": True}
        if action == "corrupt_record_slot":
            hub.corrupt_record_slots.add(int(req["index"]))
            return {"ok": True}
        if action == "clear_corrupt_record_slots":
            hub.corrupt_record_slots.clear()
            return {"ok": True}
        if action == "raw_notify":
            # arbitrary SET notification (e.g. vendor-band CMD for the
            # DUT-ignore test); dst is always the RTU
            data = bytes.fromhex(req.get("data_hex", ""))
            hub.notify(int(req["cmd"]), data,
                       note="raw_notify cmd=0x%02X" % int(req["cmd"]))
            return {"ok": True}
        if action == "clear_bad_slots":
            hub.bad_slots.clear()
            return {"ok": True}
        if action == "set_degraded":
            hub.degraded = bool(req.get("value", True))
            return {"ok": True}
        if action == "busy_for":
            hub.busy_until_ms = hub.now_ms() + int(req.get("ms", 3000))
            return {"ok": True}
        if action == "preload_inventory":
            from .hub_model import InventoryEntry
            zone = int(req.get("zone", 1))
            lines = req.get("lines", [1])
            phases = req.get("phases", [1, 2, 3])
            now = hub.now_ms()
            for line in lines:
                for phase in phases:
                    eui = make_eui(int(line), int(phase))
                    entry = InventoryEntry(zone, int(line), int(phase),
                                           eui, 0)
                    entry.last_live_ms = now
                    hub.inventory[eui.hex()] = entry
            if req.get("loaded", True):
                hub.loaded = True
                hub.learned_zone = zone
                hub.boot_next_ms = None
            return {"ok": True, "count": len(hub.inventory)}
        if action == "live_value":
            entry = hub._entry_by_line_phase(int(req["line"]),
                                             int(req["phase"]))
            if entry is None:
                raise ValueError("no inventory entry for line/phase")
            for key, value in req.get("values", {}).items():
                if not hasattr(entry, key):
                    raise ValueError("no live field %r" % key)
                setattr(entry, key, value)
            return {"ok": True}
        raise ValueError("unknown action %r" % action)


class Dispatcher:
    """Callable wrapper so scenarios and TCP share one dispatch path."""

    def __init__(self, hub, faults, lock=None):
        self.hub = hub
        self.faults = faults
        self.lock = lock or threading.RLock()

    def __call__(self, request):
        return dispatch_action(self.hub, self.faults, self.lock, request)


class ControlServer:
    def __init__(self, hub, faults, host="127.0.0.1", port=7788,
                 on_start=None, lock=None):
        self.dispatcher = Dispatcher(hub, faults, lock)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.listen(4)
        self.sock.settimeout(0.5)
        self.port = port
        self.on_start = on_start
        self.running = True
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        if self.on_start:
            self.on_start(self.port)
        clients = []
        while self.running:
            try:
                conn, _ = self.sock.accept()
                conn.settimeout(0.2)
                clients.append(conn)
            except socket.timeout:
                pass
            except OSError:
                break
            alive = []
            for conn in clients:
                try:
                    data = conn.recv(4096)
                    if data:
                        for line in data.splitlines():
                            line = line.strip()
                            if not line:
                                continue
                            try:
                                reply = self.dispatcher(json.loads(line))
                            except (ValueError, KeyError,
                                    TypeError) as err:
                                reply = {"ok": False, "error": str(err)}
                            conn.sendall((json.dumps(reply) + "\n")
                                         .encode("ascii"))
                        alive.append(conn)
                    else:
                        conn.close()
                except socket.timeout:
                    alive.append(conn)
                except OSError:
                    pass
            clients = alive
        for conn in clients:
            try:
                conn.close()
            except OSError:
                pass
        self.sock.close()

    def stop(self):
        self.running = False


class ControlClient:
    """Thin client used by the HIL harness and sim_cmd.py."""

    def __init__(self, host="127.0.0.1", port=7788, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""

    def call(self, **request):
        self.sock.sendall((json.dumps(request) + "\n").encode("ascii"))
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("control server closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line.decode("ascii"))

    def close(self):
        self.sock.close()
