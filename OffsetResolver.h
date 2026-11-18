#pragma once
// ══════════════════════════════════════════════════════════════════════════
//  OffsetResolver — офсеттерди авто-табуу
//
//  МАСЕЛЕ: оюн жаңыланганда libcsharp.so кайра курулат жана статикалык
//  талаалардын даректери жылат. Ошондо долбоор иштебей калат.
//
//  ЧЕЧИМ: старттда чынжырды ТЕКШЕРҮҮ. Иштесе — эч нерсе кылбайбыз
//  (нөл чыгым). Иштебесе — .bss аймагын скандап, туура базаны өзү
//  табат жана /data/local/tmp/offsets.cfg файлына сактайт.
//
//  ТАРТИП:
//     1. Offsets.h ичиндеги заводдук маанилер
//     2. offsets.cfg бар болсо — үстүнөн жазат (кол менен оңдоого)
//     3. Чынжырды текшерүү  ──иштесе──→  БҮТТҮ, скан жок
//     4. иштебесе → чектелген скан
//     5. тапса → cfg'ге сактайт, кийинки старт 3-кадамда бүтөт
//
//  ❗ ЧАКЫРУУЧУГА ТАЛАП: бул файл g_DriverMutex'ти көрбөйт (ал main.cpp
//     ичинде static). Ошондуктан Resolve() чакырылганда чакыруучу
//     DRIVER_LOCK() кармап турушу КЕРЕК. AttachToGame() аны мурдатан
//     кармайт; UI баскычы өзүнчө кармашы керек.
//
//  ЧЕКТӨӨ: бул 4 БАЗАНЫ гана чечет (BattleManager, LogicBattleManager,
//  Camera, SystemData). Класс ичиндеги талаа офсеттери (Hp, HeroID ж.б.)
//  автоматтык табылбайт — бирок алар сейрек өзгөрөт жана offsets.cfg
//  аркылуу кайра курбай эле оңдолот.
// ══════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <unistd.h>

#include "Memory.h"
#include "../config/Offsets.h"

namespace OffsetResolver {

inline constexpr const char* CFG_PATH = "/data/local/tmp/offsets.cfg";

// ══════════════════════════════════════════════════════════════════════
//  КООПСУЗДУК ЧЕКТЕРИ
//
//  Драйверди тынымсыз чакыруу мурда kernel panic кылган. Ошондуктан:
//    • текшерилүүчү талапкердин саны чектелген
//    • ар N талапкерден кийин ядрого дем берилет
// ══════════════════════════════════════════════════════════════════════
inline constexpr uint64_t MAX_CANDIDATES  = 40000;  // жалпы чек
inline constexpr uint64_t YIELD_EVERY     = 256;    // ар ушунчадан кийин уйку
inline constexpr useconds_t YIELD_US      = 300;    // уйкунун узундугу
inline constexpr size_t   CHUNK           = 256 * 1024;

// ══════════════════════════════════════════════════════════════════════
//  Абал — UI'де көрсөтүү үчүн
// ══════════════════════════════════════════════════════════════════════
enum class Level { None, Chain, Lobby, Match };

struct Status {
    bool     ran        = false;
    bool     ok         = false;
    bool     didScan    = false;
    bool     fromCfg    = false;
    bool     capped     = false;   // талапкер чегине жеттиби
    Level    level      = Level::None;
    uint64_t candidates = 0;
    double   scanMs     = 0.0;
    char     msg[200]   = "Иштей элек";
};
inline Status g_Status;

inline const char* LevelName(Level l) {
    switch (l) {
        case Level::Match: return "MATCH (катуу)";
        case Level::Lobby: return "LOBBY (орточо)";
        case Level::Chain: return "CHAIN (алсыз)";
        default:           return "жок";
    }
}

// ══════════════════════════════════════════════════════════════════════
//  Жардамчылар
// ══════════════════════════════════════════════════════════════════════

// 39-bit VA — бул түзүлүштө жарактуу user дарек
static inline bool IsPtr(uint64_t a) {
    return a >= 0x10000ULL && a < 0x8000000000ULL;
}

static inline uint64_t RdPtr(uint64_t a) {
    if (!IsPtr(a)) return 0;
    return (uint64_t)Memory::Read<uintptr_t>((uintptr_t)a);
}

static inline int RdInt(uint64_t a) {
    if (!IsPtr(a)) return 0;
    return Memory::Read<int>((uintptr_t)a);
}

// ══════════════════════════════════════════════════════════════════════
//  /proc/PID/maps — libcsharp.so'нун rw-p (.data/.bss) аймагы
//  il2cpp статикалык талааларын ошол жерде сактайт (~10 МБ).
// ══════════════════════════════════════════════════════════════════════
struct Region { uint64_t start = 0, end = 0; };

inline bool FindDataRegion(pid_t pid, Region& out) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "libcsharp.so") && !strstr(line, "libil2cpp.so")) continue;

        unsigned long long s = 0, e = 0;
        char perm[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &s, &e, perm) != 3) continue;

        if (perm[0] == 'r' && perm[1] == 'w') {
            if ((e - s) > (out.end - out.start)) {   // эң чоң rw-p
                out.start = (uint64_t)s;
                out.end   = (uint64_t)e;
            }
        }
    }
    fclose(f);
    return out.end > out.start;
}

