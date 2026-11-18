#pragma once
// ══════════════════════════════════════════════════════════════════════════
//  OffsetResolver — офсеттерди авто-табуу
//
//  МАСЕЛЕ: оюн жаңыланганда libcsharp.so кайра курулат жана статикалык
//  талаалардын даректери жылат. Ошондо долбоор иштебей калат.
//
//  ЧЕЧИМ: старттда чынжырды ТЕКШЕРҮҮ. Иштесе — эч нерсе кылбайбыз
//  (эч кандай чыгым). Иштебесе — .bss аймагын скандап, туура базаны
//  өзү табат жана /data/local/tmp/offsets.cfg файлына сактайт.
//
//  ИШТӨӨ ТАРТИБИ:
//     1. Offsets.h ичиндеги заводдук маанилер
//     2. offsets.cfg бар болсо — үстүнөн жазат (кол менен оңдоо үчүн)
//     3. Чынжырды текшерүү  ──иштесе──→  БҮТТҮ, скан жок
//     4. иштебесе → авто-скан
//     5. тапса → cfg'ге сактайт, кийинки старт 3-кадамда бүтөт
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

#include "Memory.h"
#include "../config/Offsets.h"

namespace OffsetResolver {

// ── Конфиг файлынын жайгашкан жери
static const char* CFG_PATH = "/data/local/tmp/offsets.cfg";

// ══════════════════════════════════════════════════════════════════════
//  Абал — UI'де көрсөтүү үчүн
// ══════════════════════════════════════════════════════════════════════
struct Status {
    bool     ran        = false;   // резолвер иштедиби
    bool     ok         = false;   // офсеттер жарактуубу
    bool     didScan    = false;   // скан жүрдүбү (же текшерүү эле жетиштүүбү)
    bool     strict     = false;   // катуу текшерүүдөн өттүбү (матчта)
    bool     fromCfg    = false;   // cfg файлдан жүктөлдүбү
    uint64_t candidates = 0;       // канча талапкер каралды
    double   scanMs     = 0.0;     // скан канча убакыт алды
    char     msg[160]   = "Иштей элек";
};
inline Status g_Status;

// ══════════════════════════════════════════════════════════════════════
//  Жардамчылар
// ══════════════════════════════════════════════════════════════════════

// 39-bit VA: жарактуу user дарек
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
//
//  Ошол аймакта il2cpp статикалык талааларын сактайт. Өлчөмү ~10 МБ,
//  ал эми бизге керектүү 4 база анын эң соңку бир нече КБ бөлүгүндө.
// ══════════════════════════════════════════════════════════════════════
struct Region { uint64_t start = 0, end = 0; };

inline bool FindDataRegion(pid_t pid, Region& out) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "libcsharp.so") && !strstr(line, "libil2cpp.so")) continue;

        uint64_t s = 0, e = 0;
        char perm[8] = {0};
        if (sscanf(line, "%llx-%llx %7s",
                   (unsigned long long*)&s, (unsigned long long*)&e, perm) != 3) continue;

        // rw-p = жазыла турган маалымат сегменти
        if (perm[0] == 'r' && perm[1] == 'w') {
            // эң чоңун алабыз (бир нече rw-p болушу мүмкүн)
            if ((e - s) > (out.end - out.start)) { out.start = s; out.end = e; }
            found = true;
        }
    }
    fclose(f);
    return found && out.end > out.start;
}

// ══════════════════════════════════════════════════════════════════════
//  ТЕКШЕРГИЧТЕР
//
//  loose : чынжыр жарактуу көрсөткүчтөрдөн турабы (лоббиде да иштейт)
//  strict: анын үстүнө чыныгы оюнчу маалыматы барбы (матчта гана)
// ══════════════════════════════════════════════════════════════════════

