#!/usr/bin/env python3
import os
import re
import csv
import argparse

PART_RE = re.compile(r'^(\d+)part$')

MEM_USED_RE = re.compile(
    r'^\s*Mem:\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$'
)


def parse_size_to_gib(s):
    s = s.strip()
    if s.endswith('Gi'):
        return float(s[:-2])
    if s.endswith('Mi'):
        return float(s[:-2]) / 1024.0
    if s.endswith('Ki'):
        return float(s[:-2]) / (1024.0 * 1024.0)
    if s.endswith('Ti'):
        return float(s[:-2]) * 1024.0
    if s.endswith('B'):
        try:
            return float(s[:-1]) / (1024.0 ** 3)
        except ValueError:
            return 0.0
    try:
        return float(s)
    except ValueError:
        return None


def read_stage_border_ts(path):
    stages = {}
    if not os.path.isfile(path):
        return stages
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            k, v = line.split()
            try:
                stages[k] = float(v)
            except ValueError:
                pass
    return stages


def read_converge_end_ts(path):
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r") as f:
            return float(f.readlines()[-1].strip())
    except Exception:
        return None


def read_global_mem_delta_gib(path):
    if not os.path.isfile(path):
        return None
    used = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = MEM_USED_RE.match(line.rstrip("\n"))
            if not m:
                continue
            val = parse_size_to_gib(m.group(2))
            if val is not None:
                used.append(val)
    if not used:
        return None
    return max(used) - min(used)


def read_meta_txt(path):
    meta = {}
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or "=" not in line:
                continue
            k, v = line.split("=", 1)
            meta[k.strip()] = v.strip()
    return meta


def parse_int(s):
    try:
        return int(s)
    except Exception:
        return ""


def parse_from_meta(meta):
    mode = meta.get("mode", "")
    image = meta.get("image", "")
    topo_type = meta.get("topo_type", "")
    topo_id = meta.get("topo_id", "")
    topo = meta.get("topo", "")

    parts = ""
    if meta.get("partitioned", "False") != "False":
        parts = parse_int(meta.get("partitioned", ""))

    if topo_type == "fattree":
        topo_short = "FT" + topo_id
    elif topo_type == "topozoo":
        topo_short = topo_id.upper()
    elif topo_type == "dupzoo":
        topo_short = topo_id.replace(":", "").upper()
    else:
        topo_short = topo

    run_id = f"{image}-{mode}-{topo_short}" + (f"-{parts}parts" if parts and parts > 1 else "")

    return {
        "id": run_id,
        "Topo": topo_short,
        "Parts": parts,
        "image": image,
        "mode": mode,
    }


def safe_round(x, nd=2):
    if x is None:
        return ""
    try:
        return str(round(float(x), nd))
    except Exception:
        return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results_dir")
    ap.add_argument("-o", "--output", default="summary.csv")
    args = ap.parse_args()

    rows = []

    for name in sorted(os.listdir(args.results_dir)):
        run_dir = os.path.join(args.results_dir, name)
        if not os.path.isdir(run_dir):
            continue

        meta = read_meta_txt(os.path.join(run_dir, "meta.txt"))
        if meta is None:
            continue

        info = parse_from_meta(meta)

        stages = read_stage_border_ts(os.path.join(run_dir, "stage_border_ts"))
        mem_delta = read_global_mem_delta_gib(os.path.join(run_dir, "dynmem.log"))

        c0 = stages.get("create_containers")
        c1 = stages.get("create_network")
        c2 = stages.get("converge")
        end_ts = stages.get("sleep")

        bootup = c1 - c0 if c0 and c1 else None
        create_network = c2 - c1 if c1 and c2 else None
        converge = end_ts - c2 if c2 and end_ts else None
        total = end_ts - c0 if c0 and end_ts else None

        rows.append({
            "id": info["id"],
            "Topo": info["Topo"],
            "Parts": info["Parts"],
            "image": info["image"],
            "mode": info["mode"],
            "bootup": safe_round(bootup),
            "create_network": safe_round(create_network),
            "converge": safe_round(converge),
            "time": safe_round(total),
            "global mem": safe_round(mem_delta),
        })

    fieldnames = [
        "id", "Topo", "Parts", "image", "mode",
        "bootup", "create_network",
        "converge", "time", "global mem"
    ]

    with open(args.output, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)


if __name__ == "__main__":
    main()
