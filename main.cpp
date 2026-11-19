#include "main.h"
#include "Engine/CanvasView.h"
#include "Vector/Matrix4x4.hpp"
#include "Memory/Memory.h"
#include "Memory/PatternScanner.h"
#include "Vector/Quaternion.hpp"
#include "Vector/ToString.h"
#include "Vector/Vector2.hpp"
#include "Vector/Vector3.hpp"
#include "utils/include.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <malloc.h>
#include <mutex>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/system_properties.h>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <curl/curl.h>
#include "config/Offsets.h"
#include "features/ElfOffsetFinder.h"
#include "utils/Decoder64.h"
#include "utils/xorstr.hpp"
#include "Includes/GUI_Custom.h"
#include "icon/HeroIcons.h"
#include "icon/Logo.h"
#include "icon/Rank/RankIcons_loader.h"
#include "icon/Spell/SpellIcons_loader.h"
#include "icon/skill_mlbb/SkillIcons_loader.h"
#include "features/RoomInfo.h"
#include <sys/stat.h>

extern bool isBeingBlocked;
#include "ui/DrawIconHero.h"
#include "ui/Toggle.h"

using namespace Memory;
bool is_root_mode = true;
bool main_thread_flag = true;
static bool show_menu = true;
std::string g_package_name = xorstr_("com.mobile.legends");

std::atomic<bool> g_IsDeployingSkin(false);
std::string g_SkinStatus = xorstr_("Ready");

struct ScanStatus {
    bool scanned = false;
    bool bm_ok = false;
    bool cam_ok = false;
    bool lbm_ok = false;
    bool sd_ok = false;
    uintptr_t bm_addr = 0;
    uintptr_t cam_addr = 0;
    uintptr_t lbm_addr = 0;
    uintptr_t sd_addr = 0;
};
ScanStatus g_ScanStatus;

struct LoginServerData {
    uint64_t AccountId;
    std::string Nickname;
    std::string ClientRealVersion;
    std::string ClientStreamVersion;
    std::string ClientLocalVersion;
    std::string ClientLoginVersion;
    std::string Channel;
    bool IsNewAccount;
    uint32_t RegionId;
    std::string CountryInfo;
    std::string CountryInfoByIP;
    std::string InternetOperator;
    std::string State;
    uint32_t AccCreateTime;
    uint32_t AccountFlag;
    std::string UserIP;
    std::string GameServerIP;
    int32_t GameServerPort;
    std::string EngineBuildVersion;
    bool IsValid;
    LoginServerData()
        : AccountId(0), IsNewAccount(false), RegionId(0), AccCreateTime(0),
          AccountFlag(0), GameServerPort(0), IsValid(false) {}
};
extern LoginServerData g_LoginServerCache;

int abs_ScreenX = 0;
int abs_ScreenY = 0;
bool drawMAddress;
bool drawMLine        = true;
bool drawMHealth      = true;

// ── FIX #2: SYSTEM колдонуучу баскычы менен гана өчүрсүн
static bool g_RequestExit = false;

bool drawAlertUnderAttack = true;
float Alert_PosX      = 0.5f;
float Alert_PosY      = 0.15f;
float Alert_Scale     = 1.0f;
bool  Alert_ShowHPText = true;
bool  iconhero        = true;
bool  drawMAvatar     = true;
float Avatar_OffsetY  = 80.0f;
bool  drawMinionMinimap     = true;
bool  drawMonsterHealth     = true;
bool  drawMonsterID         = false;
bool  drawMonsterName       = true;
bool  showRetriCircle       = true;
float minionMinimapSize     = 4.0f;
int   g_ICSize              = 4;
int   MinimapSize           = 342;
int   MinimapPos            = 76;
int   MinimapPosY           = 76;
bool  MinimapIcon           = true;
bool  MinimapIconBuff       = false;        // монстрлор toggle
bool  MinimapIconLordTurtle = false;

bool  HideLine              = false;
float RadiusCir             = 50.0f;
float g_HeroIconSize        = 28.0f;
float g_MonsterIconSize     = 22.0f;
bool  is_attached           = false;
// ── DRONE VIEW АЛЫНЫП САЛЫНДЫ: EnableDrone глобалы жок
bool  enableDOD             = false;

// ══════════════════════════════════════════════════════════════════════
//  KERNEL-SAFE: entity тизмелеринин максималдуу чеги.
//    Оюндан окулган эсептегич бузулса, цикл ушул чектен ашпайт.
//    Буларсыз кернел драйвер миллиарддаган жараксыз окуу жасап panic болот.
// ══════════════════════════════════════════════════════════════════════
static constexpr uint MAX_PLAYER_ENTITY  = 32;
static constexpr uint MAX_MONSTER_ENTITY = 256;

// ══════════════════════════════════════════════════════════════════════
//  KERNEL-SAFE: Mono контейнерлеринин чеги.
//    dict.count / list.size оюндун эс тутумунан окулат. Оюн жаңы
//    башталганда алар толук инициализацияланбай, бузулган маани
//    кайтарат (мис. 0x7FFFFFFF). Ошол маани кернел драйверге өлчөм
//    катары жетсе, драйвер kmalloc буферинен ашып жазып, SLUB
//    freelist'ти бузат → эң жакынкы kmalloc'то panic.
// ══════════════════════════════════════════════════════════════════════
static constexpr int MAX_MONO_ENTRIES = 512;

// ══════════════════════════════════════════════════════════════════════
//  KERNEL-SAFE: user-space дарек чеги.
//    Бул түзүлүштө 39-bit VA (логдо: "user pgtable: 4k pages, 39-bit VAs"),
//    демек жарактуу user дарек 0x8000000000'ден кичине болушу керек.
//    Жараксыз даректи драйверге жибербейбиз.
// ══════════════════════════════════════════════════════════════════════
static inline bool IsUserAddr(uintptr_t a) {
    return a >= 0x10000ULL && a < 0x8000000000ULL;
}

// ══════════════════════════════════════════════════════════════════════
//  KERNEL-SAFE: ДРАЙВЕРГЕ КАЙРЫЛУУНУ СЕРИЯЛАШТЫРУУ  ← ЭҢ НЕГИЗГИ ОҢДОО
//
//  Бул программада драйверге 3 thread кайрылат:
//     • main loop  (Layout_tick_UI → DrawMonster / DrawMinimapESP)
//     • game_worker
//     • RoomInfoReaderThread
//
//  Башталгыч main.cpp'те main loop = 1ms, game_worker = 1ms болчу —
//  экөө бир такт менен иштеп, фазасы байланышкан эле.
//  Оптимизациядан кийин main loop = 13.333ms, game_worker = 4ms болду.
//  13.333 / 4 бүтүн эсе эмес → фазалар дайыма жылып, үч thread тең
//  бир эле учурда драйверге кайрылган учурлар пайда болот.
//
//  Эгер драйвер thread-safe эмес болсо (бир глобалдуу буфер же
//  глобалдуу size/pid өзгөрмөсү — мындай драйверлерде кеңири тараган),
//  параллел кайрылууда бир thread'тин өлчөмү экинчисиники менен
//  алмашып, драйвер kmalloc буферинен ашык көчүрөт
//  → SLUB freelist бузулат → эң жакынкы kmalloc'то kernel panic.
//
//  Логдогу крэш так ушундай: __kmem_cache_alloc_node, kmalloc-16
//  freelist бузулган, курмандык — таптакыр башка процесстин thread'и.
//
//  Эми бардык окуу блоктору ушул mutex астында — драйвер эч качан
//  эки thread'тен бир убакта чакырылбайт.
//  recursive: функциялар ичинде функция чакырылат (DrawMonster →
//  getCoolDown), deadlock болбосун.
// ══════════════════════════════════════════════════════════════════════
static std::recursive_mutex g_DriverMutex;
#define DRIVER_LOCK() std::lock_guard<std::recursive_mutex> _drv_lk(g_DriverMutex)

// ── ESP BOX жаңы өзгөрмөлөр
bool  drawEspBox            = true;
float espBoxR               = 1.0f;
float espBoxG               = 0.0f;
float espBoxB               = 0.0f;
float espBoxA               = 1.0f;
float espBoxThickness       = 1.5f;
// Corner-box style
bool  espBoxCornerStyle     = true;
float espBoxCornerLen       = 0.25f; // box өлчөмүнүн %
// Box жылдыруу (жогого/ылдыйга, оңго/солго)
float espBoxOffsetY         = 0.0f;
float espBoxOffsetX         = 0.0f;
// Box масштаб (чоңойтуп/кичирейтүү)
float espBoxScale           = 1.0f;
// ── ЖАҢЫ: ESP BOX HP шкаласынын өлчөмү (минимаптагыдай сыдырма менен)
float espBoxHpBarSize       = 4.0f;   // барддын туурасы, px
// Скил иконкаларды бокс ичинде көрсөтүү
bool  espBoxIconsInside     = true;
// ESP Line каармандын footer'инен тартуу
bool  espLineFromFooter     = true;
// ESP Line — ак түз сызык жөндөөлөрү
float espLineThickness      = 1.5f;

// Prediction линия узундугу жөндөөчү
float predictionLineLengthMult = 1.0f;

// ── DRONE VIEW АЛЫНЫП САЛЫНДЫ: FieldView глобалы жок
long  libbase    = 0;

void AttachToGame() {
    pid = pidof(xorstr_("com.mobile.legends.usa:UnityKillsMe"));
    if (pid > 0) {
        g_package_name = xorstr_("com.mobile.legends.usa");
    } else {
        pid = pidof(xorstr_("com.mobile.legends.usa"));
        if (pid > 0)
            g_package_name = xorstr_("com.mobile.legends.usa");
    }
    if (pid <= 0) {
        pid = pidof(xorstr_("com.mobile.legends:UnityKillsMe"));
        if (pid > 0)
            g_package_name = xorstr_("com.mobile.legends");
    }
    if (pid <= 0) {
        pid = pidof(xorstr_("com.vng.mlbb:UnityKillsMe"));
        if (pid > 0)
            g_package_name = xorstr_("com.vng.mlbb");
    }
    if (pid > 0) {
        DRIVER_LOCK();   // ── KERNEL-SAFE FIX: initialize учурунда эч ким окубасын
        // ── KERNEL-SAFE FIX: attach процессинин ТУУРА тартиби.
        //    1) Алгач is_attached = false — башка thread'тер окубай турсун
        //    2) Драйверди жаңы pid менен initialize кылуу
        //    3) libbase табуу
        //    4) Экөө тең даяр болгондо гана is_attached = true
        is_attached = false;
        libbase     = 0;

        g_pid = pid;
        if (g_driver)
            g_driver->initialize(g_pid);

        long base = GetBase(xorstr_("libcsharp.so"));
        if (base == 0)
            base = GetBase(xorstr_("libil2cpp.so"));

        if (base != 0) {
            libbase     = base;
            is_attached = true;   // ← эң акырында коюлат
        }
    }
}

std::string fshy(uintptr_t address) {
    // ── KERNEL-SAFE FIX: дарек текшерүү
    if (!IsUserAddr(address)) return "";

    uint32_t stringLength = Read<uint32_t>(address + OFF_STR(LengthOffset));
    if (stringLength == 0) return "";

    // ── KERNEL-SAFE FIX: 1024 → 128 белги (256 байт).
    //    Мурда 1024 белги = 2048 байттык окуу драйверге кетчү.
    //    Драйвердин ички kmalloc буфери андан кичине болсо — kernel heap
    //    overflow → SLUB freelist бузулуп panic (логдогу
    //    __kmem_cache_alloc_node крэши так ошол).
    //    Оюндагы ник/клан аты 128 белгиден ашпайт.
    if (stringLength > 128) stringLength = 128;

    std::vector<char16_t> buffer(stringLength + 1, 0);
    uintptr_t stringPtr = address + OFF_STR(BufferOffset);
    if (!IsUserAddr(stringPtr)) return "";
    if (!pvm(reinterpret_cast<void*>(stringPtr), buffer.data(),
             static_cast<size_t>(stringLength) * 2, false))
        return "";
    return utf16_to_utf8(buffer.data(), stringLength);
}

struct RoomPlayerInfo {
    std::string Name;
    std::string UserID;
    std::string Squad;
    std::string Rank;
    std::string Hero;
    std::string Spell;
};
RoomPlayerInfo PlayerB[5];
RoomPlayerInfo PlayerR[5];

struct String {
    char pad_0000[0x10];
    int length;
    wchar_t buffer[1];
    const char* CString() const {
        static char temp[256];
        wcstombs(temp, buffer, length);
        temp[length] = '\0';
        return temp;
    }
};

uintptr_t GetMainCamera() {
      auto klass = Read<uintptr_t>(libbase + OFF_CAM(MainCameraPtr));
      if (!klass) return 0;
      auto staticFields = Read<uintptr_t>(klass + OFF_CAM(CameraData));
      if (!staticFields) return 0;
      auto p = Read<uintptr_t>(staticFields + OFF_CAM(CameraData2));
      if (!p) return 0;
      p = Read<uintptr_t>(p + OFF_CAM(CameraExtraStep));   // <-- ЖАҢЫ КАДАМ
      if (!p) return 0;
      return p;
}

struct Camera {
    Matrix4x4 worldToCameraMatrix;
    Matrix4x4 projectionMatrix;
};
Matrix4x4 _vMatrix;

bool WorldToScreen(Vector3 from, Vector2* to) {
    float w = _vMatrix.m30 * from.X + _vMatrix.m31 * from.Y +
              _vMatrix.m32 * from.Z + _vMatrix.m33;
    if (w < 0.01f) return false;
    auto viewMatrix = _vMatrix.MultiplyPoint(from);
    auto screenPos  = Vector3(viewMatrix.X + 1.0f,
                              viewMatrix.Y + 1.0f,
                              viewMatrix.Z + 1.0f) / 2.0f;
    *to = Vector2(screenPos.X * abs_ScreenX,
                  abs_ScreenY - (screenPos.Y * abs_ScreenY));
    return true;
}

void FindPoint(Vector2 origin, Vector2& point,
               int screenwidth, int screenheight, int length) {
    float halfW  = screenwidth  / 2.0f;
    float halfH  = screenheight / 2.0f;
    float halfW2 = (screenwidth  - length) / 2.0f;
    float halfH2 = (screenheight - length) / 2.0f;
    float dx = fabs(origin.X - halfW);
    float dy = fabs(origin.Y - halfH);
    float rx = (dx != 0) ? halfW2 / dx : 0;
    float ry = (dy != 0) ? halfH2 / dy : 0;
    float r  = fmin(rx, ry);
    point.X  = origin.X + (halfW - origin.X) * (1.0f - r);
    point.Y  = origin.Y + (halfH - origin.Y) * (1.0f - r);
}

int ListMonsterId[] = {
    2002, 2003, 2004, 2005, 2006, 2008, 2009, 2011, 2012,
    2013, 2056, 2059, 2072, 2220, 2221, 2222, 2223, 2224,
    2225, 2226, 2227, 2228, 2229, 2230, 2232,
};
bool bMonster(int iValue) {
    return std::find(std::begin(ListMonsterId), std::end(ListMonsterId),
                     iValue) != std::end(ListMonsterId);
}

void Touch_Tap(int x, int y) {
    Touch_Down((float)x, (float)y);
    Touch_Up();
}

// ══════════════════════════════════════════════════════════════════
//  БАГ ОҢДОО: Auto-Retribution тап СПАМЫ (джойстик багы)
//
//  Мурда: lastRetriTriggered[20] жарыяланган, бирок ЭЧ КОЛДОНУЛБАЙТ.
//  ProcessAutoRetribution() DrawMonster ичинен АР КАДРДА чакырылат,
//  демек максаттын HP'си диапазондо турганда секундасына ~60 жолу
//  Touch_Tap() иштеп, экранга жасалма тийүү агымы жиберилчү.
//  Натыйжада: джойстик секирип, каарман ары-бери кыймылдап калчу.
//
//  Эми: акыркы тап убактысы жана максаты эстелет, тап бөгөттөлөт.
// ══════════════════════════════════════════════════════════════════
static std::chrono::steady_clock::time_point g_LastRetriTap{};
static uintptr_t g_LastRetriTarget = 0;
static constexpr long RETRI_TAP_LOCKOUT_MS = 2000;  // retri CD ~35s, 2s жетиштүү
bool  autoRetribution        = false;
bool  AutoRetributionRed     = false;
bool  AutoRetributionBlue    = false;
bool  AutoRetributionLord    = false;
bool  AutoRetributionTurtle  = false;
bool  AutoRetributionCrab    = false;
bool  AutoRetributionLito    = false;
float retriTouchX            = 1575;
float retriTouchY            = 661;

void DrawVerticalHealthBar(ImDrawList* Draw, float X, float Y, float Height,
                           float Health, float MaxHealth, bool ShowText = false) {
    if (MaxHealth <= 0) return;
    float HealthPercent = Health / MaxHealth;
    if (HealthPercent < 0.0f) HealthPercent = 0.0f;
    if (HealthPercent > 1.0f) HealthPercent = 1.0f;
    float BarWidth      = 5.0f;
    float BarGap        = 22.0f;
    float FilledHeight  = Height * HealthPercent;
    float BarX0         = X - BarGap - BarWidth;
    float BarX1         = BarX0 + BarWidth;
    float Top           = Y - Height * 0.5f;
    float Bottom        = Y + Height * 0.5f;
    Draw->AddRectFilled(ImVec2(BarX0 - 1.5f, Top    - 1.5f),
                        ImVec2(BarX1 + 1.5f, Bottom + 1.5f),
                        IM_COL32(0, 0, 0, 220), 2.0f);
    ImU32 HealthColor;
    if      (HealthPercent > 0.6f) HealthColor = IM_COL32(0,   255, 120, 255);
    else if (HealthPercent > 0.3f) HealthColor = IM_COL32(255, 210,   0, 255);
    else                           HealthColor = IM_COL32(255,  50,  50, 255);
    if (HealthPercent > 0) {
        Draw->AddRectFilled(ImVec2(BarX0, Bottom - FilledHeight),
                            ImVec2(BarX1, Bottom), HealthColor, 2.0f);
        Draw->AddRectFilled(ImVec2(BarX0, Bottom - FilledHeight),
                            ImVec2(BarX0 + 1.5f, Bottom),
                            IM_COL32(255, 255, 255, 60), 2.0f);
    }
    if (ShowText) {
        std::string HealthText = std::to_string((int)Health);
        ImVec2 TextSize = ImGui::CalcTextSize(HealthText.c_str());
        ImVec2 TextPos(BarX0 - TextSize.x - 4.0f,
                       Bottom - FilledHeight - TextSize.y * 0.5f);
        Draw->AddText(ImVec2(TextPos.x + 1, TextPos.y + 1),
                      IM_COL32(0,0,0,200), HealthText.c_str());
        Draw->AddText(TextPos, IM_COL32(255,255,255,230), HealthText.c_str());
    }
}

