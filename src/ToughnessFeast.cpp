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
#include <kenshi/gui/DatapanelGUI.h>
#include <kenshi/gui/DataPanelLine.h>
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
    bool showGuiTooltips;
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
    false,  // debugLog
    true    // showGuiTooltips
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
        else if (key == "ShowGuiTooltips") g_cfg.showGuiTooltips = ParseBool(val);
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

// ---------------------------------------------------------------------------
// GUI / tooltips — show what ToughnessFeast is doing on character panels
// ---------------------------------------------------------------------------

static const char* RaceKindName(RaceKind k)
{
    switch (k)
    {
    case RACE_HIVER: return "Hiver";
    case RACE_SHEK:  return "Shek";
    case RACE_HUMAN: return "Human";
    default:         return "Other";
    }
}

static void DescribeLimbProgress(MedicalSystem* med, char* out, size_t outsz)
{
    if (!out || outsz == 0) return;
    out[0] = 0;
    if (!med) { std::snprintf(out, outsz, "none"); return; }

    MedicalSystem::HealthPartStatus* limbs[4] = {
        med->leftArm, med->rightArm, med->leftLeg, med->rightLeg
    };
    const char* names[4] = { "L-Arm", "R-Arm", "L-Leg", "R-Leg" };

    char buf[256];
    buf[0] = 0;
    int missing = 0;
    for (int i = 0; i < 4; ++i)
    {
        MedicalSystem::HealthPartStatus* p = limbs[i];
        if (!p) continue;
        LimbState ls = p->getRobotLimbState();
        if (ls != LIMB_STUMP && ls != LIMB_CRUSHED) continue;
        ++missing;
        float maxHp = p->maxHealth();
        if (maxHp <= 1.f) maxHp = p->_maxHealth;
        if (maxHp <= 1.f) maxHp = 100.f;
        float pct = (p->flesh / maxHp) * 100.f;
        if (pct < 0.f) pct = 0.f;
        if (pct > 100.f) pct = 100.f;
        char piece[64];
        std::snprintf(piece, sizeof(piece), "%s%s %s %.0f%%",
            buf[0] ? ", " : "",
            names[i],
            ls == LIMB_STUMP ? "stump" : "crush",
            pct);
        // append carefully
        size_t bl = std::strlen(buf);
        size_t pl = std::strlen(piece);
        if (bl + pl + 1 < sizeof(buf))
            std::memcpy(buf + bl, piece, pl + 1);
    }
    if (missing == 0)
        std::snprintf(out, outsz, "no missing limbs");
    else
        std::snprintf(out, outsz, "%s", buf);
}

// Build multi-line tooltip text for toughness / medical
static std::string BuildTfTooltipText(Character* me, CharStats* stats, MedicalSystem* med)
{
    if (!stats) return "ToughnessFeast: no stats";

    RaceKind rk = ClassifyRace(me);
    float tough = stats->_toughness;
    float start = FoodRegenStartFor(me);
    float power = RegenPower(me, tough);
    if (power > 4.f) power = 4.f;

    bool fedOk = true;
    float hunger = 0.f;
    if (med)
    {
        hunger = med->hunger;
        fedOk = (hunger >= g_cfg.minHungerToRegen) || med->isFed();
    }

    char limb[256];
    DescribeLimbProgress(med, limb, sizeof(limb));

    const char* combatNote = (tough > g_cfg.combatCapToughness)
        ? "Combat DR/wound soft-capped at 100"
        : "Combat bonuses still scale with toughness";

    char body[768];
    std::snprintf(body, sizeof(body),
        "ToughnessFeast\n"
        "Race: %s\n"
        "Toughness: %.1f (food regen unlock at %.0f)\n"
        "Food regen power: %.0f%% %s\n"
        "Hunger: %.0f%%  %s\n"
        "%s\n"
        "Limbs: %s\n"
        "Heal rates @ power 1.0: flesh %.1f/s, limbs %.1f/s",
        RaceKindName(rk),
        tough, start,
        power * 100.f,
        power <= 0.f ? "(need more toughness)" : (fedOk ? "(active if wounded)" : "(paused — eat)"),
        hunger * 100.f,
        fedOk ? "fed enough" : "too hungry to regen",
        combatNote,
        limb,
        g_cfg.fleshHealPerSecond,
        g_cfg.limbRegrowPerSecond);
    return std::string(body);
}

static void TfSetLine(DatapanelGUI* panel, int category,
    const char* key, const char* label, const char* value, const char* tip)
{
    if (!panel) return;
    DataPanelLine* line = panel->setLine(
        std::string(key),
        std::string(label),
        std::string(value),
        category,
        false,
        true);
    if (line && tip && tip[0])
        line->setToolTip(std::string(tip));
}

