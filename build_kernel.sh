#!/bin/bash

export PATH=/media/dirk/compile/i8200_Kernel:$PATH
mkdir -p ./kernel_out
cp -f ./arch/arm/configs/pxa986_golden_rev02_defconfig ./kernel_out/.config
make -j8 ARCH=arm KBUILD_OUTPUT=./kernel_out oldnoconfig
make -j8 ARCH=arm KBUILD_OUTPUT=./kernel_out
cp -f ./kernel_out/arch/arm/boot/zImage ./arch/arm/boot/
cp -rf ./kernel_out/arch/arm/boot/compressed/vmlinux ./arch/arm/boot/compressed/
cp -rf ./kernel_out/vmlinux ./
