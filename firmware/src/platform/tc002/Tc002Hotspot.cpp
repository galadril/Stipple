// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/Tc002Hotspot.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
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

/// A fresh passphrase for the setup network.
///
/// **The access point cannot be open.** Measured on hardware: with no `wpa`
/// lines hostapd is refused at `nl80211: Could not configure driver mode`,
/// and the identical config plus `wpa=2`, `rsn_pairwise=CCMP` and a
/// passphrase reports `AP-ENABLED`. Same interface, same driver, same
/// binary - the driver simply will not host an open BSS. See
/// docs/research/tc002-platform-findings.md.
///
/// So the original intent - an open network, because a password shared by
/// every device in the world only looks like security - is not available.
/// The replacement is better than either: **random per session, and shown on
/// the panel.** Nobody can guess it, nothing has to be printed on the case,
/// and the only person who can read it is holding the clock.
///
/// Characters avoid 0/O and 1/l/I, because this is read off a 52-pixel panel
/// and typed into a phone.
/// The setup network's passphrase, fixed and published.
///
/// **It wants to be open, and it cannot be.** Blueprint and ADR specified an
/// open setup network, reasoning that a password shared by every device only
/// looks like security. That option does not exist on this hardware: with no
/// `wpa` lines hostapd is refused at "nl80211: Could not configure driver
/// mode", and the identical configuration plus WPA2 reports AP-ENABLED.
/// Measured both ways round on a device. See
/// docs/research/tc002-platform-findings.md.
///
/// So WPA2 is forced, and the question becomes which passphrase. A random
/// per-device one was tried first and is worse than it sounds: it has to be
/// read off a 52-pixel panel while somebody is also hunting for the network
/// on a phone, and missing it locks them out of their own clock. That
/// happened on the first real use.
///
/// A fixed, documented passphrase is the closest honest equivalent of the
/// open network that was intended. It is on the panel, in the docs and in
/// this source, so it is not a secret and is not pretending to be one. What
/// it protects is nothing: an access point that exists for ten minutes so
/// somebody can type their own Wi-Fi details into their own device.
///
/// Lowercase and digits only - it gets typed into a phone keyboard from a
/// scrolling 52-pixel line.
constexpr const char* kSetupPassphrase = "stipple1234";

/// Tell the vendor's own soft AP to stand down.
///
/// **The vendor network stack runs inside this process.** The startup shim
/// lists libzkgui.so as a DT_NEEDED dependency so a STIPPLE that will not
/// load still leaves a working clock - and that drags the whole vendor
/// application in, libzknet.so with it. Confirmed by reading
/// /proc/<pid>/maps on a flashed device.
///
/// Its soft-AP manager configures wlan0 with 192.168.100.1 - the address
/// `soft_ap_get_ip` returns - and brings the interface up. That explained two
/// things that looked unrelated: a panel showing an IP for a network that did
/// not exist, and hostapd refusing the interface because it was already live.
///
/// Clearing the address is not enough, because its thread puts it back. The
/// owner has to be asked to let go, and it exports a plain C function:
///
///     int soft_ap_disable(void);
///
/// No dlopen needed - the library is already in the process, so RTLD_DEFAULT
/// finds it. Where it is absent the symbol is null and this does nothing,
/// which is correct on any platform without the vendor stack.
bool releaseVendorSoftAp() {
    using SoftApDisable = int (*)();
    void* symbol = ::dlsym(RTLD_DEFAULT, "soft_ap_disable");
    if (symbol == nullptr) {
        return false;
    }
    reinterpret_cast<SoftApDisable>(symbol)();
    return true;
}

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
    "ignore_broadcast_ssid=0\n"
    "ctrl_interface=/data/misc/wifi/hostapd\n"
    "wpa=2\n"
    "rsn_pairwise=CCMP\n"
    "wpa_passphrase=%s\n";

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

