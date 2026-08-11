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

enum StatsEnumerated { STAT_TOUGHNESS = 0 };
enum LimbState { LIMB_ORIGINAL = 0, LIMB_STUMP = 1, LIMB_REPLACED = 2, LIMB_CRUSHED = 3 };

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
};
class MedicalSystem {
public:
  class HealthPartStatus {
  public:
    float flesh=100, fleshStun=0, _maxHealth=100;
    bool isRobotic(){return false;}
    float maxHealth()const{return _maxHealth;}
    LimbState getRobotLimbState(){return LIMB_ORIGINAL;}
    void updateDerivedHealths(){}
  };
  float hunger=1.f;
  bool dead=false;
  CharStats* stats=nullptr;
  Character* me=nullptr;
  RobotLimbs* robotLimbs=nullptr;
  int getPartCount()const{return 0;}
  HealthPartStatus* getPart(unsigned long long){return nullptr;}
  HealthPartStatus* getPart(RobotLimbs::Limb){return nullptr;}
};

namespace MyGUI {
struct Colour { Colour(float=1,float=1,float=1,float=1){} };
struct Align { enum Enum { Default=0, Left=1, Top=2, Stretch=4, Center=8 }; };
struct UString { UString(){} UString(const char*){} UString(const std::string&){} };
class Widget {
public:
  virtual ~Widget(){}
  template<class T> T* createWidgetReal(const char*, float,float,float,float, int, const char*, const char* =nullptr){return nullptr;}
  void setVisible(bool){}
  void setEnabled(bool){}
  struct Ev { template<class F> void operator+=(F){} };
};
class TextBox : public Widget {
public:
  void setCaption(const UString&){}
  void setTextColour(Colour){}
};
class EditBox : public TextBox {
public:
  void setEditReadOnly(bool){}
  void setEditMultiLine(bool){}
  void setEditWordWrap(bool){}
  void setEditStatic(bool){}
  void setOnlyText(const UString&){}
  void setCaption(const UString&){}
};
class Button : public Widget {
public:
  void setCaption(const UString&){}
  Ev eventMouseButtonClick;
};
class Window : public Widget {
public:
  void setCaption(const UString&){}
  Widget* getClientWidget(){return this;}
  Ev eventWindowButtonPressed;
};
class Gui {
public:
  static Gui* getInstancePtr(){return nullptr;}
  template<class T> T* createWidgetReal(const char*, float,float,float,float, int, const char*, const char* =nullptr){return nullptr;}
};
template<class F> inline F newDelegate(F f){return f;}
}

class StringPair { public: StringPair(){} StringPair(const std::string&, const std::string&){} };
template<typename T> class lektor {
public:
  unsigned count=0, maxSize=0;
  T* stuff=nullptr;
};
