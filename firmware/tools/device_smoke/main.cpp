// SPDX-License-Identifier: GPL-3.0-or-later
//
// Proves stipple_core links and runs on the target architecture.
//
// Not the device binary. There is no TC002 platform adapter yet, so this drives
// the core through the simulator adapter — which is enough to answer the only
// questions it exists to answer: does the core cross-compile, does it link, does
// it actually execute on ARM, and how big is the result.
//
// Those matter before hardware arrives. A core that turns out to need something
// the target does not have is much cheaper to discover in a container than on a
// device with an 8 MiB partition. Copy this onto the TC002 over ADB and run it:
// if it prints its lines, the toolchain, the ABI and the glibc floor are all
// right, and everything that fails afterwards is the adapter's fault rather than
// the build's.
//
// When the real adapter exists, this is replaced by a main() that constructs it
// instead. The shape of the loop does not change.

#include <cstdio>

#include "stipple/core/Version.h"
#include "stipple/host/ApplicationHost.h"
#include "stipple/platform/simulator/SimulatorPlatform.h"

int main() {
    std::printf("stipple %.*s smoke test\n", static_cast<int>(stipple::kVersion.size()),
                stipple::kVersion.data());

    stipple::platform::simulator::SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1700000000);

    stipple::host::HostConfig config;
    config.splashMillis = 0;

    stipple::host::ApplicationHost host(platform, config);
    if (!host.initialize()) {
        std::printf("FAIL: initialize() returned false\n");
        return 1;
    }

    // Enough ticks to boot, render and settle. If the panel never lights, the
    // core is not working on this architecture whatever else compiled.
    for (std::uint64_t now = 0; now <= 2000; now += 50) {
        host.tick(now);
        platform.simulatedClock().advance(50);
    }

    int lit = 0;
    for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
        for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
            if (host.frame().at(x, y) != stipple::colors::kBlack) {
                ++lit;
            }
        }
    }

    std::printf("panel      : %dx%d\n", stipple::Framebuffer::kWidth,
                stipple::Framebuffer::kHeight);
    std::printf("lit pixels : %d\n", lit);
    std::printf("frames     : %u rendered, %u skipped\n", host.frameStats().rendered,
                host.frameStats().skipped);
    std::printf("boot       : %s\n", stipple::host::bootModeName(host.bootMode()));

    // A clock that rendered nothing is a failure even though everything linked.
    if (lit == 0) {
        std::printf("FAIL: nothing was drawn\n");
        return 1;
    }

    // And the API answers, which is the other half of what the device needs.
    stipple::api::Request request;
    request.method = stipple::api::Method::Get;
    request.path = "/api/v1/device";
    const stipple::api::Response response = host.handle(request);
    std::printf("api        : GET /api/v1/device -> %d, %zu bytes\n", response.status,
                response.body.size());
    if (response.status != 200) {
        std::printf("FAIL: the API did not answer\n");
        return 1;
    }

    std::printf("OK\n");
    return 0;
}
