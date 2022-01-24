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
    , isInterleaved(false)
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

    void SampleBufferGroup::init(std::list<SampleBuffer*> buffers) {
        if (buffers.size() == 0) return;
        
        this->sampleBuffers = buffers;
        
        auto sampleRate = sampleBuffers.front()->sampleRate;

        RubberBand::RubberBandStretcher::Options options = 0;
//        options |= RubberBand::RubberBandStretcher::OptionWindowShort;
        options |= RubberBand::RubberBandStretcher::OptionProcessRealTime;
        options |= RubberBand::RubberBandStretcher::OptionDetectorCompound;
        options |= RubberBand::RubberBandStretcher::OptionThreadingNever;
        options |= RubberBand::RubberBandStretcher::OptionTransientsCrisp;
        double frequencyshift = 1.0;
        double ratio = 1.0;
        RubberBand::RubberBandStretcher ts(sampleRate, 2, options, ratio, frequencyshift);
        
        ts.setMaxProcessSize(16);
        
        stretcher = &ts;
    }
}
