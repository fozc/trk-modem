"""JSON timeline scenario executor for the RF hub simulator.

Scenario file format (all times relative to scenario start):

{
  "duration_s": 60,              // optional; run forever if absent
  "setup": [ {"do": "action", ...}, ... ],        // applied at t=0
  "steps": [ {"at_s": 5.0, "do": "action", ...}, ... ]
}

Every "do" is a control-channel action (sim/control.py handle()).
Example:

{
  "duration_s": 45,
  "setup": [
    {"do": "preload_inventory", "zone": 1, "lines": [1, 2]},
    {"do": "set_knob", "name": "live_period_s", "value": 5}
  ],
  "steps": [
    {"at_s": 1.0,  "do": "boot"},
    {"at_s": 20.0, "do": "add_events", "count": 3, "code": 1, "line": 1},
    {"at_s": 25.0, "do": "inject_trip", "line": 1, "phase": 2}
  ]
}
"""

import time


class ScenarioExecutor:
    def __init__(self, path, dispatcher, now_ms=None):
        import json
        from pathlib import Path
        self.spec = json.loads(Path(path).read_text(encoding="ascii"))
        self.dispatcher = dispatcher      # callable(request dict)
        self.now_ms = now_ms or (lambda: int(time.monotonic() * 1000))
        self.start_ms = None
        self.pending = list(self.spec.get("steps", []))
        self.done = False

    def start(self):
        self.start_ms = self.now_ms()
        for step in self.spec.get("setup", []):
            self.dispatcher(step)

    def tick(self):
        if self.start_ms is None or self.done:
            return
        now = self.now_ms()
        elapsed_s = (now - self.start_ms) / 1000.0
        remaining = []
        for step in self.pending:
            if step.get("at_s", 0.0) <= elapsed_s:
                self.dispatcher(step)
            else:
                remaining.append(step)
        self.pending = remaining
        duration = self.spec.get("duration_s")
        if duration is not None and elapsed_s >= duration and not self.pending:
            self.done = True
