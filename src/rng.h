#pragma once
// PCG32: small, fast and identical on every machine, so a seed always makes the same run.
#include <cstdint>
#include <utility>
#include <vector>

struct Rng {
    uint64_t state = 0;

    explicit Rng(uint64_t seed = 1) {
        state = seed + 0x853c49e6748fea9bULL;
        next();
    }

    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t x = uint32_t(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = uint32_t(old >> 59u);
        return (x >> rot) | (x << ((32 - rot) & 31));
    }

    int range(int lo, int hi) { return hi <= lo ? lo : lo + int(next() % uint32_t(hi - lo + 1)); }  // inclusive
    float unit() { return float(next() >> 8) * (1.0f / 16777216.0f); }
    bool chance(float p) { return unit() < p; }
    bool percent(int p) { return range(1, 100) <= p; }

    template <class T>
    void shuffle(std::vector<T>& v) {
        for (size_t i = v.size(); i > 1; i--) std::swap(v[i - 1], v[size_t(next() % i)]);
    }
};