void DrawPremiumHealthBar(ImDrawList* draw, ImVec2 pos, float hp, float maxHp,
                          float width = 100.0f, float height = 8.0f,
                          bool showText = false) {
    if (maxHp <= 0) return;
    float hpPercent = hp / maxHp;
    if (hpPercent > 1.0f) hpPercent = 1.0f;
    if (hpPercent < 0.0f) hpPercent = 0.0f;
    ImVec2 min = ImVec2(pos.x - width * 0.5f, pos.y - height * 0.5f);
    ImVec2 max = ImVec2(pos.x + width * 0.5f, pos.y + height * 0.5f);
    draw->AddRectFilled(ImVec2(min.x - 1, min.y - 1),
                        ImVec2(max.x + 1, max.y + 1),
                        IM_COL32(0,0,0,220), 2.0f);
    draw->AddRectFilled(min, max, IM_COL32(50,50,50,255), 2.0f);
    ImU32 hpColor;
    if (hpPercent > 0.5f) hpColor = IM_COL32(255,200,  0,255);
    else                  hpColor = IM_COL32(255, 50, 50,255);
    if (hpPercent > 0) {
        ImVec2 hpMax = ImVec2(min.x + (width * hpPercent), max.y);
        draw->AddRectFilled(min, hpMax, hpColor, 2.0f);
        draw->AddRectFilled(min, ImVec2(hpMax.x, min.y + height * 0.4f),
                            IM_COL32(255,255,255,60), 2.0f);
    }
    for (int i = 1; i < 10; i++) {
        float x = min.x + (width * 0.1f * i);
        draw->AddLine(ImVec2(x, min.y), ImVec2(x, max.y),
                      IM_COL32(0,0,0,150), 1.0f);
    }
    if (showText) {
        char hpBuf[32];
        snprintf(hpBuf, sizeof(hpBuf), "%.0f / %.0f", hp, maxHp);
        ImVec2 textSize = ImGui::CalcTextSize(hpBuf);
        draw->AddText(ImVec2(pos.x - textSize.x * 0.5f + 1,
                             pos.y - height * 0.5f - textSize.y - 1),
                      IM_COL32(0,0,0,200), hpBuf);
        draw->AddText(ImVec2(pos.x - textSize.x * 0.5f,
                             pos.y - height * 0.5f - textSize.y - 2),
                      IM_COL32(255,255,255,255), hpBuf);
    }
}

void DrawGlobalWarning(ImDrawList* draw, const char* label,
                       float hp, float maxHp) {
    if (maxHp <= 0 || !drawAlertUnderAttack) return;
    float centerX  = abs_ScreenX * Alert_PosX;
    float centerY  = abs_ScreenY * Alert_PosY;
    float s        = Alert_Scale;
    ImVec2 labelSize = ImGui::CalcTextSize(label);
    float boxWidth  = 320.0f * s;
    float boxHeight = 70.0f  * s;
    float barWidth  = 280.0f * s;
    float barHeight = 22.0f  * s;
    ImVec2 boxMin(centerX - boxWidth * 0.5f, centerY - boxHeight * 0.5f);
    ImVec2 boxMax(centerX + boxWidth * 0.5f, centerY + boxHeight * 0.5f);
    draw->AddRectFilled(boxMin, boxMax, IM_COL32(15,15,15,230), 12.0f * s);
    draw->AddRect(boxMin, boxMax, IM_COL32(230,230,100,255), 12.0f * s, 0, 2.0f * s);
    ImVec2 labelPos(centerX - labelSize.x * s * 0.5f, boxMin.y + 12.0f * s);
    draw->AddText(NULL, 19.0f * s, labelPos, IM_COL32(255,255,255,255), label);
    ImVec2 barMin(centerX - barWidth * 0.5f, boxMax.y - barHeight - 12.0f * s);
    ImVec2 barMax(centerX + barWidth * 0.5f, boxMax.y - 12.0f * s);
    draw->AddRectFilled(barMin, barMax, IM_COL32(45,45,45,255), 8.0f * s);
    float hpPercent = hp / maxHp;
    if (hpPercent > 0) {
        if (hpPercent > 1.0f) hpPercent = 1.0f;
        float fillWidth = barWidth * hpPercent;
        draw->AddRectFilled(barMin, ImVec2(barMin.x + fillWidth, barMax.y),
                            IM_COL32(230,0,0,255), 8.0f * s);
    }
    if (Alert_ShowHPText) {
        char hpBuf[64];
        snprintf(hpBuf, sizeof(hpBuf), "%.0f / %.0f", hp, maxHp);
        ImVec2 hpSize = ImGui::CalcTextSize(hpBuf);
        ImVec2 hpPos(centerX - hpSize.x * s * 0.45f,
                     barMin.y + (barHeight - hpSize.y * s * 0.8f) * 0.5f);
        draw->AddText(NULL, 14.0f * s, hpPos, IM_COL32(255,255,255,255), hpBuf);
    }
}

// ─────────────────────────────────────────────
//  ESP BOX функциясы (душман каарманга гана)
//  Бокс ичинде: Hero иконкасы гана (өлчөм espBoxIconSize)
//  Скил/Cooldown логикасы мурдагындай сыртта калат
// ─────────────────────────────────────────────
float espBoxIconSize = 18.0f;   // бокс ичиндеги Hero иконка өлчөмү (сырдырма менен)

void DrawEspBox(ImDrawList* draw, Vector3 headPos, Vector3 footPos,
                int hp = 0, int maxHp = 0, int heroId = 0, int level = 0) {
    if (!drawEspBox) return;

    Vector2 headSc, footSc;
    if (!WorldToScreen(headPos, &headSc)) return;
    if (!WorldToScreen(footPos, &footSc)) return;

    // X/Y offset жана масштаб
    headSc.X += espBoxOffsetX; headSc.Y += espBoxOffsetY;
    footSc.X += espBoxOffsetX; footSc.Y += espBoxOffsetY;

    float height = footSc.Y - headSc.Y;
    if (height < 5.0f || height > 2000.0f) return;

    float width  = height * 0.6f * espBoxScale;
    height      *= espBoxScale;
    float cx     = (headSc.X + footSc.X) * 0.5f;
    float cy     = (headSc.Y + footSc.Y) * 0.5f;
    float left   = cx - width  * 0.5f;
    float right  = cx + width  * 0.5f;
    float top    = cy - height * 0.5f;
    float bottom = cy + height * 0.5f;

    ImU32 boxColor = IM_COL32(
        (int)(espBoxR * 255),
        (int)(espBoxG * 255),
        (int)(espBoxB * 255),
        (int)(espBoxA * 255));

    // ── Бокс чийүү (corner же толук)
    if (espBoxCornerStyle) {
        float cx = (right - left) * espBoxCornerLen;
        float cy = (bottom - top) * espBoxCornerLen;
        float t  = espBoxThickness;
        draw->AddLine(ImVec2(left,  top),    ImVec2(left + cx, top),        boxColor, t);
        draw->AddLine(ImVec2(left,  top),    ImVec2(left,  top + cy),       boxColor, t);
        draw->AddLine(ImVec2(right, top),    ImVec2(right - cx, top),       boxColor, t);
        draw->AddLine(ImVec2(right, top),    ImVec2(right, top + cy),       boxColor, t);
        draw->AddLine(ImVec2(left,  bottom), ImVec2(left + cx, bottom),     boxColor, t);
        draw->AddLine(ImVec2(left,  bottom), ImVec2(left,  bottom - cy),    boxColor, t);
        draw->AddLine(ImVec2(right, bottom), ImVec2(right - cx, bottom),    boxColor, t);
        draw->AddLine(ImVec2(right, bottom), ImVec2(right, bottom - cy),    boxColor, t);
    } else {
        draw->AddRect(ImVec2(left, top), ImVec2(right, bottom),
                      boxColor, 0.0f, 0, espBoxThickness);
    }

    // ── HP бар (боксту ОҢ ЖАГЫНДА вертикалдуу)
    if (drawMHealth && maxHp > 0) {
        float hpPercent = (float)hp / (float)maxHp;
        if (hpPercent < 0.0f) hpPercent = 0.0f;
        if (hpPercent > 1.0f) hpPercent = 1.0f;
        float barW   = espBoxHpBarSize;   // ── ЖАҢЫ: сыдырма менен жөнгө салынат
        float barX0  = right + 3.0f;
        float barX1  = barX0 + barW;
        float barH   = bottom - top;
        // Фон
        draw->AddRectFilled(ImVec2(barX0 - 1, top    - 1),
                            ImVec2(barX1 + 1, bottom + 1),
                            IM_COL32(0, 0, 0, 200), 2.0f);
        ImU32 hpCol;
        if      (hpPercent > 0.5f) hpCol = IM_COL32(0,   220, 90,  255); // >50% жашыл
        else if (hpPercent > 0.2f) hpCol = IM_COL32(255, 200,  0,  255); // 20-50% сары
        else                       hpCol = IM_COL32(255,  40, 40,  255); // <20% кызыл
        float filledH = barH * hpPercent;
        // HP жогорудан ылдыйга толтурат (жогору = толук)
        draw->AddRectFilled(ImVec2(barX0, bottom - filledH),
                            ImVec2(barX1, bottom),
                            hpCol, 2.0f);
    }
    // Hero иконкасы бокс ичинде — АЛЫНЫП САЛЫНДЫ
}

// ─────────────────────────────────────────────
//  Prediction namespace
// ─────────────────────────────────────────────
namespace Prediction {
    namespace Fields {
        constexpr size_t m_vCachePosition = 0x28c;
        constexpr size_t _MoveDir         = 0x2cc;
        constexpr size_t _vecFaceDir      = 0x2d8;
        constexpr size_t curSpeed         = 0x480;
        constexpr size_t m_dMoveSpeed     = 0x190;
        constexpr size_t m_dRunSpeed      = 0x1d0;
        constexpr size_t m_bIsInMoveControl = 0x372;
    }
    struct EntityPrediction {
        Vector3 position;
        Vector3 moveDir;
        float   speed;
        bool    isValid;
        bool    isMoving;
        float   lastUpdateTime;
    };
    struct PredictionCache {
        uintptr_t       entityAddr;
        EntityPrediction pred;
        bool            active;
    };
    PredictionCache predCache[150];
    constexpr int   MAX_CACHE           = 150;
    constexpr float VISUAL_SCALE        = 1.0f;
    constexpr float MIN_SPEED_THRESHOLD = 0.3f;
    constexpr float MAX_VISUAL_LENGTH   = 120.0f;

    inline float GetEntitySpeed(uintptr_t entityAddr) {
        float speed = Read<float>(entityAddr + Fields::curSpeed);
        if (speed > 0.01f) return speed;
        double moveSpeed = Read<double>(entityAddr + Fields::m_dMoveSpeed);
        if (moveSpeed > 0.01) return static_cast<float>(moveSpeed);
        double runSpeed  = Read<double>(entityAddr + Fields::m_dRunSpeed);
        return static_cast<float>(runSpeed);
    }
    inline Vector3 GetEntityMoveDir(uintptr_t entityAddr) {
        Vector3 moveDir = Read<Vector3>(entityAddr + Fields::_MoveDir);
        float len = sqrtf(moveDir.X*moveDir.X + moveDir.Y*moveDir.Y + moveDir.Z*moveDir.Z);
        if (len > 0.1f) return {moveDir.X/len, moveDir.Y/len, moveDir.Z/len};
        Vector3 faceDir = Read<Vector3>(entityAddr + Fields::_vecFaceDir);
        len = sqrtf(faceDir.X*faceDir.X + faceDir.Y*faceDir.Y + faceDir.Z*faceDir.Z);
        if (len > 0.1f) return {faceDir.X/len, faceDir.Y/len, faceDir.Z/len};
        return {0,0,0};
    }
    void UpdatePrediction(uintptr_t entityAddr, Vector3 currentPos, float currentTime) {
        for (int i = 0; i < MAX_CACHE; i++) {
            if (predCache[i].active && predCache[i].entityAddr == entityAddr) {
                float dt      = currentTime - predCache[i].pred.lastUpdateTime;
                Vector3 oldPos = predCache[i].pred.position;
                float speed   = GetEntitySpeed(entityAddr) * 0.013f;
                Vector3 moveDir = GetEntityMoveDir(entityAddr);
                if (dt > 0.001f && dt < 0.2f &&
                    (currentPos.X != oldPos.X || currentPos.Z != oldPos.Z)) {
                    Vector3 velocity = {(currentPos.X - oldPos.X)/dt, 0,
                                        (currentPos.Z - oldPos.Z)/dt};
                    float realSpeed  = sqrtf(velocity.X*velocity.X + velocity.Z*velocity.Z);
                    if (realSpeed > 0.3f) {
                        speed   = realSpeed;
                        moveDir = {velocity.X/realSpeed, 0, velocity.Z/realSpeed};
                    }
                }
                predCache[i].pred.position      = currentPos;
                predCache[i].pred.moveDir       = moveDir;
                predCache[i].pred.speed         = speed;
                predCache[i].pred.isValid       = true;
                predCache[i].pred.isMoving      = (speed > 0.1f);
                predCache[i].pred.lastUpdateTime = currentTime;
                return;
            }
        }
        for (int i = 0; i < MAX_CACHE; i++) {
            if (!predCache[i].active) {
                predCache[i].entityAddr         = entityAddr;
                predCache[i].pred.position      = currentPos;
                predCache[i].pred.moveDir       = GetEntityMoveDir(entityAddr);
                predCache[i].pred.speed         = GetEntitySpeed(entityAddr) * 0.013f;
                predCache[i].pred.isValid       = true;
                predCache[i].pred.isMoving      = (predCache[i].pred.speed > 0.1f);
                predCache[i].pred.lastUpdateTime = currentTime;
                predCache[i].active             = true;
                return;
            }
        }
    }
    EntityPrediction* GetPrediction(uintptr_t entityAddr) {
        for (int i = 0; i < MAX_CACHE; i++)
            if (predCache[i].active && predCache[i].entityAddr == entityAddr)
                return &predCache[i].pred;
        return nullptr;
    }
    void InvalidatePrediction(uintptr_t entityAddr) {
        for (int i = 0; i < MAX_CACHE; i++)
            if (predCache[i].active && predCache[i].entityAddr == entityAddr) {
                predCache[i].active = false;
                return;
            }
    }
    void CleanupStale(float currentTime, float maxAge = 2.0f) {
        for (int i = 0; i < MAX_CACHE; i++)
            if (predCache[i].active &&
                (currentTime - predCache[i].pred.lastUpdateTime) > maxAge)
                predCache[i].active = false;
    }
    inline float CalcVisualLength(float speed) {
        return fminf(speed * 8.0f * VISUAL_SCALE, MAX_VISUAL_LENGTH);
    }
    inline ImU32 GetSpeedColor(float speed) {
        float clamped = fminf(fmaxf(speed, 1.0f), 20.0f);
        float t = (clamped - 1.0f) / 19.0f;
        if (t < 0.33f) {
            float lt = t / 0.33f;
            return IM_COL32((int)(255.0f*lt), 255, 0, 200);
        } else if (t < 0.66f) {
            float lt = (t - 0.33f) / 0.33f;
            return IM_COL32(255, (int)(255.0f*(1.0f - lt*0.5f)), 0, 200);
        } else {
            float lt = (t - 0.66f) / 0.34f;
            return IM_COL32(255, (int)(127.0f*(1.0f - lt)), 0, 200);
        }
    }
}

bool  drawMPrediction          = false;
bool  predictionColorBySpeed   = true;
ImColor predictionLineColor    = ImColor(0,   255, 255, 180);
ImColor predictionCircleColor  = ImColor(255, 255, 255, 190);
float predictionCircleRadius   = 5.0f;
bool  predictionShowOnlyMoving = true;
bool  predictionShowArrowHead  = true;
bool  predictionShowSpeedText  = false;

void DrawPrediction(ImDrawList* draw, Vector2 screenPos, Vector3 worldPos,
                    uintptr_t entityAddr) {
    using namespace Prediction;
    float currentTime = static_cast<float>(std::clock()) / CLOCKS_PER_SEC;
    UpdatePrediction(entityAddr, worldPos, currentTime);
    EntityPrediction* pred = GetPrediction(entityAddr);
    if (!pred || !pred->isValid) return;
    if (predictionShowOnlyMoving && !pred->isMoving) return;
    if (pred->moveDir.X == 0 && pred->moveDir.Y == 0 && pred->moveDir.Z == 0) return;
    float TIME_VISUAL_FACTOR = predictionLineLengthMult;
    Vector3 predictedWorld = {
        worldPos.X + pred->moveDir.X * pred->speed * TIME_VISUAL_FACTOR,
        worldPos.Y + pred->moveDir.Y * pred->speed * TIME_VISUAL_FACTOR,
        worldPos.Z + pred->moveDir.Z * pred->speed * TIME_VISUAL_FACTOR};
    Vector2 predictedScreen;
    if (!WorldToScreen(predictedWorld, &predictedScreen)) return;
    float lineLength    = CalcVisualLength(pred->speed);
    ImU32 lineColor     = predictionColorBySpeed
                          ? GetSpeedColor(pred->speed)
                          : IM_COL32(0, 255, 255, 200);
    float lineThickness = 1.5f + fminf(pred->speed * 0.1f, 2.0f);
    draw->AddLine(ImVec2(screenPos.X,    screenPos.Y),
                  ImVec2(predictedScreen.X, predictedScreen.Y),
                  lineColor, lineThickness);
    if (predictionShowArrowHead && lineLength > 20.0f) {
        float arrowSize = fminf(10.0f, lineLength * 0.3f);
        float angle     = atan2f(predictedScreen.Y - screenPos.Y,
                                 predictedScreen.X - screenPos.X);
        ImVec2 arrow1 = {predictedScreen.X - arrowSize * cosf(angle - 0.4f),
                         predictedScreen.Y - arrowSize * sinf(angle - 0.4f)};
        ImVec2 arrow2 = {predictedScreen.X - arrowSize * cosf(angle + 0.4f),
                         predictedScreen.Y - arrowSize * sinf(angle + 0.4f)};
        draw->AddLine(ImVec2(predictedScreen.X, predictedScreen.Y),
                      arrow1, lineColor, lineThickness * 0.9f);
        draw->AddLine(ImVec2(predictedScreen.X, predictedScreen.Y),
                      arrow2, lineColor, lineThickness * 0.9f);
    }
    draw->AddCircle(ImVec2(predictedScreen.X, predictedScreen.Y),
                    predictionCircleRadius, predictionCircleColor, 14, 1.2f);
    draw->AddCircleFilled(ImVec2(predictedScreen.X, predictedScreen.Y),
                          predictionCircleRadius * 0.35f, predictionCircleColor);
    if (predictionShowSpeedText && pred->speed > 1.0f) {
        char speedTxt[12];
        snprintf(speedTxt, sizeof(speedTxt), xorstr_("%.1f"), pred->speed);
        ImVec2 txtSize = ImGui::CalcTextSize(speedTxt);
        draw->AddText(ImVec2(predictedScreen.X + 6.0f,
                             predictedScreen.Y - txtSize.y * 0.5f),
                      IM_COL32(255,255,255,220), speedTxt);
    }
}

uintptr_t GetLogicBattleManagerInstance() {
    auto logicBattleManager = Read<uintptr_t>(libbase + OFF_BLC(BasePtr));
    if (!logicBattleManager) return 0;
    auto staticPtr  = Read<uintptr_t>(logicBattleManager + OFF_BLC(PtrChain1));
    if (!staticPtr)  return 0;
    auto instancePtr = Read<uintptr_t>(staticPtr);
    if (!instancePtr) return 0;
    return instancePtr;
}

struct CoolDownData {
    int skill1, skill2, skill3, skill4, spell, regen;
    int spellId;
    CoolDownData()
        : skill1(0), skill2(0), skill3(0), skill4(-1),
          spell(0), regen(0), spellId(0) {}
};

struct CoolDownTimerState {
    uint32_t startTime = 0;
    uint32_t durationMs = 0;
    std::chrono::steady_clock::time_point observedAt{};
};
std::unordered_map<uintptr_t, CoolDownTimerState> g_CoolDownTimers;

bool    ESP_Player_Cooldown  = true;
bool    Cooldown_ShowSpell   = true;
bool    Cooldown_CompactStyle = false;
ImColor Cooldown_ColorReady  = ImColor(0,   255, 0,   255);
ImColor Cooldown_ColorCD     = ImColor(255,  80, 80,  255);
float   Cooldown_OffsetY     = 150.0f;
float   Cooldown_Size        = 15.0f;

