#pragma once
#include <string>
#include <map>
#include "BaseMicroservice.hpp"

/**
* @class AlarmSystem
* @brief A professional alarm microservice that manages configurable tones and plays real audio alerts.
*
* The AlarmSystem allows mapping events (e.g., intrusion, fire, access) to specific tones,
* adjusting playback volume and duration, and triggering alarms with SDL2_mixer.
* It inherits from BaseMicroservice to integrate with the microservice architecture.
*/
class AlarmSystem : public BaseMicroservice {
public:
    /**
    * @brief Construct a new AlarmSystem with default configuration.
    *
    * Initializes default volume, duration, tone mappings, and SDL audio.
    */
    AlarmSystem();

    /**
    * @brief Destroy the AlarmSystem and release audio resources.
    */
    ~AlarmSystem();

    // --- Config setters ---
    void setVolume(int level);                  ///< Set playback volume [0–10].
    void setDuration(int seconds);              ///< Set playback duration in seconds.
    void setDefaultTone(const std::string& tone); ///< Set the default tone name.
    void setTone(const std::string& toneName, const std::string& filePath); ///< Register/update tone file path.
    void assignTone(const std::string& event, const std::string& toneName); ///< Assign tone to event.

    // --- Config getters ---
    int getVolume() const;                      ///< Get current volume level.
    int getDuration() const;                    ///< Get playback duration.
    std::string getDefaultTone() const;         ///< Get default tone name.
    std::string getTone(const std::string& toneName) const; ///< Get file path of tone.
    std::string getAssignedTone(const std::string& event) const; ///< Get tone assigned to event.

    // --- Config reset ---
    void resetConfig();                         ///< Reset configuration to defaults.

    // --- Alarm actions ---
    void triggerAlarm(const std::string& event); ///< Trigger alarm for event.
    void playTone(const std::string& toneName);  ///< Play tone by name.

private:
    int volume_;                                ///< Current volume level [0–10].
    int duration_;                              ///< Playback duration in seconds.
    std::string defaultTone_;                   ///< Default tone name.
    std::map<std::string, std::string> tones_;  ///< Map of tone names to file paths.
    std::map<std::string, std::string> eventToneMap_; ///< Map of events to tone names.

    bool audioReady_ = false;                   ///< True if SDL audio initialized successfully.
};