inline bool ValidateBM(uint64_t staticAddr, bool strict) {
    uint64_t a1 = RdPtr(staticAddr);                       if (!IsPtr(a1))  return false;
    uint64_t a2 = RdPtr(a1 + OFF_BM(PtrChain1));           if (!IsPtr(a2))  return false;
    uint64_t a32 = RdPtr(a2);                              if (!IsPtr(a32)) return false;

    // ShowPlayers тизмеси — лоббиде да болушу керек
    uint64_t sp = RdPtr(a32 + OFF_BM(ShowPlayers));
    if (!IsPtr(sp)) return false;

    if (!strict) return true;

    // ── Катуу текшерүү: чыныгы оюнчу жана акылга сыярлык HP
    uint64_t self = RdPtr(a32 + OFF_BM(LocalPlayerShow));
    if (!IsPtr(self)) return false;

    int hp  = RdInt(self + OFF_SE(Hp));
    int hpm = RdInt(self + OFF_SE(HpMax));
    if (hp <= 0 || hp > 500000)     return false;
    if (hpm < hp || hpm > 500000)   return false;

    // Оюнчулар тизмеси акылга сыярлык узундуктабы
    uint32_t cnt = (uint32_t)RdInt(sp + OFF_BM(ListCountOffset));
    if (cnt < 1 || cnt > 32) return false;

    uint64_t data = RdPtr(sp + OFF_BM(ListDataOffset));
    if (!IsPtr(data)) return false;

    return true;
}

inline bool ValidateSimpleChain(uint64_t staticAddr, size_t chainOff) {
    uint64_t a1 = RdPtr(staticAddr);                 if (!IsPtr(a1)) return false;
    uint64_t a2 = RdPtr(a1 + chainOff);              if (!IsPtr(a2)) return false;
    return true;
}

inline bool ValidateCamera(uint64_t staticAddr) {
    uint64_t cam = RdPtr(staticAddr);                if (!IsPtr(cam)) return false;
    uint64_t d   = RdPtr(cam + OFF_CAM(CameraData)); if (!IsPtr(d))   return false;
    return true;
}

// Бардыгын бир жолу текшерүү
inline bool ValidateAll(uint64_t libbase, bool strict) {
    if (!ValidateBM(libbase + OFF_BM(BasePtr), strict)) return false;
    if (!ValidateCamera(libbase + OFF_CAM(MainCameraPtr))) return false;
    if (!ValidateSimpleChain(libbase + OFF_SD(BasePtr),  OFF_SD(PtrChain1)))  return false;
    if (!ValidateSimpleChain(libbase + OFF_BLC(BasePtr), OFF_BLC(PtrChain1))) return false;
    return true;
}

// ══════════════════════════════════════════════════════════════════════
//  СКАН
//
//  Аймакты 256 КБ бөлүктөп окуйт (драйверге бир жолу чоң суроо
//  жибербейбиз — ал kernel panic'ке алып келет). Эстутумда жергиликтүү
//  чыпкалайт, андан кийин талапкерлерди гана драйвер аркылуу текшерет.
// ══════════════════════════════════════════════════════════════════════
static constexpr size_t CHUNK = 256 * 1024;

// Бир бөлүктү окуйт. Ийгиликсиз болсо false.
// pvm() колдонулат — ал долбоордун окуу режимин (userspace / kernel driver)
// урматтайт жана bool кайтарат (fshy() ушуну колдонот).
inline bool ReadChunk(uint64_t addr, void* buf, size_t len) {
    return pvm(reinterpret_cast<void*>((uintptr_t)addr), buf, len, false);
}

