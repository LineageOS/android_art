# ART VIXL Simulator Integration

This file documents the use of the VIXL Simulator for running tests on ART. The
simulator enables us to run the ART run-tests without the need for a target
device. This helps to speed up the development/debug/test cycle. The full AOSP
source tree, as well as the partial master-art AOSP source tree, are supported.

## Quick User Guide
1. Set lunch target and setup environment:

    ```bash
    source build/envsetup.sh; lunch armv8-eng
    ```

2. Build ART target and host:

    ```bash
    art/tools/buildbot-build.sh --target
    art/tools/buildbot-build.sh --host
    ```

3. Run Tests:

    To enable the simulator we use the `--simulate-arm64` flag. The simulator can
    be used directly with the dalvikvm or the ART test scripts.

    To run a single test on simulator, use the command:
    ```bash
    art/test/run-test --host --simulate-arm64 --64 <TEST_NAME>
    ```

    To run all ART run-tests on simulator, use the `art/test.py` script with the
    following command:
    ```bash
    ./art/test.py --simulate-arm64 --run-test --optimizing
    ```

4. Enable simulator tracing

    Simulator provides tracing feature which is useful in debugging. Setting
    runtime option `-verbose:simulator` will enable instruction trace and register
    updates.
    For example,
    ```bash
    ./art/test/run-test --host --runtime-option -verbose:simulator --optimizing \
      --never-clean --simulate-arm64 --64 640-checker-simd
    ```

5. Debug

    Another useful usecase of the simulator is debugging using the `--gdb` flag.
    ```bash
    ./art/test/run-test --gdb --host --simulate-arm64 --64 527-checker-array-access-split
    ```
    If developing a compiler optimization which affects the test case
    `527-checker-array-access-split`, you can use the simulator to run and
    generate the control flow graph with:
    ```bash
    ./art/test/run-test --host --dex2oat-jobs 1 -Xcompiler-option --dump-cfg=oat.cfg \
      --never-clean --simulate-arm64 --64 527-checker-array-access-split
    ```

6. Control simulation

    By default, in simulator mode, all methods in `art/test/` run-tests files are
    simulated. However, within `art/simulator/code_simulator_arm64.cc`, the
    `CanSimulate()` function provides options for developer to control simulation:
    - the `kEnableSimulateMethodAllowList` to restrict the methods run in the simulator;
    - the `$simulate$` tag to force the simulator to run a method.

    #### Allow list to control simulation
    Sometimes we may wish to restrict the methods run in the simulator, this can
    be done using the `simulate_method_white_list`. Here a list of methods which
    we know to be safe to run in the simulator is kept in
    `art/simulator/code_simulator_arm64.cc`, the simulator can be forced to only
    run the methods on this list by setting
    ```
    kEnableSimulateMethodAllowList = true
    ```
    and recompile art and rerun the test cases. For example, if we set the white list to
   ```
   static const std::vector<std::string> simulate_method_white_list = {
     "other.TestByte.testDotProdComplex",
     "other.TestByte.testDotProdComplexSignedCastedToUnsigned",
     "other.TestByte.testDotProdComplexUnsigned",
     "other.TestByte.testDotProdComplexUnsignedCastedToSigned",
    };
    ```
    We only allow these methods to be run in simulator and all the other methods
    will run in the interpreter.

    #### The `$simulate$` tag to control simulation
    In the case that we may wish to quickly change a Java method test case and
    force the simulator to run a method without recompiling art, add the
    `$simulate$` tag in the method name. For example,
    ```
    public void $simulate$foo() {}
    ```
