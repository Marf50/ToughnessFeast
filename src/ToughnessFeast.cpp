// =============================================================================
// ToughnessFeast — RE_Kenshi plugin
// =============================================================================
// Design:
//   Toughness still trains past 100, but combat damage resistance and wound
//   degeneration soft-cap at CombatCapToughness (default 100). Excess toughness
//   becomes "Feast power": while fed, you slowly regrow crushed/severed limbs
//   and heal wounds that normally never self-heal — paid for with hunger.
//
// Race unlock thresholds (configurable):
//   Hiver  ~0   (always a trickle — hive biology)
//   Shek   ~50
//   Human  ~75
//
// Limb stages: Overdamage → Budding (stump) → Restored weak → Strengthening → OK
// Limb pipeline: Crushed/Missing → (bud) → setLimb(STUMP) + food dump + KO
//                 Stump → (bud) → setLimb(ORIGINAL) + big food dump + longer KO
//                 Weak ORIGINAL → strengthen. part* always re-fetched after setLimb.
//
// UI: Hunger tooltip is the Feast journal (stable hook). No MyGUI windows.
// Safety: no medical writes on hit/DR path; SEH around race/tooltip/regrow;
//         toughness via getStat (never layout-fragile _toughness field).
// Export: ?startPlugin@@YAXXZ
// =============================================================================

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
#include <cmath>
#include <string>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#if defined(_MSC_VER)
#define TF_SEH_TRY    __try
#define TF_SEH_EXCEPT __except (1)
#else
#define TF_SEH_TRY    if (true)
#define TF_SEH_EXCEPT if (false)
#endif

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

struct Config
{
    int   enableHooks;
    int   enableMedical;
    int   enableLimbRestore;
    int   enableTooltips;
    int   enableCombatPenalty;
    int   debugLog;

    float combatCap;              // soft-cap for DR / wound degen
    float unlockHuman;
    float unlockShek;
    float unlockHiver;
    float scalePerPoint;          // feast power per toughness above unlock
    float scaleHiver;             // hivers use this lower scale
    float powerCap;               // max feast power

    float fleshHealPerSec;
    float stunHealPerSec;
    float hungerDrainPerSec;
    float minHunger;              // 0..1
    int   healUnhealable;

    float limbRegrowPerSec;       // very slow by design
    float limbBudThreshold;       // stump flesh% before full restore
    float limbStumpFormPct;       // crushed flesh% before forming stump
    float limbRestoredStart;      // flesh% right after restore
    float limbStrongPct;          // above this = combat OK
    float overdamageHealMult;     // slower healing of negative HP
    float stumpHungerCost;        // hunger bar fraction spent forming stump
    float restoreHungerCost;      // hunger bar fraction spent on full restore
    float stumpKoSeconds;         // KO duration when stump forms
    float restoreKoSeconds;       // KO duration when limb returns

    float past100XpMult;
    float weakLimbDmgMult;        // skillMultDamage scale while weak
    float weakLimbDexMult;
    float weakLimbSpeedMult;
    float weakLimbDodgeMult;

    int   tooltipMaxLines;
};

static Config g_cfg = {
    1, 1, 1, 1, 1, 1,             // hooks/medical/limb/tooltips/penalty/debug
    100.f,                        // combatCap
    75.f, 50.f, 0.f,              // unlocks H/S/Hiv
    0.045f, 0.014f, 3.0f,         // scale, hiver scale, power cap
    1.35f, 0.9f, 0.012f, 0.15f, 1,// flesh, stun, hunger drain, minHunger, unhealable
    0.085f, 0.40f, 0.12f, 0.18f, 0.70f, 0.40f, // regrow/bud/stumpForm/start/strong/overdmg
    0.28f, 0.55f, 8.0f, 18.0f,    // stumpHunger, restoreHunger, stumpKO, restoreKO
    0.20f,                        // past100 xp
    0.88f, 0.90f, 0.92f, 0.90f,   // weak limb combat mults
    16                            // tooltip lines
};

static char g_pluginDir[MAX_PATH] = {};

static void Log(const char* msg)
{
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    if (g_cfg.debugLog) DebugLog(msg);
#else
    (void)msg;
#endif
}
static void LogErr(const char* msg)
{
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    ErrorLog(msg);
#else
    (void)msg;
#endif
}

static int ParseBool(const char* v)
{
    if (!v || !*v) return 0;
    if (v[0]=='1'||v[0]=='y'||v[0]=='Y'||v[0]=='t'||v[0]=='T') return 1;
    if (std::strcmp(v,"on")==0||std::strcmp(v,"ON")==0) return 1;
    return 0;
}

static void ResolvePluginDir()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    g_pluginDir[0] = 0;
#else
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&ResolvePluginDir, &mod) || !mod)
    {
        g_pluginDir[0] = 0;
        return;
    }
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(mod, path, MAX_PATH);
    if (!n || n >= MAX_PATH) { g_pluginDir[0] = 0; return; }
    char* slash = nullptr;
    for (char* p = path; *p; ++p) if (*p=='\\'||*p=='/') slash = p;
    if (!slash) { g_pluginDir[0] = 0; return; }
    *slash = 0;
    std::snprintf(g_pluginDir, sizeof(g_pluginDir), "%s", path);
#endif
}

