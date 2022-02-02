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
        float **retrieveSamples = new float *[2];
        float **scaledSamples = new float *[2];
        float *ratio = new float(1);
        size_t *windowSize = new size_t;
        size_t *sampleCount = new size_t;
        size_t *paddedCount = new size_t;
        size_t *scaledCount = new size_t;
        size_t *scaledPaddedCount = new size_t;
        size_t *processPosition = new size_t(0);
        size_t *retrievePosition = new size_t(0);

        void init(std::list<SampleBuffer*> buffers, LoopDescriptor loop);
        
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
        
        inline void fade(float *samples, double fadeTime, double sampleRate) {
            auto sampleTime = 1.0 / sampleRate;
            auto power = exp(log(10) * sampleTime / fadeTime);
            auto gain = 0.01;
            size_t endIndex = *sampleCount - 1;
            
            for (size_t i = 0; i < fadeTime; i++) {
                gain *= power;
                samples[i] = samples[i] * float(gain);
                samples[endIndex - i] = samples[endIndex - i] * float(gain);
            }
        }

        inline void process(int index) {
            if (*retrievePosition == *scaledCount) {
                stretcher->reset();
                *processPosition = 0;
                *retrievePosition = 0;
            }

            while (stretcher->available() < *windowSize) {
                auto minSize = stretcher->getSamplesRequired();
                auto samplesLeft = *paddedCount - *processPosition;
                auto size = minSize > samplesLeft ? minSize + samplesLeft : minSize;

                processSamples[0] = &channelSamples[0][*processPosition];
                processSamples[1] = &channelSamples[1][*processPosition];

                stretcher->process(processSamples, size, false);
                *processPosition = (*processPosition + size) % *paddedCount;
            }
            
            // Retrieve is one step ahead to interpolate between frames.
            if (*retrievePosition == index) {
                retrieve(*retrievePosition, 1);
            }

            retrieve(*retrievePosition, 1);
        }
        
        inline void retrieve(size_t index, size_t count) {
            retrieveSamples[0] = &scaledSamples[0][index];
            retrieveSamples[1] = &scaledSamples[1][index];
            stretcher->retrieve(retrieveSamples, count);
            *retrievePosition = (*retrievePosition + count) % *scaledPaddedCount;
        }

        inline void interp(float *leftSample, float *rightSample, double *indexPoint, double increment, double multiplier, LoopDescriptor loop) {
            auto sampleBuffer = sampleBuffers.front();
            auto endIndex = *sampleCount - 1;
            auto scaledIndex = int(*indexPoint);
            process(scaledIndex);

            float left = 0, right = 0;
            interp(scaledSamples, *scaledCount, scaledIndex, &left, &right);
//            float left = scaledSamples[0][scaledIndex];
//            float right = scaledSamples[1][scaledIndex];
//            float left = channelSamples[0][int(*indexPoint)];
//            float right = channelSamples[1][int(*indexPoint)];

            if (isnan(left) || isnan(right)) {
                printf("WOAHT?");
            }
            
            *leftSample += left;
            *rightSample += right;
            
            *indexPoint += increment * multiplier;

            if (loop.isLooping && *indexPoint >= endIndex) {
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
