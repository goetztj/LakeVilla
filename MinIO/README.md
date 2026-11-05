# MinIO

The `minio/minio` and `minio/mc` images were removed from Docker Hub. This
directory builds both from the upstream sources at fixed release tags
(see `Dockerfile`):

- target `minio`: server `RELEASE.2025-09-07T16-13-09Z` (the last release
  published as an image; used for the experiments)
- target `mc`: client `RELEASE.2025-08-13T08-35-41Z`

`docker compose build minio mc` builds them (the Go build takes a few
minutes). Other releases: `docker compose build --build-arg
MINIO_RELEASE=<tag> minio`.

Any replacement object store must support conditional writes
(`If-None-Match: *` on `PutObject`); LakeVilla's markers and the global-log
lock depend on them.
