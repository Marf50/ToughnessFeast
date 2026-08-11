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
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/util/hand.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Widget.h>
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
    int showStatusHud;              // MyGUI floating status panel
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
    1        // showStatusHud
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
        else if (std::strcmp(key, "ShowStatusHud") == 0) g_cfg.showStatusHud = ParseBoolC(val);
        else if (std::strcmp(key, "EnableMedicalHooks") == 0) g_cfg.enableMedicalHooks = ParseBoolC(val);
        else if (std::strcmp(key, "EnableAnatomyPass") == 0) { /* deprecated */ }
        else if (std::strcmp(key, "Past100XpMult") == 0) g_cfg.past100XpMult = (float)std::atof(val);
        else if (std::strcmp(key, "DebugLog") == 0) g_cfg.debugLog = ParseBoolC(val);
        else if (std::strcmp(key, "UseRaceHeuristics") == 0) g_cfg.useRaceHeuristics = ParseBoolC(val);
    }
    std::fclose(f);
}


// ---------------------------------------------------------------------------
// Status HUD (MyGUI) — own widgets, no game tooltip std::string ABI
// Shows limb stage progress, regen power, hunger, toughness soft-cap info
// ---------------------------------------------------------------------------

static char g_statusText[2048] = "ToughnessFeast\n(waiting for character...)";
static CharStats* g_lastStats = nullptr;

#if !defined(TOUGHNESSFEAST_LINUX_IDE)
static MyGUI::Window* g_hudWindow = nullptr;
static MyGUI::TextBox* g_hudText = nullptr;
static int g_hudCreateAttempts = 0;
#endif

static const char* LimbSlotName(int i)
{
    static const char* n[4] = { "L.Arm", "R.Arm", "L.Leg", "R.Leg" };
    return (i >= 0 && i < 4) ? n[i] : "?";
}

