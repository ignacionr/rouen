#import <Cocoa/Cocoa.h>
#include "mac_menu_helper.hpp"
#include "../registrar.hpp"
#include <iostream>

@interface RouenMenuHandler : NSObject
- (void)handleAboutMenu:(id)sender;
@end

static RouenMenuHandler* g_menuHandler = nil;

@implementation RouenMenuHandler
- (void)handleAboutMenu:(id)sender {
    try {
        auto service = registrar::get<std::function<void(std::string const&)>>("create_card");
        if (service) {
            (*service)("about");
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to trigger About card from macOS menu: " << e.what() << '\n';
    } catch (...) {
        std::cerr << "ERROR: Unknown error triggering About card from macOS menu" << '\n';
    }
}
@end

namespace rouen::platform {

void disable_mac_cmd_w_menu_item() {
    static bool completed = false;
    if (completed) return;
    
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSApplication* app = [NSApplication sharedApplication];
    NSMenu* mainMenu = [app mainMenu];
    if (mainMenu) {
        bool found = false;
        for (NSMenuItem* item in [mainMenu itemArray]) {
            if ([item hasSubmenu]) {
                NSMenu* submenu = [item submenu];
                for (NSMenuItem* subitem in [submenu itemArray]) {
                    NSString* eq = [subitem keyEquivalent];
                    NSUInteger mask = [subitem keyEquivalentModifierMask];
                    if ([eq isEqualToString:@"w"] && (mask & NSEventModifierFlagCommand)) {
                        [subitem setKeyEquivalent:@""];
                        [subitem setKeyEquivalentModifierMask:0];
                        found = true;
                    }
                    if ([eq isEqualToString:@"p"] && (mask & NSEventModifierFlagCommand)) {
                        [subitem setKeyEquivalent:@""];
                        [subitem setKeyEquivalentModifierMask:0];
                        found = true;
                    }
                }
            }
        }
        
        // Configure native "About Rouen" menu item to trigger our custom card
        if ([mainMenu numberOfItems] > 0) {
            NSMenuItem* appMenuItem = [mainMenu itemAtIndex:0];
            if ([appMenuItem hasSubmenu]) {
                NSMenu* appMenu = [appMenuItem submenu];
                for (NSMenuItem* subitem in [appMenu itemArray]) {
                    if ([[subitem title] rangeOfString:@"About" options:NSCaseInsensitiveSearch].location != NSNotFound) {
                        if (!g_menuHandler) {
                            g_menuHandler = [[RouenMenuHandler alloc] init];
                        }
                        [subitem setTarget:g_menuHandler];
                        [subitem setAction:@selector(handleAboutMenu:)];
                        std::cout << "DEBUG: Custom About menu handler registered successfully." << '\n';
                        found = true;
                    }
                }
            }
        }
        
        if (found) {
            completed = true;
        }
    }
    [pool release];
}

} // namespace rouen::platform
