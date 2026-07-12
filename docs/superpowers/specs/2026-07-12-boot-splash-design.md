# Boot splash sequence — design

**Date:** 2026-07-12
**Status:** approved (MP3 audio approach)

## Goal

Add a boot splash sequence that plays before the title screen:

1. **libdragon logo** — drop / spin / fade in, with a roar (ported from VoidStrider64).
2. **Cydonis logo** — travelling-sine column ripple, with the Cydonis jingle (ported).
3. **Key art** (`splash-screen-3.png`) — new; fade in + gentle scale settle. **Title music
   starts as this comes up.**
4. Final fade from the key art into the live title scene.

Skippable at any point with Start / A / B.

## Audio approach (decided)

Crux64 has **no mixer** — the synth and MP3 music share the raw `audio_write` path
(`src/audio/synth.c`), and `src/audio/music.c` streams **one** MP3 track at a time. VoidStrider
played the splash roar/jingle as `wav64` clips on mixer channels; porting that literally would add
an audio backend the game doesn't otherwise use and risks coexistence bugs with the synth's raw
`audio_write`.

Instead we route splash audio through the **existing MP3 music path**:

- Pre-mix the roar + jingle into one `assets/splash.mp3` (roar 0–2.8 s, ~0.4 s gap, jingle
  3.2–16.4 s), tuned so the roar sits under the libdragon logo and the jingle under Cydonis.
- Add a `MUSIC_SPLASH` track to `music.[ch]`.
- `main.c` plays `MUSIC_SPLASH` at boot; when the key art comes up it calls
  `music_play(MUSIC_TITLE)` (which stops the splash track) so the title loop is already going when
  the title screen appears.

This keeps the sanctioned "minimp3 is the only authored audio" posture from the GDD.

## Module

`src/render/splash.c` + `splash.h`, ported from VoidStrider and adapted:

- Owns its own timeline (seconds). Fullscreen for libdragon + Cydonis + key art; the final fade is
  an overlay drawn over the title scene.
- API: `splash_init()`, `splash_update(dt)`, `splash_skip()`, `splash_fullscreen()`,
  `splash_finished()`, `splash_render(surface_t*)`, `splash_draw_overlay()`, and a new
  `splash_title_music_due()` so `main.c` knows when to swap in the title track.
- Cydonis keeps its CPU sine-pass with the double-buffer + `rspq_syncpoint` discipline. Key art
  needs no wave buffer — it blits with an animated `scale`/`prim alpha` (fade + gentle scale).
- Drops the VoidStrider `wav64`/`mixer` calls (audio now lives in `music.c`).

## Assets (existing Makefile rules cover all of them)

- `assets/ld_logo.png`, `assets/cydonis.png` — copied from VoidStrider (320×180 / 320×173).
  → `filesystem/*.sprite` via the generic `assets/%.png` rule.
- `assets/keyart.png` — `splash-screen-3.png` cover-cropped to 320×240
  (`magick splash-screen-3.png -resize 320x240^ -gravity center -extent 320x240`).
- `assets/splash.mp3` — `ffmpeg` concat of VoidStrider's `dragon.wav` + `jingle.wav`.
  → copied into `filesystem/` via the generic `assets/%.mp3` rule.

Sprites exceed TMEM (320 px wide) but `rdpq_sprite_blit` auto-tiles — VoidStrider relies on this.

## Timeline (seconds, tuned to the 13.2 s jingle)

| phase | window | notes |
|-------|--------|-------|
| libdragon drop/spin/fade in | 0.0 – 2.2 | roar under it |
| libdragon hold | 2.2 – 3.2 | |
| crossfade → Cydonis | 3.2 – 4.6 | jingle begins |
| Cydonis hold (ripple) | 4.6 – 16.4 | jingle plays out |
| crossfade → key art | 16.4 – 17.9 | **title music starts** |
| key art hold (fade + gentle scale) | 17.9 – 20.9 | |
| fade key art → title scene (overlay) | 20.9 – 22.4 | title renders underneath |

`splash_fullscreen()` is true up to the final overlay window; `splash_finished()` at 22.4 (or on
skip).

## Integration

- **`main.c`**: new `in_splash` gate before `in_title`. Remove the boot `music_play(MUSIC_TITLE)`
  (splash starts `MUSIC_SPLASH`). Any button → `splash_skip()`. When `splash_title_music_due()`
  fires, `music_play(MUSIC_TITLE)`. When `!splash_fullscreen()`, fall through so the title frame
  renders and the overlay fades over it. `splash_init()` after `render_init()` (DFS mounted by
  `music_init`).
- **`render.c`**: one hook — call `splash_draw_overlay()` just before `rdpq_detach_show()` on the
  title frame (`hud->title`). Guarded no-op once the splash is done.
- **`Makefile`**: add `src/render/splash.o` to `OBJS`. No new asset rules.
- **`music.[ch]`**: `MUSIC_SPLASH` enum + `"/splash.mp3"` path.

## Risks

- Long (~22 s) boot sequence — mitigated by any-button skip.
- Track handoff: `music_play(MUSIC_TITLE)` mid-splash cleanly reopens the stream (proven path).
- Splash MP3 must not loop into the title window; it's stopped at the key-art handoff before EOF.
