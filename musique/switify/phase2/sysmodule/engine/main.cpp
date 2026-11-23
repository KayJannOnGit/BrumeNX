#include "background_backend.hpp"
#include "ipc_self_test.hpp"
#include "switify_service.hpp"

#include "NXConsoleLog.h"

#include <curl/curl.h>
#include <switch.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <memory>
#include <new>

namespace {

constexpr SocketInitConfig SocketConfig = {
    .tcp_tx_buf_size = 0x800,
    .tcp_rx_buf_size = 0x1000,
    .tcp_tx_buf_max_size = 0x2EE0,
    .tcp_rx_buf_max_size = 0x4000,
    // Spotify AP, Web API, and CDN traffic are TCP-only. Reserving UDP
    // buffers and six bsd:u sessions bought us nothing, but the sessions stay
    // claimed for the entire sysmodule lifetime. Nintendo's online emulators
    // need the same service while launching. Match Streamfin's proven
    // background profile: two BSD sessions and no UDP allocation.
    .udp_tx_buf_size = 0,
    .udp_rx_buf_size = 0,
    .sb_efficiency = 2,
    .num_bsd_sessions = 2,
    .bsd_service_type = BsdServiceType_User,
};

// Brume (2026-11-23): the service pairs with the phone itself when no account is
// saved yet. That needs Spotify Connect discovery: an mDNS responder (UDP
// multicast, its own blocking thread) and the small zeroconf HTTP server
// (another blocking thread), so this profile adds UDP buffers and two BSD
// sessions. Once credentials exist, every later start uses the light TCP-only
// profile above.
constexpr SocketInitConfig PairingSocketConfig = {
    .tcp_tx_buf_size = 0x800,
    .tcp_rx_buf_size = 0x1000,
    .tcp_tx_buf_max_size = 0x2EE0,
    .tcp_rx_buf_max_size = 0x4000,
    .udp_tx_buf_size = 0x2400,
    .udp_rx_buf_size = 0x2400,
    .sb_efficiency = 2,
    .num_bsd_sessions = 4,
    .bsd_service_type = BsdServiceType_User,
};
constexpr const char* CredentialsPath = "sdmc:/brume/spotify/credentials.json";
// An account already paired with Switify (same engine) is reused once: copied,
// never moved or changed (Switify keeps working).
constexpr const char* SwitifyCredentialsPath =
    "sdmc:/config/Switify/credentials.json";
bool pairingProfile = false;

Result socketInitResult = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

void abortUnless(Result result) {
  if (R_FAILED(result)) diagAbortWithResult(result);
}

bool fileExists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

// Copies a small file (credentials are well under 4 KiB; anything bigger than
// 16 KiB is not a credentials file and is left alone). Written to a temporary
// name, then renamed: a cut never leaves half a file.
void importSwitifyCredentials() {
  if (fileExists(CredentialsPath) || !fileExists(SwitifyCredentialsPath))
    return;
  FILE* in = std::fopen(SwitifyCredentialsPath, "rb");
  if (!in) return;
  static char buffer[16 * 1024];
  const size_t size = std::fread(buffer, 1, sizeof(buffer), in);
  const bool whole = std::feof(in) != 0;
  std::fclose(in);
  if (!whole || size == 0 || buffer[0] != '{') return;
  constexpr const char* Temporary = "sdmc:/brume/spotify/credentials.json.tmp";
  FILE* out = std::fopen(Temporary, "wb");
  if (!out) return;
  const bool written = std::fwrite(buffer, 1, size, out) == size;
  std::fclose(out);
  if (!written || std::rename(Temporary, CredentialsPath) != 0)
    std::remove(Temporary);
}

}  // namespace

