# Crux64

A physics-based, procedurally generated 3D mountain climbing simulation for
the Nintendo 64. Control the individual limbs of a stick-figure climber to
scale a treacherous fixed-seed peak — every movement takes calculation,
balance, and stamina.

(c) 2026 Amanda Hariette-Scott and Cydonis Heavy Industries.

See [Crux64_GDD.md](Crux64_GDD.md) for the full design document.

## Hardware requirements

- **Expansion Pak (8MB)** — mandatory (heightmap arrays, display lists, audio buffers)
- **Rumble Pak** — mandatory (stamina/balance feedback)
- Standard N64 controller

## Controls (GDD 2.4)

| Input | Action |
|---|---|
| C-Up / C-Left / C-Right / C-Down | Select right arm / left arm / right leg / left leg |
| Analog stick | Move selected limb (release C to snap to grip) |
| Z | Place piton / clip in |
| R | Shake out / rest selected limb |
| A | Apply chalk |
| D-Pad | Free-look camera |

## Building

Requires Docker (the libdragon toolchain runs in a container) and the
pinned `libdragon` + `tiny3d` submodules:

```sh
git submodule update --init
bash build.sh        # may need sudo if your user isn't in the docker group
```

Produces `crux64.z64`. Run it in [ares](https://ares-emu.net/) or on real
hardware via a flashcart.

## Status

- [x] Phase 1 — core setup, joypad subsystem, Rumble Pak mapping, Tiny3D bring-up
- [x] Phase 2 — fixed-seed Simplex noise mountain → Tiny3D mesh
- [ ] Phase 3 — stick-figure IK + limb snapping
- [ ] Phase 4 — posture, balance, stamina
- [ ] Phase 5 — audio (minimp3 music + procedural DSP SFX)
- [ ] Phase 6 — EEPROM saves, polish, optimization
