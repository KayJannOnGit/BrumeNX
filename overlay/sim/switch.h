// A fake <switch.h> for rendering Brume Overlay's screens on the PC (nothing of the system works here).
#pragma once
#include <cstdint>
#include <cstring>
#include <chrono>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64; typedef u32 Result; typedef u32 Handle;
#define R_SUCCEEDED(r) ((r) == 0)
#define R_FAILED(r) ((r) != 0)
#define MAKERESULT(m, d) (1u)
#define INVALID_HANDLE 0
enum { Module_Libnx = 345 };
enum { LibnxError_InitFail_SM = 1, LibnxError_OutOfMemory = 2 };
// the threads run at once (in the caller); the capture reads sim/game.jpg (made by render.py: a stand-in for a game)
struct Thread { void (*fn)(void *); void *arg; };
static inline Result threadCreate(Thread *t, void (*fn)(void *), void *arg, void *, size_t, int, int) { t->fn = fn; t->arg = arg; return 0; }
static inline Result threadStart(Thread *t) { t->fn(t->arg); return 0; }
static inline Result threadWaitForExit(Thread *) { return 0; } static inline Result threadClose(Thread *) { return 0; }
enum ViLayerStack { ViLayerStack_Screenshot = 2 };
static inline Result capsscInitialize() { return 0; } static inline void capsscExit() {}
#include <cstdio>
static inline Result capsscCaptureJpegScreenShot(u64 *size, void *buf, size_t cap, ViLayerStack, long long) {
    FILE *f = fopen("game.jpg", "rb");
    if (!f) return 1;
    *size = fread(buf, 1, cap, f); fclose(f);
    return *size ? 0 : 1;
}
struct Event { int x; }; struct Service { int x; }; struct NWindow { int x; }; struct AppletStorage { int x; };
struct Framebuffer { int x; };
struct PadState { u64 d; };
struct PlFontData { void *address; u32 size; };
struct TimeCalendarTime { int year, month, day, hour, minute, second; };
struct TimeCalendarAdditionalInfo { int x; };
enum SetLanguage { SetLanguage_FR, SetLanguage_FRCA, SetLanguage_ENGB };
enum PsmChargerType { PsmChargerType_Unconnected, PsmChargerType_EnoughPower };
enum { PlSharedFontType_Standard = 0 }; enum { PlServiceType_User = 0 }; enum { NifmServiceType_System = 0 };
enum { TimeType_LocalSystemClock = 0, TimeType_UserSystemClock = 1 }; enum { PIXEL_FORMAT_RGBA_8888 = 0 }; enum { HidNpadStyleSet_NpadStandard = 0 };
enum : u64 { HidNpadButton_A = 1, HidNpadButton_B = 2, HidNpadButton_Plus = 4, HidNpadButton_L = 8, HidNpadButton_R = 16,
             HidNpadButton_ZL = 32, HidNpadButton_ZR = 64, HidNpadButton_AnyUp = 128, HidNpadButton_AnyDown = 256,
             HidNpadButton_AnyLeft = 512, HidNpadButton_AnyRight = 1024 };
