/*
    EqualizerAPO macOS port
    Copyright (C) 2026

    This file is part of a GPL-2.0 project. See the repository LICENSE.
*/

#include "eapo/audio_host.h"
#include "eapo/dsp_engine.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> keepRunning{true};

void printUsage(const char* executable) {
    std::cout
        << "EqualizerAPO macOS host\n\n"
        << "Usage: " << executable << " --config <path>\n\n"
        << "Options:\n"
        << "  --input-device <name>   macOS input device (default: system default)\n"
        << "  --output-device <name>  macOS output device (default: system default)\n\n"
        << "The host reads audio from the selected input device, applies the\n"
        << "configuration with double-precision internal processing, and plays it\n"
        << "to the selected output device.\n";
}

std::string defaultConfigPath() {
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return "config.txt";
    }
    return (std::filesystem::path(home) /
            "Library/Application Support/EqualizerAPO/mac/config.txt").string();
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = defaultConfigPath();
    std::string inputDeviceName;
    std::string outputDeviceName;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (argument == "--config" && index + 1 < argc) {
            configPath = argv[++index];
            continue;
        }
        if (argument == "--input-device" && index + 1 < argc) {
            inputDeviceName = argv[++index];
            continue;
        }
        if (argument == "--output-device" && index + 1 < argc) {
            outputDeviceName = argv[++index];
            continue;
        }
        std::cerr << "Unknown or incomplete argument: " << argument << "\n";
        printUsage(argv[0]);
        return 2;
    }

    eapo::AudioHost host;
    std::string error;
    if (!host.start(configPath, inputDeviceName, outputDeviceName, error)) {
        std::cerr << "Could not start EqualizerAPO macOS host: " << error << "\n";
        return 1;
    }

    const auto* engine = host.engine();
    std::cout << "EqualizerAPO macOS host is running\n"
              << "  config: " << configPath << "\n"
              << "  sample rate: " << engine->sampleRate() << " Hz\n"
              << "  channels: " << engine->channelCount() << "\n"
              << "  filters: " << engine->filterCount() << "\n"
              << "Press Ctrl-C to stop.\n";

    std::signal(SIGINT, [](int) { keepRunning = false; });
    std::signal(SIGTERM, [](int) { keepRunning = false; });
    while (keepRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    host.stop();
    return 0;
}