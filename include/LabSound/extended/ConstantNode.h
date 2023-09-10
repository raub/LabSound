// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2020+, The LabSound Authors. All rights reserved.

#ifndef lab_constant_node_h
#define lab_constant_node_h

#include "LabSound/core/AudioArray.h"
#include "LabSound/core/AudioParam.h"
#include "LabSound/core/AudioScheduledSourceNode.h"
#include "LabSound/core/Macros.h"

namespace lab
{

class AudioBus;
class AudioContext;
class AudioSetting;

class ConstantNode : public AudioScheduledSourceNode
{
    virtual double tailTime(ContextRenderLock & r) const override { return 0; }
    virtual double latencyTime(ContextRenderLock & r) const override { return 0; }
    virtual bool propagatesSilence(ContextRenderLock & r) const override;

public:
    ConstantNode(AudioContext & ac);
    virtual ~ConstantNode();

    static const char * static_name() { return "Constant"; }
    virtual const char * name() const override { return static_name(); }
    static AudioNodeDescriptor * desc();

    virtual void process(ContextRenderLock &, int bufferSize) override;
    virtual void reset(ContextRenderLock &) override { }

    std::shared_ptr<AudioParam> constantValue() { return m_constantValue; }

    std::shared_ptr<AudioParam> m_constantValue;  // default 1.0

    void processConstant(ContextRenderLock & r, int bufferSize, int offset, int count);

    AudioFloatArray m_sampleAccurateConstantValues;
};

}  // namespace lab

#endif  // lab_constant_node_h
