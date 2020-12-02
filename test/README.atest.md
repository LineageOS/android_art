# Running ART Tests with Atest / Trade Federation

ART Testing has early support for execution in the [Trade
Federation](https://source.android.com/devices/tech/test_infra/tradefed)
("TradeFed") test harness, in particular via the
[Atest](https://source.android.com/compatibility/tests/development/atest)
command line tool.

Atest conveniently takes care of building tests and their dependencies (using
Soong, the Android build system) and executing them using Trade Federation.

See also [README.md](README.md) for a general introduction to ART run-tests and
gtests.

## ART run-tests

### Running ART run-tests on device

ART run-tests are defined in sub-directories of `test/` starting with a number
(e.g. `test/001-HelloWorld`). Each ART run-test is identified in the build
system by a Soong module name following the `art-run-test-`*`<test-directory>`*
format (e.g. `art-run-test-001-HelloWorld`).

You can run a specific ART run-test on device by passing its Soong module name
to Atest:
```bash
atest art-run-test-001-HelloWorld
```

To run all ART run-tests in a single command, the currently recommended way is
to use [test mapping](#test-mapping) (see below).

## ART gtests

### Running ART gtests on device

Because of current build- and link-related limitations, ART gtests can only run
as part of the Testing ART APEX (`com.android.art.testing.apex`) on device,
i.e. they have to be part of the ART APEX package itself to be able to build and
run properly. This means that it is not possible to test the ART APEX presently
residing on a device (either the original one, located in the "system"
partition, or an updated package, present in the "data" partition).

There are two ways to run ART gtests on device:
* by installing the Testing ART APEX (i.e. manually "updating" the ART APEX on
  device); or
* by setting up a `chroot` environment on the device, and "activating" the
  Testing ART APEX in that environment.

### Running ART gtests on device by installing the Testing ART APEX

You can run ART gtests on device with the ART APEX installation strategy by
using the following `atest` command:

```bash
atest ArtGtestsTargetInstallApex
```

This command:
1. builds the Testing ART APEX from the Android source tree (including the ART
   gtests);
2. installs the Testing ART APEX using `adb install`;
3. reboots the device;
4. runs the tests; and
5. uninstalls the module.

You can run the tests of a single ART gtest C++ class using the
`ArtGtestsTargetInstallApex:`*`<art-gtest-c++-class>`* syntax, e.g.:
```bash
atest ArtGtestsTargetInstallApex:JniInternalTest
```

This syntax also supports the use of wildcards, e.g.:
```bash
atest ArtGtestsTargetInstallApex:*Test*
```

You can also use Trade Federation options to run a subset of ART gtests, e.g.:
```bash
atest ArtGtestsTargetInstallApex -- \
  --module ArtGtestsTargetInstallApex --test '*JniInternalTest*'
```

You can also pass option `--gtest_filter` to the gtest binary to achieve a
similar effect:
```bash
atest ArtGtestsTargetInstallApex -- \
  --test-arg com.android.tradefed.testtype.GTest:native-test-flag:"--gtest_filter=*JniInternalTest*"
```

### Running ART gtests on device using a `chroot` environment

You can run ART gtests on device with the chroot-based strategy by using the
following `atest` command:

```bash
atest ArtGtestsTargetChroot
```

This command:
1. builds the Testing ART APEX from the Android source tree (including the ART
   gtests) and all the necessary dependencies for the `chroot` environment;
2. sets up a `chroot` environment on the device;
3. "activates" the Testing ART APEX (and other APEXes that it depends on) in the
   `chroot` environment;
4. runs the tests within the `chroot` environment; and
5. cleans up the environment (deactivates the APEXes and removes the `chroot`
   environment).

## Test Mapping

ART Testing supports the execution of tests via [Test
Mapping](https://source.android.com/compatibility/tests/development/test-mapping).
The tests declared in ART's [TEST_MAPPING](../TEST_MAPPING) file are executed
during pre-submit testing (when an ART changelist in Gerrit is verified by
Treehugger) and/or post-submit testing (when a given change is merged in the
Android code base), depending on the "test group" where a test is declared.

### Running tests via Test Mapping with Atest

It is possible to run tests via test mapping locally using Atest.

To run all the tests declared in ART's `TEST_MAPPING` file, use the following
command from the Android source tree top-level directory:
```bash
atest --test-mapping art:all
```
In the previous command, `art` is the (relative) path to the directory
containing the `TEST_MAPPING` file listing the tests to run, while `all` means
that tests declared in all [test
groups](https://source.android.com/compatibility/tests/development/test-mapping#defining_test_groups)
shall be run.

To only run tests executed during pre-submit testing, use:
```bash
atest --test-mapping art:presubmit
```

To only run tests executed during post-submit testing, use:
```bash
atest --test-mapping art:postsubmit
```
