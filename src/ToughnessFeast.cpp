// ToughnessFeast — RE_Kenshi plugin
// Export: C++ mangled ?startPlugin@@YAXXZ (NOT extern "C")
//
// EnableHooks=1 in config installs combat soft-cap + food limb regen.
// Set EnableHooks=0 to load-only if diagnosing crashes.

#if defined(TOUGHNESSFEAST_LINUX_IDE)
#include "kenshi_ide_stubs.h"
#else
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/CharStats.h>
#include <kenshi/Character.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Enums.h>
#include <kenshi/RaceData.h>
#include <kenshi/GameData.h>
#include <kenshi/util/StringPair.h>
#include <kenshi/util/lektor.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#if !defined(_MSC_VER) && !defined(TOUGHNESSFEAST_LINUX_IDE)
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
static int tf_strncpy_s(char* dest, size_t destsz, const char* src, size_t)
{
    if (!dest || !destsz) return 1;
    std::snprintf(dest, destsz, "%s", src ? src : "");
    return 0;
}
#define strncpy_s tf_strncpy_s
#endif

struct Config
{
    float combatCapToughness;
    float foodRegenStart;       // human / default unlock
    float foodRegenStartShek;
    float foodRegenStartHiver;
    float foodRegenScalePerPoint;
    float foodRegenScaleHiver;
    float fleshHealPerSecond;
    float stunHealPerSecond;
    float hungerDrainPerSecond;
    float minHungerToRegen;
    int healUnhealable;
    float limbRegrowPerSecond;      // VERY slow stump rebuild
    float limbBudThreshold;         // stump flesh % before setLimb
    float limbRestoredStartPct;     // flesh % right after setLimb (weak limb)
    float limbStrongPct;            // considered "mostly recovered"
    float past100XpMult;
    int debugLog;
    int useRaceHeuristics;
    int enableHooks;
    int enableMedicalHooks;         // food/limb regen (not medicalUpdate hook)
    int enableLimbRestore;          // setLimb staged restore
    int enableTooltips;             // append TF lines to toughness/hunger tooltips
};

static Config g_cfg = {
    100.f,   // combatCap
    75.f,    // human start
    50.f,    // shek
    0.f,     // hiver
    0.04f,   // scale
    0.012f,  // hiver scale
    1.2f,    // flesh heal /s (normal wounds)
    0.8f,    // stun heal /s
    0.015f,  // hunger drain base
    0.12f,   // min hunger
    1,       // heal unhealable
    0.09f,   // limb regrow /s at power 1 — full stump ~15–40+ min
    0.38f,   // bud threshold 38% before restore
    0.16f,   // spawn restored limb at 16% HP (fragile)
    0.72f,   // strong at 72%
    0.18f,   // past 100 xp
    1,       // debugLog
    1,       // race heuristics
    1,       // enableHooks
    1,       // enableMedicalHooks
    1,       // enableLimbRestore
    1        // enableTooltips
};

static char g_pluginDir[MAX_PATH] = { 0 };

static char* TrimInPlace(char* s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    if (!*s) return s;
    char* e = s + std::strlen(s) - 1;
    while (e > s && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n')) { *e = 0; --e; }
    return s;
}

static int ParseBoolC(const char* v)
{
    return (v && (v[0]=='1'||v[0]=='t'||v[0]=='T'||v[0]=='y'||v[0]=='Y')) ? 1 : 0;
}

static void ResolvePluginDir()
{
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&ResolvePluginDir, &hm))
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(hm, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
        {
            for (int i = (int)n - 1; i >= 0; --i)
            {
                if (path[i] == '\\' || path[i] == '/') { path[i] = 0; break; }
            }
            strncpy_s(g_pluginDir, path, _TRUNCATE);
        }
    }
#else
    (void)g_pluginDir;
#endif
}

static void LoadConfig()
{
    char path[MAX_PATH + 32];
    path[0] = 0;
    if (g_pluginDir[0])
        std::snprintf(path, sizeof(path), "%s\\config.ini", g_pluginDir);
    else
        std::snprintf(path, sizeof(path), "config.ini");

    FILE* f = std::fopen(path, "rb");
    if (!f && g_pluginDir[0])
    {
        std::snprintf(path, sizeof(path), "%s/config.ini", g_pluginDir);
        f = std::fopen(path, "rb");
    }
    if (!f)
    {
        DebugLog("ToughnessFeast: no config.ini (hooks stay OFF)");
        return;
    }

    char line[512];
    while (std::fgets(line, sizeof(line), f))
    {
        char* s = TrimInPlace(line);
        if (!*s || *s == ';' || *s == '#') continue;
        char* eq = std::strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = TrimInPlace(s);
        char* val = TrimInPlace(eq + 1);
        if (std::strcmp(key, "EnableHooks") == 0) g_cfg.enableHooks = ParseBoolC(val);
        else if (std::strcmp(key, "EnableMedicalHooks") == 0) g_cfg.enableMedicalHooks = ParseBoolC(val);
        else if (std::strcmp(key, "EnableAnatomyPass") == 0) { /* deprecated */ }
        else if (std::strcmp(key, "CombatCapToughness") == 0) g_cfg.combatCapToughness = (float)std::atof(val);
        else if (std::strcmp(key, "FoodRegenStartHuman") == 0 || std::strcmp(key, "FoodRegenStartOther") == 0
              || std::strcmp(key, "FoodRegenStartToughness") == 0)
            g_cfg.foodRegenStart = (float)std::atof(val);
        else if (std::strcmp(key, "FoodRegenStartShek") == 0) g_cfg.foodRegenStartShek = (float)std::atof(val);
        else if (std::strcmp(key, "FoodRegenStartHiver") == 0) g_cfg.foodRegenStartHiver = (float)std::atof(val);
        else if (std::strcmp(key, "FoodRegenScalePerPoint") == 0) g_cfg.foodRegenScalePerPoint = (float)std::atof(val);
        else if (std::strcmp(key, "FoodRegenScaleHiver") == 0) g_cfg.foodRegenScaleHiver = (float)std::atof(val);
        else if (std::strcmp(key, "FleshHealPerSecond") == 0) g_cfg.fleshHealPerSecond = (float)std::atof(val);
        else if (std::strcmp(key, "StunHealPerSecond") == 0) g_cfg.stunHealPerSecond = (float)std::atof(val);
        else if (std::strcmp(key, "HungerDrainPerSecond") == 0) g_cfg.hungerDrainPerSecond = (float)std::atof(val);
        else if (std::strcmp(key, "MinHungerToRegen") == 0) g_cfg.minHungerToRegen = (float)std::atof(val);
        else if (std::strcmp(key, "HealUnhealableWounds") == 0) g_cfg.healUnhealable = ParseBoolC(val);
        else if (std::strcmp(key, "LimbRegrowPerSecond") == 0) g_cfg.limbRegrowPerSecond = (float)std::atof(val);
        else if (std::strcmp(key, "LimbRestoreFleshPercent") == 0) g_cfg.limbBudThreshold = (float)std::atof(val);
        
        else if (std::strcmp(key, "LimbRegrowPerSecond") == 0) g_cfg.limbRegrowPerSecond = (float)std::atof(val);
        else if (std::strcmp(key, "LimbBudThreshold") == 0) g_cfg.limbBudThreshold = (float)std::atof(val);
        else if (std::strcmp(key, "LimbRestoredStartPct") == 0) g_cfg.limbRestoredStartPct = (float)std::atof(val);
        else if (std::strcmp(key, "LimbStrongPct") == 0) g_cfg.limbStrongPct = (float)std::atof(val);
        else if (std::strcmp(key, "EnableLimbRestore") == 0) g_cfg.enableLimbRestore = ParseBoolC(val);
        else if (std::strcmp(key, "EnableTooltips") == 0) g_cfg.enableTooltips = ParseBoolC(val);
        else if (std::strcmp(key, "ShowStatusHud") == 0) { /* deprecated GUI removed */ }
        else if (std::strcmp(key, "HudDelayTicks") == 0) { /* deprecated */ }
        else if (std::strcmp(key, "EnableMedicalHooks") == 0) g_cfg.enableMedicalHooks = ParseBoolC(val);
        else if (std::strcmp(key, "EnableAnatomyPass") == 0) { /* deprecated */ }
        else if (std::strcmp(key, "Past100XpMult") == 0) g_cfg.past100XpMult = (float)std::atof(val);
        else if (std::strcmp(key, "DebugLog") == 0) g_cfg.debugLog = ParseBoolC(val);
        else if (std::strcmp(key, "UseRaceHeuristics") == 0) g_cfg.useRaceHeuristics = ParseBoolC(val);
    }
    std::fclose(f);
}


