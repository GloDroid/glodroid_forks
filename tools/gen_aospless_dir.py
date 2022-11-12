#!/usr/bin/python3

import sys
import os

AOSP_ROOT = os.environ["L_AOSP_ROOT"]
AOSP_OUT_DIR = os.environ["L_AOSP_OUT_DIR"]

if (not AOSP_ROOT.startswith("/") or not AOSP_OUT_DIR.startswith("/")):
	sys.exit("Invalid environment variables")

AOSPLESS_OUT_DIR = "aospless/"
REL_PREFIX = "[BASE_DIR]/"

include_dirs = []
objects = []

def subst_include_dir(in_dir, prefix_in):
	if in_dir.startswith(prefix_in):
		return "includes" + in_dir[len(prefix_in):]
	return in_dir

def process_includes(in_text, out_dirs):
	out_args = []
	args = in_text.split()
	for arg in args:
		if arg.startswith("-I"):
			idir = arg[2:]
			tmp = subst_include_dir(idir, AOSP_OUT_DIR)
			tmp = subst_include_dir(tmp, AOSP_ROOT)
			if tmp == idir:
				sys.exit("Failed to process include direcotry: " + arg)
			out_args.append("-I" + REL_PREFIX + tmp)
			include_dirs.append((idir, AOSPLESS_OUT_DIR + tmp))
		elif arg.startswith("-isystem"):
			idir = arg[8:]
			tmp = subst_include_dir(idir, AOSP_OUT_DIR)
			tmp = subst_include_dir(tmp, AOSP_ROOT)
			if tmp == idir:
				sys.exit("Failed to process include direcotry: " + arg)
			out_args.append("-isystem" + REL_PREFIX + tmp)
			include_dirs.append((idir, AOSPLESS_OUT_DIR + tmp))
		else:
			out_args.append(arg)

	return ' '.join(out_args)

def process_objects(in_text, out_object_files):
	out_args = []
	args = in_text.split()
	for arg in args:
		if arg.endswith(".so") or arg.endswith(".o") or (arg.endswith(".a") and not arg.startswith("-Wl")):
			out_path = "/objs/" + os.path.basename(arg)
			out_object_files.append((arg, AOSPLESS_OUT_DIR + out_path))
			out_args.append(REL_PREFIX + out_path)
		else:
			out_args.append(arg)

	return ' '.join(out_args)

def copy_includes(include_dirs):
	# Remove duplicates
	include_dirs = list(dict.fromkeys(include_dirs))

	for src,dst in include_dirs:
		print("Copying include files from " + src + " to " + dst)
		os.system("mkdir -p " + dst)
		if "/include/" in src or "libcxx" in src:
			os.system("rsync -Lr " + src + "/* " + dst + "/")
		else:
			os.system('rsync -Lr --include="*/" --include="*.h" --include="*.hpp" --exclude="*" ' + src + "/* " + dst + "/")

	return

def copy_objects(objects):
	# Remove duplicates
	objects = list(dict.fromkeys(objects))

	for src,dst in objects:
		print("Copying object files from " + src + " to " + dst)
		os.system("cp " + src + " " + dst);

	return

def process_copy_cflags(in_file, include_dirs):
	out_text = process_includes(open(in_file, "r").read(), include_dirs);
	out_file = open(AOSPLESS_OUT_DIR + "/" + in_file, "w")
	out_file.write(out_text)
	out_file.close()

def process_copy_linkflags(in_file, out_object_files):
	out_text = process_objects(open(in_file, "r").read(), out_object_files);
	out_file = open(AOSPLESS_OUT_DIR + "/" + in_file, "w")
	out_file.write(out_text)
	out_file.close()

os.system("mkdir -p " + AOSPLESS_OUT_DIR + "/build_flags");
os.system("mkdir -p " + AOSPLESS_OUT_DIR + "/objs");
os.system("mkdir -p " + AOSPLESS_OUT_DIR + "/gen");
os.system("rsync gen/* " + AOSPLESS_OUT_DIR + "/gen");

for file in ['aospext_cc', 'project_specific.mk']:
	os.system("cp " + file + " " + AOSPLESS_OUT_DIR)

os.system("echo AOSPLESS:=true > " + AOSPLESS_OUT_DIR + "/Makefile")
os.system("cat Makefile >> " + AOSPLESS_OUT_DIR + "/Makefile")

os.system("echo clang > " + AOSPLESS_OUT_DIR + "/build_flags/exec.cc")
os.system("echo clang > " + AOSPLESS_OUT_DIR + "/build_flags/sharedlib.cc")
os.system("echo clang++ > " + AOSPLESS_OUT_DIR + "/build_flags/exec.cxx")
os.system("echo clang++ > " + AOSPLESS_OUT_DIR + "/build_flags/sharedlib.cxx")

process_copy_cflags("build_flags/exec.cflags", include_dirs)
process_copy_cflags("build_flags/exec.cppflags", include_dirs)
process_copy_cflags("build_flags/sharedlib.cflags", include_dirs)
process_copy_cflags("build_flags/sharedlib.cppflags", include_dirs)

process_copy_linkflags("build_flags/exec.link_args", objects)
process_copy_linkflags("build_flags/sharedlib.link_args", objects)

copy_includes(include_dirs)

copy_objects(objects)
