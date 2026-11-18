// ══════════════════════════════════════════════════════════════════════════
//  offsetdumper_mlbb - MLBB офсеттерин табуучу өзүнчө курал
//
//  1-БАСКЫЧ: базаларды табуу (чынжыр боюнча, артка карай скан)
//  2-БАСКЫЧ: талаа офсеттерин ФОРМА боюнча табуу
//
//  2-баскычтын мааниси: катталган офсеттерге таянбайт. Оюн класстын
//  түзүлүшүн өзгөрткөндө да Hp, HeroID, Position ж.б. өзү табылат.
//
//  ТУРУКТУУ деп эсептелгендер (il2cpp/.NET'тин өз түзүлүшү):
//     PtrChain1 = 0xa8   (Il2CppClass -> static_fields)
//     List+0x10 items,  List+0x18 size,  Array+0x20 биринчи элемент
//
//  МААНИЛҮҮ: кернел драйвер ТАКЫР колдонулбайт. Баары process_vm_readv
//  (root) аркылуу. Оюндун эс тутумуна ЭЧ НЕРСЕ ЖАЗЫЛБАЙТ.
//
//  Куруу:
//    cd offsetdumper_mlbb/jni
//    $HOME/android-ndk-r27c/ndk-build NDK_PROJECT_PATH=.. \
//        APP_BUILD_SCRIPT=./Android.mk APP_ABI=arm64-v8a
//
//  Колдонуу:
//    su -c "/data/local/tmp/offsetdump"          # адаттагыдай
//    su -c "/data/local/tmp/offsetdump -f"       # ofsetter jaraktuu bolso da izde
//
//  ❗ 2-баскыч МАТЧТА иштейт (каармандар тизмеси толук болушу керек).
// ══════════════════════════════════════════════════════════════════════════

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/syscall.h>

// ══════════════════════════════════════════════════════════════════════
//  ТУРУКТУУ (il2cpp/.NET деңгээли)
// ══════════════════════════════════════════════════════════════════════
static const size_t PTR_CHAIN1  = 0xa8;   // Il2CppClass -> static_fields
static const size_t LIST_ITEMS  = 0x10;   // List<T> -> массив
static const size_t LIST_SIZE   = 0x18;   // List<T> -> size (int)
static const size_t ARR_FIRST   = 0x20;   // Array -> биринчи элемент

// ══════════════════════════════════════════════════════════════════════
//  ОФСЕТ ТАБЛИЦАСЫ (заводдук = болжол, табылганда алмашат)
// ══════════════════════════════════════════════════════════════════════
struct Offsets {
    size_t BM_Base     = 0x635b290;
    size_t LBM_Base    = 0x635b5d8;
    size_t CAM_Base    = 0x635b620;
    size_t SD_Base     = 0x635cb88;

    size_t ShowPlayers  = 0x70;
    size_t ShowMonsters = 0x78;
    size_t LocalPlayer  = 0x48;

    size_t SE_Hp        = 0x1a4;
    size_t SE_HpMax     = 0x1a8;
    size_t SE_HeroID    = 0x18c;
    size_t SE_Level     = 0x190;
    size_t SE_Pos       = 0x28c;
    size_t SE_bDeath    = 0xc5;
    size_t SE_bSameCamp = 0x2a9;
};
static Offsets O;

static const size_t D_LBM = 0x348, D_CAM = 0x390, D_SD = 0x18F8;
static const char* CFG_OUT = "/data/local/tmp/offsets.cfg";

// ══════════════════════════════════════════════════════════════════════
//  ЭС ТУТУМ ОКУУ
// ══════════════════════════════════════════════════════════════════════
static pid_t    g_pid   = 0;
static uint64_t g_reads = 0;

#if defined(__aarch64__)
  #define SYS_pvreadv 270
#elif defined(__arm__)
  #define SYS_pvreadv 376
#else
  #define SYS_pvreadv SYS_process_vm_readv
#endif

static bool ReadMem(uint64_t addr, void* buf, size_t len) {
    if (g_pid <= 0 || !len) return false;
    struct iovec lo { buf, len };
    struct iovec re { reinterpret_cast<void*>((uintptr_t)addr), len };
    g_reads++;
    return syscall(SYS_pvreadv, g_pid, &lo, 1UL, &re, 1UL, 0UL) == (long)len;
}

template <typename T> static T Rd(uint64_t a) {
    T v{}; if (!ReadMem(a, &v, sizeof(T))) return T{}; return v;
}

static inline bool IsPtr(uint64_t a) {
    return a >= 0x10000ULL && a < 0x8000000000ULL && (a & 7) == 0;
}
static uint64_t RdPtr(uint64_t a) { return IsPtr(a) ? Rd<uint64_t>(a) : 0; }
static int      RdInt(uint64_t a) { return IsPtr(a) ? Rd<int>(a)      : 0; }

// Жергиликтүү буферден окуу (syscall жок - ылдам)
template <typename T> static T Pk(const uint8_t* b, size_t off) {
    T v{}; memcpy(&v, b + off, sizeof(T)); return v;
}

// ══════════════════════════════════════════════════════════════════════
//  ПРОЦЕСС / MAPS
// ══════════════════════════════════════════════════════════════════════
static pid_t PidOf(const char* name) {
    DIR* d = opendir("/proc"); if (!d) return 0;
    pid_t found = 0; struct dirent* e;
    while ((e = readdir(d))) {
        int pid = atoi(e->d_name); if (pid <= 0) continue;
        char p[64]; snprintf(p, sizeof(p), "/proc/%d/cmdline", pid);
        FILE* f = fopen(p, "r"); if (!f) continue;
        char buf[256] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, f); fclose(f);
        if (n && strcmp(buf, name) == 0) { found = pid; break; }
    }
    closedir(d); return found;
}

struct LibInfo { uint64_t base = 0, rwS = 0, rwE = 0; char path[256] = {0}; };

static bool FindLib(pid_t pid, LibInfo& o) {
    char p[64]; snprintf(p, sizeof(p), "/proc/%d/maps", (int)pid);
    FILE* f = fopen(p, "r"); if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "libcsharp.so") && !strstr(line, "libil2cpp.so")) continue;
        unsigned long long s = 0, e = 0; char perm[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &s, &e, perm) != 3) continue;
        if (o.base == 0) {
            o.base = (uint64_t)s;
            const char* sl = strchr(line, '/');
            if (sl) { snprintf(o.path, sizeof(o.path), "%s", sl);
                      char* nl = strchr(o.path, '\n'); if (nl) *nl = 0; }
        }
        if (perm[0] == 'r' && perm[1] == 'w' && (e - s) > (o.rwE - o.rwS)) {
            o.rwS = (uint64_t)s; o.rwE = (uint64_t)e;
        }
    }
    fclose(f);
    return o.base && o.rwE > o.rwS;
}

