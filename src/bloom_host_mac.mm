#import <Cocoa/Cocoa.h>
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>

#include "bloom_host.h"

#include <mach/mach_time.h>
#include <string.h>

static NSRect g_ime_caret_pt = {{40.0, 40.0}, {2.0, 24.0}};

static uint32_t map_modifiers(NSEventModifierFlags flags) {
    uint32_t m = 0u;
    if (flags & NSEventModifierFlagShift)   m |= BLOOM_MOD_SHIFT;
    if (flags & NSEventModifierFlagControl) m |= BLOOM_MOD_CTRL;
    if (flags & NSEventModifierFlagOption)  m |= BLOOM_MOD_ALT;
    if (flags & NSEventModifierFlagCommand) m |= BLOOM_MOD_SUPER;
    return m;
}

@interface BloomMetalView : NSView <NSTextInputClient> {
@public
    const BloomHostCallbacks *cbs_ptr;
    BOOL initialized;
}
@property (nonatomic, assign) CAMetalLayer *metalLayer;
@property (nonatomic, copy) NSString *markedBuf;
@property (nonatomic, assign) NSRange markedRng;
@property (nonatomic, assign) NSRange selectedRng;
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

- (void)emitTextEvent:(BloomInputEventType)t str:(NSString *)s cursor:(int32_t)cur {
    if (!initialized || !cbs_ptr || !cbs_ptr->on_input) return;
    BloomInputEvent ev = {};
    ev.type = t;
    const char *u = s ? s.UTF8String : "";
    if (u) snprintf(ev.text, sizeof(ev.text), "%s", u);
    ev.preedit_cursor = cur;
    ev.preedit_len = (int32_t)strlen(ev.text);
    cbs_ptr->on_input(cbs_ptr->userdata, &ev);
}

- (void)keyDown:(NSEvent *)event {
    if (initialized && cbs_ptr && cbs_ptr->on_input) {
        BloomInputEvent ev = {};
        ev.type = BLOOM_EVENT_KEY_DOWN;
        ev.key_code = event.keyCode;
        ev.modifiers = map_modifiers(event.modifierFlags);
        cbs_ptr->on_input(cbs_ptr->userdata, &ev);
    }
    [self interpretKeyEvents:@[event]];
}

- (void)doCommandBySelector:(SEL)selector {
    (void)selector;
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    (void)replacementRange;
    NSString *str = [string isKindOfClass:[NSAttributedString class]]
                        ? [(NSAttributedString *)string string]
                        : (NSString *)string;
    self.markedBuf = @"";
    self.markedRng = NSMakeRange(NSNotFound, 0);
    [self emitTextEvent:BLOOM_EVENT_IME_PREEDIT str:@"" cursor:0];
    if (str && str.length > 0) {
        [self emitTextEvent:BLOOM_EVENT_TEXT_INPUT str:str cursor:(int32_t)str.length];
    }
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
    (void)replacementRange;
    NSString *str = [string isKindOfClass:[NSAttributedString class]]
                        ? [(NSAttributedString *)string string]
                        : (NSString *)string;
    self.markedBuf = str ? str : @"";
    self.selectedRng = selectedRange;
    self.markedRng = self.markedBuf.length > 0
                         ? NSMakeRange(0, self.markedBuf.length)
                         : NSMakeRange(NSNotFound, 0);
    [self emitTextEvent:BLOOM_EVENT_IME_PREEDIT str:self.markedBuf cursor:(int32_t)selectedRange.location];
}

- (void)unmarkText {
    self.markedBuf = @"";
    self.markedRng = NSMakeRange(NSNotFound, 0);
    [self emitTextEvent:BLOOM_EVENT_IME_PREEDIT str:@"" cursor:0];
}

- (NSRange)selectedRange { return _selectedRng; }
- (NSRange)markedRange { return _markedBuf.length > 0 ? _markedRng : NSMakeRange(NSNotFound, 0); }
- (BOOL)hasMarkedText { return _markedBuf.length > 0; }

- (nullable NSAttributedString *)attributedSubstringForProposedRange:(NSRange)proposedRange
                                                        actualRange:(nullable NSRangePointer)actualRange {
    (void)proposedRange;
    if (actualRange) *actualRange = NSMakeRange(0, 0);
    return nil;
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
    (void)range;
    if (actualRange) *actualRange = NSMakeRange(0, 0);
    NSRect wr = [self convertRect:g_ime_caret_pt toView:nil];
    return self.window ? [self.window convertRectToScreen:wr] : wr;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    (void)point;
    return 0;
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
    ev.modifiers = map_modifiers(e.modifierFlags);
    if (t == BLOOM_EVENT_PINCH) ev.scale_delta = (float)e.magnification;
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
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify handler:^NSEvent *(NSEvent *e) {
        BloomAppDelegate *s = weakSelf;
        if (s && e.window == s->window) [s dispatchPointer:e type:BLOOM_EVENT_PINCH];
        return e;
    }];
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp handler:^NSEvent *(NSEvent *e) {
        BloomAppDelegate *s = weakSelf;
        if (s && e.window == s->window && s->initialized && s->cbs.on_input) {
            BloomInputEvent ev = {};
            ev.type = BLOOM_EVENT_KEY_UP;
            ev.key_code = e.keyCode;
            ev.modifiers = map_modifiers(e.modifierFlags);
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

bool bloom_host_clipboard_set(const char *utf8) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        NSString *s = [NSString stringWithUTF8String:(utf8 ? utf8 : "")];
        return [pb setString:(s ? s : @"") forType:NSPasteboardTypeString] ? true : false;
    }
}

size_t bloom_host_clipboard_get(char *out_buf, size_t cap) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSString *s = [pb stringForType:NSPasteboardTypeString];
        if (!s) return 0u;
        const char *u = s.UTF8String;
        size_t len = u ? strlen(u) : 0u;
        if (out_buf && cap > 0u && u) snprintf(out_buf, cap, "%s", u);
        return len;
    }
}

void bloom_host_set_ime_rect(float x_pt, float y_pt, float h_pt) {
    g_ime_caret_pt = NSMakeRect((CGFloat)x_pt, (CGFloat)y_pt, 2.0, (CGFloat)(h_pt > 4.0f ? h_pt : 20.0f));
}

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
        vw.markedBuf = @"";
        vw.markedRng = NSMakeRange(NSNotFound, 0);
        win.contentView = vw;

        BloomAppDelegate *del = [[BloomAppDelegate alloc] init];
        if (cbs) del->cbs = *cbs;
        del->window = win;
        del->view = vw;
        vw->cbs_ptr = &del->cbs;
        mach_timebase_info(&del->tb);
        del->last_ticks = mach_absolute_time();
        del->running = YES;
        win.delegate = del;

        [del syncScale];
        [win makeFirstResponder:vw];
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        if (del->cbs.on_init) {
            del->cbs.on_init(del->cbs.userdata, (__bridge void *)vw.metalLayer,
                             (uint32_t)vw.metalLayer.drawableSize.width,
                             (uint32_t)vw.metalLayer.drawableSize.height);
        }
        del->initialized = YES;
        vw->initialized = YES;
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
