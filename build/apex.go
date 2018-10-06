// Copyright (C) 2018 The Android Open Source Project
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

package apex

import (
	"fmt"
	"io"
	"path/filepath"
	"strings"

	"android/soong/android"
	"android/soong/cc"
	"android/soong/java"

	"github.com/google/blueprint"
	"github.com/google/blueprint/proptools"
)

var (
	pctx = android.NewPackageContext("android/apex")

	// Create a canned fs config file where all files and directories are
	// by default set to (uid/gid/mode) = (1000/1000/0644)
	// TODO(b/113082813) make this configurable using config.fs syntax
	generateFsConfig = pctx.StaticRule("generateFsConfig", blueprint.RuleParams{
		Command: `echo '/ 1000 1000 0644' > ${out} && ` +
			`echo '/manifest.json 1000 1000 0644' >> ${out} && ` +
			`echo ${paths} | tr ' ' '\n' | awk '{print "/"$$1 " 1000 1000 0644"}' >> ${out}`,
		Description: "fs_config ${out}",
	}, "paths")

	// TODO(b/113233103): make sure that file_contexts is sane, i.e., validate
	// against the binary policy using sefcontext_compiler -p <policy>.

	// TODO(b/114327326): automate the generation of file_contexts
	apexRule = pctx.StaticRule("apexRule", blueprint.RuleParams{
		Command: `rm -rf ${image_dir} && mkdir -p ${image_dir} && ` +
			`(${copy_commands}) && ` +
			`APEXER_TOOL_PATH=${tool_path} ` +
			`${apexer} --verbose --force --manifest ${manifest} ` +
			`--file_contexts ${file_contexts} ` +
			`--canned_fs_config ${canned_fs_config} ` +
			`--key ${key} ${image_dir} ${out} `,
		CommandDeps: []string{"${apexer}", "${avbtool}", "${e2fsdroid}", "${merge_zips}",
			"${mke2fs}", "${resize2fs}", "${sefcontext_compile}",
			"${soong_zip}", "${zipalign}", "${aapt2}"},
		Description: "APEX ${image_dir} => ${out}",
	}, "tool_path", "image_dir", "copy_commands", "manifest", "file_contexts", "canned_fs_config", "key")
)

var apexSuffix = ".apex"

type dependencyTag struct {
	blueprint.BaseDependencyTag
	name string
}

var (
	sharedLibTag  = dependencyTag{name: "sharedLib"}
	executableTag = dependencyTag{name: "executable"}
	javaLibTag    = dependencyTag{name: "javaLib"}
	prebuiltTag   = dependencyTag{name: "prebuilt"}
)

func init() {
	pctx.Import("android/soong/common")
	pctx.HostBinToolVariable("apexer", "apexer")
	pctx.HostBinToolVariable("aapt2", "aapt2")
	pctx.HostBinToolVariable("avbtool", "avbtool")
	pctx.HostBinToolVariable("e2fsdroid", "e2fsdroid")
	pctx.HostBinToolVariable("merge_zips", "merge_zips")
	pctx.HostBinToolVariable("mke2fs", "mke2fs")
	pctx.HostBinToolVariable("resize2fs", "resize2fs")
	pctx.HostBinToolVariable("sefcontext_compile", "sefcontext_compile")
	pctx.HostBinToolVariable("soong_zip", "soong_zip")
	pctx.HostBinToolVariable("zipalign", "zipalign")

	android.RegisterModuleType("apex", apexBundleFactory)
}

type apexBundleProperties struct {
	// Json manifest file describing meta info of this APEX bundle. Default:
	// "manifest.json"
	Manifest *string

	// File contexts file for setting security context to each file in this APEX bundle
	// Default: "file_contexts".
	File_contexts *string

	// List of native shared libs that are embedded inside this APEX bundle
	Native_shared_lib_modules []string

	// List of native executables that are embedded inside this APEX bundle
	Executable_modules []string

	// List of java libraries that are embedded inside this APEX bundle
	Java_modules []string

	// List of prebuilt files that are embedded inside this APEX bundle
	Prebuilt_modules []string
}

