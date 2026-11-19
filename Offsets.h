#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "../Memory/Memory.h"

using namespace Memory;




struct monoArray {
    void *klass;
    void *obj;
    void *bounds;
    size_t max_length;
    uintptr_t m_Items;
};

struct monoList {
    void *klass;
    void *obj;
    uintptr_t monoArray;
    int pSize;
};




template <typename TKey, typename TValue> struct monoDictionaryEntry {
    int hashCode;
    int next;
    TKey key;
    TValue value;
};

template <typename TKey, typename TValue> struct monoDictionary {
    void *klass;
    void *obj;
    int32_t *buckets;
    uintptr_t arrayEntries;
    int count;
    int version;
    int freeList;
    int freeCount;
    uintptr_t comparer;
    uintptr_t keys;
    uintptr_t values;
};




template <typename T>
inline std::vector<T> get_mono_list_vector(monoList array) {
    std::vector<T> output(array.pSize);
    if (array.monoArray == 0 || array.pSize <= 0)
        return output;

    auto address = array.monoArray + offsetof(monoArray, m_Items);
    for (int i = 0; i < array.pSize; i++) {
        output[i] = Memory::Read<T>(address + i * sizeof(T));
    }
    return output;
}

template <typename TKey, typename TValue>
inline std::vector<monoDictionaryEntry<TKey, TValue>>
get_mono_dictionary_vector(monoDictionary<TKey, TValue> dict) {
    std::vector<monoDictionaryEntry<TKey, TValue>> output(dict.count);
    if (dict.arrayEntries == 0 || dict.count <= 0)
        return output;

    auto address = dict.arrayEntries + offsetof(monoArray, m_Items);
    for (int i = 0; i < dict.count; i++) {
        output[i] = Memory::Read<monoDictionaryEntry<TKey, TValue>>(
                address + i * sizeof(monoDictionaryEntry<TKey, TValue>));
    }
    return output;
}




namespace Offsets {

    // ═══════════════════════════════════════════════════════════════════
    //  АВТО АПДЕЙТ ҮЧҮН ЭСКЕРТҮҮ
    //
    //  6 поле ГАНА өзгөрүлмө (inline, const эмес) — features/AutoOffset.h
    //  ушуларды авто табат жана ушул жерге жазат:
    //
    //      BattleManager::BasePtr          ← класс "BattleManager"
    //      Camera::MainCameraPtr           ← класс "Camera" (UnityEngine)
    //      SystemData::BasePtr             ← класс "SystemData"
    //      LogicBattleManager::BasePtr     ← класс "LogicBattleManager"
    //      GameServerConfig::TypeInfo      ← класс "GameServerConfig"
    //      GameCommon::TypeInfo            ← класс "GameCommon"
    //
    //  Себеби: алтоо тең Il2Cpp TypeInfo таблицасынын слотторуна
    //  көрсөтөт ("libbase + X" түрүндө окулат) жана оюн ар жаңыланганда
    //  МИЛДЕТТҮҮ өзгөрөт. Төмөндөгү маанилер — FALLBACK (авто скан
    //  иштебей калса колдонулат).
    //
    //  Калган бардык offset'тер — объекттин ӨЗ ИЧИНДЕГИ offset'тери
    //  (entity + 0x1a4 ж.б.). Алар libbase'ке таптакыр байланбайт,
    //  ошондуктан авто апдейт аларга тийбейт жана чынжыр үзүлбөйт.
    //  Ошол себептен const бойдон калды.
    // ═══════════════════════════════════════════════════════════════════




    namespace BattleManager {
                inline size_t BasePtr = 0x635b290;          // ← АВТО (өзгөрүлмө)
                inline size_t LocalPlayerShow = 0x48;   // fallback
                inline size_t ShowPlayers = 0x70;   // fallback
                inline size_t ShowMonsters = 0x78;   // fallback
                inline size_t m_RunBullets = 0x38;   // fallback
                const size_t PtrChain1 = 0xa8;
                const size_t ListDataOffset = 0x10;
                const size_t ListCountOffset = 0x18;
                const size_t ListArrayOffset = 0x20;
    }




