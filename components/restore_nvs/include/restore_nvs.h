#pragma once
#include "nvs.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct restore_nvs restore_nvs_t;
/// 在 RAM 中打开默认 NVS 的只读快照；下列操作只修改副本。/ Clone default NVS into RAM; subsequent operations only modify the copy.
esp_err_t restore_nvs_clone(restore_nvs_t **out);
/// 清除含用户凭据的缓冲区并释放副本。/ Wipe the image containing user credentials and release it.
void restore_nvs_free(restore_nvs_t *image);
/// 按真实 NVS 页布局演练写入。/ Rehearse a write against the actual NVS page layout.
esp_err_t restore_nvs_write(restore_nvs_t *image, const char *ns, const char *key, nvs_type_t type, const void *data, size_t len);
/// 清除已有键或命名空间；缺失时不创建。/ Erase an existing key or namespace; never create a missing one.
esp_err_t restore_nvs_erase(restore_nvs_t *image, const char *ns, const char *key);
#ifdef __cplusplus
}
#endif
