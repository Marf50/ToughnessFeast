// ToughnessFeast — RE_Kenshi plugin
// Export: C++ mangled ?startPlugin@@YAXXZ (NOT extern "C")
//
// EnableHooks=1 in config installs combat soft-cap + food limb regen.
// Set EnableHooks=0 to load-only if diagnosing crashes.

#if defined(TOUGHNESSFEAST_LINUX_IDE)
#include "kenshi_ide_stubs.h"
#else
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
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    float foodRegenStart;
    float foodRegenStartShek;
    float foodRegenStartHiver;
    float foodRegenScalePerPoint;
    float foodRegenScaleHiver;
    float fleshHealPerSecond;
    float stunHealPerSecond;
    float hungerDrainPerSecond;
    float minHungerToRegen;
    int healUnhealable;
    float limbRegrowPerSecond;
    float limbRestoreFleshPct;
    float past100XpMult;
    int debugLog;
    int useRaceHeuristics;
    int enableHooks; // 0 = load-only (safe), 1 = install gameplay hooks
    int enableMedicalHooks; // 0 = skip medicalUpdate (world load safer)
    int enableAnatomyPass; // 0 = only direct limb pointers (safer)
};

static Config g_cfg = {
    100.f, 75.f, 50.f, 0.f,
    0.04f, 0.012f,
    2.5f, 1.5f, 0.012f, 0.10f,
    1, 3.0f, 0.85f, 0.18f,
    1,  // debugLog
    1,  // useRaceHeuristics
    1,  // enableHooks
    1,  // enableMedicalHooks
    0   // enableAnatomyPass default OFF (getPart was crash-risk)
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
        else if (std::strcmp(key, "EnableAnatomyPass") == 0) g_cfg.enableAnatomyPass = ParseBoolC(val);
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
        else if (std::strcmp(key, "LimbRestoreFleshPercent") == 0) g_cfg.limbRestoreFleshPct = (float)std::atof(val);
        else if (std::strcmp(key, "Past100XpMult") == 0) g_cfg.past100XpMult = (float)std::atof(val);
        else if (std::strcmp(key, "DebugLog") == 0) g_cfg.debugLog = ParseBoolC(val);
        else if (std::strcmp(key, "UseRaceHeuristics") == 0) g_cfg.useRaceHeuristics = ParseBoolC(val);
    }
    std::fclose(f);
}

// ---------- gameplay (only used if EnableHooks=1) ----------

// Ultra-safe regen: only touch POD fields on MedicalSystem / HealthPartStatus /
// CharStats. No getRace, no setLimb, no isRobotic/maxHealth methods.
// Those cross-DLL method calls were crashing on world load / in-game.

static float RegenPowerFromToughness(float toughness)
{
    float excess = toughness - g_cfg.foodRegenStart;
    if (excess <= 0.f) return 0.f;
    float power = excess * g_cfg.foodRegenScalePerPoint;
    if (power > 4.f) power = 4.f;
    return power;
}

