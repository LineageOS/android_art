#!/bin/bash -e

# This script builds the APEX modules, SDKs and module exports that the ART
# Module provides.

if [ ! -e build/make/core/Makefile ]; then
  echo "$0 must be run from the top of the tree"
  exit 1
fi

skip_apex=
skip_module_sdk=
build_args=()
for arg; do
  case "$arg" in
    --skip-apex) skip_apex=true ;;
    --skip-module-sdk) skip_module_sdk=true ;;
    *) build_args+=("$arg") ;;
  esac
  shift
done

MAINLINE_MODULES=()
if [ -z "$skip_apex" ]; then
  # Take the list of modules from MAINLINE_MODULES.
  if [ -n "${MAINLINE_MODULES}" ]; then
    read -r -a MAINLINE_MODULES <<< "${MAINLINE_MODULES}"
  else
    MAINLINE_MODULES=(
      com.android.art
      com.android.art.debug
    )
  fi
fi

# Take the list of products to build the modules for from
# MAINLINE_MODULE_PRODUCTS.
if [ -n "${MAINLINE_MODULE_PRODUCTS}" ]; then
  read -r -a MAINLINE_MODULE_PRODUCTS <<< "${MAINLINE_MODULE_PRODUCTS}"
else
  # The default products are the same as in
  # build/soong/scripts/build-mainline-modules.sh.
  MAINLINE_MODULE_PRODUCTS=(
    art_module_arm
    art_module_arm64
    art_module_x86
    art_module_x86_64
  )
fi

MODULE_SDKS_AND_EXPORTS=()
if [ -z "$skip_module_sdk" ]; then
  MODULE_SDKS_AND_EXPORTS=(
    art-module-sdk
    art-module-host-exports
    art-module-test-exports
  )
fi

echo_and_run() {
  echo "$*"
  "$@"
}

export OUT_DIR=${OUT_DIR:-out}
export DIST_DIR=${DIST_DIR:-${OUT_DIR}/dist}

# Use same build settings as build_unbundled_mainline_module.sh, for build
# consistency.
# TODO(mast): Call out to a common script for building APEXes.
export UNBUNDLED_BUILD_SDKS_FROM_SOURCE=true
export TARGET_BUILD_VARIANT=${TARGET_BUILD_VARIANT:-"user"}
export TARGET_BUILD_DENSITY=alldpi
export TARGET_BUILD_TYPE=release

if [ ! -d frameworks/base ]; then
  # Configure the build system for the reduced manifest branch.
  export SOONG_ALLOW_MISSING_DEPENDENCIES=true
fi

if [ ${#MAINLINE_MODULES[*]} -gt 0 ]; then
  (
    export TARGET_BUILD_APPS="${MAINLINE_MODULES[*]}"

    # We require .apex files here, so ensure we get them regardless of product
    # settings.
    export OVERRIDE_TARGET_FLATTEN_APEX=false

    for product in ${MAINLINE_MODULE_PRODUCTS[*]}; do
      echo_and_run build/soong/soong_ui.bash --make-mode \
        TARGET_PRODUCT=${product} "${build_args[@]}" ${MAINLINE_MODULES[*]}

      vars="$(TARGET_PRODUCT=${product} build/soong/soong_ui.bash \
              --dumpvars-mode --vars="PRODUCT_OUT TARGET_ARCH")"
      # Assign to a variable and eval that, since bash ignores any error status
      # from the command substitution if it's directly on the eval line.
      eval $vars

      mkdir -p ${DIST_DIR}/${TARGET_ARCH}
      for module in ${MAINLINE_MODULES[*]}; do
        echo_and_run cp ${PRODUCT_OUT}/system/apex/${module}.apex \
          ${DIST_DIR}/${TARGET_ARCH}/
      done
    done
  )
fi

if [ ${#MODULE_SDKS_AND_EXPORTS[*]} -gt 0 ]; then
  # Create multi-arch SDKs in a different out directory. The multi-arch script
  # uses Soong in --skip-make mode which cannot use the same directory as normal
  # mode with make.
  export OUT_DIR=${OUT_DIR}/aml

  # Put the build system in apps building mode so we don't trip on platform
  # dependencies, but there are no actual apps to build here.
  export TARGET_BUILD_APPS=none

  # Make build-aml-prebuilts.sh set the source_build Soong config variable true.
  export ENABLE_ART_SOURCE_BUILD=true

  echo_and_run build/soong/scripts/build-aml-prebuilts.sh "${build_args[@]}" \
    ${MODULE_SDKS_AND_EXPORTS[*]}

  rm -rf ${DIST_DIR}/mainline-sdks
  mkdir -p ${DIST_DIR}
  echo_and_run cp -r ${OUT_DIR}/soong/mainline-sdks ${DIST_DIR}/
fi
