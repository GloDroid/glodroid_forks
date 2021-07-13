/*
 * Copyright (C) 2021-2022 Roman Stratiienko (r.stratiienko@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "GBM-MESA-GRALLOC"

extern "C" {
#include "drv_helpers.h"
}

#include "gbm_mesa_wrapper.h"

#include "UniqueFd.h"
#include "drv_priv.h"
#include "util.h"
#include <algorithm>
#include <array>
#include <cutils/properties.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <glob.h>
#include <iterator>
#include <linux/dma-buf.h>
#include <log/log.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>

// PRIx64
#include <inttypes.h>

#ifndef EMBEDDED_GBM_WRAPPER
#define GBM_WRAPPER_NAME "libgbm_mesa_wrapper.so"
#define GBM_GET_OPS_SYMBOL "get_gbm_ops"
#else
extern "C" {
struct gbm_ops *get_gbm_ops();
}
#endif

void gbm_mesa_resolve_format_and_use_flags(struct driver *drv, uint32_t format, uint64_t use_flags,
					   uint32_t *out_format, uint64_t *out_use_flags)
{
	*out_format = format;
	*out_use_flags = use_flags;
	switch (format) {
	case DRM_FORMAT_FLEX_IMPLEMENTATION_DEFINED:
		/* Camera subsystem requires NV12. */
		if (use_flags & (BO_USE_CAMERA_READ | BO_USE_CAMERA_WRITE)) {
			*out_format = DRM_FORMAT_NV12;
		} else {
			/*HACK: See b/28671744 */
			*out_format = DRM_FORMAT_XBGR8888;
		}
		break;
	case DRM_FORMAT_FLEX_YCbCr_420_888:
		*out_format = DRM_FORMAT_NV12;
		break;
	case DRM_FORMAT_BGR565:
		/* mesa3d doesn't support BGR565 */
		*out_format = DRM_FORMAT_RGB565;
		break;
	}
}

int gbm_mesa_driver_init(struct driver *drv)
{
	return 0;
}

struct GbmMesaDriver {
	~GbmMesaDriver()
	{
		if (gbm_dev)
			wrapper->dev_destroy(gbm_dev);

		if (dl_handle)
			dlclose(dl_handle);
	}

	struct gbm_ops *wrapper = nullptr;
	struct gbm_device *gbm_dev = nullptr;
	void *dl_handle = nullptr;

	UniqueFd gbm_node_fd;
	UniqueFd gpu_node_fd;
};

struct GbmMesaDriverPriv {
	std::shared_ptr<GbmMesaDriver> gbm_mesa_drv;
};

/*
 * Check if target device has KMS.
 */
int is_kms_dev(int fd)
{
	auto res = drmModeGetResources(fd);
	if (!res)
		return false;

	bool is_kms = res->count_crtcs > 0 && res->count_connectors > 0 && res->count_encoders > 0;

	drmModeFreeResources(res);

	return is_kms;
}

/*
 * Search for a KMS device. Return opened file descriptor on success.
 */
int open_drm_dev(bool card_node, std::function<bool(int, bool, std::string)> found)
{
	glob_t glob_buf;
	memset(&glob_buf, 0, sizeof(glob_buf));
	int fd;
	const char *pattern = card_node ? "/dev/dri/card*" : "/dev/dri/renderD*";

	int ret = glob(pattern, 0, NULL, &glob_buf);
	if (ret) {
		globfree(&glob_buf);
		return -EINVAL;
	}

	for (size_t i = 0; i < glob_buf.gl_pathc; ++i) {
		fd = open(glob_buf.gl_pathv[i], O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			drv_loge("Unable to open %s with error %s", glob_buf.gl_pathv[i],
				 strerror(errno));
			continue;
		}

		drmVersionPtr drm_version;
		drm_version = drmGetVersion(fd);
		std::string drm_name(drm_version->name);
		drmFreeVersion(drm_version);

		if (found(fd, is_kms_dev(fd), drm_name))
			continue;

		close(fd);
		fd = 0;
	}

	globfree(&glob_buf);

	return 0;
}

/*
 * List of GPU which rely on separate display controller drivers,
 * For this GPUs we have to find and open /dev/cardX KMS node
 * Other GPUs can be accessed via renderD GPU node.
 */
static std::array<std::string, 6> separate_dc_gpu_list = { "v3d",      "vc4",  "etnaviv",
							   "panfrost", "lima", "freedreno" };

