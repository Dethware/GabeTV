#include "PlaceLookup.h"

#include <QFile>
#include <QList>
#include <QSet>
#include <QStringView>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr double kKmPerDegree = 6371.0 * std::numbers::pi / 180.0;
constexpr double kMaxDistanceKm = 25.0;

} // namespace

PlaceLookup PlaceLookup::load(const QString &path)
{
    PlaceLookup lookup;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return lookup;
    const QString text = QString::fromUtf8(file.readAll());
    // Thousands of places share a city and a country: keep one copy of each string.
    QSet<QString> shared;
    const auto share = [&shared](QStringView s) { return *shared.insert(s.toString()); };
    for (QStringView line : QStringView(text).split(u'\n')) {
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const QList<QStringView> fields = line.split(u'\t');
        if (fields.size() < 5)
            continue;
        bool latitudeOk = false;
        bool longitudeOk = false;
        Place place{fields[0].toDouble(&latitudeOk), fields[1].toDouble(&longitudeOk),
                    fields[2].toString(), share(fields[3]), share(fields[4])};
        if (latitudeOk && longitudeOk && !place.name.isEmpty())
            lookup.m_places.push_back(std::move(place));
    }
    std::sort(lookup.m_places.begin(), lookup.m_places.end(),
              [](const Place &a, const Place &b) { return a.latitude < b.latitude; });
    return lookup;
}

QString PlaceLookup::name(double latitude, double longitude, const QString &country) const
{
    // Walk outwards from the photo's latitude, north and south in turn; a direction is done
    // once its latitude alone is farther away than the best place so far.
    const double kmPerLongitudeDegree = kKmPerDegree * std::cos(latitude * std::numbers::pi / 180.0);
    const Place *best = nullptr;
    double bestKm = kMaxDistanceKm;
    const auto consider = [&](const Place &place) {
        const double northKm = (place.latitude - latitude) * kKmPerDegree;
        if (std::abs(northKm) >= bestKm)
            return false;
        if (country.isEmpty() || place.country == country) {
            const double km = std::hypot(northKm, (place.longitude - longitude) * kmPerLongitudeDegree);
            if (km < bestKm) {
                bestKm = km;
                best = &place;
            }
        }
        return true;
    };
    const auto start = std::lower_bound(m_places.begin(), m_places.end(), latitude,
                                        [](const Place &place, double lat) { return place.latitude < lat; });
    for (auto north = start; north != m_places.end() && consider(*north); ++north) { }
    for (auto south = start; south != m_places.begin() && consider(*(south - 1)); --south) { }

    if (!best)
        return {};
    return best->name + QStringLiteral(", ") + (best->city.isEmpty() ? best->country : best->city);
}
