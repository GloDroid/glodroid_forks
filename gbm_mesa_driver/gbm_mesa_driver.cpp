#include "gbm_mesa_internals.h"

#include "drv_priv.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct backend gbm_mesa_backend = {
	.name = "gbm_mesa",
	.init = gbm_mesa_driver_init,
	.close = gbm_mesa_driver_close,
	.bo_create = gbm_mesa_bo_create,
	.bo_destroy = gbm_mesa_bo_destroy,
	.bo_import = gbm_mesa_bo_import,
	.bo_map = gbm_mesa_bo_map,
	.bo_unmap = gbm_mesa_bo_unmap,
	.resolve_format_and_use_flags = gbm_mesa_resolve_format_and_use_flags,
	.bo_get_plane_fd = gbm_mesa_bo_get_plane_fd,
};

struct backend *init_external_backend()
{
	return &gbm_mesa_backend;
}

#ifdef __cplusplus
}
#endif
