// Copyright AudioKit. All Rights Reserved.

#include "CoreSampler.h"
#include "SamplerVoice.h"
#include "FunctionTable.h"
#include "SustainPedalLogic.h"

#include <math.h>
#include <list>
#include <map>

// number of voices
#define MAX_POLYPHONY 64

// MIDI offers 128 distinct note numbers
#define MIDI_NOTENUMBERS 128

// Convert MIDI note to Hz, for 12-tone equal temperament
#define NOTE_HZ(midiNoteNumber) ( 440.0f * pow(2.0f, ((midiNoteNumber) - 69.0f)/12.0f) )

struct CoreSampler::InternalData {
    // list of (pointers to) all loaded samples
    std::list<DunneCore::KeyMappedSampleBuffer*> sampleBufferList;
    
    // maps MIDI note numbers to "closest" samples (all velocity layers)
    std::list<DunneCore::KeyMappedSampleBuffer*> keyMap[MIDI_NOTENUMBERS];
    std::map<unsigned, DunneCore::SampleBufferGroup> sampleBufferGroups;
    
    DunneCore::AHDSHREnvelopeParameters ampEnvelopeParameters;
    DunneCore::ADSREnvelopeParameters filterEnvelopeParameters;
    DunneCore::ADSREnvelopeParameters pitchEnvelopeParameters;
    
    // table of voice resources
    DunneCore::SamplerVoice voice[MAX_POLYPHONY];
    
    // one vibrato LFO shared by all voices
    DunneCore::FunctionTableOscillator vibratoLFO;
    
    DunneCore::SustainPedalLogic pedalLogic;
    
    std::list<DunneCore::SamplerVoice*> preparedVoices;
    
    // tuning table
    float tuningTable[128];
};

CoreSampler::CoreSampler()
: currentSampleRate(48000.0f)    // sensible guess
, ident(0)
, isKeyMapValid(false)
, isFilterEnabled(false)
, restartVoiceLFO(false)
, masterVolume(1.0f)
, pitchOffset(0.0f)
, vibratoDepth(0.0f)
, vibratoFrequency(5.0f)
, voiceVibratoDepth(0.0f)
, voiceVibratoFrequency(5.0f)
, glideRate(0.0f)   // 0 sec/octave means "no glide"
, isMonophonic(false)
, isLegato(false)
, portamentoRate(1.0f)
, cutoffMultiple(4.0f)
, keyTracking(1.0f)
, cutoffEnvelopeStrength(20.0f)
, filterEnvelopeVelocityScaling(0.0f)
, linearResonance(0.5f)
, pitchADSRSemitones(0.0f)
, loopThruRelease(false)
, stoppingAllVoices(false)
, data(new InternalData)
{
    DunneCore::SamplerVoice *pVoice = data->voice;
    for (int i=0; i < MAX_POLYPHONY; i++, pVoice++)
    {
        pVoice->ampEnvelope.pParameters = &data->ampEnvelopeParameters;
        pVoice->filterEnvelope.pParameters = &data->filterEnvelopeParameters;
        pVoice->pitchEnvelope.pParameters = &data->pitchEnvelopeParameters;
        pVoice->noteFrequency = 0.0f;
        pVoice->glideSecPerOctave = &glideRate;
    }
    
    for (int i=0; i < 128; i++)
        data->tuningTable[i] = NOTE_HZ(i);
}

CoreSampler::~CoreSampler()
{
    unloadAllSamples();
}

int CoreSampler::init(double sampleRate)
{
    currentSampleRate = (float)sampleRate;
    data->ampEnvelopeParameters.updateSampleRate((float)(sampleRate/CORESAMPLER_CHUNKSIZE));
    data->filterEnvelopeParameters.updateSampleRate((float)(sampleRate/CORESAMPLER_CHUNKSIZE));
    data->pitchEnvelopeParameters.updateSampleRate((float)(sampleRate/CORESAMPLER_CHUNKSIZE));
    data->vibratoLFO.waveTable.sinusoid();
    data->vibratoLFO.init(sampleRate/CORESAMPLER_CHUNKSIZE, 5.0f);
    
    for (int i=0; i<MAX_POLYPHONY; i++)
        data->voice[i].init(sampleRate);
    return 0;   // no error
}

void CoreSampler::deinit()
{
}

void CoreSampler::unloadAllSamples()
{
    isKeyMapValid = false;
    for (DunneCore::KeyMappedSampleBuffer *pBuf : data->sampleBufferList)
        delete pBuf;
    data->sampleBufferList.clear();
    for (int i=0; i < MIDI_NOTENUMBERS; i++)
        data->keyMap[i].clear();
}

