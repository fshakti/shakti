/*
 * synth_mac.m — macOS platform layer for Shakti synth (Cocoa + AudioQueue).
 */

#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>

#include "synth_platform.h"
#include "input.h"
#include <stdio.h>

#define SYNTH_MAC_DESIGN_W 960
#define SYNTH_MAC_DESIGN_H 540
#define SYNTH_MAC_MIN_W 640
#define SYNTH_MAC_MIN_H 360
#define SYNTH_MAC_SR 48000
#define SYNTH_MAC_BUF 512
#define SYNTH_MAC_N_BUFS 3

@interface SynthView : NSView
@end

@implementation SynthView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    uint32_t *px = synth_core_present_pixels();
    int w = synth_core_present_width();
    int h = synth_core_present_height();
    if (!px || w <= 0 || h <= 0) return;

    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:w * 4 bitsPerPixel:32];
    if (!rep) return;

    /* Framebuffer row 0 = top (X11-style). NSBitmapImageRep row 0 maps to bottom
     * when drawn in a flipped view — reverse rows so header appears at top. */
    unsigned char *dst = [rep bitmapData];
    for (int y = 0; y < h; y++) {
        int dy = (h - 1) - y;
        for (int x = 0; x < w; x++) {
            uint32_t c = px[y * w + x];
            unsigned char *p = dst + (dy * w + x) * 4;
            p[0] = (unsigned char)((c >> 16) & 255u);
            p[1] = (unsigned char)((c >> 8) & 255u);
            p[2] = (unsigned char)(c & 255u);
            p[3] = 255;
        }
    }
    [rep drawInRect:self.bounds];
}
- (void)mouseDown:(NSEvent *)ev {
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    input_hub_inject_mouse((int)p.x, (int)p.y, 1);
    if (!input_own_gui()) {
        synth_core_handle_click((int)p.x, (int)p.y, 1);
    }
    synth_core_mark_dirty();
}
- (void)mouseDragged:(NSEvent *)ev {
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    input_hub_inject_mouse((int)p.x, (int)p.y, 1);
    if (!input_own_gui()) {
        synth_core_handle_motion((int)p.x, (int)p.y);
    }
    synth_core_mark_dirty();
}
- (void)mouseUp:(NSEvent *)ev {
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    input_hub_inject_mouse((int)p.x, (int)p.y, 0);
    if (!input_own_gui()) {
        synth_core_handle_release((int)p.x, (int)p.y, 0);
    }
    synth_core_mark_dirty();
}
- (void)keyDown:(NSEvent *)ev {
    unsigned short code = [ev keyCode];
    NSString *chars = [ev charactersIgnoringModifiers];
    char utf8[8] = {0};
    if (chars.length > 0) {
        [chars getCString:utf8 maxLength:sizeof utf8 encoding:NSUTF8StringEncoding];
    }
    input_hub_inject_key((int)code, (int)[ev modifierFlags], utf8, 1);
    if (!input_own_gui()) {
        synth_core_handle_key((int)[chars length] > 0 ? [chars characterAtIndex:0] : code, 1);
    }
}
- (void)keyUp:(NSEvent *)ev {
    unsigned short code = [ev keyCode];
    NSString *chars = [ev charactersIgnoringModifiers];
    char utf8[8] = {0};
    if (chars.length > 0) {
        [chars getCString:utf8 maxLength:sizeof utf8 encoding:NSUTF8StringEncoding];
    }
    input_hub_inject_key((int)code, (int)[ev modifierFlags], utf8, 0);
    if (!input_own_gui()) {
        synth_core_handle_key((int)[chars length] > 0 ? [chars characterAtIndex:0] : code, 0);
    }
}
@end

@interface SynthWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation SynthWindowDelegate
- (void)windowWillClose:(NSNotification *)note {
    (void)note;
    synth_core_set_alive(0);
}
- (void)windowDidResize:(NSNotification *)note {
    NSWindow *win = note.object;
    if (!win) return;
    NSRect fr = win.contentView.frame;
    synth_core_fb_resize((int)fr.size.width, (int)fr.size.height);
    synth_core_mark_dirty();
}
@end

static NSWindow *g_win;
static SynthView *g_view;
static SynthWindowDelegate *g_delegate;
static int g_app_ready;

static AudioQueueRef g_aq;
static AudioQueueBufferRef g_aq_bufs[SYNTH_MAC_N_BUFS];

static void synth_mac_ensure_app(void) {
    if (g_app_ready) return;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    g_app_ready = 1;
}

static void synth_mac_audio_callback(void *user, AudioQueueRef q, AudioQueueBufferRef buf) {
    (void)user;
    float mono[SYNTH_MAC_BUF];
    int16_t *out;
    int i;
    if (!synth_core_audio_running()) return;
    synth_core_render(mono, SYNTH_MAC_BUF);
    out = (int16_t *)buf->mAudioData;
    for (i = 0; i < SYNTH_MAC_BUF; i++) {
        int s = (int)(mono[i] * 32767.f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i * 2] = (int16_t)s;
        out[i * 2 + 1] = (int16_t)s;
    }
    buf->mAudioDataByteSize = SYNTH_MAC_BUF * 2 * (UInt32)sizeof(int16_t);
    AudioQueueEnqueueBuffer(q, buf, 0, NULL);
}

