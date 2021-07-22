/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.tests.odsign;

import static com.google.common.truth.Truth.assertWithMessage;

import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import android.cts.install.lib.host.InstallUtilsHost;

import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.DeviceTestRunOptions;
import com.android.tradefed.util.CommandResult;

import org.junit.After;
import org.junit.Before;
import org.junit.FixMethodOrder;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.MethodSorters;

import java.time.Duration;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.stream.Collectors;

@RunWith(DeviceJUnit4ClassRunner.class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
public class OnDeviceSigningHostTest extends BaseHostJUnit4Test {

    private static final String APEX_FILENAME = "test_com.android.art.apex";

    private static final String ART_APEX_DALVIK_CACHE_DIRNAME =
            "/data/misc/apexdata/com.android.art/dalvik-cache";

    private static final String ODREFRESH_COMPILATION_LOG =
            "/data/misc/odrefresh/compilation-log.txt";

    private final String[] APP_ARTIFACT_EXTENSIONS = new String[] {".art", ".odex", ".vdex"};

    private final String[] BCP_ARTIFACT_EXTENSIONS = new String[] {".art", ".oat", ".vdex"};

    private static final String TEST_APP_PACKAGE_NAME = "com.android.tests.odsign";
    private static final String TEST_APP_APK = "odsign_e2e_test_app.apk";

    private final InstallUtilsHost mInstallUtils = new InstallUtilsHost(this);

    private static final Duration BOOT_COMPLETE_TIMEOUT = Duration.ofMinutes(2);

    @Before
    public void setUp() throws Exception {
        assumeTrue("Updating APEX is not supported", mInstallUtils.isApexUpdateSupported());
        installPackage(TEST_APP_APK);
        mInstallUtils.installApexes(APEX_FILENAME);
        removeCompilationLogToAvoidBackoff();
        reboot();
    }

    @After
    public void cleanup() throws Exception {
        ApexInfo apex = mInstallUtils.getApexInfo(mInstallUtils.getTestFile(APEX_FILENAME));
        getDevice().uninstallPackage(apex.name);
        removeCompilationLogToAvoidBackoff();
        reboot();
    }

    @Test
    public void verifyArtUpgradeSignsFiles() throws Exception {
        DeviceTestRunOptions options = new DeviceTestRunOptions(TEST_APP_PACKAGE_NAME);
        options.setTestClassName(TEST_APP_PACKAGE_NAME + ".ArtifactsSignedTest");
        options.setTestMethodName("testArtArtifactsHaveFsverity");
        runDeviceTests(options);
    }

    @Test
    public void verifyArtUpgradeGeneratesRequiredArtifacts() throws Exception {
        DeviceTestRunOptions options = new DeviceTestRunOptions(TEST_APP_PACKAGE_NAME);
        options.setTestClassName(TEST_APP_PACKAGE_NAME + ".ArtifactsSignedTest");
        options.setTestMethodName("testGeneratesRequiredArtArtifacts");
        runDeviceTests(options);
    }

    private Set<String> getMappedArtifacts(String pid, String grepPattern) throws Exception {
        final String grepCommand = String.format("grep \"%s\" /proc/%s/maps", grepPattern, pid);
        CommandResult result = getDevice().executeShellV2Command(grepCommand);
        assertTrue(result.toString(), result.getExitCode() == 0);
        Set<String> mappedFiles = new HashSet<>();
        for (String line : result.getStdout().split("\\R")) {
            int start = line.indexOf(ART_APEX_DALVIK_CACHE_DIRNAME);
            if (line.contains("[")) {
                continue; // ignore anonymously mapped sections which are quoted in square braces.
            }
            mappedFiles.add(line.substring(start));
        }
        return mappedFiles;
    }

    private String[] getSystemServerClasspath() throws Exception {
        String systemServerClasspath =
                getDevice().executeShellCommand("echo $SYSTEMSERVERCLASSPATH");
        return systemServerClasspath.split(":");
    }

    private String getSystemServerIsa(String mappedArtifact) {
        // Artifact path for system server artifacts has the form:
        //    ART_APEX_DALVIK_CACHE_DIRNAME + "/<arch>/system@framework@some.jar@classes.odex"
        // `mappedArtifacts` may include other artifacts, such as boot-framework.oat that are not
        // prefixed by the architecture.
        String[] pathComponents = mappedArtifact.split("/");
        return pathComponents[pathComponents.length - 2];
    }

    private void verifySystemServerLoadedArtifacts() throws Exception {
        String[] classpathElements = getSystemServerClasspath();
        assertTrue("SYSTEMSERVERCLASSPATH is empty", classpathElements.length > 0);

        String systemServerPid = getDevice().executeShellCommand("pgrep system_server");
        assertTrue(systemServerPid != null);

        // system_server artifacts are in the APEX data dalvik cache and names all contain
        // the word "@classes". Look for mapped files that match this pattern in the proc map for
        // system_server.
        final String grepPattern = ART_APEX_DALVIK_CACHE_DIRNAME + ".*@classes";
        final Set<String> mappedArtifacts = getMappedArtifacts(systemServerPid, grepPattern);
        assertTrue(
                "No mapped artifacts under " + ART_APEX_DALVIK_CACHE_DIRNAME,
                mappedArtifacts.size() > 0);
        final String isa = getSystemServerIsa(mappedArtifacts.iterator().next());
        final String isaCacheDirectory = String.format("%s/%s", ART_APEX_DALVIK_CACHE_DIRNAME, isa);

        // Check the non-APEX components in the system_server classpath have mapped artifacts.
        for (String element : classpathElements) {
            // Skip system_server classpath elements from APEXes as these are not currently
            // compiled.
            if (element.startsWith("/apex")) {
                continue;
            }
            String escapedPath = element.substring(1).replace('/', '@');
            for (String extension : APP_ARTIFACT_EXTENSIONS) {
                final String fullArtifactPath =
                        String.format("%s/%s@classes%s", isaCacheDirectory, escapedPath, extension);
                assertTrue(
                        "Missing " + fullArtifactPath, mappedArtifacts.contains(fullArtifactPath));
            }
        }

        for (String mappedArtifact : mappedArtifacts) {
            // Check no APEX JAR artifacts are mapped for system_server since if there
            // are, then the policy around not compiling APEX jars for system_server has
            // changed and this test needs updating here and in the system_server classpath
            // check above.
            assertTrue(
                    "Unexpected mapped artifact: " + mappedArtifact,
                    mappedArtifact.contains("/apex"));

            // Check the mapped artifact has a .art, .odex or .vdex extension.
            final boolean knownArtifactKind =
                    Arrays.stream(APP_ARTIFACT_EXTENSIONS)
                            .anyMatch(e -> mappedArtifact.endsWith(e));
            assertTrue("Unknown artifact kind: " + mappedArtifact, knownArtifactKind);
        }
    }

    private void verifyZygoteLoadedArtifacts(String zygoteName, String zygotePid) throws Exception {
        final String bootExtensionName = "boot-framework";
        final Set<String> mappedArtifacts = getMappedArtifacts(zygotePid, bootExtensionName);

        assertTrue("Expect 3 boot-framework artifacts", mappedArtifacts.size() == 3);

        String allArtifacts = mappedArtifacts.stream().collect(Collectors.joining(","));
        for (String extension : BCP_ARTIFACT_EXTENSIONS) {
            final String artifact = bootExtensionName + extension;
            final boolean found = mappedArtifacts.stream().anyMatch(a -> a.endsWith(artifact));
            assertTrue(zygoteName + " " + artifact + " not found: '" + allArtifacts + "'", found);
        }
    }

    private void verifyZygotesLoadedArtifacts() throws Exception {
        // There are potentially two zygote processes "zygote" and "zygote64". These are
        // instances 32-bit and 64-bit unspecialized app_process processes.
        // (frameworks/base/cmds/app_process).
        int zygoteCount = 0;
        for (String zygoteName : new String[] {"zygote", "zygote64"}) {
            final CommandResult pgrepResult =
                    getDevice().executeShellV2Command("pgrep " + zygoteName);
            if (pgrepResult.getExitCode() != 0) {
                continue;
            }
            final String zygotePid = pgrepResult.getStdout();
            verifyZygoteLoadedArtifacts(zygoteName, zygotePid);
            zygoteCount += 1;
        }
        assertTrue("No zygote processes found", zygoteCount > 0);
    }

    @Test
    public void verifyGeneratedArtifactsLoaded() throws Exception {
        // Checking zygote and system_server need the device have adb root to walk process maps.
        final boolean adbEnabled = getDevice().enableAdbRoot();
        assertTrue("ADB root failed and required to get process maps", adbEnabled);

        // Check there is a compilation log, we expect compilation to have occurred.
        assertTrue("Compilation log not found", haveCompilationLog());

        // Check both zygote and system_server processes to see that they have loaded the
        // artifacts compiled and signed by odrefresh and odsign. We check both here rather than
        // having a separate test because the device reboots between each @Test method and
        // that is an expensive use of time.
        verifyZygotesLoadedArtifacts();
        verifySystemServerLoadedArtifacts();
    }

    @Test
    public void verifyGeneratedArtifactsLoadedForSamegradeUpdate() throws Exception {
        // Install the same APEX effecting a samegrade update. The setUp method has installed it
        // before us.
        mInstallUtils.installApexes(APEX_FILENAME);
        reboot();

        final boolean adbEnabled = getDevice().enableAdbRoot();
        assertTrue("ADB root failed and required to get odrefresh compilation log", adbEnabled);

        // Check that odrefresh logged a compilation attempt due to samegrade ART APEX install.
        String[] logLines = getDevice().pullFileContents(ODREFRESH_COMPILATION_LOG).split("\n");
        assertTrue(
                "Expected 3 lines in " + ODREFRESH_COMPILATION_LOG + ", found " + logLines.length,
                logLines.length == 3);

        // Check that the compilation log entries are reasonable, ie times move forward.
        // The first line of the log is the log format version number.
        String[] firstUpdateEntry = logLines[1].split(" ");
        String[] secondUpdateEntry = logLines[2].split(" ");
        final int LOG_ENTRY_FIELDS = 5;
        assertTrue(
                "Unexpected number of fields: " + firstUpdateEntry.length + " != " +
                LOG_ENTRY_FIELDS,
                firstUpdateEntry.length == LOG_ENTRY_FIELDS);
        assertTrue(firstUpdateEntry.length == secondUpdateEntry.length);

        final int LAST_UPDATE_MILLIS_INDEX = 1;
        final int COMPILATION_TIME_INDEX = 3;
        for (int i = 0; i < firstUpdateEntry.length; ++i) {
            final long firstField = Long.parseLong(firstUpdateEntry[i]);
            final long secondField = Long.parseLong(secondUpdateEntry[i]);
            if (i == LAST_UPDATE_MILLIS_INDEX) {
                // The second APEX lastUpdateMillis should be after the first, but a clock
                // adjustment might reverse the order so we can't assert this (b/194365586).
                assertTrue(
                        "Last update times are expected to differ, but they are equal " +
                        firstField + " == " + secondField,
                        firstField != secondField);
            } else if (i == COMPILATION_TIME_INDEX) {
                // The second compilation time should be after the first compilation time, but
                // a clock adjustment might reverse the order so we can't assert this
                // (b/194365586).
                assertTrue(
                        "Compilation times are expected to differ, but they are equal " +
                        firstField + " == " + secondField,
                        firstField != secondField);
            } else {
                // The remaining fields should be the same, ie trigger for compilation.
                assertTrue(
                        "Compilation entries differ for position " + i + ": " +
                        firstField + " != " + secondField,
                        firstField == secondField);
            }
        }

        verifyGeneratedArtifactsLoaded();
    }

    private boolean haveCompilationLog() throws Exception {
        CommandResult result =
                getDevice().executeShellV2Command("stat " + ODREFRESH_COMPILATION_LOG);
        return result.getExitCode() == 0;
    }

    private void removeCompilationLogToAvoidBackoff() throws Exception {
        getDevice().executeShellCommand("rm -f " + ODREFRESH_COMPILATION_LOG);
    }

    private void reboot() throws Exception {
        getDevice().reboot();
        boolean success = getDevice().waitForBootComplete(BOOT_COMPLETE_TIMEOUT.toMillis());
        assertWithMessage("Device didn't boot in %s", BOOT_COMPLETE_TIMEOUT).that(success).isTrue();
    }
}
