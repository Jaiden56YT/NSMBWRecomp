// The Mods button on the file select (Free Mode / Coin Battle / Mods), US Rev 2 ("E2").
//
// The layout side - the button's panes, its F02_mods group, its own materials and animation tracks
// - is added to fileSelectBase.arc as it is served (runtime/src/product/nsmbw_layout_patch.cpp,
// PatchFileSelect). This file is the game side: dFileSelect_c in d_basesNP.rel.
//
// dFileSelect_c keeps the cursor in mChoice (+0x2A4): 0..2 the save files, 3 Free Mode, 4 Coin
// Battle; the previous choice in +0x2A8, the last file column in +0x2CC and the last bottom button
// in +0x2D0. Its states find a bottom button's animations by comparing with 3 / 4, so Mods is
// choice 5 and every state that animates or moves through the bottom row is replaced:
//   0x80782EC0  createLayout       copy; EDIT at GroupRegister (six F02_mods bindings after the
//                                  45 originals: in 45, on 46, idle 47, hit 48, off 49, out 50) and
//                                  after WPaneRegister (W_multiMods_00, the cursor's frame)
//   0x807835C0  StartWait init     + the Mods button's in animation set to its start
//   0x807837D0  OpenWait init      + its in animation
//   0x80783930  MultiButtnOnStage init (back from copy / delete) + its in animation
//   0x80783A40  ButtonSelectAnimeEndWait init      on / off animations for choice 5
//   0x80783BF0  ButtonSelectAnimeEndWait finalize  the cursor on W_multiMods_00
//   0x80783C50  SelectWait execute copy; EDIT at the navigation: the three bottom buttons sit under
//                                  the three files, so down / up go straight between file n and
//                                  button 3 + n, and left / right walk the row
//   0x80783FF0  ButtonHitAnimeEndWait init         hit animation for choice 5
//   0x80784070  ButtonHitAnimeEndWait execute      Mods opens the mods menu (nsmbw_options_menu.cpp)
//                                  instead of starting a game; when it has closed, back to
//                                  ButtonSelectAnimeEndWait with Mods highlighted
//   0x80784240  MultiButtonExitAnimeEndWait init   + its out animation (to copy / delete)
//
// Addresses: mkwcat/nsmbw-project's d_FileSelect.h lists the PAL addresses of every state function;
// all 134 are 0x40 higher in the US d_basesNP translation (checked against its function list), and
// the bodies were read from the US REL with its relocations applied. Fields from the same header
// and from those bodies: mLayout +0x74 (LytBase_c), mStateMgr +0x20C (changeState = vtable +0x18),
// mpDateFile[3] +0x24C, W_multiButton_00 / W_multiCoin_00 +0x294 / +0x298. State IDs (bss):
// ButtonSelectAnimeEndWait 0x80992030, GameStartWait 0x80992130. LytBase_c (main.dol):
// WPaneRegister 0x800C8DD0, GroupRegister 0x800C90F0, AnimeStartSetup 0x800C92F0,
// ReverseAnimeStartSetup 0x800C93D0, AllAnimeEndSetup 0x800C9500, isAnime 0x800C9610;
// dGameCom::SelectCursorSetup 0x800B44D0; SndAudioMgr::startSystemSe 0x80195380 on the instance
// at 0x8042A488.
//
// If the layout patch failed (NsmbwFileSelectHasMods false), choice 5 never happens and every
// function here does exactly what the original does.
#include <cstdint>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" bool NsmbwFileSelectHasMods();        // runtime/src/product/nsmbw_layout_patch.cpp
extern "C" void NsmbwModsMenuRequestOpen();      // runtime/src/product/nsmbw_options_menu.cpp
extern "C" bool NsmbwModsMenuTakeClosed();       // same file: true once, when it has closed

