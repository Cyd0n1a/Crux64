# Crux64 — notes for Claude

Physics-based procedural mountain climbing sim for N64. Design doc:
`Crux64_GDD.md` (authoritative — follow its phase roadmap in section 5).

## Build

- `bash build.sh` — Docker wrapper (image `ghcr.io/dragonminded/libdragon:latest`).
  It re-applies `patches/libdragon/` over the submodule, installs libdragon +
  tools in-container, builds tiny3d, then runs `make`. Output: `crux64.z64`,
  full log in `build.log`.
- Submodules are pinned: libdragon `07f1977bb` (preview branch), tiny3d
  `7f5773f64`. Do not bump them casually — the patches/ backports
  (fgeom.h, rspq_profile.h, fgeom.c) exist because tiny3d needs files this
  libdragon SHA lacks.
- LSP errors about missing `libdragon.h`/`t3d.h` headers are expected on the
  host; everything compiles inside Docker.
- n64.mk gotchas: OBJS must be prerequisites of the `.elf` target; object
  paths mirror source paths (`src/foo.c` → `build/src/foo.o`); the Makefile
  must keep `-include $(OBJS:.o=.d)` or header edits don't rebuild.

## Tiny3D gotchas (hard-won, from sibling projects)

- Frame pattern: `rdpq_attach` → `t3d_frame_start` → `t3d_viewport_attach` →
  clears → draws → `rdpq_detach_show`. There is no `t3d_frame_end`.
- Double-buffering dynamic vert/matrix buffers is NOT enough with 3
  framebuffers — the CPU runs up to 2 frames ahead. Guard rewrites with
  `rspq_syncpoint_new()` after submit / `rspq_syncpoint_wait()` before reuse.
- `T3DVertPacked` normals: single packed `uint16_t` (5,6,5) — use
  `t3d_vert_pack_normal()`, never separate bytes.
- `^` and `$` are rdpq_text escape chars — bare ones assert at draw time;
  write `^^` / `$$` for literals.
- `t3d_vert_load(verts, offset, count)`: `offset` is a slot in the 70-entry
  RSP vertex cache (0–68), NOT an index into your buffer. Passing a buffer
  index makes the RSP write transformed verts past the cache into arbitrary
  DMEM → trashed rspq state → "wait loop timed out" crashes. Batch ≤70 verts,
  load at offset 0, advance the source pointer (pair-aligned), 0-based tri
  indices.
- `rspq_write(id, cmd, arg0, ...)`: the top byte of the first word belongs to
  rspq (overlay|command). arg0 must fit in 24 bits, and overlay ucode must
  mask the command byte off a0 (`andi`, not `srl 16`). Custom overlays: no
  stack — `sp` is just a zeroed GPR; save `ra` in a spare register (only `gp`
  is reserved) before `jal DMAOut`.

## Project conventions

- Layout mirrors VoidStrider64: `src/input/`, later `src/gen/`, `src/sim/`,
  `src/render/`, `src/audio/`, `src/meta/`.
- GDD constraints: procedural everything (fixed seed `0x63727578`), minimal
  authored assets; the planned exception is minimp3 background music.
  Expansion Pak + Rumble Pak are mandatory hardware.
- Save: EEPROM 4k via eepromfs (GDD 3.4 `save_data_t`).
