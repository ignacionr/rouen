# Rouen Models Infrastructure

## Overview

The `models` directory contains data models and business logic components that support Rouen's various features. Models provide structured representations of data and handle operations on that data, serving as the foundation for the application's functionality.

## Model Structure

The models directory is organized by feature domain, with both directories and individual files:

### Directories
- **calendar/**: Models for calendar events and scheduling
- **mail/**: Email message and account models
- **rss/**: RSS feed and article models
- **travel/**: Travel planning and itinerary models

### Individual Model Files
- **git.hpp**: Git repository and version control models
- **radio.hpp**: Internet radio station and streaming models

## Model Responsibilities

Models in Rouen typically handle:

1. **Data Structure**: Define the shape and relationships of application data
2. **Business Logic**: Implement domain-specific operations and rules
3. **Data Validation**: Ensure data integrity and format compliance
4. **Persistence**: Work with SQLite databases (like `travel.db`, `rss.db`) for storage

## Integration with Other Components

Models interact with other parts of Rouen in several ways:

- **Cards**: Provide data to UI components for display
- **Hosts**: Receive data from external services via host components
- **Editors**: Supply data to be modified by text or visual editors

## Creating New Models

When creating a new model:

1. Identify which domain it belongs to (create a new directory if needed)
2. Define clear interfaces with getters/setters where appropriate
3. Consider serialization needs (for persistence or network transmission)
4. Implement business logic that's independent of UI concerns
5. Document model properties and relationships

## Best Practices

- Keep models focused on data and business logic, not presentation
- Use appropriate data structures for the problem domain
- Consider thread safety for models accessed from multiple contexts
- Design models to be testable independent of UI components
- Use proper error handling for data operations