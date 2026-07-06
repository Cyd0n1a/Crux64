#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "render.h"
#include "climber_render.h"
#include "../gen/mountain.h"
#include "../version.h"

/* Pre-dawn alpine sky; fog fades terrain into the same tint so the
 * horizon is seamless (GDD 3.1: fog hides chunk pop-in + altitude). */
#define SKY_COLOR   RGBA32(114, 128, 156, 0xFF)
#define FOG_NEAR    160.f
#define FOG_FAR     430.f
/* Near plane hugs the climber close-up; far stays past the fog wall.
 * 16-bit Z at this ratio is tight but the fog hides distant fighting. */
#define CAM_FOV     T3D_DEG_TO_RAD(65.f)
#define CAM_NEAR    1.2f
#define CAM_FAR     500.f

#define NUM_CHUNKS    (MTN_CHUNKS * MTN_CHUNKS)
#define CHUNK_VCOUNT  (MTN_CHUNK_VERTS * MTN_CHUNK_VERTS)   /* 64 <= 70 cache */
#define CHUNK_PACKED  (CHUNK_VCOUNT / 2)

static T3DViewport viewport;
static surface_t   zbuf;
static T3DMat4FP  *model_mat;

/* Static mesh: verts baked once at boot, one recorded block per chunk. */
static T3DVertPacked *chunk_verts[NUM_CHUNKS];
static rspq_block_t  *chunk_dpl[NUM_CHUNKS];
static T3DVec3        chunk_center[NUM_CHUNKS];
static float          chunk_radius[NUM_CHUNKS];
static int            chunks_drawn;

static void set_vert(T3DVertPacked *packed, int v, int gx, int gz) {
    float h = mountain_height_at(gx, gz);
    float n[3];
    mountain_normal_at(gx, gz, n);

    int16_t px = (int16_t)(gx * MTN_CELL_SIZE - MTN_HALF);
    int16_t py = (int16_t)h;
    int16_t pz = (int16_t)(gz * MTN_CELL_SIZE - MTN_HALF);

    T3DVec3 nv = {{ n[0], n[1], n[2] }};
    uint16_t norm  = t3d_vert_pack_normal(&nv);
    uint32_t color = mountain_color_at(gx, gz);

    T3DVertPacked *p = &packed[v / 2];
    if (v & 1) {
        p->posB[0] = px; p->posB[1] = py; p->posB[2] = pz;
        p->normB = norm; p->rgbaB = color;
    } else {
        p->posA[0] = px; p->posA[1] = py; p->posA[2] = pz;
        p->normA = norm; p->rgbaA = color;
    }
}

static void build_chunk(int cx, int cz) {
    int ci = cz * MTN_CHUNKS + cx;
    chunk_verts[ci] = malloc_uncached(sizeof(T3DVertPacked) * CHUNK_PACKED);

    int gx0 = cx * MTN_CHUNK_CELLS;
    int gz0 = cz * MTN_CHUNK_CELLS;

    float hmin = 1e9f, hmax = -1e9f;
    for (int vz = 0; vz < MTN_CHUNK_VERTS; vz++) {
        for (int vx = 0; vx < MTN_CHUNK_VERTS; vx++) {
            set_vert(chunk_verts[ci], vz * MTN_CHUNK_VERTS + vx,
                     gx0 + vx, gz0 + vz);
            float h = mountain_height_at(gx0 + vx, gz0 + vz);
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }

    /* Bounding sphere for CPU-side culling. */
    float half_xz = MTN_CHUNK_CELLS * MTN_CELL_SIZE * 0.5f;
    chunk_center[ci] = (T3DVec3){{
        (gx0 + MTN_CHUNK_CELLS * 0.5f) * MTN_CELL_SIZE - MTN_HALF,
        (hmin + hmax) * 0.5f,
        (gz0 + MTN_CHUNK_CELLS * 0.5f) * MTN_CELL_SIZE - MTN_HALF,
    }};
    float half_y = (hmax - hmin) * 0.5f;
    chunk_radius[ci] = sqrtf(2.f * half_xz * half_xz + half_y * half_y);

    rspq_block_begin();
    t3d_vert_load(chunk_verts[ci], 0, CHUNK_VCOUNT);
    for (int vz = 0; vz < MTN_CHUNK_CELLS; vz++) {
        for (int vx = 0; vx < MTN_CHUNK_CELLS; vx++) {
            int i00 = vz * MTN_CHUNK_VERTS + vx;
            int i10 = i00 + 1;
            int i01 = i00 + MTN_CHUNK_VERTS;
            int i11 = i01 + 1;
            /* CCW seen from above (+Y) => front faces point up/out. */
            t3d_tri_draw(i00, i01, i11);
            t3d_tri_draw(i00, i11, i10);
        }
    }
    t3d_tri_sync();
    chunk_dpl[ci] = rspq_block_end();
}

void render_init(void) {
    viewport = t3d_viewport_create();
    zbuf     = surface_alloc(FMT_RGBA16, 320, 240);

    model_mat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(model_mat);

    for (int cz = 0; cz < MTN_CHUNKS; cz++)
        for (int cx = 0; cx < MTN_CHUNKS; cx++)
            build_chunk(cx, cz);

    climber_render_init();

    rdpq_font_t *font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, font);
}

