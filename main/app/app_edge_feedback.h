/* 中文：已确认边缘返回的局部视觉回执。/ English: Local visual acknowledgement of a committed edge back. */
#pragma once
#include "app.h"

/// 在起手侧与高度呈现一次，恢复墨迹后立即交还导航；仅 UI 任务调用。
/// Pulse once at the starting side and height, returning the restored frame to navigation; UI task only.
void app_edge_feedback_show(app_ctx_t *ctx, bool right, int origin_y);
