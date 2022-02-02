// Copyright AudioKit. All Rights Reserved.

#include "SamplerVoice.h"
#include <stdio.h>

#define MIDDLE_C_HZ 262.626f

namespace DunneCore
{
    void SamplerVoice::init(double sampleRate)
    {
        samplingRate = float(sampleRate);
        leftFilter.init(sampleRate);
        rightFilter.init(sampleRate);
        ampEnvelope.init();
        filterEnvelope.init();
        pitchEnvelope.init();
        vibratoLFO.waveTable.sinusoid();
        vibratoLFO.init(sampleRate/CORESAMPLER_CHUNKSIZE, 5.0f);
        restartVoiceLFO = false;
        volumeRamper.init(0.0f);
        tempGain = 0.0f;
        next = {};
        current = {};
    }

    void SamplerVoice::prepare(unsigned note, float sampleRate, float frequency, float volume, SampleBufferGroup buffers)
    {
        prepare(note, sampleRate, frequency, volume, this->currentLoop, buffers);
    }

    void SamplerVoice::prepare(unsigned note, float sampleRate, float frequency, float volume, LoopDescriptor loop, SampleBufferGroup buffers)
    {
        prepare(note, sampleRate, frequency, volume, loop, buffers, [this]() { this->start(); });
    }

    void SamplerVoice::prepare(unsigned note, float sampleRate, float frequency, float volume, LoopDescriptor loop, SampleBufferGroup buffers,
        std::function<void()> start)
    {
        PlayEvent event;
        event.note = note;
        event.sampleRate = sampleRate;
        event.frequency = frequency;
        event.volume = volume;
        event.buffers = buffers;
        event.loop = loop;
        unsigned int *mutedEnds = new unsigned int[loop.mutedCount];
        unsigned int *mutedStarts = new unsigned int[loop.mutedCount];
        unsigned int *enabledTracks = new unsigned int[loop.enabledTracksCount];
        memcpy(mutedStarts, loop.mutedStartPoints, sizeof(unsigned int) * loop.mutedCount);
        memcpy(mutedEnds, loop.mutedEndPoints, sizeof(unsigned int) * loop.mutedCount);
        memcpy(enabledTracks, loop.enabledTracks, sizeof(unsigned int) * loop.enabledTracksCount);
        event.loop.mutedStartPoints = mutedStarts;
        event.loop.mutedEndPoints = mutedEnds;
        event.loop.enabledTracks = enabledTracks;
        event.start = start;
        
        auto buffer = buffers.sampleBuffers.front();
        event.increment = (buffer->sampleRate / sampleRate) * (frequency / buffer->noteFrequency);
        
        event.glideSemitones = 0.0f;
        if (*glideSecPerOctave != 0.0f && noteFrequency != 0.0 && noteFrequency != frequency)
        {
            // prepare to glide
            event.glideSemitones = -12.0f * log2f(frequency / noteFrequency);
            if (fabsf(event.glideSemitones) < 0.01f) event.glideSemitones = 0.0f;
        }
        
        if (!current.equals(event) || current.state != PlayEvent::PLAYING) {
            event.state = PlayEvent::CREATED;
        }
        next = event;
    }

    void SamplerVoice::play(int64_t sampleTime)
    {
        next.sampleTime = sampleTime;
    }

    void SamplerVoice::start()
    {
        sampleBuffers = next.buffers;
        currentLoop = next.loop;

        oscillator.indexPoint = 0;
        oscillator.muteIndex = 0;
        oscillator.increment = next.increment;
        oscillator.multiplier = 1.0;
        oscillator.isLooping = next.loop.isLooping;
        
        sampleBuffers.stretcher->reset();
        
        noteVolume = next.volume;
        ampEnvelope.start();
        volumeRamper.init(0.0f);
        
        samplingRate = next.sampleRate;
        leftFilter.updateSampleRate(double(samplingRate));
        rightFilter.updateSampleRate(double(samplingRate));
        filterEnvelope.start();

        pitchEnvelope.start();

        pitchEnvelopeSemitones = 0.0f;

        voiceLFOSemitones = 0.0f;

        glideSemitones = next.glideSemitones;
        noteFrequency = next.frequency;
        noteNumber = next.note;

        restartVoiceLFOIfNeeded();
    }

