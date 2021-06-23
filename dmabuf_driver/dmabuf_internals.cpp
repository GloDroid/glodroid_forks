#define LOG_TAG "DMABUF-GRALLOC"

extern "C" {
#include "drv_helpers.h"
}

#include "UniqueFd.h"
#include "dma-heap.h"
#include "drv_priv.h"
#include "util.h"
#include <algorithm>
#include <cutils/properties.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <glob.h>
#include <iterator>
#include <linux/dma-buf.h>
#include <log/log.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>

void dmabuf_resolve_format_and_use_flags(struct driver *drv, uint32_t format, uint64_t use_flags,
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

static const uint32_t scanout_render_formats[] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888,
						   DRM_FORMAT_ABGR8888, DRM_FORMAT_XBGR8888,
						   DRM_FORMAT_RGB565 };

static const uint32_t texture_only_formats[] = { DRM_FORMAT_NV12, DRM_FORMAT_NV21,
						 DRM_FORMAT_YVU420, DRM_FORMAT_YVU420_ANDROID };

static struct format_metadata linear_metadata = { 1, 0, DRM_FORMAT_MOD_LINEAR };

int dmabuf_driver_init(struct driver *drv)
{
	// TODO
	/*
	 * in case no allocation needed (Mapper HAL), we do not need to
	 * waste a time to initialize the internals of the driver.
	 */
	drv_add_combinations(drv, scanout_render_formats, ARRAY_SIZE(scanout_render_formats),
			     &linear_metadata, BO_USE_RENDER_MASK | BO_USE_SCANOUT);

	drv_add_combinations(drv, texture_only_formats, ARRAY_SIZE(texture_only_formats),
			     &linear_metadata, BO_USE_TEXTURE_MASK | BO_USE_SCANOUT);

	drv_add_combination(drv, DRM_FORMAT_R8, &linear_metadata, BO_USE_SW_MASK | BO_USE_LINEAR);

	// Fixes android.hardware.cts.HardwareBufferTest#testCreate CTS test
	drv_add_combination(drv, DRM_FORMAT_BGR888, &linear_metadata, BO_USE_SW_MASK);

	drv_modify_combination(drv, DRM_FORMAT_NV12, &linear_metadata,
			       BO_USE_HW_VIDEO_ENCODER | BO_USE_HW_VIDEO_DECODER |
				   BO_USE_CAMERA_READ | BO_USE_CAMERA_WRITE);
	drv_modify_combination(drv, DRM_FORMAT_NV21, &linear_metadata, BO_USE_HW_VIDEO_ENCODER);

	/*
	 * R8 format is used for Android's HAL_PIXEL_FORMAT_BLOB and is used for JPEG snapshots
	 * from camera and input/output from hardware decoder/encoder.
	 */
	drv_modify_combination(drv, DRM_FORMAT_R8, &linear_metadata,
			       BO_USE_CAMERA_READ | BO_USE_CAMERA_WRITE | BO_USE_HW_VIDEO_DECODER |
				   BO_USE_HW_VIDEO_ENCODER);

	/*
	 * Android also frequently requests YV12 formats for some camera implementations
	 * (including the external provider implmenetation).
	 */
	drv_modify_combination(drv, DRM_FORMAT_YVU420_ANDROID, &linear_metadata,
			       BO_USE_CAMERA_READ | BO_USE_CAMERA_WRITE);

	return drv_modify_linear_combinations(drv);
}

struct DmabufDriver {
	UniqueFd system_heap_fd;
	UniqueFd system_heap_uncached_fd;
	UniqueFd cma_heap_fd;
};

struct DmabufDriverPriv {
	std::shared_ptr<DmabufDriver> dmabuf_drv;
};

static std::shared_ptr<DmabufDriver> dmabuf_get_or_init_driver(struct driver *drv)
{
	std::shared_ptr<DmabufDriver> dmabuf_drv;

	if (!drv->priv) {
		dmabuf_drv = std::make_unique<DmabufDriver>();
		dmabuf_drv->system_heap_fd =
		    UniqueFd(open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC));

		if (!dmabuf_drv->system_heap_fd) {
			drv_loge("Can't open system heap, errno: %i", -errno);
			return nullptr;
		}

		dmabuf_drv->system_heap_uncached_fd =
		    UniqueFd(open("/dev/dma_heap/system-uncached", O_RDONLY | O_CLOEXEC));

		if (!dmabuf_drv->system_heap_uncached_fd) {
			drv_logi("No system-uncached dmabuf-heap found. Falling back to system.");
			dmabuf_drv->system_heap_uncached_fd =
			    UniqueFd(dup(dmabuf_drv->system_heap_fd.Get()));
		}

		dmabuf_drv->cma_heap_fd =
		    UniqueFd(open("/dev/dma_heap/linux,cma", O_RDONLY | O_CLOEXEC));
		if (!dmabuf_drv->cma_heap_fd) {
			drv_logi("No system-uncached dmabuf-heap found. Falling back to system.");
			dmabuf_drv->cma_heap_fd = UniqueFd(dup(dmabuf_drv->system_heap_fd.Get()));
		}

		auto priv = new DmabufDriverPriv();
		priv->dmabuf_drv = dmabuf_drv;
		drv->priv = priv;
	} else {
		dmabuf_drv = ((DmabufDriverPriv *)drv->priv)->dmabuf_drv;
	}

	return dmabuf_drv;
}

