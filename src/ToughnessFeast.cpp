// ToughnessFeast — RE_Kenshi plugin
//
// Build notes:
// - Export MUST be C++ mangled: ?startPlugin@@YAXXZ  (no extern "C")
// - Avoid reading game std::string members (VS2010 vs modern ABI crash)
// - Avoid <fstream>/<iostream> (static CRT init can crash under mixed runtime)
// - Only call game methods that return POD / raw pointers

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
// Intentionally NO GameData / StringPair / DatapanelGUI / fstream
#endif

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

// ---------------------------------------------------------------------------
// Config (POD only)
// ---------------------------------------------------------------------------

struct Config
{
    float combatCapToughness;
    float foodRegenStart;       // single threshold (no race string reads)
    float foodRegenStartHiver;  // used if RaceData::robot==false and we use heuristic later
    float foodRegenScalePerPoint;
    float foodRegenScaleHiver;
    float fleshHealPerSecond;
    float stunHealPerSecond;
    float hungerDrainPerSecond;
    float minHungerToRegen;
    int   healUnhealable;
    float limbRegrowPerSecond;
    float limbRestoreFleshPct;
    float past100XpMult;
    int   debugLog;
    // Optional race thresholds without reading names:
    //   if RaceData::robot -> no food regen (skeletons handled via amSomeoneWhoNeedsToEatToLive)
    //   if RaceData::gigantic -> treat as shek-ish (shek are large) — weak heuristic
    //   else use foodRegenStart
    float foodRegenStartShek;
    int   useRaceHeuristics;
};

