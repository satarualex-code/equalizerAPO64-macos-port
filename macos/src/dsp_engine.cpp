/*
    EqualizerAPO macOS port
    Copyright (C) 2026

    This file is part of a GPL-2.0 project. See the repository LICENSE.
*/

#include "eapo/dsp_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace eapo {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool readNumber(const std::string& text, const std::string& label, double& result) {
    const std::regex expression("\\b" + label + "\\s+([-+0-9.eE]+)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(text, match, expression)) {
        return false;
    }
    try {
        result = std::stod(match[1].str());
        return std::isfinite(result);
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

struct DspEngine::Biquad {
    enum class Kind {
        Peaking,
        LowShelf,
        HighShelf,
        LowPass,
        HighPass,
    };

    Kind kind = Kind::Peaking;
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    std::vector<double> z1;
    std::vector<double> z2;

    void configure(Kind filterKind, double sampleRate, unsigned channels,
                   double frequency, double gainDb, double q) {
        kind = filterKind;
        const double safeFrequency = std::clamp(frequency, 1.0, sampleRate * 0.49);
        const double safeQ = std::max(q, 0.01);
        const double omega = 2.0 * kPi * safeFrequency / sampleRate;
        const double sine = std::sin(omega);
        const double cosine = std::cos(omega);
        const double alpha = sine / (2.0 * safeQ);
        const double amplitude = std::pow(10.0, gainDb / 40.0);

        double rawB0;
        double rawB1;
        double rawB2;
        double rawA0;
        double rawA1;
        double rawA2;

        switch (kind) {
        case Kind::Peaking:
            rawB0 = 1.0 + alpha * amplitude;
            rawB1 = -2.0 * cosine;
            rawB2 = 1.0 - alpha * amplitude;
            rawA0 = 1.0 + alpha / amplitude;
            rawA1 = -2.0 * cosine;
            rawA2 = 1.0 - alpha / amplitude;
            break;
        case Kind::LowShelf: {
            const double twoRoot = 2.0 * std::sqrt(amplitude) * alpha;
            rawB0 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine + twoRoot);
            rawB1 = 2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cosine);
            rawB2 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine - twoRoot);
            rawA0 = (amplitude + 1.0) + (amplitude - 1.0) * cosine + twoRoot;
            rawA1 = -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cosine);
            rawA2 = (amplitude + 1.0) + (amplitude - 1.0) * cosine - twoRoot;
            break;
        }
        case Kind::HighShelf: {
            const double twoRoot = 2.0 * std::sqrt(amplitude) * alpha;
            rawB0 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine + twoRoot);
            rawB1 = -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosine);
            rawB2 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine - twoRoot);
            rawA0 = (amplitude + 1.0) - (amplitude - 1.0) * cosine + twoRoot;
            rawA1 = 2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosine);
            rawA2 = (amplitude + 1.0) - (amplitude - 1.0) * cosine - twoRoot;
            break;
        }
        case Kind::LowPass:
            rawB0 = (1.0 - cosine) / 2.0;
            rawB1 = 1.0 - cosine;
            rawB2 = rawB0;
            rawA0 = 1.0 + alpha;
            rawA1 = -2.0 * cosine;
            rawA2 = 1.0 - alpha;
            break;
        case Kind::HighPass:
            rawB0 = (1.0 + cosine) / 2.0;
            rawB1 = -(1.0 + cosine);
            rawB2 = rawB0;
            rawA0 = 1.0 + alpha;
            rawA1 = -2.0 * cosine;
            rawA2 = 1.0 - alpha;
            break;
        }

        b0 = rawB0 / rawA0;
        b1 = rawB1 / rawA0;
        b2 = rawB2 / rawA0;
        a1 = rawA1 / rawA0;
        a2 = rawA2 / rawA0;
        z1.assign(channels, 0.0);
        z2.assign(channels, 0.0);
    }

    void process(float* samples, std::size_t frames, unsigned channels) {
        for (unsigned channel = 0; channel < channels; ++channel) {
            double state1 = z1[channel];
            double state2 = z2[channel];
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const std::size_t index = frame * channels + channel;
                const double input = static_cast<double>(samples[index]);
                const double output = b0 * input + state1;
                state1 = b1 * input - a1 * output + state2;
                state2 = b2 * input - a2 * output;
                samples[index] = static_cast<float>(output);
            }
            z1[channel] = state1;
            z2[channel] = state2;
        }
    }
};

DspEngine::DspEngine(double sampleRate, unsigned channelCount)
    : sampleRate_(sampleRate), channelCount_(std::max(channelCount, 1U)), preampDb_(0.0) {}

DspEngine::~DspEngine() = default;

std::size_t DspEngine::filterCount() const {
    return filters_.size();
}

void DspEngine::configure(double sampleRate, unsigned channelCount) {
    sampleRate_ = std::max(sampleRate, 8000.0);
    channelCount_ = std::max(channelCount, 1U);
    reset();
}

