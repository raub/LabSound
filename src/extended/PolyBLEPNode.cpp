// License: BSD 2 Clause
// Copyright (C) 2020+, The LabSound Authors. All rights reserved.

#include "LabSound/extended/PolyBLEPNode.h"
#include "LabSound/extended/AudioContextLock.h"
#include "LabSound/extended/Registry.h"
#include "LabSound/extended/VectorMath.h"

#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioContext.h"
#include "LabSound/core/AudioNodeInput.h"
#include "LabSound/core/AudioNodeOutput.h"
#include "LabSound/core/AudioSetting.h"
#include "LabSound/core/Macros.h"

#include "internal/Assertions.h"
#include "internal/AudioUtilities.h"

#include <algorithm>

using namespace lab;

// clang-format off

// Adapted from "Phaseshaping Oscillator Algorithms for Musical Sound
// Synthesis" by Jari Kleimola, Victor Lazzarini, Joseph Timoney, and Vesa
// Valimaki. http://www.acoustics.hut.fi/publications/papers/smc2010-phaseshaping/

template <typename T>
inline int64_t bitwise_or_zero(const T & x) { return static_cast<int64_t>(x) | 0; }

template <typename T>
inline T square(const T & x) { return x * x; }

inline double blep(const double t, const double dt)
{
    if (t < dt) return -square(t / dt - 1);
    else if (t > 1 - dt)  return square((t - 1) / dt + 1);
    else return 0.0;
}

inline double blamp(double t, const double dt)
{
    if (t < dt)
    {
        t = t / dt - 1.0;
        return -1.0 / 3.0 * square(t) * t;
    }
    else if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt + 1.0;
        return 1.0 / 3.0 * square(t) * t;
    }
    else return 0.0;
}

// PolyBLEP, a quasi-bandlimited oscillator
class lab::PolyBlepImpl
{
    double half() const
    {
        double t2 = t + 0.5;
        t2 -= bitwise_or_zero(t2);

        double y = (t < 0.5 ? 2 * std::sin(LAB_TAU * t) - 2 / LAB_PI : -2 / LAB_PI);
        y += LAB_TAU * freqInSecondsPerSample * (blamp(t, freqInSecondsPerSample) + blamp(t2, freqInSecondsPerSample));

        return amplitude * y;
    }

    double full() const
    {
        double _t = this->t + 0.25;
        _t -= bitwise_or_zero(_t);

        double y = 2 * std::sin(LAB_PI * _t) - 4 / LAB_PI;
        y += LAB_TAU * freqInSecondsPerSample * blamp(_t, freqInSecondsPerSample);

        return amplitude * y;
    }

    double tri() const
    {
        double t1 = t + 0.25;
        t1 -= bitwise_or_zero(t1);

        double t2 = t + 0.75;
        t2 -= bitwise_or_zero(t2);

        double y = t * 4;

        if (y >= 3)
        {
            y -= 4;
        }
        else if (y > 1)
        {
            y = 2 - y;
        }

        y += 4 * freqInSecondsPerSample * (blamp(t1, freqInSecondsPerSample) - blamp(t2, freqInSecondsPerSample));
        return amplitude * y;
    }

    double tri2() const
    {
        double pulseWidth = std::fmax(0.0001, std::fmin(0.9999, this->pulseWidth));

        double t1 = t + 0.5 * pulseWidth;
        t1 -= bitwise_or_zero(t1);

        double t2 = t + 1 - 0.5 * pulseWidth;
        t2 -= bitwise_or_zero(t2);

        double y = t * 2;

        if (y >= 2 - pulseWidth)
        {
            y = (y - 2) / pulseWidth;
        }
        else if (y >= pulseWidth)
        {
            y = 1 - (y - pulseWidth) / (1 - pulseWidth);
        }
        else
        {
            y /= pulseWidth;
        }

        y += freqInSecondsPerSample / (pulseWidth - pulseWidth * pulseWidth) * (blamp(t1, freqInSecondsPerSample) - blamp(t2, freqInSecondsPerSample));

        return amplitude * y;
    }

