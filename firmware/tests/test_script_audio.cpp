// SPDX-License-Identifier: GPL-3.0-or-later
//
// The speaker, as a script sees it.
//
// Berry scripts get four builtins: tone(), sound(), audio_known() and
// volume(). Three things about them are load-bearing and are what these
// tests hold down.
//
// A device with no speaker says so (ADR 0013) rather than accepting tones
// into a void, so a script can draw something instead of bleeping.
//
// A script cannot flood the queue. The panel would keep rendering happily
// while the speaker worked through a minute of backlog, which is a device
// nobody can use and nothing on screen to explain why.
//
// And volume is readable but not writable. It is the owner's setting; an
// app turning it up on its own is not a feature.
#include "stipple/script/ScriptStore.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/platform/PlatformServices.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::script::ScriptPutResult;
using stipple::script::ScriptStore;
namespace colors = stipple::colors;

namespace {

/// Records what a script asked the speaker to do.
class RecordingAudio : public stipple::platform::IAudioOutput {
public:
    struct Tone {
        int hz;
        int ms;
    };

    std::vector<Tone> tones;
    std::vector<std::string> sounds;
    std::uint8_t level = 42;

    /// A speaker that is present but cannot take the request right now -
    /// a full queue, not a missing device.
    bool refuse = false;

    bool playTone(int frequencyHz, int durationMillis) override {
        if (refuse) {
            return false;
        }
        tones.push_back(Tone{frequencyHz, durationMillis});
        return true;
    }

    bool playSound(std::string_view name) override {
        if (refuse) {
            return false;
        }
        sounds.emplace_back(name);
        return true;
    }

    void stop() override {}
    void setVolume(std::uint8_t volume) override { level = volume; }
    std::uint8_t volume() const override { return level; }
};

int countLit(const Framebuffer& framebuffer) {
    int lit = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (framebuffer.at(x, y) != colors::kBlack) {
                ++lit;
            }
        }
    }
    return lit;
}

/// Compile `source` and draw one frame of it, reporting what landed.
bool drawOnce(ScriptStore& store, const char* id, const char* source,
              Framebuffer& framebuffer, std::uint32_t elapsedMillis = 0) {
    if (store.put(id, id, source) != ScriptPutResult::Added) {
        return false;
    }
    Canvas canvas(framebuffer);
    return store.draw(id, canvas, elapsedMillis);
}

}  // namespace

STIPPLE_TEST(ScriptAudio, AScriptCanPlayAToneAndIsToldItWorked) {
    ScriptStore store;
    RecordingAudio audio;
    store.setAudio(&audio);

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "beep", R"BE(
class App
  def draw()
    if audio_known() && tone(880, 120)
      pixel(0, 0, rgb(0, 255, 0))
    end
  end
end
return App()
)BE",
                             framebuffer));

    STIPPLE_REQUIRE(audio.tones.size() == 1);
    STIPPLE_CHECK_EQ(audio.tones[0].hz, 880);
    STIPPLE_CHECK_EQ(audio.tones[0].ms, 120);
    // It was told the tone started, and drew accordingly.
    STIPPLE_CHECK(framebuffer.at(0, 0) != colors::kBlack);
}

STIPPLE_TEST(ScriptAudio, ANamedSoundReachesTheSpeakerVerbatim) {
    ScriptStore store;
    RecordingAudio audio;
    store.setAudio(&audio);

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "chime", R"BE(
class App
  def draw()
    sound('notify')
  end
end
return App()
)BE",
                             framebuffer));

    STIPPLE_REQUIRE(audio.sounds.size() == 1);
    STIPPLE_CHECK(audio.sounds[0] == "notify");
}

STIPPLE_TEST(ScriptAudio, ADeviceWithNoSpeakerSaysSoRatherThanPretending) {
    // A script that bleeps at a panel which cannot bleep should be able to
    // find out and draw something instead. No audio is installed here.
    ScriptStore store;

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "quiet", R"BE(
class App
  def draw()
    if !audio_known()
      text(0, 0, 'mute', rgb(255, 0, 0))
    end
    if tone(440, 50)
      pixel(51, 15, rgb(0, 255, 0))
    end
  end
end
return App()
)BE",
                             framebuffer));

    STIPPLE_CHECK(countLit(framebuffer) > 0);                   // drew "mute"
    STIPPLE_CHECK(framebuffer.at(51, 15) == colors::kBlack);    // tone() was false
}

STIPPLE_TEST(ScriptAudio, ASpeakerThatRefusesIsReportedHonestly) {
    // A full queue is not the same as no speaker, and a script is told the
    // difference: audio_known() stays true, the call returns false.
    ScriptStore store;
    RecordingAudio audio;
    audio.refuse = true;
    store.setAudio(&audio);

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "busy", R"BE(
class App
  def draw()
    if audio_known() && !tone(440, 50)
      pixel(1, 1, rgb(255, 128, 0))
    end
  end
end
return App()
)BE",
                             framebuffer));

    STIPPLE_CHECK(framebuffer.at(1, 1) != colors::kBlack);
    STIPPLE_CHECK(audio.tones.empty());
}

