#include "probe_intel.h"

#include <string.h>

/* ------------------------------------------------------------ text helpers */

static char pi_lower(char c) {
    if(c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static bool pi_is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/** Lowercase copy, truncated to fit. Everything below works on this form. */
static void pi_normalize(const char* in, char* out, size_t out_len) {
    size_t i = 0;
    if(out_len == 0) return;
    if(in) {
        for(; in[i] != '\0' && i + 1 < out_len; i++) out[i] = pi_lower(in[i]);
    }
    out[i] = '\0';
}

/** strstr on already-normalized text. */
static const char* pi_find(const char* hay, const char* needle) {
    return strstr(hay, needle);
}

/**
 * Like pi_find(), but the hit must not be glued to letters or digits on either
 * side. This is what keeps the airport code "lax" out of "relaxing" - the kind
 * of false positive that would make every third row of the list a lie.
 */
static const char* pi_find_token(const char* hay, const char* needle) {
    size_t nlen = strlen(needle);
    if(nlen == 0) return NULL;
    for(const char* p = hay; (p = strstr(p, needle)) != NULL; p++) {
        bool left_ok = (p == hay) || !pi_is_alnum(p[-1]);
        char after = p[nlen];
        bool right_ok = (after == '\0') || !pi_is_alnum(after);
        if(left_ok && right_ok) return p;
    }
    return NULL;
}

static bool pi_starts_with(const char* hay, const char* prefix) {
    return strncmp(hay, prefix, strlen(prefix)) == 0;
}

/* ------------------------------------------------------------- rule tables */

typedef enum {
    MatchContains = 0,
    MatchToken,
    MatchPrefix,
    MatchExact,
} MatchMode;

typedef struct {
    const char* pat; /* already lowercase */
    uint8_t mode; /* MatchMode */
    uint8_t cat; /* PiCat */
} PiRule;

/*
 * Order is the whole design here: the first rule that matches wins, so the
 * specific brands sit above the generic words they contain. "xfinitywifi" is
 * public carrier wi-fi and has to be decided before "xfinity" pulls it into
 * somebody's living room.
 */
static const PiRule PI_RULES[] = {
    /* ---- carrier-wide public wi-fi (before the ISPs whose names they share)
     *
     * Only the branded ones live up here. The bare "free wifi" family sits at
     * the very bottom of the table, because half the hotels and airports in
     * the world put those two words after their own name and the specific
     * place has to win. */
    {"xfinitywifi", MatchContains, PiCatPublic},
    {"attwifi", MatchContains, PiCatPublic},
    {"att-wifi", MatchContains, PiCatPublic},
    {"spectrumwifi", MatchContains, PiCatPublic},
    {"optimumwifi", MatchContains, PiCatPublic},
    {"cablewifi", MatchContains, PiCatPublic},
    {"btwifi", MatchContains, PiCatPublic},
    {"bt wi-fi", MatchContains, PiCatPublic},
    {"telekom_fon", MatchContains, PiCatPublic},
    {"fon wifi", MatchContains, PiCatPublic},

    /* ---- in-flight */
    {"gogoinflight", MatchContains, PiCatAirline},
    {"inflight", MatchContains, PiCatAirline},
    {"flyfi", MatchContains, PiCatAirline},
    {"airfi", MatchContains, PiCatAirline},
    {"flynet", MatchContains, PiCatAirline},
    {"onboard wifi", MatchContains, PiCatAirline},
    {"wifi onboard", MatchContains, PiCatAirline},
    {"united_wi-fi", MatchContains, PiCatAirline},
    {"unitedwifi", MatchContains, PiCatAirline},
    {"southwestwifi", MatchContains, PiCatAirline},
    {"emirates", MatchToken, PiCatAirline},
    {"lufthansa", MatchContains, PiCatAirline},
    {"qatarairways", MatchContains, PiCatAirline},
    {"vistara", MatchToken, PiCatAirline},

    /* ---- airports */
    {"airport", MatchContains, PiCatAirport},
    {"aeropuerto", MatchContains, PiCatAirport},
    {"aeroporto", MatchContains, PiCatAirport},
    {"flughafen", MatchContains, PiCatAirport},
    {"boingo", MatchContains, PiCatAirport},
    {"terminal", MatchToken, PiCatAirport},
    {"lax", MatchToken, PiCatAirport},
    {"sfo", MatchToken, PiCatAirport},
    {"jfk", MatchToken, PiCatAirport},
    {"lhr", MatchToken, PiCatAirport},
    {"cdg", MatchToken, PiCatAirport},
    {"dxb", MatchToken, PiCatAirport},
    {"sin", MatchToken, PiCatAirport},
    {"hnd", MatchToken, PiCatAirport},
    {"del", MatchToken, PiCatAirport},
    {"bom", MatchToken, PiCatAirport},
    {"blr", MatchToken, PiCatAirport},

    /* ---- hotels */
    {"hilton", MatchContains, PiCatHotel},
    {"hampton", MatchContains, PiCatHotel},
    {"doubletree", MatchContains, PiCatHotel},
    {"marriott", MatchContains, PiCatHotel},
    {"courtyard", MatchContains, PiCatHotel},
    {"sheraton", MatchContains, PiCatHotel},
    {"westin", MatchContains, PiCatHotel},
    {"hyatt", MatchContains, PiCatHotel},
    {"radisson", MatchContains, PiCatHotel},
    {"novotel", MatchContains, PiCatHotel},
    {"ibis", MatchToken, PiCatHotel},
    {"holiday inn", MatchContains, PiCatHotel},
    {"holidayinn", MatchContains, PiCatHotel},
    {"crowneplaza", MatchContains, PiCatHotel},
    {"intercontinental", MatchContains, PiCatHotel},
    {"ramada", MatchContains, PiCatHotel},
    {"wyndham", MatchContains, PiCatHotel},
    {"premierinn", MatchContains, PiCatHotel},
    {"premier inn", MatchContains, PiCatHotel},
    {"travelodge", MatchContains, PiCatHotel},
    {"bestwestern", MatchContains, PiCatHotel},
    {"lemontree", MatchContains, PiCatHotel},
    {"oberoi", MatchContains, PiCatHotel},
    {"oyorooms", MatchContains, PiCatHotel},
    {"hotel", MatchContains, PiCatHotel},
    {"hostel", MatchContains, PiCatHotel},
    {"resort", MatchContains, PiCatHotel},
    {"guesthouse", MatchContains, PiCatHotel},
    {"airbnb", MatchContains, PiCatHotel},

    /* ---- health (kept above education: "medical college" is a hospital) */
    {"hospital", MatchContains, PiCatMedical},
    {"clinic", MatchContains, PiCatMedical},
    {"medical", MatchContains, PiCatMedical},
    {"healthcare", MatchContains, PiCatMedical},
    {"dental", MatchContains, PiCatMedical},
    {"pharmacy", MatchContains, PiCatMedical},
    {"diagnost", MatchContains, PiCatMedical},
    {"physio", MatchContains, PiCatMedical},
    {"patientwifi", MatchContains, PiCatMedical},
    {"nhs", MatchToken, PiCatMedical},
    {"kaiser", MatchContains, PiCatMedical},
    {"apollohosp", MatchContains, PiCatMedical},
    {"fortishealth", MatchContains, PiCatMedical},

    /* ---- education */
    {"eduroam", MatchContains, PiCatEducation},
    {"campus", MatchContains, PiCatEducation},
    {"university", MatchContains, PiCatEducation},
    {"college", MatchContains, PiCatEducation},
    {"school", MatchContains, PiCatEducation},
    {"student", MatchContains, PiCatEducation},
    {"academy", MatchContains, PiCatEducation},
    {"library", MatchContains, PiCatEducation},
    {"univ", MatchToken, PiCatEducation},

    /* ---- gyms */
    {"planetfitness", MatchContains, PiCatGym},
    {"planet fitness", MatchContains, PiCatGym},
    {"goldsgym", MatchContains, PiCatGym},
    {"anytimefitness", MatchContains, PiCatGym},
    {"lafitness", MatchContains, PiCatGym},
    {"puregym", MatchContains, PiCatGym},
    {"equinox", MatchToken, PiCatGym},
    {"cultfit", MatchContains, PiCatGym},
    {"cult.fit", MatchContains, PiCatGym},
    {"crossfit", MatchContains, PiCatGym},
    {"fitness", MatchContains, PiCatGym},
    {"gym", MatchToken, PiCatGym},
    {"yoga", MatchContains, PiCatGym},
    {"pilates", MatchContains, PiCatGym},

    /* ---- transit */
    {"wifionice", MatchContains, PiCatTransit},
    {"amtrak", MatchContains, PiCatTransit},
    {"eurostar", MatchContains, PiCatTransit},
    {"sncf", MatchContains, PiCatTransit},
    {"trenitalia", MatchContains, PiCatTransit},
    {"irctc", MatchContains, PiCatTransit},
    {"railway", MatchContains, PiCatTransit},
    {"railwifi", MatchContains, PiCatTransit},
    {"caltrain", MatchContains, PiCatTransit},
    {"flixbus", MatchContains, PiCatTransit},
    {"megabus", MatchContains, PiCatTransit},
    {"greyhound", MatchContains, PiCatTransit},
    {"metro", MatchToken, PiCatTransit},
    {"ferry", MatchToken, PiCatTransit},
    {"bart", MatchToken, PiCatTransit},
    {"tfl", MatchToken, PiCatTransit},
    {"mta", MatchToken, PiCatTransit},

    /* ---- cafes */
    {"starbucks", MatchContains, PiCatCafe},
    {"costacoffee", MatchContains, PiCatCafe},
    {"dunkin", MatchContains, PiCatCafe},
    {"timhortons", MatchContains, PiCatCafe},
    {"tim hortons", MatchContains, PiCatCafe},
    {"caribou", MatchContains, PiCatCafe},
    {"pretamanger", MatchContains, PiCatCafe},
    {"cafecoffeeday", MatchContains, PiCatCafe},
    {"chaayos", MatchContains, PiCatCafe},
    {"barista", MatchContains, PiCatCafe},
    {"coffee", MatchContains, PiCatCafe},
    {"cafe", MatchContains, PiCatCafe},
    {"espresso", MatchContains, PiCatCafe},

    /* ---- shops and restaurants */
    {"mcdonald", MatchContains, PiCatRetail},
    {"burgerking", MatchContains, PiCatRetail},
    {"pizzahut", MatchContains, PiCatRetail},
    {"dominos", MatchContains, PiCatRetail},
    {"subway", MatchToken, PiCatRetail},
    {"walmart", MatchContains, PiCatRetail},
    {"costco", MatchContains, PiCatRetail},
    {"target", MatchToken, PiCatRetail},
    {"tesco", MatchContains, PiCatRetail},
    {"sainsbury", MatchContains, PiCatRetail},
    {"carrefour", MatchContains, PiCatRetail},
    {"ikea", MatchToken, PiCatRetail},
    {"bestbuy", MatchContains, PiCatRetail},
    {"homedepot", MatchContains, PiCatRetail},
    {"dmart", MatchToken, PiCatRetail},
    {"bigbazaar", MatchContains, PiCatRetail},
    {"reliancesmart", MatchContains, PiCatRetail},
    {"mall", MatchToken, PiCatRetail},
    {"7-eleven", MatchContains, PiCatRetail},

    /* ---- cars */
    {"tesla", MatchToken, PiCatVehicle},
    {"onstar", MatchContains, PiCatVehicle},
    {"mbux", MatchContains, PiCatVehicle},
    {"audi_mmi", MatchContains, PiCatVehicle},
    {"mycar", MatchContains, PiCatVehicle},
    {"carplay", MatchContains, PiCatVehicle},
    {"androidauto", MatchContains, PiCatVehicle},
    {"bmw", MatchToken, PiCatVehicle},
    {"mercedes", MatchToken, PiCatVehicle},
    {"volvo", MatchToken, PiCatVehicle},
    {"camper", MatchToken, PiCatVehicle},

    /* ---- appliances that shout their own name
     *
     * Above the workplace rules: a printer called "DIRECT-4a-HP OfficeJet" is
     * a printer, not an office. */
    {"direct-", MatchPrefix, PiCatDevice},
    {"hp-print", MatchContains, PiCatDevice},
    {"hpsetup", MatchContains, PiCatDevice},
    {"printer", MatchContains, PiCatDevice},
    {"chromecast", MatchContains, PiCatDevice},
    {"firetv", MatchContains, PiCatDevice},
    {"roku", MatchContains, PiCatDevice},
    {"sonos", MatchContains, PiCatDevice},
    {"playstation", MatchContains, PiCatDevice},
    {"xbox", MatchContains, PiCatDevice},
    {"gopro", MatchContains, PiCatDevice},
    {"insta360", MatchContains, PiCatDevice},
    {"sonoff", MatchContains, PiCatDevice},
    {"tasmota", MatchContains, PiCatDevice},
    {"shelly", MatchContains, PiCatDevice},
    {"wyze", MatchContains, PiCatDevice},
    {"ipcam", MatchContains, PiCatDevice},
    {"nvr", MatchToken, PiCatDevice},
    {"dvr", MatchToken, PiCatDevice},
    {"esp_", MatchPrefix, PiCatDevice},

    /* ---- workplaces */
    {"corporate", MatchContains, PiCatWorkplace},
    {"corp", MatchToken, PiCatWorkplace},
    {"employee", MatchContains, PiCatWorkplace},
    {"staffwifi", MatchContains, PiCatWorkplace},
    {"staff", MatchToken, PiCatWorkplace},
    {"office", MatchToken, PiCatWorkplace},
    {"warehouse", MatchContains, PiCatWorkplace},
    {"conference", MatchContains, PiCatWorkplace},
    {"enterprise", MatchContains, PiCatWorkplace},
    {"internal", MatchToken, PiCatWorkplace},

    /* ---- somebody's phone sharing its connection */
    {"iphone", MatchContains, PiCatHotspot},
    {"ipad", MatchContains, PiCatHotspot},
    {"androidap", MatchContains, PiCatHotspot},
    {"galaxy", MatchContains, PiCatHotspot},
    {"pixel", MatchContains, PiCatHotspot},
    {"oneplus", MatchContains, PiCatHotspot},
    {"redmi", MatchContains, PiCatHotspot},
    {"xiaomi", MatchContains, PiCatHotspot},
    {"realme", MatchContains, PiCatHotspot},
    {"hotspot", MatchContains, PiCatHotspot},
    {"mifi", MatchToken, PiCatHotspot},
    {"jetpack", MatchContains, PiCatHotspot},
    {"tether", MatchContains, PiCatHotspot},

    /* ---- routers, by the brand or the ISP printed on the sticker */
    {"netgear", MatchContains, PiCatHome},
    {"nighthawk", MatchContains, PiCatHome},
    {"orbi", MatchContains, PiCatHome},
    {"linksys", MatchContains, PiCatHome},
    {"belkin", MatchContains, PiCatHome},
    {"d-link", MatchContains, PiCatHome},
    {"dlink", MatchContains, PiCatHome},
    {"tp-link", MatchContains, PiCatHome},
    {"tplink", MatchContains, PiCatHome},
    {"tenda", MatchContains, PiCatHome},
    {"asus", MatchContains, PiCatHome},
    {"arris", MatchContains, PiCatHome},
    {"technicolor", MatchContains, PiCatHome},
    {"sagemcom", MatchContains, PiCatHome},
    {"zyxel", MatchContains, PiCatHome},
    {"huawei", MatchContains, PiCatHome},
    {"fritz", MatchContains, PiCatHome},
    {"speedport", MatchContains, PiCatHome},
    {"vodafone", MatchContains, PiCatHome},
    {"airtel", MatchContains, PiCatHome},
    {"jiofiber", MatchContains, PiCatHome},
    {"jio", MatchToken, PiCatHome},
    {"actfibernet", MatchContains, PiCatHome},
    {"bsnl", MatchContains, PiCatHome},
    {"hathway", MatchContains, PiCatHome},
    {"excitel", MatchContains, PiCatHome},
    {"xfinity", MatchContains, PiCatHome},
    {"comcast", MatchContains, PiCatHome},
    {"centurylink", MatchContains, PiCatHome},
    {"frontier", MatchContains, PiCatHome},
    {"verizon", MatchContains, PiCatHome},
    {"fios", MatchContains, PiCatHome},
    {"virginmedia", MatchContains, PiCatHome},
    {"virgin media", MatchContains, PiCatHome},
    {"talktalk", MatchContains, PiCatHome},
    {"plusnet", MatchContains, PiCatHome},
    {"bthub", MatchContains, PiCatHome},
    {"sky", MatchToken, PiCatHome},
    {"telstra", MatchContains, PiCatHome},
    {"rogers", MatchContains, PiCatHome},
    {"eero", MatchToken, PiCatHome},
    {"deco", MatchToken, PiCatHome},

    /* ---- and the names people give their own house */
    {"home", MatchContains, PiCatHome},
    {"casa", MatchToken, PiCatHome},
    {"family", MatchContains, PiCatHome},
    {"house", MatchContains, PiCatHome},
    {"apartment", MatchContains, PiCatHome},
    {"villa", MatchToken, PiCatHome},
    {"cottage", MatchContains, PiCatHome},
    {"residence", MatchContains, PiCatHome},

    /* ---- last resort: two words that follow half the place names on earth */
    {"free public wifi", MatchContains, PiCatPublic},
    {"free_public_wifi", MatchContains, PiCatPublic},
    {"freewifi", MatchContains, PiCatPublic},
    {"free wifi", MatchContains, PiCatPublic},
    {"free_wifi", MatchContains, PiCatPublic},
    {"public wifi", MatchContains, PiCatPublic},
};

#define PI_RULE_COUNT (sizeof(PI_RULES) / sizeof(PI_RULES[0]))

/*
 * Words that appear on so many networks that they carry no location. A name
 * built entirely out of these could be any of a million places; one unfamiliar
 * word in it and the name starts pointing somewhere.
 */
static const char* const PI_GENERIC_WORDS[] = {
    "wifi",    "wi",         "fi",        "wlan",       "net",        "network",
    "internet","router",     "modem",     "hotspot",    "guest",      "public",
    "free",    "open",       "the",       "and",        "for",        "with",
    "my",      "our",        "home",      "house",      "casa",       "family",
    "ext",     "extender",   "repeater",  "mesh",       "ghz",        "band",
    "main",    "default",    "setup",     "linksys",    "netgear",    "belkin",
    "dlink",   "tplink",     "tenda",     "asus",       "arris",      "zyxel",
    "orbi",    "eero",       "deco",      "fritzbox",   "speedport",  "sagemcom",
    "technicolor",           "huawei",    "xfinity",    "xfinitywifi","attwifi",
    "comcast", "spectrum",   "optimum",   "cablewifi",  "btwifi",     "eduroam",
    "boingo",  "starbucks",  "costa",     "dunkin",     "mcdonalds",  "subway",
    "coffee",  "cafe",       "hotel",     "hilton",     "honors",     "marriott",
    "hyatt",   "sheraton",   "ibis",      "novotel",    "inn",        "lobby",
    "direct",  "printer",    "android",   "iphone",     "ipad",       "galaxy",
    "phone",   "mobile",     "test",      "temp",       "new",        "old",
};

#define PI_GENERIC_COUNT (sizeof(PI_GENERIC_WORDS) / sizeof(PI_GENERIC_WORDS[0]))

/* ------------------------------------------------------------ classifying */

static bool pi_rule_hit(const char* norm, const PiRule* r) {
    switch((MatchMode)r->mode) {
    case MatchToken:
        return pi_find_token(norm, r->pat) != NULL;
    case MatchPrefix:
        return pi_starts_with(norm, r->pat);
    case MatchExact:
        return strcmp(norm, r->pat) == 0;
    case MatchContains:
    default:
        return pi_find(norm, r->pat) != NULL;
    }
}

/** "Kailash's iPhone" and its cousins in other languages. */
static bool pi_has_personal_name(const char* norm) {
    if(pi_find(norm, "'s") != NULL) return true;
    if(pi_find(norm, "\xe2\x80\x99s") != NULL) return true; /* curly apostrophe */
    if(pi_find_token(norm, "de") != NULL && pi_find(norm, "iphone") != NULL) return true;
    if(pi_find_token(norm, "van") != NULL && pi_find(norm, "iphone") != NULL) return true;
    if(pi_find_token(norm, "di") != NULL && pi_find(norm, "iphone") != NULL) return true;
    return false;
}

static bool pi_word_is_generic(const char* word, size_t len) {
    if(len < 3) return true; /* "5g", "2", "my" say nothing on their own */

    bool all_digits = true;
    for(size_t i = 0; i < len; i++) {
        if(word[i] < '0' || word[i] > '9') {
            all_digits = false;
            break;
        }
    }
    if(all_digits) return true;

    for(size_t i = 0; i < PI_GENERIC_COUNT; i++) {
        const char* g = PI_GENERIC_WORDS[i];
        if(strlen(g) == len && strncmp(g, word, len) == 0) return true;
    }
    return false;
}

/**
 * A name points at one address when at least one of its words is not part of
 * the shared vocabulary of every router on the street. "Hilton_Honors" is the
 * same in nine hundred buildings; "Sharma_Home_5G" is one front door.
 */
static bool pi_is_distinctive(const char* norm) {
    size_t i = 0;
    while(norm[i] != '\0') {
        if(!pi_is_alnum(norm[i])) {
            i++;
            continue;
        }
        size_t start = i;
        while(norm[i] != '\0' && pi_is_alnum(norm[i])) i++;
        if(!pi_word_is_generic(norm + start, i - start)) return true;
    }
    return false;
}

PiSsidInfo pi_classify(const char* ssid) {
    PiSsidInfo info = {PiCatUnknown, PiFlagNone};
    char norm[PI_SSID_MAX];

    pi_normalize(ssid, norm, sizeof(norm));
    if(norm[0] == '\0') return info;

    for(size_t i = 0; i < PI_RULE_COUNT; i++) {
        if(pi_rule_hit(norm, &PI_RULES[i])) {
            info.cat = (PiCat)PI_RULES[i].cat;
            break;
        }
    }

    if(pi_has_personal_name(norm)) info.flags |= PiFlagPersonalName;
    if(pi_is_distinctive(norm)) info.flags |= PiFlagGeolocatable;

    return info;
}

/* -------------------------------------------------------------- vocabulary */

typedef struct {
    const char* label;
    char tag;
    uint8_t weight;
    const char* reveals;
} PiCatInfo;

/*
 * The `reveals` line is what the networks screen prints under the list, in a
 * strip 128 pixels wide. Every one of them is kept under 23 characters so it
 * lands inside that strip at the widest font it could be drawn in - checked by
 * tools_gen_mockups.py, which renders wider than the hardware does.
 */
static const PiCatInfo PI_CAT_INFO[PiCatCount] = {
    [PiCatUnknown] = {"Unknown", '?', 2, "A place we cannot name"},
    [PiCatHome] = {"Home", 'H', 5, "Where you sleep."},
    [PiCatHotspot] = {"Hotspot", 'P', 3, "A phone you carry."},
    [PiCatHotel] = {"Hotel", 'T', 4, "A night away from home"},
    [PiCatAirport] = {"Airport", 'A', 3, "A city you flew from."},
    [PiCatAirline] = {"In-flight", 'F', 3, "An airline you fly."},
    [PiCatTransit] = {"Transit", 'R', 2, "How you get around."},
    [PiCatCafe] = {"Cafe", 'C', 2, "Where you take a break"},
    [PiCatRetail] = {"Shop", 'S', 2, "Where you spend money."},
    [PiCatGym] = {"Gym", 'G', 3, "Your weekly routine."},
    [PiCatMedical] = {"Health", 'M', 5, "Care you have needed."},
    [PiCatEducation] = {"Study", 'E', 3, "Where you study."},
    [PiCatWorkplace] = {"Work", 'W', 4, "Where you work."},
    [PiCatVehicle] = {"Vehicle", 'V', 3, "The car you drive."},
    [PiCatDevice] = {"Device", 'D', 1, "Gear you own."},
    [PiCatPublic] = {"Carrier", 'X', 1, "Your internet provider"},
};

const char* pi_cat_label(PiCat cat) {
    if(cat >= PiCatCount) return PI_CAT_INFO[PiCatUnknown].label;
    return PI_CAT_INFO[cat].label;
}

char pi_cat_tag(PiCat cat) {
    if(cat >= PiCatCount) return PI_CAT_INFO[PiCatUnknown].tag;
    return PI_CAT_INFO[cat].tag;
}

const char* pi_cat_reveals(PiCat cat) {
    if(cat >= PiCatCount) return PI_CAT_INFO[PiCatUnknown].reveals;
    return PI_CAT_INFO[cat].reveals;
}

uint8_t pi_cat_weight(PiCat cat) {
    if(cat >= PiCatCount) return PI_CAT_INFO[PiCatUnknown].weight;
    return PI_CAT_INFO[cat].weight;
}

/* -------------------------------------------------------------------- MACs */

typedef struct {
    uint8_t oui[3];
    const char* name;
} PiOui;

/*
 * A short table, on purpose. It covers the radios that actually turn up in a
 * room full of people; anything else reads as "unknown vendor", which is an
 * honest answer and a better one than a guess.
 */
static const PiOui PI_OUIS[] = {
    {{0x00, 0x03, 0x93}, "Apple"},     {{0x00, 0x0A, 0x95}, "Apple"},
    {{0x00, 0x17, 0xF2}, "Apple"},     {{0x00, 0x1E, 0xC2}, "Apple"},
    {{0x00, 0x23, 0x12}, "Apple"},     {{0x00, 0x26, 0xBB}, "Apple"},
    {{0x28, 0x6A, 0xBA}, "Apple"},     {{0x3C, 0x15, 0xC2}, "Apple"},
    {{0x40, 0x33, 0x1A}, "Apple"},     {{0x4C, 0x8D, 0x79}, "Apple"},
    {{0x5C, 0x95, 0xAE}, "Apple"},     {{0x6C, 0x40, 0x08}, "Apple"},
    {{0x78, 0x4F, 0x43}, "Apple"},     {{0x7C, 0xD1, 0xC3}, "Apple"},
    {{0x8C, 0x2D, 0xAA}, "Apple"},     {{0x90, 0x84, 0x0D}, "Apple"},
    {{0xA4, 0x83, 0xE7}, "Apple"},     {{0xAC, 0xBC, 0x32}, "Apple"},
    {{0xD0, 0x81, 0x7A}, "Apple"},     {{0xF0, 0x18, 0x98}, "Apple"},

    {{0x00, 0x12, 0xFB}, "Samsung"},   {{0x00, 0x21, 0x19}, "Samsung"},
    {{0x08, 0x37, 0x3D}, "Samsung"},   {{0x18, 0x3A, 0x2D}, "Samsung"},
    {{0x28, 0x39, 0x5E}, "Samsung"},   {{0x34, 0x23, 0xBA}, "Samsung"},
    {{0x5C, 0x0A, 0x5B}, "Samsung"},   {{0x78, 0x1F, 0xDB}, "Samsung"},
    {{0x8C, 0x77, 0x12}, "Samsung"},   {{0xC0, 0xBD, 0xD1}, "Samsung"},

    {{0x3C, 0x5A, 0xB4}, "Google"},    {{0x54, 0x60, 0x09}, "Google"},
    {{0x94, 0x95, 0xA0}, "Google"},    {{0xF4, 0xF5, 0xE8}, "Google"},

    {{0x0C, 0x1D, 0xAF}, "Xiaomi"},    {{0x28, 0xE3, 0x1F}, "Xiaomi"},
    {{0x64, 0x09, 0x80}, "Xiaomi"},    {{0xF8, 0xA4, 0x5F}, "Xiaomi"},

    {{0x00, 0x18, 0x82}, "Huawei"},    {{0x20, 0x0B, 0xC7}, "Huawei"},
    {{0x48, 0x46, 0xFB}, "Huawei"},    {{0xC4, 0x07, 0x2F}, "Huawei"},

    {{0x94, 0x65, 0x2D}, "OnePlus"},   {{0xC0, 0xEE, 0xFB}, "OnePlus"},
    {{0x2C, 0x54, 0x91}, "OPPO"},      {{0x30, 0x74, 0x96}, "vivo"},

    {{0x00, 0x1A, 0x11}, "Motorola"},  {{0x40, 0x78, 0x6A}, "Motorola"},
    {{0x00, 0x0E, 0x6D}, "Sony"},      {{0x40, 0xB8, 0x37}, "Sony"},
    {{0x00, 0x1C, 0x62}, "LG"},        {{0xC4, 0x9A, 0x02}, "LG"},
    {{0x00, 0x02, 0xEE}, "Nokia"},

    {{0x00, 0x1B, 0x77}, "Intel"},     {{0x34, 0x02, 0x86}, "Intel"},
    {{0x7C, 0xB2, 0x7D}, "Intel"},     {{0xA0, 0xA8, 0xCD}, "Intel"},
    {{0x00, 0x24, 0xD7}, "Intel"},

    {{0x44, 0x65, 0x0D}, "Amazon"},    {{0xFC, 0xA1, 0x83}, "Amazon"},
    {{0xB8, 0x27, 0xEB}, "Raspberry Pi"},
    {{0xDC, 0xA6, 0x32}, "Raspberry Pi"},
    {{0x24, 0x0A, 0xC4}, "Espressif"}, {{0x7C, 0x9E, 0xBD}, "Espressif"},
    {{0x30, 0xAE, 0xA4}, "Espressif"},

    {{0x00, 0x1D, 0x7E}, "Cisco"},     {{0x00, 0x50, 0x56}, "VMware"},
    {{0x00, 0x14, 0x22}, "Dell"},      {{0x3C, 0x2C, 0x30}, "ASUS"},
    {{0x50, 0xC7, 0xBF}, "TP-Link"},   {{0xC0, 0x25, 0xE9}, "TP-Link"},
    {{0x20, 0x4E, 0x7F}, "NETGEAR"},   {{0xA0, 0x63, 0x91}, "NETGEAR"},
    {{0x00, 0x1E, 0x58}, "D-Link"},    {{0x00, 0x1C, 0xF0}, "D-Link"},
    {{0x9C, 0xD3, 0x5B}, "Samsung"},   {{0xB4, 0x74, 0x9F}, "Askey"},
    {{0x00, 0x09, 0x4C}, "Cisco"},     {{0x60, 0x38, 0xE0}, "Belkin"},
};

#define PI_OUI_COUNT (sizeof(PI_OUIS) / sizeof(PI_OUIS[0]))

bool pi_mac_is_randomized(const uint8_t mac[6]) {
    return (mac[0] & 0x02u) != 0u;
}

const char* pi_mac_vendor(const uint8_t mac[6]) {
    if(pi_mac_is_randomized(mac)) return NULL;
    for(size_t i = 0; i < PI_OUI_COUNT; i++) {
        if(PI_OUIS[i].oui[0] == mac[0] && PI_OUIS[i].oui[1] == mac[1] &&
           PI_OUIS[i].oui[2] == mac[2]) {
            return PI_OUIS[i].name;
        }
    }
    return NULL;
}

static char pi_hex_digit(uint8_t v) {
    /* Both arms cast explicitly: the firmware builds with -Werror=sign-compare
     * and an unsigned `v` would otherwise drag one branch of the ?: with it. */
    if(v < 10u) return (char)('0' + (int)v);
    return (char)('A' + (int)v - 10);
}

void pi_mac_str(const uint8_t mac[6], char* out, size_t out_len) {
    if(out_len == 0) return;
    if(out_len < 18u) {
        out[0] = '\0';
        return;
    }
    size_t p = 0;
    for(size_t i = 0; i < 6; i++) {
        out[p++] = pi_hex_digit((uint8_t)(mac[i] >> 4));
        out[p++] = pi_hex_digit((uint8_t)(mac[i] & 0x0Fu));
        if(i < 5) out[p++] = ':';
    }
    out[p] = '\0';
}

void pi_mac_str_masked(const uint8_t mac[6], char* out, size_t out_len) {
    if(out_len == 0) return;
    if(out_len < 18u) {
        out[0] = '\0';
        return;
    }
    size_t p = 0;
    for(size_t i = 0; i < 3; i++) {
        out[p++] = pi_hex_digit((uint8_t)(mac[i] >> 4));
        out[p++] = pi_hex_digit((uint8_t)(mac[i] & 0x0Fu));
        out[p++] = ':';
    }
    const char* tail = "**:**:**";
    for(size_t i = 0; tail[i] != '\0'; i++) out[p++] = tail[i];
    out[p] = '\0';
}

void pi_ssid_mask(const char* ssid, char* out, size_t out_len) {
    if(out_len == 0) return;
    if(!ssid || ssid[0] == '\0') {
        const char* none = "<broadcast>";
        size_t i = 0;
        for(; none[i] != '\0' && i + 1 < out_len; i++) out[i] = none[i];
        out[i] = '\0';
        return;
    }

    size_t keep = 0;
    while(keep < 3u && ssid[keep] != '\0') keep++;

    size_t p = 0;
    for(size_t i = 0; i < keep && p + 1 < out_len; i++) out[p++] = ssid[i];
    const char* dots = "...";
    for(size_t i = 0; dots[i] != '\0' && p + 1 < out_len; i++) out[p++] = dots[i];
    out[p] = '\0';
}

/* ------------------------------------------------------------ fingerprints */

uint32_t pi_fnv1a(const uint8_t* data, size_t len, uint32_t seed) {
    uint32_t h = seed;
    for(size_t i = 0; i < len; i++) {
        h ^= (uint32_t)data[i];
        h *= 16777619u;
    }
    return h;
}

bool pi_seq_continues(uint16_t prev, uint16_t next, uint16_t window) {
    uint16_t delta = (uint16_t)((next - prev) & 0x0FFFu);
    return delta != 0u && delta <= window;
}
