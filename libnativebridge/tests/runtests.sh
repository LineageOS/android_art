#!/bin/bash

set -e

skip_build=
skip_host=
skip_target=
skip_cleanup=
for arg; do
  case "$arg" in
    --skip-build) skip_build=true ;;
    --skip-host) skip_host=true ;;
    --skip-target) skip_target=true ;;
    --skip-cleanup) skip_cleanup=true ;;
    *) break ;;
  esac
  shift
done

echo_and_run() {
  echo "$@"
  eval "$@"
}

device_test_root=/data/local/tmp/libnativebridge-test

vars="$(build/soong/soong_ui.bash --dumpvars-mode --vars='HOST_OUT PRODUCT_OUT TARGET_ARCH')"
# Assign to a variable and eval that, since bash ignores any error status
# from the command substitution if it's directly on the eval line.
eval $vars

if [ -z "$skip_build" ]; then
  rm -rf $HOST_OUT/nativetest{,64} $PRODUCT_OUT/data/nativetest{,64}/art/$TARGET_ARCH
  echo_and_run build/soong/soong_ui.bash --make-mode MODULES-IN-art-libnativebridge-tests
fi

if [ -z "$skip_host" ]; then
  for build_dir in $HOST_OUT/nativetest{,64}/ ; do
    if [ ! -d $build_dir ]; then
      echo "Skipping missing $build_dir"
    else
      for test_path in $build_dir/*/* ; do
        test_rel_path=${test_path#${build_dir}/}
        echo_and_run \( cd $build_dir \; $test_rel_path $* \)
      done
    fi
  done
fi

if [ -z "$skip_target" ]; then
  adb root
  adb wait-for-device

  for build_dir in $PRODUCT_OUT/data/nativetest{,64}/art/$TARGET_ARCH ; do
    if [ ! -d $build_dir ]; then
      echo "Skipping missing $build_dir"
    else
      test_dir=$device_test_root/$TARGET_ARCH

      echo_and_run adb shell rm -rf $test_dir
      echo_and_run adb push $build_dir $test_dir

      for test_path in $build_dir/*/* ; do
        test_rel_path=${test_path#${build_dir}/}
        echo_and_run adb shell cd $test_dir '\;' LD_LIBRARY_PATH=. $test_rel_path $*
      done
    fi
  done

  if [ -z "$skip_cleanup" ]; then
    echo_and_run adb shell rm -rf $device_test_root
  fi
fi

echo "No errors"