static void LoadConfig()
{
    char path[MAX_PATH];
    if (g_pluginDir[0])
        std::snprintf(path, sizeof(path), "%s\\config.ini", g_pluginDir);
    else
        std::snprintf(path, sizeof(path), "config.ini");

    FILE* f = std::fopen(path, "r");
    if (!f)
    {
        Log("ToughnessFeast: no config.ini — defaults");
        return;
    }

    char line[512];
    while (std::fgets(line, sizeof(line), f))
    {
        char* s = line;
        while (*s==' '||*s=='\t') ++s;
        if (*s=='#' || *s==';' || *s=='\n' || *s=='\r' || !*s) continue;
        char* eq = std::strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = s;
        char* val = eq + 1;
        // trim key
        char* ke = key + std::strlen(key);
        while (ke > key && (ke[-1]==' '||ke[-1]=='\t')) *--ke = 0;
        // trim val
        while (*val==' '||*val=='\t') ++val;
        char* ve = val + std::strlen(val);
        while (ve > val && (ve[-1]=='\n'||ve[-1]=='\r'||ve[-1]==' '||ve[-1]=='\t')) *--ve = 0;

        auto fval = [&]() { return (float)std::atof(val); };
        auto bval = [&]() { return ParseBool(val); };

        if (!std::strcmp(key, "EnableHooks")) g_cfg.enableHooks = bval();
        else if (!std::strcmp(key, "EnableMedicalHooks") || !std::strcmp(key, "EnableMedical"))
            g_cfg.enableMedical = bval();
        else if (!std::strcmp(key, "EnableLimbRestore")) g_cfg.enableLimbRestore = bval();
        else if (!std::strcmp(key, "EnableTooltips")) g_cfg.enableTooltips = bval();
        else if (!std::strcmp(key, "EnableCombatPenalty")) g_cfg.enableCombatPenalty = bval();
        else if (!std::strcmp(key, "DebugLog")) g_cfg.debugLog = bval();
        else if (!std::strcmp(key, "CombatCapToughness") || !std::strcmp(key, "CombatCap"))
            g_cfg.combatCap = fval();
        else if (!std::strcmp(key, "FoodRegenStartHuman") || !std::strcmp(key, "UnlockHuman"))
            g_cfg.unlockHuman = fval();
        else if (!std::strcmp(key, "FoodRegenStartShek") || !std::strcmp(key, "UnlockShek"))
            g_cfg.unlockShek = fval();
        else if (!std::strcmp(key, "FoodRegenStartHiver") || !std::strcmp(key, "UnlockHiver"))
            g_cfg.unlockHiver = fval();
        else if (!std::strcmp(key, "FoodRegenScalePerPoint") || !std::strcmp(key, "ScalePerPoint"))
            g_cfg.scalePerPoint = fval();
        else if (!std::strcmp(key, "FoodRegenScaleHiver") || !std::strcmp(key, "ScaleHiver"))
            g_cfg.scaleHiver = fval();
        else if (!std::strcmp(key, "PowerCap")) g_cfg.powerCap = fval();
        else if (!std::strcmp(key, "FleshHealPerSecond")) g_cfg.fleshHealPerSec = fval();
        else if (!std::strcmp(key, "StunHealPerSecond")) g_cfg.stunHealPerSec = fval();
        else if (!std::strcmp(key, "HungerDrainPerSecond")) g_cfg.hungerDrainPerSec = fval();
        else if (!std::strcmp(key, "MinHungerToRegen")) g_cfg.minHunger = fval();
        else if (!std::strcmp(key, "HealUnhealableWounds")) g_cfg.healUnhealable = bval();
        else if (!std::strcmp(key, "LimbRegrowPerSecond")) g_cfg.limbRegrowPerSec = fval();
        else if (!std::strcmp(key, "LimbBudThreshold")) g_cfg.limbBudThreshold = fval();
        else if (!std::strcmp(key, "LimbStumpFormPct")) g_cfg.limbStumpFormPct = fval();
        else if (!std::strcmp(key, "StumpHungerCost")) g_cfg.stumpHungerCost = fval();
        else if (!std::strcmp(key, "RestoreHungerCost")) g_cfg.restoreHungerCost = fval();
        else if (!std::strcmp(key, "StumpKnockoutSeconds")) g_cfg.stumpKoSeconds = fval();
        else if (!std::strcmp(key, "RestoreKnockoutSeconds")) g_cfg.restoreKoSeconds = fval();
        else if (!std::strcmp(key, "LimbRestoredStartPct")) g_cfg.limbRestoredStart = fval();
        else if (!std::strcmp(key, "LimbStrongPct")) g_cfg.limbStrongPct = fval();
        else if (!std::strcmp(key, "OverdamageHealMult")) g_cfg.overdamageHealMult = fval();
        else if (!std::strcmp(key, "Past100XpMult")) g_cfg.past100XpMult = fval();
        else if (!std::strcmp(key, "WeakLimbDmgMult")) g_cfg.weakLimbDmgMult = fval();
        else if (!std::strcmp(key, "WeakLimbDexMult")) g_cfg.weakLimbDexMult = fval();
        else if (!std::strcmp(key, "WeakLimbSpeedMult")) g_cfg.weakLimbSpeedMult = fval();
        else if (!std::strcmp(key, "WeakLimbDodgeMult")) g_cfg.weakLimbDodgeMult = fval();
        else if (!std::strcmp(key, "TooltipMaxLines")) g_cfg.tooltipMaxLines = (int)fval();
    }
    std::fclose(f);

    // clamp nonsense
    if (g_cfg.combatCap < 50.f) g_cfg.combatCap = 50.f;
    if (g_cfg.limbBudThreshold < 0.15f) g_cfg.limbBudThreshold = 0.15f;
    if (g_cfg.limbBudThreshold > 0.9f) g_cfg.limbBudThreshold = 0.9f;
    if (g_cfg.limbStumpFormPct < 0.05f) g_cfg.limbStumpFormPct = 0.05f;
    if (g_cfg.limbStumpFormPct > 0.5f) g_cfg.limbStumpFormPct = 0.5f;
    if (g_cfg.stumpHungerCost < 0.05f) g_cfg.stumpHungerCost = 0.05f;
    if (g_cfg.restoreHungerCost < 0.1f) g_cfg.restoreHungerCost = 0.1f;
    if (g_cfg.stumpKoSeconds < 1.f) g_cfg.stumpKoSeconds = 1.f;
    if (g_cfg.restoreKoSeconds < 2.f) g_cfg.restoreKoSeconds = 2.f;
    if (g_cfg.minHunger < 0.f) g_cfg.minHunger = 0.f;
    if (g_cfg.minHunger > 0.9f) g_cfg.minHunger = 0.9f;
    if (g_cfg.tooltipMaxLines < 8) g_cfg.tooltipMaxLines = 8;
    if (g_cfg.tooltipMaxLines > 24) g_cfg.tooltipMaxLines = 24;

    Log("ToughnessFeast: config loaded");
}

// ---------------------------------------------------------------------------
// Game API resolvers (never trust C++ member offsets past CharStats map)
// ---------------------------------------------------------------------------

typedef float  (*Fn_getStat)(const CharStats*, StatsEnumerated, bool);
typedef float* (*Fn_getStatRef)(CharStats*, StatsEnumerated);
typedef float  (*Fn_toughness)(const CharStats*);

static Fn_getStat    g_getStat = nullptr;
static Fn_getStatRef g_getStatRef = nullptr;
static Fn_toughness  g_toughnessFn = nullptr;

#if !defined(TOUGHNESSFEAST_LINUX_IDE)
static void* LibExport(const char* mangled)
{
    HMODULE h = GetModuleHandleA("KenshiLib.dll");
    if (!h) return nullptr;
    return (void*)GetProcAddress(h, mangled);
}
static void* RealFromExport(const char* mangled)
{
    void* exp = LibExport(mangled);
    if (!exp) return nullptr;
    intptr_t real = KenshiLib::GetRealAddress(exp);
    return real ? (void*)real : nullptr;
}
static int HookExport(const char* mangled, void* detour, void** original, const char* okMsg)
{
    void* real = RealFromExport(mangled);
    if (!real)
    {
        LogErr("ToughnessFeast: resolve failed");
        LogErr(mangled);
        return 0;
    }
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(real, detour, original))
    {
        LogErr("ToughnessFeast: AddHook failed");
        LogErr(okMsg);
        return 0;
    }
    Log(okMsg);
    return 1;
}
#endif

static void ResolveStatApi()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    return;
#else
    if (g_getStat) return;
    g_getStat = (Fn_getStat)RealFromExport(
        "?getStat@CharStats@@QEBAMW4StatsEnumerated@@_N@Z");
    g_getStatRef = (Fn_getStatRef)RealFromExport(
        "?getStatRef@CharStats@@QEAAAEAMW4StatsEnumerated@@@Z");
    g_toughnessFn = (Fn_toughness)RealFromExport(
        "?toughness@CharStats@@QEBAMXZ");
    if (g_getStat) Log("ToughnessFeast: getStat OK");
    else LogErr("ToughnessFeast: getStat FAILED — toughness reads may be wrong");
    if (g_getStatRef) Log("ToughnessFeast: getStatRef OK");
    if (g_toughnessFn) Log("ToughnessFeast: toughness() OK");
