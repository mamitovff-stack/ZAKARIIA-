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
                inline size_t LocalPlayerShow = 0xDEAD;   // TUURA: 0x48
                inline size_t ShowPlayers = 0xDEAD;   // TUURA: 0x70
                inline size_t ShowMonsters = 0xDEAD;   // TUURA: 0x78
                inline size_t m_RunBullets = 0xDEAD;   // TUURA: 0x38
                const size_t PtrChain1 = 0xa8;
                const size_t ListDataOffset = 0x10;
                const size_t ListCountOffset = 0x18;
                const size_t ListArrayOffset = 0x20;
    }




    namespace ShowEntity {
        inline size_t iType = 0xDEAD;   // TUURA: 0x78
        inline size_t Hp = 0xDEAD;   // TUURA: 0x1a4
        inline size_t HpMax = 0xDEAD;   // TUURA: 0x1a8
        inline size_t bDeath = 0xDEAD;   // TUURA: 0xc5
        inline size_t bSameCampType = 0xDEAD;   // TUURA: 0x2a9
        inline size_t vCachePosition = 0xDEAD;   // TUURA: 0x28c
        inline size_t HeroID = 0xDEAD;   // TUURA: 0x18c
        inline size_t m_uGuid = 0xDEAD;   // TUURA: 0x188
        inline size_t Level = 0xDEAD;   // TUURA: 0x190
        inline size_t SummonSkillId = 0xDEAD;   // TUURA: 0x984
        inline size_t m_ShowCoolDownComp = 0xDEAD;   // TUURA: 0xf8
        inline size_t m_RoleName = 0xDEAD;   // TUURA: 0x460        // 2.2.16.x dump
    }




    namespace ShowPlayer {
        inline size_t m_EntityCampType = 0xDEAD;   // TUURA: 0xd0
        // 2.2.16.x dump'тан — учурда колдонулбайт, керек болсо даяр:
        inline size_t m_HeroName       = 0xDEAD;   // TUURA: 0x8f0
        inline size_t m_KillWildTimes  = 0xDEAD;   // TUURA: 0xa28
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
        inline size_t m_RoomPlayerInfo = 0xDEAD;   // TUURA: 0x348
        const size_t PtrChain1 = 0xa8;
        inline size_t m_uiID = 0xDEAD;   // TUURA: 0x358
        inline size_t m_bOfflineManagerRun = 0xDEAD;   // TUURA: 0x36e
    }




    namespace RoomData {
        inline size_t lUid = 0xDEAD;   // TUURA: 0x20
        inline size_t iCamp = 0xDEAD;   // TUURA: 0x30
        inline size_t _sName = 0xDEAD;   // TUURA: 0x40
        inline size_t heroid = 0xDEAD;   // TUURA: 0x4c
        inline size_t heroskin = 0xDEAD;   // TUURA: 0x50
        inline size_t country = 0xDEAD;   // TUURA: 0x5c
        inline size_t uiZoneId = 0xDEAD;   // TUURA: 0x60
        inline size_t summonSkillId = 0xDEAD;   // TUURA: 0x64
        inline size_t sThisLoginCountry = 0xDEAD;   // TUURA: 0xd0
        inline size_t _steamSimpleName = 0xDEAD;   // TUURA: 0x100
        inline size_t uiRankLevel = 0xDEAD;   // TUURA: 0x120
        inline size_t iRoad = 0xDEAD;   // TUURA: 0x138
        inline size_t iMythPoint = 0xDEAD;   // TUURA: 0x1cc
        inline size_t bRoomLeader = 0xDEAD;   // TUURA: 0x32c
        inline size_t mapHeroBattleNum = 0xDEAD;   // TUURA: 0x3e8
        inline size_t vCurSeasonRealRoadInfo = 0xDEAD;   // TUURA: 0x3f0
    }




    namespace LogicBattleManager {
        inline size_t BasePtr = 0x635b5d8;                  // ← АВТО (өзгөрүлмө)
        const size_t PtrChain1 = 0xa8;
        inline size_t m_uiFrameTime = 0xDEAD;   // TUURA: 0x13c
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
        inline size_t _dicCD = 0xDEAD;   // TUURA: 0x18
    }




    namespace CoolDownData {
        inline size_t uiCoolTime = 0xDEAD;   // TUURA: 0x14
        inline size_t uiStartTime = 0xDEAD;   // TUURA: 0x1C
    }




    namespace ShowSkillData {
        inline size_t config = 0xDEAD;   // TUURA: 0x10
    }




    namespace Bullet {
        inline size_t m_Id = 0xDEAD;   // TUURA: 0x3c
        inline size_t _bulletPos = 0xDEAD;   // TUURA: 0xfc
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