// ══════════════════════════════════════════════════════════════════════
//  ТЕКШЕРГИЧТЕР — үч деңгээл
//
//  Chain : чынжыр жарактуу көрсөткүчтөрдөн турабы (алсыз, жалган дал
//          келүү ыктымал — скан үчүн ЖАРАБАЙТ)
//  Lobby : + ShowPlayers тизмеси бар жана өлчөмү акылга сыярлык.
//          Матчтан СЫРТЫНДА да иштейт. Скан үчүн ушул колдонулат.
//  Match : + чыныгы оюнчу жана HP диапазондо. Эң ишенимдүү, бирок
//          матчта гана иштейт.
// ══════════════════════════════════════════════════════════════════════

inline bool ValidateBM(uint64_t staticAddr, Level lvl) {
    uint64_t a1  = RdPtr(staticAddr);              if (!IsPtr(a1))  return false;
    uint64_t a2  = RdPtr(a1 + OFF_BM(PtrChain1));  if (!IsPtr(a2))  return false;
    uint64_t a32 = RdPtr(a2);                      if (!IsPtr(a32)) return false;
    if (lvl == Level::Chain) return true;

    // ── LOBBY: ShowPlayers тизмеси
    uint64_t sp = RdPtr(a32 + OFF_BM(ShowPlayers));
    if (!IsPtr(sp)) return false;

    int cnt = RdInt(sp + OFF_BM(ListCountOffset));
    if (cnt < 0 || cnt > 32) return false;          // 0 = лоббиде нормалдуу

    uint64_t data = RdPtr(sp + OFF_BM(ListDataOffset));
    if (!IsPtr(data)) return false;

    // ShowMonsters дагы болушу керек — жалган дал келүүнү кескин азайтат
    uint64_t sm = RdPtr(a32 + OFF_BM(ShowMonsters));
    if (!IsPtr(sm)) return false;

    if (lvl == Level::Lobby) return true;

    // ── MATCH: чыныгы оюнчу
    uint64_t self = RdPtr(a32 + OFF_BM(LocalPlayerShow));
    if (!IsPtr(self)) return false;

    int hp  = RdInt(self + OFF_SE(Hp));
    int hpm = RdInt(self + OFF_SE(HpMax));
    if (hp  <= 0 || hp  > 500000) return false;
    if (hpm <  hp || hpm > 500000) return false;
    if (cnt < 1) return false;

    return true;
}

inline bool ValidateSimpleChain(uint64_t staticAddr, size_t chainOff) {
    uint64_t a1 = RdPtr(staticAddr);        if (!IsPtr(a1)) return false;
    uint64_t a2 = RdPtr(a1 + chainOff);     if (!IsPtr(a2)) return false;
    return true;
}

inline bool ValidateCamera(uint64_t staticAddr) {
    uint64_t cam = RdPtr(staticAddr);                 if (!IsPtr(cam)) return false;
    uint64_t d   = RdPtr(cam + OFF_CAM(CameraData));  if (!IsPtr(d))   return false;
    return true;
}

