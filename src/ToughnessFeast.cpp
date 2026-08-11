// ToughnessFeast — RE_Kenshi / KenshiLib plugin
// Soft-cap combat toughness at 100; race-based food regen; stump/crush limb regrow.
// IMPORTANT: only call game APIs that KenshiLib re-exports. GUI/StringPair calls
// break LoadLibrary on many RE_Kenshi installs.

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
#include <kenshi/GameData.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

struct Config
{
    float combatCapToughness;
    float foodRegenStartHuman;
    float foodRegenStartShek;
    float foodRegenStartHiver;
    float foodRegenStartOther;
    float foodRegenScalePerPoint;
    float foodRegenScaleHiver;
    float fleshHealPerSecond;
    float stunHealPerSecond;
    float hungerDrainPerSecond;
    float minHungerToRegen;
    bool healUnhealable;
    float limbRegrowPerSecond;
    float limbRestoreFleshPct;
    float past100XpMult;
    bool debugLog;
};

static Config g_cfg = {
    100.f,
    75.f,
    50.f,
    0.f,
    75.f,
    0.04f,
    0.012f,
    2.5f,
    1.5f,
    0.012f,
    0.10f,
    true,
    3.0f,
    0.85f,
    0.18f,
    false
};

static char g_pluginDir[MAX_PATH] = { 0 };

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool ParseBool(const std::string& v)
{
    return v == "1" || v == "true" || v == "True" || v == "yes" || v == "YES";
}

static void LoadConfig()
{
    char path[MAX_PATH + 32];
    if (g_pluginDir[0])
        std::snprintf(path, sizeof(path), "%s/config.ini", g_pluginDir);
    else
        std::snprintf(path, sizeof(path), "config.ini");

    std::ifstream in(path);
    if (!in && g_pluginDir[0])
    {
        std::snprintf(path, sizeof(path), "%s\\config.ini", g_pluginDir);
        in.open(path);
    }
    if (!in)
    {
        DebugLog("ToughnessFeast: no config.ini, using defaults");
        return;
    }

    std::string line;
    while (std::getline(in, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        if (key == "CombatCapToughness") g_cfg.combatCapToughness = (float)std::atof(val.c_str());
        else if (key == "FoodRegenStartToughness")
        {
            float v = (float)std::atof(val.c_str());
            g_cfg.foodRegenStartHuman = v;
            g_cfg.foodRegenStartOther = v;
        }
        else if (key == "FoodRegenStartHuman") g_cfg.foodRegenStartHuman = (float)std::atof(val.c_str());
        else if (key == "FoodRegenStartShek") g_cfg.foodRegenStartShek = (float)std::atof(val.c_str());
        else if (key == "FoodRegenStartHiver") g_cfg.foodRegenStartHiver = (float)std::atof(val.c_str());
        else if (key == "FoodRegenStartOther") g_cfg.foodRegenStartOther = (float)std::atof(val.c_str());
        else if (key == "FoodRegenScalePerPoint") g_cfg.foodRegenScalePerPoint = (float)std::atof(val.c_str());
        else if (key == "FoodRegenScaleHiver") g_cfg.foodRegenScaleHiver = (float)std::atof(val.c_str());
        else if (key == "FleshHealPerSecond") g_cfg.fleshHealPerSecond = (float)std::atof(val.c_str());
        else if (key == "StunHealPerSecond") g_cfg.stunHealPerSecond = (float)std::atof(val.c_str());
        else if (key == "HungerDrainPerSecond") g_cfg.hungerDrainPerSecond = (float)std::atof(val.c_str());
        else if (key == "MinHungerToRegen") g_cfg.minHungerToRegen = (float)std::atof(val.c_str());
        else if (key == "HealUnhealableWounds") g_cfg.healUnhealable = ParseBool(val);
        else if (key == "LimbRegrowPerSecond") g_cfg.limbRegrowPerSecond = (float)std::atof(val.c_str());
        else if (key == "LimbRestoreFleshPercent") g_cfg.limbRestoreFleshPct = (float)std::atof(val.c_str());
        else if (key == "Past100XpMult") g_cfg.past100XpMult = (float)std::atof(val.c_str());
        else if (key == "DebugLog") g_cfg.debugLog = ParseBool(val);
    }

    char msg[256];
    std::snprintf(msg, sizeof(msg),
        "ToughnessFeast: config H=%.0f S=%.0f V=%.0f cap=%.0f limb=%.1f/s",
        g_cfg.foodRegenStartHuman, g_cfg.foodRegenStartShek,
        g_cfg.foodRegenStartHiver, g_cfg.combatCapToughness,
        g_cfg.limbRegrowPerSecond);
    DebugLog(msg);
}

// ---------------------------------------------------------------------------
// Race helpers
// ---------------------------------------------------------------------------

static std::string ToLowerCopy(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') s[i] = (char)(c - 'A' + 'a');
    }
    return s;
}

