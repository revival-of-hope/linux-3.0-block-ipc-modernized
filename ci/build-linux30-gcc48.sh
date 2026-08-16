#!/usr/bin/env bash
set -euxo pipefail

cat > /etc/apt/sources.list <<'APT'
deb http://old-releases.ubuntu.com/ubuntu/ trusty main universe
deb http://old-releases.ubuntu.com/ubuntu/ trusty-updates main universe
deb http://old-releases.ubuntu.com/ubuntu/ trusty-security main universe
APT

apt-get -o Acquire::Check-Valid-Until=false update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential bc ca-certificates

echo '=== proof toolchain ==='
gcc --version
ld --version | head -n 1

# Linux 3.0 dispatches to compiler-gcc${__GNUC__}.h.  Keep the proof on GCC
# 4.x instead of broadening this project into a compiler-abstraction port.
test "$(gcc -dumpversion | cut -d. -f1)" = '4'

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
  ipc/msgutil.o \
  ipc/sem.o \
  ipc/util.o 2>&1 | tee /work/changed-objects-build.log
