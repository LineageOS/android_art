/*
 * Copyright (C) 2022 The Android Open Source Project
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

package com.android.server.art;

import static com.android.server.art.DexUseManagerLocal.CheckedSecondaryDexInfo;
import static com.android.server.art.DexUseManagerLocal.DexLoader;
import static com.android.server.art.DexUseManagerLocal.SecondaryDexInfo;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.argThat;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.when;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Binder;
import android.os.Environment;
import android.os.Process;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.storage.StorageManager;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.model.DexContainerFileUseInfo;
import com.android.server.art.proto.DexUseProto;
import com.android.server.art.testing.MockClock;
import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.AndroidPackageSplit;
import com.android.server.pm.pkg.PackageState;

import dalvik.system.PathClassLoader;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class DexUseManagerTest {
    private static final String LOADING_PKG_NAME = "com.example.loadingpackage";
    private static final String OWNING_PKG_NAME = "com.example.owningpackage";
    private static final String BASE_APK = "/somewhere/app/" + OWNING_PKG_NAME + "/base.apk";
    private static final String SPLIT_APK = "/somewhere/app/" + OWNING_PKG_NAME + "/split_0.apk";

    // A reduced limit to make the test run faster.
    private static final int MAX_SECONDARY_DEX_FILES_PER_OWNER_FOR_TESTING = 50;

    @Rule
    public StaticMockitoRule mockitoRule = new StaticMockitoRule(
            SystemProperties.class, Constants.class, Process.class, ArtJni.class);

    private final UserHandle mUserHandle = Binder.getCallingUserHandle();

    /**
     * The default value of `fileVisibility` returned by `getSecondaryDexInfo`. The value doesn't
     * matter because it's undefined, but it's needed for deep equality check, to make the test
     * simpler.
     */
    private final @FileVisibility int mDefaultFileVisibility = FileVisibility.OTHER_READABLE;

    @Mock private PackageManagerLocal.FilteredSnapshot mSnapshot;
    @Mock private DexUseManagerLocal.Injector mInjector;
    @Mock private IArtd mArtd;
    @Mock private Context mContext;
    private DexUseManagerLocal mDexUseManager;
    private String mCeDir;
    private String mDeDir;
    private MockClock mMockClock;
    private ArgumentCaptor<BroadcastReceiver> mBroadcastReceiverCaptor;
    private File mTempFile;
    private Map<String, PackageState> mPackageStates;

    @Before
    public void setUp() throws Exception {
        // No ISA translation.
        lenient()
                .when(SystemProperties.get(argThat(arg -> arg.startsWith("ro.dalvik.vm.isa."))))
                .thenReturn("");

        lenient().when(Constants.getPreferredAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative64BitAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative32BitAbi()).thenReturn("armeabi-v7a");

        lenient().when(Process.isIsolatedUid(anyInt())).thenReturn(false);

        // Use a LinkedHashMap so that we can control the iteration order.
        mPackageStates = new LinkedHashMap<>();

        // Put the null package in front of other packages to verify that it's properly skipped.
        PackageState nullPkgState =
                createPackageState("com.example.null", "arm64-v8a", false /* hasPackage */);
        addPackage("com.example.null", nullPkgState);
        PackageState loadingPkgState =
                createPackageState(LOADING_PKG_NAME, "armeabi-v7a", true /* hasPackage */);
        addPackage(LOADING_PKG_NAME, loadingPkgState);
        PackageState owningPkgState =
                createPackageState(OWNING_PKG_NAME, "arm64-v8a", true /* hasPackage */);
        addPackage(OWNING_PKG_NAME, owningPkgState);
        PackageState platformPkgState =
                createPackageState(Utils.PLATFORM_PACKAGE_NAME, "arm64-v8a", true /* hasPackage */);
        addPackage(Utils.PLATFORM_PACKAGE_NAME, platformPkgState);

        lenient().when(mSnapshot.getPackageStates()).thenReturn(mPackageStates);

        mBroadcastReceiverCaptor = ArgumentCaptor.forClass(BroadcastReceiver.class);
        lenient()
                .when(mContext.registerReceiver(mBroadcastReceiverCaptor.capture(), any()))
                .thenReturn(mock(Intent.class));

        mCeDir = Environment
                         .getDataCePackageDirectoryForUser(StorageManager.UUID_DEFAULT,
                                 Binder.getCallingUserHandle(), OWNING_PKG_NAME)
                         .toString();
        mDeDir = Environment
                         .getDataDePackageDirectoryForUser(StorageManager.UUID_DEFAULT,
                                 Binder.getCallingUserHandle(), OWNING_PKG_NAME)
                         .toString();
        mMockClock = new MockClock();

        mTempFile = File.createTempFile("package-dex-usage", ".pb");
        mTempFile.deleteOnExit();

        lenient().when(ArtJni.validateDexPath(any())).thenReturn(null);
        lenient().when(ArtJni.validateClassLoaderContext(any(), any())).thenReturn(null);

        lenient().when(mInjector.getArtd()).thenReturn(mArtd);
        lenient().when(mInjector.getCurrentTimeMillis()).thenReturn(0l);
        lenient().when(mInjector.getFilename()).thenReturn(mTempFile.getPath());
        lenient()
                .when(mInjector.createScheduledExecutor())
                .thenAnswer(invocation -> mMockClock.createScheduledExecutor());
        lenient().when(mInjector.getContext()).thenReturn(mContext);
        lenient().when(mInjector.getAllPackageNames()).thenReturn(mPackageStates.keySet());
        lenient()
                .when(mInjector.getMaxSecondaryDexFilesPerOwner())
                .thenReturn(MAX_SECONDARY_DEX_FILES_PER_OWNER_FOR_TESTING);

        mDexUseManager = new DexUseManagerLocal(mInjector);
        mDexUseManager.systemReady();
    }

    @Test
    public void testPrimaryDexOwned() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */));
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, BASE_APK)).isFalse();

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK)).isEmpty();
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, SPLIT_APK))
                .isFalse();
    }

    @Test
    public void testPrimaryDexOwnedIsolated() {
        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */));
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, BASE_APK)).isTrue();

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK)).isEmpty();
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, SPLIT_APK))
                .isFalse();
    }

    @Test
    public void testPrimaryDexOwnedSplitIsolated() {
        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(SPLIT_APK, "CLC"));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK)).isEmpty();
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, BASE_APK)).isFalse();

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */));
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, SPLIT_APK)).isTrue();
    }

    @Test
    public void testPrimaryDexOthers() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK))
                .containsExactly(DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */));
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, BASE_APK)).isTrue();

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK)).isEmpty();
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, SPLIT_APK))
                .isFalse();
    }

    /** Checks that it ignores and dedups things correctly. */
    @Test
    public void testPrimaryDexMultipleEntries() throws Exception {
        verifyPrimaryDexMultipleEntries(
                false /* saveAndLoad */, false /* shutdown */, false /* cleanup */);
    }

    /** Checks that it saves data after some time has passed and loads data correctly. */
    @Test
    public void testPrimaryDexMultipleEntriesPersisted() throws Exception {
        verifyPrimaryDexMultipleEntries(
                true /*saveAndLoad */, false /* shutdown */, false /* cleanup */);
    }

    /** Checks that it saves data when the device is being shutdown and loads data correctly. */
    @Test
    public void testPrimaryDexMultipleEntriesPersistedDueToShutdown() throws Exception {
        verifyPrimaryDexMultipleEntries(
                true /*saveAndLoad */, true /* shutdown */, false /* cleanup */);
    }

    /** Checks that it doesn't accidentally cleanup any entry that is needed. */
    @Test
    public void testPrimaryDexMultipleEntriesSurviveCleanup() throws Exception {
        verifyPrimaryDexMultipleEntries(
                false /*saveAndLoad */, false /* shutdown */, true /* cleanup */);
    }

    private void verifyPrimaryDexMultipleEntries(
            boolean saveAndLoad, boolean shutdown, boolean cleanup) throws Exception {
        when(mInjector.getCurrentTimeMillis()).thenReturn(1000l);

        lenient()
                .when(mArtd.getDexFileVisibility(BASE_APK))
                .thenReturn(FileVisibility.OTHER_READABLE);
        lenient()
                .when(mArtd.getDexFileVisibility(SPLIT_APK))
                .thenReturn(FileVisibility.OTHER_READABLE);

        // These should be ignored.
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, Utils.PLATFORM_PACKAGE_NAME, Map.of(BASE_APK, "CLC"));
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of("/somewhere/app/" + OWNING_PKG_NAME + "/non-existing.apk", "CLC"));

        // Some of these should be deduped.
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC", SPLIT_APK, "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC", SPLIT_APK, "CLC"));

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(BASE_APK, "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC"));
        when(mInjector.getCurrentTimeMillis()).thenReturn(2000l);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        if (saveAndLoad) {
            if (shutdown) {
                mBroadcastReceiverCaptor.getValue().onReceive(mContext, mock(Intent.class));
            } else {
                // MockClock runs tasks synchronously.
                mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS);
            }
            mDexUseManager = new DexUseManagerLocal(mInjector);
        }

        if (cleanup) {
            // Nothing should be cleaned up.
            mDexUseManager.cleanup();
        }

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */),
                        DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */),
                        DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */));

        assertThat(mDexUseManager.getPackageLastUsedAtMs(OWNING_PKG_NAME)).isEqualTo(2000l);
    }

    @Test
    public void testSecondaryDexOwned() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */)),
                        false /* isUsedByOtherApps */, FileVisibility.OTHER_READABLE));
        assertThat(dexInfoList.get(0).classLoaderContext()).isEqualTo("CLC");
    }

    @Test
    public void testSecondaryDexOwnedIsolated() {
        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mDeDir + "/foo.apk", "CLC"));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(CheckedSecondaryDexInfo.create(mDeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, FileVisibility.OTHER_READABLE));
        assertThat(dexInfoList.get(0).classLoaderContext()).isEqualTo("CLC");
    }

    @Test
    public void testSecondaryDexOthers() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("armeabi-v7a"),
                        Set.of(DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, FileVisibility.OTHER_READABLE));
        assertThat(dexInfoList.get(0).classLoaderContext()).isEqualTo("CLC");
    }

    @Test
    public void testSecondaryDexUnsupportedClc() {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, LOADING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk", SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT, Set.of("armeabi-v7a"),
                        Set.of(DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, FileVisibility.OTHER_READABLE));
        assertThat(dexInfoList.get(0).classLoaderContext()).isNull();
    }

    @Test
    public void testSecondaryDexVariableClc() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC2"));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        SecondaryDexInfo.VARYING_CLASS_LOADER_CONTEXTS,
                        Set.of("arm64-v8a", "armeabi-v7a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */),
                                DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, FileVisibility.OTHER_READABLE));
        assertThat(dexInfoList.get(0).classLoaderContext()).isNull();
    }

    /** Checks that it ignores and dedups things correctly. */
    @Test
    public void testSecondaryDexMultipleEntries() throws Exception {
        verifySecondaryDexMultipleEntries(
                false /*saveAndLoad */, false /* shutdown */, false /* cleanup */);
    }

    /** Checks that it saves data after some time has passed and loads data correctly. */
    @Test
    public void testSecondaryDexMultipleEntriesPersisted() throws Exception {
        verifySecondaryDexMultipleEntries(
                true /*saveAndLoad */, false /* shutdown */, false /* cleanup */);
    }

    /** Checks that it saves data when the device is being shutdown and loads data correctly. */
    @Test
    public void testSecondaryDexMultipleEntriesPersistedDueToShutdown() throws Exception {
        verifySecondaryDexMultipleEntries(
                true /*saveAndLoad */, true /* shutdown */, false /* cleanup */);
    }

    /** Checks that it doesn't accidentally cleanup any entry that is needed. */
    @Test
    public void testSecondaryDexMultipleEntriesSurviveCleanup() throws Exception {
        verifySecondaryDexMultipleEntries(
                false /*saveAndLoad */, false /* shutdown */, true /* cleanup */);
    }

    private void verifySecondaryDexMultipleEntries(
            boolean saveAndLoad, boolean shutdown, boolean cleanup) throws Exception {
        when(mInjector.getCurrentTimeMillis()).thenReturn(1000l);

        lenient()
                .when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.OTHER_READABLE);
        lenient()
                .when(mArtd.getDexFileVisibility(mCeDir + "/bar.apk"))
                .thenReturn(FileVisibility.OTHER_READABLE);
        lenient()
                .when(mArtd.getDexFileVisibility(mCeDir + "/baz.apk"))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        // These should be ignored.
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, Utils.PLATFORM_PACKAGE_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of("/some/non-existing.apk", "CLC"));

        // Some of these should be deduped.
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk", "CLC", mCeDir + "/bar.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk", "UpdatedCLC", mCeDir + "/bar.apk", "UpdatedCLC"));

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "UpdatedCLC"));

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/bar.apk", "DifferentCLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/bar.apk", "UpdatedDifferentCLC"));

        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/baz.apk", SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT));

        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        when(mInjector.getCurrentTimeMillis()).thenReturn(2000l);
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk", SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT));

        if (saveAndLoad) {
            if (shutdown) {
                mBroadcastReceiverCaptor.getValue().onReceive(mContext, mock(Intent.class));
            } else {
                // MockClock runs tasks synchronously.
                mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS);
            }
            mDexUseManager = new DexUseManagerLocal(mInjector);
        }

        if (cleanup) {
            // Nothing should be cleaned up.
            mDexUseManager.cleanup();
        }

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                                         "UpdatedCLC", Set.of("arm64-v8a", "armeabi-v7a"),
                                         Set.of(DexLoader.create(OWNING_PKG_NAME,
                                                        false /* isolatedProcess */),
                                                 DexLoader.create(OWNING_PKG_NAME,
                                                         true /* isolatedProcess */),
                                                 DexLoader.create(LOADING_PKG_NAME,
                                                         false /* isolatedProcess */)),
                                         true /* isUsedByOtherApps */, mDefaultFileVisibility),
                        CheckedSecondaryDexInfo.create(mCeDir + "/bar.apk", mUserHandle,
                                SecondaryDexInfo.VARYING_CLASS_LOADER_CONTEXTS,
                                Set.of("arm64-v8a", "armeabi-v7a"),
                                Set.of(DexLoader.create(
                                               OWNING_PKG_NAME, false /* isolatedProcess */),
                                        DexLoader.create(
                                                LOADING_PKG_NAME, false /* isolatedProcess */)),
                                true /* isUsedByOtherApps */, mDefaultFileVisibility),
                        CheckedSecondaryDexInfo.create(mCeDir + "/baz.apk", mUserHandle,
                                SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT,
                                Set.of("arm64-v8a"),
                                Set.of(DexLoader.create(
                                        OWNING_PKG_NAME, false /* isolatedProcess */)),
                                false /* isUsedByOtherApps */, mDefaultFileVisibility));

        assertThat(mDexUseManager.getSecondaryDexContainerFileUseInfo(OWNING_PKG_NAME))
                .containsExactly(DexContainerFileUseInfo.create(mCeDir + "/foo.apk", mUserHandle,
                                         Set.of(OWNING_PKG_NAME, LOADING_PKG_NAME)),
                        DexContainerFileUseInfo.create(mCeDir + "/bar.apk", mUserHandle,
                                Set.of(OWNING_PKG_NAME, LOADING_PKG_NAME)),
                        DexContainerFileUseInfo.create(
                                mCeDir + "/baz.apk", mUserHandle, Set.of(OWNING_PKG_NAME)));

        assertThat(mDexUseManager.getPackageLastUsedAtMs(OWNING_PKG_NAME)).isEqualTo(2000l);
    }

    @Test
    public void testCheckedSecondaryDexPublic() throws Exception {
        when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        assertThat(mDexUseManager.getCheckedSecondaryDexInfo(
                           OWNING_PKG_NAME, true /* excludeObsoleteDexesAndLoaders */))
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a", "armeabi-v7a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */),
                                DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, FileVisibility.OTHER_READABLE));
    }

    @Test
    public void testCheckedSecondaryDexPrivate() throws Exception {
        when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        assertThat(mDexUseManager.getCheckedSecondaryDexInfo(
                           OWNING_PKG_NAME, true /* excludeObsoleteDexesAndLoaders */))
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */)),
                        false /* isUsedByOtherApps */, FileVisibility.NOT_OTHER_READABLE));

        assertThat(mDexUseManager.getCheckedSecondaryDexInfo(
                           OWNING_PKG_NAME, false /* excludeObsoleteDexesAndLoaders */))
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a", "armeabi-v7a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */),
                                DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */),
                                DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, FileVisibility.NOT_OTHER_READABLE));
    }

    @Test
    public void testCheckedSecondaryDexNotFound() throws Exception {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk")).thenReturn(FileVisibility.NOT_FOUND);

        assertThat(mDexUseManager.getCheckedSecondaryDexInfo(
                           OWNING_PKG_NAME, true /* excludeObsoleteDexesAndLoaders */))
                .isEmpty();

        assertThat(mDexUseManager.getCheckedSecondaryDexInfo(
                           OWNING_PKG_NAME, false /* excludeObsoleteDexesAndLoaders */))
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */)),
                        false /* isUsedByOtherApps */, FileVisibility.NOT_FOUND));
    }

    @Test
    public void testCheckedSecondaryDexFilteredDueToVisibility() throws Exception {
        when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        assertThat(mDexUseManager.getCheckedSecondaryDexInfo(
                           OWNING_PKG_NAME, true /* excludeObsoleteDexesAndLoaders */))
                .isEmpty();
    }

    @Test
    public void testCleanup() throws Exception {
        PackageState pkgState = createPackageState(
                "com.example.deletedpackage", "arm64-v8a", true /* hasPackage */);
        addPackage("com.example.deletedpackage", pkgState);
        lenient()
                .when(mArtd.getDexFileVisibility(
                        "/somewhere/app/com.example.deletedpackage/base.apk"))
                .thenReturn(FileVisibility.OTHER_READABLE);
        lenient()
                .when(mArtd.getDexFileVisibility(BASE_APK))
                .thenReturn(FileVisibility.OTHER_READABLE);
        // Simulate that a package loads its own dex file and another package's dex file.
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, "com.example.deletedpackage",
                Map.of("/somewhere/app/com.example.deletedpackage/base.apk", "CLC", BASE_APK,
                        "CLC"));
        // Simulate that another package loads this package's dex file.
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, LOADING_PKG_NAME,
                Map.of("/somewhere/app/com.example.deletedpackage/base.apk", "CLC"));
        // Simulate that the package is then deleted.
        removePackage("com.example.deletedpackage");

        // Simulate that a primary dex file is loaded and then deleted.
        lenient()
                .when(mArtd.getDexFileVisibility(SPLIT_APK))
                .thenReturn(FileVisibility.OTHER_READABLE);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(SPLIT_APK, "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(SPLIT_APK, "CLC"));
        lenient().when(mArtd.getDexFileVisibility(SPLIT_APK)).thenReturn(FileVisibility.NOT_FOUND);

        // Simulate that a secondary dex file is loaded and then deleted.
        lenient()
                .when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        lenient()
                .when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.NOT_FOUND);

        // Create an entry that should be kept.
        lenient()
                .when(mArtd.getDexFileVisibility(mCeDir + "/bar.apk"))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/bar.apk", "CLC"));

        // Simulate that a secondary dex file is loaded by another package and then made private.
        lenient()
                .when(mArtd.getDexFileVisibility(mCeDir + "/baz.apk"))
                .thenReturn(FileVisibility.OTHER_READABLE);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/baz.apk", "CLC"));
        lenient()
                .when(mArtd.getDexFileVisibility(mCeDir + "/baz.apk"))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        // Simulate that all the files of a package are deleted. The whole container entry of the
        // package should be cleaned up, though the package still exists.
        lenient()
                .when(mArtd.getDexFileVisibility(
                        "/somewhere/app/" + LOADING_PKG_NAME + "/base.apk"))
                .thenReturn(FileVisibility.OTHER_READABLE);
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, LOADING_PKG_NAME,
                Map.of("/somewhere/app/" + LOADING_PKG_NAME + "/base.apk", "CLC"));
        lenient()
                .when(mArtd.getDexFileVisibility(
                        "/somewhere/app/" + LOADING_PKG_NAME + "/base.apk"))
                .thenReturn(FileVisibility.NOT_FOUND);

        // Run cleanup.
        mDexUseManager.cleanup();

        // Save.
        mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS);

        // Check that the entries are removed from the proto. Normally, we should check the return
        // values of the public get methods instead of checking the raw proto. However, here we want
        // to make sure that the container entries are cleaned up when they are empty so that they
        // don't cost extra memory or storage.
        // Note that every repeated field must not contain more than one entry, to keep the
        // textproto deterministic.
        DexUseProto proto;
        try (InputStream in = new FileInputStream(mTempFile.getPath())) {
            proto = DexUseProto.parseFrom(in);
        }
        String textproto = proto.toString();
        // Remove the first line, which is an auto-generated comment.
        textproto = textproto.substring(textproto.indexOf('\n') + 1).trim();
        assertThat(textproto).isEqualTo("package_dex_use {\n"
                + "  owning_package_name: \"com.example.owningpackage\"\n"
                + "  secondary_dex_use {\n"
                + "    dex_file: \"/data/user/0/com.example.owningpackage/bar.apk\"\n"
                + "    record {\n"
                + "      abi_name: \"arm64-v8a\"\n"
                + "      class_loader_context: \"CLC\"\n"
                + "      last_used_at_ms: 0\n"
                + "      loading_package_name: \"com.example.owningpackage\"\n"
                + "    }\n"
                + "    user_id {\n"
                + "    }\n"
                + "  }\n"
                + "}");
    }

    @Test(expected = IllegalArgumentException.class)
    public void testUnknownPackage() {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, "bogus", Map.of(BASE_APK, "CLC"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testEmptyMap() {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME, Map.of());
    }

    @Test(expected = IllegalArgumentException.class)
    public void testNullKey() {
        var map = new HashMap<String, String>();
        map.put(null, "CLC");
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME, map);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testTooLongDexPath() throws Exception {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of("/" + "X".repeat(DexUseManagerLocal.MAX_PATH_LENGTH), "CLC"));
    }

    @Test
    public void testMaxLengthDexPath() throws Exception {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of("/" + "X".repeat(DexUseManagerLocal.MAX_PATH_LENGTH - 1), "CLC"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testInvalidDexPath() throws Exception {
        lenient().when(ArtJni.validateDexPath(any())).thenReturn("invalid");
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of("/a/b.jar", "PCL[]"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testTooLongClassLoaderContext() throws Exception {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk",
                        "X".repeat(DexUseManagerLocal.MAX_CLASS_LOADER_CONTEXT_LENGTH + 1)));
    }

    @Test
    public void testMaxLengthClassLoaderContext() throws Exception {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk",
                        "X".repeat(DexUseManagerLocal.MAX_CLASS_LOADER_CONTEXT_LENGTH)));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testInvalidClassLoaderContext() throws Exception {
        lenient().when(ArtJni.validateClassLoaderContext(any(), any())).thenReturn("invalid");
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of("/a/b.jar", "PCL[]"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testNullValue() {
        var map = new HashMap<String, String>();
        map.put(mCeDir + "/foo.apk", null);
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME, map);
    }

    @Test
    public void testFileNotFound() {
        // It should fail to load the file.
        when(mInjector.getFilename()).thenReturn("/nonexisting/file");
        mDexUseManager = new DexUseManagerLocal(mInjector);

        // Add some arbitrary data to see if it works fine after a failed load.
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        assertThat(mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME))
                .containsExactly(CheckedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("armeabi-v7a"),
                        Set.of(DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, mDefaultFileVisibility));
    }

    @Test
    public void testSecondaryDexPath() throws Exception {
        mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS); // Save.
        long oldFileSize = mTempFile.length();

        String existingDexPath = mCeDir + "/foo.apk";
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(existingDexPath, "PCL[]"));

        mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS); // Save.
        assertThat(mTempFile.length()).isGreaterThan(oldFileSize);
    }

    @Test
    public void testLimitSecondaryDexFiles() throws Exception {
        for (int n = 0; n < MAX_SECONDARY_DEX_FILES_PER_OWNER_FOR_TESTING - 1; ++n) {
            mDexUseManager.notifyDexContainersLoaded(mSnapshot, LOADING_PKG_NAME,
                    Map.of(String.format("%s/%04d/foo.apk", mCeDir, n), "CLC"));
        }
        mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS); // Save.
        long oldFileSize = mTempFile.length();

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/9998/foo.apk", "CLC"));
        mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS); // Save.
        assertThat(mTempFile.length()).isGreaterThan(oldFileSize);

        oldFileSize = mTempFile.length();
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/9999/foo.apk", "CLC"));
        mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS); // Save.
        assertThat(mTempFile.length()).isEqualTo(oldFileSize);

        // Can still add loading packages to existing entries after the limit is reached.
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/9998/foo.apk", "CLC"));
        mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS); // Save.
        assertThat(mTempFile.length()).isGreaterThan(oldFileSize);
    }

    @Test
    public void testLimitSecondaryDexFilesSingleCall() throws Exception {
        Map<String, String> clcByDexFile = new HashMap<>();
        for (int n = 0; n < MAX_SECONDARY_DEX_FILES_PER_OWNER_FOR_TESTING + 1; ++n) {
            clcByDexFile.put(String.format("%s/%04d/foo.apk", mCeDir, n), "CLC");
        }
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, LOADING_PKG_NAME, clcByDexFile);
        assertThat(mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME))
                .hasSize(MAX_SECONDARY_DEX_FILES_PER_OWNER_FOR_TESTING);
    }

    private AndroidPackage createPackage(String packageName) {
        AndroidPackage pkg = mock(AndroidPackage.class);
        lenient().when(pkg.getStorageUuid()).thenReturn(StorageManager.UUID_DEFAULT);

        var baseSplit = mock(AndroidPackageSplit.class);
        lenient()
                .when(baseSplit.getPath())
                .thenReturn("/somewhere/app/" + packageName + "/base.apk");
        lenient().when(baseSplit.isHasCode()).thenReturn(true);
        lenient().when(baseSplit.getClassLoaderName()).thenReturn(PathClassLoader.class.getName());

        var split0 = mock(AndroidPackageSplit.class);
        lenient().when(split0.getName()).thenReturn("split_0");
        lenient()
                .when(split0.getPath())
                .thenReturn("/somewhere/app/" + packageName + "/split_0.apk");
        lenient().when(split0.isHasCode()).thenReturn(true);

        lenient().when(pkg.getSplits()).thenReturn(List.of(baseSplit, split0));

        return pkg;
    }

    private PackageState createPackageState(
            String packageName, String primaryAbi, boolean hasPackage) {
        PackageState pkgState = mock(PackageState.class);
        lenient().when(pkgState.getPackageName()).thenReturn(packageName);
        AndroidPackage pkg = createPackage(packageName);
        lenient().when(pkgState.getAndroidPackage()).thenReturn(hasPackage ? pkg : null);
        lenient().when(pkgState.getPrimaryCpuAbi()).thenReturn(primaryAbi);
        return pkgState;
    }

    private void addPackage(String packageName, PackageState pkgState) {
        lenient().when(mSnapshot.getPackageState(packageName)).thenReturn(pkgState);
        mPackageStates.put(packageName, pkgState);
    }

    private void removePackage(String packageName) {
        lenient().when(mSnapshot.getPackageState(packageName)).thenReturn(null);
        mPackageStates.remove(packageName);
    }
}