void CoreSampler::loadSampleData(SampleDataDescriptor& sdd)
{
    DunneCore::KeyMappedSampleBuffer *pBuf = new DunneCore::KeyMappedSampleBuffer();
    pBuf->minimumNoteNumber = sdd.sampleDescriptor.minimumNoteNumber;
    pBuf->maximumNoteNumber = sdd.sampleDescriptor.maximumNoteNumber;
    pBuf->minimumVelocity = sdd.sampleDescriptor.minimumVelocity;
    pBuf->maximumVelocity = sdd.sampleDescriptor.maximumVelocity;
    data->sampleBufferList.push_back(pBuf);
    
    pBuf->init(sdd.sampleRate, sdd.channelCount, sdd.sampleCount, sdd.isInterleaved);
    pBuf->samples = sdd.data;
    pBuf->noteNumber = sdd.sampleDescriptor.noteNumber;
    pBuf->noteFrequency = sdd.sampleDescriptor.noteFrequency;

    if (sdd.sampleDescriptor.startPoint > 0.0f) pBuf->startPoint = sdd.sampleDescriptor.startPoint;
    if (sdd.sampleDescriptor.endPoint > 0.0f)   pBuf->endPoint = sdd.sampleDescriptor.endPoint;
}

DunneCore::SampleBufferGroup CoreSampler::lookupSamples(unsigned noteNumber, unsigned velocity, LoopDescriptor loop)
{
    std::list<DunneCore::SampleBuffer*> result;
    const auto buffers = data->keyMap[noteNumber];
    bool enabled_tracks[buffers.size()];
    memset(enabled_tracks, false, buffers.size() * sizeof(bool));
    
    for(int i = 0; i < loop.enabledTracksCount; i++) {
        enabled_tracks[loop.enabledTracks[i]] = true;
    }
    
    // common case: only one sample mapped to this note - return it immediately
    if (buffers.size() == 1 && enabled_tracks[0] == true) {
        result.push_back(buffers.front());
    }
    else {
        // search samples mapped to this note for best choice based on velocity
        auto iter = buffers.begin();
        for (int i = 0; i < buffers.size(); i++, iter++) {
            auto pBuf = *iter;
            // if sample does not have velocity range, accept it trivially
            if (enabled_tracks[i] && ((pBuf->minimumVelocity < 0 || pBuf->maximumVelocity < 0) ||
                ((int)velocity >= pBuf->minimumVelocity && (int)velocity <= pBuf->maximumVelocity)))
                result.push_back(pBuf);
        }
    }
    
    DunneCore::SampleBufferGroup group;
    group.init(result, loop);
    auto findGroup = data->sampleBufferGroups.find(noteNumber);
    if (findGroup != data->sampleBufferGroups.end()) {
        findGroup->second = group;
        return findGroup->second;
    } else {
        data->sampleBufferGroups.insert({noteNumber, group});
        return group;
    }
}

void CoreSampler::setNoteFrequency(int noteNumber, float noteFrequency)
{
    data->tuningTable[noteNumber] = noteFrequency;
}

// re-compute keyMap[] so every MIDI note number is automatically mapped to the sample buffer
// closest in pitch
void CoreSampler::buildSimpleKeyMap()
{
    // clear out the old mapping entirely
    isKeyMapValid = false;
    for (int i=0; i < MIDI_NOTENUMBERS; i++)
        data->keyMap[i].clear();
    
    for (int nn=0; nn < MIDI_NOTENUMBERS; nn++)
    {
        float noteFreq = data->tuningTable[nn];
        
        // scan loaded samples to find the minimum distance to note nn
        float minDistance = 1000000.0f;
        for (DunneCore::KeyMappedSampleBuffer *pBuf : data->sampleBufferList)
        {
            float distance = fabsf(NOTE_HZ(pBuf->noteNumber) - noteFreq);
            if (distance < minDistance)
            {
                minDistance = distance;
            }
        }
        
        // scan again to add only samples at this distance to the list for note nn
        for (DunneCore::KeyMappedSampleBuffer *pBuf : data->sampleBufferList)
        {
            float distance = fabsf(NOTE_HZ(pBuf->noteNumber) - noteFreq);
            if (distance == minDistance)
            {
                data->keyMap[nn].push_back(pBuf);
            }
        }
    }
    isKeyMapValid = true;
}

