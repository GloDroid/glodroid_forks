/*
 * Copyright © 2021, Google Inc.
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

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <hardware/gralloc.h>

#include "util/log.h"

#include "buffer_info.h"

/* More recent CrOS gralloc has a perform op that fills out the struct below
 * with canonical information about the buffer and its modifier, planes,
 * offsets and strides.  If we have this, we can skip straight to
 * createImageFromDmaBufs2() and avoid all the guessing and recalculations.
 * This also gives us the modifier and plane offsets/strides for multiplanar
 * compressed buffers (eg Intel CCS buffers) in order to make that work in
 * Android.
 */

static const char cros_gralloc_module_name[] = "CrOS Gralloc";

#define CROS_GRALLOC_DRM_GET_BUFFER_INFO               4
#define CROS_GRALLOC_DRM_GET_USAGE                     5
#define CROS_GRALLOC_DRM_GET_USAGE_FRONT_RENDERING_BIT 0x1

struct cros_gralloc0_buffer_info {
   uint32_t drm_fourcc;
   int num_fds;
   int fds[4];
   uint64_t modifier;
   int offset[4];
   int stride[4];
};

static int
cros_get_buffer_info(struct gralloc *gralloc, struct android_handle *hnd,
                     struct buffer_info *out)
{
   struct cros_gralloc0_buffer_info info;
   gralloc_module_t *gr_mod = gralloc->priv;

   assert(gralloc->priv);

   if (gr_mod->perform(gr_mod, CROS_GRALLOC_DRM_GET_BUFFER_INFO, hnd->handle,
                       &info) == 0) {
      if (!out->has_format_info) {
         out->drm_fourcc = info.drm_fourcc;
         out->modifier = info.modifier;
         out->has_format_info = true;
      }

      if (!out->has_layout_info) {
         out->num_planes = info.num_fds;
         for (int i = 0; i < out->num_planes; i++) {
            out->fds[i] = info.fds[i];
            out->offsets[i] = info.offset[i];
            out->strides[i] = info.stride[i];
         }
         out->has_layout_info = true;
      }

      return 0;
   }

   return -EINVAL;
}

static int
cros_get_front_rendering_usage(struct gralloc *gralloc, uint64_t *out_usage)
{
   gralloc_module_t *gr_mod = (gralloc_module_t *)gralloc->priv;
   uint32_t front_rendering_usage = 0;

   assert(gr_mod);

   if (gr_mod->perform(gr_mod, CROS_GRALLOC_DRM_GET_USAGE,
                       CROS_GRALLOC_DRM_GET_USAGE_FRONT_RENDERING_BIT,
                       &front_rendering_usage) == 0) {
      *out_usage = front_rendering_usage;
      return 0;
   }

   return -ENOTSUP;
}

static int
destroy(struct gralloc *gralloc)
{
   if (gralloc->priv) {
      gralloc_module_t *gr_mod = gralloc->priv;

      dlclose(gr_mod->common.dso);
      gralloc->priv = NULL;
   }

   gralloc->ops = NULL;

   return 0;
}

static struct buffer_info_ops cros_api_ops = {
   .get_fmt_mod_info = cros_get_buffer_info,
   .get_layout_info = cros_get_buffer_info,
   .get_front_rendering_usage = cros_get_front_rendering_usage,
   .destroy = destroy,
};

int gralloc_v0_cros_api_probe(struct gralloc *gralloc);

int
gralloc_v0_cros_api_probe(struct gralloc *gralloc)
{
   gralloc_module_t *gr_mod = NULL;
   int err = 0;

   err =
      hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&gr_mod);

   if (err)
      goto fail;

   if (strcmp(gr_mod->common.name, cros_gralloc_module_name) != 0)
      goto fail;

   if (!gr_mod->perform) {
      mesa_logw("Oops. CrOS gralloc doesn't have perform callback");
      goto fail;
   }

   gralloc->priv = gr_mod;
   gralloc->ops = &cros_api_ops;

   mesa_logi("Using gralloc0 CrOS API");

   return 0;

fail:
   destroy(gralloc);

   return -ENOTSUP;
}
