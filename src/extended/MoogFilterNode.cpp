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

#define _USE_MATH_DEFINES
#include <math.h>

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
    in1 = in2 = in3 = in4 = out1 = out2 = out3 = out4 = 0.0;

    addInput(std::unique_ptr<AudioNodeInput>(new AudioNodeInput(this)));
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 1)));
    
    m_cutoff = param("cutoff");
    m_resonance = param("resonance");
    m_drive = param("drive");

	drive()->setValue(1.f);
    cutoff()->setValue(1.f);
    resonance()->setValue(0.f);
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
    //ASSERT(outputBus);

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
    //static std::vector<float> lastFrame;
    //if (lastFrame.size() != m_sampleAccurateCutoffValues.size())
    //{
    //    lastFrame.resize(m_sampleAccurateCutoffValues.size());
    //    memcpy(lastFrame.data(), cutoffs, m_sampleAccurateCutoffValues.size() * sizeof(float));
    //}

    //const float smoothingFactor = .85; 


    for (int i = offset; i < offset + nonSilentFramesToProcess; ++i)
     {
            float input = source[i];

            //float currentCutoff = lastFrame[i];
            //float targetCutoff = cutoffs[i];
            //
            //currentCutoff = currentCutoff + (targetCutoff - currentCutoff) * smoothingFactor;
            //
            //lastFrame[i] = currentCutoff;

            const double f = cutoffs[i] * 1.16;
            const double inputFactor = 0.35013 * (f * f) * (f * f);
            const double fb = resos[i] * (1.0 - 0.15 * f * f);
            
            input -= out4 * fb;
            input *= inputFactor;
            
            out1 = input + 0.3 * in1 + (1 - f) * out1;  // Pole 1
            in1 = input;
            out2 = out1 + 0.3 * in2 + (1 - f) * out2;  // Pole 2
            in2 = out1;
            out3 = out2 + 0.3 * in3 + (1 - f) * out3;  // Pole 3
            in3 = out2;
            out4 = out3 + 0.3 * in4 + (1 - f) * out4;  // Pole 4
            in4 = out3;
            destination[i] = out4;


     }

     outputBus->clearSilentFlag();
}

void MoogFilterNode::reset(ContextRenderLock & r)
{
     in1 = in2 = in3 = in4 = out1 = out2 = out3 = out4 = 0.0;
}

}  // namespace lab
