/*
    EqualizerAPO macOS port
    Copyright (C) 2026

    This file is part of a GPL-2.0 project. See the repository LICENSE.
*/

#pragma once

#include <memory>
#include <string>

namespace eapo {

class DspEngine;

class AudioHost {
public:
    class Impl;

    AudioHost();
    ~AudioHost();

    AudioHost(const AudioHost&) = delete;
    AudioHost& operator=(const AudioHost&) = delete;

    bool start(const std::string& configPath,
               const std::string& inputDeviceName,
               const std::string& outputDeviceName,
               std::string& error);
    void stop();
    bool running() const;

    const DspEngine* engine() const;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace eapo