// Build multi-line status for a character into g_statusText
static void BuildStatusText(CharStats* stats)
{
    if (!stats)
    {
        std::snprintf(g_statusText, sizeof(g_statusText),
            "ToughnessFeast\n(no character)");
        return;
    }

    float tough = stats->_toughness;
    float power = 0.f;
    float start = g_cfg.foodRegenStart;
    // local copies of helpers exist later — recompute simply here
    {
        Character* me = stats->me;
        RaceData* race = me ? me->getRace() : nullptr;
        if (g_cfg.useRaceHeuristics && race && !race->robot)
        {
            if (race->gigantic) start = g_cfg.foodRegenStartShek;
            else if (race->hungerRate > 1.15f) start = g_cfg.foodRegenStartHiver;
        }
        float excess = tough - start;
        if (excess > 0.f)
        {
            float scale = g_cfg.foodRegenScalePerPoint;
            if (g_cfg.useRaceHeuristics && race && !race->robot && !race->gigantic && race->hungerRate > 1.15f)
                scale = g_cfg.foodRegenScaleHiver;
            power = excess * scale;
            if (power > 3.5f) power = 3.5f;
        }
    }

    MedicalSystem* med = stats->medical;
    float hunger = med ? med->hunger : -1.f;
    int fed = (hunger >= g_cfg.minHungerToRegen) ? 1 : 0;

    char lines[1800];
    lines[0] = 0;
    int off = 0;
    #define APP(...) do { \
        int _n = std::snprintf(lines + off, sizeof(lines) - (size_t)off, __VA_ARGS__); \
        if (_n > 0) off += _n; \
        if (off < 0 || off >= (int)sizeof(lines)) off = (int)sizeof(lines) - 1; \
    } while (0)

    APP("=== Toughness Feast ===\n");
    APP("Toughness: %.1f  (combat cap %.0f)\n", tough, g_cfg.combatCapToughness);
    APP("Food regen unlock: %.0f   power: %.2f\n", start, power);
    if (hunger >= 0.f)
        APP("Hunger: %.0f%%  %s\n", hunger * 100.f, fed ? "FED" : "TOO HUNGRY");
    else
        APP("Hunger: ?\n");

    if (power <= 0.f)
        APP("Status: below unlock — no food regen yet\n");
    else if (!fed)
        APP("Status: need food for regen/regrow\n");
    else
        APP("Status: regenerating\n");

    APP("-- Limbs --\n");

    if (med && med->robotLimbs)
    {
        static const RobotLimbs::Limb kLimbs[4] = {
            RobotLimbs::LEFT_ARM, RobotLimbs::RIGHT_ARM,
            RobotLimbs::LEFT_LEG, RobotLimbs::RIGHT_LEG
        };
        for (int i = 0; i < 4; ++i)
        {
            MedicalSystem::HealthPartStatus* part = med->getPart(kLimbs[i]);
            if (!part)
            {
                APP("%s: (none)\n", LimbSlotName(i));
                continue;
            }
            if (part->isRobotic())
            {
                APP("%s: prosthetic\n", LimbSlotName(i));
                continue;
            }

            LimbState st = part->getRobotLimbState();
            float maxHp = part->maxHealth();
            if (maxHp < 1.f) maxHp = part->_maxHealth;
            if (maxHp < 1.f) maxHp = 100.f;
            float flesh = part->flesh;
            if (flesh < 0.f) flesh = 0.f;
            float pct = flesh / maxHp;
            if (pct > 1.5f) pct = 1.5f;

            if (st == LIMB_REPLACED)
            {
                APP("%s: prosthetic/replaced\n", LimbSlotName(i));
            }
            else if (st == LIMB_STUMP || st == LIMB_CRUSHED)
            {
                float prog = 0.f;
                if (g_cfg.limbBudThreshold > 0.01f)
                    prog = (pct / g_cfg.limbBudThreshold) * 100.f;
                if (prog > 100.f) prog = 100.f;
                if (prog < 0.f) prog = 0.f;
                APP("%s: %s  bud %.0f%% -> restore@%.0f%%\n",
                    LimbSlotName(i),
                    st == LIMB_CRUSHED ? "CRUSHED" : "STUMP",
                    prog, g_cfg.limbBudThreshold * 100.f);
                // simple bar
                int bars = (int)(prog / 10.f);
                if (bars < 0) bars = 0;
                if (bars > 10) bars = 10;
                char bar[12];
                for (int b = 0; b < 10; ++b) bar[b] = (b < bars) ? '#' : '.';
                bar[10] = 0;
                APP("        [%s] next: weak limb\n", bar);
            }
            else // ORIGINAL
            {
                if (pct < g_cfg.limbStrongPct)
                {
                    float lo = g_cfg.limbRestoredStartPct;
                    float hi = g_cfg.limbStrongPct;
                    float prog = 0.f;
                    if (hi > lo)
                        prog = ((pct - lo) / (hi - lo)) * 100.f;
                    if (prog < 0.f) prog = 0.f;
                    if (prog > 100.f) prog = 100.f;
                    APP("%s: WEAK  HP %.0f%%  -> strong@%.0f%%\n",
                        LimbSlotName(i), pct * 100.f, hi * 100.f);
                    int bars = (int)(prog / 10.f);
                    if (bars > 10) bars = 10;
                    char bar[12];
                    for (int b = 0; b < 10; ++b) bar[b] = (b < bars) ? '#' : '.';
                    bar[10] = 0;
                    APP("        [%s] fight penalty active\n", bar);
                }
                else
                {
                    APP("%s: OK  HP %.0f%%\n", LimbSlotName(i), pct * 100.f);
                }
            }
        }
    }
    else
    {
        APP("(limb data unavailable)\n");
    }

    APP("--------------\n");
    APP("DR/degen softcap @%.0f | XP past 100 on\n", g_cfg.combatCapToughness);

    #undef APP
    std::snprintf(g_statusText, sizeof(g_statusText), "%s", lines);
}

static CharStats* TryGetSelectedStats()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    return g_lastStats;
#else
    if (!ou || !ou->player) return g_lastStats;
    Character* c = ou->player->selectedCharacter.getCharacter();
    if (!c) return g_lastStats;
    if (c->stats) return c->stats;
    return c->getStats();
#endif
}

