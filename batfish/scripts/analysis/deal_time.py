import argparse

if __name__ == "__main__":
    # This program is used to record the end-to-end execution time of Batfish, including initialization, configuration parsing, data plane calculation, and question answering time
    # Finally recorded in the time file
    parser = argparse.ArgumentParser()
    parser.add_argument("-r","--results_dir", type=str, required=True)
    parser.add_argument("-i","--input", type=str, required=True)
    args = parser.parse_args()
    results_dir = args.results_dir
    input = args.input
    with open(input, "r") as f:
        lines = f.readlines()
    assert(len(lines)==3)
    start_time = float(lines[0])
    end_time = float(lines[2])
    with open(results_dir + "/time", "w") as f:
        f.write("Total time: {:.3f}s\n".format(end_time - start_time))