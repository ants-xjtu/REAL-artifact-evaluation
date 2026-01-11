#!/usr/bin/env python3

import argparse
import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import scienceplots


pid2gid = {}
pid2comm = {}
max_gid = 0


def working_set():
    url = "eval/data/working_set.csv"
    df = pd.read_csv(url)

    for group, data in df.groupby("interval"):
        plt.plot(
            data["time"],
            data["iolog_count"],
            label=f"{int(float(group) * 1000)}ms",
        )

    plt.xlabel("Time (Seconds)")
    plt.ylabel("Number of Active Routers")
    plt.ylim(-52, 1350)

    plt.axhline(y=1125, color="black", linestyle="--", linewidth=1.5)
    plt.text(
        x=0,
        y=1200,
        s="Total 1125 Nodes",
        color="black",
        bbox=dict(facecolor="white", alpha=0.6, edgecolor="none", boxstyle="round,pad=0.3"),
    )
    plt.tight_layout()
    return {"loc": "upper right"}


def bg_breakdown():
    df = pd.read_csv("eval/data/real.csv")

    topo_keep = ["KDL", "KDL2", "KDL3", "FT24", "FT28", "FT32"]

    df = df[
        (df["image"] == "frr")
        & (df["mode"] == "baseline")
        & (df["Topo"].isin(topo_keep))
        & (df["Parts"].isna())
    ].copy()

    df["Network-Ready"] = df["bootup"].fillna(0) + df["create_network"].fillna(0)
    df["Converge"] = df["converge"].fillna(0)

    pivot = df.set_index("Topo")[["Converge", "Network-Ready"]]

    pivot = pivot.groupby(level=0).first()
    pivot = pivot.reindex([t for t in topo_keep if t in pivot.index])

    ax = pivot.plot(kind="bar")
    ax.set_yscale("log")
    ax.set_ylim(10,6000)
    ax.tick_params(axis="x", rotation=0)

    plt.xlabel("Topology")
    plt.ylabel("Time (Seconds)")
    plt.tight_layout()


def parse_perf_pernode(filename, pid_file, comm_filter, real_ts_base):
    global max_gid

    row_set = set()
    data = []

    if pid_file:
        with open(pid_file, "r") as f:
            for line in f:
                parts = line.split()
                pid = parts[0]
                gid = int(parts[1].split("-")[-1])
                pid2gid[pid] = gid
                max_gid = max(max_gid, gid)

    cmd_line_pat = r"^(\S.*?)\s+(-?\d+)\/*(\d+)*\s+.*?\s+(\d+\.\d+):"

    with open(filename, "r") as f:
        for line in f:
            res = re.match(cmd_line_pat, line)
            if not res:
                continue

            command = res.group(1)
            if comm_filter and command not in comm_filter:
                command = "other"
            elif not comm_filter:
                command = "other"

            pid = res.group(2)
            timestamp = float(res.group(4))

            if timestamp < real_ts_base:
                continue

            if pid in pid2gid and pid2gid[pid] <= 5:
                gid = pid2gid[pid]
            else:
                continue

            pid2comm[pid] = command
            data.append({"pid": pid, "timestamp": timestamp, "command": command, "gid": gid})
            row_set.add((command, gid))

    return data, row_set


def parse_converge_ts(filename):
    with open(filename, "r") as f:
        lines = f.readlines()
        for line in lines:
            if line.startswith("converge"):
                return float(line.split()[-1])