enum RaceKind { RACE_HUMAN, RACE_SHEK, RACE_HIVER, RACE_OTHER };

static RaceKind ClassifyRace(Character* me)
{
    if (!me) return RACE_OTHER;
    RaceData* race = me->getRace();
    if (!race || !race->data) return RACE_OTHER;

    std::string blob = ToLowerCopy(race->data->name);
    blob += " ";
    blob += ToLowerCopy(race->data->stringID);

    if (blob.find("hive") != std::string::npos || blob.find("hiver") != std::string::npos)
        return RACE_HIVER;
    if (blob.find("shek") != std::string::npos)
        return RACE_SHEK;
    if (blob.find("greenlander") != std::string::npos
        || blob.find("scorchlander") != std::string::npos
        || blob.find("human") != std::string::npos)
        return RACE_HUMAN;
    return RACE_OTHER;
}

static float FoodRegenStartFor(Character* me)
{
    switch (ClassifyRace(me))
    {
    case RACE_HIVER: return g_cfg.foodRegenStartHiver;
    case RACE_SHEK:  return g_cfg.foodRegenStartShek;
    case RACE_HUMAN: return g_cfg.foodRegenStartHuman;
    default:         return g_cfg.foodRegenStartOther;
    }
}

static float FoodRegenScaleFor(Character* me)
{
    if (ClassifyRace(me) == RACE_HIVER)
        return g_cfg.foodRegenScaleHiver;
    return g_cfg.foodRegenScalePerPoint;
}

static float RegenPower(Character* me, float toughness)
{
    float start = FoodRegenStartFor(me);
    float scale = FoodRegenScaleFor(me);
    float excess = toughness - start;
    if (excess <= 0.f) return 0.f;
    return excess * scale;
}

// ---------------------------------------------------------------------------
// Soft-cap combat toughness
// ---------------------------------------------------------------------------

struct ToughnessClamp
{
    CharStats* stats;
    float saved;
    bool active;
    ToughnessClamp(CharStats* s, float cap)
        : stats(s), saved(0.f), active(false)
    {
        if (!s) return;
        saved = s->_toughness;
        if (saved > cap)
        {
            s->_toughness = cap;
            active = true;
        }
    }
    ~ToughnessClamp()
    {
        if (active && stats)
            stats->_toughness = saved;
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
// Toughness XP past 100
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
    float after = self->_toughness;
    float gained = after - before;
    if (before >= 99.5f && gained < amount * 0.02f)
    {
        float over = (before > 100.f) ? (before - 100.f) : 0.f;
        float mult = g_cfg.past100XpMult / (1.f + over * 0.02f);
        float forced = amount * mult;
        if (forced > 0.f)
            self->_toughness = before + forced;
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
    float after = self->_toughness;
    if (before >= 99.5f && after <= before + 0.0001f)
    {
        float over = (before > 100.f) ? (before - 100.f) : 0.f;
        float mult = g_cfg.past100XpMult / (1.f + over * 0.02f);
        self->_toughness = before + 0.002f * mult;
    }
}

// ---------------------------------------------------------------------------
// Food regen + limb regrow (post medicalUpdate)
// ---------------------------------------------------------------------------

static void ApplyFoodRegen(MedicalSystem* med, float frameTime)
{
    if (!med || frameTime <= 0.f) return;
    if (med->dead) return;

    CharStats* stats = med->stats;
    Character* me = med->me;
    if (!stats || !me) return;

    // Non-eaters (skeletons): skip
    if (!me->amSomeoneWhoNeedsToEatToLive())
        return;

    float tough = stats->_toughness;
    float power = RegenPower(me, tough);
    if (power <= 0.f) return;
    if (power > 4.f) power = 4.f;

    // hunger is 0..1-ish; do NOT call isFed() (import may be missing)
    if (med->hunger < g_cfg.minHungerToRegen)
        return;

    float fleshBudget = g_cfg.fleshHealPerSecond * power * frameTime;
    float stunBudget = g_cfg.stunHealPerSecond * power * frameTime;
    float limbBudget = g_cfg.limbRegrowPerSecond * power * frameTime;
    float hungerCost = 0.f;
    bool anyHeal = false;

    // --- Stumps / crushed (even if isDead) ---
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

            bool missing = (ls == LIMB_STUMP || ls == LIMB_CRUSHED);
            float maxHp = part->maxHealth();
            if (maxHp <= 1.f) maxHp = part->_maxHealth;
            if (maxHp <= 1.f) maxHp = 100.f;

            if (!missing && part->flesh > maxHp * 0.05f)
                continue;

            if (part->flesh < maxHp && limbBudget > 0.f)
            {
                if (part->flesh < 0.f)
                    part->flesh = 0.f;
                float need = maxHp - part->flesh;
                float take = (need < limbBudget) ? need : limbBudget;
                if (ls == LIMB_CRUSHED)
                    take *= 0.75f;
                part->flesh += take;
                limbBudget -= take;
                hungerCost += take * 0.8f;
                anyHeal = true;
            }

            float restoreAt = maxHp * g_cfg.limbRestoreFleshPct;
            if (restoreAt < 1.f) restoreAt = maxHp * 0.85f;

            if (missing && part->flesh >= restoreAt && med->robotLimbs)
            {
                RobotLimbs::Limb limbEnum = part->getRobotLimbEnum();
                if (limbEnum != RobotLimbs::NULL_LIMB)
                {
                    med->robotLimbs->setLimb(limbEnum, LIMB_ORIGINAL, nullptr);
                    if (part->flesh < maxHp * 0.9f)
                        part->flesh = maxHp * 0.9f;
                    part->fleshStun = 0.f;
                    part->updateDerivedHealths();
                    anyHeal = true;
                    DebugLog("ToughnessFeast: restored a limb (stump/crush -> original)");
                }
            }
            else
            {
                part->updateDerivedHealths();
            }
        }
    }