// rebuild keyMap based on explicit mapping data in samples
void CoreSampler::buildKeyMap(void)
{
    // clear out the old mapping entirely
    isKeyMapValid = false;
    for (int i=0; i < MIDI_NOTENUMBERS; i++) data->keyMap[i].clear();
    
    for (int nn=0; nn < MIDI_NOTENUMBERS; nn++)
    {
        float noteFreq = data->tuningTable[nn];
        for (DunneCore::KeyMappedSampleBuffer *pBuf : data->sampleBufferList)
        {
            float minFreq = NOTE_HZ(pBuf->minimumNoteNumber);
            float maxFreq = NOTE_HZ(pBuf->maximumNoteNumber);
            if (noteFreq >= minFreq && noteFreq <= maxFreq)
                data->keyMap[nn].push_back(pBuf);
        }
    }
    isKeyMapValid = true;
}

DunneCore::SamplerVoice *CoreSampler::voicePlayingNote(unsigned noteNumber)
{
    for (int i=0; i < MAX_POLYPHONY; i++)
    {
        DunneCore::SamplerVoice *pVoice = &data->voice[i];
        if (pVoice->noteNumber == noteNumber) return pVoice;
    }
    return 0;
}

void CoreSampler::play(int64_t sampleTime)
{
    for (auto &voice : data->preparedVoices)
    {
        voice->play(sampleTime);
    }
    data->preparedVoices.clear();
}

void CoreSampler::prepareNote(unsigned noteNumber, unsigned velocity, LoopDescriptor loop)
{
    bool anotherKeyWasDown = data->pedalLogic.isAnyKeyDown();
    data->pedalLogic.keyDownAction(noteNumber);
    prepare(noteNumber, velocity, anotherKeyWasDown, loop);
}

void CoreSampler::stopNote(unsigned noteNumber, bool immediate)
{
    if (immediate || data->pedalLogic.keyUpAction(noteNumber))
        stop(noteNumber, immediate);
}

void CoreSampler::sustainPedal(bool down)
{
    if (down) data->pedalLogic.pedalDown();
    else {
        for (int nn=0; nn < MIDI_NOTENUMBERS; nn++)
        {
            if (data->pedalLogic.isNoteSustaining(nn))
                stop(nn, false);
        }
        data->pedalLogic.pedalUp();
    }
}

void CoreSampler::prepare(unsigned noteNumber, unsigned velocity, bool anotherKeyWasDown, LoopDescriptor loop)
{
    if (stoppingAllVoices) return;
 
    auto pBufs = lookupSamples(noteNumber, velocity, loop);
    if (pBufs.sampleBuffers.size() == 0) return;  // don't crash if someone forgets to build map

    float noteFrequency = data->tuningTable[noteNumber];
    
    // sanity check: ensure we are initialized with at least one buffer
    if (!isKeyMapValid || data->sampleBufferList.size() == 0) return;
    
    if (isMonophonic)
    {
        if (isLegato && anotherKeyWasDown)
        {
            // is our one and only voice playing some note?
            DunneCore::SamplerVoice *pVoice = &data->voice[0];
            if (pVoice->noteNumber >= 0)
            {
                pVoice->restartNewNoteLegato(noteNumber, currentSampleRate, noteFrequency);
            }
            else
            {
                pVoice->prepare(noteNumber, currentSampleRate, noteFrequency, velocity / 127.0f, loop, pBufs);
            }
            data->preparedVoices.push_back(pVoice);
            lastPlayedNoteNumber = noteNumber;
            return;
        }
        else
        {
            // monophonic but not legato: always start a new note
            DunneCore::SamplerVoice *pVoice = &data->voice[0];
            if (pVoice->noteNumber >= 0)
                pVoice->restartNewNote(noteNumber, currentSampleRate, noteFrequency, velocity / 127.0f, loop, pBufs);
            else
                pVoice->prepare(noteNumber, currentSampleRate, noteFrequency, velocity / 127.0f, loop, pBufs);
            
            data->preparedVoices.push_back(pVoice);
            lastPlayedNoteNumber = noteNumber;
            return;
        }
    }
    
    else // polyphonic
    {
        // is any voice already playing this note?
        DunneCore::SamplerVoice *pVoice = voicePlayingNote(noteNumber);
        if (pVoice)
        {
            // re-start the note
            pVoice->restartSameNote(velocity / 127.0f, loop, pBufs);
            data->preparedVoices.push_back(pVoice);
            return;
        }
        
        // find a free voice (with noteNumber < 0) to play the note
        int polyphony = isMonophonic ? 1 : MAX_POLYPHONY;
        for (int i = 0; i < polyphony; i++)
        {
            DunneCore::SamplerVoice *pVoice = &data->voice[i];
            if (pVoice->noteNumber < 0)
            {
                pVoice->prepare(noteNumber, currentSampleRate, noteFrequency, velocity / 127.0f, loop, pBufs);
                data->preparedVoices.push_back(pVoice);
                lastPlayedNoteNumber = noteNumber;
                return;
            }
        }
    }
}

