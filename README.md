# ivsc-driver

This repository supports Intel Vision Sensing Controller(IVSC) on Intel Alder Lake platforms.


## Build instructions:
two ways are available:
- building with kernel source tree
- building out of kernel source tree

### build with kernel source tree
* Tested with kernel 5.12-rc4
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