def eventchart_pernode(
    perf_file,
    pid_file=None,
    comm_filter=None,
    glb_vline_file=None,
    pid_vline_file=None,
    figsize=None,
    xlabel=None,
    title=None,
):
    global pid2gid, pid2comm, max_gid
    pid2gid = {}
    pid2comm = {}
    max_gid = 0

    comm_filter = comm_filter or []
    real_start_ts = parse_converge_ts("eval/data/stage_border_ts")
    data, row_set = parse_perf_pernode(perf_file, pid_file, comm_filter, real_start_ts)

    comm_ind = {comm: i for i, comm in enumerate(sorted(comm_filter))}
    comm_ind["other"] = len(comm_filter)

    def sort_key(r):
        comm, gid = r
        return (gid, comm_ind[comm])

    rows = sorted(list(row_set), key=sort_key, reverse=True)
    r_to_row = {r: i for i, r in enumerate(rows)}

    if not data:
        plt.figure(figsize=figsize)
        return {"nolegend": True}

    timestamps = [d["timestamp"] for d in data]
    base_timestamp = min(timestamps)

    events = [[] for _ in r_to_row]
    for d in data:
        row = r_to_row[(d["command"], d["gid"])]
        events[row].append(d["timestamp"] - base_timestamp)

    if figsize is None:
        _, ax = plt.subplots()
    else:
        _, ax = plt.subplots(figsize=figsize)

    ax.eventplot(events, orientation="horizontal", linewidth=1, linelengths=0.8, color="black")

    pid_vline = {}
    if pid_vline_file:
        with open(pid_vline_file, "r") as f:
            for line in f:
                parts = line.split()
                pid = parts[0]
                key = (pid2comm.get(pid, "other"), pid2gid.get(pid, max_gid + 1))
                if key not in r_to_row:
                    continue
                row = r_to_row[key]
                pid_vline.setdefault(row, [])
                pid_vline[row] += [float(x) - base_timestamp for x in parts[1:]]

        for row, xs in pid_vline.items():
            for x in set(xs):
                ax.vlines(x=x, ymin=row - 0.5, ymax=row + 0.5, color="red", linestyle="-")

    if glb_vline_file:
        with open(glb_vline_file, "r") as f:
            for line in f:
                ts = float(line.split()[1]) - base_timestamp
                ax.axvline(x=ts, color="g", linestyle="--")

    gid_ticks, gid_labels = [], []
    for i in range(len(rows) - 1):
        curr_gid = rows[i][1]
        next_gid = rows[i + 1][1]
        if curr_gid > max_gid:
            break
        if curr_gid != next_gid:
            gid_ticks.append(i)
            gid_labels.append("host" if curr_gid > max_gid else f"{curr_gid}")

    gid_ticks.append(len(rows) - 1)
    gid_labels.append("1")

    plt.tick_params(axis="y", which="both", left=False, right=False, labelleft=False, labelright=False)
    ns_axis = ax.secondary_yaxis("left")
    ns_axis.set_yticks(gid_ticks)
    ns_axis.set_yticklabels(gid_labels)

    ax.axhline(y=-0.5, color="g", linestyle="-")
    ax.axhline(y=len(rows) - 0.5, color="g", linestyle="-")
    plt.ylim(-0.5, len(rows) - 0.5)

    if xlabel is not None:
        ax.set_xlabel(xlabel)
    ax.set_ylabel("Router ID", labelpad=10)
    if title:
        ax.set_title(title)

    plt.tight_layout()
    return {"nolegend": True}


def topo_parts_in(df, configs):
    mask = pd.Series([False] * len(df))
    for item in configs:
        if "/" in item:
            topo, parts = item.split("/")
            parts_num = int(parts)
            item_mask = (df["Topo"] == topo) & (df["Parts"] == parts_num)
        else:
            item_mask = (df["Topo"] == item) & df["Parts"].isna()
        mask = mask | item_mask
    return mask


