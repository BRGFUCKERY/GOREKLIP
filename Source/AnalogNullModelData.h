#pragma once
#include <cstdint>

namespace AnalogNullModelData
{
    static constexpr int kTaps = %d;
    static constexpr int kLutSize = 65536;

    struct Model
    {
        const float* pre;   // kTaps
        float xmin;
        float xmax;
        const int16_t* lut; // kLutSize, Q15 (scale 32767)
    };

    // Model tables: silk = {0, 50, 100}, channel = {0=L, 1=R}
    const Model& getModel(int silkPercent, int channel);
}
