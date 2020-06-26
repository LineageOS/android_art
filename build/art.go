// Copyright (C) 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package art

import (
	"fmt"
	"log"
	"strings"
	"sync"

	"github.com/google/blueprint/proptools"

	"android/soong/android"
	"android/soong/apex"
	"android/soong/cc"
)

var supportedArches = []string{"arm", "arm64", "x86", "x86_64"}

func globalFlags(ctx android.LoadHookContext) ([]string, []string) {
	var cflags []string
	var asflags []string

	opt := ctx.Config().GetenvWithDefault("ART_NDEBUG_OPT_FLAG", "-O3")
	cflags = append(cflags, opt)

	tlab := false

	gcType := ctx.Config().GetenvWithDefault("ART_DEFAULT_GC_TYPE", "CMS")

	if ctx.Config().IsEnvTrue("ART_TEST_DEBUG_GC") {
		gcType = "SS"
		tlab = true
	}

	cflags = append(cflags, "-DART_DEFAULT_GC_TYPE_IS_"+gcType)
	if tlab {
		cflags = append(cflags, "-DART_USE_TLAB=1")
	}

	if ctx.Config().IsEnvTrue("ART_HEAP_POISONING") {
		cflags = append(cflags, "-DART_HEAP_POISONING=1")
		asflags = append(asflags, "-DART_HEAP_POISONING=1")
	}
	if ctx.Config().IsEnvTrue("ART_USE_CXX_INTERPRETER") {
		cflags = append(cflags, "-DART_USE_CXX_INTERPRETER=1")
	}

	if !ctx.Config().IsEnvFalse("ART_USE_READ_BARRIER") && ctx.Config().ArtUseReadBarrier() {
		// Used to change the read barrier type. Valid values are BAKER, BROOKS,
		// TABLELOOKUP. The default is BAKER.
		barrierType := ctx.Config().GetenvWithDefault("ART_READ_BARRIER_TYPE", "BAKER")
		cflags = append(cflags,
			"-DART_USE_READ_BARRIER=1",
			"-DART_READ_BARRIER_TYPE_IS_"+barrierType+"=1")
		asflags = append(asflags,
			"-DART_USE_READ_BARRIER=1",
			"-DART_READ_BARRIER_TYPE_IS_"+barrierType+"=1")
	}

	if !ctx.Config().IsEnvFalse("ART_USE_GENERATIONAL_CC") {
		cflags = append(cflags, "-DART_USE_GENERATIONAL_CC=1")
	}

	cdexLevel := ctx.Config().GetenvWithDefault("ART_DEFAULT_COMPACT_DEX_LEVEL", "fast")
	cflags = append(cflags, "-DART_DEFAULT_COMPACT_DEX_LEVEL="+cdexLevel)

	// We need larger stack overflow guards for ASAN, as the compiled code will have
	// larger frame sizes. For simplicity, just use global not-target-specific cflags.
	// Note: We increase this for both debug and non-debug, as the overflow gap will
	//       be compiled into managed code. We always preopt (and build core images) with
	//       the debug version. So make the gap consistent (and adjust for the worst).
	if len(ctx.Config().SanitizeDevice()) > 0 || len(ctx.Config().SanitizeHost()) > 0 {
		cflags = append(cflags,
			"-DART_STACK_OVERFLOW_GAP_arm=8192",
			"-DART_STACK_OVERFLOW_GAP_arm64=16384",
			"-DART_STACK_OVERFLOW_GAP_x86=16384",
			"-DART_STACK_OVERFLOW_GAP_x86_64=20480")
	} else {
		cflags = append(cflags,
			"-DART_STACK_OVERFLOW_GAP_arm=8192",
			"-DART_STACK_OVERFLOW_GAP_arm64=8192",
			"-DART_STACK_OVERFLOW_GAP_x86=8192",
			"-DART_STACK_OVERFLOW_GAP_x86_64=8192")
	}

	if ctx.Config().IsEnvTrue("ART_ENABLE_ADDRESS_SANITIZER") {
		// Used to enable full sanitization, i.e., user poisoning, under ASAN.
		cflags = append(cflags, "-DART_ENABLE_ADDRESS_SANITIZER=1")
		asflags = append(asflags, "-DART_ENABLE_ADDRESS_SANITIZER=1")
	}

	if !ctx.Config().IsEnvFalse("USE_D8_DESUGAR") {
		cflags = append(cflags, "-DUSE_D8_DESUGAR=1")
	}

	return cflags, asflags
}

