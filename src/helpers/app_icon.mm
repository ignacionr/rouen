#import <Cocoa/Cocoa.h>
#include "app_icon.hpp"

namespace rouen::helpers {

SDL_Surface* extract_icon_surface(const std::string& path) {
    @autoreleasepool {
        NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
        if (!ns_path) return nullptr;

        NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:ns_path];
        if (!icon) return nullptr;

        const int width = 128;
        const int height = 128;

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr
                          pixelsWide:width
                          pixelsHigh:height
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:0
                        bitsPerPixel:0];
        if (!rep) return nullptr;

        NSGraphicsContext* ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:ctx];
        [icon drawInRect:NSMakeRect(0, 0, width, height)
                fromRect:NSZeroRect
               operation:NSCompositingOperationCopy
                fraction:1.0];
        [NSGraphicsContext restoreGraphicsState];

        SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (!surface) return nullptr;

        unsigned char* src = [rep bitmapData];
        NSInteger src_pitch = [rep bytesPerRow];
        for (int y = 0; y < height; ++y) {
            std::memcpy(static_cast<uint8_t*>(surface->pixels) + static_cast<size_t>(y) * static_cast<size_t>(surface->pitch),
                        src + static_cast<size_t>(y) * static_cast<size_t>(src_pitch),
                        static_cast<size_t>(width) * 4);
        }
        return surface;
    }
}

} // namespace rouen::helpers
