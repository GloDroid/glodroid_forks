#!/usr/bin/env python
#
# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
apexer is a command line tool for creating an APEX file, a package format
for system components.

Typical usage: apexer input_dir output.apex

"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

if 'APEXER_TOOL_PATH' not in os.environ:
  sys.stderr.write("""
Error. The APEXER_TOOL_PATH environment variable needs to be set, and point to
a list of directories containing all the tools used by apexer (e.g. mke2fs,
avbtool, etc.) separated by ':'. Typically this can be set as:

export APEXER_TOOL_PATH="${ANDROID_BUILD_TOP}/out/soong/host/linux-x86/bin:${ANDROID_BUILD_TOP}/prebuilts/sdk/tools/linux/bin"

Aborting.
""")
  sys.exit(1)

tool_path = os.environ['APEXER_TOOL_PATH']
tool_path_list = tool_path.split(":")

def ParseArgs(argv):
  parser = argparse.ArgumentParser(description='Create an APEX file')
  parser.add_argument('-f', '--force', action='store_true',
                      help='force overwriting output')
  parser.add_argument('-v', '--verbose', action='store_true',
                      help='verbose execution')
  parser.add_argument('--manifest', default='manifest.json',
                      help='path to the APEX manifest file')
  parser.add_argument('--file_contexts', required=True,
                      help='selinux file contexts file')
  parser.add_argument('--canned_fs_config', required=True,
                      help='canned_fs_config specifies uid/gid/mode of files')
  parser.add_argument('--key', required=True,
                      help='path to the private key file')
  parser.add_argument('input_dir', metavar='INPUT_DIR',
                      help='the directory having files to be packaged')
  parser.add_argument('output', metavar='OUTPUT',
                      help='name of the APEX file')
  return parser.parse_args(argv)

def FindBinaryPath(binary):
  for path in tool_path_list:
    binary_path = os.path.join(path, binary)
    if os.path.exists(binary_path):
      return binary_path
  raise Exception("Failed to find binary " + binary + " in path " + tool_path)

