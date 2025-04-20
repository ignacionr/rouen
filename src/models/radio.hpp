#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "../helpers/process_helper.hpp"

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
                    std::cerr << "Failed to open presets.txt" << std::endl;
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
                std::cerr << "Error loading radio presets: " << e.what() << std::endl;
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
            
            // Start playing the station using mpv player in the background
            try {
                // Use system() with nohup to properly detach the process
                std::string command = "nohup mpv --no-video \"" + it->second.url + "\" > /dev/null 2>&1 & echo $!";
                std::string result = ProcessHelper::executeCommand(command);
                
                // Store the process ID (PID) for later termination
                try {
                    player_pid = std::stoi(result);
                } catch (...) {
                    player_pid = 0;
                }
                
                // Update station status
                it->second.status = RadioStatus::Playing;
                current_station = name;
                return true;
            } catch (const std::exception& e) {
                std::cerr << "Error playing station: " << e.what() << std::endl;
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
            
            // Kill the mpv process using the stored PID if available
            if (player_pid > 0) {
                ProcessHelper::executeCommand("kill " + std::to_string(player_pid) + " 2>/dev/null || true");
                player_pid = 0;
            } else {
                // Fallback method
                ProcessHelper::executeCommand("pkill -f mpv 2>/dev/null || true");
            }
            
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

    private:
        std::map<std::string, RadioStation> stations;
        std::vector<std::string> station_names;
        std::string current_station;
        int player_pid = 0;  // Store the process ID of the running player
    };
}