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
    m_unisonCount->setUint32(1);
    m_unisonSpread->setFloat(0.f);
    // An oscillator is always mono.
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 1)));
    initialize();
    {
        ContextRenderLock r(&ac, "initwave");
        update(r);
    }
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
    for (auto & osc : m_unisonOscillators)
    {
        osc->ResetPhase();
    }
}


void WaveTableOscillatorNode::setPhase(float p)
{
    for (auto & osc : m_unisonOscillators)
    {
        osc->SetPhaseOffset(p);
    }
}

WaveTableMemory::waveTable * WaveTableOscillatorNode::GetBaseWavetable()
{
    if (m_unisonOscillators.size() == 0)
        return nullptr;

    return m_unisonOscillators[0]->GetBaseWavetable();
}


void WaveTableOscillatorNode::setType(WaveTableWaveType type)
{
    m_cachedType = type;
    m_type->setUint32(static_cast<uint32_t>(type));
}

void WaveTableOscillatorNode::update(ContextRenderLock& r)
{
    auto const desired = m_unisonCount->valueUint32() > 0 ? m_unisonCount->valueUint32() : 1;
    
    auto const actual = m_unisonOscillators.size();
    if (desired != actual)
    {
        m_unisonOscillators.clear();
        if (desired > 0)
        {
            std::cout << "allocating " << desired << ":" << m_type->valueUint32() << " oscillators" << std::endl;
            m_unisonOscillators.resize(desired);
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

float* WaveTableOscillatorNode::GetSampleAccurateData(ContextRenderLock & r, AudioFloatArray & values, std::shared_ptr<AudioParam> param, size_t bufferSize)
{
    float * sampleData = values.data();
    if (param->hasSampleAccurateValues())
    {
        param->calculateSampleAccurateValues(r, sampleData, bufferSize);
    }
    else
    {
        param->smooth(r);
        float freq = param->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) sampleData[i] = freq;
    }
    return sampleData;
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

    float* frequencies = GetSampleAccurateData(r, m_frequencyValues, m_frequency, bufferSize);
    float* detunes = GetSampleAccurateData(r, m_detuneValues, m_detune, bufferSize);
    float* pulseWidths = GetSampleAccurateData(r, m_pulseWidthValues, m_pulseWidth, bufferSize);        
    float* phaseMods = GetSampleAccurateData(r, m_phaseModValues, m_phaseMod, bufferSize);        
    float* phaseModDepths = GetSampleAccurateData(r, m_phaseModDepthValues, m_phaseModDepth, bufferSize);        

    float* destination = outputBus->channel(0)->mutableData() + offset;
    constexpr float ratio = 1.f / 1200.f;
    
    // calculate and write the wave

    auto RenderSamplesMinusOffset = [&]()
    {
        const auto & wave = m_unisonOscillators[0].get();
        for (int i = offset, end = offset + nonSilentFramesToProcess; i < end; ++i)
        {
            // Convert cents to frequency ratio directly within the computation
            float normalizedFrequency = (*frequencies++ * fastexp2(*detunes++ * ratio)) / sample_rate;
            wave->SetFrequency(normalizedFrequency);
            wave->SetPhaseOffset(*pulseWidths++);
            *destination++ = wave->GetOutputMinusOffset();
            wave->UpdatePhase(*phaseMods++ * *phaseModDepths++);
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
                wave->UpdatePhase(phaseMods[i] * phaseModDepths[i]);
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
                
                wave->SetPhaseOffset(pulseWidths[i]);
                wave->SetFrequency(normalizedFrequency);
                sample += wave->GetOutputMinusOffset();
                wave->UpdatePhase(phaseMods[i] * phaseModDepths[i]);
            }
            *destination++ = sample * gain;
        }
    };

    auto RenderSamples = [&]()
    {
        const auto & wave = m_unisonOscillators[0].get();
        for (int i = offset, end = offset + nonSilentFramesToProcess; i < end; ++i)
        {
            // Convert cents to frequency ratio directly within the computation
            float normalizedFrequency = (frequencies[i] * fastexp2(detunes[i] * ratio)) / sample_rate;
            wave->SetFrequency(normalizedFrequency);
            *destination++ = wave->GetOutput();
            wave->UpdatePhase(*phaseMods++ * *phaseModDepths++);
        }
    };

    WaveTableWaveType type = static_cast<WaveTableWaveType> (m_type->valueUint32());
    if (m_unisonCount->valueUint32() > 1)
    {
        if (type == WaveTableWaveType::SQUARE)
            RenderSamplesWithUnisonMinusOffset();
        else
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