// ---------------------------------------------------------------------------
// Race detection — do NOT use std::string methods on game objects (CRT ABI).
// Read MSVC-layout string bytes raw, plus POD RaceData flags.
// ---------------------------------------------------------------------------


// Toughness MUST come from game APIs — CharStats layout after std::map does not
// match modern MSVC, so stats->_toughness / raw 0x90 can read STRENGTH instead.
// STAT_TOUGHNESS = 21 in Kenshi Enums.h

// float CharStats::getStat(StatsEnumerated, bool unmodified) const
typedef float (*CharStats_getStat_fn)(const CharStats* self, StatsEnumerated what, bool unmodified);
// float& CharStats::getStatRef(StatsEnumerated)
typedef float* (*CharStats_getStatRef_fn)(CharStats* self, StatsEnumerated what);
// float CharStats::toughness() const
typedef float (*CharStats_toughness_fn)(const CharStats* self);

static CharStats_getStat_fn    g_getStat = nullptr;
static CharStats_getStatRef_fn g_getStatRef = nullptr;
static CharStats_toughness_fn  g_toughnessFn = nullptr;

static void ResolveToughnessApi()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    return;
#else
    if (g_getStat && g_getStatRef) return;
    HMODULE h = GetModuleHandleA("KenshiLib.dll");
    if (!h) return;
    auto resolve = [&](const char* mangled) -> void* {
        void* exp = (void*)GetProcAddress(h, mangled);
        if (!exp) return nullptr;
        intptr_t real = KenshiLib::GetRealAddress(exp);
        return real ? (void*)real : nullptr;
    };
    if (!g_getStat)
        g_getStat = (CharStats_getStat_fn)resolve(
            "?getStat@CharStats@@QEBAMW4StatsEnumerated@@_N@Z");
    if (!g_getStatRef)
        g_getStatRef = (CharStats_getStatRef_fn)resolve(
            "?getStatRef@CharStats@@QEAAAEAMW4StatsEnumerated@@@Z");
    if (!g_toughnessFn)
        g_toughnessFn = (CharStats_toughness_fn)resolve(
            "?toughness@CharStats@@QEBAMXZ");
    if (g_getStat) DebugLog("ToughnessFeast: getStat resolved");
    else ErrorLog("ToughnessFeast: getStat MISSING");
    if (g_getStatRef) DebugLog("ToughnessFeast: getStatRef resolved");
    if (g_toughnessFn) DebugLog("ToughnessFeast: toughness() resolved");
#endif
}

static float GetToughness(const CharStats* stats)
{
    if (!stats) return 0.f;
    ResolveToughnessApi();
    float t = -1.f;
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    // 1) getStat unmodified — same source as character sheet numbers
    if (g_getStat)
    {
        t = g_getStat(stats, STAT_TOUGHNESS, true);
    }
    // 2) toughness() accessor
    if ((t != t || t < 0.f || t > 500.f) && g_toughnessFn)
    {
        t = g_toughnessFn(stats);
    }
    // 3) getStatRef
    if ((t != t || t < 0.f || t > 500.f) && g_getStatRef)
    {
        float* pref = g_getStatRef(const_cast<CharStats*>(stats), STAT_TOUGHNESS);
        if (pref) t = *pref;
    }
#endif
    // 4) last resort: try a few known float offsets and pick plausible
    //    Prefer NOT 0x80 (strength). Documented toughness is 0x90.
    if (t != t || t < 0.f || t > 500.f)
    {
        static const size_t kTry[] = { 0x90, 0x94, 0x8C, 0x98 };
        t = 0.f;
        for (size_t off : kTry)
        {
            float v = 0.f;
            std::memcpy(&v, (const char*)(const void*)stats + off, sizeof(v));
            if (v == v && v >= 0.f && v <= 200.f)
            {
                // Prefer values that look like whole-ish skill levels
                t = v;
                if (v >= 1.f) break;
            }
        }
    }
    if (t != t || t < 0.f) t = 0.f;
    if (t > 500.f) t = 500.f;
    return t;
}

static void SetToughness(CharStats* stats, float t)
{
    if (!stats) return;
    if (t != t) return;
    if (t < 0.f) t = 0.f;
    if (t > 500.f) t = 500.f;
    ResolveToughnessApi();
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    if (g_getStatRef)
    {
        float* pref = g_getStatRef(stats, STAT_TOUGHNESS);
        if (pref)
        {
            *pref = t;
            return;
        }
    }
#endif
    // Fallback write documented offset only (never 0x80 strength)
    std::memcpy((char*)(void*)stats + 0x90, &t, sizeof(float));
}

static MedicalSystem* GetMedical(CharStats* stats)
{
    if (!stats) return nullptr;
    return stats->medical; // offset 0x8 — before map, safe
}


enum RaceKind
{
    RACE_UNKNOWN = 0,
    RACE_HUMAN,
    RACE_SHEK,
    RACE_HIVER,
    RACE_ROBOT
};

