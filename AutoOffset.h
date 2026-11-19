#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  features/AutoOffset.h — MLBB offset АВТО АПДЕЙТ (толук external)
//
//  ЭЧ КАНДАЙ ИНЖЕКЦИЯ ЖОК. Оюн процессине эч нерсе жүктөлбөйт.
//  Бардыгы /proc/<pid>/mem окуу аркылуу гана болот (root жетиштүү).
//
// ───────────────────────────────────────────────────────────────────────
//  ЭМНЕ ҮЧҮН ТАК УШУЛ 6 OFFSET?
//
//  main.cpp'тагы бардык чынжырларды талдадым. Оюн жаңыланганда
//  МИЛДЕТТҮҮ өзгөргөн offset'тер — "libbase + X" түрүндөгүлөр гана:
//
//    ┌────────────────────────────┬──────────┬───────────────────────┐
//    │ Offset                     │ Эски     │ Класс аты             │
//    ├────────────────────────────┼──────────┼───────────────────────┤
//    │ OFF_BM(BasePtr)            │ 0x635b290│ BattleManager         │
//    │ OFF_CAM(MainCameraPtr)     │ 0x635b620│ Camera (UnityEngine)  │
//    │ OFF_SD(BasePtr)            │ 0x635cb88│ SystemData            │
//    │ OFF_BLC(BasePtr)           │ 0x635b5d8│ LogicBattleManager    │
//    │ GAMESERVERCONFIG_TYPEINFO  │ 0x635bcf8│ GameServerConfig      │
//    │ GAMECOMMON_TYPEINFO        │ 0x760cdc0│ GameCommon            │
//    └────────────────────────────┴──────────┴───────────────────────┘
//
//  Алтоо тең БИР ЭЛЕ түзүлүш — Il2Cpp TypeInfo таблицасынын слоту:
//
//      klass       = Read(libbase + offset)       ← TypeInfo слоту
//      staticField = Read(klass + 0xa8)           ← Il2CppClass.static_fields
//      instance    = Read(staticField + N)        ← статик поле
//
//  main.cpp'те бул 0xa8 үч башка атта жүрөт, бирок бири эле нерсе:
//      OFF_BM(PtrChain1) = OFF_SD(PtrChain1)
//    = OFF_BLC(PtrChain1) = OFF_CAM(CameraData)
//    = STATIC_FIELD       = 0xa8
//
//  Калган ~66 offset (ShowEntity::Hp, RoomData::lUid, Prediction::Fields
//  ж.б.) — объекттин ӨЗ ИЧИНДЕГИ offset'тери. Алар libbase'ке таптакыр
//  байланбайт, ошондуктан бул файл аларга ТИЙБЕЙТ жана чынжыр үзүлбөйт.
//
// ───────────────────────────────────────────────────────────────────────
//  АЛГОРИТМ — 3 скан, 6 класс бир отурумда
//
//    ФАЗА 0  Offsets.h'тагы азыркы маанилер дагы иштейби?
//            Иштесе → скандоо жок (оюн жаңыланган эмес, тез баштайт).
//    ФАЗА A  класс аттарынын сабын эс тутумдан табуу        (1 скан)
//    ФАЗА B  ошол сапка көрсөткөн Il2CppClass'тарды табуу   (1 скан)
//              (сап адреси klass+0x10'до жатат)
//    ФАЗА C  ошол klass'тарга көрсөткөн слотторду модулдан  (1 скан)
//              offset = slotAddr - libbase
//    ФАЗА D  ар бир табылганды чынжыры менен текшерүү
//    ФАЗА E  Offsets'ке жазуу + /data/local/tmp/offsets_found.txt
//
// ───────────────────────────────────────────────────────────────────────
//  КОЛДОНУУ (AttachToGame ичинде, is_attached = true'дан МУРДА):
//      AutoOffset::Run(g_pid, (uintptr_t)libbase, g_scan_mode);
// ═══════════════════════════════════════════════════════════════════════

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>

// ── Scan режимдери (main.cpp ушуларды колдонот) ───────────────────────
#ifndef SCAN_AUTO
#define SCAN_AUTO     1
#define SCAN_HARDCODE 2
#endif

namespace AutoOffset {

// ══════════════════════════════════════════════════════════════════════
//  ЖӨНДӨӨЛӨР
// ══════════════════════════════════════════════════════════════════════

// Il2CppClass структурасынын офсеттери (Unity 2019–2022, ARM64).
//   static_fields = 0xa8 — main.cpp'тагы PtrChain1 / STATIC_FIELD менен бир.
static constexpr uintptr_t KLASS_NAME_OFF   = 0x10;  // const char* name
static constexpr uintptr_t KLASS_NS_OFF     = 0x18;  // const char* namespaze
static constexpr uintptr_t KLASS_STATIC_OFF = 0xa8;  // void* static_fields

// HARD CODE режими ушул файлдан окуйт
inline const char* TXT_PATH = "/data/local/tmp/offsets_found.txt";

// Скандоо чектери
static constexpr size_t MAX_REGION_BYTES = 256ull * 1024 * 1024;
static constexpr size_t READ_CHUNK       = 512 * 1024;
static constexpr size_t MAX_STR_HITS     = 48;   // бир класс аты үчүн

// ══════════════════════════════════════════════════════════════════════
//  1. ЭС ТУТУМ ОКУУ (кернел драйверге тийбейт — түз /proc)
// ══════════════════════════════════════════════════════════════════════

inline int g_memFd = -1;

inline void OpenMem(pid_t pid) {
    if (g_memFd >= 0) { close(g_memFd); g_memFd = -1; }
    char p[64];
    snprintf(p, sizeof(p), "/proc/%d/mem", (int)pid);
    g_memFd = open(p, O_RDONLY | O_CLOEXEC);
}

inline void CloseMem() {
    if (g_memFd >= 0) { close(g_memFd); g_memFd = -1; }
}

// n байт окуу: алгач process_vm_readv, ал болбосо pread64
inline bool RMem(pid_t pid, uintptr_t addr, void* buf, size_t n) {
    if (!addr || !n) return false;
    struct iovec lo = { buf, n };
    struct iovec re = { (void*)addr, n };
    if (process_vm_readv(pid, &lo, 1, &re, 1, 0) == (ssize_t)n)
        return true;
    if (g_memFd >= 0 &&
        pread64(g_memFd, buf, n, (off64_t)addr) == (ssize_t)n)
        return true;
    return false;
}

inline bool RPtr(pid_t pid, uintptr_t addr, uintptr_t& out) {
    uint64_t v = 0;
    if (!RMem(pid, addr, &v, 8)) return false;
    out = (uintptr_t)v;
    return true;
}

// User-space дарек чеги — main.cpp'тагы IsUserAddr() менен бирдей (39-bit VA)
inline bool Valid(uintptr_t a) {
    return a >= 0x10000ull && a < 0x8000000000ull;
}

// Эс тутумдан C-сап окуу (жарактуу ASCII болушу керек)
inline bool RCStr(pid_t pid, uintptr_t addr, char* out, size_t cap) {
    if (!Valid(addr) || cap < 8) return false;
    size_t want = cap - 1;
    if (!RMem(pid, addr, out, want)) {
        want = 40;                                  // регион чегине туш келди
        if (!RMem(pid, addr, out, want)) return false;
    }
    out[want] = 0;
    unsigned char c0 = (unsigned char)out[0];
    if (c0 < 0x21 || c0 > 0x7e) return false;       // басылуучу болушу керек
    return true;
}

// ══════════════════════════════════════════════════════════════════════
//  2. /proc/<pid>/maps ТАЛДОО
// ══════════════════════════════════════════════════════════════════════

struct Region { uintptr_t start = 0; uintptr_t end = 0; };

// Модулдун (libcsharp.so / libil2cpp.so) толук аралыгы
inline bool GetModuleRange(pid_t pid, const char* soName,
                           uintptr_t& outBase, uintptr_t& outEnd) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;

    uintptr_t base = 0, end = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, soName)) continue;
        unsigned long s = 0, e = 0;
        if (sscanf(line, "%lx-%lx", &s, &e) != 2) continue;
        if (!base || (uintptr_t)s < base) base = (uintptr_t)s;
        if ((uintptr_t)e > end)           end  = (uintptr_t)e;
    }
    fclose(f);
    if (!base || end <= base) return false;
    outBase = base;
    outEnd  = end;
    return true;
}

// Класс аттары / Il2CppClass'тар жаткан талапкер региондор
inline std::vector<Region> GetSearchRegions(pid_t pid) {
    std::vector<Region> out;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
    FILE* f = fopen(path, "r");
    if (!f) return out;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        unsigned long s = 0, e = 0;
        char perms[8] = {};
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perms) != 3) continue;
        if (perms[0] != 'r')  continue;              // окулбаса — өткөр
        if (e <= s)           continue;
        size_t sz = (size_t)(e - s);
        if (sz < 0x10000)            continue;       // 64KB'дан кичине
        if (sz > MAX_REGION_BYTES)   continue;       // өтө чоң

        // Керексиз региондорду чыпкалоо (ART, шрифт, apk ж.б.)
        if (strstr(line, "/dev/"))          continue;
        if (strstr(line, "/system/fonts"))  continue;
        if (strstr(line, ".apk"))           continue;
        if (strstr(line, ".oat"))           continue;
        if (strstr(line, ".art"))           continue;
        if (strstr(line, ".odex"))          continue;
        if (strstr(line, ".vdex"))          continue;
        if (strstr(line, ".jar"))           continue;
        if (strstr(line, "dalvik-"))        continue;
        if (strstr(line, "[vectors]"))      continue;
        if (strstr(line, "[vvar]"))         continue;

        out.push_back({ (uintptr_t)s, (uintptr_t)e });
        if (out.size() > 4096) break;
    }
    fclose(f);
    return out;
}

