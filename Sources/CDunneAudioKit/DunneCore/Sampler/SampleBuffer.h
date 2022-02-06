// Copyright AudioKit. All Rights Reserved.

#pragma once
#include <list>
#include <math.h>       /* isnan, sqrt */

#include "Sampler_Typedefs.h"
#include "../RubberBand/rubberband/RubberBandStretcher.h"

namespace DunneCore
{
    // SampleBuffer represents an array of sample data, which can be addressed with a real-valued
    // "index" via linear interpolation.
    struct SampleBuffer
    {
        float *samples;
        float sampleRate;
        int channelCount;
        int sampleCount;
        float startPoint, endPoint;
        bool isInterleaved;
        float noteFrequency;

        SampleBuffer();
        ~SampleBuffer();
        
        void init(float sampleRate, int channelCount, int sampleCount, bool isInterleaved);
        void deinit();
    };
    
    class SampleBufferGroup {
    public:
        std::list<SampleBuffer*> sampleBuffers;
        RubberBand::RubberBandStretcher* stretcher;
        float **channelSamples = 0;
        float **processSamples = new float *[2];
        size_t *processPosition = new size_t(0);
        float **scaledSamples = new float *[2];
        size_t *sampleCount = new size_t;
        
        double fadeTime = 100.0;
        double sampleTime = 1.0 / 48000.0;
        double power = exp(log(10) * sampleTime / fadeTime);

        void init(std::list<SampleBuffer*> buffers, LoopDescriptor loop);
        void update(float speed, float pitch, float varispeed);
        std::tuple<float, float> convert(float speed, float pitch, float varispeed);
        
        inline float convertSpeed(float value) {
            return std::min<float>(std::max<float>(1 / ((value + 24) / 24), 1.0f / 24.0f), 48);
        }
        
        inline float convertPitch(float value) {
            return std::min<float>(std::max<float>(((value + 24) / 24), 1.0f / 24.0f), 48);
        }
        
        inline void indices(double index, int *a, int *b, double *diff) {
            int pIndex = int(index);
            *diff = index - pIndex;
            *a = pIndex;
            *b = *a + 1;
        }
        
        // Use double for the real-valued index, because oscillators will need the extra precision.
        inline float interp(float *samples, size_t count, double index)
        {
            int a, b;
            double diff;
            indices(index, &a, &b, &diff);
            
            float si = samples[a % count];
            float sj = samples[b % count];
            return (float)((1.0 - diff) * si + diff * sj);
        }
        
        inline void interp(float **samples, size_t count, double fIndex, float *leftOutput, float *rightOutput)
        {
            if (samples == 0)
            {
                *leftOutput = *rightOutput = 0.0f;
                return;
            }
    
            *leftOutput = interp(samples[0], count, fIndex);
            *rightOutput = interp(samples[1], count, fIndex);
        }
        
        inline double fade(int index) {
            size_t fadeOutIndex = *sampleCount - fadeTime - 1;
            double gain = 0.01;
            
            if (index < fadeTime) {
                return gain * pow(index + 1, power);
            } else if (index > fadeOutIndex) {
                return gain * pow(fadeTime - (index - fadeOutIndex), power);
            } else {
                return 1;
            }
        }

        inline void process(int index) {
            if (index == 0) {
                stretcher->reset();
                *processPosition = 0;
            }

            while (stretcher->available() < 1) {
                auto minSize = stretcher->getSamplesRequired();
                auto samplesLeft = *sampleCount - *processPosition;
                auto size = std::min(std::min(minSize, samplesLeft), *sampleCount);

                processSamples[0] = &channelSamples[0][*processPosition];
                processSamples[1] = &channelSamples[1][*processPosition];

                stretcher->process(processSamples, size, false);
                *processPosition = (*processPosition + size) % *sampleCount;
            }
            
            stretcher->retrieve(scaledSamples, 1);
        }

        inline void interp(float *leftSample, float *rightSample, double *indexPoint, double increment, double multiplier, LoopDescriptor loop) {
            auto sampleBuffer = sampleBuffers.front();
            auto index = int(*indexPoint);
            process(index);

//            float left = 0, right = 0;
//            interp(scaledSamples, *scaledCount, index, &left, &right);
            float left = scaledSamples[0][0];
            float right = scaledSamples[1][0];
//            float left = channelSamples[0][int(*indexPoint)];
//            float right = channelSamples[1][int(*indexPoint)];

            if (isnan(left)) {
                left = 0;
            }
            if (isnan(right)) {
                right = 0;
            }
            
            auto fadeGain = fade(index);
            
            *leftSample += left * fadeGain;
            *rightSample += right * fadeGain;
            
            *indexPoint += increment * multiplier;

            if (loop.isLooping && *indexPoint >= *sampleCount) {
                *indexPoint = 0;
            }
        }
    };

    // KeyMappedSampleBuffer is a derived version with added MIDI note-number and velocity ranges
    struct KeyMappedSampleBuffer : public SampleBuffer
    {
        // Any of these members may be negative, meaning "no value assigned"
        int noteNumber;     // closest MIDI note-number to this sample's frequency (noteFrequency)
        int minimumNoteNumber, maximumNoteNumber;     // bounding note numbers for mapping
        int minimumVelocity, maximumVelocity;       // min/max MIDI velocities for mapping
    };

}