static void EnsureStatusHud()
{
#if defined(TOUGHNESSFEAST_LINUX_IDE)
    return;
#else
    if (!g_cfg.showStatusHud) return;
    if (g_hudWindow) return;
    if (g_hudCreateAttempts > 40) return; // stop spamming

    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (!gui)
    {
        ++g_hudCreateAttempts;
        return;
    }

    try
    {
        // Compact panel upper-right (real coords 0..1)
        g_hudWindow = gui->createWidgetReal<MyGUI::Window>(
            "Kenshi_WindowCX",
            0.72f, 0.06f, 0.27f, 0.40f,
            MyGUI::Align::Default,
            "Main",
            "ToughnessFeastHud");
        if (!g_hudWindow)
        {
            ++g_hudCreateAttempts;
            return;
        }
        g_hudWindow->setCaption("Toughness Feast");
        g_hudWindow->setMinSize(180, 160);

        MyGUI::Widget* client = g_hudWindow->getClientWidget();
        if (client)
        {
            g_hudText = client->createWidgetReal<MyGUI::TextBox>(
                "Kenshi_TextBox",
                0.02f, 0.02f, 0.96f, 0.96f,
                MyGUI::Align::Stretch,
                "TfHudText");
            if (!g_hudText)
            {
                // fallback skin
                g_hudText = client->createWidgetReal<MyGUI::TextBox>(
                    "TextBox",
                    0.02f, 0.02f, 0.96f, 0.96f,
                    MyGUI::Align::Stretch,
                    "TfHudText2");
            }
        }
        if (g_hudText)
        {
            g_hudText->setCaption(g_statusText);
            g_hudText->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        }
        DebugLog("ToughnessFeast: status HUD created");
    }
    catch (...)
    {
        g_hudWindow = nullptr;
        g_hudText = nullptr;
        ++g_hudCreateAttempts;
        ErrorLog("ToughnessFeast: status HUD create failed");
    }
#endif
}

static void RefreshStatusHud(CharStats* preferStats)
{
    if (!g_cfg.showStatusHud) return;

    CharStats* stats = preferStats ? preferStats : TryGetSelectedStats();
    if (!stats) stats = g_lastStats;
    if (stats) g_lastStats = stats;

    BuildStatusText(stats);
    EnsureStatusHud();

#if !defined(TOUGHNESSFEAST_LINUX_IDE)
    if (g_hudText)
    {
        g_hudText->setCaption(g_statusText);
    }
    else if (g_hudWindow)
    {
        // last resort: put text in window caption (short)
        g_hudWindow->setCaption("Toughness Feast (see log)");
    }
#endif
}

// ---------- gameplay (only used if EnableHooks=1) ----------
//
// Never hook MedicalSystem::medicalUpdate — that path crashed.
// Regen + staged limb restore run from CharStats hooks (stable).

static float FoodRegenStartFor(CharStats* stats)
{
    if (!g_cfg.useRaceHeuristics) return g_cfg.foodRegenStart;
    Character* me = stats ? stats->me : nullptr;
    RaceData* race = me ? me->getRace() : nullptr;
    if (!race || race->robot) return g_cfg.foodRegenStart;
    if (race->gigantic) return g_cfg.foodRegenStartShek;
    if (race->hungerRate > 1.15f) return g_cfg.foodRegenStartHiver;
    return g_cfg.foodRegenStart;
}

static float FoodRegenScaleFor(CharStats* stats)
{
    if (!g_cfg.useRaceHeuristics) return g_cfg.foodRegenScalePerPoint;
    Character* me = stats ? stats->me : nullptr;
    RaceData* race = me ? me->getRace() : nullptr;
    if (race && !race->robot && !race->gigantic && race->hungerRate > 1.15f)
        return g_cfg.foodRegenScaleHiver;
    return g_cfg.foodRegenScalePerPoint;
}

