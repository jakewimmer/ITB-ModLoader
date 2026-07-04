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

# x86-64 register aliases -> their 64-bit base, so `%esi`, `%sil`, `%si` and
# `%rsi` all track as one value through width changes.
_REG_BASE: dict[str, str] = {}
for _base, _aliases in {
    "rax": ("eax", "ax", "al", "ah"),
    "rbx": ("ebx", "bx", "bl", "bh"),
    "rcx": ("ecx", "cx", "cl", "ch"),
    "rdx": ("edx", "dx", "dl", "dh"),
    "rsi": ("esi", "si", "sil"),
    "rdi": ("edi", "di", "dil"),
    "rbp": ("ebp", "bp", "bpl"),
    "rsp": ("esp", "sp", "spl"),
    **{f"r{n}": (f"r{n}d", f"r{n}w", f"r{n}b") for n in range(8, 16)},
}.items():
    _REG_BASE[_base] = _base
    for _a in _aliases:
        _REG_BASE[_a] = _base


def _base(reg: str) -> str:
    """Canonical 64-bit name for a register alias (esi/sil/si -> rsi)."""
    return _REG_BASE.get(reg, reg)

# Runtime-derived offsets (from the in-game scanner) used to sanity-check the
# static extraction. Class::Field -> offset.
# Only fields with a clean, single-store setter belong here -- these are what the
# static method can derive and validate. Fields set via computed logic or with no
# setter at all (e.g. Pawn::Teleporter has no Set* symbol) are out of scope for
# this tool and must come from another source.
_RUNTIME_CHECK: dict[str, int] = {
    "Pawn::Powered": 0x10D4,
    "Pawn::Team": 0xD0,
    "Pawn::Corpse": 0xFB0,
    "Pawn::Minor": 0x10F0,
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

    Status setters have side effects -- Board/BoardSpace freeze/acid/fire setters
    read *sibling* status fields (freezing clears acid, etc.), so the first
    `cmpb OFF(%rdi)` is often a different field. The reliable signal is the offset
    the setter both *reads* and *writes with its own argument*: e.g. SetAcid does
    `cmpb $0x0,0x296c(%rdi)` (read) and `mov %sil,0x296c(%rdi)` (write) -- 0x296c
    is Acid, while 0x29a9 (written but never read) is a dirty flag.

    Tracks `this` (from %rdi) and the argument (from the %rsi family) through
    register copies with width-aware aliasing, then prefers the read-and-written
    offset, falling back to the first argument store, then the first read.
    """
    this_regs = {"rdi"}
    arg_regs = {"rsi"}
    read_offsets: set[int] = set()
    arg_writes: list[int] = []
    for insn in _disassemble(binary, addr):
        if insn.startswith("ret"):
            break
        # register-to-register copy: propagate this / arg through the alias.
        m = re.match(r"mov\s+%(\w+),%(\w+)$", insn)
        if m:
            src, dst = _base(m.group(1)), _base(m.group(2))
            if src in this_regs:
                this_regs.add(dst)
            elif src in arg_regs:
                arg_regs.add(dst)
            elif dst in arg_regs:
                arg_regs.discard(dst)  # arg reg overwritten by something else
            continue
        # read of a field: cmp/mov FROM OFF(%this)
        m = re.match(r"(?:cmp|mov)\w*\s+\$?[^,]*,?\s*(0x[0-9a-f]+)\(%(\w+)\)", insn)
        if m and _base(m.group(2)) in this_regs and insn.startswith("cmp"):
            read_offsets.add(int(m.group(1), 16))
            continue
        m = re.match(r"mov\w*\s+(0x[0-9a-f]+)\(%(\w+)\),%\w+", insn)
        if m and _base(m.group(2)) in this_regs:
            read_offsets.add(int(m.group(1), 16))
            continue
        # store OF THE ARGUMENT into this: mov %<arg>,OFF(%<this>)
        m = re.match(r"mov\w*\s+%(\w+),(0x[0-9a-f]+)\(%(\w+)\)", insn)
        if m and _base(m.group(1)) in arg_regs and _base(m.group(3)) in this_regs:
            arg_writes.append(int(m.group(2), 16))
    # Prefer the offset the setter both reads and writes with its argument (the
    # canonical field). Else a unique argument store. Never fall back to a
    # read-only offset: for side-effecting setters that is a *sibling* field, and
    # a confidently-wrong memory offset is worse than an honest "unresolved".
    both = [w for w in arg_writes if w in read_offsets]
    if both:
        return both[0]
    if len(set(arg_writes)) == 1:
        return arg_writes[0]
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
