# Mogo 平台 OTA 发布

1. 在 mogo-platform 后端设置 `MOGO_INTEGRATION_FIRMWARE_TOKEN` 为至少 32 字符的随机密钥，并通过 HTTPS 部署；确认机器接口没有其他入口绕过认证。
2. 在本仓库 GitHub Actions 的变量中设置 `MOGO_PLATFORM_URL`（HTTPS 网站根地址），在 Secrets 中设置 `MOGO_FIRMWARE_SUBMISSION_TOKEN`（与网站相同）。限制有权创建或移动发布标签的人，避免不可信代码借发布工作流读取密钥。
3. 推送 `v1.2.3` 或 `v1.2.3-rc1` 标签。工作流以 ESP-IDF 6.1 的 ESP32-S31 产品配置构建；普通版本进入 stable，rc 版本进入 beta。设备默认读取 stable，beta 不会自动推送给默认设备。
4. 在网站后台检查版本、来源、文件、更新说明和最低基础版本后手动发布。第一次使用联网升级的设备可能需要先通过网页或 TF 卡安装包含升级入口和正确分区布局的引导版本。
5. 在真实设备上验证升级、断网恢复、数据保留后再宣布可用。更换密钥时同步更新网站和 GitHub Secret。不要把密钥提交到仓库。