static Config g_cfg = {
    100.f,   // combatCap
    75.f,    // foodRegenStart (human/default)
    0.f,     // foodRegenStartHiver (unused if heuristics off for hiver names)
    0.04f,   // scale
    0.012f,  // hiver scale
    2.5f,    // flesh/s
    1.5f,    // stun/s
    0.012f,  // hunger drain
    0.10f,   // min hunger
    1,       // heal unhealable
    3.0f,    // limb regrow/s
    0.85f,   // restore pct
    0.18f,   // past100 xp
    1,       // debugLog ON while testing
    50.f,    // shek start
    1        // useRaceHeuristics
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
    return (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y') ? 1 : 0;
}

static void LoadConfig()
{
    char path[MAX_PATH + 32];
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
        DebugLog("ToughnessFeast: no config.ini, defaults");
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
        if (std::strcmp(key, "CombatCapToughness") == 0) g_cfg.combatCapToughness = (float)std::atof(val);
        else if (std::strcmp(key, "FoodRegenStartToughness") == 0 || std::strcmp(key, "FoodRegenStartHuman") == 0
              || std::strcmp(key, "FoodRegenStartOther") == 0)
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

    char msg[192];
    std::snprintf(msg, sizeof(msg),
        "ToughnessFeast: cfg start=%.0f shek=%.0f hiver=%.0f limb=%.1f",
        g_cfg.foodRegenStart, g_cfg.foodRegenStartShek,
        g_cfg.foodRegenStartHiver, g_cfg.limbRegrowPerSecond);
    DebugLog(msg);
}

// ---------------------------------------------------------------------------
// Race heuristics WITHOUT reading std::string names (ABI-safe)
// ---------------------------------------------------------------------------

// Hivers aren't flagged in POD alone reliably. We use:
//   - skeletons: amSomeoneWhoNeedsToEatToLive() == false
//   - RaceData::gigantic ~ shek / giants → shek threshold
//   - else default human threshold
// Config UseRaceHeuristics=0 forces single FoodRegenStartHuman for everyone.
// Hiver early unlock: set FoodRegenStartHiver and we'll also check healRate /
// hungerRate POD fields which differ by race in vanilla data.
static float FoodRegenStartFor(Character* me)
{
    if (!g_cfg.useRaceHeuristics)
        return g_cfg.foodRegenStart;

    RaceData* race = me ? me->getRace() : nullptr;
    if (!race)
        return g_cfg.foodRegenStart;

    // Skeletons / robots: caller already skips non-eaters; keep default
    if (race->robot)
        return g_cfg.foodRegenStart;

    // Shek / large races
    if (race->gigantic)
        return g_cfg.foodRegenStartShek;

    // Hiver-ish: vanilla hivers have higher hungerRate than greenlanders.
    // Greenlander hungerRate ~1.0, hiver worker/soldier often higher; shek similar to human.
    // Use healRate as secondary signal (hivers heal differently).
    // These are floats baked into RaceData — POD, safe.
    if (race->hungerRate > 1.15f && race->healRate > 0.f && !race->gigantic)
        return g_cfg.foodRegenStartHiver;

    return g_cfg.foodRegenStart;
}

static float FoodRegenScaleFor(Character* me)
{
    if (!g_cfg.useRaceHeuristics)
        return g_cfg.foodRegenScalePerPoint;
    RaceData* race = me ? me->getRace() : nullptr;
    if (race && !race->robot && !race->gigantic && race->hungerRate > 1.15f)
        return g_cfg.foodRegenScaleHiver;
    return g_cfg.foodRegenScalePerPoint;
}

static float RegenPower(Character* me, float toughness)
{
    float excess = toughness - FoodRegenStartFor(me);
    if (excess <= 0.f) return 0.f;
    return excess * FoodRegenScaleFor(me);
}

// ---------------------------------------------------------------------------
// Soft-cap combat toughness
// ---------------------------------------------------------------------------

struct ToughnessClamp
{
    CharStats* stats;
    float saved;
    int active;
    ToughnessClamp(CharStats* s, float cap) : stats(s), saved(0.f), active(0)
    {
        if (!s) return;
        saved = s->_toughness;
        if (saved > cap) { s->_toughness = cap; active = 1; }
    }
    ~ToughnessClamp()
    {
        if (active && stats) stats->_toughness = saved;
    }
};

static float (*calculateToughnessDamageResistanceMult_orig)(CharStats*) = nullptr;
static float calculateToughnessDamageResistanceMult_hook(CharStats* self)
{
    ToughnessClamp clamp(self, g_cfg.combatCapToughness);
    return calculateToughnessDamageResistanceMult_orig(self);
}

static float (*calculateToughnessWoundDegenerationRate_orig)(CharStats*) = nullptr;
static float calculateToughnessWoundDegenerationRate_hook(CharStats* self)
{
    ToughnessClamp clamp(self, g_cfg.combatCapToughness);
    return calculateToughnessWoundDegenerationRate_orig(self);
}

// ---------------------------------------------------------------------------
// XP past 100
// ---------------------------------------------------------------------------

static void (*xpStat_eventBased_orig)(CharStats*, StatsEnumerated, float) = nullptr;
static void xpStat_eventBased_hook(CharStats* self, StatsEnumerated st, float amount)
{
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

// ---------------------------------------------------------------------------
// Food regen + limb regrow
// ---------------------------------------------------------------------------

static void ApplyFoodRegen(MedicalSystem* med, float frameTime)
{
    if (!med || frameTime <= 0.f || med->dead) return;
    CharStats* stats = med->stats;
    Character* me = med->me;
    if (!stats || !me) return;
    if (!me->amSomeoneWhoNeedsToEatToLive()) return;

    float tough = stats->_toughness;
    float power = RegenPower(me, tough);
    if (power <= 0.f) return;
    if (power > 4.f) power = 4.f;
    if (med->hunger < g_cfg.minHungerToRegen) return;

    float fleshBudget = g_cfg.fleshHealPerSecond * power * frameTime;
    float stunBudget = g_cfg.stunHealPerSecond * power * frameTime;
    float limbBudget = g_cfg.limbRegrowPerSecond * power * frameTime;
    float hungerCost = 0.f;
    int anyHeal = 0;

    // Stumps / crushed — do not skip isDead limbs
    if (limbBudget > 0.f && g_cfg.limbRegrowPerSecond > 0.f)
    {
        MedicalSystem::HealthPartStatus* limbs[4] = {
            med->leftArm, med->rightArm, med->leftLeg, med->rightLeg
        };
        for (int li = 0; li < 4; ++li)
        {
            MedicalSystem::HealthPartStatus* part = limbs[li];
            if (!part) continue;
            if (part->isRobotic()) continue;

            LimbState ls = part->getRobotLimbState();
            if (ls == LIMB_REPLACED) continue;

            int missing = (ls == LIMB_STUMP || ls == LIMB_CRUSHED) ? 1 : 0;
            float maxHp = part->maxHealth();
            if (maxHp <= 1.f) maxHp = part->_maxHealth;
            if (maxHp <= 1.f) maxHp = 100.f;

            if (!missing && part->flesh > maxHp * 0.05f)
                continue;

            if (part->flesh < maxHp && limbBudget > 0.f)
            {
                if (part->flesh < 0.f) part->flesh = 0.f;
                float need = maxHp - part->flesh;
                float take = (need < limbBudget) ? need : limbBudget;
                if (ls == LIMB_CRUSHED) take *= 0.75f;
                part->flesh += take;
                limbBudget -= take;
                hungerCost += take * 0.8f;
                anyHeal = 1;
            }

            float restoreAt = maxHp * g_cfg.limbRestoreFleshPct;
            if (restoreAt < 1.f) restoreAt = maxHp * 0.85f;

            if (missing && part->flesh >= restoreAt && med->robotLimbs)
            {
                RobotLimbs::Limb limbEnum = part->getRobotLimbEnum();
                if (limbEnum != RobotLimbs::NULL_LIMB)
                {
                    med->robotLimbs->setLimb(limbEnum, LIMB_ORIGINAL, nullptr);
                    if (part->flesh < maxHp * 0.9f) part->flesh = maxHp * 0.9f;
                    part->fleshStun = 0.f;
                    part->updateDerivedHealths();
                    anyHeal = 1;
                    DebugLog("ToughnessFeast: restored limb");
                }
            }
            else
            {
                part->updateDerivedHealths();
            }
        }
    }

    int count = med->getPartCount();
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < count; ++i)
        {
            MedicalSystem::HealthPartStatus* part = med->getPart((unsigned long long)i);
            if (!part || part->isRobotic()) continue;

            LimbState ls = part->getRobotLimbState();
            int missing = (ls == LIMB_STUMP || ls == LIMB_CRUSHED) ? 1 : 0;
            if (part->isDead() && !missing) continue;
            if (missing) continue;

            float maxHp = part->maxHealth();
            if (maxHp <= 0.f) continue;

            int isOver = (part->flesh < 0.f) ? 1 : 0;
            if (pass == 0 && !isOver) continue;
            if (pass == 1 && isOver) continue;
            if (isOver && !g_cfg.healUnhealable) continue;

            if (part->fleshStun > 0.f && stunBudget > 0.f)
            {
                float take = (part->fleshStun < stunBudget) ? part->fleshStun : stunBudget;
                part->fleshStun -= take;
                stunBudget -= take;
                hungerCost += take * 0.15f;
                anyHeal = 1;
            }
            if (part->flesh < maxHp && fleshBudget > 0.f)
            {
                float need = maxHp - part->flesh;
                float rate = isOver ? 0.45f : 1.f;
                float room = fleshBudget * rate;
                float take = (need < room) ? need : room;
                part->flesh += take;
                fleshBudget -= take / rate;
                hungerCost += take * (isOver ? 0.55f : 0.25f);
                anyHeal = 1;
            }
            part->updateDerivedHealths();
        }
    }

    if (anyHeal && hungerCost > 0.f)
    {
        float drain = g_cfg.hungerDrainPerSecond * power * frameTime;
        drain += hungerCost * 0.0015f;
        float next = med->hunger - drain;
        med->hunger = (next > 0.f) ? next : 0.f;
        if (g_cfg.debugLog)
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                "ToughnessFeast: regen t=%.1f p=%.2f h=%.3f", tough, power, med->hunger);
            DebugLog(msg);
        }
    }
}