def grouped_stacked_bar_from_csv(
    csv_url,
    stack_columns,
    row_filter=None,
    group_by="mode",
    group_order=None,
    group_merge=None,
    x_order=None,
    default_value=None,
    hline_y=None,
    hline_label=None,
    xlabel="Configuration",
    ylabel="Value",
    ylim=None,
    title=None,
    log_scale=False,
    figsize=None,
    debug=False,
):
    from matplotlib.patches import Patch

    if isinstance(stack_columns, (list, tuple)):
        stack_columns = {col: [col] for col in stack_columns}
    elif isinstance(stack_columns, dict):
        stack_columns = {
            name: (cols if isinstance(cols, (list, tuple)) else [cols]) for name, cols in stack_columns.items()
        }
    else:
        raise ValueError("stack_columns must be a list/tuple or a dict")

    df = pd.read_csv(csv_url)

    if row_filter is not None:
        if callable(row_filter):
            df = df[row_filter(df)]
        elif isinstance(row_filter, dict):
            for col, val in row_filter.items():
                df = df[df[col].isin(val)] if isinstance(val, list) else df[df[col] == val]
    df = df.reset_index(drop=True)

    if group_merge:
        merge_map = {}
        for new_name, old_names in group_merge.items():
            for old_name in old_names:
                merge_map[old_name] = new_name
        df[group_by] = df[group_by].map(lambda x: merge_map.get(x, x))

    for new_name, cols in stack_columns.items():
        df[new_name] = df[cols].sum(axis=1, skipna=True)

    x_labels = []
    for _, row in df.iterrows():
        topo = str(row["Topo"])
        if pd.notna(row["Parts"]):
            parts_val = row["Parts"]
            if isinstance(parts_val, (int, float)):
                parts = int(parts_val) if float(parts_val).is_integer() else parts_val
            else:
                try:
                    parts_float = float(parts_val)
                    parts = int(parts_float) if parts_float.is_integer() else parts_float
                except Exception:
                    parts = parts_val
            x_labels.append(f"{topo}/{parts}")
        else:
            x_labels.append(topo)
    df["x_label"] = x_labels

    if x_order:
        unique_x_labels = [label for label in x_order if label in df["x_label"].values]
    else:
        unique_x_labels = pd.unique(x_labels).tolist()
    if not unique_x_labels:
        unique_x_labels = pd.unique(x_labels).tolist()

    if default_value is not None:
        for col in stack_columns.keys():
            df[col] = df[col].fillna(default_value)

    pivot = df.pivot_table(index="x_label", columns=group_by, values=list(stack_columns.keys()), aggfunc="first")
    pivot = pivot.swaplevel(axis=1).sort_index(axis=1)

    if group_order:
        available_groups = [g for g in group_order if g in pivot.columns.levels[0]]
        pivot = pivot.reindex(columns=available_groups, level=0)

    if unique_x_labels:
        pivot = pivot.reindex(unique_x_labels)

    valid_rows = []
    for x_label in pivot.index:
        row = pivot.loc[x_label]
        if (pd.notna(row).any()) and (row.fillna(0).sum() != 0):
            valid_rows.append(x_label)
    pivot = pivot.loc[valid_rows]

    if debug:
        print("=== Pivot used for plotting ===")
        print(pivot.fillna("-"))
        print()

    if pivot.empty:
        print("Warning: No valid data to plot")
        plt.figure()
        return {"loc": "upper left"}

    plt.figure(figsize=figsize) if figsize else plt.figure()

    groups = pivot.columns.levels[0].tolist()
    stack_names = list(stack_columns.keys())
    x_positions = np.arange(len(valid_rows))
    max_bars_per_x = len(groups)

    group_colors = {group: f"C{i}" for i, group in enumerate(groups)}
    hatch_patterns = [None, "///", "\\\\", "xx", "---", "|||"]
    hatch_map = {col: hatch_patterns[i % len(hatch_patterns)] for i, col in enumerate(stack_names)}

    bars = {group: None for group in groups}

    for x_idx, x_label in enumerate(valid_rows):
        n_bars_at_x = len(groups)
        if n_bars_at_x <= 0:
            continue
        actual_bar_width = 0.8 / max_bars_per_x
        total_width = actual_bar_width * n_bars_at_x
        start_pos = x_positions[x_idx] - total_width / 2

        for bar_idx, group in enumerate(groups):
            bar_pos = start_pos + bar_idx * actual_bar_width + actual_bar_width / 2
            bottom = 0
            for stack_col in stack_names:
                value = pivot.loc[x_label, (group, stack_col)]
                if pd.notna(value) and value != 0:
                    bar = plt.bar(
                        bar_pos,
                        value,
                        actual_bar_width * 0.9,
                        bottom=bottom,
                        color=group_colors[group],
                        hatch=hatch_map[stack_col],
                        edgecolor="black",
                        linewidth=0.5,
                    )
                    bottom += value
                    if bars[group] is None:
                        bars[group] = bar

    ax = plt.gca()
    ax.set_xticks(x_positions)
    ax.set_xticklabels(valid_rows)

    if log_scale:
        ax.set_yscale("log")

    if hline_y is not None:
        ax.axhline(y=hline_y, color="black", linestyle="--", linewidth=1.5)
        if hline_label:
            ax.text(
                x=ax.get_xlim()[0],
                y=hline_y * 1.02,
                s=hline_label,
                color="black",
                bbox=dict(facecolor="white", alpha=0.6, edgecolor="none", boxstyle="round,pad=0.3"),
            )

    if ylim:
        ax.set_ylim(ylim)
    elif log_scale:
        import math

        ymin, ymax = ax.get_ylim()
        ymin_new = 10 ** math.floor(math.log10(ymin))
        ymax_new = 10 ** (math.ceil(math.log10(ymax)) + 0.5)
        ax.set_ylim(ymin_new, ymax_new)
    else:
        ymin, ymax = ax.get_ylim()
        ax.set_ylim(ymin, ymax * 1.2)

    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if title:
        ax.set_title(title)

    if len(valid_rows) > 5:
        plt.xticks(rotation=45, ha="right")

    plt.tight_layout()

    group_handles, group_labels = [], []
    order = group_order if group_order else groups
    for g in order:
        if bars.get(g) is not None:
            group_handles.append(bars[g][0])
            group_labels.append(g)

    stack_handles = [
        Patch(facecolor="white", hatch=hatch_map[col], edgecolor="black", label=col) for col in stack_names
    ]

    legend_handles = group_handles + stack_handles
    legend_labels = group_labels + stack_names

    return {
        "handles": legend_handles,
        "labels": legend_labels,
        "loc": "upper left",
        "ncol": max(1, len(legend_handles)) / 2,
        "handletextpad": 0.5,
        "columnspacing": 0.72,
    }


