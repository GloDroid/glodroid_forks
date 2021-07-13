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

#include "gbm_mesa_internals.h"

#include "drv_priv.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct backend backend_gbm_mesa = {
	.name = "gbm_mesa",
	.init = gbm_mesa_driver_init,
	.close = gbm_mesa_driver_close,
	.bo_create_v2 = gbm_mesa_bo_create2,
	.bo_destroy = gbm_mesa_bo_destroy,
	.bo_import = gbm_mesa_bo_import,
	.bo_map = gbm_mesa_bo_map,
	.bo_unmap = gbm_mesa_bo_unmap,
	.bo_get_plane_fd = gbm_mesa_bo_get_plane_fd,
	.bo_get_map_stride = gbm_mesa_bo_get_map_stride,
	.resolve_format_and_use_flags = gbm_mesa_resolve_format_and_use_flags,
};

#ifdef __cplusplus
}
#endif
