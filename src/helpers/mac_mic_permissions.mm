#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#include "mac_mic_permissions.h"
#include <iostream>

namespace rouen::platform {

void request_mac_microphone_permission() {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    @try {
        AVAuthorizationStatus const status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        if (status == AVAuthorizationStatusNotDetermined) {
            std::cout << "[Rouen] Requesting macOS Microphone Permission...\n";
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                std::cout << "[Rouen] Microphone permission result: granted=" << (granted ? "true" : "false") << '\n';
            }];
        }
    } @catch (id) {}
    [pool drain];
}

} // namespace rouen::platform
