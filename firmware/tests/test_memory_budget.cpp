// SPDX-License-Identifier: GPL-3.0-or-later
//
// Memory budget guards (blueprint §38).
//
// Blueprint §38 treats RAM as a hard design constraint and forbids heap
// allocation during render. That is unverifiable by inspection once a call
// reaches into std::string, so this file replaces the global allocator and
// counts. The numbers here are the contract: if a change makes the render path
// allocate, a test fails rather than the device fragmenting its heap over a
// week of uptime.
//
// Replacing operator new affects the whole test binary. That is intentional and
// harmless — counting only happens inside an explicit scope.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "notrix/api/ApiServer.h"
#include "notrix/app/Carousel.h"
#include "notrix/config/Config.h"
#include "notrix/demo/TestPattern.h"
#include "notrix/graphics/Canvas.h"
#include "notrix/input/InputMapper.h"
#include "notrix/json/Json.h"
#include "notrix/notify/Notifications.h"
#include "notrix/platform/simulator/SimulatorPlatform.h"
#include "notrix/scene/Scene.h"
#include "notrix/text/Scroll.h"
#include "notrix/text/Text.h"
#include "support/TestFramework.h"

namespace {
bool g_counting = false;
int g_allocations = 0;
std::size_t g_bytes = 0;
}  // namespace

void* operator new(std::size_t size) {
    if (g_counting) {
        ++g_allocations;
        g_bytes += size;
    }
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

void* operator new[](std::size_t size) {
    return operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

using notrix::Canvas;
using notrix::Framebuffer;
using notrix::Rect;

/// MSVC's debug STL allocates an iterator-debug proxy for every container it
/// constructs, regardless of small-string optimisation. That swamps the signal:
/// a debug run reports allocations for code that allocates nothing in a real
/// build. So the counts are only asserted where they mean something, and CI runs
/// a release build specifically so this guard is not inert.
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
constexpr bool kAllocationCountsAreMeaningful = false;
#else
constexpr bool kAllocationCountsAreMeaningful = true;
#endif

/// Counts allocations inside its scope. Stop before asserting: the test
/// framework builds strings for failure messages and would count itself.
class Counting {
public:
    Counting() {
        g_allocations = 0;
        g_bytes = 0;
        g_counting = true;
    }
    ~Counting() { g_counting = false; }

    int stop() {
        g_counting = false;
        return g_allocations;
    }

private:
};

/// Deliberately includes text far longer than any small-string buffer. A short
/// label would sit in SSO and hide a per-frame allocation that real scene text —
/// the kind long enough to need scrolling — would trigger every frame.
const char* kSceneJson = R"({"name":"dash","elements":[
    {"type":"rect","rect":[0,0,10,8],"color":"#ff5000","fill":true},
    {"type":"text","rect":[11,0,40,7],"text":"21.4C","align":"left","color":"#ffaa28"},
    {"type":"text","rect":[0,9,52,7],"scroll":"auto","color":"#00c8ff",
     "text":"Living room 21.4 degrees, 48% humidity, updated two minutes ago"},
    {"type":"progress","rect":[0,9,26,3],"value":72,"color":"#00ff00"},
    {"type":"graph","rect":[28,9,24,6],"values":[1,4,2,8,5],"color":"#00aaff"}
]})";

}  // namespace

/// Assert a measured allocation count is zero, but only where the measurement
/// is trustworthy. Reports the raw number either way.
#define NOTRIX_EXPECT_NO_ALLOCATIONS(count, what)                                              \
    do {                                                                                       \
        if (kAllocationCountsAreMeaningful) {                                                  \
            NOTRIX_CHECK_EQ((count), 0);                                                       \
        } else {                                                                               \
            std::printf("        [alloc] %s: %d (debug STL inflates this; "                    \
                        "asserted in release builds only)\n",                                  \
                        (what), (count));                                                      \
        }                                                                                      \
    } while (false)

// --- footprint ---------------------------------------------------------------

