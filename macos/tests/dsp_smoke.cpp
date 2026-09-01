#include "eapo/dsp_engine.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main() {
    const std::string path = "eapo-dsp-smoke-config.txt";
    {
        std::ofstream config(path);
        config << "Preamp: -6 dB\n"
               << "Filter: ON PK Fc 1000 Hz Gain 3 dB Q 1\n";
    }

    eapo::DspEngine engine(48000.0, 2);
    const auto report = engine.loadConfig(path);
    std::remove(path.c_str());
    if (report.filtersLoaded != 1 || engine.filterCount() != 1) {
        return 1;
    }

    std::vector<float> samples(480 * 2, 0.0f);
    samples[0] = 1.0f;
    engine.process(samples.data(), 480);
    for (float sample : samples) {
        if (!std::isfinite(sample)) {
            return 2;
        }
    }
    return 0;
}