#endif
}

// True toughness (matches character sheet). Never use stats->_toughness.
static float GetToughness(const CharStats* stats)
{
    if (!stats) return 0.f;
    ResolveStatApi();
    float t = -1.f;
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    if (g_getStat)
        t = g_getStat(stats, STAT_TOUGHNESS, true);
    if ((t != t || t < 0.f || t > 500.f) && g_toughnessFn)
        t = g_toughnessFn(stats);
    if ((t != t || t < 0.f || t > 500.f) && g_getStatRef)
    {
        float* p = g_getStatRef(const_cast<CharStats*>(stats), STAT_TOUGHNESS);
        if (p) t = *p;
    }
#endif
    // Absolute last resort: documented game offset (NOT strength @ 0x80)
    if (t != t || t < 0.f || t > 500.f)
    {
        float v = 0.f;
        std::memcpy(&v, (const char*)(const void*)stats + 0x90, sizeof(v));
        t = v;
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
    ResolveStatApi();
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    if (g_getStatRef)
    {
        float* p = g_getStatRef(stats, STAT_TOUGHNESS);
        if (p) { *p = t; return; }
    }
#endif
    std::memcpy((char*)(void*)stats + 0x90, &t, sizeof(float));
}

// ---------------------------------------------------------------------------
// Race
// ---------------------------------------------------------------------------

enum RaceKind { RACE_UNKNOWN=0, RACE_HUMAN, RACE_SHEK, RACE_HIVER, RACE_ROBOT };

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
    // NEVER me->myRace (layout mismatch → AV). Virtual only.
    return me->getRace();
}