int synth_platform_init(char *err, size_t cap) {
    NSRect frame;
    int headless = getenv("SHAKTI_SYNTH_HEADLESS") != NULL;
    int has_screen = NSScreen.mainScreen != nil;
    (void)err;
    (void)cap;
    if (headless || !has_screen) {
        if (err && cap) snprintf(err, cap, "synth_open: no GUI display");
        return -1;
    }
    synth_mac_ensure_app();
    if (synth_core_fb_design_init() != 0 ||
        synth_core_fb_resize(SYNTH_MAC_DESIGN_W, SYNTH_MAC_DESIGN_H) != 0) {
        if (err && cap) snprintf(err, cap, "synth_open: framebuffer init failed");
        return -1;
    }
    frame = NSMakeRect(100, 100, SYNTH_MAC_DESIGN_W, SYNTH_MAC_DESIGN_H);
    g_view = [[SynthView alloc] initWithFrame:frame];
    g_win = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                                          backing:NSBackingStoreBuffered defer:NO];
    [g_win setContentView:g_view];
    [g_win setTitle:@"Shakti Synth"];
    [g_win setMinSize:NSMakeSize(SYNTH_MAC_MIN_W, SYNTH_MAC_MIN_H)];
    g_delegate = [[SynthWindowDelegate alloc] init];
    [g_win setDelegate:g_delegate];
    [g_win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    synth_platform_request_maximize();
    return 0;
}

void synth_platform_shutdown(void) {
    if (g_win) {
        [g_win orderOut:nil];
        g_win = nil;
    }
    g_view = nil;
    g_delegate = nil;
}

void synth_platform_present(void) {
    if (g_view) [g_view setNeedsDisplay:YES];
}

void synth_platform_request_maximize(void) {
    if (g_win) [g_win zoom:nil];
}

int synth_platform_poll(int *cfg_w, int *cfg_h, int *cfg_count) {
    NSEvent *ev;
    NSDate *until = [NSDate distantPast];
    if (cfg_w) *cfg_w = 0;
    if (cfg_h) *cfg_h = 0;
    if (cfg_count) *cfg_count = 0;
    if (!synth_core_is_alive()) return 0;
    for (;;) {
        ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                untilDate:until
                                   inMode:NSDefaultRunLoopMode
                                  dequeue:YES];
        if (!ev) break;
        [NSApp sendEvent:ev];
    }
    if (g_win && synth_core_want_maximize()) {
        NSRect fr = g_win.frame;
        NSScreen *scr = g_win.screen ?: NSScreen.mainScreen;
        NSRect vis = scr.visibleFrame;
        if (fr.size.width < vis.size.width * 0.75 || fr.size.height < vis.size.height * 0.75) {
            if (synth_core_get_maximize_tries() < 8) {
                synth_core_bump_maximize_tries();
                synth_platform_request_maximize();
            }
        } else {
            synth_core_clear_want_maximize();
        }
    }
    return synth_core_is_alive();
}

int synth_audio_start(char *err, size_t cap) {
    AudioStreamBasicDescription fmt;
    OSStatus st;
    int i;
    memset(&fmt, 0, sizeof fmt);
    fmt.mSampleRate = SYNTH_MAC_SR;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    fmt.mBitsPerChannel = 16;
    fmt.mChannelsPerFrame = 2;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mBytesPerPacket = 4;
    st = AudioQueueNewOutput(&fmt, synth_mac_audio_callback, NULL, NULL, kCFRunLoopCommonModes, 0, &g_aq);
    if (st != noErr) {
        if (err && cap) snprintf(err, cap, "synth_open: AudioQueueNewOutput %d", (int)st);
        return -1;
    }
    for (i = 0; i < SYNTH_MAC_N_BUFS; i++) {
        st = AudioQueueAllocateBuffer(g_aq, SYNTH_MAC_BUF * 2 * (UInt32)sizeof(int16_t), &g_aq_bufs[i]);
        if (st != noErr) {
            if (err && cap) snprintf(err, cap, "synth_open: AudioQueueAllocateBuffer %d", (int)st);
            synth_audio_stop();
            return -1;
        }
        g_aq_bufs[i]->mAudioDataByteSize = SYNTH_MAC_BUF * 2 * (UInt32)sizeof(int16_t);
        AudioQueueEnqueueBuffer(g_aq, g_aq_bufs[i], 0, NULL);
    }
    synth_core_set_audio_run(1);
    st = AudioQueueStart(g_aq, NULL);
    if (st != noErr) {
        if (err && cap) snprintf(err, cap, "synth_open: AudioQueueStart %d", (int)st);
        synth_audio_stop();
        return -1;
    }
    return 0;
}

void synth_audio_stop(void) {
    int i;
    synth_core_set_audio_run(0);
    if (g_aq) {
        AudioQueueStop(g_aq, true);
        for (i = 0; i < SYNTH_MAC_N_BUFS; i++) {
            if (g_aq_bufs[i]) AudioQueueFreeBuffer(g_aq, g_aq_bufs[i]);
            g_aq_bufs[i] = NULL;
        }
        AudioQueueDispose(g_aq, true);
        g_aq = NULL;
    }
}
