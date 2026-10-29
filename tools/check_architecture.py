#!/usr/bin/env python3
"""中文：检查当前产品的目录依赖、源文件登记和宿主回归入口。
English: Check product directory dependencies, source registration and host-suite entry points.
"""
import argparse
import ast
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def check(root):
    root = root.resolve()
    main = root / "main"
    errors = []
    cmake = (main / "CMakeLists.txt").read_text()
    registered = set(re.findall(r'"([^"\n]+\.c)"', cmake))
    sources = {path.relative_to(main).as_posix() for path in main.rglob("*.c")}
    for name in sorted(sources - registered):
        errors.append(f"main/{name}: source is absent from main/CMakeLists.txt")
    for name in sorted(registered - sources):
        errors.append(f"main/CMakeLists.txt: missing source {name}")
    if re.search(r"(?<![\w])INCLUDE_DIRS\b", cmake):
        errors.append("main/CMakeLists.txt: main headers must use PRIV_INCLUDE_DIRS")

    include_dirs = [main] + [path for path in main.iterdir() if path.is_dir()]
    headers = {}
    for path in main.rglob("*.h"):
        if path.name in headers:
            errors.append(f"main: ambiguous header name {path.name}")
        headers[path.name] = path

    paths = sorted(main.rglob("*.c")) + sorted(main.rglob("*.h"))
    components = root / "components"
    paths += sorted(components.rglob("*.c")) + sorted(components.rglob("*.h"))
    for path in paths:
        relative = path.relative_to(root).as_posix()
        # 保留行号；注释内的示例不属于实际依赖。/ Keep line numbers; examples inside comments are not dependencies.
        source = re.sub(r"/\*[\s\S]*?\*/|//[^\n]*", lambda match: "\n" * match[0].count("\n"),
                        path.read_text(errors="replace"))
        if relative.startswith("main/") and relative != "main/book/book_shelf_category_state.c":
            # 上下文可以由页面静态持有，内部字段只能由所属模块访问。/ Pages may store contexts, but only their owner may access internals.
            contexts = re.findall(r"\bbook_shelf_categories_t\s+\*?\s*(\w+)", source)
            for context in contexts:
                if re.search(r"\b" + re.escape(context) + r"\s*(?:\.|->)", source):
                    errors.append(f"{relative}: shelf category state must be accessed through its API")
        if relative.startswith("main/") and relative != "main/book/book_shelf_covers.c":
            contexts = re.findall(r"\bbook_shelf_covers_t\s+\*?\s*(\w+)", source)
            for context in contexts:
                if re.search(r"\b" + re.escape(context) + r"\s*(?:\.|->)", source):
                    errors.append(f"{relative}: shelf cover state must be accessed through its API")
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', source, re.M):
            name = match[1]
            candidates = [path.parent / name]
            if relative.startswith("components/"):
                component = components / path.relative_to(components).parts[0]
                candidates += [component / "include" / name, component / "src" / name]
            candidates += [directory / name for directory in include_dirs]
            target = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
            if target is None or not target.is_relative_to(main):
                continue
            target_name = target.relative_to(main).as_posix()
            target_group = target_name.split("/")[0] if "/" in target_name else "."
            location = f"{relative}:{source.count(chr(10), 0, match.start()) + 1}"
            reason = None
            if relative.startswith("components/"):
                reason = "components must not depend on main"
            elif relative.startswith("main/app/") and target_group == "apps":
                reason = "app coordinators must depend on contracts/services, not page headers"
            elif relative.startswith("main/image/") and target_group in {"app", "apps", "book", "ui", "font", "services"}:
                reason = "generic image code must not depend on application or book layers"
            elif relative.startswith("main/font/") and target_group in {"app", "apps", "services"}:
                reason = "font engines must not depend on application policy"
            elif relative.startswith("main/book/") and target_group == "apps":
                reason = "book modules must not depend on page headers"
            elif relative in {"main/book/book_shelf_covers.c", "main/book/book_shelf_covers.h"} and target_group != "book":
                reason = "shelf cover state must not depend on UI or system policy"
            elif relative == "main/book/book_shelf_category_state.c" and target_group != "book":
                reason = "shelf category state must not depend on UI or system policy"
            if relative in {"main/ui/ui_reader_settings.c", "main/ui/ui_reader_settings.h"} and target_group != "ui":
                reason = "reader settings view must not depend on page state or system policy"
            if reason:
                errors.append(f"{location}: {reason} ({target_name})")

    runner = root / "tools/run_reader_baseline.py"
    module = ast.parse(runner.read_text())
    suites = next((node.value for node in module.body if isinstance(node, ast.Assign)
                   and any(isinstance(target, ast.Name) and target.id == "SUITES" for target in node.targets)), None)
    if not isinstance(suites, ast.Dict) or not suites.keys:
        errors.append("tools/run_reader_baseline.py: missing suite registry")
    else:
        for name, command in zip(suites.keys, suites.values):
            entries = [node.value for node in ast.walk(command) if isinstance(node, ast.Constant)
                       and isinstance(node.value, str) and node.value.startswith("tools/")]
            if not entries:
                errors.append(f"suite {name.value}: missing tool entry")
            for entry in entries:
                if not (root / entry).is_file():
                    errors.append(f"suite {name.value}: missing {entry}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    errors = check(args.root)
    for error in errors:
        print(error)
    if errors:
        return 1
    print("architecture: PASS (dependencies, private headers, sources, suite entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
