import argparse
import os
import re

def get_cpu_stats(lines,results_dir):
    # For CPU, it needs to sum the utilization of each CPU to get the overall utilization, then calculate the average and maximum of the overall utilization
    stats = []
    for line in lines:
        parts = re.split(r'\s+', line.strip())
        nums = parts[1:]
        nums = [float(num) for num in nums]
        sum_nums = sum(nums)
        stats.append(sum_nums)
    
    max_value = max(stats)
    avg_value = sum(stats) / len(stats)
    with open(results_dir + "/cpu", "w") as f:
        f.write("max: {:.3f}\n".format(max_value))
        f.write("avg: {:.3f}\n".format(avg_value))

def get_memory_stats(lines,results_dir):
    # For memory, it needs to find the maximum and minimum memory usage and get the difference
    stats = []
    i = 0
    for i in range(0,len(lines),4):
        index = i + 2
        if index >= len(lines):
            break
        line = lines[index]
        parts = re.split(r'\s+', line.strip())
        num = re.search(r'\d+(?:\.\d+)?', parts[2]).group()
        stats.append(float(num))
    max_value = max(stats)
    min_value = min(stats)
    with open(results_dir + "/memory", "w") as f:
        f.write("max: {:.3f}Gi\n".format(max_value - min_value))


if __name__ == "__main__":
    # This program is used for data statistics
    # It has two modes for statistics on CPU and memory
    
    parser = argparse.ArgumentParser()
    parser.add_argument("-r","--results_dir", type=str, required=True)
    parser.add_argument("-t","--type", type=str, required=True)
    args = parser.parse_args()

    results_dir = args.results_dir
    type = args.type

    if type == "cpu":
        file_name = results_dir + "/cpu.log"
    elif type == "memory":
        file_name = results_dir + "/dynmem.log"
    else:
        print("Invalid type")
        exit(1)

    with open(file_name, "r") as f:
        lines = f.readlines()
    lines = lines[1:]
    
    if type == "cpu":
        get_cpu_stats(lines,results_dir)
    elif type == "memory":
        get_memory_stats(lines,results_dir)
    else:
        print("Invalid type")
        exit(1)



    