static void draw_hud(const render_hud_t *hud) {
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 22,
                     "CRUX64 %s  seed 0x%08lX", CRUX64_VERSION,
                     (unsigned long)MTN_SEED);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 34,
                     "gen %.0fms  grips %d  chunks %d/%d  fps %.1f",
                     hud->gen_ms, hud->grip_count, chunks_drawn, NUM_CHUNKS,
                     display_get_fps());
    if (hud->status)
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 204,
                         "%s", hud->status);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 216,
                     "ALT %dm%s", (int)hud->climber_alt,
                     hud->rumble_ok ? "" : "   INSERT RUMBLE PAK");
}

void render_frame(const T3DVec3 *eye, const T3DVec3 *target,
                  const render_hud_t *hud) {
    t3d_viewport_set_projection(&viewport, CAM_FOV, CAM_NEAR, CAM_FAR);
    t3d_viewport_look_at(&viewport, eye, target, &(T3DVec3){{ 0, 1, 0 }});

    rdpq_attach(display_get(), &zbuf);
    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(SKY_COLOR);

    t3d_screen_clear_color(SKY_COLOR);
    t3d_screen_clear_depth();

    t3d_fog_set_range(FOG_NEAR, FOG_FAR);
    t3d_fog_set_enabled(true);

    /* Low warm sun + cool skylight ambient (GDD 3.1). */
    t3d_light_set_ambient((uint8_t[]){ 72, 78, 100, 0xFF });
    T3DVec3 sun_dir = {{ 0.48f, 0.62f, 0.34f }};
    t3d_vec3_norm(&sun_dir);
    t3d_light_set_directional(0, (uint8_t[]){ 255, 235, 200, 0xFF }, &sun_dir);
    t3d_light_set_count(1);

    t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_CULL_BACK);

    /* CPU chunk culling: behind-camera and beyond-fog chunks skipped. */
    T3DVec3 fwd = {{ target->v[0] - eye->v[0],
                     target->v[1] - eye->v[1],
                     target->v[2] - eye->v[2] }};
    t3d_vec3_norm(&fwd);
    float max_dist = FOG_FAR;

    chunks_drawn = 0;
    t3d_matrix_push(model_mat);
    for (int ci = 0; ci < NUM_CHUNKS; ci++) {
        T3DVec3 to = {{ chunk_center[ci].v[0] - eye->v[0],
                        chunk_center[ci].v[1] - eye->v[1],
                        chunk_center[ci].v[2] - eye->v[2] }};
        float along = t3d_vec3_dot(&to, &fwd);
        if (along < -chunk_radius[ci])
            continue;
        float d2 = t3d_vec3_dot(&to, &to);
        float reach = max_dist + chunk_radius[ci];
        if (d2 > reach * reach)
            continue;
        rspq_block_run(chunk_dpl[ci]);
        chunks_drawn++;
    }
    t3d_matrix_pop(1);

    climber_render_draw();

    draw_hud(hud);

    rdpq_detach_show();
}
