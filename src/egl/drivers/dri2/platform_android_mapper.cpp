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

#if ANDROID_API_LEVEL >= 30 && defined(USE_IMAPPER4_METADATA_API)

#include <platform_android.h>

#include <system/window.h>
#include <aidl/android/hardware/graphics/common/ChromaSiting.h>
#include <aidl/android/hardware/graphics/common/Dataspace.h>
#include <aidl/android/hardware/graphics/common/ExtendableType.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponent.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponentType.h>
#include <gralloctypes/Gralloc4.h>

using aidl::android::hardware::graphics::common::ChromaSiting;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::ExtendableType;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::PlaneLayoutComponent;
using aidl::android::hardware::graphics::common::PlaneLayoutComponentType;
using android::hardware::graphics::common::V1_2::BufferUsage;
using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;
using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using MetadataType =
   android::hardware::graphics::mapper::V4_0::IMapper::MetadataType;

Error
GetMetadata(android::sp<IMapper> mapper, buffer_handle_t buffer,
            MetadataType type, hidl_vec<uint8_t>* metadata)
{
   Error error = Error::NONE;

   auto native_handle = const_cast<native_handle_t*>(buffer);

   auto ret = mapper->get(native_handle, type,
                          [&](const auto& get_error, const auto& get_metadata) {
                              error = get_error;
                              *metadata = get_metadata;
                          });

   if (!ret.isOk())
      error = Error::NO_RESOURCES;

   return error;
}

std::optional<std::vector<PlaneLayout>> GetPlaneLayouts(
   android::sp<IMapper> mapper, buffer_handle_t buffer)
{
   hidl_vec<uint8_t> encoded_layouts;

   Error error = GetMetadata(mapper, buffer,
                            android::gralloc4::MetadataType_PlaneLayouts,
                            &encoded_layouts);

   if (error != Error::NONE)
      return std::nullopt;

   std::vector<PlaneLayout> plane_layouts;
   android::gralloc4::decodePlaneLayouts(encoded_layouts, &plane_layouts);
   return plane_layouts;
}

extern "C"
{

bool
mapper_metadata_get_buffer_info(struct ANativeWindowBuffer *buf,
                                struct buffer_info *buf_info)
{
   static android::sp<IMapper> mapper = IMapper::getService();
   if (mapper == nullptr)
      return false;

   if (!buf->handle)
      return false;

   hidl_vec<uint8_t> encoded_format;
   auto err = GetMetadata(mapper, buf->handle, android::gralloc4::MetadataType_PixelFormatFourCC, &encoded_format);
   if (err != Error::NONE)
      return false;

   android::gralloc4::decodePixelFormatFourCC(encoded_format, &buf_info->drm_fourcc);

   hidl_vec<uint8_t> encoded_modifier;
   err = GetMetadata(mapper, buf->handle, android::gralloc4::MetadataType_PixelFormatModifier, &encoded_modifier);
   if (err != Error::NONE)
      return false;

   android::gralloc4::decodePixelFormatModifier(encoded_modifier, &buf_info->modifier);

   auto layouts_opt = GetPlaneLayouts(mapper, buf->handle);

   if (!layouts_opt)
      return false;

   std::vector<PlaneLayout>& layouts = *layouts_opt;

   buf_info->num_planes = layouts.size();

   bool per_plane_unique_fd = buf->handle->numFds == buf_info->num_planes;

   for (uint32_t i = 0; i < layouts.size(); i++) {
      buf_info->fds[i] = per_plane_unique_fd ? buf->handle->data[i] : buf->handle->data[0];
      buf_info->pitches[i] = layouts[i].strideInBytes;
      buf_info->offsets[i] = layouts[i].offsetInBytes;
   }

   /* optional attributes */
   hidl_vec<uint8_t> encoded_chroma_siting;
   err = GetMetadata(mapper, buf->handle, android::gralloc4::MetadataType_ChromaSiting, &encoded_chroma_siting);
   if (err == Error::NONE) {
      ExtendableType chroma_siting_ext;
      android::gralloc4::decodeChromaSiting(encoded_chroma_siting, &chroma_siting_ext);
      ChromaSiting chroma_siting = android::gralloc4::getStandardChromaSitingValue(chroma_siting_ext);
      switch (chroma_siting) {
         case ChromaSiting::SITED_INTERSTITIAL:
            buf_info->horizontal_siting = __DRI_YUV_CHROMA_SITING_0_5;
            buf_info->vertical_siting = __DRI_YUV_CHROMA_SITING_0_5;
            break;
         case ChromaSiting::COSITED_HORIZONTAL:
            buf_info->horizontal_siting = __DRI_YUV_CHROMA_SITING_0;
            buf_info->vertical_siting = __DRI_YUV_CHROMA_SITING_0_5;
            break;
         default:
            break;
      }
   }

   hidl_vec<uint8_t> encoded_dataspace;
   err = GetMetadata(mapper, buf->handle, android::gralloc4:: MetadataType_Dataspace, &encoded_dataspace);
   if (err == Error::NONE) {
      Dataspace dataspace;
      android::gralloc4::decodeDataspace(encoded_dataspace, &dataspace);

      Dataspace standard = (Dataspace)((int)dataspace & (uint32_t)Dataspace::STANDARD_MASK);
      switch (standard) {
         case Dataspace::STANDARD_BT709:
            buf_info->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC709;
            break;
         case Dataspace::STANDARD_BT601_625:
         case Dataspace::STANDARD_BT601_625_UNADJUSTED:
         case Dataspace::STANDARD_BT601_525:
         case Dataspace::STANDARD_BT601_525_UNADJUSTED:
            buf_info->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC601;
            break;
         case Dataspace::STANDARD_BT2020:
         case Dataspace::STANDARD_BT2020_CONSTANT_LUMINANCE:
            buf_info->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC2020;
            break;
         default:
            break;
      }

      Dataspace range = (Dataspace)((int)dataspace & (uint32_t)Dataspace::RANGE_MASK);
      switch (range) {
         case Dataspace::RANGE_FULL:
            buf_info->sample_range = __DRI_YUV_FULL_RANGE;
            break;
         case Dataspace::RANGE_LIMITED:
            buf_info->sample_range = __DRI_YUV_NARROW_RANGE;
            break;
         default:
            break;
      }
   }

   return true;
}

} // extern "C"

#endif
