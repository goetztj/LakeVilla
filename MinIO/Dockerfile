# MinIO server (target "minio") and client (target "mc") built from source.
#
# The official minio/minio and minio/mc images were removed from Docker Hub.
# RELEASE.2025-09-07T16-13-09Z is the last server release that was published
# as an image (and the one used for the experiments); MinIO supports the
# conditional writes (If-None-Match) LakeVilla relies on.
ARG GO_VERSION=1.24

FROM golang:${GO_VERSION}-bookworm AS build
ARG MINIO_RELEASE=RELEASE.2025-09-07T16-13-09Z
ARG MC_RELEASE=RELEASE.2025-08-13T08-35-41Z
ENV CGO_ENABLED=0 GOFLAGS=-trimpath

RUN git clone --depth 1 --branch ${MINIO_RELEASE} https://github.com/minio/minio.git /src/minio \
 && cd /src/minio \
 && go build -tags kqueue -ldflags "$(go run buildscripts/gen-ldflags.go)" -o /out/minio

RUN git clone --depth 1 --branch ${MC_RELEASE} https://github.com/minio/mc.git /src/mc \
 && cd /src/mc \
 && go build -ldflags "$(go run buildscripts/gen-ldflags.go)" -o /out/mc

FROM debian:bookworm-slim AS base
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*
COPY --from=build /out/mc /usr/bin/mc

FROM base AS minio
COPY --from=build /out/minio /usr/bin/minio
EXPOSE 9000 80
HEALTHCHECK --interval=5s --timeout=3s --retries=30 \
  CMD curl -fsS http://localhost:9000/minio/health/live || exit 1
ENTRYPOINT ["/usr/bin/minio"]
CMD ["server", "/data", "--console-address", ":80"]

FROM base AS mc
ENTRYPOINT ["/usr/bin/mc"]