    double trip() const
    {
        double t1 = t + 0.75 + 0.5 * pulseWidth;
        t1 -= bitwise_or_zero(t1);

        double y;
        if (t1 >= pulseWidth)
        {
            y = -pulseWidth;
        }
        else
        {
            y = 4 * t1;
            y = (y >= 2 * pulseWidth ? 4 - y / pulseWidth - pulseWidth : y / pulseWidth - pulseWidth);
        }

        if (pulseWidth > 0)
        {
            double t2 = t1 + 1 - 0.5 * pulseWidth;
            t2 -= bitwise_or_zero(t2);

            double t3 = t1 + 1 - pulseWidth;
            t3 -= bitwise_or_zero(t3);
            y += 2 * freqInSecondsPerSample / pulseWidth * (blamp(t1, freqInSecondsPerSample) - 2 * blamp(t2, freqInSecondsPerSample) + blamp(t3, freqInSecondsPerSample));
        }
        return amplitude * y;
    }

    double trap() const
    {
        double y = 4 * t;
        if (y >= 3)
        {
            y -= 4;
        }
        else if (y > 1)
        {
            y = 2 - y;
        }
        y = std::fmax(-1, std::fmin(1, 2 * y));

        double t1 = t + 0.125;
        t1 -= bitwise_or_zero(t1);

        double t2 = t1 + 0.5;
        t2 -= bitwise_or_zero(t2);

        // Triangle #1
        y += 4 * freqInSecondsPerSample * (blamp(t1, freqInSecondsPerSample) - blamp(t2, freqInSecondsPerSample));

        t1 = t + 0.375;
        t1 -= bitwise_or_zero(t1);

        t2 = t1 + 0.5;
        t2 -= bitwise_or_zero(t2);

        // Triangle #2
        y += 4 * freqInSecondsPerSample * (blamp(t1, freqInSecondsPerSample) - blamp(t2, freqInSecondsPerSample));

        return amplitude * y;
    }

    double trap2() const
    {
        double pulseWidth = std::fmin(0.9999, this->pulseWidth);
        double scale = 1 / (1 - pulseWidth);

        double y = 4 * t;
        if (y >= 3)
        {
            y -= 4;
        }
        else if (y > 1)
        {
            y = 2 - y;
        }
        y = std::fmax(-1, std::fmin(1, scale * y));

        double t1 = t + 0.25 - 0.25 * pulseWidth;
        t1 -= bitwise_or_zero(t1);

        double t2 = t1 + 0.5;
        t2 -= bitwise_or_zero(t2);

        // Triangle #1
        y += scale * 2 * freqInSecondsPerSample * (blamp(t1, freqInSecondsPerSample) - blamp(t2, freqInSecondsPerSample));

        t1 = t + 0.25 + 0.25 * pulseWidth;
        t1 -= bitwise_or_zero(t1);

        t2 = t1 + 0.5;
        t2 -= bitwise_or_zero(t2);

        // Triangle #2
        y += scale * 2 * freqInSecondsPerSample * (blamp(t1, freqInSecondsPerSample) - blamp(t2, freqInSecondsPerSample));

        return amplitude * y;
    }

    double sqr() const
    {
        double t2 = t + 0.5;
        t2 -= bitwise_or_zero(t2);

        double y = t < 0.5 ? 1 : -1;
        y += blep(t, freqInSecondsPerSample) - blep(t2, freqInSecondsPerSample);

        return amplitude * y;
    }

    double sqr2() const
    {
        double t1 = t + 0.875 + 0.25 * (pulseWidth - 0.5);
        t1 -= bitwise_or_zero(t1);

        double t2 = t + 0.375 + 0.25 * (pulseWidth - 0.5);
        t2 -= bitwise_or_zero(t2);

        // Square #1
        double y = t1 < 0.5 ? 1 : -1;

        y += blep(t1, freqInSecondsPerSample) - blep(t2, freqInSecondsPerSample);

        t1 += 0.5 * (1 - pulseWidth);
        t1 -= bitwise_or_zero(t1);

        t2 += 0.5 * (1 - pulseWidth);
        t2 -= bitwise_or_zero(t2);

        // Square #2
        y += t1 < 0.5 ? 1 : -1;

        y += blep(t1, freqInSecondsPerSample) - blep(t2, freqInSecondsPerSample);

        return amplitude * 0.5 * y;
    }

