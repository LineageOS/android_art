#!/bin/bash
# A simple preupload check that the runtests.sh script has been run for the
# libnativebridge tests if that library has been changed.
# TODO(b/189484095): Port these tests to atest and enable in presubmit so we
# don't need the custom script to run them.

commit_message="$1"
shift

nativebridge_change=false
for file; do
  [[ $file = libnativebridge/* ]] && nativebridge_change=true
done

if $nativebridge_change; then
  if grep '^Test: art/libnativebridge/tests/runtests.sh' <<< $commit_message ; then :; else
    echo "Please run art/libnativebridge/tests/runtests.sh and add the tag:" 1>&2
    echo "Test: art/libnativebridge/tests/runtests.sh [--skip-host|--skip-target]" 1>&2
    exit 1
  fi
fi
