#!/usr/bin/env python3
"""Builds ui/assets/sound_catalog.json for the Cadence sound library.

The catalog powers the "Biblioteca de sonidos" dialog: a browsable list of
free/CC audio that the user can download with attribution and drop into the
project.

This script is the single source of truth for the catalog so it can be
regenerated and extended reproducibly instead of hand-editing JSON.

Sources (licence + author captured automatically):

  Wikimedia Commons (see CATEGORY_SOURCES / CURATED_TITLES):
    - Category:Sounds of drum-kit instruments   (drum one-shots)
    - Category:Sounds of percussion instruments (percussion one-shots)
    - Category:Sounds of drums                  (drum one-shots / rolls)
    - Category:Samples of Beats                 (tempo-labelled beats)
    - hand-picked loop stems (Carrai Pass, Kevin MacLeod game loops)

  Internet Archive (see ARCHIVE_SOURCES): whole sample packs hosted as items,
    only items with an explicit PD/CC licence and only their original WAVs —
    e.g. Roland SC-8850 GM drum kit (PD), Casio SA-75 kit (PD), Korg Volca
    Modular kit (CC BY), Rhythm Ace loops (CC0), FLOOR BABA pack (CC0).

Entries kept from earlier runs that came from GitHub (Tonejs/audio, MFB
Tanzbar, infinite-state-sample-pack) are preserved as-is.

Each entry gets:
  id, name, category, url, license, author, size
  + type     "loop" | "oneshot"
  + bpm      integer when inferable from the file name
  + duration seconds (float) when reported by the source

Usage:
  python3 ui/tool/build_sound_catalog.py            # rewrite the catalog
  python3 ui/tool/build_sound_catalog.py --dry-run  # report, touch nothing
"""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

API = "https://commons.wikimedia.org/w/api.php"
UA = "CadenceDAW-SoundCatalogBuilder/1.0 (https://github.com; dev@localhost)"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CATALOG = os.path.join(ROOT, "ui", "assets", "sound_catalog.json")

AUDIO_EXT = (".ogg", ".oga", ".wav", ".flac", ".mp3", ".opus", ".m4a")

# A one-shot is short; anything longer is treated as a loop/base.
ONESHOT_MAX_SECONDS = 3.0
# A drum/perc hit with a long tail (cymbal, roll) is still a one-shot up to here.
ONESHOT_HARD_MAX_SECONDS = 8.0

# Names containing these are grooves/loops even inside a one-shot category.
LOOP_WORDS = re.compile(
    r"loop|beat|pattern|groove|break|bpm|shuffle|\bbar\b|riff|backing|jam"
    r"|accompaniment|improvisation|demo|ejemplo|demonstration|drumming|rumba"
    r"|solo\b|roll\b",
    re.I,
)

# Curated Commons categories -> (Cadence category, type hint).
#   hint "oneshot" -> always a loose hit (single-stroke drum kit samples)
#   hint "loop"    -> always a base/loop (beats, stems)
#   hint None      -> decide by duration (short = one-shot, long = loop)
CATEGORY_SOURCES = {
    "Category:Sounds of drum-kit instruments": ("Batería", "oneshot"),
    "Category:Sounds of drums": ("Batería", None),
    "Category:Sounds of percussion instruments": ("Percusión", None),
    "Category:Samples of Beats": ("Bases", "loop"),
    # Drum-machine patterns/beats (TR-808, TR-909, CR-78, EMU Orbit...).
    "Category:Sounds of drum machines": ("Bases", None),
    "Category:Audio files of drum patterns": ("Bases", None),
    # Instrument one-shots recorded for Commons (melodic/harmonic samples).
    "Category:Sounds of harmonicas": ("Instrumentos", None),
    "Category:Sounds of accordions": ("Instrumentos", None),
    "Category:Sounds of wind instruments": ("Instrumentos", None),
    "Category:Sounds of recorders": ("Instrumentos", None),
    "Category:Sounds of bass guitars": ("Instrumentos", None),
    "Category:Sounds of string instruments": ("Instrumentos", None),
    "Category:Sounds of cellos": ("Instrumentos", None),
    "Category:Sounds of clarinets": ("Instrumentos", None),
    "Category:Sounds of saxophones": ("Instrumentos", None),
    "Category:Sounds of trombone": ("Instrumentos", None),
    "Category:Sounds of oboes": ("Instrumentos", None),
    "Category:Sounds of resophonic instruments": ("Instrumentos", None),
    "Category:Sounds of keyboard instruments": ("Instrumentos", None),
    # Loose pitched note samples (single tones used as instrument one-shots).
    "Category:A4 - Musical note": ("Instrumentos", None),
    "Category:B4 - Musical note": ("Instrumentos", None),
    "Category:C3 - Musical note": ("Instrumentos", None),
    "Category:C4 - Musical note": ("Instrumentos", None),
    "Category:E4 (musical note)": ("Instrumentos", None),
    "Category:F3 (musical note)": ("Instrumentos", None),
}