namespace {
constexpr int32_t kMods = 5;
constexpr uint32_t kLayout = 0x74, kStateMgr = 0x20C, kDateFiles = 0x24C, kRootPane = 0x268;
constexpr uint32_t kChoice = 0x2A4, kPrevChoice = 0x2A8, kTopColumn = 0x2CC, kBottomButton = 0x2D0;
constexpr uint32_t kWindowFree = 0x294, kWindowCoin = 0x298;
constexpr uint32_t kStateButtonSelect = 0x80992030u, kStateGameStart = 0x80992130u;
constexpr uint32_t kSndAudioMgr = 0x8042A488u;
constexpr uint32_t kOriginalBindings = 45;
// Group-animation indices (the order of the createLayout table; see nsmbw_layout_patch.cpp).
constexpr uint32_t kInFree = 0x0D, kInCoin = 0x0E, kOnFree = 0x22, kOnCoin = 0x23, kHitFree = 0x26,
                   kHitCoin = 0x27, kOffFree = 0x28, kOffCoin = 0x29, kOutFree = 0x2A, kOutCoin = 0x2B;
constexpr uint32_t kInMods = 45, kOnMods = 46, kHitMods = 48, kOffMods = 49, kOutMods = 50;
// The anim resources each F02_mods binding uses: inMulti 7, onMulti 23, idleMulti 24, hitMulti 25,
// offMulti 26, outMulti 27 (createLayout's resource table).
constexpr uint32_t kModsAnimResources[6] = {7, 23, 24, 25, 26, 27};

uint32_t OnAnim(int32_t c) { return c == 3 ? kOnFree : c == 4 ? kOnCoin : kOnMods; }
uint32_t OffAnim(int32_t c) { return c == 3 ? kOffFree : c == 4 ? kOffCoin : kOffMods; }
uint32_t HitAnim(int32_t c) { return c == 3 ? kHitFree : c == 4 ? kHitCoin : kHitMods; }

uint32_t g_modsWindow = 0;   // W_multiMods_00 of the current file select
bool g_waitingForMenu = false;

uint32_t R32(uint32_t a) { return MemoryInline::FlatRead32(a); }
int32_t S32(uint32_t a) { return static_cast<int32_t>(MemoryInline::FlatRead32(a)); }

void AnimeStart(CpuContext* ctx, uint32_t self, uint32_t anim) {
    ctx->gpr[3] = self + kLayout;
    ctx->gpr[4] = anim;
    ctx->gpr[5] = 0;
    InvokeDirectCpu<0x800C92F0u>(ctx);
}
void AnimeReverse(CpuContext* ctx, uint32_t self, uint32_t anim) {
    ctx->gpr[3] = self + kLayout;
    ctx->gpr[4] = anim;
    ctx->gpr[5] = 0;
    InvokeDirectCpu<0x800C93D0u>(ctx);
}
void AllAnimeEnd(CpuContext* ctx, uint32_t self) {
    ctx->gpr[3] = self + kLayout;
    InvokeDirectCpu<0x800C9500u>(ctx);
}
bool IsAnime(CpuContext* ctx, uint32_t self, uint32_t anim) {
    ctx->gpr[3] = self + kLayout;
    ctx->gpr[4] = anim;
    InvokeDirectCpu<0x800C9610u>(ctx);
    return (ctx->gpr[3] & 0xFF) != 0;
}
void ChangeState(CpuContext* ctx, uint32_t self, uint32_t state) {
    const uint32_t mgr = self + kStateMgr;
    const uint32_t fn = R32(R32(mgr) + 0x18);
    ctx->gpr[3] = mgr;
    ctx->gpr[4] = state;
    InvokeIndirectCpu(fn, ctx);
}
void SystemSe(CpuContext* ctx, uint32_t sound) {
    ctx->gpr[3] = R32(kSndAudioMgr);
    ctx->gpr[4] = sound;
    ctx->gpr[5] = 1;
    InvokeDirectCpu<0x80195380u>(ctx);
}
uint32_t DateFile(uint32_t self, int32_t i) { return R32(self + kDateFiles + 4u * uint32_t(i)); }

void WriteString(uint32_t addr, const char* s) {
    do {
        MemoryInline::FlatWrite8(addr++, static_cast<uint8_t>(*s));
    } while (*s++ != '\0');
}

// createLayout's GroupRegister(&mLayout, names, indices, 45), with r3..r6 as the copy set them.
// The extended tables only live for the call (AnmGroupBase_c::create compares the names and keeps
// nothing - nsmbw_pause_options.cpp relies on the same), so they go on the guest stack below the
// caller's frame, with the stack pointer lowered past them for the callee.
void RegisterGroupsWithMods(CpuContext* ctx) {
    g_modsWindow = 0;
    g_waitingForMenu = false;
    if (!NsmbwFileSelectHasMods() || ctx->gpr[6] != kOriginalBindings) {
        InvokeDirectCpu<0x800C90F0u>(ctx);
        return;
    }
    const uint32_t lyt = ctx->gpr[3], names = ctx->gpr[4], indices = ctx->gpr[5], count = ctx->gpr[6];
    const uint32_t sp = ctx->gpr[1];
    const uint32_t scratch = (sp - 0x300u) & ~0x1Fu;
    const uint32_t nameTable = scratch, indexTable = scratch + 0x100, groupName = scratch + 0x200;
    WriteString(groupName, "F02_mods");
    for (uint32_t i = 0; i < count; ++i) {
        MemoryInline::FlatWrite32(nameTable + 4 * i, R32(names + 4 * i));
        MemoryInline::FlatWrite32(indexTable + 4 * i, R32(indices + 4 * i));
    }
    for (uint32_t i = 0; i < 6; ++i) {
        MemoryInline::FlatWrite32(nameTable + 4 * (count + i), groupName);
        MemoryInline::FlatWrite32(indexTable + 4 * (count + i), kModsAnimResources[i]);
    }
    const uint32_t calleeSp = scratch - 0x40u;
    MemoryInline::FlatWrite32(calleeSp, sp); // back chain
    ctx->gpr[1] = calleeSp;
    ctx->gpr[3] = lyt;
    ctx->gpr[4] = nameTable;
    ctx->gpr[5] = indexTable;
    ctx->gpr[6] = count + 6;
    InvokeDirectCpu<0x800C90F0u>(ctx);
    ctx->gpr[1] = sp;
}

// After createLayout's WPaneRegister: finds W_multiMods_00 the same way, for SelectCursorSetup.
void FindModsWindow(CpuContext* ctx, uint32_t self) {
    if (!NsmbwFileSelectHasMods()) return;
    const uint32_t sp = ctx->gpr[1];
    const uint32_t scratch = (sp - 0x100u) & ~0x1Fu;
    const uint32_t name = scratch, table = scratch + 0x20, out = scratch + 0x24;
    WriteString(name, "W_multiMods_00");
    MemoryInline::FlatWrite32(table, name);
    MemoryInline::FlatWrite32(out, 0);
    const uint32_t calleeSp = scratch - 0x40u;
    MemoryInline::FlatWrite32(calleeSp, sp);
    ctx->gpr[1] = calleeSp;
    ctx->gpr[3] = self + kLayout;
    ctx->gpr[4] = table;
    ctx->gpr[5] = out;
    ctx->gpr[6] = 1;
    InvokeDirectCpu<0x800C8DD0u>(ctx);
    ctx->gpr[1] = sp;
    g_modsWindow = R32(out);
}

// SelectWait's navigation with the Mods button (EDIT in the copy below). trig is the remote's
// newly pressed buttons as the original reads them: 1 down, 2 up, 4 right, 8 left (sideways remote).
// Returns the new choice, or -1 after it has already changed state (up from the bottom row).
int32_t Navigate(CpuContext* ctx, uint32_t self, int32_t choice, uint32_t trig) {
    if (choice >= 3) {
        if (trig & 2u) { // up: to the file above the button
            MemoryInline::FlatWrite32(self + kPrevChoice, uint32_t(choice));
            MemoryInline::FlatWrite32(self + kChoice, uint32_t(choice - 3));
            ChangeState(ctx, self, kStateButtonSelect);
            return -1;
        }
        if ((trig & 8u) && choice > 3) {
            --choice;
            MemoryInline::FlatWrite32(self + kBottomButton, uint32_t(choice));
        } else if ((trig & 4u) && choice < kMods) {
            ++choice;
            MemoryInline::FlatWrite32(self + kBottomButton, uint32_t(choice));
        }
        return choice;
    }
    if (trig & 1u) return choice + 3; // down: the button under the file
    if ((trig & 8u) && choice > 0) {
        --choice;
        MemoryInline::FlatWrite32(self + kTopColumn, uint32_t(choice));
    } else if ((trig & 4u) && choice < 2) {
        ++choice;
        MemoryInline::FlatWrite32(self + kTopColumn, uint32_t(choice));
    }
    return choice;
}

} // namespace

