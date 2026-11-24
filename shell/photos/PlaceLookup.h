#pragma once

#include <QString>

#include <vector>

// Names where a photo was taken, finer than Immich does: the nearest populated place from a
// local list (places.tsv, written by make-places.py), with the town or city it belongs to.
// Nothing leaves the machine. Why: docs/gabecube-findings.md, "Place names".
class PlaceLookup
{
public:
    // Reads places.tsv: latitude, longitude, name, city, country per line, tab-separated,
    // `#` lines ignored. A missing file gives an empty list.
    static PlaceLookup load(const QString &path);

    qsizetype size() const { return qsizetype(m_places.size()); }

    // "District, City" for the nearest place, or "Town, Country" when it is a town of its own.
    // Only places in `country` (as Immich names it) count, so a photo just across a border
    // isn't named after a village on this side; an empty `country` allows any.
    // Empty when the list has nothing within 25 km (Immich's own limit).
    QString name(double latitude, double longitude, const QString &country) const;

private:
    struct Place
    {
        double latitude;
        double longitude;
        QString name;
        QString city;
        QString country;
    };
    // Sorted by latitude.
    std::vector<Place> m_places;
};
