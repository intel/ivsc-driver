# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022 Intel Corporation.

obj-m += ljca.o
ljca-y := drivers/mfd/ljca.o

obj-m += spi-ljca.o
spi-ljca-y := drivers/spi/spi-ljca.o

obj-m += gpio-ljca.o
gpio-ljca-y := drivers/gpio/gpio-ljca.o

obj-m += i2c-ljca.o
i2c-ljca-y := drivers/i2c/busses/i2c-ljca.o

obj-m += mei-vsc.o
mei-vsc-y := drivers/misc/mei/aux-vsc.o
mei-vsc-y += drivers/misc/mei/hw-vsc.o

obj-m += spi-vsctp.o
spi-vsctp-y := drivers/misc/mei/spi-vsctp.o

obj-m += intel_vsc.o
intel_vsc-y := drivers/misc/ivsc/intel_vsc.o

obj-m += mei_csi.o
mei_csi-y := drivers/misc/ivsc/mei_csi.o

obj-m += mei_ace.o
mei_ace-y := drivers/misc/ivsc/mei_ace.o

obj-m += mei_pse.o
mei_pse-y := drivers/misc/ivsc/mei_pse.o

obj-m += mei_ace_debug.o
mei_ace_debug-y := drivers/misc/ivsc/mei_ace_debug.o

KERNELRELEASE ?= $(shell uname -r)
KERNEL_SRC ?= /lib/modules/$(KERNELRELEASE)/build
PWD := $(shell pwd)

ccflags-y += -I$(src)/include/
ccflags-y += -I$(src)/backport-include/drivers/misc/mei/

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

modules_install:
	$(MAKE) INSTALL_MOD_DIR=/updates -C $(KERNEL_SRC) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