def RunCommand(cmd, verbose=False, env=None):
  env = env or {}
  env.update(os.environ.copy())

  cmd[0] = FindBinaryPath(cmd[0])

  if verbose:
    print("Running: " + " ".join(cmd))
  p = subprocess.Popen(
      cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
  output, _ = p.communicate()

  if verbose:
    print(output.rstrip())

  assert p.returncode is 0, "Failed to execute: " + " ".join(cmd)

  return (output, p.returncode)


def GetDirSize(dir_name):
  size = 0
  for dirpath, _, filenames in os.walk(dir_name):
    for f in filenames:
      size += os.path.getsize(os.path.join(dirpath, f))
  return size


def RoundUp(size, unit):
  assert unit & (unit - 1) == 0
  return (size + unit - 1) & (~(unit - 1))


def PrepareAndroidManifest(package, version):
  template = """\
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
  package="{package}" android:versionCode="{version}">
  <!-- APEX does not have classes.dex -->
  <application android:hasCode="false" />
</manifest>
"""
  return template.format(package=package, version=version)


def ValidateArgs(args):
  if not os.path.exists(args.manifest):
    print("Manifest file '" + args.manifest + "' does not exist")
    return False

  if not os.path.isfile(args.manifest):
    print("Manifest file '" + args.manifest + "' is not a file")
    return False

  if not os.path.exists(args.input_dir):
    print("Input directory '" + args.input_dir + "' does not exist")
    return False

  if not os.path.isdir(args.input_dir):
    print("Input directory '" + args.input_dir + "' is not a directory")
    return False

  if not args.force and os.path.exists(args.output):
    print(args.output + ' already exists. Use --force to overwrite.')
    return False

  return True


def CreateApex(args, work_dir):
  if not ValidateArgs(args):
    return False

  if args.verbose:
    print "Using tools from " + str(tool_path_list)

  try:
    with open(args.manifest) as f:
      manifest = json.load(f)
  except ValueError:
    print("'" + args.manifest + "' is not a valid manifest file")
    return False
  except IOError:
    print("Cannot read manifest file: '" + args.manifest + "'")
    return False

  if 'name' not in manifest or manifest['name'] is None:
    print("Invalid manifest: 'name' does not exist")
    return False
  package_name = manifest['name']
  version_number = manifest['version']

  # create an empty ext4 image that is sufficiently big
  # Sufficiently big = twice the size of the input directory
  # For the case when the input directory is really small, the minimum of the
  # size is set to 10MB that is sufficiently large for filesystem metadata
  # and manifests
  size_in_mb = max(10, GetDirSize(args.input_dir) * 2 / (1024*1024))

  content_dir = os.path.join(work_dir, 'content')
  os.mkdir(content_dir)
  img_file = os.path.join(content_dir, 'image.img')

  cmd = ['mke2fs']
  cmd.extend(['-O', '^has_journal']) # because image is read-only
  cmd.extend(['-b', '4096']) # block size
  cmd.extend(['-m', '0']) # reserved block percentage
  cmd.extend(['-t', 'ext4'])
  cmd.append(img_file)
  cmd.append(str(size_in_mb) + 'M')
  RunCommand(cmd, args.verbose)

  # Compile the file context into the binary form
  compiled_file_contexts = os.path.join(work_dir, 'file_contexts.bin')
  cmd = ['sefcontext_compile']
  cmd.extend(['-o', compiled_file_contexts])
  cmd.append(args.file_contexts)
  RunCommand(cmd, args.verbose)

  # Add files to the image file
  cmd = ['e2fsdroid']
  cmd.append('-e') # input is not android_sparse_file
  cmd.extend(['-f', args.input_dir])
  cmd.extend(['-T', '0']) # time is set to epoch
  cmd.extend(['-S', compiled_file_contexts])
  cmd.extend(['-C', args.canned_fs_config])
  cmd.append(img_file)
  RunCommand(cmd, args.verbose)

  # APEX manifest is also included in the image. The manifest is included
  # twice: once inside the image and once outside the image (but still
  # within the zip container).
  manifests_dir = os.path.join(work_dir, 'manifests')
  os.mkdir(manifests_dir)
  manifest_file = os.path.join(manifests_dir, 'manifest.json')
  if args.verbose:
    print('Copying ' + args.manifest + ' to ' + manifest_file)
  shutil.copyfile(args.manifest, manifest_file)

  cmd = ['e2fsdroid']
  cmd.append('-e') # input is not android_sparse_file
  cmd.extend(['-f', manifests_dir])
  cmd.extend(['-T', '0']) # time is set to epoch
  cmd.extend(['-S', compiled_file_contexts])
  cmd.extend(['-C', args.canned_fs_config])
  cmd.append(img_file)
  RunCommand(cmd, args.verbose)

  # Resize the image file to save space
  cmd = ['resize2fs']
  cmd.append('-M') # shrink as small as possible
  cmd.append(img_file)
  RunCommand(cmd, args.verbose)

  cmd = ['avbtool']
  cmd.append('add_hashtree_footer')
  cmd.append('--do_not_generate_fec')
  cmd.extend(['--algorithm', 'SHA256_RSA4096'])
  cmd.extend(['--key', args.key])
  cmd.extend(['--prop', "apex.key:" + os.path.basename(os.path.splitext(args.key)[0])])
  cmd.extend(['--image', img_file])
  RunCommand(cmd, args.verbose)

  # Get the minimum size of the partition required.
  # TODO(b/113320014) eliminate this step
  info, _ = RunCommand(['avbtool', 'info_image', '--image', img_file], args.verbose)
  vbmeta_offset = int(re.search('VBMeta\ offset:\ *([0-9]+)', info).group(1))
  vbmeta_size = int(re.search('VBMeta\ size:\ *([0-9]+)', info).group(1))
  partition_size = RoundUp(vbmeta_offset + vbmeta_size, 4096) + 4096

  # Resize to the minimum size
  # TODO(b/113320014) eliminate this step
  cmd = ['avbtool']
  cmd.append('resize_image')
  cmd.extend(['--image', img_file])
  cmd.extend(['--partition_size', str(partition_size)])
  RunCommand(cmd, args.verbose)

  # package the image file and APEX manifest as an APK.
  # The AndroidManifest file is automatically generated.
  android_manifest_file = os.path.join(work_dir, 'AndroidManifest.xml')
  if args.verbose:
    print('Creating AndroidManifest ' + android_manifest_file)
  with open(android_manifest_file, 'w+') as f:
    f.write(PrepareAndroidManifest(package_name, version_number))

  # copy manifest to the content dir so that it is also accessible
  # without mounting the image
  shutil.copyfile(args.manifest, os.path.join(content_dir, 'manifest.json'))

  apk_file = os.path.join(work_dir, 'apex.apk')
  cmd = ['aapt2']
  cmd.append('link')
  cmd.extend(['--manifest', android_manifest_file])
  cmd.extend(['-o', apk_file])
  cmd.extend(['-I', "prebuilts/sdk/current/public/android.jar"])
  RunCommand(cmd, args.verbose)

  zip_file = os.path.join(work_dir, 'apex.zip')
  cmd = ['soong_zip']
  cmd.append('-d') # include directories
  cmd.extend(['-C', content_dir]) # relative root
  cmd.extend(['-D', content_dir]) # input dir
  for file_ in os.listdir(content_dir):
    if os.path.isfile(os.path.join(content_dir, file_)):
      cmd.extend(['-s', file_]) # don't compress any files
  cmd.extend(['-o', zip_file])
  RunCommand(cmd, args.verbose)

  unaligned_apex_file = os.path.join(work_dir, 'unaligned.apex')
  cmd = ['merge_zips']
  cmd.append('-j') # sort
  cmd.append(unaligned_apex_file) # output
  cmd.append(apk_file) # input
  cmd.append(zip_file) # input
  RunCommand(cmd, args.verbose)

  # Align the files at page boundary for efficient access
  cmd = ['zipalign']
  cmd.append('-f')
  cmd.append('4096') # 4k alignment
  cmd.append(unaligned_apex_file)
  cmd.append(args.output)
  RunCommand(cmd, args.verbose)

  if (args.verbose):
    print('Created ' + args.output)

  return True


class TempDirectory(object):
  def __enter__(self):
    self.name = tempfile.mkdtemp()
    return self.name

  def __exit__(self, *unused):
    shutil.rmtree(self.name)


def main(argv):
  args = ParseArgs(argv)
  with TempDirectory() as work_dir:
    success = CreateApex(args, work_dir)

  if not success:
    sys.exit(1)


if __name__ == '__main__':
  main(sys.argv[1:])
