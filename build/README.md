# Building the ART Module

ART is built as a module in the form of an APEX package, `com.android.art.apex`.
That package can be installed with `adb install` on a device running Android S
or later. It is also included in the system partition (in the `system/apex`
directory) of platform releases, to ensure it is always available.

The recommended way to build the ART Module is to use the `master-art` manifest,
which only has the sources and dependencies required for the module.

Currently it is also possible to build ART directly from sources in a platform
build, i.e. as has been the traditional way up until Android S. However that
method is being phased out.

The ART Module is available as a debug variant, `com.android.art.debug.apex`,
which has extra internal consistency checks enabled, and some debug tools. A
device cannot have both the non-debug and debug variants installed at once - it
may not boot then.

`com.google.android.art.apex` (note `.google.`) is the Google signed variant of
the module. It is also mutually exclusive with the other ones.


## Building as a module on `master-art`

1.  Check out the `master-art` tree:

    ```
    repo init -b master-art -u <repository url>
    ```

    See the [Android source access
    instructions](https://source.android.com/setup/build/downloading) for
    further details.

2.  Set up the development environment:

    ```
    banchan com.android.art <arch>
    export SOONG_ALLOW_MISSING_DEPENDENCIES=true
    ```

    For Google internal builds on the internal master-art branch, specify
    instead the Google variant of the module and product:

    ```
    banchan com.google.android.art mainline_modules_<arch>
    export SOONG_ALLOW_MISSING_DEPENDENCIES=true
    ```

    `<arch>` is the device architecture, one of `arm`, `arm64`, `x86`, or
    `x86_64`. Regardless of the device architecture, the build also includes the
    usual host architectures, and 64/32-bit multilib for the 64-bit products.

    To build the debug variant of the module, specify `com.android.art.debug`
    instead of `com.android.art`. It is also possible to list both.

3.  Build the module:

    ```
    m
    ```

4.  Install the module and reboot:

    ```
    adb install out/target/product/generic_<arch>/system/apex/com.android.art.apex
    adb reboot
    ```

    The name of the APEX file depends on what you passed to `banchan`.


## Building as part of the base system image

NOTE: This method of building is slated to be obsoleted in favor of the
module build on `master-art` above (b/172480617).

1.  Check out a full Android platform tree and lunch the appropriate product the
    normal way.

2.  Ensure the ART Module is built from source:

    ```
    export SOONG_CONFIG_art_module_source_build=true
    ```

    If this isn't set then the build may use prebuilts of the ART Module that
    may be older than the sources.

3.  Build the system image the normal way, for example:

    ```
    m droid
    ```


## Updating prebuilts

Prebuilts are used for the ART Module dependencies that have sources outside the
`master-art` manifest. Conversely the ART Module is (normally) a prebuilt when
used in platform builds of the base system image.

The locations of the prebuilts are:

*  `prebuilts/runtime/mainline` for prebuilts and SDKs required to build the ART
   Module.

   See
   [prebuilts/runtime/mainline/README.md](https://android.googlesource.com/platform/prebuilts/runtime/+/master/mainline/README.md)
   for instructions on how to update them.

*  `packages/modules/ArtPrebuilt` for the ART Module APEX packages.

*  `prebuilts/module_sdk/art` for the ART Module SDK and other tools, needed to
   build platform images and other modules that depend on the ART Module.

To update the ART Module prebuilts in the two last locations:

1.  Ensure the changes that need to go into the prebuilt are submitted.

2.  Wait for a new build on branch `aosp-master-art`, target `aosp_art_module`.

3.  In a full platform tree, run:

    ```
    packages/modules/ArtPrebuilt/update-art-module-prebuilts.py \
      --build <build id> --upload
    ```

    This will download the prebuilts from the given `<build id>` (an integer
    number), create a CL topic, and upload it to Gerrit. Get it reviewed and
    submit. Please do not make any file changes locally.
