// License: BSD 2 Clause
// Copyright (C) 2010, Google Inc. All rights reserved.
// Copyright (C) 2015+, The LabSound Authors. All rights reserved.

#include "LabSound/core/AudioArray.h"
#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioNodeInput.h"
#include "LabSound/core/AudioNodeOutput.h"
#include "LabSound/extended/MoogFilterNode.h"

#include "LabSound/extended/AudioContextLock.h"
#include "LabSound/extended/Registry.h"

#include "internal/Assertions.h"

namespace lab
{

static AudioParamDescriptor s_moogFilterParams[] = {
    {"cutoff", "CUTOFF", 20000.0, 0.0, 20000.0}, 
    {"resonance", "RESONANCE", 0.0, 0.0, 3.0}, 
    {"drive", "DRIVE", 1.0, 0.0, 10.0}, nullptr};
AudioNodeDescriptor * MoogFilterNode::desc()
{
    static AudioNodeDescriptor d {s_moogFilterParams, nullptr};
    return &d;
}

MoogFilterNode::MoogFilterNode(AudioContext & ac)
    : AudioNode(ac, *desc())
    , m_sampleAccurateCutoffValues(AudioNode::ProcessingSizeInFrames)
    , m_sampleAccurateResonanceValues(AudioNode::ProcessingSizeInFrames)
    , m_sampleAccurateDriveValues(AudioNode::ProcessingSizeInFrames)
{
    memset(V, 0, sizeof(V));
    memset(dV, 0, sizeof(dV));
    memset(tV, 0, sizeof(tV));

    addInput(std::unique_ptr<AudioNodeInput>(new AudioNodeInput(this)));
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 1)));
    
    m_cutoff = param("cutoff");
    m_resonance = param("resonance");
    m_drive = param("drive");
    initialize();
}

MoogFilterNode::~MoogFilterNode()
{
    uninitialize();
}

void MoogFilterNode::process(ContextRenderLock & r, int bufferSize)
{
    return processMoogFilter(r, bufferSize, _self->_scheduler._renderOffset, _self->_scheduler._renderLength);
}

void MoogFilterNode::processMoogFilter(ContextRenderLock & r, int bufferSize, int offset, int count)
{
    AudioBus * outputBus = output(0)->bus(r);
    ASSERT(outputBus);

    int nonSilentFramesToProcess = count;
    if (!nonSilentFramesToProcess)
    {
        outputBus->zero();
        return;
    }
    
    if (!isInitialized() || !input(0)->isConnected())
    {
        outputBus->zero();
        return;
    }

    AudioBus * inputBus = input(0)->bus(r);
    const int inputBusChannelCount = inputBus->numberOfChannels();
    if (!inputBusChannelCount)
    {
        outputBus->zero();
        return;
    }

    int outputBusChannelCount = outputBus->numberOfChannels();
    if (inputBusChannelCount != outputBusChannelCount)
    {
        output(0)->setNumberOfChannels(r, inputBusChannelCount);
        outputBusChannelCount = inputBusChannelCount;
        outputBus = output(0)->bus(r);
    }

    if (bufferSize > m_sampleAccurateCutoffValues.size()) m_sampleAccurateCutoffValues.allocate(bufferSize);
    if (bufferSize > m_sampleAccurateResonanceValues.size()) m_sampleAccurateResonanceValues.allocate(bufferSize);

    // fetch the cutoffs
    float * cutoffs = m_sampleAccurateCutoffValues.data();
    if (m_cutoff->hasSampleAccurateValues())
    {
        m_cutoff->calculateSampleAccurateValues(r, cutoffs, bufferSize);
    }
    else
    {
        m_cutoff->smooth(r);
        float co = m_cutoff->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) cutoffs[i] = co;
    }

    // fetch the resonances
    float * resos = m_sampleAccurateResonanceValues.data();
    if (m_resonance->hasSampleAccurateValues())
    {
        m_resonance->calculateSampleAccurateValues(r, resos, bufferSize);
    }
    else
    {
        m_resonance->smooth(r);
        float co = m_resonance->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) resos[i] = co;
    }

    // fetch the drive
    float * drives = m_sampleAccurateDriveValues.data();
    if (m_drive->hasSampleAccurateValues())
    {
        m_drive->calculateSampleAccurateValues(r, drives, bufferSize);
    }
    else
    {
        m_drive->smooth(r);
        float co = m_drive->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) resos[i] = co;
    }
    
    float * destination = outputBus->channel(0)->mutableData() + offset;
    const float * source = inputBus->channel(0)->data();
    const double sample_rate =(double)r.context()->sampleRate();


    double dV0, dV1, dV2, dV3;
    const double VT = 0.312; // Thermal voltage (26 milliwats at room temperature)
    const double samplratex2 = 2.0 * sample_rate;
    for (int i = offset; i < offset + nonSilentFramesToProcess; ++i)
     {
        double cutoff = cutoffs[i];
        x = (M_PI * cutoff) / sample_rate;
        g = 4.0 * M_PI * VT * cutoff * (1.0 - x) / (1.0 + x);

        dV0 = -g * (tanh((drives[i] * source[i] + resos[i] * V[3]) / (2.0 * VT)) + tV[0]);
        V[0] += (dV0 + dV[0]) / samplratex2;
        dV[0] = dV0;
        tV[0] = tanh(V[0] / (2.0 * VT));

        dV1 = g * (tV[0] - tV[1]);
        V[1] += (dV1 + dV[1]) / samplratex2;
        dV[1] = dV1;
        tV[1] = tanh(V[1] / (2.0 * VT));

        dV2 = g * (tV[1] - tV[2]);
        V[2] += (dV2 + dV[2]) / samplratex2;
        dV[2] = dV2;
        tV[2] = tanh(V[2] / (2.0 * VT));

        dV3 = g * (tV[2] - tV[3]);
        V[3] += (dV3 + dV[3]) / samplratex2;
        dV[3] = dV3;
        tV[3] = tanh(V[3] / (2.0 * VT));

        destination[i] = V[3];

     }

     outputBus->clearSilentFlag();
}

void MoogFilterNode::reset(ContextRenderLock & r)
{
     memset(V, 0, sizeof(V));
     memset(dV, 0, sizeof(dV));
     memset(tV, 0, sizeof(tV));
}

}  // namespace lab
