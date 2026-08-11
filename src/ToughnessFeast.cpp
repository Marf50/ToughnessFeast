// ToughnessFeast — RE_Kenshi / KenshiLib plugin
// Soft-caps vanilla toughness combat bonuses at 100, allows toughness past 100,
// and converts excess toughness into food-powered flesh / limb regeneration.
//
// Real game build: Windows MSVC + KenshiLib (see OPEN_IN_CLION.md).
// Linux CLion: TOUGHNESSFEAST_LINUX_IDE uses bundled stubs so indexing works.

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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// Portable strncpy_s for non-MSVC real builds (MSVC already has it)
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
    // Race-based food-regen unlock thresholds (toughness)
    float foodRegenStartHuman;   // default 75
    float foodRegenStartShek;    // default 50
    float foodRegenStartHiver;   // default 0 — slight regen from the start
    float foodRegenStartOther;   // default 75 (scorchlanders, etc.)
    float foodRegenScalePerPoint;
    float foodRegenScaleHiver;   // gentler curve so early hiver regen stays slight
    float fleshHealPerSecond;
    float stunHealPerSecond;
    float hungerDrainPerSecond;
    float minHungerToRegen;
    bool healUnhealable;
    float limbRegrowPerSecond;
    float limbRestoreFleshPct; // stump/crush restores to LIMB_ORIGINAL at this flesh%
    float past100XpMult;
    bool debugLog;
};

static Config g_cfg = {
    100.f,  // combatCapToughness
    75.f,   // foodRegenStartHuman
    50.f,   // foodRegenStartShek
    0.f,    // foodRegenStartHiver
    75.f,   // foodRegenStartOther
    0.04f,  // foodRegenScalePerPoint
    0.012f, // foodRegenScaleHiver
    2.5f,   // fleshHealPerSecond
    1.5f,   // stunHealPerSecond
    0.012f, // hungerDrainPerSecond
    0.10f,  // minHungerToRegen (slightly more permissive)
    true,   // healUnhealable
    3.0f,   // limbRegrowPerSecond — noticeable stump flesh rebuild
    0.85f,  // limbRestoreFleshPct
    0.18f,  // past100XpMult
    false   // debugLog
};

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

// Plugin DLL directory (resolved in startPlugin)
static char g_pluginDir[MAX_PATH] = { 0 };

