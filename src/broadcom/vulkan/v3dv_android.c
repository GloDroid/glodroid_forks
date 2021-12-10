/*
 * Copyright © 2017, Google Inc.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "v3dv_private.h"
#include <hardware/gralloc.h>

#if ANDROID_API_LEVEL >= 26
#include <hardware/gralloc1.h>
#endif

#include "drm-uapi/drm_fourcc.h"
#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>

#include "util/libsync.h"
#include "util/log.h"
#include "util/os_file.h"

static int
v3dv_hal_open(const struct hw_module_t *mod,
              const char *id,
              struct hw_device_t **dev);
static int
v3dv_hal_close(struct hw_device_t *dev);

static void UNUSED
static_asserts(void)
{
   STATIC_ASSERT(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC);
}

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
   .common =
     {
       .tag = HARDWARE_MODULE_TAG,
       .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
       .hal_api_version = HARDWARE_MAKE_API_VERSION(1, 0),
       .id = HWVULKAN_HARDWARE_MODULE_ID,
       .name = "Broadcom Vulkan HAL",
       .author = "Mesa3D",
       .methods =
         &(hw_module_methods_t) {
           .open = v3dv_hal_open,
         },
     },
};

/* If any bits in test_mask are set, then unset them and return true. */
static inline bool
unmask32(uint32_t *inout_mask, uint32_t test_mask)
{
   uint32_t orig_mask = *inout_mask;
   *inout_mask &= ~test_mask;
   return *inout_mask != orig_mask;
}

static int
v3dv_hal_open(const struct hw_module_t *mod,
              const char *id,
              struct hw_device_t **dev)
{
   assert(mod == &HAL_MODULE_INFO_SYM.common);
   assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

   hwvulkan_device_t *hal_dev = malloc(sizeof(*hal_dev));
   if (!hal_dev)
      return -1;

   *hal_dev = (hwvulkan_device_t){
      .common =
        {
          .tag = HARDWARE_DEVICE_TAG,
          .version = HWVULKAN_DEVICE_API_VERSION_0_1,
          .module = &HAL_MODULE_INFO_SYM.common,
          .close = v3dv_hal_close,
        },
     .EnumerateInstanceExtensionProperties =
        v3dv_EnumerateInstanceExtensionProperties,
     .CreateInstance = v3dv_CreateInstance,
     .GetInstanceProcAddr = v3dv_GetInstanceProcAddr,
   };

   mesa_logi("v3dv: Warning: Android Vulkan implementation is experimental");

   *dev = &hal_dev->common;
   return 0;
}