    // --- Normal flesh / overdamage ---
    int count = med->getPartCount();
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < count; ++i)
        {
            MedicalSystem::HealthPartStatus* part = med->getPart((unsigned long long)i);
            if (!part) continue;
            if (part->isRobotic()) continue;

            LimbState ls = part->getRobotLimbState();
            bool missing = (ls == LIMB_STUMP || ls == LIMB_CRUSHED);
            if (part->isDead() && !missing) continue;
            if (missing) continue;

            float maxHp = part->maxHealth();
            if (maxHp <= 0.f) continue;

            bool isOverdamage = part->flesh < 0.f;
            if (pass == 0 && !isOverdamage) continue;
            if (pass == 1 && isOverdamage) continue;
            if (isOverdamage && !g_cfg.healUnhealable) continue;

            if (part->fleshStun > 0.f && stunBudget > 0.f)
            {
                float take = (part->fleshStun < stunBudget) ? part->fleshStun : stunBudget;
                part->fleshStun -= take;
                stunBudget -= take;
                hungerCost += take * 0.15f;
                anyHeal = true;
            }

            if (part->flesh < maxHp && fleshBudget > 0.f)
            {
                float need = maxHp - part->flesh;
                float rate = isOverdamage ? 0.45f : 1.f;
                float room = fleshBudget * rate;
                float take = (need < room) ? need : room;
                part->flesh += take;
                fleshBudget -= take / rate;
                hungerCost += take * (isOverdamage ? 0.55f : 0.25f);
                anyHeal = true;
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
                "ToughnessFeast: regen t=%.1f p=%.2f hunger=%.3f",
                tough, power, med->hunger);
            DebugLog(msg);
        }
    }
}

static void (*medicalUpdate_orig)(MedicalSystem*, float) = nullptr;
static void medicalUpdate_hook(MedicalSystem* self, float frameTime)
{
    medicalUpdate_orig(self, frameTime);
    ApplyFoodRegen(self, frameTime);
}

// ---------------------------------------------------------------------------
// Plugin entry
// ---------------------------------------------------------------------------

static void ResolvePluginDir()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    (void)g_pluginDir;
#else
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&ResolvePluginDir,
            &hm))
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(hm, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
        {
            for (int i = (int)n - 1; i >= 0; --i)
            {
                if (path[i] == '\\' || path[i] == '/')
                {
                    path[i] = 0;
                    break;
                }
            }
            strncpy_s(g_pluginDir, path, _TRUNCATE);
        }
    }
#endif
}

#if defined(_MSC_VER)
#define TF_EXPORT __declspec(dllexport)
#else
#define TF_EXPORT __attribute__((visibility("default")))
#endif

extern "C" TF_EXPORT void startPlugin()
{
    ResolvePluginDir();
    LoadConfig();

    DebugLog("ToughnessFeast: installing hooks...");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::calculateToughnessDamageResistanceMult),
            (void*)calculateToughnessDamageResistanceMult_hook,
            (void**)&calculateToughnessDamageResistanceMult_orig))
        ErrorLog("ToughnessFeast: failed DR hook");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::calculateToughnessWoundDegenerationRate),
            (void*)calculateToughnessWoundDegenerationRate_hook,
            (void**)&calculateToughnessWoundDegenerationRate_orig))
        ErrorLog("ToughnessFeast: failed wound-degen hook");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::xpStat_eventBased),
            (void*)xpStat_eventBased_hook,
            (void**)&xpStat_eventBased_orig))
        ErrorLog("ToughnessFeast: failed xp event hook");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::xpStat_timeBased),
            (void*)xpStat_timeBased_hook,
            (void**)&xpStat_timeBased_orig))
        ErrorLog("ToughnessFeast: failed xp time hook");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&MedicalSystem::medicalUpdate),
            (void*)medicalUpdate_hook,
            (void**)&medicalUpdate_orig))
        ErrorLog("ToughnessFeast: failed medicalUpdate hook");

    DebugLog("ToughnessFeast: ready (no GUI imports — should load cleanly)");
}
