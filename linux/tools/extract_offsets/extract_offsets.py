#!/usr/bin/env python3
"""Extract struct field offsets from Into the Breach's accessor functions.

A fully static alternative to memedit's runtime memory scanner. The native Linux
Breach binary is unstripped and non-PIE, and its setter methods store their
argument straight into the object: e.g. Pawn::SetPowered(bool) compiles to

    cmpb $0x0, 0x10d4(%rdi)      ; the Powered field
    ...
    mov  %sil, 0x10d4(%rdi)      ; store the bool argument

so the field offset (0x10d4) is readable directly from the disassembly -- no
running game, no in-mission calibration, no 64-bit search fragility.

Usage:
    uv run extract_offsets.py "/path/to/Breach" [--class Pawn|BoardSpace|Skill|...]

Prints `Class::Field = 0xNNN` lines for every setter whose offset it can recover,
and (with --validate) cross-checks a set of runtime-derived offsets.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys

# Registers that carry the first real argument (arg after the implicit `this`),
# across widths, plus common callee-saved regs an argument gets copied into.
_ARG_SEED = {"esi", "sil", "si", "rsi"}

# Runtime-derived offsets (from the in-game scanner) used to sanity-check the
# static extraction. Class::Field -> offset.
_RUNTIME_CHECK: dict[str, int] = {
    "Pawn::Powered": 0x10D4,
    "Pawn::Team": 0xD0,
    "Pawn::Teleporter": 0x1321,
}


def demangled_symbols(binary: str) -> list[tuple[int, str]]:
    """Return (address, demangled_name) for every defined text symbol."""
    nm = subprocess.run(
        ["nm", "--defined-only", binary], capture_output=True, text=True, check=True
    ).stdout
    filt = subprocess.run(
        ["c++filt"], input=nm, capture_output=True, text=True, check=True
    ).stdout
    out: list[tuple[int, str]] = []
    for line in filt.splitlines():
        m = re.match(r"([0-9a-fA-F]+)\s+[Tt]\s+(.+)", line)
        if m:
            out.append((int(m.group(1), 16), m.group(2)))
    return out


def _disassemble(binary: str, addr: int, length: int = 0x120) -> list[str]:
    out = subprocess.run(
        [
            "objdump",
            "-d",
            f"--start-address={addr}",
            f"--stop-address={addr + length}",
            binary,
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    insns = []
    for line in out.splitlines():
        m = re.search(r":\t[0-9a-f ]+\t(.+)$", line)
        if m:
            insns.append(m.group(1).strip())
    return insns


def extract_setter_offset(binary: str, addr: int) -> int | None:
    """Recover the field offset a setter writes to.

    Tracks the `this` pointer (starts in %rdi) and the argument (starts in the
    %rsi family) through simple register copies, then returns the offset of the
    first store of the argument into `this`, or the first `cmpb $0x0,OFF(%rdi)`
    (bool setters compare the current value first).
    """
    this_regs = {"rdi", "edi"}
    arg_regs = set(_ARG_SEED)
    for insn in _disassemble(binary, addr):
        if insn.startswith("ret"):
            break
        # this-pointer copy: mov %rdi,%rXX
        m = re.match(r"mov\s+%(rdi|edi),%(\w+)", insn)
        if m:
            this_regs.add(m.group(2))
            continue
        # argument copy: mov %<arg>,%rXX
        m = re.match(r"mov\s+%(\w+),%(\w+)", insn)
        if m and m.group(1) in arg_regs:
            arg_regs.add(m.group(2))
            continue
        # store the argument into this: mov %<arg>,0xNN(%<this>)
        m = re.match(r"mov\s+%(\w+),(0x[0-9a-f]+)\((?:%(\w+))\)", insn)
        if m and m.group(1) in arg_regs and m.group(3) in this_regs:
            return int(m.group(2), 16)
        # bool setter's leading compare of the current value
        m = re.match(r"cmpb?\s+\$0x0,(0x[0-9a-f]+)\(%(?:rdi|edi)\)", insn)
        if m:
            return int(m.group(1), 16)
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binary")
    ap.add_argument("--class", dest="klass", default=None,
                    help="restrict to one class (Pawn, BoardSpace, Skill, ...)")
    ap.add_argument("--validate", action="store_true",
                    help="cross-check against runtime-derived offsets")
    args = ap.parse_args()

    syms = demangled_symbols(args.binary)
    setter = re.compile(r"^(\w+)::Set(\w+)\(")

    results: dict[str, int] = {}
    for addr, name in syms:
        m = setter.match(name)
        if not m:
            continue
        klass, field = m.group(1), m.group(2)
        if args.klass and klass != args.klass:
            continue
        off = extract_setter_offset(args.binary, addr)
        if off is not None:
            results[f"{klass}::{field}"] = off

    for key in sorted(results):
        print(f"{key} = 0x{results[key]:x}")

    if args.validate:
        print("\n-- validation vs runtime --", file=sys.stderr)
        ok = True
        for key, expected in _RUNTIME_CHECK.items():
            got = results.get(key)
            status = "OK" if got == expected else "MISMATCH"
            if got != expected:
                ok = False
            print(f"  {key}: static={got and hex(got)} runtime={hex(expected)} [{status}]",
                  file=sys.stderr)
        print(f"validation {'passed' if ok else 'FAILED'}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
