#include "restore_nvs.h"
#include "esp_partition.h"
#include "nvs_storage.hpp"
#include "nvs_platform.hpp"
#include <cstring>

// 只读闪存一次；即使页回收或修复，SDK 也只能写入这个 RAM 分区。/ Read flash once; even SDK compaction and page recovery can only write RAM.
struct restore_nvs final : nvs::Partition {
    uint8_t *bytes = nullptr;
    uint32_t size = 0;
    nvs::Storage storage{this};
    const char *get_partition_name() override { return "restore_ram"; }
    uint32_t get_address() override { return 0; }
    uint32_t get_size() override { return size; }
    bool get_readonly() override { return false; }
    bool fits(size_t pos, size_t n) const { return pos <= size && n <= size - pos; }
    esp_err_t read_raw(size_t pos, void *out, size_t n) override {
        if (!fits(pos, n)) return ESP_ERR_INVALID_SIZE;
        memcpy(out, bytes + pos, n); return ESP_OK;
    }
    esp_err_t read(size_t pos, void *out, size_t n) override { return read_raw(pos, out, n); }
    esp_err_t write_raw(size_t pos, const void *in, size_t n) override {
        if (!fits(pos, n)) return ESP_ERR_INVALID_SIZE;
        for (size_t i = 0; i < n; ++i) bytes[pos + i] &= static_cast<const uint8_t *>(in)[i];
        return ESP_OK;
    }
    esp_err_t write(size_t pos, const void *in, size_t n) override { return write_raw(pos, in, n); }
    esp_err_t erase_range(size_t pos, size_t n) override {
        if (!fits(pos, n)) return ESP_ERR_INVALID_SIZE;
        memset(bytes + pos, 0xff, n); return ESP_OK;
    }
    ~restore_nvs() override {
        if (bytes) {
            volatile uint8_t *p = bytes;
            for (size_t i = 0; i < size; ++i) p[i] = 0;
            free(bytes);
        }
    }
};

esp_err_t restore_nvs_clone(restore_nvs_t **out) {
    *out = nullptr;
#ifdef CONFIG_NVS_ENCRYPTION
    // 密文不能用普通 RAM 分区解释。/ Never interpret encrypted NVS entries as plaintext.
    return ESP_ERR_NOT_SUPPORTED;
#endif
    auto part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (!part) return ESP_ERR_NVS_PART_NOT_FOUND;
    auto image = new (std::nothrow) restore_nvs;
    if (!image) return ESP_ERR_NO_MEM;
    image->size = part->size;
    image->bytes = static_cast<uint8_t *>(heap_caps_malloc(part->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    esp_err_t err = ESP_ERR_NO_MEM;
    if (image->bytes) {
        // 不与其他任务的 NVS 写操作交错取样。/ Take the snapshot between SDK operations, not in the middle of one.
        nvs::Lock lock;
        err = esp_partition_read(part, 0, image->bytes, part->size);
    }
    if (err == ESP_OK) err = image->storage.init(0, part->size / 4096);
    if (err == ESP_OK) *out = image;
    else delete image;
    return err;
}
void restore_nvs_free(restore_nvs_t *image) { delete image; }
esp_err_t restore_nvs_write(restore_nvs_t *image, const char *ns, const char *key, nvs_type_t type, const void *data, size_t len) {
    uint8_t id;
    auto err = image->storage.createOrOpenNamespace(ns, true, id);
    auto t = type == NVS_TYPE_BLOB ? nvs::ItemType::BLOB : static_cast<nvs::ItemType>(type);
    if (err == ESP_OK) err = image->storage.writeItem(id, t, key, data, len, false);
    // 对固定分区过大的单个 blob 同样属于容量问题。/ A blob that exceeds partition geometry is also a capacity rejection.
    return err == ESP_ERR_NVS_VALUE_TOO_LONG ? ESP_ERR_NVS_NOT_ENOUGH_SPACE : err;
}
esp_err_t restore_nvs_erase(restore_nvs_t *image, const char *ns, const char *key) {
    uint8_t id;
    auto err = image->storage.createOrOpenNamespace(ns, false, id);
    if (err == ESP_OK) err = key ? image->storage.eraseItem(id, key, false) : image->storage.eraseNamespace(id, false);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}
