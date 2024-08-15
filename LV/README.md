# LakeVilla - LV

This directory contains all files used by the ''lakevilla'' docker container:

- **additional_workloads**: all additional YCSB workloads, we defined
- **binaries**: aarch64 binaries of three LakeVilla applications usable in the docker container ''LakeVilla''
- **CAB**: samples for the write streams and the generated read streams we used
- **Dockerfile**: The Dockerfile that build the ''lakevilla'' container
- **lvconfig.conf**: The LakeVilla config file. More information down below
- **setup_ubuntu.sh**: A script installing LakeVilla's dependencies in the container

## Configuring LakeVilla

All LakeVilla configurations are done via the config file. The parameters are:

- HOST: The URI to the object store
- USER: user of the object store
- PASSWD: password for the object store
- AWSREGION: region of the object store
- PathToMinioTable: Default table to use
- PathToGlobalLog: Path for LakeVilla's global version log (level 2)
- PathToGlobalTmpLog: Shared path for LakeVilla's global version log (level 2)
- PathToYCSBTable: Path of the YCSB table on the object store
- PathToBankingTable0: Path of the banking table (account 0) on the object store
- PathToBankingTable1: Path of the banking table (account 1) on the object store
- LOGTHREADS: Number of threads to use for log parsing. We recommend setting it to 1, as we used that value in the paper. Using more threads is currently under development and might cause bugs or unwanted behavior.
- FRESHRUNS: Number of runs for the freshness benchmark.
- PathToFreshTable: Table for the freshness benchmark.
- CAB_PATH: base path for the CAB benchmark. It should lead to a directory with all the namespaces with 't1', 't2', ...

Please define the above settings before creating the lakevilla container. The file will be accessible as ./lvconfig.conf in the container.