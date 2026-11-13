/* IOS (Starlet) stand-in: the IPC mailbox registers and the IOS devices the
   game opens. Requests are 0x40-byte structures at a physical address:
     0x00 command  0x04 result  0x08 fd  0x0C.. arguments
   Commands: 1 open, 2 close, 3 read, 4 write, 5 seek, 6 ioctl, 7 ioctlv.
   A reply stores the result, sets command to 8 and the old command at 0x08. */
#include "host.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

void pi_raise(u32 cause);
void pi_clear(u32 cause);
u32 mmio_get32(u32 ea);
void mmio_set32(u32 ea, u32 v);

extern const char* g_disc_root;
const char* g_nand_root = "nand";

#define IPC_PPCMSG  0xCD000000u
#define IPC_PPCCTRL 0xCD000004u
#define IPC_ARMMSG  0xCD000008u
#define HW_PPCIRQFLAG 0xCD000030u
#define HW_PPCIRQMASK 0xCD000034u

#define CTRL_X1 0x01u
#define CTRL_Y2 0x02u
#define CTRL_Y1 0x04u
#define CTRL_X2 0x08u
#define CTRL_IY1 0x10u
#define CTRL_IY2 0x20u

#define PI_CAUSE_IPC 0x4000u

enum { IOS_OK = 0, IOS_EACCES = -1, IOS_EEXIST = -2, IOS_EINVAL = -4, IOS_ENOENT = -6,
       FS_EEXIST = -105, FS_ENOENT = -106 };

static u32 ctrl;
u64 g_di_reads, g_ipc_requests;

/* ---------------- interrupts ---------------- */

static void ipc_update_irq(void) {
    int on = ((ctrl & CTRL_Y1) && (ctrl & CTRL_IY1)) || ((ctrl & CTRL_Y2) && (ctrl & CTRL_IY2));
    if (on) mmio_set32(HW_PPCIRQFLAG, mmio_get32(HW_PPCIRQFLAG) | 0x40000000u);
    if (mmio_get32(HW_PPCIRQFLAG) & mmio_get32(HW_PPCIRQMASK)) pi_raise(PI_CAUSE_IPC);
    else pi_clear(PI_CAUSE_IPC);
}

/* ---------------- reply queue ---------------- */

#define QMAX 64
static u32 reply_q[QMAX];
static int q_head, q_count;

static void queue_reply(u32 addr) {
    if (q_count == QMAX) { LOG("ios: reply queue full\n"); return; }
    reply_q[(q_head + q_count++) % QMAX] = addr;
}

/* ---------------- guest memory helpers ---------------- */

static CPUState* g_cpu;
static u32 v(u32 phys) { return phys ? (phys | 0x80000000u) : 0; }  /* physical -> cached */
static u32 r32(u32 phys) { return mem_read32(g_cpu, v(phys)); }
static void w32(u32 phys, u32 val) { mem_write32(g_cpu, v(phys), val); }

static void read_str(u32 phys, char* out, size_t cap) {
    size_t i = 0;
    for (; i + 1 < cap; i++) {
        u8 c = mem_read8(g_cpu, v(phys) + (u32)i);
        if (!c) break;
        out[i] = (char)c;
    }
    out[i] = 0;
}

static void copy_out(u32 phys, const void* src, u32 len) {
    const u8* s = src;
    for (u32 i = 0; i < len; i++) mem_write8(g_cpu, v(phys) + i, s[i]);
}

static void copy_in(void* dst, u32 phys, u32 len) {
    u8* d = dst;
    for (u32 i = 0; i < len; i++) d[i] = mem_read8(g_cpu, v(phys) + i);
}

/* ---------------- devices ---------------- */

typedef struct Device Device;
typedef struct {
    u32 cmd, fd, a[5];
    u32 addr;
    int no_reply;  /* set by handlers that answer later (or never) */
} Req;

