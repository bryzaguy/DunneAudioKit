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
        double fadeGain = 0;

        void setPitchOffsetSemitones(double semitones) { multiplier = pow(2.0, semitones/12.0); }
        
        // return true if we run out of samples
        inline bool getSamplePair(SampleBufferGroup sampleBuffers, LoopDescriptor loop, int sampleCount, float *leftOutput, float *rightOutput, float gain)
        {
            auto sampleBuffer = sampleBuffers.sampleBuffers.front();
            auto loopEndPoint = loop.loopEndPoint == 0 ? sampleBuffer->endPoint : loop.loopEndPoint;
            if (sampleBuffer == NULL || indexPoint > sampleBuffer->endPoint) {
                muteIndex = 0;
                return true;
            }
            
            auto fadeSize = (int)sampleBuffer->sampleRate / 100;
            if (muteIndex < loop.mutedCount) {
                auto start = loop.mutedStartPoints[muteIndex] + loop.loopStartPoint;
                auto end = loop.mutedEndPoints[muteIndex] + loop.loopStartPoint;
                if (indexPoint == start + fadeSize) {
                    muteVolume = 0;
                } else if (indexPoint == end + fadeSize) {
                    muteVolume = 1;
                    muteIndex++;
                } else if (indexPoint > start && indexPoint < start + fadeSize) {
                    muteVolume = 1.0 - ((indexPoint - start) / fadeSize);
                } else if (indexPoint > start + fadeSize && indexPoint > end && indexPoint < end + fadeSize) {
                    muteVolume = ((indexPoint - end) / fadeSize);
                }
            }
            
            if (indexPoint <= loop.loopStartPoint + fadeSize) {
                fadeGain = (indexPoint - loop.loopStartPoint) / fadeSize;
            } else if (indexPoint >= loop.loopEndPoint - fadeSize) {
                fadeGain = 1.0 - ((indexPoint - (loop.loopEndPoint - fadeSize)) / fadeSize);
            }
            
            *leftOutput = 0;
            *rightOutput = 0;
            
            auto finalGain = gain * muteVolume * fadeGain;
            
            if (finalGain > 0) {
                float left = 0, right = 0;
                auto reversedIndex = loop.loopEndPoint - (indexPoint - loop.loopStartPoint);
                sampleBuffers.interp(loop.reversed ? reversedIndex : indexPoint, &left, &right);
                if (loop.phaseInvert) {
                    *leftOutput += left * -1 * finalGain;
                    *rightOutput += right * -1 * finalGain;
                } else {
                    *leftOutput += left * finalGain;
                    *rightOutput += right * finalGain;
                }
            }
            
            indexPoint += multiplier * increment;
            if (loop.isLooping && isLooping)
            {
                if (indexPoint >= loopEndPoint) {
                    indexPoint = indexPoint - loopEndPoint + loop.loopStartPoint;
                    muteIndex = 0;
                }
            }
            return false;
        }
    };

}
