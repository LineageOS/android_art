#!/bin/bash -e

# This script builds the APEX modules, SDKs and module exports that the ART
# Module provides.

MAINLINE_MODULES=(
  com.android.art
  com.android.art.debug
)

# The products to build MAINLINE_MODULES for, same as in
# build/soong/scripts/build-mainline-modules.sh.
PRODUCTS=(
  aosp_arm
  aosp_arm64
  aosp_x86
  aosp_x86_64
)

MODULES_SDK_AND_EXPORTS=(
  art-module-sdk
  art-module-host-exports
  art-module-test-exports
)

# MAINLINE_MODULE_PRODUCTS can be used to override the list of products.
if [ -n "${MAINLINE_MODULE_PRODUCTS}" ]; then
  read -r -a PRODUCTS <<< "${MAINLINE_MODULE_PRODUCTS}"
fi

if [ ! -e build/make/core/Makefile ]; then
  echo "$0 must be run from the top of the tree"
  exit 1
fi

echo_and_run() {
  echo "$*"
  "$@"
}

export OUT_DIR=${OUT_DIR:-out}
export DIST_DIR=${DIST_DIR:-${OUT_DIR}/dist}

if [ ! -d frameworks/base ]; then
  # Configure the build system for the reduced manifest branch.
  build_args="SOONG_ALLOW_MISSING_DEPENDENCIES=true"
  build_args="$build_args TARGET_BUILD_UNBUNDLED=true"
fi

for product in ${PRODUCTS[*]}; do
  echo_and_run build/soong/soong_ui.bash --make-mode $build_args \
    TARGET_PRODUCT=${product} ${MAINLINE_MODULES[*]}

  eval $(TARGET_PRODUCT=${product} build/soong/soong_ui.bash --dumpvars-mode \
         --vars="PRODUCT_OUT TARGET_ARCH")

  mkdir -p ${DIST_DIR}/${TARGET_ARCH}
  for module in ${MAINLINE_MODULES[*]}; do
    echo_and_run cp ${PRODUCT_OUT}/system/apex/${module}.apex \
      ${DIST_DIR}/${TARGET_ARCH}/
  done
done

# Create multi-archs SDKs in a different out directory. The multi-arch script
# uses Soong in --skip-make mode which cannot use the same directory as normal
# mode with make.
export OUT_DIR=${OUT_DIR}/aml

echo_and_run build/soong/scripts/build-aml-prebuilts.sh $build_args \
  ${MODULES_SDK_AND_EXPORTS[*]}

rm -rf ${DIST_DIR}/mainline-sdks
echo_and_run cp -r ${OUT_DIR}/soong/mainline-sdks ${DIST_DIR}
