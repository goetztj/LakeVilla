# An adapted workload for the Yahoo! Cloud System Benchmark (banking version)
#                        
#   Transfer (update)/Deposit (scan) ratio: 50/50
#   Default structure: ID + blance
#   Request distribution: zipfian

recordcount=2
operationcount=200
workload=com.yahoo.ycsb.workloads.CoreWorkload

readallfields=true

readproportion=0
updateproportion=0.5
scanproportion=0.5
insertproportion0

requestdistribution=zipfian