// ── ОПТИМИЗАЦИЯ #3: чыныгы окуу функциясы (кэштин артында)
static CoolDownData getCoolDownUncached(uintptr_t show_entity) {
    DRIVER_LOCK();   // ── KERNEL-SAFE FIX: драйверге жалгыз кайрылуу
    CoolDownData result;
    auto cdComp = Read<uintptr_t>(show_entity + OFF_SE(m_ShowCoolDownComp));
    if (!IsUserAddr(cdComp)) return result;   // ── KERNEL-SAFE FIX
    auto dicCoolInfoPtr = Read<uintptr_t>(cdComp + OFF_SCC(_dicCD));
    if (!IsUserAddr(dicCoolInfoPtr)) return result;   // ── KERNEL-SAFE FIX
    monoDictionary<int, uintptr_t> dict{};
    dict.klass        = reinterpret_cast<void*>(Read<uintptr_t>(dicCoolInfoPtr + 0x0));
    dict.obj          = reinterpret_cast<void*>(Read<uintptr_t>(dicCoolInfoPtr + 0x8));
    dict.buckets      = reinterpret_cast<int32_t*>(Read<uintptr_t>(dicCoolInfoPtr + 0x10));
    dict.arrayEntries = Read<uintptr_t>(dicCoolInfoPtr + 0x18);
    dict.count        = Read<int>(dicCoolInfoPtr + 0x20);
    // ── KERNEL-SAFE FIX: count чеги + дарек текшерүү.
    //    Мурда count <= 0 гана текшерилчү. Бузулган count (мис. 0x7FFFFFFF)
    //    get_mono_dictionary_vector ичинде алп өлчөмдөгү окууга айланып,
    //    кернел драйверди heap overflow'го алып келчү.
    if (!IsUserAddr(dict.arrayEntries)) return result;
    if (dict.count <= 0 || dict.count > MAX_MONO_ENTRIES) return result;

    int playerSpellId = Read<int>(show_entity + OFF_SE(SummonSkillId));
    if (playerSpellId <= 0 || playerSpellId > 100) {
        int heroId = Read<int>(show_entity + OFF_SE(HeroID));
        uint64_t playerGuid = Read<uint64_t>(show_entity + OFF_SE(m_uGuid));
        std::lock_guard<std::mutex> lock(g_RoomDataMutex);
        bool found = false;
        for (auto& p : g_CachedRoomData.TeamA) {
            if (p.HeroId == heroId || p.UserID == std::to_string(playerGuid)) {
                playerSpellId = p.SpellId; found = true; break;
            }
        }
        if (!found) {
            for (auto& p : g_CachedRoomData.TeamB) {
                if (p.HeroId == heroId || p.UserID == std::to_string(playerGuid)) {
                    playerSpellId = p.SpellId; break;
                }
            }
        }
    }
    result.spellId = playerSpellId;

    auto items = get_mono_dictionary_vector<int, uintptr_t>(dict);
    for (const auto& entry : items) {
        auto skillID         = entry.key;
        auto coolDownDataAddr = entry.value;
        if (skillID <= 0 || !coolDownDataAddr) continue;

        // ── FIX #8: 2101 < 20000 болгондуктан ал шарт иштебейт,
        //            туура exclude тизмесин колдонобуз
        if (result.spellId == 0 && skillID >= 20000 && skillID < 22000
            && skillID != 21010 && skillID != 20010) {
            result.spellId = skillID;
        }

        const uint32_t startTime = Read<uint32_t>(coolDownDataAddr + OFF_CDD(uiStartTime));
        const uint32_t coolTime  = Read<uint32_t>(coolDownDataAddr + OFF_CDD(uiCoolTime));
        if (coolTime == 0) continue;

        const auto currentTime = std::chrono::steady_clock::now();
        auto& timer = g_CoolDownTimers[coolDownDataAddr];
        if (timer.startTime != startTime ||
            timer.observedAt == std::chrono::steady_clock::time_point{}) {
            timer.startTime  = startTime;
            timer.durationMs = coolTime;
            timer.observedAt = currentTime;
        }
        const auto elapsedMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime - timer.observedAt).count());
        const uint32_t remainingTime = (elapsedMs >= timer.durationMs)
                                       ? 0
                                       : timer.durationMs - elapsedMs;
        if (remainingTime == 0) continue;
        const int timeLeftSec = static_cast<int>((remainingTime + 999u) / 1000u);
        auto fSkillID = skillID % 100;

        if (skillID < 20000) {
            switch (fSkillID) {
                case 10: if (timeLeftSec > result.skill1) result.skill1 = timeLeftSec; break;
                case 20: if (timeLeftSec > result.skill2) result.skill2 = timeLeftSec; break;
                case 30: if (timeLeftSec > result.skill3) result.skill3 = timeLeftSec; break;
                case 40: if (timeLeftSec > result.skill4) result.skill4 = timeLeftSec; break;
            }
        } else {
            if (skillID == 21010 || skillID == 20010) {
                if (timeLeftSec > result.regen) result.regen = timeLeftSec;
            } else if (skillID == playerSpellId || (skillID / 10) == playerSpellId) {
                if (timeLeftSec > result.spell) result.spell = timeLeftSec;
            } else if (fSkillID == 10 || (skillID <= 20500 && (skillID % 10) == 0)) {
                if (timeLeftSec > result.spell) result.spell = timeLeftSec;
            }
        }
    }
    return result;
}

// ══════════════════════════════════════════════════════════════════════
//  ОПТИМИЗАЦИЯ #3 + #5 — COOLDOWN КЭШИ жана MAP ТАЗАЛОО
//
//  Мурда: getCoolDown() ар душманга, ар кадрда чакырылчу.
//    Ар чакырууда бүт Mono dictionary кыдырылат (ондогон драйвер окуусу).
//    5 душман × 62 fps = секундасына ~310 dictionary кыдыруу.
//
//  Эми: натыйжа 100ms кэштелет → секундасына ~50 кыдыруу (6 эсе аз).
//    Cooldown бүтүн секунд менен көрсөтүлөт, 100ms кечигүү көзгө билинбейт.
//
//  Ошондой эле g_CoolDownTimers мурда эч качан тазаланчу эмес — матч бою
//  өсө берчү. Эми экөө тең чектен ашканда же оюн жабылганда тазаланат.
// ══════════════════════════════════════════════════════════════════════
struct CDCacheEntry {
    CoolDownData data;
    std::chrono::steady_clock::time_point at{};
};
static std::unordered_map<uintptr_t, CDCacheEntry> g_CDCache;
static constexpr int    CD_CACHE_MS      = 100;
static constexpr size_t CD_MAP_MAX       = 256;

// Эс тутум агып кетпеши үчүн: чектен ашса же оюн жабылса тазалайт.
void PruneCooldownMaps(bool force = false) {
    if (force || g_CDCache.size()       > CD_MAP_MAX) g_CDCache.clear();
    if (force || g_CoolDownTimers.size() > CD_MAP_MAX) g_CoolDownTimers.clear();
}

CoolDownData getCoolDown(uintptr_t show_entity) {
    const auto now = std::chrono::steady_clock::now();
    auto it = g_CDCache.find(show_entity);
    if (it != g_CDCache.end()) {
        auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - it->second.at).count();
        if (ageMs < CD_CACHE_MS) return it->second.data;   // кэштен — драйверге тийбейт
    }
    CoolDownData fresh = getCoolDownUncached(show_entity);
    g_CDCache[show_entity] = CDCacheEntry{fresh, now};
    PruneCooldownMaps();
    return fresh;
}

void DrawCircularIndicator(ImDrawList* Draw, ImVec2 pos, float size,
                           int cdVal, const char* label,
                           ImTextureID icon = nullptr) {
    bool  isReady   = (cdVal <= 0);
    ImU32 ringColor = isReady ? IM_COL32(0,255,150,200) : IM_COL32(60,60,60,200);
    Draw->AddCircleFilled(pos, size, IM_COL32(15,15,15,230), 20);
    Draw->AddCircle(pos, size, ringColor, 20, 2.0f);
    if (icon) {
        Draw->AddImage(icon,
                       ImVec2(pos.x - size + 2, pos.y - size + 2),
                       ImVec2(pos.x + size - 2, pos.y + size - 2));
    } else {
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        Draw->AddText(ImVec2(pos.x - labelSize.x * 0.5f,
                             pos.y - labelSize.y * 0.5f),
                      IM_COL32(200,200,200,255), label);
    }
    if (!isReady) {
        Draw->AddCircleFilled(pos, size - 1.0f, IM_COL32(0,0,0,180), 20);
        std::string txt = std::to_string(cdVal);
        ImVec2 tSize = ImGui::CalcTextSize(txt.c_str());
        Draw->AddText(ImVec2(pos.x - tSize.x*0.5f + 1, pos.y - tSize.y*0.5f + 1),
                      IM_COL32(0,0,0,255), txt.c_str());
        Draw->AddText(ImVec2(pos.x - tSize.x*0.5f, pos.y - tSize.y*0.5f),
                      IM_COL32(255,255,255,255), txt.c_str());
    }
}

inline float DrawCooldownHorizontal(ImDrawList* Draw, float StartX, float BaselineY,
                                     const CoolDownData& cd, int HeroID,
                                     int hp, int maxHp, int level,
                                     bool showSkills, bool showHero, bool showSpell) {
    float size    = Cooldown_Size;
    float spacing = size * 2.3f;
    float currentX = StartX + size;
    if (showHero) {
        DrawHeroIcon(Draw, ImVec2(currentX, BaselineY - size - 2.0f),
                     HeroID, hp, maxHp, size, true, level);
        currentX += spacing;
    }
    if (showSkills) {
        DrawCircularIndicator(Draw, ImVec2(currentX, BaselineY-size-2.0f), size,
                              cd.skill1, xorstr_("S1"), GetSkillIconTexture(HeroID,1)); currentX += spacing;
        DrawCircularIndicator(Draw, ImVec2(currentX, BaselineY-size-2.0f), size,
                              cd.skill2, xorstr_("S2"), GetSkillIconTexture(HeroID,2)); currentX += spacing;
        DrawCircularIndicator(Draw, ImVec2(currentX, BaselineY-size-2.0f), size,
                              cd.skill3, xorstr_("S3"), GetSkillIconTexture(HeroID,3)); currentX += spacing;
    }
    if (showSkills && showSpell) {
        DrawCircularIndicator(Draw, ImVec2(currentX, BaselineY-size-2.0f), size,
                              cd.spell, xorstr_("SP"), GetSpellTexture(cd.spellId));
        currentX += spacing;
        if (cd.regen > 0) {
            DrawCircularIndicator(Draw, ImVec2(currentX, BaselineY-size-2.0f), size,
                                  cd.regen, xorstr_("RG"), GetSkillIconTexture(0,20010));
            currentX += spacing;
        }
    }
    return currentX - StartX - (spacing - size);
}

// ── FIX #9: int кайтарат, float/int аралашмасын болтурбайбыз
int CalculateRetriDamage(int m_Level) {
    if (m_Level < 4)
        return 520 + 80 * m_Level;
    else
        return static_cast<int>(1.5f * (float)(520 + 80 * m_Level));
}

void ProcessAutoRetribution(uintptr_t selfp, long monsterListPtr, int stopMonster) {
    if (!autoRetribution) return;
    int   myLevel    = Read<int>(selfp + OFF_SE(Level));
    int   retriDmg   = CalculateRetriDamage(myLevel);
    Vector3 myPos;
    vm_readv(selfp + OFF_SE(vCachePosition), &myPos, sizeof(myPos));

    float     closestDist         = 99999.0f;
    uintptr_t closestMonsterAddr  = 0;
    // ── FIX #10: int колдонуу
    int       closestMonsterHP    = 0;

    for (int i = 0; i < stopMonster; i++) {
        auto Objaddr = ReadPtr(monsterListPtr + (i << 3));
        if (!Objaddr || Read<bool>(Objaddr + OFF_SE(bDeath))) continue;
        auto mHeroID = Read<int>(Objaddr + OFF_SE(HeroID));
        // ── FIX #10: uint64_t → int overflow жоготуу
        int Health   = static_cast<int>(Read<int>(Objaddr + OFF_SE(Hp)));
        Vector3 monPos;
        vm_readv(Objaddr + OFF_SE(vCachePosition), &monPos, sizeof(monPos));
        float dist = Vector3::Distance(myPos, monPos);
        bool isTarget = false;
        if      (AutoRetributionLord   && mHeroID == 2002)                   isTarget = true;
        else if (AutoRetributionTurtle && (mHeroID == 2003 || mHeroID == 2110)) isTarget = true;
        else if (AutoRetributionBlue   && (mHeroID == 2005 || mHeroID == 2221)) isTarget = true;
        else if (AutoRetributionLito   && mHeroID == 2056)                   isTarget = true;
        else if (AutoRetributionCrab   && (mHeroID == 2223 || mHeroID == 2222)) isTarget = true;
        else if (AutoRetributionRed    && (mHeroID == 2004 || mHeroID == 2220)) isTarget = true;
        if (isTarget && dist < closestDist) {
            closestDist        = dist;
            closestMonsterAddr = Objaddr;
            closestMonsterHP   = Health;
        }
    }
    if (closestMonsterAddr != 0 && closestDist <= 8.0f) {
        if (closestMonsterHP > 0 && closestMonsterHP <= retriDmg) {
            int realHP = Read<int>(closestMonsterAddr + OFF_SE(Hp));
            if (realHP > 0 && realHP <= retriDmg) {
                // ── БАГ ОҢДОО: тап спамын токтотуу
                const auto now = std::chrono::steady_clock::now();
                const long sinceMs = (long)std::chrono::duration_cast<
                        std::chrono::milliseconds>(now - g_LastRetriTap).count();
                const bool newTarget = (closestMonsterAddr != g_LastRetriTarget);

                // Жаңы максатка тезирээк, ошол эле максатка узак бөгөт
                if (sinceMs >= RETRI_TAP_LOCKOUT_MS ||
                    (newTarget && sinceMs >= 500)) {
                    Touch_Tap(retriTouchX, retriTouchY);
                    g_LastRetriTap    = now;
                    g_LastRetriTarget = closestMonsterAddr;
                }
            }
        }
    }
    if (showRetriCircle) {
        ImGui::GetBackgroundDrawList()->AddCircleFilled(
            ImVec2(retriTouchX, retriTouchY), 18.0f,
            IM_COL32(255,255,255,180), 16);
    }
}

void DrawNeedleLine(ImDrawList* draw, ImVec2 p1, ImVec2 p2,
                   ImU32 col, float thickness) {
    ImVec2 dir   = {p2.x - p1.x, p2.y - p1.y};
    float length = sqrtf(dir.x*dir.x + dir.y*dir.y);
    if (length < 1.0f) return;
    dir.x /= length; dir.y /= length;
    ImVec2 norm = {-dir.y * thickness, dir.x * thickness};
    ImVec2 mid  = {(p1.x + p2.x)*0.5f, (p1.y + p2.y)*0.5f};
    ImVec2 pts[4];
    pts[0] = p1;
    pts[1] = {mid.x + norm.x, mid.y + norm.y};
    pts[2] = p2;
    pts[3] = {mid.x - norm.x, mid.y - norm.y};
    draw->AddConvexPolyFilled(pts, 4, col);
}

// ══════════════════════════════════════════════════════════════════════
//  ОПТИМИЗАЦИЯ #2 + #4 — КАДРГА БИР ЖОЛКУ КЭШ
//
//  Мурда:
//    • pointer chain (libbase→a1→a2→a32) кадрына 4 ЖОЛУ кыдырылчу
//      (DrawMonster, DrawMinimapESP, MonsterRetribution, game_worker)
//    • player тизмеси кадрына ЭКИ ЖОЛУ толук окулчу
//      (DrawMonster жана DrawMinimapESP өз-өзүнчө)
//    • ар душманга 8 окуу × 2 жолу = ашыкча 80 драйвер чакыруусу/кадр
//
//  Эми: chain бир жолу кыдырылат, душмандар бир жолу окулат,
//  экөө тең ушул кэштен алат. Драйвер жүктөмү ~2 эсе азаят.
// ══════════════════════════════════════════════════════════════════════
struct EnemySnap {
    uintptr_t addr   = 0;
    int       heroId = 0;
    int       hp     = 0;
    int       maxHp  = 0;
    int       level  = 0;
    Vector3   pos{};
};

struct FrameCache {
    uint64_t               frameId = (uint64_t)-1;
    long                   a32     = 0;
    bool                   valid   = false;
    std::vector<EnemySnap> enemies;
};
static FrameCache g_Frame;
static uint64_t   g_FrameId = 0;   // ар кадрда Layout_tick_UI башында өсөт

// ── UI: реалдуу FPS жана кадр убактысы (INFO табында көрсөтүлөт)
float g_UI_Fps     = 0.0f;
float g_UI_FrameMs = 0.0f;

// Кадрга бир жолу: chain + бардык душмандарды окуйт.
// Кайра чакырылса дароо кэштен кайтат — драйверге тийбейт.
static bool BuildFrameCache() {
    if (g_Frame.frameId == g_FrameId) return g_Frame.valid;

    g_Frame.frameId = g_FrameId;
    g_Frame.valid   = false;
    g_Frame.a32     = 0;
    g_Frame.enemies.clear();

    if (!is_attached || libbase == 0) return false;

    DRIVER_LOCK();   // ── KERNEL-SAFE: драйверге жалгыз кайрылуу

    // ── Туура (обфускацияланбаган) chain — бардык жерде бирдей
    long a1 = ReadPtr(libbase + OFF_BM(BasePtr));
    if (!IsUserAddr(a1)) return false;
    long a2 = ReadPtr(a1 + OFF_BM(PtrChain1));
    if (!IsUserAddr(a2)) return false;
    long a32 = ReadPtr(a2);
    if (!IsUserAddr(a32)) return false;

    g_Frame.a32   = a32;
    g_Frame.valid = true;   // chain жарактуу — душман болбосо да

    long showList = ReadPtr(a32 + OFF_BM(ShowPlayers));
    if (!IsUserAddr(showList)) return true;
    long pdata = ReadPtr(showList + OFF_BM(ListDataOffset));
    if (!IsUserAddr(pdata)) return true;

    uint pcount = Read<uint>(showList + OFF_BM(ListCountOffset));
    if (pcount > MAX_PLAYER_ENTITY) return true;   // ── KERNEL-SAFE чеги

    long pbase = pdata + OFF_BM(ListArrayOffset);
    g_Frame.enemies.reserve(pcount);

    for (uint i = 0; i < pcount; i++) {
        long o = ReadPtr(pbase + (i << 3));
        if (!IsUserAddr(o)) continue;
        if (Read<bool>(o + OFF_SE(bSameCampType))) continue;
        if (Read<bool>(o + OFF_SE(bDeath))) {
            Prediction::InvalidatePrediction(o);
            continue;
        }
        EnemySnap e;
        e.addr  = o;
        e.hp    = Read<int>(o + OFF_SE(Hp));
        if (e.hp <= 0) continue;
        e.maxHp = Read<int>(o + OFF_SE(HpMax));
        if (e.maxHp <= 0) continue;
        e.heroId = Read<int>(o + OFF_SE(HeroID));
        e.level  = Read<int>(o + OFF_SE(Level));
        vm_readv(o + OFF_SE(vCachePosition), &e.pos, sizeof(e.pos));
        g_Frame.enemies.push_back(e);
    }
    return true;
}