/// A failure, which is both noted and kept apart.
///
/// `note()` has one slot and `start()` can emit several lines in a single
/// pass - "starting", then "driver loaded", then why it failed - so by the
/// time the loop drains it, only the last one is left. The journal on flash
/// gets them all; the live event gets whichever was most recent.
///
/// That is fine for a log and wrong for the panel, which must show the
/// *failure* and not whatever happened to be noted last. The first attempt at
/// this filtered by prefix in the caller and put "wifi: driver loaded" on the
/// panel in alarm colours - a success message dressed as a fault.
/// Keep a failure where a *stock* device can read it.
///
/// **The one channel that survives everything.** A flashed STIPPLE with no
/// access point is reachable by nothing: no Wi-Fi, no ADB, no USB gadget, and
/// vold will not mount a stick for us. But `/mnt/storage` is mtd7, and it has
/// now been observed to survive a full res reflash *and* a /data wipe - and
/// the stock application can get itself onto a network, which means ADB.
///
/// So: STIPPLE writes here, the user holds reset to go back to stock, stock
/// joins their Wi-Fi, and the log is read over ADB. Round-about, and it is
/// the only route that does not depend on the thing that is broken.
///
/// Mounted read-only by the vendor, so it is remounted for the write and put
/// back - the same dance Tc002Recovery does, and for the same reason: vfat
/// with no journal on a device that can lose power at any moment.
void Tc002Hotspot::keepFailureForStock(const std::string& text) {
    static const char* const kStockReadableLog = "/mnt/storage/stipple-wifi.log";

    const char* const rw[] = {"/bin/mount", "-o", "remount,rw", "/mnt/storage", nullptr};
    const char* const ro[] = {"/bin/mount", "-o", "remount,ro", "/mnt/storage", nullptr};
    if (run(rw) != 0) {
        return;
    }

    // **Bounded, because this volume holds the recovery image.**
    //
    // Appending without a limit was the first version, and it is the kind of
    // thing blueprint §38 exists to forbid: /mnt/storage is 8576 KB and
    // already carries update.img and a spare, so a device that keeps failing
    // could eat the space its own way back lives in. One device reached
    // 42 KB over thirteen failures - slow, and in the wrong direction.
    //
    // Starting over at the ceiling keeps the most recent failures, which are
    // the ones anybody reads.
    constexpr long kCeilingBytes = 64L * 1024L;
    const char* mode = "a";
    struct stat existing;
    if (::stat(kStockReadableLog, &existing) == 0 && existing.st_size >= kCeilingBytes) {
        mode = "w";
    }

    FILE* out = std::fopen(kStockReadableLog, mode);
    if (out != nullptr) {
        std::fprintf(out, "%s\n", text.c_str());

        // hostapd's own output, whole and unabridged. The panel gets a short
        // sentence because somebody is reading it off a clock; this gets
        // every line, because this is where the answer actually is.
        FILE* log = std::fopen("/tmp/stipple-hostapd.log", "r");
        if (log != nullptr) {
            std::fprintf(out, "--- hostapd ---\n");
            char line[256];
            while (std::fgets(line, sizeof(line), log) != nullptr) {
                std::fputs(line, out);
            }
            std::fclose(log);
        }

        // And what the interface looked like at the time, which no journal
        // records and which decides most of the remaining explanations.
        FILE* pipe = ::popen("ifconfig -a 2>&1; cat /proc/net/dev 2>&1; lsmod 2>&1", "r");
        if (pipe != nullptr) {
            std::fprintf(out, "--- interfaces and modules ---\n");
            char line[256];
            std::size_t got = 0;
            while (std::fgets(line, sizeof(line), pipe) != nullptr && got < 8192u) {
                std::fputs(line, out);
                got += std::strlen(line);
            }
            ::pclose(pipe);
        }
        std::fprintf(out, "=== end ===\n");
        std::fclose(out);
    }

    ::sync();
    run(ro);
}

void Tc002Hotspot::noteFailure(const std::string& text) {
    note(text);
    failure_ = text;
    keepFailureForStock(text);
}

std::string Tc002Hotspot::takeFailure() {
    std::string taken;
    taken.swap(failure_);
    return taken;
}

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

