#!/bin/bash

FLASH_OPT=""
DTB_OPT=""
TFTP_OPT=""

if [ -n "${TFTPROOT}" ]; then
	TFTP_OPT=",tftp=${TFTPROOT},bootfile=boot.bin"
fi

if [ -n "${DTB_PATH}" ]; then
	# Resolve DTB_PATH relative to the original working directory
	DTB_PATH_ABS="$(cd "${ORIGINAL_PWD}" && cd "${DTB_PATH}" && pwd)"
	DTB_OPT=",dumpdtb=${DTB_PATH_ABS}/qemu.dtb"
else
	if [ ! -f ${1} ]; then
		echo "Provided file doesn't exist"
		exit -1
	else
		if [ -f /tmp/riscv-bm-qemu.flash ]; then
			rm /tmp/riscv-bm-qemu.flash
		fi
		dd of=/tmp/riscv-bm-qemu.flash bs=1k count=32768 if=/dev/zero &> /dev/null
		dd of=/tmp/riscv-bm-qemu.flash bs=1k conv=notrunc if=${1} &> /dev/null
		FLASH_OPT="-drive file=/tmp/riscv-bm-qemu.flash,format=raw,if=pflash"
	fi
fi

qemu-system-riscv64 -machine virt${DTB_OPT},aia=aplic -serial stdio -nographic -monitor null -s -bios none \
		    -smp 4 -m 2G -global virtio-mmio.force-legacy=false  \
		    -netdev user,id=net0${TFTP_OPT} \
		    -device virtio-net-device,netdev=net0 \
		    ${FLASH_OPT}

if [ -f /tmp/riscv-bm-qemu.flash ]; then
	rm /tmp/riscv-bm-qemu.flash
fi