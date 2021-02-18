# Build and Run ART tests on ARM FVP

This document describes how to build and run an Android system image targeting
the ARM Fixed Virtual Platform and to use it as a target platform for running
ART tests via ADB.

This instruction was checked to be working for the AOSP master tree on
2021-01-13; the up-to-date instruction on how to build the kernel and firmware
could be found here: device/generic/goldfish/fvpbase/README.md.

## Configuring and Building AOSP

First, an AOSP image should be configured and built, including the kernel and
firmware.

### Generating build system configs

```
cd $AOSP

. build/envsetup.sh
# fvp_mini target is used as we don't need a GUI for ART tests.
lunch fvp_mini-eng

# This is expected to fail; it generates all the build rules files.
m
```

### Building the kernel

```
cd $SOME_DIRECTORY_OUTSIDE_AOSP

mkdir android-kernel-mainline
cd android-kernel-mainline
repo init -u https://android.googlesource.com/kernel/manifest -b common-android-mainline
repo sync
BUILD_CONFIG=common/build.config.gki.aarch64 build/build.sh
BUILD_CONFIG=common-modules/virtual-device/build.config.fvp build/build.sh
```

The resulting kernel image and DTB (Device Tree Binary) must then be copied into
the product output directory:

```
cp out/android-mainline/dist/Image $ANDROID_PRODUCT_OUT/kernel
cp out/android-mainline/dist/fvp-base-revc.dtb out/android-mainline/dist/initramfs.img $ANDROID_PRODUCT_OUT/
```

### Building the firmware (ARM Trusted Firmware and U-Boot)

First, install ``dtc``, the device tree compiler. On Debian, this is in the
``device-tree-compiler`` package.

```
sudo apt-get install device-tree-compiler
```

Then run:

```
mkdir platform
cd platform
repo init -u https://git.linaro.org/landing-teams/working/arm/manifest.git -m pinned-uboot.xml -b 20.01
repo sync

# The included copy of U-Boot is incompatible with this version of AOSP, switch to a recent upstream checkout.
cd u-boot
git fetch https://gitlab.denx.de/u-boot/u-boot.git/ master
git checkout 18b9c98024ec89e00a57707f07ff6ada06089d26
cd ..

mkdir -p tools/gcc
cd tools/gcc
wget https://releases.linaro.org/components/toolchain/binaries/6.2-2016.11/aarch64-linux-gnu/gcc-linaro-6.2.1-2016.11-x86_64_aarch64-linux-gnu.tar.xz
tar -xJf gcc-linaro-6.2.1-2016.11-x86_64_aarch64-linux-gnu.tar.xz
cd ../..

build-scripts/build-test-uboot.sh -p fvp all
```

These components must then be copied into the product output directory:

```
cp output/fvp/fvp-uboot/uboot/{bl1,fip}.bin $ANDROID_PRODUCT_OUT/
```

## Setting up the FVP model

### Obtaining the model

The public Arm FVP could be obtained from https://developer.arm.com/; one would
need to create an account there and accept EULA to download and install it.
A link for the latest version:

https://developer.arm.com/tools-and-software/simulation-models/fixed-virtual-platforms/arm-ecosystem-models: "Armv8-A Base RevC AEM FVP"

The AEMv8-A Base Platform FVP is a free of charge Fixed Virtual Platform of the
latest Arm v8-A architecture features and has been validated with compatible
Open Source software, which can be found on the reference open source software
stacks page along with instructions for running the software

### Running the model

From a lunched environment:

```
export MODEL_PATH=/path/to/model/dir
export MODEL_BIN=${MODEL_PATH}/models/Linux64_GCC-6.4/FVP_Base_RevC-2xAEMv8A
./device/generic/goldfish/fvpbase/run_model
```

If any extra parameters are needed for the model (e.g. specifying plugins) they
should be specified as cmdline options for 'run_model'. E.g. to run a model
which support SVE:

```
export SVE_PLUGIN=${MODEL_PATH}/plugins/Linux64_GCC-6.4/ScalableVectorExtension.so
$ ./device/generic/goldfish/fvpbase/run_model --plugin ${SVE_PLUGIN} -C SVE.ScalableVectorExtension.veclen=2
```

Note: SVE vector length is passed in units of 64-bit blocks. So "2" would stand
for 128-bit vector length.

The model will start and will have fully booted to shell in around 20 minutes
(you will see "sys.boot_completed=1" in the log). It can be accessed as a
regular device with adb:

```
adb connect localhost:5555
```

To terminate the model, press ``Ctrl-] Ctrl-D`` to terminate the telnet
connection.

## Running ART test on FVP

The model behaves as a regular adb device so running ART tests could be done using
the standard chroot method described in test/README.chroot.md; the steps are
also described below. A separate AOSP tree (not the one used for the model
itself), should be used - full or minimal.

Then the regular ART testing routine could be performed; the regular "lunch"
target ("armv8" and other targets, not "fvp-eng").


```
export ART_TEST_CHROOT=/data/local/art-test-chroot
export OVERRIDE_TARGET_FLATTEN_APEX=true
export SOONG_ALLOW_MISSING_DEPENDENCIES=true
export TARGET_BUILD_UNBUNDLED=true
export ART_TEST_RUN_ON_ARM_FVP=true

. ./build/envsetup.sh
lunch armv8-userdebug
art/tools/buildbot-build.sh --target

art/tools/buildbot-teardown-device.sh
art/tools/buildbot-cleanup-device.sh
art/tools/buildbot-setup-device.sh
art/tools/buildbot-sync.sh

art/test/testrunner/testrunner.py --target --64 --optimizing -j1

```