void DrawMonster(ImDrawList* Draw) {
    if (!is_attached || libbase == 0) return;
    DRIVER_LOCK();   // ── KERNEL-SAFE FIX: драйверге жалгыз кайрылуу
    using namespace Prediction;
    float currentTime = static_cast<float>(std::clock()) / CLOCKS_PER_SEC;
    CleanupStale(currentTime);
    if (abs_ScreenX < abs_ScreenY) return;

    float lineSize = abs_ScreenY / 432;

    // ── ОПТИМИЗАЦИЯ #2: chain кадрга бир жолу кыдырылат (кэштен алынат)
    if (!BuildFrameCache()) return;
    long a32 = g_Frame.a32;
    long selfp = ReadPtr(a32 + OFF_BM(LocalPlayerShow));
    if (!IsUserAddr(selfp)) return;

    auto main_cam = GetMainCamera();
    if (!main_cam) return;
    auto camera = Read<uintptr_t>(main_cam + OFF_CAM(SmoothFollow));
    if (!camera) return;
    // ── DRONE VIEW АЛЫНЫП САЛЫНДЫ: мурда бул жерде камеранын FOV'уна
    //    Write<float> жасалчу. Эми оюндун эс тутумуна эч нерсе ЖАЗЫЛБАЙТ.

    auto ViewMatrix = Read<Camera>(camera + OFF_CAM(ViewMatrix));
    _vMatrix = ViewMatrix.projectionMatrix * ViewMatrix.worldToCameraMatrix;

    // ── FIX #1: shadow variable жок кылдык
    Vector3 myWorldPos;
    vm_readv(selfp + OFF_SE(vCachePosition), &myWorldPos, sizeof(myWorldPos));
    Vector2 myScreenPos;
    bool    myIsInside = WorldToScreen(myWorldPos, &myScreenPos);

    // ── ОПТИМИЗАЦИЯ #4: душмандар кадрга бир жолу окулду, кэште даяр
    //    (мурда бул тизме DrawMinimapESP'те дагы кайра окулчу)

    long showMonstersPtr = ReadPtr(a32 + OFF_BM(ShowMonsters));
    long monsterBase  = 0;
    uint monsterCount = 0;
    if (IsUserAddr(showMonstersPtr)) {   // ── KERNEL-SAFE FIX
        long monsterData = ReadPtr(showMonstersPtr + OFF_BM(ListDataOffset));
        if (IsUserAddr(monsterData)) {
            monsterBase  = monsterData + OFF_BM(ListArrayOffset);
            monsterCount = Read<uint>(showMonstersPtr + OFF_BM(ListCountOffset));
            if (monsterCount > MAX_MONSTER_ENTITY) {
                monsterBase  = 0;
                monsterCount = 0;
            }
        }
    }

    if (monsterBase && monsterCount)
        ProcessAutoRetribution(selfp, monsterBase, monsterCount);

    // ── PLAYER LOOP — ОПТИМИЗАЦИЯ #4: кэштен, кайра окулбайт
    for (const auto& en : g_Frame.enemies) {
        const uintptr_t Objaddr   = en.addr;
        const int       HeroID    = en.heroId;
        const int       Health    = en.hp;
        const int       maxHealth = en.maxHp;
        const int       level     = en.level;
        const Vector3   enemyPos  = en.pos;

        Vector2 en_posSc;
        bool isInside = WorldToScreen(enemyPos, &en_posSc);
        if (!isInside) continue;

        float   IconSize   = 35.0f;
        Vector2 HeroPos    = en_posSc;
        Vector2 Res;
        bool    isOutScreen;

        if (HeroPos.X < 0 || HeroPos.X > abs_ScreenX ||
            HeroPos.Y < 0 || HeroPos.Y > abs_ScreenY) {
            isOutScreen = true;
            IconSize = 42.0f;
            FindPoint(HeroPos, Res, abs_ScreenX, abs_ScreenY, (int)(IconSize/3));
        } else {
            isOutScreen = false;
            Res         = HeroPos;
        }

        // ── ESP BOX (душман каарманга гана, HeroID < 1000 → player)
        //    Бокс ичинде Hero иконкасы гана чийилет
        if (drawEspBox && !isOutScreen && HeroID < 1000) {
            Vector3 headPos = {enemyPos.X, enemyPos.Y + 1.8f, enemyPos.Z};
            Vector3 footPos = {enemyPos.X, enemyPos.Y,        enemyPos.Z};
            DrawEspBox(Draw, headPos, footPos, Health, maxHealth, HeroID, level);
        }

        // ── ESP Line: түз АК сызык, калыңдыгы espLineThickness
        if (drawMLine && myIsInside && HeroID < 1000) {
            ImVec2 fromPt = ImVec2(myScreenPos.X, myScreenPos.Y);
            ImVec2 toPt;
            if (espLineFromFooter) {
                Vector3 footPos3 = {enemyPos.X, enemyPos.Y, enemyPos.Z};
                Vector2 footSc2;
                if (WorldToScreen(footPos3, &footSc2)) {
                    footSc2.X += espBoxOffsetX;
                    footSc2.Y += espBoxOffsetY;
                    toPt = ImVec2(footSc2.X, footSc2.Y);
                } else {
                    toPt = ImVec2(en_posSc.X, en_posSc.Y);
                }
                Vector3 myFootPos = {myWorldPos.X, myWorldPos.Y, myWorldPos.Z};
                Vector2 myFootSc;
                if (WorldToScreen(myFootPos, &myFootSc))
                    fromPt = ImVec2(myFootSc.X, myFootSc.Y);
            } else {
                toPt = ImVec2(en_posSc.X, en_posSc.Y);
            }
            // Түз ак сызык (AddLine)
            Draw->AddLine(fromPt, toPt,
                          IM_COL32(255, 255, 255, 200), espLineThickness);
        }

        // ── Cooldown / Hero icon сызык логикасы — МУРДАГЫНДАЙ, өзгөрүүсүз
        if ((iconhero || ESP_Player_Cooldown) && !isOutScreen && HeroID < 1000) {
            float   offsetSide = 80.0f;
            float   offsetUp   = Avatar_OffsetY;
            ImVec2  p0         = ImVec2(Res.X, Res.Y);
            ImVec2  p1         = ImVec2(p0.x + offsetSide, p0.y - offsetUp - 30.0f);
            auto    cd         = getCoolDown(Objaddr);
            float   totalWidth = DrawCooldownHorizontal(Draw, p1.x, p1.y, cd,
                                                        HeroID, Health, maxHealth, level,
                                                        ESP_Player_Cooldown,
                                                        iconhero,
                                                        Cooldown_ShowSpell);
            ImVec2 p2 = ImVec2(p1.x + totalWidth, p1.y);
            Draw->AddLine(p0, p1, IM_COL32(255,255,255,200), 1.5f);
            Draw->AddLine(p1, p2, IM_COL32(255,255,255,200), 1.5f);
            Draw->AddCircleFilled(p0, 3.0f, IM_COL32(0,255,0,255), 10);
        }

        if (drawMPrediction)
            DrawPrediction(Draw, HeroPos, enemyPos, Objaddr);
    }

    // ── MONSTER LOOP
    for (uint i = 0; i < monsterCount; i++) {
        auto Objaddr = ReadPtr(monsterBase + (i << 3));
        if (!IsUserAddr(Objaddr)) continue;   // ── KERNEL-SAFE FIX
        if (Read<bool>(Objaddr + OFF_SE(bSameCampType))) continue;

        auto mHeroID = Read<int>(Objaddr + OFF_SE(HeroID));
        auto type    = Read<int>(Objaddr + OFF_SE(iType));

        if (Read<bool>(Objaddr + OFF_SE(bDeath))) {
            Prediction::InvalidatePrediction(Objaddr);
            continue;
        }
        int Health    = Read<int>(Objaddr + OFF_SE(Hp));
        if (Health <= 0) continue;
        int maxHealth = Read<int>(Objaddr + OFF_SE(HpMax));
        if (maxHealth <= 0) continue;

        Vector3 Dm;
        vm_readv(Objaddr + OFF_SE(vCachePosition), &Dm, sizeof(Dm));
        Vector2 mon_posSc;
        if (!WorldToScreen(Dm, &mon_posSc)) continue;
        Vector2 MonPos = mon_posSc;

        if (type == 5) {
            if (mHeroID == 2002 && Health < maxHealth)
                DrawGlobalWarning(Draw, xorstr_("LORD ATTACKED!"),   (float)Health, (float)maxHealth);
            if (mHeroID == 2003 && Health < maxHealth)
                DrawGlobalWarning(Draw, xorstr_("TURTLE ATTACKED!"), (float)Health, (float)maxHealth);
        }

        if (MonPos.X < 0 || MonPos.X > abs_ScreenX ||
            MonPos.Y < 0 || MonPos.Y > abs_ScreenY)
            continue;

        if (type == 1) {
            float  radius     = 10.0f;
            ImU32  minionColor = IM_COL32(255,0,0,200);
            Draw->AddCircleFilled(ImVec2(MonPos.X, MonPos.Y), radius, minionColor, 12);
            Draw->AddCircle(ImVec2(MonPos.X, MonPos.Y), radius, IM_COL32(0,0,0,255), 12, 1.0f);
        }

        if (drawMonsterName && (type == 5 || type == 2 || type == 1)) {
            float offX = 70.0f;
            float offY = Avatar_OffsetY;
            ImVec2 p0  = ImVec2(MonPos.X, MonPos.Y);
            ImVec2 p1  = ImVec2(p0.x + offX, p0.y - offY - 30.0f);
            float monsterRadius = (type == 1) ? 1.0f : 2.0f;
            const int segs = 32;
            Vector2 lastP; bool firstP = true;
            ImU32 circleCol = IM_COL32(0,255,150,150);
            if (mHeroID == 2002 || mHeroID == 2003) circleCol = IM_COL32(255,200,0,180);
            else if (type == 1)                     circleCol = IM_COL32(255,  0,0,150);
            for (int j = 0; j <= segs; j++) {
                float ang  = (float)j / (float)segs * 2.0f * M_PI;
                Vector3 wPt = {Dm.X + cosf(ang)*monsterRadius, Dm.Y+0.1f, Dm.Z + sinf(ang)*monsterRadius};
                Vector2 sPt;
                if (WorldToScreen(wPt, &sPt)) {
                    if (!firstP) Draw->AddLine(ImVec2(lastP.X,lastP.Y), ImVec2(sPt.X,sPt.Y), circleCol, 2.0f);
                    lastP = sPt; firstP = false;
                } else { firstP = true; }
            }
            std::string mName = (type == 1) ? xorstr_("Minion") : MonsterToString(mHeroID);
            char hpBuf[32]; snprintf(hpBuf, sizeof(hpBuf), xorstr_("%d"), Health);
            float monIconSize = g_MonsterIconSize;
            ImVec2 nSize = ImGui::CalcTextSize(mName.c_str());
            ImVec2 hSize = ImGui::CalcTextSize(hpBuf);
            float padding = 10.0f;
            float totalW  = nSize.x + padding + (monIconSize*2) + hSize.x + padding;
            ImVec2 p2 = ImVec2(p1.x + totalW, p1.y);
            Draw->AddLine(p0, p1, IM_COL32(255,255,255,180), 1.2f);
            Draw->AddLine(p1, p2, IM_COL32(255,255,255,180), 1.2f);
            Draw->AddCircleFilled(p0, 3.0f, IM_COL32(0,255,0,255), 10);
            float curX = p1.x;
            if (type != 1) {
                DrawHeroIcon(Draw, ImVec2(curX+monIconSize, p1.y-monIconSize-2.0f),
                             mHeroID, Health, maxHealth, monIconSize, false);
                curX += monIconSize*2 + padding;
            }
            if (!mName.empty()) {
                Draw->AddText(ImVec2(curX, p1.y-nSize.y-2.0f),
                              IM_COL32(255,255,255,255), mName.c_str());
                curX += nSize.x + padding;
            }
            Draw->AddText(ImVec2(curX, p1.y-hSize.y-2.0f),
                          IM_COL32(0,255,0,230), hpBuf);
        }
    }
}

struct MonsterData {
    uintptr_t address;
    Vector3   position;
    float     distance;
    int       health;
    int       maxHP;
    bool      isDead;
    bool      isVisible;
    bool      isValid;
    char      name[100];
};
MonsterData monster[20];
int      MonsterCount = 0;
uintptr_t Oneself    = 0;

void MonsterRetribution() {
    DRIVER_LOCK();   // ── KERNEL-SAFE FIX: драйверге жалгыз кайрылуу
    // ── KERNEL-SAFE FIX: attach болмоюнча драйверге кайрылбайт.
    //    Мурда game_worker муну башталганда эле чакырчу — ал кезде
    //    libbase == 0 жана g_driver->initialize(g_pid) азырынча иштей элек,
    //    демек кернелге pid'сиз, жараксыз даректен сурам кетип panic болчу.
    if (!is_attached || libbase == 0) { Oneself = 0; return; }

    // ── FIX #3 & #7: туура pointer chain
    // ── ОПТИМИЗАЦИЯ #2: обфускацияланган туюнтмалар алынып салынды.
    //    (0x100 | PtrChain1) & 0xFF — 0x100 биттин маскалап салат, демек
    //    PtrChain1 > 0xFF болсо offset кыркылып калмак. (a2<<1)>>1 болсо
    //    signed long'дун жогорку битин өчүрөт — UB. Экөө тең кереги жок.
    long a1 = ReadPtr(libbase + OFF_BM(BasePtr));
    if (!IsUserAddr(a1))  { Oneself = 0; return; }   // ── KERNEL-SAFE FIX
    long a2 = ReadPtr(a1 + OFF_BM(PtrChain1));
    if (!IsUserAddr(a2))  { Oneself = 0; return; }
    long a32 = ReadPtr(a2);
    if (!IsUserAddr(a32)) { Oneself = 0; return; }
    Oneself = ReadPtr(a32 + OFF_BM(LocalPlayerShow));
    if (!IsUserAddr(Oneself)) { Oneself = 0; return; }

    Vector3 MyPosition;
    vm_readv(Oneself + OFF_SE(vCachePosition), &MyPosition, sizeof(MyPosition));
    MonsterCount = 0;

    uintptr_t Showmonster = ReadPtr(a32 + OFF_BM(ShowMonsters));
    if (!IsUserAddr(Showmonster)) return;   // ── KERNEL-SAFE FIX
    int       monCount    = Read<int>(Showmonster + OFF_BM(ListCountOffset));
    uintptr_t monDataPtr  = ReadPtr(Showmonster + OFF_BM(ListDataOffset));
    if (monCount < 0 || monCount > 100 || !IsUserAddr(monDataPtr)) return;   // ── KERNEL-SAFE FIX

    uintptr_t monArray = monDataPtr + OFF_BM(ListArrayOffset);
    int found = 0;
    for (int i = 0; i < monCount && found < 20; i++) {
        uintptr_t curPtr = ReadPtr(monArray + (i * 8));
        if (!IsUserAddr(curPtr)) continue;   // ── KERNEL-SAFE FIX
        int monsterID  = Read<int>(curPtr + OFF_SE(HeroID));
        int monsterHP  = Read<int>(curPtr + OFF_SE(Hp));
        int monsterMaxHP = Read<int>(curPtr + OFF_SE(HpMax));
        Vector3 monPos = Read<Vector3>(curPtr + OFF_SE(vCachePosition));
        bool mDead     = (Read<uint8_t>(curPtr + OFF_SE(bDeath)) != 0);
        int  mType     = Read<int>(curPtr + OFF_SE(iType));
        if (mType != 2 && mType != 5) continue;
        std::string mName = MonsterToString(monsterID);
        if (mName.empty()) {
            if      (monsterID == 2002)                    mName = xorstr_("Lord");
            else if (monsterID == 2003 || monsterID == 2110) mName = xorstr_("Turtle");
            else if (monsterID == 2004 || monsterID == 2220) mName = xorstr_("Red Buff");
            else if (monsterID == 2005 || monsterID == 2221) mName = xorstr_("Blue Buff");
            else if (monsterID == 2222 || monsterID == 2223) mName = xorstr_("Crab");
            else if (monsterID == 2056)                    mName = xorstr_("Lito");
            else                                           mName = xorstr_("Monster");
        }
        monster[found].address  = curPtr;
        monster[found].position = monPos;
        monster[found].distance = Vector3::Distance(MyPosition, monPos);
        monster[found].health   = monsterHP;
        monster[found].maxHP    = monsterMaxHP;
        monster[found].isDead   = mDead;
        monster[found].isVisible = true;
        monster[found].isValid  = true;
        strncpy(monster[found].name, mName.c_str(), sizeof(monster[found].name) - 1);
        monster[found].name[sizeof(monster[found].name)-1] = '\0';
        found++;
    }
    MonsterCount = found;
}

float g_MinimapScale       = 74.11f;
float g_Res0_MultX         = 1.0f;
float g_Res0_MultY         = 1.0f;
float g_Res1_OffsetX       = 0.0f;
float g_Res1_OffsetY       = 0.0f;
int   g_MinimapHeroSize    = 38;
// ── ЖАҢЫ: астына жылдыруучу таякчанын (scrollbar) өлчөмү — Settings'те
float g_ScrollbarSize      = 25.0f;
int   g_MinimapMonsterSize = 30;

Vector2 WorldToMinimap(Vector3 HeroPosition) {
    // ── KERNEL-SAFE FIX: Oneself == 0 болгондо Read<int>(0 + offset)
    //    кернелге жараксыз дарек жибергенин жоюу.
    int CampType = 0;
    if (is_attached && Oneself != 0)
        CampType = Read<int>(Oneself + OFF_SP(m_EntityCampType));
    float angle     = (CampType == 2 ? 314.60f : 134.76f) * 0.017453292519943295f;
    float angleCos  = std::cos(angle);
    float angleSin  = std::sin(angle);
    float worldX    = -HeroPosition.X;
    float worldZ    = HeroPosition.Y;
    Vector2 Res0;
    Res0.X = ((angleCos * worldX - angleSin * worldZ) / g_MinimapScale) * g_Res0_MultX;
    Res0.Y = ((angleSin * worldX + angleCos * worldZ) / g_MinimapScale) * g_Res0_MultY;
    Vector2 Res1;
    Res1.X = (Res0.X * MinimapSize) + MinimapPos  + (MinimapSize / 2.0f) + g_Res1_OffsetX;
    Res1.Y = (Res0.Y * MinimapSize) + MinimapPosY + (MinimapSize / 2.0f) + g_Res1_OffsetY;
    return Res1;
}

