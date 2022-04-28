# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022 Canonical Ltd.

export CONFIG_MFD_LJCA = m
obj-$(CONFIG_MFD_LJCA) += drivers/mfd/

export CONFIG_SPI_LJCA = m
obj-$(CONFIG_SPI_LJCA) += drivers/spi/

export CONFIG_GPIO_LJCA = m
obj-$(CONFIG_GPIO_LJCA) += drivers/gpio/

export CONFIG_I2C_LJCA = m
obj-$(CONFIG_I2C_LJCA) += drivers/i2c/busses/

export CONFIG_INTEL_MEI_VSC = m
obj-$(CONFIG_INTEL_MEI_VSC) += drivers/misc/mei/

export CONFIG_INTEL_VSC=m
export CONFIG_INTEL_VSC_CSI=m
export CONFIG_INTEL_VSC_ACE=m
export CONFIG_INTEL_VSC_PSE=m
export CONFIG_INTEL_VSC_ACE_DEBUG=m
obj-$(CONFIG_INTEL_VSC) += drivers/misc/ivsc/

KERNELRELEASE ?= $(shell uname -r)
KERNEL_SRC ?= /lib/modules/$(KERNELRELEASE)/build
MODSRC := $(shell pwd)
subdir-ccflags-y += -I$(src)/include/

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(MODSRC) modules
modules_install:
	$(MAKE) INSTALL_MOD_DIR=/extra -C $(KERNEL_SRC) M=$(MODSRC) modules_install
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(MODSRC) clean
