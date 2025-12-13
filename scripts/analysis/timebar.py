#!/usr/bin/env python3

import sys
import matplotlib.pyplot as plt
import numpy as np


def read_data(file_path):
    with open(file_path, "r") as file:
        lines = file.readlines()
    events = []
    timestamps = []
    for line in lines:
        parts = line.strip().split()
        if len(parts) == 2:
            events.append(parts[0])
            timestamps.append(float(parts[1]))
    return events[:-1], timestamps


def calculate_durations(timestamps):
    durations = [timestamps[i + 1] - timestamps[i] for i in range(len(timestamps) - 1)]
    return durations


def plot_stacked_bar(events, durations, output_path):
    color_pool = [
        ["#fbe5d6", "#fff2cc", "#deebf7", "#e2f0d9"],
        ["#f8cbad", "#ffe699", "#bdd7ee", "#c5e0b4"],
        ["#f4b183", "#ffd966", "#9dc3e6", "#a9d18e"],
        ["#c55a11", "#bf9000", "#2e75b6", "#548235"],
    ]

    # Use the third row of colors from color_pool
    colors = [color_pool[1][i] for i in range(len(durations))]

    # Reverse events and durations for reversed order
    events = events[::-1]
    durations = durations[::-1]

    x = np.arange(1)

    bottom = 0
    for i, duration in enumerate(durations):
        plt.bar(x, duration, bottom=bottom, label=events[i], color=colors[i])
        plt.text(
            x,
            bottom + duration / 2,
            f"{duration:.2f}s",
            ha="center",
            va="center",
            color="black",
        )
        bottom += duration

    plt.xticks([])
    plt.ylabel("Duration (s)")
    plt.title("Emulation duration breakdown")
    handles, labels = plt.gca().get_legend_handles_labels()
    plt.legend(handles[::-1], labels[::-1])
    plt.savefig(output_path)


def main():
    if len(sys.argv) != 3:
        print("Usage: python script.py <file_path> <output_path>")
        sys.exit(1)

    file_path = sys.argv[1]
    output_path = sys.argv[2]
    events, timestamps = read_data(file_path)

    if len(timestamps) < 4:
        print("File must contain at least four events with timestamps.")
        sys.exit(1)

    durations = calculate_durations(timestamps)
    plot_stacked_bar(events, durations, output_path)


if __name__ == "__main__":
    main()
