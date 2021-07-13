#pragma once

// minigbm can't safely access lingbm_mesa, since gbm.h is exist in both minigbm and libgbm_mesa
// project and will cause conflict

int get_gbm_mesa_format(uint32_t drm_format);

void *gbm_mesa_dev_create(int fd);

void gbm_mesa_dev_destroy(void *gbm_ptr);

// ALLOCATOR ONLY
int gbm_mesa_alloc(void *gbm_ptr, int width, int height, uint32_t drm_format, bool use_scanout,
		   bool force_linear, int *out_fd, int *out_stride, uint64_t *out_modifier);

// MAPPER ONLY
void *gbm_import(void *gbm_ptr, int buf_fd, int width, int height, int stride, int modifier,
		 uint32_t drm_format);

void gbm_free(void *gbm_bo_ptr);

void gbm_map(void *gbm_bo_ptr, int w, int h, void **addr, void **map_data);

void gbm_unmap(void *gbm_bo_ptr, void *map_data);
