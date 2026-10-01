// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/Tc002Hotspot.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace stipple {
namespace platform {
namespace tc002 {
namespace {

constexpr const char* kInterface = "wlan0";

/// The Wi-Fi driver, in load order: fdrv needs bsp, and the vendor's own HAL
/// records the pair the same way round.
///
/// Read off `/lib/libzknet.so`, which is the network HAL the stock
/// application drives and which carries the strings "start to insmod %s",
/// "aic8800_bsp#aic8800_fdrv" and "aic_load_fw#aic8800_fdrv". Nothing in
/// init.rc or ssd_init.sh touches them, and `/lib/modules/<release>/` holds
/// no modules.dep or modules.alias - so the kernel cannot autoload them
/// either. The application really is the only thing that loads Wi-Fi on this
/// device, and replacing the application means inheriting the job.
constexpr const char* kWifiModules[] = {"aic8800_bsp.ko", "aic8800_fdrv.ko"};

bool interfaceExists(const char* name) {
    std::string path = "/sys/class/net/";
    path += name;
    struct stat info;
    return ::stat(path.c_str(), &info) == 0;
}

/// Where init's own service line says the supplicant's configuration lives.
///
///     service wpa_supplicant /bin/wpa_supplicant -iwlan0 -Dnl80211
///         -c/data/misc/wifi/wpa_supplicant.conf -C/dev/socket/ -e...
///
/// Read off the running device. The `-c` path is the whole reason this file
/// matters: it is not a default the daemon can do without.
constexpr const char* kSupplicantDir = "/data/misc/wifi";
constexpr const char* kSupplicantConf = "/data/misc/wifi/wpa_supplicant.conf";

/// The owning uid/gid of the vendor's own `/data/misc/wifi`, reused so the
/// file this creates looks like the one it replaces.
constexpr uid_t kWifiUid = 1010;

/// Write the supplicant's configuration if, and only if, it is absent.
///
/// With no such file `wpa_supplicant` exits immediately. `ctl.start` still
/// succeeds, so nothing reports an error - but no process ever binds
/// /dev/socket/wlan0, every join sits out its timeout waiting for a service
/// that already gave up, and `canScan()` is false because it means "can the
/// control socket be opened".
///
/// **`/data` is wiped by the recovery button by design, and this file goes
/// with it.** Found on hardware: a device that had been through a factory
/// reset could scan nothing and join nothing, reporting "the Wi-Fi service
/// did not come back" - a radio timeout, for a missing text file. Restoring
/// 43 bytes fixed it outright.
///
/// This is the same lesson as `ensureStation()` one layer down. Replacing the
/// vendor application means inheriting the jobs it used to do, and it both
/// started the supplicant *and* arrived with a `/data` that already held this.
///
/// Only ever creates. An existing file holds the user's own networks and is
/// never read, rewritten or truncated here.
void ensureSupplicantConfig() {
    struct stat info;
    if (::stat(kSupplicantConf, &info) == 0) {
        return;
    }

    // The directory goes in a wipe too. mkdir over an existing one fails
    // harmlessly with EEXIST, which is why the result is not checked.
    ::mkdir("/data/misc", 0771);
    ::mkdir(kSupplicantDir, 0770);

    // O_EXCL so two callers racing cannot have one truncate the other's work.
    const int fd = ::open(kSupplicantConf, O_WRONLY | O_CREAT | O_EXCL, 0660);
    if (fd < 0) {
        return;
    }

    // ctrl_interface is also given on the command line as -C/dev/socket/;
    // stating it here keeps the file valid on its own. update_config=1 is the
    // half that matters, because SAVE_CONFIG is how a joined network survives
    // a reboot and the daemon refuses to write a config that did not ask.
    static const char kDefaults[] = "ctrl_interface=/dev/socket\nupdate_config=1\n";
    const ssize_t wrote = ::write(fd, kDefaults, sizeof(kDefaults) - 1);
    ::fchown(fd, kWifiUid, kWifiUid);
    ::fchmod(fd, 0660);
    ::close(fd);

    // A partial write would leave a file that parses to something other than
    // what was meant, and the next boot would skip it because it exists.
    if (wrote != static_cast<ssize_t>(sizeof(kDefaults) - 1)) {
        ::unlink(kSupplicantConf);
    }
}

constexpr const char* kHostapdConf = "/tmp/stipple-hostapd.conf";
constexpr const char* kDnsmasqConf = "/tmp/stipple-dnsmasq.conf";

/// Open, not secured, and that is a decision rather than an oversight.
///
/// The hotspot exists so somebody can reach a device that has no network. A
/// password on it would have to be one they already know, which means printed
/// on the device or fixed in the firmware - and a fixed password shared by
/// every STIPPLE in the world is worse than none, because it looks like
/// security. It runs for ten minutes, serves one configuration page, and the
/// worst it can leak is the list of networks already broadcasting their names.
constexpr const char* kHostapdTemplate =
    "interface=wlan0\n"
    "driver=nl80211\n"
    "ssid=%s\n"
    "channel=6\n"
    "hw_mode=g\n"
    "ieee80211n=1\n"
    "ignore_broadcast_ssid=0\n";

/// Both path overrides below are the whole reason the first live test failed.
///
/// hostapd came up and the access point was visible; nothing could get an
/// address, because dnsmasq was never running. **This device has no /var** -
/// no /var/lib/misc, no /var/run, no /var at all - and dnsmasq refuses to
/// start when it cannot create either its lease file or its pid file, both
/// of which default to somewhere underneath it:
///
///   dnsmasq: cannot open or create lease file
///            /var/lib/misc/dnsmasq.leases: No such file or directory
///   dnsmasq: failed to open pidfile /var/run/dnsmasq.pid: No such file
///
/// Two separate failures, and fixing only the first gets you the second.
/// With the lease file in /tmp and the pid file disabled it starts clean and
/// stays up - checked on the device rather than reasoned about.
constexpr const char* kDnsmasqTemplate =
    "interface=wlan0\n"
    "bind-interfaces\n"
    "dhcp-range=192.168.4.10,192.168.4.60,255.255.255.0,12h\n"
    "dhcp-option=3,192.168.4.1\n"
    "dhcp-option=6,192.168.4.1\n"
    "dhcp-leasefile=/tmp/stipple-dnsmasq.leases\n"
    "pid-file=\n"
    // No upstream. This serves addresses so a phone will connect and stay
    // connected; it is not a route to the internet and should not pretend to
    // be one.
    "no-resolv\n"
    "log-facility=/tmp/stipple-dnsmasq.log\n";

}  // namespace

Tc002Hotspot::~Tc002Hotspot() { stop(); }

/// Where a post-mortem can still read it.
///
/// /tmp is tmpfs, and the only way out of a hotspot that has gone wrong is a
/// power cycle - which takes /tmp with it. Two live tests were recovered that
/// way and both left nothing whatsoever to read: the dnsmasq log, the
/// generated configs and every trace of which step failed were gone before
/// the device came back.
///
/// So this one file goes on flash. It is the documented exception to "avoid
/// flash writes": a handful of lines, written only while hosting, and the
/// alternative is diagnosing the same failure twice.
constexpr const char* kJournal = "/data/stipple/hotspot.log";

void Tc002Hotspot::note(const std::string& text) {
    event_ = text;

    FILE* journal = std::fopen(kJournal, "a");
    if (journal == nullptr) {
        return;  // best effort; a missing journal must never stop a revert
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    std::fprintf(journal, "[%lu] %s\n", static_cast<unsigned long>(now.tv_sec),
                 text.c_str());
    std::fclose(journal);
}

std::string Tc002Hotspot::takeEvent() {
    std::string taken;
    taken.swap(event_);
    return taken;
}

bool Tc002Hotspot::reapDead() {
    bool died = false;
    if (hostapdPid_ > 0 && ::waitpid(hostapdPid_, nullptr, WNOHANG) == hostapdPid_) {
        hostapdPid_ = -1;
        died = true;
        note("hotspot: hostapd exited");
    }
    if (dnsmasqPid_ > 0 && ::waitpid(dnsmasqPid_, nullptr, WNOHANG) == dnsmasqPid_) {
        dnsmasqPid_ = -1;
        died = true;
        // The exact failure the first live test hit, and the reason it was
        // invisible: hostapd kept running, so the access point looked fine
        // to anyone standing in front of it.
        note("hotspot: dnsmasq exited, no addresses being served");
    }
    return died;
}

bool Tc002Hotspot::writeFile(const char* path, const std::string& contents) const {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return false;
    }
    const ssize_t wrote = ::write(fd, contents.data(), contents.size());
    ::close(fd);
    return wrote == static_cast<ssize_t>(contents.size());
}

int Tc002Hotspot::run(const char* const argv[]) const {
    const pid_t pid = ::fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // The child inherits nothing useful and should say nothing: a daemon
        // writing to the panel process's stdout would end up in the log the
        // web UI shows.
        const int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) {
            ::dup2(null, STDOUT_FILENO);
            ::dup2(null, STDERR_FILENO);
            if (null > STDERR_FILENO) {
                ::close(null);
            }
        }
        ::execv(argv[0], const_cast<char* const*>(argv));
        ::_exit(127);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int Tc002Hotspot::spawn(const char* const argv[], const char* logPath) const {
    const pid_t pid = ::fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // Kept, not discarded.
        //
        // These went to /dev/null, and hostapd was the one component in the
        // whole path that had never said a word - so a run where it stayed
        // alive for two and a half minutes and broadcast nothing visible
        // left nothing at all to read. A daemon whose output is thrown away
        // is a daemon that can only be guessed at.
        const int log = ::open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const int sink = log >= 0 ? log : ::open("/dev/null", O_RDWR);
        if (sink >= 0) {
            ::dup2(sink, STDOUT_FILENO);
            ::dup2(sink, STDERR_FILENO);
            if (sink > STDERR_FILENO) {
                ::close(sink);
            }
        }
        ::execv(argv[0], const_cast<char* const*>(argv));
        ::_exit(127);
    }
    return static_cast<int>(pid);
}

bool Tc002Hotspot::start(const std::string& ssid, std::uint64_t nowMillis) {
    if (running_) {
        return true;
    }

    std::string hostapd;
    hostapd.resize(512);
    const int written = std::snprintf(&hostapd[0], hostapd.size(), kHostapdTemplate, ssid.c_str());
    if (written <= 0) {
        return false;
    }
    hostapd.resize(static_cast<std::size_t>(written));

    if (!writeFile(kHostapdConf, hostapd) || !writeFile(kDnsmasqConf, kDnsmasqTemplate)) {
        return false;
    }

    // Nothing to renew while there is no station, and a client still asking
    // would be broadcasting into an interface that is about to change job.
    if (dhcp_ != nullptr) {
        dhcp_->end();
    }

    // The station has to go first. One radio cannot do both, and hostapd will
    // simply fail to take an interface wpa_supplicant is holding.
    const char* const stopSupplicant[] = {"/bin/setprop", "ctl.stop", "wpa_supplicant", nullptr};
    run(stopSupplicant);

    // The access point needs the interface as much as the station does, and
    // on a cold boot nothing has loaded it.
    ensureRadio();

    // /sbin/ifconfig, which is a busybox symlink - there is no /bin/ifconfig.
    const char* const address[] = {"/sbin/ifconfig", kInterface, kAddress,
                                   "netmask", "255.255.255.0", "up", nullptr};
    if (run(address) != 0) {
        stop();
        return false;
    }

    const char* const startHostapd[] = {"/bin/hostapd", kHostapdConf, nullptr};
    hostapdPid_ = spawn(startHostapd, "/tmp/stipple-hostapd.log");
    if (hostapdPid_ < 0) {
        note("hotspot: hostapd would not start");
        stop();
        return false;
    }

    const char* const startDnsmasq[] = {"/bin/dnsmasq", "--keep-in-foreground",
                                        "--conf-file=/tmp/stipple-dnsmasq.conf", nullptr};
    dnsmasqPid_ = spawn(startDnsmasq, "/tmp/stipple-dnsmasq-stderr.log");
    if (dnsmasqPid_ < 0) {
        note("hotspot: dnsmasq would not start");
        stop();
        return false;
    }

    running_ = true;
    ssid_ = ssid;
    startedAtMillis_ = nowMillis;

    // "started" rather than "serving". Both daemons being alive says they
    // were spawned, not that anything is on the air - a run where hostapd
    // stayed up for two and a half minutes without broadcasting anything
    // visible is exactly why that distinction is now in the wording.
    note("hotspot: started " + ssid + " on " + std::string(kAddress));
    return true;
}

void Tc002Hotspot::ensureRadio() {
    // Cheap and first: on a device where something else already loaded the
    // driver - the stock application during development, or a previous call -
    // there is nothing to do and insmod would only log a failure.
    if (interfaceExists(kInterface)) {
        return;
    }

    // Built from the running kernel rather than hard-coded. 4.9.84 is what
    // this unit reports, and a hard-coded path would fail silently and
    // confusingly on any unit that reports something else.
    struct utsname release;
    std::string directory = "/lib/modules/";
    directory += (::uname(&release) == 0) ? release.release : "4.9.84";
    directory += "/";

    for (const char* const module : kWifiModules) {
        const std::string path = directory + module;
        const char* const argv[] = {"/sbin/insmod", path.c_str(), nullptr};
        run(argv);
    }

    note(interfaceExists(kInterface) ? "wifi: driver loaded"
                                     : "wifi: driver would not load, no wlan0");
}

void Tc002Hotspot::ensureStation() {
    // The interface has to exist before anything can be asked of it.
    ensureRadio();

    // Up, separately from having an address. The DHCP client needs a live
    // interface to broadcast from and gets its address later.
    const char* const up[] = {"/sbin/ifconfig", kInterface, "up", nullptr};
    run(up);

    // Before the start, not after: a supplicant launched without its config
    // exits before `ctl.start` has returned, and nothing would say so.
    ensureSupplicantConfig();

    // The same property service stop() uses to hand the radio back. Here it
    // is not handing anything back - there was never a station to begin with.
    const char* const startSupplicant[] = {"/bin/setprop", "ctl.start", "wpa_supplicant", nullptr};
    run(startSupplicant);
}

void Tc002Hotspot::stop() {
    if (hostapdPid_ > 0) {
        ::kill(hostapdPid_, SIGTERM);
        ::waitpid(hostapdPid_, nullptr, 0);
        hostapdPid_ = -1;
    }
    if (dnsmasqPid_ > 0) {
        ::kill(dnsmasqPid_, SIGTERM);
        ::waitpid(dnsmasqPid_, nullptr, 0);
        dnsmasqPid_ = -1;
    }

    // The station gets the radio back whether or not this was running. stop()
    // is also the failure path out of a half-started start(), and the one
    // state that must never be left behind is "no access point and no
    // station" - that is the device nobody can reach.
    //
    // Which is exactly why the config is checked here too: this path runs on
    // every join, and a supplicant that cannot start turns "hand the radio
    // back" into "no access point and no station".
    ensureSupplicantConfig();
    const char* const startSupplicant[] = {"/bin/setprop", "ctl.start", "wpa_supplicant", nullptr};
    run(startSupplicant);

    // Take 192.168.4.1 off first. The client starts by asking to keep
    // whatever the interface already has, and left alone it would ask a real
    // router to hand out the hotspot's own address - which a live run did,
    // and got away with only because the server ignored it.
    const char* const clear[] = {"/sbin/ifconfig", kInterface, "0.0.0.0", nullptr};
    run(clear);

    // And an address, which is the half that was missing. wpa_supplicant
    // associates and stops there; on this device nothing else asks for an
    // address, so a revert without this leaves a station nobody can reach -
    // indistinguishable, from the outside, from the device being dead.
    if (dhcp_ != nullptr) {
        dhcp_->restart();
    }

    running_ = false;
    ssid_.clear();
    startedAtMillis_ = 0;
    if (event_.empty()) {
        note("hotspot: stopped, station restored");
    }
}

bool Tc002Hotspot::tick(std::uint64_t nowMillis) {
    if (!running_) {
        return false;
    }

    // An access point handing out no addresses is worse than no access
    // point: somebody connects to it, waits, and concludes the clock is
    // broken. So a dead daemon reverts rather than limping on.
    if (reapDead()) {
        stop();
        return true;
    }
    // A clock stepping backwards must not extend this forever.
    if (nowMillis < startedAtMillis_) {
        startedAtMillis_ = nowMillis;
        return false;
    }
    if (nowMillis - startedAtMillis_ < revertMillis_) {
        return false;
    }

    // Given up on. A device that has been hosting for ten minutes with nobody
    // connected has not been provisioned, it has been forgotten - and the
    // network it could not join may well be back.
    stop();
    return true;
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