static float RegenPowerFromStats(CharStats* stats)
{
    if (!stats) return 0.f;
    float tough = stats->_toughness;
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
    float& worstSeverity) // 0 healthy .. 1 catastrophic
{
    if (!med || !stats || !g_cfg.enableLimbRestore) return;
    if (power <= 0.f) return;

    RobotLimbs* robots = med->robotLimbs;
    if (!robots) return;

    // Order matches RobotLimbs::Limb enum
    static const RobotLimbs::Limb kLimbs[4] = {
        RobotLimbs::LEFT_ARM,
        RobotLimbs::RIGHT_ARM,
        RobotLimbs::LEFT_LEG,
        RobotLimbs::RIGHT_LEG
    };

    float regrowBudget = g_cfg.limbRegrowPerSecond * power * frameTime;

    for (int i = 0; i < 4; ++i)
    {
        RobotLimbs::Limb limbEnum = kLimbs[i];
        MedicalSystem::HealthPartStatus* part = med->getPart(limbEnum);
        if (!part) continue;
        if (part->isRobotic()) continue; // never touch prosthetics

        LimbState state = part->getRobotLimbState();
        // Cross-check robotLimbs table when available
        LimbState tableState = robots->getState(limbEnum);
        if (tableState == LIMB_REPLACED) continue;
        if (state == LIMB_REPLACED) continue;

        float maxHp = part->maxHealth();
        if (maxHp < 1.f) maxHp = part->_maxHealth;
        if (maxHp < 1.f || maxHp > 10000.f) continue;

        float flesh = part->flesh;
        if (flesh != flesh) continue; // NaN
        if (flesh > maxHp * 3.f) flesh = maxHp;

        int missing = (state == LIMB_STUMP || state == LIMB_CRUSHED
                    || tableState == LIMB_STUMP || tableState == LIMB_CRUSHED) ? 1 : 0;

        if (missing)
        {
            // Stage 0–1: budding on stump/crush. Crushed is slower.
            float rate = (state == LIMB_CRUSHED || tableState == LIMB_CRUSHED) ? 0.55f : 1.f;
            // Severity: full penalty while missing
            if (worstSeverity < 0.95f) worstSeverity = 0.95f;

            if (regrowBudget > 0.f)
            {
                // Treat negative/overdamage flesh as 0 for budding
                if (flesh < 0.f) flesh = 0.f;
                float target = maxHp * g_cfg.limbBudThreshold;
                if (target < 5.f) target = maxHp * 0.38f;

                if (flesh < target)
                {
                    float need = target - flesh;
                    float take = need;
                    float room = regrowBudget * rate;
                    if (take > room) take = room;
                    if (take > 0.f)
                    {
                        part->flesh = flesh + take;
                        regrowBudget -= take / (rate > 0.01f ? rate : 1.f);
                        hungerCost += take * 1.1f; // hungry work
                        anyHeal = 1;
                        flesh = part->flesh;
                    }
                }

                // Stage 2 trigger: enough bud mass → restore organic limb weak
                if (flesh >= maxHp * g_cfg.limbBudThreshold * 0.98f)
                {
                    robots->setLimb(limbEnum, LIMB_ORIGINAL, nullptr);
                    float start = maxHp * g_cfg.limbRestoredStartPct;
                    if (start < 1.f) start = maxHp * 0.16f;
                    part->flesh = start;
                    // heavy stun — useless in a fight for a bit
                    part->fleshStun = maxHp * 0.55f;
                    part->updateDerivedHealths();
                    anyHeal = 1;
                    hungerCost += maxHp * 0.25f;
                    if (worstSeverity < 0.85f) worstSeverity = 0.85f;

                    if (g_cfg.debugLog)
                    {
                        char msg[160];
                        std::snprintf(msg, sizeof(msg),
                            "ToughnessFeast: limb RESTORED weak (slot %d) flesh=%.0f/%.0f",
                            i, start, maxHp);
                        DebugLog(msg);
                    }
                }
                else if (g_cfg.debugLog && flesh > 1.f)
                {
                    // occasional bud progress (throttle by flesh crossing 10% steps)
                    int step = (int)(flesh / maxHp * 10.f);
                    static int s_lastStep[4] = { -1, -1, -1, -1 };
                    if (step != s_lastStep[i])
                    {
                        s_lastStep[i] = step;
                        char msg[160];
                        std::snprintf(msg, sizeof(msg),
                            "ToughnessFeast: limb BUDDING slot %d %.0f%% (need %.0f%%)",
                            i, flesh / maxHp * 100.f, g_cfg.limbBudThreshold * 100.f);
                        DebugLog(msg);
                    }
                }
            }
        }
        else if (state == LIMB_ORIGINAL || tableState == LIMB_ORIGINAL)
        {
            // Stage 3–4: organic limb present — strengthen slowly if weak
            float pct = (maxHp > 0.f) ? (flesh / maxHp) : 1.f;
            if (pct < 0.f) pct = 0.f;

            if (pct < g_cfg.limbStrongPct)
            {
                // Combat severity from how weak the limb still is
                float sev = 1.f - (pct / (g_cfg.limbStrongPct > 0.1f ? g_cfg.limbStrongPct : 0.72f));
                if (sev > worstSeverity) worstSeverity = sev;

                // Prefer dedicated slow regrow budget for weak restored limbs
                if (regrowBudget > 0.f && flesh < maxHp)
                {
                    float need = maxHp - flesh;
                    // even slower after restore (consolidation)
                    float take = need;
                    float room = regrowBudget * 0.65f;
                    if (take > room) take = room;
                    if (take > 0.f)
                    {
                        part->flesh = flesh + take;
                        regrowBudget -= take / 0.65f;
                        hungerCost += take * 0.35f;
                        anyHeal = 1;
                        // clear stun gradually
                        if (part->fleshStun > 0.f)
                        {
                            float st = part->fleshStun;
                            float stTake = take * 0.8f;
                            if (stTake > st) stTake = st;
                            part->fleshStun -= stTake;
                        }
                        part->updateDerivedHealths();
                    }
                }
            }
            else
            {
                // healthy limb — no severity from this part
            }
        }
    }
}

