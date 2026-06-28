#ifndef A_CITYDB_H
#define A_CITYDB_H

#include <QDateTime>
#include <QString>
#include <QVector>

namespace A
{

/// A single record from GeoNames cities15000.txt.
struct CityRec {
    QString name;        ///< Display name (UTF-8)
    QString admin1Code;  ///< First-level admin code (state/province/region)
    QString countryCode; ///< ISO 3166-1 alpha-2, e.g. "FR"
    QString timezoneId;  ///< IANA timezone ID, e.g. "Europe/Paris"
    float   latitude;    ///< degrees N (+) / S (-)
    float   longitude;   ///< degrees E (+) / W (-)
    int     population;  ///< from GeoNames col 15
};

/// Population tiers — bitmask.  All tiers off behaves the same as all on.
enum CityPopTier : unsigned {
    CityPop_Small  = 0x01, ///< 15k – 100k
    CityPop_Medium = 0x02, ///< 100k – 500k
    CityPop_Large  = 0x04, ///< 500k – 2M
    CityPop_Huge   = 0x08, ///< >2M
    CityPop_All    = 0x0F
};

/// Continent bitmask.
enum CityContinent : unsigned {
    CityCont_Africa       = 0x01,
    CityCont_Asia         = 0x02,
    CityCont_Europe       = 0x04,
    CityCont_NorthAmerica = 0x08,
    CityCont_SouthAmerica = 0x10,
    CityCont_Oceania      = 0x20,
    CityCont_Antarctica   = 0x40,
    CityCont_All          = 0x7F
};

/// Filter for citiesNearLatitude.
struct CityFilter {
    unsigned popTiers        = CityPop_All;
    unsigned continents      = CityCont_All;
    /// Number of equal-width longitude buckets used for geographic
    /// diversification.  0 disables diversification (pure pop sort).
    int      longitudeBuckets = 12;
};

/// Cities whose latitude is within ±tolDeg of targetLatDeg.  Filtered by
/// the pop-tier / continent mask.  If `longitudeBuckets > 0`, results are
/// diversified across longitude (only the top-population city per bucket
/// survives) before final sort by population.  Capped at maxResults.
QVector<CityRec>
citiesNearLatitude(double            targetLatDeg,
                   double            tolDeg,
                   int               maxResults,
                   const CityFilter& filter = {});

/// Find cities by name. Results are ranked by match quality first, then by
/// population descending. Matching is case-insensitive and diacritic-insensitive.
QVector<CityRec>
citiesMatchingName(const QString& query, int maxResults = 10);

/// Format a city as a disambiguating display label, preferring
/// "City, State/Province, Country" when admin metadata is available.
QString
cityDisplayName(const CityRec& city);

/// Force-load the DB.  Returns false if the data file is missing.
bool
loadCityDb();

/// Resolve the city's UTC offset in hours at the given timestamp.
/// Returns false if the timezone ID is missing or invalid.
bool
cityTimezoneOffset(const CityRec& city, const QDateTime& when, double& offsetHours);

/// Continent of a 2-letter ISO country code, or 0 if unknown.
unsigned
continentOf(const QString& iso2);

/// Population tier of a population value.
unsigned
popTierOf(int population);

} // namespace A

#endif // A_CITYDB_H