// MSVC2010 basic_string at GameData::name 0x28 / stringID 0x58
static int MsvcStrContains(const char* strObj, const char* needle)
{
    if (!strObj || !needle || !*needle) return 0;
    TF_SEH_TRY
    {
        size_t size = 0, res = 0;
        std::memcpy(&size, strObj + 16, sizeof(size));
        std::memcpy(&res,  strObj + 24, sizeof(res));
        if (size == 0 || size > 200 || res > 0x200000u) return 0;
        const char* data = nullptr;
        if (res < 16u) data = strObj;
        else std::memcpy(&data, strObj, sizeof(data));
        if (!data || (uintptr_t)data < 0x10000ull) return 0;
        // case-insensitive substring
        char hay[128];
        size_t n = size < 127 ? size : 127;
        for (size_t i = 0; i < n; ++i)
        {
            char c = data[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            hay[i] = c;
        }
        hay[n] = 0;
        char ndl[64];
        size_t m = 0;
        for (; needle[m] && m < 63; ++m)
        {
            char c = needle[m];
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            ndl[m] = c;
        }
        ndl[m] = 0;
        return std::strstr(hay, ndl) != nullptr ? 1 : 0;
    }
    TF_SEH_EXCEPT { return 0; }
}

static RaceKind DetectRace(CharStats* stats)
{
    TF_SEH_TRY
    {
        RaceData* race = RaceFromStats(stats);
        if (!race || (uintptr_t)race < 0x10000ull) return RACE_UNKNOWN;
        if (race->robot) return RACE_ROBOT;
        if (race->gigantic) return RACE_SHEK;

        if (race->data)
        {
            const char* base = (const char*)(void*)race->data;
            if ((uintptr_t)base > 0x10000ull)
            {
                if (MsvcStrContains(base + 0x58, "hive") || MsvcStrContains(base + 0x28, "hive")
                 || MsvcStrContains(base + 0x58, "hiver") || MsvcStrContains(base + 0x28, "hiver"))
                    return RACE_HIVER;
                if (MsvcStrContains(base + 0x58, "shek") || MsvcStrContains(base + 0x28, "shek"))
                    return RACE_SHEK;
                if (MsvcStrContains(base + 0x58, "skeleton") || MsvcStrContains(base + 0x28, "skeleton"))
                    return RACE_ROBOT;
            }
        }
        // Hive anatomy / metabolism heuristics
        if (race->noHats && (race->noShoes || race->noShirts)) return RACE_HIVER;
        if (race->singleGender && race->hungerRate > 1.0f) return RACE_HIVER;
        if (race->hungerRate > 1.05f) return RACE_HIVER;
        return RACE_HUMAN;
    }
    TF_SEH_EXCEPT { return RACE_UNKNOWN; }
}

static const char* RaceName(RaceKind k)
{
    switch (k)
    {
    case RACE_SHEK: return "Shek";
    case RACE_HIVER: return "Hiver";
    case RACE_ROBOT: return "Skeleton";
    case RACE_HUMAN: return "Human";
    default: return "Unknown";
    }
}

static float UnlockFor(CharStats* stats)
{
    switch (DetectRace(stats))
    {
    case RACE_HIVER: return g_cfg.unlockHiver;
    case RACE_SHEK:  return g_cfg.unlockShek;
    case RACE_ROBOT: return 9000.f; // skeletons don't feast
    default:         return g_cfg.unlockHuman;
    }
}

static float ScaleFor(CharStats* stats)
{
    if (DetectRace(stats) == RACE_HIVER) return g_cfg.scaleHiver;
    return g_cfg.scalePerPoint;
}

// Feast power from excess toughness above race unlock
static float FeastPower(CharStats* stats)
{
    if (!stats) return 0.f;
    if (DetectRace(stats) == RACE_ROBOT) return 0.f;
    float t = GetToughness(stats);
    float un = UnlockFor(stats);
    if (t <= un) return 0.f;
    float p = (t - un) * ScaleFor(stats);
    if (p > g_cfg.powerCap) p = g_cfg.powerCap;
    if (p < 0.f) p = 0.f;
    return p;
}

// ---------------------------------------------------------------------------
// Limbs
// ---------------------------------------------------------------------------

struct LimbInfo
{
    char name[16];
    char stage[24];
    char detail[40];
    int  active;     // needs feast attention
    int  missing;    // stump/crush
    float severity;  // 0..1 combat impact
};

// RobotLimbs* lives at MedicalSystem+0xC8 in the game binary.
// Do not use med->robotLimbs field access (map layout drift).
static RobotLimbs* GetRobotLimbs(MedicalSystem* med)
{
    if (!med) return nullptr;
    RobotLimbs* r = nullptr;
    std::memcpy(&r, (const char*)(void*)med + 0xC8, sizeof(r));
    if (r && (uintptr_t)r > 0x10000ull) return r;
    return nullptr;
}

static MedicalSystem::HealthPartStatus* ResolveLimb(MedicalSystem* med, int slot)
{
    if (!med) return nullptr;
    static const RobotLimbs::Limb kEnum[4] = {
        RobotLimbs::LEFT_ARM, RobotLimbs::RIGHT_ARM,
        RobotLimbs::LEFT_LEG, RobotLimbs::RIGHT_LEG
    };

    // Method calls use game code — layout-safe
    MedicalSystem::HealthPartStatus* p = nullptr;
    TF_SEH_TRY { p = med->getPart(kEnum[slot]); }
    TF_SEH_EXCEPT { p = nullptr; }
    if (p && (uintptr_t)p > 0x10000ull) return p;

    using PT = MedicalSystem::HealthPartStatus::PartType;
    TF_SEH_TRY
    {
        if (slot == 0) p = med->getPart(PT::PART_ARM, SIDE_LEFT);
        else if (slot == 1) p = med->getPart(PT::PART_ARM, SIDE_RIGHT);
        else if (slot == 2) p = med->getPart(PT::PART_LEG, SIDE_LEFT);
        else if (slot == 3) p = med->getPart(PT::PART_LEG, SIDE_RIGHT);
    }
    TF_SEH_EXCEPT { p = nullptr; }
    if (p && (uintptr_t)p > 0x10000ull) return p;

    // Game offsets for limb HealthPartStatus* (KenshiLib comments)
    static const int kOff[4] = { 0x90, 0x98, 0x80, 0x88 }; // LArm RArm LLeg RLeg
    MedicalSystem::HealthPartStatus* raw = nullptr;
    std::memcpy(&raw, (const char*)(void*)med + kOff[slot], sizeof(raw));
    if (raw && (uintptr_t)raw > 0x10000ull) return raw;
    return nullptr;
}

static LimbState ReadLimbState(MedicalSystem* med, int slot)
{
    static const RobotLimbs::Limb kEnum[4] = {
        RobotLimbs::LEFT_ARM, RobotLimbs::RIGHT_ARM,
        RobotLimbs::LEFT_LEG, RobotLimbs::RIGHT_LEG
    };
    LimbState st = LIMB_ORIGINAL;

    // 1) MedicalSystem::getLimbState — method, layout-safe
    TF_SEH_TRY { st = med->getLimbState(kEnum[slot]); }
    TF_SEH_EXCEPT { st = LIMB_ORIGINAL; }

    // 2) RobotLimbs table (raw pointer @ 0xC8)
    RobotLimbs* robots = GetRobotLimbs(med);
    if (robots)
    {
        TF_SEH_TRY
        {
            LimbState t = robots->getState(kEnum[slot]);
            // Prefer non-original from table when it indicates missing/replaced
            if (t == LIMB_STUMP || t == LIMB_CRUSHED || t == LIMB_REPLACED)
                st = t;
            else if (st == LIMB_ORIGINAL)
                st = t;
        }
        TF_SEH_EXCEPT { }
    }

    // 3) Part's own state if we have a pointer
    MedicalSystem::HealthPartStatus* part = ResolveLimb(med, slot);
    if (part)
    {
        TF_SEH_TRY
        {
            LimbState ps = part->getRobotLimbState();
            if (ps == LIMB_STUMP || ps == LIMB_CRUSHED || ps == LIMB_REPLACED)
                st = ps;
            else if (st == LIMB_ORIGINAL)
                st = ps;
        }
        TF_SEH_EXCEPT { }
    }
    return st;
}

static int CollectLimbs(CharStats* stats, LimbInfo* out, int maxOut)
{
    if (!stats || !out || maxOut <= 0) return 0;
    MedicalSystem* med = stats->medical;
    if (!med) return 0;

    static const char* kNames[4] = { "Left Arm", "Right Arm", "Left Leg", "Right Leg" };
    int n = 0;

    TF_SEH_TRY
    {
        // Always emit all 4 slots — missing limbs often have NULL part*
        for (int i = 0; i < 4 && n < maxOut; ++i)
        {
            LimbInfo& L = out[n];
            std::memset(&L, 0, sizeof(L));
            std::snprintf(L.name, sizeof(L.name), "%s", kNames[i]);

            LimbState st = ReadLimbState(med, i);
            MedicalSystem::HealthPartStatus* part = ResolveLimb(med, i);

            float maxHp = 100.f;
            float raw = 0.f;
            int robotic = 0;

            if (part)
            {
                TF_SEH_TRY
                {
                    if (part->isRobotic()) robotic = 1;
                    maxHp = part->maxHealth();
                    if (maxHp < 1.f) maxHp = part->_maxHealth;
                    if (maxHp < 1.f) maxHp = 100.f;
                    raw = part->flesh;
                    if (raw != raw) raw = 0.f;
                }
                TF_SEH_EXCEPT
                {
                    part = nullptr;
                    raw = 0.f;
                    maxHp = 100.f;
                }
            }

            if (robotic || st == LIMB_REPLACED)
            {
                std::snprintf(L.stage, sizeof(L.stage), "Prosthetic");
                std::snprintf(L.detail, sizeof(L.detail), "Feast ignores");
                ++n;
                continue;
            }

            float pos = raw > 0.f ? raw : 0.f;
            float pct = pos / maxHp;
            if (pct > 1.2f) pct = 1.2f;

            // No part pointer + crushed/stump OR negative flesh on HUD = missing
            if (st == LIMB_CRUSHED || (!part && st != LIMB_ORIGINAL && st != LIMB_REPLACED))
            {
                // Treat unknown-null as missing when HUD would show a bar at -HP
                if (st != LIMB_STUMP) st = LIMB_CRUSHED;
            }
            // If part null and state still ORIGINAL, still list as "OK?" — better than blank
            // But user missing limbs: getLimbState should return STUMP/CRUSHED

            if (st == LIMB_CRUSHED)
            {
                float prog = (g_cfg.limbStumpFormPct > 0.01f)
                    ? (pos / maxHp) / g_cfg.limbStumpFormPct : 0.f;
                if (prog > 1.f) prog = 1.f;
                if (!part && prog < 0.01f)
                {
                    // Fully gone — no part to bud on yet; show waiting for stump form
                    std::snprintf(L.stage, sizeof(L.stage), "Missing");
                    std::snprintf(L.detail, sizeof(L.detail), "forming stump 0%%");
                }
                else
                {
                    std::snprintf(L.stage, sizeof(L.stage), "Missing");
                    std::snprintf(L.detail, sizeof(L.detail),
                        "to stump %.0f%%", prog * 100.f);
                }
                L.active = 1;
                L.missing = 1;
                L.severity = 1.0f;
            }
            else if (st == LIMB_STUMP)
            {
                float prog = (g_cfg.limbBudThreshold > 0.01f)
                    ? (pos / maxHp) / g_cfg.limbBudThreshold : 0.f;
                if (prog > 1.f) prog = 1.f;
                std::snprintf(L.stage, sizeof(L.stage), "Stump");
                std::snprintf(L.detail, sizeof(L.detail),
                    "bud %.0f%%", prog * 100.f);
                L.active = 1;
                L.missing = 1;
                L.severity = 0.92f;
            }
            else
            {
                // ORIGINAL (or unknown with part)
                if (!part)
                {
                    // State says original but no part — still show something
                    std::snprintf(L.stage, sizeof(L.stage), "OK");
                    std::snprintf(L.detail, sizeof(L.detail), "no part data");
                }
                else if (pct < g_cfg.limbRestoredStart + 0.06f)
                {
                    std::snprintf(L.stage, sizeof(L.stage), "Fragile");
                    std::snprintf(L.detail, sizeof(L.detail), "HP %.0f%%", pct * 100.f);
                    L.active = 1;
                    L.severity = 0.75f;
                }
                else if (pct < g_cfg.limbStrongPct)
                {
                    std::snprintf(L.stage, sizeof(L.stage), "Healing");
                    std::snprintf(L.detail, sizeof(L.detail), "HP %.0f%%", pct * 100.f);
                    L.active = 1;
                    L.severity = 0.35f + (1.f - pct / g_cfg.limbStrongPct) * 0.35f;
                }
                else
                {
                    std::snprintf(L.stage, sizeof(L.stage), "OK");
                    std::snprintf(L.detail, sizeof(L.detail), "HP %.0f%%", pct * 100.f);
                }
            }
            ++n;
        }
    }
    TF_SEH_EXCEPT { return n; }
    return n;
}

// ---------------------------------------------------------------------------
// Combat soft-cap (hit-safe: no medical writes)
// ---------------------------------------------------------------------------

static float (*orig_DR)(CharStats*) = nullptr;
static float hook_DR(CharStats* self)
{
    if (!orig_DR) return 1.f;
    if (!self) return orig_DR(self);
    float t = GetToughness(self);
    if (!(t > g_cfg.combatCap + 0.05f && t < 400.f))
        return orig_DR(self);
    // Temporarily soft-cap via getStatRef write
    SetToughness(self, g_cfg.combatCap);
    float r = orig_DR(self);
    SetToughness(self, t);
    return r;
}

static float (*orig_Wound)(CharStats*) = nullptr;
static float hook_Wound(CharStats* self)
{
    if (!orig_Wound) return 1.f;
    if (!self) return orig_Wound(self);
    float t = GetToughness(self);
    if (!(t > g_cfg.combatCap + 0.05f && t < 400.f))
        return orig_Wound(self);
    SetToughness(self, g_cfg.combatCap);
    float r = orig_Wound(self);
    SetToughness(self, t);
    return r;
}

// ---------------------------------------------------------------------------
// XP past soft skill ceiling
// ---------------------------------------------------------------------------

static void (*orig_xpEvent)(CharStats*, StatsEnumerated, float) = nullptr;
static void hook_xpEvent(CharStats* self, StatsEnumerated st, float amount)
{
    if (!orig_xpEvent) return;
    if (!self || st != STAT_TOUGHNESS)
    {
        orig_xpEvent(self, st, amount);
        return;
    }
    float before = GetToughness(self);
    orig_xpEvent(self, st, amount);
    float gained = GetToughness(self) - before;
    // Near/above 100 the game often starves gains — inject a slowed trickle
    if (before >= 99.5f && gained < amount * 0.02f)
    {
        float over = (before > 100.f) ? (before - 100.f) : 0.f;
        float mult = g_cfg.past100XpMult / (1.f + over * 0.02f);
        float forced = amount * mult;
        if (forced > 0.f) SetToughness(self, before + forced);
    }
}

static void (*orig_xpTime)(CharStats*, StatsEnumerated) = nullptr;
static int g_inRegen = 0;
static void ApplyFeastTick(CharStats* stats, float dt); // fwd

static void hook_xpTime(CharStats* self, StatsEnumerated st)
{
    if (!orig_xpTime) return;
    if (!self)
    {
        orig_xpTime(self, st);
        return;
    }

    if (st == STAT_TOUGHNESS)
    {
        float before = GetToughness(self);
        orig_xpTime(self, st);
        if (before >= 99.5f && GetToughness(self) <= before + 0.0001f)
        {
            float over = (before > 100.f) ? (before - 100.f) : 0.f;
            float mult = g_cfg.past100XpMult / (1.f + over * 0.02f);
            SetToughness(self, before + 0.002f * mult);
        }
    }
    else
    {
        orig_xpTime(self, st);
    }

    // Periodic feast tick (not on hit). Throttle across all stat time ticks.
    if (g_cfg.enableMedical)
    {
        static int throttle = 0;
        if ((++throttle % 8) == 0)
            ApplyFeastTick(self, 0.12f);
    }
}

// ---------------------------------------------------------------------------
// Feast: limb regrow + unhealable wounds (hunger-paid)
// ---------------------------------------------------------------------------

// skill mults live BEFORE the map in CharStats — safe field offsets
static void ApplyWeakLimbPenalty(CharStats* stats, float severity01)
{
    if (!g_cfg.enableCombatPenalty || !stats) return;
    if (severity01 < 0.05f) return;
    if (severity01 > 1.f) severity01 = 1.f;
    // Gentle per-tick pressure while limbs are missing/weak.
    // Fields sit before CharStats' std::map — layout-safe.
    // Keep multipliers in a sane band so we never zero someone out.
    float w = severity01 * 0.12f; // small per tick
    auto press = [&](float& m, float targetMult) {
        if (m <= 0.05f || m > 5.f) return;
        float k = 1.f - (1.f - targetMult) * w;
        if (k < 0.85f) k = 0.85f;
        m *= k;
        if (m < 0.35f) m = 0.35f;
    };
    press(stats->skillMultDamage, g_cfg.weakLimbDmgMult);
    press(stats->skillMultDexterity, g_cfg.weakLimbDexMult);
    press(stats->combatSpeedMultiplier, g_cfg.weakLimbSpeedMult);
    press(stats->skillMultDodge, g_cfg.weakLimbDodgeMult);
}

static void SpendHungerAndKnockout(MedicalSystem* med, float hungerFrac, float koSeconds, const char* reason)
{
    if (!med) return;
    if (hungerFrac < 0.f) hungerFrac = 0.f;
    if (hungerFrac > 0.95f) hungerFrac = 0.95f;
    float h = med->hunger;
    if (h == h && h >= 0.f && h <= 5.f)
    {
        h -= hungerFrac;
        if (h < 0.f) h = 0.f;
        med->hunger = h;
    }
    // Force a real KO so the player feels the transition
    if (koSeconds > 0.f)
    {
        TF_SEH_TRY
        {
            med->knockoutForceTimer(koSeconds);
        }
        TF_SEH_EXCEPT
        {
            // Fallback: raw timer if call fails
            med->knockoutTimer = koSeconds;
            med->unconcious = true;
        }
    }
    if (g_cfg.debugLog && reason)
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "ToughnessFeast: %s  hunger-%.0f%%  KO %.0fs",
            reason, hungerFrac * 100.f, koSeconds);
        Log(msg);
    }
}

