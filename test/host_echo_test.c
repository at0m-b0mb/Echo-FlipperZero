/*
 * Host tests for Echo's two pure modules: the name classifier and the exposure
 * score. Neither touches a Flipper header, so both run here at full speed and
 * under the same warnings the firmware build uses.
 *
 *     make -C test
 *
 * The table-driven part checks the classifier says what a person would say.
 * The sweep at the end is the part that actually earns its keep: it walks the
 * whole input space of the scorer and asserts the properties the UI relies on -
 * bounded score, grade that matches the score, and monotonicity in every
 * direction where leaking more must never look better.
 */

#include <stdio.h>
#include <string.h>

#include "../helpers/exposure.h"
#include "../helpers/probe_intel.h"

static unsigned long checks = 0;
static unsigned long failures = 0;

static void fail(const char* what, const char* detail) {
    failures++;
    if(failures <= 30) printf("  FAIL %s: %s\n", what, detail);
}

static void ok(int cond, const char* what, const char* detail) {
    checks++;
    if(!cond) fail(what, detail);
}

/* ------------------------------------------------------------ classifier */

typedef struct {
    const char* ssid;
    PiCat cat;
} CatCase;

static const CatCase CAT_CASES[] = {
    /* the ones that make the demo land */
    {"NETGEAR58", PiCatHome},
    {"Sharma_Home_5G", PiCatHome},
    {"Linksys00721", PiCatHome},
    {"JioFiber-4hd2k", PiCatHome},
    {"BTHub6-P9XK", PiCatHome},
    {"Virgin Media", PiCatHome},
    {"FRITZ!Box 7530 GH", PiCatHome},
    {"ASUS_5G", PiCatHome},
    {"Xfinity", PiCatHome},

    {"Hilton_Honors", PiCatHotel},
    {"Marriott_GUEST", PiCatHotel},
    {"Holiday Inn Express", PiCatHotel},
    {"Premier Inn Free WiFi", PiCatHotel},
    {"ibis", PiCatHotel},
    {"Hotel Astoria", PiCatHotel},
    {"Airbnb-Loft", PiCatHotel},

    {"LAX-Free-WiFi", PiCatAirport},
    {"Boingo Hotspot", PiCatAirport},
    {"DEL Free WiFi", PiCatAirport},
    {"Heathrow Airport WiFi", PiCatAirport},

    {"gogoinflight", PiCatAirline},
    {"United_Wi-Fi", PiCatAirline},
    {"Emirates", PiCatAirline},

    {"WIFIonICE", PiCatTransit},
    {"Amtrak_WiFi", PiCatTransit},
    {"Metro Free WiFi", PiCatTransit},

    {"Starbucks WiFi", PiCatCafe},
    {"Costa Coffee", PiCatCafe},
    {"CafeCoffeeDay", PiCatCafe},

    {"McDonalds Free WiFi", PiCatRetail},
    {"TESCO WiFi", PiCatRetail},
    {"IKEA", PiCatRetail},

    {"PlanetFitness", PiCatGym},
    {"cult.fit", PiCatGym},
    {"GoldsGym-Guest", PiCatGym},

    {"CityHospital-Guest", PiCatMedical},
    {"NHS WiFi", PiCatMedical},
    {"Bright Smile Dental", PiCatMedical},

    {"eduroam", PiCatEducation},
    {"IITD-Campus", PiCatEducation},
    {"Riverside School", PiCatEducation},

    {"ACME-Corp", PiCatWorkplace},
    {"Contoso Office", PiCatWorkplace},
    {"StaffWiFi", PiCatWorkplace},

    {"Tesla Model 3", PiCatVehicle},
    {"MyCar-Hotspot", PiCatVehicle},

    {"DIRECT-4a-HP OfficeJet", PiCatDevice},
    {"Chromecast1234", PiCatDevice},
    {"GoPro-Hero9", PiCatDevice},

    {"Kailash's iPhone", PiCatHotspot},
    {"Galaxy S21", PiCatHotspot},
    {"AndroidAP7f2", PiCatHotspot},

    {"xfinitywifi", PiCatPublic},
    {"attwifi", PiCatPublic},
    {"BTWiFi-with-FON", PiCatPublic},

    /* and the ones that must NOT match anything */
    {"", PiCatUnknown},
    {"zqx9", PiCatUnknown},
    {"Blue Lagoon", PiCatUnknown},
};

