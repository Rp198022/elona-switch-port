/*
 * P1 / T1.3 - minimal SDL2 demo for the Elona+ -> Switch (Atmosphere) port.
 *
 * Purpose: prove, on real hardware, the exact four capabilities Elona+ needs.
 *   1) SDL2 video + SDL_RenderSetLogicalSize(renderer, 800, 600)  -> the scaling plan
 *   2) SDL2_image loads and displays a BMP                        -> picload() replacement
 *   3) controller input                                           -> keyboard replacement
 *   4) SDL2_mixer audio + SDL2_ttf UTF-8 text (Japanese + Chinese) -> mmplay()/mes() replacement
 *
 * Structure follows the official devkitPro example
 *   switchbrew/switch-examples :: graphics/sdl2/sdl2-demo
 *
 * Controls (shown on screen as well):
 *   any face button (A/B/X/Y) ... spawn another moving square, reports which button
 *   B is also used to replay the sound in the joystick-fallback path
 *   + (START) ................... quit
 *
 * No game assets are used here: the BMP/WAV are generated, the font is Noto Sans SC (OFL).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>     /* chdir() */

#include <switch.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#define WIN_W       1280
#define WIN_H       720
#define LOGICAL_W   800
#define LOGICAL_H   600

#define MAX_SQUARES 64

/* raw joystick button indices used by the Switch joystick driver (fallback path) */
#define JOY_A       0
#define JOY_B       1
#define JOY_X       2
#define JOY_Y       3
#define JOY_PLUS    10

typedef struct {
    float x, y, vx, vy;
    Uint8 r, g, b;
} Square;

static Square squares[MAX_SQUARES];
static int    square_count = 0;

static SDL_Texture *make_text(SDL_Renderer *ren, TTF_Font *font, const char *utf8, SDL_Color col, int *w, int *h)
{
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, utf8, col);
    if (!surf) { if (w) *w = 0; if (h) *h = 0; return NULL; }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    if (w) *w = surf->w;
    if (h) *h = surf->h;
    SDL_FreeSurface(surf);
    return tex;
}

static int rand_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo + 1);
}

