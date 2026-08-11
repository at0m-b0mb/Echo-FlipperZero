#include "exposure.h"

/* Column ceilings. They add to exactly 100 so the score needs no rescaling. */
#define EXPO_MAX_VOLUME      30
#define EXPO_MAX_SENSITIVITY 25
#define EXPO_MAX_GEO         10
#define EXPO_MAX_IDENTITY    10
#define EXPO_MAX_TRACK       25

/* The first name costs more than the ones after it: going from nothing to
 * something is the big step, and the tenth hotel adds less than the first. */
#define EXPO_FIRST_NAME  12
#define EXPO_EXTRA_NAME  7

/* Bend points, both reported in the result. */
#define EXPO_FLOOR_ANY_NAME 20 /* one name -> no better than B */
#define EXPO_CAP_HOME_REAL  45 /* home or health on a real MAC -> no better than D */

/* Grade thresholds, upper bound of each band. */
static const uint8_t EXPO_BANDS[ExpoGradeCount] = {
    [ExpoGradeAPlus] = 0,
    [ExpoGradeA] = 9,
    [ExpoGradeB] = 24,
    [ExpoGradeC] = 44,
    [ExpoGradeD] = 64,
    [ExpoGradeF] = 100,
};

static uint8_t expo_clamp(uint32_t v, uint32_t max) {
    return (uint8_t)((v > max) ? max : v);
}

static bool expo_has(uint32_t mask, PiCat cat) {
    return (mask & expo_cat_bit(cat)) != 0u;
}

static void expo_note(ExpoResult* r, const char* text) {
    if(r->note_count < EXPO_MAX_NOTES) r->notes[r->note_count++] = text;
}

ExpoGrade expo_grade_for_score(uint8_t score) {
    for(uint8_t g = 0; g < ExpoGradeCount; g++) {
        if(score <= EXPO_BANDS[g]) return (ExpoGrade)g;
    }
    return ExpoGradeF;
}

ExpoResult expo_score(const ExpoInput* in) {
    ExpoResult r;
    for(uint8_t i = 0; i < EXPO_MAX_NOTES; i++) r.notes[i] = NULL;
    r.note_count = 0;
    r.floor_applied = false;
    r.cap_applied = false;

    /* ---- how many names came out ---- */
    if(in->named_ssids == 0u) {
        r.p_volume = 0;
    } else {
        uint32_t v = EXPO_FIRST_NAME + EXPO_EXTRA_NAME * (uint32_t)(in->named_ssids - 1u);
        r.p_volume = expo_clamp(v, EXPO_MAX_VOLUME);
    }

    /* ---- and what kind of places they were ---- */
    uint8_t worst = 0;
    if(in->named_ssids > 0u) {
        for(uint8_t c = 0; c < (uint8_t)PiCatCount; c++) {
            if(expo_has(in->cat_mask, (PiCat)c)) {
                uint8_t w = pi_cat_weight((PiCat)c);
                if(w > worst) worst = w;
            }
        }
    }
    r.p_sensitivity = expo_clamp((uint32_t)worst * 5u, EXPO_MAX_SENSITIVITY);

    /* ---- does any of it point at one address ---- */
    r.p_geo = (in->named_ssids > 0u && in->geolocatable) ? EXPO_MAX_GEO : 0;

    /* ---- does it name a person, or at least a manufacturer ---- */
    if(in->personal_name) {
        r.p_identity = EXPO_MAX_IDENTITY;
    } else if(in->vendor_known) {
        r.p_identity = 6;
    } else {
        r.p_identity = 0;
    }

    /* ---- and can it be followed out of the room ---- */
    if(!in->mac_randomized) {
        r.p_track = EXPO_MAX_TRACK;
    } else if(in->link_confidence == (uint8_t)ExpoLinkConfirmed) {
        r.p_track = 14;
    } else if(in->link_confidence == (uint8_t)ExpoLinkLikely) {
        r.p_track = 7;
    } else {
        r.p_track = 0;
    }

    uint32_t total = (uint32_t)r.p_volume + r.p_sensitivity + r.p_geo + r.p_identity + r.p_track;
    uint8_t score = expo_clamp(total, 100u);

    /* ---- the two bends ---- */
    if(in->named_ssids > 0u && score < EXPO_FLOOR_ANY_NAME) {
        score = EXPO_FLOOR_ANY_NAME;
        r.floor_applied = true;
    }

    bool home_or_health =
        expo_has(in->cat_mask, PiCatHome) || expo_has(in->cat_mask, PiCatMedical);
    if(in->named_ssids > 0u && home_or_health && !in->mac_randomized &&
       score < EXPO_CAP_HOME_REAL) {
        score = EXPO_CAP_HOME_REAL;
        r.cap_applied = true;
    }

    r.score = score;
    r.grade = (uint8_t)expo_grade_for_score(score);

    /* ---- the short version, most alarming first ---- */
    if(in->named_ssids > 0u && home_or_health) {
        expo_note(
            &r,
            expo_has(in->cat_mask, PiCatMedical) ? "Named a health site" :
                                                   "Named its home network");
    }
    if(in->personal_name) expo_note(&r, "A name is in an SSID");
    if(in->named_ssids > 0u && in->geolocatable) {
        expo_note(&r, "A name pins one address");
    }
    if(!in->mac_randomized) {
        expo_note(&r, in->vendor_known ? "Real MAC, vendor known" : "Real MAC, trackable");
    } else if(in->link_confidence == (uint8_t)ExpoLinkConfirmed) {
        expo_note(&r, "Randomization defeated");
    } else if(in->link_confidence == (uint8_t)ExpoLinkLikely) {
        expo_note(&r, "Fingerprint match nearby");
    } else {
        expo_note(&r, "Randomization working");
    }
    if(in->named_ssids == 0u) {
        expo_note(&r, "No network names leaked");
    }

    return r;
}

