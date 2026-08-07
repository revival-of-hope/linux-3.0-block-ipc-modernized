#!/usr/bin/env bash
set -euxo pipefail

cat > /etc/apt/sources.list <<'EOF'
deb http://old-releases.ubuntu.com/ubuntu/ trusty main universe
deb http://old-releases.ubuntu.com/ubuntu/ trusty-updates main universe
deb http://old-releases.ubuntu.com/ubuntu/ trusty-security main universe
EOF

apt-get -o Acquire::Check-Valid-Until=false update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential bc ca-certificates

echo '=== proof toolchain ==='
gcc --version
ld --version | head -n 1

# Linux 3.0 dispatches to compiler-gcc${__GNUC__}.h.  The proof environment
# must therefore stay in the GCC 4.x generation unless the entire old kernel
# compiler abstraction layer is intentionally ported to a newer compiler.
test "$(gcc -dumpversion | cut -d. -f1)" = '4'

cd /work/linux-3.0
make mrproper
make defconfig
make -j2 prepare scripts

set -o pipefail
make -j2 V=1 \
  block/blk-tag.o \
  block/blk-sysfs.o \
  ipc/mqueue.o \
  ipc/sem.o \
  ipc/util.o 2>&1 | tee /work/changed-objects-build.log
