# Rouen Card Infrastructure

## Overview

The Rouen application uses a card-based UI architecture where each functional component is represented as a "card" - a rectangular widget with its own presentation and behavior. This document explains how to work with and extend the card system.

## Core Components

### Card Base Class

The `card` class (in `interface/card.hpp`) is the foundation of the UI system. All cards must inherit from this base class. Key features:

- **Colors**: Each card can define its own color scheme using an array of colors
- **Window Management**: Handles focus, window rendering, and title management
- **Lifecycle**: Virtual methods for rendering and cleanup

```cpp
struct card {
    using ptr = std::shared_ptr<card>;
    
    // Colors for theming
    static constexpr size_t MAX_COLORS = 16;
    using color_array = std::array<ImVec4, MAX_COLORS>;
    
    // Required methods to implement
    virtual bool render() = 0;
    virtual std::string get_uri() const = 0;
    
    // Helper methods
    bool render_window(std::function<void()> render_func);
    void name(std::string_view name);
    ImVec4& get_color(size_t index, const ImVec4& default_color = ImVec4{0.0f, 0.0f, 0.0f, 1.0f});
    
    // Properties
    color_array colors;                // Fixed-size array of colors
    float width {300.0f};              // Default card width
    bool is_focused{false};            // Track focus state
    bool grab_focus{false};            // Request focus
    std::string window_title;          // Window title
    int requested_fps{1};              // Preferred render rate
};
```

### Deck

The `deck` class (in `interface/deck.hpp`) manages a collection of cards. It:

- **Creates cards**: Using the card factory system
- **Renders cards**: Positions and draws cards in a horizontal layout
- **Handles persistence**: Saves and loads card states from configuration
- **Manages shortcuts**: Global keyboard shortcuts to manage cards

### Card Factory

The `factory` class (in `interface/factory.hpp`) creates card instances based on URI strings. It maintains a dictionary of card types that can be instantiated.

## Card Lifecycle

1. Cards are created by the deck using the factory (`create_card("menu")`)
2. Each card is initialized with default colors and settings
3. Cards render themselves using ImGui and handle their own state
4. Cards can be closed by the user (Ctrl+W) or programmatically
5. Card URIs are saved to `rouen.ini` for persistence

## Available Cards

The system includes many example cards:

- `menu` - Application menu and card launcher
- `git` - Git repository browser
- `github` - GitHub repository browser
- `cmake` - CMake project viewer and builder
- `sysinfo` - System information display
- `fs_directory` - File system explorer
- `pomodoro` - Time management tool
- `calendar` - Google Calendar integration
- `grok` - AI chat assistant
- `rss` - RSS feed reader
- `travel` - Travel planner
- `weather` - Weather information
- `mail` - Email client
- `radio` - Internet radio player
- `envvars` - Environment variables viewer
- `dbrepair` - Database repair tool

## Creating a New Card

### 1. Define a new card class:

```cpp
namespace rouen::cards {
    class my_card : public card {
    public:
        my_card() {
            // Set custom colors
            colors[0] = {0.2f, 0.5f, 0.8f, 1.0f}; // Primary color
            colors[1] = {0.3f, 0.6f, 0.9f, 0.7f}; // Secondary color
            
            // Add more colors if needed
            get_color(2, ImVec4(0.4f, 0.7f, 1.0f, 1.0f)); // Special element color
            
            // Set window properties
            name("My Card");           // Set window title
            width = 400.0f;            // Set card width
            requested_fps = 5;         // Request higher refresh rate if needed
        }
        
        bool render() override {
            return render_window([this]() {
                // Your ImGui code here
                ImGui::Text("Hello, world!");
                
                // Use card colors for consistent theming
                ImGui::TextColored(colors[2], "Special colored text");
                
                if (ImGui::Button("Do Something")) {
                    // Button action
                }
            });
        }
        
        std::string get_uri() const override {
            return "my-card";  // Unique URI for this card type
        }
        
    private:
        // Private card state
    };
}
```

### 2. Register in the factory:

Add your card to the factory dictionary in `interface/factory.hpp`:

```cpp
// In the dictionary() method
{"my-card", [](std::string_view uri, SDL_Renderer* ) {
    return std::make_shared<my_card>();
}}
```

## UI Design Guidelines

### Colors

- Use the base colors (`colors[0]` and `colors[1]`) for main elements
- Define additional colors for specific elements using `get_color(index)`
- Use consistent coloring for similar elements across cards

### Layout

- Standard card height is 450px, width is customizable (default 300px)
- Use ImGui's layout system for responsive design
- Use appropriate spacing and separation between elements

### Interactivity

- Handle card-specific keyboard shortcuts in the render method
- Use `is_focused` to check if the card has focus
- For global actions, use the registrar system

### Performance

- Set `requested_fps` appropriately (1 for static content, higher for animations)
- Minimize heavy operations in the render loop
- Consider using background threads for expensive operations

## Advanced Features

### Card URI Parameters

Cards can accept parameters through their URIs:

```
card-type:parameter
```

Example: `dir:/home/user` creates a directory card pointing to `/home/user`

### Card Communication

Cards can communicate through the registrar system:

```cpp
// Register a function
registrar::add<std::function<void(std::string)>>(
    "some_action", 
    std::make_shared<std::function<void(std::string)>>(
        [](std::string arg) { /* ... */ }
    )
);

// Call the function from another card
"some_action"_sfn("argument");
```

### Card Snapshots

Cards can be captured as images using the Ctrl+Shift+S shortcut. This saves the current card as a PNG file.