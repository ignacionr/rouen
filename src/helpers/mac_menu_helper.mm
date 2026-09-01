#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <iostream>
#include "mac_menu_helper.hpp"
#include <SDL3/SDL.h>

#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

@interface RouenMenuBlockTarget : NSObject
@property (nonatomic, copy) void (^actionBlock)(void);
- (instancetype)initWithBlock:(void (^)(void))block;
- (void)handleMenuItem:(id)sender;
@end

@implementation RouenMenuBlockTarget
@synthesize actionBlock = _actionBlock;

- (instancetype)initWithBlock:(void (^)(void))block {
    if ((self = [super init])) {
        self.actionBlock = block;
    }
    return self;
}

- (void)handleMenuItem:(id)sender {
    (void)sender;
    if (self.actionBlock) {
        self.actionBlock();
    }
}
@end

namespace rouen::platform {

#if defined(__APPLE__)
void disable_mac_cmd_w_menu_item() {
    @autoreleasepool {
        NSMenu* mainMenu = [NSApp mainMenu];
        if (!mainMenu) return;
        for (NSMenuItem* item in [mainMenu itemArray]) {
            if ([item hasSubmenu]) {
                NSMenu* subMenu = [item submenu];
                for (NSMenuItem* subItem in [subMenu itemArray]) {
                    if ([[subItem keyEquivalent] isEqualToString:@"w"] &&
                        ([subItem keyEquivalentModifierMask] & NSEventModifierFlagCommand)) {
                        [subItem setKeyEquivalent:@""];
                    }
                }
            }
        }
    }
}

int get_mac_titlebar_height(SDL_Window* window) {
    if (!window) return 0;
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    void* ptr = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!ptr) return 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
    NSWindow* nsWindow = (__bridge NSWindow*)ptr;
#pragma clang diagnostic pop
    NSRect frame = [nsWindow frame];
    NSRect content = [nsWindow contentRectForFrameRect:frame];
    return static_cast<int>(frame.size.height - content.size.height);
}

float get_mac_backing_scale_factor(SDL_Window* window) {
    if (!window) return 1.0f;
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    void* ptr = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!ptr) return 1.0f;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
    NSWindow* nsWindow = (__bridge NSWindow*)ptr;
#pragma clang diagnostic pop
    return static_cast<float>([nsWindow backingScaleFactor]);
}
#endif

} // namespace rouen::platform

namespace rouen::mac_menu {

static NSMutableArray<RouenMenuBlockTarget*>* g_target_storage = nil;

static void ensure_storage() {
    if (!g_target_storage) {
        g_target_storage = [[NSMutableArray alloc] init];
    }
}

static NSMenuItem* build_nsmenu_item(const item& mi) {
    if (mi.is_separator) {
        return [NSMenuItem separatorItem];
    }

    NSString* titleStr = [NSString stringWithUTF8String:mi.label.c_str()];
    NSMenuItem* nsItem = [[NSMenuItem alloc] initWithTitle:titleStr action:nil keyEquivalent:@""];

    if (!mi.sub_items.empty()) {
        NSMenu* subMenu = [[NSMenu alloc] initWithTitle:titleStr];
        [subMenu setAutoenablesItems:NO];
        for (const auto& sub : mi.sub_items) {
            NSMenuItem* subNsItem = build_nsmenu_item(sub);
            [subMenu addItem:subNsItem];
        }
        [nsItem setSubmenu:subMenu];
    } else if (mi.action) {
        ensure_storage();
        std::function<void()> action_fn = mi.action;
        RouenMenuBlockTarget* target = [[RouenMenuBlockTarget alloc] initWithBlock:^{
            if (action_fn) {
                action_fn();
            }
        }];
        [g_target_storage addObject:target];
        [nsItem setTarget:target];
        [nsItem setAction:@selector(handleMenuItem:)];
        [nsItem setEnabled:mi.enabled ? YES : NO];
    } else {
        [nsItem setEnabled:NO];
    }

    return nsItem;
}

bool show_native_context_menu(const std::vector<item>& items, SDL_Window* window, float x, float y) {
    @autoreleasepool {
        ensure_storage();
        [g_target_storage removeAllObjects];

        NSMenu* contextMenu = [[NSMenu alloc] initWithTitle:@"ContextMenu"];
        [contextMenu setAutoenablesItems:NO];

        for (const auto& item_entry : items) {
            NSMenuItem* nsItem = build_nsmenu_item(item_entry);
            [contextMenu addItem:nsItem];
        }

        NSWindow* nsWindow = nil;
        if (window) {
            SDL_PropertiesID props = SDL_GetWindowProperties(window);
            void* ptr = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
            if (ptr) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
                nsWindow = (__bridge NSWindow*)ptr;
#pragma clang diagnostic pop
            }
        }

        NSView* targetView = nil;
        if (nsWindow) {
            targetView = [nsWindow contentView];
        }

        if (!targetView) {
            return false;
        }

        NSRect viewBounds = [targetView bounds];
        NSPoint locationInView = NSMakePoint(static_cast<CGFloat>(x), viewBounds.size.height - static_cast<CGFloat>(y));

        NSEvent* dummyEvent = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                                                 location:locationInView
                                            modifierFlags:0
                                                timestamp:[[NSProcessInfo processInfo] systemUptime]
                                             windowNumber:[nsWindow windowNumber]
                                                  context:nil
                                              eventNumber:0
                                               clickCount:1
                                                 pressure:1.0];

        [NSMenu popUpContextMenu:contextMenu withEvent:dummyEvent forView:targetView];
        return true;
    }
}

void clear_menu_targets() {
    @autoreleasepool {
        if (g_target_storage) {
            [g_target_storage removeAllObjects];
        }
    }
}

} // namespace rouen::mac_menu
