unset LD_PRELOAD CROSS_COMPILE
export PATH=/tools/Xilinx/2025.2/Vitis/gnu/aarch64/lin/aarch64-linux/bin:$PATH
make wrpc wrpc-vuart CC=aarch64-linux-gnu-gcc CROSS_COMPILE_TARGET_HOST=aarch64-linux-gnu-
