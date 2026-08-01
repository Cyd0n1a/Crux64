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

## Cache maintenance

- NEVER call `data_cache_hit_*` on an uncached pointer. `malloc_uncached()`
  and `surface_alloc()` (which uses `malloc_uncached_aligned`) return
  KSEG1 (`0xA0......`) addresses; writes through them go straight to RDRAM,
  so no flush is needed. A `CACHE` instruction on a KSEG1 address is
  *undefined* on the VR4300 — the hardware still derives an index from the
  address bits and acts on whatever line maps there, so it can write back
  or invalidate an unrelated dirty line and silently lose a write anywhere
  in RAM. This produced wandering rspq/RDP corruption that survived every
  feature-level bisect.
- Cache ops are only correct on genuinely cached memory the RSP/RDP DMAs
  into — e.g. `synth.c`'s static `rsp_noise_buffer`, which *must* be
  invalidated after the RSP writes it.
- ares reports these as `[unusual] [CPU] CACHE access to non-cacheable
  address ... at PC ...`; gopher64 does not. Soak under both.

## EEPROM

- Do NOT use eepromfs. `eepfs_init` identifies a filesystem by CRCing the raw
  `eepfs_entry_t` array, and that struct stores `path` as a *pointer*
  (`eepromfs.c:280`, `eepromfs.h:59`). The signature therefore depends on where
  the linker puts the path literal, so any change to a translation unit linked
  before `save.o` moves it, `eepfs_verify_signature()` fails, and `eepfs_wipe()`
  destroys the player's record with no error path. Confirmed twice: a `volatile`
  pad in `input.c` moved the pointer `0x800799f0` → `0x80079a10`, and four real
  carts carried four different signatures for one unchanged file table.
- The container in `src/meta/save_format.c` replaces it: block 0 is a
  `'C','R','X'` + version + payload-CRC header, block 1 the payload. Identity is
  a literal value, so relinking cannot invalidate it. Pre-fix carts are adopted
  by matching only bytes 0–5 of the old signature (`65 65 70 01 00 08`) — bytes
  6–7 are the build-dependent checksum and must never be compared.
- This is invisible under gopher64, which names saves `CRUX64-<uppercase sha256
  of the ROM>.eep` and so hands every rebuild a blank EEPROM. It only bites on
  hardware, where one cart is reflashed repeatedly. To test save changes, copy
  the `.eep` onto the new ROM's filename:
  `cp old.eep ~/.local/share/gopher64/saves/CRUX64-$(sha256sum crux64.z64 | awk '{print toupper($1)}').eep`
- gopher64 only rewrites the `.eep` when the game actually wrote to EEPROM, so
  an unchanged mtime after a run means "no save occurred", not "run failed".
- `eeprom_write` asserts on a nonzero status byte (`eeprom.c:80`) rather than
  returning it, so a write failure halts the ROM. Budget writes accordingly:
  each block costs ~6 ms and blocks the CPU.

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
- Save: EEPROM 4k via libdragon's low-level `eeprom_read`/`eeprom_write`
  (GDD 3.4 `save_data_t`, unchanged). NOT eepromfs — see the EEPROM section.
