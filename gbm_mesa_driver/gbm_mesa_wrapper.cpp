#define LOG_TAG "GBM-MESA-WRAPPER"

#include <gbm.h>

#include "gbm_mesa_wrapper.h"

#include <log/log.h>
#include <drm_fourcc.h>
#include <errno.h>

#define ARRAY_SIZE(A) (sizeof(A) / sizeof(*(A)))
#define DRM_TO_GBM_FORMAT(A) { DRM_##A, GBM_##A }

/* List of the formats that has mesa3d DRI_ equivalent, therefore can be allocated directly */
static const struct {
	uint32_t drm_format;
	uint32_t gbm_format;
} drm_to_gbm_image_formats[] = {
	DRM_TO_GBM_FORMAT(FORMAT_R8),
	DRM_TO_GBM_FORMAT(FORMAT_GR88),
	DRM_TO_GBM_FORMAT(FORMAT_ARGB1555),
	DRM_TO_GBM_FORMAT(FORMAT_RGB565),
	DRM_TO_GBM_FORMAT(FORMAT_XRGB8888),
	DRM_TO_GBM_FORMAT(FORMAT_ARGB8888),
	DRM_TO_GBM_FORMAT(FORMAT_XBGR8888),
	DRM_TO_GBM_FORMAT(FORMAT_ABGR8888),
	DRM_TO_GBM_FORMAT(FORMAT_XRGB2101010),
	DRM_TO_GBM_FORMAT(FORMAT_XBGR2101010),
	DRM_TO_GBM_FORMAT(FORMAT_ARGB2101010),
	DRM_TO_GBM_FORMAT(FORMAT_ABGR2101010),
	DRM_TO_GBM_FORMAT(FORMAT_XBGR16161616F),
	DRM_TO_GBM_FORMAT(FORMAT_ABGR16161616F),
};

int get_gbm_mesa_format(uint32_t drm_format)
{
	uint32_t i;
	for (i = 0; i < ARRAY_SIZE(drm_to_gbm_image_formats); i++) {
		if (drm_to_gbm_image_formats[i].drm_format == drm_format)
			return drm_to_gbm_image_formats[i].gbm_format;
	}

	return 0;
}

void *gbm_mesa_dev_create(int fd)
{
	auto *gbm = gbm_create_device(fd);
	if (!gbm) {
		ALOGE("failed to create gbm device");
	}

	return gbm;
}

void gbm_mesa_dev_destroy(void *gbm_ptr)
{
	auto *gbm = (struct gbm_device *)gbm_ptr;

	gbm_device_destroy(gbm);
}

// ALLOCATOR_ONLY!
int gbm_mesa_alloc(void *gbm_ptr, int width, int height, uint32_t drm_format, bool use_scanout, bool force_linear, int *out_fd, int *out_stride, uint64_t *out_modifier) {
	struct gbm_bo *bo;
	auto *gbm = (struct gbm_device *)gbm_ptr;

	int gbm_format = get_gbm_mesa_format(drm_format);
	int usage = 0;
	if (force_linear)
		usage |= GBM_BO_USE_LINEAR;

	if (use_scanout)
		usage |= GBM_BO_USE_SCANOUT;

	bo = gbm_bo_create(gbm, width, height, gbm_format, usage);

	if (!bo) {
		ALOGE("failed to create BO, size=%dx%d, fmt=%d", width, height, drm_format);
		return -EINVAL;
	}

	/* gbm_mesa will create new fd, therefore it's our responsibility to close it once we don't need the buffer */
	*out_fd = gbm_bo_get_fd(bo);

	*out_stride = gbm_bo_get_stride(bo);
	*out_modifier = gbm_bo_get_modifier(bo);

	/* Buffer is now handled through the system via out_fd, we can now destroy gbm_mesa bo */
	gbm_bo_destroy(bo);

	return 0;
}

// MAPPER ONLY!
void *gbm_import(void *gbm_ptr, int buf_fd, int width, int height, int stride, int modifier, uint32_t drm_format)
{
	struct gbm_bo *bo = nullptr;
	struct gbm_import_fd_modifier_data data{};
	auto *gbm = (struct gbm_device *)gbm_ptr;

	data.width = width;
	data.height = height;
	data.format = get_gbm_mesa_format(drm_format);

	data.num_fds = 1;
	data.fds[0] = buf_fd;
	data.strides[0] = stride;
	data.modifier = modifier;
	bo = gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER, &data, 0);

	return (void *)bo;
}

void gbm_free(void *gbm_bo_ptr) {
	auto *bo = (gbm_bo *)gbm_bo_ptr;
	gbm_bo_destroy(bo);
}

void gbm_map(void *gbm_bo_ptr, int w, int h, void **addr, void **map_data) {
	int flags = GBM_BO_TRANSFER_READ | GBM_BO_TRANSFER_WRITE;

	auto *bo = (gbm_bo *)gbm_bo_ptr;

	uint32_t stride = 0;
	*addr = gbm_bo_map(bo, 0, 0, w, h, flags, &stride, map_data);
}

void gbm_unmap(void *gbm_bo_ptr, void *map_data) {
	auto *bo = (gbm_bo *)gbm_bo_ptr;
	gbm_bo_unmap(bo, map_data);
}
