

// For starting the WTF library
#include <wtf/ExportMacros.h>
#include "MainThread.h"

// webaudio specific headers
#include "AudioBufferSourceNode.h"
#include "AudioContext.h"
#include "BiquadFilterNode.h"
#include "ConvolverNode.h"
#include "ExceptionCode.h"
#include "GainNode.h"
#include "MediaStream.h"
#include "MediaStreamAudioSourceNode.h"
#include "OscillatorNode.h"
#include "PannerNode.h"

// LabSound
#include "RecorderNode.h"
#include "SoundBuffer.h"

// Examples
#include "ToneAndSample.h"
#include "ToneAndSampleRecorded.h"
#include "LiveEcho.h"

#include <unistd.h>
#include <iostream>

using namespace WebCore;
using LabSound::RecorderNode;

namespace WebCore
{
    class Document { public: };
}


int main(int, char**)
{
    // Initialize threads for the WTF library
    WTF::initializeThreading();
    WTF::initializeMainThread();
    
    // Create an audio context object
    Document d;
    ExceptionCode ec;
    RefPtr<AudioContext> context = AudioContext::create(&d, ec);

#if 1
    toneAndSample(context, 3.0f);
#elif 0
    toneAndSampleRecorded(context, 3.0f, "toneAndSample.raw");
#elif 0
    liveEcho(context, 3.0f);
#elif 0
    //--------------------------------------------------------------
    // Play a sound file through a reverb convolution
    //
    SoundBuffer ir(context, "impulse-responses/tim-warehouse/cardiod-rear-35-10/cardiod-rear-levelled.wav");
    //SoundBuffer ir(context, "impulse-responses/filter-telephone.wav");
    
    SoundBuffer sample(context, "human-voice.mp4");
    
    RefPtr<ConvolverNode> convolve = context->createConvolver();
    convolve->setBuffer(ir.audioBuffer.get());
    RefPtr<GainNode> wetGain = context->createGain();
    wetGain->gain()->setValue(2.f);
    RefPtr<GainNode> dryGain = context->createGain();
    dryGain->gain()->setValue(1.f);
    
    convolve->connect(wetGain.get(), 0, 0, ec);
    wetGain->connect(context->destination(), 0, 0, ec);
    dryGain->connect(context->destination(), 0, 0, ec);
    
    sample.play(convolve.get(), 0);
    
    std::cout << "Starting convolved echo" << std::endl;
    
    for (int i = 0; i < 300; ++i)
        usleep(100000);
    std::cout << "Ending echo" << std::endl;
#elif 0
    //--------------------------------------------------------------
    // Play live audio through a reverb convolution
    //
    SoundBuffer ir(context, "impulse-responses/tim-warehouse/cardiod-rear-35-10/cardiod-rear-levelled.wav");
    //SoundBuffer ir(context, "impulse-responses/filter-telephone.wav");
    
    RefPtr<MediaStreamAudioSourceNode> input = context->createMediaStreamSource(new MediaStream(), ec);
    RefPtr<ConvolverNode> convolve = context->createConvolver();
    convolve->setBuffer(ir.audioBuffer.get());
    RefPtr<GainNode> wetGain = context->createGain();
    wetGain->gain()->setValue(2.f);
    RefPtr<GainNode> dryGain = context->createGain();
    dryGain->gain()->setValue(1.f);
    
    input->connect(convolve.get(), 0, 0, ec);
    convolve->connect(wetGain.get(), 0, 0, ec);
    wetGain->connect(context->destination(), 0, 0, ec);
    dryGain->connect(context->destination(), 0, 0, ec);

    RefPtr<RecorderNode> recorder = RecorderNode::create(context.get(), 44100);
    recorder->startRecording();
    dryGain->connect(recorder.get(), 0, 0, ec);
    wetGain->connect(recorder.get(), 0, 0, ec);
    
    std::cout << "Starting convolved echo" << std::endl;
    
    for (int i = 0; i < 100; ++i)
        usleep(100000);
    std::cout << "Ending echo" << std::endl;
    
    recorder->stopRecording();
    recorder->save("livetest.raw");
#elif 0
    //--------------------------------------------------------------
    // Demonstrate 3d spatialization and doppler shift
    //
    SoundBuffer train(context, "trainrolling.wav");
    RefPtr<PannerNode> panner = context->createPanner();
    panner->connect(context->destination(), 0, 0, ec);
    PassRefPtr<AudioBufferSourceNode> trainNode = train.play(panner.get(), 0.0f);
    trainNode->setLooping(true);
    context->listener()->setPosition(0, 0, 0);
    panner->setVelocity(15, 0, 0);
    for (float i = 0; i < 10.0f; i += 0.01f) {
        float x = (i - 5.0f) / 5.0f;
        // keep it a bit up and in front, because if it goes right through the listener
        // at (0, 0, 0) it abruptly switches from left to right.
        panner->setPosition(x, 0.1f, 0.1f);
        usleep(10000);
    }
#elif 0
    //--------------------------------------------------------------
    // Play a rhythm
    //
    SoundBuffer kick(context, "kick.wav");
    SoundBuffer hihat(context, "hihat.wav");
    SoundBuffer snare(context, "snare.wav");

    float startTime = 0;
    float eighthNoteTime = 1.0f/4.0f;
    for (int bar = 0; bar < 2; bar++) {
        float time = startTime + bar * 8 * eighthNoteTime;
        // Play the bass (kick) drum on beats 1, 5
        kick.play(time);
        kick.play(time + 4 * eighthNoteTime);
        
        // Play the snare drum on beats 3, 7
        snare.play(time + 2 * eighthNoteTime);
        snare.play(time + 6 * eighthNoteTime);
        
        // Play the hi-hat every eighth note.
        for (int i = 0; i < 8; ++i) {
            hihat.play(time + i * eighthNoteTime);
        }
    }
    for (int i = 0; i < 300; ++i)
        usleep(10000);
#elif 0
    //--------------------------------------------------------------
    // Play a rhythm through a low pass filter
    //
    SoundBuffer kick(context, "kick.wav");
    SoundBuffer hihat(context, "hihat.wav");
    SoundBuffer snare(context, "snare.wav");
    
    RefPtr<BiquadFilterNode> filter = context->createBiquadFilter();
    filter->setType(BiquadFilterNode::LOWPASS, ec);
    filter->frequency()->setValue(500.0f);
    filter->connect(context->destination(), 0, 0, ec);
    
    float startTime = 0;
    float eighthNoteTime = 1.0f/4.0f;
    for (int bar = 0; bar < 2; bar++) {
        float time = startTime + bar * 8 * eighthNoteTime;
        // Play the bass (kick) drum on beats 1, 5
        kick.play(filter.get(), time);
        kick.play(filter.get(), time + 4 * eighthNoteTime);
        
        // Play the snare drum on beats 3, 7
        snare.play(filter.get(), time + 2 * eighthNoteTime);
        snare.play(filter.get(), time + 6 * eighthNoteTime);
        
        // Play the hi-hat every eighth note.
        for (int i = 0; i < 8; ++i) {
            hihat.play(filter.get(), time + i * eighthNoteTime);
        }
    }
    for (float i = 0; i < 5.0f; i += 0.01f) {
        usleep(10000);
    }
#else
    //--------------------------------------------------------------
    // Demonstrate panning between a rhythm and a tone
    //
    RefPtr<OscillatorNode> oscillator = context->createOscillator();

    SoundBuffer kick(context, "kick.wav");
    SoundBuffer hihat(context, "hihat.wav");
    SoundBuffer snare(context, "snare.wav");
    
    RefPtr<GainNode> oscGain = context->createGain();
    oscillator->connect(oscGain.get(), 0, 0, ec);
    oscGain->connect(context->destination(), 0, 0, ec);
    oscGain->gain()->setValue(1.0f);
    oscillator->start(0);
    
    RefPtr<GainNode> drumGain = context->createGain();
    drumGain->connect(context->destination(), 0, 0, ec);
    drumGain->gain()->setValue(1.0f);
    
    float startTime = 0;
    float eighthNoteTime = 1.0f/4.0f;
    for (int bar = 0; bar < 10; bar++) {
        float time = startTime + bar * 8 * eighthNoteTime;
        // Play the bass (kick) drum on beats 1, 5
        kick.play(drumGain.get(), time);
        kick.play(drumGain.get(), time + 4 * eighthNoteTime);
        
        // Play the snare drum on beats 3, 7
        snare.play(drumGain.get(), time + 2 * eighthNoteTime);
        snare.play(drumGain.get(), time + 6 * eighthNoteTime);
        
        // Play the hi-hat every eighth note.
        for (int i = 0; i < 8; ++i) {
            hihat.play(drumGain.get(), time + i * eighthNoteTime);
        }
    }
    
    // update gain at 10ms intervals
    for (float i = 0; i < 10.0f; i += 0.01f) {
        float t1 = i / 10.0f;
        float t2 = 1.0f - t1;
        float gain1 = cosf(t1 * 0.5f * M_PI);
        float gain2 = cosf(t2 * 0.5f * M_PI);
        oscGain->gain()->setValue(gain1);
        drumGain->gain()->setValue(gain2);
        usleep(10000);
    }
#endif
    
    return 0;
}