static void ProcessLimbs(
    MedicalSystem* med, CharStats* stats, float power, float dt,
    float& hungerCost, int& anyHeal, float& worstSev)
{
    if (!med || !stats || !g_cfg.enableLimbRestore || power <= 0.f) return;
    float budget = g_cfg.limbRegrowPerSec * power * dt;
    if (budget <= 0.f) return;

    RobotLimbs* robots = GetRobotLimbs(med);
    // Without robotLimbs we cannot change limb state (stump/restore)
    if (!robots)
    {
        static int s_nr = 0;
        if (g_cfg.debugLog && (++s_nr % 40) == 1)
            Log("ToughnessFeast: robotLimbs null @0xC8 — cannot setLimb");
        return;
    }

    static const RobotLimbs::Limb kEnum[4] = {
        RobotLimbs::LEFT_ARM, RobotLimbs::RIGHT_ARM,
        RobotLimbs::LEFT_LEG, RobotLimbs::RIGHT_LEG
    };

    TF_SEH_TRY
    {
        for (int i = 0; i < 4; ++i)
        {
            LimbState eff = ReadLimbState(med, i);
            if (eff == LIMB_REPLACED) continue;
            LimbState st = eff;

            MedicalSystem::HealthPartStatus* part = ResolveLimb(med, i);
            if (part)
            {
                int rob = 0;
                TF_SEH_TRY { rob = part->isRobotic() ? 1 : 0; }
                TF_SEH_EXCEPT { rob = 0; }
                if (rob) continue;
            }

            float maxHp = 100.f;
            float flesh = 0.f;
            if (part)
            {
                maxHp = part->maxHealth();
                if (maxHp < 1.f) maxHp = part->_maxHealth;
                if (maxHp < 1.f || maxHp > 10000.f) maxHp = 100.f;
                flesh = part->flesh;
                if (flesh != flesh) flesh = 0.f;
                if (flesh > maxHp * 3.f) flesh = maxHp;
            }
            else if (eff != LIMB_CRUSHED && eff != LIMB_STUMP)
            {
                // No part and looks intact — skip
                continue;
            }
            // Missing with null part: still allow setLimb(STUMP) path

            // ========== CRUSHED / fully missing ==========
            // Heal overdamage, bud a nub, then form a STUMP (missing → stump).
            if (eff == LIMB_CRUSHED)
            {
                if (worstSev < 1.f) worstSev = 1.f;
                float rate = 0.50f;

                // No HealthPartStatus (fully severed) — form stump directly via setLimb
                if (!part)
                {
                    robots->setLimb(kEnum[i], LIMB_STUMP, nullptr);
                    part = ResolveLimb(med, i);
                    if (part)
                    {
                        float nub = maxHp * 0.04f;
                        if (nub < 1.f) nub = 1.f;
                        part->flesh = nub;
                        TF_SEH_TRY { part->updateDerivedHealths(); }
                        TF_SEH_EXCEPT { }
                    }
                    anyHeal = 1;
                    SpendHungerAndKnockout(med, g_cfg.stumpHungerCost, g_cfg.stumpKoSeconds,
                        "formed STUMP (no part)");
                    continue;
                }

                if (flesh < 0.f)
                {
                    float take = -flesh;
                    float room = budget * rate * g_cfg.overdamageHealMult;
                    if (take > room) take = room;
                    if (take > 0.f)
                    {
                        part->flesh = flesh + take;
                        budget -= take / (rate > 0.01f ? rate : 1.f);
                        hungerCost += take * 1.1f;
                        anyHeal = 1;
                        flesh = part->flesh;
                    }
                    continue;
                }

                float formNeed = maxHp * g_cfg.limbStumpFormPct;
                if (formNeed < 2.f) formNeed = maxHp * 0.12f;

                if (flesh < formNeed && budget > 0.f)
                {
                    float need = formNeed - flesh;
                    float room = budget * rate;
                    float take = need < room ? need : room;
                    if (take > 0.f)
                    {
                        part->flesh = flesh + take;
                        budget -= take / (rate > 0.01f ? rate : 1.f);
                        hungerCost += take * 1.2f;
                        anyHeal = 1;
                        flesh = part->flesh;
                    }
                }

                // Transition: Missing/Crushed → Stump
                if (flesh >= formNeed * 0.98f)
                {
                    robots->setLimb(kEnum[i], LIMB_STUMP, nullptr);
                    part = ResolveLimb(med, i);
                    if (part)
                    {
                        // Fresh stump starts with a little bud mass
                        float nub = maxHp * 0.04f;
                        if (nub < 1.f) nub = 1.f;
                        part->flesh = nub;
                        TF_SEH_TRY { part->updateDerivedHealths(); }
                        TF_SEH_EXCEPT { }
                    }
                    anyHeal = 1;
                    // Big one-shot food hit + short KO
                    SpendHungerAndKnockout(med, g_cfg.stumpHungerCost, g_cfg.stumpKoSeconds,
                        "formed STUMP");
                    // Don't also apply gradual drain for this transition
                    continue;
                }
            }
            // ========== STUMP — bud until ready for full limb ==========
            else if (eff == LIMB_STUMP)
            {
                if (worstSev < 0.95f) worstSev = 0.95f;
                float rate = 1.f;

                if (flesh < 0.f)
                {
                    float take = -flesh;
                    float room = budget * rate * g_cfg.overdamageHealMult;
                    if (take > room) take = room;
                    if (take > 0.f)
                    {
                        part->flesh = flesh + take;
                        budget -= take / (rate > 0.01f ? rate : 1.f);
                        hungerCost += take * 1.05f;
                        anyHeal = 1;
                        flesh = part->flesh;
                    }
                    continue;
                }

                float target = maxHp * g_cfg.limbBudThreshold;
                if (target < 5.f) target = maxHp * 0.40f;

                if (flesh < target && budget > 0.f)
                {
                    float need = target - flesh;
                    float room = budget * rate;
                    float take = need < room ? need : room;
                    if (take > 0.f)
                    {
                        part->flesh = flesh + take;
                        budget -= take / (rate > 0.01f ? rate : 1.f);
                        hungerCost += take * 1.15f;
                        anyHeal = 1;
                        flesh = part->flesh;
                    }
                }

                // Transition: Stump → Restored organic limb (weak)
                if (flesh >= target * 0.98f)
                {
                    robots->setLimb(kEnum[i], LIMB_ORIGINAL, nullptr);
                    part = ResolveLimb(med, i);
                    if (part)
                    {
                        float start = maxHp * g_cfg.limbRestoredStart;
                        if (start < 1.f) start = maxHp * 0.18f;
                        part->flesh = start;
                        if (part->fleshStun < maxHp * 0.45f)
                            part->fleshStun = maxHp * 0.45f;
                        TF_SEH_TRY { part->updateDerivedHealths(); }
                        TF_SEH_EXCEPT { }
                    }
                    anyHeal = 1;
                    if (worstSev < 0.85f) worstSev = 0.85f;
                    // Massive food cost + longer KO — regrowing a limb is traumatic
                    SpendHungerAndKnockout(med, g_cfg.restoreHungerCost, g_cfg.restoreKoSeconds,
                        "RESTORED limb");
                    continue;
                }
            }
            // ========== ORIGINAL weak — strengthen ==========
            else if (eff == LIMB_ORIGINAL || st == LIMB_ORIGINAL)
            {
                float pct = flesh / maxHp;
                if (pct < 0.f) pct = 0.f;
                if (pct < g_cfg.limbStrongPct)
                {
                    float sev = 1.f - (pct / (g_cfg.limbStrongPct > 0.1f ? g_cfg.limbStrongPct : 0.7f));
                    if (sev > worstSev) worstSev = sev;
                    if (budget > 0.f && flesh < maxHp)
                    {
                        float need = maxHp - flesh;
                        float room = budget * 0.65f;
                        float take = need < room ? need : room;
                        if (take > 0.f)
                        {
                            part->flesh = flesh + take;
                            budget -= take / 0.65f;
                            hungerCost += take * 0.32f;
                            anyHeal = 1;
                            if (part->fleshStun > 0.f)
                            {
                                float stn = part->fleshStun;
                                float stTake = take * 0.75f;
                                if (stTake > stn) stTake = stn;
                                part->fleshStun -= stTake;
                            }
                        }
                    }
                }
            }
        }
    }
    TF_SEH_EXCEPT
    {
        static int once = 0;
        if (!once) { LogErr("ToughnessFeast: limb regrow SEH"); once = 1; }
    }
}

