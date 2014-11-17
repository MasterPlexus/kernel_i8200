HOW TO BUILD KERNEL 3.4.5 FOR GT-I8200N_EUR_XX

1. How to Build
	- get Toolchain
			From Android Source Download site( http://source.android.com/source/downloading.html )
			Toolchain is included in Android source code.

	- edit "Makefile"
			edit "CROSS_COMPILE" to right toolchain path(You downloaded).
			Ex) CROSS_COMPILE ?= /opt/toolchains/arm-eabi-4.6/bin/arm-eabi-
			
	- edit "build_kernel.sh"
	        adding "Goleden VE defconfig" configuration
			Ex) cp -f ./arch/arm/configs/pxa986_golden_rev02_defconfig ./kernel_out/.config

	- build 
			$ ./build_kernel.sh

2. Output files
	- Kernel : kernel_out/arch/arm/boot/zImage
	- module : kernel_out/*/*.ko

3. How to make .tar binary for downloading into target.
	- change current directory to Kernel/arch/arm/boot
	- type following command
	$ tar cvf GT-I8200N_EUR_XX_Kernel.tar zImage