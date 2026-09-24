// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tries to make the TC002 speak, through the vendor's own audio library.
//
// The TC002 has a speaker - the vendor application plays /res/ui/audio/Tip.mp3
// and a person standing next to the device hears it navigate. libzkgui.so
// imports MI_AO_SetPubAttr, MI_AO_Enable, MI_AO_EnableChn, MI_AO_SendFrame and
// MI_AO_SetVolume from /lib/libmi_ao.so, so that is the path.
//
// The one thing nothing here had was the layout of MI_AUDIO_Attr_t. Guessing it
// and handing the guess to a driver is exactly the kind of thing this project
// refuses to do, so it was read off the wire instead: an LD_PRELOAD capture of
// the vendor application's ioctls on /dev/mi_ao caught SetPubAttr marshalling a
// 56-byte block, and the values inside it are legible -
//
//     38 00 00 00  size, 56
//     00 00 00 00
//     78 db 99 be  a pointer
//     ff ff ff ff  a return slot
//     00 00 00 00  device 0
//     80 3e 00 00  16000  <- sample rate
//     00 00 00 00  bit width, 16-bit
//     00 00 00 00  work mode, I2S master
//     00 00 00 00  sound mode, mono
//     06 00 00 00  6 frames
//     80 00 00 00  128 points per frame
//     00 00 00 00
//     01 00 00 00  1 channel
//     00 00 00 00
//
// - which is a 16 kHz mono 16-bit stream in 128-sample frames. Those are the
// numbers this passes back.
//
// Dynamically linked of necessity: it dlopens a vendor library, which a static
// binary cannot do. Build with the bullseye toolchain.

#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LIB "/lib/libmi_ao.so"

/// MI_AUDIO_Attr_t as the capture describes it. Nine words.
typedef struct {
    uint32_t sampleRate;      /* 16000 */
    uint32_t bitWidth;        /* 0 = 16-bit */
    uint32_t workMode;        /* 0 = I2S master */
    uint32_t soundMode;       /* 0 = mono */
    uint32_t frameNum;        /* 6 */
    uint32_t pointsPerFrame;  /* 128 */
    uint32_t codecChnCnt;     /* 0 */
    uint32_t chnCnt;          /* 1 */
    uint32_t reserved;        /* 0 */
    /* Slack, in case the real struct is longer than the part that was legible.
       Zeroed, so trailing fields take whatever the driver treats as default. */
    uint32_t slack[8];
} AudioAttr;

/// MI_AUDIO_Frame_t. Not visible in the capture - SendFrame passes a pointer -
/// so this is the SigmaStar shape and the first thing to suspect if SendFrame
/// is the call that fails.
typedef struct {
    uint32_t bitWidth;
    uint32_t soundMode;
    void* data[2];
    uint64_t timestamp;
    uint32_t seq;
    uint32_t length;
    uint32_t poolId[2];
} AudioFrame;

#define RATE 16000
#define POINTS 128

static int (*ao_SetPubAttr)(int, const void*);
static int (*ao_Enable)(int);
static int (*ao_EnableChn)(int, int);
static int (*ao_SetVolume)(int, int);
static int (*ao_SetMute)(int, int);
static int (*ao_SendFrame)(int, int, const void*, int);
static int (*ao_DisableChn)(int, int);
static int (*ao_Disable)(int);

static void* need(void* handle, const char* name, void* target) {
    void* symbol = dlsym(handle, name);
    printf("  %-22s %s\n", name, symbol ? "ok" : "MISSING");
    (void)target;
    return symbol;
}

