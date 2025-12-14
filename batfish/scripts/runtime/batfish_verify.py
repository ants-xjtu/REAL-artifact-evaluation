import pandas as pd
from pybatfish.client.session import Session
from pybatfish.datamodel import *
from pybatfish.datamodel.answer import *
from pybatfish.datamodel.flow import *
import time
import argparse

parser = argparse.ArgumentParser(description='Run batfish for different topologies')
parser.add_argument('-topology', type=str, help='topology folder,eg. ./conf/frr/fattree2')
parser.add_argument('-record', type=str, help='the time record file')
args = parser.parse_args() 


topo=args.topology
bf = Session(host="127.0.0.1",port=9996)
# start verification
time1=time.time()

bf.set_network("batfish")
snapshot_name = topo.split("/")[-1]
bf.init_snapshot(topo,name=snapshot_name,overwrite=True)
# parse complete
time2=time.time()

# make sure the topology is converged
result = bf.q.layer3Edges().answer().frame()
time3=time.time()
with open(args.record,"w") as f:
    f.write(str(time1))  # start time
    f.write("\n")
    f.write(str(time2))  # init time
    f.write("\n")
    f.write(str(time3))  # end time