def grouped_bar_from_csv_v3(
    csv_url,
    column_name,
    row_filter=None,
    group_by="mode",
    group_names=None,
    group_order=None,
    group_merge=None,
    x_order=None,
    default_value=None,
    xlabel="Configuration",
    ylabel="Value",
    ylim=None,
    title=None,
    log_scale=False,
    figsize=None,
    debug=False,
):
    df = pd.read_csv(csv_url)

    if debug:
        print("=== Raw data check ===")
        print("Rows:", len(df))
        print("Topo unique:", df["Topo"].unique())
        print("Parts unique:", df["Parts"].unique())
        print()

    if row_filter is not None:
        if callable(row_filter):
            df = df[row_filter(df)]
        elif isinstance(row_filter, dict):
            for col, val in row_filter.items():
                df = df[df[col].isin(val)] if isinstance(val, list) else df[df[col] == val]

    if debug:
        print("=== Filtered data ===")
        cols = ["Topo", "Parts", group_by, column_name]
        print(df[cols])
        print()

    df = df.reset_index(drop=True)

    if group_merge:
        merge_map = {}
        for new_name, old_names in group_merge.items():
            for old_name in old_names:
                merge_map[old_name] = new_name
        df[group_by] = df[group_by].map(lambda x: merge_map.get(x, x))

    x_labels = []
    for _, row in df.iterrows():
        topo = str(row["Topo"])
        if pd.notna(row["Parts"]):
            parts_val = row["Parts"]
            if isinstance(parts_val, (int, float)):
                parts = int(parts_val) if float(parts_val).is_integer() else parts_val
            else:
                try:
                    parts_float = float(parts_val)
                    parts = int(parts_float) if parts_float.is_integer() else parts_float
                except Exception:
                    parts = parts_val
            x_labels.append(f"{topo}/{parts}")
        else:
            x_labels.append(topo)
    df["x_label"] = x_labels

    if x_order:
        unique_x_labels = [label for label in x_order if label in df["x_label"].values]
    else:
        unique_x_labels = pd.unique(x_labels).tolist()
    if not unique_x_labels:
        unique_x_labels = pd.unique(x_labels).tolist()

    if default_value is not None:
        df[column_name] = df[column_name].fillna(default_value)

    pivot = df.pivot_table(index="x_label", columns=group_by, values=column_name, aggfunc="first")

    if unique_x_labels:
        pivot = pivot.reindex(unique_x_labels)

    if group_order:
        available_groups = [g for g in group_order if g in pivot.columns]
        pivot = pivot[available_groups]

    if group_names:
        pivot = pivot.rename(columns=group_names)

    valid_rows = []
    for x_label in pivot.index:
        row = pivot.loc[x_label]
        if any(pd.notna(val) and val != 0 for val in row):
            valid_rows.append(x_label)
    pivot = pivot.loc[valid_rows]

    if debug:
        print("=== Pivot used for plotting ===")
        print(pivot.fillna("-"))
        print()

    if pivot.empty:
        print("Warning: No valid data to plot")
        plt.figure()
        return {"loc": "upper left"}

    if figsize is None:
        n_labels = len(valid_rows)
        width = max(10, min(16, n_labels * 0.8))
        figsize = (width, 6)

    plt.figure(figsize=figsize)

    groups = pivot.columns.tolist()
    x_positions = np.arange(len(valid_rows))
    max_bars_per_x = len(groups)

    group_colors = {group: f"C{i}" for i, group in enumerate(groups)}
    bars = {group: None for group in groups}

    for x_idx, x_label in enumerate(valid_rows):
        row = pivot.loc[x_label]
        valid_groups_at_x = [g for g in groups if pd.notna(row[g]) and row[g] != 0]
        valid_values_at_x = [row[g] for g in valid_groups_at_x]

        if not valid_groups_at_x:
            continue

        actual_bar_width = 0.8 / max_bars_per_x
        total_width = actual_bar_width * len(valid_groups_at_x)
        start_pos = x_positions[x_idx] - total_width / 2

        for bar_idx, (group, value) in enumerate(zip(valid_groups_at_x, valid_values_at_x)):
            bar_pos = start_pos + bar_idx * actual_bar_width + actual_bar_width / 2
            bar = plt.bar(bar_pos, value, actual_bar_width * 0.9, color=group_colors[group])
            if bars[group] is None:
                bars[group] = bar

    ax = plt.gca()
    ax.set_xticks(x_positions)
    ax.set_xticklabels(valid_rows)
    ax.tick_params(axis="x", which="both", bottom=False, top=False)

    if log_scale:
        ax.set_yscale("log")

    if ylim:
        ax.set_ylim(ylim)
    elif log_scale:
        import math

        ymin, ymax = ax.get_ylim()
        ymin_new = 10
        ymax_new = 10 ** (math.ceil(math.log10(ymax)) + 0.5)
        ax.set_ylim(ymin_new, ymax_new)
    else:
        ymin, ymax = ax.get_ylim()
        ax.set_ylim(ymin, ymax * 1.2)

    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if title:
        ax.set_title(title)

    if len(valid_rows) > 5:
        plt.xticks(rotation=45, ha="right")

    plt.tight_layout()

    legend_handles, legend_labels = [], []
    order = group_order if group_order else groups
    for g in order:
        if bars.get(g) is not None:
            legend_handles.append(bars[g][0])
            legend_labels.append(g)

    return {
        "handles": legend_handles,
        "labels": legend_labels,
        "loc": "upper left",
        "ncol": len(legend_handles),
        "handletextpad": 0.5,
        "columnspacing": 0.72,
    }


