#include "dmabuf_internals.h"

#include "drv_priv.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct backend backend_dmabuf_heap = {
	.name = "dmabuf",
	.init = dmabuf_driver_init,
	.close = dmabuf_driver_close,
	.bo_create = dmabuf_bo_create,
	.bo_destroy = dmabuf_bo_destroy,
	.bo_import = dmabuf_bo_import,
	.bo_map = dmabuf_bo_map,
	.bo_unmap = dmabuf_bo_unmap,
	.bo_flush = dmabuf_bo_flush,
	.bo_get_plane_fd = dmabuf_bo_get_plane_fd,
	.resolve_format_and_use_flags = dmabuf_resolve_format_and_use_flags,
};

#ifdef __cplusplus
}
#endif
