# ART Testing

There are two suites of tests in the Android Runtime (ART):
* _ART run-tests_: Tests of the ART runtime using Dex bytecode (mostly written
  in Java).
* _ART gtests_: C++ tests exercising various aspects of ART.

## ART run-tests

ART run-tests are tests exercising the runtime using Dex bytecode. They are
written in Java and/or [Smali](https://github.com/JesusFreke/smali)
(compiled/assembled as Dex bytecode) and sometimes native code (written as C/C++
testing libraries). Some tests also make use of the
[Jasmin](http://jasmin.sourceforge.net/) assembler or the
[ASM](https://asm.ow2.io/) bytecode manipulation tool. Run-tests are
executed on the ART runtime (`dalvikvm`), possibly preceded by a
pre-optimization of the Dex code (using `dex2oat`).

The run-tests are identified by directories in this `test` directory, named with
a numeric prefix and containing an `info.txt` file. For most run tests, the
sources are in the `src` subdirectory. Sources found in the `src2` directory are
compiled separately but to the same output directory; this can be used to
exercise "API mismatch" situations by replacing class files created in the first
pass. The `src-ex` directory is built separately, and is intended for exercising
class loaders.  Resources can be stored in the `res` directory, which is
distributed together with the executable files.

The run-tests logic lives in the `test/run-test` Bash script. The execution of a
run-test has three main parts: building the test, running the test, and checking
the test's output. By default, these three steps are implemented by three Bash
scripts located in the `test/etc` directory (`default-build`, `default-run`, and
`default-check`). These scripts rely on environment variables set by
`test/run-test`.

The default logic for all of these these steps (build, run, check) is overridden
if the test's directory contains a Bash script named after the step
(i.e. `build`, `run`, or `check`). Note that the default logic of the "run" step
is actually implemented in the "JAR runner" (`test/etc/run-test-jar`), invoked
by `test/etc/default-run`.

After the execution of a run-test, the check step's default behavior
(implemented in `test/etc/default-check`) is to compare its standard output with
the contents of the `expected.txt` file contained in the test's directory; any
mismatch triggers a test failure.

The `test/run-test` script handles the execution of a single run-test in a given
configuration. The Python script `test/testrunner/testrunner.py` is a convenient
script handling the construction and execution of multiple tests in one
configuration or more.

To see the invocation options supported by `run-test` and `testrunner.py`, run
these commands from the Android source top-level directory:
```sh
art/test/run-test --help
```
```sh
art/test/testrunner/testrunner.py --help
```

## ART gtests

ART gtests are written in C++ using the [Google
Test](https://github.com/google/googletest) framework. These tests exercise
various aspects of the runtime (the logic in `libart`, `libart-compiler`, etc.)
and its binaries (`dalvikvm`, `dex2oat`, `oatdump`, etc.). Some of them are used
as unit tests to verify a particular construct in ART. These tests may depend on
some test Dex files and core images.

ART gtests are defined in various directories within the ART project (usually in
the same directory as the code they exercise). Their source files usually end
with the suffix `_test.cc`. The construction logic of these tests is implemented
in ART's build system (`Android.bp` and `Android*.mk` files). On host, these
gtests can be run by executing `m test-art-host-gtest`. On device, the
recommended approach is to run these tests in a chroot environment (see
`README.chroot.md` in this directory).


# Test execution

All tests in either suite can be run using the `art/test.py`
script. Additionally, run-tests can be run individually. All of the tests can be
run on the build host, on a USB-attached device, or using the build host
"reference implementation".

ART also supports running target (device) tests in a chroot environment (see
`README.chroot.md` in this directory). This is currently the recommended way to
run tests on target (rather than using `art/test.py --target`).

To see command flags run:

```sh
$ art/test.py -h
```

## Running all tests on the build host

```sh
$ art/test.py --host
```

## Running all tests on the target device

```sh
$ art/test.py --target
```

## Running all gtests on the build host

```sh
$ art/test.py --host -g
```

## Running all gtests on the target device

```sh
$ art/test.py --target -g
```

## Running all run-tests on the build host

```sh
$ art/test.py --host -r
```

## Running all run-tests on the target device

```sh
$ art/test.py --target -r
```

## Running one run-test on the build host

```sh
$ art/test.py --host -r -t 001-HelloWorld
```

## Running one run-test on the target device

```sh
$ art/test.py --target -r -t 001-HelloWorld
```


# ART Continuous Integration

Both ART run-tests and gtests are run continuously as part of [ART's continuous
integration](https://ci.chromium.org/p/art/g/luci/console). In addition, two
other test suites are run continuously on this service: Libcore tests and JDWP
tests.