static int AsciiIContains(const char* hay, size_t n, const char* needle)
{
    if (!hay || !needle || !needle[0] || n == 0) return 0;
    size_t nl = 0;
    while (needle[nl]) ++nl;
    if (nl > n) return 0;
    for (size_t i = 0; i + nl <= n; ++i)
    {
        size_t j = 0;
        for (; j < nl; ++j)
        {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (j == nl) return 1;
    }
    return 0;
}

// MSVC x64 std::string-ish: 16-byte SSO, size@+16, capacity@+24
static int MsvcStringContains(const void* strObj, const char* needle)
{
    if (!strObj || !needle) return 0;
    const unsigned char* p = (const unsigned char*)strObj;
    size_t size = 0, res = 0;
    std::memcpy(&size, p + 16, sizeof(size));
    std::memcpy(&res, p + 24, sizeof(res));
    if (size == 0 || size > 256) return 0;
    if (res > 0x100000u) return 0;
    const char* data = nullptr;
    if (res < 16u)
        data = (const char*)p;
    else
        std::memcpy(&data, p, sizeof(data));
    if (!data) return 0;
    // sanity: mostly printable
    size_t check = size < 64 ? size : 64;
    for (size_t i = 0; i < check; ++i)
    {
        unsigned char c = (unsigned char)data[i];
        if (c < 9) return 0;
    }
    return AsciiIContains(data, size, needle);
}

static Character* CharFromStats(CharStats* stats)
{
    if (!stats) return nullptr;
    if (stats->me) return stats->me;
    if (stats->medical && stats->medical->me) return stats->medical->me;
    return nullptr;
}

static RaceData* RaceFromStats(CharStats* stats)
{
    Character* me = CharFromStats(stats);
    if (!me) return nullptr;
    // Do NOT read me->myRace: Character "hand" size mismatch in headers puts
    // myRace at 0x2D0 in our build vs 0x2E0 in game → garbage pointer → AV at race+0x7c.
    // Always use virtual getRace() (RVA via vtable).
    RaceData* race = me->getRace();
    return race;
}

static RaceKind DetectRaceKind(CharStats* stats)
{
    if (!stats) return RACE_UNKNOWN;
#if defined(_MSC_VER)
    __try
    {
#endif
    RaceData* race = RaceFromStats(stats);
    if (!race) return RACE_UNKNOWN;

    // Validate race pointer roughly (reject low/null-ish)
    {
        uintptr_t rp = (uintptr_t)race;
        if (rp < 0x10000ull) return RACE_UNKNOWN;
    }

    if (race->robot) return RACE_ROBOT;
    if (race->gigantic) return RACE_SHEK;

    if (race->data)
    {
        const char* base = (const char*)race->data;
        uintptr_t bp = (uintptr_t)base;
        if (bp > 0x10000ull)
        {
            if (MsvcStringContains(base + 0x58, "hive") ||
                MsvcStringContains(base + 0x28, "hive") ||
                MsvcStringContains(base + 0x58, "hiver") ||
                MsvcStringContains(base + 0x28, "hiver"))
                return RACE_HIVER;
            if (MsvcStringContains(base + 0x58, "shek") ||
                MsvcStringContains(base + 0x28, "shek"))
                return RACE_SHEK;
            if (MsvcStringContains(base + 0x58, "skeleton") ||
                MsvcStringContains(base + 0x28, "skeleton"))
                return RACE_ROBOT;
        }
    }

    if (race->noHats && (race->noShoes || race->noShirts))
        return RACE_HIVER;
    if (race->singleGender && race->hungerRate > 1.0f)
        return RACE_HIVER;
    if (race->hungerRate > 1.05f)
        return RACE_HIVER;

    return RACE_HUMAN;
#if defined(_MSC_VER)
    }
    __except (1)
    {
        return RACE_UNKNOWN;
    }
#endif
}

static const char* RaceKindName(RaceKind k)
{
    switch (k)
    {
    case RACE_SHEK: return "shek";
    case RACE_HIVER: return "hiver";
    case RACE_ROBOT: return "robot";
    case RACE_HUMAN: return "human";
    default: return "unknown";
    }
}

static float FoodRegenStartFor(CharStats* stats)
{
    if (!g_cfg.useRaceHeuristics) return g_cfg.foodRegenStart;
    RaceKind k = DetectRaceKind(stats);
    if (k == RACE_SHEK) return g_cfg.foodRegenStartShek;
    if (k == RACE_HIVER) return g_cfg.foodRegenStartHiver;
    if (k == RACE_ROBOT) return 9999.f; // no food regen for robots
    return g_cfg.foodRegenStart;
}

static float FoodRegenScaleFor(CharStats* stats)
{
    if (!g_cfg.useRaceHeuristics) return g_cfg.foodRegenScalePerPoint;
    if (DetectRaceKind(stats) == RACE_HIVER) return g_cfg.foodRegenScaleHiver;
    return g_cfg.foodRegenScalePerPoint;
}

// ---------------------------------------------------------------------------
// Tooltips (no MyGUI window) — append lines into game lektor<StringPair>
// Uses raw MSVC-layout short strings + game StringPair ctor (avoids CRT ABI).
// ---------------------------------------------------------------------------

// Game-compatible short/long strings for StringPair ctor (no our std::string).
// SSO if len < 16; else pointer into a rotating pool (lives long enough for tooltip paint).

struct GameStr
{
    char data[16]; // SSO buffer OR first 8 bytes = char* when heap/pool
    size_t size;
    size_t cap;
};

static char g_strPool[48][96];
static int g_strPoolI = 0;

static void GameStrSet(GameStr* s, const char* text)
{
    std::memset(s, 0, sizeof(*s));
    if (!text) text = "";
    size_t n = 0;
    while (text[n] && n < 95) ++n;

    if (n < 16)
    {
        std::memcpy(s->data, text, n);
        s->data[n] = 0;
        s->size = n;
        s->cap = 15;
    }
    else
    {
        char* slot = g_strPool[g_strPoolI];
        g_strPoolI = (g_strPoolI + 1) % 48;
        std::memcpy(slot, text, n);
        slot[n] = 0;
        // MSVC heap/pool string: store pointer in first 8 bytes
        char* ptr = slot;
        std::memcpy(s->data, &ptr, sizeof(ptr));
        s->size = n;
        s->cap = 95; // >= 16 => non-SSO
    }
}

typedef void* (*StringPairCtorFn)(void* self, const GameStr* a, const GameStr* b);
static StringPairCtorFn g_spCtor = nullptr;

static void ResolveStringPairCtor()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    return;
#else
    if (g_spCtor) return;
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) exe = GetModuleHandleA("kenshi_GOG_x64.exe");
    if (!exe) exe = GetModuleHandleA("kenshi_x64.exe");
    if (!exe) return;
    g_spCtor = (StringPairCtorFn)((unsigned char*)exe + 0xF32C0);
#endif
}

static int LektorAppendPair(lektor<StringPair>* dats, const char* left, const char* right)
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    (void)dats; (void)left; (void)right;
    return 0;
#else
    if (!dats || !dats->stuff) return 0;
    if (dats->maxSize == 0) return 0;
    ResolveStringPairCtor();
    if (!g_spCtor) return 0;

    static const size_t kGameStringPairSize = 0x60;

    // NEVER realloc stuff (game owns the buffer / frees it). If full, drop
    // the last vanilla line to make one slot — safe for tooltip lifetime.
    if (dats->count >= dats->maxSize)
    {
        if (dats->maxSize < 1) return 0;
        dats->count = dats->maxSize - 1;
    }

    GameStr a, b;
    GameStrSet(&a, left ? left : "");
    GameStrSet(&b, right ? right : "");

    void* slot = (char*)(void*)dats->stuff + (size_t)dats->count * kGameStringPairSize;
    std::memset(slot, 0, kGameStringPairSize);
    g_spCtor(slot, &a, &b);
    dats->count += 1;
    return 1;
#endif
}

static float RegenPowerOf(CharStats* stats)
{
    if (!stats) return 0.f;
    float un = FoodRegenStartFor(stats);
    float tough = GetToughness(stats);
    if (tough <= un || un >= 9000.f) return 0.f;
    float pwr = (tough - un) * FoodRegenScaleFor(stats);
    if (pwr > 3.5f) pwr = 3.5f;
    if (pwr < 0.f) pwr = 0.f;
    return pwr;
}

// Returns 0..100 food-% per second estimate while regenerating
static float FoodUsePercentPerSec(CharStats* stats)
{
    float pwr = RegenPowerOf(stats);
    if (pwr <= 0.f) return 0.f;
    // hunger is 0..1; config is fraction/sec at power scale
    float drain = g_cfg.hungerDrainPerSecond * pwr;
    // plus typical limb work overhead while missing limbs
    int missing = 0;
    MedicalSystem* med = stats->medical;
    if (med && med->robotLimbs)
    {
        static const RobotLimbs::Limb kL[4] = {
            RobotLimbs::LEFT_ARM, RobotLimbs::RIGHT_ARM,
            RobotLimbs::LEFT_LEG, RobotLimbs::RIGHT_LEG
        };
        for (int i = 0; i < 4; ++i)
        {
            MedicalSystem::HealthPartStatus* part = med->getPart(kL[i]);
            if (!part || part->isRobotic()) continue;
            LimbState st = part->getRobotLimbState();
            if (st == LIMB_STUMP || st == LIMB_CRUSHED)
                ++missing;
            else
            {
                float maxHp = part->maxHealth();
                if (maxHp < 1.f) maxHp = part->_maxHealth;
                if (maxHp > 1.f && part->flesh < maxHp * g_cfg.limbStrongPct)
                    ++missing;
            }
        }
    }
    if (missing > 0)
        drain += 0.004f * pwr * (float)missing;
    return drain * 100.f; // percent of full-bar per second
}

struct LimbTip
{
    char name[16];
    char stage[28];   // Budding / Strengthening / Healthy...
    char detail[48];  // progress sentence
    int active;       // needs attention
};