func debugFlags(ctx android.LoadHookContext) []string {
	var cflags []string

	opt := ctx.Config().GetenvWithDefault("ART_DEBUG_OPT_FLAG", "-O2")
	cflags = append(cflags, opt)

	return cflags
}

func deviceFlags(ctx android.LoadHookContext) []string {
	var cflags []string
	deviceFrameSizeLimit := 1736
	if len(ctx.Config().SanitizeDevice()) > 0 {
		deviceFrameSizeLimit = 7400
	}
	cflags = append(cflags,
		fmt.Sprintf("-Wframe-larger-than=%d", deviceFrameSizeLimit),
		fmt.Sprintf("-DART_FRAME_SIZE_LIMIT=%d", deviceFrameSizeLimit),
	)

	cflags = append(cflags, "-DART_BASE_ADDRESS="+ctx.Config().LibartImgDeviceBaseAddress())
	if ctx.Config().IsEnvTrue("ART_TARGET_LINUX") {
		cflags = append(cflags, "-DART_TARGET_LINUX")
	} else {
		cflags = append(cflags, "-DART_TARGET_ANDROID")
	}
	minDelta := ctx.Config().GetenvWithDefault("LIBART_IMG_TARGET_MIN_BASE_ADDRESS_DELTA", "-0x1000000")
	maxDelta := ctx.Config().GetenvWithDefault("LIBART_IMG_TARGET_MAX_BASE_ADDRESS_DELTA", "0x1000000")
	cflags = append(cflags, "-DART_BASE_ADDRESS_MIN_DELTA="+minDelta)
	cflags = append(cflags, "-DART_BASE_ADDRESS_MAX_DELTA="+maxDelta)

	return cflags
}

func hostFlags(ctx android.LoadHookContext) []string {
	var cflags []string
	hostFrameSizeLimit := 1736
	if len(ctx.Config().SanitizeHost()) > 0 {
		// art/test/137-cfi/cfi.cc
		// error: stack frame size of 1944 bytes in function 'Java_Main_unwindInProcess'
		hostFrameSizeLimit = 6400
	}
	cflags = append(cflags,
		fmt.Sprintf("-Wframe-larger-than=%d", hostFrameSizeLimit),
		fmt.Sprintf("-DART_FRAME_SIZE_LIMIT=%d", hostFrameSizeLimit),
	)

	cflags = append(cflags, "-DART_BASE_ADDRESS="+ctx.Config().LibartImgHostBaseAddress())
	minDelta := ctx.Config().GetenvWithDefault("LIBART_IMG_HOST_MIN_BASE_ADDRESS_DELTA", "-0x1000000")
	maxDelta := ctx.Config().GetenvWithDefault("LIBART_IMG_HOST_MAX_BASE_ADDRESS_DELTA", "0x1000000")
	cflags = append(cflags, "-DART_BASE_ADDRESS_MIN_DELTA="+minDelta)
	cflags = append(cflags, "-DART_BASE_ADDRESS_MAX_DELTA="+maxDelta)

	if len(ctx.Config().SanitizeHost()) > 0 && !ctx.Config().IsEnvFalse("ART_ENABLE_ADDRESS_SANITIZER") {
		// We enable full sanitization on the host by default.
		cflags = append(cflags, "-DART_ENABLE_ADDRESS_SANITIZER=1")
	}

	return cflags
}

func globalDefaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Cflags []string
			}
			Host struct {
				Cflags []string
			}
		}
		Cflags   []string
		Asflags  []string
		Sanitize struct {
			Recover []string
		}
	}

	p := &props{}
	p.Cflags, p.Asflags = globalFlags(ctx)
	p.Target.Android.Cflags = deviceFlags(ctx)
	p.Target.Host.Cflags = hostFlags(ctx)

	if ctx.Config().IsEnvTrue("ART_DEX_FILE_ACCESS_TRACKING") {
		p.Cflags = append(p.Cflags, "-DART_DEX_FILE_ACCESS_TRACKING")
		p.Sanitize.Recover = []string{
			"address",
		}
	}

	ctx.AppendProperties(p)
}