    double rect() const
    {
        double t2 = t + 1 - pulseWidth;
        t2 -= bitwise_or_zero(t2);

        double y = -2 * pulseWidth;
        if (t < pulseWidth)
        {
            y += 2;
        }

        y += blep(t, freqInSecondsPerSample) - blep(t2, freqInSecondsPerSample);

        return amplitude * y;
    }

    double saw() const
    {
        double _t = t + 0.5;
        _t -= bitwise_or_zero(_t);

        double y = 2 * _t - 1;
        y -= blep(_t, freqInSecondsPerSample);

        return amplitude * y;
    }

    double ramp() const
    {
        double _t = t;
        _t -= bitwise_or_zero(_t);

        double y = 1 - 2 * _t;
        y += blep(_t, freqInSecondsPerSample);

        return amplitude * y;
    }

    double sine() const
    {
        return amplitude * sin(t* 2.f * static_cast<float>(LAB_PI));
    }

    PolyBLEPType type;
    double phaseMod = 0.0;
    double phaseModDepth = 0.0;
    double sampleRate;
    double freqInSecondsPerSample;
    double amplitude {1.f};  // Frequency dependent gain [0.0..1.0]
    double pulseWidth;  // [0.0..1.0]
    double t {0.0};  // The current phase [0.0..1.0) of the oscillator.

    void setFrequencyInSecondsPerSample(double time) { freqInSecondsPerSample = time; }

public:

    PolyBlepImpl(double sampleRate_) : sampleRate(sampleRate_)
    {
        setWaveform(PolyBLEPType::TRIANGLE);
        setFrequency(440.0);
        setPulseWidth(0.5);
        setPhaseMod(0.0);
        setPhaseModDepth(0.0);
    }

    ~PolyBlepImpl() = default;

    double get() const
    {
        switch (type) 
        {
            case PolyBLEPType::SINE: return sine();
            case PolyBLEPType::TRIANGLE: return tri();
            case PolyBLEPType::SQUARE: return sqr();
            case PolyBLEPType::RECTANGLE: return rect();
            case PolyBLEPType::SAWTOOTH: return saw();
            case PolyBLEPType::RAMP: return ramp();
            case PolyBLEPType::MODIFIED_TRIANGLE: return tri2();
            case PolyBLEPType::MODIFIED_SQUARE: return sqr2();
            case PolyBLEPType::HALF_WAVE_RECTIFIED_SINE: return half();
            case PolyBLEPType::FULL_WAVE_RECTIFIED_SINE: return full();
            case PolyBLEPType::TRIANGULAR_PULSE: return trip();
            case PolyBLEPType::TRAPEZOID_FIXED: return trap();
            case PolyBLEPType::TRAPEZOID_VARIABLE: return trap2();
            
            default: return 0.0;
        }
    }

    void incrementPhase()
    {
        const float mod = 1.0 + phaseMod*phaseModDepth;
        t += freqInSecondsPerSample * mod;
        t -= bitwise_or_zero(t);
    }

    double getPhaseAndIncrement()
    {
        const double sample = get();
        incrementPhase();
        return sample;
    }

    void setPhaseMod(float val) {
        phaseMod = val;
    }

    void setPhaseModDepth(float val) {
        phaseModDepth = val;
    }

    void syncToPhase(const double phase)
    {
        t = phase;
        if (t >= 0.0)  t -= bitwise_or_zero(t);
        else t += 1.0 - bitwise_or_zero(t);
    }