// ══════════════════════════════════════════════════════════════════════
//  1-БАСКЫЧ - катталган офсеттер менен текшерүү
// ══════════════════════════════════════════════════════════════════════
enum class Lvl { Chain, Lobby, Match };

static uint64_t ChainA32(uint64_t staticAddr) {
    uint64_t a1 = RdPtr(staticAddr);              if (!IsPtr(a1))  return 0;
    uint64_t a2 = RdPtr(a1 + PTR_CHAIN1);         if (!IsPtr(a2))  return 0;
    uint64_t a32 = RdPtr(a2);                     return IsPtr(a32) ? a32 : 0;
}

static bool ValidateBM(uint64_t sa, Lvl lvl) {
    uint64_t a32 = ChainA32(sa); if (!a32) return false;
    if (lvl == Lvl::Chain) return true;

    uint64_t sp = RdPtr(a32 + O.ShowPlayers);     if (!IsPtr(sp)) return false;
    int cnt = RdInt(sp + LIST_SIZE);
    if (cnt < 0 || cnt > 32) return false;
    if (!IsPtr(RdPtr(sp + LIST_ITEMS))) return false;
    if (!IsPtr(RdPtr(a32 + O.ShowMonsters))) return false;
    if (lvl == Lvl::Lobby) return true;

    uint64_t self = RdPtr(a32 + O.LocalPlayer);   if (!IsPtr(self)) return false;
    int hp = RdInt(self + O.SE_Hp), hpm = RdInt(self + O.SE_HpMax);
    return hp > 0 && hp <= hpm && hpm <= 500000 && cnt >= 1;
}

static bool ValidateChainOnly(uint64_t sa) { return ChainA32(sa) != 0; }

static bool ValidateCam(uint64_t sa) {
    uint64_t c = RdPtr(sa); if (!IsPtr(c)) return false;
    return IsPtr(RdPtr(c + 0xa8));
}

// ══════════════════════════════════════════════════════════════════════
//  АРТКА КАРАЙ СКАН
//  Базалар rw-p аймагынын аягында жатат - аягынан баштаган 800 эсе ылдам.
// ══════════════════════════════════════════════════════════════════════
static const size_t CHUNK = 1024 * 1024;

struct ScanStat { uint64_t cand = 0, chunks = 0, fails = 0; double ms = 0; };

