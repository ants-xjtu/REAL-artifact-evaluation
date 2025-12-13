import struct
import sys

# Define perf event record types (comprehensive list)
PERF_RECORD_TYPES = {
    0: "PERF_RECORD_MMAP",
    1: "PERF_RECORD_LOST",
    2: "PERF_RECORD_COMM",
    3: "PERF_RECORD_EXIT",
    4: "PERF_RECORD_THROTTLE",
    5: "PERF_RECORD_UNTHROTTLE",
    6: "PERF_RECORD_FORK",
    7: "PERF_RECORD_READ",
    8: "PERF_RECORD_SAMPLE",
    9: "PERF_RECORD_MMAP2",
    10: "PERF_RECORD_AUX",
    11: "PERF_RECORD_ITRACE_START",
    12: "PERF_RECORD_LOST_SAMPLES",
    13: "PERF_RECORD_SWITCH",
    14: "PERF_RECORD_SWITCH_CPU_WIDE",
    15: "PERF_RECORD_NAMESPACES",
    16: "PERF_RECORD_KSYMBOL",
    17: "PERF_RECORD_BPF_EVENT",
    18: "PERF_RECORD_CGROUP",
    19: "PERF_RECORD_TEXT_POKE",
    20: "PERF_RECORD_AUX_OUTPUT_HW_ID",
    21: "PERF_RECORD_THREAD_MAP",
    22: "PERF_RECORD_CPU_MAP",
    23: "PERF_RECORD_STAT_CONFIG",
    24: "PERF_RECORD_STAT",
    25: "PERF_RECORD_STAT_ROUND",
    26: "PERF_RECORD_EVENT_UPDATE",
    27: "PERF_RECORD_TIME_CONV",
    28: "PERF_RECORD_HEADER_FEATURE",
}


def parse_perf_data(input_file, output_file):
    """
    Parse perf.data and filter out records of a specific type.

    :param input_file: Path to the input perf.data file.
    :param output_file: Path to save the filtered perf.data file.
    """
    try:
        with open(input_file, "rb") as infile, open(output_file, "wb") as outfile:
            # Read and parse the perf file header
            header_size = 104  # Adjust if necessary
            perf_header = infile.read(header_size)
            (
                magic,
                size,
                attr_size,
                attrs_offset,
                attrs_size,
                data_offset,
                data_size,
                event_types_offset,
                event_types_size,
                features_bitmap,
            ) = struct.unpack("8sQQQQQQQQ32s", perf_header)
            if size > header_size:
                infile.read(size - header_size)

            print(f"Magic: {magic}, Data Offset: {data_offset}, Data Size: {data_size}")
            outfile.write(perf_header)

            curr = infile.tell()
            attrs = infile.read(attrs_size + attrs_offset - curr)
            outfile.write(attrs)

            current_offset = data_offset
            infile.seek(data_offset)

            # Process the data section
            while current_offset < data_offset + data_size:
                # if (0x1121000 <= current_offset <= 0x1123010):
                #     print(f"offset {current_offset:x}")
                # Read the event header (8 bytes per event header)
                header = infile.read(8)
                if not header:
                    print("not header")
                    break

                # Extract record type and size from the header
                record_type, record_size = struct.unpack("II", header)
                orig_record_size = record_size
                record_size //= 65536

                # if (0x1121000 <= current_offset <= 0x1123010):
                #     print(record_type, record_size)
                # Read the entire record
                if record_size > 8:
                    record = infile.read(record_size - 8)

                    # print(record_type // 65536)
                    if record_type != 17 and record_type != 18:
                        # Write the record if it's not excluded
                        outfile.write(header)
                        outfile.write(record)
                    else:
                        # Adjust the data size and offsets if record is excluded
                        # print(f"Excluded record of type {record_type}")
                        outfile.write(struct.pack("II", 65, orig_record_size))
                        outfile.write(record)
                        # outfile.write(header)
                        # outfile.write(record)
                elif record_size == 8:
                    outfile.write(header)
                else:
                    print("record size: ", record_size, orig_record_size)

                current_offset = infile.tell()

            # Read and copy the remaining part of the file
            remaining_data = infile.read()
            outfile.write(remaining_data)

            infile.seek(event_types_offset)
            event_types = infile.read(event_types_size)
            outfile.write(event_types)

            # Adjust the file header to update data_size
            adjusted_header = struct.pack(
                "8sQQQQQQQQ32s",
                magic,
                size,
                attr_size,
                attrs_offset,
                attrs_size,
                data_offset,
                data_size,
                event_types_offset,
                event_types_size,
                features_bitmap,
            )
            outfile.seek(0)
            outfile.write(adjusted_header)

    except FileNotFoundError:
        print(f"Error: File {input_file} not found.")
        sys.exit(1)
    except Exception as e:
        print(f"An error occurred: {e}")
        sys.exit(1)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Filter records from a perf.data file."
    )
    parser.add_argument("input_file", help="Path to the input perf.data file")
    parser.add_argument("output_file", help="Path to save the filtered perf.data file")

    args = parser.parse_args()

    parse_perf_data(args.input_file, args.output_file)
    print(f"Filtered perf.data saved to {args.output_file}")