static MedicalSystem::HealthPartStatus* ResolveLimbPart(MedicalSystem* med, int slot)
{
    // slot: 0 LArm 1 RArm 2 LLeg 3 RLeg
    if (!med) return nullptr;
    static const RobotLimbs::Limb kLimbs[4] = {
        RobotLimbs::LEFT_ARM, RobotLimbs::RIGHT_ARM,
        RobotLimbs::LEFT_LEG, RobotLimbs::RIGHT_LEG
    };
    MedicalSystem::HealthPartStatus* part = med->getPart(kLimbs[slot]);
    if (part) return part;

    // PartType + side (more reliable for legs on some races)
    using PT = MedicalSystem::HealthPartStatus::PartType;
    if (slot == 0) part = med->getPart(PT::PART_ARM, SIDE_LEFT);
    else if (slot == 1) part = med->getPart(PT::PART_ARM, SIDE_RIGHT);
    else if (slot == 2) part = med->getPart(PT::PART_LEG, SIDE_LEFT);
    else if (slot == 3) part = med->getPart(PT::PART_LEG, SIDE_RIGHT);
    if (part) return part;

    // Raw MedicalSystem limb pointers
    static const int kOff[4] = { 0x90, 0x98, 0x80, 0x88 }; // LArm RArm LLeg RLeg
    MedicalSystem::HealthPartStatus* raw = nullptr;
    std::memcpy(&raw, (const char*)(void*)med + kOff[slot], sizeof(raw));
    if (raw && (uintptr_t)raw > 0x10000ull)
        return raw;
    return nullptr;
}

static int CollectLimbTips(CharStats* stats, LimbTip* out, int maxOut)
{
    if (!stats || !out || maxOut <= 0) return 0;
    MedicalSystem* med = stats->medical;
    if (!med) return 0;

#if defined(_MSC_VER)
    __try
    {
#endif
    static const char* kNames[4] = { "Left Arm", "Right Arm", "Left Leg", "Right Leg" };

    int n = 0;
    for (int i = 0; i < 4 && n < maxOut; ++i)
    {
        MedicalSystem::HealthPartStatus* part = ResolveLimbPart(med, i);

        LimbTip& tip = out[n];
        std::memset(&tip, 0, sizeof(tip));
        std::snprintf(tip.name, sizeof(tip.name), "%s", kNames[i]);

        if (!part || (uintptr_t)part < 0x10000ull)
        {
            std::snprintf(tip.stage, sizeof(tip.stage), "n/a");
            std::snprintf(tip.detail, sizeof(tip.detail), "-");
            tip.active = 0;
            ++n;
            continue;
        }

        if (part->isRobotic())
        {
            std::snprintf(tip.stage, sizeof(tip.stage), "Prosthetic");
            std::snprintf(tip.detail, sizeof(tip.detail), "skip");
            tip.active = 0;
            ++n;
            continue;
        }

        LimbState st = part->getRobotLimbState();
        float maxHp = part->maxHealth();
        if (maxHp < 1.f) maxHp = part->_maxHealth;
        if (maxHp < 1.f) maxHp = 100.f;
        float rawFlesh = part->flesh;
        if (rawFlesh != rawFlesh) rawFlesh = 0.f;
        float flesh = rawFlesh > 0.f ? rawFlesh : 0.f;
        float pct = flesh / maxHp;
        if (pct > 1.2f) pct = 1.2f;

        if (st == LIMB_STUMP || st == LIMB_CRUSHED)
        {
            float prog = (g_cfg.limbBudThreshold > 0.01f && maxHp > 1.f)
                ? (flesh / maxHp) / g_cfg.limbBudThreshold : 0.f;
            if (prog > 1.f) prog = 1.f;
            if (prog < 0.f) prog = 0.f;
            std::snprintf(tip.stage, sizeof(tip.stage), "%s",
                st == LIMB_CRUSHED ? "Crushed" : "MISSING");
            std::snprintf(tip.detail, sizeof(tip.detail),
                "bud%.0f HP%.0f", prog * 100.f, rawFlesh);
            tip.active = 1;
        }
        else if (st == LIMB_REPLACED)
        {
            std::snprintf(tip.stage, sizeof(tip.stage), "Replaced");
            std::snprintf(tip.detail, sizeof(tip.detail), "no regrow");
            tip.active = 0;
        }
        else
        {
            if (pct < g_cfg.limbRestoredStartPct + 0.08f)
            {
                std::snprintf(tip.stage, sizeof(tip.stage), "Fragile");
                std::snprintf(tip.detail, sizeof(tip.detail), "HP%.0f%%", pct * 100.f);
                tip.active = 1;
            }
            else if (pct < g_cfg.limbStrongPct)
            {
                std::snprintf(tip.stage, sizeof(tip.stage), "Healing");
                std::snprintf(tip.detail, sizeof(tip.detail), "HP%.0f%%", pct * 100.f);
                tip.active = 1;
            }
            else
            {
                std::snprintf(tip.stage, sizeof(tip.stage), "OK");
                std::snprintf(tip.detail, sizeof(tip.detail), "HP%.0f%%", pct * 100.f);
                tip.active = 0;
            }
        }
        ++n;
    }
    return n;
#if defined(_MSC_VER)
    }
    __except (1)
    {
        return 0;
    }
#endif
}

// Toughness hover: overview only (easy to scan)
static int g_tooltipOnce = 0;


// Truncate lektor at our previous TF header so values can refresh each hover.
static void StripPreviousTfBlock(lektor<StringPair>* dats)
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    (void)dats;
#else
    if (!dats || !dats->stuff || dats->count == 0) return;
    static const size_t kPair = 0x60;
    unsigned n = dats->count;
    if (n > 64) n = 64;
    for (unsigned i = 0; i < n; ++i)
    {
        const char* base = (const char*)(void*)dats->stuff + (size_t)i * kPair;
        const unsigned char* sp = (const unsigned char*)base + 0x8;
        size_t size = 0, res = 0;
        std::memcpy(&size, sp + 16, sizeof(size));
        std::memcpy(&res, sp + 24, sizeof(res));
        if (size == 0 || size > 80 || res > 0x200000u) continue;
        const char* data = nullptr;
        if (res < 16u) data = (const char*)sp;
        else std::memcpy(&data, sp, sizeof(data));
        if (!data) continue;
        if (std::strncmp(data, "== Toughness Feast", 18) == 0
            || std::strncmp(data, "========", 8) == 0
            || std::strncmp(data, "Toughness Feast", 15) == 0
            || std::strncmp(data, "-- Limbs --", 11) == 0)
        {
            dats->count = i;
            return;
        }
    }
#endif
}



// Live TF status snapshot (updated from regen tick — tooltips read this so numbers stay current)
struct TfLiveStatus
{
    float toughness;
    float power;
    float hungerPct;
    float foodUse;
    float unlock;
    int activeLimbs;
    char race[16];
    char limbLine[4][64]; // "Left Arm|MISSING|detail"
    int nLimbs;
    int valid;
};
static TfLiveStatus g_tfLive = {};

static void RefreshTfLiveStatus(CharStats* stats)
{
    if (!stats) return;
    std::memset(&g_tfLive, 0, sizeof(g_tfLive));
    g_tfLive.toughness = GetToughness(stats);
    g_tfLive.unlock = FoodRegenStartFor(stats);
    g_tfLive.power = RegenPowerOf(stats);
    g_tfLive.foodUse = FoodUsePercentPerSec(stats);
    const char* rn = RaceKindName(DetectRaceKind(stats));
    std::snprintf(g_tfLive.race, sizeof(g_tfLive.race), "%s", rn ? rn : "?");
    MedicalSystem* med = stats->medical;
    if (med && !med->dead)
    {
        float h = med->hunger;
        if (h == h && h >= 0.f && h <= 5.f)
            g_tfLive.hungerPct = h * 100.f;
        else
            g_tfLive.hungerPct = -1.f;
        LimbTip limbs[4];
        int n = CollectLimbTips(stats, limbs, 4);
        g_tfLive.nLimbs = n;
        g_tfLive.activeLimbs = 0;
        for (int i = 0; i < n; ++i)
        {
            if (limbs[i].active) g_tfLive.activeLimbs++;
            std::snprintf(g_tfLive.limbLine[i], sizeof(g_tfLive.limbLine[i]),
                "%s|%s|%s", limbs[i].name, limbs[i].stage, limbs[i].detail);
        }
    }
    g_tfLive.valid = 1;
}

