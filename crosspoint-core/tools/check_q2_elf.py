#!/usr/bin/env python3
"""Fail closed before shipping a CrossPod ELF with the wrong Shanling Q2 ABI.

Inspect the *installed* stripped engine, not just the intermediate object.
Reference: DiamondBond/q2-librespot/contrib/shanlingq2/build.sh (X2000:
MIPS32r2, O32, FP64, NaN-2008, abs2008).
"""
from __future__ import annotations
import re
import subprocess
import sys
from pathlib import Path

def read(tool: str, *flags: str) -> str:
    return subprocess.check_output(
        [tool, *flags, str(path)], text=True, stderr=subprocess.STDOUT
    )

if len(sys.argv) != 4:
    sys.exit("usage: check_q2_elf.py ELF READELF OBJDUMP")
path = Path(sys.argv[1])
readelf, objdump = sys.argv[2:]
if not path.is_file() or not path.stat().st_size:
    sys.exit("ERROR: Q2 ELF missing or empty")
header = read(readelf, "-h")
programs = read(readelf, "-l")
attrs = read(readelf, "-A")
sections = read(readelf, "-S")
private = read(objdump, "-p")
interpreter = re.search(r"Requesting program interpreter:\s*([^]\n]+)", programs)
dynamic_loader = interpreter.group(1).strip() if interpreter else None
if dynamic_loader:
    loader_ok = dynamic_loader == "/lib/ld-linux-mipsn8.so.1"
    # Q2's glibc is 2.28; newer symbols cannot resolve on the device.
    version_info = read(readelf, "--version-info")
    requested_versions = [
        (int(a), int(b)) for a, b in re.findall(r"GLIBC_(\\d+)\\.(\\d+)", version_info)
    ]
    glibc_ok = all(v <= (2, 28) for v in requested_versions)
else:
    loader_ok = True
    glibc_ok = True
checks = {
    "ELF32": "Class:" in header and bool(re.search(r"Class:\s+ELF32", header)),
    "MIPS little-endian": bool(re.search(r"Data:.*little endian", header)) and bool(re.search(r"Machine:.*MIPS",header)),
    "O32 ABI": bool(re.search(r"Flags:.*o32", header, re.IGNORECASE)),
    "MIPS32r2": bool(re.search(r"(?:ISA:\s+MIPS32r2|mips32r2)", attrs + "\n" + header, re.IGNORECASE)),
    "NaN-2008": "nan2008" in header.lower() or "nan2008" in attrs.lower(),
    "FP64 hard-float": bool(re.search(r"FP ABI:\s+Hard float.*64-bit FPU", attrs, re.IGNORECASE)),
    "Q2 loader or static executable": loader_ok,
    "glibc symbols no newer than 2.28": glibc_ok,
    "static binary has no DT_NEEDED": dynamic_loader is not None or "NEEDED" not in private,
    "ELF entry point": bool(re.search(r"Entry point address:\s+0x[1-9a-fA-F]", header)),
}
for name, ok in checks.items():
    print(f'{"PASS" if ok else "FAIL"}: {name}')
if not all(checks.values()):
    print("\nELF header:\n", header, "\nMIPS attributes:\n", attrs,
          "\nProgram headers:\n", programs, file=sys.stderr)
    sys.exit("ERROR: built ELF does not meet Shanling Q2 ABI requirements")
print(f"Runtime: {dynamic_loader or 'fully static'}")
print("Shanling Q2 ELF compatibility gate: PASS")