/// Kill wpa_supplicant by pid, and confirm it is gone.
///
/// `setprop ctl.stop` asks init to do it, which is indirect twice over: the
/// request is asynchronous, and reading back `init.svc.wpa_supplicant`
/// depends on a getprop binary whose path was only ever a guess. After
/// several cycles of "hostapd could not configure driver mode" it is worth
/// removing the whole question rather than refining the wait.
///
/// /proc is the authority on what is running. Nothing here depends on init,
/// on a property, or on a tool existing.
///
/// Returns how many were killed, or -1 if /proc could not be read.
int Tc002Hotspot::killSupplicant() {
    DIR* proc = ::opendir("/proc");
    if (proc == nullptr) {
        return -1;
    }

    int killed = 0;
    struct dirent* entry = nullptr;
    while ((entry = ::readdir(proc)) != nullptr) {
        // Numeric directories only; everything else in /proc is not a process.
        const char* name = entry->d_name;
        bool numeric = name[0] != 0;
        for (const char* c = name; *c != 0; ++c) {
            if (*c < '0' || *c > '9') {
                numeric = false;
                break;
            }
        }
        if (!numeric) {
            continue;
        }

        std::string path = "/proc/";
        path += name;
        path += "/cmdline";
        FILE* cmdline = std::fopen(path.c_str(), "rb");
        if (cmdline == nullptr) {
            continue;
        }
        char buffer[256] = {0};
        const std::size_t got = std::fread(buffer, 1, sizeof(buffer) - 1, cmdline);
        std::fclose(cmdline);
        if (got == 0) {
            continue;
        }
        // cmdline is NUL-separated, so a plain strstr sees only argv[0] -
        // which is the whole of what we are looking for.
        if (std::strstr(buffer, "wpa_supplicant") == nullptr) {
            continue;
        }

        const int pid = std::atoi(name);
        if (pid > 1 && ::kill(static_cast<pid_t>(pid), SIGKILL) == 0) {
            ++killed;
        }
    }
    ::closedir(proc);
    return killed;
}

bool Tc002Hotspot::waitForSupplicantToStop() {
    // init's own view of the service, which is the only authoritative one.
    //
    // The control socket was tried first and is not trustworthy: a supplicant
    // killed rather than asked may leave /dev/socket/wlan0 behind as a stale
    // file, so its presence proves nothing. `init.svc.wpa_supplicant` is set
    // by whatever did the killing.
    //
    // **An unreadable answer is not a yes.** The first version of this
    // returned success when the pipe gave nothing back, on the reasoning that
    // a missing service cannot be holding anything - which quietly turned the
    // whole wait into a no-op on any device where /bin/getprop is not where
    // we guessed. Only /bin/setprop has ever been proven to exist here. So a
    // failed read falls through to the settle below rather than claiming the
    // interface is free.
    constexpr int kAttempts = 30;
    constexpr long kSleepNanos = 50L * 1000L * 1000L;  // 50 ms
    constexpr char kNul = 0;
    constexpr char kNewline = 10;

    bool couldAsk = false;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        FILE* pipe = ::popen("getprop init.svc.wpa_supplicant", "r");
        if (pipe == nullptr) {
            break;
        }
        char answer[32] = {0};
        const char* read = std::fgets(answer, sizeof(answer), pipe);
        ::pclose(pipe);
        if (read == nullptr) {
            break;  // Cannot ask. Settle instead of pretending to know.
        }
        couldAsk = true;
        if (std::strncmp(answer, "stopped", 7) == 0 || answer[0] == kNul ||
            answer[0] == kNewline) {
            return true;
        }
        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = kSleepNanos;
        ::nanosleep(&pause, nullptr);
    }

    if (!couldAsk) {
        // Blind, so wait long enough to cover it. Several times the observed
        // stop time, and it only happens on the path to hosting - which is
        // already seconds long and only reached when nothing else worked.
        struct timespec settle;
        settle.tv_sec = 1;
        settle.tv_nsec = 0;
        ::nanosleep(&settle, nullptr);
        note("hotspot: could not read init.svc.wpa_supplicant, waited instead");
        return true;
    }

    // Carrying on anyway: a hotspot that might work beats a certain refusal
    // on the one path somebody takes when they have no other way in.
    noteFailure("hotspot: the station would not let go of wlan0");
    return false;
}

