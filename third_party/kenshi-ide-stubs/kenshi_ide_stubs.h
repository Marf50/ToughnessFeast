// Minimal stand-ins for KenshiLib types — used only when TOUGHNESSFEAST_LINUX_IDE=1
#pragma once

#include <cstdint>
#include <string>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

using DWORD = unsigned long;
using HMODULE = void*;
using LPCSTR = const char*;
using LPSTR = char*;
using BOOL = int;
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif
inline BOOL GetModuleHandleExA(DWORD, LPCSTR, HMODULE* out)
{
    if (out) *out = nullptr;
    return FALSE;
}
inline DWORD GetModuleFileNameA(HMODULE, LPSTR buf, DWORD size)
{
    if (buf && size) buf[0] = '\0';
    return 0;
}

inline void DebugLog(const char*) {}
inline void DebugLog(const std::string&) {}
inline void ErrorLog(const char*) {}
inline void ErrorLog(const std::string&) {}

enum StatsEnumerated
{
    STAT_TOUGHNESS = 0
};

enum LimbState
{
    LIMB_ORIGINAL = 0,
    LIMB_STUMP = 1,
    LIMB_REPLACED = 2,
    LIMB_CRUSHED = 3
};

class Character;
class CharStats;
class MedicalSystem;
class GameData;
class RaceData;
class Item;

class GameData
{
public:
    std::string name;
    std::string stringID;
};

class RaceData
{
public:
    GameData* data = nullptr;
};

class Character
{
public:
    bool amSomeoneWhoNeedsToEatToLive() { return true; }
    bool isPlayerCharacter() const { return true; }
    RaceData* getRace() const { return nullptr; }
};

class CharStats
{
public:
    float _toughness = 0.f;
    float calculateToughnessDamageResistanceMult() { return 1.f; }
    float calculateToughnessWoundDegenerationRate() { return 1.f; }
    void xpStat_eventBased(StatsEnumerated, float) {}
    void xpStat_timeBased(StatsEnumerated) {}
};

class RobotLimbs
{
public:
    enum Limb
    {
        LEFT_ARM,
        RIGHT_ARM,
        LEFT_LEG,
        RIGHT_LEG,
        NULL_LIMB
    };
    void setLimb(Limb, LimbState, Item*) {}
};

class MedicalSystem
{
public:
    struct HealthPartStatus
    {
        enum PartType
        {
            PART_TORSO = 0,
            PART_HEAD = 1,
            PART_ARM = 2,
            PART_LEG = 3
        };
        float flesh = 0.f;
        float fleshStun = 0.f;
        float _maxHealth = 100.f;
        PartType whatAmI = PART_TORSO;
        bool isRobotic() const { return false; }
        bool isDead() const { return false; }
        float maxHealth() const { return _maxHealth; }
        LimbState getRobotLimbState() const { return LIMB_ORIGINAL; }
        RobotLimbs::Limb getRobotLimbEnum() const { return RobotLimbs::NULL_LIMB; }
        void updateDerivedHealths() {}
    };

    bool dead = false;
    float hunger = 1.f;
    float fed = 1.f;
    CharStats* stats = nullptr;
    Character* me = nullptr;
    HealthPartStatus* leftArm = nullptr;
    HealthPartStatus* rightArm = nullptr;
    HealthPartStatus* leftLeg = nullptr;
    HealthPartStatus* rightLeg = nullptr;
    RobotLimbs* robotLimbs = nullptr;

    bool isFed() const { return fed > 0.5f; }
    int getPartCount() const { return 0; }
    HealthPartStatus* getPart(unsigned long long) { return nullptr; }
    void medicalUpdate(float) {}
};

namespace KenshiLib
{
enum HookStatus
{
    SUCCESS = 0,
    FAIL = 1
};

inline intptr_t GetRealAddress(void*) { return 0; }

template <typename T>
inline intptr_t GetRealAddress(T fun)
{
    return GetRealAddress(reinterpret_cast<void*&>(fun));
}

inline HookStatus AddHook(void*, void*, void**) { return SUCCESS; }

template <typename T>
inline HookStatus AddHook(intptr_t target, void* detour, T** original)
{
    return AddHook(reinterpret_cast<void*>(target), detour, reinterpret_cast<void**>(original));
}

template <typename T1, typename T2>
inline HookStatus AddHook(T1* target, void* detour, T2** original)
{
    return AddHook(static_cast<void*>(target), detour, reinterpret_cast<void**>(original));
}
}

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
inline int strncpy_s(char* dest, size_t destsz, const char* src, size_t count)
{
    if (!dest || !destsz) return 1;
    size_t n = 0;
    while (n + 1 < destsz && src && src[n] && (count == _TRUNCATE || n < count))
    {
        dest[n] = src[n];
        ++n;
    }
    dest[n] = '\0';
    return 0;
}
template <size_t N>
inline int strncpy_s(char (&dest)[N], const char* src, size_t count)
{
    return strncpy_s(dest, N, src, count);
}

#ifndef __declspec
#define __declspec(x)
#endif
