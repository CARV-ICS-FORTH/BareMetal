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
	DTB_OPT=",dumpdtb=${DTB_PATH_ABS}/riser.dtb"
else
	if [ ! -f ${1} ]; then
		echo "Provided file doesn't exist"
		exit -1
	else
		if [ -f /tmp/riscv-bm-qemu.flash ]; then
			rm /tmp/riscv-bm-qemu.flash
		fi
		dd of=/tmp/riscv-bm-qemu.flash bs=1k count=2048 if=/dev/zero &> /dev/null
		dd of=/tmp/riscv-bm-qemu.flash bs=1k conv=notrunc if=${1} &> /dev/null
		FLASH_OPT="-drive file=/tmp/riscv-bm-qemu.flash,format=raw,if=pflash"
	fi
fi

qemu-system-riscv64 -machine eupilot-vec${DTB_OPT} -serial stdio -nographic -monitor null -s -bios none \
		    -smp 1 -m 2G			\
		    -nic user,id=hnet0${TFTP_OPT}	\
		    -nic user,id=hnet1${TFTP_OPT}	\
		    ${FLASH_OPT}

if [ -f /tmp/riscv-bm-qemu.flash ]; then
	rm /tmp/riscv-bm-qemu.flash
fi
