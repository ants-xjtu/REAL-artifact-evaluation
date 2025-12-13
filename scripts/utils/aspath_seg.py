#!/usr/bin/env python3
# aspath_segmenter.py

import re
import os
import json
import argparse
import pandas as pd
from typing import Dict, List, Tuple, Optional
import matplotlib.pyplot as plt
import numpy as np


def collect_route_files(routes_dir: str) -> List[Tuple[str, int, int]]:
    """
    Traverse emu-real-<node_id> subdirectories under routes_dir.
    Find all bgp_routes-<num>.log files.
    Return [(filepath, node_id, num), ...], where num is used for sorting.
    """
    results = []
    for root, _, files in os.walk(routes_dir):
        base = os.path.basename(root)
        m = re.match(r"emu-real-(\d+)", base)
        if not m:
            continue
        node_id = int(m.group(1))
        for fn in files:
            m2 = re.match(r"bgp_routes-(\d+)\.log$", fn)
            if m2:
                num = int(m2.group(1))
                results.append((os.path.join(root, fn), node_id, num))
    return results


AS_RE = re.compile(r"\brouter\s+bgp\s+(\d+)\b", re.IGNORECASE)
NODE_ID_LINE_RE = re.compile(r"\bnode[_-]?id\s*[:=]?\s*(\d+)\b", re.IGNORECASE)
TRAILING_ASPATH_RE = re.compile(r"0 (\d+(?:\s+\d+)+)\s*[ie\?]?\s*$")
IPV4_PREFIX_RE = re.compile(r"\b\d{1,3}(?:\.\d{1,3}){3}/\d{1,2}\b")
IPV4_NEXT_HOP_RE = re.compile(r"\b\d{1,3}(?:\.\d{1,3}){3}\b")


def parse_frr_conf(file_path: str) -> Tuple[Optional[int], Optional[int]]:
    """Return (node_id, asn) parsed from a single frr.conf file."""
    node_id = None
    asn = None
    try:
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()
    except Exception:
        return (None, None)
    m_as = AS_RE.search(text)
    if m_as:
        asn = int(m_as.group(1))
    m_n = NODE_ID_LINE_RE.search(text)
    if m_n:
        node_id = int(m_n.group(1))
    if node_id is None:
        digits = re.findall(r"(\d+)", os.path.basename(file_path))
        if digits:
            node_id = int(digits[-1])
    return (node_id, asn)


def build_as_to_nodeid(frr_dir: str) -> Dict[int, int]:
    """Walk directory, parse conf files, return mapping AS -> node_id."""
    as_to_node: Dict[int, int] = {}
    for root, _, files in os.walk(frr_dir):
        for fn in files:
            if not fn.lower().endswith((".conf", ".cfg", ".txt")):
                continue
            nid, asn = parse_frr_conf(os.path.join(root, fn))
            if nid is None or asn is None:
                continue
            if asn not in as_to_node:
                as_to_node[asn] = nid
    return as_to_node


def load_blocks(blocks_json_path: str) -> List[List[int]]:
    with open(blocks_json_path, "r", encoding="utf-8") as f:
        blocks = json.load(f)
    if not isinstance(blocks, list) or not all(isinstance(g, list) for g in blocks):
        raise ValueError("blocks.json must be a JSON list of lists of node_ids")
    return blocks


def parse_route_line(
    line: str,
) -> Optional[Tuple[Optional[str], Optional[str], List[int]]]:
    """Try to parse a BGP line to (prefix, next_hop, as_path_list)."""
    m = TRAILING_ASPATH_RE.search(line)
    if not m:
        return None
    as_path = [int(x) for x in m.group(1).strip().split()]
    prefix = None
    nh = None
    m_pref = IPV4_PREFIX_RE.search(line)
    if m_pref:
        prefix = m_pref.group(0)
    ips = IPV4_NEXT_HOP_RE.findall(line)
    if ips:
        if prefix and prefix.split("/")[0] in ips:
            ips2 = [ip for ip in ips if ip != prefix.split("/")[0]]
            nh = ips2[0] if ips2 else None
        else:
            nh = ips[0]
    return (prefix, nh, as_path)


def colors_for_path(
    as_path: List[int], as_to_node: Dict[int, int], blocks: List[List[int]]
) -> Tuple[List[str], List[str], int]:
    """Return (raw_colors, minimized_colors, min_segments)."""
    color_of_node: Dict[int, int] = {}
    for ci, group in enumerate(blocks):
        for nid in group:
            color_of_node[nid] = ci
    wildcard_color_index = len(blocks) - 1 if blocks else -1

    raw_colors: List[str] = []
    marks: List[str] = []  # 'W' for wildcard, 'U<asn>' for unknown, or 'Ck'

    for asn in as_path:
        nid = as_to_node.get(asn)
        if nid is None:
            c = f"U{asn}"
            raw_colors.append(c)
            marks.append(c)
            continue
        ci = color_of_node.get(nid)
        if ci is None:
            c = f"U{asn}"
            raw_colors.append(c)
            marks.append(c)
            continue
        if ci == wildcard_color_index:
            raw_colors.append(f"W({asn})")
            marks.append("W")
        else:
            raw_colors.append(f"C{ci}")
            marks.append(f"C{ci}")

    filled = marks[:]
    n = len(filled)
    i = 0
    while i < n:
        if filled[i] != "W":
            i += 1
            continue
        j = i
        while j < n and filled[j] == "W":
            j += 1
        left = filled[i - 1] if i - 1 >= 0 and filled[i - 1] != "W" else None
        right = filled[j] if j < n and filled[j] != "W" else None
        if left is not None:
            fill = left
        elif right is not None:
            fill = right
        else:
            fill = "C*"
        for k in range(i, j):
            filled[k] = fill
        i = j

    segments = 0
    prev = None
    minimized_labels: List[str] = []
    for lab in filled:
        if lab != prev:
            segments += 1
            prev = lab
        minimized_labels.append(lab)

    return raw_colors, minimized_labels, segments


