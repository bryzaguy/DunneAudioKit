// Copyright AudioKit. All Rights Reserved.

// This file is safe to include in either (Objective-)C or C++ contexts.

#pragma once

typedef struct
{
    bool isLooping, reversed;
    unsigned int loopStartPoint, loopEndPoint,
        enabledTracksCount, *enabledTracks,
        mutedCount, *mutedStartPoints, *mutedEndPoints;

} LoopDescriptor;

typedef struct
{
    int noteNumber;
    float noteFrequency;
    
    int minimumNoteNumber, maximumNoteNumber;
    int minimumVelocity, maximumVelocity;
    
//    bool isLooping;
//    float loopStartPoint, loopEndPoint;
    float startPoint, endPoint;

} SampleDescriptor;

typedef struct
{
    SampleDescriptor sampleDescriptor;
    
    float sampleRate;
    bool isInterleaved;
    int channelCount;
    int sampleCount;
    float *data;

} SampleDataDescriptor;

typedef struct
{
    SampleDescriptor sampleDescriptor;
    
    const char *path;
    
} SampleFileDescriptor;