def cdf_latency(
    csv_file="eval/data/buildup_latency.csv",
    time_col="timestamp",
    group_col="conf",
    legend_map=None,
    plot_order=None,
    xlabel="Latency (s)",
    ylabel="CDF",
    title=None,
    figsize=(4, 3),
):
    if legend_map is None:
        legend_map = {
            "two_phase": "With Two-Phase",
            "no_two_phase": "Without Two-Phase",
        }

    df = pd.read_csv(csv_file)
    groups = dict(tuple(df.groupby(group_col)))
    if plot_order is None:
        plot_order = list(groups.keys())

    _, ax = plt.subplots(figsize=figsize)

    for conf in plot_order:
        if conf not in groups:
            continue
        values = np.sort(groups[conf][time_col].values)
        y = np.arange(1, len(values) + 1) / len(values)
        label = legend_map.get(conf, conf)
        ax.plot(values, y, label=label, linewidth=1.8)

    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if title:
        ax.set_title(title)

    plt.tight_layout()
    return {"loc": "lower right"}


def barline_time_llcmiss(
    csv_file,
    group_col="topo",
    category_col="runtime",
    time_col="time",
    miss_col="llcmiss",
    legend_map=None,
    group_order=None,
    category_order=None,
    bar_width=0.35,
    line_width=2.0,
    figsize=None,
    xlabel="Topology",
    ylabel_left="Time (s)",
    ylabel_right="LLC Miss Rate",
    title=None,
):
    df = pd.read_csv(csv_file)

    if legend_map is None:
        legend_map = {c: c for c in df[category_col].unique()}

    if group_order is None:
        group_order = list(df[group_col].unique())
    if category_order is None:
        category_order = list(df[category_col].unique())

    if figsize is None:
        fig, ax1 = plt.subplots()
    else:
        fig, ax1 = plt.subplots(figsize=figsize)
    ax2 = ax1.twinx()

    x = np.arange(len(group_order))
    total_cats = len(category_order)
    offsets = np.linspace(-0.5, 0.5, total_cats, endpoint=False) * bar_width * total_cats

    colors = plt.get_cmap("tab10").colors
    handles, labels = [], []

    for i, cat in enumerate(category_order):
        label = legend_map.get(cat, cat)

        data_time, data_miss = [], []
        for g in group_order:
            subset = df[(df[group_col] == g) & (df[category_col] == cat)]
            if not subset.empty:
                data_time.append(subset[time_col].values[0])
                data_miss.append(subset[miss_col].values[0])
            else:
                data_time.append(0)
                data_miss.append(0)

        b = ax1.bar(x + offsets[i], data_time, width=bar_width, alpha=0.8, label=f"{label} (T)")
        handles.append(b[0])
        labels.append(f"{label} (T)")

        line_color = colors[i % len(colors)]
        l, = ax2.plot(x, data_miss, marker="o", linewidth=line_width, color=line_color, label=f"{label} (M)")
        handles.append(l)
        labels.append(f"{label} (M)")

    ax1.set_xlabel(xlabel)
    ax1.set_ylabel(ylabel_left)
    ax2.set_ylabel(ylabel_right)
    ax1.set_xticks(x)
    ax1.set_xticklabels(group_order)
    ax1.set_ylim(0, 170)
    ax2.set_ylim(0, 1.6)

    if title:
        ax1.set_title(title)

    fig.tight_layout()
    return {
        "handles": handles,
        "labels": labels,
        "loc": "upper left",
        "columnspacing": 0.72,
        "ncol": 2,
    }


