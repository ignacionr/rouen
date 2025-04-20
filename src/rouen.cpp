#include <imgui/imgui.h>
#include <SDL2/SDL.h>
#include <iostream>

// Add include for our new main_wnd class
#include "main_wnd.hpp"

// These are still needed for the registrar and deck
#include "cards/deck.hpp"
#include "registrar.hpp"
#include "helpers/deferred_operations.hpp"

int main() {
    // Create and initialize the main window
    main_wnd window;
    if (!window.initialize()) {
        std::cerr << "Failed to initialize window" << std::endl;
        return -1;
    }
    
    // Run the main loop
    window.run();

    return 0;
}
