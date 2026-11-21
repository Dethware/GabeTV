#!/usr/bin/env python3
"""Build the place list the shell uses to name where a photo was taken (src/PlaceLookup.*).

Immich names a photo after the nearest GeoNames place with 500+ people, leaving out the
districts of cities, which is too coarse (docs/gabecube-findings.md, "Place names"). This
writes every populated place of the given countries, each with the town or city it belongs
to, from the GeoNames country dumps (https://download.geonames.org/export/dump/, CC BY 4.0).

Usage: ./make-places.py [-o build/places.tsv] [SE CZ ...]

Output, one place per line, tab-separated: latitude, longitude, name, city, country.
`city` is empty when the place is the town itself or the town is unknown; `country` is the
name Immich uses, so the shell can tell which of its labels a place may replace.
Downloads are kept in build/geonames/; delete a file there to fetch it again.
"""

import argparse
import collections
import io
import re
import sys
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DUMP_URL = "https://download.geonames.org/export/dump/{code}.zip"

# Feature codes (class P) that aren't places people are in today: historical, abandoned,
# destroyed, and the historical capital.
SKIPPED_CODES = {"PPLH", "PPLQ", "PPLW", "PPLCH"}
SEAT_CODES = {"PPLC", "PPLA", "PPLA2"}

Row = collections.namedtuple("Row", "name lat lon fclass code admin1 admin2 admin3")


def read_dump(code: str, cache: Path) -> list[Row]:
    archive = cache / f"{code}.zip"
    if not archive.exists():
        cache.mkdir(parents=True, exist_ok=True)
        print(f"downloading {DUMP_URL.format(code=code)}", file=sys.stderr)
        request = urllib.request.Request(DUMP_URL.format(code=code), headers={"User-Agent": "make-places"})
        with urllib.request.urlopen(request, timeout=120) as response:
            archive.write_bytes(response.read())
    rows = []
    with zipfile.ZipFile(archive) as z, z.open(f"{code}.txt") as f:
        for line in io.TextIOWrapper(f, encoding="utf-8"):
            c = line.rstrip("\n").split("\t")
            rows.append(Row(c[1], float(c[4]), float(c[5]), c[6], c[7], c[10], c[11], c[12]))
    return rows


def sweden_cities(rows: list[Row]):
    """Municipality (kommun) = admin2. Its record is named "Stockholms Kommun": drop
    "kommun" and the genitive s, which follows a consonant ("Marks" -> "Mark") unless the
    name is a place's as it stands ("Hällefors"; "Sotenäs" has a vowel before the s).
    "Falu" is the one name built differently, so a seat called nearly the same wins ("Falun")."""
    names_in = collections.defaultdict(set)
    seat = {}
    for r in rows:
        if r.fclass == "P" and r.admin2:
            names_in[r.admin1, r.admin2].add(r.name)
            if r.code in SEAT_CODES:
                seat[r.admin1, r.admin2] = r.name
    city = {}
    for r in rows:
        if r.code != "ADM2":
            continue
        base = re.sub(r"\s+(kommun|stad)$", "", r.name, flags=re.IGNORECASE)
        key = (r.admin1, r.admin2)
        if base not in names_in[key]:
            if re.search(r"[^aeiouyåäö]s$", base):
                base = base[:-1]
            elif seat.get(key, "").startswith(base) and len(seat[key]) == len(base) + 1:
                base = seat[key]
        city[key] = base
    return lambda r: city.get((r.admin1, r.admin2), "")


def czechia_cities(rows: list[Row]):
    """Municipality (obec) = admin3, whose record carries the plain name ("Brno").
    Prague is a region of its own (admin1 52) split into Praha 1..22."""
    capital = next(r.name for r in rows if r.code == "PPLC")
    city = {r.admin3: re.sub(r"\s*\(.*\)$", "", r.name) for r in rows if r.code == "ADM3" and r.admin3}
    return lambda r: capital if r.admin1 == "52" else city.get(r.admin3, "")


# Country code -> (the country's name in Immich, how to find a place's city).
# The administrative levels mean different things per country, so each needs its own rule.
COUNTRIES = {
    "SE": ("Sweden", sweden_cities),
    "CZ": ("Czech Republic", czechia_cities),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("countries", nargs="*", default=["SE", "CZ"], metavar="CC")
    parser.add_argument("-o", "--output", type=Path, default=HERE / "build" / "places.tsv")
    parser.add_argument("--cache", type=Path, default=HERE / "build" / "geonames")
    args = parser.parse_args()

    lines = []
    for code in args.countries:
        if code not in COUNTRIES:
            parser.error(f"no rule for {code} yet (known: {', '.join(COUNTRIES)}); add one to COUNTRIES")
        country, rule = COUNTRIES[code]
        rows = read_dump(code, args.cache)
        city_of = rule(rows)
        count = with_city = 0
        for r in rows:
            if r.fclass != "P" or r.code in SKIPPED_CODES or not r.name:
                continue
            city = city_of(r)
            if city == r.name:
                city = ""
            count += 1
            with_city += bool(city)
            lines.append(f"{r.lat:.5f}\t{r.lon:.5f}\t{r.name}\t{city}\t{country}\n")
        print(f"{code}: {count} places, {with_city} inside a named town or city", file=sys.stderr)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as out:
        out.write("# Places from GeoNames (https://www.geonames.org, CC BY 4.0); made by make-places.py\n")
        out.writelines(lines)
    print(f"wrote {args.output} ({len(lines)} places)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