static void ApplyFoodRegen(MedicalSystem* med, float frameTime)
{
    if (!med) return;
    if (frameTime <= 0.f || frameTime > 2.f) return;

    // POD flags on MedicalSystem
    if (med->dead) return;

    CharStats* stats = med->stats;
    if (!stats) return;

    float tough = stats->_toughness;
    // Sanity: toughness is normally 0..200-ish; reject garbage
    if (tough < 0.f || tough > 500.f) return;

    float power = RegenPowerFromToughness(tough);
    if (power <= 0.f) return;

    // hunger is POD float 0..1
    float hunger = med->hunger;
    if (hunger < g_cfg.minHungerToRegen) return;
    if (hunger > 10.f) return; // garbage guard

    float fleshBudget = g_cfg.fleshHealPerSecond * power * frameTime;
    float stunBudget = g_cfg.stunHealPerSecond * power * frameTime;
    float limbBudget = g_cfg.limbRegrowPerSecond * power * frameTime;
    float hungerCost = 0.f;
    int anyHeal = 0;

    // Prefer direct limb pointers (POD) for stump rebuild — no method calls
    MedicalSystem::HealthPartStatus* limbs[4] = {
        med->leftArm, med->rightArm, med->leftLeg, med->rightLeg
    };
    for (int li = 0; li < 4; ++li)
    {
        MedicalSystem::HealthPartStatus* part = limbs[li];
        if (!part) continue;

        // Only POD fields. Treat very low flesh as "missing / ruined" candidate.
        float maxHp = part->_maxHealth;
        if (maxHp < 1.f || maxHp > 10000.f) continue;

        float flesh = part->flesh;
        // reject NaN/garbage
        if (flesh != flesh) continue;
        if (flesh > maxHp * 2.f) continue;

        // Rebuild heavily damaged / missing-looking flesh on limbs
        if (flesh < maxHp * 0.95f && limbBudget > 0.f)
        {
            // Focus budget on limbs that are badly hurt (stump-like)
            float severity = 1.f;
            if (flesh < 0.f)
            {
                if (!g_cfg.healUnhealable) continue;
                severity = 0.5f;
                flesh = 0.f; // start climbing from 0
            }
            else if (flesh > maxHp * 0.25f)
            {
                // mostly healthy limb — leave for general pass
                continue;
            }

            float need = maxHp - flesh;
            float take = need;
            if (take > limbBudget * severity) take = limbBudget * severity;
            if (take < 0.f) continue;
            part->flesh = flesh + take;
            limbBudget -= take;
            hungerCost += take * 0.5f;
            anyHeal = 1;

            // Soft stun clear
            if (part->fleshStun > 0.f && stunBudget > 0.f)
            {
                float st = part->fleshStun;
                if (st > stunBudget) st = stunBudget;
                part->fleshStun -= st;
                stunBudget -= st;
            }
        }
    }

    // Optional anatomy pass (getPart). Off by default — method iteration crashed for some.
    if (g_cfg.enableAnatomyPass)
    {
        int count = 0;
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
        count = med->getPartCount();
#endif
        if (count < 0) count = 0;
        if (count > 32) count = 32;

        for (int i = 0; i < count; ++i)
        {
            MedicalSystem::HealthPartStatus* part = nullptr;
#if !defined(TOUGHNESSFEAST_LINUX_IDE)
            part = med->getPart((unsigned long long)i);
#endif
            if (!part) continue;

            float maxHp = part->_maxHealth;
            if (maxHp < 1.f || maxHp > 10000.f) continue;
            float flesh = part->flesh;
            if (flesh != flesh) continue;
            if (flesh > maxHp * 2.f) continue;

            if (part->fleshStun > 0.f && stunBudget > 0.f)
            {
                float st = part->fleshStun;
                if (st != st || st > 1e6f) continue;
                if (st > stunBudget) st = stunBudget;
                part->fleshStun -= st;
                stunBudget -= st;
                hungerCost += st * 0.1f;
                anyHeal = 1;
            }

            if (flesh < maxHp && fleshBudget > 0.f)
            {
                int isOver = (flesh < 0.f) ? 1 : 0;
                if (isOver && !g_cfg.healUnhealable) continue;
                float rate = isOver ? 0.4f : 1.f;
                float need = isOver ? (-flesh) : (maxHp - flesh);
                float room = fleshBudget * rate;
                float take = need;
                if (take > room) take = room;
                if (take <= 0.f) continue;
                part->flesh = flesh + take;
                fleshBudget -= take / (rate > 0.01f ? rate : 1.f);
                hungerCost += take * (isOver ? 0.5f : 0.2f);
                anyHeal = 1;
            }
        }
    }

    if (anyHeal && hungerCost > 0.f)
    {
        float drain = g_cfg.hungerDrainPerSecond * power * frameTime;
        drain += hungerCost * 0.001f;
        float next = hunger - drain;
        if (next < 0.f) next = 0.f;
        if (next > 10.f) next = hunger; // don't write garbage
        med->hunger = next;

        if (g_cfg.debugLog)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                "ToughnessFeast: regen t=%.1f p=%.2f h=%.3f", tough, power, next);
            DebugLog(msg);
        }
    }
}