// ---- States written natively ----------------------------------------------------------------------

extern "C" void NsmbwFileSelect_StartWaitInit(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    AllAnimeEnd(ctx, self);
    for (const uint32_t anim : {0x1Fu, 0x20u, 0x21u, 0x02u, 0x01u, 0x0Fu, kInFree, kInCoin}) AnimeReverse(ctx, self, anim);
    if (NsmbwFileSelectHasMods()) AnimeReverse(ctx, self, kInMods);
    AnimeReverse(ctx, self, 0x00u);
    for (int32_t i = 0; i < 3; ++i) {
        MemoryInline::FlatWrite8(DateFile(self, i) + 0xE49, 0);
        MemoryInline::FlatWrite8(DateFile(self, i) + 0xE4C, 1);
        MemoryInline::FlatWrite8(DateFile(self, i) + 0xE50, 1);
    }
    MemoryInline::FlatWrite8(self + 0x2DB, 0);
    MemoryInline::FlatWrite8(self + 0x2DC, 0);
    MemoryInline::FlatWrite32(self + 0x2C4, 2);
    const uint32_t root = R32(self + kRootPane);
    MemoryInline::FlatWrite8(root + 0xBB, MemoryInline::FlatRead8(root + 0xBB) | 1u); // visible
    ctx->gpr[3] = self + kLayout;
    InvokeDirectCpu<0x800C9560u>(ctx);
    // mLayout's vtable slot +0x10 (its calc), as the original calls it.
    ctx->gpr[3] = self + kLayout;
    InvokeIndirectCpu(R32(R32(self + 0x7C) + 0x10), ctx);
}

