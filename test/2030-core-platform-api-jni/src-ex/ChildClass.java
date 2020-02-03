/*
 * Copyright (C) 2017 The Android Open Source Project
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

import dalvik.system.VMRuntime;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodType;
import java.util.function.Consumer;

public class ChildClass {
  enum PrimitiveType {
    TInteger('I', Integer.TYPE, Integer.valueOf(0)),
    TLong('J', Long.TYPE, Long.valueOf(0)),
    TFloat('F', Float.TYPE, Float.valueOf(0)),
    TDouble('D', Double.TYPE, Double.valueOf(0)),
    TBoolean('Z', Boolean.TYPE, Boolean.valueOf(false)),
    TByte('B', Byte.TYPE, Byte.valueOf((byte) 0)),
    TShort('S', Short.TYPE, Short.valueOf((short) 0)),
    TCharacter('C', Character.TYPE, Character.valueOf('0'));

    PrimitiveType(char shorty, Class klass, Object value) {
      mShorty = shorty;
      mClass = klass;
      mDefaultValue = value;
    }

    public char mShorty;
    public Class mClass;
    public Object mDefaultValue;
  }

  enum Hiddenness {
    Whitelist(PrimitiveType.TShort),
    LightGreylist(PrimitiveType.TBoolean),
    DarkGreylist(PrimitiveType.TByte),
    Blacklist(PrimitiveType.TCharacter),
    BlacklistAndCorePlatformApi(PrimitiveType.TInteger);

    Hiddenness(PrimitiveType type) { mAssociatedType = type; }
    public PrimitiveType mAssociatedType;
  }

  enum Visibility {
    Public(PrimitiveType.TInteger),
    Package(PrimitiveType.TFloat),
    Protected(PrimitiveType.TLong),
    Private(PrimitiveType.TDouble);

    Visibility(PrimitiveType type) { mAssociatedType = type; }
    public PrimitiveType mAssociatedType;
  }

  enum Behaviour {
    Granted,
    Warning,
    Denied,
  }

  // This needs to be kept in sync with DexDomain in Main.
  enum DexDomain {
    CorePlatform,
    Platform,
    Application
  }

  private static final boolean booleanValues[] = new boolean[] { false, true };

  public static void runTest(String libFileName, int parentDomainOrdinal,
                             int childDomainOrdinal, int childNativeDomainOrdinal,
                             boolean everythingWhitelisted,
                             MethodHandle postSystemLoadHook) throws Throwable {
    System.load(libFileName);

    // Configure domain of native library.
    postSystemLoadHook.invokeExact(libFileName);

    parentDomain = DexDomain.values()[parentDomainOrdinal];
    childDomain = DexDomain.values()[childDomainOrdinal];
    childNativeDomain = DexDomain.values()[childNativeDomainOrdinal];

    configMessage = "parentDomain=" + parentDomain.name() +
            ", childDomain=" + childDomain.name() +
            ", childNativeDomain=" + childNativeDomain.name() +
            ", everythingWhitelisted=" + everythingWhitelisted;

    // Check expectations about loading into boot class path.
    boolean isParentInBoot = (ParentClass.class.getClassLoader().getParent() == null);
    boolean expectedParentInBoot = (parentDomain != DexDomain.Application);
    if (isParentInBoot != expectedParentInBoot) {
      throw new RuntimeException("Expected ParentClass " +
                                 (expectedParentInBoot ? "" : "not ") + "in boot class path");
    }
    boolean isChildInBoot = (ChildClass.class.getClassLoader().getParent() == null);
    boolean expectedChildInBoot = (childDomain != DexDomain.Application);
    if (isChildInBoot != expectedChildInBoot) {
      throw new RuntimeException("Expected ChildClass " + (expectedChildInBoot ? "" : "not ") +
                                 "in boot class path");
    }
    ChildClass.everythingWhitelisted = everythingWhitelisted;

    boolean isSameBoot = (isParentInBoot == isChildInBoot);
    boolean isDebuggable = VMRuntime.getRuntime().isJavaDebuggable();

    // For compat reasons, meta-reflection should still be usable by apps if hidden api check
    // hardening is disabled (i.e. target SDK is Q or earlier). The only configuration where this
    // workaround used to work is for ChildClass in the Application domain and ParentClass in the
    // Platform domain, so only test that configuration with hidden api check hardening disabled.
    boolean testHiddenApiCheckHardeningDisabled =
        (childDomain == DexDomain.Application) && (parentDomain == DexDomain.Platform);

    // Run meaningful combinations of access flags.
    for (Hiddenness hiddenness : Hiddenness.values()) {
      Behaviour expected;
      final boolean invokesMemberCallback;
      // Warnings are now disabled whenever access is granted, even for
      // greylisted APIs. This is the behaviour for release builds.
      if (everythingWhitelisted || hiddenness == Hiddenness.Whitelist) {
        expected = Behaviour.Granted;
        invokesMemberCallback = false;
      } else if (parentDomain == DexDomain.CorePlatform && childDomain == DexDomain.Platform) {
        expected = (hiddenness == Hiddenness.BlacklistAndCorePlatformApi)
                ? Behaviour.Granted : Behaviour.Denied;
        invokesMemberCallback = false;
      } else if (isSameBoot) {
        expected = Behaviour.Granted;
        invokesMemberCallback = false;
      } else if (hiddenness == Hiddenness.Blacklist ||
                 hiddenness == Hiddenness.BlacklistAndCorePlatformApi) {
        expected = Behaviour.Denied;
        invokesMemberCallback = true;
      } else {
        expected = Behaviour.Warning;
        invokesMemberCallback = true;
      }

      if (childNativeDomain == DexDomain.CorePlatform) {
          // Native code that is part of the Core Platform (it's in the ART module). This code is
          // assumed to have access to all methods and fields.
          expected = Behaviour.Granted;
      }

      for (boolean isStatic : booleanValues) {
        String suffix = (isStatic ? "Static" : "") + hiddenness.name();

        for (Visibility visibility : Visibility.values()) {
          // Test methods and fields
          for (Class klass : new Class<?>[] { ParentClass.class, ParentInterface.class }) {
            String baseName = visibility.name() + suffix;
            checkField(klass, "field" + baseName, isStatic, visibility, expected,
                invokesMemberCallback, testHiddenApiCheckHardeningDisabled);
            checkMethod(klass, "method" + baseName, isStatic, visibility, expected,
                invokesMemberCallback, testHiddenApiCheckHardeningDisabled);
          }

          // Check whether one can use a class constructor.
          checkConstructor(ParentClass.class, visibility, hiddenness, expected,
                testHiddenApiCheckHardeningDisabled);
        }
      }
    }
  }

  private static void checkField(Class<?> klass, String name, boolean isStatic,
      Visibility visibility, Behaviour behaviour, boolean invokesMemberCallback,
      boolean testHiddenApiCheckHardeningDisabled) throws Exception {

    boolean isPublic = (visibility == Visibility.Public);
    boolean canDiscover = (behaviour != Behaviour.Denied);

    if (klass.isInterface() && (!isStatic || !isPublic)) {
      // Interfaces only have public static fields.
      return;
    }

    // Test discovery with JNI.

    if (JNI.canDiscoverField(klass, name, isStatic) != canDiscover) {
      throwDiscoveryException(klass, name, true, "JNI", canDiscover);
    }

    if (canDiscover) {
      if (!JNI.canGetField(klass, name, isStatic)) {
        throwAccessException(klass, name, true, "getIntField");
      }
      if (!JNI.canSetField(klass, name, isStatic)) {
        throwAccessException(klass, name, true, "setIntField");
      }
    }
  }

  private static void checkMethod(Class<?> klass, String name, boolean isStatic,
      Visibility visibility, Behaviour behaviour, boolean invokesMemberCallback,
      boolean testHiddenApiCheckHardeningDisabled) throws Exception {

    boolean isPublic = (visibility == Visibility.Public);
    if (klass.isInterface() && !isPublic) {
      // All interface members are public.
      return;
    }

    boolean canDiscover = (behaviour != Behaviour.Denied);

    // Test discovery with JNI.

    if (JNI.canDiscoverMethod(klass, name, isStatic) != canDiscover) {
      throwDiscoveryException(klass, name, false, "JNI", canDiscover);
    }

    // Finish here if we could not discover the method.

    if (canDiscover) {
      // Test whether we can invoke the method. This skips non-static interface methods.
      if (!klass.isInterface() || isStatic) {
        if (!JNI.canInvokeMethodA(klass, name, isStatic)) {
          throwAccessException(klass, name, false, "CallMethodA");
        }
        if (!JNI.canInvokeMethodV(klass, name, isStatic)) {
          throwAccessException(klass, name, false, "CallMethodV");
        }
      }
    }
  }

  private static void checkConstructor(Class<?> klass, Visibility visibility, Hiddenness hiddenness,
      Behaviour behaviour, boolean testHiddenApiCheckHardeningDisabled) throws Exception {

    boolean isPublic = (visibility == Visibility.Public);
    String signature = "(" + visibility.mAssociatedType.mShorty +
                             hiddenness.mAssociatedType.mShorty + ")V";
    String fullName = "<init>" + signature;
    Class<?> args[] = new Class[] { visibility.mAssociatedType.mClass,
                                    hiddenness.mAssociatedType.mClass };
    Object initargs[] = new Object[] { visibility.mAssociatedType.mDefaultValue,
                                       hiddenness.mAssociatedType.mDefaultValue };
    MethodType methodType = MethodType.methodType(void.class, args);

    boolean canDiscover = (behaviour != Behaviour.Denied);

    // Test discovery with JNI.

    if (JNI.canDiscoverConstructor(klass, signature) != canDiscover) {
      throwDiscoveryException(klass, fullName, false, "JNI", canDiscover);
    }
  }

  private static void throwDiscoveryException(Class<?> klass, String name, boolean isField,
      String fn, boolean canAccess) {
    throw new RuntimeException("Expected " + (isField ? "field " : "method ") + klass.getName() +
        "." + name + " to " + (canAccess ? "" : "not ") + "be discoverable with " + fn + ". " +
        configMessage);
  }

  private static void throwAccessException(Class<?> klass, String name, boolean isField,
      String fn) {
    throw new RuntimeException("Expected to be able to access " + (isField ? "field " : "method ") +
        klass.getName() + "." + name + " using " + fn + ". " + configMessage);
  }

  private static void throwModifiersException(Class<?> klass, String name, boolean isField) {
    throw new RuntimeException("Expected " + (isField ? "field " : "method ") + klass.getName() +
        "." + name + " to not expose hidden modifiers");
  }

  private static DexDomain parentDomain;
  private static DexDomain childDomain;
  private static DexDomain childNativeDomain;
  private static boolean everythingWhitelisted;

  private static String configMessage;
}
