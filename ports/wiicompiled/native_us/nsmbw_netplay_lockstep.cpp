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
//   NSMBW_NETPLAY_RELAY=1         internet sessions with 3-4 players: copies other than the host send
//                                 what is meant for another non-host copy to the host, which forwards it
//                                 (joiners behind different routers usually cannot reach each other)
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
// u8 flags (bit 0: the sender is leaving the session; bit 1: relayed, for the slot in bits 4-5,
// which the host forwards unchanged), u32 token, u32 ack (the highest frame of the receiver's input this copy holds, all
// earlier ones too; 0xFFFFFFFF none), u32 first frame, u32 hash frame, u32 hash (hash frame
// 0xFFFFFFFF = none), then count x { u32 hold, u32 tilt bits, i8 movie-skip request, 3 pad }.
// Everything received is bounds-checked; nothing in a packet is ever executed.
//
// Internet play (docs/online-play.md 8h): a peer's address is taken from its own packets (the
// address its router puts on them), not only from the room: a router may change the port on the
// way out, and the room saw the address before the relaunch into the session.
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
// runtime/src/product/nsmbw_online_room.cpp
extern "C" bool NsmbwOnlineTakeLeaveRequest();
extern "C" [[noreturn]] void NsmbwOnlineEndSession(const char* reason);
extern "C" void NsmbwOnlineSetWaitingTitle(const char* textOrNull);
extern "C" bool NsmbwOnlinePumpEventsQuit(); // pumps window events; true if the window was closed

