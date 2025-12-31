


#include "AlarmSystem.hpp"
#include "utils/PathFinder.hpp"   // NEW include for cross-platform path resolution
#include <iostream>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

AlarmSystem::AlarmSystem() {
    volume_ = 5;
    duration_ = 10;
    defaultTone_ = "siren";

    // Register tone file paths using PathFinder
    tones_.clear();
    tones_["siren"]        = PathFinder::resolve("src/microservice/alarm_system/sounds/mixkit-manual-siren-fire-alert-1652.wav").string();
    tones_["beep"]         = PathFinder::resolve("src/microservice/alarm_system/sounds/mixkit-classic-alarm-995.wav").string();
    tones_["confirmation"] = PathFinder::resolve("src/microservice/alarm_system/sounds/mixkit-sci-fi-confirmation-914.wav").string();
    tones_["access"]       = PathFinder::resolve("src/microservice/alarm_system/sounds/mixkit-access-allowed-tone-2869.wav").string();

    // Consistent lowercase event keys
    eventToneMap_.clear();
    eventToneMap_["intrusion"]    = "siren";
    eventToneMap_["door_open"]    = "beep"; 
    eventToneMap_["fire"]         = "siren";
    eventToneMap_["access"]       = "access";
    eventToneMap_["warming"]      = "beep";
    eventToneMap_["confirmation"] = "confirmation";

    // Force driver if needed (optional)
    SDL_setenv("SDL_AUDIODRIVER", "directsound", 1);

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        audioReady_ = false;
    } else if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "SDL_mixer init failed: " << Mix_GetError() << std::endl;
        audioReady_ = false;
    } else {
        audioReady_ = true;
        Mix_AllocateChannels(16);
        setVolume(5);

        // Debug: list drivers
        int numDrivers = SDL_GetNumAudioDrivers();
        for (int i = 0; i < numDrivers; ++i) {
          //  std::cout << "Available driver: " << SDL_GetAudioDriver(i) << std::endl;
        }
        std::cout << "Active audio driver: "
                  << (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none")
                  << std::endl;
    }
}

AlarmSystem::~AlarmSystem() {
    Mix_CloseAudio();
    SDL_Quit();
}

void AlarmSystem::setVolume(int level) {
    if (level < 0) level = 0;
    if (level > 10) level = 10;
    volume_ = level;
}

void AlarmSystem::setDuration(int seconds) {
    if (seconds < 1) seconds = 1;
    duration_ = seconds;
}

void AlarmSystem::setDefaultTone(const std::string& tone) { defaultTone_ = tone; }
void AlarmSystem::setTone(const std::string& toneName, const std::string& filePath) { tones_[toneName] = filePath; }
void AlarmSystem::assignTone(const std::string& event, const std::string& toneName) { eventToneMap_[event] = toneName; }

int AlarmSystem::getVolume() const { return volume_; }
int AlarmSystem::getDuration() const { return duration_; }
std::string AlarmSystem::getDefaultTone() const { return defaultTone_; }
std::string AlarmSystem::getTone(const std::string& toneName) const { return tones_.at(toneName); }
std::string AlarmSystem::getAssignedTone(const std::string& event) const { return eventToneMap_.at(event); }

void AlarmSystem::resetConfig() {
    volume_ = 5;
    duration_ = 10;
    defaultTone_ = "siren";

    tones_.clear();
    tones_["siren"]        = PathFinder::resolve("src/microservice/alarm_system/sounds/mixkit-manual-siren-fire-alert-1652 (1).wav").string();
    tones_["beep"]         = PathFinder::resolve("src/microservice/alarm_system/sounds/mixkit-classic-alarm-995.wav").string();
    tones_["confirmation"] = PathFinder::resolve("src/microservice/alarm_system/sounds/mixkit-sci-fi-confirmation-914.wav").string();
    tones_["access"]       = PathFinder::resolve("src/microservice/alarm_system/sounds/mixkit-access-allowed-tone-2869.wav").string();

    eventToneMap_.clear();
    eventToneMap_["intrusion"]    = "siren";
    eventToneMap_["door_open"]    = "beep";
    eventToneMap_["fire"]         = "siren";
    eventToneMap_["access"]       = "access";
    eventToneMap_["warming"]      = "beep";
    eventToneMap_["confirmation"] = "confirmation";
}

void AlarmSystem::triggerAlarm(const std::string& event) {
    auto it = eventToneMap_.find(event);
    if (it != eventToneMap_.end()) {
        playTone(it->second);
    } else {
        playTone(defaultTone_);
    }
}

void AlarmSystem::playTone(const std::string& toneName) {
    if (!audioReady_) {
        std::cerr << "Audio system not initialized. Cannot play tone: " << toneName << std::endl;
        return;
    }

    auto it = tones_.find(toneName);
    if (it == tones_.end()) {
        std::cerr << "Tone not found: " << toneName << std::endl;
        return;
    }

    Mix_Chunk* sound = Mix_LoadWAV(it->second.c_str());
    if (!sound) {
        std::cerr << "Failed to load sound: " << it->second
                  << " (" << Mix_GetError() << ")" << std::endl;
        return;
    }

    Mix_VolumeChunk(sound, volume_ * MIX_MAX_VOLUME / 10);

    if (Mix_PlayChannelTimed(-1, sound, 0, duration_ * 1000) == -1) {
        std::cerr << "Mix_PlayChannelTimed failed: " << Mix_GetError() << std::endl;
    }

    Mix_FreeChunk(sound);
}