template <typename Fn>
static uint64_t ScanBack(uint64_t from, uint64_t to, Fn ok, ScanStat& st,
                         uint64_t budget = 0) {
    if (to <= from) return 0;
    std::vector<uint8_t> buf(CHUNK);
    uint64_t pos = to;
    while (pos > from) {
        uint64_t s = ((pos - from) >= (uint64_t)CHUNK) ? (pos - CHUNK) : from;
        size_t len = (size_t)(pos - s);
        pos = s;
        if (!ReadMem(s, buf.data(), len)) { st.fails++; continue; }
        st.chunks++;
        for (size_t k = len / 8; k-- > 0; ) {
            uint64_t v = Pk<uint64_t>(buf.data(), k * 8);
            if (!IsPtr(v)) continue;
            st.cand++;
            if (budget && st.cand > budget) return 0;
            uint64_t sa = s + (uint64_t)k * 8;
            if (ok(sa)) return sa;
        }
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════════
//  2-БАСКЫЧ - ФОРМА БОЮНЧА ТАБУУ
// ══════════════════════════════════════════════════════════════════════
static const size_t A32_SPAN = 0x400;    // a32 ичинен канча карайбыз
static const size_t ENT_SPAN = 0x800;    // entity ичинен канча карайбыз
static const int    MAX_ENTS = 12;
static const int    MIN_ENTS = 4;    // ушундан аз болсо издөө жүрбөйт
// ── МАТЧ ЧЕГИ ───────────────────────────────────────────────────
//  Чыныгы 5v5 матчта ТАК 10 оюнчу болот. Лоббиде/менюда 2-6 гана.
//  Патчтан кийинки сыноо 6 entity менен жүрүп, таптакыр туура эмес
//  офсеттерди тапкан. Ошондуктан жыйынтыкты кабыл алуу үчүн
//  тизмеде эң азы 8 entity болушун талап кылабыз.
static const int    MATCH_ENTS = 8;

struct EntityFields {
    bool   ok        = false;
    size_t hp        = 0, hpMax = 0;
    size_t heroId    = 0;
    size_t level     = 0;
    size_t pos       = 0;
    size_t sameCamp  = 0;
    bool   hasLevel  = false;
    bool   hasCamp   = false;
};

struct Found {
    uint64_t     staticAddr = 0;
    uint64_t     a32        = 0;
    size_t       showPlayers = 0;
    size_t       showMonsters = 0;
    size_t       localPlayer = 0;
    bool         hasMonsters = false;
    bool         hasLocal    = false;
    int          nEnts       = 0;
    EntityFields f;
};

static bool FiniteF(float v) { return std::isfinite(v); }

// Координата ишенимдүүбү?  МААНИЛҮҮ: denormal float (~1e-38) да
// "finite жана < 500" чыпкасынан өтөт — ошондуктан чыныгы чек керек.
static bool PlausibleCoord(float v) {
    if (!FiniteF(v)) return false;
    if (v == 0.0f) return true;
    float a = std::fabs(v);
    return a >= 0.01f && a <= 500.0f;
}

// IL2CPP объектинин алгачкы 0x10 байты — класс башы (klass* + monitor).
// Ошол жерде оюн талаасы ЭЧ КАЧАН болбойт. Мурда бул текшерилбей,
// 0x4 / 0x10 сыяктуу жалган талапкерлер тандалып калчу.
static const size_t FIELD_MIN = 0x10;

// Entity блокторунан талааларды чыгаруу
static bool DiscoverEntityFields(const std::vector<std::vector<uint8_t>>& ents,
                                 EntityFields& out, bool verbose) {
    const int n = (int)ents.size();
    // ── КОРГОО: кайчылаш текшерүү үчүн жетиштүү entity керек.
    //    Чыныгы 5v5 матчта 10 entity болот.
    if (n < MIN_ENTS) return false;

    // ── Hp / HpMax : катар эки int
    //  ОҢДОО: мурда `hp > 0` талап кылынчу. Бирок матчта өлгөн же
    //  пайда боло элек каармандын HP'си 0 болот — ошондо ЧЫНЫГЫ
    //  офсет четке кагылып калчу (так ушундай болгон: 0x1a4 жоголгон).
    std::vector<size_t> hpCand;
    for (size_t off = FIELD_MIN; off + 8 <= ENT_SPAN; off += 4) {
        bool ok = true, anyAlive = false;
        std::vector<int> maxes;
        for (int i = 0; i < n; i++) {
            int hp  = Pk<int>(ents[i].data(), off);
            int hpm = Pk<int>(ents[i].data(), off + 4);
            if (hp < 0 || hp > hpm)          { ok = false; break; }
            if (hpm < 500 || hpm > 100000)   { ok = false; break; }
            if (hp > 0 && hp < hpm) anyAlive = true;
            maxes.push_back(hpm);
        }
        if (!ok || !anyAlive) continue;
        // HpMax ар каарманда ар башка (эң азы 3 башка маани)
        std::sort(maxes.begin(), maxes.end());
        maxes.erase(std::unique(maxes.begin(), maxes.end()), maxes.end());
        if ((int)maxes.size() < 3) continue;
        hpCand.push_back(off);
    }

    // ── HeroID : матчта каармандар КАЙТАЛАНБАЙТ — баары ар башка.
    //  Бул эң күчтүү шарт. Мурда жөн гана "1..999" эле, ошондуктан
    //  27 талапкер чыккан.
    std::vector<size_t> idCand;
    for (size_t off = FIELD_MIN; off + 4 <= ENT_SPAN; off += 4) {
        std::vector<int> vs; int zeros = 0; bool ok = true;
        for (int i = 0; i < n; i++) {
            int v = Pk<int>(ents[i].data(), off);
            if (v == 0) { zeros++; continue; }       // пайда боло элек
            if (v < 1 || v > 999) { ok = false; break; }
            vs.push_back(v);
        }
        if (!ok || zeros > 2 || (int)vs.size() < 5) continue;
        std::vector<int> u = vs;
        std::sort(u.begin(), u.end());
        u.erase(std::unique(u.begin(), u.end()), u.end());
        if (u.size() != vs.size()) continue;         // баары ар башка
        int big = 0; for (int v : vs) if (v > 10) big++;
        if (big * 2 < (int)vs.size()) continue;      // көбү 10'дон чоң
        idCand.push_back(off);
    }

    // ── Level : 0..15 (0 = пайда боло элек), эң азы бирөө >= 1
    std::vector<size_t> lvCand;
    for (size_t off = FIELD_MIN; off + 4 <= ENT_SPAN; off += 4) {
        bool ok = true, any = false;
        for (int i = 0; i < n; i++) {
            int v = Pk<int>(ents[i].data(), off);
            if (v < 0 || v > 15) { ok = false; break; }
            if (v >= 1) any = true;
        }
        if (ok && any) lvCand.push_back(off);
    }

    // ── vCachePosition : катар үч float (x,y,z)
    std::vector<size_t> posCand;
    for (size_t off = FIELD_MIN; off + 12 <= ENT_SPAN; off += 4) {
        bool ok = true;
        std::vector<float> xs, zs;
        for (int i = 0; i < n; i++) {
            float x = Pk<float>(ents[i].data(), off);
            float y = Pk<float>(ents[i].data(), off + 4);
            float z = Pk<float>(ents[i].data(), off + 8);
            if (!PlausibleCoord(x) || !PlausibleCoord(y) || !PlausibleCoord(z)) {
                ok = false; break;
            }
            xs.push_back(x); zs.push_back(z);
        }
        if (!ok) continue;
        // Картада каармандар ар жерде турат: эң чоң XZ аралыгы 1..300
        float maxd = 0.0f;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                float dx = xs[i] - xs[j], dz = zs[i] - zs[j];
                float d = std::sqrt(dx * dx + dz * dz);
                if (d > maxd) maxd = d;
            }
        if (maxd < 1.0f || maxd > 300.0f) continue;
        posCand.push_back(off);
    }

    // ── bSameCampType : 0/1 байт, 5v5'те ТЕҢ БӨЛҮНӨТ.
    //  Мурда "0 же 1" эле — ошондуктан 31 талапкер чыккан.
    //  Эми эки топтун ар бири жетиштүү чоң болушу керек.
    const int minSide = (n / 3) > 2 ? (n / 3) : 2;
    std::vector<size_t> campCand;
    for (size_t off = FIELD_MIN; off + 1 <= ENT_SPAN; off += 1) {
        int z = 0, o = 0; bool ok = true;
        for (int i = 0; i < n; i++) {
            uint8_t v = ents[i][off];
            if (v == 0) z++; else if (v == 1) o++; else { ok = false; break; }
        }
        if (!ok || z < minSide || o < minSide) continue;
        campCand.push_back(off);
    }

    if (verbose) {
        printf("    Hp/HpMax talapker : %zu", hpCand.size());
        for (size_t i = 0; i < hpCand.size() && i < 12; i++) printf(" 0x%zx", hpCand[i]);
        printf("\n    HeroID   talapker : %zu", idCand.size());
        for (size_t i = 0; i < idCand.size() && i < 12; i++) printf(" 0x%zx", idCand[i]);
        printf("\n    Level    talapker : %zu", lvCand.size());
        for (size_t i = 0; i < lvCand.size() && i < 12; i++) printf(" 0x%zx", lvCand[i]);
        printf("\n    Position talapker : %zu", posCand.size());
        for (size_t i = 0; i < posCand.size() && i < 12; i++) printf(" 0x%zx", posCand[i]);
        printf("\n    SameCamp talapker : %zu", campCand.size());
        for (size_t i = 0; i < campCand.size() && i < 12; i++) printf(" 0x%zx", campCand[i]);
        printf("\n");
    }

    if (hpCand.empty() || idCand.empty() || posCand.empty()) return false;

    // ════════════════════════════════════════════════════════════════
    //  КЛАСТЕР БОЮНЧА ТАНДОО  (мурда жөн гана [0] алынчу — ката)
    //
    //  Бир класстын талаалары эс тутумда ЧОГУУ жатат. Чыныгы MLBB'де:
    //      HeroID 0x18c, Level 0x190, Hp 0x1a4, HpMax 0x1a8
    //  — үчөө 0x18 байт ичинде. Ошондуктан (Hp, HeroID, Level)
    //  айкалышынын ичинен ЭҢ ТЫГЫЗЫН тандайбыз. Бул оюн
    //  жаңыртылганда офсеттер жылса да иштейт, себеби алар
    //  дайыма бири-бирине жакын калат.
    // ════════════════════════════════════════════════════════════════
    size_t bestHp = hpCand[0], bestId = idCand[0], bestLv = 0;
    bool   haveLv = false;
    long   bestScore = -1;

    for (size_t hp : hpCand) {
        for (size_t id : idCand) {
            if (id == hp || id == hp + 4) continue;    // HpMax менен кайчылашпа
            // Level жок болсо да (Hp,HeroID) эле баалайбыз
            if (lvCand.empty()) {
                long span = (long)(hp > id ? hp - id : id - hp);
                if (bestScore < 0 || span < bestScore) {
                    bestScore = span; bestHp = hp; bestId = id; haveLv = false;
                }
                continue;
            }
            for (size_t lv : lvCand) {
                if (lv == id || lv == hp || lv == hp + 4) continue;
                size_t lo = hp, hi = hp;
                if (id < lo) lo = id;
                if (id > hi) hi = id;
                if (lv < lo) lo = lv;
                if (lv > hi) hi = lv;
                long span = (long)(hi - lo);
                // Level HeroID'дин артында катар турса — күчтүү белги
                if (lv == id + 4) span -= 8;
                if (bestScore < 0 || span < bestScore) {
                    bestScore = span;
                    bestHp = hp; bestId = id; bestLv = lv; haveLv = true;
                }
            }
        }
    }

    out.hp     = bestHp;
    out.hpMax  = bestHp + 4;
    out.heroId = bestId;
    out.level  = bestLv;
    out.hasLevel = haveLv;

    // Position: кластерге эң жакыныраагы (бир эле класстын талаасы)
    out.pos = posCand[0];
    {
        size_t bd = (size_t)-1;
        for (size_t c : posCand) {
            size_t d = (c > out.hp) ? (c - out.hp) : (out.hp - c);
            if (d < bd) { bd = d; out.pos = c; }
        }
    }

    // SameCamp: ал да ошол эле класста — позицияга эң жакыныраагы
    if (!campCand.empty()) {
        size_t best = campCand[0], bd = (size_t)-1;
        for (size_t c : campCand) {
            size_t d = (c > out.pos) ? (c - out.pos) : (out.pos - c);
            if (d < bd) { bd = d; best = c; }
        }
        out.sameCamp = best; out.hasCamp = true;
    }

    if (verbose)
        printf("    tandalgan klaster : Hp=0x%zx HeroID=0x%zx Level=0x%zx"
               " (span=%ld)\n", out.hp, out.heroId, out.level, bestScore);

    out.ok = true;
    return true;
}

// a32 талапкеринен ShowPlayers тизмесин жана entity талааларын табуу
static bool TryDiscover(uint64_t a32, Found& out, bool verbose) {
    std::vector<uint8_t> blk(A32_SPAN);
    if (!ReadMem(a32, blk.data(), A32_SPAN)) return false;

    struct ListHit { size_t off; uint64_t items; int size; };
    std::vector<ListHit> lists;

    for (size_t off = 0; off + 8 <= A32_SPAN; off += 8) {
        uint64_t p = Pk<uint64_t>(blk.data(), off);
        if (!IsPtr(p)) continue;
        uint64_t items = RdPtr(p + LIST_ITEMS);
        if (!IsPtr(items)) continue;
        int sz = RdInt(p + LIST_SIZE);
        if (sz < 1 || sz > 32) continue;
        lists.push_back({off, items, sz});
    }
    if (lists.empty()) return false;

    if (verbose) printf("  a32 0x%" PRIx64 " : %zu list-forma tabyldy\n",
                        a32, lists.size());

    // Эң чоң тизмеден баштайбыз (оюнчулар тизмеси адатта 10)
    std::sort(lists.begin(), lists.end(),
              [](const ListHit& a, const ListHit& b){ return a.size > b.size; });

    for (auto& L : lists) {
        // Матч эмес абалдагы кичине тизмелер жалган жыйынтык берет
        if (L.size < MATCH_ENTS) continue;
        int take = L.size < MAX_ENTS ? L.size : MAX_ENTS;
        std::vector<uint64_t> eptr;
        bool bad = false;
        for (int i = 0; i < take; i++) {
            uint64_t e = RdPtr(L.items + ARR_FIRST + (uint64_t)i * 8);
            if (!IsPtr(e)) { bad = true; break; }
            eptr.push_back(e);
        }
        if (bad || eptr.size() < 2) continue;

        std::vector<std::vector<uint8_t>> ents;
        for (uint64_t e : eptr) {
            std::vector<uint8_t> b(ENT_SPAN);
            if (!ReadMem(e, b.data(), ENT_SPAN)) { bad = true; break; }
            ents.push_back(std::move(b));
        }
        if (bad || ents.size() < 2) continue;

        if (verbose)
            printf("  -> list @ +0x%zx  size=%d  entity okuldu=%zu\n",
                   L.off, L.size, ents.size());

        EntityFields f;
        if (!DiscoverEntityFields(ents, f, verbose)) continue;

        out.a32         = a32;
        out.showPlayers = L.off;
        out.nEnts       = (int)ents.size();
        out.f           = f;

        // ── ShowMonsters: башка List формасы.
        //  ОҢДОО: мурда "биринчи башка тизме" алынчу — ошондуктан 0xc8
        //  тандалып калган. Чындыгында ShowPlayers менен ShowMonsters
        //  бир эле класста катар жатат (0x70 / 0x78), ошондуктан
        //  ShowPlayers'ке ЭҢ ЖАКЫН тизмени алабыз.
        {
            size_t bd = (size_t)-1;
            for (auto& M : lists) {
                if (M.off == L.off) continue;
                size_t d = (M.off > L.off) ? (M.off - L.off) : (L.off - M.off);
                if (d < bd) { bd = d; out.showMonsters = M.off; out.hasMonsters = true; }
            }
        }

        // ── LocalPlayerShow: a32 ичинен тизмедеги entity'ге көрсөткөн
        for (size_t off = 0; off + 8 <= A32_SPAN; off += 8) {
            uint64_t p = Pk<uint64_t>(blk.data(), off);
            if (!IsPtr(p)) continue;
            for (uint64_t e : eptr) {
                if (p == e) { out.localPlayer = off; out.hasLocal = true; break; }
            }
            if (out.hasLocal) break;
        }
        return true;
    }
    return false;
}

// ══════════════════════════════════════════════════════════════════════
//  ЧЫГАРУУ
// ══════════════════════════════════════════════════════════════════════
static void Line() { printf("==================================================\n"); }

static bool WriteCfg() {
    // ── КОРГОО: эски конфигдин көчүрмөсүн сактап коёбуз
    {
        FILE* a = fopen(CFG_OUT, "rb");
        if (a) {
            char bak[320]; snprintf(bak, sizeof(bak), "%s.bak", CFG_OUT);
            FILE* b = fopen(bak, "wb");
            if (b) {
                char tmp[4096]; size_t k;
                while ((k = fread(tmp, 1, sizeof(tmp), a)) > 0) fwrite(tmp, 1, k, b);
                fclose(b);
                printf("  eski konfig saktaldy: %s\n", bak);
            }
            fclose(a);
        }
    }
    FILE* f = fopen(CFG_OUT, "w");
    if (!f) { printf("[X] %s jazyla albady\n", CFG_OUT); return false; }
    fprintf(f, "# offsetdumper_mlbb avto-jazgan\n");
    fprintf(f, "BattleManager.BasePtr=0x%zx\n",         O.BM_Base);
    fprintf(f, "LogicBattleManager.BasePtr=0x%zx\n",    O.LBM_Base);
    fprintf(f, "Camera.MainCameraPtr=0x%zx\n",          O.CAM_Base);
    fprintf(f, "SystemData.BasePtr=0x%zx\n",            O.SD_Base);
    fprintf(f, "BattleManager.ShowPlayers=0x%zx\n",     O.ShowPlayers);
    fprintf(f, "BattleManager.ShowMonsters=0x%zx\n",    O.ShowMonsters);
    fprintf(f, "BattleManager.LocalPlayerShow=0x%zx\n", O.LocalPlayer);
    fprintf(f, "ShowEntity.Hp=0x%zx\n",                 O.SE_Hp);
    fprintf(f, "ShowEntity.HpMax=0x%zx\n",              O.SE_HpMax);
    fprintf(f, "ShowEntity.HeroID=0x%zx\n",             O.SE_HeroID);
    fprintf(f, "ShowEntity.Level=0x%zx\n",              O.SE_Level);
    fprintf(f, "ShowEntity.bDeath=0x%zx\n",             O.SE_bDeath);
    fprintf(f, "ShowEntity.bSameCampType=0x%zx\n",      O.SE_bSameCamp);
    fprintf(f, "ShowEntity.vCachePosition=0x%zx\n",     O.SE_Pos);
    fclose(f);
    return true;
}

static void DumpChain(uint64_t libbase) {
    uint64_t sa = libbase + O.BM_Base;
    uint64_t a1 = RdPtr(sa), a2 = RdPtr(a1 + PTR_CHAIN1), a32 = RdPtr(a2);
    printf("  static @ 0x%" PRIx64 "\n  a1  = 0x%" PRIx64 "\n  a2  = 0x%" PRIx64
           "\n  a32 = 0x%" PRIx64 "\n", sa, a1, a2, a32);
    uint64_t sp = RdPtr(a32 + O.ShowPlayers);
    if (IsPtr(sp)) printf("  players = %d\n", RdInt(sp + LIST_SIZE));
    uint64_t self = RdPtr(a32 + O.LocalPlayer);
    if (IsPtr(self))
        printf("  self = 0x%" PRIx64 "  hp=%d/%d  hero=%d  lvl=%d\n", self,
               RdInt(self + O.SE_Hp), RdInt(self + O.SE_HpMax),
               RdInt(self + O.SE_HeroID), RdInt(self + O.SE_Level));
    else
        printf("  self = jok (matchta emessin)\n");

    // ── ОЮНЧУЛАР ТАБЛИЦАСЫ ───────────────────────────────────────
    //  Азыркы офсеттер менен ар бир entity'ни окуйбуз. Бул эки
    //  нерсени бир заматта көрсөтөт:
    //    1) чын эле матчтабызбы (HP > 0 болгон каармандар барбы)
    //    2) жазылган офсеттер дагы деле туура окуп жатабы
    if (!IsPtr(sp)) return;
    uint64_t items = RdPtr(sp + LIST_ITEMS);
    int      cnt   = RdInt(sp + LIST_SIZE);
    if (!IsPtr(items) || cnt < 1) return;
    if (cnt > MAX_ENTS) cnt = MAX_ENTS;

    printf("\n  ## entity              hp /  hpmax  hero  lvl camp"
           "      x        z\n");
    int alive = 0, withHero = 0;
    for (int i = 0; i < cnt; i++) {
        uint64_t e = RdPtr(items + ARR_FIRST + (uint64_t)i * 8);
        if (!IsPtr(e)) { printf("  %2d  <okulbady>\n", i); continue; }
        int hp = RdInt(e + O.SE_Hp),  hpm = RdInt(e + O.SE_HpMax);
        int hid= RdInt(e + O.SE_HeroID), lv = RdInt(e + O.SE_Level);
        uint8_t camp = 0; ReadMem(e + O.SE_bSameCamp, &camp, 1);
        float xyz[3] = {0,0,0}; ReadMem(e + O.SE_Pos, xyz, 12);
        if (hp > 0)  alive++;
        if (hid > 0) withHero++;
        printf("  %2d  0x%010" PRIx64 " %6d /%6d  %4d  %3d  %2d  %8.1f %8.1f\n",
               i, e, hp, hpm, hid, lv, (int)camp, xyz[0], xyz[2]);
    }
    printf("\n  tiruu: %d/%d    hero ID bar: %d/%d\n", alive, cnt, withHero, cnt);
    if (alive == 0)
        printf("  >>> ESKERTUU: bir da tiruu kaarman jok.\n"
               "      Lobbide / juktoodo turasyn — 2-baskych (-f) bul\n"
               "      abalda ISHTEBEYT. Chynygy matchta kayra ishtet.\n");
}

// ══════════════════════════════════════════════════════════════════════
//  ӨЗҮН-ӨЗҮ СЫНОО (-t) - оюнсуз, жасалма маалымат менен.
//  Алгоритм туура иштейби деп текшерет: белгилүү офсеттерге маани
//  коюп, калганын таштанды менен толтуруп, табыла алабы деп карайт.
// ══════════════════════════════════════════════════════════════════════
static uint32_t s_rnd = 12345;
static uint32_t Rnd() { s_rnd = s_rnd * 1103515245u + 12345u; return s_rnd >> 8; }

static int SelfTest(uint32_t seed) {
    s_rnd = seed;
    printf("== OZUN-OZU SYNOO (seed %u) ==\n", seed);
    const size_t T_HP = 0x1a4, T_HPM = 0x1a8, T_ID = 0x18c,
                 T_LV = 0x190, T_POS = 0x28c, T_CAMP = 0x2a9;
    const int N = 10;
    int heroes[N] = { 12, 45, 112, 7, 88, 203, 56, 134, 19, 77 };
    int hpmax[N]  = { 4200, 3800, 5600, 2900, 7100, 4400, 3300, 6200, 5000, 4800 };

    std::vector<std::vector<uint8_t>> ents;
    for (int i = 0; i < N; i++) {
        std::vector<uint8_t> b(ENT_SPAN);
        // Реалдуу эс тутумга окшош таштанды: толук кокустук, кичине
        // сандар, көрсөткүчтөр жана нөлдөр аралашкан
        for (size_t k = 0; k < ENT_SPAN; k += 4) {
            uint32_t r = Rnd(), v;
            switch (r % 4) {
                case 0:  v = 0; break;                        // нөл
                case 1:  v = Rnd() % 4096; break;             // кичине int
                case 2:  v = 0x70000000u | (Rnd() & 0xFFFFFF); break; // көрсөткүч
                default: v = (Rnd() << 8) ^ Rnd(); break;     // толук кокустук
            }
            memcpy(b.data() + k, &v, 4);
        }
        // ── ЧЫНЫГЫ МАТЧТЫН ООР ШАРТТАРЫ (мурда тест буларды текшербеген,
        //    ошондуктан телефондо жыйылганда ката чыккан):
        //      i == 0 : каарман ӨЛГӨН        -> hp = 0
        //      i == 1 : пайда боло ЭЛЕК      -> heroID = 0, lvl = 0
        int hp = hpmax[i] - (int)(Rnd() % 900);
        if (i == 0) hp = 0;                       // өлгөн
        int hid = heroes[i];
        int lv  = 1 + (i % 15);
        if (i == 1) { hid = 0; lv = 0; }          // пайда боло элек
        memcpy(b.data() + T_HP,  &hp,       4);
        memcpy(b.data() + T_HPM, &hpmax[i], 4);
        memcpy(b.data() + T_ID,  &hid,      4);
        memcpy(b.data() + T_LV,  &lv, 4);
        float px = -40.0f + i * 7.3f, py = 1.5f, pz = 20.0f - i * 3.1f;
        memcpy(b.data() + T_POS + 0, &px, 4);
        memcpy(b.data() + T_POS + 4, &py, 4);
        memcpy(b.data() + T_POS + 8, &pz, 4);
        b[T_CAMP] = (i < 5) ? 0 : 1;
        ents.push_back(std::move(b));
    }

    EntityFields f;
    bool ok = DiscoverEntityFields(ents, f, true);
    printf("\n  jyyyntyk: %s\n", ok ? "TABYLDY" : "TABYLGAN JOK");
    if (!ok) return 1;

    struct Chk { const char* n; size_t got, want; bool has; };
    Chk cs[] = {
        { "Hp",       f.hp,       T_HP,   true },
        { "HpMax",    f.hpMax,    T_HPM,  true },
        { "HeroID",   f.heroId,   T_ID,   true },
        { "Level",    f.level,    T_LV,   f.hasLevel },
        { "Position", f.pos,      T_POS,  true },
        { "SameCamp", f.sameCamp, T_CAMP, f.hasCamp },
    };
    int bad = 0;
    for (auto& c : cs) {
        bool good = c.has && c.got == c.want;
        printf("  %-9s tabyldy=0x%-5zx kutulgon=0x%-5zx  %s\n",
               c.n, c.got, c.want, good ? "OK" : "KATA");
        if (!good) bad++;
    }
    printf("\n  %s\n", bad == 0 ? ">>> BARDYGY TUURA <<<" : ">>> KATA BAR <<<");
    return bad == 0 ? 0 : 1;
}

// ══════════════════════════════════════════════════════════════════════
//  MAIN
// ══════════════════════════════════════════════════════════════════════
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    bool force = false, verbose = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f")) force = true;
        if (!strcmp(argv[i], "-v")) verbose = true;
        if (!strcmp(argv[i], "-t")) {
            uint32_t sd = (i + 1 < argc) ? (uint32_t)atoi(argv[i + 1]) : 12345u;
            return SelfTest(sd);
        }
    }

    Line();
    printf("  offsetdumper_mlbb  (1+2 baskych)\n");
    printf("  okuyt gana, oyunga ech nerse jazylbayt\n");
    Line();

    static const char* kProcs[] = {
        "com.mobile.legends:UnityKillsMe",
        "com.mobile.legends.usa:UnityKillsMe",
        "com.vng.mlbb:UnityKillsMe" };
    const char* pname = nullptr;
    for (auto n : kProcs) { pid_t p = PidOf(n); if (p > 0) { g_pid = p; pname = n; break; } }
    if (g_pid <= 0) { printf("[X] Oyun tabylgan jok.\n"); return 1; }
    printf("[OK] Process : %s  (PID %d)\n", pname, (int)g_pid);

    LibInfo lib;
    if (!FindLib(g_pid, lib)) { printf("[X] libcsharp.so tabylgan jok\n"); return 1; }
    printf("[OK] Lib     :%s\n", lib.path);
    printf("[OK] Base    : 0x%" PRIx64 "\n", lib.base);
    printf("[OK] rw-p    : %" PRIu64 " KB  (+0x%" PRIx64 " .. +0x%" PRIx64 ")\n",
           (lib.rwE - lib.rwS) / 1024, lib.rwS - lib.base, lib.rwE - lib.base);

    // ══ 1-БАСКЫЧ ══
    Line(); printf("  1-BASKYCH : jazylgan ofsetter\n"); Line();
    bool m = ValidateBM(lib.base + O.BM_Base, Lvl::Match);
    bool l = ValidateBM(lib.base + O.BM_Base, Lvl::Lobby);
    printf("  BattleManager 0x%zx : %s\n", O.BM_Base,
           m ? "MATCH OK" : (l ? "LOBBY OK" : "JARAKSYZ"));
    printf("  Camera        0x%zx : %s\n", O.CAM_Base,
           ValidateCam(lib.base + O.CAM_Base) ? "OK" : "JARAKSYZ");

    if ((m || l) && !force) {
        printf("\n  Ofsetter jaraktuu - skan kerek emes.\n");
        printf("  (forma boyuncha izdooni synoo uchun: -f)\n");
        DumpChain(lib.base);
        Line(); printf("  JYYYNTYK\n"); Line();
        printf("  BattleManager = 0x%zx\n  LogicBattleM  = 0x%zx\n"
               "  Camera        = 0x%zx\n  SystemData    = 0x%zx\n",
               O.BM_Base, O.LBM_Base, O.CAM_Base, O.SD_Base);
        if (WriteCfg()) printf("\n  Jazyldy: %s\n", CFG_OUT);
        printf("  Jalpy okuu: %" PRIu64 "\n", g_reads);
        Line();
        return 0;
    }

    // ══ 2-БАСКЫЧ : ФОРМА БОЮНЧА ══
    Line();
    printf("  2-BASKYCH : FORMA BOYUNCHA IZDOO\n");
    printf("  (jazylgan ofsetterge TAYANBAYT)\n");
    Line();

    // ══════════════════════════════════════════════════════════════
    //  АЛДЫН АЛА КОРГОО: эгер жазылган офсеттер АЗЫР ДА иштеп турса
    //  (б.а. оюн жаңырган эмес) жана бир да тирүү каарман жок болсо —
    //  лоббидесиң. Мындай абалда форма издөө маани таба албайт,
    //  ошондуктан бекер скан кылбай токтойбуз.
    // ══════════════════════════════════════════════════════════════
    if (m || l) {
        uint64_t a32p = ChainA32(lib.base + O.BM_Base);
        uint64_t spp  = a32p ? RdPtr(a32p + O.ShowPlayers) : 0;
        if (IsPtr(spp)) {
            uint64_t it = RdPtr(spp + LIST_ITEMS);
            int c = RdInt(spp + LIST_SIZE);
            if (c > MAX_ENTS) c = MAX_ENTS;
            int alive = 0;
            for (int i = 0; IsPtr(it) && i < c; i++) {
                uint64_t e = RdPtr(it + ARR_FIRST + (uint64_t)i * 8);
                if (IsPtr(e) && RdInt(e + O.SE_Hp) > 0) alive++;
            }
            if (c >= MIN_ENTS && alive == 0) {
                printf("\n[X] LOBBIDE / JUKTOODO TURASYN (tiruu kaarman 0/%d).\n", c);
                printf("    Forma boyuncha izdoo bul abalda ISHTEBEYT:\n");
                printf("    HP, hero ID jana koordinattar azyrak nol.\n\n");
                printf("    Chynygy 5v5 matchta, kaarmandar kartaga chykkandan\n");
                printf("    1-2 munottan kiyin kayra ishtet.\n");
                printf("    offsets.cfg OZGORTULBODU.\n");
                Line();
                return 4;
            }
        }
    }

    auto t0 = std::chrono::steady_clock::now();
    ScanStat st;
    Found fd;

    // Чынжыр деңгээлиндеги ар бир талапкерге форма издөөсүн колдонобуз.
    uint64_t hit = ScanBack(lib.rwS, lib.rwE,
        [&](uint64_t sa) -> bool {
            if (!ValidateChainOnly(sa)) return false;
            uint64_t a32 = ChainA32(sa);
            if (!a32) return false;
            if (!TryDiscover(a32, fd, verbose)) return false;
            fd.staticAddr = sa;
            return true;
        }, st);

    auto t1 = std::chrono::steady_clock::now();
    st.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    printf("  talapker : %" PRIu64 "\n", st.cand);
    printf("  ubakyt   : %.0f ms\n", st.ms);

    if (!hit) {
        printf("\n[X] TABYLGAN JOK.\n");
        printf("    Matchta ishtetip korgun - oyunchular tizmesi toluk bolushu kerek.\n");
        printf("    Kenireek uchun: -v\n");
        printf("  Jalpy okuu: %" PRIu64 "\n", g_reads);
        return 2;
    }

    // ── Табылгандарды колдонуу
    O.BM_Base      = (size_t)(hit - lib.base);
    O.ShowPlayers  = fd.showPlayers;
    if (fd.hasMonsters) O.ShowMonsters = fd.showMonsters;
    if (fd.hasLocal)    O.LocalPlayer  = fd.localPlayer;
    O.SE_Hp        = fd.f.hp;
    O.SE_HpMax     = fd.f.hpMax;
    O.SE_HeroID    = fd.f.heroId;
    O.SE_Pos       = fd.f.pos;
    if (fd.f.hasLevel) O.SE_Level     = fd.f.level;
    if (fd.f.hasCamp)  O.SE_bSameCamp = fd.f.sameCamp;

    // ══════════════════════════════════════════════════════════════
    //  КОРГОО: табылган офсеттерди АКЫРКЫ ЖОЛУ текшеребиз.
    //  Эгер алар менен толук чынжыр иштебесе - жазбайбыз, эски
    //  конфигди бузуп албашыбыз керек.
    // ══════════════════════════════════════════════════════════════
    if (!ValidateBM(lib.base + O.BM_Base, Lvl::Lobby)) {
        printf("\n[X] TABYLGAN OFSETTER AKYRKY TEKSHERUUDON OTPODU.\n");
        printf("    offsets.cfg OZGORTULBODU (eski boydon kaldy).\n");
        printf("    Chynygy 5v5 matchta kayra ishtetip kor.\n");
        return 3;
    }

    // ══════════════════════════════════════════════════════════════
    //  MATCH DALILI. Jogorku tekshereu ozu tapkan ofsetter menen
    //  ozun tekshert - andai tekshereu dayyma otot, oshonduktan
    //  alsyz. Chynygy matchta bolgonubuzdun UCH koz karandysyz
    //  belgisi kerek:
    //    1) tizmede >= 8 entity
    //    2) LocalPlayerShow tabyldy (lobbide ol jok)
    //    3) entity'lerdin kobu tiruu (hp > 0)
    // ══════════════════════════════════════════════════════════════
    int aliveN = 0;
    {
        uint64_t sp = RdPtr(fd.a32 + O.ShowPlayers);
        uint64_t it = IsPtr(sp) ? RdPtr(sp + LIST_ITEMS) : 0;
        int c = IsPtr(sp) ? RdInt(sp + LIST_SIZE) : 0;
        if (c > MAX_ENTS) c = MAX_ENTS;
        for (int i = 0; IsPtr(it) && i < c; i++) {
            uint64_t e = RdPtr(it + ARR_FIRST + (uint64_t)i * 8);
            if (IsPtr(e) && RdInt(e + O.SE_Hp) > 0) aliveN++;
        }
    }
    bool okEnts  = fd.nEnts >= MATCH_ENTS;
    bool okSelf  = fd.hasLocal;
    bool okAlive = aliveN >= 4;

    if (!okEnts || !okSelf || !okAlive) {
        printf("\n[X] MATCHTA BOLGONUBUZ DALILDENBEDI.\n");
        printf("    entity sany  : %d   (kerek >= %d)  %s\n",
               fd.nEnts, MATCH_ENTS, okEnts  ? "OK" : "<<< JETISHSIZ");
        printf("    self tabyldy : %s        %s\n",
               fd.hasLocal ? "ooba" : "jok ",  okSelf  ? "OK" : "<<< JOK");
        printf("    tiruu kaarman: %d   (kerek >= 4)  %s\n",
               aliveN, okAlive ? "OK" : "<<< JETISHSIZ");
        printf("\n    Tabylgan ofsetterge ISHENUUGO BOLBOYT -\n");
        printf("    lobbide je menude skan kylgansyn. offsets.cfg\n");
        printf("    OZGORTULBODU (eski boydon kaldy).\n\n");
        printf("    Chynygy 5v5 matchta, kaarmandar kartaga chykkandan\n");
        printf("    1-2 munottan kiyin kayra ishtet.\n");
        Line();
        return 5;
    }

    printf("\n[OK] TABYLDY\n");
    printf("  static      @ 0x%" PRIx64 "  (+0x%zx)\n", fd.staticAddr, O.BM_Base);
    printf("  a32         @ 0x%" PRIx64 "\n", fd.a32);
    printf("  entity okuldu: %d\n", fd.nEnts);

    // ── Калган 3 база: ара алыстык боюнча
    struct G { size_t* t; size_t d; const char* n; bool cam; };
    G gs[] = { { &O.LBM_Base, D_LBM, "LogicBattleManager", false },
               { &O.CAM_Base, D_CAM, "Camera",             true  },
               { &O.SD_Base,  D_SD,  "SystemData",         false } };
    // ══════════════════════════════════════════════════════════════
    //  КОРГОО: ар бир база ӨЗҮНЧӨ дарек болушу керек.
    //  Мурда "чынжыр иштейт" деген жалпы текшерүү менен скан
    //  LogicBattleManager жана SystemData үчүн бир эле даректи
    //  кайтарып жатты (экөө тең 0x639e7b8). Эми алынган даректерди
    //  эстеп, кайталанганын четке кагабыз.
    // ══════════════════════════════════════════════════════════════
    std::vector<size_t> taken{ O.BM_Base };
    auto isTaken = [&](size_t v) {
        for (size_t t : taken) if (t == v) return true;
        return false;
    };

    for (auto& g : gs) {
        size_t c = O.BM_Base + g.d;
        bool ok = (g.cam ? ValidateCam(lib.base + c) : (ChainA32(lib.base + c) != 0))
                  && !isTaken(c);
        if (ok) {
            *g.t = c; taken.push_back(c);
            printf("  %-19s: 0x%zx (delta)\n", g.n, c);
            continue;
        }
        ScanStat s2;
        bool cam = g.cam;
        uint64_t h2 = ScanBack(lib.rwS, lib.rwE,
            [&](uint64_t a) {
                if (isTaken((size_t)(a - lib.base))) return false;   // кайталанбасын
                return cam ? ValidateCam(a) : (ChainA32(a) != 0);
            }, s2);
        if (h2) {
            *g.t = (size_t)(h2 - lib.base); taken.push_back(*g.t);
            printf("  %-19s: 0x%zx (skan)\n", g.n, *g.t);
        } else {
            printf("  %-19s: TABYLGAN JOK (eski maani kaldy)\n", g.n);
        }
    }

    Line(); printf("  TABYLGAN OFSETTER\n"); Line();
    printf("  BattleManager      = 0x%zx\n", O.BM_Base);
    printf("  LogicBattleManager = 0x%zx\n", O.LBM_Base);
    printf("  Camera             = 0x%zx\n", O.CAM_Base);
    printf("  SystemData         = 0x%zx\n", O.SD_Base);
    printf("  --\n");
    printf("  ShowPlayers        = 0x%zx\n", O.ShowPlayers);
    printf("  ShowMonsters       = 0x%zx%s\n", O.ShowMonsters,
           fd.hasMonsters ? "" : "  (eski maani)");
    printf("  LocalPlayerShow    = 0x%zx%s\n", O.LocalPlayer,
           fd.hasLocal ? "" : "  (eski maani)");
    printf("  --\n");
    printf("  Hp                 = 0x%zx\n", O.SE_Hp);
    printf("  HpMax              = 0x%zx\n", O.SE_HpMax);
    printf("  HeroID             = 0x%zx\n", O.SE_HeroID);
    printf("  Level              = 0x%zx%s\n", O.SE_Level,
           fd.f.hasLevel ? "" : "  (eski maani)");
    printf("  vCachePosition     = 0x%zx\n", O.SE_Pos);
    printf("  bSameCampType      = 0x%zx%s\n", O.SE_bSameCamp,
           fd.f.hasCamp ? "" : "  (eski maani)");
    printf("  bDeath             = 0x%zx  (eski maani - forma menen tabylbayt)\n",
           O.SE_bDeath);

    printf("\n");
    DumpChain(lib.base);

    if (WriteCfg()) printf("\n  Jazyldy: %s\n", CFG_OUT);
    printf("  Jalpy okuu: %" PRIu64 "\n", g_reads);
    Line();
    return 0;
}