static void (*medicalUpdate_orig)(MedicalSystem*, float) = nullptr;
static void medicalUpdate_hook(MedicalSystem* self, float frameTime)
{
    if (medicalUpdate_orig)
        medicalUpdate_orig(self, frameTime);
    ApplyFoodRegen(self, frameTime);
}

// ---------------------------------------------------------------------------
// Safe hook install (SEH on MSVC so one bad address doesn't kill Kenshi)
// ---------------------------------------------------------------------------

static void ResolvePluginDir()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    (void)g_pluginDir;
#else
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
#endif
}

#if defined(_MSC_VER)
static int TryAddHook(void* target, void* detour, void** original, const char* name)
{
    __try
    {
        if (!target)
        {
            ErrorLog(name);
            ErrorLog("ToughnessFeast: GetRealAddress returned null");
            return 0;
        }
        if (KenshiLib::SUCCESS != KenshiLib::AddHook(target, detour, original))
        {
            ErrorLog(name);
            ErrorLog("ToughnessFeast: AddHook failed");
            return 0;
        }
        DebugLog(name);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ErrorLog(name);
        ErrorLog("ToughnessFeast: exception during hook install");
        return 0;
    }
}
#else
static int TryAddHook(void* target, void* detour, void** original, const char* name)
{
    if (!target) return 0;
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(target, detour, original)) return 0;
    DebugLog(name);
    return 1;
}
#endif

