# Adapted workload for the Yahoo! Cloud System Benchmark
# Workload Custom2: Insert heavy workload
#                        
#   Read/insert ratio: 50/50
#   Default data size: 1 KB records (10 fields, 100 bytes each, plus key)
#   Request distribution: zipfian

recordcount=1000
operationcount=1000
workload=com.yahoo.ycsb.workloads.CoreWorkload

readallfields=true

readproportion=0.5
updateproportion=0
scanproportion=0
insertproportion=0.5

requestdistribution=zipfian