def ratio_grouped_line_from_csv(
    csv_url,
    topo="FT30",
    image="frr",
    parts_min=2,
    time_col="time",
    mem_col="global mem",
    xlabel="Parts",
    ylabel="Increased Ratio",
    title=None,
    ylim=[0.05,30],
    log_scale=False,
    figsize=None,
    debug=False,
    dedup_keep="first",
):
    df_all = pd.read_csv(csv_url)

    if "Parts" not in df_all.columns:
        raise ValueError("CSV must contain column: Parts")
    df_all["Parts_num_raw"] = pd.to_numeric(df_all["Parts"], errors="coerce")

    df_all = df_all.drop_duplicates(subset=["Topo", "Parts", "image"], keep=dedup_keep).copy()

    baseline_df = df_all[
        (df_all["Topo"] == topo) &
        (df_all["image"] == image) &
        (df_all["Parts"].isna())
    ].copy()

    if baseline_df.empty:
        raise ValueError(f"Baseline row not found for Topo={topo}, image={image}, Parts is NaN")

    baseline = baseline_df.iloc[0]

    df = df_all[
        (df_all["Topo"] == topo) &
        (df_all["image"] == image) &
        (df_all["Parts_num_raw"].notna()) &
        (df_all["Parts_num_raw"] >= parts_min)
    ].copy()

    if df.empty:
        raise ValueError(f"No data after filtering for Topo={topo}, image={image}, Parts>={parts_min}")

    if time_col not in df_all.columns:
        raise ValueError(f"CSV must contain column: {time_col}")
    if mem_col not in df_all.columns:
        raise ValueError(f"CSV must contain column: {mem_col}")

    base_time = float(baseline[time_col])
    base_mem = float(baseline[mem_col])

    df["time_ratio"] = df[time_col].astype(float) / base_time
    df["mem_ratio"] = df[mem_col].astype(float) / base_mem
    df["Parts_num"] = df["Parts_num_raw"].astype(int)

    baseline_point = {
        "Parts": np.nan,
        "Parts_num": 1,
        "time_ratio": 1.0,
        "mem_ratio": 1.0,
    }
    df_plot = pd.concat([pd.DataFrame([baseline_point]), df[["Parts", "Parts_num", "time_ratio", "mem_ratio"]]], ignore_index=True)
    df_plot = df_plot.sort_values("Parts_num")

    if figsize is None:
        figsize = (6, 4)
    _, ax = plt.subplots(figsize=figsize)

    line1, = ax.plot(df_plot["Parts_num"], df_plot["time_ratio"], "-o", label="Time")
    line2, = ax.plot(df_plot["Parts_num"], df_plot["mem_ratio"], "-s", label="Memory")

    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if title:
        ax.set_title(title)
    if ylim:
        ax.set_ylim(ylim)
    if log_scale:
        ax.set_yscale("log")

    ax.axhline(1, linestyle="--", linewidth=1)

    if debug:
        print("=== Baseline ===")
        print(baseline_df[[ "id", "Topo", "Parts", "image", time_col, mem_col ]].to_string(index=False))
        print("\n=== Ratio data preview ===")
        print(df_plot[["Parts_num", "time_ratio", "mem_ratio"]].to_string(index=False))
        print()

    return {
        "handles": [line1, line2],
        "labels": ["Time", "Memory"],
        "loc": "center right",
        "ncol": 1,
        "handletextpad": 0.5,
        "columnspacing": 0.72,
    }



def make_plot(func, file_name):
    plt.style.use(["science", "grid", "no-latex"])
    import matplotlib as mpl
    from cycler import cycler

    vivid_colors = [
        "#1F77B4",
        "#FF7F0E",
        "#2CA02C",
        "#D62728",
        "#9467BD",
        "#8C564B",
        "#E377C2",
        "#7F7F7F",
    ]

    old_cycle = mpl.rcParams["axes.prop_cycle"]
    markers = None
    for c in old_cycle:
        if "marker" in c:
            markers = [d["marker"] for d in old_cycle]
            break

    new_cycle = cycler(color=vivid_colors) + cycler(marker=markers) if markers else cycler(color=vivid_colors)

    mpl.rcParams.update(
        {
            "font.family": "sans-serif",
            "font.sans-serif": [
                "Lucida Sans Unicode",
                "DejaVu Serif",
                "Times New Roman",
                "Nimbus Roman",
            ],
        }
    )
    mpl.rcParams["axes.prop_cycle"] = new_cycle

    Path(file_name).parent.mkdir(parents=True, exist_ok=True)

    _ = plt.figure()
    ax = plt.gca()

    legend_kwargs = func() or {}
    if "nolegend" not in legend_kwargs:
        legend = plt.legend(fancybox=False, edgecolor="black", **legend_kwargs)
        legend.get_frame().set_linewidth(0.5)

    width = 0.5
    ax.spines["left"].set_linewidth(width)
    ax.spines["bottom"].set_linewidth(width)
    ax.spines["right"].set_linewidth(width)
    ax.spines["top"].set_linewidth(width)
    ax.tick_params(width=width)

    plt.savefig(file_name)
    plt.close()


