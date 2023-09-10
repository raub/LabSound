// License: BSD 2 Clause
// Copyright (C) 2011, Google Inc. All rights reserved.
// Copyright (C) 2015+, The LabSound Authors. All rights reserved.

#ifndef MoogFilterNode_h
#define MoogFilterNode_h

#include "LabSound/core/AudioArray.h"
#include "LabSound/core/AudioNode.h"
#include "LabSound/core/AudioParam.h"

namespace lab
{

class AudioContext;

// GainNode is an AudioNode with one input and one output which applies a gain (volume) change to the audio signal.
// De-zippering (smoothing) is applied when the gain value is changed dynamically.
//
// params: gain
// settings:
//
class MoogFilterNode : public AudioNode
{
public:
    MoogFilterNode(AudioContext & ac);
    virtual ~MoogFilterNode();

    static const char * static_name() { return "MoogFilter"; }
    virtual const char * name() const override { return static_name(); }
    static AudioNodeDescriptor * desc();

    // AudioNode
    virtual void process(ContextRenderLock &, int bufferSize) override;
    virtual void reset(ContextRenderLock &) override;

    std::shared_ptr<AudioParam> cutoff() const { return m_cutoff; }
    std::shared_ptr<AudioParam> resonance() const { return m_resonance; }
    std::shared_ptr<AudioParam> drive() const { return m_drive; }

    //double in1, in2, in3, in4;
    //double out1, out2, out3, out4;


    //double drive;

    void processMoogFilter(ContextRenderLock & r, int bufferSize, int offset, int count);

protected:
    double V[4];
    double dV[4];
    double tV[4];

    double x;
    double g;

    virtual double tailTime(ContextRenderLock & r) const override { return 0; }
    virtual double latencyTime(ContextRenderLock & r) const override { return 0; }

    std::shared_ptr<AudioParam> m_cutoff;
    std::shared_ptr<AudioParam> m_resonance;
    std::shared_ptr<AudioParam> m_drive;

    AudioFloatArray m_sampleAccurateCutoffValues;
    AudioFloatArray m_sampleAccurateResonanceValues;
    AudioFloatArray m_sampleAccurateDriveValues;
};

}  // namespace lab

#endif  // GainNode_h
