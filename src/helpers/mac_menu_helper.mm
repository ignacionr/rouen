#import <Cocoa/Cocoa.h>
#include "mac_menu_helper.hpp"
#include <iostream>

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
                }
            }
        }
        if (found) {
            std::cout << "DEBUG: Successfully disabled Cmd+W menu shortcut." << std::endl;
            completed = true;
        }
    }
    [pool release];
}

} // namespace rouen::platform
