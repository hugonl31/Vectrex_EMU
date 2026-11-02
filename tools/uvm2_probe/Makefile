# Vectrex_EMU UVMC2 probe (WSL): make uvm2 < /dev/null  ->  build_uvm2/uvm2_probe.um2  (then copy to package/system)
UVMC2_KIT   ?= $(HOME)/uvmc2-starter-kit
ARM_TC      ?= $(HOME)/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi
export PATH := $(ARM_TC)/bin:$(HOME)/.cargo/bin:$(HOME)/.local/bin:$(PATH)
export PICO_SDK_PATH ?= $(HOME)/pico-sdk-2.2.0
UVM2_ARM_TOOLCHAIN = $(ARM_TC)/arm-none-eabi
UVM2_SDK    ?= $(UVMC2_KIT)/sdk/uvm2-sdk
RP2350_SDK  ?= $(UVMC2_KIT)/sdk/rp2350-sdk
VPY_C_SDK   ?= $(UVMC2_KIT)/sdk/vpy-c
PITREX_INC  ?= $(UVMC2_KIT)/sdk/pitrex-sim/include
UVM2_NAME    = uvm2_probe
UVM2_SRCS    = src/main.c $(VPY_C_SDK)/vpy.c
UVM2_CC      = arm-none-eabi-gcc
UVM2_CXX     = arm-none-eabi-gcc
UVM2_OBJCOPY = arm-none-eabi-objcopy
UVM2_CFLAGS  = -mthumb -mcpu=cortex-m33 -mfloat-abi=soft -ffreestanding -O2 -std=gnu11 \
               -ffunction-sections -fdata-sections -DVPY_RP2350 -DPROBE $(PROBE_FLAGS) \
               -Isrc -I$(VPY_C_SDK)/include -I$(PITREX_INC) -I$(UVM2_SDK)
UVM2_PIO_STREAM ?= 1
UVM2_HZ ?= 50
include $(UVM2_SDK)/uvm2.mk
clean:
	rm -rf build_uvm2
.PHONY: clean
