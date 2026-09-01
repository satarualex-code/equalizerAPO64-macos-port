/*
    EqualizerAPO macOS port
    Copyright (C) 2026

    This file is part of a GPL-2.0 project. See the repository LICENSE.
*/

#include "eapo/audio_host.h"
#include "eapo/dsp_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace eapo {

class AudioHost::Impl {
public:
    Impl()
        : engine(48000.0, 2)
#if defined(__APPLE__)
          ,
          inputUnit(nullptr),
          outputUnit(nullptr)
#endif
    {}

    DspEngine engine;
    bool isRunning = false;
#if defined(__APPLE__)
    AudioUnit inputUnit;
    AudioUnit outputUnit;
    std::vector<float> scratch;
#endif
};

#if defined(__APPLE__)
namespace {

OSStatus renderCallback(void* refCon, AudioUnitRenderActionFlags*,
                        const AudioTimeStamp* timeStamp, UInt32, UInt32 frameCount,
                        AudioBufferList* outputData) {
    auto* impl = static_cast<AudioHost::Impl*>(refCon);
    if (outputData == nullptr || outputData->mNumberBuffers == 0) {
        return noErr;
    }

    const unsigned channels = std::max(impl->engine.channelCount(), 1U);
    const std::size_t samples = static_cast<std::size_t>(frameCount) * channels;
    if (impl->scratch.size() < samples) {
        for (UInt32 buffer = 0; buffer < outputData->mNumberBuffers; ++buffer) {
            if (outputData->mBuffers[buffer].mData != nullptr) {
                std::memset(outputData->mBuffers[buffer].mData, 0,
                            outputData->mBuffers[buffer].mDataByteSize);
            }
        }
        return kAudio_ParamError;
    }

    AudioBufferList inputData{};
    inputData.mNumberBuffers = 1;
    inputData.mBuffers[0].mNumberChannels = channels;
    inputData.mBuffers[0].mDataByteSize = static_cast<UInt32>(samples * sizeof(float));
    inputData.mBuffers[0].mData = impl->scratch.data();

    const OSStatus renderStatus = AudioUnitRender(
        impl->inputUnit, nullptr, timeStamp, 1, frameCount, &inputData);
    if (renderStatus != noErr) {
        for (UInt32 buffer = 0; buffer < outputData->mNumberBuffers; ++buffer) {
            if (outputData->mBuffers[buffer].mData != nullptr) {
                std::memset(outputData->mBuffers[buffer].mData, 0,
                            outputData->mBuffers[buffer].mDataByteSize);
            }
        }
        return renderStatus;
    }

    impl->engine.process(impl->scratch.data(), frameCount);

    if (outputData->mNumberBuffers == 1) {
        const std::size_t bytes = std::min<std::size_t>(
            outputData->mBuffers[0].mDataByteSize, samples * sizeof(float));
        if (outputData->mBuffers[0].mData != nullptr) {
            std::memcpy(outputData->mBuffers[0].mData, impl->scratch.data(), bytes);
        }
        return noErr;
    }

    for (UInt32 channel = 0; channel < outputData->mNumberBuffers && channel < channels; ++channel) {
        auto* destination = static_cast<float*>(outputData->mBuffers[channel].mData);
        if (destination == nullptr) {
            continue;
        }
        const auto destinationFrames = outputData->mBuffers[channel].mDataByteSize / sizeof(float);
        const auto framesToCopy = std::min<std::size_t>(destinationFrames, frameCount);
        for (std::size_t frame = 0; frame < framesToCopy; ++frame) {
            destination[frame] = impl->scratch[frame * channels + channel];
        }
    }
    return noErr;
}

bool setAudioFormat(AudioUnit unit, AudioStreamBasicDescription& format, AudioUnitScope scope,
                    AudioUnitElement element) {
    return AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, scope, element,
                                &format, sizeof(format)) == noErr;
}

bool setCurrentDevice(AudioUnit unit, AudioDeviceID device) {
    return AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 0, &device, sizeof(device)) == noErr;
}