// ══════════════════════════════════════════════════════════════════════
//  3. ИЗДЕЛҮҮЧҮ КЛАССТАР (таблица)
// ══════════════════════════════════════════════════════════════════════

struct Target {
    // ── киргизүү ──
    const char* name    = nullptr;  // класстын аты
    const char* ns      = nullptr;  // namespace (nullptr = каалаганы болот)
    int         instOff = -1;       // static_fields + instOff объект болушу
                                    // керекпи (-1 = текшербе)
    size_t*     apply   = nullptr;  // Offsets'те кайда жазыла турган жери
    const char* txtKey  = nullptr;  // txt файлдагы ачкычы
    bool wantSlot      = true;      // TypeInfo слоту изделсинби
    bool requireStatic = true;      // static_fields болушу шартпы
                                    //  (instance класстарда ал жок)

    // ── натыйжалар ──
    std::vector<uintptr_t> strAddrs;
    uintptr_t   klass   = 0;
    uintptr_t   nameOff = KLASS_NAME_OFF;  // ФАЗА B аныктайт
    uintptr_t   slot    = 0;
    uintptr_t   offset  = 0;
    bool        valid   = false;
    bool        structFound = false;  // ат боюнча эмес, структура боюнча
};

// ── Camera'ны кийинчерээк издөө үчүн абал ────────────────────────────
//  Камера объекти оюн BATTLE'га киргенде гана түзүлөт. AttachToGame()
//  болсо оюн ачылганда эле иштейт — ал кезде меню экраны, камера жок.
//  Ошондуктан табылбай калса бул желекче коюлуп, game_worker ичинен
//  RetryCamera() чакырылат.
inline bool      g_camPending = false;
inline uintptr_t g_camModBase = 0;
inline uintptr_t g_camModEnd  = 0;
inline std::vector<Target> g_camTargets;

// ── Таблицанын өзү ───────────────────────────────────────────────────
//  instOff эскертүүсү: статик поле оюн сабакка кирмейинче 0 болушу
//  мүмкүн. Ошондуктан катуу текшерүү Camera'га гана коюлат — анын
//  аты уникалдуу эмес (көп "Camera" классы болот), ажыратуу керек.
//  Калгандардын аттары уникалдуу, аты боюнча ажыратуу жетиштүү.
inline std::vector<Target> MakeTargets() {
    std::vector<Target> t;

    { Target x;
      x.name   = "BattleManager";
      x.apply  = &Offsets::BattleManager::BasePtr;
      x.txtKey = "BM_BasePtr";
      t.push_back(x); }

    // Камеранын класс аты "GameMethod" — UnityEngine.Camera ЭМЕС.
    //   (MLBB 2.2.16.x dump боюнча тастыкталган)
    //   Чынжыры өзгөчө болгондуктан instOff эмес, DetectCameraChain
    //   текшерет.
    { Target x;
      x.name   = "GameMethod";
      x.apply  = &Offsets::Camera::MainCameraPtr;
      x.txtKey = "CAM_BasePtr";
      t.push_back(x); }

    { Target x;
      x.name   = "SystemData";
      x.apply  = &Offsets::SystemData::BasePtr;
      x.txtKey = "SD_BasePtr";
      t.push_back(x); }

    { Target x;
      x.name   = "LogicBattleManager";
      x.apply  = &Offsets::LogicBattleManager::BasePtr;
      x.txtKey = "LBM_BasePtr";
      t.push_back(x); }

    { Target x;
      x.name   = "GameServerConfig";
      x.apply  = &Offsets::GameServerConfig::TypeInfo;
      x.txtKey = "GSC_TypeInfo";
      t.push_back(x); }

    { Target x;
      x.name   = "GameCommon";
      x.apply  = &Offsets::GameCommon::TypeInfo;
      x.txtKey = "GC_TypeInfo";
      t.push_back(x); }

    // ── FIELD КЛАССТАРЫ ────────────────────────────────────────────
    //  Булардын TypeInfo слоту кереги жок — klass'тын өзү гана керек,
    //  андан FieldInfo массивин окуйбуз. Көбү instance класс, демек
    //  static_fields болбойт (requireStatic = false).
    //
    //  BattleManager / SystemData / LogicBattleManager жогоруда бар,
    //  алардын klass'ы ошол жерден кайра колдонулат.
    {
        const char* fieldClasses[] = {
            "ShowEntity", "ShowPlayer", "ShowCoolDownComp",
            "CoolDownData", "ShowSkillData", "Bullet",
            nullptr
        };
        for (int i = 0; fieldClasses[i]; i++) {
            Target x;
            x.name          = fieldClasses[i];
            x.wantSlot      = false;
            x.requireStatic = false;
            t.push_back(x);
        }
    }

    return t;
}

// ══════════════════════════════════════════════════════════════════════
//  FIELD SCANNER — класстын ичиндеги поле offset'терин авто табуу
//
//  Il2Cpp'де ар бир класстын FieldInfo массиви бар. Ар бир жазуу 0x20
//  байт жана поленин АТЫН да, OFFSET'ин да камтыйт:
//
//      FieldInfo:
//        +0x00  const char*   name     "m_Hp"
//        +0x08  Il2CppType*   type
//        +0x10  Il2CppClass*  parent   ← класстын өзүнө барабар
//        +0x18  int32_t       offset   ← 0x1a4   МЫНА КЕРЕКТҮҮСҮ
//        +0x1C  uint32_t      token
//
//  Демек класс табылса, полени аты боюнча издеп offset'ин түз окуйбуз —
//  болжол жок, dump'тагыдай так. Оюн жаңырып поле жылса да табылат.
//
//  Массивдин klass ичиндеги орду Unity версиясына жараша жылат,
//  ошондуктан КАЛИБРЛӨӨ менен аныкталат: klass+0x58..0xa0 аралыгын
//  сынап, катары менен 3 жазуунун parent'и klass'ка барабар болгон
//  жерди табабыз. Жалган дал келүү иш жүзүндө мүмкүн эмес.
// ══════════════════════════════════════════════════════════════════════

static constexpr uintptr_t FI_NAME   = 0x00;
static constexpr uintptr_t FI_PARENT = 0x10;
static constexpr uintptr_t FI_OFFSET = 0x18;
static constexpr uintptr_t FI_SIZE   = 0x20;
// Бир класстагы поле чеги. MLBB'нин SystemData классы 2048'ден ашат,
// ошондуктан чек кеңири коюлду. Чекке жетсе логдо "+" белгиси чыгат —
// ал массив андан да узун экенин билдирет.
static constexpr int       FI_MAX    = 16384;

// ── fields массивинин klass ичиндеги ордун табуу ────────────────────
inline uintptr_t CalibrateFieldsOffset(pid_t pid, uintptr_t klass) {
    for (uintptr_t fo = 0x58; fo <= 0xa0; fo += 8) {
        uintptr_t fields = 0;
        if (!RPtr(pid, klass + fo, fields)) continue;
        if (!Valid(fields) || (fields & 7))  continue;

        int good = 0;
        for (int i = 0; i < 3; i++) {
            uintptr_t fi = fields + (uintptr_t)i * FI_SIZE;
            uintptr_t namePtr = 0, parent = 0;
            if (!RPtr(pid, fi + FI_NAME,   namePtr)) break;
            if (!Valid(namePtr))                     break;
            if (!RPtr(pid, fi + FI_PARENT, parent))  break;
            if (parent != klass)                     break;   // ← негизги белги
            char nm[64] = {};
            if (!RCStr(pid, namePtr, nm, sizeof(nm))) break;
            good++;
        }
        if (good >= 3) return fo;
    }
    return 0;
}

// ── Класстын бардык полелерин кыдыруу ───────────────────────────────
//    fn(name, offset) — ар бир поле үчүн чакырылат
template <class FN>
inline int ForEachField(pid_t pid, uintptr_t klass, uintptr_t fieldsOff, FN fn) {
    uintptr_t fields = 0;
    if (!RPtr(pid, klass + fieldsOff, fields) || !Valid(fields)) return 0;

    int n = 0;
    for (int i = 0; i < FI_MAX; i++) {
        uintptr_t fi = fields + (uintptr_t)i * FI_SIZE;
        uintptr_t namePtr = 0, parent = 0;
        int32_t   off = 0;
        if (!RPtr(pid, fi + FI_NAME,   namePtr)) break;
        if (!Valid(namePtr))                     break;
        if (!RPtr(pid, fi + FI_PARENT, parent))  break;
        if (parent != klass)                     break;   // массивдин чеги
        if (!RMem(pid, fi + FI_OFFSET, &off, 4)) break;

        char nm[96] = {};
        if (!RCStr(pid, namePtr, nm, sizeof(nm))) continue;
        fn(nm, off);
        n++;
    }
    return n;
}

// ══════════════════════════════════════════════════════════════════════
//  ИЗДЕЛҮҮЧҮ ПОЛЕЛӨР
//
//  names[] — талапкер аттар. MLBB'де көпчүлүк поле "m_" префикси менен
//  (m_Hp, m_ID), бирок баары эмес. Биринчи дал келгени алынат.
// ══════════════════════════════════════════════════════════════════════
struct FieldTarget {
    const char* cls;            // Il2Cpp класс аты
    const char* names[5];       // талапкер аттар, nullptr менен бүтөт
    size_t*     apply;          // Offsets.h'тагы жери
    const char* txtKey;         // txt ачкычы
    // натыйжа
    size_t      found    = 0;
    bool        ok       = false;
    char        hitName[64] = {};
};

