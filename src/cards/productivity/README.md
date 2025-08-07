# Trello Integration

Rouen provides comprehensive integration with Trello, supporting both general Trello access and specific board viewing through URI schema support.

## Features

### Core Functionality
- **Board Management**: View all user boards with stats (lists, cards, members)
- **Card Operations**: Create, view, search, and manage Trello cards
- **List Management**: Browse and organize cards across lists
- **Search Capabilities**: Search cards globally or within specific boards
- **Real-time Sync**: Asynchronous operations with live status updates

### Authentication & Connection
- **Multiple Connection Methods**:
  - Environment variables (`TRELLO_API_KEY`, `TRELLO_TOKEN`)
  - Manual configuration with API key and token
  - Saved connection profiles for easy switching
- **Secure Profile Storage**: Connection profiles saved locally for reuse
- **Connection Testing**: Validate credentials before saving

## URI Schema Support

Rouen supports two Trello URI schemas for flexible card creation:

### `trello:`
Opens the general Trello interface with access to all boards and functionality.

**Usage:**
```
trello:
```

**Features:**
- Browse all user boards
- Search cards across all boards
- Create new cards in any board/list
- Manage board settings and preferences

### `trello-board:`
Opens a specific Trello board directly with enhanced board-focused functionality.

**Usage:**
```
trello-board:<board_id>
```

**Example:**
```
trello-board:5f8b9c1a2d3e4f5g6h7i8j9k
```

**Features:**
- Direct access to specific board
- Optimized interface for single-board workflows
- Board-specific card creation and management
- Enhanced board overview with detailed statistics

## Getting Started

### 1. Obtain Trello API Credentials

