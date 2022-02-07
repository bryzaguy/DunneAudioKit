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

    std::tuple<float, float> SampleBufferGroup::convert(float speed, float pitch, float varispeed) {
        auto newVarispeed = std::min<float>(std::max<float>(1 / ((varispeed + 24) / 24), 1.0f / 24.0f), 48);
        auto newSpeed = std::min<float>(std::max<float>((1 / ((speed + 24) / 24)) * newVarispeed, 1.0f / 24.0f), 48);
        auto newPitch = std::min<float>(std::max<float>(((pitch + 24) / 24) * (1 / newVarispeed), 1.0f / 24.0f), 48);
        return std::make_tuple(newSpeed, newPitch);
    }

    void SampleBufferGroup::update(float speed, float pitch, float varispeed) {
        auto converted = convert(speed, pitch, varispeed);
        auto newSpeed = std::get<0>(converted);
        auto newPitch = std::get<1>(converted);
        
        if (stretcher->getTimeRatio() != newSpeed) {
            stretcher->setTimeRatio(newSpeed);
        }

        if (stretcher->getPitchScale() != newPitch) {
            stretcher->setPitchScale(newPitch);
        }
    }

    void SampleBufferGroup::init(std::list<SampleBuffer*> buffers, LoopDescriptor loop) {
        if (buffers.size() == 0) return;
        
        this->sampleBuffers = buffers;
        auto buffer = buffers.front();
        auto sampleRate = buffer->sampleRate;
        
        auto converted = convert(loop.speed, loop.pitch, loop.varispeed);
        auto ratio = std::get<0>(converted);
        auto pitch = std::get<1>(converted);

        RubberBand::RubberBandStretcher::Options options = 0;
        options |= RubberBand::RubberBandStretcher::OptionProcessRealTime;
        options |= RubberBand::RubberBandStretcher::OptionChannelsTogether;
        options |= RubberBand::RubberBandStretcher::OptionStretchPrecise;
        
        stretcher = new RubberBand::RubberBandStretcher(sampleRate, 2, options, ratio, pitch);

        auto count = *sampleCount = loop.endPoint - loop.startPoint;

        scaledSamples[0] = new float [1] {0};
        scaledSamples[1] = new float [1] {0};
        
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

        channelSamples = samples;
    }
}
