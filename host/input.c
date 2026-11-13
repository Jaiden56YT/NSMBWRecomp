/* Wii Remote input from the keyboard or a gamepad, handed to the game
   through high-level replacements of WPADProbe and KPADRead/KPADReadEx.

   The remote is held sideways (NSMBW's default), so the host d-pad is
   rotated: host left = remote up, host right = remote down,
   host up = remote right, host down = remote left.

   Keyboard: arrows move, X/Space = 2 (jump), Z = 1 (run), Enter = A,
   C = B, P = +, M = -, H = Home, S/Shift = shake.
   Gamepad (Nintendo layout): south = 2, west = 1, east = A, north = shake,
   shoulders = B/shake, Start = +, Back = -, Guide = Home. */
#include "host.h"

#include <SDL2/SDL.h>
#include <string.h>

enum {
    BTN_LEFT = 0x0001, BTN_RIGHT = 0x0002, BTN_DOWN = 0x0004, BTN_UP = 0x0008,
    BTN_PLUS = 0x0010, BTN_2 = 0x0100, BTN_1 = 0x0200, BTN_B = 0x0400,
    BTN_A = 0x0800, BTN_MINUS = 0x1000, BTN_HOME = 0x8000,
};

static u32 cur_hold;
static int shake_frames;
static SDL_GameController* pad;
u32 g_input_script_hold;  /* buttons forced by -press (testing) */

static void open_pad(void) {
    if (pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i) && (pad = SDL_GameControllerOpen(i))) {
            LOG("input: gamepad %s\n", SDL_GameControllerName(pad));
            return;
        }
    }
}

/* Sample the host devices; called once per guest field (60 Hz). */
void input_update(void) {
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    open_pad();
    const u8* k = SDL_GetKeyboardState(NULL);
    u32 h = 0;
    int left = k[SDL_SCANCODE_LEFT], right = k[SDL_SCANCODE_RIGHT], up = k[SDL_SCANCODE_UP], down = k[SDL_SCANCODE_DOWN];
    int shake = k[SDL_SCANCODE_S] || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_SPACE]) h |= BTN_2;
    if (k[SDL_SCANCODE_Z]) h |= BTN_1;
    if (k[SDL_SCANCODE_RETURN]) h |= BTN_A;
    if (k[SDL_SCANCODE_C]) h |= BTN_B;
    if (k[SDL_SCANCODE_P]) h |= BTN_PLUS;
    if (k[SDL_SCANCODE_M]) h |= BTN_MINUS;
    if (k[SDL_SCANCODE_H]) h |= BTN_HOME;
    if (pad) {
        left |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        right |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        up |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        down |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (lx < -16000) left = 1;
        if (lx > 16000) right = 1;
        if (ly < -16000) up = 1;
        if (ly > 16000) down = 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) h |= BTN_2;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X)) h |= BTN_1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) h |= BTN_A;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) h |= BTN_B;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) h |= BTN_PLUS;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) h |= BTN_MINUS;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_GUIDE)) h |= BTN_HOME;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) shake = 1;
    }
    /* sideways rotation */
    if (left) h |= BTN_UP;
    if (right) h |= BTN_DOWN;
    if (up) h |= BTN_RIGHT;
    if (down) h |= BTN_LEFT;
    h |= g_input_script_hold;
    if (shake && shake_frames <= 0) shake_frames = 12;
    cur_hold = h;
}

/* ---- game-side replacements ---- */

static void wf(CPUState* cpu, u32 addr, float f) { u32 v; memcpy(&v, &f, 4); mem_write32(cpu, addr, v); }

/* KPAD's internal reader (chan r3, samples r4, count r5, result ptr r6). */
void hle_kpad_read(CPUState* cpu, int ex) {
    s32 chan = (s32)cpu->gpr[3];
    u32 out = cpu->gpr[4];
    s32 n = (s32)cpu->gpr[5];
    u32 errp = ex ? cpu->gpr[6] : 0;
    static u32 last_hold[4];
    if (chan != 0 || n <= 0 || !out) {
        if (errp) mem_write32(cpu, errp, (u32)-1);  /* WPAD_ERR_NO_CONTROLLER */
        cpu->gpr[3] = 0;
        cpu->pc = cpu->lr;
        return;
    }
    u32 hold = cur_hold;
    for (u32 i = 0; i < 0x84; i += 4) mem_write32(cpu, out + i, 0);
    mem_write32(cpu, out + 0x00, hold);
    mem_write32(cpu, out + 0x04, hold & ~last_hold[chan]);
    mem_write32(cpu, out + 0x08, last_hold[chan] & ~hold);
    last_hold[chan] = hold;
    /* Accelerometer (g): resting flat, face up. A shake swings it hard. */
    float ax = 0, ay = -1, az = 0;
    if (shake_frames > 0) {
        float s = (shake_frames & 2) ? 3.0f : -3.0f;
        ax = s; ay = -1 + s * 0.5f;
        shake_frames--;
    }
    wf(cpu, out + 0x0C, ax);
    wf(cpu, out + 0x10, ay);
    wf(cpu, out + 0x14, az);
    float mag = ax * ax + ay * ay + az * az;
    wf(cpu, out + 0x18, mag > 0 ? __builtin_sqrtf(mag) : 0);
    wf(cpu, out + 0x1C, shake_frames > 0 ? 2.0f : 0.0f);
    wf(cpu, out + 0x34, 1.0f);   /* horizon: level */
    wf(cpu, out + 0x48, 1.0f);   /* dist */
    wf(cpu, out + 0x54, 1.0f);   /* acc_vertical */
    mem_write8(cpu, out + 0x5C, 0);      /* dev_type: core remote */
    mem_write8(cpu, out + 0x5D, 0);      /* wpad_err: none */
    mem_write8(cpu, out + 0x5E, 0);      /* pointer not valid */
    mem_write8(cpu, out + 0x5F, 1);      /* data format: core + acc */
    if (errp) mem_write32(cpu, errp, 0);
    cpu->gpr[3] = 1;
    cpu->pc = cpu->lr;
}

/* WPADProbe(chan r3, s32* type r4): one core remote on channel 0. */
void hle_wpad_probe(CPUState* cpu) {
    s32 chan = (s32)cpu->gpr[3];
    u32 typep = cpu->gpr[4];
    if (chan == 0) {
        if (typep) mem_write32(cpu, typep, 0);  /* WPAD_DEV_CORE */
        cpu->gpr[3] = 0;
    } else {
        if (typep) mem_write32(cpu, typep, 0xFD);  /* WPAD_DEV_NONE */
        cpu->gpr[3] = (u32)-1;
    }
    cpu->pc = cpu->lr;
}