type apexBundle struct {
	android.ModuleBase
	android.DefaultableModuleBase

	properties apexBundleProperties

	outputFile android.WritablePath
	installDir android.OutputPath
}

func (a *apexBundle) DepsMutator(ctx android.BottomUpMutatorContext) {
	// Native shared libs are added for all architectures of the device
	// i.e., native_shared_lib_modules: ["libc"] adds both 64 and 32 variation
	// of the module
	arches := ctx.DeviceConfig().Arches()
	if len(arches) == 0 {
		panic("device build with no primary arch")
	}

	for _, arch := range arches {
		// Use *FarVariation* to be able to depend on modules having
		// conflicting variations with this module. This is required since
		// arch variant of an APEX bundle is 'common' but it is 'arm' or 'arm64'
		// for native shared libs.
		//
		// TODO(b/115717630) support modules having single arch variant (e.g. 32-bit only)
		ctx.AddFarVariationDependencies([]blueprint.Variation{
			{Mutator: "arch", Variation: ctx.Os().String() + "_" + arch.String()},
			{Mutator: "image", Variation: "core"},
			{Mutator: "link", Variation: "shared"},
		}, sharedLibTag, a.properties.Native_shared_lib_modules...)
	}

	ctx.AddFarVariationDependencies([]blueprint.Variation{
		{Mutator: "image", Variation: "core"},
	}, executableTag, a.properties.Executable_modules...)

	ctx.AddFarVariationDependencies([]blueprint.Variation{}, javaLibTag, a.properties.Java_modules...)

	ctx.AddFarVariationDependencies([]blueprint.Variation{}, prebuiltTag, a.properties.Prebuilt_modules...)
}

