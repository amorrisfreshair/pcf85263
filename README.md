# pcf85263

The pcf85363 RTC has a driver that exists in kernel version 4.19.94-ti-r42.
This is a stripped down version of that driver for pcf85262 on devices that cannot be easily updated.

Only reading and setting time are supported.

This loosely follows this [guide](https://opencoursehub.cs.sfu.ca/bfraser/grav-cms/cmpt433/guides/files/DriverCreationGuide.pdf) by Brian Fraser.

## Building

### Setting up a linux kernel to build against
The module uses headers from the linux kernel, the instructions below will set up an environment with the correct headers for the kernel version against which the driver can be built.

#### Cloning the kernel
 - Clone the kernel 
    `$ git clone https://github.com/beagleboard/linux.git`
 - Checkout the appropriate branch (in this case version 4.9.78-ti-r94) 
    `$ git checkout tags/4.9.78-ti-r94`
#### Fixing dependency issues
The build script will try to automatically download a cross compilation toolchain from http://rcn-ee.online/builds/jenkins-dl/.
This is a dead link. It's looking for this file `gcc-linaro-6.4.1-2017.11-x86_64_arm-linux-gnueabihf.tar.xz`.
We can download the file directly from [linaro](https://releases.linaro.org/components/toolchain/binaries/6.4-2017.11/arm-linux-gnueabihf/) or use a search engine to find an alternative.
 - After downloading the tarball, extract it in the base directory of the kernel source:
    `$ tar -xf gcc-linaro-6.4.1-2017.11-x86_64_arm-linux-gnueabihf.tar.xz`
 - Install any missing dependencies with your preferred package manager:
    `flex bison rsync gettext libmpc-dev lzop lzma libncurses5-dev:native build-essential libssl-dev:native`
 - Run the build script:
    `./jenkins_build.sh`

#### Building the driver
 - In the MAKEFILE, change the `KERNEL_SOURCE :=` to point at the root directory of the kernel build environment created in the previous step.
 - run `$ make`
 - If there are no errors, a `rtc-pcf85263.ko` object should appear.

## Installing
 - Copy `rtc-pcf85263.ko` into `/lib/modules/4.19.94-ti-442/extra` on the target device
 - Run `# depmod`

## Registering the device with the kernel
At this point the kernel is aware of the driver as a module and will automatically load it when it finds a device with a matching module alias.

There are two ways to achieve this:

 1. Write the i2c address and name of the device to `new_device` on the appropriate i2c bus:
    `# echo pcf85263 0x51 > /sys/class/i2c-dev/i2c-2/device/new_device`

 2. Use a device tree overlay to inform the kernel that there is a device at that address.


If no other rtc is registered with the kernel, the device will probably appear as `/dev/rtc1`. In that case:
 - Read the time with `hwclocl -f /dev/rtc1 -w`
 - Set the time with `hwclocl -f /dev/rtc1 -s`

It may be impossible to set the rtc-pcf85363 as the default system RTC if the kernel was compiled with `CONFIG_RTC_SYSTOHC=y`.
There are options to change the default rtc in `/etc/defaults/hwclock` but these do nothing when `CONFIG_RTC_HCTOSYS_DEVICE` is baked into the kernel.

System time can be set from the rtc with hwclock after boot in this case, and written to rtc on ntp or http time update.
