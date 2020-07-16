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
	"path/filepath"
	"sort"
	"strings"

	"android/soong/android"
	"android/soong/cc/config"
)

var (
	pctx = android.NewPackageContext("android/soong/art")

	// Copy the following prebuilts to the testcases directory.
	// The original prebuilts directory is not accessible when running tests remotely.
	prebuiltToolsForTests = []string{
		"bin/clang",
		"bin/clang.real",
		"bin/llvm-addr2line",
		"bin/llvm-dwarfdump",
		"bin/llvm-objdump",
		"lib64/libc++.so.1",
	}
)

func init() {
	android.RegisterMakeVarsProvider(pctx, makeVarsProvider)
	pctx.Import("android/soong/cc/config")
}

func makeVarsProvider(ctx android.MakeVarsContext) {
	ctx.Strict("LIBART_IMG_HOST_BASE_ADDRESS", ctx.Config().LibartImgHostBaseAddress())
	ctx.Strict("LIBART_IMG_TARGET_BASE_ADDRESS", ctx.Config().LibartImgDeviceBaseAddress())

	testMap := testMap(ctx.Config())
	var testNames []string
	for name := range testMap {
		testNames = append(testNames, name)
	}

	sort.Strings(testNames)

	for _, name := range testNames {
		ctx.Strict("ART_TEST_LIST_"+name, strings.Join(testMap[name], " "))
	}

	// Create list of copy commands to install the content of the testcases directory.
	testcasesContent := testcasesContent(ctx.Config())
	copy_cmds := []string{}
	for _, key := range android.SortedStringKeys(testcasesContent) {
		copy_cmds = append(copy_cmds, testcasesContent[key]+":"+key)
	}
	ctx.Strict("ART_TESTCASES_CONTENT", strings.Join(copy_cmds, " "))

	// Add prebuilt tools.
	clang_path := filepath.Join(config.ClangDefaultBase, ctx.Config().PrebuiltOS(), config.ClangDefaultVersion)
	copy_cmds = []string{}
	for _, tool := range prebuiltToolsForTests {
		src := filepath.Join(clang_path, "/", tool)
		copy_cmds = append(copy_cmds, src+":"+src)
	}
	ctx.Strict("ART_TESTCASES_PREBUILT_CONTENT", strings.Join(copy_cmds, " "))
}