    void setFrequency(const double freqInHz) { setFrequencyInSecondsPerSample(freqInHz / sampleRate); }
    void setPulseWidth(const double pw) { pulseWidth = pw; }
    void setWaveform(const PolyBLEPType waveform) { type = waveform; }
    void setSampleRate(float sr) { sampleRate = sr; }
    double getFreqInHz() const { return freqInSecondsPerSample * sampleRate; }
};

// clang-format on

static char const * const s_polyblep_types[] = {
    "Sine", "Triangle", "Square", "Sawtooth", "Ramp", "Modified_Triangle", "Modified_Square", "Half Wave Rectified Sine",
    "Full Wave Rectified Sine", "Triangular Pulse", "Trapezoid Fixed", "Trapezoid Variable",
    nullptr};

static AudioParamDescriptor s_pbParams[] = {
    {"frequency",  "FREQ", 440, 0, 100000},
    {"amplitude",  "AMPL",   1, 0, 100000}, 
    {"detune", "DTUN", 0.0, -4800.0, 4800.0},
    {"pulseWidth", "PWDTH", 0.0, 0.0, 1.0},
    {"phaseMod", "PHASE", 0.0, -1.0, 1.0},
    {"phaseModDepth", "PHDPTH", 0.0, 0.0, 100.0},
    nullptr};
static AudioSettingDescriptor s_pbSettings[] = {{"type", "TYPE", SettingType::Enum, s_polyblep_types}, nullptr};

AudioNodeDescriptor * PolyBLEPNode::desc()
{
    static AudioNodeDescriptor d {s_pbParams, s_pbSettings, 1};
    return &d;
}

PolyBLEPNode::PolyBLEPNode(AudioContext & ac)
    : AudioScheduledSourceNode(ac, *desc()), 
      m_frequencyValues(AudioNode::ProcessingSizeInFrames),
      m_detuneValues(AudioNode::ProcessingSizeInFrames), 
      m_pulseWidthValues(AudioNode::ProcessingSizeInFrames), 
      m_phaseModValues(AudioNode::ProcessingSizeInFrames), 
      m_phaseModDepthValues(AudioNode::ProcessingSizeInFrames)
{
    m_type = setting("type");
    m_frequency = param("frequency");
    m_amplitude = param("amplitude");
    m_detune = param("detune");
    m_pulseWidth = param("pulseWidth");
    m_phaseMod = param("phaseMod");
    m_phaseModDepth = param("phaseModDepth");
    
    m_amplitude->setValue(1.f);
    m_detune->setValue(0.f);
    m_pulseWidth->setValue(0.5f);
    m_phaseMod->setValue(0.f);
    m_phaseModDepth->setValue(0.f);

    m_type->setValueChanged([this]() { 
        setType(PolyBLEPType(m_type->valueUint32())); 
    });

    polyblep.reset(new PolyBlepImpl(ac.sampleRate()));
    setType(PolyBLEPType::TRIANGLE);
    initialize();
}

PolyBLEPNode::~PolyBLEPNode()
{
    uninitialize();
}

PolyBLEPType PolyBLEPNode::type() const
{
    return PolyBLEPType(m_type->valueUint32());
}

void PolyBLEPNode::setType(PolyBLEPType type)
{
    m_type->setUint32(static_cast<uint32_t>(type));
}