static bool is_separate_dc_gpu(UniqueFd *out_gpu_fd)
{
	UniqueFd gpu_fd;
	bool separate_dc = false;
	std::string gpu_name;

	open_drm_dev(false, [&](int fd, bool is_kms, std::string drm_name) -> bool {
		if (separate_dc)
			return false;

		for (const auto &name : separate_dc_gpu_list) {
			if (drm_name == std::string(name))
				separate_dc = true;
		}
		gpu_fd = UniqueFd(fd);
		gpu_name = drm_name;

		return true;
	});

	*out_gpu_fd = std::move(gpu_fd);

	drv_logi("Found GPU %s\n", gpu_name.c_str());

	return separate_dc;
}

static std::shared_ptr<GbmMesaDriver> gbm_mesa_get_or_init_driver(struct driver *drv,
								  bool mapper_sphal)
{
	std::shared_ptr<GbmMesaDriver> gbm_mesa_drv;

	if (!drv->priv) {
		gbm_mesa_drv = std::make_unique<GbmMesaDriver>();

		bool look_for_kms = is_separate_dc_gpu(&gbm_mesa_drv->gpu_node_fd);

		if (look_for_kms && !mapper_sphal) {
			drv_logi("GPU require KMSRO entry, searching for separate KMS driver...\n");
			open_drm_dev(true, [&](int fd, bool is_kms, std::string drm_name) -> bool {
				if (!is_kms || gbm_mesa_drv->gbm_node_fd)
					return false;

				gbm_mesa_drv->gbm_node_fd = UniqueFd(fd);
				drv_logi("Found KMS dev %s\n", drm_name.c_str());
				return true;
			});
			/* cardX KMS node need this otherwise composer won't be able to configure
			 * KMS state */
			if (gbm_mesa_drv->gbm_node_fd)
				drmDropMaster(gbm_mesa_drv->gbm_node_fd.Get());
			else
				drv_loge(
				    "Unable to find/open /dev/card node with KMS capabilities.\n");
		} else {
			gbm_mesa_drv->gbm_node_fd = UniqueFd(dup(gbm_mesa_drv->gpu_node_fd.Get()));
		}

		if (!gbm_mesa_drv->gbm_node_fd) {
			drv_loge("Unable to find or open DRM node");
			return nullptr;
		}

#ifndef EMBEDDED_GBM_WRAPPER
		gbm_mesa_drv->dl_handle = dlopen(GBM_WRAPPER_NAME, RTLD_NOW);
		if (gbm_mesa_drv->dl_handle == nullptr) {
			drv_loge("%s", dlerror());
			drv_loge("Unable to open '%s' shared library", GBM_WRAPPER_NAME);
			return nullptr;
		}

		auto get_gbm_ops =
		    (struct gbm_ops * (*)(void)) dlsym(gbm_mesa_drv->dl_handle, GBM_GET_OPS_SYMBOL);
		if (get_gbm_ops == nullptr) {
			drv_loge("Unable to find '%s' symbol", GBM_GET_OPS_SYMBOL);
			return nullptr;
		}
#endif

		gbm_mesa_drv->wrapper = get_gbm_ops();
		if (gbm_mesa_drv->wrapper == nullptr) {
			drv_loge("Unable to get wrapper ops");
			return nullptr;
		}

		gbm_mesa_drv->gbm_dev =
		    gbm_mesa_drv->wrapper->dev_create(gbm_mesa_drv->gbm_node_fd.Get());
		if (!gbm_mesa_drv->gbm_dev) {
			drv_loge("Unable to create gbm_mesa driver");
			return nullptr;
		}

		auto priv = new GbmMesaDriverPriv();
		priv->gbm_mesa_drv = gbm_mesa_drv;
		drv->priv = priv;
	} else {
		gbm_mesa_drv = ((GbmMesaDriverPriv *)drv->priv)->gbm_mesa_drv;
	}

	return gbm_mesa_drv;
}

void gbm_mesa_driver_close(struct driver *drv)
{
	if (drv->priv) {
		delete (GbmMesaDriverPriv *)(drv->priv);
		drv->priv = nullptr;
	}
}

struct GbmMesaBoPriv {
	~GbmMesaBoPriv()
	{
		if (gbm_bo) {
			auto wr = drv->wrapper;
			wr->free(gbm_bo);
		}
	}

	std::shared_ptr<GbmMesaDriver> drv;
	uint32_t map_stride = 0;
	UniqueFd fds[DRV_MAX_PLANES];
	struct gbm_bo *gbm_bo = nullptr;
};

static bool unmask64(uint64_t *value, uint64_t mask)
{
	if ((*value & mask) != 0) {
		*value &= ~mask;
		return true;
	}
	return false;
}

static const uint32_t supported_formats[] = {
	DRM_FORMAT_ARGB8888,	   DRM_FORMAT_XRGB8888, DRM_FORMAT_ABGR8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB565,	   DRM_FORMAT_BGR888,	DRM_FORMAT_NV12,     DRM_FORMAT_YVU420,
	DRM_FORMAT_YVU420_ANDROID, DRM_FORMAT_R8,
};