def compute_segments_from_strings(
    route_table_text: str, as_to_node: Dict[int, int], blocks: List[List[int]]
):
    import pandas as pd

    rows = []
    for line in route_table_text.splitlines():
        parsed = parse_route_line(line)
        if not parsed:
            continue
        prefix, nh, path = parsed
        raw_colors, min_colors, segs = colors_for_path(path, as_to_node, blocks)
        rows.append(
            {
                "prefix": prefix,
                "next_hop": nh,
                "as_path": " ".join(map(str, path)),
                "colors": " ".join(raw_colors),
                "colors_minimized": " ".join(min_colors),
                "segments_min": segs,
            }
        )
    df = pd.DataFrame(
        rows,
        columns=[
            "prefix",
            "next_hop",
            "as_path",
            "colors",
            "colors_minimized",
            "segments_min",
        ],
    )
    return df


def compute_segments(
    routes_path: str, blocks_json_path: str, frr_dir: Optional[str] = None
):
    if frr_dir is None:
        raise ValueError(
            "frr_dir is required to derive AS->node_id mapping from configs"
        )
    as_to_node = build_as_to_nodeid(frr_dir)
    blocks = load_blocks(blocks_json_path)
    with open(routes_path, "r", encoding="utf-8", errors="ignore") as f:
        route_text = f.read()
    df = compute_segments_from_strings(route_text, as_to_node, blocks)
    return df


def main():
    parser = argparse.ArgumentParser(
        description="Compute minimal color segments for FRR AS_PATHs with wildcard final block."
    )
    parser.add_argument(
        "--frr-dir", required=True, help="Directory containing FRR config files"
    )
    parser.add_argument(
        "--blocks",
        required=True,
        help="Path to blocks JSON (list of list of node_ids; last list is wildcard)",
    )
    parser.add_argument(
        "--routes",
        required=True,
        help="Directory containing bgp_routes-*.log files (structured as emu-real-<node_id>/)",
    )
    parser.add_argument(
        "--out", default="/mnt/data/aspath_segments.csv", help="Output CSV path"
    )
    args = parser.parse_args()

    as_to_node = build_as_to_nodeid(args.frr_dir)
    blocks = load_blocks(args.blocks)

    all_dfs = []
    for path, node_id, num in collect_route_files(args.routes):
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()
        df = compute_segments_from_strings(text, as_to_node, blocks)
        if not df.empty:
            df.insert(0, "log_num", num)
            df.insert(0, "node_id", node_id)
            all_dfs.append(df)

    if all_dfs:
        final_df = pd.concat(all_dfs, ignore_index=True)
    else:
        final_df = pd.DataFrame(
            columns=[
                "node_id",
                "prefix",
                "next_hop",
                "as_path",
                "colors",
                "colors_minimized",
                "segments_min",
            ]
        )

    final_df.to_csv(args.out, index=False)
    print(f"Saved: {args.out}")

    if not final_df.empty:
        max_segments = final_df["segments_min"].max()
        max_row = final_df.loc[final_df["segments_min"].idxmax()]
        print(f"Max segments_min = {max_segments}")
        print("Row with max segments_min:")
        print(max_row.to_dict())

        # Find all historical records for node_id + prefix, sorted by log_num
        node_id = max_row["node_id"]
        prefix = max_row["prefix"]
        history = final_df[
            (final_df["node_id"] == node_id) & (final_df["prefix"] == prefix)
        ]
        history = history.sort_values("log_num")

        print(f"\nHistory of node_id={node_id}, prefix={prefix}:")
        for _, row in history.iterrows():
            print(
                {
                    "log_num": row["log_num"],
                    "as_path": row["as_path"],
                    "colors": row["colors"],
                    "colors_minimized": row["colors_minimized"],
                    "segments_min": row["segments_min"],
                }
            )

        # Collect all segment counts
        segs = final_df["segments_min"].values
        segs_sorted = np.sort(segs)
        cdf = np.arange(1, len(segs_sorted) + 1) / len(segs_sorted)

        plt.figure(figsize=(6, 4))
        plt.plot(segs_sorted, cdf, marker=".", linestyle="-")
        plt.xlabel("segments_min")
        plt.ylabel("CDF")
        plt.title("CDF of minimal segments")
        plt.grid(True, linestyle="--", alpha=0.5)
        out_png = os.path.splitext(args.out)[0] + "_cdf.png"
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        print(f"CDF plot saved: {out_png}")
    else:
        print("No valid AS_PATH entries found.")


if __name__ == "__main__":
    main()
