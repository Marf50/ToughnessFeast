// ToughnessFeast — RE_Kenshi plugin
// Export: C++ mangled ?startPlugin@@YAXXZ (NOT extern "C")
//
// SAFETY: EnableHooks=0 by default. startPlugin only logs.
// Set EnableHooks=1 in config.ini after the game opens cleanly.

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
};

static Config g_cfg = {
    100.f, 75.f, 50.f, 0.f,
    0.04f, 0.012f,
    2.5f, 1.5f, 0.012f, 0.10f,
    1, 3.0f, 0.85f, 0.18f,
    1, 1,
    0  // EnableHooks DEFAULT OFF until proven stable
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

static float FoodRegenStartFor(Character* me)
{
    if (!g_cfg.useRaceHeuristics) return g_cfg.foodRegenStart;
    RaceData* race = me ? me->getRace() : nullptr;
    if (!race) return g_cfg.foodRegenStart;
    if (race->robot) return g_cfg.foodRegenStart;
    if (race->gigantic) return g_cfg.foodRegenStartShek;
    if (race->hungerRate > 1.15f && !race->gigantic) return g_cfg.foodRegenStartHiver;
    return g_cfg.foodRegenStart;
}

static float FoodRegenScaleFor(Character* me)
{
    if (!g_cfg.useRaceHeuristics) return g_cfg.foodRegenScalePerPoint;
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

    if (limbBudget > 0.f && g_cfg.limbRegrowPerSecond > 0.f)
    {
        MedicalSystem::HealthPartStatus* limbs[4] = {
            med->leftArm, med->rightArm, med->leftLeg, med->rightLeg
        };
        for (int li = 0; li < 4; ++li)
        {
            MedicalSystem::HealthPartStatus* part = limbs[li];
            if (!part || part->isRobotic()) continue;
            LimbState ls = part->getRobotLimbState();
            if (ls == LIMB_REPLACED) continue;
            int missing = (ls == LIMB_STUMP || ls == LIMB_CRUSHED) ? 1 : 0;
            float maxHp = part->maxHealth();
            if (maxHp <= 1.f) maxHp = part->_maxHealth;
            if (maxHp <= 1.f) maxHp = 100.f;
            if (!missing && part->flesh > maxHp * 0.05f) continue;

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
                part->updateDerivedHealths();
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
            char msg[128];
            std::snprintf(msg, sizeof(msg), "ToughnessFeast: regen t=%.1f p=%.2f", tough, power);
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

static int TryAddHook(void* target, void* detour, void** original, const char* okMsg)
{
    if (!target)
    {
        ErrorLog("ToughnessFeast: null GetRealAddress");
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

static void InstallHooks()
{
    DebugLog("ToughnessFeast: EnableHooks=1, installing...");
    TryAddHook((void*)KenshiLib::GetRealAddress(&CharStats::calculateToughnessDamageResistanceMult),
               (void*)calculateToughnessDamageResistanceMult_hook,
               (void**)&calculateToughnessDamageResistanceMult_orig,
               "ToughnessFeast: hooked DR");
    TryAddHook((void*)KenshiLib::GetRealAddress(&CharStats::calculateToughnessWoundDegenerationRate),
               (void*)calculateToughnessWoundDegenerationRate_hook,
               (void**)&calculateToughnessWoundDegenerationRate_orig,
               "ToughnessFeast: hooked wound degen");
    TryAddHook((void*)KenshiLib::GetRealAddress(&CharStats::xpStat_eventBased),
               (void*)xpStat_eventBased_hook,
               (void**)&xpStat_eventBased_orig,
               "ToughnessFeast: hooked xp event");
    TryAddHook((void*)KenshiLib::GetRealAddress(&CharStats::xpStat_timeBased),
               (void*)xpStat_timeBased_hook,
               (void**)&xpStat_timeBased_orig,
               "ToughnessFeast: hooked xp time");
    TryAddHook((void*)KenshiLib::GetRealAddress(&MedicalSystem::medicalUpdate),
               (void*)medicalUpdate_hook,
               (void**)&medicalUpdate_orig,
               "ToughnessFeast: hooked medicalUpdate");
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
    DebugLog("ToughnessFeast: ready (hooks on)");
}