inline std::vector<FieldTarget> MakeFieldTargets() {
    std::vector<FieldTarget> f;
    auto A = [&](const char* cls, std::initializer_list<const char*> nm,
                 size_t* ap, const char* key) {
        FieldTarget t{};
        t.cls = cls; t.apply = ap; t.txtKey = key;
        int i = 0;
        for (const char* n : nm) { if (i < 4) t.names[i++] = n; }
        t.names[i] = nullptr;
        f.push_back(t);
    };

    // ── BattleManager ──
    A("BattleManager", {"m_LocalPlayerShow","LocalPlayerShow"},
      &Offsets::BattleManager::LocalPlayerShow, "BM_LocalPlayerShow");
    A("BattleManager", {"m_ShowPlayers","ShowPlayers"},
      &Offsets::BattleManager::ShowPlayers,     "BM_ShowPlayers");
    A("BattleManager", {"m_ShowMonsters","ShowMonsters"},
      &Offsets::BattleManager::ShowMonsters,    "BM_ShowMonsters");
    A("BattleManager", {"m_RunBullets","RunBullets"},
      &Offsets::BattleManager::m_RunBullets,    "BM_RunBullets");

    // ── ShowEntity ──
    A("ShowEntity", {"m_iType","iType"},            &Offsets::ShowEntity::iType,          "SE_iType");
    A("ShowEntity", {"m_Hp","Hp"},                  &Offsets::ShowEntity::Hp,             "SE_Hp");
    A("ShowEntity", {"m_HpMax","HpMax"},            &Offsets::ShowEntity::HpMax,          "SE_HpMax");
    A("ShowEntity", {"m_bDeath","bDeath"},          &Offsets::ShowEntity::bDeath,         "SE_bDeath");
    A("ShowEntity", {"m_bSameCampType","bSameCampType"},
      &Offsets::ShowEntity::bSameCampType, "SE_bSameCampType");
    A("ShowEntity", {"m_vCachePosition","vCachePosition"},
      &Offsets::ShowEntity::vCachePosition, "SE_vCachePosition");
    A("ShowEntity", {"m_ID","m_HeroID","HeroID"},   &Offsets::ShowEntity::HeroID,         "SE_HeroID");
    A("ShowEntity", {"m_uGuid","uGuid"},            &Offsets::ShowEntity::m_uGuid,        "SE_uGuid");
    A("ShowEntity", {"m_Level","Level"},            &Offsets::ShowEntity::Level,          "SE_Level");
    // ⚠ DUMP ачты: бул поле ShowEntity'де ЭМЕС, ShowPlayer'де турат
    //   (ShowPlayer dump: m_iSummonSkillId = 0x984)
    A("ShowPlayer", {"m_iSummonSkillId","m_SummonSkillId","SummonSkillId"},
      &Offsets::ShowEntity::SummonSkillId, "SE_SummonSkillId");
    A("ShowEntity", {"m_ShowCoolDownComp","ShowCoolDownComp"},
      &Offsets::ShowEntity::m_ShowCoolDownComp, "SE_ShowCoolDownComp");
    A("ShowEntity", {"m_RoleName","RoleName"},      &Offsets::ShowEntity::m_RoleName,     "SE_RoleName");

    // ── ShowPlayer ──
    // ⚠ DUMP ачты: бул поле ShowPlayer'де ЭМЕС, ShowEntity'де турат
    //   (ShowEntity dump: m_EntityCampType = 0xd0)
    A("ShowEntity", {"m_EntityCampType","EntityCampType"},
      &Offsets::ShowPlayer::m_EntityCampType, "SP_EntityCampType");
    A("ShowPlayer", {"m_HeroName","HeroName"},      &Offsets::ShowPlayer::m_HeroName,     "SP_HeroName");
    A("ShowPlayer", {"m_KillWildTimes","KillWildTimes"},
      &Offsets::ShowPlayer::m_KillWildTimes, "SP_KillWildTimes");

    // ── SystemData ──
    A("SystemData", {"m_RoomPlayerInfo","RoomPlayerInfo"},
      &Offsets::SystemData::m_RoomPlayerInfo, "SD_RoomPlayerInfo");
    A("SystemData", {"m_uiID","uiID"},              &Offsets::SystemData::m_uiID,         "SD_uiID");
    A("SystemData", {"m_bOfflineManagerRun","bOfflineManagerRun"},
      &Offsets::SystemData::m_bOfflineManagerRun, "SD_bOfflineRun");

    // ── LogicBattleManager ──
    A("LogicBattleManager", {"m_uiFrameTime","uiFrameTime"},
      &Offsets::LogicBattleManager::m_uiFrameTime, "LBM_uiFrameTime");

    // ── ShowCoolDownComp / CoolDownData / ShowSkillData / Bullet ──
    A("ShowCoolDownComp", {"_dicCD","m_dicCD","dicCD"},
      &Offsets::ShowCoolDownComp::_dicCD, "SCC_dicCD");
    A("CoolDownData", {"uiCoolTime","m_uiCoolTime"},
      &Offsets::CoolDownData::uiCoolTime,  "CDD_uiCoolTime");
    A("CoolDownData", {"uiStartTime","m_uiStartTime"},
      &Offsets::CoolDownData::uiStartTime, "CDD_uiStartTime");
    A("ShowSkillData", {"config","m_config","_config"},
      &Offsets::ShowSkillData::config, "SSD_config");
    A("Bullet", {"m_Id","m_ID","Id"},               &Offsets::Bullet::m_Id,        "BUL_Id");
    A("Bullet", {"_bulletPos","m_bulletPos","bulletPos"},
      &Offsets::Bullet::_bulletPos, "BUL_bulletPos");

    // ══════════════════════════════════════════════════════════════
    //  RoomData — АТАЙЫН КОШУЛГАН ЖОК
    //
    //  "RoomPlayerInfo" классы табылды, бирок аттары дал келген эки
    //  полесинин offset'и Offsets.h'тагыдан таптакыр башка чыкты:
    //
    //      uiRankLevel :  Offsets.h 0x120   dump 0x34
    //      iRoad       :  Offsets.h 0x138   dump 0xfc
    //
    //  Демек main.cpp окуп жаткан структура ал ЭМЕС — башка класс.
    //  Жаңылыш класстан жазсак Room Info таб бузулуп калмак,
    //  ошондуктан RoomData сканга кирбейт: эски маанилер кала берет.
    //
    //  Чыныгы классты табуу жолу: SystemData.m_RoomPlayerInfo
    //  dictionary'синин элементинин тибин кароо. Ал аныкталганда
    //  ушул жерге кошулат.
    // ══════════════════════════════════════════════════════════════

    return f;
}

// ══════════════════════════════════════════════════════════════════════
//  ФАЗА A — класс аттарынын сабын БИР сканда табуу
//    Сап таблицасында аттар '\0' менен ажыратылат, ошондуктан
//    "<name>\0" издейбиз жана алдындагы байт '\0' болушун талап кылабыз.
// ══════════════════════════════════════════════════════════════════════
inline void PhaseA_FindStrings(pid_t pid, const std::vector<Region>& regs,
                               std::vector<Target>& tg) {
    struct Pat { std::vector<char> b; size_t len; int ti; };
    std::vector<Pat> pats;
    size_t maxPat = 0;
    for (size_t i = 0; i < tg.size(); i++) {
        size_t nl = strlen(tg[i].name);
        if (!nl || nl > 120) continue;
        Pat p;
        p.b.resize(nl + 1);
        memcpy(p.b.data(), tg[i].name, nl);
        p.b[nl] = '\0';
        p.len = nl + 1;
        p.ti  = (int)i;
        if (p.len > maxPat) maxPat = p.len;
        pats.push_back(std::move(p));
    }
    if (pats.empty()) return;

    // ── ЫЛДАМДЫК: биринчи байт боюнча маска таблицасы ────────────────
    //  Муну колдонбосо, ар бир байт үчүн 6 pattern салыштырылып,
    //  1.5 GB эс тутумда 9 миллиард операция болуп кетет (телефондо
    //  ~30 секунд). Таблица менен байтка 1 издөө гана.
    uint64_t fbMask[256] = {};
    for (size_t pi = 0; pi < pats.size() && pi < 64; pi++)
        fbMask[(uint8_t)pats[pi].b[0]] |= (1ull << pi);

    std::vector<uint8_t> buf(READ_CHUNK + maxPat);

    for (const auto& r : regs) {
        for (uintptr_t cur = r.start; cur < r.end; ) {
            size_t want = (size_t)(r.end - cur);
            if (want > READ_CHUNK) want = READ_CHUNK;
            size_t total = want + maxPat;             // чектешкен жерди камтуу
            if (cur + total > r.end) total = want;

            if (!RMem(pid, cur, buf.data(), total)) { cur += want; continue; }

            const uint8_t* b = buf.data();
            for (size_t i = 0; i < total; i++) {
                uint64_t m = fbMask[b[i]];
                if (!m) continue;                     // ← байттын 99%+ ушунда
                if (i > 0 && b[i - 1] != 0x00) continue;   // сап чеги
                while (m) {
                    size_t pi = (size_t)__builtin_ctzll(m);
                    m &= m - 1;
                    const Pat& p = pats[pi];
                    if (i + p.len > total) continue;
                    if (memcmp(b + i, p.b.data(), p.len) != 0) continue;
                    Target& t = tg[(size_t)p.ti];
                    if (t.strAddrs.size() < MAX_STR_HITS)
                        t.strAddrs.push_back(cur + (uintptr_t)i);
                }
            }
            cur += want;
        }
    }
}

