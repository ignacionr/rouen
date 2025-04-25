# Rouen Host Infrastructure

## Overview

The `hosts` directory contains implementation of data hosts - components that provide access to external services and data sources. Each host is responsible for communicating with a specific external API or service and providing data to the Rouen application.

## Host Components

### RSS Host
Located in `rss_host.hpp`, this component manages the connection to RSS feeds. It likely handles:
- Fetching RSS feeds from external sources
- Parsing and normalizing feed content
- Caching content to improve performance
- Exposing feed data to RSS-related cards

### Travel Host
Located in `travel_host.hpp`, this component manages travel-related data. It likely handles:
- Interfacing with travel APIs or services
- Managing travel plans and destinations
- Handling travel-related data persistence
- Providing data to the travel planner card

### Weather Host
Located in `weather_host.hpp`, this component manages weather data. It likely handles:
- Connecting to weather service APIs
- Fetching and parsing weather forecasts
- Caching weather information
- Providing weather data to relevant cards

## Using Hosts

Hosts are typically used by card components to retrieve and display data. They abstract away the details of external service communication, allowing cards to focus on presentation and user interaction.

### Typical Usage Pattern

1. A card component requests data from a host
2. The host retrieves data (from cache or external source)
3. The host processes and normalizes the data
4. The card presents the data to the user

## Creating a New Host

When creating a new host:

1. Create a new header file in the `hosts` directory
2. Implement the host class with methods for data retrieval and management
3. Consider implementing caching for performance
4. Provide a clean API for cards to consume

For integration with external services, hosts should handle:
- Authentication and authorization
- Rate limiting and request throttling
- Error handling and recovery
- Data caching and invalidation