NOTRIX_TEST(MemoryBudget, FramebufferIsExactlyTheExpectedSize) {
    // 52 * 16 * 3. The single largest fixed allocation in the system, and the
    // one number the whole display budget is built on.
    NOTRIX_CHECK_EQ(sizeof(Framebuffer), std::size_t(2496));
    NOTRIX_CHECK_EQ(Framebuffer::kByteSize, std::size_t(2496));
}

NOTRIX_TEST(MemoryBudget, CanvasIsAStackHandleNotAnOwner) {
    // Canvas must stay cheap enough to construct per draw pass.
    NOTRIX_CHECK(sizeof(Canvas) <= 32);
}

NOTRIX_TEST(MemoryBudget, ReportsStructureFootprints) {
    // Not assertions so much as a visible record: these are the objects that
    // will live for the lifetime of the device.
    std::printf("        [sizeof] Framebuffer=%zu Canvas=%zu Scene=%zu\n", sizeof(Framebuffer),
                sizeof(Canvas), sizeof(notrix::scene::Scene));
    std::printf("        [sizeof] AppRegistry=%zu Carousel=%zu App=%zu\n",
                sizeof(notrix::app::AppRegistry), sizeof(notrix::app::Carousel),
                sizeof(notrix::app::App));
    std::printf("        [sizeof] NotificationQueue=%zu Notification=%zu Config=%zu\n",
                sizeof(notrix::notify::NotificationQueue), sizeof(notrix::notify::Notification),
                sizeof(notrix::config::Config));
    std::printf("        [sizeof] JsonToken=%zu InputMapper=%zu\n", sizeof(notrix::json::Token),
                sizeof(notrix::input::InputMapper));
    NOTRIX_CHECK(true);
}

// --- the render path ---------------------------------------------------------

NOTRIX_TEST(MemoryBudget, CanvasPrimitivesNeverAllocate) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    Counting counting;
    for (int i = 0; i < 100; ++i) {
        canvas.clear();
        canvas.pixel(1, 1, notrix::colors::kRed);
        canvas.line(0, 0, 51, 15, notrix::colors::kGreen);
        canvas.rect(Rect{2, 2, 10, 10}, notrix::colors::kBlue);
        canvas.fillRect(Rect{4, 4, 6, 6}, notrix::colors::kWhite);
    }
    const int allocations = counting.stop();

    NOTRIX_EXPECT_NO_ALLOCATIONS(allocations, "canvas primitives");
}

NOTRIX_TEST(MemoryBudget, TestPatternNeverAllocates) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    Counting counting;
    for (int frame = 0; frame < 200; ++frame) {
        notrix::demo::drawTestPattern(canvas, frame);
    }
    const int allocations = counting.stop();

    NOTRIX_EXPECT_NO_ALLOCATIONS(allocations, "test pattern");
}

NOTRIX_TEST(MemoryBudget, TextRenderingNeverAllocates) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    notrix::text::TextStyle style;
    style.font = &notrix::text::font5x7();

    Counting counting;
    for (int i = 0; i < 100; ++i) {
        notrix::text::measureLine("21.4\xC2\xB0" "C", notrix::text::font5x7());
        notrix::text::draw(canvas, "Living room", Rect{0, 0, 52, 7}, style);
        notrix::text::drawScrolling(canvas, "A long scrolling label", Rect{0, 8, 40, 7}, style,
                                    notrix::text::ScrollMode::Auto,
                                    static_cast<std::uint64_t>(i) * 100u);
    }
    const int allocations = counting.stop();

    NOTRIX_EXPECT_NO_ALLOCATIONS(allocations, "text rendering");
}

NOTRIX_TEST(MemoryBudget, CarouselTickNeverAllocates) {
    notrix::app::AppRegistry registry;
    for (int i = 0; i < 4; ++i) {
        notrix::app::App entry;
        entry.id = "app" + std::to_string(i);
        entry.name = entry.id;
        entry.durationSeconds = 2;
        registry.put(std::move(entry));
    }
    notrix::app::Carousel carousel(registry);
    carousel.tick(0);

    Counting counting;
    for (int i = 0; i < 500; ++i) {
        carousel.tick(static_cast<std::uint64_t>(i) * 100u);
    }
    const int allocations = counting.stop();

    // Ticking is the steady state: it runs every frame, forever.
    NOTRIX_EXPECT_NO_ALLOCATIONS(allocations, "carousel tick");
}

