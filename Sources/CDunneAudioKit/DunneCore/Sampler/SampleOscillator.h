// Copyright AudioKit. All Rights Reserved.

#pragma once
#include <math.h>
#include <list>

#include "Sampler_Typedefs.h"
#include "SampleBuffer.h"

namespace DunneCore
{

    struct SampleOscillator
    {
        bool isLooping;     // true until note released
        double indexPoint;  // use double so we don't lose precision when indexPoint becomes much larger than increment
        double increment;   // 1.0 = play at original speed
        double multiplier;  // multiplier applied to increment for pitch bend, vibrato
        double muteVolume = 1;
        unsigned int muteIndex = 0;
        
        
        void setPitchOffsetSemitones(double semitones) { multiplier = pow(2.0, semitones/12.0); }
        
        // return true if we run out of samples
        inline bool getSample(SampleBuffer *sampleBuffer, int sampleCount, float *output, float gain)
        {
            if (sampleBuffer == NULL || indexPoint > sampleBuffer->endPoint) return true;
            *output = sampleBuffer->interp(indexPoint, gain);
            
            indexPoint += multiplier * increment;
            if (sampleBuffer->isLooping && isLooping)
            {
                if (indexPoint > sampleBuffer->loopEndPoint)
                    indexPoint = indexPoint - sampleBuffer->loopEndPoint + sampleBuffer->loopStartPoint;
            }
            return false;
        }
        
        // return true if we run out of samples
        inline bool getSamplePair(std::list<SampleBuffer*> sampleBuffers, LoopDescriptor loop, int sampleCount, float *leftOutput, float *rightOutput, float gain)
        {
            auto sampleBuffer = sampleBuffers.front();
            auto loopEndPoint = loop.loopEndPoint == 0 ? sampleBuffer->loopEndPoint : loop.loopEndPoint;
            if (sampleBuffer == NULL || indexPoint > sampleBuffer->endPoint) return true;
            
            if (muteIndex < loop.mutedCount) {
                auto start = loop.mutedStartPoints[muteIndex] + loop.loopStartPoint;
                auto end = loop.mutedEndPoints[muteIndex] + loop.loopStartPoint;
                auto fadeSize = (int)sampleBuffer->sampleRate / 100;
                if (indexPoint > start && indexPoint < start + fadeSize) {
                    muteVolume = 1.0 - ((indexPoint - start) / fadeSize);
                } else if (indexPoint > start + fadeSize && indexPoint > end && indexPoint < end + fadeSize) {
                    muteVolume = ((indexPoint - end) / fadeSize);
                } else if (indexPoint == start + fadeSize) {
                    muteVolume = 0;
                } else if (indexPoint == end + fadeSize) {
                    muteVolume = 1;
                    muteIndex++;
                }
            }
            
            *leftOutput = 0;
            *rightOutput = 0;
            for (const auto buffer : sampleBuffers) {
                float left = 0, right = 0;
                buffer->interp(loop.reversed ? buffer->endPoint - indexPoint : indexPoint, &left, &right, gain * muteVolume);
                *leftOutput += left;
                *rightOutput += right;
            }
            
            indexPoint += multiplier * increment;
            if (loop.isLooping && isLooping)
            {
                if (indexPoint > loopEndPoint) {
                    indexPoint = indexPoint - loopEndPoint + loop.loopStartPoint;
                    muteIndex = 0;
                }
            }
            return false;
        }
    };

}
