#!/bin/sh

set -e

MAKE_PARAMS=
NCPU=$(sysctl -n hw.ncpu)
NEED_XTOOLCHAIN_LIST="riscv64"

cat >/etc/make.conf <<EOF
NO_CLEAN=yes
EOF

for i in ${NEED_XTOOLCHAIN_LIST}
do
	if [ ${i} = ${TARGET_ARCH} ]; then
		pkg install -y ${i}-xtoolchain-gcc
		MAKE_PARAMS="CROSS_TOOLCHAIN=${i}-gcc"
		break
	fi
done

make -j ${NCPU} TARGET=${TARGET} TARGET_ARCH=${TARGET_ARCH} ${MAKE_PARAMS} buildworld
make -j ${NCPU} TARGET=${TARGET} TARGET_ARCH=${TARGET_ARCH} ${MAKE_PARAMS} buildkernel
