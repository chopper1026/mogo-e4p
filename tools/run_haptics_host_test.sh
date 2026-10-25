#!/bin/sh
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 触摸震动驱动的宿主回归；无马达场景单独跑一次。
# Host regression for the touch-vibration driver; the no-motor path runs separately.
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -g \
  -Itools/haptics_host_stubs -Imain \
  tools/haptics_host_test.c main/haptics.c -o "$out/haptics"
"$out/haptics"
"$out/haptics" nomotor
