# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022 Canonical Ltd.

obj-$(CONFIG_INTEL_MEI_VSC) += mei-vsc.o
mei-vsc-objs := spi-vsc.o
mei-vsc-objs += hw-vsc.o

ccflags-y += -I$(src)/../../../backport-include/drivers/misc/mei
