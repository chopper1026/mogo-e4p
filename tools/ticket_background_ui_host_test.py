#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：票根选项共用同页选择器回归。/ English: Ticket choices share the inline-selector regression."""
import re
def branch(source, condition):
    at = source.index(condition)
    begin = source.index("{", at)
    scan = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', lambda m: " " * len(m[0]), source)
    end, depth = begin + 1, 1
    while depth:
        depth += (scan[end] == "{") - (scan[end] == "}")
        end += 1
    return source[at:end]


if __name__ == "__main__":
    from lock_style_ui_host_test import main
    main()
