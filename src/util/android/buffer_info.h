/*
 * Copyright (C) 2022 Roman Stratiienko (r.stratiienko@gmail.com)
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

#ifndef ANDROID_BUFFER_INFO_INCLUDED
#define ANDROID_BUFFER_INFO_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <cutils/native_handle.h>

#include <stdbool.h>

#include "GL/internal/dri_interface.h"

struct buffer_info_ops;

struct gralloc {
   struct buffer_info_ops *ops;
   void *priv;
};

/* Both Vulkan and EGL exposes HAL format / pixel stride which is required
 * by fallback implementation.
 */
struct android_handle {
   const native_handle_t *handle;
   int hal_format;
   int pixel_stride;
};

struct buffer_info {
   bool has_size_info;
   int width;
   int height;

   bool has_format_info;
   uint32_t drm_fourcc;
   uint64_t modifier;

   bool has_layout_info;
   int num_planes;
   int fds[4];
   int offsets[4];
   int strides[4];

   bool has_color_info;
   enum __DRIYUVColorSpace yuv_color_space;
   enum __DRISampleRange sample_range;
   enum __DRIChromaSiting horizontal_siting;
   enum __DRIChromaSiting vertical_siting;
};

struct buffer_info_ops {
   int (*get_fmt_mod_info)(struct gralloc *gralloc, struct android_handle *hnd,
                           struct buffer_info *out);
   int (*get_layout_info)(struct gralloc *gralloc, struct android_handle *hnd,
                          struct buffer_info *out);
   int (*get_color_info)(struct gralloc *gralloc, struct android_handle *hnd,
                         struct buffer_info *out);
   int (*get_front_rendering_usage)(struct gralloc *gralloc,
                                    uint64_t *out_usage);
   int (*destroy)(struct gralloc *info);
};

int init_empty_buffer_info(struct buffer_info *out);

int probe_gralloc(struct gralloc *gralloc);

int destroy_gralloc(struct gralloc *gralloc);

int gralloc_get_fmt_mod_info(struct gralloc *gralloc,
                             struct android_handle *hnd,
                             struct buffer_info *out);

int gralloc_get_layout_info(struct gralloc *gralloc, struct android_handle *hnd,
                            struct buffer_info *out);

int gralloc_get_color_info(struct gralloc *gralloc, struct android_handle *hnd,
                           struct buffer_info *out);

int gralloc_get_front_rendering_usage(struct gralloc *gralloc,
                                      uint64_t *out_usage);

#ifdef __cplusplus
}
#endif

#endif
