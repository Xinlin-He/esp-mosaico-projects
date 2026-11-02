// SPDX-License-Identifier: Apache-2.0
#pragma once

#define GUITAR_CHORD_COUNT 6
#define GUITAR_STRING_COUNT 6

/* Open strings, low E to high E: E2 A2 D3 G3 B3 E4. Fret -1 is muted. */
int guitar_chord_fret(int chord, int string);
int guitar_chord_midi(int chord, int string);
const char *guitar_chord_name(int chord);
const char *guitar_chord_quality(int chord);