func debugDefaults(ctx android.LoadHookContext) {
	type props struct {
		Cflags []string
	}

	p := &props{}
	p.Cflags = debugFlags(ctx)
	ctx.AppendProperties(p)
}

func customLinker(ctx android.LoadHookContext) {
	linker := ctx.Config().Getenv("CUSTOM_TARGET_LINKER")
	type props struct {
		DynamicLinker string
	}

	p := &props{}
	if linker != "" {
		p.DynamicLinker = linker
	}

	ctx.AppendProperties(p)
}

func prefer32Bit(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Host struct {
				Compile_multilib *string
			}
		}
	}

	p := &props{}
	if ctx.Config().IsEnvTrue("HOST_PREFER_32_BIT") {
		p.Target.Host.Compile_multilib = proptools.StringPtr("prefer32")
	}

	// Prepend to make it overridable in the blueprints. Note that it doesn't work
	// to override the property in a cc_defaults module.
	ctx.PrependProperties(p)
}

var testMapKey = android.NewOnceKey("artTests")

func testMap(config android.Config) map[string][]string {
	return config.Once(testMapKey, func() interface{} {
		return make(map[string][]string)
	}).(map[string][]string)
}

func testInstall(ctx android.InstallHookContext) {
	testMap := testMap(ctx.Config())

	var name string
	if ctx.Host() {
		name = "host_"
	} else {
		name = "device_"
	}
	name += ctx.Arch().ArchType.String() + "_" + ctx.ModuleName()

	artTestMutex.Lock()
	defer artTestMutex.Unlock()

	tests := testMap[name]
	tests = append(tests, ctx.Path().ToMakePath().String())
	testMap[name] = tests
}

var testcasesContentKey = android.NewOnceKey("artTestcasesContent")

func testcasesContent(config android.Config) map[string]string {
	return config.Once(testcasesContentKey, func() interface{} {
		return make(map[string]string)
	}).(map[string]string)
}

// Binaries and libraries also need to be copied in the testcases directory for
// running tests on host.  This method adds module to the list of needed files.
// The 'key' is the file in testcases and 'value' is the path to copy it from.
// The actual copy will be done in make since soong does not do installations.
func addTestcasesFile(ctx android.InstallHookContext) {
	testcasesContent := testcasesContent(ctx.Config())

	artTestMutex.Lock()
	defer artTestMutex.Unlock()

	if ctx.Os().Class == android.Host {
		src := ctx.SrcPath().String()
		path := strings.Split(ctx.Path().ToMakePath().String(), "/")
		// Keep last two parts of the install path (e.g. bin/dex2oat).
		dst := strings.Join(path[len(path)-2:], "/")
		testcasesContent[dst] = src
	}
}

var artTestMutex sync.Mutex

func init() {
	artModuleTypes := []string{
		"art_cc_library",
		"art_cc_library_static",
		"art_cc_binary",
		"art_cc_test",
		"art_cc_test_library",
		"art_cc_defaults",
		"libart_cc_defaults",
		"libart_static_cc_defaults",
		"art_global_defaults",
		"art_debug_defaults",
		"art_apex_test_host",
	}
	android.AddNeverAllowRules(
		android.NeverAllow().
			NotIn("art", "external/vixl").
			ModuleType(artModuleTypes...))

	android.RegisterModuleType("art_cc_library", artLibrary)
	android.RegisterModuleType("art_cc_library_static", artStaticLibrary)
	android.RegisterModuleType("art_cc_binary", artBinary)
	android.RegisterModuleType("art_cc_test", artTest)
	android.RegisterModuleType("art_cc_test_library", artTestLibrary)
	android.RegisterModuleType("art_cc_defaults", artDefaultsFactory)
	android.RegisterModuleType("libart_cc_defaults", libartDefaultsFactory)
	android.RegisterModuleType("libart_static_cc_defaults", libartStaticDefaultsFactory)
	android.RegisterModuleType("art_global_defaults", artGlobalDefaultsFactory)
	android.RegisterModuleType("art_debug_defaults", artDebugDefaultsFactory)

	// ART apex is special because it must include dexpreopt files for bootclasspath jars.
	android.RegisterModuleType("art_apex", artApexBundleFactory)
	android.RegisterModuleType("art_apex_test", artTestApexBundleFactory)

	// TODO: This makes the module disable itself for host if HOST_PREFER_32_BIT is
	// set. We need this because the multilib types of binaries listed in the apex
	// rule must match the declared type. This is normally not difficult but HOST_PREFER_32_BIT
	// changes this to 'prefer32' on all host binaries. Since HOST_PREFER_32_BIT is
	// only used for testing we can just disable the module.
	// See b/120617876 for more information.
	android.RegisterModuleType("art_apex_test_host", artHostTestApexBundleFactory)
}

