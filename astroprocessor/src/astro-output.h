#ifndef A_OUTPUT_H
#define A_OUTPUT_H

#include "astro-data.h"
#include "astro-gui.h"

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0)) && defined(_MSC_VER)
#define A_DECODE(s) QString::fromLocal8Bit(s)
#else
#define A_DECODE(s) QString::fromUtf8(s)
#endif

namespace A
{

enum PlanetsOrder { Order_NoOrder, Order_Power, Order_House, Order_Element };

enum Article {
    Article_All           = 0xFF,
    Article_Input         = 0x1,
    Article_Houses        = 0x2,
    Article_Aspects       = 0x4,
    Article_Planet        = 0x8,
    Article_Power         = 0x10,
    Article_Parans        = 0x20,
    Article_DiurnalEvents = 0x40,
    Article_FixedStars    = 0x80,
    Article_Speculum      = 0x100,
    Article_PSSR          = 0x200
};

enum AnglePrecision { LowPrecision, NormalPrecision, HighPrecision };

Q_DECLARE_FLAGS(Articles, Article)

QString
romanNum(int num);
QString
houseTag(int num);
QString
houseNum(const Planet& planet);
QString
getPositionName(PlanetPosition p);
QString
degreeToString(float deg, AnglePrecision precision = NormalPrecision);
QString
formatLatitude(float latitude, AnglePrecision precision = HighPrecision);
QString
formatLongitude(float longitude, AnglePrecision precision = HighPrecision);
QString
raToString(double raDegrees, AnglePrecision precision = NormalPrecision);
QString
siderealTimeToString(double         raDegrees,
                     AnglePrecision precision = NormalPrecision);
QString
zodiacPosition(float          deg,
               const Zodiac&  zodiac,
               AnglePrecision precision = NormalPrecision,
               bool           isRetro   = false);
QString
zodiacPosition(const Star&    planet,
               const Zodiac&  zodiac,
               AnglePrecision precision = NormalPrecision);
void
sortPlanets(PlanetList& planets, PlanetsOrder order);

QString
describeInput(const InputData& data);
QString
describeHouses(const Houses& houses, const Zodiac& zodiac);
QString
describeHouses(const Houses&    houses,
               const Zodiac&    zodiac,
               const PlanetMap& planets);
enum AspectSortOrder {
    SortByPlanets =
        0, // planet names, then by orb strength - default (original)
    SortByOrbStrength, // orb/maxOrb (tightest first)
    SortByAspectType   // aspect type, then by orb strength
};

QString
describeAspect(const Aspect& aspect, bool monospace = false);
QString
describeAspectsTable(const AspectList& aspects,
                     AspectSortOrder   sortOrder = SortByPlanets);
QString
describeAspectFull(const Aspect& asp, QString tag1 = "", QString tag2 = "");
QString
describePlanet(const Planet& planet, const Zodiac& zodiac);
QString
describePlanetCoord(const Planet& planet);
QString
describePlanetCoordInHtml(const Planet& planet);
QString
describePower(const Planet& planet, const Horoscope& scope);
QString
describePowerInHtml(const Planet& planet, const Horoscope& scope);
QString
describeParans(const AstroFileList& scopes,
               bool                 showAll,
               bool                 showFixedStars,
               double               paranOrb,
               SpeculumDisplayMode  displayMode = DisplayLocalTime);
QString
describeSpeculum(const Horoscope&    scope,
                 bool                showFixedStars,
                 SpeculumDisplayMode displayMode = DisplayLocalTime);
QString
describePSSR(const AstroFileList& scopes,
             double               paranOrb,
             int                  daysRange       = 365,
             SpeculumDisplayMode  displayMode     = DisplayLocalTime,
             bool                 useMeanSun      = true);
QString
_formatTime(const QDateTime& dt, short tz);
QString
describe(AstroFileList&& scopes,
         Articles        article  = Article_All,
         double          paranOrb = 1.0);

} // namespace A

#endif // A_OUTPUT_H
