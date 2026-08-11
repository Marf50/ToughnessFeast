#pragma once
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
using DWORD=unsigned long; using HMODULE=void*; using LPCSTR=const char*; using LPSTR=char*; using BOOL=int;
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x2
#endif
inline BOOL GetModuleHandleExA(DWORD, LPCSTR, HMODULE* o){ if(o)*o=nullptr; return FALSE; }
inline DWORD GetModuleFileNameA(HMODULE, LPSTR b, DWORD s){ if(b&&s)b[0]=0; return 0; }
inline void DebugLog(const char*){}
inline void DebugLog(const std::string&){}
inline void ErrorLog(const char*){}
inline void ErrorLog(const std::string&){}
enum StatsEnumerated { STAT_TOUGHNESS=0 };
enum LimbState { LIMB_ORIGINAL=0, LIMB_STUMP=1, LIMB_REPLACED=2, LIMB_CRUSHED=3 };
class Character; class CharStats; class MedicalSystem; class GameData; class RaceData; class Item; class DatapanelGUI;
class GameData { public: std::string name, stringID; };
class RaceData { public: GameData* data=nullptr; };
class StringPair {
public:
  StringPair(){}
  StringPair(const std::string&a,const std::string&b):s1(a),s2(b){}
  StringPair(const StringPair&o):s1(o.s1),s2(o.s2),val1(o.val1){}
  virtual ~StringPair(){}
  std::string s1,s2; float val1=0;
};
template<typename T> class lektor {
public:
  uint32_t count=0,maxSize=0; T* stuff=nullptr;
  T* allocate(uint32_t n){ return (T*)::operator new(sizeof(T)*n); }
  void deallocate(T*p,uint32_t){ ::operator delete(p); }
  void construct(T*p){ new(p)T(); }
  void construct(T*p,const T&v){ new(p)T(v); }
  void destroy(T*p){ p->~T(); }
  uint32_t size()const{return count;}
  T& operator[](uint32_t i){return stuff[i];}
};
class DataPanelLine {
public:
  std::string keyValue,s1,s2;
  void setToolTip(const std::string&){}
  void setToolTipMainBar(const std::string&,bool){}
  void setToolTipMainBar(const lektor<StringPair>&,bool){}
};
class DatapanelGUI {
public:
  DataPanelLine* setLine(const std::string&,const std::string&,const std::string&,int,bool,bool){return nullptr;}
  DataPanelLine* getLine(const std::string&,int){return nullptr;}
  int getNumLines(int){return 0;}
  DataPanelLine* getLineByNum(int,int){return nullptr;}
};
class Character {
public:
  bool amSomeoneWhoNeedsToEatToLive(){return true;}
  bool isPlayerCharacter()const{return true;}
  RaceData* getRace()const{return nullptr;}
  CharStats* getStats(){return nullptr;}
  MedicalSystem* getMedical(){return nullptr;}
  void updateGUIStatsDetails(DatapanelGUI*,const std::string&,int){}
};
class RobotLimbs {
public:
  enum Limb{LEFT_ARM,RIGHT_ARM,LEFT_LEG,RIGHT_LEG,NULL_LIMB};
  void setLimb(Limb,LimbState,Item*){}
};
class CharStats {
public:
  float _toughness=0;
  MedicalSystem* medical=nullptr;
  Character* me=nullptr;
  float calculateToughnessDamageResistanceMult(){return 1;}
  float calculateToughnessWoundDegenerationRate(){return 1;}
  void xpStat_eventBased(StatsEnumerated,float){}
  void xpStat_timeBased(StatsEnumerated){}
  void getGUIData(DatapanelGUI*,int){}
  void getGUIDataForMainInfo(DatapanelGUI*,int,bool){}
  bool getStatPenaltiesForGUI(const std::string&,StatsEnumerated,lektor<StringPair>&){return false;}
  void printExertionHungerMultTooltip(lektor<StringPair>*){}
};
class MedicalSystem {
public:
  struct HealthPartStatus {
    enum PartType{PART_TORSO=0,PART_HEAD=1,PART_ARM=2,PART_LEG=3};
    float flesh=0,fleshStun=0,_maxHealth=100;
    PartType whatAmI=PART_TORSO;
    bool isRobotic()const{return false;}
    bool isDead()const{return false;}
    float maxHealth()const{return _maxHealth;}
    LimbState getRobotLimbState()const{return LIMB_ORIGINAL;}
    RobotLimbs::Limb getRobotLimbEnum()const{return RobotLimbs::NULL_LIMB;}
    void updateDerivedHealths(){}
  };
  bool dead=false; float hunger=1,fed=1;
  CharStats* stats=nullptr; Character* me=nullptr;
  HealthPartStatus *leftArm=nullptr,*rightArm=nullptr,*leftLeg=nullptr,*rightLeg=nullptr;
  RobotLimbs* robotLimbs=nullptr;
  bool isFed()const{return fed>0.5f;}
  int getPartCount()const{return 0;}
  HealthPartStatus* getPart(unsigned long long){return nullptr;}
  void medicalUpdate(float){}
  void getMedicalGUIData(DatapanelGUI*){}
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