static void AppendTfGui(DatapanelGUI* panel, int category,
    Character* me, CharStats* stats, MedicalSystem* med)
{
    if (!g_cfg.showGuiTooltips) return;
    if (!panel || !stats) return;

    RaceKind rk = ClassifyRace(me);
    float tough = stats->_toughness;
    float start = FoodRegenStartFor(me);
    float power = RegenPower(me, tough);
    if (power > 4.f) power = 4.f;
    bool fedOk = true;
    float hunger = 0.f;
    if (med)
    {
        hunger = med->hunger;
        fedOk = (hunger >= g_cfg.minHungerToRegen) || med->isFed();
    }

    char limb[256];
    DescribeLimbProgress(med, limb, sizeof(limb));
    std::string tip = BuildTfTooltipText(me, stats, med);

    char vRace[64];
    std::snprintf(vRace, sizeof(vRace), "%s  unlock@%.0f", RaceKindName(rk), start);

    char vPower[80];
    if (power <= 0.f)
        std::snprintf(vPower, sizeof(vPower), "locked (need %.0f toughness)", start);
    else if (!fedOk)
        std::snprintf(vPower, sizeof(vPower), "%.0f%% paused (eat)", power * 100.f);
    else
        std::snprintf(vPower, sizeof(vPower), "%.0f%% active", power * 100.f);

    char vCombat[64];
    if (tough > g_cfg.combatCapToughness)
        std::snprintf(vCombat, sizeof(vCombat), "soft-cap %.0f (excess -> regen)", g_cfg.combatCapToughness);
    else
        std::snprintf(vCombat, sizeof(vCombat), "normal (cap %.0f)", g_cfg.combatCapToughness);

    char vLimb[280];
    std::snprintf(vLimb, sizeof(vLimb), "%s", limb);

    // Visual separator + status rows (hover for full tooltip)
    TfSetLine(panel, category, "TF_Head", "ToughnessFeast", "food / limb regen", tip.c_str());
    TfSetLine(panel, category, "TF_Race", "TF race unlock", vRace, tip.c_str());
    TfSetLine(panel, category, "TF_Power", "TF food regen", vPower, tip.c_str());
    TfSetLine(panel, category, "TF_Combat", "TF combat", vCombat, tip.c_str());
    TfSetLine(panel, category, "TF_Limbs", "TF limbs", vLimb, tip.c_str());
}

static void (*getMedicalGUIData_orig)(MedicalSystem*, DatapanelGUI*) = nullptr;
static void getMedicalGUIData_hook(MedicalSystem* self, DatapanelGUI* panel)
{
    getMedicalGUIData_orig(self, panel);
    if (!self || !panel) return;
    // Medical panel typically uses category 0
    AppendTfGui(panel, 0, self->me, self->stats, self);
}

static void (*charStatsGetGUIData_orig)(CharStats*, DatapanelGUI*, int) = nullptr;
static void charStatsGetGUIData_hook(CharStats* self, DatapanelGUI* panel, int category)
{
    charStatsGetGUIData_orig(self, panel, category);
    if (!self || !panel) return;
    AppendTfGui(panel, category, self->me, self, self->medical);

    // Also try to attach tooltip to the vanilla Toughness row if present
    if (g_cfg.showGuiTooltips)
    {
        std::string tip = BuildTfTooltipText(self->me, self, self->medical);
        const char* keys[] = {
            "Toughness", "toughness", "STAT_TOUGHNESS", "stat_toughness", nullptr
        };
        for (int i = 0; keys[i]; ++i)
        {
            DataPanelLine* line = panel->getLine(std::string(keys[i]), category);
            if (line)
                line->setToolTip(tip);
        }
    }
}

static void (*getGUIDataForMainInfo_orig)(CharStats*, DatapanelGUI*, int, bool) = nullptr;
static void getGUIDataForMainInfo_hook(CharStats* self, DatapanelGUI* panel, int category, bool combatMode)
{
    getGUIDataForMainInfo_orig(self, panel, category, combatMode);
    if (!self || !panel) return;
    // Compact one-liner on main info
    if (!g_cfg.showGuiTooltips) return;
    float power = RegenPower(self->me, self->_toughness);
    if (power > 4.f) power = 4.f;
    char v[96];
    std::snprintf(v, sizeof(v), "regen %.0f%% | cap %.0f",
        power * 100.f, g_cfg.combatCapToughness);
    std::string tip = BuildTfTooltipText(self->me, self, self->medical);
    TfSetLine(panel, category, "TF_Main", "ToughnessFeast", v, tip.c_str());
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

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&MedicalSystem::getMedicalGUIData),
            (void*)getMedicalGUIData_hook,
            (void**)&getMedicalGUIData_orig))
    {
        ErrorLog("ToughnessFeast: failed to hook getMedicalGUIData");
    }

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::getGUIData),
            (void*)charStatsGetGUIData_hook,
            (void**)&charStatsGetGUIData_orig))
    {
        ErrorLog("ToughnessFeast: failed to hook CharStats::getGUIData");
    }

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CharStats::getGUIDataForMainInfo),
            (void*)getGUIDataForMainInfo_hook,
            (void**)&getGUIDataForMainInfo_orig))
    {
        ErrorLog("ToughnessFeast: failed to hook getGUIDataForMainInfo");
    }

    DebugLog("ToughnessFeast: ready — race regen + GUI tooltips; combat soft-cap 100");
}