static void ApplyFeastTick(CharStats* stats, float dt)
{
    if (!g_cfg.enableMedical || g_inRegen) return;
    if (!stats || dt <= 0.f) return;
    if (dt > 0.25f) dt = 0.25f;

    float power = FeastPower(stats);
    if (power <= 0.f) return;

    MedicalSystem* med = stats->medical;
    if (!med || med->dead) return;

    Character* me = CharFromStats(stats);
    if (!me || !me->amSomeoneWhoNeedsToEatToLive()) return;

    float hunger = med->hunger;
    if (hunger != hunger || hunger < 0.f || hunger > 5.f) return;
    if (hunger < g_cfg.minHunger) return;

    g_inRegen = 1;

    float fleshBudget = g_cfg.fleshHealPerSec * power * dt;
    float stunBudget  = g_cfg.stunHealPerSec * power * dt;
    float hungerCost  = 0.f;
    int anyHeal = 0;
    float worstSev = 0.f;

    ProcessLimbs(med, stats, power, dt, hungerCost, anyHeal, worstSev);

    // General flesh / unhealable residual (skip stumps — ProcessLimbs owns those)
    TF_SEH_TRY
    {
        int count = med->getPartCount();
        if (count < 0) count = 0;
        if (count > 32) count = 32;
        for (int i = 0; i < count; ++i)
        {
            MedicalSystem::HealthPartStatus* part = med->getPart((unsigned long long)i);
            if (!part || part->isRobotic()) continue;
            LimbState ls = part->getRobotLimbState();
            if (ls == LIMB_STUMP || ls == LIMB_CRUSHED || ls == LIMB_REPLACED)
                continue;

            float maxHp = part->maxHealth();
            if (maxHp < 1.f) maxHp = part->_maxHealth;
            if (maxHp < 1.f || maxHp > 10000.f) continue;
            float flesh = part->flesh;
            if (flesh != flesh || flesh > maxHp * 3.f) continue;

            if (part->fleshStun > 0.f && stunBudget > 0.f)
            {
                float st = part->fleshStun;
                if (st > 0.f && st < 1e6f)
                {
                    if (st > stunBudget) st = stunBudget;
                    part->fleshStun -= st;
                    stunBudget -= st;
                    hungerCost += st * 0.07f;
                    anyHeal = 1;
                }
            }

            if (flesh < maxHp && fleshBudget > 0.f)
            {
                int over = flesh < 0.f ? 1 : 0;
                if (over && !g_cfg.healUnhealable) continue;
                float rate = over ? g_cfg.overdamageHealMult : 1.f;
                float need = over ? (-flesh + 0.5f) : (maxHp - flesh);
                if (need <= 0.f) continue;
                float room = fleshBudget * rate;
                float take = need < room ? need : room;
                part->flesh = flesh + take;
                fleshBudget -= take / (rate > 0.01f ? rate : 1.f);
                hungerCost += take * (over ? 0.45f : 0.14f);
                anyHeal = 1;
            }
        }
    }
    TF_SEH_EXCEPT { /* skip */ }

    if (anyHeal && hungerCost > 0.f)
    {
        // hunger is 0..1 full-bar units roughly; scale cost down to a gentle drain
        float drain = hungerCost * g_cfg.hungerDrainPerSec * 0.02f;
        if (drain > 0.08f) drain = 0.08f;
        float h = med->hunger - drain;
        if (h < 0.f) h = 0.f;
        med->hunger = h;
    }

    if (worstSev > 0.05f)
        ApplyWeakLimbPenalty(stats, worstSev);

    g_inRegen = 0;

    static int logCd = 0;
    if (g_cfg.debugLog && anyHeal && (++logCd % 25) == 0)
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "ToughnessFeast: feast t=%.0f p=%.2f sev=%.2f",
            GetToughness(stats), power, worstSev);
        Log(msg);
    }
}