static int
v3dv_hal_close(struct hw_device_t *dev)
{
   /* hwvulkan.h claims that hw_device_t::close() is never called. */
   return -1;
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

/* get buffer info from VkNativeBufferANDROID */
static VkResult
v3dv_gralloc_info_other(struct v3dv_device *device,
                        const VkNativeBufferANDROID *native_buffer,
                        struct buffer_info *out_buffer_info)
{
   if (native_buffer->format == 0)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   out_buffer_info->strides[0] = native_buffer->stride /*in pixels*/ *
                                 get_format_bpp(native_buffer->format);
   out_buffer_info->fds[0] = native_buffer->handle->data[0];
   out_buffer_info->modifier = DRM_FORMAT_MOD_LINEAR;
   out_buffer_info->num_planes = 1;
   return VK_SUCCESS;
}

static const char cros_gralloc_module_name[] = "CrOS Gralloc";

#define CROS_GRALLOC_DRM_GET_BUFFER_INFO 4

struct cros_gralloc0_buffer_info
{
   uint32_t drm_fourcc;
   int num_fds;
   int fds[4];
   uint64_t modifier;
   int offset[4];
   int stride[4];
};

static VkResult
v3dv_gralloc_info_cros(struct v3dv_device *device,
                       const VkNativeBufferANDROID *native_buffer,
                       struct buffer_info *out_buffer_info)
{
   const gralloc_module_t *gralloc = device->gralloc;
   struct cros_gralloc0_buffer_info info;
   int ret;

   ret = gralloc->perform(gralloc, CROS_GRALLOC_DRM_GET_BUFFER_INFO,
                          native_buffer->handle, &info);
   if (ret)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   out_buffer_info->strides[0] = info.stride[0];
   out_buffer_info->modifier = info.modifier;
   out_buffer_info->fds[0] = native_buffer->handle->data[0];
   out_buffer_info->num_planes = info.num_fds;

   return VK_SUCCESS;
}

VkResult
v3dv_gralloc_info(struct v3dv_device *device,
                  const VkNativeBufferANDROID *native_buffer,
                  struct buffer_info *out_buffer_info)
{
   VkResult result = VK_SUCCESS;

   if (device->gralloc_type == V3DV_GRALLOC_UNKNOWN) {
      /* get gralloc module for gralloc buffer info query */
      int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                              (const hw_module_t **) &device->gralloc);

      device->gralloc_type = V3DV_GRALLOC_OTHER;

      if (err == 0) {
         const gralloc_module_t *gralloc = device->gralloc;
         mesa_logi("opened gralloc module name: %s", gralloc->common.name);

         if (strcmp(gralloc->common.name, cros_gralloc_module_name) == 0 &&
             gralloc->perform) {
            device->gralloc_type = V3DV_GRALLOC_CROS;
         }
      }
   }

   if (device->gralloc_type == V3DV_GRALLOC_CROS) {
      result = v3dv_gralloc_info_cros(device, native_buffer, out_buffer_info);
   } else {
      result = mapper_metadata_get_buffer_info(native_buffer->handle,
                                               out_buffer_info);

      if (result != VK_SUCCESS)
         result =
            v3dv_gralloc_info_other(device, native_buffer, out_buffer_info);
   }

   if (result != VK_SUCCESS)
      return result;

   out_buffer_info->sizes[0] = lseek(out_buffer_info->fds[0], 0, SEEK_END);

   return VK_SUCCESS;
}

VkResult
v3dv_import_native_buffer_fd(VkDevice device_h,
                             int native_buffer_fd,
                             const VkAllocationCallbacks *alloc,
                             VkImage image_h)
{
   struct v3dv_image *image = NULL;
   VkResult result;

   image = v3dv_image_from_handle(image_h);

   VkDeviceMemory memory_h;

   const VkMemoryDedicatedAllocateInfo ded_alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .buffer = VK_NULL_HANDLE,
      .image = image_h
   };

   const VkImportMemoryFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = &ded_alloc,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
      .fd = os_dupfd_cloexec(native_buffer_fd),
   };

   result =
      v3dv_AllocateMemory(device_h,
                          &(VkMemoryAllocateInfo) {
                             .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                             .pNext = &import_info,
                             .allocationSize = image->planes[0].size,
                             .memoryTypeIndex = 0,
                          },
                          alloc, &memory_h);

   if (result != VK_SUCCESS)
      goto fail_create_image;

   VkBindImageMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image = image_h,
      .memory = memory_h,
      .memoryOffset = 0,
   };
   v3dv_BindImageMemory2(device_h, 1, &bind_info);

   image->is_native_buffer_memory = true;

   return VK_SUCCESS;

fail_create_image:
   close(import_info.fd);

   return result;
}

static VkResult
format_supported_with_usage(VkDevice device_h,
                            VkFormat format,
                            VkImageUsageFlags imageUsage)
{
   V3DV_FROM_HANDLE(v3dv_device, device, device_h);
   struct v3dv_physical_device *phys_dev = &device->instance->physicalDevice;
   VkPhysicalDevice phys_dev_h = v3dv_physical_device_to_handle(phys_dev);
   VkResult result;

   const VkPhysicalDeviceImageFormatInfo2 image_format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = format,
      .type = VK_IMAGE_TYPE_2D,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = imageUsage,
   };

   VkImageFormatProperties2 image_format_props = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
   };

   /* Check that requested format and usage are supported. */
   result = v3dv_GetPhysicalDeviceImageFormatProperties2(
      phys_dev_h, &image_format_info, &image_format_props);
   if (result != VK_SUCCESS) {
      return vk_errorf(device, result,
                       "v3dv_GetPhysicalDeviceImageFormatProperties2 failed "
                       "inside %s",
                       __func__);
   }

   return VK_SUCCESS;
}

