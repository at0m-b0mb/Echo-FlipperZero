# Changelog

## v1.0 — 2026-08-11

First release.

### The app
- **Chamber** — the room drawn as ripples, one per probe request, with the listener in
  the middle. `OK` flips to a raw feed of everything said, named and unnamed.
- **Networks** — every network name heard, sorted by how much it gives away, with a
  plain-English line under each and a marker on the names distinctive enough to resolve
  to one street address.
- **Devices** — every radio, graded A+ to F, most exposed first.
- **Dossier** — three pages per device: what it named, the five columns behind its grade,
  and what its owner could do about it.
- **How it works** — four animated pages on probe requests, why names are places, and why
  a new MAC is not a new phone.
- **Save report** — a redacted summary to `apps_data/echo/`: MACs cut to the vendor
  prefix, names to three characters.
- **Demo mode** — a scripted room of five phones, so the app works with no radio attached.

### The engine
- 250-odd classification rules over sixteen categories, with token-boundary matching so
  `LAX-Free-WiFi` is an airport and `Relaxing Room` is not.
- Distinctiveness test: a name built entirely from the vocabulary every router on the
  street shares pins nothing; one unfamiliar word and it points somewhere.
- MAC linking by probe fingerprint (`Likely`) upgraded by a continuing 802.11 sequence
  counter (`Confirmed`).
- Exposure score across five columns, with a floor at B for any named network and a cap
  at D for a home or health network named from a permanent MAC.

### The radio
- ESP32 companion firmware: promiscuous receive, management frames, subtype 4 only.
  Brought up in `WIFI_MODE_NULL` so it cannot associate or transmit. Channel hop 1–13 or
  camp on one, per-device dedup, ~1 Hz heartbeat.

### Verification
- 44,000+ host checks over the classifier and the scorer, including a full sweep of the
  scorer's input space and a width budget on every user-facing string.
- Built and checked against firmware API 87.1 with `ufbt`; CI covers the release and dev
  SDK channels.