int main(int argc, char** argv) {
    const int volume = (argc > 1) ? atoi(argv[1]) : -10;
    const int toneHz = (argc > 2) ? atoi(argv[2]) : 880;

    static const char* support[] = {
        "/lib/libcam_os_wrapper.so", "/lib/libcam_fs_wrapper.so",
        "/lib/libmi_common.so",      "/lib/libmi_sys.so",
    };
    for (unsigned i = 0; i < sizeof(support) / sizeof(support[0]); ++i) {
        if (dlopen(support[i], RTLD_NOW | RTLD_GLOBAL) == NULL) {
            printf("support %s: %s\n", support[i], dlerror());
        }
    }

    void* handle = dlopen(LIB, RTLD_NOW | RTLD_GLOBAL);
    if (handle == NULL) {
        printf("dlopen " LIB ": %s\n", dlerror());
        return 1;
    }
    printf("symbols:\n");
    ao_SetPubAttr = need(handle, "MI_AO_SetPubAttr", NULL);
    ao_Enable     = need(handle, "MI_AO_Enable", NULL);
    ao_EnableChn  = need(handle, "MI_AO_EnableChn", NULL);
    ao_SetVolume  = need(handle, "MI_AO_SetVolume", NULL);
    ao_SetMute    = need(handle, "MI_AO_SetMute", NULL);
    ao_SendFrame  = need(handle, "MI_AO_SendFrame", NULL);
    ao_DisableChn = need(handle, "MI_AO_DisableChn", NULL);
    ao_Disable    = need(handle, "MI_AO_Disable", NULL);
    if (!ao_SetPubAttr || !ao_Enable || !ao_EnableChn || !ao_SendFrame) {
        return 1;
    }

    AudioAttr attr;
    memset(&attr, 0, sizeof(attr));
    attr.sampleRate = RATE;
    attr.bitWidth = 0;
    attr.workMode = 0;
    attr.soundMode = 0;
    attr.frameNum = 6;
    attr.pointsPerFrame = POINTS;
    attr.codecChnCnt = 0;
    attr.chnCnt = 1;

    printf("\nMI_AO_SetPubAttr(0) -> 0x%08x\n", (unsigned)ao_SetPubAttr(0, &attr));
    printf("MI_AO_Enable(0)     -> 0x%08x\n", (unsigned)ao_Enable(0));
    printf("MI_AO_EnableChn(0,0)-> 0x%08x\n", (unsigned)ao_EnableChn(0, 0));
    if (ao_SetMute) {
        printf("MI_AO_SetMute(0,0)  -> 0x%08x\n", (unsigned)ao_SetMute(0, 0));
    }
    // Scan rather than guess, for the same reason as u32Len: -10 was refused
    // and an error code alone does not say whether the range is decibels, a
    // percentage, or something this device made up.
    if (ao_SetVolume) {
        printf("%s", "\nvolume values the device accepts:\n  ");
        int accepted = 0;
        for (int candidate = -80; candidate <= 100; ++candidate) {
            if (ao_SetVolume(0, candidate) == 0) {
                printf("%d ", candidate);
                ++accepted;
            }
        }
        printf("%s", accepted ? "\n" : "(none)\n");
        if (accepted) {
            ao_SetVolume(0, volume);
        }
    }

    // Find u32Len by scanning, not by guessing again.
    //
    // MI_AUDIO_Frame_t is the one struct the ioctl capture could not show:
    // SendFrame marshals a pointer, so the frame itself never crossed the
    // boundary being watched. The first attempt put u32Len at offset 28 and the
    // library answered "u32Len is error, u32Len[0]" - it read a zero, so the
    // field is somewhere else.
    //
    // That error message is the whole tool. Writing the length at one candidate
    // offset at a time and watching for the message to change turns an unknown
    // layout into a short search, and the library validates the answer rather
    // than the speaker having to.
    static int16_t samples[POINTS];
    for (int i = 0; i < POINTS; ++i) {
        samples[i] = 0;  // silence while searching; nothing should be audible
    }

    unsigned char frame[128];
    int lengthOffset = -1;

    printf("\nsearching for u32Len...\n");
    for (int offset = 8; offset + 4 <= (int)sizeof(frame); offset += 4) {
        memset(frame, 0, sizeof(frame));
        // eBitwidth and eSoundmode are both zero, which the capture says is
        // 16-bit mono - so only the pointer and the length need placing.
        void* pointer = samples;
        memcpy(frame + 8, &pointer, sizeof(pointer));
        const uint32_t length = (uint32_t)sizeof(samples);
        memcpy(frame + offset, &length, sizeof(length));

        const int result = ao_SendFrame(0, 0, frame, 200);
        printf("  u32Len at +%-2d -> 0x%08x\n", offset, (unsigned)result);
        if (result == 0) {
            lengthOffset = offset;
            break;
        }
    }

    if (lengthOffset < 0) {
        printf("no offset accepted; the frame layout is still wrong\n");
        goto done;
    }
    printf("u32Len lives at offset %d\n", lengthOffset);

    // Now make a noise, with the layout the library just confirmed.
    {
        const int halfPeriod = RATE / (toneHz * 2);
        int phase = 0;
        int high = 1;
        printf("\nsending %d Hz for one second...\n", toneHz);
        int firstError = 0;
        const int frames = RATE / POINTS;
        for (int f = 0; f < frames; ++f) {
            for (int i = 0; i < POINTS; ++i) {
                samples[i] = (int16_t)(high ? 8000 : -8000);
                if (++phase >= halfPeriod) { phase = 0; high = !high; }
            }
            memset(frame, 0, sizeof(frame));
            void* pointer = samples;
            memcpy(frame + 8, &pointer, sizeof(pointer));
            const uint32_t length = (uint32_t)sizeof(samples);
            memcpy(frame + lengthOffset, &length, sizeof(length));

            const int result = ao_SendFrame(0, 0, frame, 1000);
            if (result != 0 && firstError == 0) {
                firstError = result;
                printf("  frame %d -> 0x%08x\n", f, (unsigned)result);
            }
        }
        printf("done; first SendFrame error: 0x%08x\n", (unsigned)firstError);
    }

done:
    sleep(1);
    if (ao_DisableChn) { ao_DisableChn(0, 0); }
    if (ao_Disable) { ao_Disable(0); }
    dlclose(handle);
    return 0;
}