# Hand-picked loop files (stems, beats, backing). Broad Commons searches are
# too noisy (TTS, podcasts, pronunciations), so loops come from explicit
# sources instead. "Carrai Pass" is a coherent royalty-free loop pack.
CURATED_TITLES = {
    # Carrai Pass loop pack (stems that line up at the same tempo).
    "File:Drum loop (Carrai Pass).wav": "Bases",
    "File:Snare loop (Carrai Pass).wav": "Bases",
    "File:Hat open loop (Carrai Pass).wav": "Bases",
    "File:Hat close loop (Carrai Pass).wav": "Bases",
    "File:Clap and snare loop (Carrai Pass).wav": "Bases",
    "File:Bass loop 1 (Carrai Pass).wav": "Bases",
    "File:Bass loop 2 (Carrai Pass).wav": "Bases",
    "File:Siren loop (Carrai Pass).wav": "Bases",
    "File:Trumpet loop 1 (Carrai Pass).wav": "Bases",
    "File:Trumpet loop 2 (Carrai Pass).wav": "Bases",
    "File:Refrain melody loop 1 (Carrai Pass).wav": "Bases",
    "File:Refrain melody loop 2 (Carrai Pass).wav": "Bases",
    "File:Refrain melody loop 3 (Carrai Pass).wav": "Bases",
    "File:Auxiliary melody loop 1 (Carrai Pass).wav": "Bases",
    "File:Auxiliary melody loop 2 (Carrai Pass).wav": "Bases",
    "File:Basic melody loop 1 (Carrai Pass).wav": "Bases",
    "File:Basic melody loop 2 (Carrai Pass).wav": "Bases",
    # Kevin MacLeod game loops (CC BY).
    "File:Funk Game Loop (ISRC USUAN1100839).mp3": "Bases",
    "File:Slow Ska Game Loop (ISRC USUAN1100838).mp3": "Bases",
    "File:Wah Game Loop (ISRC USUAN1100837).mp3": "Bases",
    "File:Bassa Island Game Loop (ISRC USUAN1100840).mp3": "Bases",
    "File:Space Fighter Loop (ISRC USUAN1100672).mp3": "Bases",
    "File:Bushwick Tarantella Loop (ISRC USUAN1300003).mp3": "Bases",
    "File:Go Cart - Loop Mix (ISRC USUAN1300008).mp3": "Bases",
    "File:Music loop 106bpm (Still frivolous).oga": "Bases",
}

# Names we never want in the library (scraping noise that slipped into
# categories/searches: pronunciations, TTS, podcasts, full albums).
NOISE = re.compile(
    r"LL-Q\d|^Nl-|^En-us-|\bepisode\b|\bearwolf\b|pronunciation|\bfull album\b"
    r"|jury discussion|\bTTS\b|^hello, world|\bpurr\b|listening to raindrops"
    r"|crotophaga|groove-billed|jeu des 1 000 euros|métallophone"
    r"|^chominciamento|^london bridge|^fondo\b|^base mood|polyrhythm"
    r"|^stimmgabel$|karfreitagsratschen|^jew's harp|^pelog$"
    r"|^tom (robinson|scott|tugendhat)|^tom,? tom,? the piper",
    re.I,
)

# ---------------------------------------------------------------------------
# Internet Archive (archive.org) sources.
#
# Unlike Commons, archive.org hosts whole sample packs as items; each item
# exposes its individual files under /download/<identifier>/<path>. We only
# take items with an explicit public-domain / CC licence and only their
# original .wav uploads (the .mp3/.flac/.ogg siblings are site derivatives).
# ---------------------------------------------------------------------------

ARCHIVE_API = "https://archive.org/metadata/"
ARCHIVE_DL = "https://archive.org/download/"

# licenceurl suffix -> short label shown in the library dialog.
ARCHIVE_LICENSE = {
    "publicdomain/mark/1.0": "Public Domain",
    "publicdomain/zero/1.0": "CC0 1.0",
    "licenses/by/4.0": "CC BY 4.0",
    "licenses/by/3.0": "CC BY 3.0",
    "licenses/by/2.5": "CC BY 2.5",
    "licenses/by-sa/4.0": "CC BY-SA 4.0",
    "licenses/by-sa/3.0": "CC BY-SA 3.0",
}

# Hand-percussion names routed to "Percusión" even when the pack sits under
# a drum category.
PERC_WORDS = re.compile(
    r"agogo|conga|bongo|timbal|cowbell|clap|shaker|tambourin|wood ?block|"
    r"guiro|whistle|triangle|cabasa|maraca|clave|castanet|cajon|djembe|"
    r"surdo|repinique|cuica|whistle|bell ?tree|crotale",
    re.I,
)