static bool is_format_supported(uint32_t format)
{
	return std::find(std::begin(supported_formats), std::end(supported_formats), format) !=
	       std::end(supported_formats);
}

int gbm_mesa_bo_create2(struct bo *bo, uint32_t width, uint32_t height, uint32_t format,
			uint64_t use_flags, bool test_only)
{
	uint64_t use_flags_copy = use_flags;

	/* Check if format is supported */
	if (!is_format_supported(format)) {
		char format_str[5] = { 0 };
		memcpy(format_str, &format, 4);
		drv_loge("Format %s is not supported", format_str);
		return -EINVAL;
	}

	/* For some ARM SOCs, if no more free CMA available, buffer can be allocated in VRAM but HWC
	 * won't be able to display it directly, using GPU for compositing */
	bool scanout_strong = false;
	bool bo_layout_ready = false;
	uint32_t size_align = 1;
	int err = 0;

	auto drv = gbm_mesa_get_or_init_driver(bo->drv, false);
	if (drv == nullptr) {
		drv_loge("Failed to init gbm driver");
		return -EINVAL;
	}

	auto wr = drv->wrapper;

	bool sw_mask = unmask64(&use_flags, BO_USE_SW_MASK);

	struct alloc_args alloc_args = {
		.gbm = drv->gbm_dev,
		.width = width,
		.height = height,
		.drm_format = wr->get_gbm_format(format) ? format : 0,
		.use_scanout = unmask64(&use_flags, BO_USE_SCANOUT | BO_USE_CURSOR),
		.force_linear = sw_mask,
		.needs_map_stride = sw_mask,
	};

	/* Alignment for RPI4 CSI camera. Since we do not care about other cameras, keep this
	 * globally for now.
	 * TODO: Create/use constraints table for camera/codecs */
	if (unmask64(&use_flags, BO_USE_CAMERA_READ | BO_USE_CAMERA_WRITE)) {
		scanout_strong = true;
		alloc_args.use_scanout = true;
		alloc_args.width = ALIGN(alloc_args.width, 32);
		size_align = 4096;
	}

	if (alloc_args.drm_format == 0) {
		/* Always use linear for spoofed format allocations. */
		drv_bo_from_format(bo, alloc_args.width, 1, alloc_args.height, format);
		bo_layout_ready = true;
		bo->meta.total_size = ALIGN(bo->meta.total_size, size_align);
		alloc_args.drm_format = DRM_FORMAT_R8;
		alloc_args.width = bo->meta.total_size;
		alloc_args.height = 1;
		alloc_args.force_linear = true;

		drv_logv("Unable to allocate 0x%08x format, allocate as 1D buffer", format);
	}

	if (alloc_args.drm_format == DRM_FORMAT_R8 && alloc_args.height == 1) {
		/* Some mesa drivers may not support 1D allocations.
		 * Use 2D texture with 4096 width instead.
		 */
		alloc_args.needs_map_stride = false;
		alloc_args.height = DIV_ROUND_UP(alloc_args.width, 4096);
		alloc_args.width = 4096;
		drv_logv("Allocate 1D buffer as %dx%d R8 2D texture", alloc_args.width,
			 alloc_args.height);
	}

	/* GBM API will always return a buffer that can be used by the GPU */
	unmask64(&use_flags, BO_USE_CURSOR | BO_USE_TEXTURE | BO_USE_RENDERING);

	if (use_flags != 0) {
		char use_str[128] = { 0 };
		drv_use_flags_to_string(use_flags, use_str, sizeof(use_str));
		drv_loge("Unsupported use flags: %s", use_str);
		return -EINVAL;
	}

	if (test_only)
		return 0;

	err = wr->alloc(&alloc_args);

	if (err && !scanout_strong) {
		drv_loge("Failed to allocate for scanout, trying non-scanout");
		alloc_args.use_scanout = false;
		err = wr->alloc(&alloc_args);
	}

	if (err) {
		drv_loge("Failed to allocate buffer");
		return err;
	}

	if (!bo_layout_ready)
		drv_bo_from_format(bo, alloc_args.out_stride, 1, alloc_args.height, format);

	char use_str[128];
	use_str[0] = 0;

	drv_use_flags_to_string(use_flags_copy, use_str, sizeof(use_str));
	char format_str[5] = { 0 };
	memcpy(format_str, &format, 4);

	drv_logv("Allocate buffer: %s %dx%d, stride %d, total_size: %llu, use: %s", format_str,
		 width, height, alloc_args.out_stride, bo->meta.total_size, use_str);

	auto priv = new GbmMesaBoPriv();
	bo->inode = drv_get_inode(alloc_args.out_fd);
	for (size_t plane = 0; plane < bo->meta.num_planes; plane++) {
		priv->fds[plane] = UniqueFd(alloc_args.out_fd);
	}

	priv->map_stride = alloc_args.out_map_stride;
	bo->meta.format_modifier = alloc_args.out_modifier;

	bo->priv = priv;
	priv->drv = drv;

	return 0;
}