    void SamplerVoice::restartNewNote(unsigned note, float sampleRate, float frequency, float volume, SampleBufferGroup buffers)
    {
        restartNewNote(note, sampleRate, frequency, volume, this->currentLoop, buffers);
    }

    void SamplerVoice::restartNewNote(unsigned note, float sampleRate, float frequency, float volume, LoopDescriptor loop, SampleBufferGroup buffers)
    {
        prepare(note, sampleRate, frequency, volume, loop, buffers, [this]() { this->restartNewNote(); });
    }

    void SamplerVoice::restartNewNote()
    {
        samplingRate = next.sampleRate;
        leftFilter.updateSampleRate(double(samplingRate));
        rightFilter.updateSampleRate(double(samplingRate));

        oscillator.increment = next.increment;

        pitchEnvelopeSemitones = 0.0f;

        voiceLFOSemitones = 0.0f;

        noteFrequency = next.frequency;
        noteNumber = next.note;
        tempNoteVolume = noteVolume;
        newSampleBuffers = next.buffers;
        nextLoop = next.loop;
        ampEnvelope.restart();
        noteVolume = next.volume;
        filterEnvelope.restart();
        pitchEnvelope.restart();
        restartVoiceLFOIfNeeded();
    }

    void SamplerVoice::restartNewNoteLegato(unsigned note, float sampleRate, float frequency)
    {
        prepare(note, sampleRate, frequency, noteVolume, currentLoop, sampleBuffers, [this]() { this->restartNewNoteLegato(); });
    }

    void SamplerVoice::restartNewNoteLegato()
    {
        samplingRate = next.sampleRate;
        leftFilter.updateSampleRate(double(samplingRate));
        rightFilter.updateSampleRate(double(samplingRate));

        oscillator.increment = next.increment;
        noteFrequency = next.frequency;
        noteNumber = next.note;
    }

    void SamplerVoice::restartSameNote(float volume, LoopDescriptor loop, SampleBufferGroup buffers)
    {
        prepare(noteNumber, samplingRate, noteFrequency, volume, loop, buffers, [this]() { this->restartSameNote(); });
    }

    void SamplerVoice::restartSameNote()
    {
        tempNoteVolume = noteVolume;
        newSampleBuffers = next.buffers;
        nextLoop = next.loop;
        ampEnvelope.restart();
        noteVolume = next.volume;
        filterEnvelope.restart();
        pitchEnvelope.restart();
        restartVoiceLFOIfNeeded();
    }
    
    void SamplerVoice::release(bool loopThruRelease)
    {
        if (!loopThruRelease) oscillator.isLooping = false;
        ampEnvelope.release();
        filterEnvelope.release();
        pitchEnvelope.release();
    }
    
    void SamplerVoice::stop()
    {
        noteNumber = -1;
        ampEnvelope.reset();
        volumeRamper.init(0.0f);
        filterEnvelope.reset();
        pitchEnvelope.reset();
        next = {};
        current = {};
    }

