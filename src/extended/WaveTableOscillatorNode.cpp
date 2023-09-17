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

template <typename T>
inline int64_t bitwise_or_zero(const T & x) { return static_cast<int64_t>(x) | 0; }

template <typename T>
inline T square(const T & x) { return x * x; }

// clang-format on

static char const * const s_wavetable_types[] = {
    "Sine", "Triangle", "Square", "Sawtooth",
    nullptr};

static AudioSettingDescriptor s_wavetableOscSettings[] = {
    {"unisonCount", "UNICNT", SettingType::Integer},
    {"unisonSpread","UNISPR", SettingType::Float},
    {"type", "TYPE", SettingType::Enum, s_wavetable_types},
    nullptr};

static AudioParamDescriptor s_waveTableParams[] = {
    {"frequency",  "FREQ", 440, 0, 100000},
    {"detune", "DTUN", 0.0, -4800.0, 4800.0},
    {"pulseWidth", "PWDTH", 0.0, 0.0, 1.0},
    {"phaseMod", "PHASE", 0.0, -1.0, 1.0},
    {"phaseModDepth", "PHDPTH", 0.0, -1050.0, 100.0},
    nullptr};

AudioNodeDescriptor * WaveTableOscillatorNode::desc()
{
    static AudioNodeDescriptor d {s_waveTableParams, s_wavetableOscSettings};
    return &d;
}

