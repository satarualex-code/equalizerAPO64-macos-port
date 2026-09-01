# EqualizerAPO on macOS

This directory adds a native macOS audio host while keeping the existing
Windows APO implementation intact.

## What changed

EqualizerAPO itself is a Windows Audio Processing Object. macOS has no
compatible APO or registry-based device hook, so the Windows DLL cannot be
recompiled for macOS. The port is split into two parts:

1. `macos/src/dsp_engine.cpp` is a portable double-precision processing core.
   It reads the familiar text configuration format and supports `Preamp`,
   `GraphicEQ`, and common `Filter` biquads (`PK`, `LS`, `HS`, `LP`, and `HP`).
2. `macos/src/audio_host.cpp` uses Apple's HAL Output Audio Unit to read from
   the current input device, process audio in real time, and write to the
   current output device.

Unsupported Windows-only directives are reported as warnings. They are not
silently treated as working.

## Build on a Mac

Install Xcode Command Line Tools and CMake, then run:

```sh
cmake -S macos -B macos/build -DCMAKE_BUILD_TYPE=Release
cmake --build macos/build --config Release
```

The executable is `macos/build/eapo-macos`.

The same build also creates a native double-clickable application bundle:

```text
macos/build/eapo-macos-app.app
```

Open `eapo-macos-app.app` from Finder. It creates a starter configuration at
`~/Library/Application Support/EqualizerAPO/mac/config.txt` on first launch.
Use **Browse…** to choose another configuration, then enter the names of the
virtual input device and physical output device and click **Start Equalizer**.

## System-wide playback routing

macOS does not let a normal application intercept every application's output
without a virtual audio device. To use this host system-wide:

1. Install a Core Audio virtual device such as BlackHole 2ch separately.
2. Route system output into that virtual device with Audio MIDI Setup.
3. Start the host with the virtual device as its input and the physical
   speakers/headphones as its output.
4. Start the host with a config file:

```sh
./macos/build/eapo-macos \
  --input-device "BlackHole 2ch" \
  --output-device "MacBook Pro Speakers" \
  --config "$HOME/Library/Application Support/EqualizerAPO/mac/config.txt"
```

Set macOS system output to the virtual device itself (not a multi-output
device that also contains the physical speakers). The host then receives the
system mix from the virtual input, applies the filters, and sends the result
to the physical output. Input and output devices must use the same sample rate
and channel count.

This first macOS milestone intentionally uses the supported user-space Core
Audio path. A distributable driver that creates its own virtual device would
be a separate AudioServerPlugIn project and requires additional installation,
signing, and permissions work.

## Configuration notes

The original Windows editor is still Windows-specific. On macOS, copy
`macos/config.example.txt` to the application-support path above and edit it
as a text file. Existing common `Preamp`, `GraphicEQ`, and biquad filter lines
can be reused without changes.