    namespace ShowEntity {
        inline size_t iType = 0x78;   // fallback
        inline size_t Hp = 0x1a4;   // fallback
        inline size_t HpMax = 0x1a8;   // fallback
        inline size_t bDeath = 0xc5;   // fallback
        inline size_t bSameCampType = 0x2a9;   // fallback
        inline size_t vCachePosition = 0x28c;   // fallback
        inline size_t HeroID = 0x18c;   // fallback
        inline size_t m_uGuid = 0x188;   // fallback
        inline size_t Level = 0x190;   // fallback
        inline size_t SummonSkillId = 0x984;   // fallback
        inline size_t m_ShowCoolDownComp = 0xf8;   // fallback
        inline size_t m_RoleName = 0x460;   // fallback        // 2.2.16.x dump
    }




    namespace ShowPlayer {
        inline size_t m_EntityCampType = 0xd0;   // fallback
        // 2.2.16.x dump'тан — учурда колдонулбайт, керек болсо даяр:
        inline size_t m_HeroName       = 0x8f0;   // fallback
        inline size_t m_KillWildTimes  = 0xa4c;   // fallback (dump: eski 0xa28 kata ele)
    }




    // ═══════════════════════════════════════════════════════════════════
    //  CAMERA — чыныгы класстын аты "GameMethod" (UnityEngine.Camera ЭМЕС)
    //
    //  Толук чынжыр (MLBB 2.2.16.x, tested):
    //      klass = Read(libbase + MainCameraPtr)   ← GameMethod класс слоту
    //      sf    = Read(klass + CameraData)        ← 0xa8  static_fields
    //      p     = Read(sf    + CameraData2)       ← SmoothFollow
    //      p     = Read(p     + CameraExtraStep)   ← m_MainCamera (managed)
    //      cam   = Read(p     + SmoothFollow)      ← native Camera
    //      mtx   = Read(cam   + ViewMatrix)        ← 2 × Matrix4x4
    //
    //  CameraData2 жана CameraExtraStep өзгөрүлмө: версияга жараша
    //  чынжыр 3 же 4 кадам. features/AutoOffset.h экөөнү тең сынап,
    //  иштегенин коёт:
    //      эски (3 кадам): CameraData2 = 0x08,  CameraExtraStep = 0
    //      жаңы (4 кадам): CameraData2 = 0x10,  CameraExtraStep = 0x48
    //  CameraExtraStep == 0 болсо ал кадам өткөрүлөт.
    // ═══════════════════════════════════════════════════════════════════
    namespace Camera {
        inline size_t MainCameraPtr   = 0x635b620;   // ← АВТО (GameMethod слоту)
        const  size_t CameraData      = 0xa8;        // static_fields
        inline size_t CameraData2     = 0x10;        // ← АВТО (0x08 же 0x10)
        inline size_t CameraExtraStep = 0x48;        // ← АВТО (0 же 0x48)
        const  size_t SmoothFollow    = 0x10;
        const  size_t ViewMatrix      = 0x5C;
        const  size_t v2              = 0xc8;
    }




    namespace String {
        const size_t LengthOffset = 0x10;
        const size_t BufferOffset = 0x14;
    }




    namespace SystemData {
        inline size_t BasePtr = 0x635cb88;                  // ← АВТО (өзгөрүлмө)
        inline size_t m_RoomPlayerInfo = 0x348;   // fallback
        const size_t PtrChain1 = 0xa8;
        inline size_t m_uiID = 0x358;   // fallback
        inline size_t m_bOfflineManagerRun = 0x36e;   // fallback
    }