// RE_Kenshi: GetProcAddress(plugin, "?startPlugin@@YAXXZ") — C++ linkage required
#if defined(_MSC_VER)
#define TF_EXPORT __declspec(dllexport)
#else
#define TF_EXPORT __attribute__((visibility("default")))
#endif

TF_EXPORT void startPlugin()
{
    DebugLog("ToughnessFeast: startPlugin entered");
    ResolvePluginDir();
    LoadConfig();
    DebugLog("ToughnessFeast: installing hooks...");

    // Core combat soft-cap
    TryAddHook(
        (void*)KenshiLib::GetRealAddress(&CharStats::calculateToughnessDamageResistanceMult),
        (void*)calculateToughnessDamageResistanceMult_hook,
        (void**)&calculateToughnessDamageResistanceMult_orig,
        "ToughnessFeast: hooked DR");

    TryAddHook(
        (void*)KenshiLib::GetRealAddress(&CharStats::calculateToughnessWoundDegenerationRate),
        (void*)calculateToughnessWoundDegenerationRate_hook,
        (void**)&calculateToughnessWoundDegenerationRate_orig,
        "ToughnessFeast: hooked wound degen");

    // XP past 100
    TryAddHook(
        (void*)KenshiLib::GetRealAddress(&CharStats::xpStat_eventBased),
        (void*)xpStat_eventBased_hook,
        (void**)&xpStat_eventBased_orig,
        "ToughnessFeast: hooked xp event");

    TryAddHook(
        (void*)KenshiLib::GetRealAddress(&CharStats::xpStat_timeBased),
        (void*)xpStat_timeBased_hook,
        (void**)&xpStat_timeBased_orig,
        "ToughnessFeast: hooked xp time");

    // Food regen / limbs — this is the critical gameplay hook
    TryAddHook(
        (void*)KenshiLib::GetRealAddress(&MedicalSystem::medicalUpdate),
        (void*)medicalUpdate_hook,
        (void**)&medicalUpdate_orig,
        "ToughnessFeast: hooked medicalUpdate");

    // NO GUI hooks — they pass std::string across the VS2010/modern ABI boundary.

    DebugLog("ToughnessFeast: ready");
}
