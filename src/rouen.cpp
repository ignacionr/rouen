#include <imgui/imgui.h>
#include <SDL2/SDL.h>
#include <iostream>

// Add include for our new main_wnd class
#include "main_wnd.hpp"

// These are still needed for the registrar and deck
#include "cards/interface/deck.hpp"
#include "registrar.hpp"
#include "helpers/notify_service.hpp"
#include "helpers/debug.hpp"

int main() {
    notify_service notify; // Initialize the notify service
    // Create and initialize the main window
    main_wnd window;
    if (!window.initialize()) {
        SYS_ERROR("Failed to initialize window");
        return -1;
    }
    
    // Run the main loop
    window.run();

    return 0;
}
