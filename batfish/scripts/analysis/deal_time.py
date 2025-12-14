import argparse
from datetime import datetime

def parse_time(file_path):
    # parse time from batfish.log
    with open(file_path, "r") as f:
        lines = f.readlines()
    start_time = None
    end_time = None
    for line in lines:
        if "coordinator.WorkMgr Beginning snapshot upload to" in line:
            # 2025-12-14 11:27:49,874
            string = " ".join(line.split()[:2])
            start_time = datetime.strptime(string, "%Y-%m-%d %H:%M:%S,%f")
        elif "INFO main.Batfish Finished data plane computation successfully" in line:
            string = " ".join(line.split()[:2])
            end_time = datetime.strptime(string, "%Y-%m-%d %H:%M:%S,%f")
    
    if start_time != None and end_time != None:
        return (end_time - start_time).total_seconds()
    else:
        return None

if __name__ == "__main__":
    # This program is used to record the end-to-end execution time of Batfish, including initialization, configuration parsing, data plane calculation, and question answering time
    # Finally recorded in the time file
    parser = argparse.ArgumentParser()
    parser.add_argument("-r","--results_dir", type=str, required=True)
    args = parser.parse_args()
    results_dir = args.results_dir
    time = parse_time(results_dir + "/batfish.log")
    if time != None:
        with open(results_dir + "/time", "w") as f:
            f.write("Total time: {:.3f}s\n".format(time))
    else:
        print("Failed to parse time from batfish.log")
        exit(1)