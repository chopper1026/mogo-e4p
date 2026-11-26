idf_component_register(SRCS "restore_nvs.cpp" INCLUDE_DIRS "include" REQUIRES nvs_flash PRIV_REQUIRES esp_partition)
# 仅此适配层使用固定 IDF 6.1 的页分配器。/ Only this adapter uses the pinned IDF 6.1 page allocator.
idf_component_get_property(nvs_dir nvs_flash COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${nvs_dir}/src" "${nvs_dir}/private_include")
