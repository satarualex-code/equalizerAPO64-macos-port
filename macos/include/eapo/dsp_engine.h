/*
    EqualizerAPO macOS port
    Copyright (C) 2026

    This file is part of a GPL-2.0 project. See the repository LICENSE.
*/

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace eapo {

struct ConfigReport {
    std::size_t filtersLoaded = 0;
    std::vector<std::string> warnings;
};

class DspEngine {
public:
    DspEngine(double sampleRate = 48000.0, unsigned channelCount = 2);
    ~DspEngine();

    void configure(double sampleRate, unsigned channelCount);
    void reset();

    ConfigReport loadConfig(const std::string& path);
    void process(float* interleavedSamples, std::size_t frameCount);

    double sampleRate() const { return sampleRate_; }
    unsigned channelCount() const { return channelCount_; }
    std::size_t filterCount() const;
    double preampDb() const { return preampDb_; }

private:
    struct Biquad;

    void addBiquad(const std::string& type, double frequency, double gainDb, double q);
    void addGraphicEq(const std::string& value, ConfigReport& report);

    double sampleRate_;
    unsigned channelCount_;
    double preampDb_;
    std::vector<Biquad> filters_;
};

}  // namespace eapo