// klass талапкерин текшерүү: аты, namespace, static_fields
inline bool CheckKlass(pid_t pid, uintptr_t klass, const Target& t,
                       uintptr_t nameOff = KLASS_NAME_OFF) {
    if (!Valid(klass) || (klass & 7)) return false;

    uintptr_t namePtr = 0;
    if (!RPtr(pid, klass + nameOff, namePtr))        return false;
    if (!Valid(namePtr))                             return false;

    char nm[128] = {};
    if (!RCStr(pid, namePtr, nm, sizeof(nm)))        return false;
    if (strcmp(nm, t.name) != 0)                     return false;

    if (t.ns) {
        uintptr_t nsPtr = 0;
        // namespace дайыма name'дин артында 8 байт
        if (!RPtr(pid, klass + nameOff + 8, nsPtr))  return false;
        if (nsPtr == 0) {
            if (t.ns[0] != '\0') return false;
        } else {
            char ns[128] = {};
            if (!RCStr(pid, nsPtr, ns, sizeof(ns)))  return false;
            if (strcmp(ns, t.ns) != 0)               return false;
        }
    }

    // static_fields көрсөткүчү (instance класстарда болбойт)
    uintptr_t sf = 0;
    if (t.requireStatic) {
        if (!RPtr(pid, klass + KLASS_STATIC_OFF, sf)) return false;
        if (!Valid(sf))                               return false;
    }

    // Талап кылынса — статик поледеги объектти да текшеребиз
    if (t.instOff >= 0) {
        uintptr_t obj = 0;
        if (!RPtr(pid, sf + (uintptr_t)t.instOff, obj)) return false;
        if (!Valid(obj))                                return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════════════════════
//  ДИАГНОСТИКА — скандоо иштебей калса себебин аныктоого жардам берет
//
//  "Manager" деген сапты издейт. Эгер "UIManager", "SoundManager"
//  сыяктуу аттар чыкса → metadata ачык, демек көйгөй класстын
//  атында (мүмкүн MLBB башкача атаган). Эгер эч нерсе чыкпаса →
//  metadata шифрленген, бул ыкма бул версияда иштебейт.
//
//  Логдо чыккан аттарды мага көрсөтсөң, таблицаны оңдойм.
// ══════════════════════════════════════════════════════════════════════
inline void Diagnose(pid_t pid, const std::vector<Region>& regs) {
    printf("[Diag] 'Manager' sabyn izdep koroboz...\n");
    fflush(stdout);

    const char* needle = "Manager";
    const size_t nlen  = 7;
    int shown = 0;
    std::vector<uint8_t> buf(READ_CHUNK + 64);

    for (const auto& r : regs) {
        if (shown >= 25) break;
        for (uintptr_t cur = r.start; cur < r.end && shown < 25; ) {
            size_t want = (size_t)(r.end - cur);
            if (want > READ_CHUNK) want = READ_CHUNK;
            size_t total = want + 64;
            if (cur + total > r.end) total = want;
            if (!RMem(pid, cur, buf.data(), total)) { cur += want; continue; }

            for (size_t i = 0; i + nlen <= total && shown < 25; i++) {
                if (buf[i] != 'M') continue;
                if (memcmp(buf.data() + i, needle, nlen) != 0) continue;
                // Сапты башынан окуйбуз: артка '\0' чейин кайтабыз
                size_t s = i;
                while (s > 0 && buf[s - 1] >= 0x21 && buf[s - 1] <= 0x7e) s--;
                char nm[96] = {};
                size_t n = 0;
                while (s + n < total && n < sizeof(nm) - 1 &&
                       buf[s + n] >= 0x20 && buf[s + n] <= 0x7e) {
                    nm[n] = (char)buf[s + n];
                    n++;
                }
                nm[n] = 0;
                if (n >= 7 && n < 60) {
                    printf("[Diag]   %s\n", nm);
                    shown++;
                }
                i = s + n;
            }
            cur += want;
        }
    }
    if (!shown) {
        printf("[Diag] Ech nerse tabylbady -> metadata SHIFRLENGEN.\n");
        printf("[Diag] Bul ykma oyundun bul versiyasynda ishtebeyt,\n");
        printf("[Diag] HARD CODE [2] rejimin koldon.\n");
    } else {
        printf("[Diag] Attar achyk turat -> masele klass atynda.\n");
        printf("[Diag] Jogorudagy tizmeni korsot -> tablitsa tuuralanat.\n");
    }
    fflush(stdout);
}

// ══════════════════════════════════════════════════════════════════════
//  ФАЗА B — сап адресине көрсөткөн Il2CppClass'тарды БИР сканда табуу
//    Сап адреси klass + nameOff'то жатат → klass = тапкан адрес - nameOff
// ══════════════════════════════════════════════════════════════════════
inline void PhaseB_FindKlasses(pid_t pid, const std::vector<Region>& regs,
                               std::vector<Target>& tg) {
    struct Key { uint64_t v; int ti; };
    std::vector<Key> keys;
    for (size_t i = 0; i < tg.size(); i++)
        for (uintptr_t a : tg[i].strAddrs)
            keys.push_back({ (uint64_t)a, (int)i });
    if (keys.empty()) return;

    // ── ЫЛДАМДЫК: сорттоо + min/max чеги ─────────────────────────────
    //  Муну колдонбосо ар бир 8-байт маани үчүн ~288 ачкыч кыдырылып,
    //  1.5 GB эс тутумда 54 МИЛЛИАРД операция болот (телефондо
    //  бир нече МИНУТ). Сап адрестери metadata'нын тар аралыгында
    //  жатат, ошондуктан min/max эки салыштыруусу маанилердин
    //  99.9%+ ин дароо кесип салат.
    std::sort(keys.begin(), keys.end(),
              [](const Key& a, const Key& b) { return a.v < b.v; });
    const uint64_t minK = keys.front().v;
    const uint64_t maxK = keys.back().v;

    std::vector<uint8_t> buf(READ_CHUNK);

    for (const auto& r : regs) {
        // Баары табылса — скандоону токтотобуз
        bool allFound = true;
        for (const auto& t : tg) if (!t.klass) { allFound = false; break; }
        if (allFound) break;

        for (uintptr_t cur = r.start; cur < r.end; ) {
            size_t want = (size_t)(r.end - cur);
            if (want > READ_CHUNK) want = READ_CHUNK;
            if (!RMem(pid, cur, buf.data(), want)) { cur += want; continue; }

            size_t n8 = want / 8;
            const uint64_t* p = (const uint64_t*)buf.data();
            for (size_t i = 0; i < n8; i++) {
                uint64_t v = p[i];
                if (v < minK || v > maxK) continue;    // ← негизги чыпка
                // Бинардык издөө (288 ачкычта ~8 кадам)
                size_t lo = 0, hi = keys.size();
                while (lo < hi) {
                    size_t mid = (lo + hi) / 2;
                    if (keys[mid].v < v) lo = mid + 1; else hi = mid;
                }
                for (size_t j = lo; j < keys.size() && keys[j].v == v; j++) {
                    Target& t = tg[(size_t)keys[j].ti];
                    if (t.klass) continue;             // мурда табылган
                    // Il2CppClass'тын name полеси 0x10'до деп күтөбүз,
                    // бирок Unity версиясына жараша жылышы мүмкүн —
                    // үч кадыресе вариантты тең сынап көрөбүз.
                    static const uintptr_t nameOffs[] = { 0x10, 0x08, 0x18 };
                    uintptr_t at = cur + (uintptr_t)(i * 8);
                    for (uintptr_t no : nameOffs) {
                        if (at < no) continue;
                        uintptr_t cand = at - no;
                        if (CheckKlass(pid, cand, t, no)) { t.klass = cand;
                                                            t.nameOff = no; break; }
                    }
                }
            }
            cur += want;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
//  ФАЗА C — klass'тарга көрсөткөн TypeInfo слотторун модулдан табуу
//    Бир скан, модулдун [base, end) аралыгы боюнча
// ══════════════════════════════════════════════════════════════════════
inline void PhaseC_FindSlots(pid_t pid, uintptr_t modBase, uintptr_t modEnd,
                             std::vector<Target>& tg) {
    std::vector<uint8_t> buf(READ_CHUNK);

    for (uintptr_t cur = modBase; cur < modEnd; ) {
        size_t want = (size_t)(modEnd - cur);
        if (want > READ_CHUNK) want = READ_CHUNK;
        if (!RMem(pid, cur, buf.data(), want)) { cur += want; continue; }

        size_t n8 = want / 8;
        const uint64_t* p = (const uint64_t*)buf.data();
        for (size_t i = 0; i < n8; i++) {
            uint64_t v = p[i];
            if (v < 0x10000ull) continue;
            for (auto& t : tg) {
                if (!t.wantSlot) continue;          // field класстары
                if (!t.klass || t.slot) continue;
                if (v != (uint64_t)t.klass) continue;
                t.slot   = cur + (uintptr_t)(i * 8);
                t.offset = t.slot - modBase;
            }
        }
        cur += want;
    }
}

// ══════════════════════════════════════════════════════════════════════
//  ФАЗА C2 — CAMERA СЛОТУН СТРУКТУРА БОЮНЧА ТАБУУ
//
//  Эмне үчүн өзүнчө: чыныгы оюнда "Camera" (UnityEngine) классы
//  табылат, БИРОК анын TypeInfo слоту libcsharp.so'до жок — ал
//  UnityEngine.CoreModule.dll'ге таандык, башка жерде катталган.
//  Ал эми Offsets.h'тагы MainCameraPtr оюндун ӨЗ классына көрсөтөт,
//  анын аты бизге белгисиз.
//
//  Ошондуктан бул жерде АТ боюнча эмес, ЧЫНЖЫРДЫН ФОРМАСЫ боюнча
//  издейбиз (main.cpp'тагы GetMainCamera + ViewMatrix чынжыры):
//
//      klass = Read(slot)
//      sf    = Read(klass + 0xa8)   ← CameraData     (static_fields)
//      obj   = Read(sf    + 0x08)   ← CameraData2
//      cam   = Read(obj   + 0x10)   ← SmoothFollow
//      mtx   = Read(cam   + 0x5C)   ← ViewMatrix: 2 × Matrix4x4 = 128 байт
//
//  Акыркы кадам чечүүчү: эгер ал жерде ЧЫНЫГЫ view/projection матрица
//  турса, бул так ошол класс. Матрицанын белгиси —
//  worldToCameraMatrix'тин m33 элементи 1.0'го барабар.
//
//  Издөө аймагы: табылган башка слоттордун min..max аралыгы + запас.
//  Баары бир эле TypeInfo таблицасында жатат, ошондуктан Camera да
//  ошол жерде.
// ══════════════════════════════════════════════════════════════════════

// Матрица чын элеби?  (32 float = worldToCamera + projection)
//   strict=true  → m33 == 1.0 талап кылынат (так, жалган дал келбейт)
//   strict=false → жөн гана акылга сыярлык float массив (кененирээк)
inline bool LooksLikeViewMatrix(const float* f, bool strict) {
    int nonZero = 0;
    for (int i = 0; i < 32; i++) {
        float v = f[i];
        if (v != v)                  return false;  // NaN
        if (v > 1e18f || v < -1e18f) return false;  // Inf
        if (v > 1e6f  || v < -1e6f)  return false;  // акылга сыйбаган
        if (v != 0.0f) nonZero++;
    }
    if (nonZero < 8) return false;                  // бош аймак

    if (strict) {
        // worldToCameraMatrix'тин m33 элементи Unity'де дайыма 1.0
        float m33 = f[15];
        if (m33 < 0.99f || m33 > 1.01f) return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════════════════════
//  КАМЕРА ЧЫНЖЫРЫН АНЫКТОО
//
//  Оюн версиясына жараша GameMethod'тон камерага чейинки жол 3 же 4
//  кадам болот. Экөөнү тең сынап, ViewMatrix чыныгы матрица болгонун
//  текшеребиз — кайсынысы өтсө, ошол туура:
//
//    4-кадам (2.2.16.x):  sf +0x10 → +0x48 → +0x10 → +0x5C
//    3-кадам (эски):      sf +0x08 →        → +0x10 → +0x5C
//
//  apply=true болсо Offsets::Camera::CameraData2 / CameraExtraStep
//  ушул жерден коюлат — main.cpp'тагы GetMainCamera() ошону окуйт.
// ══════════════════════════════════════════════════════════════════════
inline bool DetectCameraChain(pid_t pid, uintptr_t klass,
                              bool strict, bool apply) {
    uintptr_t sf = 0;
    if (!RPtr(pid, klass + KLASS_STATIC_OFF, sf)) return false;
    if (!Valid(sf)) return false;

    struct Variant { uintptr_t d2, extra; const char* tag; };
    static const Variant vs[] = {
        { 0x10, 0x48, "4-kadam (2.2.16.x)" },
        { 0x08, 0x00, "3-kadam (eski)"     },
    };

    for (const auto& v : vs) {
        uintptr_t p = 0;
        if (!RPtr(pid, sf + v.d2, p) || !Valid(p)) continue;

        if (v.extra) {                              // m_MainCamera кадамы
            uintptr_t q = 0;
            if (!RPtr(pid, p + v.extra, q) || !Valid(q)) continue;
            p = q;
        }

        uintptr_t cam = 0;                          // native Camera
        if (!RPtr(pid, p + 0x10, cam) || !Valid(cam)) continue;

        float mtx[32];                              // 2 × Matrix4x4
        if (!RMem(pid, cam + 0x5C, mtx, sizeof(mtx))) continue;
        if (!LooksLikeViewMatrix(mtx, strict)) continue;

        if (apply) {
            Offsets::Camera::CameraData2     = (size_t)v.d2;
            Offsets::Camera::CameraExtraStep = (size_t)v.extra;
            printf("[AutoScan]     Camera chain: %s"
                   "  (CameraData2=0x%zx ExtraStep=0x%zx)\n",
                   v.tag, (size_t)v.d2, (size_t)v.extra);
            fflush(stdout);
        }
        return true;
    }
    return false;
}

// ── ФАЗА C2'нин кадам статистикасы (диагностика үчүн) ────────────────
struct C2Stats {
    long slots = 0, okKlass = 0, okStatic = 0, okObj = 0, okCam = 0, okMtx = 0;
    void Print() const {
        printf("[AutoScan]     Camera diag: slot=%ld klass=%ld static=%ld"
               " obj=%ld cam=%ld mtx=%ld\n",
               slots, okKlass, okStatic, okObj, okCam, okMtx);
        fflush(stdout);
    }
};

inline bool PhaseC2_FindCameraSlot(pid_t pid, uintptr_t modBase,
                                   uintptr_t modEnd,
                                   const std::vector<Target>& tg,
                                   uintptr_t& outOffset,
                                   char* outName, size_t outNameCap,
                                   bool strict = true,
                                   C2Stats* st = nullptr,
                                   bool quiet = false) {
    outOffset = 0;

    // ── Издөө аймагы: табылган слоттордун min..max + запас ───────────
    uintptr_t lo = 0, hi = 0;
    for (const auto& t : tg) {
        if (!t.slot) continue;
        if (!lo || t.slot < lo) lo = t.slot;
        if (t.slot > hi)        hi = t.slot;
    }
    if (!lo) return false;

    uintptr_t pad = (hi - lo) / 2;
    if (pad < 0x20000) pad = 0x20000;               // эң аз 128 KB
    if (pad > 0x200000) pad = 0x200000;             // эң көп 2 MB
    uintptr_t from = (lo > modBase + pad) ? (lo - pad) : modBase;
    uintptr_t to   = (hi + pad < modEnd)  ? (hi + pad) : modEnd;

    // ⚠ 8-БАЙТ ЧЕГИНЕ ТЕГИЗДӨӨ — МИЛДЕТТҮҮ!
    //   pad = (hi-lo)/2 ар дайым 8ге бөлүнө бербейт. Тегизделбесе
    //   бардык 64-биттик маанилер көрсөткүчтөрдүн ОРТОСУНАН окулуп,
    //   таштанды болуп чыгат — эч бир klass табылбайт.
    //   (Чыныгы оюнда так ушул болгон: from % 8 == 4)
    from &= ~(uintptr_t)7;
    to   &= ~(uintptr_t)7;
    if (to <= from) return false;

    if (!quiet) {
        printf("[AutoScan]     Camera: struktura boyuncha izdoo"
               " 0x%zx..0x%zx (%zu KB) strict=%d\n",
               from - modBase, to - modBase,
               (size_t)((to - from) / 1024), (int)strict);
        fflush(stdout);
    }

    std::vector<uint8_t> buf(READ_CHUNK);

    for (uintptr_t cur = from; cur < to; ) {
        size_t want = (size_t)(to - cur);
        if (want > READ_CHUNK) want = READ_CHUNK;
        if (!RMem(pid, cur, buf.data(), want)) { cur += want; continue; }

        size_t n8 = want / 8;
        const uint64_t* p = (const uint64_t*)buf.data();
        for (size_t i = 0; i < n8; i++) {
            if (st) st->slots++;
            uintptr_t klass = (uintptr_t)p[i];
            if (!Valid(klass) || (klass & 7)) continue;
            if (st) st->okKlass++;

            // 1) static_fields — класстардын көбүндө 0, тез чыпкалайт
            uintptr_t sf = 0;
            if (!RPtr(pid, klass + KLASS_STATIC_OFF, sf)) continue;
            if (!Valid(sf)) continue;
            if (st) st->okStatic++;

            // 2-4) Чынжырды аныктоо — 3 жана 4 кадамдуу эки вариант
            //      тең сыналат, ViewMatrix чыныгы матрица болушу керек.
            if (st) st->okObj++;
            if (!DetectCameraChain(pid, klass, strict, false)) continue;
            if (st) { st->okCam++; st->okMtx++; }

            // ✅ Табылды
            outOffset = cur + (uintptr_t)(i * 8) - modBase;

            // Класстын атын да окуп алабыз — логго чыгаруу үчүн
            if (outName && outNameCap) {
                outName[0] = 0;
                uintptr_t np = 0;
                if (RPtr(pid, klass + KLASS_NAME_OFF, np) && Valid(np))
                    RCStr(pid, np, outName, outNameCap);
            }
            return true;
        }
        cur += want;
    }
    return false;
}

// ══════════════════════════════════════════════════════════════════════
//  ФАЗА F — ПОЛЕ OFFSET'ТЕРИН ТАБУУ
//
//  Класстар ФАЗА B'де табылган. Эми ар биринин FieldInfo массивин
//  калибрлеп, керектүү полелерди аты боюнча издейбиз.
//
//  Табылбаган класс же поле болсо — DUMP жазылат: ошол класстын
//  БАРДЫК полелери аты жана offset'и менен txt'ка чыгат. Ошондон
//  чыныгы атын көрүп, таблицаны тууралоого болот.
// ══════════════════════════════════════════════════════════════════════
inline int ScanFields(pid_t pid, const std::vector<Target>& tg,
                      std::vector<FieldTarget>& ftg,
                      std::string* dumpOut) {
    // класс аты → klass адреси
    auto FindKlass = [&](const char* cls) -> uintptr_t {
        for (const auto& t : tg)
            if (t.name && !strcmp(t.name, cls)) return t.klass;
        return 0;
    };

    // класс аты → fields массивинин offset'и (кэш)
    struct CalEntry { const char* cls; uintptr_t klass, fieldsOff; int count; };
    std::vector<CalEntry> cal;

    auto GetCal = [&](const char* cls) -> CalEntry* {
        for (auto& c : cal) if (!strcmp(c.cls, cls)) return &c;
        CalEntry e{ cls, FindKlass(cls), 0, 0 };
        if (e.klass) {
            e.fieldsOff = CalibrateFieldsOffset(pid, e.klass);
            if (e.fieldsOff)
                e.count = ForEachField(pid, e.klass, e.fieldsOff,
                                       [](const char*, int32_t){});
        }
        cal.push_back(e);
        return &cal.back();
    };

    int okCount = 0;
    for (auto& f : ftg) {
        CalEntry* c = GetCal(f.cls);
        if (!c->klass || !c->fieldsOff) continue;

        ForEachField(pid, c->klass, c->fieldsOff,
            [&](const char* nm, int32_t off) {
                if (f.ok) return;
                if (off < 0 || off > 0x20000) return;
                for (int i = 0; i < 4 && f.names[i]; i++) {
                    if (strcmp(nm, f.names[i]) != 0) continue;
                    f.found = (size_t)off;
                    f.ok    = true;
                    snprintf(f.hitName, sizeof(f.hitName), "%s", nm);
                    return;
                }
            });
        if (f.ok) {
            if (f.apply) *f.apply = f.found;
            okCount++;
        }
    }

    // ── Калибрлөөнүн жыйынтыгы ──
    for (const auto& c : cal) {
        if (!c.klass)
            printf("[Fields]   %-20s X klass tabylbady\n", c.cls);
        else if (!c.fieldsOff)
            printf("[Fields]   %-20s X FieldInfo kalibrlenbedi\n", c.cls);
        else
            printf("[Fields]   %-20s fields@0x%zx  (%d pole%s)\n",
                   c.cls, (size_t)c.fieldsOff, c.count,
                   c.count >= FI_MAX ? "+ CHEKKE JETTI" : "");
    }
    fflush(stdout);

    // ── DUMP: табылбаган полеси бар класстардын баарын жазабыз ──
    if (dumpOut) {
        for (const auto& c : cal) {
            bool need = false;
            for (const auto& f : ftg)
                if (!f.ok && !strcmp(f.cls, c.cls)) { need = true; break; }
            if (!need || !c.klass || !c.fieldsOff) continue;

            char hdr[160];
            snprintf(hdr, sizeof(hdr),
                     "\n// ---- %s : bardyk pole (%d) ----\n", c.cls, c.count);
            *dumpOut += hdr;
            ForEachField(pid, c.klass, c.fieldsOff,
                [&](const char* nm, int32_t off) {
                    char ln[160];
                    snprintf(ln, sizeof(ln), "//   %-34s 0x%x\n", nm, (unsigned)off);
                    *dumpOut += ln;
                });
        }
        // klass такыр табылбаган класстар
        for (const auto& c : cal) {
            if (c.klass) continue;
            char ln[160];
            snprintf(ln, sizeof(ln),
                     "\n// ---- %s : KLASS TABYLBADY (aty bashka bolushu mumkun)\n",
                     c.cls);
            *dumpOut += ln;
        }
    }
    return okCount;
}

// ══════════════════════════════════════════════════════════════════════
//  ФАЗА D — чынжырды текшерүү
//    libbase + offset → klass → (аты) → static_fields → (объект)
// ══════════════════════════════════════════════════════════════════════
inline bool ValidateChain(pid_t pid, uintptr_t libbase, uintptr_t off,
                          const char* wantName, int instOff = -1,
                          uintptr_t nameOff = KLASS_NAME_OFF) {
    if (!off) return false;

    uintptr_t klass = 0;
    if (!RPtr(pid, libbase + off, klass)) return false;
    if (!Valid(klass) || (klass & 7))     return false;

    uintptr_t namePtr = 0;
    if (!RPtr(pid, klass + nameOff, namePtr))        return false;
    if (!Valid(namePtr))                             return false;

    char nm[128] = {};
    if (!RCStr(pid, namePtr, nm, sizeof(nm)))        return false;
    if (wantName && strcmp(nm, wantName) != 0)       return false;

    uintptr_t sf = 0;
    if (!RPtr(pid, klass + KLASS_STATIC_OFF, sf))    return false;
    if (!Valid(sf))                                  return false;

    if (instOff >= 0) {
        uintptr_t obj = 0;
        if (!RPtr(pid, sf + (uintptr_t)instOff, obj)) return false;
        if (!Valid(obj))                              return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════════════════════
//  TXT САКТОО / ЖҮКТӨӨ
// ══════════════════════════════════════════════════════════════════════

inline void SaveTxt(const std::vector<Target>& tg, const char* note,
                    const std::vector<FieldTarget>* ftg = nullptr,
                    const std::string* dump = nullptr) {
    FILE* fp = fopen(TXT_PATH, "w");
    if (!fp) {
        printf("[AutoOffset] Txt jazylbady: %s\n", TXT_PATH);
        fflush(stdout);
        return;
    }
    time_t now = time(nullptr);
    char tb[64] = {};
    strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(fp, "# =========================================\n");
    fprintf(fp, "# MLBB AUTO OFFSET SCAN\n");
    fprintf(fp, "# Ubakyt : %s\n", tb);
    fprintf(fp, "# Ykma   : %s\n", note ? note : "scan");
    fprintf(fp, "# =========================================\n");
    fprintf(fp, "# HARD CODE [2] rejimi ushul fayldan okuyt.\n");
    fprintf(fp, "# Offsets.h'ka kol menen kochursong da bolot.\n");
    fprintf(fp, "# =========================================\n\n");

    for (const auto& t : tg)
        if (t.txtKey)
            fprintf(fp, "%s=0x%zx\n", t.txtKey, t.apply ? *t.apply : (size_t)0);

    // ── Поле offset'тери ──────────────────────────────────────────────
    if (ftg && !ftg->empty()) {
        fprintf(fp, "\n# ---- pole offsettteri ----\n");
        for (const auto& f : *ftg)
            fprintf(fp, "%s=0x%zx%s\n", f.txtKey,
                    f.apply ? *f.apply : (size_t)0,
                    f.ok ? "" : "   # TABYLBADY");
    }

    // ══════════════════════════════════════════════════════════════════
    //  КӨЧҮРҮҮГӨ ДАЯР КОД — HARD CODE долбоорго түз көчүрүү үчүн
    //  Санды кол менен терүүнүн кереги жок: төмөнкү блокторду
    //  көчүрүп, өз долбооруңдагыларды алмаштыр.
    // ══════════════════════════════════════════════════════════════════
    auto G = [&](const char* key) -> size_t {
        for (const auto& t : tg)
            if (t.txtKey && !strcmp(t.txtKey, key))
                return t.apply ? *t.apply : (size_t)0;
        return 0;
    };

    fprintf(fp, "\n\n");
    fprintf(fp, "// ==================================================\n");
    fprintf(fp, "//  1) config/Offsets.h  — ushul 4 sandy almashtyr\n");
    fprintf(fp, "// ==================================================\n");
    fprintf(fp, "namespace BattleManager {\n");
    fprintf(fp, "    const size_t BasePtr = 0x%zx;\n", G("BM_BasePtr"));
    fprintf(fp, "}\n");
    fprintf(fp, "namespace Camera {\n");
    fprintf(fp, "    const size_t MainCameraPtr   = 0x%zx;  // GameMethod\n",
            G("CAM_BasePtr"));
    fprintf(fp, "    const size_t CameraData      = 0xa8;\n");
    fprintf(fp, "    const size_t CameraData2     = 0x%zx;\n",
            (size_t)Offsets::Camera::CameraData2);
    fprintf(fp, "    const size_t CameraExtraStep = 0x%zx;\n",
            (size_t)Offsets::Camera::CameraExtraStep);
    fprintf(fp, "    const size_t SmoothFollow    = 0x10;\n");
    fprintf(fp, "    const size_t ViewMatrix      = 0x5C;\n");
    fprintf(fp, "    const size_t v2              = 0xc8;\n");
    fprintf(fp, "}\n");
    fprintf(fp, "namespace SystemData {\n");
    fprintf(fp, "    const size_t BasePtr = 0x%zx;\n", G("SD_BasePtr"));
    fprintf(fp, "}\n");
    fprintf(fp, "namespace LogicBattleManager {\n");
    fprintf(fp, "    const size_t BasePtr = 0x%zx;\n", G("LBM_BasePtr"));
    fprintf(fp, "}\n\n");

    fprintf(fp, "// ==================================================\n");
    fprintf(fp, "//  2) main.cpp  — ushul eki #define'di almashtyr\n");
    fprintf(fp, "// ==================================================\n");
    fprintf(fp, "#define GAMESERVERCONFIG_TYPEINFO  0x%zx\n", G("GSC_TypeInfo"));
    fprintf(fp, "#define GAMECOMMON_TYPEINFO        0x%zx\n\n", G("GC_TypeInfo"));

    fprintf(fp, "// ==================================================\n");
    fprintf(fp, "//  3) main.cpp  — GetMainCamera() TOLUGU MENEN almashtyr\n");
    fprintf(fp, "//     (CameraExtraStep kadamy bolboso ESP chykpayt!)\n");
    fprintf(fp, "// ==================================================\n");
    fprintf(fp, "uintptr_t GetMainCamera() {\n");
    fprintf(fp, "    auto klass = Read<uintptr_t>(libbase + OFF_CAM(MainCameraPtr));\n");
    fprintf(fp, "    if (!klass) return 0;\n");
    fprintf(fp, "    auto sf = Read<uintptr_t>(klass + OFF_CAM(CameraData));\n");
    fprintf(fp, "    if (!sf) return 0;\n");
    fprintf(fp, "    auto p = Read<uintptr_t>(sf + OFF_CAM(CameraData2));\n");
    fprintf(fp, "    if (!p) return 0;\n");
    if (Offsets::Camera::CameraExtraStep) {
        fprintf(fp, "    p = Read<uintptr_t>(p + OFF_CAM(CameraExtraStep));\n");
        fprintf(fp, "    if (!p) return 0;\n");
    }
    fprintf(fp, "    return p;\n");
    fprintf(fp, "}\n");

    // ── DUMP: табылбаган полелер болсо, класстын баарын көрсөтөбүз ──
    if (dump && !dump->empty()) {
        fprintf(fp, "\n\n");
        fprintf(fp, "// ==================================================\n");
        fprintf(fp, "//  DUMP — tabylbagan poleler uchun\n");
        fprintf(fp, "//  Tomondogu tizmeden chynygy atyn tabyp, tablitsany\n");
        fprintf(fp, "//  tuurala (AutoOffset.h -> MakeFieldTargets).\n");
        fprintf(fp, "// ==================================================\n");
        fputs(dump->c_str(), fp);
    }

    fclose(fp);
    chmod(TXT_PATH, 0666);
    printf("[AutoOffset] Saktaldy -> %s\n", TXT_PATH);
    fflush(stdout);
}

// txt'тан окуп, түздөн-түз Offsets'ке жазат
inline bool LoadTxt(std::vector<Target>& tg,
                    std::vector<FieldTarget>* ftg = nullptr) {
    FILE* fp = fopen(TXT_PATH, "r");
    if (!fp) return false;

    int got = 0;
    char line[192];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char k[96] = {}, v[96] = {};
        if (sscanf(line, "%95[^=]=%95s", k, v) != 2) continue;
        uintptr_t x = (uintptr_t)strtoull(v, nullptr, 16);
        if (!x) continue;
        bool hit = false;
        for (auto& t : tg) {
            if (!t.txtKey || strcmp(k, t.txtKey) != 0) continue;
            if (t.apply) { *t.apply = (size_t)x; got++; }
            hit = true;
            break;
        }
        if (hit || !ftg) continue;
        for (auto& f : *ftg) {
            if (!f.txtKey || strcmp(k, f.txtKey) != 0) continue;
            if (f.apply) { *f.apply = (size_t)x; got++; }
            break;
        }
    }
    fclose(fp);
    return got > 0;
}

// ══════════════════════════════════════════════════════════════════════
//  НЕГИЗГИ ФУНКЦИЯ
//
//    pid      — оюндун процесс ID'си
//    libbase  — main.cpp'тагы libbase (libcsharp.so / libil2cpp.so базасы)
//    scanMode — SCAN_AUTO же SCAN_HARDCODE
//
//    true  → offset'тер колдонулду
//    false → Offsets.h'тагы эски маанилер кала берди
// ══════════════════════════════════════════════════════════════════════
inline bool Run(pid_t pid, uintptr_t libbase, int scanMode) {

    if (pid <= 0 || !libbase) {
        printf("[AutoOffset] pid je libbase jok\n");
        fflush(stdout);
        return false;
    }

    std::vector<Target> tg = MakeTargets();
    OpenMem(pid);

    // ─────────────────────────────────────────────────────────────────
    //  HARD CODE режим — txt'тан окуу
    // ─────────────────────────────────────────────────────────────────
    if (scanMode == SCAN_HARDCODE) {
        std::vector<FieldTarget> ftg = MakeFieldTargets();
        if (!LoadTxt(tg, &ftg)) {
            printf("[HardCode] Fayl tabylbady: %s\n", TXT_PATH);
            printf("[HardCode] Algach AUTO SCAN [1] rejimde bir jolu ishtet!\n");
            fflush(stdout);
            CloseMem();
            return false;
        }
        printf("[HardCode] OK\n");
        for (const auto& t : tg)
            if (t.txtKey)
                printf("[HardCode]   %-18s = 0x%zx\n",
                       t.txtKey, t.apply ? *t.apply : (size_t)0);
        int fl = 0;
        for (const auto& f : ftg) if (f.apply && *f.apply != 0xDEAD) fl++;
        printf("[HardCode]   + %d pole offset juktoldu\n", fl);
        fflush(stdout);
        CloseMem();
        return true;
    }

    // ─────────────────────────────────────────────────────────────────
    //  AUTO SCAN режим
    // ─────────────────────────────────────────────────────────────────
    printf("[AutoScan] pid=%d libbase=0x%lx\n",
           (int)pid, (unsigned long)libbase);
    fflush(stdout);

    // ── ФАЗА 0: азыркы маанилер дагы иштейби? ────────────────────────
    //  Бул жерде Il2CppClass'тын name полесинин ордун да аныктап
    //  алабыз: үч кадыресе вариантты сынап, иштегенин t.nameOff'ко
    //  жазабыз — ФАЗА D ошону колдонот.
    {
        static const uintptr_t nameOffs[] = { 0x10, 0x08, 0x18 };
        int okCount = 0;
        for (auto& t : tg) {
            if (!t.apply) continue;
            for (uintptr_t no : nameOffs) {
                if (ValidateChain(pid, libbase, (uintptr_t)*t.apply,
                                  t.name, t.instOff, no)) {
                    t.nameOff = no;
                    okCount++;
                    break;
                }
            }
        }
        // Слот класстарынын саны (field класстарында apply жок)
        int slotCount = 0;
        for (const auto& t : tg) if (t.apply) slotCount++;
        if (okCount == slotCount) {
            printf("[AutoScan] Slottor tuura — birok pole offsetteri"
                   " baary bir tekshirilet\n");
            for (const auto& t : tg)
                printf("[AutoScan]   %-18s = 0x%zx\n", t.txtKey, *t.apply);
            fflush(stdout);
            // ЭСКЕРТҮҮ: мурда бул жерде return болчу. Эми поле
            // offset'тери да изделгендиктен скандоону улантабыз —
            // слоттор туура болсо да поле жылган болушу мүмкүн.
        } else {
            printf("[AutoScan] Slottor eskirgen (%d/%d tuura)\n",
                   okCount, slotCount);
        }
        fflush(stdout);
    }

    // ── Модулдун чектери ─────────────────────────────────────────────
    uintptr_t modBase = 0, modEnd = 0;
    if (!GetModuleRange(pid, "libcsharp.so", modBase, modEnd))
        GetModuleRange(pid, "libil2cpp.so", modBase, modEnd);

    if (!modBase || modEnd <= modBase) {
        printf("[AutoScan] Modul aralygy tabylbady\n");
        fflush(stdout);
        CloseMem();
        return false;
    }
    // Offset libbase'ден эсептелиши керек (main.cpp ошону колдонот)
    if (modBase != libbase) {
        printf("[AutoScan] ESKERTUU: modBase=0x%lx != libbase=0x%lx\n",
               (unsigned long)modBase, (unsigned long)libbase);
        modBase = libbase;
    }
    printf("[AutoScan] Modul: 0x%lx - 0x%lx (%.1f MB)\n",
           (unsigned long)modBase, (unsigned long)modEnd,
           (double)(modEnd - modBase) / (1024.0 * 1024.0));
    fflush(stdout);

    std::vector<Region> regs = GetSearchRegions(pid);
    printf("[AutoScan] Izdoo regiondoru: %zu\n", regs.size());
    fflush(stdout);
    if (regs.empty()) {
        printf("[AutoScan] FAIL — region jok\n");
        fflush(stdout);
        CloseMem();
        return false;
    }

    // ── ФАЗА A ───────────────────────────────────────────────────────
    printf("[AutoScan] [A] klass attary izdeluudo...\n");
    fflush(stdout);
    PhaseA_FindStrings(pid, regs, tg);
    for (const auto& t : tg)
        printf("[AutoScan]     %-20s sap: %zu\n", t.name, t.strAddrs.size());
    fflush(stdout);

    if (tg[0].strAddrs.empty()) {
        printf("[AutoScan] FAIL — 'BattleManager' saby tabylbady.\n");
        Diagnose(pid, regs);
        fflush(stdout);
        CloseMem();
        return false;
    }

    // ── ФАЗА B ───────────────────────────────────────────────────────
    printf("[AutoScan] [B] Il2CppClass izdeluudo...\n");
    fflush(stdout);
    PhaseB_FindKlasses(pid, regs, tg);
    for (const auto& t : tg)
        printf("[AutoScan]     %-20s klass: 0x%lx\n",
               t.name, (unsigned long)t.klass);
    fflush(stdout);

    if (!tg[0].klass) {
        printf("[AutoScan] FAIL — BattleManager klassy tabylbady\n");
        fflush(stdout);
        CloseMem();
        return false;
    }

    // ── ФАЗА C ───────────────────────────────────────────────────────
    printf("[AutoScan] [C] TypeInfo slottoru izdeluudo...\n");
    fflush(stdout);
    PhaseC_FindSlots(pid, modBase, modEnd, tg);

    // ── ФАЗА C2: Camera слотун структура боюнча издөө ────────────────
    for (auto& t : tg) {
        if (!t.txtKey || strcmp(t.txtKey, "CAM_BasePtr") != 0) continue;

        // (а) "GameMethod" аты боюнча ФАЗА A/B/C таап койгонбу? —
        //     анда чынжырдын вариантын гана аныктап алабыз.
        if (t.offset) {
            uintptr_t k = 0;
            if (RPtr(pid, libbase + t.offset, k) &&
                DetectCameraChain(pid, k, false, true)) {
                printf("[AutoScan]     Camera: 'GameMethod' aty boyuncha OK\n");
            } else {
                printf("[AutoScan]     Camera: klass tabyldy, birok chynjyr"
                       " azyrynsha uzuk\n");
                printf("[AutoScan]     -> battle bashtalganda kayra tekshert\n");
                g_camPending = true;
                g_camModBase = modBase;
                g_camModEnd  = modEnd;
            }
            fflush(stdout);
            break;
        }

        // (б) Ат боюнча табылбады — структура боюнча издейбиз
        char camName[96] = {};
        uintptr_t camOff = 0;
        C2Stats st;

        // 1-аракет: катуу валидация (m33 == 1.0)
        bool ok = PhaseC2_FindCameraSlot(pid, modBase, modEnd, tg, camOff,
                                         camName, sizeof(camName), true, &st);
        // 2-аракет: жумшак валидация
        if (!ok) {
            st.Print();
            ok = PhaseC2_FindCameraSlot(pid, modBase, modEnd, tg, camOff,
                                        camName, sizeof(camName), false);
        }
        // 3-аракет: эски версиядагы BM->Camera аралыгы (0x390) боюнча болжол
        if (!ok) {
            uintptr_t bmOff = 0;
            for (const auto& b : tg)
                if (b.txtKey && !strcmp(b.txtKey, "BM_BasePtr")) bmOff = b.offset;
            if (bmOff) {
                uintptr_t guess = bmOff + 0x390;
                if (ValidateChain(pid, libbase, guess, nullptr, 0x08)) {
                    camOff = guess;
                    ok = true;
                    printf("[AutoScan]     Camera: boljol boyuncha"
                           " (BM + 0x390)\n");
                }
            }
        }

        if (ok) {
            t.offset      = camOff;
            t.structFound = true;
            printf("[AutoScan]     Camera: TABYLDY offset=0x%zx  klass='%s'\n",
                   camOff, camName[0] ? camName : "?");
            // Чынжырдын кайсы вариант экенин аныктап, Offsets'ке жазабыз
            uintptr_t k = 0;
            if (RPtr(pid, libbase + camOff, k))
                DetectCameraChain(pid, k, false, true);
        } else {
            // Эң ыктымалдуу себеп: skan BATTLE'ga chejin ishtedi —
            // kamera objekti azyrynsha tuzulo elek. game_worker ichinde
            // RetryCamera() aryly kayra araket kylabyz.
            g_camPending = true;
            g_camModBase = modBase;
            g_camModEnd  = modEnd;
            printf("[AutoScan]     Camera: azyrynsha jok"
                   " (oyun battle'da emes okshoyt)\n");
            printf("[AutoScan]     -> battle bashtalganda avto kayra"
                   " izdeyt\n");
        }
        fflush(stdout);
        break;
    }

    // ── ФАЗА D ───────────────────────────────────────────────────────
    int applied = 0;
    for (auto& t : tg) {
        if (!t.offset) {
            printf("[AutoScan]     %-20s X slot tabylbady\n", t.name);
            continue;
        }
        t.valid = ValidateChain(pid, libbase, t.offset,
                                t.structFound ? nullptr : t.name,
                                t.instOff, t.nameOff);
        printf("[AutoScan]     %-20s offset=0x%-10zx valid=%d\n",
               t.name, t.offset, (int)t.valid);
        // Текшерүүдөн өткөндөрдү гана колдонобуз — эски маани
        // тастыкталбаган жаңыдан ишенимдүүрөөк.
        if (t.valid && t.apply) {
            *t.apply = (size_t)t.offset;
            applied++;
        }
    }
    fflush(stdout);

    if (!applied)
        printf("[AutoScan] ESKERTUU — slot tastyktalbady, Offsets.h kala beret\n");
    fflush(stdout);

    // ── ФАЗА F: ПОЛЕ OFFSET'ТЕРИ ─────────────────────────────────────
    printf("[AutoScan] [F] Pole offsetteri izdeluudo...\n");
    fflush(stdout);
    std::vector<FieldTarget> ftg = MakeFieldTargets();
    std::string dump;
    int fOk = ScanFields(pid, tg, ftg, &dump);
    printf("[AutoScan] [F] %d/%zu pole tabyldy\n", fOk, ftg.size());
    for (const auto& f : ftg)
        if (!f.ok)
            printf("[AutoScan]     X %-18s (%s)\n", f.txtKey, f.cls);
    fflush(stdout);

    // ── ФАЗА E ───────────────────────────────────────────────────────
    // Txt'ка Offsets'тин АЗЫРКЫ абалын жазабыз: табылгандар жаңы,
    // табылбагандар эски маанини сактап калат.
    SaveTxt(tg, "auto scan (typeinfo slot + fields)", &ftg,
            dump.empty() ? nullptr : &dump);
    g_camTargets = tg;          // RetryCamera ушуну колдонот

    printf("[AutoScan] Buttu! %d slot + %d pole janyrtyldy\n", applied, fOk);
    fflush(stdout);
    CloseMem();
    return true;
}

// ══════════════════════════════════════════════════════════════════════
//  RetryCamera — BATTLE башталганда Camera'ны кайра издөө
//
//  Эмне үчүн керек: AttachToGame() оюн процесси табылганда ДАРОО
//  иштейт — ал кезде оюнчу логин/меню экранында турат жана камера
//  объекти (static_fields + 0x08) азырынча NULL. Чынжыр үзүк болуп,
//  структуралык издөө эч нерсе таппайт.
//
//  Battle башталганда камера түзүлөт — ошондо бул функция табат.
//  main.cpp'тагы game_worker ичинен 3 секунд сайын чакырылат.
//
//  Табылганда желекче өчөт жана txt кайра жазылат — кийинки жолу
//  HARD CODE режими даяр маанини окуйт.
//
//  true  → Camera табылды (же издөөнүн кереги жок)
//  false → азырынча жок, кийин кайра аракет кылуу керек
// ══════════════════════════════════════════════════════════════════════
inline bool RetryCamera(pid_t pid, uintptr_t libbase) {
    if (!g_camPending)                 return true;   // кереги жок
    if (pid <= 0 || !libbase)          return false;
    if (!g_camModBase || !g_camModEnd) return false;

    OpenMem(pid);

    // (а) Offset мурда табылган болсо (GameMethod аты боюнча), жөн гана
    //     чынжырды кайра текшеребиз — battle'да ал эми иштеши мүмкүн.
    {
        uintptr_t curOff = (uintptr_t)Offsets::Camera::MainCameraPtr;
        uintptr_t k = 0;
        if (curOff && RPtr(pid, libbase + curOff, k) &&
            DetectCameraChain(pid, k, false, true)) {
            g_camPending = false;
            printf("[AutoScan] Camera chynjyry ishtedi (battle)!"
                   " offset=0x%zx\n", (size_t)curOff);
            if (!g_camTargets.empty())
                SaveTxt(g_camTargets, "auto scan + camera chain (battle)");
            fflush(stdout);
            CloseMem();
            return true;
        }
    }

    // (б) Offset такыр жок — структура боюнча издейбиз
    char camName[96] = {};
    uintptr_t camOff = 0;

    // Катуу, анан жумшак валидация (quiet — фондо иштейт, лог баспайт)
    bool ok = PhaseC2_FindCameraSlot(pid, g_camModBase, g_camModEnd,
                                     g_camTargets, camOff, camName,
                                     sizeof(camName), true, nullptr, true);
    if (!ok)
        ok = PhaseC2_FindCameraSlot(pid, g_camModBase, g_camModEnd,
                                    g_camTargets, camOff, camName,
                                    sizeof(camName), false, nullptr, true);

    if (!ok) { CloseMem(); return false; }

    // Акыркы текшерүү: чынжыр толук иштейби + вариантын аныктоо
    {
        uintptr_t k = 0;
        if (!RPtr(pid, libbase + camOff, k) ||
            !DetectCameraChain(pid, k, false, true)) {
            CloseMem();
            return false;
        }
    }

    Offsets::Camera::MainCameraPtr = (size_t)camOff;
    g_camPending = false;

    printf("[AutoScan] Camera TABYLDY (battle)! offset=0x%zx klass='%s'\n",
           camOff, camName[0] ? camName : "?");

    // txt'ты жаңыртабыз — HARD CODE режими даяр маанини окусун
    if (!g_camTargets.empty())
        SaveTxt(g_camTargets, "auto scan + camera (battle)");

    fflush(stdout);
    CloseMem();
    return true;
}

} // namespace AutoOffset