int gbm_mesa_bo_import(struct bo *bo, struct drv_import_fd_data *data)
{
	if (bo->priv) {
		drv_loge("%s bo isn't empty", __func__);
		return -EINVAL;
	}
	auto priv = new GbmMesaBoPriv();
	for (size_t plane = 0; plane < bo->meta.num_planes; plane++) {
		priv->fds[plane] = UniqueFd(dup(data->fds[plane]));
	}

	bo->priv = priv;

	/* Defer GBM import to gbm_mesa_bo_map */
	return 0;
}

static int gbm_mesa_gbm_bo_import(struct bo *bo)
{
	auto priv = (GbmMesaBoPriv *)bo->priv;

	auto drv = gbm_mesa_get_or_init_driver(bo->drv, true);
	auto wr = drv->wrapper;

	uint32_t s_format = bo->meta.format;
	int s_height = bo->meta.height;
	int s_width = bo->meta.width;
	int s_stride = bo->meta.strides[0];
	if (wr->get_gbm_format(s_format) == 0) {
		s_width = bo->meta.total_size;
		s_height = 1;
		s_format = DRM_FORMAT_R8;
	}

	if (s_format == DRM_FORMAT_R8 && s_height == 1) {
		/* Some mesa drivers(lima) may not support large 1D buffers.
		 * Use 2D texture (width=4096) instead.
		 */
		s_height = DIV_ROUND_UP(s_width, 4096);
		s_width = s_stride = 4096;
	}

	priv->drv = drv;
	int fd = priv->fds[0].Get();
	priv->gbm_bo = wr->import(drv->gbm_dev, fd, s_width, s_height, s_stride,
				  bo->meta.format_modifier, s_format);

	if (!priv->gbm_bo) {
		drv_loge("Failed to import buffer: %dx%d fd(%d), s_format(0x%x), "
			 "modifier(0x%" PRIx64 "), stride(%d), into GBM",
			 s_width, s_height, fd, s_format, bo->meta.format_modifier, s_stride);
		return -EINVAL;
	}

	return 0;
}

int gbm_mesa_bo_destroy(struct bo *bo)
{
	if (bo->priv) {
		delete (GbmMesaBoPriv *)(bo->priv);
		bo->priv = nullptr;
	}
	return 0;
}

int gbm_mesa_bo_get_plane_fd(struct bo *bo, size_t plane)
{
	return dup(((GbmMesaBoPriv *)bo->priv)->fds[plane].Get());
}

void *gbm_mesa_bo_map(struct bo *bo, struct vma *vma, uint32_t map_flags)
{
	if (!(bo->meta.use_flags & BO_USE_SW_MASK)) {
		drv_loge("Can't map buffer without BO_USE_SW_MASK");
		return MAP_FAILED;
	}

	auto priv = (GbmMesaBoPriv *)bo->priv;
	if (!priv->gbm_bo) {
		if (gbm_mesa_gbm_bo_import(bo) != 0)
			return MAP_FAILED;
	}

	auto drv = gbm_mesa_get_or_init_driver(bo->drv, true);
	auto wr = drv->wrapper;

	vma->length = bo->meta.total_size;

	void *buf = MAP_FAILED;

	uint32_t s_format = bo->meta.format;
	int s_width = bo->meta.width;
	int s_height = bo->meta.height;
	if (wr->get_gbm_format(s_format) == 0) {
		s_width = bo->meta.total_size;
		s_height = 1;
	}

	wr->map(priv->gbm_bo, s_width, s_height, &buf, &vma->priv);

	return buf;
}

int gbm_mesa_bo_unmap(struct bo *bo, struct vma *vma)
{
	if (!(bo->meta.use_flags & BO_USE_SW_MASK)) {
		drv_loge("Can't unmap buffer without BO_USE_SW_MASK");
		return -EINVAL;
	}

	auto drv = gbm_mesa_get_or_init_driver(bo->drv, true);
	auto wr = drv->wrapper;

	auto priv = (GbmMesaBoPriv *)bo->priv;
	if (vma->priv == nullptr || priv->gbm_bo == nullptr) {
		drv_loge("Buffer internal state is invalid");
		return -EINVAL;
	}
	wr->unmap(priv->gbm_bo, vma->priv);
	vma->priv = nullptr;
	return 0;
}

uint32_t gbm_mesa_bo_get_map_stride(struct bo *bo)
{
	auto priv = (GbmMesaBoPriv *)bo->priv;

	return priv->map_stride;
}