static void spawn_square(void)
{
    if (square_count >= MAX_SQUARES) return;
    Square *s = &squares[square_count++];
    s->x  = (float)rand_range(40, LOGICAL_W - 80);
    s->y  = (float)rand_range(40, LOGICAL_H - 220);
    s->vx = (float)rand_range(2, 6) * ((rand() & 1) ? 1.0f : -1.0f);
    s->vy = (float)rand_range(2, 6) * ((rand() & 1) ? 1.0f : -1.0f);
    s->r  = (Uint8)rand_range(90, 255);
    s->g  = (Uint8)rand_range(90, 255);
    s->b  = (Uint8)rand_range(90, 255);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    romfsInit();
    chdir("romfs:/");
    srand((unsigned)time(NULL));

    int exit_requested = 0;
    int frames = 0;

    /* ---------- the four subsystems Elona+ needs ---------- */

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

    IMG_Init(0);            /* BMP needs no optional decoder, it is always available */
    TTF_Init();

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("audio subsystem failed: %s\n", SDL_GetError());
    }
    if (Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 4096) != 0) {
        printf("Mix_OpenAudio failed: %s\n", Mix_GetError());
    }
    Mix_AllocateChannels(8);

    SDL_Window *window = SDL_CreateWindow("elona-switch-port / sdl_demo",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    /* (1) the 800x600 logical canvas, integer-scaled and letterboxed into 1280x720 */
    SDL_RenderSetLogicalSize(ren, LOGICAL_W, LOGICAL_H);

    /* (2) BMP through SDL2_image */
    SDL_Texture *bmp_tex = NULL;
    int bmp_w = 0, bmp_h = 0;
    SDL_Surface *bmp = IMG_Load("test.bmp");
    if (bmp) {
        bmp_w = bmp->w;
        bmp_h = bmp->h;
        bmp_tex = SDL_CreateTextureFromSurface(ren, bmp);
        SDL_FreeSurface(bmp);
    } else {
        printf("IMG_Load(test.bmp) failed: %s\n", IMG_GetError());
    }

    /* (4a) audio */
    Mix_Chunk *beep = Mix_LoadWAV("beep.wav");
    if (!beep) printf("Mix_LoadWAV(beep.wav) failed: %s\n", Mix_GetError());

    /* (4b) font: Noto Sans SC (OFL) - must render both kana and hanzi */
    TTF_Font *font = TTF_OpenFont("font.otf", 22);
    if (!font) printf("TTF_OpenFont(font.otf) failed: %s\n", TTF_GetError());

    /* (3) controller: GameController first, raw joystick as fallback */
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);

    SDL_GameController *gc = NULL;
    SDL_Joystick       *js = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            gc = SDL_GameControllerOpen(i);
            if (gc) break;
        }
    }
    if (!gc) {
        js = SDL_JoystickOpen(0);
    }

    char backend[128];
    snprintf(backend, sizeof backend, "input: %s",
             gc ? "SDL_GameController" : (js ? "SDL_Joystick (fallback)" : "NONE"));

    const char *pad_name = "none";
    if (gc) pad_name = SDL_GameControllerName(gc);
    else if (js) pad_name = SDL_JoystickName(js);

    SDL_Color white   = { 235, 240, 255, 255 };
    SDL_Color dim     = { 150, 165, 200, 255 };

    int tw = 0, th = 0;
    SDL_Texture *t_title = font ? make_text(ren, font, "Elona+ -> Switch  /  P1 SDL2 demo", white, &tw, &th) : NULL;
    SDL_Texture *t_scale = font ? make_text(ren, font, "logical canvas 800x600 scaled to 1280x720 (black bars = letterbox)", dim, NULL, NULL) : NULL;
    SDL_Texture *t_jp    = font ? make_text(ren, font, "日本語のテキスト描画テスト:ノースティリス / エーテル風", white, NULL, NULL) : NULL;
    SDL_Texture *t_cn    = font ? make_text(ren, font, "中文渲染测试:伊尔瓦大陆・冒险者・装备", white, NULL, NULL) : NULL;
    SDL_Texture *t_img   = bmp_tex ? make_text(ren, font, "SDL2_image: test.bmp loaded", dim, NULL, NULL) : NULL;

    spawn_square();

    char  status1[256] = { 0 };
    char  status2[256] = { 0 };
    char  status3[256] = { 0 };
    char  status4[256] = { 0 };
    char  last_btn[64] = "none";
    int   last_btn_idx = -1;
    SDL_Texture *t_status1 = NULL, *t_status2 = NULL;
    SDL_Texture *t_status3 = NULL, *t_status4 = NULL;
    int   w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0, w4 = 0, h4 = 0;
    Uint32 status_next = 0;

    /* T1.5 probe: count each event family separately, so the runtime choice of
       which one actually drives input can be verified on hardware. */
    int   evt_ctrl = 0, evt_joy = 0;

    if (beep) Mix_PlayChannel(-1, beep, 0);   /* prove audio immediately on boot */

    /* ---------- main loop ---------- */
    while (!exit_requested && appletMainLoop()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            int spawn = 0;

            if (ev.type == SDL_QUIT) exit_requested = 1;

            if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                evt_ctrl++;
                last_btn_idx = ev.cbutton.button;
                switch (ev.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A: snprintf(last_btn, sizeof last_btn, "A"); spawn = 1; break;
                    case SDL_CONTROLLER_BUTTON_B: snprintf(last_btn, sizeof last_btn, "B"); spawn = 1; break;
                    case SDL_CONTROLLER_BUTTON_X: snprintf(last_btn, sizeof last_btn, "X"); spawn = 1; break;
                    case SDL_CONTROLLER_BUTTON_Y: snprintf(last_btn, sizeof last_btn, "Y"); spawn = 1; break;
                    case SDL_CONTROLLER_BUTTON_START: snprintf(last_btn, sizeof last_btn, "+ (START)"); exit_requested = 1; break;
                    default: snprintf(last_btn, sizeof last_btn, "btn#%d", ev.cbutton.button); break;
                }
            }

            if (ev.type == SDL_JOYBUTTONDOWN) {
                evt_joy++;
                /* SDL2 emits BOTH SDL_CONTROLLERBUTTONDOWN and SDL_JOYBUTTONDOWN for a
                   controller-mapped device on Switch. Acting on both spawned two
                   squares per press, so raw joystick is only acted on when no
                   GameController could be opened (the fallback this demo intends). */
                if (!gc) {
                    last_btn_idx = ev.jbutton.button;
                    switch (ev.jbutton.button) {
                        case JOY_A: snprintf(last_btn, sizeof last_btn, "raw 0 (A pos)");  spawn = 1; break;
                        case JOY_B: snprintf(last_btn, sizeof last_btn, "raw 1 (B pos)");  spawn = 1; if (beep) Mix_PlayChannel(-1, beep, 0); break;
                        case JOY_X: snprintf(last_btn, sizeof last_btn, "raw 2 (X pos)");  spawn = 1; break;
                        case JOY_Y: snprintf(last_btn, sizeof last_btn, "raw 3 (Y pos)");  spawn = 1; break;
                        case JOY_PLUS: snprintf(last_btn, sizeof last_btn, "raw 10 (+)");  exit_requested = 1; break;
                        default: snprintf(last_btn, sizeof last_btn, "raw %d", ev.jbutton.button); break;
                    }
                }
            }

            if (spawn) {
                spawn_square();
                if (gc && beep) Mix_PlayChannel(-1, beep, 0);
            }
        }

        /* move + bounce inside the 800x600 logical canvas */
        for (int i = 0; i < square_count; i++) {
            Square *s = &squares[i];
            s->x += s->vx;
            s->y += s->vy;
            if (s->x < 0)                     { s->x = 0; s->vx = -s->vx; }
            if (s->x + 24 > LOGICAL_W)        { s->x = (float)(LOGICAL_W - 24); s->vx = -s->vx; }
            if (s->y < 0)                     { s->y = 0; s->vy = -s->vy; }
            if (s->y + 24 > LOGICAL_H - 150)  { s->y = (float)(LOGICAL_H - 174); s->vy = -s->vy; }
        }

        /* ---- draw ---- */
        SDL_SetRenderDrawColor(ren, 12, 16, 28, 255);
        SDL_RenderClear(ren);

        /* outer border proves the logical canvas was letterboxed correctly */
        SDL_SetRenderDrawColor(ren, 60, 90, 150, 255);
        SDL_Rect frame = { 0, 0, LOGICAL_W, LOGICAL_H };
        SDL_RenderDrawRect(ren, &frame);
        SDL_Rect inner = { 1, 1, LOGICAL_W - 2, LOGICAL_H - 2 };
        SDL_RenderDrawRect(ren, &inner);

        /* (2) the BMP, tinted so it is obvious it went through SDL2_image */
        if (bmp_tex) {
            SDL_Rect dst = { LOGICAL_W - bmp_w - 16, LOGICAL_H - bmp_h - 16, bmp_w, bmp_h };
            SDL_SetTextureColorMod(bmp_tex, 255, 255, 255);
            SDL_RenderCopy(ren, bmp_tex, NULL, &dst);
        }

        for (int i = 0; i < square_count; i++) {
            SDL_SetRenderDrawColor(ren, squares[i].r, squares[i].g, squares[i].b, 255);
            SDL_Rect r = { (int)squares[i].x, (int)squares[i].y, 24, 24 };
            SDL_RenderFillRect(ren, &r);
        }

        /* (4b) UTF-8 text through SDL2_ttf */
        int y = 10;
        SDL_Rect tr = { 12, y, 0, 0 };
        if (t_title) { SDL_QueryTexture(t_title, NULL, NULL, &tr.w, &tr.h); SDL_RenderCopy(ren, t_title, NULL, &tr); y += tr.h + 4; }
        tr.y = y;
        if (t_scale) { SDL_QueryTexture(t_scale, NULL, NULL, &tr.w, &tr.h); SDL_RenderCopy(ren, t_scale, NULL, &tr); y += tr.h + 10; }
        tr.y = y;
        if (t_jp)    { SDL_QueryTexture(t_jp, NULL, NULL, &tr.w, &tr.h); SDL_RenderCopy(ren, t_jp, NULL, &tr); y += tr.h + 4; }
        tr.y = y;
        if (t_cn)    { SDL_QueryTexture(t_cn, NULL, NULL, &tr.w, &tr.h); SDL_RenderCopy(ren, t_cn, NULL, &tr); y += tr.h + 4; }
        tr.y = y;
        if (t_img)   { SDL_QueryTexture(t_img, NULL, NULL, &tr.w, &tr.h); SDL_RenderCopy(ren, t_img, NULL, &tr); y += tr.h + 4; }

        /* dynamic status, refreshed twice per second. Split across four short lines:
           the previous single line was ~1650px wide on an 800px logical canvas, so
           its tail (squares / audio / font / frames) was clipped at the screen edge. */
        Uint32 now = SDL_GetTicks();
        if (now >= status_next) {
            status_next = now + 500;
            snprintf(status1, sizeof status1, "%s  |  pad: %s",
                     backend, pad_name ? pad_name : "none");
            snprintf(status2, sizeof status2, "last: %s (#%d)  |  squares: %d  |  frames: %d",
                     last_btn, last_btn_idx, square_count, frames);
            snprintf(status3, sizeof status3, "audio: %s  |  font: %s",
                     beep ? "beep.wav ok" : "FAILED", font ? "font.otf ok" : "FAILED");
            snprintf(status4, sizeof status4, "btn events: controller=%d  rawjoystick=%d",
                     evt_ctrl, evt_joy);
            if (t_status1) { SDL_DestroyTexture(t_status1); t_status1 = NULL; }
            if (t_status2) { SDL_DestroyTexture(t_status2); t_status2 = NULL; }
            if (t_status3) { SDL_DestroyTexture(t_status3); t_status3 = NULL; }
            if (t_status4) { SDL_DestroyTexture(t_status4); t_status4 = NULL; }
            if (font) {
                t_status1 = make_text(ren, font, status1, dim, &w1, &h1);
                t_status2 = make_text(ren, font, status2, dim, &w2, &h2);
                t_status3 = make_text(ren, font, status3, dim, &w3, &h3);
                t_status4 = make_text(ren, font, status4, dim, &w4, &h4);
            }
        }
        {
            int sy = LOGICAL_H - (h1 + h2 + h3 + h4) - 3 * 4 - 10;
            SDL_Rect r1 = { 12, sy, w1, h1 };
            SDL_Rect r2 = { 12, sy + h1 + 4, w2, h2 };
            SDL_Rect r3 = { 12, sy + h1 + h2 + 8, w3, h3 };
            SDL_Rect r4 = { 12, sy + h1 + h2 + h3 + 12, w4, h4 };
            if (t_status1) SDL_RenderCopy(ren, t_status1, NULL, &r1);
            if (t_status2) SDL_RenderCopy(ren, t_status2, NULL, &r2);
            if (t_status3) SDL_RenderCopy(ren, t_status3, NULL, &r3);
            if (t_status4) SDL_RenderCopy(ren, t_status4, NULL, &r4);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
        frames++;
    }

    /* ---- teardown ---- */
    if (t_title)  SDL_DestroyTexture(t_title);
    if (t_scale)  SDL_DestroyTexture(t_scale);
    if (t_jp)     SDL_DestroyTexture(t_jp);
    if (t_cn)     SDL_DestroyTexture(t_cn);
    if (t_img)    SDL_DestroyTexture(t_img);
    if (t_status1) SDL_DestroyTexture(t_status1);
    if (t_status2) SDL_DestroyTexture(t_status2);
    if (t_status3) SDL_DestroyTexture(t_status3);
    if (t_status4) SDL_DestroyTexture(t_status4);
    if (bmp_tex)  SDL_DestroyTexture(bmp_tex);
    if (font)     TTF_CloseFont(font);
    if (beep)     { Mix_HaltChannel(-1); Mix_FreeChunk(beep); }
    if (gc)       SDL_GameControllerClose(gc);
    if (js)       SDL_JoystickClose(js);

    Mix_CloseAudio();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    romfsExit();
    return 0;
}