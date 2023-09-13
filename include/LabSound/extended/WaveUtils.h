//
//  WaveUtils.h
//
//  Created by Nigel Redmon on 2/18/13
//
//

#ifndef WaveUtils_h
#define WaveUtils_h
#include <memory>

#include "WaveTableOsc.h"

int fillTables(WaveTableOsc * osc, double * freqWaveRe, double * freqWaveIm, int numSamples);
int fillTables2(WaveTableOsc * osc, double * freqWaveRe, double * freqWaveIm, int numSamples, double minTop = 0.4, double maxTop = 0);
float makeWaveTable(WaveTableOsc * osc, int len, double * ar, double * ai, double scale, double topFreq);

// examples
std::shared_ptr<WaveTableOsc> sawOsc(void);
std::shared_ptr<WaveTableOsc> sinOsc(void);
std::shared_ptr<WaveTableOsc> squareOsc(void);
std::shared_ptr<WaveTableOsc> triangleOsc(void);
std::shared_ptr<WaveTableOsc> richTriangleOsc(void);
std::shared_ptr<WaveTableOsc> noiseOsc(void);
    std::shared_ptr<WaveTableOsc> convertFromWebAudio(float * webReal, float * webImag, int webLength);
    WaveTableOsc * waveOsc(double * waveSamples, int tableLen);

#endif
