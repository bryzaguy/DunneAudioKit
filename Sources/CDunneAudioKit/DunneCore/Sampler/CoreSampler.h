// Copyright AudioKit. All Rights Reserved.

#ifdef __cplusplus
#ifdef _WIN32
#include "Sampler_Typedefs.h"
#include <memory>
#else
#import "Sampler_Typedefs.h"
#import <memory>
#endif

#import <list>
#include "SampleBuffer.h"

// process samples in "chunks" this size
#define CORESAMPLER_CHUNKSIZE 16


namespace DunneCore {
    struct SamplerVoice;
    struct KeyMappedSampleBuffer;
}

class CoreSampler
{
public:
    CoreSampler();
    ~CoreSampler();
    
    int ident;
    /// returns system error code, nonzero only if a problem occurs
    int init(double sampleRate);
    
    /// call this to un-load all samples and clear the keymap
    void deinit();
    
    /// call before/after loading/unloading samples, to ensure none are in use
    void stopAllVoices();
    void restartVoices();
    
    /// call to load samples
    void loadSampleData(SampleDataDescriptor& sdd);

    /// call to unload samples, freeing memory
    void unloadAllSamples();
    
    // after loading samples, call one of these to build the key map
    
    /// call for noteNumber 0-127 to define tuning table (defaults to standard 12-tone equal temperament)
    void setNoteFrequency(int noteNumber, float noteFrequency);
    
    /// use this when you have full key mapping data (min/max note, vel)
    void buildKeyMap(void);
    
    /// use this when you don't have full key mapping data (min/max note, vel)
    void buildSimpleKeyMap(void);
    
    /// optionally call this to make samples continue looping after note-release
    void setLoopThruRelease(bool value) { loopThruRelease = value; }

    void play(int64_t sampleTime);
    void stop(unsigned noteNumber, bool immediate, int64_t offset);
    
    void prepareNote(unsigned noteNumber, unsigned velocity, LoopDescriptor loop);
    void stopNote(unsigned noteNumber, bool immediate);
    void sustainPedal(bool down);
    
    void render(unsigned channelCount, unsigned sampleCount, float *outBuffers[], int64_t now);
    void renderVoice(bool allowSampleRunout, float cutoffMul, float *pOutLeft, float *pOutRight, DunneCore::SamplerVoice *pVoice, float pitchDev, unsigned int sampleCount);
    void extracted(bool allowSampleRunout, float cutoffMul, int nn, float *pOutLeft, float *pOutRight, DunneCore::SamplerVoice *pVoice, float pitchDev, unsigned int sampleCount);
    
    void extracted(bool allowSampleRunout, float cutoffMul, float *pOutLeft, float *pOutRight, DunneCore::SamplerVoice *pVoice, float pitchDev, unsigned int sampleCount);

    void  setADSRAttackDurationSeconds(float value);
    float getADSRAttackDurationSeconds(void);
    void  setADSRHoldDurationSeconds(float value);
    float getADSRHoldDurationSeconds(void);
    void  setADSRDecayDurationSeconds(float value);
    float getADSRDecayDurationSeconds(void);
    void  setADSRSustainFraction(float value);
    float getADSRSustainFraction(void);
    void  setADSRReleaseHoldDurationSeconds(float value);
    float getADSRReleaseHoldDurationSeconds(void);
    void  setADSRReleaseDurationSeconds(float value);
    float getADSRReleaseDurationSeconds(void);

    void  setFilterAttackDurationSeconds(float value);
    float getFilterAttackDurationSeconds(void);
    void  setFilterDecayDurationSeconds(float value);
    float getFilterDecayDurationSeconds(void);
    void  setFilterSustainFraction(float value);
    float getFilterSustainFraction(void);
    void  setFilterReleaseDurationSeconds(float value);
    float getFilterReleaseDurationSeconds(void);

    void  setPitchAttackDurationSeconds(float value);
    float getPitchAttackDurationSeconds(void);
    void  setPitchDecayDurationSeconds(float value);
    float getPitchDecayDurationSeconds(void);
    void  setPitchSustainFraction(float value);
    float getPitchSustainFraction(void);
    void  setPitchReleaseDurationSeconds(float value);
    float getPitchReleaseDurationSeconds(void);
    
protected:
    // current sampling rate, samples/sec
    // not named sampleRate to avoid clashing with AudioKit's sampleRate
    float currentSampleRate;
    
    struct InternalData;
    std::unique_ptr<InternalData> data;
    
    bool isKeyMapValid;
    
    // simple parameters
    bool isFilterEnabled, restartVoiceLFO;
    
    // performance parameters
    float masterVolume, pitchOffset, vibratoDepth, vibratoFrequency,
    voiceVibratoDepth, voiceVibratoFrequency, glideRate, speed, pitch, varispeed;
    
    // parameters for mono-mode only
    
    // default false
    bool isMonophonic;
    
    // true if notes shouldn't retrigger in mono mode
    bool isLegato;
    
    // semitones/sec
    float portamentoRate;
    
    // mono-mode state
    unsigned lastPlayedNoteNumber;
    float lastPlayedNoteFrequency;
    
    // per-voice filter parameters
    
    // multiple of note frequency - 1.0 means cutoff at fundamental
    float cutoffMultiple;

    // key tracking factor: 1.0 means perfect key tracking, 0.0 means none; may be e.g. -2.0 to +2.0
    float keyTracking;
    
    // how much filter EG adds on top of cutoffMultiple
    float cutoffEnvelopeStrength;
    
    /// fraction 0.0 - 1.0, scaling note volume's effect on cutoffEnvelopeStrength
    float filterEnvelopeVelocityScaling;

    // resonance [-20 dB, +20 dB] becomes linear [10.0, 0.1]
    float linearResonance;

    // how much pitch ADSR adds on top of pitch
    float pitchADSRSemitones;
    
    // sample-related parameters
    
    // if true, sample continue looping thru note release phase
    bool loopThruRelease;
    
    // temporary state
    bool stoppingAllVoices;
    
    // helper functions
    DunneCore::SamplerVoice *voicePlayingNote(unsigned noteNumber);
    DunneCore::SampleBufferGroup lookupSamples(unsigned noteNumber, unsigned velocity, LoopDescriptor loop);
    void prepare(unsigned noteNumber,
              unsigned velocity,
              bool anotherKeyWasDown,
              LoopDescriptor loop);
    void stop(unsigned noteNumber, bool immediate);
};

#endif
