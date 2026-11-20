// Online play, step 1 (docs/online-play.md section 8): delay-based lockstep between game copies
// over UDP, no room screen yet. Every copy runs the whole game; game frame N runs only once every
// player's input for N is in, so all copies get the same input on the same frames and (step 0)
// stay in the same state.
//
//   NSMBW_NETPLAY_SLOT=<n>        this copy's player slot (0 = host, Wii Remote channel n)
//   NSMBW_NETPLAY_PLAYERS=<n>     players in the session (2-4); every copy sees that many remotes
//   NSMBW_NETPLAY_PORT=<port>     this copy's UDP port (bound on 127.0.0.1, or NSMBW_NETPLAY_BIND)
//   NSMBW_NETPLAY_PEERS=<slot>@<host>:<port>,...   every other copy's slot and address
//   NSMBW_NETPLAY_DELAY=<frames>  input delay D (default 2): input sampled at frame N is used at N+D
//   NSMBW_NETPLAY_TOKEN=<hex>     session token; packets without it are dropped (default 0x4E50)
// Setting NSMBW_NETPLAY_PORT turns on frame mode (frame-driven audio, the frame clock, the fixed
// random seed: nsmbw_netplay_replay.cpp), so a session starts from boot in the same state on every
// copy - the "restart into the session" of section 3, done here by launching the copies together.
//
// This step uses a full mesh (each copy sends its own slot's input to every other copy); the
// host-as-hub forwarding of section 5 is a transport detail for step 3. One local player per copy:
// the local controller's player 1 drives this copy's slot.
//
// Per frame, at the frame boundary (the controller read, see nsmbw_netplay_replay.cpp):
//  1. the local input is sampled and scheduled for frame N + D (frames 0..D-1 have no input);
//  2. it is sent with every frame the peer has not acknowledged yet (redundancy, no resends);
//  3. the game waits until every slot's input for frame N is in, resending while it waits;
//  4. every 30 frames a state hash (both random generators plus the guest memory pages that are
//     not host-paced) is exchanged and compared; a mismatch is logged as "OUT OF SYNC".
//
// Packet (little-endian, at most 512 bytes): u32 magic "NSNP", u8 version, u8 slot, u8 count,
// u8 flags, u32 token, u32 ack (the highest frame of the receiver's input this copy holds, all
// earlier ones too; 0xFFFFFFFF none), u32 first frame, u32 hash frame, u32 hash (hash frame
// 0xFFFFFFFF = none), then count x { u32 hold, u32 tilt bits, i8 movie-skip request, 3 pad }.
// Everything received is bounds-checked; nothing in a packet is ever executed.
#ifdef _WIN32
// Winsock (ws2_32 is already linked for Windows, NsmbwProduct.cmake). No MSG_DONTWAIT: the socket
// is put in non-blocking mode once instead.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
using SocketHandle = SOCKET;
using socklen_t = int;
constexpr SocketHandle kNoSocket = INVALID_SOCKET;
#define NSMBW_NET_DONTWAIT 0
inline int NetPoll(pollfd* p, unsigned long count, int ms) { return WSAPoll(p, count, ms); }
inline void NetClose(SocketHandle s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kNoSocket = -1;
#define NSMBW_NET_DONTWAIT MSG_DONTWAIT
inline int NetPoll(pollfd* p, unsigned long count, int ms) { return poll(p, static_cast<nfds_t>(count), ms); }
inline void NetClose(SocketHandle s) { close(s); }
#endif

// After winsock2.h, which must come before anything that includes windows.h.
#include "memory.h"
#include <aurora/env.hpp>

#include <cstdarg>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" uint32_t NsmbwReplayCurrentFrame();
extern "C" uint32_t g_nsmbwCurrentSceneProfile;
extern "C" void NsmbwPrepareImmediateExit();

namespace {
constexpr uint32_t kMagic = 0x504E534Eu; // "NSNP"
constexpr uint8_t kVersion = 1;
constexpr uint32_t kMaxSlots = 4;
constexpr uint32_t kRing = 256;          // frames of input kept per slot
constexpr uint32_t kMaxPerPacket = 32;   // frames per packet
constexpr uint32_t kNone = 0xFFFFFFFFu;
constexpr uint32_t kHashEvery = 30;
constexpr uint32_t kMem1Base = 0x80000000u, kMem1Size = 0x01800000u, kPageSize = 256u * 1024u;
// Seeds of the game's two random generators (US r13 = 0x8042F680; see nsmbw_netplay_replay.cpp).
constexpr uint32_t kSeedAddr = 0x80429F44u, kSeed2Addr = 0x8042A390u;

struct FrameInput {
    uint32_t frame = kNone;
    uint32_t hold = 0;
    float tilt = 0.0f;
    int8_t skip = -1;
};

struct Peer {
    uint32_t slot = 0;
    sockaddr_in addr{};
    uint32_t acked = kNone;      // highest of our frames the peer holds (contiguous)
};

struct Net {
    bool init = false;
    bool active = false;
    uint32_t slot = 0, players = 2, delay = 2, token = 0x4E50;
    SocketHandle sock = kNoSocket;
    std::vector<Peer> peers;
    FrameInput ring[kMaxSlots][kRing];
    uint32_t contiguous[kMaxSlots]; // highest frame held for each slot with all earlier ones too
    // Our own state hash per checked frame, and the peers' as they arrive.
    uint32_t myHashFrame = kNone, myHash = 0;
    uint32_t peerHashFrame[kMaxSlots], peerHash[kMaxSlots];
    uint32_t lastCompared[kMaxSlots];
    uint64_t waits = 0, waitedMicros = 0, desyncs = 0, checks = 0;
    uint32_t curFrame = 0;
};
Net n;

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[nsmbw][netplay] ");
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    va_end(ap);
}

bool ParseAddr(const std::string& hostPort, sockaddr_in& out) {
    const size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos) return false;
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<uint16_t>(std::strtoul(hostPort.c_str() + colon + 1, nullptr, 10)));
    std::string host = hostPort.substr(0, colon);
    if (host == "localhost") host = "127.0.0.1";
    return inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1;
}

