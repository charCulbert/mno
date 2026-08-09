#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MNOModulationState
{
    float lfoPhase;
    float lfoValue;
    float adsrValue;
    int32_t adsrActive;
    int32_t adsrStage;
    float adsrStageProgress;
    float adsrVelocityScale;
    float lfo2Phase;
    float lfo2Value;
    float adsr2Value;
    int32_t adsr2Active;
    int32_t adsr2Stage;
    float adsr2StageProgress;
    float osc1Shape;
    float osc2Shape;
    float filterCutoff;
    float filterResonance;
} MNOModulationState;

#ifdef __cplusplus
}
#endif
