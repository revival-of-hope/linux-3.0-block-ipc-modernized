#!/usr/bin/env bash
set -euxo pipefail

# This script intentionally performs no package-manager/network setup.
# The workflow runs it inside Docker Official Image gcc:4.8.5, which already
# provides the compiler toolchain needed for this Linux 3.0 object proof.
# Avoid reviving Ubuntu 14.04 apt repositories: those archives are external
# infrastructure and are not part of the code-under-test.

required_tools=(gcc ld make perl awk sed grep tee)
for tool in "${required_tools[@]}"; do
  command -v "$tool" >/dev/null
 done

echo '=== proof toolchain ==='
gcc --version
ld --version | head -n 1
make --version | head -n 1
perl --version | head -n 2

# Linux 3.0 dispatches to compiler-gcc${__GNUC__}.h. Keep the proof on GCC
# 4.8.x instead of broadening this project into a compiler-abstraction port.
gcc_version="$(gcc -dumpversion)"
case "$gcc_version" in
  4.8*) ;;
  *)
    echo "error: expected GCC 4.8.x, got $gcc_version" >&2
    exit 2
    ;;
esac

cd /work/linux-3.0
make mrproper
make defconfig
make -j2 prepare scripts

set -o pipefail
make -j2 V=1 \
  block/blk-tag.o \
  block/blk-sysfs.o \
  block/genhd.o \
  block/scsi_ioctl.o \
  ipc/alloc.o \
  ipc/mqueue.o \
  ipc/msg.o \
  ipc/msgutil.o \
  ipc/sem.o \
  ipc/shm.o \
  ipc/util.o 2>&1 | tee /work/changed-objects-build.log
