import re
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys
import os


def parse_global_memlog(input_file):
    """Parse 'free' output log"""
    times = []
    used_mem = []
    mem_pattern = re.compile(r"Mem:\s+\d+\w+\s+([\d\.]+)(\w+)")
    current_time = None

    with open(input_file, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if re.match(r"^\d{2}:\d{2}:\d{2}$", line):
                current_time = line
            m = mem_pattern.search(line)
            if m and current_time:
                used_val, used_unit = m.groups()
                factor = {"Gi": 1024, "Mi": 1, "Ti": 1024 * 1024}
                used = float(used_val) * factor.get(used_unit, 1) / 1024  # in GB
                times.append(current_time)
                used_mem.append(used)

    return pd.DataFrame({"Time": times, "Global": used_mem})


def parse_controller_memlog(input_file):
    """Parse VmRSS-formatted logs"""
    times = []
    used_mem = []

    with open(input_file, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f if line.strip()]
        for i in range(0, len(lines), 2):
            if i + 1 >= len(lines):
                continue
            time_str = lines[i]
            mem_line = lines[i + 1]
            m = re.search(r"VmRSS:\s+(\d+)\s+kB", mem_line)
            if m:
                kb_val = int(m.group(1))
                gb_val = kb_val / (1024 * 1024)  # kB → GB
                times.append(time_str)
                used_mem.append(gb_val)

    return pd.DataFrame({"Time": times, "Controller": used_mem})


def plot_memlogs(global_file, controller_file, output_csv, output_png):
    df_global = parse_global_memlog(global_file)

    # Check whether controller exists
    if controller_file != "-" and os.path.exists(controller_file):
        df_controller = parse_controller_memlog(controller_file)
        # Align by time
        df = pd.merge(df_global, df_controller, on="Time", how="inner")
    else:
        df = df_global

    # Normalize Global relatively
    baseline = df["Global"].min()
    df["Global"] = df["Global"] - baseline

    # Maximum point
    g_idx = df["Global"].idxmax()
    g_val = df.loc[g_idx, "Global"]
    g_time = df.loc[g_idx, "Time"]

    # Output CSV
    df_out = df[
        ["Time", "Global"] + (["Controller"] if "Controller" in df.columns else [])
    ]
    df_out.to_csv(output_csv, index=False)

    print(f"✅ Data saved to {output_csv}")
    print(f"📊 Global Baseline: {baseline:.2f} GB")
    print(f"📈 Global Maximum relative increase: {g_val:.2f} GB @ {g_time}")

    if "Controller" in df.columns:
        c_idx = df["Controller"].idxmax()
        c_val = df.loc[c_idx, "Controller"]
        c_time = df.loc[c_idx, "Time"]
        print(f"📈 Controller Maximum value: {c_val:.2f} GB @ {c_time}")

    plt.figure(figsize=(12, 6))
    plt.plot(
        df["Time"],
        df["Global"],
        marker="o",
        markersize=3,
        linewidth=1,
        label="Global memory usage",
    )

    # Maximum point
    plt.scatter(g_idx, g_val, color="red", zorder=5)
    plt.text(
        g_idx,
        g_val,
        f"G Max {g_val:.2f} GB\n@{g_time}",
        ha="left",
        va="bottom",
        fontsize=9,
        color="red",
    )

    # controller
    if "Controller" in df.columns:
        plt.plot(
            df["Time"],
            df["Controller"],
            marker="x",
            markersize=3,
            linewidth=1,
            label="Controller memory usage",
        )
        plt.scatter(c_idx, c_val, color="blue", zorder=5)
        plt.text(
            c_idx,
            c_val,
            f"C Max {c_val:.2f} GB\n@{c_time}",
            ha="left",
            va="bottom",
            fontsize=9,
            color="blue",
        )

    step = max(1, len(df) // 10)
    plt.xticks(np.arange(0, len(df), step), df["Time"].iloc[::step], rotation=45)

    plt.xlabel("Time")
    plt.ylabel("Memory (GB)")
    plt.title("Global vs Controller Memory Usage Over Time")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(output_png)
    print(f"🖼️ Image saved as {output_png}")


if __name__ == "__main__":
    plot_memlogs(
        sys.argv[1], sys.argv[2], sys.argv[3] + "/mem.csv", sys.argv[3] + "/mem.png"
    )
