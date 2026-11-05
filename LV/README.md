# LakeVilla - LV

This directory contains all files used by the ''lakevilla'' docker container:

- **additional_workloads**: all additional YCSB workloads, we defined
- **binaries**: the original aarch64 binaries of the LakeVilla applications used for the paper; `binaries/x86_64` contains x86_64 binaries built from this repository (`build_binaries.sh`). The Dockerfile selects them by architecture (see `select_binaries.sh`).
- **CAB**: samples for the write streams and the generated read streams we used
- **Dockerfile**: The Dockerfile that build the ''lakevilla'' container
- **lvconfig.conf**: The LakeVilla config file. More information down below
- **setup_ubuntu.sh**: A script installing LakeVilla's (pinned) dependencies in the container or on Ubuntu 24.04
- **build_binaries.sh**: builds the binaries in the container environment and stores them in `binaries/<arch>/`

## Building LakeVilla

To build LakeVilla from source (dependencies: `setup_ubuntu.sh`; submodules: `git submodule update --init LV/src/YCSB-C LV/src/rapidjson LV/src/CAB/cab` in the repository root) run:

```
cmake -S <Path to LV source> -B build -G Ninja -DAWSSDK_ROOT_DIR=<Path to AWS SDK install>
cmake --build build
```

See `src/tests/README.md` for the tests.

## Configuring LakeVilla

All LakeVilla configurations are done via the config file. The parameters are:

- HOST: The URI to the object store
- USER: user of the object store
- PASSWD: password for the object store
- AWSREGION: region of the object store
- BUCKET: bucket of all paths (default: warehouse; used by the benchmark drivers and the JNI library)
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

Lines starting with `#` are comments. Please define the above settings before creating the lakevilla container. The file will be accessible as ./lvconfig.conf in the container.