static const char* const EXPO_GRADE_STR[ExpoGradeCount] = {
    [ExpoGradeAPlus] = "A+",
    [ExpoGradeA] = "A",
    [ExpoGradeB] = "B",
    [ExpoGradeC] = "C",
    [ExpoGradeD] = "D",
    [ExpoGradeF] = "F",
};

static const char* const EXPO_VERDICT[ExpoGradeCount] = {
    [ExpoGradeAPlus] = "Silent. Nothing learned.",
    [ExpoGradeA] = "Almost nothing given up.",
    [ExpoGradeB] = "A small leak.",
    [ExpoGradeC] = "Followable.",
    [ExpoGradeD] = "Places you have been.",
    [ExpoGradeF] = "Wide open.",
};

static const char* const EXPO_ADVICE[ExpoGradeCount] = {
    [ExpoGradeAPlus] = "Nothing to fix. This is\nwhat good looks like.",
    [ExpoGradeA] = "Little to fix. Keep the\nprivate address on.",
    [ExpoGradeB] = "Forget networks you no\nlonger use.",
    [ExpoGradeC] = "Turn on private Wi-Fi\naddresses per network.",
    [ExpoGradeD] = "Forget old networks. Turn\non a private address.",
    [ExpoGradeF] = "Forget saved networks and\nrandomize the address.",
};

const char* expo_grade_str(ExpoGrade grade) {
    if(grade >= ExpoGradeCount) return EXPO_GRADE_STR[ExpoGradeF];
    return EXPO_GRADE_STR[grade];
}

const char* expo_verdict(ExpoGrade grade) {
    if(grade >= ExpoGradeCount) return EXPO_VERDICT[ExpoGradeF];
    return EXPO_VERDICT[grade];
}

const char* expo_advice(ExpoGrade grade) {
    if(grade >= ExpoGradeCount) return EXPO_ADVICE[ExpoGradeF];
    return EXPO_ADVICE[grade];
}