void Init() {
    if (n.init) return;
    n.init = true;
    const char* port = AURORA_ENV("NSMBW_NETPLAY_PORT");
    if (!port) return;
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_SLOT")) n.slot = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_PLAYERS")) n.players = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_DELAY")) n.delay = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_TOKEN")) n.token = static_cast<uint32_t>(std::strtoul(v, nullptr, 16));
    if (n.players < 1 || n.players > kMaxSlots || n.slot >= n.players || n.delay > 30) {
        Log("bad settings (slot %u, players %u, delay %u); netplay off", n.slot, n.players, n.delay);
        return;
    }
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_PEERS")) {
        std::string s = v;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find(',', pos);
            if (end == std::string::npos) end = s.size();
            const std::string item = s.substr(pos, end - pos);
            const size_t at = item.find('@');
            Peer p;
            if (at != std::string::npos && ParseAddr(item.substr(at + 1), p.addr)) {
                p.slot = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10));
                if (p.slot < n.players && p.slot != n.slot) n.peers.push_back(p);
            } else {
                Log("could not read peer '%s'", item.c_str());
            }
            pos = end + 1;
        }
    }
    if (n.peers.size() != n.players - 1) {
        Log("%zu peers for %u players; netplay off", n.peers.size(), n.players);
        return;
    }
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    n.sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in local{};
    const char* bind = AURORA_ENV("NSMBW_NETPLAY_BIND");
    if (n.sock == kNoSocket || !ParseAddr(std::string(bind ? bind : "127.0.0.1") + ":" + port, local) ||
        ::bind(n.sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        Log("could not open UDP port %s; netplay off", port);
        if (n.sock != kNoSocket) NetClose(n.sock);
        n.sock = kNoSocket;
        return;
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(n.sock, FIONBIO, &nonBlocking);
#endif
    for (uint32_t s = 0; s < kMaxSlots; ++s) {
        n.contiguous[s] = kNone;
        n.peerHashFrame[s] = kNone;
        n.lastCompared[s] = kNone;
    }
    n.active = true;
    Log("slot %u of %u, port %s, input delay %u frames, %zu peer(s)", n.slot, n.players, port, n.delay, n.peers.size());
}

FrameInput& At(uint32_t slot, uint32_t frame) { return n.ring[slot][frame % kRing]; }

bool Has(uint32_t slot, uint32_t frame) {
    // Frames before the delay has passed carry no input on any copy.
    return frame < n.delay || At(slot, frame).frame == frame;
}

void Store(uint32_t slot, uint32_t frame, uint32_t hold, float tilt, int8_t skip) {
    FrameInput& f = At(slot, frame);
    f.frame = frame;
    f.hold = hold;
    f.tilt = tilt;
    f.skip = skip;
    // Highest frame held with every earlier one too (the ack sent back for this slot).
    uint32_t c = n.contiguous[slot];
    uint32_t next = c == kNone ? n.delay : c + 1;
    while (At(slot, next).frame == next) {
        c = next++;
    }
    n.contiguous[slot] = c;
}

void Put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint32_t Get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// Our input for frames the peer has not acknowledged, up to the newest we have.
void SendTo(Peer& p, uint32_t newest) {
    if (newest == kNone || newest < n.delay) return;
    uint32_t first = p.acked == kNone ? n.delay : p.acked + 1;
    if (newest + 1 > kMaxPerPacket && first < newest + 1 - kMaxPerPacket) first = newest + 1 - kMaxPerPacket;
    if (first > newest) first = newest; // nothing new: still send the newest, as a keep-alive and ack
    std::vector<uint8_t> b;
    b.reserve(28 + 12 * kMaxPerPacket);
    Put32(b, kMagic);
    b.push_back(kVersion);
    b.push_back(static_cast<uint8_t>(n.slot));
    b.push_back(static_cast<uint8_t>(newest - first + 1));
    b.push_back(0);
    Put32(b, n.token);
    Put32(b, n.contiguous[p.slot]);
    Put32(b, first);
    Put32(b, n.myHashFrame);
    Put32(b, n.myHash);
    for (uint32_t f = first; f <= newest; ++f) {
        const FrameInput& in = At(n.slot, f);
        uint32_t tiltBits = 0;
        std::memcpy(&tiltBits, &in.tilt, 4);
        Put32(b, in.hold);
        Put32(b, tiltBits);
        b.push_back(static_cast<uint8_t>(in.skip));
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
    }
    sendto(n.sock, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0, reinterpret_cast<const sockaddr*>(&p.addr), sizeof(p.addr));
}

void SendAll(uint32_t newest) {
    for (Peer& p : n.peers) SendTo(p, newest);
}

void Receive() {
    uint8_t buf[600];
    while (true) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        const auto len = recvfrom(n.sock, reinterpret_cast<char*>(buf), static_cast<int>(sizeof(buf)), NSMBW_NET_DONTWAIT,
                                  reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (len < 0) return;
        if (len < 28 || Get32(buf) != kMagic || buf[4] != kVersion || Get32(buf + 8) != n.token) continue;
        const uint32_t slot = buf[5], count = buf[6];
        if (slot >= n.players || slot == n.slot || count > kMaxPerPacket || static_cast<size_t>(len) != 28u + 12u * count) {
            continue;
        }
        Peer* peer = nullptr;
        for (Peer& p : n.peers) {
            if (p.slot == slot) peer = &p;
        }
        if (!peer) continue;
        const uint32_t ack = Get32(buf + 12), first = Get32(buf + 16);
        if (ack != kNone && (peer->acked == kNone || ack > peer->acked)) peer->acked = ack;
        const uint32_t hashFrame = Get32(buf + 20);
        if (hashFrame != kNone && (n.peerHashFrame[slot] == kNone || hashFrame > n.peerHashFrame[slot])) {
            n.peerHashFrame[slot] = hashFrame;
            n.peerHash[slot] = Get32(buf + 24);
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* e = buf + 28 + 12 * i;
            const uint32_t frame = first + i;
            // Old redundant copies (already used, and their ring entry may hold a newer frame).
            if (frame + kRing / 2 < n.curFrame || frame > n.curFrame + kRing / 2 || Has(slot, frame)) continue;
            float tilt = 0.0f;
            const uint32_t tiltBits = Get32(e + 4);
            std::memcpy(&tilt, &tiltBits, 4);
            Store(slot, frame, Get32(e), tilt, static_cast<int8_t>(e[8]));
        }
    }
}

// State hash for the desync check: both random generators, the scene, and every MEM1 page except
// the ones host timing writes (the VI retrace count's page 0x80400000 - the seeds are added on
// their own - EGG's display state at 0x80740000, and 0x80340000 / 0x80380000, which a host thread
// writes during the frame boundary; docs/online-play.md 8d).
uint32_t StateHash(uint32_t frame) {
    uint64_t h = 0xCBF29CE484222325ull ^ frame;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 0x100000001B3ull; };
    uint32_t s1 = 0, s2 = 0;
    Memory::TryRead32(kSeedAddr, s1);
    Memory::TryRead32(kSeed2Addr, s2);
    mix(s1);
    mix(s2);
    mix(g_nsmbwCurrentSceneProfile);
    const uint8_t* mem = static_cast<const uint8_t*>(Memory::GetPointer(kMem1Base, kMem1Size));
    for (uint32_t off = 0; off < kMem1Size; off += kPageSize) {
        const uint32_t page = kMem1Base + off;
        if (page == 0x80340000u || page == 0x80380000u || page == 0x80400000u || page == 0x80740000u) continue;
        uint64_t a = 0x9E3779B97F4A7C15ull;
        for (uint32_t i = 0; i < kPageSize; i += 8) {
            uint64_t w;
            std::memcpy(&w, mem + off + i, 8);
            a = (a ^ w) * 0x100000001B3ull;
        }
        mix(a);
    }
    return static_cast<uint32_t>(h ^ (h >> 32));
}

