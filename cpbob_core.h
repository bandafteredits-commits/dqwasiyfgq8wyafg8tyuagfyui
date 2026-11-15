// cpbob_core.h - portable core for CheesyProxxy "Sword bob" (crit animation).
// Header-only, no allocations, no OS calls. Only decides WHEN a crit happened and WHAT
// vertical offset the first-person held item should have this frame. Cosmetic only:
// it never touches server data, networking or gameplay.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace cpbob {

struct Settings {
    bool  enabled    = false;   // off by default
    float depth      = 0.12f;   // how far the sword dips, in item-space units (~blocks)
    float durationMs = 220.0f;  // whole down+back motion
    float downFrac   = 0.35f;   // share of the motion spent going down (rest = coming back)
};

// What the hook reads from the local player each frame / on each landed hit.
struct PlayerState {
    bool  onGround   = true;
    bool  inLiquid   = false;
    bool  onLadder   = false;
    bool  riding     = false;
    float fallDistance = 0.0f;
};

// Standard crit rule: a hit landed while falling, off the ground, not swimming/climbing/riding.
inline bool isCrit(const PlayerState& p) {
    return !p.onGround && !p.inLiquid && !p.onLadder && !p.riding && p.fallDistance > 0.0f;
}

class BobAnim {
public:
    void trigger()            { t_ = 0.0f; active_ = true; }      // a new crit restarts the bob
    void reset()              { t_ = 0.0f; active_ = false; }
    bool active() const       { return active_; }
    // Advance by dt seconds and return the Y offset to ADD to the held item (<= 0 = down).
    float step(const Settings& s, float dt) {
        if (!s.enabled || !active_) { if (!s.enabled) active_ = false; return 0.0f; }
        float dur = s.durationMs * 0.001f;
        if (dur < 0.02f) dur = 0.02f;
        float df = s.downFrac < 0.05f ? 0.05f : (s.downFrac > 0.95f ? 0.95f : s.downFrac);
        t_ += dt;
        if (t_ >= dur) { active_ = false; return 0.0f; }
        float p = t_ / dur, k;
        const float HALF_PI = 1.57079632679f;
        if (p < df) k = std::sin((p / df) * HALF_PI);                     // fast ease-out down
        else        k = std::cos(((p - df) / (1.0f - df)) * HALF_PI);     // smooth ease back up
        return -s.depth * k;
    }
private:
    float t_ = 0.0f; bool active_ = false;
};

// ---- settings file shared with CheesyProxxy.exe: C:\Users\Public\CheesyProxxy\bob.cfg
// 8 bytes, little endian: dword magic "CBOB" (0x424F4243), dword on (0 = off, nonzero = on).
constexpr uint32_t kCfgMagic = 0x424F4243u;
inline bool parseCfg(const unsigned char* b, size_t n, bool* on) {
    if (!b || n < 8 || !on) return false;
    uint32_t m = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    uint32_t f = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    if (m != kCfgMagic) return false;
    *on = (f != 0);
    return true;
}

} // namespace cpbob
