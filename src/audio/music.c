#include "music.h"
#include "minimp3.h"

#include <libdragon.h>
#include <string.h>

/* Output rate must match the synth's buffer (src/audio/synth.c). We downmix
 * whatever the file is (both tracks are 44.1kHz stereo) to this rate with a
 * fractional read cursor, so any source rate / channel count still plays. */
#define OUT_RATE   22050

/* Raw-MP3 input window streamed off the cartridge. A worst-case MP3 frame is
 * ~1441 bytes; 16K keeps several frames buffered so decode never starves
 * between refills. */
#define IN_CAP     (16 * 1024)
#define FRAME_MIN  2048    /* refill below this so a whole frame is always present */

static const char *const track_path[MUSIC_TRACK_COUNT] = {
    [MUSIC_NONE]  = NULL,
    [MUSIC_TITLE] = "/title-screen.mp3",
    [MUSIC_GAME]  = "/main-game-loop1.mp3",
};

static bool      avail;             /* DFS mounted: a track can be played */
static bool      playing;
static int       fh = -1;           /* current DragonFS handle, -1 = none */
static mp3dec_t  dec;

static uint8_t   in_buf[IN_CAP];
static int       in_size;           /* valid bytes in in_buf */
static int       in_pos;            /* bytes already consumed by the decoder */

static short     frame_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int       frame_len;         /* samples-per-channel in frame_pcm */
static int       frame_ch;
static float     ratio;             /* source Hz / OUT_RATE */
static float     src_pos;           /* fractional read cursor within the frame */
static float     fade;              /* 0..1 fade-in, re-armed on start + each loop */

/* Slide the unconsumed tail to the front and top the window back up from the
 * cartridge. Returns bytes newly read (0 at end-of-file). */
static int refill(void) {
    if (in_pos > 0) {
        memmove(in_buf, in_buf + in_pos, in_size - in_pos);
        in_size -= in_pos;
        in_pos   = 0;
    }
    if (fh < 0) return 0;
    int want = IN_CAP - in_size;
    if (want <= 0) return 0;
    int got = dfs_read(in_buf + in_size, 1, want, fh);
    if (got > 0) in_size += got;
    return got > 0 ? got : 0;
}

/* Seek back to the top of the current track and reset the decoder — the
 * looping seam. Re-arms the fade so the (very small) MP3 padding gap at the
 * join doesn't click. Returns false only if the stream is unusable. */
static bool loop_restart(void) {
    if (fh < 0) return false;
    if (dfs_seek(fh, 0, SEEK_SET) != DFS_ESUCCESS) return false;
    mp3dec_init(&dec);
    in_size = in_pos = 0;
    refill();
    fade = 0.f;
    return in_size > 0;
}

/* Decode the next frame into frame_pcm, refilling / looping as needed.
 * Returns false only if no audio can be produced at all. */
static bool fill_frame(void) {
    for (int guard = 0; guard < 16; guard++) {
        int have = in_size - in_pos;
        if (have < FRAME_MIN) { refill(); have = in_size - in_pos; }
        if (have <= 0) {                 /* nothing left: wrap to the top */
            if (!loop_restart()) return false;
            continue;
        }

        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&dec, in_buf + in_pos, have,
                                          frame_pcm, &info);
        in_pos += info.frame_bytes;

        if (info.frame_bytes == 0) {     /* no complete frame in a full window: EOF */
            if (!loop_restart()) return false;
            continue;
        }
        if (samples > 0) {               /* real audio frame */
            frame_len = samples;
            frame_ch  = info.channels;
            ratio     = (float)info.hz / (float)OUT_RATE;
            if (ratio <= 0.f) ratio = 1.f;
            return true;
        }
        /* samples == 0 but bytes consumed: skipped an ID3 tag / junk — retry. */
    }
    return false;
}

static bool open_track(const char *path) {
    if (fh >= 0) { dfs_close(fh); fh = -1; }
    int h = dfs_open(path);
    if (h < 0) return false;
    fh = h;
    mp3dec_init(&dec);
    in_size = in_pos = 0;
    frame_len = 0;
    frame_ch  = 2;
    ratio     = 1.f;
    src_pos   = 0.f;
    fade      = 0.f;
    refill();
    return true;
}

bool music_init(void) {
    avail   = false;
    playing = false;
    fh      = -1;
    if (dfs_init(DFS_DEFAULT_LOCATION) != DFS_ESUCCESS) return false;
    avail = true;
    return true;
}

void music_play(music_track_t t) {
    if (!avail || t <= MUSIC_NONE || t >= MUSIC_TRACK_COUNT) { music_stop(); return; }
    if (!open_track(track_path[t])) { playing = false; return; }
    playing = true;
}

void music_stop(void) {
    playing = false;
    if (fh >= 0) { dfs_close(fh); fh = -1; }
}

bool music_active(void) { return playing; }

void music_sample(float *l, float *r) {
    if (!playing) { *l = *r = 0.f; return; }

    /* Advance across frame boundaries, carrying the fractional remainder. */
    while (src_pos >= (float)frame_len) {
        src_pos -= (float)frame_len;
        if (!fill_frame()) { *l = *r = 0.f; playing = false; return; }
    }

    int idx = (int)src_pos;
    if (idx >= frame_len) idx = frame_len - 1;

    float sl, sr;
    if (frame_ch == 2) { sl = frame_pcm[idx * 2]; sr = frame_pcm[idx * 2 + 1]; }
    else               { sl = sr = frame_pcm[idx]; }

    src_pos += ratio;

    if (fade < 1.f) {
        fade += 1.f / (OUT_RATE * 0.02f);   /* ~20ms */
        if (fade > 1.f) fade = 1.f;
    }
    float g = fade * (1.f / 32768.f);
    *l = sl * g;
    *r = sr * g;
}