std::string deviceName(AudioDeviceID device) {
    AudioObjectPropertyAddress address{
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    CFStringRef name = nullptr;
    UInt32 size = sizeof(name);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &name) != noErr ||
        name == nullptr) {
        return {};
    }

    char buffer[512] = {};
    const bool converted = CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(name);
    return converted ? std::string(buffer) : std::string();
}

AudioDeviceID findDevice(const std::string& requestedName, bool input) {
    AudioObjectPropertyAddress address{
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return kAudioObjectUnknown;
    }
    std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
                                   devices.data()) != noErr) {
        return kAudioObjectUnknown;
    }

    const AudioDeviceID defaultDevice = [&]() {
        AudioObjectPropertyAddress defaultAddress{
            input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain,
        };
        AudioDeviceID value = kAudioObjectUnknown;
        UInt32 valueSize = sizeof(value);
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddress, 0, nullptr,
                                   &valueSize, &value);
        return value;
    }();

    if (requestedName.empty()) {
        return defaultDevice;
    }
    for (const auto device : devices) {
        if (deviceName(device) == requestedName) {
            return device;
        }
    }
    return kAudioObjectUnknown;
}

}  // namespace
#endif

AudioHost::AudioHost() : impl_(std::make_unique<Impl>()) {}

AudioHost::~AudioHost() {
    stop();
}

