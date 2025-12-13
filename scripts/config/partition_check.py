#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse, json, sys
from collections import Counter
from typing import Any, List


def load_blueprint(image, conf_name):
    with open(f"conf/{image}/{conf_name}/blueprint.json") as f:
        blueprint = json.load(f)
    return blueprint


def load_parts(image, conf_name) -> List[List[int]]:
    with open(f"conf/{image}/{conf_name}/partition.json", "r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, list):
        raise ValueError("Top-level JSON must be an array (list).")
    parts: List[List[int]] = []
    for i, row in enumerate(data):
        if not isinstance(row, list):
            raise ValueError(f"Element {i} is not an array.")
        row_ints = []
        for j, v in enumerate(row):
            if not isinstance(v, int):
                raise ValueError(f"Item {j} of subarray {i} is not an integer: {v!r}")
            row_ints.append(v)
        parts.append(row_ints)
    return parts


def check_unique(parts, n_nodes):
    # Flatten
    flat: List[int] = [v for row in parts for v in row]

    # Count occurrences
    cnt = Counter(flat)

    # Target set
    target = set(range(1, n_nodes + 1))

    present = set(x for x in flat if 1 <= x <= n_nodes)
    missing = sorted(target - present)

    # Duplicates (count > 1)
    duplicates = sorted([x for x, c in cnt.items() if c > 1])

    # Out of range: values not in 1..N (e.g., 0, negatives, >N)
    out_of_range = sorted([x for x in flat if x < 1 or x > n_nodes])

    ok = (not duplicates) and (not missing) and (not out_of_range)

    if ok:
        print("check_unique OK")
    else:
        if duplicates:
            print(f"Duplicate values (value: count):", file=sys.stderr)
            print(
                "  " + ", ".join(f"{x}:{cnt[x]}" for x in duplicates), file=sys.stderr
            )
        if missing:
            print(f"Missing (should include 1..{n_nodes}):", file=sys.stderr)
            print("  " + ", ".join(map(str, missing)), file=sys.stderr)
        if out_of_range:
            print(f"Out of range (not in 1..{n_nodes}):", file=sys.stderr)
            print("  " + ", ".join(map(str, out_of_range)), file=sys.stderr)


def check_cut(blueprint, parts):
    part_id = {}
    cut_part_id = len(parts) - 1
    for i, part in enumerate(parts):
        for u in part:
            part_id[u] = i

    for i, part in enumerate(parts[:-1]):
        for u in part:
            for neigh in blueprint["routers"][u - 1]["neighbors"]:
                v = neigh["peeridx"]
                if part_id[v] != i and part_id[v] != cut_part_id:
                    raise ValueError(f"Invalid edge {u} => {v}")

    print("check_cut OK")


def main():
    ap = argparse.ArgumentParser(
        description="Check JSON (array of arrays) has no duplicates and contains 1..N."
    )
    ap.add_argument("image", help="image name (frr, crpd, srlinux)")
    ap.add_argument("config", help="config name (ft20, kdl)")
    args = ap.parse_args()

    blueprint = load_blueprint(args.image, args.config)
    n_nodes = len(blueprint["routers"])

    try:
        parts = load_parts(args.image, args.config)
    except Exception as e:
        print(f"Failed to read/validate input: {e}", file=sys.stderr)
        sys.exit(1)

    check_unique(parts, n_nodes)
    check_cut(blueprint, parts)


if __name__ == "__main__":
    main()