// SAFE hunger tooltip only — short fixed lines, no anatomy scan, no GameData names.
// Expanded body-bar scanner caused ACCESS_VIOLATION (dump c0000005).
static void AppendFullTfTooltips(lektor<StringPair>* dats, CharStats* stats)
{
    if (!dats || !stats) return;
    if (!dats->stuff || dats->maxSize < 8) return;

    StripPreviousTfBlock(dats);

    // Carve free slots for full panel (4 limbs). Even small lektors get room.
    unsigned kNeed = 14;
    if (kNeed + 2 > dats->maxSize)
        kNeed = dats->maxSize > 4 ? dats->maxSize - 2 : 0;
    if (kNeed > 0 && dats->count + kNeed > dats->maxSize)
        dats->count = dats->maxSize - kNeed;

    const unsigned kBudget = 16;
    unsigned startCount = dats->count;
    auto room = [&]() -> int {
        if (dats->count >= dats->maxSize) return 0;
        if (dats->count - startCount >= kBudget) return 0;
        return 1;
    };
    auto line = [&](const char* a, const char* b) {
        if (!room()) return;
        LektorAppendPair(dats, a, b);
    };

    // LIVE toughness every hover (do not cache)
    float toughNow = GetToughness(stats);
    if (toughNow != toughNow || toughNow < 0.f) toughNow = 0.f;
    if (toughNow > 500.f) toughNow = 500.f;

    float un = FoodRegenStartFor(stats);
    float scale = FoodRegenScaleFor(stats);
    float pwr = 0.f;
    if (toughNow > un && un < 9000.f)
    {
        pwr = (toughNow - un) * scale;
        if (pwr > 3.5f) pwr = 3.5f;
    }
    float foodUse = (pwr > 0.f) ? (g_cfg.hungerDrainPerSecond * pwr * 100.f) : 0.f;

    RaceKind rk = DetectRaceKind(stats);
    MedicalSystem* med = stats->medical;
    float hungerPct = -1.f;
    if (med && !med->dead)
    {
        float h = med->hunger;
        if (h == h && h >= 0.f && h <= 5.f)
            hungerPct = h * 100.f;
    }

    line("== Toughness Feast ==", "live");
    {
        char r[40];
        const char* race = RaceKindName(rk);
        std::snprintf(r, sizeof(r), "%s", race ? race : "?");
        if (r[0] >= 'a' && r[0] <= 'z') r[0] = (char)(r[0] - 32);
        line("Race", r);
    }
    {
        char r[40];
        // Integer like HUD (no fake decimals from wrong field)
        int tInt = (int)(toughNow + 0.5f);
        if (tInt < 0) tInt = 0;
        std::snprintf(r, sizeof(r), "%d (cap %.0f)", tInt, g_cfg.combatCapToughness);
        line("Toughness", r);
    }
    if (hungerPct >= 0.f)
    {
        char r[40];
        std::snprintf(r, sizeof(r), "%.0f%%", hungerPct);
        line("Hunger", r);
    }
    if (pwr <= 0.f)
    {
        char r[40];
        std::snprintf(r, sizeof(r), "LOCK need %.0f", un);
        line("Food regen", r);
    }
    else
    {
        char r[40];
        std::snprintf(r, sizeof(r), "ON p%.2f ~%.1f%%/s", pwr, foodUse);
        line("Food regen", r);
    }

    LimbTip limbs[4];
    int n = 0;
    if (med && !med->dead)
        n = CollectLimbTips(stats, limbs, 4);

    int active = 0;
    line("-- Limbs --", "all 4");
    if (n <= 0)
        line("Limbs", "no data");
    else
    {
        for (int i = 0; i < n; ++i)
        {
            if (limbs[i].active) ++active;
            char right[56];
            std::snprintf(right, sizeof(right), "%s %s", limbs[i].stage, limbs[i].detail);
            line(limbs[i].name, right);
        }
        char r[40];
        if (active <= 0) std::snprintf(r, sizeof(r), "all OK");
        else std::snprintf(r, sizeof(r), "%d healing", active);
        line("Summary", r);
    }
    line("== end TF ==", "-");

    static int s_tipLog = 0;
    if (g_cfg.debugLog && ((++s_tipLog) % 5) == 1)
    {
        char mbuf[160];
        std::snprintf(mbuf, sizeof(mbuf),
            "ToughnessFeast: tip t=%.1f p=%.2f limbs=%d max=%u",
            toughNow, pwr, n, (unsigned)dats->maxSize);
        DebugLog(mbuf);
    }
}

static void AppendHungerTooltips(lektor<StringPair>* dats, CharStats* stats)
{
    // Hunger tooltip path is stable; put the whole TF panel here.
    AppendFullTfTooltips(dats, stats);
}


// Still build short log line (no GUI)
static CharStats* g_lastStats = nullptr;

static int g_statusLogCooldown = 0;
static int g_raceLogged = 0;

static void MaybeLogStatus(CharStats* stats)
{
    if (!stats || !g_cfg.debugLog) return;
    if (!g_raceLogged)
    {
        RaceData* race = RaceFromStats(stats);
        RaceKind rk = DetectRaceKind(stats);
        char msg[256];
        if (race)
            std::snprintf(msg, sizeof(msg),
                "ToughnessFeast: race=%s hungerRate=%.2f unlock=%.0f",
                RaceKindName(rk), race->hungerRate, FoodRegenStartFor(stats));
        else
            std::snprintf(msg, sizeof(msg), "ToughnessFeast: race=%s", RaceKindName(rk));
        DebugLog(msg);
        g_raceLogged = 1;
    }
    if (++g_statusLogCooldown < 40) return;
    g_statusLogCooldown = 0;
    RaceKind rk = DetectRaceKind(stats);
    char shortLog[192];
    float un = FoodRegenStartFor(stats);
    float pwr = 0.f;
    if (GetToughness(stats) > un && un < 9000.f)
        pwr = (GetToughness(stats) - un) * FoodRegenScaleFor(stats);
    std::snprintf(shortLog, sizeof(shortLog),
        "TF [%s] t=%.1f unlock=%.0f p=%.2f",
        RaceKindName(rk), GetToughness(stats), un, pwr);
    DebugLog(shortLog);
}

// Lightweight refresh used by combat hooks (log only)
static void RefreshStatusHud(CharStats* preferStats)
{
    if (preferStats)
        MaybeLogStatus(preferStats);
}

static float RegenPowerFromStats(CharStats* stats)
{
    if (!stats) return 0.f;
    float tough = GetToughness(stats);
    if (tough < 0.f || tough > 500.f) return 0.f;
    float excess = tough - FoodRegenStartFor(stats);
    if (excess <= 0.f) return 0.f;
    float power = excess * FoodRegenScaleFor(stats);
    if (power > 3.5f) power = 3.5f;
    return power;
}

