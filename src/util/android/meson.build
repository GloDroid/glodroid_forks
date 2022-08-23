# Copyright (C) 2022 Roman Stratiienko (r.stratiienko@gmail.com)

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

c_args_for_android_buffer = []
cpp_args_for_android_buffer = []

files_android_buffer = files(
  'buffer_info.c',
  'buffer_info_fallback.c',
  'buffer_info_gralloc0_cros_api.c',
)

if dep_android_mapper4.found()
  files_android_buffer += files('buffer_info_gralloc4_api.cpp')
  c_args_for_android_buffer += '-DUSE_IMAPPER4_METADATA_API'
  cpp_args_for_android_buffer += ['-std=c++17', '-DUSE_IMAPPER4_METADATA_API']
endif

_libmesa_android_buffer = static_library(
  '_mesa_android_buffer',
  [files_android_buffer],
  include_directories : [inc_include, inc_src],
  c_args : [c_args_for_android_buffer],
  cpp_args : [cpp_args_for_android_buffer],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
  dependencies: dep_android,
)

idep_android_buffer = declare_dependency(link_with: _libmesa_android_buffer)