# Each source: identifier, default category/type, and optional routing.
#   folder_routing: {"/PATH/PART/": (category, type)}  (order matters)
#   route_percussion: hand-percussion names go to "Percusión"
#   bpm_prefix: read a leading NNN (e.g. "080RhythmAce001" -> 80 BPM)
#   name_with_parent: prefix the name with its parent folder
#   name_prefix: prefix the name with a fixed tag (bare-numbered packs)
#   limit: cap the number of entries taken from this item
ARCHIVE_SOURCES = [
    {  # GM drum kit, one WAV per kit piece (Agogo, Bongo, Kick, Snare...).
        "identifier": "SC8850DrumSamples",
        "category": "Batería", "type": "oneshot",
        "route_percussion": True,
    },
    {  # Casio SA-75 drum/percussion hits (Mailbox Badger Vol. 1).
        "identifier": "HeatDish",
        "category": "Batería", "type": "oneshot",
        "route_percussion": True,
    },
    {  # Mailbox Badger Vol. 2: more Casio/analog hits (CC BY 3.0).
        # Names are a grab-bag, so route by name to keep drum hits in Batería
        # and send hand percussion, nature, foley and voice FX to their homes.
        "identifier": "mailboxbadgerdrumsamplesvolume2",
        "category": "Batería", "type": "oneshot",
        "name_routing": [
            (r"conga|bongo|timbale|guiro|shaker|wood.?block|clave|tambourine"
             r"|cowbell|djembe|cajon|cabasa|maraca|agogo|triangle|castanet|darbuka",
             "Percusión", "oneshot"),
            (r"cricket|bird|frog|thunder|rain|drizzle|firework|spout|storm"
             r"|wind|water|ocean|submarine",
             "Naturaleza", "oneshot"),
            (r"glass|breaking|aerosol|switch|startup|splitting|noise|vibration"
             r"|ringing|spring|sprung",
             "Foley", "oneshot"),
            (r"applause|boo|fart|scream|cat call|dial|message|smooch|quack"
             r"|heehaw|eep|death|silly|tough luck|good|okay|\(ha\)|ko$|wah$",
             "FX", "oneshot"),
        ],
    },
    {  # Patrick Callan pack: cajon / bodhran / hand-percussion one-shots.
        "identifier": "public_domain_drum_samples_pack_for_upcoming_2026_album",
        "category": "Percusión", "type": "oneshot",
    },
    {  # Trap drum kit (PD); voice tags are not usable audio.
        "identifier": "thehopeneetscapeedrumkit",
        "category": "Batería", "type": "oneshot",
        "exclude": r"voicetag|neetscapeenowmurda",
    },
    {  # Korg Volca Modular drum kit, one-shots (all of them).
        "identifier": "beckstrom-volca-modular-drum-samples-1",
        "category": "Batería", "type": "oneshot",
    },
    {  # Rhythm Ace drum-machine grooves (CC0), BPM in the leading digits.
        "identifier": "RhythmAceDrumLoopsForDownload",
        "category": "Bases", "type": "loop",
        "bpm_prefix": True,
    },
    {  # Analog drum loops (CC BY).
        "identifier": "analogdrumloops",
        "category": "Bases", "type": "loop",
    },
    {  # Homemade modular drum loops, BPM in the name (127bpm).
        "identifier": "beckstrom-modular-drum-loops-1",
        "category": "Bases", "type": "loop",
    },
    {  # Organised pack: BASS / DRUMS / LOOPS / MISC (CC0).
        "identifier": "FLOOR-BABA-BABA-LAB-INGREDIENTS",
        "folder_routing": {
            "/BASS/": ("Bajos", "oneshot"),
            "/DRUMS/": ("Batería", "oneshot"),
            "/LOOPS/": ("Bases", "loop"),
        },
        "name_with_parent": True,
    },
    {  # Boss DR-110 drum-machine patterns (Public Domain Mark), BPM in name.
        "identifier": "Boss_DR-110_Rhythms",
        "category": "Bases", "type": "loop",
        "name_with_parent": True,
    },
    {  # Korg ESX-1 drum machine + Kaoss Pad vinyl resamples (CC0).
        # The files are bare numbers, so tag them to stay recognisable.
        "identifier": "ESX_Samples_RamzGT",
        "category": "Batería", "type": "oneshot",
        "name_prefix": "ESX",
    },
    {  # 108 BPM breakbeat: one full break plus its slices (CC0).
        "identifier": "108_bpm_breakbeat_to_use",
        "category": "Batería", "type": "oneshot",
    },
    {  # Hohner Special 20 harmonica (CC0). The upload also mirrors a standard
        # ocarina already covered by freepats/ocarina1, so skip that half.
        "identifier": "sangeet-bank-v21-cc0",
        "category": "Mundo", "type": "oneshot",
        "exclude": r"^StdOcarina",
    },
    {  # Beckstrom "Packet Loss": modular glitch textures (CC BY 4.0).
        "identifier": "volcaKeysGoToWifi40perPacketLoss-1.32_Beckstrom",
        "category": "Texturas", "type": "oneshot", "limit": 120,
    },
]


def api_get(params: dict, tries: int = 6) -> dict:
    params = {**params, "format": "json", "maxlag": "5"}
    url = API + "?" + urllib.parse.urlencode(params)
    last: Exception | None = None
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=45) as resp:
                return json.load(resp)
        except Exception as exc:  # noqa: BLE001 - network flakiness, retry
            last = exc
            time.sleep(1.5 * (attempt + 1))
    raise RuntimeError(f"Commons API failed: {url}") from last


def category_members(category: str) -> list[str]:
    titles: list[str] = []
    cont: dict = {}
    while True:
        data = api_get({
            "action": "query",
            "list": "categorymembers",
            "cmtitle": category,
            "cmtype": "file",
            "cmlimit": "500",
            **cont,
        })
        titles += [m["title"] for m in data["query"]["categorymembers"]]
        if "continue" in data:
            cont = data["continue"]
            time.sleep(0.6)
        else:
            break
    return titles


def fetch_info(titles: list[str]) -> dict[str, dict]:
    """Batch-fetch imageinfo (url, size, duration, licence, author)."""
    info: dict[str, dict] = {}
    titles = [t for t in titles if t.lower().endswith(AUDIO_EXT)]
    for i in range(0, len(titles), 25):
        chunk = titles[i:i + 25]
        data = api_get({
            "action": "query",
            "titles": "|".join(chunk),
            "prop": "imageinfo",
            "iiprop": "url|size|mime|extmetadata",
            "iiextmetadatafilter": "LicenseShortName|Artist|ImageDescription",
        })
        for page in data.get("query", {}).get("pages", {}).values():
            title = page.get("title")
            ii = page.get("imageinfo")
            if not title or not ii:
                continue
            info[title] = ii[0]
        time.sleep(0.6)
    return info


def meta_field(ii: dict, key: str) -> str:
    raw = ii.get("extmetadata", {}).get(key, {}).get("value", "") or ""
    return html.unescape(re.sub(r"<[^>]+>", "", str(raw))).strip()


def clean_name(title: str) -> str:
    name = title[len("File:"):] if title.startswith("File:") else title
    name = re.sub(r"\.(ogg|oga|wav|flac|mp3|opus|m4a)$", "", name, flags=re.I)
    name = name.replace("_", " ").strip()
    return name


def parse_bpm(name: str) -> int | None:
    m = re.search(r"(\d{2,3})\s*BPM", name, re.I) or re.search(r"@\s*(\d{2,3})", name)
    if not m:
        return None
    bpm = int(m.group(1))
    return bpm if 40 <= bpm <= 300 else None


def is_junk(title: str, ii: dict) -> bool:
    """Drop non-audio and obvious scraping noise (speech, images)."""
    if not title.lower().endswith(AUDIO_EXT):
        return True
    # Note: Ogg/Opus report "application/ogg", so only reject real media types.
    mime = ii.get("mime", "")
    if mime.startswith(("image/", "video/")):
        return True
    return bool(NOISE.search(clean_name(title)))


