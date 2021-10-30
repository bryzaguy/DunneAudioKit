// Copyright AudioKit. All Rights Reserved.

#include "SampleBuffer.h"

namespace DunneCore
{

    SampleBuffer::SampleBuffer()
    : samples(0)
    , channelCount(0)
    , sampleCount(0)
    , startPoint(0.0f)
    , endPoint(0.0f)
//    , isLooping(false)
    , isInterleaved(false)
//    , loopStartPoint(0.0f)
//    , loopEndPoint(0.0f)
    {
    }
    
    SampleBuffer::~SampleBuffer()
    {
        deinit();
    }
    
    void SampleBuffer::init(float sampleRate, int channelCount, int sampleCount, bool isInterleaved)
    {
        this->sampleRate = sampleRate;
        this->sampleCount = sampleCount;
        this->channelCount = channelCount;
        this->isInterleaved = isInterleaved;
        startPoint = 0.0f;
    }
    
    void SampleBuffer::deinit()
    {
        samples = 0;
    }
}