    // ⚠ RoomData АВТО СКАНГА КИРБЕЙТ
    //   "RoomPlayerInfo" классынын dump'у Offsets.h менен дал келбеди
    //   (uiRankLevel: 0x120 vs 0x34, iRoad: 0x138 vs 0xfc) — демек
    //   main.cpp башка структураны окуп жатат. Жаңылыш класстан жазсак
    //   Room Info бузулмак, ошондуктан бул блок const бойдон калды.
    namespace RoomData {
        const size_t lUid = 0x20;
        const size_t iCamp = 0x30;
        const size_t _sName = 0x40;
        const size_t heroid = 0x4c;
        const size_t heroskin = 0x50;
        const size_t country = 0x5c;
        const size_t uiZoneId = 0x60;
        const size_t summonSkillId = 0x64;
        const size_t sThisLoginCountry = 0xd0;
        const size_t _steamSimpleName = 0x100;
        const size_t uiRankLevel = 0x120;
        const size_t iRoad = 0x138;
        const size_t iMythPoint = 0x1cc;
        const size_t bRoomLeader = 0x32c;
        const size_t mapHeroBattleNum = 0x3e8;
        const size_t vCurSeasonRealRoadInfo = 0x3f0;
    }




    namespace LogicBattleManager {
        inline size_t BasePtr = 0x635b5d8;                  // ← АВТО (өзгөрүлмө)
        const size_t PtrChain1 = 0xa8;
        inline size_t m_uiFrameTime = 0x13c;   // fallback
    }




    // ═══════════════════════════════════════════════════════════════════
    //  Login Server маалыматы (ReadLoginServerInfo колдонот)
    //
    //  Мурда main.cpp'те #define катары турган эле — ошондуктан авто
    //  апдейтке кирбей калчу. Эми ушул жерге көчүрүлдү: TypeInfo
    //  өзгөрүлмө (авто табылат), калганы класс ички offset — const.
    //
    //  Чынжыры калган 4 BasePtr менен таптакыр бирдей:
    //      klass       = Read(libbase + TypeInfo)
    //      staticField = Read(klass + StaticField)   ← 0xa8
    //      instance    = Read(staticField + Instance)
    //      loginInfo   = Read(instance + LoginInfo)
    // ═══════════════════════════════════════════════════════════════════

    namespace GameServerConfig {
        inline size_t TypeInfo = 0x635bcf8;                 // ← АВТО (өзгөрүлмө)
        const size_t StaticField = 0xa8;
        const size_t Instance    = 0xa8;
        const size_t LoginInfo   = 0x250;
    }




    namespace GameCommon {
        inline size_t TypeInfo = 0x760cdc0;                 // ← АВТО (өзгөрүлмө)
        const size_t Inst      = 0x0;
        const size_t LoginInfo = 0x58;
    }




    namespace LoginInfo {
        const size_t AccountId     = 0x28;
        const size_t AccCreateTime = 0x88;
    }




    namespace ShowCoolDownComp {
        inline size_t _dicCD = 0x18;   // fallback
    }




    namespace CoolDownData {
        inline size_t uiCoolTime = 0x14;   // fallback
        inline size_t uiStartTime = 0x1C;   // fallback
    }




    namespace ShowSkillData {
        inline size_t config = 0x10;   // fallback
    }




    namespace Bullet {
        inline size_t m_Id = 0x3c;   // fallback
        inline size_t _bulletPos = 0xfc;   // fallback
    }

}




#define OFF_STR(field) Offsets::String::field
#define OFF_CAM(field) Offsets::Camera::field
#define OFF_BM(field) Offsets::BattleManager::field
#define OFF_SE(field) Offsets::ShowEntity::field
#define OFF_SP(field) Offsets::ShowPlayer::field
#define OFF_SD(field) Offsets::SystemData::field
#define OFF_RD(field) Offsets::RoomData::field
#define OFF_BLC(field) Offsets::LogicBattleManager::field
#define OFF_SCC(field) Offsets::ShowCoolDownComp::field
#define OFF_CDD(field) Offsets::CoolDownData::field
#define OFF_BUL(field) Offsets::Bullet::field