def classify(name: str, duration: float, hint: str | None = None) -> str:
    """loop vs oneshot. The source category wins unless the name is a groove."""
    if LOOP_WORDS.search(name):
        return "loop"
    if hint in ("loop", "oneshot"):
        return hint
    return "loop" if duration > ONESHOT_MAX_SECONDS else "oneshot"


def entry_from(title: str, ii: dict, category: str, hint: str | None) -> dict | None:
    if is_junk(title, ii):
        return None
    url = ii.get("url")
    if not url:
        return None
    url = norm_url(url)
    name = clean_name(title)
    duration = float(ii.get("duration") or 0.0)
    e = {
        "name": name,
        "category": category,
        "url": url,
        "license": meta_field(ii, "LicenseShortName") or "CC",
        "author": meta_field(ii, "Artist"),
        "size": int(ii.get("size") or 0),
        "type": classify(name, duration, hint),
        "duration": round(duration, 2),
    }
    bpm = parse_bpm(name)
    if bpm:
        e["bpm"] = bpm
    return e


def load_existing() -> list[dict]:
    if not os.path.exists(CATALOG):
        return []
    with open(CATALOG, encoding="utf-8") as fh:
        return json.load(fh)


def gather_discoveries() -> list[tuple[str, str, str | None, dict]]:
    """Returns a list of (title, cadence_category, hint, imageinfo)."""
    wanted: dict[str, tuple[str, str | None]] = {}
    for cat, (dest, hint) in CATEGORY_SOURCES.items():
        for title in category_members(cat):
            wanted.setdefault(title, (dest, hint))
        time.sleep(0.5)
    for title, dest in CURATED_TITLES.items():
        wanted.setdefault(title, (dest, "loop"))

    info = fetch_info(list(wanted))
    out: list[tuple[str, str, str | None, dict]] = []
    for title, (dest, hint) in wanted.items():
        if title in info:
            out.append((title, dest, hint, info[title]))
    return out


def archive_http_json(url: str, tries: int = 4) -> dict:
    last: Exception | None = None
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=60) as resp:
                return json.load(resp)
        except Exception as exc:  # noqa: BLE001 - network flakiness, retry
            last = exc
            time.sleep(1.0 * (attempt + 1))
    raise RuntimeError(f"archive.org failed: {url}") from last


def archive_license(licenseurl: str) -> str:
    for key, label in ARCHIVE_LICENSE.items():
        if key in licenseurl:
            return label
    return ""


def archive_url(identifier: str, path: str) -> str:
    quoted = "/".join(urllib.parse.quote(seg) for seg in path.split("/"))
    return f"{ARCHIVE_DL}{identifier}/{quoted}"


def parse_duration(value) -> float:
    """archive.org reports length as 'M:SS.ff', 'H:MM:SS' or plain seconds."""
    if value is None:
        return 0.0
    text = str(value).strip()
    if not text:
        return 0.0
    try:
        if ":" in text:
            seconds = 0.0
            for part in text.split(":"):
                seconds = seconds * 60 + float(part)
            return seconds
        return float(text)
    except ValueError:
        return 0.0


# ---------------------------------------------------------------------------
# GitHub-hosted CC0 sample libraries. VCSL and VSCO 2 CE (both by Versilian)
# are plain WAV trees served over raw.githubusercontent.com, so we index the
# repository tree and build one catalogue entry per sample.
# ---------------------------------------------------------------------------

GITHUB_AUDIO_EXTS = (".wav", ".flac", ".aif", ".aiff", ".ogg", ".mp3")