// Hides each save file's "new file" marker pane (dDateFile_c +0xDF4, visibility bit 0 of +0xBB),
// as OpenWait and MultiButtnOnStage do before the bottom row comes in.
static void HideDateFileMarkers(uint32_t self) {
    for (int32_t i = 0; i < 3; ++i) {
        const uint32_t pane = R32(DateFile(self, i) + 0xDF4);
        MemoryInline::FlatWrite8(pane + 0xBB, MemoryInline::FlatRead8(pane + 0xBB) & 0xFEu);
    }
}

extern "C" void NsmbwFileSelect_OpenWaitInit(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    AnimeStart(ctx, self, 0x00u);
    HideDateFileMarkers(self);
    AnimeStart(ctx, self, kInFree);
    AnimeStart(ctx, self, kInCoin);
    if (NsmbwFileSelectHasMods()) AnimeStart(ctx, self, kInMods);
}

extern "C" void NsmbwFileSelect_MultiOnStageInit(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    HideDateFileMarkers(self);
    AnimeStart(ctx, self, kInFree);
    AnimeStart(ctx, self, kInCoin);
    if (NsmbwFileSelectHasMods()) AnimeStart(ctx, self, kInMods);
}

extern "C" void NsmbwFileSelect_ButtonSelectInit(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    const int32_t prev = S32(self + kPrevChoice);
    bool ended = false;
    if (prev >= 0) {
        if (prev >= 3) {
            AllAnimeEnd(ctx, self);
            ended = true;
            AnimeStart(ctx, self, OffAnim(prev));
        } else {
            MemoryInline::FlatWrite32(DateFile(self, prev) + 0xDD8, 4);
        }
        SystemSe(ctx, 0x78); // SE_SYS_CURSOR
    }
    const int32_t choice = S32(self + kChoice);
    if (choice >= 3) {
        if (!ended) AllAnimeEnd(ctx, self);
        AnimeStart(ctx, self, OnAnim(choice));
        if (S32(self + kPrevChoice) < 0) MemoryInline::FlatWrite32(self + kPrevChoice, 0);
    } else {
        MemoryInline::FlatWrite32(DateFile(self, choice) + 0xDD8, 1);
    }
}

extern "C" void NsmbwFileSelect_ButtonSelectFinalize(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    const int32_t choice = S32(self + kChoice);
    if (choice < 3) return;
    const uint32_t window = choice == 3 ? R32(self + kWindowFree) : choice == 4 ? R32(self + kWindowCoin) : g_modsWindow;
    if (window == 0) return;
    ctx->gpr[3] = window;
    ctx->gpr[4] = 1;
    ctx->gpr[5] = 0;
    InvokeDirectCpu<0x800B44D0u>(ctx);
}