// Combat feedback while a natural limb is regrowing / weak:
// scale game injury-facing multipliers on CharStats (reapplied each tick).
static void ApplyRegrowthCombatPenalty(CharStats* stats, float severity01)
{
    if (!stats) return;
    if (severity01 < 0.f) severity01 = 0.f;
    if (severity01 > 1.f) severity01 = 1.f;
    // Keep some fight left in them — never zero out completely
    float dmgMul = 1.f - 0.55f * severity01;   // up to -55% damage mult
    float dexMul = 1.f - 0.45f * severity01;   // up to -45% dex
    float spdMul = 1.f - 0.35f * severity01;   // up to -35% combat speed
    float dodgeMul = 1.f - 0.40f * severity01;

    if (stats->skillMultDamage > 0.05f)
        stats->skillMultDamage *= dmgMul;
    if (stats->skillMultDexterity > 0.05f)
        stats->skillMultDexterity *= dexMul;
    if (stats->combatSpeedMultiplier > 0.05f)
        stats->combatSpeedMultiplier *= spdMul;
    if (stats->skillMultDodge > 0.05f)
        stats->skillMultDodge *= dodgeMul;
}

// Stages (per limb, driven by LimbState + flesh%):
//  0 MISSING  — STUMP / CRUSHED: slowly bud flesh on the stump (very slow)
//  1 BUDDING  — still stump, flesh climbing toward LimbBudThreshold
//  2 RESTORED_WEAK — setLimb(ORIGINAL), flesh forced to LimbRestoredStartPct
//  3 STRENGTHENING — flesh < LimbStrongPct, fights poorly (game injury + our mults)
//  4 HEALTHY  — flesh >= LimbStrongPct, no extra penalty
//
// Combat ability follows flesh% after restore (and missing limbs while stump).

static void ProcessLimbRegrowth(
    MedicalSystem* med,
    CharStats* stats,
    float power,
    float frameTime,
    float& hungerCost,
    int& anyHeal,
    float& worstSeverity)
{
    if (!med || !stats || !g_cfg.enableLimbRestore) return;
    if (power <= 0.f) return;

    RobotLimbs* robots = med->robotLimbs;
    // robots may be null early; still try flesh bud via ResolveLimbPart
    float regrowBudget = g_cfg.limbRegrowPerSecond * power * frameTime;
    if (regrowBudget <= 0.f) return;

    static const RobotLimbs::Limb kLimbs[4] = {
        RobotLimbs::LEFT_ARM, RobotLimbs::RIGHT_ARM,
        RobotLimbs::LEFT_LEG, RobotLimbs::RIGHT_LEG
    };

#if defined(_MSC_VER)
    __try
    {
#endif
    for (int i = 0; i < 4; ++i)
    {
        RobotLimbs::Limb limbEnum = kLimbs[i];
        MedicalSystem::HealthPartStatus* part = ResolveLimbPart(med, i);
        if (!part) continue;
        if (part->isRobotic()) continue;

        LimbState state = part->getRobotLimbState();
        LimbState tableState = state;
        if (robots)
            tableState = robots->getState(limbEnum);
        if (state == LIMB_REPLACED || tableState == LIMB_REPLACED)
            continue;

        float maxHp = part->maxHealth();
        if (maxHp < 1.f) maxHp = part->_maxHealth;
        if (maxHp < 1.f || maxHp > 10000.f) continue;

        float flesh = part->flesh;
        if (flesh != flesh) continue;
        if (flesh > maxHp * 3.f) flesh = maxHp;

        int missing = (state == LIMB_STUMP || state == LIMB_CRUSHED
                    || tableState == LIMB_STUMP || tableState == LIMB_CRUSHED) ? 1 : 0;

        if (missing)
        {
            float rate = (state == LIMB_CRUSHED || tableState == LIMB_CRUSHED) ? 0.55f : 1.f;
            if (worstSeverity < 0.95f) worstSeverity = 0.95f;

            // Phase A: heal overdamage (negative HP) toward 0 — no setLimb
            if (flesh < 0.f)
            {
                float need = -flesh;
                float take = need;
                float room = regrowBudget * rate;
                if (take > room) take = room;
                if (take > 0.f)
                {
                    part->flesh = flesh + take;
                    regrowBudget -= take / (rate > 0.01f ? rate : 1.f);
                    hungerCost += take * 1.0f;
                    anyHeal = 1;
                    flesh = part->flesh;
                }
                continue; // do not restore while still overdamaged
            }

            // Phase B: bud positive flesh on stump (still STUMP/CRUSHED)
            float target = maxHp * g_cfg.limbBudThreshold;
            if (target < 5.f) target = maxHp * 0.38f;

            if (flesh < target && regrowBudget > 0.f)
            {
                float need = target - flesh;
                float take = need;
                float room = regrowBudget * rate;
                if (take > room) take = room;
                if (take > 0.f)
                {
                    // Only write flesh — never updateDerivedHealths on stump (crashy)
                    part->flesh = flesh + take;
                    regrowBudget -= take / (rate > 0.01f ? rate : 1.f);
                    hungerCost += take * 1.1f;
                    anyHeal = 1;
                    flesh = part->flesh;
                }
            }

            // Phase C: restore organic limb once bud threshold met
            // setLimb invalidates part* — MUST re-fetch before any further writes
            if (robots && flesh >= maxHp * g_cfg.limbBudThreshold * 0.98f)
            {
                robots->setLimb(limbEnum, LIMB_ORIGINAL, nullptr);

                // Re-resolve AFTER setLimb (old part pointer is dead)
                part = ResolveLimbPart(med, i);
                if (!part)
                {
                    anyHeal = 1;
                    if (g_cfg.debugLog)
                        DebugLog("ToughnessFeast: setLimb ok but part lost");
                    continue;
                }

                float start = maxHp * g_cfg.limbRestoredStartPct;
                if (start < 1.f) start = maxHp * 0.16f;
                if (start > maxHp * 0.4f) start = maxHp * 0.16f;

                part->flesh = start;
                if (part->fleshStun < maxHp * 0.4f)
                    part->fleshStun = maxHp * 0.4f;
                // Soft derived update only after restore, when part is ORIGINAL
#if defined(_MSC_VER)
                __try { part->updateDerivedHealths(); }
                __except (1) { /* ignore */ }
#else
                part->updateDerivedHealths();
#endif
                anyHeal = 1;
                hungerCost += maxHp * 0.25f;
                if (worstSeverity < 0.85f) worstSeverity = 0.85f;

                if (g_cfg.debugLog)
                {
                    char msg[160];
                    std::snprintf(msg, sizeof(msg),
                        "ToughnessFeast: limb RESTORED weak slot %d flesh=%.0f/%.0f",
                        i, start, maxHp);
                    DebugLog(msg);
                }
            }
            else if (g_cfg.debugLog && flesh > 1.f)
            {
                int step = (int)(flesh / maxHp * 10.f);
                static int s_lastStep[4] = { -1, -1, -1, -1 };
                if (step != s_lastStep[i])
                {
                    s_lastStep[i] = step;
                    char msg[160];
                    std::snprintf(msg, sizeof(msg),
                        "ToughnessFeast: BUDDING slot %d %.0f%% need %.0f%%",
                        i, flesh / maxHp * 100.f, g_cfg.limbBudThreshold * 100.f);
                    DebugLog(msg);
                }
            }
        }
        else if (state == LIMB_ORIGINAL || tableState == LIMB_ORIGINAL)
        {
            float pct = (maxHp > 0.f) ? (flesh / maxHp) : 1.f;
            if (pct < 0.f) pct = 0.f;

            if (pct < g_cfg.limbStrongPct)
            {
                float sev = 1.f - (pct / (g_cfg.limbStrongPct > 0.1f ? g_cfg.limbStrongPct : 0.72f));
                if (sev > worstSeverity) worstSeverity = sev;

                if (regrowBudget > 0.f && flesh < maxHp)
                {
                    float need = maxHp - flesh;
                    float take = need;
                    float room = regrowBudget * 0.65f;
                    if (take > room) take = room;
                    if (take > 0.f)
                    {
                        part->flesh = flesh + take;
                        regrowBudget -= take / 0.65f;
                        hungerCost += take * 0.35f;
                        anyHeal = 1;
                        if (part->fleshStun > 0.f)
                        {
                            float st = part->fleshStun;
                            float stTake = take * 0.8f;
                            if (stTake > st) stTake = st;
                            part->fleshStun -= stTake;
                        }
                    }
                }
            }
        }
    }
#if defined(_MSC_VER)
    }
    __except (1)
    {
        static int s_once = 0;
        if (!s_once) { ErrorLog("ToughnessFeast: ProcessLimbRegrowth SEH"); s_once = 1; }
    }
#endif
}