extern "C" {

uint32_t __nx_applet_type = AppletType_None;
uint32_t __nx_fs_num_sessions = 2;

void __libnx_initheap(void) {
  // The artwork-enabled service performs authenticated browse requests whose
  // transient JSON tree can overlap the player, artwork URL, and browse page.
  // Three MiB was sufficient for playback alone but could exhaust the process
  // as soon as Home, Library, Search, or a playlist was opened. Keep the Lite
  // service at 3 MiB and restore the hardware-proven 4 MiB ceiling here.
#ifdef SWITIFY_LIGHTWEIGHT_OVERLAY
  static char heap[3 * 1024 * 1024];
#else
  static char heap[4 * 1024 * 1024];
#endif
  extern char* fake_heap_start;
  extern char* fake_heap_end;
  fake_heap_start = heap;
  fake_heap_end = heap + sizeof(heap);
}

void __appInit(void) {
  abortUnless(smInitialize());
  abortUnless(fsInitialize());
  abortUnless(fsdevMountSdmc());
  // Brume: our own folder, an account paired with Switify reused, then the
  // network profile: pairing (no account yet) or the light TCP-only one
  mkdir("sdmc:/brume", 0755);
  mkdir("sdmc:/brume/spotify", 0755);
  importSwitifyCredentials();
  pairingProfile = !fileExists(CredentialsPath);
  socketInitResult =
      socketInitialize(pairingProfile ? &PairingSocketConfig : &SocketConfig);
}

void __appExit(void) {
  curl_global_cleanup();
  if (R_SUCCEEDED(socketInitResult)) socketExit();
  fsdevUnmountDevice("sdmc");
  fsExit();
  smExit();
}

}  // extern "C"

int main(int, char**) {
  mkdir("/brume", 0755);
  mkdir("/brume/spotify", 0755);
  nxlog::installBellLogger();
  nxlog::setLogFile("/brume/spotify/background.log", 512 * 1024);
  nxlog::setQueueEnabled(false);
#ifdef SWITIFY_LIGHTWEIGHT_OVERLAY
  nxlog::push(
      "Brume Musique (Switify engine 0.5.0-lite1) starting");
#else
  nxlog::push("Brume Musique (Switify engine 0.5.0) starting");
#endif
#ifdef SWITIFY_LIGHTWEIGHT_OVERLAY
  nxlog::push("Optimized profile: 3072 KiB heap, Vorbis-only decoder");
#else
  nxlog::push("Resilient profile: 4096 KiB heap, Vorbis-only decoder");
#endif
  // Brume: what socketInitialize (in __appInit, before main) was really given, and its answer
  nxlog::push("Compatibility profile: Applet pool, lazy audout");
  {
    const SocketInitConfig& c = pairingProfile ? PairingSocketConfig : SocketConfig;
    nxlog::pushf("Network profile: %s - %u BSD sessions, UDP tx 0x%X rx 0x%X, "
                 "socketInitialize 0x%X",
                 pairingProfile ? "pairing (mDNS + zeroconf)" : "TCP-only",
                 c.num_bsd_sessions, c.udp_tx_buf_size, c.udp_rx_buf_size,
                 socketInitResult);
  }
  nxlog::pushHeapUsage("startup");

  // ServiceCore owns the fixed-capacity browsing snapshot and is far larger
  // than the 16 KiB main-thread stack. Keep all long-lived engine state on the
  // process heap so adding browse fields cannot silently overflow that stack.
  auto core = std::unique_ptr<switify::phase2::ServiceCore>(
      new (std::nothrow) switify::phase2::ServiceCore());
  if (!core)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
  nxlog::pushHeapUsage("service core ready");
  abortUnless(switify::phase2::switch_service::initialize(*core));

  auto backend = std::unique_ptr<switify::phase2::BackgroundBackend>(
      new (std::nothrow) switify::phase2::BackgroundBackend(*core));
  if (!backend)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
  bool backendStarted = false;
  if (R_SUCCEEDED(socketInitResult)) {
    backendStarted = backend->start();
    if (!backendStarted) diagAbortWithResult(MAKERESULT(421, 10));
    nxlog::pushHeapUsage("Spotify backend started");
  } else {
    char detail[40];
    std::snprintf(detail, sizeof(detail), "Network unavailable: 0x%08X",
                  socketInitResult);
    core->configureCommands(false, SwitifyIpcCapability_Status, 0);
    core->publishServiceState(SwitifyServiceState_Error, 0);
    core->publishTrack({}, {}, detail, 0);
    nxlog::pushf("Socket initialization failed: 0x%08X", socketInitResult);
  }
  abortUnless(switify::phase2::ipc_self_test::start());

  switify::phase2::switch_service::loop();

  if (backendStarted) backend->stop();
  abortUnless(switify::phase2::switch_service::exit());
  return 0;
}
