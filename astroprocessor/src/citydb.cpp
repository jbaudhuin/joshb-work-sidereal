#include "citydb.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <algorithm>
#include <vector>

namespace A
{

namespace
{
QMutex                 g_mutex;
bool                   g_loaded   = false;
bool                   g_loadFailed = false;
std::vector<CityRec>   g_db; // sorted by latitude ascending

QString
locateCityFile()
{
    // Match the install layout: bin/data/cities15000.txt sits alongside the executable.
    QStringList candidates {
        QCoreApplication::applicationDirPath() + QStringLiteral("/data/cities15000.txt"),
        QDir::currentPath() + QStringLiteral("/data/cities15000.txt"),
        QDir::currentPath() + QStringLiteral("/bin/data/cities15000.txt"),
    };
    for (const QString& path : candidates) {
        if (QFile::exists(path)) return path;
    }
    return QString();
}

void
loadLocked()
{
    if (g_loaded || g_loadFailed) return;

    const QString path = locateCityFile();
    if (path.isEmpty()) { g_loadFailed = true; return; }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { g_loadFailed = true; return; }

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    g_db.reserve(35000);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty()) continue;
        const QStringList cols = line.split(QLatin1Char('\t'));
        // GeoNames columns (1-indexed): 1 geonameid, 2 name, 3 asciiname,
        // 4 alternatenames, 5 latitude, 6 longitude, 7 feature class,
        // 8 feature code, 9 country code, ..., 15 population.
        if (cols.size() < 15) continue;

        CityRec r;
        r.name        = cols[1];
        r.latitude    = cols[4].toFloat();
        r.longitude   = cols[5].toFloat();
        r.countryCode = cols[8];
        r.population  = cols[14].toInt();
        g_db.push_back(std::move(r));
    }
    std::sort(g_db.begin(), g_db.end(),
              [](const CityRec& a, const CityRec& b) {
                  return a.latitude < b.latitude;
              });
    g_loaded = true;
}