// ---------------------------------------------------------------------------
// Hunger tooltip — Feast journal
// ---------------------------------------------------------------------------

// MSVC2010 string layout for StringPair ctor
struct GameStr
{
    union { char sso[16]; char* ptr; } u;
    size_t size;
    size_t cap;
};

static void GameStrSet(GameStr* s, const char* text)
{
    std::memset(s, 0, sizeof(*s));
    if (!text) text = "";
    size_t n = std::strlen(text);
    if (n > 95) n = 95;
    if (n < 16)
    {
        std::memcpy(s->u.sso, text, n);
        s->size = n;
        s->cap = 15;
    }
    else
    {
        // ring buffer for longer lines (heap-layout MSVC2010 string)
        static char pool[24][96];
        static int pi = 0;
        char* slot = pool[pi++ % 24];
        std::memcpy(slot, text, n);
        slot[n] = 0;
        s->u.ptr = slot;
        s->size = n;
        s->cap = 95;
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

static int TipAppend(lektor<StringPair>* dats, const char* left, const char* right)
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    (void)dats; (void)left; (void)right;
    return 0;
#else
    if (!dats || !dats->stuff || dats->maxSize == 0) return 0;
    ResolveStringPairCtor();
    if (!g_spCtor) return 0;
    static const size_t kPair = 0x60;
    if (dats->count >= dats->maxSize)
        dats->count = dats->maxSize - 1;
    GameStr a, b;
    GameStrSet(&a, left ? left : "");
    GameStrSet(&b, right ? right : "");
    void* slot = (char*)(void*)dats->stuff + (size_t)dats->count * kPair;
    std::memset(slot, 0, kPair);
    g_spCtor(slot, &a, &b);
    dats->count += 1;
    return 1;
#endif
}

static void StripOldFeastBlock(lektor<StringPair>* dats)
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
        std::memcpy(&res,  sp + 24, sizeof(res));
        if (size == 0 || size > 80 || res > 0x200000u) continue;
        const char* data = nullptr;
        if (res < 16u) data = (const char*)sp;
        else std::memcpy(&data, sp, sizeof(data));
        if (!data) continue;
        if (std::strncmp(data, "== Feast", 8) == 0
         || std::strncmp(data, "== Toughness", 12) == 0
         || std::strncmp(data, "-- Limbs", 8) == 0)
        {
            dats->count = i;
            return;
        }
    }
#endif
}