// Берилген диапазондо текшергичтен өткөн биринчи даректи табат.
// validator: абсолюттук static даректи алат.
template <typename Fn>
inline uint64_t ScanRange(uint64_t from, uint64_t to, Fn validator, uint64_t& tried) {
    if (to <= from) return 0;

    std::vector<uint8_t> buf(CHUNK);

    for (uint64_t off = from; off < to; off += CHUNK) {
        size_t len = (size_t)((to - off) < CHUNK ? (to - off) : CHUNK);
        if (!ReadChunk(off, buf.data(), len)) continue;   // окулбаса — өтүп кетебиз

        const uint64_t* p = reinterpret_cast<const uint64_t*>(buf.data());
        const size_t    n = len / 8;

        for (size_t i = 0; i < n; i++) {
            // ── 1-баскыч: жергиликтүү чыпка (драйверге ТИЙБЕЙТ, ылдам)
            if (!IsPtr(p[i])) continue;

            // ── 2-баскыч: толук текшерүү (драйвер окуусу)
            uint64_t staticAddr = off + (uint64_t)i * 8;
            tried++;
            if (validator(staticAddr)) return staticAddr;
        }
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════════
//  КОНФИГ ФАЙЛЫ  (key=value, долбоордогу settings форматындай)
// ══════════════════════════════════════════════════════════════════════

inline bool SaveConfig() {
    FILE* f = fopen(CFG_PATH, "w");
    if (!f) return false;
    fprintf(f, "# MLBB offsets - OffsetResolver авто-жазган\n");
    fprintf(f, "# Кол менен да оңдосо болот. Кайра куруунун кереги жок.\n");
    fprintf(f, "BattleManager.BasePtr=0x%zx\n",      OFF_BM(BasePtr));
    fprintf(f, "LogicBattleManager.BasePtr=0x%zx\n", OFF_BLC(BasePtr));
    fprintf(f, "Camera.MainCameraPtr=0x%zx\n",       OFF_CAM(MainCameraPtr));
    fprintf(f, "SystemData.BasePtr=0x%zx\n",         OFF_SD(BasePtr));
    // талаа офсеттери — кол менен оңдоо үчүн
    fprintf(f, "ShowEntity.Hp=0x%zx\n",              OFF_SE(Hp));
    fprintf(f, "ShowEntity.HpMax=0x%zx\n",           OFF_SE(HpMax));
    fprintf(f, "ShowEntity.HeroID=0x%zx\n",          OFF_SE(HeroID));
    fprintf(f, "ShowEntity.Level=0x%zx\n",           OFF_SE(Level));
    fprintf(f, "ShowEntity.bDeath=0x%zx\n",          OFF_SE(bDeath));
    fprintf(f, "ShowEntity.bSameCampType=0x%zx\n",   OFF_SE(bSameCampType));
    fprintf(f, "ShowEntity.vCachePosition=0x%zx\n",  OFF_SE(vCachePosition));
    fprintf(f, "BattleManager.ShowPlayers=0x%zx\n",  OFF_BM(ShowPlayers));
    fprintf(f, "BattleManager.ShowMonsters=0x%zx\n", OFF_BM(ShowMonsters));
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
        if (line[0] == '#' || line[0] == '\n') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* key = line;
        size_t val = (size_t)strtoull(eq + 1, nullptr, 0);
        if (val == 0) continue;

        #define APPLY(k, target) if (strcmp(key, k) == 0) { target = val; applied++; continue; }
        APPLY("BattleManager.BasePtr",         Offsets::BattleManager::BasePtr)
        APPLY("LogicBattleManager.BasePtr",    Offsets::LogicBattleManager::BasePtr)
        APPLY("Camera.MainCameraPtr",          Offsets::Camera::MainCameraPtr)
        APPLY("SystemData.BasePtr",            Offsets::SystemData::BasePtr)
        APPLY("ShowEntity.Hp",                 Offsets::ShowEntity::Hp)
        APPLY("ShowEntity.HpMax",              Offsets::ShowEntity::HpMax)
        APPLY("ShowEntity.HeroID",             Offsets::ShowEntity::HeroID)
        APPLY("ShowEntity.Level",              Offsets::ShowEntity::Level)
        APPLY("ShowEntity.bDeath",             Offsets::ShowEntity::bDeath)
        APPLY("ShowEntity.bSameCampType",      Offsets::ShowEntity::bSameCampType)
        APPLY("ShowEntity.vCachePosition",     Offsets::ShowEntity::vCachePosition)
        APPLY("BattleManager.ShowPlayers",     Offsets::BattleManager::ShowPlayers)
        APPLY("BattleManager.ShowMonsters",    Offsets::BattleManager::ShowMonsters)
        APPLY("BattleManager.LocalPlayerShow", Offsets::BattleManager::LocalPlayerShow)
        #undef APPLY
    }
    fclose(f);
    return applied > 0;
}

// ══════════════════════════════════════════════════════════════════════
//  НЕГИЗГИ КИРҮҮ ЧЕКИТИ
//
//  AttachToGame() ичинен, libbase табылгандан КИЙИН чакырылат.
// ══════════════════════════════════════════════════════════════════════
inline bool Resolve(uint64_t libbase, pid_t pid) {
    g_Status = Status{};
    g_Status.ran = true;

    if (!libbase || pid <= 0) {
        snprintf(g_Status.msg, sizeof(g_Status.msg), "libbase же pid жок");
        return false;
    }

    // ── 1. Сакталган конфиг (кол менен оңдоого жана мурдагы скандын жыйынтыгы)
    g_Status.fromCfg = LoadConfig();

    // ── 2. Азыркы маанилер иштейби? (эң ылдам жол — скан такыр керек эмес)
    if (ValidateAll(libbase, true)) {
        g_Status.ok = true; g_Status.strict = true;
        snprintf(g_Status.msg, sizeof(g_Status.msg),
                 "Офсеттер жарактуу (катуу текшерүү). Скан керек эмес.");
        return true;
    }
    if (ValidateAll(libbase, false)) {
        g_Status.ok = true; g_Status.strict = false;
        snprintf(g_Status.msg, sizeof(g_Status.msg),
                 "Офсеттер жарактуу (жумшак). Матчта кайра текшерилет.");
        return true;
    }

    // ── 3. Иштебейт → СКАН
    auto t0 = std::chrono::steady_clock::now();
    g_Status.didScan = true;

    Region rg;
    if (!FindDataRegion(pid, rg)) {
        snprintf(g_Status.msg, sizeof(g_Status.msg), "rw-p аймагы табылган жок");
        return false;
    }

    auto bmValidator = [](uint64_t a) { return ValidateBM(a, true); };

    // Биринчи соңку 512 КБ (базалар дээрлик дайыма ошол жерде —
    // өлчөнгөн: аймактын аягына 11 КБ калганда). Табылбаса — толук аймак.
    uint64_t hit = 0;
    uint64_t tailFrom = (rg.end > rg.start + 512 * 1024) ? (rg.end - 512 * 1024) : rg.start;

    hit = ScanRange(tailFrom, rg.end, bmValidator, g_Status.candidates);
    if (!hit && tailFrom > rg.start)
        hit = ScanRange(rg.start, tailFrom, bmValidator, g_Status.candidates);

    auto t1 = std::chrono::steady_clock::now();
    g_Status.scanMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (!hit) {
        snprintf(g_Status.msg, sizeof(g_Status.msg),
                 "Скан таппады (%llu талапкер, %.0f ms). Матчта кайра аракет кыл.",
                 (unsigned long long)g_Status.candidates, g_Status.scanMs);
        return false;
    }

    // ── 4. Табылды → BattleManager жаңыртылат
    Offsets::BattleManager::BasePtr = (size_t)(hit - libbase);

    // ── 5. Калган үчөө: мурдагы ара алыстык боюнча болжоп, текшеребиз.
    //      Алар .bss ичинде катар жатат жана аралыгы көбүнчө сакталат.
    //      Туура келбесе — ар бирин өзүнчө издейбиз.
    const size_t bm = Offsets::BattleManager::BasePtr;
    struct Guess { size_t* target; size_t delta; int kind; };  // kind: 0=chain 1=camera
    Guess guesses[] = {
        { &Offsets::LogicBattleManager::BasePtr, 0x348,  0 },
        { &Offsets::Camera::MainCameraPtr,       0x390,  1 },
        { &Offsets::SystemData::BasePtr,         0x18F8, 0 },
    };

    for (auto& g : guesses) {
        size_t cand = bm + g.delta;
        bool ok = (g.kind == 1)
                ? ValidateCamera(libbase + cand)
                : ValidateSimpleChain(libbase + cand, 0xa8);
        if (ok) { *g.target = cand; continue; }

        // Аралык сакталбаптыр — өзүнчө издейбиз
        uint64_t h2 = 0;
        uint64_t dummy = 0;
        uint64_t from = (rg.end > rg.start + 512 * 1024) ? (rg.end - 512 * 1024) : rg.start;
        if (g.kind == 1) {
            h2 = ScanRange(from, rg.end, [](uint64_t a){ return ValidateCamera(a); }, dummy);
        } else {
            h2 = ScanRange(from, rg.end, [](uint64_t a){ return ValidateSimpleChain(a, 0xa8); }, dummy);
        }
        if (h2) *g.target = (size_t)(h2 - libbase);
    }

    g_Status.ok     = true;
    g_Status.strict = true;
    SaveConfig();

    snprintf(g_Status.msg, sizeof(g_Status.msg),
             "СКАН ИЙГИЛИКТҮҮ: BM=0x%zx (%llu талапкер, %.0f ms). Сакталды.",
             Offsets::BattleManager::BasePtr,
             (unsigned long long)g_Status.candidates, g_Status.scanMs);
    return true;
}

} // namespace OffsetResolver
