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

#include "buffer_info.h"

#include <assert.h>
#include <errno.h>

#include "drm-uapi/drm_fourcc.h"
#include "util/macros.h"

extern int gralloc_v0_cros_api_probe(struct gralloc *gralloc);
extern int gralloc_v4_meatdata_api_probe(struct gralloc *gralloc);
extern int fallback_gralloc_probe(struct gralloc *gralloc);

static const struct gr_probe {
   int (*probe)(struct gralloc *gralloc);
} grallocs[] = {
   {.probe = gralloc_v0_cros_api_probe},
#ifdef USE_IMAPPER4_METADATA_API
   {.probe = gralloc_v4_meatdata_api_probe},
#endif /* USE_IMAPPER4_METADATA_API */
   {.probe = fallback_gralloc_probe},
};

int
init_empty_buffer_info(struct buffer_info *out_buf_info)
{
   assert(out_buf_info);

   *out_buf_info = (struct buffer_info){
      .has_size_info = false,
      .width = 0,
      .height = 0,

      .has_format_info = false,
      .drm_fourcc = DRM_FORMAT_INVALID,
      .modifier = DRM_FORMAT_MOD_INVALID,

      .has_layout_info = false,
      .num_planes = 0,
      .fds = {-1, -1, -1, -1},
      .offsets = {0, 0, 0, 0},
      .strides = {0, 0, 0, 0},

      .has_color_info = false,
      .yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC601,
      .sample_range = __DRI_YUV_NARROW_RANGE,
      .horizontal_siting = __DRI_YUV_CHROMA_SITING_0,
      .vertical_siting = __DRI_YUV_CHROMA_SITING_0,
   };

   return 0;
}

int
probe_gralloc(struct gralloc *gralloc)
{
   assert(gralloc);

   if (gralloc->ops != NULL && gralloc->ops->destroy != NULL)
      gralloc->ops->destroy(gralloc);

   for (int i = 0; i < ARRAY_SIZE(grallocs); i++) {
      if (!grallocs[i].probe(gralloc))
         return 0;
   }

   return -ENOTSUP;
}

int
destroy_gralloc(struct gralloc *gralloc)
{
   assert(gralloc);

   if (gralloc->ops != NULL && gralloc->ops->destroy != NULL)
      return gralloc->ops->destroy(gralloc);

   return 0;
}

int
gralloc_get_fmt_mod_info(struct gralloc *gralloc, struct android_handle *hnd,
                         struct buffer_info *out)
{
   assert(gralloc);
   assert(gralloc->ops);
   assert(hnd);
   assert(out);

   if (out->has_format_info)
      return 0;

   if (!gralloc->ops->get_fmt_mod_info)
      return -ENOTSUP;

   return gralloc->ops->get_fmt_mod_info(gralloc, hnd, out);
}

int
gralloc_get_layout_info(struct gralloc *gralloc, struct android_handle *hnd,
                        struct buffer_info *out)
{
   assert(gralloc);
   assert(gralloc->ops);
   assert(hnd);
   assert(out);

   if (out->has_layout_info)
      return 0;

   if (!gralloc->ops->get_layout_info)
      return -ENOTSUP;

   return gralloc->ops->get_layout_info(gralloc, hnd, out);
}

int
gralloc_get_color_info(struct gralloc *gralloc, struct android_handle *hnd,
                       struct buffer_info *out)
{
   assert(gralloc);
   assert(gralloc->ops);
   assert(hnd);
   assert(out);

   if (out->has_color_info)
      return 0;

   if (!gralloc->ops->get_color_info)
      return -ENOTSUP;

   return gralloc->ops->get_color_info(gralloc, hnd, out);
}

int
gralloc_get_front_rendering_usage(struct gralloc *gralloc, uint64_t *out_usage)
{
   assert(gralloc);
   assert(gralloc->ops);
   assert(out_usage);

   if (!gralloc->ops->get_front_rendering_usage)
      return -ENOTSUP;

   return gralloc->ops->get_front_rendering_usage(gralloc, out_usage);
}