# Each source is a raw.githubusercontent.com WAV/FLAC tree. "families" keeps
# every sample of a big orchestral library; "category"+"limit" pulls a
# capped, evenly-spaced subset from a community SFZ pack (those ship dozens
# of mic/velocity/round-robin takes per note, far too many to list in full).
GITHUB_LIBRARIES = [
    {
        "repo": "sgossner/VCSL",
        "license": "CC0 1.0",
        "author": "Versilian Studios (VCSL)",
        "instrument_index": 2,  # e.g. Membranophones/Struck Membranophones/<inst>/f.wav
        "families": {
            "Membranophones": "Batería",
            "Idiophones": "Percusión",
            "Aerophones": "Instrumentos",
            "Chordophones": "Instrumentos",  # pianos, harpsichords, harps...
            "Electrophones": "Sinte",
        },
    },
    {
        # Only the orchestral families VCSL does not already cover; its
        # percussion/pianos duplicate VCSL and Miscellania Raw is assorted junk.
        "repo": "sgossner/VSCO-2-CE",
        "license": "CC0 1.0",
        "author": "Versilian Studios (VSCO 2 CE)",
        "instrument_index": 1,  # e.g. Brass/<inst>/<articulation>/f.wav
        "families": {
            "Brass": "Orquestal",
            "Strings": "Orquestal",
            "Woodwinds": "Orquestal",
        },
    },

    # --- Drum kits (the priority: real multitrack kits, capped) -------------
    {"repo": "sfzinstruments/virtuosity_drums", "license": "CC0 1.0",
     "author": "Versilian Studios & Karoryfer Samples", "category": "Batería", "limit": 250},
    {"repo": "sfzinstruments/DrumGizmo.DRSKit", "license": "CC BY 4.0",
     "author": "DrumGizmo project", "category": "Batería", "limit": 250},
    {"repo": "sfzinstruments/DrumGizmo.MuldjordKit", "license": "CC BY 4.0",
     "author": "DrumGizmo project", "category": "Batería", "limit": 250},
    {"repo": "freepats/muldjordkit", "license": "CC BY 4.0",
     "author": "FreePats (Lars Muldjord)", "category": "Batería", "limit": 250},
    {"repo": "sfzinstruments/WilkinsonAudio.NakedDrums", "license": "CC BY 4.0",
     "author": "Wilkinson Audio", "category": "Batería", "limit": 250},
    {"repo": "sfzinstruments/SamsSonor", "license": "CC BY-SA 4.0",
     "author": "Sam Greene", "category": "Batería", "limit": 150},
    {"repo": "sfzinstruments/karoryfer.swirly-drums", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Batería", "limit": 250},
    {"repo": "sfzinstruments/karoryfer.unruly-drums", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Batería", "limit": 250},
    {"repo": "sfzinstruments/karoryfer.big-rusty-drums", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Batería", "limit": 250},
    {"repo": "sfzinstruments/karoryfer.frankensnare", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Batería", "limit": 250},
    {"repo": "sfzinstruments/Karoryfer.TheHatWithThePhat", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Batería", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.gogodze-phu-vol-ii", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Batería", "limit": 200},
    {"repo": "MckAudio/MckSamplePacks", "license": "CC0 1.0",
     "author": "MckAudio", "category": "Batería", "limit": 300},
    {"repo": "Gradigang-Solutions/kringel-samples", "license": "CC0 1.0",
     "author": "Gradigang Solutions", "category": "Batería", "limit": 60},
    {"repo": "octalmusic/octal-samples", "license": "CC0 1.0",
     "author": "Octal Music", "category": "Batería", "limit": 60},

    # --- Percussion --------------------------------------------------------
    {"repo": "sfzinstruments/body_percussion", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Percusión", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.gogodze-phu-vol-i", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Percusión", "limit": 200},
    {"repo": "freepats/world-percussion", "license": "CC0 1.0",
     "author": "FreePats", "category": "Percusión", "limit": 240},
    {"repo": "freepats/synthesizer-percussion", "license": "CC0 1.0",
     "author": "FreePats", "category": "Percusión", "limit": 40},
    {"repo": "freepats/timpani", "license": "CC0 1.0",
     "author": "FreePats", "category": "Percusión", "limit": 40},
    {"repo": "freepats/tubular-bells1", "license": "CC0 1.0",
     "author": "FreePats", "category": "Percusión", "limit": 40},
    {"repo": "freepats/hang-D-minor", "license": "CC0 1.0",
     "author": "FreePats", "category": "Percusión", "limit": 60},
    {"repo": "freepats/glass", "license": "CC0 1.0",
     "author": "FreePats", "category": "Percusión", "limit": 150},
    {"repo": "sfzinstruments/kinwie.dim-cabasa", "license": "CC BY 4.0",
     "author": "Kinwie", "category": "Percusión", "limit": 250},
    {"repo": "sfzinstruments/jlearman.SteelDrum", "license": "Unlicense",
     "author": "Jeff Learman", "category": "Percusión", "limit": 250},

    # --- Pianos & keys -----------------------------------------------------
    {"repo": "freepats/upright-piano-KW", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 120},
    {"repo": "freepats/old-piano-FB", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 120},
    {"repo": "freepats/fm-piano1", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 60},
    {"repo": "freepats/fm-piano2", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 30},
    {"repo": "sfzinstruments/Osiris_Piano", "license": "CC0 1.0",
     "author": "Versilian Studios & Karoryfer Samples", "category": "Instrumentos", "limit": 250},
    {"repo": "sfzinstruments/BengtNilsson.HeadroomPiano", "license": "CC BY 4.0",
     "author": "Bengt Nilsson", "category": "Instrumentos", "limit": 250},
    {"repo": "sfzinstruments/karoryfer.scarypiano", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Instrumentos", "limit": 200},

    # --- Guitars & basses --------------------------------------------------
    {"repo": "freepats/spanish-classical-guitar", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 60},
    {"repo": "freepats/electric-guitar-FSBS-clean", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 120},
    {"repo": "freepats/electric-guitar-FSBS-dist1", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 120},
    {"repo": "freepats/electric-guitar-FSBS-dist2", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 120},
    {"repo": "freepats/electric-guitar-FSBS-direct", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 120},
    {"repo": "freepats/electric-guitar-FSBS-jazz", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 120},
    {"repo": "freepats/electric-bass-YR", "license": "CC0 1.0",
     "author": "FreePats", "category": "Bajos", "limit": 40},
    {"repo": "sfzinstruments/karoryfer.black-and-green-guitars", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Instrumentos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.shinyguitar", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Instrumentos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.emilyguitar", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Instrumentos", "limit": 200},
    {"repo": "sfzinstruments/ganjo", "license": "CC0 1.0",
     "author": "Pearce Roswell", "category": "Instrumentos", "limit": 70},
    {"repo": "sfzinstruments/dsmolken.double-bass", "license": "CC0 1.0",
     "author": "D. Smolken", "category": "Instrumentos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.big-little-bass", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Bajos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.black-and-blue-basses", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Bajos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.sneakybass", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Bajos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.meatbass", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Bajos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.growlybass", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Bajos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.swagbass", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Bajos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.fashionbass", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Bajos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.pastabass", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Bajos", "limit": 200},

    # --- Winds, brass & world instruments ---------------------------------
    {"repo": "sfzinstruments/MTG.SoloSax", "license": "CC BY 4.0",
     "author": "Music Technology Group (UPF)", "category": "Instrumentos", "limit": 250},
    {"repo": "sfzinstruments/karoryfer.bear-sax", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Instrumentos", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.weresax", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Instrumentos", "limit": 200},
    {"repo": "sfzinstruments/Ixox.Flute", "license": "CC BY 4.0",
     "author": "Xavier Hosxe", "category": "Instrumentos", "limit": 90},
    {"repo": "sfzinstruments/karoryfer.war-tuba", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Orquestal", "limit": 200},
    {"repo": "freepats/bagpipe", "license": "CC0 1.0",
     "author": "FreePats", "category": "Mundo", "limit": 40},
    {"repo": "freepats/ocarina1", "license": "CC0 1.0",
     "author": "FreePats", "category": "Mundo", "limit": 20},
    {"repo": "freepats/button-accordion-HN", "license": "CC0 1.0",
     "author": "FreePats", "category": "Mundo", "limit": 40},
    {"repo": "freepats/ukulele1", "license": "CC0 1.0",
     "author": "FreePats", "category": "Instrumentos", "limit": 20},
    {"repo": "sfzinstruments/cithara-barbarica", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Mundo", "limit": 200},
    {"repo": "sfzinstruments/hungarian_zither", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Mundo", "limit": 210},
    {"repo": "sfzinstruments/aliexpress-erhu", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Mundo", "limit": 170},
    {"repo": "sfzinstruments/Karoryfer.HorsePulse", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Mundo", "limit": 200},
    {"repo": "cleary/samples-cello-plucked", "license": "CC0 1.0",
     "author": "Cleary", "category": "Instrumentos", "limit": 20},
    {"repo": "Yale-OMI/sample-pack-vol-1", "license": "CC0 1.0",
     "author": "Yale Open Music Initiative", "category": "Mundo", "limit": 80},

    # --- Synths ------------------------------------------------------------
    {"repo": "sfzinstruments/karoryfer.cowsynth", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Sinte", "limit": 40},
    {"repo": "sfzinstruments/karoryfer.caveman-cosmonaut", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Sinte", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.ergo", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Sinte", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.string-cyborgs", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Sinte", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.squidpipes", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Sinte", "limit": 200},
    {"repo": "freepats/synth-bass-1", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/synth-bass-2", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 20},
    {"repo": "freepats/synth-bass-lead", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/synth-brass-1", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 20},
    {"repo": "freepats/synth-brass-2", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 20},
    {"repo": "freepats/synth-square", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/synth-strings-1", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 20},
    {"repo": "freepats/synth-strings-2", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/synth-calliope", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/synth-crystal", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 20},
    {"repo": "freepats/synth-fifths", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/synth-goblins", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/synth-pad-bowed", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 40},
    {"repo": "freepats/synth-pad-choir", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 20},
    {"repo": "freepats/synth-scifi", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/synth-soundtrack", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/new-age", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 30},
    {"repo": "freepats/sweep-pad", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 20},
    {"repo": "freepats/lately-bass", "license": "CC0 1.0",
     "author": "FreePats", "category": "Sinte", "limit": 20},

    # --- Strings, world winds & voices (second pass) -----------------------
    {"repo": "sfzinstruments/karoryfer-bigcat.cello", "license": "CC0 1.0",
     "author": "Karoryfer Samples & Bigcat Instruments", "category": "Instrumentos", "limit": 200},
    {"repo": "SamsterBirdies/panflute", "license": "CC0 1.0",
     "author": "SamsterBirdies", "category": "Mundo", "limit": 30},
    {"repo": "sfzinstruments/legato_vocal_tutorial", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Voces", "limit": 200},
    {"repo": "sfzinstruments/karoryfer.272-merry-orks", "license": "CC0 1.0",
     "author": "Karoryfer Samples", "category": "Voces", "limit": 200},

    # --- Small drum-machine packs ------------------------------------------
    {"repo": "sykesgabri/PSS-470-Drums", "license": "CC0 1.0",
     "author": "sykesgabri (Yamaha PSS-470)", "category": "Batería", "limit": 20},
    {"repo": "rudkx/samples", "license": "CC0 1.0",
     "author": "rudkx", "category": "Batería", "limit": 10},

    # --- Sound effects ------------------------------------------------------
    {  # Big soundbank curated from freesound.org uploads. The same repo also
       # bundles Sonniss GDC material, whose licence allows use in productions
       # but NOT redistribution of the raw files, so keep only the files that
       # follow freesound's "ID__author__name" convention and label the source
       # exactly as the repository declares it.
       "repo": "Wh1teDuke/WilhelmSFX",
     "license": "CC0 1.0",
     "author": "Wilhelm SFX (freesound.org, CC0)",
     "category": "FX",
     "include": r"^Samples/\d{4,}__[A-Za-z0-9_.-]+__",
     "strip_freesound_id": True,
     "limit": 750},
]


def _github_json(url: str) -> dict:
    """One-shot GET: fail fast so a rate limit falls through to git."""
    req = urllib.request.Request(url, headers={
        "User-Agent": UA, "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.load(resp)


def _git_default_branch(repo: str) -> str:
    res = subprocess.run(["git", "ls-remote", "--symref",
                          f"https://github.com/{repo}", "HEAD"],
                         capture_output=True, text=True, timeout=90)
    for line in res.stdout.splitlines():
        if line.startswith("ref:") and line.endswith("HEAD"):
            return line.split()[1].rsplit("/", 1)[-1]
    return "master"


def _git_repo_files(repo: str, branch: str) -> list[list]:
    tmp = tempfile.mkdtemp(prefix="cadence_gh_")
    try:
        subprocess.run(
            ["git", "clone", "--quiet", "--depth", "1", "--filter=blob:none",
             "--no-checkout", "--branch", branch, f"https://github.com/{repo}", tmp],
            check=True, capture_output=True, timeout=600)
        res = subprocess.run(["git", "-C", tmp, "ls-tree", "-r", "--name-only", "HEAD"],
                             check=True, capture_output=True, text=True)
        return [[p, 0] for p in res.stdout.splitlines()
                if p.lower().endswith(GITHUB_AUDIO_EXTS)]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def github_repo_files(repo: str) -> tuple[str, list[list]]:
    """(branch, [[path, size]]) for a repo's audio files, cached for a week.

    The GitHub tree API needs a token to scale past ~60 calls/hour, so when it
    rate-limits we fall back to a blob-less git clone (no API limit, no sizes).
    """
    cache = f"/tmp/{repo.replace('/', '_')}_files.json"
    try:
        if os.path.exists(cache) and time.time() - os.path.getmtime(cache) < 604800:
            with open(cache, encoding="utf-8") as fh:
                data = json.load(fh)
            return data["branch"], data["files"]
    except (OSError, ValueError, KeyError):
        pass

    branch, files = None, None
    for candidate in ("master", "main"):
        try:
            tree = _github_json(
                f"https://api.github.com/repos/{repo}/git/trees/{candidate}?recursive=1")
            files = [[b["path"], int(b.get("size") or 0)] for b in tree.get("tree", [])
                     if b.get("type") == "blob"
                     and b["path"].lower().endswith(GITHUB_AUDIO_EXTS)]
            branch = candidate
            break
        except Exception:  # noqa: BLE001 - rate limit / missing branch -> try next
            continue
    if files is None:
        branch = _git_default_branch(repo)
        files = _git_repo_files(repo, branch)

    try:
        with open(cache, "w", encoding="utf-8") as fh:
            json.dump({"branch": branch, "files": files}, fh)
    except OSError:
        pass
    return branch, files


def _sample_display(parts: list[str], instrument: str | None,
                    strip_freesound_id: bool = False) -> str:
    raw = parts[-1].rsplit(".", 1)[0]
    if strip_freesound_id:
        # freesound names look like "190627__danielsilveira321__hahaha";
        # keep just the descriptive tail.
        m = re.match(r"^\d{4,}__[A-Za-z0-9_.-]+__(.+)$", raw)
        if m:
            raw = m.group(1)
    stem = re.sub(r"_+", " ", raw).strip()
    if instrument is None:
        low = [p.lower() for p in parts]
        if "samples" in low and low.index("samples") < len(parts) - 2:
            instrument = parts[low.index("samples") + 1]
        elif len(parts) >= 2:
            instrument = parts[-2]
        else:
            instrument = ""
    if instrument.lower() in ("samples", "sample"):
        instrument = ""
    if instrument and not stem.lower().startswith(instrument.lower()):
        stem = f"{instrument} {stem}"
    return re.sub(r"\s+", " ", stem)


def github_library(lib: dict) -> list[dict]:
    repo = lib["repo"]
    branch, files = github_repo_files(repo)
    raw = f"https://raw.githubusercontent.com/{repo}/{branch}/"
    families = lib.get("families")
    depth = lib.get("instrument_index", 0)

    rows = []
    for path, size in files:
        if lib.get("include") and not re.search(lib["include"], path):
            continue
        parts = path.split("/")
        if families:
            category = families.get(parts[0])
            if not category:
                continue
            instrument = parts[depth] if depth < len(parts) - 1 else parts[-2]
        else:
            category = lib["category"]
            instrument = None
        rows.append((path, int(size), category,
                     _sample_display(parts, instrument,
                                     bool(lib.get("strip_freesound_id")))))

    limit = lib.get("limit")
    if limit and len(rows) > limit:  # evenly-spaced subset across the pack
        rows.sort(key=lambda r: r[0])
        step = len(rows) / limit
        rows = [rows[int(i * step)] for i in range(limit)]

    counts: dict[str, int] = {}
    out: list[dict] = []
    for path, size, category, display in rows:
        counts[category] = counts.get(category, 0) + 1
        out.append({
            "name": display,
            "category": category,
            "url": raw + urllib.parse.quote(path),
            "license": lib["license"],
            "author": lib["author"],
            "size": size,
            "type": "oneshot",
            "duration": 0.0,
        })
    summary = ", ".join(f"{c}:{n}" for c, n in sorted(counts.items()))
    print(f"  {repo}: {summary}", file=sys.stderr)
    return out


def gather_github_libraries() -> list[dict]:
    out: list[dict] = []
    for lib in GITHUB_LIBRARIES:
        try:
            out += github_library(lib)
        except Exception as exc:  # noqa: BLE001 - never break the build
            print(f"  {lib['repo']}: skipped ({exc})", file=sys.stderr)
    return out


def gather_archive() -> list[dict]:
    """Builds entries from the archive.org packs listed in ARCHIVE_SOURCES."""
    out: list[dict] = []
    for src in ARCHIVE_SOURCES:
        meta = archive_http_json(ARCHIVE_API + src["identifier"])
        md = meta.get("metadata", {})
        lic = archive_license(md.get("licenseurl", ""))
        author = md.get("creator") or md.get("uploader") or ""
        if isinstance(author, list):
            author = ", ".join(author)
        taken = 0
        for f in meta.get("files", []):
            name = f.get("name", "")
            if f.get("source") != "original" or not name.lower().endswith(".wav"):
                continue
            if src.get("exclude") and re.search(src["exclude"], name, re.I):
                continue
            category = src.get("category", "")
            type_ = src.get("type", "")
            if "folder_routing" in src:
                category = type_ = ""
                folder = "/" + name
                for marker, (cat, typ) in src["folder_routing"].items():
                    if marker in folder:
                        category, type_ = cat, typ
                        break
                if not category:
                    continue
            elif src.get("route_percussion") and PERC_WORDS.search(name):
                category = "Percusión"
            for pattern, cat, typ in src.get("name_routing", ()):
                if re.search(pattern, name, re.I):
                    category, type_ = cat, typ
                    break

            stem = name.rsplit("/", 1)[-1][:-4]
            parent = name.rsplit("/", 2)[-2] if "/" in name else ""
            display = re.sub(r"_+", " ", stem).strip()
            if src.get("name_with_parent") and parent and parent != stem:
                display = f"{re.sub(r'_+', ' ', parent).strip()} {display}"
            if src.get("name_prefix"):
                display = f"{src['name_prefix']} {display}"

            bpm = parse_bpm(display)
            if bpm is None and src.get("bpm_prefix"):
                m = re.match(r"(\d{2,3})", stem)
                if m and 40 <= int(m.group(1)) <= 300:
                    bpm = int(m.group(1))

            entry = {
                "name": display,
                "category": category,
                "url": archive_url(src["identifier"], name),
                "license": lic or "See source",
                "author": author,
                "size": int(f.get("size") or 0),
                "type": type_ or "oneshot",
                "duration": round(parse_duration(f.get("length")), 2),
            }
            if bpm:
                entry["bpm"] = bpm
            out.append(entry)
            taken += 1
            if src.get("limit") and taken >= src["limit"]:
                break
        print(f"  archive {src['identifier']}: {taken}", file=sys.stderr)
        time.sleep(0.4)
    return out


def norm_url(url: str) -> str:
    """Commons imageinfo appends ?utm_* tracking; strip it for stable dedupe."""
    if "upload.wikimedia.org" in url:
        return url.split("?", 1)[0]
    return url


def commons_title(url: str) -> str | None:
    if "upload.wikimedia.org" not in url:
        return None
    segment = urllib.parse.unquote(norm_url(url).rsplit("/", 1)[-1])
    return "File:" + segment.replace("_", " ")


def enrich(entries: list[dict]) -> list[dict]:
    """Refresh licence/author/size/duration for Commons entries in place."""
    titles = sorted({t for e in entries if (t := commons_title(e["url"]))})
    info = fetch_info(titles)
    for e in entries:
        ii = info.get(commons_title(e["url"]) or "")
        if not ii:
            continue
        e["license"] = meta_field(ii, "LicenseShortName") or e.get("license", "CC")
        e["author"] = meta_field(ii, "Artist") or e.get("author", "")
        e["size"] = int(ii.get("size") or e.get("size", 0))
        dur = float(ii.get("duration") or 0.0)
        if dur:
            e["duration"] = round(dur, 2)
    return entries


def build(existing: list[dict],
          discoveries: list[tuple[str, str, str | None, dict]],
          archive_entries: list[dict] | None = None) -> list[dict]:
    existing = enrich([dict(e) for e in existing])
    configured = {s["identifier"].lower() for s in ARCHIVE_SOURCES}

    def from_configured_archive(url: str) -> bool:
        """Archive/GitHub packs are rebuilt every run, so their old copies are stale."""
        if any(url.startswith(f"https://raw.githubusercontent.com/{lib['repo']}/")
               for lib in GITHUB_LIBRARIES):
            return True
        if "archive.org/download/" not in url:
            return False
        item = url.split("archive.org/download/", 1)[1].split("/", 1)[0]
        return item.lower() in configured

    by_url: dict[str, dict] = {}
    entries: list[dict] = []
    for e in existing:  # dedupe by url; also drop known scraping noise
        e["url"] = norm_url(e["url"])
        if e["url"] in by_url or NOISE.search(e["name"]):
            continue
        if from_configured_archive(e["url"]):
            continue
        # type/bpm are derived: drop stale values so they are recomputed below.
        e.pop("type", None)
        e.pop("bpm", None)
        by_url[e["url"]] = e
        entries.append(e)

    for title, dest, hint, ii in discoveries:
        e = entry_from(title, ii, dest, hint)
        if e is None or e["url"] in by_url:
            continue
        by_url[e["url"]] = e
        entries.append(e)

    for e in archive_entries or []:  # archive.org packs, already shaped
        if e["url"] in by_url:
            continue
        by_url[e["url"]] = e
        entries.append(e)

    # Fill type / bpm / duration without clobbering values already set.
    for e in entries:
        e["duration"] = round(float(e.get("duration") or 0.0), 2)
        if e.get("type") not in ("loop", "oneshot"):
            if LOOP_WORDS.search(e["name"]) or e["category"] in ("Loops", "Bases"):
                e["type"] = "loop"
            elif e["duration"] > ONESHOT_HARD_MAX_SECONDS:
                e["type"] = "loop"
            else:
                e["type"] = "oneshot"
        bpm = parse_bpm(e["name"])
        if bpm:
            e["bpm"] = bpm

    # Preserve existing ids; only renumber duplicates / missing ones.
    used: set[str] = set()
    nums = [int(e["id"].split("-")[1]) for e in entries
            if re.match(r"snd-\d+$", str(e.get("id", "")))]
    nxt = (max(nums) + 1) if nums else 0
    for e in entries:
        eid = e.get("id")
        if not eid or eid in used:
            while f"snd-{nxt:03d}" in used:
                nxt += 1
            eid = f"snd-{nxt:03d}"
            nxt += 1
        e["id"] = eid
        used.add(eid)

    # Stable sort: category, then loops (bases) first, then name.
    entries.sort(key=lambda e: (e["category"], 0 if e["type"] == "loop" else 1,
                                e["name"].lower()))
    return entries


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    existing = load_existing()
    print(f"existing entries: {len(existing)}", file=sys.stderr)
    discoveries = gather_discoveries()
    print(f"discoveries: {len(discoveries)}", file=sys.stderr)
    archive_entries = gather_archive()
    archive_entries += gather_github_libraries()
    print(f"archive entries: {len(archive_entries)}", file=sys.stderr)
    entries = build(existing, discoveries, archive_entries)

    cats: dict[str, int] = {}
    types: dict[str, int] = {}
    for e in entries:
        cats[e["category"]] = cats.get(e["category"], 0) + 1
        types[e["type"]] = types.get(e["type"], 0) + 1
    print(f"total: {len(entries)}  categories={cats}", file=sys.stderr)
    print(f"types: {types}", file=sys.stderr)

    if args.dry_run:
        new = [e for e in entries if e["url"] not in {x["url"] for x in existing}]
        print("\n-- new entries --")
        for e in new:
            print(f"  [{e['category']}/{e['type']}] {e['name']}"
                  f"{' @'+str(e['bpm'])+'BPM' if e.get('bpm') else ''}")
        return 0

    with open(CATALOG, "w", encoding="utf-8") as fh:
        json.dump(entries, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    print(f"wrote {CATALOG}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
