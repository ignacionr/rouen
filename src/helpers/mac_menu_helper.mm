#import <Cocoa/Cocoa.h>
#include "mac_menu_helper.hpp"
#include "../cards/interface/menu.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
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
    void (^block)(void) = self.actionBlock;
    if (block) {
        try {
            block();
        } catch (const std::exception& e) {
            std::cerr << "ERROR: macOS menu callback exception: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "ERROR: Unknown exception in macOS menu callback\n";
        }
    }
}
@end

static const char kRouenMenuTargetKey = 0;

namespace rouen::platform {

void disable_mac_cmd_w_menu_item() {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSApplication* app = [NSApplication sharedApplication];
    NSMenu* mainMenu = [app mainMenu];

    if (mainMenu && [mainMenu numberOfItems] > 0) {
        // Check if custom categories are already merged
        bool already_merged = false;
        for (NSMenuItem* item in [mainMenu itemArray]) {
            if ([[item title] isEqualToString:@"Development"]) {
                already_merged = true;
                break;
            }
        }
        if (already_merged) {
            [pool release];
            return;
        }

        std::cout << "DEBUG: mac_menu_helper merging categories into mainMenu (items before: " << [mainMenu numberOfItems] << ")\n";

        for (NSMenuItem* item in [mainMenu itemArray]) {
            if ([item hasSubmenu]) {
                NSMenu* submenu = [item submenu];
                for (NSMenuItem* subitem in [submenu itemArray]) {
                    NSString* eq = [[subitem keyEquivalent] lowercaseString];
                    NSUInteger const mask = [subitem keyEquivalentModifierMask];
                    if ((mask & NSEventModifierFlagCommand) && ![eq isEqualToString:@"q"]) {
                        [subitem setKeyEquivalent:@""];
                        [subitem setKeyEquivalentModifierMask:0];
                    }
                }
            }
        }
        
        NSMenuItem* appMenuItem = [mainMenu itemAtIndex:0];
        NSMenu* appMenu = [appMenuItem hasSubmenu] ? [appMenuItem submenu] : nil;
        if (appMenu) {
            [appMenu setAutoenablesItems:NO];
        }

        const auto& categories = rouen::cards::menu::get_categories();

        for (const auto& category : categories) {
            std::cout << "DEBUG: Processing menu category '" << category.name << "' (" << category.items.size() << " items)\n";
            if (category.name == "System") {
                if (!appMenu) continue;

                NSInteger insertIndex = 1;
                if ([appMenu numberOfItems] > 1 && [[appMenu itemAtIndex:1] isSeparatorItem]) {
                    insertIndex = 2;
                }

                for (const auto& item : category.items) {
                    NSString* nsTitle = [NSString stringWithUTF8String:item.first.c_str()];
                    NSMenuItem* existingSubitem = nil;

                    for (NSMenuItem* subitem in [appMenu itemArray]) {
                        NSString* subTitle = [subitem title];
                        if ([subTitle rangeOfString:nsTitle options:NSCaseInsensitiveSearch].location != NSNotFound ||
                            (item.first == "About" && [subTitle rangeOfString:@"About" options:NSCaseInsensitiveSearch].location != NSNotFound) ||
                            (item.first == "Exit Application" && [subTitle rangeOfString:@"Quit" options:NSCaseInsensitiveSearch].location != NSNotFound)) {
                            existingSubitem = subitem;
                            break;
                        }
                    }

                    auto callback = item.second;
                    RouenMenuBlockTarget* blockTarget = [[RouenMenuBlockTarget alloc] initWithBlock:^{
                        callback();
                    }];

                    if (existingSubitem) {
                        [existingSubitem setTarget:blockTarget];
                        [existingSubitem setAction:@selector(handleMenuItem:)];
                        [existingSubitem setEnabled:YES];
                        objc_setAssociatedObject(existingSubitem, &kRouenMenuTargetKey, blockTarget, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    } else {
                        NSMenuItem* newItem = [appMenu insertItemWithTitle:nsTitle action:@selector(handleMenuItem:) keyEquivalent:@"" atIndex:insertIndex++];
                        [newItem setTarget:blockTarget];
                        [newItem setEnabled:YES];
                        objc_setAssociatedObject(newItem, &kRouenMenuTargetKey, blockTarget, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    }
                }
            } else {
                NSString* nsCategoryName = [NSString stringWithUTF8String:category.name.c_str()];
                NSMenuItem* catMenuItem = nil;
                NSMenu* catSubmenu = nil;

                for (NSMenuItem* item in [mainMenu itemArray]) {
                    if ([[item title] isEqualToString:nsCategoryName]) {
                        catMenuItem = item;
                        catSubmenu = [item submenu];
                        break;
                    }
                }

                if (!catMenuItem) {
                    catSubmenu = [[NSMenu alloc] initWithTitle:nsCategoryName];
                    [catSubmenu setAutoenablesItems:NO];
                    catMenuItem = [[NSMenuItem alloc] initWithTitle:nsCategoryName action:nil keyEquivalent:@""];
                    [catMenuItem setSubmenu:catSubmenu];
                    [catMenuItem setEnabled:YES];
                    [mainMenu addItem:catMenuItem];
                }

                for (const auto& item : category.items) {
                    NSString* nsTitle = [NSString stringWithUTF8String:item.first.c_str()];
                    NSMenuItem* existingSubitem = nil;

                    for (NSMenuItem* subitem in [catSubmenu itemArray]) {
                        if ([[subitem title] isEqualToString:nsTitle]) {
                            existingSubitem = subitem;
                            break;
                        }
                    }

                    auto callback = item.second;
                    RouenMenuBlockTarget* blockTarget = [[RouenMenuBlockTarget alloc] initWithBlock:^{
                        callback();
                    }];

                    if (existingSubitem) {
                        [existingSubitem setTarget:blockTarget];
                        [existingSubitem setAction:@selector(handleMenuItem:)];
                        [existingSubitem setEnabled:YES];
                        objc_setAssociatedObject(existingSubitem, &kRouenMenuTargetKey, blockTarget, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    } else {
                        NSMenuItem* newItem = [catSubmenu addItemWithTitle:nsTitle action:@selector(handleMenuItem:) keyEquivalent:@""];
                        [newItem setTarget:blockTarget];
                        [newItem setEnabled:YES];
                        objc_setAssociatedObject(newItem, &kRouenMenuTargetKey, blockTarget, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    }
                }
            }
        }

        [mainMenu update];
        [app setMainMenu:mainMenu];
        std::cout << "DEBUG: macOS menu merged successfully. Total top-level items: " << [mainMenu numberOfItems] << '\n';
    }
    [pool release];
}

int get_mac_titlebar_height(SDL_Window* window) {
    if (!window) {
        return 0;
    }

    NSWindow* ns_window = static_cast<NSWindow*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL));
    if (!ns_window) {
        return 0;
    }

    const NSRect frame_rect = [ns_window frame];
    const NSRect content_rect = [ns_window contentRectForFrameRect:frame_rect];
    const CGFloat titlebar_height = NSMaxY(frame_rect) - NSMaxY(content_rect);
    return static_cast<int>(std::lround(titlebar_height));
}

float get_mac_backing_scale_factor(SDL_Window* window) {
    if (!window) {
        return 1.0f;
    }

    NSWindow* ns_window = static_cast<NSWindow*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL));
    if (!ns_window) {
        return 1.0f;
    }

    return static_cast<float>([ns_window backingScaleFactor]);
}

} // namespace rouen::platform
