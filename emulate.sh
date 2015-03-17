#!/bin/bash

QEMU_STM32=../qemu_stm32/arm-softmmu/qemu-system-arm

CUR=`dirname $0`

emulate () {
	$QEMU_STM32 \
		-M stm32-p103 \
		-kernel $1 \
		-semihosting \
		-monitor null <&0 1>/dev/null & pid=$!
}

stm32_qemu () {
	emulate $1 <<< "
"
	echo "Modeling STM32 in QEMU..."
	(sleep $2; kill $pid; sleep 1; kill -KILL $pid)& timer=$!
	if ! wait $pid; then
		kill $timer 2>/dev/null
		echo
		echo "Modeling failed to execute in $2 seconds, giving up."
		exit -1
	fi
	kill $timer
}

stm32_qemu $1 5
