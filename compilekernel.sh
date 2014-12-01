#!/bin/bash
(./build_kernel.sh 2>&1) | tee ./LOG-$1.txt 
