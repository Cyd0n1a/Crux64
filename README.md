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

The title screen orbits the whole massif with the CRUX64 logo spelled
out in rainbow-rippling cubes — press **Start** to begin.

You start on foot at base camp — a tent and campfire pitched on the
apron below the first pitch. Walk in to the wall and grab on.

**On foot**

| Input | Action |
|---|---|
| Analog stick | Walk (camera-relative) |
| Z (hold) | Steer the camera with the stick instead |
| C (any, hold) | Grab the wall when facing holds within reach |
| D-Pad | Orbit camera |

**On the wall**

| Input | Action |
|---|---|
| C-Up / C-Left / C-Right / C-Down | Select right arm / left arm / right leg / left leg |
| Analog stick | Move selected limb (release C to snap to the green hold) |
| Z | Drive a piton — full stamina restore + rope checkpoint for falls |
| R (hold, limb selected) | Shake out that limb to recover its stamina |
| R (no limb selected) | Step off onto walkable ground (base, ledges, benches) |
| A | Chalk up (slower grip drain for the next 5 holds) |
| D-Pad | Orbit camera |

Anchored limbs tire — arms faster than legs, much faster when
overextended or with your weight hung outside your holds. A spent limb
peels off; lose the wall and you fall until the rope catches on your
last piton or you hit walkable ground. Watch the shakes and feel the
rumble build.

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
- [x] Phase 3 — stick-figure IK + limb snapping
      (hold a C button to steer that limb along the rock; release to
      catch the pulsing green hold; grips generate on all 45°+ faces)
- [x] Phase 3.5 — on-foot mode: spawn at a procedurally pitched base
      camp (tent, stone fire ring, animated campfire casting real
      point light), walk the aprons and benches, grab on and step off
      anywhere walkable meets climbable; title screen with a slow
      orbit of the massif and a waving rainbow cube-letter logo
- [x] Phase 4 — posture, balance, stamina (CoG support envelope,
      per-limb + core stamina, overextension, limb peel + falls,
      shake-out rests, chalk, pitons that catch falls, limb-shake
      visuals, progressive rumble, strain-driven camera zoom)
- [x] Phase 5 — procedural audio: real-time DSP synthesis (no sampled
      assets) — altitude-driven wind, a heartbeat that quickens with
      grip failure, exertion grunts, rock-placement thuds, a metallic
      piton strike, and an ambient drone bed standing in for the planned
      minimp3 track
- [x] Phase 6 — EEPROM save state: max altitude, lifetime falls and
      total play time persist across power-offs in a single 8-byte
      eepromfs block (recorded in RAM, flushed only at rest points); the
      title screen shows your saved best

### Polish

- [x] Music — composed MP3 loops (a title theme and an in-game theme)
      streamed off the cartridge and decoded live with minimp3, mixed
      under the procedural wind/heartbeat/SFX. The in-game track is
      larger than RAM, so it streams from the ROM filesystem; the drone
      bed steps aside whenever a track is playing
- [x] Terrain scatter — proctree-generated stunted conifers along the
      lower slopes (thinning to a treeline), plus procedural mossy rocks
      and the occasional boulder. Placed once from the fixed seed on
      gentle-enough ground, clear of base camp; drawn as bake-once
      instanced meshes with per-kind distance culling