// Баарын бир жолу текшерүү. Эң жогорку өткөн деңгээлди кайтарат.
inline Level ValidateAllLevel(uint64_t libbase) {
    if (!ValidateCamera(libbase + OFF_CAM(MainCameraPtr)))                     return Level::None;
    if (!ValidateSimpleChain(libbase + OFF_SD(BasePtr),  OFF_SD(PtrChain1)))   return Level::None;
    if (!ValidateSimpleChain(libbase + OFF_BLC(BasePtr), OFF_BLC(PtrChain1)))  return Level::None;

    if (ValidateBM(libbase + OFF_BM(BasePtr), Level::Match)) return Level::Match;
    if (ValidateBM(libbase + OFF_BM(BasePtr), Level::Lobby)) return Level::Lobby;
    return Level::None;
}

// ══════════════════════════════════════════════════════════════════════
//  СКАН
//
//  Аймакты 256 КБ бөлүктөп окуйт (драйверге бир жолу чоң суроо
//  жибербейбиз). Эстутумда жергиликтүү чыпкалайт, андан кийин гана
//  талапкерди драйвер аркылуу текшерет. Талапкер саны чектелген жана
//  ар 256 текшерүүдөн кийин ядрого дем берилет.
// ══════════════════════════════════════════════════════════════════════
inline bool ReadChunk(uint64_t addr, void* buf, size_t len) {
    return pvm(reinterpret_cast<void*>((uintptr_t)addr), buf, len, false);
}