void PolyBLEPNode::processPolyBLEP(ContextRenderLock & r, int bufferSize, int offset, int count)
{
    AudioBus * outputBus = output(0)->bus(r);
    if (!r.context() || !isInitialized() || !outputBus->numberOfChannels())
    {
        outputBus->zero();
        return;
    }

    const float sample_rate = r.context()->sampleRate();
    polyblep->setSampleRate(sample_rate);

    int nonSilentFramesToProcess = count;

    if (!nonSilentFramesToProcess)
    {
        outputBus->zero();
        return;
    }

    if (bufferSize > m_amplitudeValues.size()) m_amplitudeValues.allocate(bufferSize);
    if (bufferSize > m_frequencyValues.size()) m_frequencyValues.allocate(bufferSize);
    if (bufferSize > m_detuneValues.size()) m_detuneValues.allocate(bufferSize);
    if (bufferSize > m_pulseWidthValues.size()) m_pulseWidthValues.allocate(bufferSize);
    if (bufferSize > m_phaseModValues.size()) m_phaseModValues.allocate(bufferSize);
    if (bufferSize > m_phaseModDepthValues.size()) m_phaseModDepthValues.allocate(bufferSize);

    // fetch the amplitudes
    float * amplitudes = m_amplitudeValues.data();
    if (m_amplitude->hasSampleAccurateValues())
    {
        m_amplitude->calculateSampleAccurateValues(r, amplitudes, bufferSize);
    }
    else
    {
        m_amplitude->smooth(r);
        float amp = m_amplitude->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) amplitudes[i] = amp;
    }

    // fetch the frequencies
    float * frequencies = m_frequencyValues.data();
    if (m_frequency->hasSampleAccurateValues())
    {
        m_frequency->calculateSampleAccurateValues(r, frequencies, bufferSize);
    }
    else
    {
        m_frequency->smooth(r);
        float freq = m_frequency->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) frequencies[i] = freq;
    }

    // calculate and write the wave
    float * detunes = m_detuneValues.data();
    if (m_detune->hasSampleAccurateValues())
    {
        m_detune->calculateSampleAccurateValues(r, detunes, bufferSize);
    }
    else
    {
        m_detune->smooth(r);
        float detuneValue = m_detune->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) detunes[i] = detuneValue;
    }

    float * pulseWidths = m_pulseWidthValues.data();
    if (m_pulseWidth->hasSampleAccurateValues())
    {
        m_pulseWidth->calculateSampleAccurateValues(r, pulseWidths, bufferSize);
    }
    else
    {
        m_pulseWidth->smooth(r);
        float pulseWidthValue = m_pulseWidth->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) pulseWidths[i] = pulseWidthValue;
    }

    float * phaseMods = m_phaseModValues.data();
    if (m_phaseMod->hasSampleAccurateValues())
    {
        m_phaseMod->calculateSampleAccurateValues(r, phaseMods, bufferSize);
    }
    else
    {
        m_phaseMod->smooth(r);
        float phaseModValue = m_phaseMod->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) phaseMods[i] = phaseModValue;
    }

    float * phaseModDepths = m_phaseModDepthValues.data();
    if (m_phaseModDepth->hasSampleAccurateValues())
    {
        m_phaseModDepth->calculateSampleAccurateValues(r, phaseModDepths, bufferSize);
    }
    else
    {
        m_phaseModDepth->smooth(r);
        float phaseModDepthValue = m_phaseModDepth->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) phaseModDepths[i] = phaseModDepthValue;
    }


    float * destination = outputBus->channel(0)->mutableData() + offset;
    polyblep->setWaveform(static_cast<PolyBLEPType>(m_type->valueUint32()));

    for (int i = offset; i < offset + nonSilentFramesToProcess; ++i)
    {
        // Update the PolyBlepImpl's frequency for each sample
        double detuneFactor = std::pow(2.0, detunes[i] / 1200.0);  // Convert cents to frequency ratio
        // Update the PolyBlepImpl's frequency for each sample
        polyblep->setFrequency(frequencies[i] * detuneFactor);
        polyblep->setPulseWidth(pulseWidths[i]);
        polyblep->setPhaseMod(phaseMods[i]);
        polyblep->setPhaseModDepth(phaseModDepths[i]);
        destination[i] = (amplitudes[i] * static_cast<float>(polyblep->getPhaseAndIncrement()));
    }

    outputBus->clearSilentFlag();
}

void PolyBLEPNode::process(ContextRenderLock & r, int bufferSize)
{
    return processPolyBLEP(r, bufferSize, _self->_scheduler._renderOffset, _self->_scheduler._renderLength);
}

bool PolyBLEPNode::propagatesSilence(ContextRenderLock & r) const
{
    return !isPlayingOrScheduled() || hasFinished();
}
