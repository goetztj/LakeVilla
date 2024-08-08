FROM ubuntu:24.04

RUN apt-get -qq update && \
    apt-get -qq install -y --no-install-recommends \
      sudo \
      curl \
      vim \
      unzip \
      build-essential \
      software-properties-common \
      wget \
      rsync \
      git \
      cmake \
      ssh && \
    apt-get -qq clean && \
    rm -rf /var/lib/apt/lists/*


RUN mkdir /LakeVilla
WORKDIR /LakeVilla

COPY setup_ubuntu.sh .
RUN chmod +x setup_ubuntu.sh
RUN ./setup_ubuntu.sh

COPY lvconfig.conf .
COPY binaries/ycsbc-lv .
COPY binaries/cab-lv .

RUN mkdir additional_workloads
COPY additional_workloads/workloadbanking.spec ./additional_workloads/
COPY additional_workloads/workloadcustom1.spec ./additional_workloads/
COPY additional_workloads/workloadcustom2.spec ./additional_workloads/

RUN mkdir CAB
RUN mkdir CAB/streams

COPY ./CAB/streams/query_stream_0.json ./CAB/streams/
COPY ./CAB/streams/query_stream_1.json ./CAB/streams/
COPY ./CAB/streams/query_stream_2.json ./CAB/streams/
COPY ./CAB/streams/query_stream_3.json ./CAB/streams/
COPY ./CAB/streams/query_stream_4.json ./CAB/streams/
COPY ./CAB/streams/query_stream_5.json ./CAB/streams/
COPY ./CAB/streams/query_stream_6.json ./CAB/streams/
COPY ./CAB/streams/query_stream_7.json ./CAB/streams/
COPY ./CAB/streams/query_stream_8.json ./CAB/streams/
COPY ./CAB/streams/query_stream_9.json ./CAB/streams/
COPY ./CAB/streams/query_stream_10.json ./CAB/streams/
COPY ./CAB/streams/query_stream_11.json ./CAB/streams/
COPY ./CAB/streams/query_stream_12.json ./CAB/streams/
COPY ./CAB/streams/query_stream_13.json ./CAB/streams/
COPY ./CAB/streams/query_stream_14.json ./CAB/streams/
COPY ./CAB/streams/query_stream_15.json ./CAB/streams/


ENTRYPOINT ["/bin/bash"]