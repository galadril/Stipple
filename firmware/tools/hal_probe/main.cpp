// SPDX-License-Identifier: GPL-3.0-or-later
//
// Asks the vendor HAL what it offers, without asking it to do anything.
//
// The TC002's LED panel is reached through `libzkhw.so` (`ledc_set_led`,
// `ledc_set_group`) rather than by driving SPI directly. Before writing an
// adapter against that, two things are worth establishing separately:
//
//   1. Can we load the library and find the symbols at all? That is a question
//      about linking, paths and ABI, and it has nothing to do with hardware.
//   2. Do those functions do what we think? That question requires taking the
//      panel away from the vendor application, and is not asked here.
//
// This program answers only the first. It resolves every symbol and then
// **calls none of them** — deliberately, because `register_ledcdev` would claim
// the LED controller while the vendor app is still driving it, and the point of
// a probe is to be safe to run on a working device.
//
// Run it on a stock device with everything running. It should change nothing.

#include <dlfcn.h>

#include <cstdio>

namespace {

struct Symbol {
    const char* name;
    const char* note;
};

// From `readelf --dyn-syms` on the device's own libraries. Grouped so a missing
// symbol says which capability is affected rather than just failing.
constexpr Symbol kLedControl[] = {
    {"register_ledcdev", "claim the LED controller"},
    {"unregister_ledcdev", "release it"},
    {"ledc_set_led", "set one LED"},
    {"ledc_set_group", "set a group of LEDs"},
    {"ledc_set_args", "configure the controller"},
    {"ledc_get_errcode", "read the last error"},
};

constexpr Symbol kSpi[] = {
    {"register_spidev", "claim the SPI device"},
    {"unregister_spidev", "release it"},
    {"spi_halfduplex_transfer", "transfer bytes"},
    {"spi_get_errcode", "read the last error"},
};

int report(void* handle, const char* group, const Symbol* symbols, int count) {
    std::printf("\n%s\n", group);
    int missing = 0;

    for (int i = 0; i < count; ++i) {
        // dlerror() must be cleared first: a null symbol address is legal, so
        // the error state is the only reliable indicator.
        dlerror();
        void* address = dlsym(handle, symbols[i].name);
        const char* failure = dlerror();

        if (failure != nullptr) {
            std::printf("  MISSING  %-24s %s\n", symbols[i].name, symbols[i].note);
            ++missing;
        } else {
            std::printf("  ok       %-24s %s  [%p]\n", symbols[i].name, symbols[i].note,
                        address);
        }
    }
    return missing;
}

}  // namespace

int main() {
    std::printf("NOTRIX HAL probe - resolves symbols, calls none of them\n");

    const char* candidates[] = {"libzkhw.so", "/lib/libzkhw.so"};
    void* handle = nullptr;

    for (const char* path : candidates) {
        handle = dlopen(path, RTLD_LAZY);
        if (handle != nullptr) {
            std::printf("loaded   : %s\n", path);
            break;
        }
        std::printf("not here : %s (%s)\n", path, dlerror());
    }

    if (handle == nullptr) {
        std::printf("\nFAIL: could not load the vendor HAL.\n");
        return 1;
    }

    int missing = 0;
    missing += report(handle, "LED control", kLedControl,
                      sizeof(kLedControl) / sizeof(kLedControl[0]));
    missing += report(handle, "SPI transport", kSpi, sizeof(kSpi) / sizeof(kSpi[0]));

    // Closing is the last thing that happens, and nothing was claimed, so the
    // vendor application is undisturbed throughout.
    dlclose(handle);

    std::printf("\n%s\n", missing == 0
        ? "Every symbol resolved. The adapter has an API to build against."
        : "Some symbols are missing - see above.");
    return missing == 0 ? 0 : 1;
}
