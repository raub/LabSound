// License: BSD 2 Clause
// Copyright (C) 2010, Google Inc. All rights reserved.
// Copyright (C) 2015+, The LabSound Authors. All rights reserved.

#include "LabSound/core/AudioArray.h"
#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioNodeInput.h"
#include "LabSound/core/AudioNodeOutput.h"
#include "LabSound/extended/ConstantNode.h"

#include "LabSound/extended/AudioContextLock.h"
#include "LabSound/extended/Registry.h"

#include "internal/Assertions.h"

namespace lab
{

static AudioParamDescriptor s_constantFilterParams[] = {
    {"constantValue", "CONSTANT", 1, 0.0, 20000.0},
    nullptr};
AudioNodeDescriptor * ConstantNode::desc()
{
    static AudioNodeDescriptor d {s_constantFilterParams, nullptr};
    return &d;
}

ConstantNode::ConstantNode(AudioContext & ac)
    : AudioScheduledSourceNode(ac, *desc())
    , m_sampleAccurateConstantValues(AudioNode::ProcessingSizeInFrames)
{
    addInput(std::unique_ptr<AudioNodeInput>(new AudioNodeInput(this)));
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 1)));

    m_constantValue = param("constantValue");
    initialize();
}

ConstantNode::~ConstantNode()
{
    uninitialize();
}

bool ConstantNode::propagatesSilence(ContextRenderLock & r) const
{
    return !isPlayingOrScheduled() || hasFinished();
}

void ConstantNode::process(ContextRenderLock & r, int bufferSize)
{
    return processConstant(r, bufferSize, _self->_scheduler._renderOffset, _self->_scheduler._renderLength);
}

void ConstantNode::processConstant(ContextRenderLock & r, int bufferSize, int offset, int count)
{
    AudioBus * outputBus = output(0)->bus(r);
    ASSERT(outputBus);

    int nonSilentFramesToProcess = count;
    if (!nonSilentFramesToProcess)
    {
        outputBus->zero();
        return;
    }

    if (!isInitialized())
    {
        outputBus->zero();
        return;
    }

    int outputBusChannelCount = outputBus->numberOfChannels();

    if (bufferSize > m_sampleAccurateConstantValues.size()) m_sampleAccurateConstantValues.allocate(bufferSize);

    // fetch the constants
    float * constants = m_sampleAccurateConstantValues.data();
    if (m_constantValue->hasSampleAccurateValues())
    {
        m_constantValue->calculateSampleAccurateValues(r, constants, bufferSize);
    }
    else
    {
        m_constantValue->smooth(r);
        float co = m_constantValue->smoothedValue();
        for (int i = 0; i < bufferSize; ++i) constants[i] = co;
    }

    float * destination = outputBus->channel(0)->mutableData() + offset;
    const double sample_rate = (double) r.context()->sampleRate();

    for (int i = offset; i < offset + nonSilentFramesToProcess; ++i)
    {
        destination[i] = constants[i];
    }

    outputBus->clearSilentFlag();
}

}  // namespace lab