namespace {
constexpr uint32_t kMagic = 0x504E534Eu; // "NSNP"
constexpr uint8_t kVersion = 2; // 2: relay flags (8h); a version-1 copy is refused, not misread
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
    bool heard = false;          // any valid packet from this peer yet
    bool relayed = false;        // reached through the host (NSMBW_NETPLAY_RELAY), not directly
    std::chrono::steady_clock::time_point lastPacket{};
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
    bool leaving = false;      // set once this copy leaves: every packet from then on says so
    bool relay = false;        // NSMBW_NETPLAY_RELAY: non-host copies reach each other through the host
    uint64_t relayedOut = 0;   // host: packets forwarded for other copies
    std::chrono::steady_clock::time_point startedAt{}; // when this copy joined the session
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

void SendLeave();

void Init() {
    if (n.init) return;
    n.init = true;
    const char* port = AURORA_ENV("NSMBW_NETPLAY_PORT");
    if (!port) return;
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_SLOT")) n.slot = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_PLAYERS")) n.players = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_DELAY")) n.delay = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_TOKEN")) n.token = static_cast<uint32_t>(std::strtoul(v, nullptr, 16));
    if (const char* v = AURORA_ENV("NSMBW_NETPLAY_RELAY")) n.relay = v[0] == '1';
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
    if (n.relay && n.slot != 0) {
        // Every other non-host copy is reached through the host (slot 0).
        const Peer* host = nullptr;
        for (const Peer& p : n.peers) if (p.slot == 0) host = &p;
        if (!host) {
            Log("relay mode without the host among the peers; netplay off");
            return;
        }
        const sockaddr_in hostAddr = host->addr;
        for (Peer& p : n.peers) {
            if (p.slot == 0) continue;
            p.relayed = true;
            p.addr = hostAddr;
        }
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
    n.startedAt = std::chrono::steady_clock::now();
    std::atexit(SendLeave); // a normal quit (the window closed) tells the others at once
    Log("slot %u of %u, port %s, input delay %u frames, %zu peer(s)%s", n.slot, n.players, port, n.delay, n.peers.size(),
        n.relay ? (n.slot == 0 ? ", relaying for the others" : ", through the host") : "");
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
    // flags: bit 0 = this copy is leaving the session; bit 1 + bits 4-5 = relayed to slot p.slot
    b.push_back(static_cast<uint8_t>((n.leaving ? 1u : 0u) | (p.relayed ? (2u | (p.slot << 4)) : 0u)));
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
        const bool relayed = (buf[7] & 2u) != 0;
        const uint32_t relayTo = (buf[7] >> 4) & 3u;
        if (relayed && relayTo != n.slot) {
            // Host: forward unchanged to the copy it is meant for (only the host relays; a packet
            // that names another slot anywhere else is dropped).
            if (n.slot != 0 || relayTo >= n.players) continue;
            for (const Peer& to : n.peers) {
                if (to.slot != relayTo) continue;
                sendto(n.sock, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0,
                       reinterpret_cast<const sockaddr*>(&to.addr), sizeof(to.addr));
                ++n.relayedOut;
            }
            // It came straight from its sender, so this is the sender's current address.
            peer->addr = from;
            continue;
        }
        // A direct packet carries the peer's current address (its router may have changed the
        // port, or the room saw the address before the relaunch): send there from now on.
        if (!relayed && !peer->relayed &&
            (from.sin_addr.s_addr != peer->addr.sin_addr.s_addr || from.sin_port != peer->addr.sin_port)) {
            char was[32], now[32];
            std::snprintf(was, sizeof(was), "%u.%u.%u.%u:%u", peer->addr.sin_addr.s_addr & 255, (peer->addr.sin_addr.s_addr >> 8) & 255,
                          (peer->addr.sin_addr.s_addr >> 16) & 255, peer->addr.sin_addr.s_addr >> 24, ntohs(peer->addr.sin_port));
            std::snprintf(now, sizeof(now), "%u.%u.%u.%u:%u", from.sin_addr.s_addr & 255, (from.sin_addr.s_addr >> 8) & 255,
                          (from.sin_addr.s_addr >> 16) & 255, from.sin_addr.s_addr >> 24, ntohs(from.sin_port));
            Log("slot %u is at %s (was %s)", slot, now, was);
            peer->addr = from;
        }
        if (buf[7] & 1u) {
            char why[64];
            std::snprintf(why, sizeof(why), "Player %u left the session", slot + 1);
            Log("%s", why);
            NsmbwOnlineEndSession(why);
        }
        if (!peer->heard) {
            peer->heard = true;
            Log("first packet from slot %u at frame %u", slot, n.curFrame);
        }
        peer->lastPacket = std::chrono::steady_clock::now();
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

// Tells every peer this copy is leaving (a few times: no acknowledgement is waited for).
void SendLeave() {
    if (!n.active || n.sock == kNoSocket) return;
    n.leaving = true;
    for (int i = 0; i < 5; ++i) SendAll(n.curFrame + n.delay);
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
    // NSMBW_NETPLAY_TEST_LEAVE=<frame>: test runs leave the session at that frame, as the button does.
    static const uint32_t testLeaveAt = [] {
        const char* v = AURORA_ENV("NSMBW_NETPLAY_TEST_LEAVE");
        return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : 0xFFFFFFFFu;
    }();
    if (NsmbwOnlineTakeLeaveRequest() || frame == testLeaveAt) { // the Online screen's Leave the session
        SendLeave();
        NsmbwOnlineEndSession("You left the session");
    }
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
    bool waited = false, titleShown = false;
    while (true) {
        bool ready = true;
        for (const Peer& p : n.peers) ready = ready && Has(p.slot, frame);
        if (ready) break;
        waited = true;
        pollfd pfd{n.sock, POLLIN, 0};
        NetPoll(&pfd, 1, 2);
        // Keep the window alive while the game is held here (the OS marks a window that stops
        // taking events as not responding), and let it be closed.
        if (NsmbwOnlinePumpEventsQuit()) {
            SendLeave();
            NsmbwOnlineSetWaitingTitle(nullptr);
            NsmbwPrepareImmediateExit();
            std::fflush(stderr);
            _exit(0);
        }
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
        if (!titleShown && now - start >= std::chrono::seconds(1)) {
            uint32_t missing = 0;
            for (const Peer& p : n.peers) if (!Has(p.slot, frame)) { missing = p.slot; break; }
            char text[64];
            std::snprintf(text, sizeof(text), "Waiting for player %u...", missing + 1);
            NsmbwOnlineSetWaitingTitle(text);
            titleShown = true;
        }
        // A peer we are still waiting on ends the session after 20 s with no packet from it. Before
        // its first packet the limit is 45 s from the start of the session (below). Timed from its last packet, not from the start of this wait: the first
        // version ended a session the moment the first packet arrived after a 32 s wait.
        bool silent = false;
        for (const Peer& p : n.peers) {
            if (!Has(p.slot, frame) && p.heard && now - p.lastPacket >= std::chrono::seconds(20)) silent = true;
        }
        // A peer never heard from at all, 45 s after this copy started the session: its packets
        // are not getting through (a firewall prompt hidden behind a fullscreen window, a blocked
        // Hamachi network). Without this the session waited forever on a black screen before its
        // first frame (2026-11-20, the developer and a friend).
        uint32_t neverHeard = kNone;
        for (const Peer& p : n.peers) {
            if (!p.heard && now - n.startedAt >= std::chrono::seconds(45)) neverHeard = p.slot;
        }
        if (neverHeard != kNone) {
            Log("frame %u: nothing from slot %u in 45 s, ending the session", frame, neverHeard);
            NsmbwOnlineSetWaitingTitle(nullptr);
            char why[96];
            std::snprintf(why, sizeof(why), "Couldn't reach player %u (check the firewall and Hamachi)", neverHeard + 1);
            NsmbwOnlineEndSession(why);
        }
        if (silent) {
            Log("frame %u: no input for 20 s, ending the session", frame);
            NsmbwOnlineSetWaitingTitle(nullptr);
            uint32_t missing = 0;
            for (const Peer& p : n.peers) if (!Has(p.slot, frame)) { missing = p.slot; break; }
            char why[64];
            std::snprintf(why, sizeof(why), "Player %u stopped responding", missing + 1);
            NsmbwOnlineEndSession(why);
        }
    }
    if (waited) {
        ++n.waits;
        n.waitedMicros += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
    }
    // Send once more after the wait so peers get our acks promptly.
    if (waited) SendAll(newest);
    if (titleShown) NsmbwOnlineSetWaitingTitle(nullptr);
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
        if (n.relay && n.slot == 0) Log("relayed %llu packets", static_cast<unsigned long long>(n.relayedOut));
    }
}