// ISO 3166-1 alpha-2 → CityContinent.  Sourced from GeoNames countryInfo.txt.
const QHash<QString, unsigned>&
countryToContinent()
{
    static const QHash<QString, unsigned> map = {
        {"AD", CityCont_Europe},
        {"AE", CityCont_Asia},
        {"AF", CityCont_Asia},
        {"AG", CityCont_NorthAmerica},
        {"AI", CityCont_NorthAmerica},
        {"AL", CityCont_Europe},
        {"AM", CityCont_Asia},
        {"AO", CityCont_Africa},
        {"AQ", CityCont_Antarctica},
        {"AR", CityCont_SouthAmerica},
        {"AS", CityCont_Oceania},
        {"AT", CityCont_Europe},
        {"AU", CityCont_Oceania},
        {"AW", CityCont_NorthAmerica},
        {"AX", CityCont_Europe},
        {"AZ", CityCont_Asia},
        {"BA", CityCont_Europe},
        {"BB", CityCont_NorthAmerica},
        {"BD", CityCont_Asia},
        {"BE", CityCont_Europe},
        {"BF", CityCont_Africa},
        {"BG", CityCont_Europe},
        {"BH", CityCont_Asia},
        {"BI", CityCont_Africa},
        {"BJ", CityCont_Africa},
        {"BL", CityCont_NorthAmerica},
        {"BM", CityCont_NorthAmerica},
        {"BN", CityCont_Asia},
        {"BO", CityCont_SouthAmerica},
        {"BQ", CityCont_NorthAmerica},
        {"BR", CityCont_SouthAmerica},
        {"BS", CityCont_NorthAmerica},
        {"BT", CityCont_Asia},
        {"BV", CityCont_Antarctica},
        {"BW", CityCont_Africa},
        {"BY", CityCont_Europe},
        {"BZ", CityCont_NorthAmerica},
        {"CA", CityCont_NorthAmerica},
        {"CC", CityCont_Asia},
        {"CD", CityCont_Africa},
        {"CF", CityCont_Africa},
        {"CG", CityCont_Africa},
        {"CH", CityCont_Europe},
        {"CI", CityCont_Africa},
        {"CK", CityCont_Oceania},
        {"CL", CityCont_SouthAmerica},
        {"CM", CityCont_Africa},
        {"CN", CityCont_Asia},
        {"CO", CityCont_SouthAmerica},
        {"CR", CityCont_NorthAmerica},
        {"CU", CityCont_NorthAmerica},
        {"CV", CityCont_Africa},
        {"CW", CityCont_NorthAmerica},
        {"CX", CityCont_Oceania},
        {"CY", CityCont_Europe},
        {"CZ", CityCont_Europe},
        {"DE", CityCont_Europe},
        {"DJ", CityCont_Africa},
        {"DK", CityCont_Europe},
        {"DM", CityCont_NorthAmerica},
        {"DO", CityCont_NorthAmerica},
        {"DZ", CityCont_Africa},
        {"EC", CityCont_SouthAmerica},
        {"EE", CityCont_Europe},
        {"EG", CityCont_Africa},
        {"EH", CityCont_Africa},
        {"ER", CityCont_Africa},
        {"ES", CityCont_Europe},
        {"ET", CityCont_Africa},
        {"FI", CityCont_Europe},
        {"FJ", CityCont_Oceania},
        {"FK", CityCont_SouthAmerica},
        {"FM", CityCont_Oceania},
        {"FO", CityCont_Europe},
        {"FR", CityCont_Europe},
        {"GA", CityCont_Africa},
        {"GB", CityCont_Europe},
        {"GD", CityCont_NorthAmerica},
        {"GE", CityCont_Asia},
        {"GF", CityCont_SouthAmerica},
        {"GG", CityCont_Europe},
        {"GH", CityCont_Africa},
        {"GI", CityCont_Europe},
        {"GL", CityCont_NorthAmerica},
        {"GM", CityCont_Africa},
        {"GN", CityCont_Africa},
        {"GP", CityCont_NorthAmerica},
        {"GQ", CityCont_Africa},
        {"GR", CityCont_Europe},
        {"GS", CityCont_Antarctica},
        {"GT", CityCont_NorthAmerica},
        {"GU", CityCont_Oceania},
        {"GW", CityCont_Africa},
        {"GY", CityCont_SouthAmerica},
        {"HK", CityCont_Asia},
        {"HM", CityCont_Antarctica},
        {"HN", CityCont_NorthAmerica},
        {"HR", CityCont_Europe},
        {"HT", CityCont_NorthAmerica},
        {"HU", CityCont_Europe},
        {"ID", CityCont_Asia},
        {"IE", CityCont_Europe},
        {"IL", CityCont_Asia},
        {"IM", CityCont_Europe},
        {"IN", CityCont_Asia},
        {"IO", CityCont_Asia},
        {"IQ", CityCont_Asia},
        {"IR", CityCont_Asia},
        {"IS", CityCont_Europe},
        {"IT", CityCont_Europe},
        {"JE", CityCont_Europe},
        {"JM", CityCont_NorthAmerica},
        {"JO", CityCont_Asia},
        {"JP", CityCont_Asia},
        {"KE", CityCont_Africa},
        {"KG", CityCont_Asia},
        {"KH", CityCont_Asia},
        {"KI", CityCont_Oceania},
        {"KM", CityCont_Africa},
        {"KN", CityCont_NorthAmerica},
        {"KP", CityCont_Asia},
        {"KR", CityCont_Asia},
        {"XK", CityCont_Europe},
        {"KW", CityCont_Asia},
        {"KY", CityCont_NorthAmerica},
        {"KZ", CityCont_Asia},
        {"LA", CityCont_Asia},
        {"LB", CityCont_Asia},
        {"LC", CityCont_NorthAmerica},
        {"LI", CityCont_Europe},
        {"LK", CityCont_Asia},
        {"LR", CityCont_Africa},
        {"LS", CityCont_Africa},
        {"LT", CityCont_Europe},
        {"LU", CityCont_Europe},
        {"LV", CityCont_Europe},
        {"LY", CityCont_Africa},
        {"MA", CityCont_Africa},
        {"MC", CityCont_Europe},
        {"MD", CityCont_Europe},
        {"ME", CityCont_Europe},
        {"MF", CityCont_NorthAmerica},
        {"MG", CityCont_Africa},
        {"MH", CityCont_Oceania},
        {"MK", CityCont_Europe},
        {"ML", CityCont_Africa},
        {"MM", CityCont_Asia},
        {"MN", CityCont_Asia},
        {"MO", CityCont_Asia},
        {"MP", CityCont_Oceania},
        {"MQ", CityCont_NorthAmerica},
        {"MR", CityCont_Africa},
        {"MS", CityCont_NorthAmerica},
        {"MT", CityCont_Europe},
        {"MU", CityCont_Africa},
        {"MV", CityCont_Asia},
        {"MW", CityCont_Africa},
        {"MX", CityCont_NorthAmerica},
        {"MY", CityCont_Asia},
        {"MZ", CityCont_Africa},
        {"NA", CityCont_Africa},
        {"NC", CityCont_Oceania},
        {"NE", CityCont_Africa},
        {"NF", CityCont_Oceania},
        {"NG", CityCont_Africa},
        {"NI", CityCont_NorthAmerica},
        {"NL", CityCont_Europe},
        {"NO", CityCont_Europe},
        {"NP", CityCont_Asia},
        {"NR", CityCont_Oceania},
        {"NU", CityCont_Oceania},
        {"NZ", CityCont_Oceania},
        {"OM", CityCont_Asia},
        {"PA", CityCont_NorthAmerica},
        {"PE", CityCont_SouthAmerica},
        {"PF", CityCont_Oceania},
        {"PG", CityCont_Oceania},
        {"PH", CityCont_Asia},
        {"PK", CityCont_Asia},
        {"PL", CityCont_Europe},
        {"PM", CityCont_NorthAmerica},
        {"PN", CityCont_Oceania},
        {"PR", CityCont_NorthAmerica},
        {"PS", CityCont_Asia},
        {"PT", CityCont_Europe},
        {"PW", CityCont_Oceania},
        {"PY", CityCont_SouthAmerica},
        {"QA", CityCont_Asia},
        {"RE", CityCont_Africa},
        {"RO", CityCont_Europe},
        {"RS", CityCont_Europe},
        {"RU", CityCont_Europe},
        {"RW", CityCont_Africa},
        {"SA", CityCont_Asia},
        {"SB", CityCont_Oceania},
        {"SC", CityCont_Africa},
        {"SD", CityCont_Africa},
        {"SS", CityCont_Africa},
        {"SE", CityCont_Europe},
        {"SG", CityCont_Asia},
        {"SH", CityCont_Africa},
        {"SI", CityCont_Europe},
        {"SJ", CityCont_Europe},
        {"SK", CityCont_Europe},
        {"SL", CityCont_Africa},
        {"SM", CityCont_Europe},
        {"SN", CityCont_Africa},
        {"SO", CityCont_Africa},
        {"SR", CityCont_SouthAmerica},
        {"ST", CityCont_Africa},
        {"SV", CityCont_NorthAmerica},
        {"SX", CityCont_NorthAmerica},
        {"SY", CityCont_Asia},
        {"SZ", CityCont_Africa},
        {"TC", CityCont_NorthAmerica},
        {"TD", CityCont_Africa},
        {"TF", CityCont_Antarctica},
        {"TG", CityCont_Africa},
        {"TH", CityCont_Asia},
        {"TJ", CityCont_Asia},
        {"TK", CityCont_Oceania},
        {"TL", CityCont_Oceania},
        {"TM", CityCont_Asia},
        {"TN", CityCont_Africa},
        {"TO", CityCont_Oceania},
        {"TR", CityCont_Asia},
        {"TT", CityCont_NorthAmerica},
        {"TV", CityCont_Oceania},
        {"TW", CityCont_Asia},
        {"TZ", CityCont_Africa},
        {"UA", CityCont_Europe},
        {"UG", CityCont_Africa},
        {"UM", CityCont_Oceania},
        {"US", CityCont_NorthAmerica},
        {"UY", CityCont_SouthAmerica},
        {"UZ", CityCont_Asia},
        {"VA", CityCont_Europe},
        {"VC", CityCont_NorthAmerica},
        {"VE", CityCont_SouthAmerica},
        {"VG", CityCont_NorthAmerica},
        {"VI", CityCont_NorthAmerica},
        {"VN", CityCont_Asia},
        {"VU", CityCont_Oceania},
        {"WF", CityCont_Oceania},
        {"WS", CityCont_Oceania},
        {"YE", CityCont_Asia},
        {"YT", CityCont_Africa},
        {"ZA", CityCont_Africa},
        {"ZM", CityCont_Africa},
        {"ZW", CityCont_Africa},
        {"CS", CityCont_Europe},
        {"AN", CityCont_NorthAmerica},
    };
    return map;
}

} // namespace