extern "C" void NsmbwFileSelect_ButtonHitInit(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    const int32_t choice = S32(self + kChoice);
    if (choice >= 3) AnimeStart(ctx, self, HitAnim(choice));
    else MemoryInline::FlatWrite32(DateFile(self, choice) + 0xDD8, 2);
    SystemSe(ctx, 0x79); // SE_SYS_DECIDE
}

extern "C" void NsmbwFileSelect_ButtonHitExecute(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    const int32_t choice = S32(self + kChoice);
    if (choice >= 3) {
        if (IsAnime(ctx, self, 0xFFFFFFFFu)) return;
    } else if (MemoryInline::FlatRead8(DateFile(self, choice) + 0xE4D) != 0) {
        return;
    }
    if (choice != kMods) {
        ChangeState(ctx, self, kStateGameStart);
        return;
    }
    // Mods: the menu opens over the frozen file select (it stops the game's processes while open,
    // so this state does not run again until it has closed).
    if (!g_waitingForMenu) {
        g_waitingForMenu = true;
        NsmbwModsMenuRequestOpen();
        return;
    }
    if (!NsmbwModsMenuTakeClosed()) return;
    g_waitingForMenu = false;
    // Back to the button as the game comes back to one: no "off" for a previous button, Mods' "on"
    // animation, then the cursor frame on it (ButtonSelectAnimeEndWait's finalize).
    MemoryInline::FlatWrite32(self + kPrevChoice, 0xFFFFFFFFu);
    ChangeState(ctx, self, kStateButtonSelect);
}

extern "C" void NsmbwFileSelect_MultiExitInit(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    AnimeStart(ctx, self, kOutFree);
    AnimeStart(ctx, self, kOutCoin);
    if (NsmbwFileSelectHasMods()) AnimeStart(ctx, self, kOutMods);
}

PPC_NATIVE_OVERRIDE_VOID(807835C0, NsmbwFileSelect_StartWaitInit, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(807837D0, NsmbwFileSelect_OpenWaitInit, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80783930, NsmbwFileSelect_MultiOnStageInit, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80783A40, NsmbwFileSelect_ButtonSelectInit, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80783BF0, NsmbwFileSelect_ButtonSelectFinalize, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80783FF0, NsmbwFileSelect_ButtonHitInit, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80784070, NsmbwFileSelect_ButtonHitExecute, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80784240, NsmbwFileSelect_MultiExitInit, (CpuContext* ctx), (ctx));

// ---- Structural copies of translated bodies ---------------------------------------------------------
// From ports/wiicompiled/build/nsmbw_us-d_basesNP/functions/func_<addr>.cpp; only the lines marked
// EDIT differ.

extern "C" void NsmbwFileSelect_CreateLayout(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80782EC0;

loc_80782EC0:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r5 = 0;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = 0x80940000u;
    r31 = (r31 + 13856);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    r4 = (r31 + 1920);
    r3 = (r3 + 116);
    ctx->lr = 0x80782EF0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8CB0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_80782EF4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80782F00;
    }
}

loc_80782EF8:
{
    r3 = 0;
    goto loc_80782FE0;
}