struct Device {
    const char* name;
    s32 (*open)(Req* r, const char* path, u32 mode);
    s32 (*ioctl)(Req* r, u32 num, u32 in, u32 in_len, u32 out, u32 out_len);
    s32 (*ioctlv)(Req* r, u32 num, u32 n_in, u32 n_io, u32 vec);
    s32 (*read)(Req* r, u32 buf, u32 len);
    s32 (*write)(Req* r, u32 buf, u32 len);
    s32 (*seek)(Req* r, s32 where, u32 whence);
    void (*close)(Req* r);
};

#define MAX_FD 32
static struct {
    const Device* dev;
    char path[256];
    FILE* file;  /* NAND files */
    u32 mode;
} fds[MAX_FD];

/* --- /dev/di: disc ---------------------------------------------------- */

typedef struct { u64 off; u32 size; char path[512]; } DiscFile;
static DiscFile* disc_files;
static int disc_file_count;
static u32 dol_off, fst_off, fst_size;

static void disc_build_map(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/sys/boot.bin", g_disc_root);
    FILE* f = fopen(path, "rb");
    u8 hdr[0x440];
    if (!f || fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { LOG("ios: no boot.bin\n"); if (f) fclose(f); return; }
    fclose(f);
#define BE(p) ((u32)(p)[0] << 24 | (u32)(p)[1] << 16 | (u32)(p)[2] << 8 | (p)[3])
    dol_off = BE(hdr + 0x420) << 2;
    fst_off = BE(hdr + 0x424) << 2;
    fst_size = BE(hdr + 0x428) << 2;

    snprintf(path, sizeof(path), "%s/sys/fst.bin", g_disc_root);
    f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* fst = malloc((size_t)n);
    if (fread(fst, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(fst); return; }
    fclose(f);
    u32 count = BE(fst + 8);
    const char* names = (const char*)fst + count * 12;
    disc_files = calloc(count, sizeof(DiscFile));
    /* Walk the tree keeping the directory stack to build host paths. */
    u32 dir_end[64];
    char dir_path[64][512];
    int depth = 0;
    dir_end[0] = count;
    snprintf(dir_path[0], sizeof(dir_path[0]), "%s/files", g_disc_root);
    for (u32 i = 1; i < count; i++) {
        while (depth > 0 && i >= dir_end[depth]) depth--;
        const u8* e = fst + i * 12;
        const char* name = names + (BE(e) & 0xFFFFFF);
        if (e[0]) {  /* directory */
            depth++;
            dir_end[depth] = BE(e + 8);
            snprintf(dir_path[depth], sizeof(dir_path[depth]), "%s/%s", dir_path[depth - 1], name);
        } else {
            DiscFile* d = &disc_files[disc_file_count++];
            d->off = (u64)BE(e + 4) << 2;
            d->size = BE(e + 8);
            snprintf(d->path, sizeof(d->path), "%s/%s", dir_path[depth], name);
        }
    }
    free(fst);
    LOG("ios: disc map: %d files, dol @%X, fst @%X\n", disc_file_count, dol_off, fst_off);
#undef BE
}

static int read_host_file(const char* path, u64 off, u8* dst, u32 len) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseeko(f, (off_t)off, SEEK_SET);
    size_t got = fread(dst, 1, len, f);
    fclose(f);
    if (got < len) memset(dst + got, 0, len - got);
    return 1;
}

/* Read from the data partition at a byte offset. */
static int disc_read(u64 off, u8* dst, u32 len) {
    char path[1024];
    struct { u64 start; u64 size; const char* file; } sys[] = {
        {0, 0x440, "boot.bin"}, {0x440, 0x2000, "bi2.bin"}, {0x2440, 0, "apploader.img"},
        {dol_off, 0, "main.dol"}, {fst_off, fst_size, "fst.bin"},
    };
    for (unsigned i = 0; i < sizeof(sys) / sizeof(sys[0]); i++) {
        snprintf(path, sizeof(path), "%s/sys/%s", g_disc_root, sys[i].file);
        u64 size = sys[i].size;
        if (!size) { struct stat st; if (stat(path, &st) == 0) size = (u64)st.st_size; }
        if (off >= sys[i].start && off < sys[i].start + size)
            return read_host_file(path, off - sys[i].start, dst, len);
    }
    for (int i = 0; i < disc_file_count; i++) {
        DiscFile* d = &disc_files[i];
        if (off >= d->off && off < d->off + d->size) {
            if (g_verbose) LOG("ios: di read %s +%llX len %X\n", d->path, (unsigned long long)(off - d->off), len);
            return read_host_file(d->path, off - d->off, dst, len);
        }
    }
    LOG("ios: di read at %llX len %X: nothing there\n", (unsigned long long)off, len);
    memset(dst, 0, len);
    return 1;
}

#define DI_OK 1

static s32 di_ioctl(Req* r, u32 num, u32 in, u32 in_len, u32 out, u32 out_len) {
    (void)r; (void)in_len;
    u32 cmd = r32(in) >> 24;
    LOG("ios: di cmd %02X args %08X %08X %08X out %X/%X\n", cmd, r32(in + 4), r32(in + 8), r32(in + 12), out, out_len);
    switch (cmd) {
    case 0x12: {  /* inquiry */
        u8 buf[0x20] = {0};
        buf[2] = 0x20; buf[3] = 0x02; buf[4] = 0x02; buf[5] = 0x04; buf[6] = 0x02; buf[7] = 0x01;
        copy_out(out, buf, out_len < 0x20 ? out_len : 0x20);
        return DI_OK;
    }
    case 0x70: {  /* read disc ID */
        u8 buf[0x20];
        disc_read(0, buf, 0x20);
        copy_out(out, buf, 0x20);
        return DI_OK;
    }
    case 0x71: {  /* read (partition offset >> 2) */
        u32 len = r32(in + 4);
        u64 off = (u64)r32(in + 8) << 2;
        u8* buf = malloc(len ? len : 1);
        g_di_reads++;
        disc_read(off, buf, len);
        copy_out(out, buf, len);
        free(buf);
        return DI_OK;
    }
    case 0x7A:  /* prepare cover register (DICVR): closed */
    case 0x88:  /* cover status: closed, disc in */
    case 0x95:  /* status register */
    case 0x96:  /* control register */
    case 0xE0:  /* request error: none */
        if (out && out_len >= 4) w32(out, cmd == 0x88 ? 2 : 0);
        return DI_OK;
    case 0xDA: {  /* read BCA. NSMBW's copy check wants 0x33 zero bytes then a 1
                     (as documented in Dolphin's DI emulation). */
        u8 bca[0x40] = {0};
        bca[0x33] = 1;
        copy_out(out, bca, out_len < sizeof(bca) ? out_len : (u32)sizeof(bca));
        return DI_OK;
    }
    case 0x7E: /* notify reset */ case 0x86: /* clear cover irq */ case 0x89: /* enable cover irq */
    case 0x8A: /* reset */ case 0x8E: /* DVD video */ case 0xE3: /* stop motor */
    case 0xE4: /* audio buffer config */ case 0xAB: /* seek */
        return DI_OK;
    }
    LOG("ios: di ioctl %02X (num %X) unhandled\n", cmd, num);
    return DI_OK;
}

static s32 di_ioctlv(Req* r, u32 num, u32 n_in, u32 n_io, u32 vec) {
    (void)r; (void)n_in; (void)n_io;
    u32 in = r32(vec);
    u32 cmd = r32(in) >> 24;
    LOG("ios: di ioctlv cmd %02X (in %u io %u)\n", cmd, n_in, n_io);
    if (cmd == 0x8B) {  /* open partition: out[0] = TMD */
        LOG("ios: di open partition\n");
        if (n_io >= 1) {
            u32 tmd_out = r32(vec + 8 * n_in);
            u32 tmd_len = r32(vec + 8 * n_in + 4);
            char path[1024];
            snprintf(path, sizeof(path), "%s/tmd.bin", g_disc_root);
            u8* buf = calloc(1, tmd_len);
            read_host_file(path, 0, buf, tmd_len);
            copy_out(tmd_out, buf, tmd_len);
            free(buf);
        }
        return DI_OK;
    }
    LOG("ios: di ioctlv %02X (num %X) unhandled\n", cmd, num);
    return DI_OK;
}

static const Device dev_di = { "/dev/di", NULL, di_ioctl, di_ioctlv, NULL, NULL, NULL, NULL };

/* --- /dev/es: titles and tickets ------------------------------------------- */

static const u64 TITLE_ID = 0x00010000534D4E45ull;  /* SMNE */

static s32 es_ioctlv(Req* r, u32 num, u32 n_in, u32 n_io, u32 vec) {
    (void)r;
    u32 out0 = n_io ? r32(vec + 8 * n_in) : 0;
    u32 out0_len = n_io ? r32(vec + 8 * n_in + 4) : 0;
    switch (num) {
    case 0x20:  /* GetTitleID */
        w32(out0, (u32)(TITLE_ID >> 32));
        w32(out0 + 4, (u32)TITLE_ID);
        return IOS_OK;
    case 0x1D: {  /* GetDataDir */
        char dir[64];
        snprintf(dir, sizeof(dir), "/title/%08x/%08x/data", (u32)(TITLE_ID >> 32), (u32)TITLE_ID);
        copy_out(out0, dir, (u32)strlen(dir) + 1);
        return IOS_OK;
    }
    case 0x12:  /* GetNumTicketViews */
        if (out0_len >= 4) w32(out0, 1);
        return IOS_OK;
    case 0x14:  /* GetTMDViewSize */
        if (out0_len >= 4) w32(out0, 0x5C);
        return IOS_OK;
    }
    LOG("ios: es ioctlv %X unhandled (in %u io %u)\n", num, n_in, n_io);
    return IOS_OK;
}

static const Device dev_es = { "/dev/es", NULL, NULL, es_ioctlv, NULL, NULL, NULL, NULL };

/* --- NAND files and /dev/fs ------------------------------------------------ */

static void nand_host_path(const char* nand, char* out, size_t cap) {
    snprintf(out, cap, "%s%s", g_nand_root, nand);
}

static void mkdirs(const char* path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static s32 fs_ioctl(Req* r, u32 num, u32 in, u32 in_len, u32 out, u32 out_len) {
    (void)r; (void)in_len; (void)out_len;
    char path[64], host[1024];
    switch (num) {
    case 0x02:  /* GetStats */
        if (out) for (u32 i = 0; i < out_len; i += 4) w32(out + i, 0);
        return IOS_OK;
    case 0x03:  /* CreateDir: owner(4) gid(2) path(64) ... */
        read_str(in + 6, path, sizeof(path));
        nand_host_path(path, host, sizeof(host));
        mkdirs(host);
        LOG("ios: fs mkdir %s\n", path);
        return IOS_OK;
    case 0x05:  /* SetAttr */
        return IOS_OK;
    case 0x06: {  /* GetAttr: in = path; out = owner, gid, path, perms, attr */
        struct stat st;
        read_str(in, path, sizeof(path));
        nand_host_path(path, host, sizeof(host));
        if (stat(host, &st) != 0) { LOG("ios: fs getattr %s: missing\n", path); return FS_ENOENT; }
        u8 buf[0x4C] = {0};
        memcpy(buf + 6, path, strlen(path));
        buf[0x46] = 3; buf[0x47] = 3; buf[0x48] = 3;  /* rw for all */
        copy_out(out, buf, out_len < sizeof(buf) ? out_len : sizeof(buf));
        return IOS_OK;
    }
    case 0x07:  /* Delete */
        read_str(in, path, sizeof(path));
        nand_host_path(path, host, sizeof(host));
        LOG("ios: fs delete %s\n", path);
        return remove(host) == 0 ? IOS_OK : FS_ENOENT;
    case 0x08: {  /* Rename: two 64-byte paths */
        char path2[64], host2[1024];
        read_str(in, path, sizeof(path));
        read_str(in + 64, path2, sizeof(path2));
        nand_host_path(path, host, sizeof(host));
        nand_host_path(path2, host2, sizeof(host2));
        remove(host2);
        LOG("ios: fs rename %s -> %s\n", path, path2);
        return rename(host, host2) == 0 ? IOS_OK : FS_ENOENT;
    }
    case 0x09: {  /* CreateFile */
        struct stat st;
        read_str(in + 6, path, sizeof(path));
        nand_host_path(path, host, sizeof(host));
        LOG("ios: fs create %s\n", path);
        if (stat(host, &st) == 0) return FS_EEXIST;
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", host);
        char* slash = strrchr(dir, '/');
        if (slash) { *slash = 0; mkdirs(dir); }
        FILE* f = fopen(host, "wb");
        if (!f) return FS_ENOENT;
        fclose(f);
        return IOS_OK;
    }
    case 0x0B:  /* Shutdown */
        return IOS_OK;
    }
    LOG("ios: fs ioctl %X unhandled\n", num);
    return IOS_OK;
}

static s32 fs_ioctlv(Req* r, u32 num, u32 n_in, u32 n_io, u32 vec) {
    (void)r; (void)n_io;
    char path[64], host[1024];
    if (num == 0x04) {  /* ReadDir: in path, in max | out names, out count */
        read_str(r32(vec), path, sizeof(path));
        nand_host_path(path, host, sizeof(host));
        struct stat st;
        if (stat(host, &st) != 0) { LOG("ios: fs readdir %s: missing\n", path); return FS_ENOENT; }
        /* Report an empty directory for now. */
        u32 count_out = n_in == 1 ? r32(vec + 8) : r32(vec + 8 * 3);
        w32(count_out, 0);
        return IOS_OK;
    }
    if (num == 0x0C) {  /* GetUsage: out clusters, out inodes */
        w32(r32(vec + 8), 0);
        w32(r32(vec + 16), 0);
        return IOS_OK;
    }
    LOG("ios: fs ioctlv %X unhandled\n", num);
    return IOS_OK;
}

static const Device dev_fs = { "/dev/fs", NULL, fs_ioctl, fs_ioctlv, NULL, NULL, NULL, NULL };

static s32 file_open(Req* r, const char* path, u32 mode) {
    char host[1024];
    nand_host_path(path, host, sizeof(host));
    FILE* f = fopen(host, mode & 2 ? "r+b" : "rb");
    if (!f) {
        LOG("ios: open %s (mode %u): not found\n", path, mode);
        return FS_ENOENT;
    }
    fds[r->fd].file = f;
    LOG("ios: open %s (mode %u)\n", path, mode);
    return IOS_OK;
}

static s32 file_read(Req* r, u32 buf, u32 len) {
    u8* tmp = malloc(len ? len : 1);
    size_t got = fread(tmp, 1, len, fds[r->fd].file);
    copy_out(buf, tmp, (u32)got);
    free(tmp);
    return (s32)got;
}

static s32 file_write(Req* r, u32 buf, u32 len) {
    u8* tmp = malloc(len ? len : 1);
    copy_in(tmp, buf, len);
    size_t put = fwrite(tmp, 1, len, fds[r->fd].file);
    fflush(fds[r->fd].file);
    free(tmp);
    return (s32)put;
}

static s32 file_seek(Req* r, s32 where, u32 whence) {
    FILE* f = fds[r->fd].file;
    if (fseek(f, where, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END) != 0) return IOS_EINVAL;
    return (s32)ftell(f);
}

static s32 file_ioctl(Req* r, u32 num, u32 in, u32 in_len, u32 out, u32 out_len) {
    (void)in; (void)in_len; (void)out_len;
    if (num == 0x0B) {  /* GetFileStats: length, position */
        FILE* f = fds[r->fd].file;
        long pos = ftell(f);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, pos, SEEK_SET);
        w32(out, (u32)size);
        w32(out + 4, (u32)pos);
        return IOS_OK;
    }
    LOG("ios: file ioctl %X unhandled\n", num);
    return IOS_OK;
}

static void file_close(Req* r) {
    if (fds[r->fd].file) fclose(fds[r->fd].file);
    fds[r->fd].file = NULL;
}

static const Device dev_file = { "file", file_open, file_ioctl, NULL, file_read, file_write, file_seek, file_close };

/* --- /dev/stm: state manager (power/reset buttons) ------------------------- */

static s32 stm_ioctl(Req* r, u32 num, u32 in, u32 in_len, u32 out, u32 out_len) {
    (void)in; (void)in_len; (void)out; (void)out_len;
    if (num == 0x1000) {  /* register event hook: answered on an event only */
        r->no_reply = 1;
        return 0;
    }
    return IOS_OK;
}

static const Device dev_stm = { "/dev/stm", NULL, stm_ioctl, NULL, NULL, NULL, NULL, NULL };

/* --- anything else: accept, log, succeed ---------------------------------- */

static s32 generic_ioctl(Req* r, u32 num, u32 in, u32 in_len, u32 out, u32 out_len) {
    LOG("ios: %s ioctl %X (in %X/%X out %X/%X)\n", fds[r->fd].path, num, in, in_len, out, out_len);
    return IOS_OK;
}

static s32 generic_ioctlv(Req* r, u32 num, u32 n_in, u32 n_io, u32 vec) {
    (void)vec;
    LOG("ios: %s ioctlv %X (in %u io %u)\n", fds[r->fd].path, num, n_in, n_io);
    return IOS_OK;
}

/* --- /dev/usb/oh1/57e/305: Bluetooth (Wii Remotes) -----------------------
   A minimal emulated HCI controller. ioctlv 0 = control message (HCI
   command), 1 = bulk (ACL data), 2 = interrupt (HCI events). Commands are
   answered with Command Complete events delivered on the pending interrupt
   read. No remotes are attached yet, so ACL reads stay pending. */
#define BT_EVQ 64
static u8 bt_events[BT_EVQ][64];
static u8 bt_event_len[BT_EVQ];
static int bt_ev_head, bt_ev_count;
static u32 bt_event_reads[16];  /* pending interrupt-read requests */
static int bt_reads;

static void bt_push_event(const u8* ev, u8 len) {
    if (bt_ev_count == BT_EVQ) return;
    int i = (bt_ev_head + bt_ev_count++) % BT_EVQ;
    memcpy(bt_events[i], ev, len);
    bt_event_len[i] = len;
}

static void bt_command_complete(u16 opcode, const u8* ret, u8 ret_len) {
    u8 ev[64] = {0x0E, (u8)(3 + 1 + ret_len), 1, (u8)opcode, (u8)(opcode >> 8), 0};
    if (ret_len) memcpy(ev + 6, ret, ret_len);
    bt_push_event(ev, (u8)(6 + ret_len));
}

static void bt_command(u16 opcode, const u8* params, u8 plen) {
    (void)params; (void)plen;
    static const u8 bd_addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    switch (opcode) {
    case 0x1005: { /* Read Buffer Size */
        u8 ret[7] = {0x53, 0x01, 0x40, 0x0A, 0x00, 0x00, 0x00};  /* ACL 339 bytes x10 */
        bt_command_complete(opcode, ret, 7);
        return;
    }
    case 0x1009: bt_command_complete(opcode, bd_addr, 6); return;  /* Read BD_ADDR */
    case 0x1001: { /* Read Local Version */
        u8 ret[8] = {3, 0xA7, 0x40, 3, 0x0F, 0x00, 0x0E, 0x43};
        bt_command_complete(opcode, ret, 8);
        return;
    }
    case 0x1003: { /* Read Local Features */
        u8 ret[8] = {0xFF, 0xFF, 0x8D, 0xFE, 0x9B, 0xF9, 0x00, 0x80};
        bt_command_complete(opcode, ret, 8);
        return;
    }
    case 0x0C0D: { /* Read Stored Link Key: none stored */
        u8 ret[4] = {0xFF, 0x00, 0x00, 0x00};
        bt_command_complete(opcode, ret, 4);
        return;
    }
    case 0x0C12: { /* Delete Stored Link Key */
        u8 ret[2] = {0, 0};
        bt_command_complete(opcode, ret, 2);
        return;
    }
    case 0x0401: { /* Inquiry: status now, then "complete, nobody found" */
        u8 st[6] = {0x0F, 4, 0, 1, (u8)opcode, (u8)(opcode >> 8)};
        bt_push_event(st, 6);
        u8 done[3] = {0x01, 1, 0};
        bt_push_event(done, 3);
        return;
    }
    case 0x0402: /* Inquiry Cancel */
    default:
        bt_command_complete(opcode, NULL, 0);
        return;
    }
}

static void bt_deliver(void) {
    while (bt_reads && bt_ev_count) {
        u32 req = bt_event_reads[0];
        memmove(bt_event_reads, bt_event_reads + 1, sizeof(u32) * (size_t)--bt_reads);
        u32 vec = r32(req + 0x0C + 12);
        u32 n_in = r32(req + 0x0C + 4);
        u32 buf = r32(vec + 8 * n_in);
        u8 len = bt_event_len[bt_ev_head];
        copy_out(buf, bt_events[bt_ev_head], len);
        bt_ev_head = (bt_ev_head + 1) % BT_EVQ;
        bt_ev_count--;
        w32(req + 4, len);
        w32(req + 8, r32(req));
        w32(req, 8);
        queue_reply(req);
    }
}

static s32 bt_ioctlv(Req* r, u32 num, u32 n_in, u32 n_io, u32 vec) {
    (void)n_io;
    if (num == 0) {  /* control: HCI command in io[0] */
        u32 data = r32(vec + 8 * n_in);
        u32 len = r32(vec + 8 * n_in + 4);
        u8 cmd[260] = {0};
        copy_in(cmd, data, len < sizeof(cmd) ? len : sizeof(cmd));
        u16 opcode = (u16)(cmd[0] | cmd[1] << 8);
        if (g_verbose) LOG("ios: bt hci %04X\n", opcode);
        bt_command(opcode, cmd + 3, cmd[2]);
        return (s32)len;
    }
    if (num == 2) {  /* HCI event read */
        if (bt_reads < 16) bt_event_reads[bt_reads++] = r->addr;
        r->no_reply = 1;
        return 0;
    }
    r->no_reply = 1;  /* ACL reads: no remotes */
    return 0;
}

static const Device dev_bt = { "bt", NULL, NULL, bt_ioctlv, NULL, NULL, NULL, NULL };

static const Device dev_generic = { "generic", NULL, generic_ioctl, generic_ioctlv, NULL, NULL, NULL, NULL };

static const Device* find_device(const char* path) {
    if (!strcmp(path, "/dev/di")) return &dev_di;
    if (!strcmp(path, "/dev/es")) return &dev_es;
    if (!strcmp(path, "/dev/fs")) return &dev_fs;
    if (!strncmp(path, "/dev/stm/", 9)) return &dev_stm;
    if (!strcmp(path, "/dev/usb/oh1/57e/305")) return &dev_bt;
    if (!strncmp(path, "/dev/", 5)) return &dev_generic;
    return &dev_file;
}

/* Devices that don't exist on this "console": opening them fails. */
static int device_missing(const char* path) {
    return !strncmp(path, "/dev/net/", 9) || !strcmp(path, "/dev/sdio/slot0") ||
           !strncmp(path, "/dev/usb/oh0", 12) || !strncmp(path, "/dev/wfs", 8);
}

/* ---------------- request dispatch ---------------- */

static void ios_execute(u32 addr) {
    Req r = {0};
    g_ipc_requests++;
    r.addr = addr;
    r.cmd = r32(addr);
    r.fd = r32(addr + 8);
    for (int i = 0; i < 5; i++) r.a[i] = r32(addr + 0x0C + 4u * i);
    s32 result = IOS_EINVAL;

    if (r.cmd == 1) {  /* open */
        char path[256];
        read_str(r.a[0], path, sizeof(path));
        int fd = -1;
        for (int i = 0; i < MAX_FD; i++) if (!fds[i].dev) { fd = i; break; }
        if (device_missing(path)) {
            LOG("ios: open %s -> not present\n", path);
            result = IOS_ENOENT;
        } else if (fd < 0) {
            result = -22;
        } else {
            const Device* d = find_device(path);
            r.fd = (u32)fd;
            fds[fd].dev = d;
            snprintf(fds[fd].path, sizeof(fds[fd].path), "%s", path);
            fds[fd].mode = r.a[1];
            result = d->open ? d->open(&r, path, r.a[1]) : IOS_OK;
            if (result < 0) fds[fd].dev = NULL;
            else result = fd;
            if (d != &dev_file) LOG("ios: open %s -> fd %d\n", path, (int)result);
        }
    } else if (r.fd >= MAX_FD || !fds[r.fd].dev) {
        LOG("ios: cmd %u on bad fd %u\n", r.cmd, r.fd);
        result = IOS_EINVAL;
    } else {
        const Device* d = fds[r.fd].dev;
        switch (r.cmd) {
        case 2:
            if (d->close) d->close(&r);
            fds[r.fd].dev = NULL;
            result = IOS_OK;
            break;
        case 3: result = d->read ? d->read(&r, r.a[0], r.a[1]) : IOS_EINVAL; break;
        case 4: result = d->write ? d->write(&r, r.a[0], r.a[1]) : IOS_EINVAL; break;
        case 5: result = d->seek ? d->seek(&r, (s32)r.a[0], r.a[1]) : IOS_EINVAL; break;
        case 6: result = d->ioctl ? d->ioctl(&r, r.a[0], r.a[1], r.a[2], r.a[3], r.a[4]) : IOS_EINVAL; break;
        case 7: result = d->ioctlv ? d->ioctlv(&r, r.a[0], r.a[1], r.a[2], r.a[3]) : IOS_EINVAL; break;
        default: LOG("ios: unknown command %u\n", r.cmd); break;
        }
    }
    if (r.no_reply) return;
    w32(addr + 4, (u32)result);
    w32(addr + 8, r.cmd);
    w32(addr, 8);
    queue_reply(addr);
}

/* ---------------- register interface ---------------- */

int ios_mmio_write(CPUState* cpu, u32 ea, u32 value) {
    g_cpu = cpu;
    ea &= 0xFF00FFFFu;  /* 0xCD80xxxx mirrors 0xCD00xxxx */
    switch (ea) {
    case IPC_PPCCTRL: {
        ctrl = (ctrl & ~(CTRL_IY1 | CTRL_IY2)) | (value & (CTRL_IY1 | CTRL_IY2));
        ctrl &= ~(value & (CTRL_Y1 | CTRL_Y2));  /* write 1 to clear */
        if (value & CTRL_X1) {
            u32 addr = mmio_get32(IPC_PPCMSG);
            ctrl |= CTRL_Y2;  /* ack */
            ios_execute(addr);
        }
        ipc_update_irq();
        return 1;
    }
    case HW_PPCIRQFLAG:
        mmio_set32(HW_PPCIRQFLAG, mmio_get32(HW_PPCIRQFLAG) & ~value);
        ipc_update_irq();
        return 1;
    case HW_PPCIRQMASK:
        mmio_set32(HW_PPCIRQMASK, value);
        ipc_update_irq();
        return 1;
    }
    return 0;
}

int ios_mmio_read(u32 ea, u32* value) {
    ea &= 0xFF00FFFFu;
    if (ea == IPC_PPCCTRL) { *value = ctrl; return 1; }
    return 0;
}

void ios_init(CPUState* cpu) {
    g_cpu = cpu;
    disc_build_map();
    mkdirs(g_nand_root);
}

/* Deliver one queued reply when the PPC has taken the previous one. */
void ios_tick(CPUState* cpu) {
    g_cpu = cpu;
    bt_deliver();
    if (q_count && !(ctrl & CTRL_Y1) && !(ctrl & CTRL_Y2)) {
        mmio_set32(IPC_ARMMSG, reply_q[q_head]);
        q_head = (q_head + 1) % QMAX;
        q_count--;
        ctrl |= CTRL_Y1;
        ipc_update_irq();
    }
}