static void AppendFeastJournal(lektor<StringPair>* dats, CharStats* stats)
{
    if (!dats || !stats || !dats->stuff) return;
    if (dats->maxSize < 6) return;

    StripOldFeastBlock(dats);

    // Carve room for full journal — limbs first so they always fit
    unsigned need = (unsigned)g_cfg.tooltipMaxLines;
    if (need < 14) need = 14;
    if (need + 1 > dats->maxSize)
        need = dats->maxSize > 3 ? dats->maxSize - 1 : 0;
    if (need > 0 && dats->count + need > dats->maxSize)
        dats->count = dats->maxSize - need;

    unsigned start = dats->count;
    unsigned budget = need > 0 ? need : 14;

    auto room = [&]() -> int {
        return (dats->count < dats->maxSize && dats->count - start < budget) ? 1 : 0;
    };
    auto line = [&](const char* a, const char* b) {
        if (room()) TipAppend(dats, a, b);
    };

    float tough = GetToughness(stats);
    int tInt = (int)(tough + 0.5f);
    if (tInt < 0) tInt = 0;
    float power = FeastPower(stats);
    float unlock = UnlockFor(stats);
    RaceKind rk = DetectRace(stats);

    MedicalSystem* med = stats->medical;
    float hungerPct = -1.f;
    if (med && !med->dead)
    {
        float h = med->hunger;
        if (h == h && h >= 0.f && h <= 5.f) hungerPct = h * 100.f;
    }

    line("== Feast ==", "live");
    {
        char r[40];
        std::snprintf(r, sizeof(r), "%d (cap %.0f)", tInt, g_cfg.combatCap);
        line("Toughness", r);
    }
    line("Race", RaceName(rk));

    // LIMBS FIRST (priority) — always try even if dead flag weird
    LimbInfo limbs[4];
    int nLimbs = 0;
    if (med)
        nLimbs = CollectLimbs(stats, limbs, 4);

    int active = 0, missing = 0;
    line("-- Limbs --", "4 slots");
    if (nLimbs <= 0)
    {
        line("Limbs", "scan failed");
        // Still print placeholders so user sees the section
        line("Left Arm", "?");
        line("Right Arm", "?");
        line("Left Leg", "?");
        line("Right Leg", "?");
    }
    else
    {
        for (int i = 0; i < nLimbs; ++i)
        {
            if (limbs[i].active) ++active;
            if (limbs[i].missing) ++missing;
            char right[56];
            std::snprintf(right, sizeof(right), "%s %s", limbs[i].stage, limbs[i].detail);
            line(limbs[i].name, right);
        }
    }
    {
        char r[48];
        if (missing > 0)
            std::snprintf(r, sizeof(r), "%d missing", missing);
        else if (active > 0)
            std::snprintf(r, sizeof(r), "%d mending", active);
        else
            std::snprintf(r, sizeof(r), "all sound");
        line("Summary", r);
    }

    if (rk == RACE_ROBOT)
    {
        line("Feast", "Skeletons cannot");
        line("== end ==", "-");
        return;
    }

    if (power <= 0.f)
    {
        char r[40];
        std::snprintf(r, sizeof(r), "need %.0f tgh", unlock);
        line("Feast", "LOCKED");
        line("Unlock", r);
    }
    else
    {
        char r[48];
        float drain = g_cfg.hungerDrainPerSec * power * 100.f;
        std::snprintf(r, sizeof(r), "ON pwr %.2f", power);
        line("Feast", r);
        std::snprintf(r, sizeof(r), "~%.1f%%/s heal", drain);
        line("Food use", r);
    }

    if (hungerPct >= 0.f)
    {
        char r[40];
        int ok = hungerPct >= g_cfg.minHunger * 100.f ? 1 : 0;
        std::snprintf(r, sizeof(r), "%.0f%% %s", hungerPct, ok ? "fed" : "TOO LOW");
        line("Hunger", r);
    }

    if (active > 0 || missing > 0)
        line("Note", "stage done = food dump + KO");
    line("== end ==", "hover Hunger");

    static int tipLog = 0;
    if (g_cfg.debugLog && ((++tipLog) % 5) == 1)
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "ToughnessFeast: journal t=%d limbs=%d miss=%d act=%d",
            tInt, nLimbs, missing, active);
        Log(msg);
    }
}

#if !defined(TOUGHNESSFEAST_LINUX_IDE)
static void (*orig_hungerTip)(CharStats*, lektor<StringPair>*) = nullptr;
static void hook_hungerTip(CharStats* self, lektor<StringPair>* dats)
{
    if (orig_hungerTip) orig_hungerTip(self, dats);
    if (!g_cfg.enableTooltips || !self || !dats) return;
    if (!dats->stuff || dats->maxSize == 0 || dats->maxSize > 256) return;
    if (dats->count > dats->maxSize) return;
    if (g_inRegen) return;
    TF_SEH_TRY { AppendFeastJournal(dats, self); }
    TF_SEH_EXCEPT
    {
        static int once = 0;
        if (!once) { LogErr("ToughnessFeast: tooltip SEH"); once = 1; }
    }
}
#endif

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

static void InstallHooks()
{
    Log("ToughnessFeast: installing hooks...");
    ResolveStatApi();

#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    HookExport("?calculateToughnessDamageResistanceMult@CharStats@@QEAAMXZ",
               (void*)hook_DR, (void**)&orig_DR,
               "ToughnessFeast: DR soft-cap");
    HookExport("?calculateToughnessWoundDegenerationRate@CharStats@@QEAAMXZ",
               (void*)hook_Wound, (void**)&orig_Wound,
               "ToughnessFeast: wound degen soft-cap");
    HookExport("?xpStat_eventBased@CharStats@@QEAAXW4StatsEnumerated@@M@Z",
               (void*)hook_xpEvent, (void**)&orig_xpEvent,
               "ToughnessFeast: XP event past 100");
    HookExport("?xpStat_timeBased@CharStats@@QEAAXW4StatsEnumerated@@@Z",
               (void*)hook_xpTime, (void**)&orig_xpTime,
               "ToughnessFeast: XP time + feast tick");

    if (g_cfg.enableTooltips)
    {
        HookExport(
            "?printExertionHungerMultTooltip@CharStats@@QEAAXPEAV?$lektor@VStringPair@@@@@Z",
            (void*)hook_hungerTip, (void**)&orig_hungerTip,
            "ToughnessFeast: Hunger Feast journal");
        ResolveStringPairCtor();
        if (g_spCtor) Log("ToughnessFeast: StringPair ctor OK");
        else LogErr("ToughnessFeast: StringPair ctor missing");
    }
#endif

    if (g_cfg.enableMedical)
        Log("ToughnessFeast: feast regen ON (xp-time tick, not on hit)");
    if (g_cfg.enableLimbRestore)
        Log("ToughnessFeast: staged limb restore ON");
}

#if defined(_MSC_VER)
#define TF_EXPORT __declspec(dllexport)
#else
#define TF_EXPORT __attribute__((visibility("default")))
#endif

// RE_Kenshi entry — mangled C++ export
TF_EXPORT void startPlugin()
{
    Log("ToughnessFeast: startPlugin");
    ResolvePluginDir();
    LoadConfig();

    if (!g_cfg.enableHooks)
    {
        Log("ToughnessFeast: SAFE MODE (EnableHooks=0)");
        return;
    }

    InstallHooks();
    Log("ToughnessFeast: ready — hover Hunger for Feast journal");
}
