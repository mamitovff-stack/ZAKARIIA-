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




    namespace BattleManager {
                const size_t BasePtr = 0x6029fe8;   // BM_BasePtr  (2026-11-19)
                const size_t LocalPlayerShow = 0x48;
                const size_t ShowPlayers = 0x70;
                const size_t ShowMonsters = 0x78;
                const size_t m_RunBullets = 0x38;
                const size_t PtrChain1 = 0xa8;
                const size_t ListDataOffset = 0x10;
                const size_t ListCountOffset = 0x18;
                const size_t ListArrayOffset = 0x20;
    } 




    namespace ShowEntity {
        const size_t iType = 0x78;
        const size_t Hp = 0x1a4;
        const size_t HpMax = 0x1a8;
        const size_t bDeath = 0xc5;
        const size_t bSameCampType = 0x2a9;
        const size_t vCachePosition = 0x28c;
        const size_t HeroID = 0x18c;
        const size_t m_uGuid = 0x188;
        const size_t Level = 0x190;
        const size_t SummonSkillId = 0x984;
        const size_t m_ShowCoolDownComp = 0xf8;
    } 




    namespace ShowPlayer {
        const size_t m_EntityCampType = 0xd0;
    } 




    // ── CAMERA ─────────────────────────────────────────────────────────
    //  Класстын аты "GameMethod" (UnityEngine.Camera ЭМЕС).
    //  Чынжыр 4 кадам:
    //      klass = Read(libbase + MainCameraPtr)
    //      sf    = Read(klass + CameraData)        0xa8  static_fields
    //      p     = Read(sf    + CameraData2)       0x10  SmoothFollow
    //      p     = Read(p     + CameraExtraStep)   0x48  m_MainCamera  <-- ЖАҢЫ
    //      cam   = Read(p     + SmoothFollow)      0x10  native Camera
    //      mtx   = Read(cam   + ViewMatrix)        0x5C  2 x Matrix4x4
    //
    //  ⚠ CameraExtraStep кадамы main.cpp'тагы GetMainCamera() ичинде
    //    жазылышы керек — болбосо ESP экранда ЧЫКПАЙТ.
    // ───────────────────────────────────────────────────────────────────
    namespace Camera {
        const size_t MainCameraPtr   = 0x600c4b0;  // CAM_BasePtr (2026-11-19)
        const size_t CameraData      = 0xa8;
        const size_t CameraData2     = 0x10;       // <- мурда 0x08 эле
        const size_t CameraExtraStep = 0x48;       // <- ЖАҢЫ сап
        const size_t SmoothFollow    = 0x10;
        const size_t ViewMatrix      = 0x5C;
        const size_t v2              = 0xc8;
    } 




    namespace String {
        const size_t LengthOffset = 0x10;
        const size_t BufferOffset = 0x14;
    } 




    namespace SystemData {
        const size_t BasePtr = 0x607c7d0;   // SD_BasePtr  (2026-11-19)
        const size_t m_RoomPlayerInfo = 0x348;
        const size_t PtrChain1 = 0xa8;
        const size_t m_uiID = 0x358;
        const size_t m_bOfflineManagerRun = 0x36e;
    } 




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
        const size_t BasePtr = 0x607c5e0;   // LBM_BasePtr (2026-11-19)
        const size_t PtrChain1 = 0xa8;
        const size_t m_uiFrameTime = 0x13c;
    } 




    namespace ShowCoolDownComp {
        const size_t _dicCD = 0x18;
    }




    namespace CoolDownData {
        const size_t uiCoolTime = 0x14;
        const size_t uiStartTime = 0x1C;
    } 




    namespace ShowSkillData {
        const size_t config = 0x10;
    } 




    namespace Bullet {
        const size_t m_Id = 0x3c;
        const size_t _bulletPos = 0xfc;
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
