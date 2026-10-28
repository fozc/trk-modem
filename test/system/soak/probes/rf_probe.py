"""RF saglik probu (Faz 3): /monitor/rf yuzeyinden durum gecisleri.

Periyodik olarak her fiderin /monitor/rf/N durumunu okur ve gecisleri
izler. GERCERLESEN JSON bicimi (rf_monitor_json.c:243-287):
{"lines":[{"LineId":1,"Phases":[{...}x3],"Maintenance":{...}}]} - faz
nesnelerinde Online/FsmError/UptimeStalled/TripFailedAlarm (emit_phase,
rf_monitor_json.c:125-140), tek-hat modunda ayrica FaultCountsSinceStartup
+ Maintenance.EventStoreDegraded.

Gecisler:
- Online 1 -> 0          : rf_offline (warning)
- FsmError != 0 degisimi : rf_fsm_error (critical)
- UptimeStalled true     : rf_uptime_stalled (warning)
- TripFailedAlarm true   : rf_trip_alarm (warning)
- EventStoreDegraded true: rf_store_degraded (warning)
"""


def extract_rf_state(line):
    """/monitor/rf/N JSON -> karsilastirilabilir saglik durumu.

    Faz durumu "P1".."P3" anahtarlarinda; bicim beklenenden farkliysa
    (device yazilimi degisirse) None doner - yanlis pozitif uretilmez.
    """
    if not isinstance(line, dict):
        return None
    lines = line.get("lines")
    if not isinstance(lines, list) or not lines or \
            not isinstance(lines[0], dict):
        return None
    phases = lines[0].get("Phases")
    if not isinstance(phases, list) or not phases:
        return None
    state = {}
    for index, phase in enumerate(phases[:3]):
        if not isinstance(phase, dict):
            continue
        state["P%d" % (index + 1)] = {
            "online": phase.get("Online") in (1, True),
            "fsm_error": phase.get("FsmError"),
            "uptime_stalled": phase.get("UptimeStalled") in (1, True),
            "trip_alarm": phase.get("TripFailedAlarm") in (1, True),
        }
    maintenance = lines[0].get("Maintenance")
    if isinstance(maintenance, dict):
        state["store_degraded"] = maintenance.get("EventStoreDegraded") \
            in (1, True)
    return state or None


def diff_rf_state(feeder_no, previous, current):
    """Iki durum arasindaki gecisler -> (sinif, siddet, detay) bulgulari."""
    findings = []
    if previous is None or current is None:
        return findings
    for key in sorted(set(previous) | set(current)):
        if not key.startswith("P"):
            continue
        before = previous.get(key)
        after = current.get(key)
        if before is None or after is None:
            continue
        label = "fider%d %s" % (feeder_no, key)
        if before["online"] and not after["online"]:
            findings.append(("rf_offline", "warning",
                             "%s Online 1 -> 0" % label))
        if after["fsm_error"] not in (None, 0) and \
                after["fsm_error"] != before["fsm_error"]:
            findings.append(("rf_fsm_error", "critical",
                             "%s FsmError=%s" % (label, after["fsm_error"])))
        if after["uptime_stalled"] and not before["uptime_stalled"]:
            findings.append(("rf_uptime_stalled", "warning",
                             "%s uptime stalled" % label))
        if after["trip_alarm"] and not before["trip_alarm"]:
            findings.append(("rf_trip_alarm", "warning",
                             "%s trip-failed alarm aktif" % label))
    if current.get("store_degraded") and not previous.get("store_degraded"):
        findings.append(("rf_store_degraded", "warning",
                         "fider%d event store degraded" % feeder_no))
    return findings


class RfProbe:
    """Periyodik RF durum yoklamasi + gecis raporlama (rf.jsonl)."""

    def __init__(self, web_probe, writer, detector, feeder_count=4):
        self.web = web_probe
        self.writer = writer
        self.detector = detector
        self.feeder_count = feeder_count
        self.previous = {}

    def poll(self):
        for feeder_no in range(1, self.feeder_count + 1):
            line = self.web.monitor_line(feeder_no)
            current = extract_rf_state(line)
            if current is None:
                continue
            previous = self.previous.get(feeder_no)
            self.previous[feeder_no] = current
            self.writer.write({"action": "rf_state", "feeder": feeder_no,
                               "state": current})
            if previous is None:
                continue
            for finding in diff_rf_state(feeder_no, previous, current):
                self.detector.report(finding[0], finding[1], "rf", finding[2])