static void test_categories(void) {
    for(size_t i = 0; i < sizeof(CAT_CASES) / sizeof(CAT_CASES[0]); i++) {
        PiSsidInfo info = pi_classify(CAT_CASES[i].ssid);
        char detail[128];
        snprintf(
            detail,
            sizeof(detail),
            "\"%s\" -> %s, wanted %s",
            CAT_CASES[i].ssid,
            pi_cat_label(info.cat),
            pi_cat_label(CAT_CASES[i].cat));
        ok(info.cat == CAT_CASES[i].cat, "classify", detail);
    }
}

/* Short airport codes are the reason token matching exists at all. */
static void test_token_boundaries(void) {
    struct {
        const char* ssid;
        int is_airport;
    } cases[] = {
        {"LAX-Free-WiFi", 1},   {"Relaxing Room", 0},  {"laxative", 0},
        {"MyLAX", 0},           {"WiFi LAX", 1},       {"SFO_Public", 1},
        {"Safon", 0},           {"del monte foods", 1}, /* honest false positive */
        {"Bombay Delight", 0},
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        PiSsidInfo info = pi_classify(cases[i].ssid);
        int got = (info.cat == PiCatAirport) ? 1 : 0;
        char detail[128];
        snprintf(detail, sizeof(detail), "\"%s\" airport=%d", cases[i].ssid, got);
        ok(got == cases[i].is_airport, "token boundary", detail);
    }

    /* "gym" must not fire inside a longer word */
    ok(pi_classify("Gym Free WiFi").cat == PiCatGym, "token gym", "Gym Free WiFi");
    ok(pi_classify("Olympia Hall").cat != PiCatGym, "token gym", "Olympia Hall");
}

static void test_personal_names(void) {
    struct {
        const char* ssid;
        int expect;
    } cases[] = {
        {"Kailash's iPhone", 1},
        {"Sam\xe2\x80\x99s Galaxy", 1},
        {"iPhone de Juan", 1},
        {"iPhone van Piet", 1},
        {"AndroidAP", 0},
        {"NETGEAR58", 0},
        {"Starbucks WiFi", 0},
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        PiSsidInfo info = pi_classify(cases[i].ssid);
        int got = (info.flags & PiFlagPersonalName) ? 1 : 0;
        ok(got == cases[i].expect, "personal name", cases[i].ssid);
    }
}

static void test_distinctiveness(void) {
    struct {
        const char* ssid;
        int expect;
    } cases[] = {
        /* mass-produced: the same name in a million buildings */
        {"NETGEAR", 0},
        {"linksys", 0},
        {"Free WiFi", 0},
        {"Guest", 0},
        {"Starbucks WiFi", 0},
        {"Hilton_Honors", 0},
        {"xfinitywifi", 0},
        {"eduroam", 0},
        {"Home WiFi 5G", 0},
        {"MY HOME NETWORK", 0},
        /* one building */
        {"NETGEAR58", 1},
        {"Sharma_Home_5G", 1},
        {"Kailash's iPhone", 1},
        {"BTHub6-P9XK", 1},
        {"ACME-Corp", 1},
        {"CityHospital-Guest", 1},
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        PiSsidInfo info = pi_classify(cases[i].ssid);
        int got = (info.flags & PiFlagGeolocatable) ? 1 : 0;
        char detail[96];
        snprintf(detail, sizeof(detail), "\"%s\" distinctive=%d", cases[i].ssid, got);
        ok(got == cases[i].expect, "distinctive", detail);
    }
}

/* --------------------------------------------------------------- addresses */

