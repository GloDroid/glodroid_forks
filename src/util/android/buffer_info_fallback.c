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

#include "buffer_info.h"

#include <hardware/gralloc.h>

#include "drm-uapi/drm_fourcc.h"
#include "util/log.h"
#include "util/macros.h"

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

enum chroma_order {
   YCbCr,
   YCrCb,
};

struct droid_yuv_format {
   /* Lookup keys */
   int native;                     /* HAL_PIXEL_FORMAT_ */
   enum chroma_order chroma_order; /* chroma order is {Cb, Cr} or {Cr, Cb} */
   int chroma_step; /* Distance in bytes between subsequent chroma pixels. */

   /* Result */
   int fourcc; /* DRM_FORMAT_ */
};

/* The following table is used to look up a DRI image FourCC based
 * on native format and information contained in android_ycbcr struct. */
static const struct droid_yuv_format droid_yuv_formats[] = {
   /* Native format, YCrCb, Chroma step, DRI image FourCC */
   {HAL_PIXEL_FORMAT_YCbCr_420_888, YCbCr, 2, DRM_FORMAT_NV12},
   {HAL_PIXEL_FORMAT_YCbCr_420_888, YCbCr, 1, DRM_FORMAT_YUV420},
   {HAL_PIXEL_FORMAT_YCbCr_420_888, YCrCb, 1, DRM_FORMAT_YVU420},
   {HAL_PIXEL_FORMAT_YV12, YCrCb, 1, DRM_FORMAT_YVU420},
   /* HACK: See droid_create_image_from_prime_fds() and
    * https://issuetracker.google.com/32077885. */
   {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCbCr, 2, DRM_FORMAT_NV12},
   {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCbCr, 1, DRM_FORMAT_YUV420},
   {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_YVU420},
   {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_AYUV},
   {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_XYUV8888},
};

static int
get_fourcc_yuv(int native, enum chroma_order chroma_order, int chroma_step)
{
   for (int i = 0; i < ARRAY_SIZE(droid_yuv_formats); ++i)
      if (droid_yuv_formats[i].native == native &&
          droid_yuv_formats[i].chroma_order == chroma_order &&
          droid_yuv_formats[i].chroma_step == chroma_step)
         return droid_yuv_formats[i].fourcc;

   return -1;
}

static bool
is_yuv(int native)
{
   for (int i = 0; i < ARRAY_SIZE(droid_yuv_formats); ++i)
      if (droid_yuv_formats[i].native == native)
         return true;

   return false;
}

static int
get_format_bpp(int native)
{
   int bpp;

   switch (native) {
   case HAL_PIXEL_FORMAT_RGBA_FP16:
      bpp = 8;
      break;
   case HAL_PIXEL_FORMAT_RGBA_8888:
   case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      /*
       * HACK: Hardcode this to RGBX_8888 as per cros_gralloc hack.
       * TODO: Remove this once https://issuetracker.google.com/32077885 is
       * fixed.
       */
   case HAL_PIXEL_FORMAT_RGBX_8888:
   case HAL_PIXEL_FORMAT_BGRA_8888:
   case HAL_PIXEL_FORMAT_RGBA_1010102:
      bpp = 4;
      break;
   case HAL_PIXEL_FORMAT_RGB_565:
      bpp = 2;
      break;
   default:
      bpp = 0;
      break;
   }

   return bpp;
}