template <typename Fn>
inline uint64_t ScanRange(uint64_t from, uint64_t to, Fn validator,
                          uint64_t& tried, uint64_t budget, bool& capped) {
    if (to <= from || budget == 0) return 0;

    std::vector<uint8_t> buf(CHUNK);

    for (uint64_t off = from; off < to; off += CHUNK) {
        size_t len = (size_t)((to - off) < (uint64_t)CHUNK ? (to - off) : (uint64_t)CHUNK);
        if (!ReadChunk(off, buf.data(), len)) continue;   // окулбаса — өтүп кетебиз

        const size_t n = len / 8;
        for (size_t i = 0; i < n; i++) {
            uint64_t v;
            memcpy(&v, buf.data() + i * 8, 8);           // тегиздөө коопсуз

            // ── 1-баскыч: жергиликтүү чыпка. Драйверге ТИЙБЕЙТ.
            if (!IsPtr(v)) continue;

            // ── 2-баскыч: драйвер аркылуу толук текшерүү
            if (tried >= budget) { capped = true; return 0; }
            if ((tried % YIELD_EVERY) == (YIELD_EVERY - 1)) usleep(YIELD_US);
            tried++;

            if (validator(off + (uint64_t)i * 8)) return off + (uint64_t)i * 8;
        }
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════════
//  КОНФИГ ФАЙЛЫ (key=value — долбоордогу settings форматындай)
// ══════════════════════════════════════════════════════════════════════

inline bool SaveConfig() {
    FILE* f = fopen(CFG_PATH, "w");
    if (!f) return false;
    fprintf(f, "# MLBB offsets - OffsetResolver avto-jazgan\n");
    fprintf(f, "# Kol menen da ondoso bolot. Kayra kuruunun keregi jok.\n");
    fprintf(f, "BattleManager.BasePtr=0x%zx\n",         OFF_BM(BasePtr));
    fprintf(f, "LogicBattleManager.BasePtr=0x%zx\n",    OFF_BLC(BasePtr));
    fprintf(f, "Camera.MainCameraPtr=0x%zx\n",          OFF_CAM(MainCameraPtr));
    fprintf(f, "SystemData.BasePtr=0x%zx\n",            OFF_SD(BasePtr));
    fprintf(f, "ShowEntity.Hp=0x%zx\n",                 OFF_SE(Hp));
    fprintf(f, "ShowEntity.HpMax=0x%zx\n",              OFF_SE(HpMax));
    fprintf(f, "ShowEntity.HeroID=0x%zx\n",             OFF_SE(HeroID));
    fprintf(f, "ShowEntity.Level=0x%zx\n",              OFF_SE(Level));
    fprintf(f, "ShowEntity.bDeath=0x%zx\n",             OFF_SE(bDeath));
    fprintf(f, "ShowEntity.bSameCampType=0x%zx\n",      OFF_SE(bSameCampType));
    fprintf(f, "ShowEntity.vCachePosition=0x%zx\n",     OFF_SE(vCachePosition));
    fprintf(f, "BattleManager.ShowPlayers=0x%zx\n",     OFF_BM(ShowPlayers));
    fprintf(f, "BattleManager.ShowMonsters=0x%zx\n",    OFF_BM(ShowMonsters));
    fprintf(f, "BattleManager.LocalPlayerShow=0x%zx\n", OFF_BM(LocalPlayerShow));
    fclose(f);
    return true;
}

inline bool LoadConfig() {
    FILE* f = fopen(CFG_PATH, "r");
    if (!f) return false;

    char line[256];
    int applied = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* key = line;
        size_t val = (size_t)strtoull(eq + 1, nullptr, 0);
        if (val == 0) continue;

        #define OR_APPLY(k, target) if (strcmp(key, k) == 0) { target = val; applied++; continue; }
        OR_APPLY("BattleManager.BasePtr",         Offsets::BattleManager::BasePtr)
        OR_APPLY("LogicBattleManager.BasePtr",    Offsets::LogicBattleManager::BasePtr)
        OR_APPLY("Camera.MainCameraPtr",          Offsets::Camera::MainCameraPtr)
        OR_APPLY("SystemData.BasePtr",            Offsets::SystemData::BasePtr)
        OR_APPLY("ShowEntity.Hp",                 Offsets::ShowEntity::Hp)
        OR_APPLY("ShowEntity.HpMax",              Offsets::ShowEntity::HpMax)
        OR_APPLY("ShowEntity.HeroID",             Offsets::ShowEntity::HeroID)
        OR_APPLY("ShowEntity.Level",              Offsets::ShowEntity::Level)
        OR_APPLY("ShowEntity.bDeath",             Offsets::ShowEntity::bDeath)
        OR_APPLY("ShowEntity.bSameCampType",      Offsets::ShowEntity::bSameCampType)
        OR_APPLY("ShowEntity.vCachePosition",     Offsets::ShowEntity::vCachePosition)
        OR_APPLY("BattleManager.ShowPlayers",     Offsets::BattleManager::ShowPlayers)
        OR_APPLY("BattleManager.ShowMonsters",    Offsets::BattleManager::ShowMonsters)
        OR_APPLY("BattleManager.LocalPlayerShow", Offsets::BattleManager::LocalPlayerShow)
        #undef OR_APPLY
    }
    fclose(f);
    return applied > 0;
}

// ══════════════════════════════════════════════════════════════════════
//  НЕГИЗГИ КИРҮҮ ЧЕКИТИ
//
//  ❗ Чакыруучу DRIVER_LOCK() кармап турушу керек.
//
//  allowScan=false : текшерүү гана (attach учурунда — тез, чыгымсыз)
//  allowScan=true  : керек болсо скан да жүргүзөт (UI баскычы)
// ══════════════════════════════════════════════════════════════════════
inline bool Resolve(uint64_t libbase, pid_t pid, bool allowScan) {
    Status st;
    st.ran = true;

    if (!libbase || pid <= 0) {
        snprintf(st.msg, sizeof(st.msg), "libbase je pid jok");
        g_Status = st;
        return false;
    }

    // ── 1. Сакталган конфиг (бир жолу гана)
    static bool s_CfgTried = false;
    if (!s_CfgTried) { s_CfgTried = true; st.fromCfg = LoadConfig(); }
    else             { st.fromCfg = g_Status.fromCfg; }

    // ── 2. Азыркы маанилер иштейби? (эң ылдам жол)
    Level lv = ValidateAllLevel(libbase);
    if (lv != Level::None) {
        st.ok = true; st.level = lv;
        snprintf(st.msg, sizeof(st.msg),
                 "Ofsetter jaraktuu [%s]. Skan kerek emes.", LevelName(lv));
        g_Status = st;
        return true;
    }

    if (!allowScan) {
        snprintf(st.msg, sizeof(st.msg),
                 "Ofsetter JARAKSYZ. Matchka kirip FIND OFFSETS bas.");
        g_Status = st;
        return false;
    }

    // ── 3. СКАН
    auto t0 = std::chrono::steady_clock::now();
    st.didScan = true;

    Region rg;
    if (!FindDataRegion(pid, rg)) {
        snprintf(st.msg, sizeof(st.msg), "rw-p aymagy tabylgan jok");
        g_Status = st;
        return false;
    }

    // Матчта болсок катуу текшергич, болбосо lobby деңгээли.
    // Катуу текшергич жалган дал келүүнү дээрлик нөлгө түшүрөт.
    const Level scanLvl = Level::Lobby;
    auto bmValidator = [scanLvl](uint64_t a) { return ValidateBM(a, scanLvl); };

    // Базалар дээрлик дайыма аймактын аягында (өлчөнгөн: аягына 11 КБ).
    // Ошондуктан алгач соңку 1 МБ, табылбаса калганы.
    uint64_t hit = 0;
    uint64_t tailFrom = (rg.end > rg.start + 1024 * 1024)
                      ? (rg.end - 1024 * 1024) : rg.start;

    hit = ScanRange(tailFrom, rg.end, bmValidator,
                    st.candidates, MAX_CANDIDATES, st.capped);
    if (!hit && !st.capped && tailFrom > rg.start)
        hit = ScanRange(rg.start, tailFrom, bmValidator,
                        st.candidates, MAX_CANDIDATES, st.capped);

    auto t1 = std::chrono::steady_clock::now();
    st.scanMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (!hit) {
        snprintf(st.msg, sizeof(st.msg),
                 "Skan tappady (%llu talapker, %.0f ms%s).",
                 (unsigned long long)st.candidates, st.scanMs,
                 st.capped ? ", chekke jetti" : "");
        g_Status = st;
        return false;
    }

    // ── 4. Табылды
    Offsets::BattleManager::BasePtr = (size_t)(hit - libbase);

    // ── 5. Калган үчөө: мурдагы ара алыстык боюнча болжоп, текшеребиз.
    //      Алар .bss ичинде катар жатат жана аралыгы көбүнчө сакталат.
    const size_t bm = Offsets::BattleManager::BasePtr;
    struct Guess { size_t* target; size_t delta; bool isCam; };
    Guess guesses[] = {
        { &Offsets::LogicBattleManager::BasePtr, 0x348,  false },
        { &Offsets::Camera::MainCameraPtr,       0x390,  true  },
        { &Offsets::SystemData::BasePtr,         0x18F8, false },
    };

    for (auto& g : guesses) {
        size_t cand = bm + g.delta;
        bool ok = g.isCam ? ValidateCamera(libbase + cand)
                          : ValidateSimpleChain(libbase + cand, 0xa8);
        if (ok) { *g.target = cand; continue; }

        // Аралык сакталбаптыр — өзүнчө, кичине бюджет менен издейбиз
        uint64_t tried2 = 0; bool cap2 = false;
        uint64_t from = (rg.end > rg.start + 256 * 1024)
                      ? (rg.end - 256 * 1024) : rg.start;
        uint64_t h2 = g.isCam
            ? ScanRange(from, rg.end, [](uint64_t a){ return ValidateCamera(a); },
                        tried2, MAX_CANDIDATES / 4, cap2)
            : ScanRange(from, rg.end, [](uint64_t a){ return ValidateSimpleChain(a, 0xa8); },
                        tried2, MAX_CANDIDATES / 4, cap2);
        st.candidates += tried2;
        if (h2) *g.target = (size_t)(h2 - libbase);
    }

    st.ok    = true;
    st.level = ValidateAllLevel(libbase);
    SaveConfig();

    snprintf(st.msg, sizeof(st.msg),
             "SKAN IYGILIKTUU: BM=0x%zx (%llu talapker, %.0f ms). Saktaldy.",
             Offsets::BattleManager::BasePtr,
             (unsigned long long)st.candidates, st.scanMs);
    g_Status = st;
    return true;
}

} // namespace OffsetResolver
