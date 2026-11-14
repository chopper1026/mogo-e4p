#!/usr/bin/env python3
"""Exercise real HTTPS endpoint selection and IDF URL parsing with deterministic DNS."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]


def build_objects(work, env, compiler, flags):
    idf = os.environ.get("IDF_PATH")
    description = ROOT / "build/project_description.json"
    if not idf and description.is_file():
        idf = json.loads(description.read_text()).get("idf_path")
    parser = Path(idf or Path.home() / "esp/esp-idf") / "components/http_parser"
    if not (parser / "http_parser.c").is_file():
        raise RuntimeError("Set IDF_PATH to ESP-IDF 6.1 for the product's real HTTP URL parser")
    includes = ["-Itools/network_http_stubs", "-Itools/ota_stubs", "-Imain/services", "-I" + str(parser)]
    objects = []
    for name, source, extra in (
        ("network_http", "main/services/network_http.c", [
            "-Dgetaddrinfo=network_http_test_getaddrinfo", "-Dfreeaddrinfo=network_http_test_freeaddrinfo"]),
        ("network_http_dns", "tools/network_http_test_dns.c", []),
        ("http_parser", str(parser / "http_parser.c"), []),
    ):
        output = work / (name + ".o")
        subprocess.run(compiler + flags + includes + extra + ["-c", source, "-o", str(output)],
                       cwd=ROOT, env=env, check=True)
        objects.append(str(output))
    return objects


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--object-dir", type=Path)
    args.add_argument("--no-sanitize", action="store_true")
    args = args.parse_args()
    with tempfile.TemporaryDirectory(prefix="network-http-host-") as temporary:
        work = args.object_dir or Path(temporary)
        work.mkdir(parents=True, exist_ok=True)
        env, compiler = toolchain(work)
        flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g"]
        if not args.no_sanitize:
            flags.append("-fsanitize=address,undefined")
        objects = build_objects(work, env, compiler, flags)
        if args.object_dir:
            return
        binary = work / "test"
        subprocess.run(compiler + flags + ["-Itools/ota_stubs", "-Imain/services",
                       "tools/network_http_host_test.c"] + objects + ["-o", str(binary)],
                       cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    main()
