/*
 * Mesa 3-D graphics library
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef EGL_DRI2_ANDROID_INCLUDED
#define EGL_DRI2_ANDROID_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include <GL/internal/dri_interface.h>

struct buffer_info {
   uint32_t drm_fourcc;
   int num_planes;
   int fds[4];
   uint64_t modifier;
   int offsets[4];
   int pitches[4];
   enum __DRIYUVColorSpace yuv_color_space;
   enum __DRISampleRange sample_range;
   enum __DRIChromaSiting horizontal_siting;
   enum __DRIChromaSiting vertical_siting;
};

#ifdef __cplusplus
extern "C" {
#endif
extern bool
mapper_metadata_get_buffer_info(struct ANativeWindowBuffer *buf,
                                struct buffer_info *buf_info);
#ifdef __cplusplus
}
#endif

#endif /* EGL_DRI2_ANDROID_INCLUDED */
