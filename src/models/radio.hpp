#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "../helpers/media_player.hpp"
#include "../helpers/debug.hpp"

namespace rouen::models {
    // Radio station status enum
    enum class RadioStatus {
        Stopped,
        Playing,
        Error
    };

    // Radio station struct
    struct RadioStation {
        std::string name;
        std::string url;
        RadioStatus status = RadioStatus::Stopped;
    };

    class radio {
    public:
        radio() {
            loadPresets();
        }

        ~radio() {
            stopCurrentStation();
        }

        /**
         * Load radio station presets from presets.txt file
         */
        void loadPresets() {
            stations.clear();
            station_names.clear();
            
            try {
                // Read presets file
                std::ifstream presets_file("presets.txt");
                if (!presets_file.is_open()) {
                    RADIO_ERROR("Failed to open presets.txt");
                    return;
                }
                
                std::string line;
                while (std::getline(presets_file, line)) {
                    // Skip empty lines or comments
                    if (line.empty() || line[0] == '#' || line[0] == '/') {
                        continue;
                    }
                    
                    // Parse "Name=URL" format
                    size_t delimiter_pos = line.find('=');
                    if (delimiter_pos != std::string::npos) {
                        std::string name = line.substr(0, delimiter_pos);
                        std::string url = line.substr(delimiter_pos + 1);
                        
                        // Add to stations map and names vector
                        RadioStation station{name, url, RadioStatus::Stopped};
                        stations[name] = station;
                        station_names.push_back(name);
                    }
                }
                
                // Sort station names alphabetically for display
                std::sort(station_names.begin(), station_names.end());
                
            } catch (const std::exception& e) {
                RADIO_ERROR_FMT("Error loading radio presets: {}", e.what());
            }
        }

        /**
         * Get list of station names
         * 
         * @return Vector of station names
         */
        const std::vector<std::string>& getStationNames() const {
            return station_names;
        }

        /**
         * Get station by name
         * 
         * @param name Station name
         * @return Pointer to RadioStation or nullptr if not found
         */
        const RadioStation* getStation(const std::string& name) const {
            auto it = stations.find(name);
            if (it != stations.end()) {
                return &(it->second);
            }
            return nullptr;
        }

        /**
         * Play a radio station by name
         * 
         * @param name Station name
         * @return true if successful, false if failed
         */
        bool playStation(const std::string& name) {
            // Stop any currently playing station
            stopCurrentStation();
            
            // Find the station
            auto it = stations.find(name);
            if (it == stations.end()) {
                return false;
            }
            
            // Use media_player to play the station
            media_item.url = it->second.url;
            if (media_item.playMedia()) {
                // Update station status
                it->second.status = RadioStatus::Playing;
                current_station = name;
                return true;
            } else {
                it->second.status = RadioStatus::Error;
                return false;
            }
        }

        /**
         * Stop the currently playing station
         * 
         * @return true if successful, false if no station was playing
         */
        bool stopCurrentStation() {
            if (current_station.empty()) {
                return false;
            }
            
            // Use media_player to stop playback
            media_item.stopMedia();
            
            // Update station status
            auto it = stations.find(current_station);
            if (it != stations.end()) {
                it->second.status = RadioStatus::Stopped;
            }
            
            current_station.clear();
            return true;
        }

        /**
         * Get the name of the currently playing station
         * 
         * @return Name of the current station or empty string if none is playing
         */
        const std::string& getCurrentStation() const {
            return current_station;
        }

        /**
         * Check if a station is currently playing
         *
         * @return true if a station is playing, false otherwise
         */
        bool isPlaying() {
            if (current_station.empty()) {
                return false;
            }
            return media_item.checkMediaStatus();
        }

    private:
        std::map<std::string, RadioStation> stations;
        std::vector<std::string> station_names;
        std::string current_station;
        media_player::item media_item; // Use the media_player helper
    };
}