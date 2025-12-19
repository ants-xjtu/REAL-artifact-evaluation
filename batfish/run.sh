#!/bin/bash

single_run(){
	# This function is used to run an experiment
	# INPUT:
	#	$1: indicate the core number. eg. 1/2/4/8
	#	$2: indicate image. eg. frr
	#	$3: indicate the topo name. eg. fattree10
	set -x
	cores=$1
	local image=$2
	local topo_name=$3
	local topo_type=$4
	local topo_id=$5
	
	# Validate if the specified core number is supported
	list=(1 2 4 8 16 32 64)
	if [[ " ${list[*]} " =~ "$1 " ]]; then
		echo "Core count $1 is supported."
	else
		echo "Error: Core count '$1' is not supported." >&2
		echo "Supported values are: ${list[*]}" >&2
		exit 1
	fi
	
	# Validate if the config directory exists
	config_dir=./conf/${image}/${topo_name}
	if [ ! -d $config_dir ]; then
		echo "Config directory $config_dir not found"
		exit 1
	fi

	absolute_path=$(pwd)
	timestamp=$(date +%Y-%m-%d_%H-%M-%S)
	results_dir=${absolute_path}/results/batfish_${topo_name}_${image}_${cores}_${timestamp}
	mkdir -p $results_dir

	exec >> ${results_dir}/run.cmd 2>&1

	echo "topo=${topo_name}" >> ${results_dir}/meta.txt
	echo "topo_id=${topo_id}" >> ${results_dir}/meta.txt
	echo "topo_type=${topo_type}" >> ${results_dir}/meta.txt
	echo "mode=batfish" >> ${results_dir}/meta.txt
	echo "image=${image}" >> ${results_dir}/meta.txt
	echo "partitioned=False" >> ${results_dir}/meta.txt

	# Kill the previous docker container if it exists
	if [ "$(docker ps -q -f name=batfish)" ]; then
		docker rm -f batfish
	fi

	core_index=$(($(($1))-1))
	# Run batfish in docker
	docker run -d --name batfish --cpuset-cpus="0-${core_index}" -v batfish-data:/data -p 8888:8888 -p 9997:9997 -p 9996:9996 -e JAVA_TOOL_OPTIONS="-Xcomp -Xss1g -Xmx256g -Djava.util.concurrent.ForkJoinPool.common.parallelism=${cores}" batfish/allinone
	# Wait for batfish to be ready
	sleep 40

	docker exec batfish bash -c "taskset -cap 0-${core_index} \$(pgrep java)"

	# Monitor cpu and memory
	./scripts/runtime/monitor_cpu.sh $results_dir $core_index &
	monitor_cpu_pid=$!
	./scripts/runtime/monitor_memory.sh $results_dir &
	monitor_mem_pid=$!

	# Record the time of the experiment
	output=$results_dir/record_time 
	http_proxy="" all_proxy="" python3 scripts/runtime/batfish_verify.py -topology $config_dir -record $output

	# Log the batfish container logs
	docker logs batfish > $results_dir/batfish.log 2>&1

	# Kill the monitor processes
	kill $monitor_cpu_pid
	kill $monitor_mem_pid
	docker rm -f batfish

	# Analyze the memory usage
	python3 scripts/analysis/monitor_stats.py -r $results_dir -t memory
	# Analyze the cpu usage
	python3 scripts/analysis/monitor_stats.py -r $results_dir -t cpu
	# Analyze the time usage
	python3 scripts/analysis/deal_time.py -r $results_dir
}

# Validate if the correct number of arguments is provided
if [ $# -ne 3 ]; then
    echo "Usage: $0 <core> <topo_type> <topo_id>"
    echo "  core: number of cores (1, 2, 4, 8, 16, 32, 64)"
    echo "  topo_type: topology type (fattree, topozoo, dupzoo)"
    echo "  topo_id: topology id"
	echo "  example: $0 64 fattree 10"
	echo "  example: $0 64 topozoo Kdl"
	echo "  example: $0 64 dupzoo Fccn:2"
    exit 1
fi

# Parse the arguments
core=$1
topo_type=$2
topo_id=$3

# Generate the config
python3 ./scripts/config/confgen.py frr $topo_type $topo_id

# Parse the topo name
if [ $topo_type == "fattree" ]; then
    topo_name="fattree${topo_id}"
elif [ $topo_type == "topozoo" ]; then
    topo_name="topozoo_${topo_id}"
elif [ $topo_type == "dupzoo" ]; then
    IFS=':' read -r name_prefix copy <<< "$topo_id"
    topo_name="topozoo_${name_prefix}_dup${copy}"
fi

echo "Running with core=$core, topo_type=$topo_type, topo_id=$topo_id, topo_name=$topo_name"
single_run $core frr $topo_name $topo_type $topo_id