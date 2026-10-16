#pragma once
// Mozzi configuration shared by every file that uses Mozzi's classes (Oscil,
// ADSR, ...). It must come before any Mozzi header, and every file has to see
// the same values.
//
// Only MozziEngine.cpp includes <Mozzi.h> itself. Voices (apps) include this
// header and the Mozzi class headers they need, nothing more.
//
// EXTERNAL_CUSTOM: Mozzi only synthesizes; MozziEngine hands its samples to
// board::speaker, so the codec, pins and shared audio bus keep working as
// before. Mozzi needs power-of-two rates, so it runs at 16384 Hz and the
// engine resamples to the speaker's 16000 Hz.

#include <MozziConfigValues.h>

#define MOZZI_AUDIO_MODE MOZZI_OUTPUT_EXTERNAL_CUSTOM
#define MOZZI_AUDIO_CHANNELS MOZZI_MONO
#define MOZZI_AUDIO_RATE 16384
#define MOZZI_CONTROL_RATE 256