1. Visit [Trello Developer Portal](https://trello.com/app-key)
2. Generate your API Key
3. Generate a Token with appropriate permissions

### 2. Configure Connection

#### Option A: Environment Variables
Set the following environment variables:
```bash
export TRELLO_API_KEY="your_api_key_here"
export TRELLO_TOKEN="your_token_here"
```

#### Option B: Manual Configuration
1. Open Trello card in Rouen
2. Go to "Settings" tab
3. Enter API Key and Token
4. Optionally save as a named profile

### 3. Create Trello Cards

#### General Trello Access
Create a card with access to all boards:
```
Create Card → trello:
```

#### Specific Board Access
Create a card for a specific board:
```
Create Card → trello-board:<your_board_id>
```

To find your board ID:
1. Open the general `trello:` card
2. Browse to your desired board
3. The board ID will be visible in the interface
4. Alternatively, check the board URL in Trello web interface

## Interface Overview

### Tabs

#### Boards Tab (General `trello:` only)
- **Board List**: Shows all accessible boards with stats
- **Quick Actions**: View board details or open in browser
- **Board Creation**: Create new boards directly from Rouen

#### Cards Tab
- **Board Selector**: Choose which board to focus on (general mode)
- **Board Overview**: Statistics and quick access to Trello web interface
- **List Organization**: View cards organized by lists
- **Card Details**: Rich card information with badges, due dates, attachments

#### Create Tab
- **Board Selection**: Choose target board (if not already specified)
- **List Selection**: Choose target list within the board
- **Card Creation**: Name, description, and position settings
- **Real-time Feedback**: Visual indicators during card creation

#### Search Tab
- **Global Search**: Search across all accessible boards
- **Board-specific Search**: Limit search to current board context
- **Rich Results**: Display cards with descriptions and metadata
- **Quick Actions**: Open cards directly in Trello web interface

#### Settings Tab
- **Connection Management**: Add, edit, and test connection profiles
- **Profile Switching**: Quick switching between different Trello accounts
- **Environment Detection**: Automatic detection of environment-based credentials

### Card Interface Features

#### Card Display
- **Interactive Cards**: Click to open in browser, double-click for quick access
- **Rich Metadata**: Comments, attachments, due dates with visual indicators
- **Color Coding**: Label-based visual organization
- **Hover Details**: Card descriptions shown on hover

#### Board Navigation
- **List Organization**: Cards grouped by their respective lists
- **Visual Stats**: Quick overview of board activity and structure
- **Drag & Drop**: Future support for card movement between lists

## API Integration

### Supported Operations

#### Boards
- List user boards
- Get board details with lists, cards, and members
- Create new boards
- Update board properties

#### Lists
- Get board lists
- Create new lists
- Update list properties
- Archive lists

#### Cards
- Get board/list cards
- Search cards with optional board filtering
- Create new cards with descriptions and positioning
- Update card properties
- Move cards between lists
- Delete cards

#### Members & Labels
- Get board members
- Get board labels
- Rich metadata for enhanced UI experience

### Asynchronous Architecture

All Trello operations are implemented asynchronously to maintain UI responsiveness:

- **Non-blocking UI**: Interface remains responsive during API calls
- **Progress Indicators**: Visual feedback for long-running operations
- **Error Handling**: Graceful degradation with user-friendly error messages
- **Caching Strategy**: Intelligent caching to minimize API calls

## Configuration Files

### Connection Profiles
Profiles are stored in the user's configuration directory:
```
~/.config/rouen/trello_profiles.json
```

### Environment Variables
Rouen automatically detects these environment variables:
- `TRELLO_API_KEY`: Your Trello API key
- `TRELLO_TOKEN`: Your Trello authentication token

## Security Considerations

- **Local Storage**: API credentials stored locally, never transmitted to third parties
- **Secure Connections**: All API calls use HTTPS
- **Token Scope**: Use tokens with minimal required permissions
- **Profile Encryption**: Consider using environment variables for sensitive deployments

## Troubleshooting

### Connection Issues
1. Verify API key and token are correct
2. Check internet connectivity
3. Ensure token has required permissions
4. Test connection using the built-in connection test

### Performance
- Large boards may take longer to load
- Search operations are optimized but depend on board size
- Consider using board-specific cards for better performance

### Common Errors
- **"Not connected"**: Check authentication credentials
- **"Failed to get boards"**: Verify API key and token permissions
- **"Failed to create card"**: Ensure list and board IDs are valid

## Development Integration

### Card Factory Registration
Trello cards are registered in the factory system via `trello_registrar.cpp`:

```cpp
// Register the main Trello card
dict["trello"] = [](std::string_view, SDL_Renderer*) {
    return std::make_shared<rouen::cards::trello_card>();
};

// Register Trello board viewer with board ID support
dict["trello-board"] = [](std::string_view board_id, SDL_Renderer*) -> card::ptr {
    return std::make_shared<rouen::cards::trello_card>(std::string(board_id));
};
```

### URI Resolution
The card's `get_uri()` method dynamically returns the appropriate schema:

```cpp
std::string get_uri() const override { 
    return initial_board_id_.empty() ? "trello" : "trello-board:" + initial_board_id_; 
}
```

## Related Files

### Core Implementation
- `src/cards/productivity/trello_card.hpp` - Main card interface
- `src/cards/productivity/trello_card.cpp` - Card implementation
- `src/cards/productivity/trello_registrar.cpp` - Factory registration

### Model & API
- `src/models/trello_model.hpp` - Trello API model definitions
- `src/models/trello_model.cpp` - API implementation
- `src/hosts/trello_host.hpp` - Host interface abstraction
- `src/hosts/trello_host.cpp` - Host implementation

### Dependencies
- `src/helpers/api_keys.hpp` - API key management
- `src/helpers/fetch.hpp` - HTTP client functionality
- `external/glaze/` - JSON serialization/deserialization

## Examples

### Creating a Project Management Workspace
1. Create a general Trello card: `trello:`
2. Create specific board cards for active projects: `trello-board:<project_board_id>`
3. Use the search functionality to track cards across projects
4. Set up different connection profiles for work and personal accounts

### Board-Specific Workflows
For teams working on specific projects, create dedicated board cards:
```
trello-board:5f8b9c1a2d3e4f5g6h7i8j9k  # Development Board
trello-board:1a2b3c4d5e6f7g8h9i0j1k2l  # Marketing Board  
trello-board:9z8y7x6w5v4u3t2s1r0q9p8o  # Support Board
```

This provides focused interfaces for each team while maintaining the ability to search globally with the general `trello:` card.
