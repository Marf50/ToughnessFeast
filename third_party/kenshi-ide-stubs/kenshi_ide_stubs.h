#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

struct HINSTANCE__;
typedef struct HINSTANCE__* HMODULE;
typedef unsigned long DWORD;
typedef char* LPSTR;
typedef const char* LPCSTR;
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
inline int GetModuleHandleExA(DWORD, LPCSTR, HMODULE*) { return 0; }
inline DWORD GetModuleFileNameA(HMODULE, LPSTR, DWORD) { return 0; }
inline void* GetProcAddress(void*, const char*) { return nullptr; }
inline HMODULE GetModuleHandleA(const char*) { return nullptr; }
inline void DebugLog(const char*) {}
inline void ErrorLog(const char*) {}

namespace KenshiLib {
enum HookStatus { SUCCESS, FAIL };
inline intptr_t GetRealAddress(void*) { return 0; }
template<typename T> inline intptr_t GetRealAddress(T) { return 0; }
inline HookStatus AddHook(void*, void*, void**) { return SUCCESS; }
}

enum StatsEnumerated {
    STAT_NONE=0, STAT_STRENGTH=1, STAT_MELEE_ATTACK=2, STAT_LABOURING=3,
    STAT_SCIENCE=4, STAT_ENGINEERING=5, STAT_ROBOTICS=6, STAT_SMITHING_WEAPON=7,
    STAT_SMITHING_ARMOUR=8, STAT_MEDIC=9, STAT_THIEVING=10, STAT_TURRETS=11,
    STAT_FARMING=12, STAT_COOKING=13, STAT_HIVEMEDIC=14, STAT_VET=15,
    STAT_STEALTH=16, STAT_ATHLETICS=17, STAT_DEXTERITY=18, STAT_MELEE_DEFENCE=19,
    STAT_WEAPONS=20, STAT_TOUGHNESS=21
};
enum LeftRight { SIDE_NEITHER=0, SIDE_LEFT=1, SIDE_RIGHT=2, SIDE_BOTH=3 };
enum LimbState { LIMB_ORIGINAL=0, LIMB_STUMP=1, LIMB_REPLACED=2, LIMB_CRUSHED=3 };

class Character; class CharStats; class MedicalSystem; class RaceData; class Item; class GameData;
class GameData { public: char pad[0x80]; };
class RaceData {
public:
  float hungerRate=1.f, healRate=1.f;
  bool gigantic=false, robot=false;
  bool noHats=false, noShirts=false, noShoes=false, singleGender=false;
  GameData* data=nullptr;
};
class Character {
public:
  bool amSomeoneWhoNeedsToEatToLive(){return true;}
  RaceData* getRace()const{return nullptr;}
  CharStats* stats=nullptr;
  RaceData* myRace=nullptr;
  CharStats* getStats(){return stats;}
  bool isPlayerCharacter()const{return true;}
  bool isWithThePlayer(){return true;}
};
class RobotLimbs {
public:
  enum Limb{LEFT_ARM,RIGHT_ARM,LEFT_LEG,RIGHT_LEG,NULL_LIMB};
  LimbState getState(Limb)const{return LIMB_ORIGINAL;}
  void setLimb(Limb,LimbState,Item*){}
};
class CharStats {
public:
  MedicalSystem* medical=nullptr;
  Character* me=nullptr;
  float combatSpeedMultiplier=1.f;
  float skillMultDodge=1.f, skillMultDexterity=1.f, skillMultDamage=1.f;
  float _toughness=0;
  float calculateToughnessDamageResistanceMult(){return 1;}
  float calculateToughnessWoundDegenerationRate(){return 1;}
  void xpStat_eventBased(StatsEnumerated,float){}
  void xpStat_timeBased(StatsEnumerated){}
  void getGUIDataForMainInfo(void*, int, bool){}
  bool getStatPenaltiesForGUI(const std::string&, StatsEnumerated, void*){return false;}
  float toughness() const { return _toughness; }
  float getStat(StatsEnumerated, bool) const { return _toughness; }
  float& getStatRef(StatsEnumerated) { return _toughness; }
};
class MedicalSystem {
public:
  class HealthPartStatus {
  public:
    enum PartType { PART_TORSO, PART_LEG, PART_ARM, PART_HEAD };
    float flesh=100, fleshStun=0, _maxHealth=100;
    bool isRobotic(){return false;}
    float maxHealth()const{return _maxHealth;}
    LimbState getRobotLimbState(){return LIMB_ORIGINAL;}
    void updateDerivedHealths(){}
  };
  float hunger=1.f;
  float knockoutTimer=0.f;
  bool unconcious=false;
  bool dead=false;
  void startKnockoutTimer(){}
  void knockout(float){}
  void knockoutForceTimer(float){}
  CharStats* stats=nullptr;
  Character* me=nullptr;
  RobotLimbs* robotLimbs=nullptr;
  HealthPartStatus* leftLeg=nullptr;
  HealthPartStatus* rightLeg=nullptr;
  HealthPartStatus* leftArm=nullptr;
  HealthPartStatus* rightArm=nullptr;
  int getPartCount()const{return 0;}
  HealthPartStatus* getPart(unsigned long long){return nullptr;}
  HealthPartStatus* getPart(RobotLimbs::Limb){return nullptr;}
  HealthPartStatus* getPart(HealthPartStatus::PartType, LeftRight){return nullptr;}
  LimbState getLimbState(RobotLimbs::Limb)const{return LIMB_ORIGINAL;}
  void setRobotLimbItem(RobotLimbs::Limb, Item*, bool){}
  void updateStats(){}
  void validateHealthValues(){}
  void medicalUpdate(float){}
  void periodicUpdate(){}
  void getMedicalGUIData(void*){}
  void load(GameData*){}
  float getToughnessXpBonus(){return 0;}
  bool isLeftArmOk()const{return true;}
  bool isRightArmOk()const{return true;}
  bool canIkick()const{return true;}
};

template<typename T> class lektor {
public:
  T* stuff=nullptr;
  unsigned count=0, maxSize=0;
};
struct StringPair { char pad[0x60]; };