bool AudioHost::start(const std::string& configPath,
                      const std::string& inputDeviceName,
                      const std::string& outputDeviceName,
                      std::string& error) {
    if (impl_->isRunning) {
        error = "The audio host is already running.";
        return false;
    }

#if !defined(__APPLE__)
    (void)configPath;
    (void)inputDeviceName;
    (void)outputDeviceName;
    error = "The real-time Core Audio host can only run on macOS. The DSP library itself is portable.";
    return false;
#else
    try {
        const AudioDeviceID inputDevice = findDevice(inputDeviceName, true);
        const AudioDeviceID outputDevice = findDevice(outputDeviceName, false);
        if (inputDevice == kAudioObjectUnknown) {
            error = inputDeviceName.empty() ? "No default macOS input device was found."
                                            : "Input device not found: " + inputDeviceName;
            return false;
        }
        if (outputDevice == kAudioObjectUnknown) {
            error = outputDeviceName.empty() ? "No default macOS output device was found."
                                             : "Output device not found: " + outputDeviceName;
            return false;
        }

        AudioComponentDescription description{};
        description.componentType = kAudioUnitType_Output;
        description.componentSubType = kAudioUnitSubType_HALOutput;
        description.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (component == nullptr ||
            AudioComponentInstanceNew(component, &impl_->inputUnit) != noErr ||
            AudioComponentInstanceNew(component, &impl_->outputUnit) != noErr) {
            error = "Could not create the macOS HAL audio unit.";
            stop();
            return false;
        }

        UInt32 enabled = 1;
        UInt32 disabled = 0;
        if (AudioUnitSetProperty(impl_->inputUnit, kAudioOutputUnitProperty_EnableIO,
                                 kAudioUnitScope_Input, 1, &enabled, sizeof(enabled)) != noErr) {
            error = "Could not enable the Core Audio input bus.";
            stop();
            return false;
        }
        if (AudioUnitSetProperty(impl_->inputUnit, kAudioOutputUnitProperty_EnableIO,
                                 kAudioUnitScope_Output, 0, &disabled, sizeof(disabled)) != noErr ||
            AudioUnitSetProperty(impl_->outputUnit, kAudioOutputUnitProperty_EnableIO,
                                 kAudioUnitScope_Input, 1, &disabled, sizeof(disabled)) != noErr ||
            AudioUnitSetProperty(impl_->outputUnit, kAudioOutputUnitProperty_EnableIO,
                                 kAudioUnitScope_Output, 0, &enabled, sizeof(enabled)) != noErr) {
            error = "Could not configure the Core Audio input/output buses.";
            stop();
            return false;
        }
        if (!setCurrentDevice(impl_->inputUnit, inputDevice) ||
            !setCurrentDevice(impl_->outputUnit, outputDevice)) {
            error = "Could not select the requested Core Audio devices.";
            stop();
            return false;
        }

        AudioStreamBasicDescription inputFormat{};
        AudioStreamBasicDescription outputFormat{};
        UInt32 inputFormatSize = sizeof(inputFormat);
        UInt32 outputFormatSize = sizeof(outputFormat);
        if (AudioUnitGetProperty(impl_->inputUnit, kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output, 1, &inputFormat, &inputFormatSize) != noErr ||
            AudioUnitGetProperty(impl_->outputUnit, kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Input, 0, &outputFormat, &outputFormatSize) != noErr) {
            error = "Could not read the Core Audio input format.";
            stop();
            return false;
        }
        if (std::abs(inputFormat.mSampleRate - outputFormat.mSampleRate) > 1.0 ||
            inputFormat.mChannelsPerFrame != outputFormat.mChannelsPerFrame) {
            error = "Input and output devices must use the same sample rate and channel count.";
            stop();
            return false;
        }

        auto makeFloatFormat = [](const AudioStreamBasicDescription& source) {
            AudioStreamBasicDescription format = source;
            format.mFormatID = kAudioFormatLinearPCM;
            format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
            format.mFramesPerPacket = 1;
            format.mBitsPerChannel = 32;
            format.mBytesPerFrame = sizeof(float) * format.mChannelsPerFrame;
            format.mBytesPerPacket = format.mBytesPerFrame;
            return format;
        };
        inputFormat = makeFloatFormat(inputFormat);
        outputFormat = makeFloatFormat(outputFormat);

        if (!setAudioFormat(impl_->inputUnit, inputFormat, kAudioUnitScope_Output, 1) ||
            !setAudioFormat(impl_->outputUnit, outputFormat, kAudioUnitScope_Input, 0)) {
            error = "Could not configure the Core Audio stream format.";
            stop();
            return false;
        }

        impl_->engine.configure(inputFormat.mSampleRate, inputFormat.mChannelsPerFrame);
        impl_->scratch.assign(16384ULL * impl_->engine.channelCount(), 0.0f);
        const ConfigReport report = impl_->engine.loadConfig(configPath);
        for (const auto& warning : report.warnings) {
            std::fprintf(stderr, "warning: %s\n", warning.c_str());
        }

        AURenderCallbackStruct callback{};
        callback.inputProc = renderCallback;
        callback.inputProcRefCon = impl_.get();
        if (AudioUnitSetProperty(impl_->outputUnit, kAudioUnitProperty_SetRenderCallback,
                                 kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr) {
            error = "Could not install the Core Audio render callback.";
            stop();
            return false;
        }

        if (AudioUnitInitialize(impl_->inputUnit) != noErr ||
            AudioUnitInitialize(impl_->outputUnit) != noErr ||
            AudioOutputUnitStart(impl_->inputUnit) != noErr ||
            AudioOutputUnitStart(impl_->outputUnit) != noErr) {
            error = "Could not start the Core Audio stream. Check microphone/audio permissions and devices.";
            stop();
            return false;
        }

        impl_->isRunning = true;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        stop();
        return false;
    }
#endif
}

void AudioHost::stop() {
#if defined(__APPLE__)
    if (impl_->outputUnit != nullptr && impl_->isRunning) {
        AudioOutputUnitStop(impl_->outputUnit);
    }
    if (impl_->inputUnit != nullptr && impl_->isRunning) {
        AudioOutputUnitStop(impl_->inputUnit);
    }
    impl_->isRunning = false;
    if (impl_->inputUnit != nullptr) {
        AudioUnitUninitialize(impl_->inputUnit);
        AudioComponentInstanceDispose(impl_->inputUnit);
        impl_->inputUnit = nullptr;
    }
    if (impl_->outputUnit != nullptr) {
        AudioUnitUninitialize(impl_->outputUnit);
        AudioComponentInstanceDispose(impl_->outputUnit);
        impl_->outputUnit = nullptr;
    }
#else
    impl_->isRunning = false;
#endif
}

bool AudioHost::running() const {
    return impl_->isRunning;
}

const DspEngine* AudioHost::engine() const {
    return &impl_->engine;
}

}  // namespace eapo