void DrawMinimapESP(ImDrawList* draw) {
    if (!MinimapIcon) return;
    // ── KERNEL-SAFE FIX: attach болмоюнча драйверге такыр кайрылбайт.
    //    Мурда libbase == 0 болгондо ReadPtr(0 + offset) кернелге кеткен.
    if (!is_attached || libbase == 0) return;
    DRIVER_LOCK();   // ── KERNEL-SAFE FIX: драйверге жалгыз кайрылуу

    // ── ОПТИМИЗАЦИЯ #2: chain кадрга бир жолу (кэштен)
    if (!BuildFrameCache()) return;
    long a32 = g_Frame.a32;

    // ── ОПТИМИЗАЦИЯ #4: душмандар DrawMonster менен бир кэшти бөлүшөт.
    //    Мурда бул жерде бүт тизме экинчи жолу кайра окулчу.
    for (const auto& en : g_Frame.enemies) {
        const Vector3& pos = en.pos;
        if (pos.X == 0 && pos.Y == 0 && pos.Z == 0) continue;

        Vector2 minimapPos = WorldToMinimap({pos.X, pos.Y, pos.Z});
        const int heroId = en.heroId;
        const int hp     = en.hp;
        const int maxHp  = en.maxHp;
        const int level  = en.level;
        float iconR = g_MinimapHeroSize / 2.0f;
        DrawHeroIcon(draw, ImVec2(minimapPos.X, minimapPos.Y),
                     heroId, hp, maxHp, iconR, true, level, 0);

        // ── Тоголок HP шкала иконконун айланасында
        if (maxHp > 0) {
            float hpPct = (float)hp / (float)maxHp;
            if (hpPct < 0.0f) hpPct = 0.0f;
            if (hpPct > 1.0f) hpPct = 1.0f;
            float ringR = iconR + 3.0f;
            ImU32 hpRingCol;
            if      (hpPct > 0.5f) hpRingCol = IM_COL32(0,   220, 90,  230); // >50% жашыл
            else if (hpPct > 0.2f) hpRingCol = IM_COL32(255, 200,  0,  230); // 20-50% сары
            else                   hpRingCol = IM_COL32(255,  40, 40,  230); // <20% кызыл
            // Фон шакекче (сур)
            draw->AddCircle(ImVec2(minimapPos.X, minimapPos.Y), ringR,
                            IM_COL32(60, 60, 60, 180), 32, 2.0f);
            // HP дуга: -π/2 дан башталат (жогорудан), HP % га жараша
            float startAngle = -IM_PI * 0.5f;
            float endAngle   = startAngle + (2.0f * IM_PI * hpPct);
            const int arcSegs = 32;
            for (int s = 0; s < arcSegs; s++) {
                float a0 = startAngle + (endAngle - startAngle) * ((float)s       / arcSegs);
                float a1 = startAngle + (endAngle - startAngle) * ((float)(s + 1) / arcSegs);
                draw->AddLine(
                    ImVec2(minimapPos.X + cosf(a0) * ringR, minimapPos.Y + sinf(a0) * ringR),
                    ImVec2(minimapPos.X + cosf(a1) * ringR, minimapPos.Y + sinf(a1) * ringR),
                    hpRingCol, 2.0f);
            }
        }
    }

    // ── Монстрлор: MinimapIconBuff toggle менен башкарылат
    if (MinimapIconBuff) {
        long showMonster = ReadPtr(a32 + OFF_BM(ShowMonsters));
        if (IsUserAddr(showMonster)) {   // ── KERNEL-SAFE FIX
            long monsterList = ReadPtr(showMonster + OFF_BM(ListDataOffset));
            if (IsUserAddr(monsterList)) {
                monsterList += OFF_BM(ListArrayOffset);
                uint monCount = Read<uint>(showMonster + OFF_BM(ListCountOffset));
                // ── KERNEL-SAFE FIX: чексиз цикл коргоосу
                if (monCount > MAX_MONSTER_ENTITY) monCount = 0;
                for (uint i = 0; i < monCount; i++) {
                    long Objaddr = ReadPtr(monsterList + (i << 3));
                    if (!IsUserAddr(Objaddr)) continue;   // ── KERNEL-SAFE FIX
                    int  mType   = Read<int>(Objaddr + OFF_SE(iType));
                    int  mHeroID = Read<int>(Objaddr + OFF_SE(HeroID));
                    if (Read<bool>(Objaddr + OFF_SE(bDeath))) continue;
                    Vector3A pos;
                    vm_readv(Objaddr + OFF_SE(vCachePosition), &pos, sizeof(pos));
                    if (pos.X == 0 && pos.Y == 0 && pos.Z == 0) continue;
                    Vector2 minimapPos = WorldToMinimap({pos.X, pos.Y, pos.Z});
                    int hp    = Read<int>(Objaddr + OFF_SE(Hp));
                    int maxHp = Read<int>(Objaddr + OFF_SE(HpMax));

                    if (mType == 1 && drawMinionMinimap) {
                        if (Read<bool>(Objaddr + OFF_SE(bSameCampType))) continue;
                        draw->AddCircleFilled(ImVec2(minimapPos.X, minimapPos.Y),
                                             minionMinimapSize, IM_COL32(255,0,0,200), 8);
                        draw->AddCircle(ImVec2(minimapPos.X, minimapPos.Y),
                                        minionMinimapSize, IM_COL32(0,0,0,255), 8, 1.0f);
                        continue;
                    }
                    if (mType == 2 || mType == 5) {
                        if (bMonster(mHeroID)) {
                            DrawHeroIcon(draw, ImVec2(minimapPos.X, minimapPos.Y),
                                         mHeroID, hp, maxHp,
                                         g_MinimapMonsterSize / 2.0f, false, 0, 0);
                        }
                    }
                }
            }
        }
    }

    if (!HideLine) {
        draw->AddRect(ImVec2(MinimapPos, MinimapPosY),
                      ImVec2(MinimapPos + MinimapSize, MinimapPosY + MinimapSize),
                      IM_COL32(255,255,255,255));
    }
}

// ─────────────────────────────────────────────
//  RoomInfo жана башка маалыматтар
// ─────────────────────────────────────────────
CachedRoomData g_CachedRoomData;
std::mutex     g_RoomDataMutex;
static std::atomic<bool>  g_RoomThreadRunning{false};
static std::atomic<bool>  g_RoomThreadShouldStop{false};
static std::thread*       g_RoomInfoThread = nullptr;

std::unordered_map<std::string,std::string> countryMap = {
    {"id","Indonesia"},{"ph","Philippines"},{"my","Malaysia"},
    {"sg","Singapore"},{"th","Thailand"},  {"vn","Vietnam"},
    {"mm","Myanmar"},  {"kh","Cambodia"},  {"la","Laos"},
    {"bn","Brunei"},   {"tl","Timor-Leste"},{"jp","Japan"},
    {"kr","South Korea"},{"cn","China"},   {"tw","Taiwan"},
    {"hk","Hong Kong"},{"mo","Macau"},     {"in","India"},
    {"br","Brazil"},   {"us","USA"},       {"ru","Russia"},
    {"tr","Turkey"},   {"mx","Mexico"},    {"ar","Argentina"},
    {"cl","Chile"},    {"co","Colombia"},  {"pe","Peru"},
    {"es","Spain"},    {"it","Italy"},     {"fr","France"},
    {"de","Germany"},  {"gb","United Kingdom"},{"ua","Ukraine"},
    {"pl","Poland"},   {"au","Australia"}, {"nz","New Zealand"},
    {"sa","Saudi Arabia"},{"ae","UAE"},    {"eg","Egypt"},
    {"ma","Morocco"}
};

std::string VerifiedParty(bool bRoomLeader) {
    return bRoomLeader ? std::string(xorstr_("Solo")) : std::string(xorstr_("Party"));
}
std::string RoadToRole(int roadValue) {
    switch (roadValue) {
        case 0: return xorstr_("All");
        case 1: return xorstr_("Exp");
        case 2: return xorstr_("Mid");
        case 3: return xorstr_("Roam");
        case 4: return xorstr_("Jungle");
        case 5: return xorstr_("Gold");
        default: return xorstr_("-");
    }
}
std::vector<int> ParseAdditionalHeroes(uintptr_t dictPtr) {
    std::vector<int> heroIds;
    // ── KERNEL-SAFE FIX: дарек текшерүү
    if (!IsUserAddr(dictPtr)) return heroIds;
    monoDictionary<uint32_t,uint32_t> heroDict{};
    heroDict.klass        = reinterpret_cast<void*>(Read<uintptr_t>(dictPtr + 0x0));
    heroDict.obj          = reinterpret_cast<void*>(Read<uintptr_t>(dictPtr + 0x8));
    heroDict.buckets      = Read<int32_t*>(dictPtr + 0x10);
    heroDict.arrayEntries = Read<uintptr_t>(dictPtr + 0x18);
    heroDict.count        = Read<int>(dictPtr + 0x20);
    // ── KERNEL-SAFE FIX: count чеги + дарек текшерүү
    if (!IsUserAddr(heroDict.arrayEntries)) return heroIds;
    if (heroDict.count <= 0 || heroDict.count > MAX_MONO_ENTRIES) return heroIds;
    auto entries = get_mono_dictionary_vector<uint32_t,uint32_t>(heroDict);
    if (entries.empty()) return heroIds;
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b){ return a.value > b.value; });
    for (const auto& e : entries) {
        if (e.key == 0 || e.value == 0) continue;
        heroIds.push_back(static_cast<int>(e.key));
    }
    return heroIds;
}
float ParsePlayerWinRate(uintptr_t listPtr, int currentRoad,
                         int& outGames, int& outWins) {
    // ── KERNEL-SAFE FIX: дарек текшерүү
    if (!IsUserAddr(listPtr)) return -1.0f;
    monoList roadList = Read<monoList>(listPtr);
    auto roads = get_mono_list_vector<uintptr_t>(roadList);
    // ── KERNEL-SAFE FIX: тизме өлчөмүнүн чеги
    if (roads.size() > static_cast<size_t>(MAX_MONO_ENTRIES)) return -1.0f;
    for (auto roadInfoPtr : roads) {
        if (!IsUserAddr(roadInfoPtr)) continue;
        int roadType = Read<int>(roadInfoPtr + 0x10);
        if (roadType != currentRoad) continue;
        int totalNum = Read<int>(roadInfoPtr + 0x14);
        int winNum   = Read<int>(roadInfoPtr + 0x18);
        outGames = totalNum; outWins = winNum;
        if (totalNum <= 0) return -1.0f;
        return (static_cast<float>(winNum) / static_cast<float>(totalNum)) * 100.0f;
    }
    return -1.0f;
}
int GetRankValue(const CachedRoomPlayer& p) { return p.RankLevel; }

enum ColumnID {
    COL_NICKNAME = 0, COL_USERID, COL_HERO, COL_RANK,
    COL_SPELL,        COL_ROLE,   COL_ADDHERO, COL_WINRATE,
    COL_PARTY,        COL_COUNT
};
struct SortSpecs {
    int  columnID;
    bool ascending;
    bool valid;
    SortSpecs() : columnID(-1), ascending(true), valid(false) {}
};
static SortSpecs g_SortSpecs[2];

bool ComparePlayers(const CachedRoomPlayer& a, const CachedRoomPlayer& b,
                    int sortCol, bool ascending) {
    int cmp = 0;
    switch (sortCol) {
        case COL_NICKNAME: cmp = a.Name.compare(b.Name);         break;
        case COL_USERID:   cmp = a.UserID.compare(b.UserID);     break;
        case COL_HERO:     cmp = a.Hero.compare(b.Hero);         break;
        case COL_RANK:     cmp = GetRankValue(a) - GetRankValue(b); break;
        case COL_SPELL:    cmp = a.Spell.compare(b.Spell);       break;
        case COL_ROLE:     cmp = a.Role.compare(b.Role);         break;
        case COL_ADDHERO:  cmp = (int)(a.AddHeroIds.size() - b.AddHeroIds.size()); break;
        case COL_WINRATE:
            if      (a.WinRate < 0 && b.WinRate < 0) cmp = 0;
            else if (a.WinRate < 0) cmp = -1;
            else if (b.WinRate < 0) cmp =  1;
            else cmp = (int)((a.WinRate - b.WinRate) * 10);
            break;
        case COL_PARTY:    cmp = a.Party.compare(b.Party);       break;
        default:           cmp = GetRankValue(a) - GetRankValue(b); break;
    }
    return ascending ? (cmp < 0) : (cmp > 0);
}

