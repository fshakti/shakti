#import <Cocoa/Cocoa.h>
#include <ApplicationServices/ApplicationServices.h>
#include "gfx_platform.h"
#include "gfx.h"
#include "input.h"
#undef in
#undef st

#define GFX_MAC_W 960
#define GFX_MAC_H 540

@interface GfxView : NSView {
    NSBitmapImageRep *_rep;
    int _repW;
    int _repH;
}
@end

static void gfx_mac_blit_rep(NSBitmapImageRep *rep, uint32_t *px, int w, int h) {
    unsigned char *dst;
    int x, y;
    if (!rep || !px || w <= 0 || h <= 0) return;
    dst = [rep bitmapData];
    for (y = 0; y < h; y++) {
        int dy = (h - 1) - y;
        const uint32_t *src = px + (size_t)y * (size_t)w;
        unsigned char *row = dst + (size_t)dy * (size_t)w * 4u;
        for (x = 0; x < w; x++) {
            uint32_t c = src[x];
            unsigned char *p = row + (size_t)x * 4u;
            p[0] = (unsigned char)((c >> 16) & 255u);
            p[1] = (unsigned char)((c >> 8) & 255u);
            p[2] = (unsigned char)(c & 255u);
            p[3] = 255;
        }
    }
}

@implementation GfxView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (_rep) [_rep drawInRect:self.bounds];
}
- (void)mouseDown:(NSEvent *)ev {
    [[self window] makeFirstResponder:self];
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    input_hub_inject_mouse((int)p.x, (int)p.y, 1);
    gfx_core_mouse_design((int)p.x, (int)p.y, 1);
    gfx_core_mark_dirty();
}
- (void)mouseUp:(NSEvent *)ev {
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    input_hub_inject_mouse((int)p.x, (int)p.y, 0);
    gfx_core_mouse_design((int)p.x, (int)p.y, 0);
    gfx_core_mark_dirty();
}
- (void)keyDown:(NSEvent *)ev {
    [[self window] makeFirstResponder:self];
    unsigned short code = [ev keyCode];
    NSString *chars = [ev charactersIgnoringModifiers];
    char utf8[8] = {0};
    if (chars.length > 0)
        [chars getCString:utf8 maxLength:sizeof utf8 encoding:NSUTF8StringEncoding];
    input_hub_inject_key((int)code, (int)[ev modifierFlags], utf8, 1);
    if (input_own_gui())
        input_hub_key_set((int)code, 1);
}
- (void)keyUp:(NSEvent *)ev {
    unsigned short code = [ev keyCode];
    NSString *chars = [ev charactersIgnoringModifiers];
    char utf8[8] = {0};
    int focused;
    if (chars.length > 0)
        [chars getCString:utf8 maxLength:sizeof utf8 encoding:NSUTF8StringEncoding];
    input_hub_inject_key((int)code, (int)[ev modifierFlags], utf8, 0);
    /* Ignore keyUp when we are not the key window — macOS synthesizes
     * releases on resignKey, which previously stuck the bat after scoring. */
    focused = (self.window && [self.window isKeyWindow]) ? 1 : 0;
    if (input_own_gui() && focused)
        input_hub_key_set((int)code, 0);
}
- (void)gfxEnsureRep:(int)w h:(int)h {
    if (_rep && _repW == w && _repH == h) return;
    _rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:w * 4 bitsPerPixel:32];
    _repW = w;
    _repH = h;
}
- (void)gfxUpdatePixels:(uint32_t *)px w:(int)w h:(int)h {
    [self gfxEnsureRep:w h:h];
    gfx_mac_blit_rep(_rep, px, w, h);
    [self setNeedsDisplay:YES];
}
@end

@interface GfxWindowDelegate : NSObject <NSWindowDelegate>
@end

static NSWindow *g_win;
static GfxView *g_view;
static GfxWindowDelegate *g_delegate;
static int g_app_ready;