topo_list = {
    "frr": [
        "KDL",
        "KDL2",
        "KDL3",
        "KDL4/4",
        "FT24",
        "FT28",
        "FT28/4",
        "FT32/8",
        "FT36/9",
        "FT40/10",
        "FT44/11",
    ]
    + [f"FT{i}/{int(i/2)}" for i in [40, 44, 48, 52]],
    "bird": [
        "KDL",
        "KDL3",
        "KDL4/4",
        "FT24",
        "FT28",
        "FT32",
        "FT36",
        "FT40",
        "FT40/4",
        "FT44/11",
        "FT48/12",
        "FT52/13",
    ]
    + [f"FT{i}/{int(i/2)}" for i in [36, 40, 44, 48, 52]],
    "crpd": [
        "KDL",
        "FT16",
        "FT24",
        "FT28/4",
    ]
    + [f"FT{i}/{int(i/2)}" for i in [36, 40, 44, 48, 52]],
}


def e2e(images="frr", yvalue="time", x_label=None, y_label="Total Time (s)", ylim=None, log=True):
    return grouped_bar_from_csv_v3(
        csv_url="eval/data/real.csv",
        column_name=yvalue,
        row_filter=lambda df: (df["image"] == images) & topo_parts_in(df, topo_list[images]),
        group_by="mode",
        group_merge={
            "REAL": ["preload", "preload-partitioned"],
            "Batfish": ["batfish"],
            "Default": ["baseline"],
        },
        group_order=["REAL", "Batfish", "Default"],
        x_order=topo_list[images],
        figsize=(3.84, 2.16),
        xlabel=x_label,
        ylabel=y_label,
        ylim=ylim,
        log_scale=log,
        debug=True,
    )


small_topolist = ["KDL", "KDL2", "KDL3", "FT24", "FT28", "FT32", "FT36"]


def breakdown_stacked(images="frr", x_label=None, y_label="Time Cost (Seconds)", ylim=None, log=True):
    return grouped_stacked_bar_from_csv(
        csv_url="eval/data/real.csv",
        stack_columns={
            "Converge": ["converge"],
            "NetworkReady": ["bootup", "create_network"],
        },
        row_filter=lambda df: (df["image"] == images) & topo_parts_in(df, small_topolist),
        group_by="mode",
        group_merge={
            "REAL": ["preload", "preload-partitioned"],
            "Default": ["baseline"],
        },
        group_order=["REAL", "Default"],
        x_order=small_topolist,
        figsize=(3.84, 2.16),
        xlabel=x_label,
        ylabel=y_label,
        ylim=ylim,
        log_scale=log,
        debug=True,
    )


def e2e_time_mem_ratio(topo):
    return ratio_grouped_line_from_csv(
        csv_url="eval/data/real.csv",
        topo=topo,
        time_col="time",
        mem_col="global mem",
        ylabel="Increased/Decreased Ratio",
        xlabel="Number of Partitions",
        log_scale=True,
        title=None,
        figsize=(3.5, 2.8),
        debug=True,
    )


def barline_simple(
    csv_file,
    x_col="topo",
    bar_col="time",
    line_col="memory",
    xlabel="Topology",
    ylabel_left="Time (s)",
    ylabel_right="Memory (GB)",
    bar_width=0.6,
    line_width=2.0,
    figsize=(5, 3),
    title=None,
):
    df = pd.read_csv(csv_file)
    x = np.arange(len(df[x_col]))

    fig, ax1 = plt.subplots(figsize=figsize)
    ax2 = ax1.twinx()

    bars = ax1.bar(x, df[bar_col], width=bar_width, alpha=0.8, color="C0", label=ylabel_left)
    line, = ax2.plot(x, df[line_col], "-o", linewidth=line_width, color="C1", label=ylabel_right)

    ax1.set_xticks(x)
    ax1.set_xticklabels(df[x_col], rotation=30, ha="right")
    ax1.tick_params(axis="x", which="both", bottom=False, top=False)

    ax1.set_xlabel(xlabel)
    ax1.set_ylabel(ylabel_left)
    ax2.set_ylabel(ylabel_right)

    if title:
        ax1.set_title(title)

    fig.tight_layout()
    return {"handles": [bars[0], line], "labels": [ylabel_left, ylabel_right], "loc": "upper left"}


