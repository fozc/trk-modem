"""Zamanlayici: periyodik (jitter'li) ve rastgele (ortalamali aralikli) gorevler.

- add_periodic(name, interval_s, fn, jitter_s): her turda interval +
  [0, jitter) gecikme; jitter 0 ise sabit kadrans.
- add_random(name, mean_interval_s, fn, min_gap_s): expovariate cekisiyle
  "ortalama X'te bir" rastgele an; min_gap alt siniri. Tohumlanabilir RNG
  ile koşu tekrar uretilebilir (problar kendi rastgele secimlerinde ayni
  RNG'yi paylasir; cok is parcacikli erisim sirasi determinizmi bozar,
  tohum yine de korele koşular icin degerlidir).

Gorevler kucuk bir isci havuzunda kosar; ayni gorev onceki calismasini
hala surduruyorsa yeni tur atlanir (ust uste binme yok). Gorev istisnasi
yutulmaz: on_error(name, error, traceback_text) geri cagrisina gider ve
gorev yine bir sonraki aralikta yeniden planlanir.
"""

import threading
import time
import traceback
from concurrent.futures import ThreadPoolExecutor


class Task:
    """Tek zamanlanmis gorev: fn() + sonraki bekleme suresi uretici."""

    def __init__(self, name, fn, next_delay_s):
        self.name = name
        self.fn = fn
        self.next_delay_s = next_delay_s
        self.due_at = 0.0
        self.running = False
        self.runs = 0
        self.errors = 0


class Scheduler:
    """due olan gorevleri isci havuzuna veren ana zamanlayici."""

    def __init__(self, rng, on_error=None, on_success=None, workers=3):
        self.rng = rng
        self.on_error = on_error
        self.on_success = on_success
        self.tasks = {}
        self.lock = threading.Lock()
        self.pool = ThreadPoolExecutor(max_workers=workers,
                                       thread_name_prefix="soak")

    def add_periodic(self, name, interval_s, fn, jitter_s=0.0):
        """Sabit kadransli gorev; jitter her turda rastgele ek gecikme."""

        def next_delay():
            extra = self.rng.uniform(0.0, jitter_s) if jitter_s > 0.0 else 0.0
            return interval_s + extra

        self._add(name, fn, next_delay, first=interval_s)

    def add_random(self, name, mean_interval_s, fn, min_gap_s=30.0):
        """Rastgele anli gorev: expovariate(1/mean), min_gap alt siniri."""

        def next_delay():
            return max(min_gap_s, self.rng.expovariate(1.0 / mean_interval_s))

        self._add(name, fn, next_delay, first=next_delay())

    def _add(self, name, fn, next_delay, first):
        task = Task(name, fn, next_delay)
        task.due_at = time.monotonic() + first
        with self.lock:
            self.tasks[name] = task

    def run_once(self):
        """Bir tur: su an due olan ve bos olan gorevleri havuza ver."""
        now = time.monotonic()
        with self.lock:
            pending = [task for task in self.tasks.values()
                       if now >= task.due_at and not task.running]
        for task in pending:
            task.running = True
            self.pool.submit(self._run, task)

    def _run(self, task):
        try:
            task.fn()
            task.runs += 1
            if self.on_success is not None:
                try:
                    self.on_success(task.name)
                except Exception:       # noqa: BLE001 (rapor hatasi gorevi bozmaz)
                    pass
        except Exception as error:      # noqa: BLE001 (gorev izolasyonu)
            task.errors += 1
            if self.on_error is not None:
                try:
                    self.on_error(task.name, error,
                                  traceback.format_exc())
                except Exception:       # noqa: BLE001 (hata raporu kesin atlanmaz)
                    pass
        finally:
            task.due_at = time.monotonic() + task.next_delay_s()
            task.running = False

    def stats(self):
        """Gorev basina tur/hata sayilari (durum satiri icin)."""
        with self.lock:
            return {name: {"runs": task.runs, "errors": task.errors,
                           "running": task.running}
                    for name, task in self.tasks.items()}

    def stop(self, wait_s=30.0):
        """Kalan isleri bekle ve havuzu kapat."""
        self.pool.shutdown(wait=True)
        deadline = time.monotonic() + wait_s
        while time.monotonic() < deadline:
            with self.lock:
                if not any(task.running for task in self.tasks.values()):
                    break
            time.sleep(0.1)