static VkResult
setup_gralloc0_usage(struct v3dv_device *device,
                     VkFormat format,
                     VkImageUsageFlags imageUsage,
                     int *grallocUsage)
{
   if (unmask32(&imageUsage, VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
      *grallocUsage |= GRALLOC_USAGE_HW_RENDER;

   if (unmask32(&imageUsage, VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_STORAGE_BIT |
                             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
      *grallocUsage |= GRALLOC_USAGE_HW_TEXTURE;

   /* All VkImageUsageFlags not explicitly checked here are unsupported for
    * gralloc swapchains.
    */
   if (imageUsage != 0) {
      return vk_errorf(device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                       "unsupported VkImageUsageFlags(0x%x) for gralloc "
                       "swapchain",
                       imageUsage);
   }

   /* Swapchain assumes direct displaying, therefore enable COMPOSER flag,
    * In case format is not supported by display controller, gralloc will
    * drop this flag and still allocate the buffer in VRAM
    */
   *grallocUsage |= GRALLOC_USAGE_HW_COMPOSER;

   if (*grallocUsage == 0)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetSwapchainGrallocUsageANDROID(VkDevice device_h,
                                     VkFormat format,
                                     VkImageUsageFlags imageUsage,
                                     int *grallocUsage)
{
   V3DV_FROM_HANDLE(v3dv_device, device, device_h);
   VkResult result;

   result = format_supported_with_usage(device_h, format, imageUsage);
   if (result != VK_SUCCESS)
      return result;

   *grallocUsage = 0;
   return setup_gralloc0_usage(device, format, imageUsage, grallocUsage);
}

#if ANDROID_API_LEVEL >= 26
VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetSwapchainGrallocUsage2ANDROID(
   VkDevice device_h,
   VkFormat format,
   VkImageUsageFlags imageUsage,
   VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
   uint64_t *grallocConsumerUsage,
   uint64_t *grallocProducerUsage)
{
   V3DV_FROM_HANDLE(v3dv_device, device, device_h);
   VkResult result;

   *grallocConsumerUsage = 0;
   *grallocProducerUsage = 0;
   mesa_logd("%s: format=%d, usage=0x%x", __func__, format, imageUsage);

   result = format_supported_with_usage(device_h, format, imageUsage);
   if (result != VK_SUCCESS)
      return result;

   int32_t grallocUsage = 0;
   result = setup_gralloc0_usage(device, format, imageUsage, &grallocUsage);
   if (result != VK_SUCCESS)
      return result;

   /* Setup gralloc1 usage flags from gralloc0 flags. */

   if (grallocUsage & GRALLOC_USAGE_HW_RENDER) {
      *grallocProducerUsage |= GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
   }

   if (grallocUsage & GRALLOC_USAGE_HW_TEXTURE) {
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
   }

   if (grallocUsage & GRALLOC_USAGE_HW_COMPOSER) {
      /* GPU composing case */
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
      /* Hardware composing case */
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER;
   }

   return VK_SUCCESS;
}
#endif

/* ----------------------------- AHardwareBuffer --------------------------- */

enum
{
   /* Usage bit equal to GRALLOC_USAGE_HW_CAMERA_MASK */
   BUFFER_USAGE_CAMERA_MASK = 0x00060000U,
};

static inline VkFormat
vk_format_from_android(unsigned android_format, unsigned android_usage)
{
   switch (android_format) {
   case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
   case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
      return VK_FORMAT_R8G8B8A8_UNORM;
   case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
      return VK_FORMAT_R8G8B8_UNORM;
   case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
      return VK_FORMAT_R5G6B5_UNORM_PACK16;
   case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
   case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:
      return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
   case AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:
      return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
   case AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED:
      if (android_usage & BUFFER_USAGE_CAMERA_MASK)
         return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
      else
         return VK_FORMAT_R8G8B8_UNORM;
   case AHARDWAREBUFFER_FORMAT_BLOB:
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

static inline unsigned
android_format_from_vk(unsigned vk_format)
{
   switch (vk_format) {
   case VK_FORMAT_R8G8B8A8_UNORM:
      return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_R8G8B8_UNORM:
      return AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
   case VK_FORMAT_R5G6B5_UNORM_PACK16:
      return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
   case VK_FORMAT_R16G16B16A16_SFLOAT:
      return AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      return AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
   default:
      return AHARDWAREBUFFER_FORMAT_BLOB;
   }
}

uint64_t
v3dv_ahb_usage_from_vk_usage(const VkImageCreateFlags vk_create,
                             const VkImageUsageFlags vk_usage)
{
   uint64_t ahb_usage = 0;
   if (vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   if (vk_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   if (vk_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;

   if (vk_create & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP;

   if (vk_create & VK_IMAGE_CREATE_PROTECTED_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT;

   /* No usage bits set - set at least one GPU usage. */
   if (ahb_usage == 0)
      ahb_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
   return ahb_usage;
}

static VkResult
get_ahb_buffer_format_properties(
   VkDevice device_h,
   const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferFormatPropertiesANDROID *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_device, device, device_h);

   /* Get a description of buffer contents . */
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(buffer, &desc);

   /* Verify description. */
   const uint64_t gpu_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                              AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                              AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

   /* "Buffer must be a valid Android hardware buffer object with at least
    * one of the AHARDWAREBUFFER_USAGE_GPU_* usage flags."
    */
   if (!(desc.usage & (gpu_usage)))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* Fill properties fields based on description. */
   VkAndroidHardwareBufferFormatPropertiesANDROID *p = pProperties;

   p->format = vk_format_from_android(desc.format, desc.usage);
   p->externalFormat = (uint64_t) (uintptr_t) p->format;

   VkFormatProperties2 format_properties = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2
   };

   v3dv_GetPhysicalDeviceFormatProperties2(
      v3dv_physical_device_to_handle(device->pdevice), p->format,
      &format_properties);

   if (desc.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER)
      p->formatFeatures =
         format_properties.formatProperties.linearTilingFeatures;
   else
      p->formatFeatures =
         format_properties.formatProperties.optimalTilingFeatures;

   /* "Images can be created with an external format even if the Android
    * hardware buffer has a format which has an equivalent Vulkan format to
    * enable consistent handling of images from sources that might use either
    * category of format. However, all images created with an external format
    * are subject to the valid usage requirements associated with external
    * formats, even if the Android hardware buffer’s format has a Vulkan
    * equivalent."
    *
    * "The formatFeatures member *must* include
    *  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT and at least one of
    *  VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT or
    *  VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT"
    */
   assert(p->formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

   p->formatFeatures |= VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;

   /* "Implementations may not always be able to determine the color model,
    *  numerical range, or chroma offsets of the image contents, so the values
    *  in VkAndroidHardwareBufferFormatPropertiesANDROID are only suggestions.
    *  Applications should treat these values as sensible defaults to use in
    *  the absence of more reliable information obtained through some other
    *  means."
    */
   p->samplerYcbcrConversionComponents.r = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.g = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.b = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.a = VK_COMPONENT_SWIZZLE_IDENTITY;

   p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
   p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;

   p->suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
   p->suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;

   return VK_SUCCESS;
}

static VkResult
get_ahb_buffer_format_properties2(
   VkDevice device_h,
   const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferFormatProperties2ANDROID *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_device, device, device_h);

   /* Get a description of buffer contents . */
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(buffer, &desc);

   /* Verify description. */
   const uint64_t gpu_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                              AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                              AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

   /* "Buffer must be a valid Android hardware buffer object with at least
    * one of the AHARDWAREBUFFER_USAGE_GPU_* usage flags."
    */
   if (!(desc.usage & (gpu_usage)))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* Fill properties fields based on description. */
   VkAndroidHardwareBufferFormatProperties2ANDROID *p = pProperties;

   p->format = vk_format_from_android(desc.format, desc.usage);
   p->externalFormat = (uint64_t) (uintptr_t) p->format;

   VkFormatProperties2 format_properties = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2
   };

   v3dv_GetPhysicalDeviceFormatProperties2(
      v3dv_physical_device_to_handle(device->pdevice), p->format,
      &format_properties);

   if (desc.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER)
      p->formatFeatures =
         format_properties.formatProperties.linearTilingFeatures;
   else
      p->formatFeatures =
         format_properties.formatProperties.optimalTilingFeatures;

   /* "Images can be created with an external format even if the Android
    * hardware buffer has a format which has an equivalent Vulkan format to
    * enable consistent handling of images from sources that might use either
    * category of format. However, all images created with an external format
    * are subject to the valid usage requirements associated with external
    * formats, even if the Android hardware buffer’s format has a Vulkan
    * equivalent."
    *
    * "The formatFeatures member *must* include
    *  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR and at least one of
    *  VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT_KHR or
    *  VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT_KHR"
    */
   assert(p->formatFeatures & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR);

   p->formatFeatures |= VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT_KHR;

   /* "Implementations may not always be able to determine the color model,
    *  numerical range, or chroma offsets of the image contents, so the values
    *  in VkAndroidHardwareBufferFormatPropertiesANDROID are only suggestions.
    *  Applications should treat these values as sensible defaults to use in
    *  the absence of more reliable information obtained through some other
    *  means."
    */
   p->samplerYcbcrConversionComponents.r = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.g = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.b = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.a = VK_COMPONENT_SWIZZLE_IDENTITY;

   p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
   p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;

   p->suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
   p->suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;

   return VK_SUCCESS;
}

VkResult
v3dv_import_ahb_memory(struct v3dv_device *device,
                       struct v3dv_device_memory *mem,
                       const VkImportAndroidHardwareBufferInfoANDROID *info,
                       VkImage image_h)
{
   VkResult result;

   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(info->buffer);

   int fd = (handle && handle->numFds) ? handle->data[0] : -1;
   if (fd < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   struct buffer_info buf_info = { 0 };

   VkNativeBufferANDROID native_buffer = {
      .sType = VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID,
      .handle = handle,
   };

   if (image_h) {
      struct v3dv_image *image = v3dv_image_from_handle(image_h);
      struct buffer_info buf_info = {0};

      VkNativeBufferANDROID native_buffer = {
         .sType = VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID,
         .handle = handle,
         .stride = 0,
         .format = 0,
      };

      int err = v3dv_gralloc_info(device, &native_buffer, &buf_info);
      if (err)
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;

      result = v3d_create_from_android_metadata(image, &buf_info);
      if (result != VK_SUCCESS)
         return result;
   }

   off_t size = lseek(fd, 0, SEEK_END);

   result = v3dv_device_import_bo(device, fd, size, &mem->bo);
   if (result != VK_SUCCESS)
      return result;

   /* "If the vkAllocateMemory command succeeds, the implementation must
    * acquire a reference to the imported hardware buffer, which it must
    * release when the device memory object is freed. If the command fails,
    * the implementation must not retain a reference."
    */
   AHardwareBuffer_acquire(info->buffer);
   mem->android_hardware_buffer = info->buffer;

   return VK_SUCCESS;
}

VkResult
v3dv_GetAndroidHardwareBufferPropertiesANDROID(
   VkDevice device_h,
   const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
   mesa_logi("In %s", __func__);
   V3DV_FROM_HANDLE(v3dv_device, dev, device_h);
   struct v3dv_physical_device *pdevice = dev->pdevice;

   VkAndroidHardwareBufferFormatPropertiesANDROID *format_prop =
      vk_find_struct(pProperties->pNext,
                     ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID);

   /* Fill format properties of an Android hardware buffer. */
   if (format_prop)
      get_ahb_buffer_format_properties(device_h, buffer, format_prop);

   VkAndroidHardwareBufferFormatProperties2ANDROID *format_prop2 =
      vk_find_struct(pProperties->pNext,
                     ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID);
   if (format_prop2)
      get_ahb_buffer_format_properties2(device_h, buffer, format_prop2);

   /* NOTE - We support buffers with only one handle but do not error on
    * multiple handle case. Reason is that we want to support YUV formats
    * where we have many logical planes but they all point to the same
    * buffer, like is the case with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
    */
   const native_handle_t *handle = AHardwareBuffer_getNativeHandle(buffer);
   int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
   if (dma_buf < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* All memory types. */
   uint32_t memory_types = (1u << pdevice->memory.memoryTypeCount) - 1;

   pProperties->allocationSize = lseek(dma_buf, 0, SEEK_END);
   pProperties->memoryTypeBits = memory_types;

   mesa_logi("%s: Size: %llu", __func__, (unsigned long long) pProperties->allocationSize);

   return VK_SUCCESS;
}

VkResult
v3dv_GetMemoryAndroidHardwareBufferANDROID(
   VkDevice device_h,
   const VkMemoryGetAndroidHardwareBufferInfoANDROID *pInfo,
   struct AHardwareBuffer **pBuffer)
{
   mesa_logi("In %s", __func__);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, pInfo->memory);

   /* Some quotes from Vulkan spec:
    *
    * "If the device memory was created by importing an Android hardware
    * buffer, vkGetMemoryAndroidHardwareBufferANDROID must return that same
    * Android hardware buffer object."
    *
    * "VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID must
    * have been included in VkExportMemoryAllocateInfo::handleTypes when
    * memory was created."
    */
   if (mem->android_hardware_buffer) {
      *pBuffer = mem->android_hardware_buffer;
      /* Increase refcount. */
      AHardwareBuffer_acquire(mem->android_hardware_buffer);
      return VK_SUCCESS;
   }

   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult
v3dv_create_ahb_memory(struct v3dv_device *device,
                       struct v3dv_device_memory *mem,
                       const VkMemoryAllocateInfo *pAllocateInfo,
                       VkImage image_h)
{
   const VkMemoryDedicatedAllocateInfo *dedicated_info = vk_find_struct_const(
      pAllocateInfo->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);

   uint32_t w = 0;
   uint32_t h = 1;
   uint32_t layers = 1;
   uint32_t format = 0;
   uint64_t usage = 0;

   /* If caller passed dedicated information. */
   if (dedicated_info && dedicated_info->image) {
      V3DV_FROM_HANDLE(v3dv_image, image, dedicated_info->image);
      w = image->planes[0].width;
      h = image->planes[0].height;
      layers = 1;
      format = android_format_from_vk(image->vk.format);
      usage = v3dv_ahb_usage_from_vk_usage(image->vk.create_flags, image->vk.usage);
   } else if (dedicated_info && dedicated_info->buffer) {
      V3DV_FROM_HANDLE(v3dv_buffer, buffer, dedicated_info->buffer);
      w = buffer->size;
      format = AHARDWAREBUFFER_FORMAT_BLOB;
      usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
              AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
   } else {
      w = pAllocateInfo->allocationSize;
      format = AHARDWAREBUFFER_FORMAT_BLOB;
      usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
              AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
   }

   struct AHardwareBuffer *android_hardware_buffer = NULL;
   struct AHardwareBuffer_Desc desc = {
      .width = w,
      .height = h,
      .layers = layers,
      .format = format,
      .usage = usage,
   };

   if (AHardwareBuffer_allocate(&desc, &android_hardware_buffer) != 0)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   mem->android_hardware_buffer = android_hardware_buffer;

   const struct VkImportAndroidHardwareBufferInfoANDROID import_info = {
      .buffer = mem->android_hardware_buffer,
   };

   VkResult result =
      v3dv_import_ahb_memory(device, mem, &import_info, image_h);

   /* Release a reference to avoid leak for AHB allocation. */
   AHardwareBuffer_release(mem->android_hardware_buffer);

   return result;
}