static int g_inFoodRegen = 0;

static void ApplyFoodRegenFromStats(CharStats* stats, float frameTime)
{
    if (!g_cfg.enableMedicalHooks) return;
    if (g_inFoodRegen) return;
    if (!stats) return;
    if (frameTime <= 0.f) return;
    if (frameTime > 0.25f) frameTime = 0.25f;

    float power = RegenPowerFromStats(stats);

    // Always refresh HUD (even with 0 power — shows unlock threshold)
    g_lastStats = stats;
    RefreshTfLiveStatus(stats);
    RefreshStatusHud(stats);

    if (power <= 0.f) return;

    MedicalSystem* med = stats->medical;
    if (!med) return;
    if (med->dead) return;

    Character* me = stats->me;
    if (!me) return;
    if (!me->amSomeoneWhoNeedsToEatToLive()) return;

    float hunger = med->hunger;
    if (hunger < g_cfg.minHungerToRegen)
    {
        RefreshStatusHud(stats);
        return;
    }
    if (hunger < 0.f || hunger > 5.f) return;

    float fleshBudget = g_cfg.fleshHealPerSecond * power * frameTime;
    float stunBudget = g_cfg.stunHealPerSecond * power * frameTime;
    float hungerCost = 0.f;
    int anyHeal = 0;
    float worstSeverity = 0.f;

    // --- staged limb restore (slow) ---
    g_inFoodRegen = 1;
    ProcessLimbRegrowth(med, stats, power, frameTime, hungerCost, anyHeal, worstSeverity);

    // --- general flesh wounds (non-limb / residual) via getPart index ---
    int count = med->getPartCount();
    if (count < 0) count = 0;
    if (count > 32) count = 32;

    for (int i = 0; i < count; ++i)
    {
        MedicalSystem::HealthPartStatus* part = med->getPart((unsigned long long)i);
        if (!part) continue;
        if (part->isRobotic()) continue;

        LimbState ls = part->getRobotLimbState();
        // Stumps handled in ProcessLimbRegrowth
        if (ls == LIMB_STUMP || ls == LIMB_CRUSHED || ls == LIMB_REPLACED)
            continue;

        float maxHp = part->maxHealth();
        if (maxHp < 1.f) maxHp = part->_maxHealth;
        if (maxHp < 1.f || maxHp > 10000.f) continue;

        float flesh = part->flesh;
        if (flesh != flesh) continue;
        if (flesh > maxHp * 3.f) continue;

        if (part->fleshStun > 0.f && stunBudget > 0.f)
        {
            float st = part->fleshStun;
            if (st > 0.f && st < 1e6f)
            {
                if (st > stunBudget) st = stunBudget;
                part->fleshStun -= st;
                stunBudget -= st;
                hungerCost += st * 0.08f;
                anyHeal = 1;
            }
        }

        if (flesh < maxHp && fleshBudget > 0.f)
        {
            int isOver = (flesh < 0.f) ? 1 : 0;
            if (isOver && !g_cfg.healUnhealable) continue;
            float rate = isOver ? 0.35f : 1.f;
            float need = isOver ? (-flesh + 0.5f) : (maxHp - flesh);
            if (need <= 0.f) continue;
            float room = fleshBudget * rate;
            float take = need;
            if (take > room) take = room;
            part->flesh = flesh + take;
            fleshBudget -= take / (rate > 0.01f ? rate : 1.f);
            hungerCost += take * (isOver ? 0.4f : 0.15f);
            anyHeal = 1;
        }
    }

    // Combat integration: while any limb is missing/weak, fight worse
    if (worstSeverity > 0.05f)
        ApplyRegrowthCombatPenalty(stats, worstSeverity);

    if (anyHeal && hungerCost > 0.f)
    {
        float drain = g_cfg.hungerDrainPerSecond * power * frameTime;
        drain += hungerCost * 0.0012f;
        float next = hunger - drain;
        if (next < 0.f) next = 0.f;
        if (next <= 5.f)
            med->hunger = next;

        if (g_cfg.debugLog)
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                "ToughnessFeast: regen t=%.1f p=%.2f h=%.3f sev=%.2f",
                GetToughness(stats), power, next, worstSeverity);
            DebugLog(msg);
        }
    }
    else if (worstSeverity > 0.05f)
    {
        // Still apply combat penalty even if no heal this tick (starving / slow)
        ApplyRegrowthCombatPenalty(stats, worstSeverity);
    }

    g_lastStats = stats;
    RefreshTfLiveStatus(stats);
    RefreshStatusHud(stats);
    g_inFoodRegen = 0;
}

// ---------------------------------------------------------------------------
// Soft-cap combat toughness
// ---------------------------------------------------------------------------

static float (*calculateToughnessDamageResistanceMult_orig)(CharStats*) = nullptr;
static float calculateToughnessDamageResistanceMult_hook(CharStats* self)
{
    // Soft-cap combat DR only. NEVER limb regen / setLimb here.
    // Under cap: pure passthrough so toughness tooltips stay correct
    // (forcing return 1.f previously looked like "100% dmg resist").
    if (!calculateToughnessDamageResistanceMult_orig)
        return 1.f;
    if (!self)
        return calculateToughnessDamageResistanceMult_orig(self);

    float t = GetToughness(self);
    // Only soft-cap clearly super-human values; leave normal range alone
    if (!(t > g_cfg.combatCapToughness + 0.05f && t < 400.f))
        return calculateToughnessDamageResistanceMult_orig(self);

    SetToughness(self, g_cfg.combatCapToughness);
    float r = calculateToughnessDamageResistanceMult_orig(self);
    SetToughness(self, t);
    if (r != r) r = calculateToughnessDamageResistanceMult_orig(self); // NaN only
    return r;
}

static float (*calculateToughnessWoundDegenerationRate_orig)(CharStats*) = nullptr;
static float calculateToughnessWoundDegenerationRate_hook(CharStats* self)
{
    if (!calculateToughnessWoundDegenerationRate_orig)
        return 1.f;
    if (!self)
        return calculateToughnessWoundDegenerationRate_orig(self);

    float t = GetToughness(self);
    if (!(t > g_cfg.combatCapToughness + 0.05f && t < 400.f))
        return calculateToughnessWoundDegenerationRate_orig(self);

    SetToughness(self, g_cfg.combatCapToughness);
    float r = calculateToughnessWoundDegenerationRate_orig(self);
    SetToughness(self, t);
    if (r != r) r = calculateToughnessWoundDegenerationRate_orig(self);
    return r;
}

// ---------------------------------------------------------------------------
// XP past 100
// ---------------------------------------------------------------------------

static void (*xpStat_eventBased_orig)(CharStats*, StatsEnumerated, float) = nullptr;
static void xpStat_eventBased_hook(CharStats* self, StatsEnumerated st, float amount)
{
    if (!xpStat_eventBased_orig) return;
    if (!self || st != STAT_TOUGHNESS)
    {
        xpStat_eventBased_orig(self, st, amount);
        return;
    }
    float before = GetToughness(self);
    xpStat_eventBased_orig(self, st, amount);
    float gained = GetToughness(self) - before;
    if (before >= 99.5f && gained < amount * 0.02f)
    {
        float over = (before > 100.f) ? (before - 100.f) : 0.f;
        float mult = g_cfg.past100XpMult / (1.f + over * 0.02f);
        float forced = amount * mult;
        if (forced > 0.f) SetToughness(self, before + forced);
    }
}

