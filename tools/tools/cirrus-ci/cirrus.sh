#!/bin/sh

set -e

NCPU=$(sysctl -n hw.ncpu)

cat >/etc/make.conf <<EOF
NO_CLEAN=yes
EOF

make -j ${NCPU} TARGET=${TARGET} TARGET_ARCH=${TARGET_ARCH} buildworld
make -j ${NCPU} TARGET=${TARGET} TARGET_ARCH=${TARGET_ARCH} buildkernel