/* createImageFromFds requires fourcc format */
static int
get_fourcc(int native)
{
   switch (native) {
   case HAL_PIXEL_FORMAT_RGB_565:
      return DRM_FORMAT_RGB565;
   case HAL_PIXEL_FORMAT_BGRA_8888:
      return DRM_FORMAT_ARGB8888;
   case HAL_PIXEL_FORMAT_RGBA_8888:
      return DRM_FORMAT_ABGR8888;
   case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      /*
       * HACK: Hardcode this to RGBX_8888 as per cros_gralloc hack.
       * TODO: Remove this once https://issuetracker.google.com/32077885 is
       * fixed.
       */
   case HAL_PIXEL_FORMAT_RGBX_8888:
      return DRM_FORMAT_XBGR8888;
   case HAL_PIXEL_FORMAT_RGBA_FP16:
      return DRM_FORMAT_ABGR16161616F;
   case HAL_PIXEL_FORMAT_RGBA_1010102:
      return DRM_FORMAT_ABGR2101010;
   default:
      mesa_logw("unsupported native buffer format 0x%x", native);
   }
   return -1;
}

/* returns # of fds, and by reference the actual fds */
static unsigned
get_native_buffer_fds(const native_handle_t *handle, int fds[3])
{
   if (!handle)
      return 0;

   /*
    * Various gralloc implementations exist, but the dma-buf fd tends
    * to be first. Access it directly to avoid a dependency on specific
    * gralloc versions.
    */
   for (int i = 0; i < handle->numFds; i++)
      fds[i] = handle->data[i];

   return handle->numFds;
}

static int
fallback_gralloc_get_yuv_info(struct gralloc *gralloc,
                              struct android_handle *hnd,
                              struct buffer_info *out)
{
   gralloc_module_t *gr_mod = gralloc->priv;
   enum chroma_order chroma_order;
   struct android_ycbcr ycbcr;
   int drm_fourcc = 0;
   int num_planes = 0;
   int num_fds = 0;
   int fds[3];
   int ret;

   num_fds = get_native_buffer_fds(hnd->handle, fds);
   if (num_fds == 0)
      return -EINVAL;

   if (!gr_mod || !gr_mod->lock_ycbcr) {
      return -EINVAL;
   }

   memset(&ycbcr, 0, sizeof(ycbcr));
   ret = gr_mod->lock_ycbcr(gr_mod, hnd->handle, 0, 0, 0, 0, 0, &ycbcr);
   if (ret) {
      /* HACK: See native_window_buffer_get_buffer_info() and
       * https://issuetracker.google.com/32077885.*/
      if (hnd->hal_format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)
         return -EAGAIN;

      mesa_logw("gralloc->lock_ycbcr failed: %d", ret);
      return -EINVAL;
   }
   gr_mod->unlock(gr_mod, hnd->handle);

   chroma_order = ((size_t)ycbcr.cr < (size_t)ycbcr.cb) ? YCrCb : YCbCr;

   /* .chroma_step is the byte distance between the same chroma channel
    * values of subsequent pixels, assumed to be the same for Cb and Cr. */
   drm_fourcc =
      get_fourcc_yuv(hnd->hal_format, chroma_order, ycbcr.chroma_step);
   if (drm_fourcc == -1) {
      mesa_logw(
         "unsupported YUV format, native = %x, chroma_order = %s, chroma_step = %zu",
         hnd->hal_format, chroma_order == YCbCr ? "YCbCr" : "YCrCb",
         ycbcr.chroma_step);
      return -EINVAL;
   }

   if (!out->has_format_info) {
      out->drm_fourcc = drm_fourcc;
      out->modifier = DRM_FORMAT_MOD_INVALID;
      out->has_format_info = true;
   }

   if (out->has_layout_info)
      return 0;

   num_planes = ycbcr.chroma_step == 2 ? 2 : 3;
   /* When lock_ycbcr's usage argument contains no SW_READ/WRITE flags
    * it will return the .y/.cb/.cr pointers based on a NULL pointer,
    * so they can be interpreted as offsets. */
   out->offsets[0] = (size_t)ycbcr.y;
   /* We assume here that all the planes are located in one DMA-buf. */
   if (chroma_order == YCrCb) {
      out->offsets[1] = (size_t)ycbcr.cr;
      out->offsets[2] = (size_t)ycbcr.cb;
   } else {
      out->offsets[1] = (size_t)ycbcr.cb;
      out->offsets[2] = (size_t)ycbcr.cr;
   }