static void test_macs(void) {
    uint8_t apple[6] = {0x8C, 0x2D, 0xAA, 0x11, 0x22, 0x33};
    uint8_t randomized[6] = {0x8E, 0x2D, 0xAA, 0x11, 0x22, 0x33}; /* 0x02 set */
    uint8_t unknown[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    char buf[24];

    ok(!pi_mac_is_randomized(apple), "mac real", "8C:2D:AA");
    ok(pi_mac_is_randomized(randomized), "mac randomized", "8E:2D:AA");

    const char* v = pi_mac_vendor(apple);
    ok(v != NULL && strcmp(v, "Apple") == 0, "vendor", "8C:2D:AA -> Apple");
    ok(pi_mac_vendor(randomized) == NULL, "vendor", "randomized has no vendor");
    ok(pi_mac_vendor(unknown) == NULL, "vendor", "unlisted OUI");

    pi_mac_str(apple, buf, sizeof(buf));
    ok(strcmp(buf, "8C:2D:AA:11:22:33") == 0, "mac str", buf);

    pi_mac_str_masked(apple, buf, sizeof(buf));
    ok(strcmp(buf, "8C:2D:AA:**:**:**") == 0, "mac masked", buf);

    /* every locally-administered address must read as randomized, and no
     * globally-assigned one may */
    for(unsigned b = 0; b < 256u; b++) {
        uint8_t m[6] = {(uint8_t)b, 0, 0, 0, 0, 0};
        int want = (b & 0x02u) ? 1 : 0;
        ok(pi_mac_is_randomized(m) == (want != 0), "mac bit sweep", "first octet");
        if(want) ok(pi_mac_vendor(m) == NULL, "mac bit sweep", "no vendor when randomized");
    }
}

static void test_masking(void) {
    char buf[32];
    pi_ssid_mask("Hilton_Honors", buf, sizeof(buf));
    ok(strcmp(buf, "Hil...") == 0, "ssid mask", buf);
    pi_ssid_mask("", buf, sizeof(buf));
    ok(strcmp(buf, "<broadcast>") == 0, "ssid mask", buf);
    pi_ssid_mask("ab", buf, sizeof(buf));
    ok(strcmp(buf, "ab...") == 0, "ssid mask", buf);

    /* the masked form must never contain the tail of the original */
    pi_ssid_mask("SuperSecretHomeNet", buf, sizeof(buf));
    ok(strstr(buf, "Home") == NULL, "ssid mask", "no tail leaks through");
}

/* ------------------------------------------------------------ fingerprints */

static void test_fingerprint_and_sequence(void) {
    const uint8_t a[] = {1, 2, 3, 4, 5};
    const uint8_t b[] = {1, 2, 3, 4, 6};
    uint32_t ha = pi_fnv1a(a, sizeof(a), PI_FNV_SEED);
    uint32_t hb = pi_fnv1a(b, sizeof(b), PI_FNV_SEED);
    ok(ha == pi_fnv1a(a, sizeof(a), PI_FNV_SEED), "fnv", "stable");
    ok(ha != hb, "fnv", "one byte changes the hash");
    ok(pi_fnv1a(NULL, 0, PI_FNV_SEED) == PI_FNV_SEED, "fnv", "empty is the seed");

    ok(pi_seq_continues(100, 101, 64), "seq", "next frame");
    ok(pi_seq_continues(100, 160, 64), "seq", "within the window");
    ok(!pi_seq_continues(100, 200, 64), "seq", "too far ahead");
    ok(!pi_seq_continues(100, 100, 64), "seq", "a retransmit is not progress");
    ok(!pi_seq_continues(100, 99, 64), "seq", "backwards");
    /* the counter is 12 bits and wraps */
    ok(pi_seq_continues(4090, 5, 64), "seq", "across the wrap");
    ok(!pi_seq_continues(4090, 4090, 64), "seq", "wrap does not excuse equality");

    /* a full sweep of the 12-bit space at a fixed window */
    for(uint16_t prev = 0; prev < 4096u; prev++) {
        uint16_t inside = (uint16_t)((prev + 32u) & 0x0FFFu);
        uint16_t outside = (uint16_t)((prev + 900u) & 0x0FFFu);
        ok(pi_seq_continues(prev, inside, 64), "seq sweep", "inside window");
        ok(!pi_seq_continues(prev, outside, 64), "seq sweep", "outside window");
    }
}

/* --------------------------------------------------------------- exposure */

static ExpoInput base_input(void) {
    ExpoInput in;
    memset(&in, 0, sizeof(in));
    in.mac_randomized = true;
    in.link_confidence = (uint8_t)ExpoLinkNone;
    in.linked_macs = 1;
    return in;
}

static void test_exposure_anchors(void) {
    /* A modern phone doing everything right: randomized MAC, broadcast probes
     * only. Echo must be able to say "nothing". */
    ExpoInput silent = base_input();
    ExpoResult r = expo_score(&silent);
    ok(r.score == 0, "expo silent", "score 0");
    ok(r.grade == (uint8_t)ExpoGradeAPlus, "expo silent", "A+");
    ok(!r.floor_applied && !r.cap_applied, "expo silent", "no bends");

    /* The same phone, but its randomization has been defeated. */
    ExpoInput linked = base_input();
    linked.link_confidence = (uint8_t)ExpoLinkConfirmed;
    linked.linked_macs = 3;
    r = expo_score(&linked);
    ok(r.score == 14, "expo linked", "trackable despite randomizing");
    ok(r.grade == (uint8_t)ExpoGradeB, "expo linked", "B");

    /* A permanent MAC and nothing else: nowhere you have been, but you can be
     * followed for as long as you own the device. */
    ExpoInput permanent = base_input();
    permanent.mac_randomized = false;
    permanent.vendor_known = true;
    r = expo_score(&permanent);
    ok(r.score == 31, "expo permanent", "25 track + 6 vendor");
    ok(r.grade == (uint8_t)ExpoGradeC, "expo permanent", "C");

    /* The one that makes people put their phone down: an old handset with a
     * saved-network list and no randomization at all. */
    ExpoInput leaky = base_input();
    leaky.mac_randomized = false;
    leaky.vendor_known = true;
    leaky.named_ssids = 6;
    leaky.geolocatable = true;
    leaky.cat_mask = expo_cat_bit(PiCatHome) | expo_cat_bit(PiCatHotel) |
                     expo_cat_bit(PiCatGym) | expo_cat_bit(PiCatAirport);
    r = expo_score(&leaky);
    ok(r.score == 96, "expo leaky", "30+25+10+6+25");
    ok(r.grade == (uint8_t)ExpoGradeF, "expo leaky", "F");
    ok(r.note_count > 0, "expo leaky", "explains itself");

    /* One chain coffee shop from a randomized MAC is a real but small leak,
     * and the floor is what keeps it from reading as clean. */
    ExpoInput cafe = base_input();
    cafe.named_ssids = 1;
    cafe.cat_mask = expo_cat_bit(PiCatCafe);
    r = expo_score(&cafe);
    ok(r.score == 22, "expo cafe", "12 + 10");
    ok(r.grade == (uint8_t)ExpoGradeB, "expo cafe", "B");

    /* Under the floor on the raw columns, lifted by it. */
    ExpoInput tiny = base_input();
    tiny.named_ssids = 1;
    tiny.cat_mask = expo_cat_bit(PiCatDevice); /* weight 1 -> 5 points */
    r = expo_score(&tiny);
    ok(r.floor_applied, "expo floor", "one name cannot beat B");
    ok(r.score == 20, "expo floor", "lifted to 20");

    /* Home network named from a permanent address: the cap. */
    ExpoInput home = base_input();
    home.mac_randomized = false;
    home.named_ssids = 1;
    home.cat_mask = expo_cat_bit(PiCatHome);
    home.geolocatable = false;
    r = expo_score(&home);
    ok(r.score >= 45, "expo cap", "no better than D");
    ok(r.grade >= (uint8_t)ExpoGradeD, "expo cap", "D or worse");

    /* Health is treated like home, and says so. */
    ExpoInput health = base_input();
    health.mac_randomized = false;
    health.named_ssids = 1;
    health.cat_mask = expo_cat_bit(PiCatMedical);
    r = expo_score(&health);
    ok(r.grade >= (uint8_t)ExpoGradeD, "expo cap", "health caps too");
    ok(strcmp(r.notes[0], "Named a health site") == 0, "expo cap", "note names it");
}

/*
 * Every string the engine hands to a screen has a pixel budget, and a 128 px
 * line at the widest font it could be drawn in holds about 25 characters. The
 * budgets below come from where each string is actually drawn:
 *
 *   reveals   networks screen, x=2, must clear the pin marker at x=118
 *   verdict   dossier footer, x=2, must clear the page dots at x=100
 *   advice    dossier page three, x=2, full width, one line per newline
 *   notes     dossier page three, x=9, after the bullet
 *   label     dossier name list, x=2, must clear the name column at x=40
 *
 * This is the test that stops a nicer turn of phrase from silently running off
 * the right-hand edge of the device six months from now.
 */
#define BUDGET_REVEALS 23
#define BUDGET_VERDICT 24
#define BUDGET_ADVICE  26
#define BUDGET_NOTE    24
#define BUDGET_LABEL   9

static void check_width(const char* text, size_t budget, const char* what) {
    char detail[128];
    snprintf(detail, sizeof(detail), "\"%s\" is %u, budget %u",
             text, (unsigned)strlen(text), (unsigned)budget);
    ok(strlen(text) <= budget, what, detail);
}

/** Longest single line of a string that carries its own newlines. */
static size_t longest_line(const char* text) {
    size_t best = 0, run = 0;
    for(size_t i = 0;; i++) {
        if(text[i] == '\n' || text[i] == '\0') {
            if(run > best) best = run;
            run = 0;
            if(text[i] == '\0') return best;
        } else {
            run++;
        }
    }
}

static void test_string_widths(void) {
    for(uint8_t c = 0; c < (uint8_t)PiCatCount; c++) {
        check_width(pi_cat_reveals((PiCat)c), BUDGET_REVEALS, "reveals width");
        check_width(pi_cat_label((PiCat)c), BUDGET_LABEL, "label width");
        ok(pi_cat_tag((PiCat)c) > ' ', "tag", "printable");
    }

    for(uint8_t g = 0; g < (uint8_t)ExpoGradeCount; g++) {
        check_width(expo_verdict((ExpoGrade)g), BUDGET_VERDICT, "verdict width");

        const char* advice = expo_advice((ExpoGrade)g);
        char detail[128];
        snprintf(detail, sizeof(detail), "grade %s advice line is %u",
                 expo_grade_str((ExpoGrade)g), (unsigned)longest_line(advice));
        ok(longest_line(advice) <= BUDGET_ADVICE, "advice width", detail);
        /* two lines exactly: the layout draws at a fixed 9 px pitch */
        size_t lines = 1;
        for(size_t i = 0; advice[i]; i++)
            if(advice[i] == '\n') lines++;
        ok(lines <= 2, "advice lines", "fits the band");
    }

    /* Category tags have to be distinguishable, or the list is decoration. */
    for(uint8_t a = 0; a < (uint8_t)PiCatCount; a++) {
        for(uint8_t b = (uint8_t)(a + 1u); b < (uint8_t)PiCatCount; b++) {
            ok(pi_cat_tag((PiCat)a) != pi_cat_tag((PiCat)b), "tag", "unique per category");
        }
    }
}

static void test_exposure_strings(void) {
    for(uint8_t g = 0; g < (uint8_t)ExpoGradeCount; g++) {
        ok(expo_grade_str((ExpoGrade)g) != NULL, "grade str", "not null");
        ok(expo_verdict((ExpoGrade)g) != NULL, "verdict", "not null");
        ok(expo_advice((ExpoGrade)g) != NULL, "advice", "not null");
        ok(strlen(expo_grade_str((ExpoGrade)g)) <= 2, "grade str", "fits the badge");
    }
    /* out of range must not walk off the table */
    ok(expo_grade_str((ExpoGrade)99) != NULL, "grade str", "out of range");
    ok(expo_verdict((ExpoGrade)99) != NULL, "verdict", "out of range");
    ok(expo_advice((ExpoGrade)99) != NULL, "advice", "out of range");
}

/*
 * The properties the screens depend on, checked across the whole input space
 * rather than at a handful of points.
 */
static void test_exposure_sweep(void) {
    static const PiCat cats[] = {
        PiCatUnknown,
        PiCatHome,
        PiCatHotspot,
        PiCatHotel,
        PiCatAirport,
        PiCatAirline,
        PiCatTransit,
        PiCatCafe,
        PiCatRetail,
        PiCatGym,
        PiCatMedical,
        PiCatEducation,
        PiCatWorkplace,
        PiCatVehicle,
        PiCatDevice,
        PiCatPublic,
    };
    const size_t cat_n = sizeof(cats) / sizeof(cats[0]);

    for(size_t ci = 0; ci < cat_n; ci++) {
        for(int personal = 0; personal < 2; personal++) {
            for(int geo = 0; geo < 2; geo++) {
                for(int randomized = 0; randomized < 2; randomized++) {
                    for(uint8_t link = 0; link <= (uint8_t)ExpoLinkConfirmed; link++) {
                        uint8_t prev = 0;
                        for(uint16_t names = 0; names <= 8u; names++) {
                            ExpoInput in = base_input();
                            in.named_ssids = names;
                            in.cat_mask = names ? expo_cat_bit(cats[ci]) : 0u;
                            in.personal_name = (personal != 0) && names > 0u;
                            in.geolocatable = (geo != 0);
                            in.mac_randomized = (randomized != 0);
                            in.vendor_known = (randomized == 0);
                            in.link_confidence = link;

                            ExpoResult r = expo_score(&in);

                            ok(r.score <= 100u, "sweep", "score is bounded");
                            ok(r.grade < (uint8_t)ExpoGradeCount, "sweep", "grade in range");
                            ok((uint8_t)expo_grade_for_score(r.score) == r.grade,
                               "sweep",
                               "grade matches score");
                            ok(r.note_count > 0u && r.note_count <= EXPO_MAX_NOTES,
                               "sweep",
                               "always explains itself");
                            for(uint8_t nn = 0; nn < r.note_count; nn++) {
                                check_width(r.notes[nn], BUDGET_NOTE, "note width");
                            }

                            /* Leaking one more name can never look better. */
                            ok(r.score >= prev, "sweep", "monotonic in names leaked");
                            prev = r.score;

                            /* Any name at all keeps it out of the A band. */
                            if(names > 0u) {
                                ok(r.score >= 20u, "sweep", "floor holds");
                                ok(r.grade >= (uint8_t)ExpoGradeB, "sweep", "B at best");
                            } else {
                                ok(r.p_volume == 0u, "sweep", "no names, no volume");
                                ok(r.p_sensitivity == 0u, "sweep", "no names, no category");
                                ok(r.p_geo == 0u, "sweep", "no names, nothing to locate");
                            }

                            /* Randomizing must never make a device look worse. */
                            ExpoInput real = in;
                            real.mac_randomized = false;
                            real.vendor_known = in.vendor_known;
                            ExpoResult rr = expo_score(&real);
                            ok(r.score <= rr.score || !in.mac_randomized,
                               "sweep",
                               "randomization never costs points");

                            /* Home or health on a permanent MAC is capped. */
                            if(names > 0u && !in.mac_randomized &&
                               (cats[ci] == PiCatHome || cats[ci] == PiCatMedical)) {
                                ok(r.score >= 45u, "sweep", "home/health cap holds");
                            }
                        }
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------ demo dump */

/*
 * The five personas from helpers/demo_feed.c, scored by the real engine and
 * printed as TSV. tools_gen_mockups.py reads this, so every number on a README
 * screenshot is a number the shipped code produced - not one somebody typed
 * into a drawing.
 *
 * The facts below (which addresses, which names, how many times each) mirror
 * the script in demo_feed.c. echo_db.c does the accumulating on the device;
 * here the aggregate is stated directly, because the aggregating is not what
 * the mockups are testing.
 */
typedef struct {
    const char* mac_str;
    uint8_t mac[6];
    uint16_t probes;
    uint8_t link;
    uint8_t group;
    const char* names[8];
} DemoDev;

static const DemoDev DEMO_DEVS[] = {
    {"34:23:BA:7C:19:04",
     {0x34, 0x23, 0xBA, 0x7C, 0x19, 0x04},
     14,
     ExpoLinkNone,
     1,
     {"NETGEAR58", "Hilton_Honors", "PlanetFitness", "LAX-Free-WiFi", "Starbucks WiFi",
      "CityHospital-Guest", NULL}},
    {"7C:B2:7D:44:12:9A",
     {0x7C, 0xB2, 0x7D, 0x44, 0x12, 0x9A},
     5,
     ExpoLinkNone,
     1,
     {"ACME-Corp", "Sharma_Home_5G", NULL}},
    {"C0:BD:D1:0A:5E:22",
     {0xC0, 0xBD, 0xD1, 0x0A, 0x5E, 0x22},
     5,
     ExpoLinkNone,
     1,
     {"Marriott_GUEST", "eduroam", NULL}},
    {"8A:3F:11:02:9C:41",
     {0x8A, 0x3F, 0x11, 0x02, 0x9C, 0x41},
     3,
     ExpoLinkConfirmed,
     3,
     {NULL}},
    {"B6:7C:D9:41:22:08",
     {0xB6, 0x7C, 0xD9, 0x41, 0x22, 0x08},
     3,
     ExpoLinkConfirmed,
     3,
     {NULL}},
    {"EA:05:63:9F:11:B2",
     {0xEA, 0x05, 0x63, 0x9F, 0x11, 0xB2},
     2,
     ExpoLinkConfirmed,
     3,
     {NULL}},
    {"8E:41:C2:19:7A:03",
     {0x8E, 0x41, 0xC2, 0x19, 0x7A, 0x03},
     7,
     ExpoLinkNone,
     1,
     {NULL}},
};

/* how many times each name was called out across one pass of the script */
typedef struct {
    const char* name;
    uint16_t hits;
    uint8_t askers;
} DemoName;

static const DemoName DEMO_NAMES[] = {
    {"NETGEAR58", 3, 1},
    {"Hilton_Honors", 2, 1},
    {"PlanetFitness", 1, 1},
    {"LAX-Free-WiFi", 1, 1},
    {"Starbucks WiFi", 1, 1},
    {"CityHospital-Guest", 1, 1},
    {"ACME-Corp", 2, 1},
    {"Sharma_Home_5G", 1, 1},
    {"Marriott_GUEST", 1, 1},
    {"eduroam", 1, 1},
};

static void dump_demo(void) {
    for(size_t i = 0; i < sizeof(DEMO_DEVS) / sizeof(DEMO_DEVS[0]); i++) {
        const DemoDev* d = &DEMO_DEVS[i];

        ExpoInput in;
        memset(&in, 0, sizeof(in));
        in.probe_count = d->probes;
        in.mac_randomized = pi_mac_is_randomized(d->mac);
        in.vendor_known = (pi_mac_vendor(d->mac) != NULL);
        in.link_confidence = d->link;
        in.linked_macs = d->group;

        const char* worst = "";
        uint8_t worst_w = 0;
        PiCat worst_cat = PiCatUnknown;

        for(size_t k = 0; k < 8u && d->names[k]; k++) {
            PiSsidInfo info = pi_classify(d->names[k]);
            in.named_ssids++;
            in.cat_mask |= expo_cat_bit(info.cat);
            if(info.flags & PiFlagPersonalName) in.personal_name = true;
            if(info.flags & PiFlagGeolocatable) in.geolocatable = true;
            if(pi_cat_weight(info.cat) > worst_w) {
                worst_w = pi_cat_weight(info.cat);
                worst = d->names[k];
                worst_cat = info.cat;
            }
        }

        ExpoResult r = expo_score(&in);
        printf(
            "DEV\t%s\t%s\t%d\t%u\t%u\t%s\t%u\t%u\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%s\n",
            d->mac_str,
            pi_mac_vendor(d->mac) ? pi_mac_vendor(d->mac) : "",
            in.mac_randomized ? 1 : 0,
            (unsigned)in.named_ssids,
            (unsigned)r.score,
            expo_grade_str((ExpoGrade)r.grade),
            (unsigned)d->group,
            (unsigned)d->probes,
            worst,
            pi_cat_label(worst_cat),
            (unsigned)r.p_volume,
            (unsigned)r.p_sensitivity,
            (unsigned)r.p_geo,
            (unsigned)r.p_identity,
            (unsigned)r.p_track,
            expo_verdict((ExpoGrade)r.grade));
    }

    for(size_t i = 0; i < sizeof(DEMO_NAMES) / sizeof(DEMO_NAMES[0]); i++) {
        const DemoName* n = &DEMO_NAMES[i];
        PiSsidInfo info = pi_classify(n->name);
        printf(
            "SSID\t%s\t%s\t%c\t%u\t%u\t%d\t%s\n",
            n->name,
            pi_cat_label(info.cat),
            pi_cat_tag(info.cat),
            (unsigned)n->hits,
            (unsigned)n->askers,
            (info.flags & PiFlagGeolocatable) ? 1 : 0,
            pi_cat_reveals(info.cat));
    }
}

int main(int argc, char** argv) {
    if(argc > 1 && strcmp(argv[1], "--dump") == 0) {
        dump_demo();
        return 0;
    }

    printf("Echo engine tests\n");
    printf("-----------------\n");

    test_categories();
    test_token_boundaries();
    test_personal_names();
    test_distinctiveness();
    test_macs();
    test_masking();
    test_fingerprint_and_sequence();
    test_exposure_anchors();
    test_exposure_strings();
    test_string_widths();
    test_exposure_sweep();

    printf("%lu checks, %lu failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
