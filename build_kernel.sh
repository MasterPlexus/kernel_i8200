#!/bin/bash

export PATH=/media/dirk/android/kernel/i8200_Kernel:$PATH
make mrproper
mkdir -p ./kernel_out
cp -f ./arch/arm/configs/pxa986_golden_rev02_defconfig ./kernel_out/.config
make -j7 ARCH=arm KBUILD_OUTPUT=./kernel_out oldnoconfig
make -j7 ARCH=arm KBUILD_OUTPUT=./kernel_out
cp -f ./kernel_out/arch/arm/boot/zImage ./arch/arm/boot/
cp -rf ./kernel_out/arch/arm/boot/compressed/vmlinux ./arch/arm/boot/compressed/
cp -rf ./kernel_out/vmlinux ./
cp ./kernel_out/arch/arm/boot/zImage /media/dirk/android/kernel/bootimg/zImage
find ./kernel_out -type f -name *.ko -exec cp {} //media/dirk/android/kernel/bootimg/ramdisk/lib/modules/ \;