void CoreSampler::stop(unsigned noteNumber, bool immediate)
{
    DunneCore::SamplerVoice *pVoice = voicePlayingNote(noteNumber);
    if (pVoice == 0) return;

    if (immediate)
    {
        pVoice->stop();
    }
    else if (isMonophonic)
    {
        int key = data->pedalLogic.firstKeyDown();
        if (key < 0) pVoice->release(loopThruRelease);
        else if (isLegato) pVoice->restartNewNoteLegato((unsigned)key, currentSampleRate, data->tuningTable[key]);
        else
        {
            unsigned velocity = 100;
            auto pBufs = lookupSamples(key, velocity, pVoice->currentLoop);
            if (pBufs.sampleBuffers.size() == 0) return;  // don't crash if someone forgets to build map
            if (pVoice->noteNumber >= 0)
                pVoice->restartNewNote(key, currentSampleRate, data->tuningTable[key], velocity / 127.0f, pBufs);
            else
                pVoice->prepare(key, currentSampleRate, data->tuningTable[key], velocity / 127.0f, pBufs);
        }
    }
    else
    {
        pVoice->release(loopThruRelease);
    }
}

void CoreSampler::stopAllVoices()
{
    // Lock out starting any new notes, and tell Render() to stop all active notes
    stoppingAllVoices = true;
    
    // Wait until Render() has killed all active notes
    bool noteStillSounding = true;
    while (noteStillSounding)
    {
        noteStillSounding = false;
        for (int i=0; i < MAX_POLYPHONY; i++)
            if (data->voice[i].noteNumber >= 0) noteStillSounding = true;
    }
}

void CoreSampler::restartVoices()
{
    // Allow starting new notes again
    stoppingAllVoices = false;
}

void CoreSampler::renderVoice(bool allowSampleRunout, float cutoffMul, float *pOutLeft, float *pOutRight, DunneCore::SamplerVoice *pVoice, float pitchDev, unsigned int sampleCount) {
    int nn = pVoice->noteNumber;
    if (nn >= 0)
    {
        if (stoppingAllVoices ||
            pVoice->prepToGetSamples(sampleCount, masterVolume, pitchDev, cutoffMul, keyTracking,
                                     cutoffEnvelopeStrength, filterEnvelopeVelocityScaling, linearResonance,
                                     pitchADSRSemitones, voiceVibratoDepth, voiceVibratoFrequency, speed, pitch, varispeed) ||
            (pVoice->getSamples(sampleCount, pOutLeft, pOutRight) && allowSampleRunout))
        {
            stopNote(nn, true);
        }
    }
}

void CoreSampler::render(unsigned channelCount, unsigned sampleCount, float *outBuffers[], int64_t now)
{
    float *pOutLeft = outBuffers[0];
    float *pOutRight = outBuffers[1];
    data->vibratoLFO.setFrequency(vibratoFrequency);
    float pitchDev = this->pitchOffset + vibratoDepth * data->vibratoLFO.getSample();
    float cutoffMul = isFilterEnabled ? cutoffMultiple : -1.0f;
    
    bool allowSampleRunout = !(isMonophonic && isLegato);

    DunneCore::SamplerVoice *pVoice = &data->voice[0];
    for (int i=0; i < MAX_POLYPHONY; i++, pVoice++)
    {
        pVoice->restartVoiceLFO = restartVoiceLFO;

        auto nextTime = pVoice->next.sampleTime;

        if (pVoice->next.state == DunneCore::PlayEvent::CREATED && nextTime >= now) {
            auto offset = nextTime - now >= 0 || nextTime - now < sampleCount ? (unsigned int)(nextTime - now) : 0;
            if (offset > 0) {
                renderVoice(allowSampleRunout, cutoffMul, pOutLeft, pOutRight, pVoice, pitchDev, offset);

                pOutLeft += offset;
                pOutRight += offset;
            }

            pVoice->oscillator.indexPoint = double(size_t((now + offset) - nextTime) % *pVoice->sampleBuffers.sampleCount);
            pVoice->next.start();
            pVoice->next.state = DunneCore::PlayEvent::PLAYING;
            pVoice->current = pVoice->next;
            
            renderVoice(allowSampleRunout, cutoffMul, pOutLeft, pOutRight, pVoice, pitchDev, sampleCount - offset);
        } else {
            renderVoice(allowSampleRunout, cutoffMul, pOutLeft, pOutRight, pVoice, pitchDev, sampleCount);
        }
    }
}

