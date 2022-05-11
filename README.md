# ivsc-driver

This repository supports Intel Vision Sensing Controller(IVSC) on Intel Alder Lake platforms.


## Build instructions:
Three ways are available:
  1. build with kernel source tree
  2. build out of kernel source tree
  3. build with dkms

### build with kernel source tree
* Tested with kernel 5.12-rc4 and 5.13-rc5
* Check out kernel
* Copy repo content to kernel source
* Modify related Kconfig and Makefile

* Add to drivers/mfd/Kconfig
```
config MFD_LJCA
        tristate "Intel La Jolla Cove Adapter support"
        select MFD_CORE
        depends on USB
        help
          This adds support for Intel La Jolla Cove USB-I2C/SPI/GPIO
          Adapter (LJCA). Additional drivers such as I2C_LJCA,
          GPIO_LJCA, etc. must be enabled in order to use the
          functionality of the device.
```
* add to drivers/mfd/Makefile
```
obj-$(CONFIG_MFD_LJCA) += ljca.o
```

* Add to drivers/spi/Kconfig
```
config SPI_LJCA
       tristate "INTEL La Jolla Cove Adapter SPI support"
       depends on MFD_LJCA
       help
          Select this option to enable SPI driver for the INTEL
          La Jolla Cove Adapter (LJCA) board.

          This driver can also be built as a module. If so, the module
          will be called spi-ljca.
```
* Add to drivers/spi/Makefile
```
obj-$(CONFIG_SPI_LJCA) += spi-ljca.o
```

* Add to drivers/gpio/Kconfig
```
config GPIO_LJCA
        tristate "INTEL La Jolla Cove Adapter GPIO support"
        depends on MFD_LJCA

        help
          Select this option to enable GPIO driver for the INTEL
          La Jolla Cove Adapter (LJCA) board.

          This driver can also be built as a module. If so, the module
          will be called gpio-ljca.
```
* Add to drivers/gpio/Makefile
```
obj-$(CONFIG_GPIO_LJCA) += gpio-ljca.o
```

* Add to drivers/i2c/busses/Kconfig
```
config I2C_LJCA
        tristate "I2C functionality of INTEL La Jolla Cove Adapter"
        depends on MFD_LJCA
        help
         If you say yes to this option, I2C functionality support of INTEL
         La Jolla Cove Adapter (LJCA) will be included.

         This driver can also be built as a module.  If so, the module
         will be called i2c-ljca.
```
* Add to drivers/i2c/busses/Makefile
```
obj-$(CONFIG_I2C_LJCA) += i2c-ljca.o
```

* Add to drivers/misc/mei/Kconfig
```
config INTEL_MEI_VSC
        tristate "Intel Vision Sensing Controller device with ME interface"
        select INTEL_MEI
        depends on X86 && SPI
        help
         MEI over SPI for Intel Vision Sensing Controller device
```
* Add to drivers/misc/mei/Makefile
```
obj-$(CONFIG_INTEL_MEI_VSC) += mei-vsc.o
mei-vsc-objs := spi-vsc.o
mei-vsc-objs += hw-vsc.o
```

* Add to drivers/misc/Kconfig
```
source "drivers/misc/ivsc/Kconfig"
```
* Add to drivers/misc/Makefile
```
obj-$(CONFIG_INTEL_VSC) += ivsc/
```

* Enable the following settings in .config
```
CONFIG_MFD_LJCA=m
CONFIG_I2C_LJCA=m
CONFIG_SPI_LJCA=m
CONFIG_GPIO_LJCA=m

CONFIG_INTEL_MEI_VSC=m

CONFIG_INTEL_VSC=m
CONFIG_INTEL_VSC_CSI=m
CONFIG_INTEL_VSC_ACE=m
CONFIG_INTEL_VSC_PSE=m
CONFIG_INTEL_VSC_ACE_DEBUG=m
```

### build out of kernel source tree
* Requires 5.13 or later kernel header installed on compiling machine

* To compile:
```
$cd ivsc-driver
$make -j`nproc`
```

* To install and use modules
```
$sudo make modules_install
$sudo depmod -a
```

### Build with dkms
a dkms.conf file is also provided as an example for building with dkms which can be
used by ```dkms``` ```add```, ```build``` and ```install```.


## Deployment:
ivsc firmware bins should be copied to /lib/firmware/vsc.

And on debugging platform the binaries(e.g. ov01a10 sensor) will be put as below:
```
/lib/firmware/vsc/soc_a1/ivsc_fw_a1.bin
/lib/firmware/vsc/soc_a1/ivsc_pkg_ovti01a0_0_a1.bin
/lib/firmware/vsc/soc_a1/ivsc_skucfg_ovti01a0_0_1_a1.bin
```

And on production platform the binaries(e.g. ov01a10 sensor) will be put as below:
```
/lib/firmware/vsc/soc_a1_prod/ivsc_fw_a1_prod.bin
/lib/firmware/vsc/soc_a1_prod/ivsc_pkg_ovti01a0_0_a1_prod.bin
/lib/firmware/vsc/soc_a1_prod/ivsc_skucfg_ovti01a0_0_1_a1_prod.bin
```