loc_80782F00:
{
    r12 = MemoryInline::FlatRead32((r30 + 124));
    r3 = (r30 + 116);
    r4 = (r31 + 1960);
    r5 = 0;
    r12 = MemoryInline::FlatRead32((r12 + 20));
    ctr = r12;
    ctx->lr = 0x80782F1Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 116);
    r4 = (r31 + 1096);
    r5 = 29;
    ctx->lr = 0x80782F2Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8FB0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r5 = 0x80930000u;
    r3 = (r30 + 116);
    r4 = (r31 + 1348);
    r6 = 45;
    r5 = (r5 + 16788);
    ctx->lr = 0x80782F44u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    RegisterGroupsWithMods(ctx); // EDIT: + the F02_mods bindings
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 116);
    // inline leaf 0x80007220 (2 guest instruction(s))
    r3 = MemoryInline::FlatRead32((r3 + 32));
    // end of inlined leaf 0x80007220
    MemoryInline::FlatWrite32((r30 + 616), r3);
    r3 = (r30 + 116);
    r4 = (r31 + 1596);
    r5 = (r30 + 620);
    r6 = 5;
    ctx->lr = 0x80782F64u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8D60u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r5 = 0x80930000u;
    r3 = (r30 + 116);
    r4 = (r31 + 1744);
    r6 = 50;
    r5 = (r5 + 16968);
    r7 = 11;
    ctx->lr = 0x80782F80u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8F20u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 116);
    r4 = (r31 + 1824);
    r5 = (r30 + 640);
    r6 = 3;
    ctx->lr = 0x80782F94u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8EB0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    // inline leaf 0x800CDC60 (4 guest instruction(s))
    r3 = 0x80360000u;
    r3 = (r3 + -25444);
    r3 = MemoryInline::FlatRead32((r3 + 24));
    // end of inlined leaf 0x800CDC60
    r4 = r3;
    r3 = MemoryInline::FlatRead32((r30 + 648));
    r5 = 2;
    r6 = 29;
    r7 = 0;
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(2), static_cast<uint32_t>(6), static_cast<uint32_t>(6), static_cast<uint32_t>(6));
    ctx->lr = 0x80782FB4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C9A60u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 116);
    r4 = (r31 + 1864);
    r5 = (r30 + 652);
    r6 = 2;
    ctx->lr = 0x80782FC8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8E40u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 116);
    r4 = (r31 + 1912);
    r5 = (r30 + 660);
    r6 = 2;
    ctx->lr = 0x80782FDCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8DD0u>(ctx);
    FindModsWindow(ctx, ctx->gpr[30]); // EDIT: W_multiMods_00 (r30 = this)
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = 1;
}

loc_80782FE0:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r30 = MemoryInline::FlatRead32((r1 + 8));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

extern "C" void NsmbwFileSelect_SelectWaitExecute(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t cr1_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r0_rot_5 = 0;
    uint32_t r0_rot_6 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80783C50;

loc_80783C50:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    r4 = 0x80430000u;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    r31 = 0x80990000u;
    r31 = (r31 + 8032);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    MemoryInline::FlatWriteRam32((r1 + 16), r28);
    r28 = r3;
    r29 = MemoryInline::FlatRead32((r3 + 676));
    r30 = MemoryInline::FlatRead32((r4 + -24752));
    SetCRResident(cr, xer, 1, static_cast<int32_t>(r29), static_cast<int32_t>(3));
    // EDIT: with the Mods button the bottom row has three buttons, one under each file (Navigate).
    if (NsmbwFileSelectHasMods()) {
        const uint32_t remocon = MemoryInline::FlatRead32(r30 + 4 + 4 * MemoryInline::FlatRead32(0x80430000u + -23452));
        const uint32_t trig = MemoryInline::FlatRead32(remocon + 28);
        ctx->gpr[1] = r1; // this function's frame is below the caller's: callees go below it
        ctx->gpr[28] = r28;
        ctx->gpr[29] = r29;
        ctx->gpr[30] = r30;
        ctx->gpr[31] = r31;
        const int32_t next = Navigate(ctx, r3, static_cast<int32_t>(r29), trig);
        if (next < 0) goto loc_80783F98; // moved up and already changed state, as the original does
        r29 = static_cast<uint32_t>(next);
        goto loc_80783D94;
    }
}

loc_80783C88:
{
    if (((cr & 0x08000000u) != 0)) {
        goto loc_80783D28;
    }
}

loc_80783C8C:
{
    r4 = 0x80430000u;
    r0 = MemoryInline::FlatRead32((r4 + -23452));
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_0 & -4);
    r4 = (r30 + r0);
    r4 = MemoryInline::FlatRead32((r4 + 4));
    r4 = MemoryInline::FlatRead32((r4 + 28));
    r0 = (r4 & 2);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80783CA8:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80783CF4;
    }
}

loc_80783CAC:
{
    MemoryInline::FlatWrite32((r3 + 680), r29);
    if (((cr & 0x02000000u) == 0)) {
        goto loc_80783CD4;
    }
}

loc_80783CB4:
{
    r0 = MemoryInline::FlatRead32((r3 + 716));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(1));
}

loc_80783CBC:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80783CC8;
    }
}

loc_80783CC0:
{
    MemoryInline::FlatWrite32((r3 + 676), r0);
    goto loc_80783CDC;
}

loc_80783CC8:
{
    r0 = 0;
    MemoryInline::FlatWrite32((r3 + 676), r0);
    goto loc_80783CDC;
}

loc_80783CD4:
{
    r0 = 2;
    MemoryInline::FlatWrite32((r3 + 676), r0);
}

