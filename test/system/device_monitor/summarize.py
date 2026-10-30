#!/usr/bin/env python3
"""Koşu dizininden summary.md ureten cevrimdisi analizci.

Kullanim:
  python summarize.py test/build/device_monitor/<timestamp>

Okur: manifest.json, metrics.json, anomalies.jsonl, chains.jsonl,
iec104.jsonl, rf.jsonl; evidence/ dizinini listeler. Yazdigi summary.md:
kimlik + sure, anomali siniflari (sayi/ilk/son/siddet), zincir halka
basarilari ve gecikmeleri, periyodik cevrim istatistikleri, kanit paketi
dizini. Sonradan insan/ajan incelemesi icin tek bakis ozeti.
"""

import argparse
import datetime
import json
import sys
from collections import defaultdict
from pathlib import Path


def read_json(path):
    source = Path(path)
    if not source.exists():
        return None
    try:
        return json.loads(source.read_text(encoding="utf-8"))
    except ValueError:
        return None


def read_jsonl(path, max_lines=200000):
    source = Path(path)
    if not source.exists():
        return []
    with open(source, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.readlines()[-max_lines:]
    records = []
    for line in lines:
        try:
            records.append(json.loads(line))
        except ValueError:
            continue
    return records


def fmt_wall(value):
    if not value:
        return "-"
    return datetime.datetime.fromtimestamp(value).strftime("%m-%d %H:%M:%S")


def anomaly_table(records):
    groups = defaultdict(list)
    for record in records:
        groups[record.get("class", "?")].append(record)
    rows = []
    for label in sorted(groups, key=lambda name: -len(groups[name])):
        items = groups[label]
        severities = sorted({item.get("severity", "?") for item in items})
        rows.append("| %s | %s | %d | %s | %s |" %
                    (label, "/".join(severities), len(items),
                     fmt_wall(min(item.get("wall", 0) for item in items)),
                     fmt_wall(max(item.get("wall", 0) for item in items))))
    return rows


def chain_stats(records):
    total = len(records)
    triggered = [r for r in records if r.get("ok")]
    link_stats = defaultdict(lambda: {"ok": 0, "total": 0, "ms": []})
    for record in triggered:
        for name, link in (record.get("links") or {}).items():
            entry = link_stats[name]
            entry["total"] += 1
            if link.get("ok"):
                entry["ok"] += 1
                entry["ms"].append(link.get("ms") or 0.0)
    return total, triggered, link_stats


def build_summary(run_dir):
    run_dir = Path(run_dir)
    manifest = read_json(run_dir / "manifest.json") or {}
    metrics = read_json(run_dir / "metrics.json") or {}
    anomalies = read_jsonl(run_dir / "anomalies.jsonl")
    chains = read_jsonl(run_dir / "chains.jsonl")
    iec104 = read_jsonl(run_dir / "iec104.jsonl")
    rf_states = read_jsonl(run_dir / "rf.jsonl")

    lines = ["# Cihaz Izleme Ozeti", ""]
    lines.append("- Koşu dizini: `%s`" % run_dir)
    lines.append("- Git: `%s` (dirty=%s) | host=%s | tohum=%s | kip=%s" %
                 (manifest.get("git_short"), manifest.get("git_dirty_files"),
                  manifest.get("host"), manifest.get("seed"),
                  manifest.get("iec104_mode")))
    if manifest.get("iec104_master"):
        lines.append("- IEC104 master: %s" % manifest["iec104_master"])
    lines.append("- Durum: **%s** | sure=%.2f sa | critical=%s warning=%s "
                 "harness=%s" %
                 (metrics.get("state"), metrics.get("elapsed_hours") or 0,
                  metrics.get("anomalies_critical"),
                  metrics.get("anomalies_warning"),
                  metrics.get("harness_errors")))
    lines.append("")

    lines.append("## Anomali siniflari")
    lines.append("")
    rows = anomaly_table(anomalies)
    if rows:
        lines.append("| Sinif | Siddet | Sayi | Ilk | Son |")
        lines.append("|---|---|---|---|---|")
        lines.extend(rows)
    else:
        lines.append("Anomali yok.")
    lines.append("")

    total, triggered, link_stats = chain_stats(chains)
    lines.append("## Olay zincirleri")
    lines.append("")
    if total == 0:
        lines.append("Zincir koşusu yok (AY kapali).")
    else:
        lines.append("%d tetikleme denemesi, %d olay gonderildi." %
                     (total, len(triggered)))
        lines.append("")
        lines.append("| Halka | Basarili | Toplam | min/ort/max ms |")
        lines.append("|---|---|---|---|")
        for name in sorted(link_stats):
            entry = link_stats[name]
            timings = entry["ms"]
            center = sum(timings) / len(timings) if timings else 0.0
            lines.append("| %s | %d | %d | %s/%s/%s |" %
                         (name, entry["ok"], entry["total"],
                          round(min(timings)) if timings else "-",
                          round(center) if timings else "-",
                          round(max(timings)) if timings else "-"))
    lines.append("")

    cycles = [r for r in iec104 if r.get("action") == "periodic_cycle" and
              r.get("cycle_ms") is not None]
    gi = [r for r in iec104 if r.get("action") == "gi"]
    lines.append("## IEC104")
    lines.append("")
    if cycles:
        timings = [r["cycle_ms"] for r in cycles]
        lines.append("- %d periyodik cevrim; cevrim suresi min/ort/max = "
                     "%.0f/%.0f/%.0f ms" %
                     (len(cycles), min(timings), sum(timings) / len(timings),
                      max(timings)))
    else:
        lines.append("- Periyodik cevrim kaydi yok.")
    ok_gi = sum(1 for r in gi if r.get("ok") is True)
    skipped_gi = sum(1 for r in gi if r.get("skip"))
    failed_gi = sum(1 for r in gi if r.get("ok") is False)
    lines.append("- GI turlari: %d basarili, %d basarisiz, %d skip "
                 "(hatta hizmet yok)" % (ok_gi, failed_gi, skipped_gi))
    lines.append("")

    lines.append("## RF")
    lines.append("")
    lines.append("- %s durum anligi kaydedildi (rf.jsonl)." % len(rf_states))
    lines.append("")

    evidence_root = run_dir / "evidence"
    if evidence_root.exists():
        packages = sorted(p.name for p in evidence_root.iterdir()
                          if p.is_dir())
        lines.append("## Kanit paketleri (%d)" % len(packages))
        lines.append("")
        lines.extend("- `%s`" % name for name in packages)
    else:
        lines.append("## Kanit paketleri")
        lines.append("")
        lines.append("Kanit toplanmadi.")
    lines.append("")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--stdout", action="store_true",
                        help="summary.md yazmadan stdout'a bas")
    args = parser.parse_args(argv)
    text = build_summary(args.run_dir)
    if args.stdout:
        print(text)
    else:
        target = args.run_dir / "summary.md"
        target.write_text(text, encoding="utf-8")
        print("yazildi: %s" % target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
