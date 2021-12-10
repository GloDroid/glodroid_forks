/*
 * Copyright © 2021, Google Inc.
 * Copyright (C) 2021, GlobalLogic Ukraine
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef EGL_ANDROID_INCLUDED
#define EGL_ANDROID_INCLUDED

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <GL/internal/dri_interface.h>
#include <cutils/native_handle.h>

struct buffer_info {
   uint32_t drm_fourcc;
   int num_planes;
   int fds[4];
   uint64_t modifier;
   int offsets[4];
   int strides[4];
   int sizes[4];
};

#ifdef USE_IMAPPER4_METADATA_API
#ifdef __cplusplus
extern "C" {
#endif
extern int
mapper_metadata_get_buffer_info(const native_handle_t *handle,
                                struct buffer_info *out_buf_info);
#ifdef __cplusplus
}
#endif
#else
static inline int
mapper_metadata_get_buffer_info(const native_handle_t *handle,
                                struct buffer_info *out_buf_info) {
   return -ENOTSUP;
}
#endif /* USE_IMAPPER4_METADATA_API */

#endif /* EGL_ANDROID_INCLUDED */
