// Copyright AudioKit. All Rights Reserved.

#include "SampleBuffer.h"
#include <Accelerate/Accelerate.h>

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

    void SampleBufferGroup::init(std::list<SampleBuffer*> buffers, LoopDescriptor loop) {
        if (buffers.size() == 0) return;
        
        this->sampleBuffers = buffers;
        auto buffer = buffers.front();
        auto sampleRate = buffer->sampleRate;
        
        *ratio = 1 / loop.speed;

        RubberBand::RubberBandStretcher::Options options = 0;
//        options |= RubberBand::RubberBandStretcher::OptionWindowLong;
//        options |= RubberBand::RubberBandStretcher::OptionDetectorCompound;
        options |= RubberBand::RubberBandStretcher::OptionProcessRealTime;
        options |= RubberBand::RubberBandStretcher::OptionPitchHighQuality;
//        options |= RubberBand::RubberBandStretcher::OptionTransientsMixed;
//        options |= RubberBand::RubberBandStretcher::OptionPhaseIndependent;
//        options |= RubberBand::RubberBandStretcher::OptionThreadingNever;
//        options |= RubberBand::RubberBandStretcher::OptionSmoothingOn;
        options |= RubberBand::RubberBandStretcher::OptionStretchPrecise;
        
        stretcher = new RubberBand::RubberBandStretcher(sampleRate, 2, options, *ratio, 1.0);
        
        *sampleCount = loop.endPoint - loop.startPoint;
        *windowSize = stretcher->getSamplesRequired();
        auto padding = *windowSize - (*sampleCount % *windowSize);
        auto count = *paddedCount = (int)padding + *sampleCount;
        *scaledCount = *sampleCount * *ratio;
        *scaledPaddedCount = *paddedCount * *ratio;

        scaledSamples[0] = new float [*scaledPaddedCount];
        scaledSamples[1] = new float [*scaledPaddedCount];
        memset(scaledSamples[0], 0, *scaledPaddedCount * sizeof(float));
        memset(scaledSamples[1], 0, *scaledPaddedCount * sizeof(float));
        
        float **samples = new float *[2];
        samples[0] = new float[count];
        samples[1] = new float[count];
        memset(samples[0], 0, count * sizeof(float));
        memset(samples[1], 0, count * sizeof(float));

        auto stride = vDSP_Stride(1);
        auto length = vDSP_Length(*sampleCount);

        for (auto buffer : buffers) {
            vDSP_vadd(samples[0], stride, &buffer->samples[loop.startPoint], stride, samples[0], stride, length);
            
            if (buffer->channelCount == 1) {
                vDSP_vadd(samples[1], stride, samples[0], stride, samples[1], stride, length);
            } else {
                vDSP_vadd(samples[1], stride, &buffer->samples[buffer->sampleCount + loop.startPoint], stride, samples[1], stride, length);
            }
        }

        if (loop.reversed) {
            vDSP_vrvrs(samples[0], stride, length);
            vDSP_vrvrs(samples[1], stride, length);
        }

        fade(samples[0], 100, buffer->sampleRate);
        fade(samples[1], 100, buffer->sampleRate);

        channelSamples = samples;
    }
}