void dmabuf_driver_close(struct driver *drv)
{
	if (drv->priv) {
		delete (DmabufDriverPriv *)(drv->priv);
		drv->priv = nullptr;
	}
}

struct DmabufBoPriv {
	UniqueFd fds[DRV_MAX_PLANES];
};

int dmabuf_bo_create(struct bo *bo, uint32_t width, uint32_t height, uint32_t format,
		     uint64_t use_flags)
{
	auto drv = dmabuf_get_or_init_driver(bo->drv);

	if (drv == nullptr)
		return -EINVAL;

	int stride = drv_stride_from_format(format, width, 0);
	drv_bo_from_format(bo, stride, height, format);

	struct dma_heap_allocation_data heap_data {
		.len = bo->meta.total_size, .fd_flags = O_RDWR | O_CLOEXEC,
	};

	int heap_fd = drv->system_heap_fd.Get();

	if (!(use_flags & BO_USE_SW_MASK))
		heap_fd = drv->system_heap_uncached_fd.Get();

	if (use_flags & BO_USE_SCANOUT)
		heap_fd = drv->cma_heap_fd.Get();

	int ret = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data);
	if (ret) {
		drv_loge("Failed to allocate dmabuf: %s", strerror(errno));
		return -errno;
	}

	auto buf_fd = UniqueFd(heap_data.fd);

	auto priv = new DmabufBoPriv();
	uint32_t inode = drv_get_inode(buf_fd.Get());
	for (size_t plane = 0; plane < bo->meta.num_planes; plane++) {
		priv->fds[plane] = UniqueFd(dup(buf_fd.Get()));
		bo->inodes[plane] = inode;
	}

	bo->priv = priv;

	return 0;
}

int dmabuf_bo_import(struct bo *bo, struct drv_import_fd_data *data)
{
	if (bo->priv) {
		drv_loge("%s bo isn't empty", __func__);
		return -EINVAL;
	}
	auto priv = new DmabufBoPriv();
	for (size_t plane = 0; plane < bo->meta.num_planes; plane++) {
		priv->fds[plane] = UniqueFd(dup(data->fds[plane]));
	}

	bo->priv = priv;

	return 0;
}

int dmabuf_bo_destroy(struct bo *bo)
{
	if (bo->priv) {
		delete (DmabufBoPriv *)(bo->priv);
		bo->priv = nullptr;
	}
	return 0;
}

int dmabuf_bo_get_plane_fd(struct bo *bo, size_t plane)
{
	return dup(((DmabufBoPriv *)bo->priv)->fds[plane].Get());
}

void *dmabuf_bo_map(struct bo *bo, struct vma *vma, size_t plane, uint32_t map_flags)
{
	vma->length = bo->meta.total_size;

	auto priv = (DmabufBoPriv *)bo->priv;

	void *buf =
	    mmap(0, vma->length, drv_get_prot(map_flags), MAP_SHARED, priv->fds[0].Get(), 0);
	if (buf == MAP_FAILED) {
		drv_loge("%s mmap err, errno: %i", __func__, -errno);
		return buf;
	}

	return buf;
}

int dmabuf_bo_unmap(struct bo *bo, struct vma *vma)
{
	return munmap(vma->addr, vma->length);
}

int dmabuf_bo_flush(struct bo *bo, struct mapping *mapping)
{
	auto priv = (DmabufBoPriv *)bo->priv;
	struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW };

	int ret = ioctl(priv->fds[0].Get(), DMA_BUF_IOCTL_SYNC, &sync);
	if (ret)
		drv_loge("DMA_BUF_IOCTL_SYNC DMA_BUF_SYNC_END failed");

	sync = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW };
	ret = ioctl(priv->fds[0].Get(), DMA_BUF_IOCTL_SYNC, &sync);
	if (ret)
		drv_loge("DMA_BUF_IOCTL_SYNC DMA_BUF_SYNC_START failed");

	return 0;
}
