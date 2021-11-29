// Copyright AudioKit. All Rights Reserved.

#pragma once
#include <math.h>
#include <list>
#include <functional>

#include "Sampler_Typedefs.h"
#include "SampleBuffer.h"
#include "SampleOscillator.h"
#include "ADSREnvelope.h"
#include "AHDSHREnvelope.h"
#include "FunctionTable.h"
#include "ResonantLowPassFilter.h"
#include "LinearRamper.h"

// process samples in "chunks" this size
#define CORESAMPLER_CHUNKSIZE 16 // should probably be set elsewhere - currently only use this for setting up lfo

namespace DunneCore
{
    struct PlayEvent {
        unsigned note;
        float sampleRate, frequency, volume, glideSemitones;
        double increment;
        LoopDescriptor loop;
        std::list<SampleBuffer*> buffers;
        int64_t futureTime;
        bool seen = false;
        enum PlayState {
            INIT = 0,
            CREATED,
            SCHEDULED,
            QUEUED,
            READY,
            PLAYING
        };
        PlayState state = INIT;
        std::function<void()> start;
        bool loopsAreEqual(LoopDescriptor otherLoop) {
            if (loop.isLooping != otherLoop.isLooping ||
                loop.reversed != otherLoop.reversed ||
                loop.loopStartPoint != otherLoop.loopStartPoint ||
                loop.loopEndPoint != otherLoop.loopEndPoint ||
                loop.enabledTracksCount != otherLoop.enabledTracksCount ||
                loop.mutedCount != otherLoop.mutedCount) return false;

            for (int i = 0; i < loop.mutedCount; i++) {
                if (loop.mutedStartPoints[i] != otherLoop.mutedStartPoints[i] ||
                    loop.mutedEndPoints[i] != otherLoop.mutedEndPoints[i]) return false;
            }

            for (int i = 0; i < loop.enabledTracksCount; i++) {
                if (loop.enabledTracks[i] != otherLoop.enabledTracks[i]) return false;
            }
            return true;
        };
        bool equals(PlayEvent event) {
            return (
                event.note == note &&
                event.sampleRate == sampleRate &&
                event.frequency == frequency &&
                event.volume == volume &&
                event.buffers == buffers &&
                loopsAreEqual(event.loop)
            );
        };
    };

    struct SamplerVoice
    {
        PlayEvent scheduled;
        PlayEvent next;
        PlayEvent current;

        float samplingRate;
        /// every voice has 1 oscillator
        SampleOscillator oscillator;

        /// a pointer to the sample buffer for that oscillator
        std::list<SampleBuffer*> sampleBuffers;
        LoopDescriptor currentLoop;
        
        /// two filters (left/right)
        ResonantLowPassFilter leftFilter, rightFilter;
        AHDSHREnvelope ampEnvelope;
        ADSREnvelope filterEnvelope, pitchEnvelope;

        // per-voice vibrato LFO
        FunctionTableOscillator vibratoLFO;

        // restart phase of per-voice vibrato LFO
        bool restartVoiceLFO;

        /// common glide rate, seconds per octave
        float *glideSecPerOctave;

        /// MIDI note number, or -1 if not playing any note
        int noteNumber;

        /// (target) note frequency in Hz
        float noteFrequency;

        /// will reduce to zero during glide
        float glideSemitones;

        /// amount of semitone change via pitch envelope
        float pitchEnvelopeSemitones;

        /// amount of semitone change via voice lfo
        float voiceLFOSemitones;

        /// fraction 0.0 - 1.0, based on MIDI velocity
        float noteVolume;

        // temporary holding variables

        /// Previous note volume while damping note before restarting
        float tempNoteVolume;

        /// Next sample buffer to use at restart
        std::list<SampleBuffer*> newSampleBuffers;
        LoopDescriptor nextLoop;

        /// product of global volume, note volume
        float tempGain;

        /// ramper to smooth subsampled output of adsrEnvelope
        LinearRamper volumeRamper;

        /// true if filter should be used
        bool isFilterEnabled;
        
        SamplerVoice() : noteNumber(-1) {}

        void init(double sampleRate);

        void updateAmpAdsrParameters() { ampEnvelope.updateParams(); }
        void updateFilterAdsrParameters() { filterEnvelope.updateParams(); }
        void updatePitchAdsrParameters() { pitchEnvelope.updateParams(); }
        
        void prepare(unsigned noteNumber,
                   float sampleRate,
                   float frequency,
                   float volume,
                   std::list<SampleBuffer*> sampleBuffers);
        void prepare(unsigned noteNumber,
                   float sampleRate,
                   float frequency,
                   float volume,
                   LoopDescriptor loop,
                   std::list<SampleBuffer*> sampleBuffers);
        void prepare(unsigned noteNumber,
                   float sampleRate,
                   float frequency,
                   float volume,
                   LoopDescriptor loop,
                   std::list<SampleBuffer*> sampleBuffers,
                   std::function<void()> start);

        void play(int64_t offset);
        void start();
        void restartNewNote(unsigned noteNumber, float sampleRate, float frequency, float volume, LoopDescriptor loop, std::list<SampleBuffer*> buffers);
        void restartNewNote(unsigned noteNumber, float sampleRate, float frequency, float volume, std::list<SampleBuffer*> buffers);
        void restartNewNote();
        void restartNewNoteLegato(unsigned noteNumber, float sampleRate, float frequency);
        void restartNewNoteLegato();
        void restartSameNote();
        void restartSameNote(float volume, LoopDescriptor loop, std::list<SampleBuffer*> sampleBuffers);
        void release(bool loopThruRelease);
        void stop();
        
        // return true if amp envelope is finished
        bool prepToGetSamples(int sampleCount,
                              float masterVolume,
                              float pitchOffset,
                              float cutoffMultiple,
                              float keyTracking,
                              float cutoffEnvelopeStrength,
                              float cutoffEnvelopeVelocityScaling,
                              float resLinear,
                              float pitchADSRSemitones,
                              float voiceLFOFrequencyHz,
                              float voiceLFODepthSemitones);

        bool getSamples(int sampleCount, float *leftOutput, float *rightOutput);

    private:
        bool hasStartedVoiceLFO;
        void restartVoiceLFOIfNeeded();
    };

}
