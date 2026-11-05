# syntax=docker/dockerfile:1
#
# LakeVilla benchmark container.
#
# Build argument LV_BINARIES selects the binaries:
#   prebuilt (default)  prebuilt binaries for the target architecture:
#                       amd64 -> binaries/x86_64 (built from this repository
#                                with ./build_binaries.sh)
#                       arm64 -> binaries/aarch64 if present, otherwise the
#                                original binaries of the paper (binaries/)
#   source              compile LakeVilla from src/ for the target
#                       architecture (unit tests run during the build)
#
# The original aarch64 binaries were linked against Arrow 19/24 (installed
# below) and an unpinned aws-sdk-cpp snapshot; if they fail with the pinned
# SDK, use LV_BINARIES=source.
ARG LV_BINARIES=prebuilt

FROM ubuntu:24.04 AS deps
ARG TARGETARCH
ARG ARROW_VERSION=24.0.0-1
ARG AWS_SDK_VERSION=1.11.880
ENV DEBIAN_FRONTEND=noninteractive

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
      ninja-build \
      ca-certificates \
      lsb-release \
      openjdk-11-jdk \
      maven \
      ssh && \
    apt-get -qq clean && \
    rm -rf /var/lib/apt/lists/*

# architecture independent JAVA_HOME
RUN ln -s "/usr/lib/jvm/java-11-openjdk-$(dpkg --print-architecture)" /usr/lib/jvm/java-11
ENV JAVA_HOME=/usr/lib/jvm/java-11
ENV PATH="$JAVA_HOME/bin:$PATH"

RUN mkdir /LakeVilla
WORKDIR /LakeVilla

COPY setup_ubuntu.sh .
# the original aarch64 binaries additionally need the Arrow 19 runtime
RUN chmod +x setup_ubuntu.sh && \
    if [ "$TARGETARCH" = "arm64" ]; then export ARROW_EXTRA_RUNTIMES=19.0.1-1; fi && \
    ARROW_VERSION="$ARROW_VERSION" AWS_SDK_VERSION="$AWS_SDK_VERSION" \
    AWS_PREFIX=/LakeVilla/aws ./setup_ubuntu.sh && \
    rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# compile LakeVilla (used for LV_BINARIES=source and ./build_binaries.sh)
# ---------------------------------------------------------------------------
FROM deps AS lv-build
COPY src /LakeVilla/src
RUN cmake -S /LakeVilla/src -B /tmp/lv-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DAWSSDK_ROOT_DIR=/LakeVilla/aws \
      -DLV_BUILD_TESTS=ON && \
    cmake --build /tmp/lv-build && \
    ctest --test-dir /tmp/lv-build -L unit --output-on-failure && \
    mkdir -p /out && \
    cp /tmp/lv-build/*-lv /tmp/lv-build/liblakevilla.so /out/

# export target: docker build --target lv-binaries --output <dir> .
FROM scratch AS lv-binaries
COPY --from=lv-build /out/ /

# ---------------------------------------------------------------------------
# binary selection
# ---------------------------------------------------------------------------
FROM deps AS binaries-prebuilt
COPY binaries /tmp/binaries

FROM deps AS binaries-source
COPY --from=lv-build /out /tmp/binaries/source

FROM binaries-${LV_BINARIES}
ARG TARGETARCH
COPY select_binaries.sh /tmp/
RUN sh /tmp/select_binaries.sh /tmp/binaries "$TARGETARCH" /LakeVilla && \
    rm -rf /tmp/binaries /tmp/select_binaries.sh

COPY lvconfig.conf .

COPY additional_workloads ./additional_workloads/
COPY src/YCSB-C/workloads ./YCSB-C/workloads/

RUN mkdir -p CAB/streams CAB/sample
COPY CAB/streams/*.json ./CAB/streams/
COPY CAB/sample/*.parquet ./CAB/sample/

COPY ./congestion /LakeVilla/congestion/

RUN git clone https://github.com/goetztj/YCSB-LH.git

ENTRYPOINT ["tail", "-f", "/dev/null"]