static void LoadConfig()
{
    char path[MAX_PATH + 32];
    if (g_pluginDir[0])
        std::snprintf(path, sizeof(path), "%s/config.ini", g_pluginDir);
    else
        std::snprintf(path, sizeof(path), "config.ini");

    // Also try Windows-style path when built as DLL next to config
    std::ifstream in(path);
    if (!in && g_pluginDir[0])
    {
        std::snprintf(path, sizeof(path), "%s\\config.ini", g_pluginDir);
        in.open(path);
    }
    if (!in)
    {
        DebugLog("ToughnessFeast: no config.ini found, using defaults");
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
        // Legacy single-threshold key applies to human/other baseline
        else if (key == "FoodRegenStartToughness") {
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

    char msg[320];
    std::snprintf(msg, sizeof(msg),
        "ToughnessFeast: config (cap=%.0f starts H=%.0f S=%.0f V=%.0f O=%.0f)",
        g_cfg.combatCapToughness,
        g_cfg.foodRegenStartHuman, g_cfg.foodRegenStartShek,
        g_cfg.foodRegenStartHiver, g_cfg.foodRegenStartOther);
    DebugLog(msg);
}

// ---------------------------------------------------------------------------
// Helpers — race detection + food regen power
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

// Classify by race name / stringID (Hive*, Shek*, else human/other)
enum RaceKind { RACE_HUMAN, RACE_SHEK, RACE_HIVER, RACE_OTHER };

static RaceKind ClassifyRace(Character* me)
{
    if (!me) return RACE_OTHER;
    RaceData* race = me->getRace();
    if (!race || !race->data) return RACE_OTHER;

    std::string blob = ToLowerCopy(race->data->name);
    blob += " ";
    blob += ToLowerCopy(race->data->stringID);

    // Hivers: "Hive Worker", "Hive Soldier", "Southern Hive", "Hiver", etc.
    if (blob.find("hive") != std::string::npos || blob.find("hiver") != std::string::npos)
        return RACE_HIVER;
    // Shek
    if (blob.find("shek") != std::string::npos)
        return RACE_SHEK;
    // Greenlanders / Scorchlanders / default playable humans
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

// Temporarily clamp _toughness for vanilla combat formula functions
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

// ---------------------------------------------------------------------------
// Hooks: soft-cap DR + wound degen at CombatCapToughness
// ---------------------------------------------------------------------------

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
// Hooks: allow toughness XP past 100
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

    // Soft-cap stalled near/above 100 — residual growth at reduced rate
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
// Food-powered regeneration (post medicalUpdate)
// ---------------------------------------------------------------------------

static void ApplyFoodRegen(MedicalSystem* med, float frameTime)
{
    if (!med || frameTime <= 0.f) return;
    if (med->dead) return;

    CharStats* stats = med->stats;
    Character* me = med->me;
    if (!stats || !me) return;

    // Skeletons / non-eaters: no metabolic flesh regen
    if (!me->amSomeoneWhoNeedsToEatToLive())
        return;

    float tough = stats->_toughness;
    float power = RegenPower(me, tough);
    if (power <= 0.f) return;
    if (power > 4.f) power = 4.f;

    // Prefer medical hunger; fall back to fed flag if hunger is weird
    if (med->hunger < g_cfg.minHungerToRegen && !med->isFed())
        return;

    float fleshBudget = g_cfg.fleshHealPerSecond * power * frameTime;
    float stunBudget = g_cfg.stunHealPerSecond * power * frameTime;
    float limbBudget = g_cfg.limbRegrowPerSecond * power * frameTime;
    float hungerCost = 0.f;
    bool anyHeal = false;

    // --- 1) Stump / crushed limb regrowth (MUST run even if part->isDead()) ---
    // Previous builds skipped isDead() parts, so lost limbs never healed.
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
            // Prosthetic — leave alone
            if (ls == LIMB_REPLACED) continue;

            bool missing = (ls == LIMB_STUMP || ls == LIMB_CRUSHED);
            // Also treat near-zero flesh arm/leg as ruined organic
            float maxHp = part->maxHealth();
            if (maxHp <= 1.f) maxHp = part->_maxHealth;
            if (maxHp <= 1.f) maxHp = 100.f;

            if (!missing && part->flesh > maxHp * 0.05f)
                continue; // healthy enough for normal flesh pass

            // Rebuild stump flesh toward max
            if (part->flesh < maxHp && limbBudget > 0.f)
            {
                // Start crushed/stump HP at least at 0 so we can climb
                if (part->flesh < 0.f)
                    part->flesh = 0.f;

                float need = maxHp - part->flesh;
                float take = (need < limbBudget) ? need : limbBudget;
                // Crushed regrows a bit slower than stump tissue
                if (ls == LIMB_CRUSHED)
                    take *= 0.75f;
                part->flesh += take;
                limbBudget -= take;
                hungerCost += take * 0.8f;
                anyHeal = true;
            }

            // Restore functional limb once flesh is high enough
            float restoreAt = maxHp * g_cfg.limbRestoreFleshPct;
            if (restoreAt < 1.f) restoreAt = maxHp * 0.85f;

            if (missing && part->flesh >= restoreAt && med->robotLimbs)
            {
                RobotLimbs::Limb limbEnum = part->getRobotLimbEnum();
                if (limbEnum != RobotLimbs::NULL_LIMB)
                {
                    med->robotLimbs->setLimb(limbEnum, LIMB_ORIGINAL, nullptr);
                    // Clear "dead limb" residual damage so the part is usable
                    if (part->flesh < maxHp * 0.9f)
                        part->flesh = maxHp * 0.9f;
                    part->fleshStun = 0.f;
                    part->updateDerivedHealths();
                    anyHeal = true;

                    char msg[192];
                    std::snprintf(msg, sizeof(msg),
                        "ToughnessFeast: restored limb (state was %d) flesh=%.1f/%.1f t=%.1f p=%.2f",
                        (int)ls, part->flesh, maxHp, tough, power);
                    DebugLog(msg);
                }
            }
            else
            {
                part->updateDerivedHealths();
            }
        }
    }

    // --- 2) Normal flesh / overdamage / stun heal (skip truly dead non-limb parts) ---
    int count = med->getPartCount();
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < count; ++i)
        {
            MedicalSystem::HealthPartStatus* part = med->getPart((unsigned long long)i);
            if (!part) continue;
            if (part->isRobotic()) continue;

            LimbState ls = part->getRobotLimbState();
            bool isLimbPart =
                part->whatAmI == MedicalSystem::HealthPartStatus::PART_ARM
                || part->whatAmI == MedicalSystem::HealthPartStatus::PART_LEG;
            bool missing = (ls == LIMB_STUMP || ls == LIMB_CRUSHED);

            // Dead torso/head = ignore. Dead stump already handled above.
            if (part->isDead() && !missing) continue;
            if (missing) continue; // already handled in limb pass

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
            (void)isLimbPart;
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
            char msg[192];
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
    {
        ErrorLog("ToughnessFeast: failed to hook calculateToughnessDamageResistanceMult");
    }

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::calculateToughnessWoundDegenerationRate),
            (void*)calculateToughnessWoundDegenerationRate_hook,
            (void**)&calculateToughnessWoundDegenerationRate_orig))
    {
        ErrorLog("ToughnessFeast: failed to hook calculateToughnessWoundDegenerationRate");
    }

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::xpStat_eventBased),
            (void*)xpStat_eventBased_hook,
            (void**)&xpStat_eventBased_orig))
    {
        ErrorLog("ToughnessFeast: failed to hook xpStat_eventBased");
    }

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::xpStat_timeBased),
            (void*)xpStat_timeBased_hook,
            (void**)&xpStat_timeBased_orig))
    {
        ErrorLog("ToughnessFeast: failed to hook xpStat_timeBased");
    }

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&MedicalSystem::medicalUpdate),
            (void*)medicalUpdate_hook,
            (void**)&medicalUpdate_orig))
    {
        ErrorLog("ToughnessFeast: failed to hook MedicalSystem::medicalUpdate");
    }

    DebugLog("ToughnessFeast: ready — race regen: Hiver@0 Shek@50 Human@75, combat soft-cap 100");
}