void RoomInfoReaderThread() {
    g_RoomThreadRunning = true;
    while (!g_RoomThreadShouldStop && main_thread_flag) {
        if (!is_attached || !libbase) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        // ── KERNEL-SAFE FIX: бул итерациянын бардык окуусу жалгыз.
        //    Уйкуга киргенге чейин _drv_lk.unlock() чакырылат —
        //    башка thread'тер 200-300ms бойго күтүп калбасын.
        std::unique_lock<std::recursive_mutex> _drv_lk(g_DriverMutex);
        uintptr_t systemBase = Read<uintptr_t>(libbase + OFF_SD(BasePtr));
        if (!IsUserAddr(systemBase)) {   // ── KERNEL-SAFE FIX
            _drv_lk.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        uintptr_t instancePtr = Read<uintptr_t>(systemBase + OFF_SD(PtrChain1));
        uint64_t  myUid       = 0;
        if (instancePtr) myUid = Read<uint64_t>(instancePtr + OFF_SD(m_uiID));
        if (myUid == 0)  myUid = g_LoginServerCache.AccountId;

        uintptr_t dictPtr = Read<uintptr_t>(instancePtr + OFF_SD(m_RoomPlayerInfo));
        if (!IsUserAddr(dictPtr)) { _drv_lk.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }   // ── KERNEL-SAFE FIX

        monoDictionary<uint64_t,uintptr_t> dict{};
        dict.klass        = reinterpret_cast<void*>(Read<uintptr_t>(dictPtr + 0x0));
        dict.obj          = reinterpret_cast<void*>(Read<uintptr_t>(dictPtr + 0x8));
        dict.arrayEntries = Read<uintptr_t>(dictPtr + 0x18);
        dict.count        = Read<int>(dictPtr + 0x20);
        // ── KERNEL-SAFE FIX: count чеги + дарек текшерүү.
        //    Оюн жаңы жүктөлүп жатканда бул dictionary толук
        //    инициализацияланбай, бузулган count кайтарчу.
        if (!IsUserAddr(dict.arrayEntries) ||
            dict.count <= 0 || dict.count > MAX_MONO_ENTRIES) {
            _drv_lk.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue;
        }
        auto entries = get_mono_dictionary_vector<uint64_t,uintptr_t>(dict);
        if (entries.empty()) { _drv_lk.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }

        const auto& lastEntry = entries.back();
        // ── KERNEL-SAFE FIX: дарек текшерүү
        if (!IsUserAddr(lastEntry.value)) { _drv_lk.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }

        auto playerList = Read<monoList>(lastEntry.value);
        auto players    = get_mono_list_vector<uintptr_t>(playerList);
        // ── KERNEL-SAFE FIX: тизме өлчөмүнүн чеги
        if (players.empty() || players.size() > static_cast<size_t>(MAX_MONO_ENTRIES)) {
            _drv_lk.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue;
        }

        // ══════════════════════════════════════════════════════════════
        //  РЕГРЕССИЯ ОҢДОО — lock'ту узак кармабоо
        //
        //  Мен DRIVER_LOCK'ту бул циклдин БҮТҮНДӨЙ үстүнөн кармаган элем.
        //  Төмөндөгү цикл ар оюнчуга ондогон окуу жасайт (fshy() менен
        //  ат, клан, өлкө ж.б.). KERNEL режиминде ар бир окуу ioctl —
        //  рут режиминен алда канча жай. Натыйжада бул блок lock'ту
        //  жүздөгөн миллисекунд кармап, чийүү агымы (DrawMonster) ага
        //  жетпей калчу. Ошон үчүн кернел режиминде ESP такыр чыкпаган,
        //  ал эми рут режиминде окуу ылдам болуп, байкалбай калган.
        //
        //  Эми: чынжыр чечилгенден кийин lock бошотулат, ар бир оюнчу
        //  өз кезегинде кыска кармайт. Чийүү агымы ортосуна кире алат.
        // ══════════════════════════════════════════════════════════════
        _drv_lk.unlock();

        std::vector<CachedRoomPlayer> tempTeamA, tempTeamB;
        for (auto infoPtr : players) {
            // ── KERNEL-SAFE FIX: дарек текшерүү
            if (!IsUserAddr(infoPtr)) continue;
            // Ар бир оюнчу өзүнчө кыска критикалык аймак
            std::lock_guard<std::recursive_mutex> _pl_lk(g_DriverMutex);
            CachedRoomPlayer p{};
            p.Address      = infoPtr;
            p.Camp         = Read<int>(infoPtr + OFF_RD(iCamp));
            p.IsRoomLeader = Read<bool>(infoPtr + OFF_RD(bRoomLeader));
            p.Name         = fshy(Read<uintptr_t>(infoPtr + OFF_RD(_sName)));
            p.HeroId       = Read<int>(infoPtr + OFF_RD(heroid));
            p.Hero         = HeroToString(p.HeroId);
            p.Rank         = RankToString(Read<int>(infoPtr + OFF_RD(uiRankLevel)),
                                          Read<int>(infoPtr + OFF_RD(iMythPoint)));
            p.SpellId      = Read<int>(infoPtr + OFF_RD(summonSkillId));
            p.Spell        = SpellToString(p.SpellId);
            uint64_t playerUid = Read<uint64_t>(infoPtr + OFF_RD(lUid));
            p.UserID       = std::to_string(playerUid);
            p.Squad        = fshy(Read<uintptr_t>(infoPtr + OFF_RD(_steamSimpleName)));
            p.CountryId    = Read<int>(infoPtr + OFF_RD(country));
            p.Country      = fshy(Read<uintptr_t>(infoPtr + OFF_RD(sThisLoginCountry)));
            if (p.Country.empty()) p.Country = "-";
            p.Party        = VerifiedParty(p.IsRoomLeader);
            p.Role         = RoadToRole(Read<int>(infoPtr + OFF_RD(iRoad)));
            auto heroBattleMapPtr   = Read<uintptr_t>(infoPtr + OFF_RD(mapHeroBattleNum));
            p.AddHeroIds   = ParseAdditionalHeroes(heroBattleMapPtr);
            auto roadInfoListPtr    = Read<uintptr_t>(infoPtr + OFF_RD(vCurSeasonRealRoadInfo));
            int  currentRoad        = Read<int>(infoPtr + OFF_RD(iRoad));
            p.WinRate      = ParsePlayerWinRate(roadInfoListPtr, currentRoad, p.RoadGames, p.Wins);
            p.RankLevel    = Read<int>(infoPtr + OFF_RD(uiRankLevel));
            p.ZoneId       = Read<int>(infoPtr + OFF_RD(uiZoneId));
            p.SkinID       = Read<int>(infoPtr + OFF_RD(heroskin));
            p.IsValid      = true;
            if      (p.Camp == 1) tempTeamA.push_back(p);
            else if (p.Camp == 2) tempTeamB.push_back(p);
        }
        int foundCamp = 0;
        std::string myUidStr = std::to_string(myUid);
        for (auto& p : tempTeamA) {
            if (p.UserID == myUidStr) {
                foundCamp = 1;
                if (!p.Name.empty()) g_LoginServerCache.Nickname = p.Name;
                g_LoginServerCache.AccountId = myUid;
                g_LoginServerCache.IsValid   = true;
                break;
            }
        }
        if (foundCamp == 0) {
            for (auto& p : tempTeamB) {
                if (p.UserID == myUidStr) {
                    foundCamp = 2;
                    if (!p.Name.empty()) g_LoginServerCache.Nickname = p.Name;
                    g_LoginServerCache.AccountId = myUid;
                    g_LoginServerCache.IsValid   = true;
                    break;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_RoomDataMutex);
            g_CachedRoomData.TeamA     = std::move(tempTeamA);
            g_CachedRoomData.TeamB     = std::move(tempTeamB);
            g_CachedRoomData.GroupID   = lastEntry.key;
            g_CachedRoomData.HasData   = true;
            if (foundCamp != 0)
                g_CachedRoomData.LocalPlayerCamp = foundCamp;
            g_CachedRoomData.LastUpdate = std::time(nullptr);
        }
        // _drv_lk циклге чейин эле бошотулган — кайра unlock кылбайбыз
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    g_RoomThreadRunning = false;
}

#define GAMESERVERCONFIG_TYPEINFO  0x60740b0
#define GAMECOMMON_TYPEINFO        0x6029bd8
#define STATIC_FIELD               0xa8
#define GAMESERVERCONFIG_INSTANCE  0xa8
#define GAMESERVERCONFIG_LOGININFO 0x250
#define GAMECOMMON_INST            0x0
#define GAMECOMMON_LOGININFO       0x58
#define LOGININFO_ACCOUNT_ID       0x28
#define LOGININFO_ACC_CREATE_TIME  0x88

LoginServerData ReadLoginServerInfo(uintptr_t libbase) {
    LoginServerData data{};
    uintptr_t loginInfoPtr = 0;
    if (GAMESERVERCONFIG_TYPEINFO != 0) {
        uintptr_t typeInfo    = ReadPtr(libbase + GAMESERVERCONFIG_TYPEINFO);
        if (typeInfo) {
            uintptr_t staticField = ReadPtr(typeInfo + STATIC_FIELD);
            if (staticField) {
                uintptr_t instance = ReadPtr(staticField + GAMESERVERCONFIG_INSTANCE);
                if (instance)
                    loginInfoPtr = ReadPtr(instance + GAMESERVERCONFIG_LOGININFO);
            }
        }
    }
    if (loginInfoPtr == 0 && GAMECOMMON_TYPEINFO != 0) {
        uintptr_t typeInfo    = ReadPtr(libbase + GAMECOMMON_TYPEINFO);
        if (typeInfo) {
            uintptr_t staticField = ReadPtr(typeInfo + STATIC_FIELD);
            if (staticField) {
                uintptr_t inst = ReadPtr(staticField + GAMECOMMON_INST);
                if (inst)
                    loginInfoPtr = ReadPtr(inst + GAMECOMMON_LOGININFO);
            }
        }
    }
    if (!loginInfoPtr) return data;
    data.AccountId   = Read<uint64_t>(loginInfoPtr + LOGININFO_ACCOUNT_ID);
    uintptr_t nickPtr = Read<uintptr_t>(loginInfoPtr + 0x18);
    if (nickPtr) data.Nickname = fshy(nickPtr);
    if (data.Nickname.empty()) {
        nickPtr = Read<uintptr_t>(loginInfoPtr + 0x10);
        if (nickPtr) data.Nickname = fshy(nickPtr);
    }
    data.ClientRealVersion    = fshy(Read<uintptr_t>(loginInfoPtr + 0x30));
    data.ClientStreamVersion  = fshy(Read<uintptr_t>(loginInfoPtr + 0x38));
    data.ClientLocalVersion   = fshy(Read<uintptr_t>(loginInfoPtr + 0x40));
    data.ClientLoginVersion   = fshy(Read<uintptr_t>(loginInfoPtr + 0x50));
    data.Channel              = fshy(Read<uintptr_t>(loginInfoPtr + 0x58));
    data.IsNewAccount         = Read<bool>(loginInfoPtr + 0x60);
    data.RegionId             = Read<uint32_t>(loginInfoPtr + 0x64);
    data.CountryInfo          = fshy(Read<uintptr_t>(loginInfoPtr + 0x68));
    data.CountryInfoByIP      = fshy(Read<uintptr_t>(loginInfoPtr + 0x70));
    data.InternetOperator     = fshy(Read<uintptr_t>(loginInfoPtr + 0x78));
    data.State                = fshy(Read<uintptr_t>(loginInfoPtr + 0x80));
    data.AccCreateTime        = Read<uint32_t>(loginInfoPtr + LOGININFO_ACC_CREATE_TIME);
    data.AccountFlag          = Read<uint32_t>(loginInfoPtr + 0x8c);
    data.UserIP               = fshy(Read<uintptr_t>(loginInfoPtr + 0x90));
    data.GameServerIP         = fshy(Read<uintptr_t>(loginInfoPtr + 0xb8));
    data.GameServerPort       = Read<int32_t>(loginInfoPtr + 0xc0);
    data.EngineBuildVersion   = fshy(Read<uintptr_t>(loginInfoPtr + 0xf8));
    data.IsValid              = true;
    return data;
}
LoginServerData g_LoginServerCache{};
std::time_t     g_LoginServerLastUpdate = 0;

void RenderLoginServerInfo() {
    ImGui::Separator();
    ImGui::BeginGroup();
    ImGui::Text(xorstr_("Account ID:")); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.8f,0.4f,0.0f,1.0f), xorstr_("%llu"), g_LoginServerCache.AccountId);
    ImGui::Text(xorstr_("Region ID:")); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0f,0.4f,0.7f,1.0f), xorstr_("%u"), g_LoginServerCache.RegionId);
    ImGui::Text(xorstr_("New Account:")); ImGui::SameLine();
    ImGui::TextColored(g_LoginServerCache.IsNewAccount
                       ? ImVec4(0,0.6f,0,1) : ImVec4(0.8f,0,0,1),
                       xorstr_("%s"),
                       g_LoginServerCache.IsNewAccount ? xorstr_("Yes") : xorstr_("No"));
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text(xorstr_("Channel:")); ImGui::SameLine();
    ImGui::Text(xorstr_("%s"), g_LoginServerCache.Channel.empty()
                ? xorstr_("-") : g_LoginServerCache.Channel.c_str());
    ImGui::Text(xorstr_("State:")); ImGui::SameLine();
    ImGui::Text(xorstr_("%s"), g_LoginServerCache.State.empty()
                ? xorstr_("-") : g_LoginServerCache.State.c_str());
    ImGui::EndGroup();
    ImGui::Separator();
    if (ImGui::BeginTable(xorstr_("##VersionTable"), 2,
                          ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(xorstr_("Key"),   ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn(xorstr_("Value"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        auto AddRow = [](const char* key, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text(xorstr_("%s"), key);
            ImGui::TableSetColumnIndex(1); ImGui::Text(xorstr_("%s"), value.empty() ? xorstr_("-") : value.c_str());
        };
        AddRow(xorstr_("Real Version"),   g_LoginServerCache.ClientRealVersion);
        AddRow(xorstr_("Stream Version"), g_LoginServerCache.ClientStreamVersion);
        AddRow(xorstr_("Local Version"),  g_LoginServerCache.ClientLocalVersion);
        AddRow(xorstr_("Login Version"),  g_LoginServerCache.ClientLoginVersion);
        AddRow(xorstr_("Engine Build"),   g_LoginServerCache.EngineBuildVersion);
        ImGui::EndTable();
    }
    ImGui::Separator();
    if (ImGui::BeginTable(xorstr_("##NetworkTable"), 2,
                          ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(xorstr_("Key"),   ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn(xorstr_("Value"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        auto AddNetRow = [](const char* key, const std::string& val) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text(xorstr_("%s"), key);
            ImGui::TableSetColumnIndex(1); ImGui::Text(xorstr_("%s"), val.empty() ? xorstr_("-") : val.c_str());
        };
        AddNetRow(xorstr_("User IP"),      g_LoginServerCache.UserIP);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text(xorstr_("Game Server"));
        ImGui::TableSetColumnIndex(1);
        if (!g_LoginServerCache.GameServerIP.empty())
            ImGui::Text(xorstr_("%s:%d"), g_LoginServerCache.GameServerIP.c_str(),
                        g_LoginServerCache.GameServerPort);
        else ImGui::Text(xorstr_("-"));
        AddNetRow(xorstr_("Country (Info)"), g_LoginServerCache.CountryInfo);
        AddNetRow(xorstr_("Country (IP)"),   g_LoginServerCache.CountryInfoByIP);
        AddNetRow(xorstr_("ISP"),            g_LoginServerCache.InternetOperator);
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::Text(xorstr_("Account Created:")); ImGui::SameLine();
    if (g_LoginServerCache.AccCreateTime > 0) {
        std::time_t createT = static_cast<std::time_t>(g_LoginServerCache.AccCreateTime);
        char timeBuf[64];
        std::strftime(timeBuf, sizeof(timeBuf), xorstr_("%Y-%m-%d %H:%M:%S"),
                      std::localtime(&createT));
        ImGui::Text(xorstr_("%s (Unix: %u)"), timeBuf, g_LoginServerCache.AccCreateTime);
    } else {
        ImGui::Text(xorstr_("-"));
    }
    ImGui::Text(xorstr_("Account Flag:")); ImGui::SameLine();
    ImGui::Text(xorstr_("0x%X (%u)"), g_LoginServerCache.AccountFlag,
                g_LoginServerCache.AccountFlag);
    ImGui::Spacing();
    if (ImGui::Button(xorstr_("Refresh Data"))) {
        g_LoginServerCache      = ReadLoginServerInfo(libbase);
        g_LoginServerLastUpdate = std::time(nullptr);
    }
}

// ─────────────────────────────────────────────
//  Floating Logo
// ─────────────────────────────────────────────
float FloatingIcon_PosX    = 0.12f;
float FloatingIcon_PosY    = 0.5f;
bool  ForceUpdateIconPos   = false;

void DrawFloatingLogo() {
    static ImTextureID logoTex = nullptr;
    if (!logoTex) {
        int w, h, ch;
        unsigned char* pixels = stbi_load_from_memory(icon, sizeof(icon), &w, &h, &ch, 4);
        if (pixels) {
            GLuint tex;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            stbi_image_free(pixels);
            logoTex = (ImTextureID)(intptr_t)tex;
        }
    }
    if (!logoTex) return;
    float iconSize = 125.0f;
    ImGui::SetNextWindowPos(
        ImVec2(abs_ScreenX * FloatingIcon_PosX, abs_ScreenY * FloatingIcon_PosY),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("##FloatingLogo", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Image(logoTex, ImVec2(iconSize, iconSize));
    ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
    ImGui::InvisibleButton("##LogoBtn", ImVec2(iconSize, iconSize));
    if (ImGui::IsItemClicked())
        show_menu = !show_menu;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        FloatingIcon_PosX += ImGui::GetIO().MouseDelta.x / abs_ScreenX;
        FloatingIcon_PosY += ImGui::GetIO().MouseDelta.y / abs_ScreenY;
        if (FloatingIcon_PosX < 0.05f) FloatingIcon_PosX = 0.05f;
        if (FloatingIcon_PosX > 0.95f) FloatingIcon_PosX = 0.95f;
        if (FloatingIcon_PosY < 0.05f) FloatingIcon_PosY = 0.05f;
        if (FloatingIcon_PosY > 0.95f) FloatingIcon_PosY = 0.95f;
    }
    ImGui::End();
}

// ─────────────────────────────────────────────
//  Settings Save / Load
// ─────────────────────────────────────────────
#define SETTINGS_PATH xorstr_("/data/local/tmp/YOURNAME.cfg")

inline void SaveSTACOOLexSettings() {
    std::ofstream out(SETTINGS_PATH);
    if (!out.is_open()) return;
    out << xorstr_("drawMHealth=")         << (drawMHealth        ? 1 : 0) << "\n";
    out << xorstr_("drawMLine=")           << (drawMLine          ? 1 : 0) << "\n";
    out << xorstr_("drawMAvatar=")         << (drawMAvatar        ? 1 : 0) << "\n";
    out << xorstr_("drawMonsterHealth=")   << (drawMonsterHealth  ? 1 : 0) << "\n";
    out << xorstr_("drawMonsterID=")       << (drawMonsterID      ? 1 : 0) << "\n";
    out << xorstr_("drawMonsterName=")     << (drawMonsterName    ? 1 : 0) << "\n";
    out << xorstr_("iconhero=")            << (iconhero           ? 1 : 0) << "\n";
    out << xorstr_("drawAlertUnderAttack=") << (drawAlertUnderAttack ? 1 : 0) << "\n";
    out << xorstr_("drawMPrediction=")     << (drawMPrediction    ? 1 : 0) << "\n";
    out << xorstr_("predictionColorBySpeed=") << (predictionColorBySpeed ? 1 : 0) << "\n";
    out << xorstr_("predictionShowOnlyMoving=") << (predictionShowOnlyMoving ? 1 : 0) << "\n";
    out << xorstr_("predictionShowArrowHead=")  << (predictionShowArrowHead  ? 1 : 0) << "\n";
    out << xorstr_("predictionShowSpeedText=")  << (predictionShowSpeedText  ? 1 : 0) << "\n";
    out << xorstr_("predictionCircleRadius=")   << predictionCircleRadius            << "\n";
    // ESP BOX
    out << xorstr_("drawEspBox=")          << (drawEspBox         ? 1 : 0) << "\n";
    out << xorstr_("espBoxCornerStyle=")   << (espBoxCornerStyle  ? 1 : 0) << "\n";
    out << xorstr_("espBoxThickness=")     << espBoxThickness               << "\n";
    out << xorstr_("espBoxR=")             << espBoxR                       << "\n";
    out << xorstr_("espBoxG=")             << espBoxG                       << "\n";
    out << xorstr_("espBoxB=")             << espBoxB                       << "\n";
    out << xorstr_("espBoxA=")             << espBoxA                       << "\n";
    out << xorstr_("espBoxCornerLen=")     << espBoxCornerLen               << "\n";
    out << xorstr_("espBoxHpBarSize=")     << espBoxHpBarSize               << "\n";
    out << xorstr_("g_ScrollbarSize=")     << g_ScrollbarSize               << "\n";
    out << xorstr_("espBoxOffsetY=")        << espBoxOffsetY                 << "\n";
    out << xorstr_("espBoxOffsetX=")        << espBoxOffsetX                 << "\n";
    out << xorstr_("espBoxScale=")          << espBoxScale                   << "\n";
    out << xorstr_("espBoxIconsInside=")    << (espBoxIconsInside  ? 1 : 0) << "\n";
    out << xorstr_("espBoxIconSize=")       << espBoxIconSize                << "\n";
    out << xorstr_("espLineFromFooter=")    << (espLineFromFooter  ? 1 : 0) << "\n";
    out << xorstr_("espLineThickness=")     << espLineThickness              << "\n";
    out << xorstr_("predictionLineLengthMult=") << predictionLineLengthMult << "\n";
    // Retri
    out << xorstr_("autoRetribution=")     << (autoRetribution    ? 1 : 0) << "\n";
    out << xorstr_("retriTouchX=")         << retriTouchX                   << "\n";
    out << xorstr_("retriTouchY=")         << retriTouchY                   << "\n";
    out << xorstr_("showRetriCircle=")     << (showRetriCircle    ? 1 : 0) << "\n";
    out << xorstr_("AutoRetributionRed=")    << (AutoRetributionRed    ? 1 : 0) << "\n";
    out << xorstr_("AutoRetributionBlue=")   << (AutoRetributionBlue   ? 1 : 0) << "\n";
    out << xorstr_("AutoRetributionLord=")   << (AutoRetributionLord   ? 1 : 0) << "\n";
    out << xorstr_("AutoRetributionTurtle=") << (AutoRetributionTurtle ? 1 : 0) << "\n";
    out << xorstr_("AutoRetributionCrab=")   << (AutoRetributionCrab   ? 1 : 0) << "\n";
    out << xorstr_("AutoRetributionLito=")   << (AutoRetributionLito   ? 1 : 0) << "\n";
    // Minimap
    out << xorstr_("MinimapIcon=")           << (MinimapIcon      ? 1 : 0) << "\n";
    out << xorstr_("MinimapIconBuff=")       << (MinimapIconBuff  ? 1 : 0) << "\n";
    out << xorstr_("HideLine=")              << (HideLine         ? 1 : 0) << "\n";
    out << xorstr_("drawMinionMinimap=")     << (drawMinionMinimap ? 1 : 0) << "\n";
    out << xorstr_("minionMinimapSize=")     << minionMinimapSize           << "\n";
    out << xorstr_("MinimapSize=")           << MinimapSize                 << "\n";
    out << xorstr_("MinimapPos=")            << MinimapPos                  << "\n";
    out << xorstr_("MinimapPosY=")           << MinimapPosY                 << "\n";
    out << xorstr_("g_MinimapHeroSize=")     << g_MinimapHeroSize           << "\n";
    out << xorstr_("g_MinimapMonsterSize=")  << g_MinimapMonsterSize        << "\n";
    out << xorstr_("g_Res0_MultX=")          << g_Res0_MultX                << "\n";
    out << xorstr_("g_Res0_MultY=")          << g_Res0_MultY                << "\n";
    out << xorstr_("g_Res1_OffsetX=")        << g_Res1_OffsetX              << "\n";
    out << xorstr_("g_Res1_OffsetY=")        << g_Res1_OffsetY              << "\n";
    out << xorstr_("g_MinimapScale=")        << g_MinimapScale              << "\n";
    out << xorstr_("g_HeroIconSize=")        << g_HeroIconSize              << "\n";
    // Cooldown
    out << xorstr_("ESP_Player_Cooldown=")   << (ESP_Player_Cooldown ? 1 : 0) << "\n";
    out << xorstr_("Cooldown_OffsetY=")      << Cooldown_OffsetY            << "\n";
    out << xorstr_("Cooldown_Size=")         << Cooldown_Size               << "\n";
    // Alert
    out << xorstr_("Alert_PosX=")            << Alert_PosX                  << "\n";
    out << xorstr_("Alert_PosY=")            << Alert_PosY                  << "\n";
    out << xorstr_("Alert_Scale=")           << Alert_Scale                 << "\n";
    out << xorstr_("Alert_ShowHPText=")      << (Alert_ShowHPText ? 1 : 0)  << "\n";
    // Icon
    out << xorstr_("FloatingIcon_PosX=")     << FloatingIcon_PosX           << "\n";
    out << xorstr_("FloatingIcon_PosY=")     << FloatingIcon_PosY           << "\n";
    out << xorstr_("Avatar_OffsetY=")        << Avatar_OffsetY              << "\n";
    out.close();
}

inline void LoadSTACOOLexSettings() {
    std::ifstream in(SETTINGS_PATH);
    if (!in.is_open()) return;
    std::unordered_map<std::string,std::string> cfg;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos)
            cfg[line.substr(0, eq)] = line.substr(eq + 1);
    }
    in.close();
    auto getVal = [&](const std::string& key) -> const std::string& {
        static std::string empty;
        auto it = cfg.find(key);
        return it != cfg.end() ? it->second : empty;
    };
    try {
#define LOAD_BOOL(k,v)  if (!getVal(k).empty()) v = (getVal(k) == "1");
#define LOAD_FLOAT(k,v) if (!getVal(k).empty()) v = std::stof(getVal(k));
#define LOAD_INT(k,v)   if (!getVal(k).empty()) v = std::stoi(getVal(k));
        LOAD_BOOL(xorstr_("drawMHealth"),          drawMHealth)
        LOAD_BOOL(xorstr_("drawMLine"),            drawMLine)
        LOAD_BOOL(xorstr_("drawMAvatar"),          drawMAvatar)
        LOAD_BOOL(xorstr_("drawMonsterHealth"),    drawMonsterHealth)
        LOAD_BOOL(xorstr_("drawMonsterID"),        drawMonsterID)
        LOAD_BOOL(xorstr_("drawMonsterName"),      drawMonsterName)
        LOAD_BOOL(xorstr_("iconhero"),             iconhero)
        LOAD_BOOL(xorstr_("drawAlertUnderAttack"), drawAlertUnderAttack)
        LOAD_BOOL(xorstr_("drawMPrediction"),      drawMPrediction)
        LOAD_BOOL(xorstr_("predictionColorBySpeed"),   predictionColorBySpeed)
        LOAD_BOOL(xorstr_("predictionShowOnlyMoving"), predictionShowOnlyMoving)
        LOAD_BOOL(xorstr_("predictionShowArrowHead"),  predictionShowArrowHead)
        LOAD_BOOL(xorstr_("predictionShowSpeedText"),  predictionShowSpeedText)
        LOAD_FLOAT(xorstr_("predictionCircleRadius"),  predictionCircleRadius)
        // ESP BOX
        LOAD_BOOL(xorstr_("drawEspBox"),           drawEspBox)
        LOAD_BOOL(xorstr_("espBoxCornerStyle"),    espBoxCornerStyle)
        LOAD_FLOAT(xorstr_("espBoxThickness"),     espBoxThickness)
        LOAD_FLOAT(xorstr_("espBoxR"),             espBoxR)
        LOAD_FLOAT(xorstr_("espBoxG"),             espBoxG)
        LOAD_FLOAT(xorstr_("espBoxB"),             espBoxB)
        LOAD_FLOAT(xorstr_("espBoxA"),             espBoxA)
        LOAD_FLOAT(xorstr_("espBoxCornerLen"),     espBoxCornerLen)
        LOAD_FLOAT(xorstr_("espBoxOffsetY"),       espBoxOffsetY)
        LOAD_FLOAT(xorstr_("espBoxOffsetX"),       espBoxOffsetX)
        LOAD_FLOAT(xorstr_("espBoxScale"),         espBoxScale)
        LOAD_BOOL(xorstr_("espBoxIconsInside"),    espBoxIconsInside)
        LOAD_FLOAT(xorstr_("espBoxIconSize"),      espBoxIconSize)
        LOAD_BOOL(xorstr_("espLineFromFooter"),    espLineFromFooter)
        LOAD_FLOAT(xorstr_("espLineThickness"),    espLineThickness)
        LOAD_FLOAT(xorstr_("predictionLineLengthMult"), predictionLineLengthMult)
        // Retri
        LOAD_BOOL(xorstr_("autoRetribution"),      autoRetribution)
        LOAD_FLOAT(xorstr_("retriTouchX"),         retriTouchX)
        LOAD_FLOAT(xorstr_("retriTouchY"),         retriTouchY)
        LOAD_BOOL(xorstr_("showRetriCircle"),      showRetriCircle)
        LOAD_BOOL(xorstr_("AutoRetributionRed"),   AutoRetributionRed)
        LOAD_BOOL(xorstr_("AutoRetributionBlue"),  AutoRetributionBlue)
        LOAD_BOOL(xorstr_("AutoRetributionLord"),  AutoRetributionLord)
        LOAD_BOOL(xorstr_("AutoRetributionTurtle"),AutoRetributionTurtle)
        LOAD_BOOL(xorstr_("AutoRetributionCrab"),  AutoRetributionCrab)
        LOAD_BOOL(xorstr_("AutoRetributionLito"),  AutoRetributionLito)
        // Minimap
        LOAD_BOOL(xorstr_("MinimapIcon"),          MinimapIcon)
        LOAD_BOOL(xorstr_("MinimapIconBuff"),      MinimapIconBuff)
        LOAD_BOOL(xorstr_("HideLine"),             HideLine)
        LOAD_BOOL(xorstr_("drawMinionMinimap"),    drawMinionMinimap)
        LOAD_FLOAT(xorstr_("minionMinimapSize"),   minionMinimapSize)
        LOAD_INT(xorstr_("MinimapSize"),           MinimapSize)
        LOAD_INT(xorstr_("MinimapPos"),            MinimapPos)
        LOAD_INT(xorstr_("MinimapPosY"),           MinimapPosY)
        LOAD_INT(xorstr_("g_MinimapHeroSize"),     g_MinimapHeroSize)
        LOAD_FLOAT(xorstr_("espBoxHpBarSize"),     espBoxHpBarSize)
        LOAD_FLOAT(xorstr_("g_ScrollbarSize"),     g_ScrollbarSize)
        LOAD_INT(xorstr_("g_MinimapMonsterSize"),  g_MinimapMonsterSize)
        LOAD_FLOAT(xorstr_("g_Res0_MultX"),        g_Res0_MultX)
        LOAD_FLOAT(xorstr_("g_Res0_MultY"),        g_Res0_MultY)
        LOAD_FLOAT(xorstr_("g_Res1_OffsetX"),      g_Res1_OffsetX)
        LOAD_FLOAT(xorstr_("g_Res1_OffsetY"),      g_Res1_OffsetY)
        LOAD_FLOAT(xorstr_("g_MinimapScale"),      g_MinimapScale)
        LOAD_FLOAT(xorstr_("g_HeroIconSize"),      g_HeroIconSize)
        // Cooldown
        LOAD_BOOL(xorstr_("ESP_Player_Cooldown"),  ESP_Player_Cooldown)
        LOAD_FLOAT(xorstr_("Cooldown_OffsetY"),    Cooldown_OffsetY)
        LOAD_FLOAT(xorstr_("Cooldown_Size"),       Cooldown_Size)
        // Alert
        LOAD_FLOAT(xorstr_("Alert_PosX"),          Alert_PosX)
        LOAD_FLOAT(xorstr_("Alert_PosY"),          Alert_PosY)
        LOAD_FLOAT(xorstr_("Alert_Scale"),         Alert_Scale)
        LOAD_BOOL(xorstr_("Alert_ShowHPText"),     Alert_ShowHPText)
        // Icon
        LOAD_FLOAT(xorstr_("FloatingIcon_PosX"),   FloatingIcon_PosX)
        LOAD_FLOAT(xorstr_("FloatingIcon_PosY"),   FloatingIcon_PosY)
        LOAD_FLOAT(xorstr_("Avatar_OffsetY"),      Avatar_OffsetY)
#undef LOAD_BOOL
#undef LOAD_FLOAT
#undef LOAD_INT
    } catch (...) {}
}

// ─────────────────────────────────────────────
//  Main UI
// ─────────────────────────────────────────────
void Layout_tick_UI() {
    ImGuiIO& io = ImGui::GetIO();

    // ── ОПТИМИЗАЦИЯ #2/#4: жаңы кадр — кэш жараксыз болот
    ++g_FrameId;

    // ══════════════════════════════════════════════════════════════════
    //  ОПТИМИЗАЦИЯ #1 — СТИЛЬ БИР ЖОЛУ ГАНА ОРНОТУЛАТ
    //
    //  Мурда бул блок АР КАДРДА кайра иштечү: ~60 ImVec4 + 18 style
    //  талаасы × 62 fps = секундасына ~4800 бекер жазуу. Меню ЖАБЫК
    //  турганда да иштечү. Стиль эч качан өзгөрбөйт, демек бир жолу
    //  орноткон жетиштүү.
    // ══════════════════════════════════════════════════════════════════
    static bool s_StyleReady = false;
    if (!s_StyleReady) {
    s_StyleReady = true;
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowPadding    = ImVec2(20, 20);
    style.WindowRounding   = 12.0f;
    style.ChildRounding    = 8.0f;
    style.FramePadding     = ImVec2(12, 8);
    style.FrameRounding    = 10.0f;
    style.ItemSpacing      = ImVec2(15, 12);
    style.ItemInnerSpacing = ImVec2(10, 8);
    style.IndentSpacing    = 25.0f;
    style.ScrollbarSize    = 25.0f;
    style.ScrollbarRounding = 15.0f;
    style.GrabMinSize      = 20.0f;
    style.GrabRounding     = 6.0f;
    style.TabRounding      = 6.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(1,1,1,1);
    colors[ImGuiCol_TextDisabled]          = ImVec4(.5f,.5f,.5f,1);
    colors[ImGuiCol_WindowBg]              = ImVec4(.06f,.06f,.06f,.98f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0,0,0,0);
    colors[ImGuiCol_PopupBg]               = ImVec4(.08f,.08f,.08f,.94f);
    colors[ImGuiCol_Border]                = ImVec4(.25f,.25f,.25f,.5f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0,0,0,0);
    colors[ImGuiCol_FrameBg]               = ImVec4(.12f,.12f,.12f,.54f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(.18f,.18f,.18f,.4f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(.24f,.24f,.24f,.67f);
    colors[ImGuiCol_TitleBg]               = ImVec4(.04f,.04f,.04f,1);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(.1f,.1f,.1f,1);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0,0,0,.51f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(.02f,.02f,.02f,.53f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(.31f,.31f,.31f,1);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(.41f,.41f,.41f,1);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(.51f,.51f,.51f,1);
    colors[ImGuiCol_CheckMark]             = ImVec4(.8f,.8f,.8f,1);
    colors[ImGuiCol_SliderGrab]            = ImVec4(.4f,.4f,.4f,1);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(.6f,.6f,.6f,1);
    colors[ImGuiCol_Button]                = ImVec4(.15f,.15f,.15f,1);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(.25f,.25f,.25f,1);
    colors[ImGuiCol_ButtonActive]          = ImVec4(.35f,.35f,.35f,1);
    colors[ImGuiCol_Header]                = ImVec4(.2f,.2f,.2f,.31f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(.25f,.25f,.25f,.8f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(.3f,.3f,.3f,1);
    colors[ImGuiCol_Tab]                   = ImVec4(.1f,.1f,.1f,1);
    colors[ImGuiCol_TabHovered]            = ImVec4(.25f,.25f,.25f,1);
    colors[ImGuiCol_TabActive]             = ImVec4(.2f,.2f,.2f,1);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(.08f,.08f,.08f,1);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(.15f,.15f,.15f,1);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(.19f,.19f,.2f,1);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(.31f,.31f,.35f,1);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(.23f,.23f,.25f,1);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0,0,0,0);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1,1,1,.06f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(.35f,.35f,.35f,.35f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(.45f,.45f,.45f,1);
    }   // ── ОПТИМИЗАЦИЯ #1: стиль блогунун аягы (бир жолу гана)

    // ── ЖАҢЫ: scrollbar өлчөмү сыдырма менен реалдуу убакытта өзгөрөт,
    //    ошондуктан ал бир жолку блоктон СЫРТТА турат (арзан: бир жазуу).
    ImGui::GetStyle().ScrollbarSize = g_ScrollbarSize;

    if (is_root_mode) {
        DrawFloatingLogo();
        if (show_menu) {
            ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(xorstr_("KERNEL ZAKARIA"), &show_menu)) {

                // ══════════════════════════════════════════════════════
                //  ЖАҢЫ LAYOUT: сол жакта вертикалдуу таб тилкеси,
                //  жогоруда статус сабы, оң жакта сыдырылуучу мазмун.
                //  Меню элементтери эч өзгөргөн жок — жайгашуусу гана.
                // ══════════════════════════════════════════════════════
                static int s_Tab = 0;

                // ── ЖОГОРКУ СТАТУС САБЫ ───────────────────────────────
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.10f,.10f,.12f,.95f));
                    ImGui::BeginChild(xorstr_("##TopBar"), ImVec2(0, 52), true,
                                      ImGuiWindowFlags_NoScrollbar);

                    ImGui::TextColored(ImVec4(.85f,.25f,.25f,1), xorstr_("KERNEL ZAKARIA"));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(.45f,.45f,.45f,1), xorstr_("ML External Menu"));

                    // [ATTACHED] абалы
                    ImGui::SameLine(0, 30);
                    if (is_attached)
                        ImGui::TextColored(ImVec4(.2f,.9f,.4f,1), xorstr_("[ ATTACHED ]"));
                    else
                        ImGui::TextColored(ImVec4(1,.35f,.35f,1), xorstr_("[ SEARCHING ]"));

                    // Region
                    ImGui::SameLine(0, 12);
                    ImGui::TextColored(ImVec4(.35f,.35f,.35f,1), xorstr_("|"));
                    ImGui::SameLine(0, 12);
                    // ── xorstr_ убактылуу объектин указателге сактабайбыз:
                    //    std::string мазмунду КӨЧҮРӨТ, демек коопсуз.
                    std::string regionTxt = xorstr_("--");
                    if      (g_package_name == xorstr_("com.mobile.legends"))     regionTxt = xorstr_("GL");
                    else if (g_package_name == xorstr_("com.mobile.legends.usa")) regionTxt = xorstr_("USA");
                    else if (g_package_name == xorstr_("com.vng.mlbb"))           regionTxt = xorstr_("VNG");
                    ImGui::TextColored(ImVec4(.75f,.75f,.75f,1), xorstr_("Region: %s"), regionTxt.c_str());

                    // FPS
                    ImGui::SameLine(0, 12);
                    ImGui::TextColored(ImVec4(.35f,.35f,.35f,1), xorstr_("|"));
                    ImGui::SameLine(0, 12);
                    ImVec4 fpsC = (g_UI_Fps >= 55.0f) ? ImVec4(.2f,.9f,.4f,1)
                                : (g_UI_Fps >= 35.0f) ? ImVec4(1,.8f,.2f,1)
                                                      : ImVec4(1,.35f,.35f,1);
                    ImGui::TextColored(fpsC, xorstr_("FPS: %.0f"), g_UI_Fps);

                    // Ник / Account ID
                    ImGui::SameLine(0, 12);
                    ImGui::TextColored(ImVec4(.35f,.35f,.35f,1), xorstr_("|"));
                    ImGui::SameLine(0, 12);
                    if (!g_LoginServerCache.Nickname.empty())
                        ImGui::TextColored(ImVec4(.75f,.75f,.75f,1), xorstr_("%s"),
                                           g_LoginServerCache.Nickname.c_str());
                    else if (g_LoginServerCache.AccountId != 0)
                        ImGui::TextColored(ImVec4(.75f,.75f,.75f,1), xorstr_("ID: %llu"),
                                           (unsigned long long)g_LoginServerCache.AccountId);
                    else
                        ImGui::TextColored(ImVec4(.45f,.45f,.45f,1), xorstr_("ID: --"));

                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                // ── СОЛ ЖАКТАГЫ ТАБ ТИЛКЕСИ ───────────────────────────
                {
                    // ══════════════════════════════════════════════════
                    //  МААНИЛҮҮ: xorstr_() УБАКТЫЛУУ объект кайтарат —
                    //  ал чакыруу туюнтмасы бүткөндө эле жок болот.
                    //  Ошондуктан аны const char* га САКТООГО БОЛБОЙТ
                    //  (мис. static массивге): указатель асылып калып,
                    //  экран суроо белги / таштанды көрсөтөт.
                    //
                    //  Чечим: xorstr_() лямбданын аргументи катары ТИКЕ
                    //  берилет — убактылуу объект чакыруу аяктагыча жашайт.
                    // ══════════════════════════════════════════════════
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.08f,.08f,.09f,.95f));
                    ImGui::BeginChild(xorstr_("##Sidebar"), ImVec2(190, 0), true);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 14));

                    auto TabButton = [&](int idx, const char* name) {
                        bool sel = (s_Tab == idx);
                        if (sel) {
                            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(.70f,.10f,.10f,1));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(.80f,.15f,.15f,1));
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(.90f,.20f,.20f,1));
                        }
                        if (ImGui::Selectable(name, sel, 0, ImVec2(0, 42)))
                            s_Tab = idx;
                        if (sel) ImGui::PopStyleColor(3);
                        ImGui::Spacing();
                    };

                    TabButton(0, xorstr_("INFO"));
                    TabButton(1, xorstr_("ROOM"));
                    TabButton(2, xorstr_("ESP"));
                    TabButton(3, xorstr_("VISUAL"));
                    TabButton(4, xorstr_("RETRI"));
                    TabButton(5, xorstr_("SETTINGS"));

                    ImGui::PopStyleVar();
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                ImGui::SameLine();

                // ── ОҢ ЖАКТАГЫ МАЗМУН (сыдырылат) ─────────────────────
                ImGui::BeginChild(xorstr_("##Content"), ImVec2(0, 0), false);
                {

                    // ── INFO TAB ──────────────────────────────
                    if (s_Tab == 0) {
                        ImGui::BeginChild(xorstr_("##InfoBox"), ImVec2(0,0), false);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.8f,0.3f,1.0f));
                        ImGui::SetCursorPosX((ImGui::GetWindowWidth() -
                            ImGui::CalcTextSize(xorstr_("--- PREMIUM SUBSCRIPTION ---")).x) * 0.5f);
                        ImGui::Text(xorstr_("--- PREMIUM SUBSCRIPTION ---"));
                        ImGui::PopStyleColor();
                        ImGui::Separator(); ImGui::Spacing();
                        auto InfoRow = [](const char* label, const char* val,
                                          ImVec4 col = ImVec4(1,1,1,1)) {
                            ImGui::TextColored(ImVec4(.6f,.6f,.6f,1.0f), xorstr_("%s"), label);
                            ImGui::SameLine(160);
                            ImGui::TextColored(col, xorstr_(": %s"), val);
                            ImGui::Spacing();
                        };
                        // ══════════════════════════════════════════════
                        //  PERFORMANCE — оптимизациянын натыйжасы
                        // ══════════════════════════════════════════════
                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.3f,.9f,.5f,1.0f));
                        ImGui::SetCursorPosX((ImGui::GetWindowWidth() -
                            ImGui::CalcTextSize(xorstr_("--- PERFORMANCE ---")).x) * 0.5f);
                        ImGui::Text(xorstr_("--- PERFORMANCE ---"));
                        ImGui::PopStyleColor(); ImGui::Spacing();
                        {
                            char   fpsBuf[48];
                            float  fps = g_UI_Fps;
                            ImVec4 fpsCol = (fps >= 55.0f) ? ImVec4(.2f,.9f,.4f,1)
                                          : (fps >= 35.0f) ? ImVec4(1,.8f,.2f,1)
                                                           : ImVec4(1,.3f,.3f,1);
                            snprintf(fpsBuf, sizeof(fpsBuf), xorstr_("%.1f FPS"), fps);
                            InfoRow(xorstr_("Overlay FPS"), fpsBuf, fpsCol);

                            char msBuf[48];
                            snprintf(msBuf, sizeof(msBuf), xorstr_("%.2f ms"), g_UI_FrameMs);
                            InfoRow(xorstr_("Frame time"), msBuf,
                                    (g_UI_FrameMs < 16.0f) ? ImVec4(.2f,.9f,.4f,1)
                                                           : ImVec4(1,.8f,.2f,1));

                            char entBuf[48];
                            snprintf(entBuf, sizeof(entBuf), xorstr_("%d"),
                                     (int)g_Frame.enemies.size());
                            InfoRow(xorstr_("Enemies tracked"), entBuf, ImVec4(.8f,.8f,.8f,1));

                            char cdBuf[48];
                            snprintf(cdBuf, sizeof(cdBuf), xorstr_("%d / %d"),
                                     (int)g_CDCache.size(), (int)CD_MAP_MAX);
                            InfoRow(xorstr_("CD cache"), cdBuf, ImVec4(.8f,.8f,.8f,1));
                        }

                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.7f,.7f,.7f,1.0f));
                        ImGui::SetCursorPosX((ImGui::GetWindowWidth() -
                            ImGui::CalcTextSize(xorstr_("--- DEVICE INFORMATION ---")).x) * 0.5f);
                        ImGui::Text(xorstr_("--- DEVICE INFORMATION ---"));
                        ImGui::PopStyleColor(); ImGui::Spacing();
                        char model[PROP_VALUE_MAX] = {0};
                        __system_property_get(xorstr_("ro.product.model"), model);
                        InfoRow(xorstr_("Model"), model);
                        InfoRow(xorstr_("Package"), g_package_name.c_str());
                        InfoRow(xorstr_("Injection"),
                                is_attached ? xorstr_("Active") : xorstr_("Pending"),
                                is_attached ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1));
                        ImGui::Spacing(); ImGui::Separator();
                        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 45);
                        float cw = ImGui::CalcTextSize(xorstr_("Copyright (c) 2026 @system.mlbb ")).x;
                        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - cw) * 0.5f);
                        ImGui::TextDisabled(xorstr_("Copyright (c) 2026 @system.mlbb "));
                        ImGui::EndChild();
                    }

                    // ── ROOM TAB ──────────────────────────────
                    if (s_Tab == 1) {
                        RenderRoomPlayerInfoImGui();
                    }

                    // ── ESP TAB ───────────────────────────────
                    if (s_Tab == 2) {
                        if (ImGui::CollapsingHeader(xorstr_("ESP LINE"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("ESP Line"),        &drawMLine);
                            if (drawMLine) {
                                ImGui::CustomCheckbox(xorstr_("Line From Foot"), &espLineFromFooter);
                                ImGui::SliderFloat(xorstr_("Line Thickness"),    &espLineThickness, 0.5f, 6.0f);
                            }
                        }
                        if (ImGui::CollapsingHeader(xorstr_("ESP BOX"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("ESP Box"),         &drawEspBox);
                            if (drawEspBox) {
                                ImGui::CustomCheckbox(xorstr_("Corner Style"),   &espBoxCornerStyle);
                                ImGui::SliderFloat(xorstr_("Box Thickness"),     &espBoxThickness,  0.5f,   5.0f);
                                if (espBoxCornerStyle)
                                    ImGui::SliderFloat(xorstr_("Corner Len %"),  &espBoxCornerLen,  0.1f,   0.5f);
                                ImGui::SliderFloat(xorstr_("Box Offset Y"),      &espBoxOffsetY,  -200.0f, 200.0f);
                                ImGui::SliderFloat(xorstr_("Box Offset X"),      &espBoxOffsetX,  -200.0f, 200.0f);
                                ImGui::SliderFloat(xorstr_("Box Scale"),         &espBoxScale,      0.3f,   3.0f);
                                // ── ЖАҢЫ: HP шкаласын чоңойтуп/кичирейтүү
                                //    (минимаптагы Hero Icon Size сыдырмасындай)
                                ImGui::SliderFloat(xorstr_("HP Bar Size"),       &espBoxHpBarSize,  1.0f,  20.0f);
                                float col[4] = {espBoxR, espBoxG, espBoxB, espBoxA};
                                if (ImGui::ColorEdit4(xorstr_("Box Color"), col)) {
                                    espBoxR = col[0]; espBoxG = col[1];
                                    espBoxB = col[2]; espBoxA = col[3];
                                }
                            }
                        }
                        if (ImGui::CollapsingHeader(xorstr_("PLAYER ESP"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("Hero ESP"),        &iconhero);
                            if (iconhero || ESP_Player_Cooldown) {
                                ImGui::SliderFloat(xorstr_("ESP Height"),  &Avatar_OffsetY,  0.0f, 500.0f);
                                ImGui::SliderFloat(xorstr_("Icon Size"),   &g_HeroIconSize, 10.0f,  60.0f);
                            }
                            ImGui::CustomCheckbox(xorstr_("Prediction ESP"), &drawMPrediction);
                            if (drawMPrediction) {
                                ImGui::SliderFloat(xorstr_("Pred Line Length"), &predictionLineLengthMult, 0.1f, 5.0f);
                            }
                        }
                        // ── DRONE VIEW АЛЫНЫП САЛЫНДЫ (колдонуучунун талабы)
                        if (ImGui::CollapsingHeader(xorstr_("COOLDOWN"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("Enemy Cooldown"), &ESP_Player_Cooldown);
                            if (ESP_Player_Cooldown)
                                ImGui::SliderFloat(xorstr_("CD Size"), &Cooldown_Size, 5.0f, 40.0f);
                            ImGui::CustomCheckbox(xorstr_("Show Spell"), &Cooldown_ShowSpell);
                        }
                    }

                    // ── VISUAL TAB ────────────────────────────
                    if (s_Tab == 3) {
                        if (ImGui::CollapsingHeader(xorstr_("MONSTERS"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("Monster ESP"), &drawMonsterName);
                            if (drawMonsterName)
                                ImGui::SliderFloat(xorstr_("Monster Icon Size"),
                                                   &g_MonsterIconSize, 10.0f, 60.0f);
                        }
                        if (ImGui::CollapsingHeader(xorstr_("MINIMAP"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("Enable Minimap"),    &MinimapIcon);
                            ImGui::CustomCheckbox(xorstr_("Hide Minimap Line"), &HideLine);
                            // ── MONSTER TOGGLE: MinimapIconBuff колдонот
                            ImGui::CustomCheckbox(xorstr_("Show Monsters on Map"), &MinimapIconBuff);
                            ImGui::CustomCheckbox(xorstr_("Show Minion Dot"),   &drawMinionMinimap);
                            ImGui::SliderInt(xorstr_("Minimap Size"), &MinimapSize, 100, 600);
                            ImGui::SliderInt(xorstr_("Minimap X"),    &MinimapPos,    0, 1000);
                            ImGui::SliderInt(xorstr_("Minimap Y"),    &MinimapPosY,   0, 1000);
                            ImGui::SliderInt(xorstr_("Hero Icon Size"),    &g_MinimapHeroSize,    10, 100);
                            ImGui::SliderInt(xorstr_("Monster Icon Size"), &g_MinimapMonsterSize, 10, 100);
                            if (ImGui::TreeNode(xorstr_("Calibration"))) {
                                ImGui::SliderFloat(xorstr_("Scale"),    &g_MinimapScale,  10.0f, 200.0f);
                                ImGui::SliderFloat(xorstr_("Offset X"), &g_Res1_OffsetX, -100.0f, 100.0f);
                                ImGui::SliderFloat(xorstr_("Offset Y"), &g_Res1_OffsetY, -100.0f, 100.0f);
                                ImGui::TreePop();
                            }
                        }
                        if (ImGui::CollapsingHeader(xorstr_("ALERT WARNING"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("Show Alert"),    &drawAlertUnderAttack);
                            ImGui::CustomCheckbox(xorstr_("Show Alert HP"), &Alert_ShowHPText);
                            ImGui::SliderFloat(xorstr_("Alert X (%)"), &Alert_PosX,  0.0f, 1.0f);
                            ImGui::SliderFloat(xorstr_("Alert Y (%)"), &Alert_PosY,  0.0f, 1.0f);
                            ImGui::SliderFloat(xorstr_("Alert Scale"), &Alert_Scale, 0.5f, 3.0f);
                        }
                    }

                    // ── RETRI TAB ─────────────────────────────
                    if (s_Tab == 4) {
                        if (ImGui::CollapsingHeader(xorstr_("AUTO RETRI"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("Enable Auto Retri"), &autoRetribution);
                            ImGui::CustomCheckbox(xorstr_("Show Circle"),       &showRetriCircle);
                            ImGui::SliderFloat(xorstr_("Retri X"), &retriTouchX, 0.0f, 3000.0f);
                            ImGui::SliderFloat(xorstr_("Retri Y"), &retriTouchY, 0.0f, 1500.0f);
                        }
                        if (ImGui::CollapsingHeader(xorstr_("TARGETS"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::CustomCheckbox(xorstr_("Buff Red"),  &AutoRetributionRed);
                            ImGui::CustomCheckbox(xorstr_("Buff Blue"), &AutoRetributionBlue);
                            ImGui::CustomCheckbox(xorstr_("Lord"),      &AutoRetributionLord);
                            ImGui::CustomCheckbox(xorstr_("Turtle"),    &AutoRetributionTurtle);
                            ImGui::CustomCheckbox(xorstr_("Crab"),      &AutoRetributionCrab);
                            ImGui::CustomCheckbox(xorstr_("Lito"),      &AutoRetributionLito);
                        }
                    }

                    // ── SETTINGS TAB ──────────────────────────
                    if (s_Tab == 5) {
                        if (ImGui::CollapsingHeader(xorstr_("CONFIG"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            if (ImGui::Button(xorstr_("RE-SCAN GAME"), ImVec2(-1,0)))
                                AttachToGame();
                            ImGui::Text(xorstr_("Status: %s"),
                                        is_attached
                                        ? xorstr_("READY (Auto-Attached)")
                                        : xorstr_("SEARCHING GAME..."));
                            ImGui::Spacing();
                            if (ImGui::Button(xorstr_("SAVE SETTINGS"), ImVec2(-1,0)))
                                SaveSTACOOLexSettings();
                            if (ImGui::Button(xorstr_("LOAD SETTINGS"), ImVec2(-1,0)))
                                LoadSTACOOLexSettings();
                        }
                        if (ImGui::CollapsingHeader(xorstr_("SYSTEM"),
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            static float opacity = 1.0f;
                            if (ImGui::SliderFloat(xorstr_("UI Opacity"), &opacity, 0.1f, 1.0f))
                                ImGui::GetStyle().Alpha = opacity;

                            // ── ЖАҢЫ: астына жылдыруучу таякчанын (scrollbar)
                            //    өлчөмүн жөнгө салуучу сыдырма
                            ImGui::SliderFloat(xorstr_("Scrollbar Size"),
                                               &g_ScrollbarSize, 8.0f, 60.0f);

                            // ── FIX #2: баскыч менен гана чыгат
                            ImGui::Spacing();
                            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(.8f,0,0,.8f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f,0,0,1));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(.6f,0,0,1));
                            if (ImGui::Button(xorstr_("EXIT"), ImVec2(-1,0)))
                                g_RequestExit = true;
                            ImGui::PopStyleColor(3);
                        }
                    }

                }
                ImGui::EndChild();   // ##Content
                ImGui::End();
            }
        }
    }

    abs_ScreenX = (int)io.DisplaySize.x;
    abs_ScreenY = (int)io.DisplaySize.y;

    if (MinimapIcon)
        DrawMinimapESP(ImGui::GetForegroundDrawList());

    DrawMonster(ImGui::GetForegroundDrawList());

    // EXIT: толук чыгат, терминал input кайтпайт
    if (g_RequestExit) {
        main_thread_flag = false;
        // Тазалоо
        g_RoomThreadShouldStop = true;
        system(xorstr_("settings put global private_dns_mode off"));
        curl_global_cleanup();
        fflush(stdout);
        fflush(stderr);
        _exit(0); // толук токтойт, терминалга кайтпайт
    }
}

// ─────────────────────────────────────────────
//  PID Monitor
// ─────────────────────────────────────────────
__attribute__((visibility("default"))) void* pid_monitor(void*) {
    while (main_thread_flag) {
        pid_t pid = 0;
        if (g_package_name == xorstr_("com.mobile.legends"))
            pid = pidof(xorstr_("com.mobile.legends:UnityKillsMe"));
        else if (g_package_name == xorstr_("com.mobile.legends.usa"))
            pid = pidof(xorstr_("com.mobile.legends.usa:UnityKillsMe"));
        else if (g_package_name == xorstr_("com.vng.mlbb"))
            pid = pidof(xorstr_("com.vng.mlbb:UnityKillsMe"));

        if (pid <= 0) {
            pid = pidof(xorstr_("com.mobile.legends:UnityKillsMe"));
            if (pid <= 0) pid = pidof(xorstr_("com.mobile.legends.usa:UnityKillsMe"));
            if (pid <= 0) pid = pidof(xorstr_("com.vng.mlbb:UnityKillsMe"));
        }
        if (pid <= 0) {
            // ── KERNEL-SAFE FIX: оюн жабылганда libbase дагы тазаланат.
            //    Мурда is_attached гана false болуп, libbase эски (эми
            //    жараксыз) дарек бойдон кала берчү. Оюн кайра ачылганда
            //    ошол эски даректен кернел окуу жасалып panic болчу.
            is_attached = false;
            libbase     = 0;
            g_pid       = 0;
            PruneCooldownMaps(true);   // ── ОПТИМИЗАЦИЯ #5: кэштерди тазалоо
        } else if (!is_attached || pid != g_pid) {
            // pid өзгөрсө (оюн кайра ачылса) кайра attach болот
            AttachToGame();
        }
        usleep(500000);
    }
    return nullptr;
}

// ─────────────────────────────────────────────
//  Game Worker — FIX #7: бирдей pointer chain
// ─────────────────────────────────────────────
__attribute__((visibility("default"))) void* game_worker(void*) {
    auto lastUpdate = std::chrono::steady_clock::now();
    while (main_thread_flag) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - lastUpdate).count();
        // ── KERNEL-SAFE FIX: 1ms → 8ms.
        //    Мурда 1ms'те бир кернел драйверге сурам кетип, драйвер
        //    дем алууга үлгүрбөй жаткан.
        if (elapsed >= 8) {
            // ── KERNEL-SAFE FIX: MonsterRetribution() мурда коргоосуз
            //    чакырылчу — attach болмоюнча драйверге кайрылбасын.
            if (is_attached && libbase != 0) {
                MonsterRetribution();

                // ── ОПТИМИЗАЦИЯ #2: бул жерде chain кайра кыдырылып,
                //    selfAddr окулуп, анан ЭЧ КАЙДА КОЛДОНУЛБАЙ ТАШТАЛЧУ
                //    ((void)selfAddr). Skill feature алынганда калып калган
                //    өлүк код эле — 8ms сайын 4 бекер драйвер окуусу.
                //    Толугу менен алынып салынды.
            }
            // Login info жаңыртуу — 1000ms'де бир жолу жетиштүү.
            // ── KERNEL-SAFE FIX: 100ms → 1000ms, is_attached кошулду.
            static auto lastLoginUpdate = std::chrono::steady_clock::now();
            auto loginElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now - lastLoginUpdate).count();
            if (loginElapsed >= 1000 && is_attached && libbase != 0) {
                DRIVER_LOCK();   // ── KERNEL-SAFE FIX: драйверге жалгыз кайрылуу
                LoginServerData newData = ReadLoginServerInfo(libbase);
                if (newData.IsValid) {
                    if (g_LoginServerCache.Nickname.empty())  g_LoginServerCache.Nickname  = newData.Nickname;
                    if (g_LoginServerCache.AccountId == 0)    g_LoginServerCache.AccountId = newData.AccountId;
                    g_LoginServerCache.ClientRealVersion      = newData.ClientRealVersion;
                    g_LoginServerCache.RegionId               = newData.RegionId;
                }
                lastLoginUpdate = now;
            }
            lastUpdate = now;
        }
        // ── KERNEL-SAFE FIX: 4ms → 8ms, main loop (16ms) менен так 2:1
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    return nullptr;
}

// ─────────────────────────────────────────────
//  main()
// ─────────────────────────────────────────────
#include <sys/wait.h>

__attribute__((visibility("default"))) int main(int argc, char* argv[]) {
    pid_t watchdog_pid = fork();
    if (watchdog_pid < 0) {
        // fork ката — child катары улантабыз
    } else if (watchdog_pid > 0) {
        // PARENT: child бүткөнгө чейин күтүп, чыгат
        int status;
        waitpid(watchdog_pid, &status, 0);
        return 0; // ← Parent толук чыгат, кайра башталбайт
    }
    // CHILD ГАНА жеткен жер — негизги логика башталат

    curl_global_init(CURL_GLOBAL_DEFAULT);
    is_root_mode = true;
    bool use_kernel = false;

    printf(xorstr_("==================================================\n"));
    printf(xorstr_("   EXTERNAL MLBB ZAKARIA V0 PRO\n"));
    printf(xorstr_("   Developer: @Zakarya_787\n"));
    printf(xorstr_("==================================================\n"));
    printf(xorstr_("Select Driver Mode:\n"));
    printf(xorstr_("[1] NORMAL (ROOT)\n"));
    printf(xorstr_("[2] KERNEL (PRO)\n"));
    printf(xorstr_("--------------------------------------------------\n"));
    printf(xorstr_("Input: "));
    fflush(stdout);

    char input[8] = {0};
    if (fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = 0;
        if (strcmp(input, xorstr_("2")) == 0)
            use_kernel = true;
    }
    if (use_kernel) {
        g_driver = new KernelDriver();
        Memory::SetReadMode(Memory::ReadMode::KernelDriver);
    } else {
        Memory::SetReadMode(Memory::ReadMode::Userspace);
    }

    system(xorstr_("settings put global private_dns_mode hostname"));
    system(xorstr_("settings put global private_dns_specifier dns.adguard.com"));

    screen_config();
    ::abs_ScreenX = (displayInfo.height > displayInfo.width
                     ? displayInfo.height : displayInfo.width);
    ::abs_ScreenY = (displayInfo.height < displayInfo.width
                     ? displayInfo.height : displayInfo.width);
    ::native_window_screen_x = ::abs_ScreenX;
    ::native_window_screen_y = ::abs_ScreenY;

    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true))
        return -1;

    Touch_Init(displayInfo.width, displayInfo.height,
               displayInfo.orientation, false);

    ImGui::StyleColorsDark();
    {
        ImGuiStyle& s = ImGui::GetStyle();
        s.Colors[ImGuiCol_TitleBgActive]      = ImVec4(.8f,0,0,1);
        s.Colors[ImGuiCol_FrameBgHovered]     = ImVec4(.8f,0,0,.4f);
        s.Colors[ImGuiCol_FrameBgActive]      = ImVec4(.8f,0,0,.67f);
        s.Colors[ImGuiCol_CheckMark]          = ImVec4(1,0,0,1);
        s.Colors[ImGuiCol_SliderGrab]         = ImVec4(.8f,0,0,1);
        s.Colors[ImGuiCol_SliderGrabActive]   = ImVec4(1,0,0,1);
        s.Colors[ImGuiCol_Button]             = ImVec4(.8f,0,0,.4f);
        s.Colors[ImGuiCol_ButtonHovered]      = ImVec4(.8f,0,0,1);
        s.Colors[ImGuiCol_ButtonActive]       = ImVec4(1,0,0,1);
        s.Colors[ImGuiCol_Header]             = ImVec4(.8f,0,0,.31f);
        s.Colors[ImGuiCol_HeaderHovered]      = ImVec4(.8f,0,0,.8f);
        s.Colors[ImGuiCol_HeaderActive]       = ImVec4(.8f,0,0,1);
        s.Colors[ImGuiCol_Tab]                = ImVec4(.8f,0,0,.86f);
        s.Colors[ImGuiCol_TabHovered]         = ImVec4(1,0,0,.8f);
        s.Colors[ImGuiCol_TabActive]          = ImVec4(1,0,0,1);
        s.Colors[ImGuiCol_TabUnfocused]       = ImVec4(.4f,0,0,.97f);
        s.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(.6f,0,0,1);
        s.WindowRounding = 8.0f;
    }

    pthread_t pid_tid, worker_tid;
    pthread_create(&pid_tid,    nullptr, pid_monitor,  nullptr);
    pthread_create(&worker_tid, nullptr, game_worker,  nullptr);
    pthread_detach(pid_tid);
    pthread_detach(worker_tid);

    g_RoomThreadShouldStop = false;
    g_RoomInfoThread = new std::thread(RoomInfoReaderThread);

    struct timespec lastTime;
    clock_gettime(CLOCK_MONOTONIC, &lastTime);
    LoadSTACOOLexSettings();

    // ── Лаг азайтуу: наносекунддук уйку, frame cap
    // ── KERNEL-SAFE FIX: 13.333ms → 16ms.
    //    Башталгыч main.cpp'те main loop = 1ms, game_worker = 1ms болуп,
    //    эки thread бир такт менен иштечү. Оптимизациядан кийин
    //    13.333ms / 4ms = 3.333… — бүтүн эсе эмес, фазалар дайыма жылып
    //    үч thread тең бир убакта драйверге кайрылган учурлар пайда болгон.
    //    Эми 16ms / 8ms = так 2:1 — гармоникалуу, кагылышуу минималдуу.

    while (main_thread_flag) {
        drawBegin();

        struct timespec currentTime;
        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        float deltaTime = (currentTime.tv_sec  - lastTime.tv_sec) +
                          (currentTime.tv_nsec - lastTime.tv_nsec) / 1e9f;
        lastTime = currentTime;

        Layout_tick_UI();
        drawEnd();


        // ══════════════════════════════════════════════════════════════
        //  РЕГРЕССИЯ ОҢДОО — баштапкы ритм кайтарылды
        //
        //  Мен бул жерге 16ms frame-pacing киргизген элем. Чоң ката
        //  болуптур: негизги цикл тийүү окуяларын да ушул жерде тейлейт
        //  (drawBegin → ImGui Android backend). 1ms → 16ms дегени
        //  тийүүнү 16 ЭСЕ сейрек текшерүү дегени.
        //
        //  Натыйжа: джойстикти кармап турганда тийүү окуяларынын агымы
        //  толуп кетип, навык басуу каттала албай калчу. Токтоп турганда
        //  агым жок, ошондуктан иштечү.
        //
        //  Баштапкы долбоордо usleep(1000) эле жана баг жок болчу.
        // ══════════════════════════════════════════════════════════════

        // FPS ченөө (INFO табы үчүн) — цикл ритмине тийбейт
        {
            struct timespec frameEnd;
            clock_gettime(CLOCK_MONOTONIC, &frameEnd);
            static struct timespec prevFrame = frameEnd;
            long dns = (frameEnd.tv_sec - prevFrame.tv_sec) * 1000000000L
                     + (frameEnd.tv_nsec - prevFrame.tv_nsec);
            prevFrame = frameEnd;
            if (dns > 0) {
                float frameMs = dns / 1000000.0f;
                g_UI_FrameMs  = (g_UI_FrameMs <= 0.0f)
                              ? frameMs : g_UI_FrameMs * 0.9f + frameMs * 0.1f;
                float instFps = 1000.0f / frameMs;
                g_UI_Fps      = (g_UI_Fps <= 0.0f)
                              ? instFps : g_UI_Fps * 0.9f + instFps * 0.1f;
            }
        }

        usleep(1000);
    }

    g_RoomThreadShouldStop = true;
    if (g_RoomInfoThread && g_RoomInfoThread->joinable()) {
        g_RoomInfoThread->join();
        delete g_RoomInfoThread;
        g_RoomInfoThread = nullptr;
    }

    system(xorstr_("settings put global private_dns_mode off"));
    curl_global_cleanup();
    shutdown();
    Touch_Close();
    return 0;
}
