#pragma once
#include <cstdint>
#include <cstring>
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
using DWORD=unsigned long; using HMODULE=void*; using LPCSTR=const char*; using LPSTR=char*; using BOOL=int;
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2
#endif
inline BOOL GetModuleHandleExA(DWORD,LPCSTR,HMODULE*o){if(o)*o=0;return 0;}
inline void* GetProcAddress(void*, const char*){return nullptr;}
inline void* GetModuleHandleA(const char*){return nullptr;}
inline DWORD GetModuleFileNameA(HMODULE,LPSTR b,DWORD s){if(b&&s)b[0]=0;return 0;}
inline void DebugLog(const char*){}
inline void ErrorLog(const char*){}
enum StatsEnumerated{STAT_TOUGHNESS=0};
enum LimbState{LIMB_ORIGINAL=0,LIMB_STUMP=1,LIMB_REPLACED=2,LIMB_CRUSHED=3};
class Character; class CharStats; class MedicalSystem; class RaceData; class Item;
class RaceData {
public:
  float hungerRate=1.f, healRate=1.f;
  bool gigantic=false, robot=false;
};
class Character {
public:
  bool amSomeoneWhoNeedsToEatToLive(){return true;}
  RaceData* getRace()const{return nullptr;}
};
class RobotLimbs {
public:
  enum Limb{LEFT_ARM,RIGHT_ARM,LEFT_LEG,RIGHT_LEG,NULL_LIMB};
  void setLimb(Limb,LimbState,Item*){}
};
class CharStats {
public:
  MedicalSystem* medical=nullptr;
  Character* me=nullptr;
  float _toughness=0;
  float calculateToughnessDamageResistanceMult(){return 1;}
  float calculateToughnessWoundDegenerationRate(){return 1;}
  void xpStat_eventBased(StatsEnumerated,float){}
  void xpStat_timeBased(StatsEnumerated){}
};
class MedicalSystem {
public:
  struct HealthPartStatus {
    float flesh=0,fleshStun=0,_maxHealth=100;
    bool isRobotic()const{return false;}
    bool isDead()const{return false;}
    float maxHealth()const{return _maxHealth;}
    LimbState getRobotLimbState()const{return LIMB_ORIGINAL;}
    RobotLimbs::Limb getRobotLimbEnum()const{return RobotLimbs::NULL_LIMB;}
    void updateDerivedHealths(){}
  };
  bool dead=false; float hunger=1;
  CharStats* stats=nullptr; Character* me=nullptr;
  HealthPartStatus *leftArm=nullptr,*rightArm=nullptr,*leftLeg=nullptr,*rightLeg=nullptr;
  RobotLimbs* robotLimbs=nullptr;
  int getPartCount()const{return 0;}
  HealthPartStatus* getPart(unsigned long long){return nullptr;}
  void medicalUpdate(float){}
};
namespace KenshiLib {
enum HookStatus{SUCCESS=0,FAIL=1};
inline intptr_t GetRealAddress(void*){return 0;}
template<typename T> inline intptr_t GetRealAddress(T f){return GetRealAddress(reinterpret_cast<void*&>(f));}
inline HookStatus AddHook(void*,void*,void**){return SUCCESS;}
template<typename T> inline HookStatus AddHook(intptr_t t,void*d,T**o){return AddHook((void*)t,d,(void**)o);}
template<typename A,typename B> inline HookStatus AddHook(A*t,void*d,B**o){return AddHook((void*)t,d,(void**)o);}
}
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
inline int strncpy_s(char*d,size_t n,const char*s,size_t){if(!d||!n)return 1;size_t i=0;while(i+1<n&&s&&s[i]){d[i]=s[i];++i;}d[i]=0;return 0;}
template<size_t N> inline int strncpy_s(char(&d)[N],const char*s,size_t c){return strncpy_s(d,N,s,c);}
#ifndef __declspec
#define __declspec(x)
#endif
#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER 1
#endif