loc_80783CDC:
{
    r3 = (r3 + 524);
    r12 = MemoryInline::FlatRead32(r3);
    r4 = (r31 + 208);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x80783CF0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783F98;
}

loc_80783CF4:
{
}

loc_80783CF8:
{
    if ((static_cast<int32_t>(r29) != static_cast<int32_t>(4))) {
        goto loc_80783D10;
    }
}

loc_80783CFC:
{
    r0 = (r4 & 8);
}

loc_80783D00:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80783D94;
    }
}

loc_80783D04:
{
    r29 = 3;
    MemoryInline::FlatWrite32((r3 + 720), r29);
    goto loc_80783D94;
}

loc_80783D10:
{
    if (((cr & 0x02000000u) == 0)) {
        goto loc_80783D94;
    }
}

loc_80783D14:
{
    r0 = (r4 & 4);
}

loc_80783D18:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80783D94;
    }
}

loc_80783D1C:
{
    r29 = 4;
    MemoryInline::FlatWrite32((r3 + 720), r29);
    goto loc_80783D94;
}

loc_80783D28:
{
    r4 = 0x80430000u;
    r0 = MemoryInline::FlatRead32((r4 + -23452));
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r4 = (r30 + r0);
    r4 = MemoryInline::FlatRead32((r4 + 4));
    r4 = MemoryInline::FlatRead32((r4 + 28));
    r0 = (r4 & 1);
}

loc_80783D44:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80783D60;
    }
}

loc_80783D48:
{
}

loc_80783D4C:
{
    if ((static_cast<int32_t>(r29) > static_cast<int32_t>(1))) {
        goto loc_80783D58;
    }
}

loc_80783D50:
{
    r29 = 3;
    goto loc_80783D94;
}

loc_80783D58:
{
    r29 = 4;
    goto loc_80783D94;
}

loc_80783D60:
{
    r0 = (r4 & 8);
}

loc_80783D64:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80783D7C;
    }
}

loc_80783D68:
{
}

loc_80783D6C:
{
    if ((static_cast<int32_t>(r29) <= static_cast<int32_t>(0))) {
        goto loc_80783D94;
    }
}

loc_80783D70:
{
    r29 = (r29 + -1);
    MemoryInline::FlatWrite32((r3 + 716), r29);
    goto loc_80783D94;
}

loc_80783D7C:
{
    r0 = (r4 & 4);
}

loc_80783D80:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80783D94;
    }
}

loc_80783D84:
{
}

loc_80783D88:
{
    if ((static_cast<int32_t>(r29) >= static_cast<int32_t>(2))) {
        goto loc_80783D94;
    }
}

loc_80783D8C:
{
    r29 = (r29 + 1);
    MemoryInline::FlatWrite32((r3 + 716), r29);
}

loc_80783D94:
{
    r0 = MemoryInline::FlatRead32((r3 + 676));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r29), static_cast<int32_t>(r0));
}

loc_80783D9C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80783DC0;
    }
}

loc_80783DA0:
{
    MemoryInline::FlatWrite32((r3 + 680), r0);
    r4 = (r31 + 208);
    MemoryInline::FlatWrite32((r3 + 676), r29);
    r3 = (r3 + 524);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x80783DBCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783F98;
}

loc_80783DC0:
{
    r3 = 0;
    ctx->lr = 0x80783DC8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800B53A0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_80783DCC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80783E00;
    }
}

loc_80783DD0:
{
    r3 = 0x80430000u;
    r4 = 122;
    r3 = MemoryInline::FlatRead32((r3 + -23416));
    r5 = 1;
    ctx->lr = 0x80783DE4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x80195380u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r12 = MemoryInline::FlatRead32((r28 + 524));
    r3 = (r28 + 524);
    r4 = (r31 + 400);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x80783DFCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783F98;
}

loc_80783E00:
{
    r3 = 0x80430000u;
    r4 = 2304;
    r3 = MemoryInline::FlatRead32((r3 + -23448));
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 32));
    ctr = r12;
    ctx->lr = 0x80783E1Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80783E20:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_80783E60;
    }
}

loc_80783E24:
{
    r0 = MemoryInline::FlatRead32((r28 + 676));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_80783E2C:
{
    if (((cr & 0x80000000u) == 0)) {
        goto loc_80783E44;
    }
}

loc_80783E30:
{
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_3 & -4);
    r4 = 1;
    r3 = (r28 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 588));
    MemoryInline::FlatWrite8((r3 + 3662), static_cast<uint8_t>(r4));
}

