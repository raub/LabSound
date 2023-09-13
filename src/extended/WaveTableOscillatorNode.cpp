// License: BSD 2 Clause
// Copyright (C) 2020+, The LabSound Authors. All rights reserved.

#include "LabSound/extended/WaveTableOscillatorNode.h"
#include "LabSound/extended/AudioContextLock.h"
#include "LabSound/extended/Registry.h"

#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioContext.h"
#include "LabSound/core/AudioNodeInput.h"
#include "LabSound/core/AudioNodeOutput.h"
#include "LabSound/core/AudioSetting.h"
#include "LabSound/core/Macros.h"

#include "internal/Assertions.h"
#include "internal/AudioUtilities.h"
#include "internal/VectorMath.h"
#include <vector>
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

// clang-format on

static char const * const s_wavetable_types[] = {
    "Sine", "Triangle", "Square", "Sawtooth",
    nullptr};

static AudioParamDescriptor s_waveTableParams[] = {
    {"frequency",  "FREQ", 440, 0, 100000},
    {"detune", "DTUN", 0.0, -4800.0, 4800.0},
    {"pulseWidth", "PWDTH", 0.0, 0.0, 1.0},
    {"phaseMod", "PHASE", 0.0, -1.0, 1.0},
    {"phaseModDepth", "PHDPTH", 0.0, 0.0, 100.0},
    nullptr};
static AudioSettingDescriptor s_pbSettings[] = {{"type", "TYPE", SettingType::Enum, s_wavetable_types}, nullptr};

AudioNodeDescriptor * WaveTableOscillatorNode::desc()
{
    static AudioNodeDescriptor d {s_waveTableParams, s_pbSettings};
    return &d;
}
//std::shared_ptr<WaveTableOsc> WaveTableOscillatorNode::wavetable_cache[] = {
//    sinOsc(),
//    triangleOsc(),
//    squareOsc(),
//    sawOsc()};



WaveTableOscillatorNode::WaveTableOscillatorNode(AudioContext & ac)
    : AudioScheduledSourceNode(ac, *desc()), 
      m_frequencyValues(AudioNode::ProcessingSizeInFrames),
      m_detuneValues(AudioNode::ProcessingSizeInFrames), 
      m_pulseWidthValues(AudioNode::ProcessingSizeInFrames), 
      m_phaseModValues(AudioNode::ProcessingSizeInFrames), 
      m_phaseModDepthValues(AudioNode::ProcessingSizeInFrames)
{
    m_waveOscillators = {
        sawOsc(),
        sawOsc(),
        sawOsc(),
        sawOsc(),
        sawOsc(),
        sawOsc(),
        sawOsc()};

        wavetable_cache = {
        sinOsc(),
        triangleOsc(),
        squareOsc(),
        sawOsc()};
    
    m_type = setting("type");
    m_frequency = param("frequency");
    m_detune = param("detune");
    m_pulseWidth = param("pulseWidth");
    m_phaseMod = param("phaseMod");
    m_phaseModDepth = param("phaseModDepth");
    
    m_detune->setValue(0.f);
    m_pulseWidth->setValue(0.5f);
    m_phaseMod->setValue(0.f);
    m_phaseModDepth->setValue(0.f);

    m_type->setValueChanged([this]() { 
        setType(WaveTableWaveType(m_type->valueUint32())); 
    });

    // An oscillator is always mono.
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 1)));
    setType(WaveTableWaveType::SINE);
    initialize();
}

WaveTableOscillatorNode::~WaveTableOscillatorNode()
{
    uninitialize();
}

WaveTableWaveType WaveTableOscillatorNode::type() const
{
    return WaveTableWaveType(m_type->valueUint32());
}

void WaveTableOscillatorNode::setType(WaveTableWaveType type)
{
    switch (type)
    {
        case WaveTableWaveType::SINE:
    //        m_waveOsc = sinOsc();
            m_waveOsc = wavetable_cache[0];
            break;
        case WaveTableWaveType::TRIANGLE:
            m_waveOsc = wavetable_cache[1];
            //m_waveOsc = triangleOsc();
            break;
        case WaveTableWaveType::SQUARE:
            m_waveOsc = wavetable_cache[2];
    //        m_waveOsc = squareOsc();
            break;
        case WaveTableWaveType::SAWTOOTH:
            m_waveOsc = wavetable_cache[3];
    //        m_waveOsc = sawOsc();
            break;
    }
    m_type->setUint32(static_cast<uint32_t>(type));
}