static void ApplyFoodRegenFromStats(CharStats* stats, float frameTime)
{
    if (!g_cfg.enableMedicalHooks) return;
    if (!stats) return;
    if (frameTime <= 0.f) return;
    if (frameTime > 0.25f) frameTime = 0.25f;

    float power = RegenPowerFromStats(stats);
    if (power <= 0.f) return;

    MedicalSystem* med = stats->medical;
    if (!med) return;
    if (med->dead) return;

    Character* me = stats->me;
    if (!me) return;
    if (!me->amSomeoneWhoNeedsToEatToLive()) return;

    float hunger = med->hunger;
    if (hunger < g_cfg.minHungerToRegen) return;
    if (hunger < 0.f || hunger > 5.f) return;

    float fleshBudget = g_cfg.fleshHealPerSecond * power * frameTime;
    float stunBudget = g_cfg.stunHealPerSecond * power * frameTime;
    float hungerCost = 0.f;
    int anyHeal = 0;
    float worstSeverity = 0.f;

    // --- staged limb restore (slow) ---
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
                stats->_toughness, power, next, worstSeverity);
            DebugLog(msg);
        }
    }
    else if (worstSeverity > 0.05f)
    {
        // Still apply combat penalty even if no heal this tick (starving / slow)
        ApplyRegrowthCombatPenalty(stats, worstSeverity);
    }

    // HUD: show selected/this character stage progress
    g_lastStats = stats;
    RefreshStatusHud(stats);
}

// ---------------------------------------------------------------------------
// Soft-cap combat toughness
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
    if (g_cfg.enableMedicalHooks)
        ApplyFoodRegenFromStats(self, 0.02f);
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
    if (g_cfg.enableMedicalHooks)
        ApplyFoodRegenFromStats(self, 0.05f);
    else if (g_cfg.showStatusHud)
        RefreshStatusHud(self);
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
    if (g_cfg.enableMedicalHooks)
        ApplyFoodRegenFromStats(self, 0.1f);
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

    // Intentionally NOT hooking medicalUpdate (re-entrancy / layout crashes).
    // Regen is applied from CharStats DR / wound-degen / XP-time hooks instead.
    if (g_cfg.enableMedicalHooks)
        DebugLog("ToughnessFeast: food regen + staged limb restore ON (no medicalUpdate hook)");
    else
        DebugLog("ToughnessFeast: food regen OFF (EnableMedicalHooks=0)");
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
    DebugLog("ToughnessFeast: ready (staged limb restore + status HUD)");
}
