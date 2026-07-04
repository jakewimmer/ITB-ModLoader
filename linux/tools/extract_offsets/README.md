# Static offset extraction for memedit (decompilation approach)

An alternative to memedit's runtime memory scanner for producing the Linux
offset table (`__addresses_linux.lua`). Instead of manipulating a live game and
scanning memory (which needs an in-mission session and is fragile on 64-bit),
this reads the offsets **statically** from the game binary's accessor functions.

## Why this works

The native Linux `Breach` binary is **unstripped** and **non-PIE** (ET_EXEC). Its
setter methods store their argument straight into the object, so the field offset
is a literal in the disassembly:

```
Pawn::SetPowered(bool):
    cmpb $0x0, 0x10d4(%rdi)   ; the Powered field
    mov  %sil, 0x10d4(%rdi)   ; store the bool argument
```

`0x10d4` is exactly `pawn.Powered`. No running game, no calibration, no 64-bit
pointer-search fragility.

## Class map (memedit term -> game class)

| memedit | game class    | notes |
|---------|---------------|-------|
| pawn    | `Pawn`        | 291 methods |
| tile    | `BoardSpace`  | 216 methods -- the runtime scanner could not derive these at all |
| weapon  | `Skill`       | weapons are `Skill`s in ITB |
| board   | `Board`       | container; tile addressing lives here (`Board::GetTerrain(Point)`) |
| spaceDamage | `SpaceDamage` | |

## Validation (vs the runtime scanner's derived offsets)

```
Pawn::Powered = 0x10d4   [matches runtime]
Pawn::Team    = 0xd0     [matches runtime]
Pawn::Corpse  = 0xfb0    [matches runtime]
Pawn::Minor   = 0x10f0   [matches runtime]
Pawn::BonusShift = 0xac4 [confirms the BonusMove offset the scanner could not converge on]
```

## Usage

```bash
uv run extract_offsets.py "/path/to/Into the Breach/Breach" --class Pawn --validate
uv run extract_offsets.py "/path/to/Into the Breach/Breach" --class BoardSpace
```

## What this reliably covers (and what it can't)

The setter approach is **safe by construction**: it only reports an offset when a
setter cleanly stores its argument (preferably to an offset it also reads back).
It never falls back to a guess -- a confidently-wrong memory offset is worse than
an honest "unresolved". Validated against runtime: Pawn Powered/Team/Corpse/Minor
match exactly.

It handles clean scalar setters well (most of `pawn`), but three categories are
**not** derivable this way, as the disassembly makes clear:

- **Fields with no `Set*` symbol.** e.g. `Pawn::Teleporter` has no setter; it is
  written via computed/loadout code. ~9 pawn fields and most `weapon` fields are
  like this. They need offsets from getters, constructors, or use-sites instead.
- **Side-effecting status bools.** `BoardSpace::SetFrozen` never stores its
  argument to a plain offset -- it reads sibling status (Acid `0x296c`, Terrain
  `0x2998`) and computes the frozen state elsewhere. And `SetAcid` writes both a
  shared state byte (`0x296c`, read+written) and an acid-specific byte (`0x29a9`,
  write-only); which one memedit wants needs in-game confirmation.
- **POD structs with no methods.** `spaceDamage` is a plain struct accessed by
  direct member reference; there are no setters to disassemble. Its offsets must
  come from the `SpaceDamage` constructor's member initialisation, and the
  `vital` struct sizes from each class's `operator new`.

## Remaining work to a complete, trustworthy `__addresses_linux.lua`

memedit's loader is **all-or-nothing**: it marks itself calibrated only if *every*
field in *every* category resolves (see `memedit.lua` `load()`). So the table must
be both complete and correct before it activates -- a validated subset does not
turn memedit on. To finish:

1. Map the resolved game names to memedit field names (mostly 1:1; `BonusMove` <->
   `BonusShift`; reuse `[2]` access and `[3]` datatype from the Windows
   `__addresses.lua` -- they are ABI-invariant, only the offset changes).
2. Recover the no-setter fields (weapon members, remaining pawn fields) from
   getters/use-sites, and the `spaceDamage`/`vital` values from constructors and
   `operator new`.
3. Disambiguate the shared-state status bools (Acid/Frozen/Fire/Shield) and
   validate every offset in a throwaway mission before trusting it -- wrong
   offsets corrupt live game state. This validation step needs the game running.

Static extraction gives trustworthy *candidates* with no interactive session; the
final disambiguation and safety validation still want one in-game pass.