    bool SamplerVoice::prepToGetSamples(int sampleCount, float masterVolume, float pitchOffset,
                                        float cutoffMultiple, float keyTracking,
                                        float cutoffEnvelopeStrength, float cutoffEnvelopeVelocityScaling,
                                        float resLinear, float pitchADSRSemitones,
                                        float voiceLFODepthSemitones, float voiceLFOFrequencyHz)
    {
        if (ampEnvelope.isIdle()) return true;

        if (ampEnvelope.isPreStarting())
        {
            tempGain = masterVolume * tempNoteVolume;
            volumeRamper.reinit(ampEnvelope.getSample(), sampleCount);
            // This can execute as part of the voice-stealing mechanism, and will be executed rarely.
            // To test, set MAX_POLYPHONY in CoreSampler.cpp to something small like 2 or 3.
            if (!ampEnvelope.isPreStarting())
            {
                tempGain = masterVolume * noteVolume;
                volumeRamper.reinit(ampEnvelope.getSample(), sampleCount);
                sampleBuffers = newSampleBuffers;
                currentLoop = nextLoop;
                auto sampleBuffer = sampleBuffers.sampleBuffers.front();
                oscillator.increment = (sampleBuffer->sampleRate / samplingRate) * (noteFrequency / sampleBuffer->noteFrequency);
                oscillator.indexPoint = 0;
                oscillator.muteIndex = 0;
                oscillator.isLooping = nextLoop.isLooping;
                sampleBuffers.stretcher->reset();
            }
        }
        else
        {
            tempGain = masterVolume * noteVolume;
            volumeRamper.reinit(ampEnvelope.getSample(), sampleCount);
        }

        if (*glideSecPerOctave != 0.0f && glideSemitones != 0.0f)
        {
            float seconds = sampleCount / samplingRate;
            float semitones = 12.0f * seconds / *glideSecPerOctave;
            if (glideSemitones < 0.0f)
            {
                glideSemitones += semitones;
                if (glideSemitones > 0.0f) glideSemitones = 0.0f;
            }
            else
            {
                glideSemitones -= semitones;
                if (glideSemitones < 0.0f) glideSemitones = 0.0f;
            }
        }

        float pitchCurveAmount = 1.0f; // >1 = faster curve, 0 < curve < 1 = slower curve - make this a parameter
        if (pitchCurveAmount < 0) { pitchCurveAmount = 0; }
        pitchEnvelopeSemitones = pow(pitchEnvelope.getSample(), pitchCurveAmount) * pitchADSRSemitones;

        vibratoLFO.setFrequency(voiceLFOFrequencyHz);
        voiceLFOSemitones = vibratoLFO.getSample() * voiceLFODepthSemitones;

        float pitchOffsetModified = pitchOffset + glideSemitones + pitchEnvelopeSemitones + voiceLFOSemitones;
        oscillator.setPitchOffsetSemitones(pitchOffsetModified);

        // negative value of cutoffMultiple means filters are disabled
        if (cutoffMultiple < 0.0f)
        {
            isFilterEnabled = false;
        }
        else
        {
            isFilterEnabled = true;
            float noteHz = noteFrequency * powf(2.0f, (pitchOffsetModified) / 12.0f);
            float baseFrequency = MIDDLE_C_HZ + keyTracking * (noteHz - MIDDLE_C_HZ);
            float envStrength = ((1.0f - cutoffEnvelopeVelocityScaling) + cutoffEnvelopeVelocityScaling * noteVolume);
            double cutoffFrequency = baseFrequency * (1.0f + cutoffMultiple + cutoffEnvelopeStrength * envStrength * filterEnvelope.getSample());
            leftFilter.setParameters(cutoffFrequency, resLinear);
            rightFilter.setParameters(cutoffFrequency, resLinear);
        }
        
        return false;
    }

    bool SamplerVoice::getSamples(int sampleCount, float *leftOutput, float *rightOutput)
    {
        for (int i=0; i < sampleCount; i++)
        {
            float gain = tempGain * volumeRamper.getNextValue();
            float leftSample, rightSample;
            if (oscillator.getSamplePair(sampleBuffers, currentLoop, sampleCount, &leftSample, &rightSample, gain))
                return true;
            if (isFilterEnabled)
            {
                *leftOutput++ += leftFilter.process(leftSample);
                *rightOutput++ += rightFilter.process(rightSample);
            }
            else
            {
                *leftOutput++ += leftSample;
                *rightOutput++ += rightSample;
            }
        }

        return false;
    }

    void SamplerVoice::restartVoiceLFOIfNeeded() {
        if (restartVoiceLFO || !hasStartedVoiceLFO) {
            vibratoLFO.phase = 0;
            hasStartedVoiceLFO = true;
        }
    }

}