/* Full HID sync after focus returns. Regular poll only promotes keys. */
static void gfx_mac_recover_held_keys(void) {
    static const CGKeyCode codes[] = {126, 125, 13, 1};
    size_t i;
    if (!input_own_gui()) return;
    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        CGKeyCode c = codes[i];
        int hid = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, c) ? 1 : 0;
        int ses = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, c) ? 1 : 0;
        input_hub_key_set((int)c, (hid || ses) ? 1 : 0);
    }
}

@implementation GfxWindowDelegate
- (void)windowWillClose:(NSNotification *)note {
    (void)note;
    gfx_core_set_alive(0);
}
- (void)windowDidResize:(NSNotification *)note {
    NSWindow *win = note.object;
    if (!win) return;
    NSRect fr = win.contentView.frame;
    gfx_core_fb_resize((int)fr.size.width, (int)fr.size.height);
    gfx_core_mark_dirty();
}
- (void)windowDidBecomeKey:(NSNotification *)note {
    (void)note;
    if (g_win && g_view)
        [g_win makeFirstResponder:g_view];
    gfx_mac_recover_held_keys();
}
@end

static void gfx_mac_ensure_app(void) {
    if (g_app_ready) return;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    g_app_ready = 1;
}

int gfx_platform_init(const char *title, char *err, size_t cap) {
    NSRect frame;
    NSString *t;
    (void)err;(void)cap;
    if (NSScreen.mainScreen == nil) return -1;
    gfx_mac_ensure_app();
    if (gfx_core_fb_resize(GFX_MAC_W, GFX_MAC_H) != 0) return -1;
    frame = NSMakeRect(100, 100, GFX_MAC_W, GFX_MAC_H);
    g_view = [[GfxView alloc] initWithFrame:frame];
    g_win = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                                          backing:NSBackingStoreBuffered defer:NO];
    t = title ? [NSString stringWithUTF8String:title] : @"Shakti GFX";
    [g_win setContentView:g_view];
    [g_win setTitle:t];
    g_delegate = [[GfxWindowDelegate alloc] init];
    [g_win setDelegate:g_delegate];
    [g_win makeKeyAndOrderFront:nil];
    [g_win makeFirstResponder:g_view];
    [NSApp activateIgnoringOtherApps:YES];
    return 0;
}

void gfx_platform_shutdown(void) {
    if (g_win) { [g_win orderOut:nil]; g_win = nil; }
    g_view = nil;
    g_delegate = nil;
}

void gfx_platform_present(void) {
    uint32_t *px;
    int w, h;
    if (!g_view) return;
    px = gfx_core_present_pixels();
    w = gfx_core_present_width();
    h = gfx_core_present_height();
    if (!px || w <= 0 || h <= 0) return;
    [g_view gfxUpdatePixels:px w:w h:h];
}

/* Poll path: only promote HID-down keys. Never clear — call-count debounce
 * previously fired multiple times per frame via tick+physics sync. */
static void gfx_mac_sync_keys(void) {
    static const CGKeyCode codes[] = {126, 125, 13, 1};
    size_t i;
    if (!input_own_gui()) return;
    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        CGKeyCode c = codes[i];
        int hid = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, c) ? 1 : 0;
        int ses = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, c) ? 1 : 0;
        if (hid || ses)
            input_hub_key_set((int)c, 1);
    }
}

int gfx_platform_poll(void) {
    NSEvent *ev;
    if (!gfx_core_is_alive()) return 0;
    for (;;) {
        ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                untilDate:[NSDate distantPast]
                                   inMode:NSDefaultRunLoopMode
                                  dequeue:YES];
        if (!ev) break;
        [NSApp sendEvent:ev];
    }
    /* Do not call makeKeyAndOrderFront every frame — that resigns/becomes key
     * and synthesizes keyUps after scoring. */
    if (g_win && g_view && [g_win isKeyWindow] && [g_win firstResponder] != g_view)
        [g_win makeFirstResponder:g_view];
    return gfx_core_is_alive() ? 0 : -1;
}

void gfx_platform_sync_keys(void) {
    gfx_mac_sync_keys();
}