static inline u64 armGetSystemTick() { return (u64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static inline u64 armGetSystemTickFreq() { return 1000000; }
static inline Result setInitialize() { return 1; } static inline void setExit() {}
static inline Result setGetSystemLanguage(u64 *) { return 1; } static inline Result setMakeLanguage(u64, SetLanguage *) { return 1; }
static inline Result psmInitialize() { return 0; }
static inline Result psmGetBatteryChargePercentage(u32 *p) { *p = 76; return 0; }
static inline Result psmGetChargerType(PsmChargerType *t) { *t = PsmChargerType_Unconnected; return 0; }
static inline Result timeInitialize() { return 0; }
static inline Result timeGetCurrentTime(int, u64 *t) { *t = 1795000000ull; return 0; }
static inline Result timeToCalendarTimeWithMyRule(u64, TimeCalendarTime *c, TimeCalendarAdditionalInfo *) { c->year = 2026; c->month = 11; c->day = 19; c->hour = 21; c->minute = 24; return 0; }
static inline Result appletCreateStorage(AppletStorage *, s64) { return 1; }
static inline Result appletStorageWrite(AppletStorage *, s64, const void *, size_t) { return 1; }
static inline Result appletPushToGeneralChannel(AppletStorage *) { return 1; } static inline void appletStorageClose(AppletStorage *) {}
static inline Result lblInitialize() { return 0; }
static inline Result lblGetCurrentBrightnessSetting(float *f) { *f = 0.62f; return 0; }
static inline Result lblIsAutoBrightnessControlEnabled(bool *b) { *b = false; return 0; }
static inline Result lblSetCurrentBrightnessSetting(float) { return 0; } static inline Result lblApplyCurrentBrightnessSettingToBacklight() { return 0; }
static inline Result lblSaveCurrentSetting() { return 0; } static inline Result lblEnableAutoBrightnessControl() { return 0; }
static inline Result lblDisableAutoBrightnessControl() { return 0; }
static inline Result nifmInitialize(int) { return 0; }
static inline Result nifmIsWirelessCommunicationEnabled(bool *b) { *b = true; return 0; } static inline Result nifmSetWirelessCommunicationEnabled(bool) { return 0; }
static inline Result setsysInitialize() { return 0; }
static inline Result setsysGetNfcEnableFlag(bool *b) { *b = true; return 0; } static inline Result setsysSetNfcEnableFlag(bool) { return 0; }
static inline Result plInitialize(int) { return 1; } static inline Result plGetSharedFontByType(PlFontData *, int) { return 1; }
static inline Result hidInitialize() { return 0; }
static inline void padConfigureInput(int, int) {} static inline void padInitializeAny(PadState *) {}
static inline void padUpdate(PadState *) {} static inline u64 padGetButtonsDown(PadState *) { return 0; }
static inline NWindow *nwindowGetDefault() { return nullptr; }
static inline Result framebufferCreate(Framebuffer *, NWindow *, int, int, int, int) { return 1; }
static inline void framebufferMakeLinear(Framebuffer *) {} static inline void framebufferClose(Framebuffer *) {}
static inline void *framebufferBegin(Framebuffer *, u32 *) { return nullptr; } static inline void framebufferEnd(Framebuffer *) {}
static inline bool appletMainLoop() { return false; } static inline void svcSleepThread(u64) {}
// v74.4: the sounds (audout: no sound on the PC - the overlay's silent path)
struct AudioOutBuffer { AudioOutBuffer *next; void *buffer; u64 buffer_size, data_size, data_offset; };
static inline Result audoutInitialize() { return 1; } static inline void audoutExit() {}
static inline u32 audoutGetSampleRate() { return 48000; } static inline u32 audoutGetChannelCount() { return 2; }
static inline Result audoutStartAudioOut() { return 1; } static inline Result audoutStopAudioOut() { return 0; }
static inline Result audoutAppendAudioOutBuffer(AudioOutBuffer *) { return 1; }
static inline Result audoutContainsAudioOutBuffer(AudioOutBuffer *, bool *c) { *c = false; return 0; }
static inline void armDCacheFlush(void *, size_t) {}
// v98: the « centre de contrôle » (made-up values: a game running for 1 h 20, Joy-Con + a Pro controller, 46 °C)
static inline bool hosversionAtLeast(int, int, int) { return true; }
enum AudioTarget { AudioTarget_Invalid = 0, AudioTarget_Speaker = 1, AudioTarget_Headphone = 2, AudioTarget_Tv = 3 };
static s32 g_simVol = 9; static bool g_simMute = false;
static inline Result audctlInitialize() { return 0; } static inline void audctlExit() {}
static inline Result audctlGetActiveOutputTarget(AudioTarget *t) { *t = AudioTarget_Speaker; return 0; }
static inline Result audctlGetDefaultTarget(AudioTarget *t) { *t = AudioTarget_Speaker; return 0; }
static inline Result audctlGetTargetVolumeMin(s32 *v) { *v = 0; return 0; } static inline Result audctlGetTargetVolumeMax(s32 *v) { *v = 15; return 0; }
static inline Result audctlGetTargetVolume(s32 *v, AudioTarget) { *v = g_simVol; return 0; }
static inline Result audctlSetTargetVolume(AudioTarget, s32 v) { g_simVol = v; return 0; }
static inline Result audctlIsTargetMute(bool *m, AudioTarget) { *m = g_simMute; return 0; }
static inline Result audctlSetTargetMute(AudioTarget, bool m) { g_simMute = m; return 0; }
struct TsSession { int x; };
enum : u32 { TsDeviceCode_LocationInternal = 0x41000001u, TsDeviceCode_LocationExternal = 0x41000002u };
static inline Result tsInitialize() { return 0; } static inline void tsExit() {}
static inline Result tsOpenSession(TsSession *, u32) { return 0; } static inline void tsSessionClose(TsSession *) {}
static inline Result tsSessionGetTemperature(TsSession *, float *c) { *c = 46.4f; return 0; }
struct ClkrstSession { int x; };
enum PcvModuleId { PcvModuleId_CpuBus = 0x40000001, PcvModuleId_GPU = 0x40000002, PcvModuleId_EMC = 0x40000039 };
static inline Result clkrstInitialize() { return 0; } static inline void clkrstExit() {}
static inline Result clkrstOpenSession(ClkrstSession *, PcvModuleId, u32) { return 0; } static inline void clkrstCloseSession(ClkrstSession *) {}
static inline Result clkrstGetClockRate(ClkrstSession *, u32 *hz) { *hz = 1020000000u; return 0; }
static inline Result pmdmntInitialize() { return 0; } static inline void pmdmntExit() {}
static inline Result pmdmntGetApplicationProcessId(u64 *p) { *p = 0x51; return 0; }
static inline Result pmshellInitialize() { return 1; } static inline void pmshellExit() {}
static inline Result pmshellGetApplicationProcessIdForShell(u64 *) { return 1; }
static inline Result pminfoInitialize() { return 0; } static inline void pminfoExit() {}
static inline Result pminfoGetProgramId(u64 *id, u64) { *id = 0x0100ABCD12340000ull; return 0; }
struct NacpLanguageEntry { char name[0x200]; char author[0x100]; };
struct NacpStruct { NacpLanguageEntry lang[16]; u8 rest[0x1000]; };
struct NsApplicationControlData { NacpStruct nacp; u8 icon[0x20000]; };
enum NsApplicationControlSource { NsApplicationControlSource_CacheOnly = 0, NsApplicationControlSource_Storage = 1 };
static inline Result nsInitialize() { return 0; } static inline void nsExit() {}
static inline Result nsGetApplicationControlData(NsApplicationControlSource, u64, NsApplicationControlData *b, size_t, u64 *sz) {
    memset(&b->nacp, 0, sizeof b->nacp); strcpy(b->nacp.lang[0].name, "Les Brumes d'Orion : \xc3\x89" "dition D\xc3\xa9" "finitive"); *sz = sizeof *b; return 0;
}
static inline Result nacpGetLanguageEntry(NacpStruct *n, NacpLanguageEntry **e) { *e = &n->lang[0]; return 0; }
enum { PdmPlayEventType_Applet = 0 }; enum { PdmAppletEventType_Launch = 0 }; enum { AppletId_application = 1 };
struct PdmPlayEvent {
    union { struct { u32 program_id[2]; u32 unk; u8 applet_id, storage_id, log_policy, event_type; u8 unused[0xc]; } applet; u8 data[0x1c]; } event_data;
    u8 play_event_type; u8 pad[3]; u64 timestamp_user, timestamp_network, timestamp_steady;
};
constexpr u64 kSimNow = 1795000000ull;
static inline Result pdmqryInitialize() { return 0; } static inline void pdmqryExit() {}
static inline Result pdmqryGetAvailablePlayEventRange(s32 *t, s32 *a, s32 *b) { *t = 40; *a = 0; *b = 39; return 0; }
static inline Result pdmqryQueryPlayEvent(s32 from, PdmPlayEvent *ev, s32 count, s32 *n) {
    memset(ev, 0, sizeof(PdmPlayEvent) * count); *n = 0;
    for (s32 i = 0; i < count && from + i <= 39; i++) {                   // (entry 30: the game's launch, 1 h 20 ago)
        PdmPlayEvent &e = ev[i]; e.play_event_type = 0; e.event_data.applet.applet_id = 1; e.event_data.applet.event_type = (from + i) == 30 ? 0 : 2;
        e.event_data.applet.program_id[0] = 0x0100ABCDu; e.event_data.applet.program_id[1] = 0x12340000u; e.timestamp_user = kSimNow - 80 * 60; ++*n;
    }
    return 0;
}
enum HidNpadIdType { HidNpadIdType_No1 = 0, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4, HidNpadIdType_No5, HidNpadIdType_No6,
                     HidNpadIdType_No7, HidNpadIdType_No8, HidNpadIdType_Handheld = 0x20 };
enum : u32 { HidNpadStyleTag_NpadFullKey = 1, HidNpadStyleTag_NpadHandheld = 2, HidNpadStyleTag_NpadJoyDual = 4, HidNpadStyleTag_NpadJoyLeft = 8, HidNpadStyleTag_NpadJoyRight = 16 };
struct HidPowerInfo { bool is_powered, is_charging; u8 reserved[6]; u32 battery_level; };
static inline u32 hidGetNpadStyleSet(HidNpadIdType id) { return id == HidNpadIdType_No1 ? HidNpadStyleTag_NpadJoyDual : (id == HidNpadIdType_No2 ? HidNpadStyleTag_NpadFullKey : 0); }
static inline void hidGetNpadPowerInfoSplit(HidNpadIdType, HidPowerInfo *l, HidPowerInfo *r) { *l = {}; *r = {}; l->battery_level = 3; r->battery_level = 1; }
static inline void hidGetNpadPowerInfoSingle(HidNpadIdType, HidPowerInfo *p) { *p = {}; p->battery_level = 4; p->is_charging = true; }
static inline Result svcGetSystemInfo(u64 *out, u64 id0, Handle, u64) { *out = id0 == 0 ? 3422552064ull : 3113851290ull; return 0; }
enum AppletOperationMode { AppletOperationMode_Handheld = 0, AppletOperationMode_Console = 1 };
static inline AppletOperationMode appletGetOperationMode() { return AppletOperationMode_Handheld; }
