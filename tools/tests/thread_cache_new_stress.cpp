#include "thread_cache_new.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <deque>
THREAD_CACHE_NEW_DEFINE_OPERATORS

struct Msg { unsigned char* p; size_t n; unsigned char tag; };
static std::mutex qm[8]; static std::deque<Msg> q[8];
static std::atomic<long> bad{0}, ops{0};
static bool check(const Msg& m) { for (size_t i = 0; i < m.n; ++i) if (m.p[i] != (unsigned char)(m.tag + i)) return false; return true; }

static void worker(int id, int iters) {
    std::mt19937 rng(id * 7919 + 1);
    std::vector<Msg> mine;
    for (int it = 0; it < iters; ++it) {
        size_t n = (rng() % 10 == 0) ? 513 + rng() % 3000 : 1 + rng() % 512;
        auto* p = static_cast<unsigned char*>(::operator new(n));
        if (((uintptr_t)p & 15) != 0) bad++;
        unsigned char tag = (unsigned char)rng();
        for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)(tag + i);
        Msg m{p, n, tag};
        if (rng() % 2) { const int t = (id + 1 + rng() % 7) % 8; std::lock_guard<std::mutex> l(qm[t]); q[t].push_back(m); }
        else mine.push_back(m);
        if (mine.size() > 200) { size_t k = rng() % mine.size(); if (!check(mine[k])) bad++; ::operator delete(mine[k].p); mine[k] = mine.back(); mine.pop_back(); }
        if (it % 16 == 0) {
            std::deque<Msg> got; { std::lock_guard<std::mutex> l(qm[id % 8]); got.swap(q[id % 8]); }
            for (auto& g : got) { if (!check(g)) bad++; ::operator delete(g.p); }
        }
        // std containers through the replaced operators too
        if (it % 50 == 0) { std::vector<std::string> v; for (int j = 0; j < 20; ++j) v.push_back(std::string(rng() % 100, 'x')); }
        ops++;
    }
    for (auto& m : mine) { if (!check(m)) bad++; ::operator delete(m.p); }
}
int main() {
    thread_cache_new::Enable();
    for (int round = 0; round < 6; ++round) {
        std::vector<std::thread> ts;
        for (int i = 0; i < 8; ++i) ts.emplace_back(worker, i + round * 8, 60000);
        // short-lived threads that exit with cached blocks
        for (int i = 0; i < 20; ++i) std::thread([] { std::vector<int*> v; for (int j = 0; j < 500; ++j) v.push_back(new int(j)); for (auto* p : v) delete p; for (int j = 0; j < 100; ++j) new char[17]; }).join();
        for (auto& t : ts) t.join();
    }
    for (int i = 0; i < 8; ++i) for (auto& m : q[i]) { if (!check(m)) bad++; ::operator delete(m.p); }
    long globalBlocks = 0; for (uint32_t c = 0; c < thread_cache_new::kClasses; ++c) globalBlocks += thread_cache_new::GlobalPools()[c].count;
    std::printf("ops=%ld bad=%ld globalPoolBlocks=%ld\n", ops.load(), bad.load(), globalBlocks);
    return bad != 0;
}
