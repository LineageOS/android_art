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

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.SystemApi;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Binder;
import android.os.Build;
import android.os.Environment;
import android.os.Process;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.os.UserHandle;
import android.util.Log;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.Immutable;
import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.model.DetailedDexInfo;
import com.android.server.art.model.DexContainerFileUseInfo;
import com.android.server.art.proto.DexUseProto;
import com.android.server.art.proto.Int32Value;
import com.android.server.art.proto.PackageDexUseProto;
import com.android.server.art.proto.PrimaryDexUseProto;
import com.android.server.art.proto.PrimaryDexUseRecordProto;
import com.android.server.art.proto.SecondaryDexUseProto;
import com.android.server.art.proto.SecondaryDexUseRecordProto;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.AndroidPackageSplit;
import com.android.server.pm.pkg.PackageState;

import com.google.auto.value.AutoValue;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.stream.Collectors;

/**
 * A singleton class that maintains the information about dex uses. This class is thread-safe.
 *
 * This class collects data sent directly by apps, and hence the data should be trusted as little as
 * possible.
 *
 * To avoid overwriting data, {@link #load()} must be called exactly once, during initialization.
 *
 * @hide
 */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class DexUseManagerLocal {
    private static final String TAG = ArtManagerLocal.TAG;
    private static final String FILENAME = "/data/system/package-dex-usage.pb";

    /**
     * The minimum interval between disk writes.
     *
     * In practice, the interval will be much longer because we use a debouncer to postpone the disk
     * write to the end of a series of changes. Note that in theory we could postpone the disk write
     * indefinitely, and therefore we could lose data if the device isn't shut down in the normal
     * way, but that's fine because the data isn't crucial and is recoverable.
     *
     * @hide
     */
    @VisibleForTesting public static final long INTERVAL_MS = 15_000;

    // Impose a limit on the input accepted by notifyDexContainersLoaded per owning package.
    /** @hide */
    @VisibleForTesting public static final int MAX_PATH_LENGTH = 4096;
    /** @hide */
    @VisibleForTesting public static final int MAX_CLASS_LOADER_CONTEXT_LENGTH = 10000;
    /** @hide */
    private static final int MAX_SECONDARY_DEX_FILES_PER_OWNER = 500;

    private static final Object sLock = new Object();
    @GuardedBy("sLock") @Nullable private static DexUseManagerLocal sInstance = null;

    @NonNull private final Injector mInjector;
    @NonNull private final Debouncer mDebouncer;

    private final Object mLock = new Object();
    @GuardedBy("mLock") @NonNull private DexUse mDexUse; // Initialized by `load`.
    @GuardedBy("mLock") private int mRevision = 0;
    @GuardedBy("mLock") private int mLastCommittedRevision = 0;
    @GuardedBy("mLock")
    @NonNull
    private SecondaryDexLocationManager mSecondaryDexLocationManager =
            new SecondaryDexLocationManager();

    /**
     * Creates the singleton instance.
     *
     * Only {@code SystemServer} should create the instance and register it in {@link
     * LocalManagerRegistry}. Other API users should obtain the instance from {@link
     * LocalManagerRegistry}.
     *
     * In practice, it must be created and registered in {@link LocalManagerRegistry} before {@code
     * PackageManagerService} starts because {@code PackageManagerService} needs it as soon as it
     * starts. It's safe to create an instance early because it doesn't depend on anything else.
     *
     * @param context the system server context
     * @throws IllegalStateException if the instance is already created
     * @throws NullPointerException if required dependencies are missing
     */
    @NonNull
    public static DexUseManagerLocal createInstance(@NonNull Context context) {
        synchronized (sLock) {
            if (sInstance != null) {
                throw new IllegalStateException("DexUseManagerLocal is already created");
            }
            sInstance = new DexUseManagerLocal(context);
            return sInstance;
        }
    }

    private DexUseManagerLocal(@NonNull Context context) {
        this(new Injector(context));
    }

    /** @hide */
    @VisibleForTesting
    public DexUseManagerLocal(@NonNull Injector injector) {
        mInjector = injector;
        mDebouncer = new Debouncer(INTERVAL_MS, mInjector::createScheduledExecutor);
        load();
    }

    /** Notifies dex use manager that {@link Context#registerReceiver} is ready for use. */
    public void systemReady() {
        // Save the data when the device is being shut down. The receiver is blocking, with a
        // 10s timeout.
        mInjector.getContext().registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                context.unregisterReceiver(this);
                save();
            }
        }, new IntentFilter(Intent.ACTION_SHUTDOWN));
    }

    /**
     * Returns the information about the use of all secondary dex files owned by the given package,
     * or an empty list if the package does not own any secondary dex file or it does not exist.
     */
    @NonNull
    public List<DexContainerFileUseInfo> getSecondaryDexContainerFileUseInfo(
            @NonNull String packageName) {
        return getSecondaryDexInfo(packageName)
                .stream()
                .map(info
                        -> DexContainerFileUseInfo.create(info.dexPath(), info.userHandle(),
                                info.loaders()
                                        .stream()
                                        .map(loader -> loader.loadingPackageName())
                                        .collect(Collectors.toSet())))
                .collect(Collectors.toList());
    }

    /**
     * Returns all entities that load the given primary dex file owned by the given package.
     *
     * @hide
     */
    @NonNull
    public Set<DexLoader> getPrimaryDexLoaders(
            @NonNull String packageName, @NonNull String dexPath) {
        synchronized (mLock) {
            PackageDexUse packageDexUse =
                    mDexUse.mPackageDexUseByOwningPackageName.get(packageName);
            if (packageDexUse == null) {
                return Set.of();
            }
            PrimaryDexUse primaryDexUse = packageDexUse.mPrimaryDexUseByDexFile.get(dexPath);
            if (primaryDexUse == null) {
                return Set.of();
            }
            return Set.copyOf(primaryDexUse.mRecordByLoader.keySet());
        }
    }

    /**
     * Returns whether a primary dex file owned by the given package is used by other apps.
     *
     * @hide
     */
    public boolean isPrimaryDexUsedByOtherApps(
            @NonNull String packageName, @NonNull String dexPath) {
        return isUsedByOtherApps(getPrimaryDexLoaders(packageName, dexPath), packageName);
    }

    /**
     * Returns the basic information about all secondary dex files owned by the given package. This
     * method doesn't take dex file visibility into account, so it can only be used for debugging
     * purpose, such as dumpsys.
     *
     * @see #getCheckedSecondaryDexInfo(String)
     * @hide
     */
    public @NonNull List<? extends SecondaryDexInfo> getSecondaryDexInfo(
            @NonNull String packageName) {
        return getSecondaryDexInfoImpl(
                packageName, false /* checkDexFile */, false /* excludeObsoleteDexesAndLoaders */);
    }

    /**
     * Same as above, but requires disk IO, and returns the detailed information, including dex file
     * visibility.
     *
     * @param excludeObsoleteDexesAndLoaders If true, excludes secondary dex files and loaders based
     *         on file visibility. More specifically, excludes loaders that can no longer load a
     *         secondary dex file due to a file visibility change, and excludes secondary dex files
     *         that are not found or only have obsolete loaders
     *
     * @hide
     */
    public @NonNull List<CheckedSecondaryDexInfo> getCheckedSecondaryDexInfo(
            @NonNull String packageName, boolean excludeObsoleteDexesAndLoaders) {
        return getSecondaryDexInfoImpl(
                packageName, true /* checkDexFile */, excludeObsoleteDexesAndLoaders);
    }

    /**
     * Returns the last time the package was used, or 0 if the package has never been used.
     *
     * @hide
     */
    public long getPackageLastUsedAtMs(@NonNull String packageName) {
        synchronized (mLock) {
            PackageDexUse packageDexUse =
                    mDexUse.mPackageDexUseByOwningPackageName.get(packageName);
            if (packageDexUse == null) {
                return 0;
            }
            long primaryLastUsedAtMs =
                    packageDexUse.mPrimaryDexUseByDexFile.values()
                            .stream()
                            .flatMap(primaryDexUse
                                    -> primaryDexUse.mRecordByLoader.values().stream())
                            .map(record -> record.mLastUsedAtMs)
                            .max(Long::compare)
                            .orElse(0l);
            long secondaryLastUsedAtMs =
                    packageDexUse.mSecondaryDexUseByDexFile.values()
                            .stream()
                            .flatMap(secondaryDexUse
                                    -> secondaryDexUse.mRecordByLoader.values().stream())
                            .map(record -> record.mLastUsedAtMs)
                            .max(Long::compare)
                            .orElse(0l);
            return Math.max(primaryLastUsedAtMs, secondaryLastUsedAtMs);
        }
    }

    /**
     * @param checkDexFile if true, check the existence and visibility of the dex files. Note that
     *         the value of the {@link CheckedSecondaryDexInfo#fileVisibility()} field is undefined
     *         if this argument is false
     * @param excludeObsoleteDexesAndLoaders see {@link #getCheckedSecondaryDexInfo}. Only takes
     *         effect if {@code checkDexFile} is true
     */
    private @NonNull List<CheckedSecondaryDexInfo> getSecondaryDexInfoImpl(
            @NonNull String packageName, boolean checkDexFile,
            boolean excludeObsoleteDexesAndLoaders) {
        synchronized (mLock) {
            PackageDexUse packageDexUse =
                    mDexUse.mPackageDexUseByOwningPackageName.get(packageName);
            if (packageDexUse == null) {
                return List.of();
            }
            var results = new ArrayList<CheckedSecondaryDexInfo>();
            for (var entry : packageDexUse.mSecondaryDexUseByDexFile.entrySet()) {
                String dexPath = entry.getKey();
                SecondaryDexUse secondaryDexUse = entry.getValue();

                @FileVisibility
                int visibility = checkDexFile ? getDexFileVisibility(dexPath)
                                              : FileVisibility.OTHER_READABLE;
                if (visibility == FileVisibility.NOT_FOUND && excludeObsoleteDexesAndLoaders) {
                    continue;
                }

                Map<DexLoader, SecondaryDexUseRecord> filteredRecordByLoader;
                if (visibility == FileVisibility.OTHER_READABLE
                        || !excludeObsoleteDexesAndLoaders) {
                    filteredRecordByLoader = secondaryDexUse.mRecordByLoader;
                } else {
                    // Only keep the entry that belongs to the same app.
                    DexLoader sameApp = DexLoader.create(packageName, false /* isolatedProcess */);
                    SecondaryDexUseRecord record = secondaryDexUse.mRecordByLoader.get(sameApp);
                    filteredRecordByLoader = record != null ? Map.of(sameApp, record) : Map.of();
                }
                if (filteredRecordByLoader.isEmpty()) {
                    continue;
                }
                List<String> distinctClcList =
                        filteredRecordByLoader.values()
                                .stream()
                                .map(record -> Utils.assertNonEmpty(record.mClassLoaderContext))
                                .filter(clc
                                        -> !clc.equals(
                                                SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT))
                                .distinct()
                                .collect(Collectors.toList());
                String clc;
                if (distinctClcList.size() == 0) {
                    clc = SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT;
                } else if (distinctClcList.size() == 1) {
                    clc = distinctClcList.get(0);
                } else {
                    // If there are more than one class loader contexts, we can't dexopt the dex
                    // file.
                    clc = SecondaryDexInfo.VARYING_CLASS_LOADER_CONTEXTS;
                }
                // Although we filter out unsupported CLCs above, `distinctAbiNames` and `loaders`
                // still need to take apps with unsupported CLCs into account because the vdex file
                // is still usable to them.
                Set<String> distinctAbiNames =
                        filteredRecordByLoader.values()
                                .stream()
                                .map(record -> Utils.assertNonEmpty(record.mAbiName))
                                .collect(Collectors.toSet());
                Set<DexLoader> loaders = Set.copyOf(filteredRecordByLoader.keySet());
                results.add(CheckedSecondaryDexInfo.create(dexPath,
                        Objects.requireNonNull(secondaryDexUse.mUserHandle), clc, distinctAbiNames,
                        loaders, isUsedByOtherApps(loaders, packageName), visibility));
            }
            return Collections.unmodifiableList(results);
        }
    }

    /**
     * Notifies ART Service that a list of dex container files have been loaded.
     *
     * ART Service uses this information to:
     * <ul>
     *   <li>Determine whether an app is used by another app
     *   <li>Record which secondary dex container files to dexopt and how to dexopt them
     * </ul>
     *
     * @param loadingPackageName the name of the package who performs the load. ART Service assumes
     *         that this argument has been validated that it exists in the snapshot and matches the
     *         calling UID
     * @param classLoaderContextByDexContainerFile a map from dex container files' absolute paths to
     *         the string representations of the class loader contexts used to load them
     * @throws IllegalArgumentException if {@code classLoaderContextByDexContainerFile} contains
     *         invalid entries
     */
    public void notifyDexContainersLoaded(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String loadingPackageName,
            @NonNull Map<String, String> classLoaderContextByDexContainerFile) {
        // "android" comes from `SystemServerDexLoadReporter`. ART Services doesn't need to handle
        // this case because it doesn't compile system server and system server isn't allowed to
        // load artifacts produced by ART Services.
        if (loadingPackageName.equals(Utils.PLATFORM_PACKAGE_NAME)) {
            return;
        }

        validateInputs(snapshot, loadingPackageName, classLoaderContextByDexContainerFile);

        // TODO(jiakaiz): Investigate whether it should also be considered as isolated process if
        // `Process.isSdkSandboxUid` returns true.
        boolean isolatedProcess = Process.isIsolatedUid(Binder.getCallingUid());
        long lastUsedAtMs = mInjector.getCurrentTimeMillis();

        for (var entry : classLoaderContextByDexContainerFile.entrySet()) {
            String dexPath = Utils.assertNonEmpty(entry.getKey());
            String classLoaderContext = Utils.assertNonEmpty(entry.getValue());
            String owningPackageName = findOwningPackage(snapshot, loadingPackageName,
                    (pkgState) -> isOwningPackageForPrimaryDex(pkgState, dexPath));
            if (owningPackageName != null) {
                addPrimaryDexUse(owningPackageName, dexPath, loadingPackageName, isolatedProcess,
                        lastUsedAtMs);
                continue;
            }
            Path path = Paths.get(dexPath);
            synchronized (mLock) {
                owningPackageName = findOwningPackage(snapshot, loadingPackageName,
                        (pkgState) -> isOwningPackageForSecondaryDexLocked(pkgState, path));
            }
            if (owningPackageName != null) {
                PackageState loadingPkgState =
                        Utils.getPackageStateOrThrow(snapshot, loadingPackageName);
                // An app is always launched with its primary ABI.
                Utils.Abi abi = Utils.getPrimaryAbi(loadingPkgState);
                addSecondaryDexUse(owningPackageName, dexPath, loadingPackageName, isolatedProcess,
                        classLoaderContext, abi.name(), lastUsedAtMs);
                continue;
            }
            // It is expected that a dex file isn't owned by any package. For example, the dex
            // file could be a shared library jar.
        }
    }

    @Nullable
    private static String findOwningPackage(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String loadingPackageName,
            @NonNull Function<PackageState, Boolean> predicate) {
        // Most likely, the package is loading its own dex file, so we check this first as an
        // optimization.
        PackageState loadingPkgState = Utils.getPackageStateOrThrow(snapshot, loadingPackageName);
        if (predicate.apply(loadingPkgState)) {
            return loadingPkgState.getPackageName();
        }

        for (PackageState pkgState : snapshot.getPackageStates().values()) {
            if (predicate.apply(pkgState)) {
                return pkgState.getPackageName();
            }
        }

        return null;
    }

    private static boolean isOwningPackageForPrimaryDex(
            @NonNull PackageState pkgState, @NonNull String dexPath) {
        AndroidPackage pkg = pkgState.getAndroidPackage();
        if (pkg == null) {
            return false;
        }
        List<AndroidPackageSplit> splits = pkg.getSplits();
        for (int i = 0; i < splits.size(); i++) {
            if (splits.get(i).getPath().equals(dexPath)) {
                return true;
            }
        }
        return false;
    }

    @GuardedBy("mLock")
    private boolean isOwningPackageForSecondaryDexLocked(
            @NonNull PackageState pkgState, @NonNull Path dexPath) {
        UserHandle userHandle = Binder.getCallingUserHandle();
        List<Path> locations = mSecondaryDexLocationManager.getLocations(pkgState, userHandle);
        for (int i = 0; i < locations.size(); i++) {
            if (dexPath.startsWith(locations.get(i))) {
                return true;
            }
        }
        return false;
    }

    private void addPrimaryDexUse(@NonNull String owningPackageName, @NonNull String dexPath,
            @NonNull String loadingPackageName, boolean isolatedProcess, long lastUsedAtMs) {
        synchronized (mLock) {
            PrimaryDexUseRecord record =
                    mDexUse.mPackageDexUseByOwningPackageName
                            .computeIfAbsent(owningPackageName, k -> new PackageDexUse())
                            .mPrimaryDexUseByDexFile
                            .computeIfAbsent(dexPath, k -> new PrimaryDexUse())
                            .mRecordByLoader.computeIfAbsent(
                                    DexLoader.create(loadingPackageName, isolatedProcess),
                                    k -> new PrimaryDexUseRecord());
            record.mLastUsedAtMs = lastUsedAtMs;
            mRevision++;
        }
        maybeSaveAsync();
    }

    private void addSecondaryDexUse(@NonNull String owningPackageName, @NonNull String dexPath,
            @NonNull String loadingPackageName, boolean isolatedProcess,
            @NonNull String classLoaderContext, @NonNull String abiName, long lastUsedAtMs) {
        DexLoader loader = DexLoader.create(loadingPackageName, isolatedProcess);
        synchronized (mLock) {
            PackageDexUse packageDexUse = mDexUse.mPackageDexUseByOwningPackageName.computeIfAbsent(
                    owningPackageName, k -> new PackageDexUse());
            SecondaryDexUse secondaryDexUse =
                    packageDexUse.mSecondaryDexUseByDexFile.computeIfAbsent(dexPath, k -> {
                        if (packageDexUse.mSecondaryDexUseByDexFile.size()
                                >= mInjector.getMaxSecondaryDexFilesPerOwner()) {
                            Log.w(TAG, "Not recording too many secondary dex use entries for "
                                    + owningPackageName);
                            return null;
                        }
                        return new SecondaryDexUse();
                    });
            if (secondaryDexUse == null) {
                return;
            }
            secondaryDexUse.mUserHandle = Binder.getCallingUserHandle();
            SecondaryDexUseRecord record =
                    secondaryDexUse.mRecordByLoader.computeIfAbsent(
                            loader, k -> new SecondaryDexUseRecord());
            record.mClassLoaderContext = classLoaderContext;
            record.mAbiName = abiName;
            record.mLastUsedAtMs = lastUsedAtMs;
            mRevision++;
        }
        maybeSaveAsync();
    }

    /** @hide */
    public @NonNull String dump() {
        var builder = DexUseProto.newBuilder();
        synchronized (mLock) {
            mDexUse.toProto(builder);
        }
        return builder.build().toString();
    }

    private void save() {
        var builder = DexUseProto.newBuilder();
        int thisRevision;
        synchronized (mLock) {
            if (mRevision <= mLastCommittedRevision) {
                return;
            }
            mDexUse.toProto(builder);
            thisRevision = mRevision;
        }
        var file = new File(mInjector.getFilename());
        File tempFile = null;
        try {
            tempFile = File.createTempFile(file.getName(), null /* suffix */, file.getParentFile());
            try (OutputStream out = new FileOutputStream(tempFile.getPath())) {
                builder.build().writeTo(out);
            }
            synchronized (mLock) {
                // Check revision again in case `mLastCommittedRevision` has changed since the check
                // above, to avoid ABA race.
                if (thisRevision > mLastCommittedRevision) {
                    Files.move(tempFile.toPath(), file.toPath(),
                            StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
                    mLastCommittedRevision = thisRevision;
                }
            }
        } catch (IOException e) {
            Log.e(TAG, "Failed to save dex use data", e);
        } finally {
            Utils.deleteIfExistsSafe(tempFile);
        }
    }

    private void maybeSaveAsync() {
        mDebouncer.maybeRunAsync(this::save);
    }

    /** This should only be called during initialization. */
    private void load() {
        DexUseProto proto = null;
        try (InputStream in = new FileInputStream(mInjector.getFilename())) {
            proto = DexUseProto.parseFrom(in);
        } catch (IOException e) {
            // Nothing else we can do but to start from scratch.
            Log.e(TAG, "Failed to load dex use data", e);
        }
        synchronized (mLock) {
            if (mDexUse != null) {
                throw new IllegalStateException("Load has already been attempted");
            }
            mDexUse = new DexUse();
            if (proto != null) {
                mDexUse.fromProto(
                        proto, ArtJni::validateDexPath, ArtJni::validateClassLoaderContext);
            }
        }
    }

    private static boolean isUsedByOtherApps(
            @NonNull Set<DexLoader> loaders, @NonNull String owningPackageName) {
        return loaders.stream().anyMatch(loader -> isLoaderOtherApp(loader, owningPackageName));
    }

    /**
     * Returns true if {@code loader} is considered as "other app" (i.e., its process UID is
     * different from the UID of the package represented by {@code owningPackageName}).
     *
     * @hide
     */
    public static boolean isLoaderOtherApp(
            @NonNull DexLoader loader, @NonNull String owningPackageName) {
        // If the dex file is loaded by an isolated process of the same app, it can also be
        // considered as "used by other apps" because isolated processes are sandboxed and can only
        // read world readable files, so they need the dexopt artifacts to be world readable. An
        // example of such a package is webview.
        return !loader.loadingPackageName().equals(owningPackageName) || loader.isolatedProcess();
    }

    private void validateInputs(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String loadingPackageName,
            @NonNull Map<String, String> classLoaderContextByDexContainerFile) {
        if (classLoaderContextByDexContainerFile.isEmpty()) {
            throw new IllegalArgumentException("Nothing to record");
        }

        for (var entry : classLoaderContextByDexContainerFile.entrySet()) {
            String dexPath = entry.getKey();
            String classLoaderContext = entry.getValue();
            Utils.assertNonEmpty(dexPath);
            if (dexPath.length() > MAX_PATH_LENGTH) {
                throw new IllegalArgumentException(
                        "Dex path too long - exceeds " + MAX_PATH_LENGTH + " chars");
            }
            String errorMsg = ArtJni.validateDexPath(dexPath);
            if (errorMsg != null) {
                throw new IllegalArgumentException(errorMsg);
            }
            Utils.assertNonEmpty(classLoaderContext);
            if (classLoaderContext.length() > MAX_CLASS_LOADER_CONTEXT_LENGTH) {
                throw new IllegalArgumentException("Class loader context too long - exceeds "
                        + MAX_CLASS_LOADER_CONTEXT_LENGTH + " chars");
            }
            errorMsg = ArtJni.validateClassLoaderContext(dexPath, classLoaderContext);
            if (errorMsg != null) {
                throw new IllegalArgumentException(errorMsg);
            }
        }

        // TODO(b/253570365): Make the validation more strict.
    }

    private @FileVisibility int getDexFileVisibility(@NonNull String dexPath) {
        try {
            return mInjector.getArtd().getDexFileVisibility(dexPath);
        } catch (ServiceSpecificException | RemoteException e) {
            Log.e(TAG, "Failed to get visibility of " + dexPath, e);
            return FileVisibility.NOT_FOUND;
        }
    }

    /** @hide */
    @Nullable
    public String getSecondaryClassLoaderContext(
            @NonNull String owningPackageName, @NonNull String dexFile, @NonNull DexLoader loader) {
        synchronized (mLock) {
            return Optional
                    .ofNullable(mDexUse.mPackageDexUseByOwningPackageName.get(owningPackageName))
                    .map(packageDexUse -> packageDexUse.mSecondaryDexUseByDexFile.get(dexFile))
                    .map(secondaryDexUse -> secondaryDexUse.mRecordByLoader.get(loader))
                    .map(record -> record.mClassLoaderContext)
                    .orElse(null);
        }
    }

    /**
     * Cleans up obsolete information about dex files and packages that no longer exist.
     *
     * @hide
     */
    public void cleanup() {
        Set<String> packageNames = mInjector.getAllPackageNames();
        Map<String, Integer> dexFileVisibilityByName = new HashMap<>();

        // Scan the data in two passes to avoid holding the lock during I/O.
        synchronized (mLock) {
            for (PackageDexUse packageDexUse : mDexUse.mPackageDexUseByOwningPackageName.values()) {
                for (String dexFile : packageDexUse.mPrimaryDexUseByDexFile.keySet()) {
                    dexFileVisibilityByName.put(dexFile, FileVisibility.NOT_FOUND);
                }
                for (String dexFile : packageDexUse.mSecondaryDexUseByDexFile.keySet()) {
                    dexFileVisibilityByName.put(dexFile, FileVisibility.NOT_FOUND);
                }
            }
        }

        for (var entry : dexFileVisibilityByName.entrySet()) {
            entry.setValue(getDexFileVisibility(entry.getKey()));
        }

        synchronized (mLock) {
            for (var it = mDexUse.mPackageDexUseByOwningPackageName.entrySet().iterator();
                    it.hasNext();) {
                Map.Entry<String, PackageDexUse> entry = it.next();
                String owningPackageName = entry.getKey();
                PackageDexUse packageDexUse = entry.getValue();

                if (!packageNames.contains(owningPackageName)) {
                    // Remove information about the non-existing owning package.
                    it.remove();
                    mRevision++;
                    continue;
                }

                cleanupPrimaryDexUsesLocked(packageDexUse.mPrimaryDexUseByDexFile, packageNames,
                        dexFileVisibilityByName, owningPackageName);

                cleanupSecondaryDexUsesLocked(packageDexUse.mSecondaryDexUseByDexFile, packageNames,
                        dexFileVisibilityByName, owningPackageName);

                if (packageDexUse.mPrimaryDexUseByDexFile.isEmpty()
                        && packageDexUse.mSecondaryDexUseByDexFile.isEmpty()) {
                    it.remove();
                    mRevision++;
                }
            }
        }

        maybeSaveAsync();
    }

    @GuardedBy("mLock")
    private void cleanupPrimaryDexUsesLocked(@NonNull Map<String, PrimaryDexUse> primaryDexUses,
            @NonNull Set<String> packageNames,
            @NonNull Map<String, Integer> dexFileVisibilityByName,
            @NonNull String owningPackageName) {
        for (var it = primaryDexUses.entrySet().iterator(); it.hasNext();) {
            Map.Entry<String, PrimaryDexUse> entry = it.next();
            String dexFile = entry.getKey();
            PrimaryDexUse primaryDexUse = entry.getValue();

            if (!dexFileVisibilityByName.containsKey(dexFile)) {
                // This can only happen when the file is added after the first pass. We can just
                // keep it as-is and check it in the next `cleanup` run.
                continue;
            }

            @FileVisibility int visibility = dexFileVisibilityByName.get(dexFile);

            if (visibility == FileVisibility.NOT_FOUND) {
                // Remove information about the non-existing dex files.
                it.remove();
                mRevision++;
                continue;
            }

            cleanupRecordsLocked(
                    primaryDexUse.mRecordByLoader, packageNames, visibility, owningPackageName);

            if (primaryDexUse.mRecordByLoader.isEmpty()) {
                it.remove();
                mRevision++;
            }
        }
    }

    @GuardedBy("mLock")
    private void cleanupSecondaryDexUsesLocked(
            @NonNull Map<String, SecondaryDexUse> secondaryDexUses,
            @NonNull Set<String> packageNames,
            @NonNull Map<String, Integer> dexFileVisibilityByName,
            @NonNull String owningPackageName) {
        for (var it = secondaryDexUses.entrySet().iterator(); it.hasNext();) {
            Map.Entry<String, SecondaryDexUse> entry = it.next();
            String dexFile = entry.getKey();
            SecondaryDexUse secondaryDexUse = entry.getValue();

            if (!dexFileVisibilityByName.containsKey(dexFile)) {
                // This can only happen when the file is added after the first pass. We can just
                // keep it as-is and check it in the next `cleanup` run.
                continue;
            }

            @FileVisibility int visibility = dexFileVisibilityByName.get(dexFile);

            // Remove information about non-existing dex files.
            if (visibility == FileVisibility.NOT_FOUND) {
                it.remove();
                mRevision++;
                continue;
            }

            cleanupRecordsLocked(
                    secondaryDexUse.mRecordByLoader, packageNames, visibility, owningPackageName);

            if (secondaryDexUse.mRecordByLoader.isEmpty()) {
                it.remove();
                mRevision++;
            }
        }
    }

    @GuardedBy("mLock")
    private void cleanupRecordsLocked(@NonNull Map<DexLoader, ?> records,
            @NonNull Set<String> packageNames, @FileVisibility int visibility,
            @NonNull String owningPackageName) {
        for (var it = records.entrySet().iterator(); it.hasNext();) {
            Map.Entry<DexLoader, ?> entry = it.next();
            DexLoader loader = entry.getKey();

            if (!packageNames.contains(loader.loadingPackageName())) {
                // Remove information about the non-existing loading package.
                it.remove();
                mRevision++;
                continue;
            }

            if (visibility == FileVisibility.NOT_OTHER_READABLE
                    && isLoaderOtherApp(loader, owningPackageName)) {
                // The visibility must have changed since the last load. The loader cannot load this
                // dex file anymore.
                it.remove();
                mRevision++;
                continue;
            }
        }
    }

    /**
     * Basic information about a secondary dex file (an APK or JAR file that an app adds to its
     * own data directory and loads dynamically).
     *
     * @hide
     */
    @Immutable
    public abstract static class SecondaryDexInfo implements DetailedDexInfo {
        // Special encoding used to denote a foreign ClassLoader was found when trying to encode
        // class loader contexts for each classpath element in a ClassLoader.
        // Must be in sync with `kUnsupportedClassLoaderContextEncoding` in
        // `art/runtime/class_loader_context.h`.
        public static final String UNSUPPORTED_CLASS_LOADER_CONTEXT =
                "=UnsupportedClassLoaderContext=";

        // Special encoding used to denote that a dex file is loaded by different packages with
        // different ClassLoader's. Only for display purpose (e.g., in dumpsys). This value is not
        // written to the file, and so far only used here.
        @VisibleForTesting
        public static final String VARYING_CLASS_LOADER_CONTEXTS = "=VaryingClassLoaderContexts=";

        /** The absolute path to the dex file within the user's app data directory. */
        public abstract @NonNull String dexPath();

        /**
         * The {@link UserHandle} that represents the human user who owns and loads the dex file. A
         * secondary dex file belongs to a specific human user, and only that user can load it.
         */
        public abstract @NonNull UserHandle userHandle();

        /**
         * A string describing the structure of the class loader that the dex file is loaded with,
         * or {@link #UNSUPPORTED_CLASS_LOADER_CONTEXT} or {@link #VARYING_CLASS_LOADER_CONTEXTS}.
         */
        public abstract @NonNull String displayClassLoaderContext();

        /**
         * A string describing the structure of the class loader that the dex file is loaded with,
         * or null if the class loader context is invalid.
         */
        public @Nullable String classLoaderContext() {
            return !displayClassLoaderContext().equals(UNSUPPORTED_CLASS_LOADER_CONTEXT)
                            && !displayClassLoaderContext().equals(VARYING_CLASS_LOADER_CONTEXTS)
                    ? displayClassLoaderContext()
                    : null;
        }

        /** The set of ABIs of the dex file is loaded with. Guaranteed to be non-empty. */
        public abstract @NonNull Set<String> abiNames();

        /** The set of entities that load the dex file. Guaranteed to be non-empty. */
        public abstract @NonNull Set<DexLoader> loaders();

        /** Returns whether the dex file is used by apps other than the app that owns it. */
        public abstract boolean isUsedByOtherApps();
    }

    /**
     * Detailed information about a secondary dex file (an APK or JAR file that an app adds to its
     * own data directory and loads dynamically). It contains the visibility of the dex file in
     * addition to what is in {@link SecondaryDexInfo}, but producing it requires disk IO.
     *
     * @hide
     */
    @Immutable
    @AutoValue
    public abstract static class CheckedSecondaryDexInfo
            extends SecondaryDexInfo implements DetailedDexInfo {
        static CheckedSecondaryDexInfo create(@NonNull String dexPath,
                @NonNull UserHandle userHandle, @NonNull String displayClassLoaderContext,
                @NonNull Set<String> abiNames, @NonNull Set<DexLoader> loaders,
                boolean isUsedByOtherApps, @FileVisibility int fileVisibility) {
            return new AutoValue_DexUseManagerLocal_CheckedSecondaryDexInfo(dexPath, userHandle,
                    displayClassLoaderContext, Collections.unmodifiableSet(abiNames),
                    Collections.unmodifiableSet(loaders), isUsedByOtherApps, fileVisibility);
        }

        /** Indicates the visibility of the dex file. */
        public abstract @FileVisibility int fileVisibility();
    }

    private static class DexUse {
        @NonNull Map<String, PackageDexUse> mPackageDexUseByOwningPackageName = new HashMap<>();

        void toProto(@NonNull DexUseProto.Builder builder) {
            for (var entry : mPackageDexUseByOwningPackageName.entrySet()) {
                var packageBuilder =
                        PackageDexUseProto.newBuilder().setOwningPackageName(entry.getKey());
                entry.getValue().toProto(packageBuilder);
                builder.addPackageDexUse(packageBuilder);
            }
        }

        void fromProto(@NonNull DexUseProto proto,
                @NonNull Function<String, String> validateDexPath,
                @NonNull BiFunction<String, String, String> validateClassLoaderContext) {
            for (PackageDexUseProto packageProto : proto.getPackageDexUseList()) {
                var packageDexUse = new PackageDexUse();
                packageDexUse.fromProto(packageProto, validateDexPath, validateClassLoaderContext);
                mPackageDexUseByOwningPackageName.put(
                        Utils.assertNonEmpty(packageProto.getOwningPackageName()), packageDexUse);
            }
        }
    }

    private static class PackageDexUse {
        /**
         * The keys are absolute paths to primary dex files of the owning package (the base APK and
         * split APKs).
         */
        @NonNull Map<String, PrimaryDexUse> mPrimaryDexUseByDexFile = new HashMap<>();

        /**
         * The keys are absolute paths to secondary dex files of the owning package (the APKs and
         * JARs in CE and DE directories).
         */
        @NonNull Map<String, SecondaryDexUse> mSecondaryDexUseByDexFile = new HashMap<>();

        void toProto(@NonNull PackageDexUseProto.Builder builder) {
            for (var entry : mPrimaryDexUseByDexFile.entrySet()) {
                var primaryBuilder = PrimaryDexUseProto.newBuilder().setDexFile(entry.getKey());
                entry.getValue().toProto(primaryBuilder);
                builder.addPrimaryDexUse(primaryBuilder);
            }
            for (var entry : mSecondaryDexUseByDexFile.entrySet()) {
                var secondaryBuilder = SecondaryDexUseProto.newBuilder().setDexFile(entry.getKey());
                entry.getValue().toProto(secondaryBuilder);
                builder.addSecondaryDexUse(secondaryBuilder);
            }
        }

        void fromProto(@NonNull PackageDexUseProto proto,
                @NonNull Function<String, String> validateDexPath,
                @NonNull BiFunction<String, String, String> validateClassLoaderContext) {
            for (PrimaryDexUseProto primaryProto : proto.getPrimaryDexUseList()) {
                var primaryDexUse = new PrimaryDexUse();
                primaryDexUse.fromProto(primaryProto);
                mPrimaryDexUseByDexFile.put(
                        Utils.assertNonEmpty(primaryProto.getDexFile()), primaryDexUse);
            }
            for (SecondaryDexUseProto secondaryProto : proto.getSecondaryDexUseList()) {
                String dexFile = Utils.assertNonEmpty(secondaryProto.getDexFile());

                // Skip invalid dex paths persisted by previous versions.
                String errorMsg = validateDexPath.apply(dexFile);
                if (errorMsg != null) {
                    Log.e(TAG, errorMsg);
                    continue;
                }

                var secondaryDexUse = new SecondaryDexUse();
                secondaryDexUse.fromProto(secondaryProto,
                        classLoaderContext
                        -> validateClassLoaderContext.apply(dexFile, classLoaderContext));
                mSecondaryDexUseByDexFile.put(dexFile, secondaryDexUse);
            }
        }
    }

    private static class PrimaryDexUse {
        @NonNull Map<DexLoader, PrimaryDexUseRecord> mRecordByLoader = new HashMap<>();

        void toProto(@NonNull PrimaryDexUseProto.Builder builder) {
            for (var entry : mRecordByLoader.entrySet()) {
                var recordBuilder =
                        PrimaryDexUseRecordProto.newBuilder()
                                .setLoadingPackageName(entry.getKey().loadingPackageName())
                                .setIsolatedProcess(entry.getKey().isolatedProcess());
                entry.getValue().toProto(recordBuilder);
                builder.addRecord(recordBuilder);
            }
        }

        void fromProto(@NonNull PrimaryDexUseProto proto) {
            for (PrimaryDexUseRecordProto recordProto : proto.getRecordList()) {
                var record = new PrimaryDexUseRecord();
                record.fromProto(recordProto);
                mRecordByLoader.put(
                        DexLoader.create(Utils.assertNonEmpty(recordProto.getLoadingPackageName()),
                                recordProto.getIsolatedProcess()),
                        record);
            }
        }
    }

    private static class SecondaryDexUse {
        @Nullable UserHandle mUserHandle = null;
        @NonNull Map<DexLoader, SecondaryDexUseRecord> mRecordByLoader = new HashMap<>();

        void toProto(@NonNull SecondaryDexUseProto.Builder builder) {
            builder.setUserId(Int32Value.newBuilder().setValue(mUserHandle.getIdentifier()));
            for (var entry : mRecordByLoader.entrySet()) {
                var recordBuilder =
                        SecondaryDexUseRecordProto.newBuilder()
                                .setLoadingPackageName(entry.getKey().loadingPackageName())
                                .setIsolatedProcess(entry.getKey().isolatedProcess());
                entry.getValue().toProto(recordBuilder);
                builder.addRecord(recordBuilder);
            }
        }

        void fromProto(@NonNull SecondaryDexUseProto proto,
                @NonNull Function<String, String> validateClassLoaderContext) {
            Utils.check(proto.hasUserId());
            mUserHandle = UserHandle.of(proto.getUserId().getValue());
            for (SecondaryDexUseRecordProto recordProto : proto.getRecordList()) {
                // Skip invalid class loader context persisted by previous versions.
                String errorMsg = validateClassLoaderContext.apply(
                        Utils.assertNonEmpty(recordProto.getClassLoaderContext()));
                if (errorMsg != null) {
                    Log.e(TAG, errorMsg);
                    continue;
                }

                var record = new SecondaryDexUseRecord();
                record.fromProto(recordProto);
                mRecordByLoader.put(
                        DexLoader.create(Utils.assertNonEmpty(recordProto.getLoadingPackageName()),
                                recordProto.getIsolatedProcess()),
                        record);
            }
        }
    }

    /**
     * Represents an entity that loads a dex file.
     *
     * @hide
     */
    @Immutable
    @AutoValue
    public abstract static class DexLoader implements Comparable<DexLoader> {
        static DexLoader create(@NonNull String loadingPackageName, boolean isolatedProcess) {
            return new AutoValue_DexUseManagerLocal_DexLoader(loadingPackageName, isolatedProcess);
        }

        abstract @NonNull String loadingPackageName();

        /** @see Process#isIsolatedUid(int) */
        abstract boolean isolatedProcess();

        @Override
        @NonNull
        public String toString() {
            return loadingPackageName() + (isolatedProcess() ? " (isolated)" : "");
        }

        @Override
        public int compareTo(DexLoader o) {
            return Comparator.comparing(DexLoader::loadingPackageName)
                    .thenComparing(DexLoader::isolatedProcess)
                    .compare(this, o);
        }
    }

    private static class PrimaryDexUseRecord {
        @Nullable long mLastUsedAtMs = 0;

        void toProto(@NonNull PrimaryDexUseRecordProto.Builder builder) {
            builder.setLastUsedAtMs(mLastUsedAtMs);
        }

        void fromProto(@NonNull PrimaryDexUseRecordProto proto) {
            mLastUsedAtMs = proto.getLastUsedAtMs();
            Utils.check(mLastUsedAtMs > 0);
        }
    }

    private static class SecondaryDexUseRecord {
        // An app constructs their own class loader to load a secondary dex file, so only itself
        // knows the class loader context. Therefore, we need to record the class loader context
        // reported by the app.
        @Nullable String mClassLoaderContext = null;
        @Nullable String mAbiName = null;
        @Nullable long mLastUsedAtMs = 0;

        void toProto(@NonNull SecondaryDexUseRecordProto.Builder builder) {
            builder.setClassLoaderContext(mClassLoaderContext)
                    .setAbiName(mAbiName)
                    .setLastUsedAtMs(mLastUsedAtMs);
        }

        void fromProto(@NonNull SecondaryDexUseRecordProto proto) {
            mClassLoaderContext = Utils.assertNonEmpty(proto.getClassLoaderContext());
            mAbiName = Utils.assertNonEmpty(proto.getAbiName());
            mLastUsedAtMs = proto.getLastUsedAtMs();
            Utils.check(mLastUsedAtMs > 0);
        }
    }

    // TODO(b/278697552): Consider removing the cache or moving it to `Environment`.
    static class SecondaryDexLocationManager {
        private @NonNull Map<CacheKey, CacheValue> mCache = new HashMap<>();

        public @NonNull List<Path> getLocations(
                @NonNull PackageState pkgState, @NonNull UserHandle userHandle) {
            AndroidPackage pkg = pkgState.getAndroidPackage();
            if (pkg == null) {
                return List.of();
            }

            UUID storageUuid = pkg.getStorageUuid();
            String packageName = pkgState.getPackageName();

            CacheKey cacheKey = CacheKey.create(packageName, userHandle);
            CacheValue cacheValue = mCache.get(cacheKey);
            if (cacheValue != null && cacheValue.storageUuid().equals(storageUuid)) {
                return cacheValue.locations();
            }

            File ceDir = Environment.getDataCePackageDirectoryForUser(
                    storageUuid, userHandle, packageName);
            File deDir = Environment.getDataDePackageDirectoryForUser(
                    storageUuid, userHandle, packageName);
            List<Path> locations = List.of(ceDir.toPath(), deDir.toPath());
            mCache.put(cacheKey, CacheValue.create(locations, storageUuid));
            return locations;
        }

        @Immutable
        @AutoValue
        abstract static class CacheKey {
            static CacheKey create(@NonNull String packageName, @NonNull UserHandle userHandle) {
                return new AutoValue_DexUseManagerLocal_SecondaryDexLocationManager_CacheKey(
                        packageName, userHandle);
            }

            abstract @NonNull String packageName();

            abstract @NonNull UserHandle userHandle();
        }

        @Immutable
        @AutoValue
        abstract static class CacheValue {
            static CacheValue create(@NonNull List<Path> locations, @NonNull UUID storageUuid) {
                return new AutoValue_DexUseManagerLocal_SecondaryDexLocationManager_CacheValue(
                        locations, storageUuid);
            }

            abstract @NonNull List<Path> locations();

            abstract @NonNull UUID storageUuid();
        }
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final Context mContext;

        Injector(@NonNull Context context) {
            mContext = context;

            // Call the getters for various dependencies, to ensure correct initialization order.
            ArtModuleServiceInitializer.getArtModuleServiceManager();
            getPackageManagerLocal();
        }

        @NonNull
        public IArtd getArtd() {
            return ArtdRefCache.getInstance().getArtd();
        }

        public long getCurrentTimeMillis() {
            return System.currentTimeMillis();
        }

        @NonNull
        public String getFilename() {
            return FILENAME;
        }

        @NonNull
        public ScheduledExecutorService createScheduledExecutor() {
            return Executors.newScheduledThreadPool(1 /* corePoolSize */);
        }

        @NonNull
        public Context getContext() {
            return mContext;
        }

        @NonNull
        public Set<String> getAllPackageNames() {
            try (PackageManagerLocal.UnfilteredSnapshot snapshot =
                            getPackageManagerLocal().withUnfilteredSnapshot()) {
                return new HashSet<>(snapshot.getPackageStates().keySet());
            }
        }

        @NonNull
        private PackageManagerLocal getPackageManagerLocal() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(PackageManagerLocal.class));
        }

        public int getMaxSecondaryDexFilesPerOwner() {
            return MAX_SECONDARY_DEX_FILES_PER_OWNER;
        }
    }
}
