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

## Status / remaining work to produce a complete table

Prototype: extracts field offsets for `Set<Field>` methods and cross-checks
against runtime values. Still to do before it can emit a full, trustworthy
`__addresses_linux.lua`:

1. **Refine the bool-setter heuristic.** A few bool fields (Acid/Frozen/Lava)
   currently resolve to a shared offset because the leading `cmpb $0x0,OFF(%rdi)`
   is a common status compare; prefer the actual argument store, and for status
   bits also recover the bit index (these are likely bitflags in one byte).
2. **Map game field names to memedit field names.** Most are 1:1 (Fire, Acid,
   Terrain, Powered, Team, ...); a few differ (BonusMove <-> BonusShift) and some
   memedit fields are read via non-`Set` accessors or are computed.
3. **Struct sizes** (`size_pawn`, `size_tile`, `size_weapon`, `size_board`,
   `size_space_damage`): read the `operator new` size in each class's allocating
   constructor, or the array stride.
4. **Tile addressing** (`delta_rows`, `step_rows`, `size_tile`): disassemble
   `Board::GetTerrain(Point)` / `Board::SetTerrain(Point,int)` to see how the game
   computes a `BoardSpace*` from `(x,y)` -- that gives the board->tile layout the
   runtime scanner mis-derived.
5. **Datatype + access tuple**: memedit stores `{offset, access, datatype}` per
   field; datatype can be inferred from the setter's argument type (bool/int/
   float/std::string/const char*) and access from whether get/set exist.
6. Emit `__addresses_linux.lua` keyed by game version (`modApi.gameVersion`,
   currently 1.2.93).

This path needs **no interactive game session** and is the recommended way to
complete Linux memedit calibration.
