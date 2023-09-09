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

static AudioParamDescriptor s_moogFilterParams[] = {{"cutoff", "CUTOFF", 1.0, 0.0, 1.0}, {"resonance", "RESONANCE", 1.0, 0.0, 4.0}, nullptr};
AudioNodeDescriptor * MoogFilterNode::desc()
{
    static AudioNodeDescriptor d {s_moogFilterParams, nullptr};
    return &d;
}

MoogFilterNode::MoogFilterNode(AudioContext & ac)
    : AudioNode(ac, *desc())
    , m_sampleAccurateCutoffValues(AudioNode::ProcessingSizeInFrames)
    , m_sampleAccurateResonanceValues(AudioNode::ProcessingSizeInFrames)
{
    in1 = in2 = in3 = in4 = 0.0;
    out1 = out2 = out3 = out4 = 0.0;

    addInput(std::unique_ptr<AudioNodeInput>(new AudioNodeInput(this)));
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 1)));
    
    m_cutoff = param("cutoff");
    m_resonance = param("resonance");
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

    // FIXME: for some cases there is a nice optimization to avoid processing here, and let the gain change
    // happen in the summing junction input of the AudioNode we're connected to.
    // Then we can avoid all of the following:

    AudioBus * outputBus = output(0)->bus(r);
    ASSERT(outputBus);

    int nonSilentFramesToProcess = count;
    if (!nonSilentFramesToProcess)
    {
        outputBus->zero();
        return;
    }

    //const float sample_rate = r.context()->sampleRate();
    //polyblep->setSampleRate(sample_rate);
    
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
    
    float * destination = outputBus->channel(0)->mutableData() + offset;
    const float * source = inputBus->channel(0)->data();
    for (int i = offset; i < offset + nonSilentFramesToProcess; ++i)
     {
        double input = source[i];
        double fc = cutoffs[i];
        double res = resos[i];
        //std::cout << "fc:" << fc << ", res:" << res << std::endl;
        double f = fc * 1.16;
        double fb = res * (1.0 - 0.15 * f * f);
        input -= out4 * fb;
        input *= 0.35013 * (f * f) * (f * f);
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
    // Snap directly to desired gain.
    in1 = in2 = in3 = in4 = 0.0;
    out1 = out2 = out3 = out4 = 0.0;
}

}  // namespace lab
