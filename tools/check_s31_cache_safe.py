#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：对最终 S31 ELF 检查缓存关闭时的 FreeRTOS 汇编入口依赖。
English: Check FreeRTOS assembly-entry dependencies in the final S31 ELF for cache-off safety.
"""
import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--elf", required=True)
    args = parser.parse_args()
    table = subprocess.check_output([args.objdump, "-t", args.elf], text=True)
    required = {
        "pxPortGetCoprocArea", "pxPortUpdateCoprocOwner",
        "vPortTaskPinToCore", "vPortTaskUsedPIEOnCPU0",
    }
    found = {}
    for line in table.splitlines():
        fields = line.split()
        if len(fields) >= 5 and fields[-1] in required:
            found[fields[-1]] = (int(fields[0], 16), fields[-3])
    errors = []
    for name in sorted(required):
        placement = found.get(name)
        if not placement:
            errors.append(f"{name}: missing; review the S31 FreeRTOS port after SDK changes")
        elif placement[1] != ".iram0.text" or not 0x2F000000 <= placement[0] < 0x2F080000:
            errors.append(f"{name}: cache-off call target is at {placement[0]:#x} in {placement[1]}")
    if errors:
        parser.exit(1, "S31 cache safety check failed:\n" + "\n".join(errors) + "\n")
    print("S31 cache safety: all four FreeRTOS coprocessor entry targets are in internal IRAM")


if __name__ == "__main__":
    main()
