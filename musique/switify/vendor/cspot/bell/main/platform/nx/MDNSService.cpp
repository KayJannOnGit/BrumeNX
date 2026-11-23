// Nintendo Switch (libnx) implementation of bell::MDNSService.
// Always uses the vendored mdnssvc responder (no avahi on this platform).
// The host IP is obtained from libnx's gethostid(), which returns the
// console's current IP address in network byte order (same source ftpd uses).

#include <arpa/inet.h>
#include <switch.h>
#include <sys/socket.h>
#include <cstdio>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "BellLogger.h"
#include "MDNSService.h"
#include "mdnssvc.h"

using namespace bell;

class implMDNSService : public MDNSService {
 private:
  struct mdns_service* service;

 public:
  static struct mdnsd* mdnsServer;
  static std::atomic<size_t> instances;

  implMDNSService(struct mdns_service* service) : service(service) {
    instances++;
  }
  void unregisterService();
};

struct mdnsd* implMDNSService::mdnsServer = NULL;
std::atomic<size_t> implMDNSService::instances = 0;
static std::mutex registerMutex;

// Brume (2026-11-24): the console's address, found the reliable way. libnx's gethostid() returns
// INADDR_LOOPBACK in HOST byte order (0x7F000001) when nifm fails - the check below only knew the
// network-order form, so a background program whose nifm:u call failed went on with 1.0.0.127 and
// the multicast setup failed silently. Order: nifm:s (made for system programs), nifm:u, then a UDP
// socket "connected" to a public address (nothing is sent) whose local address is the interface's.
static bool usableAddress(uint32_t a) {
  return a != 0 && a != INADDR_NONE && a != htonl(INADDR_LOOPBACK) &&
         a != INADDR_LOOPBACK && (ntohl(a) >> 24) != 127;
}

static uint32_t consoleAddress(const char** how) {
  static const NifmServiceType kTypes[2] = {NifmServiceType_System,
                                            NifmServiceType_User};
  for (int i = 0; i < 2; i++) {
    u32 id = 0;
    if (R_SUCCEEDED(nifmInitialize(kTypes[i]))) {
      const Result rc = nifmGetCurrentIpAddress(&id);
      nifmExit();
      if (R_SUCCEEDED(rc) && usableAddress(id)) {
        *how = i == 0 ? "nifm:s" : "nifm:u";
        return id;
      }
    }
  }
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s >= 0) {
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = inet_addr("1.1.1.1");
    struct sockaddr_in me;
    socklen_t len = sizeof(me);
    memset(&me, 0, sizeof(me));
    const bool ok = connect(s, (struct sockaddr*)&to, sizeof(to)) == 0 &&
                    getsockname(s, (struct sockaddr*)&me, &len) == 0;
    close(s);
    if (ok && usableAddress(me.sin_addr.s_addr)) {
      *how = "udp-getsockname";
      return me.sin_addr.s_addr;
    }
  }
  *how = "none";
  return 0;
}

// What the menu reads to know whether it must announce BrumeNX itself (docs/SPOTIFY.md)
static void writeState(const char* line) {
  FILE* f = fopen("sdmc:/brume/spotify/mdns-etat.txt", "w");
  if (!f) return;
  fprintf(f, "%s\n", line);
  fclose(f);
}

static void hookLine(const char* line) { BELL_LOG(info, "MDNS", "%s", line); }

void implMDNSService::unregisterService() {
  mdns_service_remove(implMDNSService::mdnsServer, service);
  if (!--instances && implMDNSService::mdnsServer) {
    mdnsd_stop(implMDNSService::mdnsServer);
    implMDNSService::mdnsServer = nullptr;
  }
}

std::unique_ptr<MDNSService> MDNSService::registerService(
    const std::string& serviceName, const std::string& serviceType,
    const std::string& serviceProto, const std::string& serviceHost,
    int servicePort, const std::map<std::string, std::string> txtData) {
  std::lock_guard lock(registerMutex);

  if (!implMDNSService::mdnsServer) {
    mdnsd_log_hook = hookLine;
    struct in_addr addr;
    const char* how = "";
    addr.s_addr = consoleAddress(&how);
    // Brume v101: started with the menu, the service often runs before the Wi-Fi is connected (console, 2026-11-23:
    // « console address 0.0.0.0 », then no mDNS at all). It now waits for the address: every 2 s, up to 3 minutes.
    if (!addr.s_addr) {
      BELL_LOG(info, "MDNS", "mdns: no address yet, waiting for the Wi-Fi (3 min at most)");
      writeState("attente adresse");
      for (int i = 0; i < 90 && !addr.s_addr; i++) {
        svcSleepThread(2000000000ULL);
        addr.s_addr = consoleAddress(&how);
      }
    }
    char ip[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &addr, ip, sizeof(ip));
    BELL_LOG(info, "MDNS", "mdns: console address %s (%s)", ip, how);
    if (!addr.s_addr) {
      BELL_LOG(error, "MDNS", "no network connection, cannot start mDNS");
      writeState("echec adresse");
      return nullptr;
    }

    char hostname[64] = "brumenx";

    implMDNSService::mdnsServer = mdnsd_start(addr, false);
    if (implMDNSService::mdnsServer) {
      mdnsd_set_hostname(implMDNSService::mdnsServer, hostname, addr);
      BELL_LOG(info, "MDNS", "mdns: responder listening on UDP 5353");
    }
  }

  if (implMDNSService::mdnsServer) {
    std::vector<const char*> txt;
    std::vector<std::unique_ptr<std::string>> txtStr;

    for (auto& [key, value] : txtData) {
      auto str = std::make_unique<std::string>(key + "=" + value);
      txtStr.push_back(std::move(str));
      txt.push_back(txtStr.back()->c_str());
    }

    txt.push_back(NULL);
    std::string type(serviceType + "." + serviceProto + ".local");

    BELL_LOG(info, "MDNS", "using built-in mDNS for %s", serviceName.c_str());
    auto service =
        mdnsd_register_svc(implMDNSService::mdnsServer, serviceName.c_str(),
                           type.c_str(), servicePort, NULL, txt.data());

    if (service) {
      char line[96];
      snprintf(line, sizeof(line), "ok %s", serviceName.c_str());
      writeState(line);
      BELL_LOG(info, "MDNS", "mdns: %s announced", serviceName.c_str());
      return std::make_unique<implMDNSService>(service);
    }
  }

  writeState("echec responder");
  BELL_LOG(error, "MDNS", "cannot start mDNS listener for %s",
           serviceName.c_str());
  return nullptr;
}
