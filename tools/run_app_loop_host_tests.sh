#!/bin/sh
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
# 开启触摸诊断时也编译真实主循环，覆盖诊断包装与公共函数体。
# Compile the real loop with touch diagnostics enabled to cover its wrapper and shared body.
sed 's/#define CONFIG_TOUCH_DIAG 0/#define CONFIG_TOUCH_DIAG 1/' tools/app_loop_stubs/sdkconfig.h > "$out/sdkconfig.h"
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only \
  -I"$out" -Itools/app_loop_stubs -Imain -Icomponents/ble_page_turner/include -Icomponents/read_pico/include -Imain/app -Imain/ui -Itools/ui_gesture_stubs \
  main/app/app_loop.c
rm "$out/sdkconfig.h"
python3 tools/power_dialog_host_test.py --input-c "$out/power.c"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/app_loop_stubs -Imain -Icomponents/ble_page_turner/include -Icomponents/read_pico/include -Imain/app -Imain/ui -Itools/ui_gesture_stubs \
  "$out/power.c" tools/app_loop_host_test.c main/app/app_loop.c main/app/app_input.c main/app/app_media.c main/app/app_navigation.c main/app/app_quick_layer.c tools/app_loop_stubs/touch_sampler_stub.c main/ui/ui_gesture.c main/ui/ui_quick_menu_input.c -o "$out/test"
"$out/test"
# 保留字体回退调用及可切换 USB 所有权，避免主循环的内联桩屏蔽这些分支。
# Preserve font-fallback calls and configurable USB ownership instead of the loop's fixed inline stubs.
printf '%s\n' '#pragma once' '#include <stdbool.h>' 'void ui_text_set_system_font(bool);' > "$out/ui_kit.h"
printf '%s\n' '#pragma once' '#include <stdbool.h>' 'bool usb_storage_active(void);' > "$out/usb_storage.h"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$out" -Itools/app_loop_stubs -Imain -Imain/app -Imain/ui -Itools/ui_gesture_stubs \
  tools/app_media_host_test.c main/app/app_media.c -o "$out/media"
"$out/media"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/app_loop_stubs -Imain/app -Imain/ui -Itools/ui_gesture_stubs \
  tools/ui_quick_input_host_test.c main/ui/ui_quick_menu_input.c -o "$out/quick"
"$out/quick"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/app_loop_stubs -Imain -Icomponents/ble_page_turner/include -Imain/app -Imain/ui -Itools/ui_gesture_stubs \
  tools/ui_menu_host_test.c main/ui/ui_menu.c -o "$out/menu"
"$out/menu"
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/sd_probe_stubs -Icomponents/read_pico/include \
  tools/sd_probe_host_test.c -o "$out/sd_probe"
"$out/sd_probe"

python3 tools/lock_feedback_host_test.py

python3 tools/power_dialog_host_test.py
