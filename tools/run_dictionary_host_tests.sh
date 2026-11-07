#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：真实字典、选区与渲染观察回归，启用 ASan/UBSan。/ English: Actual dictionary, selection and observed renderer checks with sanitizers.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/dictionary-tests
flags=(-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itools/book_layout_stubs -Itools/book_source_host_stubs -Imain/book)
cc "${flags[@]}" tools/reader_selection_host_test.c main/book/reader_selection.c -o build/dictionary-tests/selection
cc "${flags[@]}" tools/dictionary_host_test.c main/book/book_dictionary.c main/book/reader_selection.c -lz -o build/dictionary-tests/dictionary
build/dictionary-tests/selection
python3 tools/reader_dictionary_host_test.py
python3 tools/reader_dictionary_build_host_test.py
python3 tools/reader_dictionary_worker_host_test.py
python3 tools/reader_dictionary_ui_host_test.py
python3 tools/reader_dictionary_view_host_test.py
python3 tools/ui_refresh_feedback_host_test.py
python3 tools/ttf_glyph_positions_host_test.py