loc_80783E44:
{
    r12 = MemoryInline::FlatRead32((r28 + 524));
    r3 = (r28 + 524);
    r4 = (r31 + 336);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x80783E5Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783F98;
}

loc_80783E60:
{
    r3 = 0x80430000u;
    r0 = MemoryInline::FlatRead32((r3 + -23452));
    r0_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_5 & -4);
    r3 = (r30 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 4));
    r3 = MemoryInline::FlatRead32((r3 + 28));
    r0 = (r3 & 16);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80783E7C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80783F0C;
    }
}

loc_80783E80:
{
    r3 = r28;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    InvokeDirectCpu<0x80783210u>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(3));
}

loc_80783E8C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80783EF4;
    }
}

loc_80783E90:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r29), static_cast<int32_t>(3));
}

loc_80783E94:
{
    r0 = 0;
    MemoryInline::FlatWrite8((r28 + 730), static_cast<uint8_t>(r0));
    MemoryInline::FlatWrite32((r28 + 604), r0);
    MemoryInline::FlatWrite32((r28 + 684), r3);
    if (((cr & 0x80000000u) != 0)) {
        goto loc_80783EC4;
    }
}

loc_80783EA8:
{
    r12 = MemoryInline::FlatRead32((r28 + 524));
    r3 = (r28 + 524);
    r4 = (r31 + 592);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x80783EC0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783EDC;
}

loc_80783EC4:
{
    r12 = MemoryInline::FlatRead32((r28 + 524));
    r3 = (r28 + 524);
    r4 = (r31 + 528);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x80783EDCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80783EDC:
{
    r3 = 0x80430000u;
    r4 = 160;
    r3 = MemoryInline::FlatRead32((r3 + -23416));
    r5 = 1;
    ctx->lr = 0x80783EF0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x80195380u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783F98;
}

loc_80783EF4:
{
    r3 = 0x80430000u;
    r4 = 123;
    r3 = MemoryInline::FlatRead32((r3 + -23416));
    r5 = 1;
    ctx->lr = 0x80783F08u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x80195380u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783F98;
}

loc_80783F0C:
{
    r0 = (r3 & 4096);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80783F10:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80783F98;
    }
}

loc_80783F14:
{
    r3 = r28;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    InvokeDirectCpu<0x80783210u>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(3));
}

loc_80783F20:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80783F84;
    }
}

loc_80783F24:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r29), static_cast<int32_t>(3));
}

loc_80783F28:
{
    r0 = 1;
    MemoryInline::FlatWrite32((r28 + 604), r0);
    MemoryInline::FlatWrite32((r28 + 684), r3);
    if (((cr & 0x80000000u) != 0)) {
        goto loc_80783F54;
    }
}

loc_80783F38:
{
    r12 = MemoryInline::FlatRead32((r28 + 524));
    r3 = (r28 + 524);
    r4 = (r31 + 592);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x80783F50u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783F6C;
}

loc_80783F54:
{
    r12 = MemoryInline::FlatRead32((r28 + 524));
    r3 = (r28 + 524);
    r4 = (r31 + 528);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x80783F6Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80783F6C:
{
    r3 = 0x80430000u;
    r4 = 161;
    r3 = MemoryInline::FlatRead32((r3 + -23416));
    r5 = 1;
    ctx->lr = 0x80783F80u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x80195380u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80783F98;
}

loc_80783F84:
{
    r3 = 0x80430000u;
    r4 = 123;
    r3 = MemoryInline::FlatRead32((r3 + -23416));
    r5 = 1;
    ctx->lr = 0x80783F98u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x80195380u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80783F98:
{
    r0 = MemoryInline::FlatRead32((r1 + 36));
    r31 = MemoryInline::FlatRead32((r1 + 28));
    r30 = MemoryInline::FlatRead32((r1 + 24));
    r29 = MemoryInline::FlatRead32((r1 + 20));
    r28 = MemoryInline::FlatRead32((r1 + 16));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(80782EC0, NsmbwFileSelect_CreateLayout, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80783C50, NsmbwFileSelect_SelectWaitExecute, (CpuContext* ctx), (ctx));