   /* .ystride is the line length (in bytes) of the Y plane,
    * .cstride is the line length (in bytes) of any of the remaining
    * Cb/Cr/CbCr planes, assumed to be the same for Cb and Cr for fully
    * planar formats. */
   out->strides[0] = ycbcr.ystride;
   out->strides[1] = out->strides[2] = ycbcr.cstride;

   /*
    * Since this is EGL_NATIVE_BUFFER_ANDROID don't assume that
    * the single-fd case cannot happen.  So handle eithe single
    * fd or fd-per-plane case:
    */
   if (num_fds == 1) {
      out->fds[1] = out->fds[0] = fds[0];
      if (out->num_planes == 3)
         out->fds[2] = fds[0];
   } else {
      assert(num_fds == out->num_planes);
      out->fds[0] = fds[0];
      out->fds[1] = fds[1];
      out->fds[2] = fds[2];
   }

   out->has_layout_info = true;

   return 0;
}

static int
fallback_gralloc_get_buffer_info(struct gralloc *gralloc,
                                 struct android_handle *hnd,
                                 struct buffer_info *out)
{
   int num_planes = 0;
   int drm_fourcc = 0;
   int stride = 0;
   int fds[3];

   if (is_yuv(hnd->hal_format)) {
      int ret = fallback_gralloc_get_yuv_info(gralloc, hnd, out);
      /*
       * HACK: https://issuetracker.google.com/32077885
       * There is no API available to properly query the IMPLEMENTATION_DEFINED
       * format. As a workaround we rely here on gralloc allocating either
       * an arbitrary YCbCr 4:2:0 or RGBX_8888, with the latter being recognized
       * by lock_ycbcr failing.
       */
      if (ret != -EAGAIN)
         return ret;
   }

   /*
    * Non-YUV formats could *also* have multiple planes, such as ancillary
    * color compression state buffer, but the rest of the code isn't ready
    * yet to deal with modifiers:
    */
   num_planes = get_native_buffer_fds(hnd->handle, fds);
   if (num_planes == 0)
      return -EINVAL;

   assert(num_planes == 1);

   drm_fourcc = get_fourcc(hnd->hal_format);
   if (drm_fourcc == -1) {
      mesa_loge("Failed to get drm_fourcc");
      return -EINVAL;
   }

   stride = hnd->pixel_stride * get_format_bpp(hnd->hal_format);
   if (stride == 0) {
      mesa_loge("Failed to calcuulate stride");
      return -EINVAL;
   }

   if (out->has_format_info) {
      out->drm_fourcc = drm_fourcc;
      out->modifier = DRM_FORMAT_MOD_INVALID;
      out->has_format_info = true;
   }

   if (out->has_layout_info) {
      out->num_planes = num_planes;
      out->fds[0] = fds[0];
      out->strides[0] = stride;
      out->has_layout_info = true;
   }

   return 0;
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

static struct buffer_info_ops fallback_gralloc_ops = {
   .get_fmt_mod_info = fallback_gralloc_get_buffer_info,
   .get_layout_info = fallback_gralloc_get_buffer_info,
   .destroy = destroy,
};

int fallback_gralloc_probe(struct gralloc *gralloc);

int
fallback_gralloc_probe(struct gralloc *gralloc)
{
   gralloc_module_t *gr_mod = NULL;
   int err = 0;

   err =
      hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&gr_mod);

   if (err) {
      mesa_logw(
         "No gralloc hwmodule detected (video buffers won't be supported)");
   } else if (!gr_mod->lock_ycbcr) {
      mesa_logw(
         "Gralloc doesn't support lock_ycbcr (video buffers won't be supported)");
   } else {
      gralloc->priv = gr_mod;
   }

   gralloc->ops = &fallback_gralloc_ops;

   mesa_logi("Using fallback gralloc implementation");

   return 0;
}
