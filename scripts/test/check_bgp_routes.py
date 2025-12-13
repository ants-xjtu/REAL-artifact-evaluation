#!/usr/bin/env python3
"""
check_bgp_routes.py

Check whether each node's bgp_routes-final.log in the given results_dir contains
all prefixes that the topology's blueprint (blueprint.json) expects the nodes to
learn (the networks field).

Supports three image formats: crpd, frr, and bird.

Usage:
    python check_bgp_routes.py -d <path> -i <crpd|frr|bird> -t <topology_name>
"""

import argparse
import re
import json
from pathlib import Path
from typing import List, Tuple, Set


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check bgp_routes-final.log files for route completeness."
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


# ---------------------------------------------------------------------------
# Route parsing logic for each image type
# ---------------------------------------------------------------------------


def parse_frr_routes(file: Path) -> Set[str]:
    """Extract advertised/learned network prefixes from FRR output."""
    text = file.read_text(errors="ignore")
    routes = set()
    in_table = False
    for line in text.splitlines():
        if line.startswith("For address family"):
            in_table = True
            continue
        if in_table:
            if line.strip().startswith("Displayed"):
                break
            m = re.match(r"^\s*[*>=]+\s*([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/[0-9]+)", line)
            if m:
                routes.add(m.group(1))
    return routes


def parse_crpd_routes(file: Path) -> Set[str]:
    """Extract learned prefixes from crpd output (inet.0 table only)."""
    text = file.read_text(errors="ignore")
    routes = set()
    for line in text.splitlines():
        m = re.match(r"^([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/[0-9]+)", line.strip())
        if m:
            routes.add(m.group(1))
    return routes


def parse_bird_routes(file: Path) -> Set[str]:
    """Extract routes from BIRD's Table master4."""
    text = file.read_text(errors="ignore")
    routes = set()
    in_table = False
    for line in text.splitlines():
        if line.startswith("Table master4:"):
            in_table = True
            continue
        if in_table:
            if not line.strip() or line.startswith("Table"):
                break
            m = re.match(r"^([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/[0-9]+)", line.strip())
            if m:
                routes.add(m.group(1))
    return routes


def expected_prefixes(blueprint: dict) -> Set[str]:
    """Return all prefixes this node should learn."""
    routers = blueprint["routers"]
    prefixes = set()
    for r in routers:
        for n in r.get("networks", []):
            prefixes.add(f"{n}/24")
    return prefixes


def check_node_routes(
    expected: Set[str], node_dir: Path, image: str
) -> Tuple[str, bool, str]:
    routes_file = node_dir / "bgp_routes-final.log"
    assert routes_file.exists()

    if image == "crpd":
        routes = parse_crpd_routes(routes_file)
    elif image == "frr":
        routes = parse_frr_routes(routes_file)
    elif image == "bird":
        routes = parse_bird_routes(routes_file)
    else:
        raise ValueError(f"Unsupported image type: {image}")

    missing = expected - routes
    # extra = routes - expected

    if missing:
        return node_dir.name, False, f"Missing prefixes: {', '.join(sorted(missing))}"
    # Optional: if strict matching is desired, also check for extra prefixes
    return node_dir.name, True, f"All {len(routes)} prefixes present"


# ---------------------------------------------------------------------------
# Main program
# ---------------------------------------------------------------------------


def main() -> None:
    args = parse_args()
    results_dir = Path(args.results_dir).resolve()
    curr_dir = Path("./").resolve()

    passed = []
    failed = []
    node_dirs = find_node_logs(results_dir)
    with open(curr_dir / "conf" / args.image / args.topo / "blueprint.json") as f:
        blueprint = json.load(f)

    prefixes = expected_prefixes(blueprint)
    for node_dir in sorted(node_dirs):
        routes_file = node_dir / "bgp_routes-final.log"
        if not routes_file.exists():
            continue
        name, ok, msg = check_node_routes(prefixes, node_dir, args.image)
        if not ok:
            print(f"{name:15s} FAIL {msg}")
            failed.append(name)
        else:
            passed.append(name)

    if failed:
        print(f"Summary: {len(failed)} node(s) have route issues: {', '.join(failed)}")
        exit(1)
    elif len(passed) == 0:
        print("Summary: No bgp_routes-final.log found.")
    else:
        print("Summary: All nodes have complete routes.")


if __name__ == "__main__":
    main()