WaveTableOscillatorNode::WaveTableOscillatorNode(AudioContext & ac)
    : AudioScheduledSourceNode(ac, *desc()), 
      m_frequencyValues(AudioNode::ProcessingSizeInFrames),
      m_detuneValues(AudioNode::ProcessingSizeInFrames), 
      m_pulseWidthValues(AudioNode::ProcessingSizeInFrames), 
      m_phaseModValues(AudioNode::ProcessingSizeInFrames), 
      m_phaseModDepthValues(AudioNode::ProcessingSizeInFrames), 
      m_contextRef(ac)
{
    m_type = setting("type");
    m_frequency = param("frequency");
    m_detune = param("detune");
    m_pulseWidth = param("pulseWidth");
    m_phaseMod = param("phaseMod");
    m_phaseModDepth = param("phaseModDepth");
    m_unisonCount = setting("unisonCount");
    m_unisonSpread = setting("unisonSpread");

    m_detune->setValue(0.f);
    m_pulseWidth->setValue(0.5f);
    m_phaseMod->setValue(0.f);
    m_phaseModDepth->setValue(0.f);
    m_waveOsc = std::make_shared<WaveTableOsc>();
    //m_type->setValueChanged([&]() { 
    //    auto t = WaveTableWaveType(m_type->valueUint32());
    //    setType(t);
    //});
    m_unisonCount->setUint32(0);
    m_unisonSpread->setFloat(0.f);
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

void WaveTableOscillatorNode::resetPhase()
{
    m_waveOsc->ResetPhase();
    for (auto & osc : m_unisonOscillators)
    {
        osc->ResetPhase();
    }
}

void WaveTableOscillatorNode::setType(WaveTableWaveType type)
{
    m_cachedType = type;
    m_waveOsc->SetType(type);
    m_type->setUint32(static_cast<uint32_t>(type));
}

void WaveTableOscillatorNode::update(ContextRenderLock& r)
{
    auto const desired = m_unisonCount->valueUint32();
    auto const actual = m_unisonOscillators.size();
    if (desired != actual)
    {
        m_unisonOscillators.clear();
        if (desired > 0)
        {
            std::cout << "allocating " << m_unisonCount->valueUint32() << ":" << m_type->valueUint32() << " oscillators" << std::endl;
            m_unisonOscillators.resize(m_unisonCount->valueUint32());
            for (int i = 0; i < m_unisonCount->valueUint32(); i++)
            {
                m_unisonOscillators[i] = std::make_shared<WaveTableOsc>(static_cast<WaveTableWaveType>(m_type->valueUint32()));
            }
        }
    }

    if (desired > 0)
    {
        for (int i = 0; i < desired; i++)
        {
            m_unisonOscillators[i]->SetType(static_cast<WaveTableWaveType>(m_type->valueUint32()));
        }
    }
}
void WaveTableOscillatorNode::processWavetable(ContextRenderLock & r, int bufferSize, int offset, int count)
{
    update(r);

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

    //if (bufferSize > m_frequencyValues.size()) m_frequencyValues.allocate(bufferSize);
    //if (bufferSize > m_detuneValues.size()) m_detuneValues.allocate(bufferSize);
    //if (bufferSize > m_pulseWidthValues.size()) m_pulseWidthValues.allocate(bufferSize);
    //if (bufferSize > m_phaseModValues.size()) m_phaseModValues.allocate(bufferSize);
    //if (bufferSize > m_phaseModDepthValues.size()) m_phaseModDepthValues.allocate(bufferSize);

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
    //for (int i = 0; i < bufferSize; ++i) detunes[i] = std::pow(2.0, detunes[i] / 1200.0);

     
    float * pulseWidths = m_pulseWidthValues.data();
    if (m_pulseWidth->hasSampleAccurateValues())
    {
        m_pulseWidth->calculateSampleAccurateValues(r, pulseWidths, bufferSize);
    }
    else
    {
        //m_pulseWidth->smooth(r);
        float pulseWidthValue = m_pulseWidth->value();
        for (int i = 0; i < bufferSize; ++i) pulseWidths[i] = pulseWidthValue;
    }

    float * phaseMods = m_phaseModValues.data();
    if (m_phaseMod->hasSampleAccurateValues())
    {
        m_phaseMod->calculateSampleAccurateValues(r, phaseMods, bufferSize);
    }
    else
    {
        //m_phaseMod->smooth(r);
        float phaseModValue = m_phaseMod->value();
            //smoothedValue();
        for (int i = 0; i < bufferSize; ++i) phaseMods[i] = phaseModValue;
    }

    float * phaseModDepths = m_phaseModDepthValues.data();
    if (m_phaseModDepth->hasSampleAccurateValues())
    {
        m_phaseModDepth->calculateSampleAccurateValues(r, phaseModDepths, bufferSize);
    }
    else
    {
        //m_phaseModDepth->smooth(r);
        float phaseModDepthValue = m_phaseModDepth->value();
        for (int i = 0; i < bufferSize; ++i) phaseModDepths[i] = phaseModDepthValue;
    }

    float * destination = outputBus->channel(0)->mutableData() + offset;
    constexpr float ratio = 1.f / 1200.f;
    
    auto RenderSamplesMinusOffset = [&]()
    {
        for (int i = offset, end = offset + nonSilentFramesToProcess; i < end; ++i)
        {
            // Convert cents to frequency ratio directly within the computation
            float normalizedFrequency = (*frequencies++ * fastexp2(*detunes++ * ratio)) / sample_rate;
            m_waveOsc->SetFrequency(normalizedFrequency);
            m_waveOsc->SetPhaseOffset(*pulseWidths++);
            *destination++ = m_waveOsc->GetOutputMinusOffset();
            m_waveOsc->UpdatePhase(*phaseMods++ * *phaseModDepths++);
        }
    };

    auto RenderSamplesWithUnison = [&]()
    {
        float numOscillators = m_unisonOscillators.size();
        const float gain = 1.f / numOscillators;
        // Pre-calculate constant values
        float totalSpreadInCents = m_unisonSpread->valueFloat();
        float stepInCents = (numOscillators > 1.f) ? totalSpreadInCents / (numOscillators - 1.f) : 0;
        float detuneBase = -totalSpreadInCents / 2.0f;

        for (int i = offset, end = offset + nonSilentFramesToProcess; i < end; ++i)
        {
            float sample = 0.f;
            const float freq = *frequencies++;
            const float detune = *detunes++;
            float detuneAmount = detuneBase;
            for (float u = 0; u < numOscillators; u+=1.f, detuneAmount += stepInCents)
            {
                const auto& wave = m_unisonOscillators[u].get();
                float normalizedFrequency = (freq * fastexp2((detune + detuneAmount) * ratio)) / sample_rate;
                wave->SetFrequency(normalizedFrequency);
                
                sample += wave->GetOutput();
                wave->UpdatePhase();
                
            }
            *destination++ = sample * gain;
        }
    };

    auto RenderSamplesWithUnisonMinusOffset = [&]()
    {
        float numOscillators = m_unisonOscillators.size();
        const float gain = 1.f / numOscillators;
        // Pre-calculate constant values
        float totalSpreadInCents = m_unisonSpread->valueFloat();
        float stepInCents = (numOscillators > 1.f) ? totalSpreadInCents / (numOscillators - 1.f) : 0;
        float detuneBase = -totalSpreadInCents / 2.0f;

        for (int i = offset, end = offset + nonSilentFramesToProcess; i < end; ++i)
        {
            float sample = 0.f;
            const float freq = *frequencies++;
            const float detune = *detunes++;
            float detuneAmount = detuneBase;
            for (float u = 0; u < numOscillators; u += 1.f, detuneAmount += stepInCents)
            {
                const auto & wave = m_unisonOscillators[u].get();
                float normalizedFrequency = (freq * fastexp2((detune + detuneAmount) * ratio)) / sample_rate;
                
                wave->SetPhaseOffset(*pulseWidths++);
                wave->SetFrequency(normalizedFrequency);
                sample += wave->GetOutputMinusOffset();
                // wave->UpdatePhase();
                const float mod = *phaseMods++;
                const float depth = *phaseModDepths++;
                wave->UpdatePhase();//mod * depth);
            }
            *destination++ = sample * gain;
        }
    };

    auto RenderSamples = [&]()
    {
        for (int i = offset, end = offset + nonSilentFramesToProcess; i < end; ++i)
        {
            // Convert cents to frequency ratio directly within the computation
            float normalizedFrequency = (frequencies[i] * fastexp2(detunes[i] * ratio)) / sample_rate;
            m_waveOsc->SetFrequency(normalizedFrequency);
            *destination++ = m_waveOsc->GetOutput();
            m_waveOsc->UpdatePhase(*phaseMods++ * *phaseModDepths++
            );
        }
    };

    WaveTableWaveType type = static_cast<WaveTableWaveType> (m_type->valueUint32());
    if (m_unisonCount->valueUint32() > 1)
    {
        //if (type == WaveTableWaveType::SQUARE)
        //    RenderSamplesWithUnisonMinusOffset();
        //else
            RenderSamplesWithUnison();
    }
    else
    {
        if (type == WaveTableWaveType::SQUARE)
            RenderSamplesMinusOffset();
        else
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
