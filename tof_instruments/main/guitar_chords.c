// SPDX-License-Identifier: Apache-2.0

#include "guitar_chords.h"

/* Fret -1 is a muted string and does not sound. The six shapes are the
 * guitar page of mosaico_musical-v2: C G Am F Em D. */
static const int OPEN_MIDI[GUITAR_STRING_COUNT] = {40, 45, 50, 55, 59, 64};
static const int FRETS[GUITAR_CHORD_COUNT][GUITAR_STRING_COUNT] = {
    {-1, 3, 2, 0, 1, 0},
    {3, 2, 0, 0, 0, 3},
    {-1, 0, 2, 2, 1, 0},
    {1, 3, 3, 2, 1, 1},
    {0, 2, 2, 0, 0, 0},
    {-1, -1, 0, 2, 3, 2},
};
static const char *const NAMES[GUITAR_CHORD_COUNT] = {
    "C", "G", "Am", "F", "Em", "D",
};
static const char *const QUALITY[GUITAR_CHORD_COUNT] = {
    "major", "major", "minor", "major", "minor", "major",
};

int guitar_chord_fret(int chord, int string)
{
    if (chord < 0 || chord >= GUITAR_CHORD_COUNT ||
            string < 0 || string >= GUITAR_STRING_COUNT) {
        return -1;
    }
    return FRETS[chord][string];
}

int guitar_chord_midi(int chord, int string)
{
    const int fret = guitar_chord_fret(chord, string);
    if (fret < 0) {
        return -1;
    }
    return OPEN_MIDI[string] + fret;
}

const char *guitar_chord_name(int chord)
{
    if (chord < 0 || chord >= GUITAR_CHORD_COUNT) {
        return "";
    }
    return NAMES[chord];
}

const char *guitar_chord_quality(int chord)
{
    if (chord < 0 || chord >= GUITAR_CHORD_COUNT) {
        return "";
    }
    return QUALITY[chord];
}