// ---------------------------------------------------------------------------
// Soft-cap combat toughness at combatCapToughness (default 100)
// ---------------------------------------------------------------------------

static float (*calculateToughnessDamageResistanceMult_orig)(CharStats*) = nullptr;
static float calculateToughnessDamageResistanceMult_hook(CharStats* self)
{
    if (!self || !calculateToughnessDamageResistanceMult_orig)
        return 1.f;
    float saved = self->_toughness;
    if (saved > g_cfg.combatCapToughness)
        self->_toughness = g_cfg.combatCapToughness;
    float r = calculateToughnessDamageResistanceMult_orig(self);
    self->_toughness = saved;
    return r;
}

static float (*calculateToughnessWoundDegenerationRate_orig)(CharStats*) = nullptr;
static float calculateToughnessWoundDegenerationRate_hook(CharStats* self)
{
    if (!self || !calculateToughnessWoundDegenerationRate_orig)
        return 1.f;
    float saved = self->_toughness;
    if (saved > g_cfg.combatCapToughness)
        self->_toughness = g_cfg.combatCapToughness;
    float r = calculateToughnessWoundDegenerationRate_orig(self);
    self->_toughness = saved;
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
    float before = self->_toughness;
    xpStat_eventBased_orig(self, st, amount);
    float gained = self->_toughness - before;
    if (before >= 99.5f && gained < amount * 0.02f)
    {
        float over = (before > 100.f) ? (before - 100.f) : 0.f;
        float mult = g_cfg.past100XpMult / (1.f + over * 0.02f);
        float forced = amount * mult;
        if (forced > 0.f) self->_toughness = before + forced;
    }
}

static void (*xpStat_timeBased_orig)(CharStats*, StatsEnumerated) = nullptr;
static void xpStat_timeBased_hook(CharStats* self, StatsEnumerated st)
{
    if (!xpStat_timeBased_orig) return;
    if (!self || st != STAT_TOUGHNESS)
    {
        xpStat_timeBased_orig(self, st);
        return;
    }
    float before = self->_toughness;
    xpStat_timeBased_orig(self, st);
    if (before >= 99.5f && self->_toughness <= before + 0.0001f)
    {
        float over = (before > 100.f) ? (before - 100.f) : 0.f;
        float mult = g_cfg.past100XpMult / (1.f + over * 0.02f);
        self->_toughness = before + 0.002f * mult;
    }
}

static void (*medicalUpdate_orig)(MedicalSystem*, float) = nullptr;
static void medicalUpdate_hook(MedicalSystem* self, float frameTime)
{
    if (medicalUpdate_orig)
        medicalUpdate_orig(self, frameTime);
    if (!self) return;
    if (frameTime <= 0.f || frameTime > 1.5f) return;
    ApplyFoodRegen(self, frameTime);
}

// ---------------------------------------------------------------------------
// Resolve game functions through KenshiLib.dll exports, then GetRealAddress.
// Taking &Class::method in our DLL puts a LOCAL stub address into GetRealAddress,
// which asserts: "address appears to be in your own module".
// GetProcAddress(KenshiLib, mangledName) returns a pointer INSIDE KenshiLib.dll,
// which GetRealAddress can map to the real game function.
// ---------------------------------------------------------------------------

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

    // Food / limb regen
    if (g_cfg.enableMedicalHooks)
    {
        HookExport("?medicalUpdate@MedicalSystem@@QEAAXM@Z",
                   (void*)medicalUpdate_hook,
                   (void**)&medicalUpdate_orig,
                   "ToughnessFeast: hooked medicalUpdate");
    }
    else
        DebugLog("ToughnessFeast: medicalUpdate skipped (EnableMedicalHooks=0)");
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
    DebugLog("ToughnessFeast: ready (hooks on, POD-safe regen)");
}
