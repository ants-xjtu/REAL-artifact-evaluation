#!/usr/bin/env python3
"""
check_bgp_summary.py

Check whether there are any down BGP neighbors in the bgp_summary-final.log files of each node under the specified results_dir.
Supports three image formats: crpd, frr, and bird.

Usage:
    python check_bgp_summary.py -d <path> -i <crpd|frr|bird> -t <topology_name>
"""

import argparse
import re
import json
from pathlib import Path
from typing import List, Tuple


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check bgp_summary-final.log files for dropped peers."
    )
    parser.add_argument(
        "-d",
        "--results-dir",
        required=True,
        help="Path to results directory (without node_logs).",
    )
    parser.add_argument(
        "-i",
        "--image",
        required=True,
        choices=["crpd", "frr", "bird"],
        help="Docker image type.",
    )
    parser.add_argument(
        "-t",
        "--topo",
        required=True,
        help="Topology name (e.g. fattree2, topozoo_Fccn, etc.).",
    )
    return parser.parse_args()


def find_node_logs(results_dir: Path) -> List[Path]:
    node_logs_dir = results_dir / "node_logs"
    if not node_logs_dir.exists():
        raise FileNotFoundError(f"{node_logs_dir} not found under {results_dir}")
    return [p for p in node_logs_dir.iterdir() if p.is_dir()]


def parse_crpd_summary(node_id: int, file: Path, blueprint: dict) -> Tuple[bool, str]:
    """Return (is_all_up, reason)"""
    text = file.read_text(errors="ignore")
    m = re.search(r"Down peers:\s*(\d+)", text)
    if not m:
        return False, "No 'Down peers' field found"
    down_peers = int(m.group(1))
    if down_peers > 0:
        return False, f"{down_peers} down peers detected"
    return True, "All peers established"


def parse_frr_summary(node_id: int, file: Path, blueprint: dict) -> Tuple[bool, str]:
    text = file.read_text(errors="ignore")
    if re.search(r"State/PfxRcd\s+", text) is None:
        return False, "No FRR peer section found"
    down_lines = []
    npeers = 0
    for line in text.splitlines():
        if re.search(r"^([0-9]+\.){3}[0-9]+\b", line) is None:
            continue
        if not line.split()[-3].isdigit():
            down_lines.append(line.strip())
        else:
            npeers += 1

    npeers_expected = len(blueprint["routers"][node_id - 1]["neighbors"])
    if npeers != npeers_expected:
        return False, f"Expected {npeers_expected} peers, found {npeers}"

    if down_lines:
        return False, f"{len(down_lines)} peer(s) not established"
    return True, "All peers established"


def parse_bird_summary(node_id: int, file: Path, blueprint: dict) -> Tuple[bool, str]:
    text = file.read_text(errors="ignore")
    npeers = 0
    for line in text.splitlines():
        if "BGP" not in line:
            continue
        if re.search(r"BGP\s+.*\bup\b.*Established", line) is None:
            return False, f"BGP peer {line.split()[0]} not up"
        npeers += 1

    npeers_expected = len(blueprint["routers"][node_id - 1]["neighbors"])
    if npeers != npeers_expected:
        return False, f"Expected {npeers_expected} peers, found {npeers}"
    return True, "All peers established"


def check_node(node_dir: Path, image: str, blueprint: dict) -> Tuple[str, bool, str]:
    node_id = int(node_dir.name.replace("emu-real-", ""))
    summary_file = node_dir / "bgp_summary-final.log"
    assert summary_file.exists()
    if image == "crpd":
        return node_dir.name, *parse_crpd_summary(node_id, summary_file, blueprint)
    elif image == "frr":
        return node_dir.name, *parse_frr_summary(node_id, summary_file, blueprint)
    elif image == "bird":
        return node_dir.name, *parse_bird_summary(node_id, summary_file, blueprint)
    else:
        raise ValueError(f"Unsupported image type: {image}")


def main() -> None:
    args = parse_args()
    results_dir = Path(args.results_dir).resolve()
    curr_dir = Path("./").resolve()

    failed = []
    passed = []
    node_dirs = find_node_logs(results_dir)
    with open(curr_dir / "conf" / args.image / args.topo / "blueprint.json") as f:
        blueprint = json.load(f)

    for node_dir in sorted(node_dirs):
        summary_file = node_dir / "bgp_summary-final.log"
        if not summary_file.exists():
            continue
        name, ok, msg = check_node(node_dir, args.image, blueprint)
        if not ok:
            print(f"{name:15s} FAIL {msg}")
            failed.append(name)
        else:
            passed.append(name)

    if failed:
        print(f"Summary: {len(failed)} node(s) have issues: {', '.join(failed)}")
        exit(1)
    elif len(passed) == 0:
        print("Summary: No bgp_summary-final.log found.")
        exit(1)
    else:
        print("Summary: All nodes are healthy.")


if __name__ == "__main__":
    main()