NOTRIX_TEST(MemoryBudget, NotificationTickNeverAllocates) {
    notrix::notify::NotificationQueue queue;
    notrix::notify::Notification notification;
    notification.id = "n1";
    notification.text = "Doorbell";
    notification.durationSeconds = 600;
    queue.push(std::move(notification), 0);

    Counting counting;
    for (int i = 0; i < 500; ++i) {
        queue.tick(static_cast<std::uint64_t>(i) * 100u);
    }
    const int allocations = counting.stop();

    NOTRIX_EXPECT_NO_ALLOCATIONS(allocations, "notification tick");
}

NOTRIX_TEST(MemoryBudget, SceneRenderNeverAllocates) {
    // Scenes are the normal render path once apps exist, so it has to hold here
    // too, not just for hand-called primitives. Text is rendered straight from
    // the parsed document's bytes rather than materialised into a std::string.
    notrix::json::Token tokens[256];
    notrix::scene::Scene scene(tokens, 256);
    const std::string json = kSceneJson;
    NOTRIX_CHECK(scene.load(json));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    // One warm-up frame, so the measurement is steady-state rather than
    // first-call effects.
    scene.render(canvas, 0);

    Counting counting;
    for (int i = 0; i < 10; ++i) {
        scene.render(canvas, static_cast<std::uint64_t>(i) * 33u);
    }
    const int allocations = counting.stop();

    NOTRIX_EXPECT_NO_ALLOCATIONS(allocations, "scene render (10 frames)");
}

// --- bounded structures ------------------------------------------------------

NOTRIX_TEST(MemoryBudget, EveryQueueAndRegistryIsBounded) {
    // §38 forbids unbounded queues. These caps are what make the worst case
    // knowable before it happens.
    NOTRIX_CHECK_EQ(notrix::app::AppRegistry::kMaxApps, 32);
    NOTRIX_CHECK_EQ(notrix::notify::NotificationQueue::kMaxQueued, 16);
    NOTRIX_CHECK_EQ(notrix::platform::simulator::SimulatorInput::kCapacity, std::size_t(32));
    NOTRIX_CHECK(notrix::app::AppRegistry::kMaxSceneBytes <= 8192);
    NOTRIX_CHECK(notrix::notify::NotificationQueue::kMaxTextBytes <= 512);
}

NOTRIX_TEST(MemoryBudget, WorstCaseAppStorageIsKnowable) {
    // 32 apps at 4 KB of scene each is the ceiling the device must survive.
    const std::size_t worstCase = static_cast<std::size_t>(notrix::app::AppRegistry::kMaxApps) *
                                  notrix::app::AppRegistry::kMaxSceneBytes;
    std::printf("        [budget] worst-case app scene storage: %zu bytes\n", worstCase);

    // Flagged deliberately: 128 KB of scene text may not fit alongside
    // everything else on the real device. Phase 7 measures it; until then this
    // records the number rather than pretending it is safe.
    NOTRIX_CHECK_EQ(worstCase, std::size_t(131072));
}

NOTRIX_TEST(MemoryBudget, ApiRequestBudgetIsBounded) {
    const notrix::api::ApiOptions defaults;
    NOTRIX_CHECK(defaults.maxBodyBytes <= 32u * 1024u);
    NOTRIX_CHECK(defaults.maxJsonTokens <= 1024);

    // Token storage for one request, which is transient but concurrent with
    // everything else.
    const std::size_t tokenBytes =
        static_cast<std::size_t>(defaults.maxJsonTokens) * sizeof(notrix::json::Token);
    std::printf("        [budget] api request token storage: %zu bytes\n", tokenBytes);
    NOTRIX_CHECK(tokenBytes <= 16u * 1024u);
}