def build_jobs():
    return {
        "fig1": ("eval/figures/fig1.pdf", bg_breakdown),
        "fig2": (
            "eval/figures/fig2.pdf",
            lambda: eventchart_pernode(
                "eval/data/converge.perf",
                pid_file="eval/data/pid_to_dockername",
                comm_filter=[],
                title=None,
            ),
        ),
        "fig3": ("eval/figures/fig3.pdf", working_set),
        "fig9a": ("eval/figures/fig9a.pdf", lambda: e2e(images="frr", yvalue="time")),
        "fig9b": ("eval/figures/fig9b.pdf", lambda: e2e(images="bird", yvalue="time")),
        "fig9c": ("eval/figures/fig9c.pdf", lambda: e2e(images="crpd", yvalue="time", log=True)),
        "fig9d": ("eval/figures/fig9d.pdf", lambda: e2e(images="frr", yvalue="global mem", log=False, y_label="Peak Memory (GB)")),
        "fig9e": ("eval/figures/fig9e.pdf", lambda: e2e(images="bird", yvalue="global mem", log=False, y_label="Peak Memory (GB)")),
        "fig9f": ("eval/figures/fig9f.pdf", lambda: e2e(images="crpd", yvalue="global mem", log=False, y_label="Peak Memory (GB)")),
        "fig10": ("eval/figures/fig10.pdf", lambda: breakdown_stacked(images="frr")),
        "fig11a": (
            "eval/figures/fig11a.pdf",
            lambda: cdf_latency(
                csv_file="eval/data/ft32-cdf.csv",
                plot_order=["two-phase", "no-two-phase"],
                legend_map={"two-phase": "Two-Phase", "no-two-phase": "W/O Two-Phase"},
                figsize=(3.2, 2),
            ),
        ),
        "fig11b": (
            "eval/figures/fig11b.pdf",
            lambda: cdf_latency(
                csv_file="eval/data/ft28-cdf.csv",
                plot_order=["two-phase", "no-two-phase"],
                legend_map={"two-phase": "Two-Phase", "no-two-phase": "W/O Two-Phase"},
                figsize=(3.2, 2),
            ),
        ),
        "fig12a": (
            "eval/figures/fig12a.pdf",
            lambda: eventchart_pernode(
                "eval/data/r2i_no2phase_converge.perf",
                pid_file="eval/data/r2i_no2phase_pid_to_dockername",
                comm_filter=[],
                real_start_ts=46925.0,
                figsize=(3.5, 2.8),
                xlabel=None,
                title=None,
            ),
        ),
        "fig12b": (
            "eval/figures/fig12b.pdf",
            lambda: eventchart_pernode(
                "eval/data/r2i_converge.perf",
                pid_file="eval/data/r2i_pid_to_dockername",
                comm_filter=[],
                real_start_ts=69365.0,
                figsize=(3.5, 2.8),
                xlabel=None,
                title=None,
            ),
        ),
        "fig13": (
            "eval/figures/fig13.pdf",
            lambda: barline_time_llcmiss(
                csv_file="eval/data/two-phase.csv",
                figsize=(4.2, 3.2 * 3 / 4.7),
                group_order=["FT24", "FT28", "FT32", "FT36"],
                category_order=["r2i", "nor2i"],
                legend_map={"nor2i": "W/O", "r2i": "W"},
            ),
        ),
        "fig14": ("eval/figures/fig14.pdf", lambda: e2e_time_mem_ratio("FT30")),
        "fig15": ("eval/figures/fig15.pdf", lambda: barline_simple(csv_file="eval/data/ultra-large.csv", figsize=(3.5, 2.8))),
    }


def main():
    jobs = build_jobs()

    parser = argparse.ArgumentParser(description="Generate figures.")
    parser.add_argument(
        "--plot",
        choices=sorted(jobs.keys()),
        default=None,
        help="Generate only the specified figure key. If omitted, generate all figures.",
    )
    args = parser.parse_args()

    if args.plot:
        out, fn = jobs[args.plot]
        make_plot(func=fn, file_name=out)
    else:
        for key in sorted(jobs.keys()):
            out, fn = jobs[key]
            make_plot(func=fn, file_name=out)


if __name__ == "__main__":
    main()