/// The line in hostapd's log that explains the failure.
///
/// Not the last line. hostapd's exit path always prints
/// "hostapd_free_hapd_data: Interface wlan0 wasn't started", which is an
/// epilogue rather than a cause - and taking the last line put exactly that
/// on the panel, which cost a flash cycle to learn nothing.
///
/// The cause sits a few lines above and says what it could not do:
///
///     nl80211: Could not configure driver mode
///     nl80211 driver initialization failed.
///     hostapd_free_hapd_data: Interface wlan0 wasn't started
///
/// So the first line that reads like a diagnosis wins, and the last line is
/// only a fallback for when nothing does.
std::string Tc002Hotspot::lastHostapdLine() const {
    FILE* log = std::fopen("/tmp/stipple-hostapd.log", "r");
    if (log == nullptr) {
        return std::string();
    }

    // Phrases hostapd uses when it is telling you why, in the order they are
    // worth having. "wasn't started" is deliberately absent.
    static const char* kTelling[] = {"Could not configure", "driver initialization failed",
                                     "Could not", "not supported", "Failed to",
                                     "failed to", "nl80211"};

    char line[256];
    std::string best;
    std::string last;
    while (std::fgets(line, sizeof(line), log) != nullptr) {
        std::string text(line);
        while (!text.empty() && (text.back() == 10 || text.back() == 13)) {
            text.pop_back();
        }
        if (text.empty()) {
            continue;
        }
        last = text;
        if (text.find("wasn't started") != std::string::npos) {
            continue;  // The epilogue, every time.
        }
        if (!best.empty()) {
            continue;  // Already have a diagnosis; the first one is the cause.
        }
        for (const char* phrase : kTelling) {
            if (text.find(phrase) != std::string::npos) {
                best = text;
                break;
            }
        }
    }
    std::fclose(log);

    std::string chosen = best.empty() ? last : best;
    // Bounded: this scrolls across 52 pixels, and a hundred characters is
    // already a long read.
    if (chosen.size() > 100u) {
        chosen.resize(100u);
    }
    return chosen;
}