void WaveTableOscillatorNode::processWavetable(ContextRenderLock & r, int bufferSize, int offset, int count)
{
    AudioBus * outputBus = output(0)->bus(r);
    if (!r.context() || !isInitialized() || !outputBus->numberOfChannels())
    {
        outputBus->zero();
        return;
    }

    const float sample_rate = r.context()->sampleRate();

    int nonSilentFramesToProcess = count;

    if (!nonSilentFramesToProcess)
    {
        outputBus->zero();
        return;
    }

    if (bufferSize > m_frequencyValues.size()) m_frequencyValues.allocate(bufferSize);
    if (bufferSize > m_detuneValues.size()) m_detuneValues.allocate(bufferSize);
    if (bufferSize > m_pulseWidthValues.size()) m_pulseWidthValues.allocate(bufferSize);
    if (bufferSize > m_phaseModValues.size()) m_phaseModValues.allocate(bufferSize);
    if (bufferSize > m_phaseModDepthValues.size()) m_phaseModDepthValues.allocate(bufferSize);

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
    //polyblep->setWaveform(static_cast<PolyBLEPType>(m_type->valueUint32()));

    //float lastFreq = -1;
    auto RenderSamplesMinusOffset = [&]() {
        
        for (int i = offset; i < offset + nonSilentFramesToProcess; ++i)
        {
            // Update the PolyBlepImpl's frequency for each sample
            double detuneFactor = std::pow(2.0, detunes[i] / 1200.0);  // Convert cents to frequency ratio
            const auto freq = frequencies[i] * detuneFactor;
            //if (freq != lastFreq)
            //{
                float normalizedFrequency = freq / sample_rate;

                m_waveOsc->SetFrequency(normalizedFrequency);
                //lastFreq = freq;
            //}
            
            m_waveOsc->SetPhaseOffset(pulseWidths[i]);
            *destination++ = m_waveOsc->GetOutputMinusOffset();
            m_waveOsc->UpdatePhase();
        }
    };

    auto RenderSamples = [&]() {
        for (int i = offset; i < offset + nonSilentFramesToProcess; ++i)
        {
            double detuneFactor = std::pow(2.0, detunes[i] / 1200.0);  // Convert cents to frequency ratio
            const auto freq = frequencies[i] * detuneFactor;
            float modulation = phaseMods[i] * phaseModDepths[i];
            float normalizedFrequency = freq / sample_rate;
            //float normalizedFrequency = freq / sample_rate;
            m_waveOsc->SetFrequency(normalizedFrequency);
            *destination++ = m_waveOsc->GetOutput();
            m_waveOsc->UpdatePhase(modulation);
        }
    };

    auto RenderSuperSawSamples = [&]()
    {
        const int numOscillators = 7;  // For the central saw and 3 detuned saws on either side
        float detuneAmounts[numOscillators] = {-0.3, -0.2, -0.1, 0.0, 0.1, 0.2, 0.3};  // Example detuning amounts

        for (int i = offset; i < offset + nonSilentFramesToProcess; ++i)
        {
            double detuneFactor = std::pow(2.0, detunes[i] / 1200.0);  // Convert cents to frequency ratio
            const auto freq = frequencies[i] * detuneFactor;
            float modulation = phaseMods[i] * phaseModDepths[i];
            float sum = 0.0;

            for (int osc = 0; osc < numOscillators; ++osc)
            {
                float detunedFrequency = freq + (detuneAmounts[osc]*0.1);
                float normalizedFrequency = detunedFrequency / sample_rate;

                m_waveOscillators[osc]->SetFrequency(normalizedFrequency);
                sum += m_waveOscillators[osc]->GetOutput();
                m_waveOscillators[osc]->UpdatePhase(modulation);
            }

            *destination++ = sum / numOscillators;  // Averaging the output of the saw oscillators
        }
    };


    if (type() == WaveTableWaveType::SUPERSAW)
    {
        RenderSuperSawSamples();
    }
    else
    if (type() == WaveTableWaveType::SQUARE)
    {
        RenderSamplesMinusOffset();
    }
    else
    {
        RenderSamples();
    }
    

    outputBus->clearSilentFlag();
}

void WaveTableOscillatorNode::process(ContextRenderLock & r, int bufferSize)
{
    return processWavetable(r, bufferSize, _self->_scheduler._renderOffset, _self->_scheduler._renderLength);
}

bool WaveTableOscillatorNode::propagatesSilence(ContextRenderLock & r) const
{
    return !isPlayingOrScheduled() || hasFinished();
}
