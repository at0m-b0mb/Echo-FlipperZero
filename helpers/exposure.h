#pragma once

/**
 * exposure - how much one device gave away, on a scale a stranger understands.
 *
 * The input is everything Echo learned about a single radio; the output is a
 * number out of a hundred and a letter. The scoring is deliberately blunt and
 * deliberately explainable: every point that lands is attributable to one of
 * five columns, and the dossier screen prints them.
 *
 * Two rules bend the arithmetic on purpose, and both are stated in the output
 * so nobody has to guess:
 *
 *   floor  - a device that leaked even one network name cannot score better
 *            than B, however clean the rest of it is. One name is one place
 *            you have been.
 *   cap    - a home or health network named from a permanent MAC cannot score
 *            better than D. That pair is an address attached to a person.
 *
 * No Flipper headers: this compiles and runs on a laptop under `make -C test`.
 */

#include <stdbool.h>
#include <stdint.h>

#include "probe_intel.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How sure Echo is that two MAC addresses are the same physical radio. */
typedef enum {
    ExpoLinkNone = 0,
    /** Same probe fingerprint. Two of one phone model look alike, so: likely. */
    ExpoLinkLikely,
    /**
     * Fingerprint plus a sequence counter that carried on across the change of
     * address. A counter does not continue by coincidence.
     */
    ExpoLinkConfirmed,
} ExpoLink;

typedef enum {
    ExpoGradeAPlus = 0,
    ExpoGradeA,
    ExpoGradeB,
    ExpoGradeC,
    ExpoGradeD,
    ExpoGradeF,
    ExpoGradeCount,
} ExpoGrade;

typedef struct {
    /** Distinct named networks this device asked for by name. */
    uint16_t named_ssids;
    /** Every probe heard from it, named or not. Not scored - shown. */
    uint16_t probe_count;
    /** Bit per PiCat seen, built with expo_cat_bit(). */
    uint32_t cat_mask;
    /** Some network name carried what looks like a person's name. */
    bool personal_name;
    /** Some network name is distinctive enough to resolve to one address. */
    bool geolocatable;
    /** The address it is using right now is locally administered. */
    bool mac_randomized;
    /** The OUI resolved - only ever true for a real, permanent address. */
    bool vendor_known;
    uint8_t link_confidence; /* ExpoLink */
    /** Addresses tied to this radio, including the current one. */
    uint8_t linked_macs;
} ExpoInput;

#define EXPO_MAX_NOTES 6

typedef struct {
    uint8_t score; /* 0..100, higher is more exposed */
    uint8_t grade; /* ExpoGrade */

    /* the five columns, so the score can be read rather than trusted */
    uint8_t p_volume; /* how many names came out */
    uint8_t p_sensitivity; /* what kind of places they were */
    uint8_t p_geo; /* whether a name pins an address */
    uint8_t p_identity; /* whether it names a person or a manufacturer */
    uint8_t p_track; /* whether the device can be followed */

    bool floor_applied;
    bool cap_applied;

    const char* notes[EXPO_MAX_NOTES]; /* static strings, safe to keep */
    uint8_t note_count;
} ExpoResult;

/** Bit for a category, for building ExpoInput.cat_mask. */
static inline uint32_t expo_cat_bit(PiCat cat) {
    return (uint32_t)1u << (uint32_t)cat;
}

/** Score one device. Pure - same input, same answer, no allocation. */
ExpoResult expo_score(const ExpoInput* in);

/**
 * The band a raw score falls in. expo_score() applies this itself; it is
 * exported so tests and the mockup tooling can check the two agree.
 */
ExpoGrade expo_grade_for_score(uint8_t score);

/** "A+" .. "F". Never NULL. */
const char* expo_grade_str(ExpoGrade grade);

/** Headline for the grade: "Wide open." Never NULL. */
const char* expo_verdict(ExpoGrade grade);

/** What the owner of this phone could do about it. Never NULL. */
const char* expo_advice(ExpoGrade grade);

#ifdef __cplusplus
}
#endif
