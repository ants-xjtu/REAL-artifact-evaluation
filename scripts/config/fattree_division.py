#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Script to partition numbers into groups and save as JSON.

Usage:
    python script.py <k> <n_parts> <path>
"""

import sys
import json
from pathlib import Path


def partition_numbers(k: int, n_parts: int):
    """
    Partition numbers into n_parts + 1 groups.

    Parameters
    ----------
    k : int
        Base parameter.
    n_parts : int
        Number of partitions for the first range.

    Returns
    -------
    list[list[int]]
        Nested list representing the partitioned numbers.
    """
    total = k * k
    base_numbers = list(range(1, total + 1))

    groups = []
    size = total // n_parts
    remainder = total % n_parts

    start = 0
    for i in range(n_parts):
        end = start + size + (1 if i < remainder else 0)
        groups.append(base_numbers[start:end])
        start = end

    extra_start = total + 1
    extra_end = (total * 5) // 4
    if extra_start <= extra_end:
        groups.append(list(range(extra_start, extra_end + 1)))

    return groups


def main():
    if len(sys.argv) != 4:
        print("Usage: python script.py <k> <n_parts> <path>")
        sys.exit(1)

    k = int(sys.argv[1])
    n_parts = int(sys.argv[2])
    out_path = Path(sys.argv[3]).expanduser().resolve()

    partitions = partition_numbers(k, n_parts)

    out_path.mkdir(parents=True, exist_ok=True)
    output_file = out_path / "partition.json"
    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(partitions, f, ensure_ascii=False, indent=2)

    print(f"Partition file written to: {output_file}")


if __name__ == "__main__":
    main()