func (a *apexBundle) GenerateAndroidBuildActions(ctx android.ModuleContext) {
	// files to copy -> path in apex
	copyManifest := make(map[android.Path]string)
	// files and dirs that will be created in apex
	var pathsInApex []string

	ctx.VisitDirectDeps(func(dep android.Module) {
		depTag := ctx.OtherModuleDependencyTag(dep)
		switch depTag {
		case sharedLibTag:
			if cc, ok := dep.(*cc.Module); ok {
				// Decide the APEX-local directory by the multilib of the library
				// In the future, we may query this to the module.
				var libdir string
				switch cc.Arch().ArchType.Multilib {
				case "lib32":
					libdir = "lib"
				case "lib64":
					libdir = "lib64"
				}
				if !cc.Arch().Native {
					libdir = filepath.Join(libdir, cc.Arch().ArchType.String())
				}

				pathsInApex = append(pathsInApex, filepath.Join(libdir, cc.OutputFile().Path().Base()))
				if !android.InList(libdir, pathsInApex) {
					pathsInApex = append(pathsInApex, libdir)
				}
				copyManifest[cc.OutputFile().Path()] = filepath.Join(libdir, cc.OutputFile().Path().Base())
			}
		case executableTag:
			if cc, ok := dep.(*cc.Module); ok {
				dir := "bin"
				pathsInApex = append(pathsInApex, filepath.Join(dir, cc.OutputFile().Path().Base()))
				if !android.InList(dir, pathsInApex) {
					pathsInApex = append(pathsInApex, dir)
				}
				copyManifest[cc.OutputFile().Path()] = filepath.Join(dir, cc.OutputFile().Path().Base())
			}
		case javaLibTag:
			if java, ok := dep.(*java.Library); ok {
				dir := "javalib"
				pathsInApex = append(pathsInApex, filepath.Join(dir, java.Srcs()[0].Base()))
				if !android.InList(dir, pathsInApex) {
					pathsInApex = append(pathsInApex, dir)
				}
				copyManifest[java.Srcs()[0]] = filepath.Join(dir, java.Srcs()[0].Base())
			}
		case prebuiltTag:
			if prebuilt, ok := dep.(*android.PrebuiltEtc); ok {
				dir := filepath.Join("etc", prebuilt.SubDir())
				pathsInApex = append(pathsInApex, filepath.Join(dir, prebuilt.OutputFile().Base()))
				if !android.InList(dir, pathsInApex) {
					pathsInApex = append(pathsInApex, dir)
				}
				copyManifest[prebuilt.OutputFile()] = filepath.Join(dir, prebuilt.OutputFile().Base())
			}
		}
	})

	cannedFsConfig := android.PathForModuleOut(ctx, "canned_fs_config")
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:   generateFsConfig,
		Output: cannedFsConfig,
		Args: map[string]string{
			"paths": strings.Join(pathsInApex, " "),
		},
	})

	manifest := android.PathForModuleSrc(ctx, proptools.StringDefault(a.properties.Manifest, "manifest.json"))
	fileContexts := android.PathForModuleSrc(ctx, proptools.StringDefault(a.properties.File_contexts, "file_contexts"))
	// TODO(b/114488804) make this customizable
	key := android.PathForSource(ctx, "system/apex/apexer/testdata/testkey.pem")

	a.outputFile = android.PathForModuleOut(ctx, a.ModuleBase.Name()+apexSuffix)

	implicitInputs := []android.Path{}
	for file := range copyManifest {
		implicitInputs = append(implicitInputs, file)
	}
	copyCommands := []string{}
	for src, dest := range copyManifest {
		dest_path := filepath.Join(android.PathForModuleOut(ctx, "image").String(), dest)
		copyCommands = append(copyCommands, "mkdir -p "+filepath.Dir(dest_path))
		copyCommands = append(copyCommands, "cp "+src.String()+" "+dest_path)
	}
	implicitInputs = append(implicitInputs, cannedFsConfig, manifest, fileContexts, key)
	outHostBinDir := android.PathForOutput(ctx, "host", ctx.Config().PrebuiltOS(), "bin").String()
	prebuiltSdkToolsBinDir := filepath.Join("prebuilts", "sdk", "tools", ctx.Config().PrebuiltOS(), "bin")
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:      apexRule,
		Implicits: implicitInputs,
		Output:    a.outputFile,
		Args: map[string]string{
			"tool_path":        outHostBinDir + ":" + prebuiltSdkToolsBinDir,
			"image_dir":        android.PathForModuleOut(ctx, "image").String(),
			"copy_commands":    strings.Join(copyCommands, " && "),
			"manifest":         manifest.String(),
			"file_contexts":    fileContexts.String(),
			"canned_fs_config": cannedFsConfig.String(),
			"key":              key.String(),
		},
	})

	a.installDir = android.PathForModuleInstall(ctx, "apex")
}

func (a *apexBundle) AndroidMk() android.AndroidMkData {
	return android.AndroidMkData{
		Custom: func(w io.Writer, name, prefix, moduleDir string, data android.AndroidMkData) {
			fmt.Fprintln(w, "\ninclude $(CLEAR_VARS)")
			fmt.Fprintln(w, "LOCAL_PATH :=", moduleDir)
			fmt.Fprintln(w, "LOCAL_MODULE :=", name)
			fmt.Fprintln(w, "LOCAL_MODULE_CLASS := ETC") // do we need a new class?
			fmt.Fprintln(w, "LOCAL_PREBUILT_MODULE_FILE :=", a.outputFile.String())
			fmt.Fprintln(w, "LOCAL_MODULE_PATH :=", filepath.Join("$(OUT_DIR)", a.installDir.RelPathString()))
			fmt.Fprintln(w, "LOCAL_INSTALLED_MODULE_STEM :=", name+apexSuffix)
			fmt.Fprintln(w, "include $(BUILD_PREBUILT)")
		}}
}

func apexBundleFactory() android.Module {
	module := &apexBundle{}
	module.AddProperties(&module.properties)
	android.InitAndroidArchModule(module, android.DeviceSupported, android.MultilibFirst)
	android.InitDefaultableModule(module)
	return module
}
