// NSMBW remote test station.
//
// Installed as sdmc:/hbmenu.nro so that every fresh homebrew launch starts here. When station mode
// is off (no sdmc:/switch/nsmbw-station/active file) it immediately chains to the real menu, so
// day-to-day homebrew use is unchanged. When it is on and the process is a full-memory Application,
// it shows its address and waits for the Mac (tools/station.py) on TCP port 5577:
//
//   PING                      -> "OK station 1 <program id>\n"
//   PUT <path> <size>\n<data> -> writes the file (via <path>.part), "OK\n"
//   GET <path>                -> "OK <size>\n<data>" or "ERR ...\n"
//   DEL <path>                -> "OK\n"
//   LIST <dir>                -> "OK\n" then one "<size> <name>" line per entry, then ".\n"
//   RUN                       -> "OK\n", then launches sdmc:/switch/nsmbw2/nsmbw2.nro
//   STOP                      -> "OK\n", turns station mode off and opens the real menu
//
// One command per connection. Paths must be under /switch/nsmbw2/ or /switch/nsmbw-station/ and
// may not contain "..". The game, started with NSMBW_STATION_RUN_SECONDS in its env.txt, plays for
// that long and then restarts the application, which brings the process back here.
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <switch.h>

#define STATION_DIR "sdmc:/switch/nsmbw-station"
#define ACTIVE_FLAG STATION_DIR "/active"
#define LOG_PATH STATION_DIR "/station.log"
#define REAL_MENU "sdmc:/switch/nsmbw-station/menu.nro"
#define GAME_NRO "sdmc:/switch/nsmbw2/nsmbw2.nro"
#define PORT 5577

static FILE* g_log;

static void logf_(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fflush(g_log);
    }
    consoleUpdate(NULL);
}

static bool file_exists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void chain(const char* nro) {
    char argv[0x300];
    snprintf(argv, sizeof(argv), "%s", nro);
    envSetNextLoad(nro, argv);
}

// Maps a protocol path ("/switch/nsmbw2/x") to "sdmc:/switch/nsmbw2/x", or returns false.
static bool map_path(const char* in, char* out, size_t outSize) {
    if (strstr(in, "..") != NULL) return false;
    if (strncmp(in, "/switch/nsmbw2/", 15) != 0 && strcmp(in, "/switch/nsmbw2") != 0 &&
        strncmp(in, "/switch/nsmbw-station/", 22) != 0) {
        return false;
    }
    int n = snprintf(out, outSize, "sdmc:%s", in);
    return n > 0 && (size_t)n < outSize;
}

static bool send_all(int fd, const void* data, size_t size) {
    const char* p = data;
    while (size > 0) {
        ssize_t n = send(fd, p, size, 0);
        if (n <= 0) return false;
        p += n;
        size -= (size_t)n;
    }
    return true;
}

static void reply(int fd, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    send_all(fd, buf, strlen(buf));
}

// Reads one '\n'-terminated line (without the newline). Returns false on error or overflow.
static bool read_line(int fd, char* out, size_t outSize) {
    size_t n = 0;
    while (n + 1 < outSize) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) return false;
        if (c == '\n') {
            out[n] = 0;
            if (n > 0 && out[n - 1] == '\r') out[n - 1] = 0;
            return true;
        }
        out[n++] = c;
    }
    return false;
}