unsigned
continentOf(const QString& iso2)
{
    const auto& map = countryToContinent();
    return map.value(iso2, 0u);
}

unsigned
popTierOf(int pop)
{
    if (pop < 100000)  return CityPop_Small;
    if (pop < 500000)  return CityPop_Medium;
    if (pop < 2000000) return CityPop_Large;
    return CityPop_Huge;
}

bool
loadCityDb()
{
    QMutexLocker lock(&g_mutex);
    loadLocked();
    return g_loaded;
}

QVector<CityRec>
citiesNearLatitude(double            targetLatDeg,
                   double            tolDeg,
                   int               maxResults,
                   const CityFilter& filter)
{
    QMutexLocker lock(&g_mutex);
    loadLocked();
    if (!g_loaded) return {};

    // Treat empty masks as "all" so toggling everything off doesn't yield zero results.
    const unsigned popMask = filter.popTiers   ? filter.popTiers   : unsigned(CityPop_All);
    const unsigned conMask = filter.continents ? filter.continents : unsigned(CityCont_All);

    const float lo = static_cast<float>(targetLatDeg - tolDeg);
    const float hi = static_cast<float>(targetLatDeg + tolDeg);

    auto first = std::lower_bound(g_db.begin(), g_db.end(), lo,
                                  [](const CityRec& r, float v) {
                                      return r.latitude < v;
                                  });

    QVector<CityRec> hits;
    for (auto it = first; it != g_db.end() && it->latitude <= hi; ++it) {
        if (!(popTierOf(it->population) & popMask)) continue;
        const unsigned cont = continentOf(it->countryCode);
        if (cont == 0 || !(cont & conMask)) continue;
        hits.append(*it);
    }

    // Longitude-bucket diversification.  Keep the top-pop city per bucket.
    if (filter.longitudeBuckets > 0 && hits.size() > 1) {
        const int    nBuckets   = filter.longitudeBuckets;
        const double bucketWide = 360.0 / nBuckets;
        QHash<int, int> bestIdxByBucket; // bucket → index into hits
        for (int i = 0; i < hits.size(); ++i) {
            const double lon = hits[i].longitude + 180.0; // 0..360
            int bucket = int(lon / bucketWide);
            if (bucket < 0)         bucket = 0;
            if (bucket >= nBuckets) bucket = nBuckets - 1;
            auto it = bestIdxByBucket.find(bucket);
            if (it == bestIdxByBucket.end()
                || hits[i].population > hits[*it].population)
            {
                bestIdxByBucket.insert(bucket, i);
            }
        }
        QVector<CityRec> diversified;
        diversified.reserve(bestIdxByBucket.size());
        for (int idx : std::as_const(bestIdxByBucket)) diversified.append(hits[idx]);
        hits = std::move(diversified);
    }

    std::sort(hits.begin(), hits.end(),
              [](const CityRec& a, const CityRec& b) {
                  return a.population > b.population;
              });
    if (maxResults > 0 && hits.size() > maxResults) hits.resize(maxResults);
    return hits;
}

} // namespace A