void CompareHashes() {
    for (const Peer& p : n.peers) {
        const uint32_t f = n.peerHashFrame[p.slot];
        if (f == kNone || f != n.myHashFrame || f == n.lastCompared[p.slot]) continue;
        n.lastCompared[p.slot] = f;
        ++n.checks;
        if (n.peerHash[p.slot] != n.myHash) {
            ++n.desyncs;
            Log("OUT OF SYNC with slot %u at frame %u (ours %08X, theirs %08X)", p.slot, f, n.myHash, n.peerHash[p.slot]);
        }
    }
}
} // namespace

extern "C" bool NsmbwNetplayActive() {
    Init();
    return n.active;
}

// At the start of game frame `frame`, after the local devices were read. Schedules the local
// player's input for frame + D, exchanges input, waits until every slot's input for `frame` is
// in, and returns the agreed input for every channel.
extern "C" void NsmbwNetplayExchange(uint32_t localHold, float localTilt, int32_t localSkip, uint32_t* channels,
                                     uint32_t* hold, float* tilt, int32_t* skip) {
    Init();
    const uint32_t frame = NsmbwReplayCurrentFrame();
    n.curFrame = frame;
    Store(n.slot, frame + n.delay, localHold, localTilt, static_cast<int8_t>(localSkip));
    const uint32_t newest = frame + n.delay;

    // Desync check: hash this frame's starting state every kHashEvery frames; it goes out with the
    // input and is compared when the peers' hash for the same frame arrives.
    if (frame % kHashEvery == 0) {
        n.myHashFrame = frame;
        n.myHash = StateHash(frame);
    }

    SendAll(newest);
    Receive();
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto lastSend = start, lastNote = start;
    bool waited = false;
    while (true) {
        bool ready = true;
        for (const Peer& p : n.peers) ready = ready && Has(p.slot, frame);
        if (ready) break;
        waited = true;
        pollfd pfd{n.sock, POLLIN, 0};
        NetPoll(&pfd, 1, 2);
        Receive();
        const auto now = Clock::now();
        if (now - lastSend >= std::chrono::milliseconds(8)) {
            SendAll(newest);
            lastSend = now;
        }
        if (now - lastNote >= std::chrono::seconds(2)) {
            lastNote = now;
            Log("frame %u: waiting for input (%.0f s)", frame, std::chrono::duration<double>(now - start).count());
        }
        if (now - start >= std::chrono::seconds(20)) {
            Log("frame %u: no input for 20 s, ending the session", frame);
            NsmbwPrepareImmediateExit();
            std::fflush(stderr);
            _exit(3);
        }
    }
    if (waited) {
        ++n.waits;
        n.waitedMicros += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
    }
    // Send once more after the wait so peers get our acks promptly.
    if (waited) SendAll(newest);
    CompareHashes();

    *channels = n.players;
    *skip = -1;
    for (uint32_t s = 0; s < kMaxSlots; ++s) {
        hold[s] = 0;
        tilt[s] = 0.0f;
        if (s >= n.players || frame < n.delay) continue;
        const FrameInput& in = At(s, frame);
        hold[s] = in.hold;
        tilt[s] = in.tilt;
        // The movie-skip request: the lowest slot that asks, so every copy picks the same one.
        if (*skip < 0 && in.skip >= 0) *skip = in.skip;
    }
    if (frame % 600 == 0) {
        Log("frame %u: waited on %llu frames (%.1f ms in all); state checks: %llu compared, %llu out of sync", frame,
            static_cast<unsigned long long>(n.waits), static_cast<double>(n.waitedMicros) / 1000.0,
            static_cast<unsigned long long>(n.checks), static_cast<unsigned long long>(n.desyncs));
    }
}