static void make_parent_dirs(const char* path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 6; *p; ++p) {  // skip "sdmc:/"
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

static u64 g_programId;

// The game relaunches the application when its run is over. For that relaunch to reach hbl (and so
// this station) without anyone holding R, the title used to start the station must open hbl by
// default. Atmosphere has no override_config.ini on this card, so the station writes one with the
// stock defaults plus that title, and deletes it again when station mode is turned off. An existing
// file is never modified.
#define OVERRIDE_INI "sdmc:/atmosphere/config/override_config.ini"
#define OVERRIDE_MARK STATION_DIR "/override_created"
static void ensure_override(void) {
    if (file_exists(OVERRIDE_MARK)) {
        logf_("Override: already set up by the station.\n");
        return;
    }
    if (file_exists(OVERRIDE_INI)) {
        logf_("Override: %s exists and was not written by the station; not touching it.\n"
              "Unattended restarts need this title to open homebrew without R.\n", OVERRIDE_INI);
        return;
    }
    FILE* f = fopen(OVERRIDE_INI, "w");
    if (!f) {
        logf_("Override: cannot write %s (%d)\n", OVERRIDE_INI, errno);
        return;
    }
    fprintf(f,
            "; Written by the NSMBW test station (sdmc:/switch/nsmbw-station); deleted when station mode\n"
            "; is turned off. Stock Atmosphere defaults, plus program %016lX opening homebrew\n"
            "; unless R is held.\n"
            "[hbl_config]\n"
            "program_id_0=010000000000100D\n"
            "override_key_0=!R\n"
            "program_id_1=%016lX\n"
            "override_key_1=!R\n"
            "override_any_app=true\n"
            "override_any_app_key=R\n",
            g_programId, g_programId);
    fclose(f);
    FILE* m = fopen(OVERRIDE_MARK, "w");
    if (m) fclose(m);
    logf_("Override: program %016lX now opens homebrew without R.\n", g_programId);
}

static void remove_override(void) {
    if (!file_exists(OVERRIDE_MARK)) return;
    remove(OVERRIDE_INI);
    remove(OVERRIDE_MARK);
    logf_("Override removed.\n");
}

// Returns 1 when the station should exit to launch the game, 2 to stop station mode.
static int handle_client(int fd) {
    char line[600], path[600];
    if (!read_line(fd, line, sizeof(line))) return 0;

    if (strcmp(line, "PING") == 0) {
        reply(fd, "OK station 1 %016lX\n", g_programId);
        return 0;
    }
    if (strcmp(line, "RUN") == 0) {
        if (!file_exists(GAME_NRO)) {
            reply(fd, "ERR no game nro\n");
            return 0;
        }
        reply(fd, "OK\n");
        logf_("RUN: launching the game\n");
        return 1;
    }
    if (strcmp(line, "STOP") == 0) {
        reply(fd, "OK\n");
        return 2;
    }
    if (strncmp(line, "PUT ", 4) == 0) {
        char rel[512];
        unsigned long long size = 0;
        if (sscanf(line + 4, "%511s %llu", rel, &size) != 2 || !map_path(rel, path, sizeof(path))) {
            reply(fd, "ERR bad PUT\n");
            return 0;
        }
        char part[620];
        snprintf(part, sizeof(part), "%s.part", path);
        make_parent_dirs(part);
        FILE* f = fopen(part, "wb");
        if (!f) {
            reply(fd, "ERR open %d\n", errno);
            return 0;
        }
        static char buf[256 * 1024];
        setvbuf(f, NULL, _IONBF, 0);
        unsigned long long left = size;
        bool ok = true;
        while (left > 0) {
            ssize_t r = recv(fd, buf, left < sizeof(buf) ? (size_t)left : sizeof(buf), 0);
            if (r <= 0) { ok = false; break; }
            if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) { ok = false; break; }
            left -= (unsigned long long)r;
        }
        ok = (fclose(f) == 0) && ok;
        if (!ok) {
            remove(part);
            reply(fd, "ERR write\n");
            return 0;
        }
        remove(path);
        if (rename(part, path) != 0) {
            reply(fd, "ERR rename %d\n", errno);
            return 0;
        }
        logf_("PUT %s (%llu bytes)\n", rel, size);
        reply(fd, "OK\n");
        return 0;
    }
    if (strncmp(line, "GET ", 4) == 0) {
        if (!map_path(line + 4, path, sizeof(path))) {
            reply(fd, "ERR bad path\n");
            return 0;
        }
        FILE* f = fopen(path, "rb");
        if (!f) {
            reply(fd, "ERR missing\n");
            return 0;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        reply(fd, "OK %ld\n", size);
        static char buf[256 * 1024];
        size_t r;
        while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
            if (!send_all(fd, buf, r)) break;
        }
        fclose(f);
        return 0;
    }
    if (strncmp(line, "DEL ", 4) == 0) {
        if (!map_path(line + 4, path, sizeof(path))) {
            reply(fd, "ERR bad path\n");
            return 0;
        }
        reply(fd, remove(path) == 0 ? "OK\n" : "ERR %d\n", errno);
        return 0;
    }
    if (strncmp(line, "LIST ", 5) == 0) {
        if (!map_path(line + 5, path, sizeof(path))) {
            reply(fd, "ERR bad path\n");
            return 0;
        }
        DIR* d = opendir(path);
        if (!d) {
            reply(fd, "ERR missing\n");
            return 0;
        }
        reply(fd, "OK\n");
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            char full[900];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
            long long size = stat(full, &st) == 0 ? (long long)st.st_size : -1;
            reply(fd, "%lld %s\n", size, e->d_name);
        }
        closedir(d);
        reply(fd, ".\n");
        return 0;
    }
    reply(fd, "ERR unknown command\n");
    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    // Station mode is opt-in, and only a full-memory Application can run the game afterwards;
    // anything else goes straight to the real menu.
    const AppletType type = appletGetAppletType();
    if (!file_exists(ACTIVE_FLAG) || (type != AppletType_Application && type != AppletType_SystemApplication)) {
        FILE* why = fopen(LOG_PATH, "a");
        if (why) {
            fprintf(why, "menu instead of station: active=%d applet_type=%d\n", file_exists(ACTIVE_FLAG) ? 1 : 0, (int)type);
            fclose(why);
        }
        if (file_exists(REAL_MENU)) chain(REAL_MENU);
        return 0;
    }

    consoleInit(NULL);
    mkdir(STATION_DIR, 0777);
    g_log = fopen(LOG_PATH, "a");
    svcGetInfo(&g_programId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    // The night runs unattended: no auto-sleep, no screen dimming.
    appletSetAutoSleepDisabled(true);
    appletSetMediaPlaybackState(true);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        logf_("socketInitialize failed: 0x%x. Press + for the menu.\n", rc);
    }
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(server, 4) != 0) {
        logf_("bind/listen failed: %d\n", errno);
    }
    fcntl(server, F_SETFL, fcntl(server, F_GETFL, 0) | O_NONBLOCK);

    struct in_addr ip = {.s_addr = gethostid()};
    logf_("NSMBW test station  (program %016lX)\n", g_programId);
    ensure_override();
    logf_("Listening on %s:%d\n", inet_ntoa(ip), PORT);
    logf_("Press + to open the normal homebrew menu.\n\n");

    int action = 0;
    while (appletMainLoop() && action == 0) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            action = 3;  // menu for this launch only; station mode stays on
            break;
        }
        int client = accept(server, NULL, NULL);
        if (client >= 0) {
            fcntl(client, F_SETFL, fcntl(client, F_GETFL, 0) & ~O_NONBLOCK);
            struct timeval tv = {.tv_sec = 30, .tv_usec = 0};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            action = handle_client(client);
            close(client);
        }
        svcSleepThread(20 * 1000 * 1000);
    }

    close(server);
    socketExit();
    if (action == 1) {
        chain(GAME_NRO);
    } else {
        if (action == 2) {
            remove(ACTIVE_FLAG);
            remove_override();
            logf_("Station mode off.\n");
        }
        if (file_exists(REAL_MENU)) chain(REAL_MENU);
    }
    if (g_log) fclose(g_log);
    consoleExit(NULL);
    return 0;
}