void  CoreSampler::setADSRAttackDurationSeconds(float value)
{
    data->ampEnvelopeParameters.setAttackDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateAmpAdsrParameters();
}

float CoreSampler::getADSRAttackDurationSeconds(void)
{
    return data->ampEnvelopeParameters.getAttackDurationSeconds();
}

void  CoreSampler::setADSRHoldDurationSeconds(float value)
{
    data->ampEnvelopeParameters.setHoldDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateAmpAdsrParameters();
}

float CoreSampler::getADSRHoldDurationSeconds(void)
{
    return data->ampEnvelopeParameters.getHoldDurationSeconds();
}

void  CoreSampler::setADSRDecayDurationSeconds(float value)
{
    data->ampEnvelopeParameters.setDecayDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateAmpAdsrParameters();
}

float CoreSampler::getADSRDecayDurationSeconds(void)
{
    return data->ampEnvelopeParameters.getDecayDurationSeconds();
}

void  CoreSampler::setADSRSustainFraction(float value)
{
    data->ampEnvelopeParameters.sustainFraction = value;
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateAmpAdsrParameters();
}

float CoreSampler::getADSRSustainFraction(void)
{
    return data->ampEnvelopeParameters.sustainFraction;
}

void  CoreSampler::setADSRReleaseHoldDurationSeconds(float value)
{
    data->ampEnvelopeParameters.setReleaseHoldDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateAmpAdsrParameters();
}

float CoreSampler::getADSRReleaseHoldDurationSeconds(void)
{
    return data->ampEnvelopeParameters.getReleaseHoldDurationSeconds();
}

void  CoreSampler::setADSRReleaseDurationSeconds(float value)
{
    data->ampEnvelopeParameters.setReleaseDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateAmpAdsrParameters();
}

float CoreSampler::getADSRReleaseDurationSeconds(void)
{
    return data->ampEnvelopeParameters.getReleaseDurationSeconds();
}

void  CoreSampler::setFilterAttackDurationSeconds(float value)
{
    data->filterEnvelopeParameters.setAttackDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateFilterAdsrParameters();
}

float CoreSampler::getFilterAttackDurationSeconds(void)
{
    return data->filterEnvelopeParameters.getAttackDurationSeconds();
}

void  CoreSampler::setFilterDecayDurationSeconds(float value)
{
    data->filterEnvelopeParameters.setDecayDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateFilterAdsrParameters();
}

float CoreSampler::getFilterDecayDurationSeconds(void)
{
    return data->filterEnvelopeParameters.getDecayDurationSeconds();
}

void  CoreSampler::setFilterSustainFraction(float value)
{
    data->filterEnvelopeParameters.sustainFraction = value;
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateFilterAdsrParameters();
}

float CoreSampler::getFilterSustainFraction(void)
{
    return data->filterEnvelopeParameters.sustainFraction;
}

void  CoreSampler::setFilterReleaseDurationSeconds(float value)
{
    data->filterEnvelopeParameters.setReleaseDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updateFilterAdsrParameters();
}

float CoreSampler::getFilterReleaseDurationSeconds(void)
{
    return data->filterEnvelopeParameters.getReleaseDurationSeconds();
}


void  CoreSampler::setPitchAttackDurationSeconds(float value)
{
    data->pitchEnvelopeParameters.setAttackDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updatePitchAdsrParameters();
}

float CoreSampler::getPitchAttackDurationSeconds(void)
{
    return data->pitchEnvelopeParameters.getAttackDurationSeconds();
}

void  CoreSampler::setPitchDecayDurationSeconds(float value)
{
    data->pitchEnvelopeParameters.setDecayDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updatePitchAdsrParameters();
}

float CoreSampler::getPitchDecayDurationSeconds(void)
{
    return data->pitchEnvelopeParameters.getDecayDurationSeconds();
}

void  CoreSampler::setPitchSustainFraction(float value)
{
    data->pitchEnvelopeParameters.sustainFraction = value;
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updatePitchAdsrParameters();
}

float CoreSampler::getPitchSustainFraction(void)
{
    return data->pitchEnvelopeParameters.sustainFraction;
}

void  CoreSampler::setPitchReleaseDurationSeconds(float value)
{
    data->pitchEnvelopeParameters.setReleaseDurationSeconds(value);
    for (int i = 0; i < MAX_POLYPHONY; i++) data->voice[i].updatePitchAdsrParameters();
}

float CoreSampler::getPitchReleaseDurationSeconds(void)
{
    return data->pitchEnvelopeParameters.getReleaseDurationSeconds();
}