static void (*xpStat_timeBased_orig)(CharStats*, StatsEnumerated) = nullptr;
static void xpStat_timeBased_hook(CharStats* self, StatsEnumerated st)
{
    if (!xpStat_timeBased_orig) return;
    if (!self)
    {
        xpStat_timeBased_orig(self, st);
        return;
    }

    if (st == STAT_TOUGHNESS)
    {
        float before = GetToughness(self);
        xpStat_timeBased_orig(self, st);
        if (before >= 99.5f && GetToughness(self) <= before + 0.0001f)
        {
            float over = (before > 100.f) ? (before - 100.f) : 0.f;
            float mult = g_cfg.past100XpMult / (1.f + over * 0.02f);
            SetToughness(self, before + 0.002f * mult);
        }
    }
    else
    {
        xpStat_timeBased_orig(self, st);
    }

    // Limb/food regen ONLY here (periodic), never on DR/hit.
    // Throttle so we do not re-enter medical every single stat tick.
    if (g_cfg.enableMedicalHooks)
    {
        static int s_regenThrottle = 0;
        if ((++s_regenThrottle % 8) == 0)
            ApplyFoodRegenFromStats(self, 0.12f);
    }
}

// ---------------------------------------------------------------------------
// Resolve game functions through KenshiLib.dll exports, then GetRealAddress.
// Taking &Class::method in our DLL puts a LOCAL stub address into GetRealAddress,
// which asserts: "address appears to be in your own module".
// GetProcAddress(KenshiLib, mangledName) returns a pointer INSIDE KenshiLib.dll,
// which GetRealAddress can map to the real game function.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Tooltip hooks — hover toughness / hunger in character UI
// ---------------------------------------------------------------------------

#if !defined(TOUGHNESSFEAST_LINUX_IDE)
static void (*printExertionHungerMultTooltip_orig)(CharStats*, lektor<StringPair>*) = nullptr;
static void printExertionHungerMultTooltip_hook(CharStats* self, lektor<StringPair>* dats)
{
    if (printExertionHungerMultTooltip_orig)
        printExertionHungerMultTooltip_orig(self, dats);

    if (!g_cfg.enableTooltips || !self || !dats) return;
    if (!dats->stuff) return;
    if (dats->maxSize == 0 || dats->maxSize > 256) return;
    if (dats->count > dats->maxSize) return;
    if (g_inFoodRegen) return;

    g_lastStats = self;
#if defined(_MSC_VER)
    __try
    {
#endif
        AppendFullTfTooltips(dats, self);
#if defined(_MSC_VER)
    }
    __except (1)
    {
        static int s_once = 0;
        if (!s_once) { ErrorLog("ToughnessFeast: tooltip SEH caught"); s_once = 1; }
    }
#endif
}
#endif

static void* KenshiLibModule()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    return nullptr;
#else
    static HMODULE h = nullptr;
    if (!h)
        h = GetModuleHandleA("KenshiLib.dll");
    return (void*)h;
#endif
}

// Mangled names match KenshiLib exports (MSVC x64)
static void* LibExport(const char* mangled)
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    (void)mangled;
    return nullptr;
#else
    HMODULE h = (HMODULE)KenshiLibModule();
    if (!h)
    {
        ErrorLog("ToughnessFeast: KenshiLib.dll not loaded");
        return nullptr;
    }
    void* p = (void*)GetProcAddress(h, mangled);
    if (!p)
    {
        ErrorLog("ToughnessFeast: GetProcAddress failed for:");
        ErrorLog(mangled);
    }
    return p;
#endif
}

static void* RealFromExport(const char* mangled)
{
    void* exp = LibExport(mangled);
    if (!exp) return nullptr;
    // GetRealAddress accepts KenshiLib export thunks (not addresses in our DLL)
    intptr_t real = KenshiLib::GetRealAddress(exp);
    if (!real)
    {
        ErrorLog("ToughnessFeast: GetRealAddress returned 0 for:");
        ErrorLog(mangled);
        return nullptr;
    }
    return (void*)real;
}

static int TryAddHook(void* target, void* detour, void** original, const char* okMsg)
{
    if (!target)
    {
        ErrorLog("ToughnessFeast: null target");
        ErrorLog(okMsg);
        return 0;
    }
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(target, detour, original))
    {
        ErrorLog("ToughnessFeast: AddHook failed");
        ErrorLog(okMsg);
        return 0;
    }
    DebugLog(okMsg);
    return 1;
}

static int HookExport(const char* mangled, void* detour, void** original, const char* okMsg)
{
    void* real = RealFromExport(mangled);
    return TryAddHook(real, detour, original, okMsg);
}



// Progressive enable via config — medical is highest risk during world load
static void InstallHooks()
{
    DebugLog("ToughnessFeast: EnableHooks=1, resolving via KenshiLib exports...");
    ResolveToughnessApi();

    // Combat soft-cap (usually safe)
    HookExport("?calculateToughnessDamageResistanceMult@CharStats@@QEAAMXZ",
               (void*)calculateToughnessDamageResistanceMult_hook,
               (void**)&calculateToughnessDamageResistanceMult_orig,
               "ToughnessFeast: hooked DR");
    HookExport("?calculateToughnessWoundDegenerationRate@CharStats@@QEAAMXZ",
               (void*)calculateToughnessWoundDegenerationRate_hook,
               (void**)&calculateToughnessWoundDegenerationRate_orig,
               "ToughnessFeast: hooked wound degen");

    // XP past 100
    HookExport("?xpStat_eventBased@CharStats@@QEAAXW4StatsEnumerated@@M@Z",
               (void*)xpStat_eventBased_hook,
               (void**)&xpStat_eventBased_orig,
               "ToughnessFeast: hooked xp event");
    HookExport("?xpStat_timeBased@CharStats@@QEAAXW4StatsEnumerated@@@Z",
               (void*)xpStat_timeBased_hook,
               (void**)&xpStat_timeBased_orig,
               "ToughnessFeast: hooked xp time");

    // Intentionally NOT hooking medicalUpdate (re-entrancy / layout crashes).
    // Regen is applied from CharStats DR / wound-degen / XP-time hooks instead.
    if (g_cfg.enableMedicalHooks)
        DebugLog("ToughnessFeast: food regen + staged limb restore ON (no medicalUpdate hook)");
    else
        DebugLog("ToughnessFeast: food regen OFF (EnableMedicalHooks=0)");

#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    if (g_cfg.enableTooltips)
    {
        // getStatPenaltiesForGUI NOT hooked (crashes); all TF info on hunger tooltip.
        HookExport(
            "?printExertionHungerMultTooltip@CharStats@@QEAAXPEAV?$lektor@VStringPair@@@@@Z",
            (void*)printExertionHungerMultTooltip_hook,
            (void**)&printExertionHungerMultTooltip_orig,
            "ToughnessFeast: hooked hunger tooltips");
        ResolveStringPairCtor();
        if (g_spCtor)
            DebugLog("ToughnessFeast: StringPair ctor resolved");
        else
            ErrorLog("ToughnessFeast: StringPair ctor MISSING — tooltips may be empty");
    }
#endif
    if (g_cfg.debugLog)
        DebugLog("ToughnessFeast: race unlocks Hiver/Shek/Human via name+flags");
}

#if defined(_MSC_VER)
#define TF_EXPORT __declspec(dllexport)
#else
#define TF_EXPORT __attribute__((visibility("default")))
#endif

// RE_Kenshi: GetProcAddress(..., "?startPlugin@@YAXXZ")
TF_EXPORT void startPlugin()
{
    DebugLog("ToughnessFeast: startPlugin entered");
    ResolvePluginDir();
    LoadConfig();

    if (!g_cfg.enableHooks)
    {
        DebugLog("ToughnessFeast: SAFE MODE (EnableHooks=0) — loaded, no hooks");
        DebugLog("ToughnessFeast: set EnableHooks=1 in config.ini after game opens OK");
        return;
    }

    InstallHooks();
    DebugLog("ToughnessFeast: ready (combat-safe: no regen on hit)");
}