STIPPLE_TEST(ScriptAudio, AScriptCannotFloodTheSpeaker) {
    ScriptStore store;
    RecordingAudio audio;
    store.setAudio(&audio);

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "spam", R"BE(
class App
  def draw()
    var i = 0
    while i < 200
      tone(440, 10)
      i += 1
    end
  end
end
return App()
)BE",
                             framebuffer));

    STIPPLE_CHECK_EQ(static_cast<int>(audio.tones.size()), 4);

    // The budget is per call, so the next frame gets its own four rather
    // than the script being silenced for ever by one greedy frame.
    audio.tones.clear();
    Canvas canvas(framebuffer);
    STIPPLE_REQUIRE(store.draw("spam", canvas, 33));
    STIPPLE_CHECK_EQ(static_cast<int>(audio.tones.size()), 4);
}

STIPPLE_TEST(ScriptAudio, AbsurdTonesAreRefusedBeforeTheyReachTheSpeaker) {
    ScriptStore store;
    RecordingAudio audio;
    store.setAudio(&audio);

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "silly", R"BE(
class App
  def draw()
    tone(0, 100)
    tone(-5, 100)
    tone(99000, 100)
    tone(440, 0)
  end
end
return App()
)BE",
                             framebuffer));

    STIPPLE_CHECK(audio.tones.empty());
}

STIPPLE_TEST(ScriptAudio, AVeryLongToneIsClampedRatherThanRefused) {
    // Asking for a minute-long note is a mistake, not an attack. Clamping
    // keeps the script working and keeps the speaker available.
    ScriptStore store;
    RecordingAudio audio;
    store.setAudio(&audio);

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "long", R"BE(
class App
  def draw()
    tone(440, 60000)
  end
end
return App()
)BE",
                             framebuffer));

    STIPPLE_REQUIRE(audio.tones.size() == 1);
    STIPPLE_CHECK_EQ(audio.tones[0].ms, 5000);
}

STIPPLE_TEST(ScriptAudio, VolumeIsReadableAndNotWritable) {
    ScriptStore store;
    RecordingAudio audio;
    audio.level = 77;
    store.setAudio(&audio);

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "vol", R"BE(
class App
  def draw()
    pixel(volume() % width(), 0, rgb(1, 2, 3))
  end
end
return App()
)BE",
                             framebuffer));

    STIPPLE_CHECK(framebuffer.at(77 % Framebuffer::kWidth, 0) != colors::kBlack);
    // Nothing a script can call changed it.
    STIPPLE_CHECK_EQ(static_cast<int>(audio.level), 77);
}

STIPPLE_TEST(ScriptAudio, ASpeakerArrivingLateReachesScriptsAlreadyLoaded) {
    // The host installs scripts before it knows what the platform offers.
    // A store told about the speaker afterwards must reach every host it
    // already made, not only the next one.
    ScriptStore store;

    Framebuffer framebuffer;
    STIPPLE_REQUIRE(drawOnce(store, "late", R"BE(
class App
  def draw()
    if audio_known()
      tone(660, 40)
    end
  end
end
return App()
)BE",
                             framebuffer));

    RecordingAudio audio;
    store.setAudio(&audio);

    Canvas canvas(framebuffer);
    STIPPLE_REQUIRE(store.draw("late", canvas, 33));
    STIPPLE_REQUIRE(audio.tones.size() == 1);
    STIPPLE_CHECK_EQ(audio.tones[0].hz, 660);
}

STIPPLE_TEST(ScriptAudio, TheSpeakerGoingAwayDoesNotTakeTheScriptWithIt) {
    // Nothing should keep a dangling pointer to a platform that has been
    // torn down, and a script that bleeped happily a second ago has to
    // carry on drawing when it cannot any more.
    ScriptStore store;
    Framebuffer framebuffer;

    {
        RecordingAudio audio;
        store.setAudio(&audio);
        STIPPLE_REQUIRE(drawOnce(store, "gone", R"BE(
class App
  def draw()
    clear(rgb(0, 0, 0))
    if audio_known()
      tone(440, 20)
      pixel(0, 0, rgb(0, 255, 0))
    else
      pixel(0, 0, rgb(255, 0, 0))
    end
  end
end
return App()
)BE",
                                 framebuffer));
        STIPPLE_CHECK_EQ(static_cast<int>(audio.tones.size()), 1);
        store.setAudio(nullptr);
    }

    Canvas canvas(framebuffer);
    STIPPLE_REQUIRE(store.draw("gone", canvas, 33));
    STIPPLE_CHECK(framebuffer.at(0, 0) == colors::kRed);
}
