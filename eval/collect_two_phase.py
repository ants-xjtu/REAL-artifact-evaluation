#!/usr/bin/env python3
import os
import re
import csv
import argparse


def read_stage_border_ts(path):
    """Return converge time (sleep - converge)."""
    stages = {}
    with open(path) as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 2:
                try:
                    stages[parts[0]] = float(parts[1])
                except ValueError:
                    pass
    c_converge = stages.get("converge")
    c_sleep = stages.get("sleep")
    return c_sleep - c_converge if c_converge and c_sleep else None


def read_llc_miss_rate(path):
    """Extract LLC miss rate percentage from cache_stat.log."""
    pattern = re.compile(r'LLC-load-misses\s+#\s+([\d.]+)%')
    with open(path) as f:
        for line in f:
            m = pattern.search(line)
            if m:
                return float(m.group(1))
    return None


def read_meta(path):
    meta = {}
    with open(path) as f:
        for line in f:
            if "=" in line:
                k, v = line.strip().split("=", 1)
                meta[k.strip()] = v.strip()
    return meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results_dir")
    ap.add_argument("-o", "--output", default="eval/data/two-phase.csv")
    ap.add_argument("--tag", default="ae-fig13")
    args = ap.parse_args()

    rows = []
    for name in sorted(os.listdir(args.results_dir)):
        if args.tag not in name:
            continue
        run_dir = os.path.join(args.results_dir, name)
        if not os.path.isdir(run_dir):
            continue

        meta = read_meta(os.path.join(run_dir, "meta.txt"))
        if not meta or meta.get("mode") != "preload":
            continue

        topo_id = meta.get("topo_id", "")
        topo = f"FT{topo_id}" if meta.get("topo_type") == "fattree" else topo_id.upper()
        runtime = meta.get("sched_mode", "unknown")

        converge = read_stage_border_ts(os.path.join(run_dir, "stage_border_ts"))
        llcmiss = read_llc_miss_rate(os.path.join(run_dir, "cache_stat.log"))

        if converge and llcmiss:
            rows.append({
                "topo": topo,
                "runtime": runtime,
                "time": round(converge, 2),
                "llcmiss": round(llcmiss, 2)
            })

    topo_order = {"FT24": 0, "FT26": 1, "FT28": 2, "FT30": 3}
    rows.sort(key=lambda r: (topo_order.get(r["topo"], 99), r["runtime"]))

    with open(args.output, "w", newline="") as f:
        w = csv.DictWriter(f, ["topo", "runtime", "time", "llcmiss"])
        w.writeheader()
        w.writerows(rows)
    print(f"Wrote {len(rows)} rows to {args.output}")


if __name__ == "__main__":
    main()