func artApexBundleFactory() android.Module {
	return apex.ApexBundleFactory(false /*testApex*/, true /*artApex*/)
}

func artTestApexBundleFactory() android.Module {
	return apex.ApexBundleFactory(true /*testApex*/, true /*artApex*/)
}

func artHostTestApexBundleFactory() android.Module {
	module := apex.ApexBundleFactory(true /*testApex*/, true /*artApex*/)
	android.AddLoadHook(module, func(ctx android.LoadHookContext) {
		if ctx.Config().IsEnvTrue("HOST_PREFER_32_BIT") {
			type props struct {
				Target struct {
					Host struct {
						Enabled *bool
					}
				}
			}

			p := &props{}
			p.Target.Host.Enabled = proptools.BoolPtr(false)
			ctx.AppendProperties(p)
			log.Print("Disabling host build of " + ctx.ModuleName() + " for HOST_PREFER_32_BIT=true")
		}
	})

	return module
}

func artGlobalDefaultsFactory() android.Module {
	module := artDefaultsFactory()
	android.AddLoadHook(module, globalDefaults)

	return module
}

func artDebugDefaultsFactory() android.Module {
	module := artDefaultsFactory()
	android.AddLoadHook(module, debugDefaults)

	return module
}

func artDefaultsFactory() android.Module {
	c := &codegenProperties{}
	module := cc.DefaultsFactory(c)
	android.AddLoadHook(module, func(ctx android.LoadHookContext) { codegen(ctx, c, staticAndSharedLibrary) })

	return module
}

func libartDefaultsFactory() android.Module {
	c := &codegenProperties{}
	module := cc.DefaultsFactory(c)
	android.AddLoadHook(module, func(ctx android.LoadHookContext) { codegen(ctx, c, staticAndSharedLibrary) })

	return module
}

func libartStaticDefaultsFactory() android.Module {
	c := &codegenProperties{}
	module := cc.DefaultsFactory(c)
	android.AddLoadHook(module, func(ctx android.LoadHookContext) { codegen(ctx, c, staticLibrary) })

	return module
}

func artLibrary() android.Module {
	module := cc.LibraryFactory()

	installCodegenCustomizer(module, staticAndSharedLibrary)

	android.AddInstallHook(module, addTestcasesFile)
	return module
}

func artStaticLibrary() android.Module {
	module := cc.LibraryStaticFactory()

	installCodegenCustomizer(module, staticLibrary)

	return module
}

func artBinary() android.Module {
	module := cc.BinaryFactory()

	android.AddLoadHook(module, customLinker)
	android.AddLoadHook(module, prefer32Bit)
	android.AddInstallHook(module, addTestcasesFile)
	return module
}

func artTest() android.Module {
	module := cc.TestFactory()

	installCodegenCustomizer(module, binary)

	android.AddLoadHook(module, customLinker)
	android.AddLoadHook(module, prefer32Bit)
	android.AddInstallHook(module, testInstall)
	return module
}

func artTestLibrary() android.Module {
	module := cc.TestLibraryFactory()

	installCodegenCustomizer(module, staticAndSharedLibrary)

	android.AddLoadHook(module, prefer32Bit)
	android.AddInstallHook(module, testInstall)
	return module
}