void DspEngine::reset() {
    for (auto& filter : filters_) {
        filter.z1.assign(channelCount_, 0.0);
        filter.z2.assign(channelCount_, 0.0);
    }
}

void DspEngine::addBiquad(const std::string& type, double frequency, double gainDb, double q) {
    const std::string normalizedType = lower(type);
    Biquad::Kind kind;
    if (normalizedType == "pk" || normalizedType == "peaking") {
        kind = Biquad::Kind::Peaking;
    } else if (normalizedType == "ls" || normalizedType == "lowshelf") {
        kind = Biquad::Kind::LowShelf;
    } else if (normalizedType == "hs" || normalizedType == "highshelf") {
        kind = Biquad::Kind::HighShelf;
    } else if (normalizedType == "lp" || normalizedType == "lowpass") {
        kind = Biquad::Kind::LowPass;
    } else if (normalizedType == "hp" || normalizedType == "highpass") {
        kind = Biquad::Kind::HighPass;
    } else {
        return;
    }

    Biquad filter;
    filter.configure(kind, sampleRate_, channelCount_, frequency, gainDb, q);
    filters_.push_back(std::move(filter));
}

void DspEngine::addGraphicEq(const std::string& value, ConfigReport& report) {
    std::stringstream bands(value);
    std::string band;
    while (std::getline(bands, band, ';')) {
        std::stringstream pair(trim(band));
        double frequency = 0.0;
        double gainDb = 0.0;
        if (!(pair >> frequency >> gainDb) || frequency <= 0.0) {
            if (!trim(band).empty()) {
                report.warnings.push_back("Could not parse GraphicEQ band: " + trim(band));
            }
            continue;
        }
        addBiquad("PK", frequency, gainDb, 0.70710678);
        ++report.filtersLoaded;
    }
}

ConfigReport DspEngine::loadConfig(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open configuration file: " + path);
    }

    filters_.clear();
    preampDb_ = 0.0;
    ConfigReport report;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            report.warnings.push_back("Line " + std::to_string(lineNumber) + " has no command separator.");
            continue;
        }

        const std::string command = lower(trim(line.substr(0, separator)));
        const std::string value = trim(line.substr(separator + 1));

        if (command == "preamp") {
            try {
                preampDb_ = std::stod(value);
            } catch (const std::exception&) {
                report.warnings.push_back("Line " + std::to_string(lineNumber) + " has an invalid Preamp value.");
            }
            continue;
        }

        if (command == "graphiceq") {
            addGraphicEq(value, report);
            continue;
        }

        if (command == "filter") {
            if (lower(value).find("off") == 0) {
                continue;
            }
            std::smatch typeMatch;
            const std::regex typeExpression("\\b(PK|LS|HS|LP|HP|Peaking|LowShelf|HighShelf|LowPass|HighPass)\\b",
                                            std::regex::icase);
            if (!std::regex_search(value, typeMatch, typeExpression)) {
                report.warnings.push_back("Line " + std::to_string(lineNumber) + " uses an unsupported filter type.");
                continue;
            }

            double frequency = 0.0;
            double gainDb = 0.0;
            double q = 0.70710678;
            if (!readNumber(value, "Fc", frequency)) {
                report.warnings.push_back("Line " + std::to_string(lineNumber) + " is missing Fc.");
                continue;
            }
            if (lower(typeMatch[1].str()) != "lp" && lower(typeMatch[1].str()) != "hp") {
                if (!readNumber(value, "Gain", gainDb)) {
                    report.warnings.push_back("Line " + std::to_string(lineNumber) + " is missing Gain.");
                    continue;
                }
            }
            readNumber(value, "Q", q);
            addBiquad(typeMatch[1].str(), frequency, gainDb, q);
            ++report.filtersLoaded;
            continue;
        }

        if (command == "include" || command == "delay" || command == "copy" ||
            command == "convolution" || command == "vstplugin" || command == "device" ||
            command == "channel" || command == "stage" || command == "if") {
            report.warnings.push_back("Line " + std::to_string(lineNumber) + " uses '" + command +
                                      "', which is not implemented by the macOS host yet.");
            continue;
        }

        report.warnings.push_back("Line " + std::to_string(lineNumber) + " uses unknown command '" + command + "'.");
    }

    reset();
    return report;
}

void DspEngine::process(float* interleavedSamples, std::size_t frameCount) {
    if (interleavedSamples == nullptr || frameCount == 0) {
        return;
    }

    const double preampGain = std::pow(10.0, preampDb_ / 20.0);
    if (preampGain != 1.0) {
        const std::size_t sampleCount = frameCount * channelCount_;
        for (std::size_t i = 0; i < sampleCount; ++i) {
            interleavedSamples[i] = static_cast<float>(static_cast<double>(interleavedSamples[i]) * preampGain);
        }
    }

    for (auto& filter : filters_) {
        filter.process(interleavedSamples, frameCount, channelCount_);
    }
}

}  // namespace eapo