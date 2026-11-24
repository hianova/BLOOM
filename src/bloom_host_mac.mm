#import <Cocoa/Cocoa.h>
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>

#include "bloom_host.h"

#include <mach/mach_time.h>

@interface BloomMetalView : NSView
@property (nonatomic, assign) CAMetalLayer *metalLayer;
@end

@implementation BloomMetalView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (CALayer *)makeBackingLayer {
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.displaySyncEnabled = YES;
    self.metalLayer = layer;
    return layer;
}
@end

@interface BloomAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@public
    BloomHostCallbacks cbs;
    NSWindow *window;
    BloomMetalView *view;
    uint64_t last_ticks;
    mach_timebase_info_data_t tb;
    BOOL running;
    BOOL initialized;
}
- (void)onTick:(id)sender;
@end

@implementation BloomAppDelegate
- (void)syncScale {
    CGFloat sc = window.backingScaleFactor > 0.0 ? window.backingScaleFactor : 1.0;
    view.metalLayer.contentsScale = sc;
    CGSize sz = view.bounds.size;
    view.metalLayer.drawableSize = CGSizeMake(sz.width * sc, sz.height * sc);
}

- (void)windowDidResize:(NSNotification *)notification {
    (void)notification;
    [self syncScale];
    if (initialized && cbs.on_input) {
        BloomInputEvent ev = {};
        ev.type = BLOOM_EVENT_RESIZE;
        ev.width = (uint32_t)view.metalLayer.drawableSize.width;
        ev.height = (uint32_t)view.metalLayer.drawableSize.height;
        cbs.on_input(cbs.userdata, &ev);
    }
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    running = NO;
    [NSApp stop:nil];
    NSEvent *ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                      location:NSZeroPoint modifierFlags:0 timestamp:0
                                  windowNumber:0 context:nil subtype:0 data1:0 data2:0];
    [NSApp postEvent:ev atStart:YES];
    return YES;
}

- (void)dispatchPointer:(NSEvent *)e type:(BloomInputEventType)t {
    if (!initialized || !cbs.on_input) return;
    NSPoint pt = [view convertPoint:e.locationInWindow fromView:nil];
    CGFloat sc = view.metalLayer.contentsScale > 0.0 ? view.metalLayer.contentsScale : 1.0;
    BloomInputEvent ev = {};
    ev.type = t;
    ev.x = (float)(pt.x * sc);
    ev.y = (float)(pt.y * sc);
    ev.dx = (float)(e.deltaX * sc);
    ev.dy = (float)(e.deltaY * sc);
    ev.button = (uint32_t)e.buttonNumber;
    cbs.on_input(cbs.userdata, &ev);
}

- (void)installMonitors {
    __weak BloomAppDelegate *weakSelf = self;
    [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown) handler:^NSEvent *(NSEvent *e) {
        BloomAppDelegate *s = weakSelf;
        if (s && e.window == s->window) [s dispatchPointer:e type:BLOOM_EVENT_POINTER_DOWN];
        return e;
    }];
    [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp) handler:^NSEvent *(NSEvent *e) {
        BloomAppDelegate *s = weakSelf;
        if (s && e.window == s->window) [s dispatchPointer:e type:BLOOM_EVENT_POINTER_UP];
        return e;
    }];
    [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged) handler:^NSEvent *(NSEvent *e) {
        BloomAppDelegate *s = weakSelf;
        if (s && e.window == s->window) [s dispatchPointer:e type:BLOOM_EVENT_POINTER_MOVE];
        return e;
    }];
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *e) {
        BloomAppDelegate *s = weakSelf;
        if (s && e.window == s->window) [s dispatchPointer:e type:BLOOM_EVENT_SCROLL];
        return e;
    }];
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *e) {
        BloomAppDelegate *s = weakSelf;
        if (s && e.window == s->window && s->initialized && s->cbs.on_input) {
            BloomInputEvent ev = {};
            ev.type = BLOOM_EVENT_KEY_DOWN;
            ev.key_code = e.keyCode;
            s->cbs.on_input(s->cbs.userdata, &ev);
        }
        return e;
    }];
}

- (void)onTick:(id)sender {
    (void)sender;
    if (!running || !initialized) return;
    uint64_t now = mach_absolute_time();
    double dt = (double)(now - last_ticks) * (double)tb.numer / ((double)tb.denom * 1e9);
    last_ticks = now;
    if (dt > 0.1) dt = 0.016;
    uint32_t w = (uint32_t)view.metalLayer.drawableSize.width;
    uint32_t h = (uint32_t)view.metalLayer.drawableSize.height;
    if (cbs.on_frame && w > 0u && h > 0u) cbs.on_frame(cbs.userdata, dt, w, h);
}
@end

int bloom_host_run_cocoa(const char *title, uint32_t width_pt, uint32_t height_pt,
                         const BloomHostCallbacks *cbs) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect rc = NSMakeRect(120, 120, width_pt, height_pt);
        NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
        NSWindow *win = [[NSWindow alloc] initWithContentRect:rc styleMask:mask
                                                      backing:NSBackingStoreBuffered defer:NO];
        win.title = [NSString stringWithUTF8String:(title ? title : "BLOOM vNext")];

        BloomMetalView *vw = [[BloomMetalView alloc] initWithFrame:rc];
        vw.wantsLayer = YES;
        win.contentView = vw;

        BloomAppDelegate *del = [[BloomAppDelegate alloc] init];
        if (cbs) del->cbs = *cbs;
        del->window = win;
        del->view = vw;
        mach_timebase_info(&del->tb);
        del->last_ticks = mach_absolute_time();
        del->running = YES;
        win.delegate = del;

        [del syncScale];
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        if (del->cbs.on_init) {
            del->cbs.on_init(del->cbs.userdata, (__bridge void *)vw.metalLayer,
                             (uint32_t)vw.metalLayer.drawableSize.width,
                             (uint32_t)vw.metalLayer.drawableSize.height);
        }
        del->initialized = YES;
        [del installMonitors];

        if (@available(macOS 14.0, *)) {
            CADisplayLink *dl = [vw displayLinkWithTarget:del selector:@selector(onTick:)];
            [dl addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        } else {
            [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0) target:del
                                           selector:@selector(onTick:) userInfo:nil repeats:YES];
        }

        [NSApp run];
        if (del->cbs.on_shutdown) del->cbs.on_shutdown(del->cbs.userdata);
    }
    return 0;
}