bool Tc002Hotspot::stillAlive(int pid) const {
    // Long enough for a daemon that cannot take its interface to have given
    // up - hostapd fails on the nl80211 call, not after any real work - and
    // short enough to sit inside a frame budget without being noticed.
    struct timespec pause;
    pause.tv_sec = 0;
    pause.tv_nsec = 250L * 1000L * 1000L;  // 250 ms
    ::nanosleep(&pause, nullptr);

    int status = 0;
    const pid_t reaped = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
    // 0 means it is still there and has not been reaped, which is the answer
    // we want. Its own pid means it exited and we have just collected it.
    return reaped == 0;
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
    // hostapd wants its control directory to exist, and a factory reset wipes
    // /data - which is exactly the state a device needing setup is in. The
    // configuration proven to work on this hardware names this path, so it is
    // used verbatim rather than something of ours under /tmp.
    ::mkdir("/data/misc", 0771);
    ::mkdir("/data/misc/wifi", 0770);
    ::mkdir("/data/misc/wifi/hostapd", 0770);

    if (running_) {
        return true;
    }

    // Logged before anything is tried. Without it, a journal with no hotspot
    // lines cannot be told apart from a device that was never asked to host -
    // and that was the exact ambiguity that made this hard to diagnose.
    note("hotspot: starting " + ssid);

    std::string hostapd;
    hostapd.resize(512);
    passphrase_ = kSetupPassphrase;
    const int written = std::snprintf(&hostapd[0], hostapd.size(), kHostapdTemplate, ssid.c_str(),
                                      passphrase_.c_str());
    if (written <= 0) {
        noteFailure("hotspot: could not build a hostapd configuration");
        return false;
    }
    hostapd.resize(static_cast<std::size_t>(written));

    if (!writeFile(kHostapdConf, hostapd) || !writeFile(kDnsmasqConf, kDnsmasqTemplate)) {
        // /tmp is tmpfs and this should not be possible, which is exactly why
        // it was not reported. Every return out of start() now says something:
        // an evening was spent on a hotspot that failed silently twice.
        noteFailure("hotspot: could not write its configuration to /tmp");
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

    // **And it has to have actually gone.** `setprop ctl.stop` asks init to
    // kill the service and returns immediately; the supplicant is still
    // holding wlan0 for some milliseconds after that. Taking the interface in
    // the same breath is a race.
    //
    // It is a race that could not be lost until recently, which is why it
    // survived this long: on a device with no `wpa_supplicant.conf` the
    // supplicant exited at once on every boot, so by the time anybody asked
    // for a hotspot there was nothing to stop and the interface was already
    // free. Writing that config when it is missing - correct, and necessary
    // after a factory reset wipes it - means the supplicant now really runs,
    // really holds wlan0, and really has to be waited for.
    //
    // Found on hardware, on the first install that had both changes in it: the
    // knob countdown appeared and no access point ever did.
    waitForSupplicantToStop();

    // **No SIGKILL here, deliberately.**
    //
    // This used to find wpa_supplicant by pid in /proc and kill -9 it, added
    // when the theory was that the station still held the radio. It is not
    // needed and it is actively suspect: a killed supplicant never runs its
    // nl80211 cleanup, so the interface can stay claimed by a process that
    // no longer exists - which presents as hostapd being unable to configure
    // driver mode, the very symptom it was added to cure.
    //
    // The sequence proven on hardware uses `ctl.stop` and a wait, nothing
    // more. Every speculative step added to this function has broken it:
    // first a driver reload, then this. The remedy is to do what was
    // measured and no more.

    ensureRadio();

    // The vendor's soft-AP manager owns wlan0 in this process, so it is asked
    // to let go before anything of ours touches the interface. Without this,
    // clearing and downing wlan0 is a race its thread wins.
    note(releaseVendorSoftAp() ? "wifi: vendor soft ap asked to stand down"
                               : "wifi: no vendor soft ap in this process");

    // **Clear the address first, then take the interface down - in that
    // order, and the order is the entire fix.**
    //
    // This was the other way round, and `ifconfig wlan0 0.0.0.0` *brings the
    // interface up*: assigning an address implies up. So the `down` was being
    // undone by the very next line, and hostapd was handed a live interface.
    //
    // Read off a real failing device, from the log this writes to
    // /mnt/storage for exactly this purpose:
    //
    //     nl80211: Could not configure driver mode
    //     ...
    //     wlan0  UP BROADCAST MULTICAST  MTU:1500
    //            RX packets:0  TX packets:0
    //
    // Up, not associated, and never having passed a packet - the signature of
    // a radio that had just been configured and then handed over while still
    // owned by the station. hostapd cannot change the mode of an interface
    // that is up, and "could not configure driver mode" is what it says
    // instead of anything clearer.
    //
    // Something on this platform also puts a foreign address on wlan0 - it
    // survives a /data wipe and a res reflash - so clearing is still wanted.
    // It just has to happen before the down, not after.
    const char* const clear[] = {"/sbin/ifconfig", kInterface, "0.0.0.0", nullptr};
    run(clear);

    // And now down, with nothing after it but hostapd.
    const char* const down[] = {"/sbin/ifconfig", kInterface, "down", nullptr};
    run(down);


    // **No driver reload here, deliberately.**
    //
    // An earlier version unloaded and reloaded aic8800_bsp/aic8800_fdrv on
    // the theory that the radio could not switch STA to AP on a live
    // interface. It cannot have been that: `lsmod` on stock shows the same
    // two modules with no extra parameters, stock never reloads them, and a
    // hand-run `hostapd -B` with a WPA2 config brought the access point up
    // on this device without touching them.
    //
    // The reload was removed rather than left in as insurance, because it is
    // a two-second window in which wlan0 does not exist and nothing good
    // comes of keeping a remedy for a cause that was ruled out.

    // **hostapd first, the address afterwards.**
    //
    // This had it the other way round: wlan0 was brought up with 192.168.4.1
    // and only then was hostapd started. That is backwards. hostapd is the
    // thing that takes the radio into AP mode, and handing it an interface
    // already up in station mode is what it refuses with "nl80211: Could not
    // configure driver mode".
    //
    // Known rather than reasoned: a hand-run `hostapd -B` on this device
    // brought the access point up with the interface left **down** and no
    // address assigned at all. That sequence worked; this one did not; and
    // the config was identical by then. Ordering was the last difference.
    //
    // So: interface down (above), hostapd takes it and sets the mode, and the
    // address goes on after - which is the ordinary way round for hostapd on
    // any platform, and was only ever inverted here by accident.
    const char* const startHostapd[] = {"/bin/hostapd", kHostapdConf, nullptr};
    hostapdPid_ = spawn(startHostapd, "/tmp/stipple-hostapd.log");
    if (hostapdPid_ < 0) {
        noteFailure("hotspot: hostapd would not start");
        stop();
        return false;
    }

    // Started is not running. `spawn` reports whether the *fork* worked, and
    // hostapd refusing the interface looks identical to hostapd serving
    // happily until it exits a moment later - at which point `serving()`
    // notices and the loop quietly reverts.
    //
    // That quiet is the real fault. A person who held the knob for five
    // seconds, watched the countdown, and then found no network had no way
    // to tell whether the request had been heard. Give it a moment and ask.
    if (!stillAlive(hostapdPid_)) {
        {
            // **The journal gets hostapd's words; the panel gets the user's.**
            //
            // "nl80211: Could not configure driver mode" is the right thing
            // to record and the wrong thing to scroll past somebody holding a
            // clock - it also overflowed the display, which refused it as
            // "string too long". 832 pixels is a sentence, not a log line.
            //
            // And the sentence can be definite, because this is understood
            // now rather than being investigated: a flashed STIPPLE cannot
            // raise an access point on this hardware at all. hostapd is
            // refused the managed-to-AP switch, nothing holds the interface,
            // and the device carries no tool to set the mode itself. The
            // stock application manages it through the vendor's network HAL,
            // which is the documented shape on this device - the panel goes
            // through libzkhw.so for the same reason. See
            // docs/research/tc002-platform-findings.md.
            //
            // So the honest message is the one that gets the user working:
            // set Wi-Fi up on the stock clock, then install.
            note("hostapd: " + lastHostapdLine());
            noteFailure("setup hotspot failed");
        }
        stop();
        return false;
    }

    // Now that the radio is an access point, give it the address clients
    // will be told to use. Before hostapd this was a station address on an
    // interface hostapd then refused; after it, it is the AP's own.
    const char* const address[] = {"/sbin/ifconfig", kInterface, kAddress,
                                   "netmask", "255.255.255.0", "up", nullptr};
    if (run(address) != 0) {
        noteFailure("hotspot: ifconfig would not give wlan0 its address");
        stop();
        return false;
    }

    const char* const startDnsmasq[] = {"/bin/dnsmasq", "--keep-in-foreground",
                                        "--conf-file=/tmp/stipple-dnsmasq.conf", nullptr};
    dnsmasqPid_ = spawn(startDnsmasq, "/tmp/stipple-dnsmasq-stderr.log");
    if (dnsmasqPid_ < 0) {
        noteFailure("hotspot: dnsmasq would not start");
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
    note("hotspot: started " + ssid + " on " + std::string(kAddress) + " key " + passphrase_);
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

    // The vendor's soft-AP manager configures wlan0 with 192.168.100.1 when
    // the vendor application initialises inside this process. Ask it to let
    // go here too, not only when hosting: otherwise the very first thing the
    // panel shows is an IP address for a network that does not exist.
    releaseVendorSoftAp();

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
