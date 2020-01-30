# ART Chroot-Based On-Device Testing

This file documents the use of a chroot environment in on-device testing of the
Android Runtime (ART). Using a chroot allows tests to run a standalone ART from
a locally built source tree on a device running (almost any) system image and
does not interfere with the Runtime installed in the device's system partition.

## Introduction

The Android Runtime (ART) supports testing in a chroot-based environment, by
setting up a chroot directory in a `ART_TEST_CHROOT` directory located under
`/data/local` (e.g. `ART_TEST_CHROOT=/data/local/art-test-chroot`) on a device,
installing ART and all other required artifacts there, and having tests use `adb
shell chroot $ART_TEST_CHROOT <command>` to execute commands on the device
within this environment.

This way to run tests using a "standalone ART" ("guest system") only affects
files in the data partition (the system partition and other partitions are left
untouched) and is as independent as possible from the Android system ("host
system") running on the device. This has some benefits:

* no need to build and flash a whole device to do ART testing (or "overwriting"
  an existing ART by syncing the system partition);
* the possibility to use a smaller AOSP Android manifest
  ([`master-art`](https://android.googlesource.com/platform/manifest/+/refs/heads/master-art/default.xml))
  to build ART and the required dependencies for testing;
* no instability due to updating/replacing ART on the system partition (a
  functional Android Runtime is necessary to properly boot a device);
* the possibility to have several standalone ART instances (one per directory,
  e.g. `/data/local/art-test-chroot1`, `/data/local/art-test-chroot2`, etc.).

Note that using this chroot-based approach requires root access to the device
(i.e. be able to run `adb root` successfully).

## Quick User Guide

0. Unset variables which are not used with the chroot-based approach (if they
   were set previously):
   ```bash
   unset ART_TEST_ANDROID_ROOT
   unset CUSTOM_TARGET_LINKER
   unset ART_TEST_ANDROID_ART_ROOT
   unset ART_TEST_ANDROID_RUNTIME_ROOT
   unset ART_TEST_ANDROID_I18N_ROOT
   unset ART_TEST_ANDROID_TZDATA_ROOT
   ```
1. Set the chroot directory in `ART_TEST_CHROOT`:
    ```bash
    export ART_TEST_CHROOT=/data/local/art-test-chroot
    ```
2. Set lunch target and ADB:
    * With a minimal `aosp/master-art` tree:
        ```bash
        export SOONG_ALLOW_MISSING_DEPENDENCIES=true
        . ./build/envsetup.sh
        lunch armv8-eng  # or arm_krait-eng for 32-bit ARM
        export PATH="$(pwd)/prebuilts/runtime:$PATH"
        export ADB="$ANDROID_BUILD_TOP/prebuilts/runtime/adb"
        ```
    * With a full Android (AOSP) `aosp/master` tree:
        ```bash
        export OVERRIDE_TARGET_FLATTEN_APEX=true
        . ./build/envsetup.sh
        lunch aosp_arm64-eng  # or aosp_arm-eng for 32-bit ARM
        m adb
        ```
3. Build ART and required dependencies:
    ```bash
    art/tools/buildbot-build.sh --target
    ```
4. Clean up the device:
    ```bash
    art/tools/buildbot-cleanup-device.sh
    ```
5. Setup the device (including setting up mount points and files in the chroot directory):
    ```bash
    art/tools/buildbot-setup-device.sh
    ```
6. Populate the chroot tree on the device (including "activating" APEX packages
   in the chroot environment):
    ```bash
    art/tools/buildbot-sync.sh
    ```
7. Run ART gtests:
    ```bash
    art/tools/run-gtests.sh -j4
    ```
    * Note: This currently fails on test
    `test-art-target-gtest-image_space_test{32,64}` when using the full AOSP
    tree (b/119815008).
        * Workaround: Run `m clean-oat-host` before the build step
        (`art/tools/buildbot-build.sh --target`) above.
    * Note: The `-j` option is not honored yet (b/129930445).
    * Specific tests to run can be passed on the command line, specified by
    their absolute paths beginning with `/apex/`.
8. Run ART run-tests:
    * On a 64-bit target:
        ```bash
        art/test/testrunner/testrunner.py --target --64
        ```
    * On a 32-bit target:
        ```bash
        art/test/testrunner/testrunner.py --target --32
        ```
9. Run Libcore tests:
    * On a 64-bit target:
        ```bash
        art/tools/run-libcore-tests.sh --mode=device --variant=X64
        ```
    * On a 32-bit target:
        ```bash
        art/tools/run-libcore-tests.sh --mode=device --variant=X32
        ```
10. Run JDWP tests:
    * On a 64-bit target:
        ```bash
        art/tools/run-jdwp-tests.sh --mode=device --variant=X64
        ```
    * On a 32-bit target:
        ```bash
        art/tools/run-jdwp-tests.sh --mode=device --variant=X32
        ```
11. Tear down device setup:
    ```bash
    art/tools/buildbot-teardown-device.sh
    ```
12. Clean up the device:
    ```bash
    art/tools/buildbot-cleanup-device.sh
    ```
