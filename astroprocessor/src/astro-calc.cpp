#include "astro-data.h"
#include <QDebug>
#include <QtConcurrent/QtConcurrent>

#undef MSDOS // undef macros made by SWE library
#undef UCHAR
#undef forward

#define min min

#include <math.h>
#include <tuple>

#include <boost/math/tools/minima.hpp>
#include <boost/math/tools/roots.hpp>

#include "astro-calc.h"
#include "astro-gui.h"
#include "astro-output.h"
#include "fileeditor.h"

#include <sweodef.h> // the swe.h files are a little squirrely about include order
#include <swehouse.h>
#include <swephexp.h>

// Set to 1 to enable pseudo-finder debug mode (runs for 15 sec, no actual work)
#define DEBUG_FINDER_THREADS 0

using namespace boost::math::tools;

namespace A
{

static QString angleDesc[] = { "asc", "desc", "mc", "ic" };

QString
dtToString(const QDateTime& dt)
{
    return dt.toString("yyyy-MM-dd hh:mm:ss.zzz");
}

template <typename T>
inline constexpr int
sgn(T x, std::false_type)
{
    return T(0) < x;
}

template <typename T>
inline constexpr int
sgn(T x, std::true_type)
{
    return (T(0) < x) - (x < T(0));
}

template <typename T>
inline constexpr int
sgn(T x)
{
    return sgn(x, std::is_signed<T>());
}

aspectModeType aspectMode { amcEcliptic };

/*static*/
const aspectModeType&
aspectModeType::current()
{
    return aspectMode;
}

PrimDirMode primDirMode = prdMundane;

double
getJulianDate(QDateTime GMT, bool ephemerisTime /*=false*/)
{
    char        serr[256];
    double      ret[2];
    const auto& date(GMT.date());
    const auto& time(GMT.time());
    swe_utc_to_jd(date.year(),
                  date.month(),
                  date.day(),
                  time.hour(),
                  time.minute(),
                  double(time.second()) + (time.msec() / 1000.),
                  1 /*gregorian*/,
                  ret,
                  serr);
    return ret[ephemerisTime ? 0 : 1]; // ET or UT
}

double
getUTfromET(double et)
{
    int32  iyear, imonth, iday, ihour, imin;
    double dsec;
    swe_jdut1_to_utc(et,
                     1 /*greg*/,
                     &iyear,
                     &imonth,
                     &iday,
                     &ihour,
                     &imin,
                     &dsec);

    double ret[2];
    char   serr[256];
    swe_utc_to_jd(iyear,
                  imonth,
                  iday,
                  ihour,
                  imin,
                  dsec,
                  1 /*greg*/,
                  ret,
                  serr);

    return ret[1];
}

unsigned
windowOf(PlanetLoc* pl)
{
    switch (pl->planet.planetId()) {
    case Planet_Mercury: return 45;
    case Planet_Venus:   return 90;
    case Planet_Mars:    return 210;
    default:             return 360;
    }
};

inline qreal
harmonic(double h, qreal value)
{
    return fmod(value * h, 360.);
}

float
roundDegree(float deg)
{
    deg = deg - (int(deg / 360)) * 360;
    if (deg < 0) deg += 360;
    return deg;
}

const ZodiacSign&
getSign(float deg, const Zodiac& zodiac)
{
    for (const ZodiacSign& s : zodiac.signs)
        if (s.startAngle <= deg && s.endAngle > deg) return s;
    return zodiac.signs[zodiac.signs.count() - 1];
}

int
getHouse(const Houses& houses, float deg)
{
    for (int i = 0; i <= 10; i++)
        if ((deg >= houses.cusp[i] && deg < houses.cusp[i + 1])
            || (houses.cusp[i] > 180 && houses.cusp[i + 1] < 180
                && (deg < houses.cusp[i + 1] || deg >= houses.cusp[i])))
        {
            return i + 1;
        }

    return 12;
}

int
getHouse(ZodiacSignId sign, const Houses& houses, const Zodiac& zodiac)
{
    if (sign == Sign_None) return 0;

    for (int i = 1; i <= 12; i++) {
        auto hs = getSign(houses.cusp[i - 1], zodiac).id;
        if (sign == hs
            /*|| (sign + 1) % 13 == getSign(houses.cusp[i % 12], zodiac).id*/)
            return i;
    }
    return 0;
}

int
getHouse(const Horoscope& scope, float deg)
{
    return getHouse(scope.houses, deg);
}

QList<int>
getHouses(ZodiacSignId sign, const Houses& houses, const Zodiac& zodiac)
{
    if (sign == Sign_None) return {};
    QList<int> ret;

    // Helper to check if angle 'x' is strictly inside the interval (a, b)
    // moving forward around the circle (0..360). Handles wraparound.
    auto angleInInterval = [](float x, float a, float b) -> bool {
        x = fmod(x + 360.0f, 360.0f);
        a = fmod(a + 360.0f, 360.0f);
        b = fmod(b + 360.0f, 360.0f);
        if (a < b) return (x > a && x < b);
        // wraparound
        return (x > a && x < 360.0f) || (x >= 0.0f && x < b);
    };

    // Get sign's angular span from the zodiac
    const ZodiacSign* ssign = nullptr;
    for (const ZodiacSign& zs : zodiac.signs) {
        if (zs.id == sign) {
            ssign = &zs;
            break;
        }
    }
    if (!ssign) return ret;

    float sstart = ssign->startAngle;
    float send   = ssign->endAngle;

    // For each house, check two cases:
    // 1) The sign is the sign at the house start cusp -> normal rulership
    // (positive house) 2) The sign's entire span lies strictly within the house
    // span -> intercepted (negative house)
    for (int i = 1; i <= 12; ++i) {
        float houseStart = houses.cusp[i - 1];
        float houseEnd   = houses.cusp[i % 12]; // wraps to cusp[0] for house 12

        // Case 1: cusp start sign equals the target sign
        if (getSign(houseStart, zodiac).id == sign) {
            if (!ret.contains(i)) ret << i;
            continue;
        }

        // Case 2: intercepted sign - sign's start and end both strictly inside
        // the house span
        if (angleInInterval(sstart, houseStart, houseEnd)
            && angleInInterval(send, houseStart, houseEnd))
        {
            if (!ret.contains(-i)) ret << -i;
        }
    }

    return ret;
}

float
angle(const Star& body1, const Star& body2)
{
    switch (aspectMode) {
    case amcGreatCircle: {
        float a = angle(body1.eclipticPos.x(), body2.eclipticPos.x());
        float b = angle(body1.eclipticPos.y(), body2.eclipticPos.y());
        return sqrt(a * a + b * b);
    }
    case amcEcliptic:
        return angle(body1.eclipticPos.x(), body2.eclipticPos.x());
    case amcEquatorial:
        return angle(body1.equatorialPos.x(), body2.equatorialPos.x());
    case amcPrimeVertical: return angle(body1.pvPos, body2.pvPos);

    case amcUnknown:
    case amcEND:           break;
    }
    return 0;
}

float
angle(const Star& body, float deg)
{
    switch (aspectMode) {
    case amcGreatCircle:
    case amcEcliptic:      return angle(body.eclipticPos.x(), deg);
    case amcEquatorial:    return angle(body.equatorialPos.x(), deg);
    case amcPrimeVertical: return angle(body.pvPos, deg);

    case amcUnknown:
    case amcEND:           break;
    }
    return 0;
}

float
angle(const Star& body, QPointF coordinate)
{
    switch (aspectMode) {
    case amcGreatCircle: {
        float a = angle(body.eclipticPos.x(), coordinate.x());
        float b = angle(body.eclipticPos.y(), coordinate.y());
        return sqrt(pow(a, 2) + pow(b, 2));
    }
    case amcEcliptic:      return angle(body.eclipticPos.x(), coordinate.x());
    case amcEquatorial:    return angle(body.equatorialPos.x(), coordinate.x());
    case amcPrimeVertical: return angle(body.pvPos, coordinate.x());
    default:               return 0;
    }
}

float
angle(float deg1, float deg2)
{
    float ret = fabs(deg1 - deg2);
    if (ret > 180) ret = 360 - ret;
    return ret;
}

AspectId
aspect(const Star& planet1, const Star& planet2, const AspectsSet& aspectSet)
{
#if 0
    if (planet1.isPlanet() && planet2.isPlanet()
        && planet1.getSWENum() == planet2.getSWENum())
        return Aspect_None;
#endif
    return aspect(angle(planet1, planet2), aspectSet);
}

inline AspectId
aspect(const Planet* p1, const Planet* p2, const AspectsSet& asps)
{
    return aspect(*p1, *p2, asps);
}

AspectId
aspect(const Star& planet1, float degree, const AspectsSet& aspectSet)
{
    return aspect(angle(planet1, degree), aspectSet);
}

AspectId
aspect(const Star& planet, QPointF coordinate, const AspectsSet& aspectSet)
{
    return aspect(angle(planet, coordinate), aspectSet);
}

AspectId
aspect(float angle, const AspectsSet& aspectSet)
{
    AspectId closest = Aspect_None;
    float    pct     = 1;
    for (const AspectType& aspect : aspectSet.aspects) {
        auto npct = abs(aspect.angle) / aspect.orb();
        if (aspect.isEnabled() && aspect.angle - aspect.orb() <= angle
            && aspect.angle + aspect.orb() >= angle
            && (closest == Aspect_None ? true : npct < pct))
        {
            closest = aspect.id;
            pct     = npct;
            // return aspect.id;
        }
    }
    return closest;
}

bool
towardsMovement(const Planet& planet1, const Planet& planet2)
{
    const Planet *p1 = &planet1, *p2 = &planet2;

    if (!isEarlier(planet1, planet2)) // make first planet earlier than second
    {
        p1 = &planet2;
        p2 = &planet1;
    }

    switch (aspectMode) {
    case amcPrimeVertical:
    case amcEquatorial:
        return (p1->equatorialSpeed.x() > p2->equatorialSpeed.x());
    default:
    case amcEcliptic: return (p1->eclipticSpeed.x() > p2->eclipticSpeed.x());
    }
}

PlanetPosition
getPosition(const Planet& planet, ZodiacSignId sign)
{
    if (planet.homeSigns.contains(sign)) return Position_Dwelling;
    if (planet.exaltationSigns.contains(sign)) return Position_Exaltation;
    if (planet.exileSigns.contains(sign)) return Position_Exile;
    if (planet.downfallSigns.contains(sign)) return Position_Downfall;
    return Position_Normal;
}

const Planet*
almuten(const Horoscope& scope)
{
    int           max = 0;
    const Planet* ret = 0;

    for (const Planet& p : scope.planets) {
        int val = p.power.dignity + p.power.deficient;
        if (val > max) {
            max = val;
            ret = &p;
        }
    }

    return ret;
}

const Planet*
doryphoros(const Horoscope& scope)
{
    int           minAngle = 180;
    const Planet* ret      = 0;

    for (const Planet& p : scope.planets) {
        if (p.id == Planet_Sun || !p.isReal) continue;

        int val = angle(p, scope.sun);

        if (val < minAngle && isEarlier(p, scope.sun)) {
            minAngle = val;
            ret      = &p;
        }
    }

    return ret;
}

const Planet*
auriga(const Horoscope& scope)
{
    int           minAngle = 180;
    const Planet* ret      = 0;

    for (const Planet& p : scope.planets) {
        if (p.id == Planet_Sun || !p.isReal) continue;

        int val = angle(p, scope.sun);

        if (val < minAngle && !isEarlier(p, scope.sun)) {
            minAngle = val;
            ret      = &p;
        }
    }

    return ret;
}

bool
rulerDisposition(int house, int houseAuthority, const Horoscope& scope)
{
    if (house <= 0 || houseAuthority <= 0) return false;

    for (const Planet& p : scope.planets)
        if (p.house == house && p.houseRuler.contains(houseAuthority))
            return true;

    return false;
}

bool
isEarlier(const Planet& planet, const Planet& sun)
{
    // float opposite = roundDegree(sun.eclipticPos.x() - 180);
    switch (aspectMode) {
    case amcPrimeVertical: return (roundDegree(planet.pvPos - sun.pvPos) > 180);
    case amcEquatorial:
        return (roundDegree(planet.equatorialPos.x() - sun.equatorialPos.x())
                > 180);
    default:
    case amcEcliptic:
        return (roundDegree(planet.eclipticPos.x() - sun.eclipticPos.x())
                > 180);
    }
}

const Planet&
ruler(int house, const Horoscope& scope)
{
    static Planet emptyPlanet;
    if (house <= 0 || house > 12) return emptyPlanet;

    // Iterate using keys() method like elsewhere in the code
    for (PlanetId id : scope.planets.keys()) {
        const Planet& p = scope.planets[id];
        if (p.houseRuler.contains(house)) return p;
    }

    return emptyPlanet;
}

PlanetId
receptionWith(const Planet& planet, const Horoscope& scope)
{
    for (const Planet& p : scope.planets) {
        if (p != planet) {
            if (getPosition(p, planet.sign->id) == Position_Dwelling
                && getPosition(planet, p.sign->id) == Position_Dwelling)
                return p.id;

            if (getPosition(p, planet.sign->id) == Position_Exaltation
                && getPosition(planet, p.sign->id) == Position_Exaltation)
                return p.id;
        }
    }

    return Planet_None;
}

Planet
calculatePlanet(PlanetId         planet,
                const InputData& input,
                double           jd,
                double&          eps,
                unsigned int&    flags,
                double&          ablong,
                double           RAMC,
                ZodiacId         zid)
{
    Planet ret = getPlanet(planet);

    uint   invertPositionFlag = 256 * 1024;
    char   errStr[256]        = "";
    double xx[6];

    // turn off true pos
    flags = (SEFLG_SWIEPH | ret.sweFlags) & ~SEFLG_TRUEPOS;

    if (zid > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(zid - 2, 0, 0);
    }
    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    eps = xx[0];

    // TODO: wrong moon speed calculation
    // (flags: SEFLG_TRUEPOS|SEFLG_SPEED = 272)
    //         272|invertPositionFlag = 262416
    if (swe_calc_ut(jd, ret.sweNum, flags, xx, errStr) == ERR) {
        qDebug("A: can't calculate position of '%s' at julian day %f: %s",
               qPrintable(ret.name),
               jd,
               errStr);
    }
    if (!(ret.sweFlags & invertPositionFlag)) ret.eclipticPos.setX(xx[0]);
    else // found 'inverted position' flag
        ret.eclipticPos.setX(roundDegree(xx[0] - 180));

    ret.eclipticPos.setY(xx[1]);
    ret.distance = xx[2];
    ret.eclipticSpeed.setX(xx[3]);
    ret.eclipticSpeed.setY(xx[4]);

    if (/*ret.sweNum != SE_MOON &&*/ ret.sweNum != SE_SUN) {
        double phaen[20];
        swe_pheno_ut(jd, ret.sweNum, flags, phaen, errStr);
        ret.phaseAngle = phaen[0];
        ret.elongation = phaen[2];
    }

    double geopos[3];
    geopos[0] = input.location().x();
    geopos[1] = input.location().y();
    geopos[2] = 199 /*meters*/; // input.location.z();

    ablong = xx[0];

    // A hack to calculate prime vertical longitude from the campanus house
    // position
    if (zid <= 1
        || swe_calc_ut(jd, ret.sweNum, flags & ~SEFLG_SIDEREAL, xx, errStr)
               != ERR)
    {
        ablong = xx[0];
        // We may have had to get tropical position to get the
        // house position -- this API wants tropical longitude.
        // From there we fudge a prime vertical coordinate.
        double housePos = swe_house_pos(RAMC, geopos[1], eps, 'C', xx, errStr);
        ret.pvPos       = (housePos - 1) / 12 * 360;
    }

    // calculate horizontal coordinates
    double hor[3];
    swe_azalt(jd, SE_ECL2HOR, geopos, 0, 0, xx, hor);
    ret.horizontalPos.setX(hor[0]);
    ret.horizontalPos.setY(hor[1]);

    if (swe_calc_ut(jd,
                    ret.sweNum,
                    flags | SEFLG_EQUATORIAL | SEFLG_SPEED,
                    xx,
                    errStr)
        != ERR)
    {
        ret.equatorialPos.setX(xx[0]);
        ret.equatorialPos.setY(xx[1]);
        ret.equatorialSpeed.setX(xx[3]);
        ret.equatorialSpeed.setY(xx[4]);
    }

    return ret;
}

Planet
calculatePlanet(PlanetId         planet,
                const InputData& input,
                const Houses&    houses,
                const Zodiac&    zodiac)
{
    double jd = getJulianDate(input.GMT());

    char errStr[256] = "";

    double       eps, ablong;
    unsigned int flags;
    Planet       ret = calculatePlanet(planet,
                                 input,
                                 jd,
                                 eps,
                                 flags,
                                 ablong,
                                 houses.RAMC,
                                 zodiac.id);

    double geopos[3];
    geopos[0] = input.location().x();
    geopos[1] = input.location().y();
    geopos[2] = 199 /*meters*/; // input.location.z();

    ret.sign     = &getSign(ret.eclipticPos.x(), zodiac);
    ret.house    = getHouse(houses, ret.eclipticPos.x());
    ret.position = getPosition(ret, ret.sign->id);
    int prev     = -1;
    for (auto s : std::as_const(ret.homeSigns)) {
        if (s == prev) continue;
        ret.houseRuler << getHouses(s, houses, zodiac);
        prev = s;
    }
    std::sort(ret.houseRuler.begin(), ret.houseRuler.end());
    // Remove duplicate house entries (can occur when multiple home signs map
    // to the same house or interception logic produced overlaps)
    ret.houseRuler.erase(
        std::unique(ret.houseRuler.begin(), ret.houseRuler.end()),
        ret.houseRuler.end());

    double rettm;
    int    eflg = SEFLG_SWIEPH;

    int32  deg, min, sec, sgn;
    double frac;
    double st = swe_degnorm(ret.equatorialPos.x());
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);

    if (primDirMode == prdActive) {
        for (Star::angleTransitMode m = Star::atAsc; m < Star::numAngles;
             m                        = Star::angleTransitMode(m + 1))
        {
            if (swe_rise_trans(houses.startSpeculum,
                               ret.sweNum,
                               nullptr /*starname*/,
                               eflg,
                               Star::angleTransitFlag(m),
                               geopos,
                               1013.25 /*atpress*/,
                               10 /*attemp*/,
                               &rettm,
                               errStr)
                >= 0)
            {
                st =
                    swe_degnorm(swe_sidtime(rettm) * 15 + input.location().x());
                swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
                qDebug("  ACTUAL TIME %s %3d %02d %02d",
                       qPrintable(angleDesc[m]),
                       deg,
                       min,
                       sec);
                ret.angleTransit[m] = Planet::timeToDT(rettm);

                // Calculate RA at this transit time for prdActive mode
                double xx[6];
                char   serr[AS_MAXCH];
                int    iflgret = swe_calc_ut(rettm,
                                          ret.sweNum,
                                          SEFLG_EQUATORIAL | SEFLG_SPEED,
                                          xx,
                                          serr);
                if (iflgret >= 0) {
                    ret.angleTransitRA[m] = xx[0]; // RA in degrees
                } else {
                    ret.angleTransitRA[m] = 0.0;
                }
            }
        }
    } else {
        std::array<double,Star::numAngles> at;

        double RA = ret.equatorialPos.x();
        double DD;
        if (primDirMode == prdZodiacal) {
            DD = asind(sind(eps) * sind(ret.eclipticPos.x()));
        } else {
            DD = ret.equatorialPos.y();
        }
        double AD   = asind(tand(DD) * tand(input.location().y()));
        double OA   = RA - AD;
        double OD   = RA + AD;
        double HDor = swe_degnorm(OA - houses.OAAC);
        double HDoc = swe_degnorm(OD - houses.ODDC);

        at[Star::atMC]   = RA;
        at[Star::atAsc]  = swe_degnorm(houses.RAMC + HDor);
        at[Star::atDesc] = swe_degnorm(houses.RAMC + HDoc);
        at[Star::atIC]   = swe_degnorm(RA - 180);

        double jd0          = getJulianDate(input.GMT());
        double RAMC0        = houses.RAMC; // in degrees
        double sidereal_day = 1; //0.99726958;  // days

        QString rastr = "Planet Angle Transits in RA of " + ret.name.left(3);

        char delim = ':';
        for (unsigned i = 0; i < Star::numAngles; ++i) {
            auto m = Star::angleTransitMode(i);
            double RA_target = at[m];
            double delta_deg =
                swe_difdeg2n(RA_target, RAMC0); // signed difference
            double delta_jd     = delta_deg / 360.0 * sidereal_day;
            double jd_target    = jd0 + delta_jd;

            ret.angleTransit[m] = dateTimeFromJulian(jd_target);
            
            // Store the RA value for this transit
            ret.angleTransitRA[m] = RA_target;
            
            QString desc = angleDesc[m];

            swe_split_deg(RA_target, 0, &deg, &min, &sec, &frac, &sgn);
            rastr += QString("%1 %2 %3 %4'%5\"")
                         .arg(delim)
                         .arg(desc)
                         .arg(deg, 3, 10, QChar(' '))
                         .arg(min, 2, 10, QChar('0'))
                         .arg(sec, 2, 10, QChar('0'));
            delim = ',';
        }
#if 0 // !DEBUG_FINDER_THREADS
        qDebug() << qPrintable(rastr);
#endif
    }
    if (ret.id == Planet_SouthNode) {
        qSwap(ret.angleTransit[Star::atAsc], ret.angleTransit[Star::atDesc]);
        qSwap(ret.angleTransit[Star::atMC], ret.angleTransit[Star::atIC]);
        qSwap(ret.angleTransitRA[Star::atAsc],
              ret.angleTransitRA[Star::atDesc]);
        qSwap(ret.angleTransitRA[Star::atMC], ret.angleTransitRA[Star::atIC]);
    }

    return ret;
}

qreal
PlanetLoc::compute(const InputData& ida, double jd, int h)
{
    qreal pos;
    std::tie(pos, speed) = compute(planet, ida, jd);
    loc = _rasiLoc = pos;
    if (h > 1) {
        loc = harmonic(h, pos);
        speed *= h;
    }
    return pos;
}

std::pair<qreal, qreal>
PlanetLoc::compute(const ChartPlanetId& planet, const InputData& ida, double jd)
{
    constexpr uint invertPositionFlag = 256 * 1024;
    char           errStr[256]        = "";

    const Planet& p1(getPlanet(planet.planetId()));
    uint          flags = (SEFLG_SWIEPH | p1.sweFlags) & ~SEFLG_TRUEPOS;
    bool          trop  = true;
    if (ida.zodiac() > 1) {
        trop = false;
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(ida.zodiac() - 2, 0, 0);
    }

    double xx[6];
    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    auto eps = xx[0];

    typedef std::pair<double, double> posSpd;
    auto getAscMC = [&](unsigned i, bool trop = false) -> posSpd {
        double cusps[14], cuspspd[14], ascmc[11], ascmcspd[11];
        auto   jdut  = getUTfromET(jd);
        uint   flags = SEFLG_SWIEPH;
        if (!trop) flags |= SEFLG_SIDEREAL;
        swe_houses_ex2(jdut,
                       flags,
                       ida.location().y(),
                       ida.location().x(),
                       'C',
                       cusps,
                       ascmc,
                       cuspspd,
                       ascmcspd,
                       errStr);
        return { ascmc[i], ascmcspd[i] };
    };

    auto getPos = [&](const Planet& p, qreal& speed) -> qreal {
        int   ret = ERR;
        qreal pos = 0.0;
        switch (aspectMode) {
        case amcEcliptic:
            if (p.id == Planet_Asc || p.id == Planet_Desc) {
                std::tie(pos, speed) = getAscMC(0, trop);
                if (p.id == Planet_Desc) pos = swe_degnorm(pos + 180.);
                ret = OK;
                // FIXME speed?
            } else if (p.id == Planet_MC || p.id == Planet_IC) {
                std::tie(pos, speed) = getAscMC(1, trop);
                if (p.id == Planet_IC) pos = swe_degnorm(pos + 180.);
                ret = OK;
                // FIXME speed?
            } else {
                ret = swe_calc_ut(jd, p.sweNum, flags, xx, errStr);
                pos = xx[0];
                if (p.id == Planet_SouthNode) pos = swe_degnorm(pos + 180.);
                speed = xx[3];
            }
            break;

        case amcEquatorial:
            if (p.id == Planet_Asc || p.id == Planet_Desc) {
                double xx[6];
                std::tie(xx[0], speed) = getAscMC(0, true /*trop*/);
                xx[1] = 0, xx[2] = 1.0;
                swe_cotrans(xx, xx, -eps);
                pos = xx[0];
                if (p.id == Planet_Desc) pos = swe_degnorm(pos + 180.);
                ret = OK;
                // FIXME speed?
            } else if (p.id == Planet_MC || p.id == Planet_IC) {
                // XXX do we not use cotrans here, too?
                std::tie(pos, speed) = getAscMC(2, true /*trop*/);
                if (p.id == Planet_IC) pos = swe_degnorm(pos + 180.);
                ret = OK;
            } else {
                ret = swe_calc_ut(jd,
                                  p.sweNum,
                                  (flags & ~SEFLG_SIDEREAL) | SEFLG_EQUATORIAL
                                      | SEFLG_SPEED,
                                  xx,
                                  errStr);
                pos = xx[0];
                if (p.id == Planet_SouthNode) pos = swe_degnorm(pos + 180.);
                speed = xx[3];
            }
            break;

        default:
        case amcPrimeVertical: {
            if (p.id == Planet_Asc) {
                pos = 0;
                ret = OK; // speed?
            } else if (p.id == Planet_Desc) {
                pos = 180;
                ret = OK;
            } else if (p.id == Planet_MC) {
                pos = 270;
                ret = OK; // speed?
            } else if (p.id == Planet_IC) {
                pos = 90;
                ret = OK;
            } else {
                ret           = swe_calc_ut(jd,
                                  p.sweNum,
                                  flags & ~SEFLG_SIDEREAL,
                                  xx,
                                  errStr);
                auto housePos = swe_house_pos(getAscMC(2, true /*trop*/).first,
                                              ida.location().y(),
                                              eps,
                                              'C',
                                              xx,
                                              errStr);
                pos           = (housePos - 1) / 12 * 360;
            }
            break;
        }
        }
        if (ret != ERR) return pos;
        qDebug() << "Can't calculate position of " << p1.name << "at jd" << jd
                 << ":" << errStr;
        return 0;
    };

    qreal speed = 0;
    auto  pos   = getPos(p1, speed);
    if (planet.isMidpt()) {
        qreal speed2 = 0;
        auto  pos2   = getPos(getPlanet(planet.planetId2()), speed2);
        if (pos - pos2 >= 180.) pos -= 360.;
        else if (pos2 - pos >= 180.)
            pos2 -= 360.;
        pos = swe_degnorm((pos + pos2) / 2.);
        speed += speed2;
        if (planet.isOppMidpt()) pos = swe_degnorm(pos + 180.);
    }
    return { pos, speed };
}

qreal
PlanetLoc::compute(const InputData& ida)
{
    return compute(ida, getJulianDate(ida.GMT()), -1);
}

qreal
PlanetLoc::defaultSpeed() const
{
    switch (aspectMode) {
    case amcEquatorial:
    case amcEcliptic:
        if (planet.isMidpt()) {
            auto p1 = getPlanet(planet.planetId());
            auto p2 = getPlanet(planet.planetId2());
            return std::abs(p1.defaultEclipticSpeed.x()
                            - p2.defaultEclipticSpeed.y());
        }
        return getPlanet(planet.planetId()).defaultEclipticSpeed.x();
    case amcPrimeVertical: return -360;
    default:               return 0;
    }
}

/*static*/
std::pair<qreal, qreal>
PlanetProfile::computeDelta(const Loc* a, const Loc* b, unsigned int h /*=1*/)
{
    // It is not obvious what needs to happen here.
    // We certainly want to update the positions of the in-motion
    // entities, but if there is more than one of these, getting
    // an average position wouldn't allow us to converge, only
    // track an aspect to a moving midpoint. In contrast, in most
    // cases we want to find a "root", i.e., a zero-point in between
    // a negative and a positive result. Even if we use a spread
    // value to minimize (will never be less than zero), the
    // averaged speed value isn't necessarily a proper derivative...
    // Hmm... Perhaps when there is more than one planet, we
    // need to compute spread and position? There is more than one
    // value, but we only have one degree of freedom (one input variable)
    // to control...
    qreal speed = (a->speed + b->speed);
    if (h > 1) speed *= qreal(h);

    qreal apos = h > 1 ? harmonic(h, a->loc) : a->loc;
    qreal bpos = h > 1 ? harmonic(h, b->loc) : b->loc;

    if (bpos - apos > 180) bpos -= 360;
    else if (apos - bpos > 180)
        bpos += 360;

    return { bpos - apos, speed };
}

/*static*/
std::pair<qreal, qreal>
PlanetProfile::computeSpread(std::initializer_list<const Loc*> locs,
                             unsigned int                      h /*=1*/)
{
    std::vector<qreal> pos(locs.size());
    qreal              spd = 0.0;
    unsigned           i   = 0;
    for (auto loc : locs) {
        pos[i++] = (h > 1 ? harmonic(h, loc->loc) : loc->loc);
        spd += (h > 1 ? loc->speed * qreal(h) : loc->speed);
    }
    if (false && locs.size() == 2) {
        return { angle(pos[0], pos[1]), spd };
    }

    qreal maxa = 0;
    for (unsigned i = 0, n = pos.size(); i + 1 < n; ++i) {
        for (unsigned j = i + 1; j < n; ++j) {
            auto a = angle(pos[i], pos[j]);
            if (a > maxa) maxa = a;
        }
    }
    return { maxa, spd }; // XXX
}

qreal
PlanetProfile::computePos(double jd, unsigned int h /*=1*/)
{
    Loc::loc   = 0;
    Loc::speed = 0;
    for (auto loc : *this) {
        (*loc)(jd, 1);
    }
    if (size() == 1) {
        Loc::loc   = h > 1 ? harmonic(h, front()->loc) : front()->loc;
        Loc::speed = h > 1 ? h * front()->speed : front()->speed;
    } else if (size() == 2) {
        std::tie(Loc::loc, Loc::speed) = computeDelta(front(), back(), h);
    }
    return Loc::loc;
}

PlanetProfile::PlanetProfile(std::initializer_list<QMap<int, Planet>*> pms)
{
    int fid = 0;
    for (auto ppm : pms) {
        auto& pm = *ppm;
        for (const auto& plan : pm) {
            auto  pid  = plan.id;
            qreal ploc = plan.getPrefPos();
            qreal spd  = plan.getPrefSpd();
            emplace_back(new PlanetLoc(fid, pid, ploc, spd));
        }
        ++fid;
    }
}

qreal
PlanetProfile::computeSpread(double jd)
{
    computePos(jd);
    return computeSpread();
}

qreal
PlanetProfile::computeSpread()
{
    if (size() == 2) return angle(at(0)->loc, at(1)->loc);

    std::sort(begin(), end(), [](const Loc* a, const Loc* b) {
        return a->loc < b->loc;
    });

    qreal maxa = 0;
    for (unsigned i = 0, n = size(); i < n; ++i) {
        auto a = angle(at(i)->loc, at((i + 1) % n)->loc);
        if (a > maxa) maxa = a;
    }

    return maxa;
}

Star
calculateStar(const QString&   name,
              const InputData& input,
              const Houses&    houses,
              const Zodiac&    zodiac)
{
    Star ret = getStar(name);

    uint   invertPositionFlag = 256 * 1024;
    double jd                 = getJulianDate(input.GMT());
    char   errStr[256]        = "";

    double xx[6];
    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];

    unsigned int flags = ret.sweFlags & ~SEFLG_TRUEPOS; // turn off true pos
    if (zodiac.id > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(zodiac.id - 2, 0, 0);
    }

    // (flags: SEFLG_TRUEPOS|SEFLG_SPEED = 272)
    //         272|invertPositionFlag = 262416
    char starName[256];
    strcpy(starName, ret.name.toStdString().c_str());
    if (swe_fixstar_ut(starName, jd, flags | SEFLG_SWIEPH, xx, errStr) != ERR
        && strlen(errStr) != ERR)
    {
        if (!(ret.sweFlags & invertPositionFlag)) ret.eclipticPos.setX(xx[0]);
        else // found 'inverted position' flag
            ret.eclipticPos.setX(roundDegree(xx[0] - 180));

        ret.eclipticPos.setY(xx[1]);
        ret.distance = xx[2];

        if (swe_fixstar_ut(starName,
                           jd,
                           (flags & ~SEFLG_SIDEREAL) | SEFLG_SWIEPH
                               | SEFLG_EQUATORIAL,
                           xx,
                           errStr)
                != ERR
            && strlen(errStr) == 0)
        {
            ret.equatorialPos.setX(xx[0]);
            ret.equatorialPos.setY(xx[1]);
        }

        double geopos[3]; // calculate horizontal coordinates
        double hor[3];
        geopos[0] = input.location().x();
        geopos[1] = input.location().y();
        geopos[2] = input.location().z();
        swe_azalt(jd, SE_ECL2HOR, geopos, 0, 0, xx, hor);
        ret.horizontalPos.setX(hor[0]);
        ret.horizontalPos.setY(hor[1]);

        if (primDirMode == prdActive) {
            double rettm;
            int    eflg = SEFLG_SWIEPH;

            for (auto m = Star::atAsc; m < Star::numAngles;
                 m      = Star::angleTransitMode(m + 1))
            {
                if (swe_rise_trans(houses.startSpeculum,
                                   -1,
                                   starName,
                                   eflg,
                                   Star::angleTransitFlag(m),
                                   geopos,
                                   1013.25,
                                   10,
                                   &rettm,
                                   errStr)
                    >= 0)
                {
                    ret.angleTransit[m] = Planet::timeToDT(rettm);

                    // Calculate RA at this transit time for prdActive mode
                    double xx[6];
                    char   serr[AS_MAXCH];
                    int    iflgret = swe_fixstar_ut(starName,
                                                 rettm,
                                                 SEFLG_EQUATORIAL | SEFLG_SPEED,
                                                 xx,
                                                 serr);
                    if (iflgret >= 0) {
                        ret.angleTransitRA[m] = xx[0]; // RA in degrees
                    } else {
                        ret.angleTransitRA[m] = 0.0;
                    }
                }
            }
        } else {
            std::array<double,Star::numAngles> at;

            double RA = ret.equatorialPos.x();
            double DD;
            if (primDirMode == prdZodiacal) {
                DD = asind(sind(eps) * sind(ret.eclipticPos.x()));
            } else {
                DD = ret.equatorialPos.y();
            }
            double AD = asind(tand(DD) * tand(input.location().y()));
            double OA = RA - AD;
            double OD = RA + AD;

            at[Star::atMC]   = RA;
            at[Star::atAsc]  = swe_degnorm(houses.RAMC + (OA - houses.OAAC));
            at[Star::atDesc] = swe_degnorm(houses.RAMC + (OD - houses.ODDC));
            at[Star::atIC]   = swe_degnorm(RA - 180);

            double jd0          = getJulianDate(input.GMT());
            double RAMC0        = houses.RAMC; // in degrees
            double sidereal_day = 1; //0.99726958;  // days

#if 0
            QString rastr = "Star Angle Transits in RA of " + ret.name.left(3);
            char    delim = ':';
            int32   deg, min, sec, sgn;
            double  frac;
#endif
            for (unsigned i = 0; i < Star::numAngles; ++i) {
                auto m = Star::angleTransitMode(i);
                double RA_target = at[m];
                double delta_deg =
                    swe_difdeg2n(RA_target, RAMC0); // signed difference
                double delta_jd     = delta_deg / 360.0 * sidereal_day;
                double jd_target    = jd0 + delta_jd;
                ret.angleTransit[m] = dateTimeFromJulian(jd_target);

                // Store the RA value for this transit
                ret.angleTransitRA[m] = RA_target;
#if 0
                swe_split_deg(RA_target, 0, &deg, &min, &sec, &frac, &sgn);
                rastr += QString(A_DECODE("%1 %2 %3 %4'%5\""))
                             .arg(delim)
                             .arg(angleDesc[m])
                             .arg(deg)
                             .arg(min, 2, 10, QChar('0'))
                             .arg(sec, 2, 10, QChar('0'));
                delim = ',';
#endif
            }
            // qDebug() << qPrintable(rastr);
        }
    } else {
        qDebug("A: can't calculate position of '%s' at julian day %f: %s",
               qPrintable(ret.name),
               jd,
               errStr);
    }

    ret.house = getHouse(houses, ret.eclipticPos.x());

    return ret;
}

Houses
calculateHouses(const InputData& input)
{
    Houses ret;
    ret.system         = &getHouseSystem(input.houseSystem());
    unsigned int flags = SEFLG_SWIEPH;
    if (input.zodiac() > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(input.zodiac() - 2, 0, 0);
    }

    double julianDay   = getJulianDate(input.GMT(), false /*i.e., UT*/);
    double jd          = getJulianDate(input.GMT(), true /*i.e., ET*/);
    char   errStr[256] = "";
    double xx[6];

    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];

    double hcusps[14], ascmc[11];

    // get the tropical ascendant...
    swe_houses_ex(julianDay,
                  flags & ~SEFLG_SIDEREAL,
                  input.location().y(),
                  input.location().x(),
                  ret.system->sweCode,
                  hcusps,
                  ascmc);
    double asc = ascmc[0]; // tropical asc
    ret.RAMC = ascmc[2];
    xx[0]      = ascmc[0];
    xx[1]      = 0.0;
    xx[2]      = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RAAC = xx[0];
    xx[0]    = swe_degnorm(ascmc[0] + 180);
    xx[1]    = 0.0;
    xx[2]    = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RADC = xx[0];
    // TODO could also get eastpoint and vertex in RA here...
    
    // Calculate OAAC and ODDC using declination and geographic latitude
    double DD = asind(sind(eps) * sind(asc));
    double AD = asind(tand(DD) * tand(input.location().y()));
    ret.OAAC  = ret.RAAC - AD;
    
    DD = asind(sind(eps) * sind(swe_degnorm(asc + 180)));
    AD = asind(tand(DD) * tand(input.location().y()));
    ret.ODDC  = ret.RADC + AD;

    if (flags & SEFLG_SIDEREAL) {
        swe_houses_ex(julianDay,
                      flags,
                      input.location().y(),
                      input.location().x(),
                      ret.system->sweCode,
                      hcusps,
                      ascmc);
    }

    for (int i = 0; i < 12; i++) ret.cusp[i] = hcusps[i + 1];

    ret.Asc  = ascmc[0];
    ret.MC   = ascmc[1];
    //ret.RAMC = ascmc[2];
    ret.Vx   = ascmc[3];
    ret.EA   = ascmc[4];

    double st =
        swe_degnorm((swe_sidtime(julianDay)) * 15 + input.location().x());
    double frac;
    int    deg, min, sec, sgn;
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    // qDebug("ST from GMT %3d %02d %02d", deg, min, sec);
    st = swe_degnorm(ret.RAMC);
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    // qDebug("ST from RAMC %3d %02d %02d", deg, min, sec);

    st = swe_degnorm(ret.RAAC);
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    // qDebug("RAAC %3d %02d %02d", deg, min, sec);

    ret.halfMedium = swe_difdegn(ret.RAAC, ret.RAMC);
    ret.halfImum   = 180 - ret.halfMedium;
    // ret.swneDelta = swe_degnorm(ret.RAMC - swe_degnorm(ret.RAAC - 180)) /
    // 360.0;

    // compute house position of sun so we can see
    // whether it's closer to sunset or sunrise.

    // first get tropical position (well, latitude)
    swe_calc_ut(julianDay, SE_SUN, flags & ~SEFLG_SIDEREAL, xx, errStr);

    double geopos[3] = { input.location().x(),
                         input.location().y(),
                         input.location().z() };

    double housePos = swe_house_pos(ret.RAMC, geopos[1], eps, 'C', xx, errStr);

    // now get our speculum start
    int which = (housePos >= 4 && housePos < 10) ? SE_CALC_RISE : SE_CALC_SET;
    swe_rise_trans(julianDay - 1,
                   SE_SUN,
                   NULL,
                   SEFLG_SWIEPH,
                   which,
                   geopos,
                   1013.25,
                   10,
                   &ret.startSpeculum,
                   errStr);

    return ret;
}

Houses
calculateHouses(const InputData& input, double progressedMC)
{
    // This version calculates houses using a specified MC value
    // Used for progressed charts where MC is calculated via solar arc

    Houses ret;
    ret.system         = &getHouseSystem(input.houseSystem());
    unsigned int flags = SEFLG_SWIEPH;
    if (input.zodiac() > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(input.zodiac() - 2, 0, 0);
    }

    double julianDay   = getJulianDate(input.GMT(), false /*i.e., UT*/);
    double jd          = getJulianDate(input.GMT(), true /*i.e., ET*/);
    char   errStr[256] = "";
    double xx[6];

    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];

    // Use the provided progressed MC
    ret.MC = progressedMC;

    // Convert MC to RAMC
    xx[0] = progressedMC;
    xx[1] = 0.0;
    xx[2] = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RAMC = xx[0];

    // Calculate Ascendant and house cusps from the progressed RAMC
    double geopos[3] = { input.location().x(),
                         input.location().y(),
                         input.location().z() };

    double hcusps[14], ascmc[11];

    // Use swe_houses_armc to calculate houses from RAMC
    swe_houses_armc(ret.RAMC,
                    geopos[1],
                    eps,
                    ret.system->sweCode,
                    hcusps,
                    ascmc);

    for (int i = 0; i < 12; i++) ret.cusp[i] = hcusps[i + 1];

    ret.Asc = ascmc[0];
    ret.Vx  = ascmc[3];
    ret.EA  = ascmc[4];

    // Calculate RAAC (RA of Ascendant)
    xx[0] = ret.Asc;
    xx[1] = 0.0;
    xx[2] = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RAAC = xx[0];

    xx[0] = swe_degnorm(ret.Asc + 180);
    xx[1] = 0.0;
    xx[2] = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RADC = xx[0];

    // Calculate oblique ascension
    double DD = asind(sind(eps) * sind(ret.Asc));
    double AD = asind(tand(DD) * tand(input.location().y()));
    ret.OAAC  = input.location().y() >= 0 ? (ret.RAAC - AD) : (ret.RAAC + AD);
    DD        = asind(sind(eps) * sind(swe_degnorm(ret.Asc + 180)));
    AD        = asind(tand(DD) * tand(input.location().y()));
    ret.ODDC  = input.location().y() >= 0 ? (ret.RADC + AD) : (ret.RADC - AD);

    ret.halfMedium = swe_difdegn(ret.RAAC, ret.RAMC);
    ret.halfImum   = 180 - ret.halfMedium;

    // Speculum start (use current time for this)
    swe_calc_ut(julianDay, SE_SUN, flags & ~SEFLG_SIDEREAL, xx, errStr);
    double housePos = swe_house_pos(ret.RAMC, geopos[1], eps, 'C', xx, errStr);
    int which = (housePos >= 4 && housePos < 10) ? SE_CALC_RISE : SE_CALC_SET;
    swe_rise_trans(julianDay - 1,
                   SE_SUN,
                   NULL,
                   SEFLG_SWIEPH,
                   which,
                   geopos,
                   1013.25,
                   10,
                   &ret.startSpeculum,
                   errStr);

    return ret;
}

PlanetPower
calculatePlanetPower(const Planet& planet, const Horoscope& scope)
{
    PlanetPower ret;

    // TODO: does this shit works properly at all?

    if (!planet.isReal) return ret;

    bool peregrine = false;
    switch (planet.position) {
    case Position_Dwelling:
    case Position_Exaltation: ret.dignity += 5; break;
    case Position_Exile:      ret.deficient -= 5; break;
    case Position_Downfall:   ret.deficient -= 4; break;
    case Position_Normal:     peregrine = true;
    default:                  break;
    }

    if (receptionWith(planet, scope) != Planet_None) ret.dignity += 5;
    else if (peregrine)
        ret.deficient -= 5;

    switch (planet.house) {
    case 1:
    case 10: ret.dignity += 5; break;
    case 4:
    case 7:
    case 11: ret.dignity += 4; break;
    case 2:
    case 5:  ret.dignity += 3; break;
    case 9:  ret.dignity += 2; break;
    case 3:  ret.dignity += 1; break;
    case 12: ret.deficient -= 5; break;
    case 8:
    case 6:  ret.deficient -= 2; break;
    default: break;
    }

    if (planet.eclipticSpeed.x() > 0 && planet.id != Planet_Sun
        && planet.id != Planet_Moon)
    {
        ret.dignity += 4;
    }

    if (planet.eclipticSpeed.x() > planet.defaultEclipticSpeed.x())
        ret.dignity += 2;
    else if (planet.eclipticSpeed.x() > 0)
        ret.deficient -= 2;
    else
        ret.deficient -= 5;

    switch (planet.id) {
    case Planet_Mars:
    case Planet_Jupiter:
    case Planet_Saturn:
        if (isEarlier(planet, scope.sun)) ret.dignity += 2;
        else
            ret.deficient -= 2;
        break;
    case Planet_Mercury:
    case Planet_Venus:
    case Planet_Moon:
        if (!isEarlier(planet, scope.sun)) ret.dignity += 2;
        else
            ret.deficient -= 2;
        break;
    default: break;
    }

    if (planet.id != Planet_Sun) {
        if (angle(planet, scope.sun) > 9) // not burned by sun
            ret.dignity += 5;
        else if (angle(planet, scope.sun) < 0.4) // 'in cazimo'
            ret.dignity += 5;
        else // burned by sun
            ret.deficient -= 4;
    }

    if (planet.id != Planet_Jupiter)
        switch (aspect(planet, scope.jupiter, topAspectSet())) {
        case Aspect_Conjunction: ret.dignity += 5; break;
        case Aspect_Trine:       ret.dignity += 4; break;
        case Aspect_Sextile:     ret.dignity += 3; break;
        default:                 break;
        }

    if (planet.id != Planet_Venus)
        switch (aspect(planet, scope.venus, topAspectSet())) {
        case Aspect_Conjunction: ret.dignity += 5; break;
        case Aspect_Trine:       ret.dignity += 4; break;
        case Aspect_Sextile:     ret.dignity += 3; break;
        default:                 break;
        }

    if (planet.id != Planet_NorthNode)
        switch (aspect(planet, scope.northNode, topAspectSet())) {
        case Aspect_Conjunction:
        /*case Aspect_Trine:
        case Aspect_Sextile:     ret.dignity   += 4; break;
        case Aspect_Opposition:  ret.deficient -= 4; break;*/
        default: break;
        }

    if (planet.id != Planet_Mars)
        switch (aspect(planet, scope.mars, topAspectSet())) {
        case Aspect_Conjunction: ret.deficient -= 5; break;
        case Aspect_Opposition:  ret.deficient -= 4; break;
        case Aspect_Quadrature:  ret.deficient -= 3; break;
        default:                 break;
        }

    if (planet.id != Planet_Saturn)
        switch (aspect(planet, scope.saturn, topAspectSet())) {
        case Aspect_Conjunction: ret.deficient -= 5; break;
        case Aspect_Opposition:  ret.deficient -= 4; break;
        case Aspect_Quadrature:  ret.deficient -= 3; break;
        default:                 break;
        }

    if (aspect(planet, scope.stars["Regulus"], tightConjunction())
        == Aspect_Conjunction)
        ret.dignity += 6; // Regulus coordinates at 2000year: 29LEO50, +00.27'

    if (aspect(planet, scope.stars["Spica"], tightConjunction())
        == Aspect_Conjunction)
        ret.dignity += 5; // Spica coordinates at 2000year: 23LIB50, -02.03'

    if (aspect(planet, scope.stars["Algol"], tightConjunction())
        == Aspect_Conjunction)
        ret.deficient -= 5; // Algol coordinates at 2000year: 26TAU10, +22.25'

    return ret;
}

Aspect
calculateAspect(const AspectsSet& aspectSet,
                const Planet&     planet1,
                const Planet&     planet2)
{
    Aspect a;

    a.angle    = angle(planet1, planet2);
    a.d        = &getAspect(aspect(a.angle, aspectSet), aspectSet);
    a.orb      = fabs(a.d->angle - a.angle);
    a.planet1  = &planet1;
    a.planet2  = &planet2;
    a.applying = towardsMovement(planet1, planet2) == (a.angle > a.d->angle);

    return a;
}

inline Aspect
calculateAspect(const AspectsSet& asps, const Planet* p1, const Planet* p2)
{
    return calculateAspect(asps, *p1, *p2);
}

Aspect
calculateAspect(const AspectsSet& aspectSet, const Loc* p1loc, const Loc* p2loc)
{
    Aspect a;
    auto   l1 = p1loc->loc;
    if (auto pl = dynamic_cast<const PlanetLoc*>(p1loc)) l1 = pl->rasiLoc();
    auto l2 = p2loc->loc;
    if (auto pl = dynamic_cast<const PlanetLoc*>(p2loc)) l2 = pl->rasiLoc();
    a.angle = angle(l1, l2);
    a.d     = &getAspect(aspect(a.angle, aspectSet), aspectSet);
    a.orb   = fabs(a.d->angle - a.angle);
    return a;
}

AspectList
calculateAspects(const AspectsSet& aspectSet, const PlanetMap& planets)
{
    AspectList ret;

    PlanetMap::const_iterator i = planets.constBegin();
    while (i != planets.constEnd()) {
#if 0
        if ((i->id >= Planet_Sun && i->id <= Planet_Pluto)
            || i->id == Planet_Chiron)
        {
#endif
        PlanetMap::const_iterator j = std::next(i);
        while (j != planets.constEnd()) {
            if (/*((j->id >= Planet_Sun && j->id <= Planet_Pluto)
                 || j->id == Planet_Chiron)
                &&*/
                aspect(i.value(), j.value(), aspectSet) != Aspect_None)
            {
                ret << calculateAspect(aspectSet, i.value(), j.value());
            }
            ++j;
        }
#if 0
        }
#endif
        ++i;
    }

    return ret;
}

AspectList
calculateAspects(const AspectsSet& aspectSet, const ChartPlanetPtrMap& planets)
{
    AspectList ret;

    for (auto it = planets.cbegin(); it != planets.cend(); ++it) {
        for (auto jit = std::next(it); jit != planets.cend(); ++jit) {
            if (aspect(it->second, jit->second, aspectSet) != Aspect_None) {
                ret << calculateAspect(aspectSet, it->second, jit->second);
            }
        }
    }

    return ret;
}

AspectList
calculateAspects(const AspectsSet& aspectSet,
                 const PlanetMap&  planets1,
                 const PlanetMap&  planets2)
{
    AspectList ret;
    for (auto i = planets1.constBegin(); i != planets1.constEnd(); ++i) {
        // if ((i->id >= Planet_Sun && i->id <= Planet_Pluto) || i->id ==
        // Planet_Chiron) {
        for (auto j = planets2.constBegin(); j != planets2.constEnd(); ++j) {
            if (/*((j->id >= Planet_Sun && j->id <= Planet_Pluto)
                 || j->id == Planet_Chiron)
                &&*/
                aspect(i.value(), j.value(), aspectSet) != Aspect_None)
            {
                ret << calculateAspect(aspectSet, i.value(), j.value());
            }
        }
        //}
    }

    return ret;
}

void
findPlanetStarConfigs(const PlanetMap& planets, StarMap& stars)
{
    modalize<aspectModeType> aspects(aspectMode, amcGreatCircle);

    for (Star& s : stars.values()) s.configuredWithPlanet = Planet_None;

    for (PlanetId pid : planets.keys()) {
        const Planet& p(planets[pid]);
        for (const std::string& name : stars.keys()) {
            Star& s(stars[name]);
            if (aspect(p, s, tightConjunction()) != Aspect_None) {
                s.configuredWithPlanet = p.id;
            }
        }
    }
}

void
calculateHarmonic(double h, Planet& p)
{
    qreal& ecliPos(p.eclipticPos.rx());
    ecliPos = harmonic(h, ecliPos);

    qreal& equiPos(p.equatorialPos.rx());
    equiPos = harmonic(h, equiPos);

    qreal& pvPos(p.pvPos);
    pvPos = harmonic(h, pvPos);
}

void
calculateHarmonic(double     h,
                  Houses&    houses,
                  PlanetMap& planets,
                  bool       includeAsteroids = true,
                  bool       includeCentaurs  = true)
{
    houses.cusp[0] = harmonic(h, houses.cusp[0]);
    for (int i = 1; i < 12; ++i) {
        // use equal house
        houses.cusp[i] = fmod(houses.cusp[i - 1] + 30.0, 360.);
    }
    houses.Asc  = harmonic(h, houses.Asc);
    houses.RAAC = harmonic(h, houses.RAAC);
    houses.MC   = harmonic(h, houses.MC);
    houses.RAMC = harmonic(h, houses.RAMC);

    for (PlanetId id : getPlanets(includeAsteroids, includeCentaurs)) {
        calculateHarmonic(h, planets[id]);
    }
}

uintMSet
getPrimeFactors(unsigned n)
{
    // Adapted from
    // http://www.geeksforgeeks.org/print-all-prime-factors-of-a-given-number/

    uintMSet ret;
    while (!(n & 0x1)) {
        ret.insert(2);
        n >>= 1;
    }

    for (unsigned i = 3, sn = sqrt(n); i <= sn; i = i + 2) {
        while ((n % i) == 0) {
            ret.insert(i);
            n /= i;
        }
    }

    if (n > 2) ret.insert(n);
    return ret;
}

void
getPrimeFactors(unsigned n, uintSSet& ss)
{
    ss.clear();
    for (auto f : getPrimeFactors(n)) ss.emplace(f);
}

uintMSet
getAllFactors(unsigned h)
{
    uintMSet fs;
    fs.insert(1);
    for (unsigned i = 2, m = sqrt(h); i <= m; ++i) {
        if (h % i == 0) {
            fs.insert(i);
            fs.insert(h / i);
        }
    }
    return fs;
}

void
getAllFactors(unsigned n, uintSSet& ss)
{
    ss.clear();
    for (auto f : getAllFactors(n)) ss.emplace(f);
}

void
getAllFactorsAlt(unsigned n, uintSSet& ss)
{
    getAllFactors(n, ss);
    ss.emplace(n);
}

bool
hasPlanetGroupInLowerHarmonic(const PlanetHarmonics& harmonics,
                              unsigned int           harmonic,
                              const PlanetSet&       plist)
{
    for (unsigned lower : getAllFactors(harmonic)) {
        auto hat = harmonics.find(lower);
        if (hat == harmonics.end()) continue;
#if 1
        for (auto hit : hat->second) {
            bool foundAll = true;
            for (auto pit : plist) {
                if (hit.first.count(pit) == 0) {
                    foundAll = false;
                    break;
                }
            }
            if (foundAll) {
                return true;
            }
        }
#else
        if (hat != scope.harmonics.end() && hat->second.count(plist) != 0) {
            return true;
        }
#endif
    }
    return false;
}

bool
meetsQuorum(const PlanetQueue&   curr,
            const std::set<int>& needs,
            unsigned int         quorum)
{
    std::map<int, int> counts;
    // std::map<int, bool> hasSolo;
    unsigned q = 0;
    // for (int need : needs) { counts[need] = 0; hasSolo[need] = false; }
    for (const PlanetLoc& pl : curr) {
        // if (pl.planet.notwo()) hasSolo[pl.planet.fileId()] = true;
        int n = pl.planet.isSolo() ? 1 : 2;
        q += n, counts[pl.planet.fileId()] += n;
        //++q, ++counts[pl.planet.fileId()];
    }
    if (q < quorum) return false;
    for (int need : needs) {
        if (counts[need] == 0 /*|| !hasSolo[need]*/) return false;
    }
    return true;
}

bool
outOfOrb(const PlanetLoc& a,
         const PlanetLoc& b,
         double           orb,
         bool             tightenForMidpoints = false)
{
    if (tightenForMidpoints && (!a.planet.isSolo() || !b.planet.isSolo())) {
        orb /= 10.;
    }
    return angle(a.loc, b.loc) > orb;
}

std::list<PlanetQueue>
cluster(const PlanetRange& in, qreal orb)
{
    std::list<PlanetQueue> qs;
    qs.push_back(PlanetQueue());
    std::copy(in.cbegin(), in.cend(), std::back_inserter(qs.front()));
    while (!qs.empty()) {
        for (auto q : qs) {
            auto h = q.rbegin();
            while (h != q.rend()) {
                auto t = h;
                ++t;
                while (t != q.rend() && !outOfOrb(*h, *t, orb, false)) {
                    ++t;
                }
                if (t == q.rend()) break;
                PlanetQueue qn;
                qn.splice(qn.end(), q, t.base(), h.base());
                if (q.empty()) q.swap(qn);
            }
        }
    }
    return qs;
}

typedef std::tuple<unsigned, qreal, bool> quorumOrbCleanup;
typedef std::vector<quorumOrbCleanup>     looper;
typedef std::pair<unsigned, PlanetGroups> harmonicResult;

class joiner {
    PlanetHarmonics&   hx;
    std::vector<bool>& sieve;

    typedef std::list<ChartPlanetBitmap> cpbList;
    std::map<unsigned, cpbList>          seen;

  public:
    joiner(PlanetHarmonics& hxIn, std::vector<bool>& primeSieve) :
        hx(hxIn),
        sieve(primeSieve)
    {
    }

    void operator()(uintMSet& completed, const harmonicResult& res)
    {
        completed.insert(res.first);
        if (sieve[res.first]) {
            hx[res.first] = res.second;
            for (auto g : res.second) {
                if (!g.first.containsMidPt())
                    seen[res.first].push_back(g.first);
            }
            return;
        }

        uintMSet factors = getAllFactors(res.first);
        for (auto g : res.second) {
            if (!g.first.containsMidPt()) {
                ChartPlanetBitmap cpb(g.first);
                bool              found = false;
                for (unsigned lh : factors) {
                    for (const auto& lcpb : seen[lh]) {
                        if (lcpb.contains(cpb)) {
#if 0
                            qDebug() << QString("H%1").arg(lh)
                                     << PlanetSet(lcpb).names()
                                     << "already contains"
                                     << QString("H%1").arg(res.first)
                                     << PlanetSet(cpb).names();
#endif
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                }
                if (found) continue;
                seen[res.first].push_back(cpb);
            }

            hx[res.first].insert(g);
        }
    }
};

std::vector<bool>
getPrimeSieve(unsigned top)
{
    std::vector<bool> ret(top + 1, true);
    ret[0] = false;
    for (unsigned i = 2; i <= top; ++i) {
        if (!ret[i]) continue;
        for (unsigned j = 2 * i; j <= top; j += i) {
            ret[j] = false;
        }
    }
    return ret;
}

uintMSet
getPrimes(unsigned top)
{
    uintMSet ret;
    unsigned i = 0;
    for (auto&& p : getPrimeSieve(top)) {
        if (p) ret.insert(i);
        ++i;
    }
    return ret;
}

void
findHarmonics(const ChartPlanetMap& cpm,
              PlanetHarmonics&      hx,
              const looper&         loop,
              bool                  doMidpoints = false)
{
    std::set<int> needsFiles;
    for (const ChartPlanetId& cpi : cpm.keys()) {
        needsFiles.insert(cpi.fileId());
    }

    const unsigned minQuorum = harmonicsMinQuorum();
    const unsigned maxH      = maxHarmonic();

    // Creates a prime sieve and a sequence of numbers which is has primes
    // less than or equal to the primeFactorLimit and non-primes with
    // no factors less than or equal to the primeFactorLimit.
    static std::vector<bool>  primeSieve;
    static std::set<unsigned> seq;
    static unsigned           lastMaxH = 0;
    static unsigned           lastPFL  = 0;
    if (maxH > lastMaxH || lastPFL != primeFactorLimit()) {
        std::set<unsigned> primes { 1 }, nonPrimes;
        primeSieve.assign(maxH + 1, true);
        std::vector<bool> maxFactor(maxH + 1, false);
        for (unsigned int i = 2; i <= maxH; ++i) {
            if (!primeSieve[i]) {
                if (!maxFactor[i]) nonPrimes.insert(i);
                continue;
            }
            bool beyond = i > primeFactorLimit();
            if (!beyond) primes.insert(i);
            for (unsigned int j = i + i; j <= maxH; j += i) {
                primeSieve[j] = false;
                maxFactor[j]  = beyond;
            }
        }
        seq = primes;
        seq.insert(nonPrimes.cbegin(), nonPrimes.cend());
        lastPFL = primeFactorLimit();
    } else if (maxH < lastMaxH) {
        auto zap = seq.lower_bound(maxH + 1);
        while (zap != seq.end()) {
            seq.erase(zap++);
        }
    }
    lastMaxH = maxH;

    auto computePositions =
        [&](unsigned h, PlanetRange& allOfThem, bool doMidpoints = false) {
            for (auto cpit = cpm.cbegin(); cpit != cpm.cend(); ++cpit) {
                Planet p = cpit.value();
                if (h > 1) calculateHarmonic(h, p);

                qreal loc;
                if (aspectMode == amcEcliptic) {
                    loc = p.eclipticPos.x();
                } else if (aspectMode == amcEquatorial) {
                    loc = p.equatorialPos.x();
                } else {
                    loc = p.pvPos;
                }
                allOfThem.insert(PlanetLoc(cpit.key(), loc));

                if (!doMidpoints) continue;

                for (auto cpot = std::next(cpit);
                     cpot != cpm.cend()
                     && cpit.key().fileId() == cpot.key().fileId();
                     ++cpot)
                {
                    Planet p2 = cpot.value();
                    if (h > 1) calculateHarmonic(h, p2);

                    qreal loc1 = loc;
                    qreal loc2;
                    if (aspectMode == amcEcliptic) {
                        loc2 = p2.eclipticPos.x();
                    } else if (aspectMode == amcEquatorial) {
                        loc2 = p2.equatorialPos.x();
                    } else {
                        loc2 = p2.pvPos;
                    }

                    if (loc1 > loc2) qSwap(loc1, loc2);
                    if (loc2 - loc1 > 180) loc1 += 360;
                    loc2 = fmod((loc1 + loc2) / 2, 360.);

                    allOfThem.insert(PlanetLoc(cpit.key().fileId(),
                                               cpit.key().planetId(),
                                               cpot.key().planetId(),
                                               loc2));
                    allOfThem.insert(PlanetLoc(cpit.key().fileId(),
                                               cpot.key().planetId(),
                                               cpit.key().planetId(),
                                               fmod(loc2 + 180, 360.)));
                }
            }
        };

    auto cluster = [&](PlanetGroups&           groups,
                       const quorumOrbCleanup& l,
                       const PlanetRange&      allOfThem) {
        unsigned quorum;
        qreal    orb;
        bool     cleanup;
        std::tie(quorum, orb, cleanup) = l;

        PlanetSet plist;

        // Let's see if anything wraps around...
        ChartPlanetId seen;
        bool          wrapped = false;
        PlanetQueue   current;
        PlanetLoc     last;
        if (!outOfOrb(*allOfThem.crbegin(), *allOfThem.cbegin(), orb)) {
            auto next = allOfThem.crbegin();
            do {
                wrapped = true;
                last    = *next;
                seen    = last.planet;
                current.push_front(last);
            } while (++next != allOfThem.crend()
                     && !outOfOrb(*allOfThem.cbegin(), *next, orb));
        } else {
            last = *allOfThem.cbegin();
        }
        current.push_back(*allOfThem.cbegin());
        for (auto plit = allOfThem.cbegin(); ++plit != allOfThem.cend();) {
            const auto& pl = *plit;
            // const Planet& plan = getPlanet(pl.planet);
            if (/* DISABLES CODE */ (false) && wrapped && pl.planet == seen) {
                break;
            }
            if (outOfOrb(pl, last, orb)) {
                auto next = plit;
                ++next;

                if (meetsQuorum(current, needsFiles, quorum)) {
                    groups.insert(current, minQuorum);
                }
                do {
                    current.pop_front();
                    if (current.empty()) {
                        last = pl;
                        break;
                    }
                    last = *current.cbegin();
                } while (outOfOrb(pl, last, orb));
            }
            current.push_back(pl);
        }

        if (meetsQuorum(current, needsFiles, quorum)) {
            groups.insert(current, minQuorum);
        }
    };

    std::function<harmonicResult(unsigned)> orbLoop = [&](unsigned h) {
        PlanetGroups groups;
        PlanetRange  allOfThem;
        if (doMidpoints) {
            computePositions(h, allOfThem, true);
            for (auto l : loop) {
                std::get<1>(l) /= 10;
                cluster(groups, l, allOfThem);
            }
            allOfThem.clear();
        }
        computePositions(h, allOfThem);
        for (const auto& l : loop) {
            cluster(groups, l, allOfThem);
        }
        return harmonicResult(h, groups);
    };

    using namespace QtConcurrent;

    joiner            j(hx, primeSieve);
    QFuture<uintMSet> f = /*uintSet foo =*/
        mappedReduced<uintMSet>(seq, orbLoop, j, OrderedReduce);
    f.waitForFinished();
}

void
findHarmonics(const ChartPlanetMap& cpm, PlanetHarmonics& hx)
{
    bool     doMidpoints = includeMidpoints();
    int      d       = harmonicsMinQuorum() <= harmonicsMaxQuorum() ? 1 : -1;
    unsigned num     = fabs(harmonicsMaxQuorum() - harmonicsMinQuorum()) + 1;
    qreal    orb     = harmonicsMinQOrb() * orbFactor();
    qreal    lorbMin = log2(orb);
    qreal    lorbMax = log2(harmonicsMaxQOrb() * orbFactor());
    qreal    od      = num < 2 ? 0 : pow(2, (lorbMax - lorbMin) / (num - 1));
    unsigned quorum  = unsigned(harmonicsMinQuorum());
    unsigned i       = 0;
    looper   loop;
    while (i < num) {
        bool cleanup = (++i == num) && (num > 1) && filterFew();
        loop.push_back(quorumOrbCleanup(quorum, orb, cleanup));
        quorum = unsigned(int(quorum) + d);
        orb *= od;
    }
    findHarmonics(cpm, hx, loop, doMidpoints);
    for (auto it = hx.begin(); it != hx.end();) {
        if (it->second.empty()) hx.erase(it++);
        else
            ++it;
    }
}

void
calculateBaseChartHarmonic(Horoscope& scope)
{
    scope.houses  = scope.housesOrig;
    scope.planets = scope.planetsOrig;

    const InputData& input(scope.inputData);
    if (scope.harmonic != 1.0 && aspectMode != amcGreatCircle) {
        calculateHarmonic(scope.harmonic, scope.houses, scope.planets);
    } else {
        findPlanetStarConfigs(scope.planets, scope.stars);
    }

    scope.sun       = scope.planets[Planet_Sun];
    scope.moon      = scope.planets[Planet_Moon];
    scope.mercury   = scope.planets[Planet_Mercury];
    scope.venus     = scope.planets[Planet_Venus];
    scope.mars      = scope.planets[Planet_Mars];
    scope.jupiter   = scope.planets[Planet_Jupiter];
    scope.saturn    = scope.planets[Planet_Saturn];
    scope.uranus    = scope.planets[Planet_Uranus];
    scope.neptune   = scope.planets[Planet_Neptune];
    scope.pluto     = scope.planets[Planet_Pluto];
    scope.northNode = scope.planets[Planet_NorthNode];

    for (PlanetId id : scope.planets.keys()) {
        if (id >= Planet_Sun) {
            scope.planets[id].power =
                calculatePlanetPower(scope.planets[id], scope);
        }
    }
}

/*
An implementation of an improved & simplified Brent's Method.
Calculates root of a function f(x) in the interval [a,b].
Zhang, Z. (2011). An Improvement to the Brent’s Method. International Journal of
Experimental Algorithms (IJEA), (2), 21–26. Retrieved from
http://www.cscjournals.org/csc/manuscript/Journals/IJEA/volume2/Issue1/IJEA-7.pdf
[This link appears to be invalid, though the article is retrievable by googling
the title.]

I've adapted it with the further corrections from Steven Stage, whose analysis
seems to indicate that the method is not quite an improvement in practical
terms, but is clearly a simplification. See:

https://www.cscjournals.org/manuscript/Journals/IJEA/Volume4/Issue1/IJEA-33.pdf

f -> Functor to be evaluated
lo -> Starting point of interval
hi -> Ending point of interval
result -> This will contain the root when the functiuon is complete
tol -> tolerance . Set to a low value like 1e-6
Returns bool indicating success or failure
*/

template <typename F>
bool
brentZhangStage(F&      f,
                double  lo,
                double  hi,
                double  f_lo,
                double  f_hi,
                double& result,
                double  tol = 1e-9)
{
    // Root not bound by the given guesses?
    if (f_lo * f_hi >= 0) return false;

    double s, f_s;
    do {
        double c   = (lo + hi) / 2;
        double f_c = f(c);
        if (fabs(f_lo - f_c) > tol && fabs(f_hi - f_c) > tol) {
            // f(a)!=f(c) and f(b)!=f(c)
            // Inverse quadratic interpolation
            s = lo * f_hi * f_c / ((f_lo - f_hi) * (f_lo - f_c))
                + hi * f_lo * f_c / ((f_hi - f_lo) * (f_hi - f_c))
                + c * f_lo * f_hi / ((f_c - f_lo) * (f_c - f_hi));
            if (s < lo || s > hi) {
                // According to Stage, s can go awry in this
                // calculation (i.e., exceed lo-hi bounds),
                // so we need to get it back in
                // line one of three ways, of which
                s = c; // is the simplest option
            }
        } else {
            // Secant rule
            s = hi - f_hi * (hi - lo) / (f_hi - f_lo);
        }
        f_s = f(s);

        if (c > s) qSwap(s, c);

        if (f_c * f_s < 0) {
            f_lo = f(lo = c);
            f_hi = f(hi = s);
        } else if (f_lo * f_c < 0) {
            f_hi = f(hi = c);
        } else {
            f_lo = f(lo = s);
        }
    } while (f_hi != 0 && f_lo != 0
             && fabs(hi - lo) > tol); // Convergence conditions

    result = hi; // either hi or lo could be returned,
    // as we are now at tolerated convergence.

    return true;
}

template <typename F>
bool
brentZhangStage(F& f, double lo, double hi, double& result, double tol = 1e-9)
{
    if (fabs(hi - lo) <= tol) return false;

    if (lo > hi) qSwap(lo, hi);

    double f_lo = f(lo);
    double f_hi = f(hi);
    return brentZhangStage(f, lo, hi, f_lo, f_hi, result, tol);
}

// Adapted from the Algol60 code in "Algorithms for Minimization
// without Derivatives" by Richard P. Brent (2002).
// Comments inline generally copied verbatim. Yes, it uses gotos!
// I don't quite understand the distinction between err and tol,
// or what 'm' means. (The latter seems to help in bracketing: the
// higher the number, the more likely it can find a tolerable
// solution.)

template <typename F>
double
brentGlobalMin(F       f,
               double  lo,
               double  hi,
               double  guess,
               double  m,
               double  err,
               double  tol,
               double& x)
{
    const auto macheps = std::numeric_limits<double>::epsilon();
    int        k;
    double a0, a2, a3, d0, d1, d2, h, m2, p, q, qs, r, s, y, y0, y1, y2, y3, yb,
        z0, z1, z2;

    x = a0 = hi;
    a2     = lo;
    yb = y0 = f(hi);
    y = y2 = f(lo);
    if (y0 < y) y = y0;
    else
        x = lo;

    if ((m > 0) && (lo < hi)) {
        // nontrivial case
        m2 = 0.5 * (1 + 16 * macheps) * m;
        if ((guess <= lo) || (guess >= hi)) guess = 0.5 * (lo + hi);
        y1 = f(guess);
        k  = 3;
        d0 = a2 - guess;
        h  = 9 / 11;

        if (y1 < y) {
            x = guess;
            y = y1;
        }

    // main loop
    next:
        d1 = a2 - a0;
        d2 = guess - a0;
        z2 = hi - a2;
        z0 = y2 - y1;
        z1 = y2 - y0;
        p = r = d1 * d1 * z0 - d0 * d0 * z1;
        q = qs = 2 * (d0 * z1 - d1 * z0);

        // try to find lower value of f using quadratic interpolation
        if ((k > 100000) && (y < y2)) goto skip;

    retry:
        if (q * (r * (yb - y2) + z2 * q * ((y2 - y) + tol))
            < z2 * m2 * r * (z2 * q - r))
        {
            a3 = a2 + r / q;
            y3 = f(a3);
            if (y3 < y) x = a3, y = y3;
        }

        // with probability about 0.1 do a random search for a lower
        // value of f. Any reasonable random number generator can be
        // used in place of the one here (it need not be very good).

    skip:
        k = 1611 * k;
        k = k - 1048576 * (k / 1048576);
        q = 1;
        r = (hi - lo) * (k / 100000);

        if (r < z2) goto retry;

        // prepare to step as far as possible
        r = m2 * d0 * d1 * d2;
        s = sqrt(((y2 - y) + tol) / m2);
        h = 0.5 * (1 + h);
        p = h * (p + 2 * r * s);
        q = r + 0.5 * qs;
        r = -0.5 * (d0 + (z0 + 2.01 * err) / (d0 * m2));
        r = a2 + ((r < s || d0 < 0) ? s : r);

        // it is safe to step to r, but we may try to step further
        a3 = (p * q > 0) ? a2 + p / q : r;

    inner:
        if (a3 < r) a3 = r;
        if (a3 > hi) {
            a3 = hi;
            y3 = yb;
        } else
            y3 = f(a3);
        if (y3 < y) {
            x = a3;
            y = y3;
        }
        d0 = a3 - a2;

        if (a3 > r) {
            // inspect the parabolic lower bound on f in (a2,a3)
            p = 2 * (y2 - y3) / (m * d0);
            if ((fabs(p) < (1 + 9 * macheps) * d0)
                && (0.5 * m2 * (d0 * d0 + p * p)
                    > (y2 - y) + (y3 - y) + 2 * tol))
            {
                // halve the step and try again
                a3 = 0.5 * (a2 + a3);
                h  = 0.9 * h;
                goto inner;
            }
        }
        if (a3 < hi) {
            // prepare for the next step
            a0    = guess;
            guess = a2;
            a2    = a3;
            y0    = y1;
            y1    = y2;
            y2    = y3;
            goto next;
        }
    }
    return y;
}

QDateTime
dateTimeFromJulian(double jd)
{
    int32  y, d, m;
    int32  hr, min, sec;
    double dsec;
    swe_jdut1_to_utc(jd, SE_GREG_CAL, &y, &m, &d, &hr, &min, &dsec);
    sec      = dsec;
    int msec = int((dsec - double(sec)) * 1000.0);
    return QDateTime(QDate(y, m, d), QTime(hr, min, sec, msec), QTimeZone::UTC);
}

namespace
{
static bool       s_quiet  = true;
thread_local bool st_quiet = s_quiet;
} // namespace

struct calcPos {
    PlanetProfile& poses;
    uintmax_t      _iter = 0;

    calcPos(PlanetProfile& p) : poses(p) { }

    qreal operator()(double jd)
    {
        ++_iter;
        auto ret = poses.computePos(jd);
        if (!st_quiet) {
            QDateTime dt(dateTimeFromJulian(jd));
            qDebug() << "calc iter:" << dtToString(dt) << "Ret:" << ret;
        }
        return ret;
    }

    uintmax_t& count() { return _iter; }
};

struct calcSpd {
    PlanetProfile& poses;
    calcSpd(PlanetProfile& p) : poses(p) { }

    qreal operator()(double jd)
    {
        poses.computePos(jd);
        if (!st_quiet) {
            qDebug() << "calc iter:" << dtToString(dateTimeFromJulian(jd))
                     << "Ret:" << poses.speed();
        }
        return poses.speed();
    }
};

typedef std::pair<qreal, qreal> posSpd;
struct calcPosSpd {
    PlanetProfile& poses;
    calcPosSpd(PlanetProfile& p) : poses(p) { }

    posSpd operator()(double jd)
    {
        auto pos = poses.computePos(jd);
        auto ret = std::make_pair(pos, poses.speed());

        if (!st_quiet) {
            QDateTime dt(dateTimeFromJulian(jd));
            qDebug() << "ncalc iter:" << dtToString(dt) << "Ret:" << ret;
        }

        return ret;
    }
};

struct calcSpread {
    PlanetProfile& poses;
    int            m = 100;

    calcSpread(PlanetProfile& p) : poses(p) { }

    qreal operator()(double jd)
    {
#if 1
        auto ret = poses.computeSpread(jd);
#else
        for (auto& pos : poses) pos->operator()(jd);
        auto ret = getSpread(poses);
#endif

        QDateTime dt(dateTimeFromJulian(jd));
        qDebug() << "spread iter:" << dtToString(dt) << "Ret:" << ret;
        return ret;
    }
};

struct calcLoop {
    calcPos    cpos;
    calcSpd    cspd;
    calcPosSpd ncpos;
    calcSpread csprd;

    PlanetProfile& poses;
    double&        jd;

    static constexpr double tol    = 5e-08;
    static constexpr int    digits = std::numeric_limits<double>::digits;

    calcLoop(PlanetProfile& ps, double& jdate) :
        cpos(ps),
        cspd(ps),
        ncpos(ps),
        csprd(ps),
        poses(ps),
        jd(jdate)
    {
    }

#if 1 // old version
    bool operator()(double& begin,
                    double  end,
                    double  span,
                    double  flo,
                    double  splo,
                    bool    cont)
    {
        qDebug() << "calcLoop: begin" << begin << "end" << end << "span" << span
                 << "flo" << flo << "splo" << splo;

        bool   done = false;
        double fhi, sphi;
        for (double& jdc = begin; !done && (cont || jdc < end);
             splo = sphi, flo = fhi, jdc += span)
        {
            fhi  = cpos(jdc + span);
            sphi = poses.speed();
            if ((done = (fabs(poses.loc) <= tol))) {
                qDebug() << "  done by span convergence";
                jd = jdc + span / 2;
                continue;
            }
            if (span <= tol) {
                qDebug() << "  zeno's paradox";
                return false;
            }
            if (sgn(flo) == sgn(fhi)) {
                qDebug() << "same sign";
                continue;
            }
            if (abs(fhi) >= 170. && abs(flo) >= 170.) {
                qDebug() << "flo and fhi >= 170";
                continue;
            }
            qDebug() << "sgn(flo)=" << sgn(flo) << " sgn(splo)=" << sgn(splo)
                     << " sgn(fhi)=" << sgn(fhi) << " sgn(sphi)=" << sgn(sphi);
            if (sgn(flo) != sgn(splo) || sgn(fhi) != -sgn(sphi)) {
#if 0
                done = brentZhangStage(cpos, jdc,jdc+span, flo, fhi, jd);
                if (done) qDebug() << "  done by brent";
#else
                double guess =
                    jdc + (fabs(flo) / (fabs(flo) + fabs(fhi))) * span;
                uintmax_t iter = 20;
                try {
                    jd = newton_raphson_iterate(ncpos,
                                                guess,
                                                jdc,
                                                jdc + span,
                                                digits,
                                                iter);

                    done = fabs(poses[1]->loc - poses[0]->loc) <= tol
                           || span < tol;
                    if (done) qDebug() << "  done by newton";
                }
                catch (...) {
                }
#endif
            }
            double b = jdc;
            if (!done) {
                done = operator()(b, jdc + span, span / 4, flo, splo, false);
            }
        }
        if (!done) qDebug() << "  ran out clock";
        return done;
    }
#endif

    template <typename T>
    void calc(double j, T& ret);

    bool signsEqual(const posSpd& a, const posSpd& b) const
    {
        return sgn(a.first) == sgn(b.first);
    }

    bool signsEqual(qreal a, qreal b) const { return sgn(a) == sgn(b); }

    bool speedSignsEqual(const posSpd& a, const posSpd& b) const
    {
        return sgn(a.second) == sgn(b.second);
    }

    bool speedSignsEqual(qreal, qreal) const { return true; }

    bool longDistance(const posSpd& a, const posSpd& b) const
    {
        return abs(a.first) >= 170. && abs(b.first) > 170.;
    }

    bool longDistance(qreal, qreal) { return false; }

    template <typename T>
    bool doIterativeCalc(double& jd, double jlo, double jhi, T lo, T hi);

    template <typename T>
    bool operator()(double& begin, double end, double span, T lo, bool cont)
    {
        qDebug() << "calcLoop: begin" << dtToString(dateTimeFromJulian(begin))
                 << "end" << dtToString(dateTimeFromJulian(end)) << "span"
                 << span;

        bool done = false;
        T    hi;
        for (double& jdc = begin; !done && (cont || jdc < end);
             lo          = hi, jdc += span)
        {
            calc(jdc + span, hi);
            if (poses.needsFindMinimalSpread()) {
                if (doIterativeCalc(jd, jdc, jdc + span, lo, hi)) {
                    modalize<int> precise(csprd.m, 1000);
                    done = doIterativeCalc(jd, jdc, jdc + span, lo, hi);
                }
            } else {
                if ((done = (fabs(poses.loc) <= tol))) {
                    qDebug() << "  done by span convergence";
                    jd = jdc + span / 2;
                    continue;
                }
                if (span <= tol) {
                    qDebug() << "  zeno's paradox";
                    return false;
                }
                if (signsEqual(lo, hi)) {
                    qDebug() << "same sign";
                    continue;
                }
                if (longDistance(lo, hi)) {
                    qDebug() << "flo and fhi >= 170";
                    continue;
                }
                // if (sgn(flo)!=sgn(splo) || sgn(fhi)!=-sgn(sphi)) {
                done = doIterativeCalc(jd, jdc, jdc + span, lo, hi);
                //}
                double b = jdc;
                if (!done) {
                    done = operator()(b, jdc + span, span / 4, lo, false);
                }
            }
        }
        if (!done) qDebug() << "  ran out clock";
        return done;
    }
};

template <>
inline void
calcLoop::calc<posSpd>(double j, posSpd& ret)
{
    ret = ncpos(j);
}

template <>
inline void
calcLoop::calc<qreal>(double j, qreal& ret)
{
    ret = cspd(j);
}

template <>
inline bool
calcLoop::doIterativeCalc<qreal>(double& jd,
                                 double  jlo,
                                 double  jhi,
                                 qreal   lo,
                                 qreal   hi)
{
    bool done = brentZhangStage(cspd, jlo, jhi, lo, hi, jd);
    if (done) qDebug() << "  done by brent";
    return done;
}

template <>
inline bool
calcLoop::doIterativeCalc<posSpd>(double& jd,
                                  double  jlo,
                                  double  jhi,
                                  posSpd  lo,
                                  posSpd  hi)
{
    double span = jhi - jlo;
    double g =
        jlo + (fabs(lo.first) / (fabs(lo.first) + fabs(hi.first))) * span;

    uintmax_t iter = 20;
    try {
        bool done = false;
        if (poses.needsFindMinimalSpread()) {
            constexpr auto tol = double(std::numeric_limits<float>::epsilon());
            brentGlobalMin(csprd,
                           jlo,
                           jhi,
                           jlo / 2. + jhi / 2.,
                           csprd.m,
                           .0000001 /*err*/,
                           tol,
                           jd);
            if (csprd.poses.computeSpread(jd) <= harmonicsMinQOrb()) {
                done = true;
                qDebug() << "  done by brentGlobalMin";
            }
        } else {
            jd   = newton_raphson_iterate(ncpos, g, jlo, jhi, digits, iter);
            done = fabs(poses[1]->loc - poses[0]->loc) <= tol || span < tol;
            if (done) qDebug() << "  done by newton";
        }
        return done;
    }
    catch (...) {
        return false;
    }
}

void
calculateOrbAndSpan(const PlanetProfile& poses,
                    const InputData&     locale,
                    double               harmonic,
                    double&              orb,
                    double&              horb,
                    double&              span)
{
    if (aspectMode == amcPrimeVertical) horb = .5;
    float speed = poses.defaultSpeed();
    orb         = 360 / speed / harmonic;
    horb        = orb / 1.8;

    // auto plid = poses.back()->planet.planetId();
    if (true /*plid == Planet_Sun || plid==Planet_Moon*/) span = orb / 4;
    else
        span = 1 / speed;

    qDebug() << poses[0]->description() << "half-orbit" << horb << "days";
}

QDateTime
calculateClosestTime(PlanetProfile&   poses,
                     const InputData& locale,
                     double           harmonic)
{

    double jdIn = getJulianDate(locale.GMT());

    double   jd = jdIn;
    calcLoop looper(poses, jd);

    double orb, horb, span;
    calculateOrbAndSpan(poses, locale, harmonic, orb, horb, span);

    double begin = jdIn - orb / 2;
    double end   = begin + horb * 2;

    calcPos cpos(poses);
    double  flo  = cpos(begin);
    double  splo = poses.speed();

    // if (looper(begin,end,span,flo,true))
    if (looper(begin, end, span, flo, splo, true /*cont*/))
        return dateTimeFromJulian(jd);

    return locale.GMT();
}

QList<QDateTime>
quotidianSearch(PlanetProfile&   poses,
                const InputData& locale,
                const QDateTime& endDT,
                double           span /*= 1.0*/,
                bool             forceMin)
{
    modalize<bool> mum(st_quiet, true);

    double jd1 = getJulianDate(locale.GMT());
    double jd2 = getJulianDate(endDT);

    poses.setForceMinimize(forceMin);
    if (poses.needsFindMinimalSpread()) span *= 2.;
    else
        span /= 4.;

#if 0
    if (poses.needsFindMinimalSpread()) {
#if 1
        calcSpread fsprd(poses);
#else
        auto fsprd = [&] (double j) {
            auto ret = poses.computeSpread(j);
            auto dt = dateTimeFromJulian(j);
            qDebug() << "spreadIter:" << dtToString(dt) << "Ret:" << ret;
            return ret;
        };
#endif

        double x;
        constexpr auto tol = double(std::numeric_limits<float>::epsilon());
        brentGlobalMin(fsprd, jd1, jd2, jd1/2.+jd2/2.,
                            1/*m*/, .0000001/*err*/, tol, x);
        auto ret = QList<QDateTime>() << dateTimeFromJulian(x);
        return ret;
    }
#endif

    double           jd {};
    calcLoop         looper(poses, jd);
    QList<QDateTime> ret;
    auto             loop = [&](auto lo) {
        // modalize<int> precise(looper.csprd.m,1000);
        looper.calc(jd1, lo);
        do {
            if (looper(jd1, jd2, span, lo, false)) {
                auto dt = dateTimeFromJulian(jd);
                if (poses.needsFindMinimalSpread() && !ret.isEmpty()
                    && qAbs(dt.secsTo(ret.back())) < 86400)
                {
                    auto ugh = qAbs(dt.secsTo(ret.back()));
                    qDebug() << ugh << "Let's scrunch up" << dtToString(dt)
                             << "and" << dtToString(ret.back());
                    // XXX goofy hack to ignore adjacent values
                    auto hi  = lo;
                    auto jda = jd - .5;
                    looper.calc(jda, lo);
                    auto jdb = jd + .5;
                    looper.calc(jdb, hi);
                    modalize<int> precise(looper.csprd.m, 1000);
                    bool found = looper.doIterativeCalc(jd, jda, jdb, lo, hi);
                    if (found) {
                        ret.pop_back();
                        dt = dateTimeFromJulian(jd);
                    }
                }
                ret << dt;

                qDebug() << "** Finding:" << dtToString(dt);
                looper.calc(jd1, lo);
            }
        } while (jd1 < jd2);
    };

    if (poses.size() == 1) {
        auto l = *poses.begin();
        // looking for stations...
        if (l->inMotion()) {
            qreal lo {};
            loop(lo);
        }
    } else {
        posSpd lo;
        loop(lo);
    }
    return ret;
}

// ============================================================================
// PSSR (Progressed Sidereal Solar Return) Functions
// ============================================================================

double
calculateRAMS(const QDateTime& dt, bool useMeanSun)
{
    // Calculate Right Ascension of Mean (or Apparent) Sun
    double       jd  = getJulianDate(dt, false); // UT
    char         errStr[256];
    double       xx[6];
    unsigned int flags = SEFLG_SWIEPH | SEFLG_EQUATORIAL;

    if (useMeanSun) {
        // For Mean Sun: use true position (geometric) + no nutation
        flags |= SEFLG_TRUEPOS | SEFLG_NONUT;
    }
    // For Apparent Sun: use default (apparent position with nutation)

    int ret = swe_calc_ut(jd, SE_SUN, flags, xx, errStr);
    if (ret < 0) {
        qWarning() << "calculateRAMS error:" << errStr;
        return 0.0;
    }

    return xx[0]; // Right Ascension in degrees
}

double
calculateAnniversarySecond(const Houses& return1, const Houses& return2)
{
    // Anniversary Second = (RAMC₂ - RAMC₁ + 24h) / (Mean Sun annual travel)
    // where Mean Sun travels approximately 24h 00m 03s per year

    double ramc1 = return1.RAMC;
    double ramc2 = return2.RAMC;

    // Calculate the difference, accounting for the full 24-hour cycle
    double ramcDiff = swe_difdegn(ramc2, ramc1); // Normalized difference
    if (ramcDiff < 0)
        ramcDiff += 360.0;

    // Add 24 hours (360 degrees) to get the actual advance
    double actualAdvance = ramcDiff + 360.0;

    // Convert to hours (360 degrees = 24 hours)
    double actualAdvanceHours = (actualAdvance / 360.0) * 24.0;

    // Mean Sun's annual travel: approximately 24h 00m 03s = 24.000833 hours
    // This accounts for precession (~50.29" per year ≈ 3.35 seconds per year)
    double meanSunAnnualTravel = 24.000833; // hours

    // Anniversary second
    double anniversarySecond = actualAdvanceHours / meanSunAnnualTravel;

    // Helper lambda to convert degrees to sidereal time HH:MM:SS
    auto degToST = [](double deg) {
        double hours = deg / 15.0;
        int h = (int)hours;
        int m = (int)((hours - h) * 60.0);
        double s = (((hours - h) * 60.0 - m) * 60.0);
        if (h >= 24) h -= 24;
        if (h < 0) h += 24;
        return QString("%1h %2m %3s")
                              .arg(h, 2, 10, QChar('0'))
                              .arg(m, 2, 10, QChar('0'))
                              .arg(s, 0, 'f', 3);
    };

    qDebug() << "=== calculateAnniversarySecond ===";
    qDebug() << "  Return 1 RAMC:" << ramc1 << "deg =" << qPrintable(degToST(ramc1));
    qDebug() << "  Return 2 RAMC:" << ramc2 << "deg =" << qPrintable(degToST(ramc2));
    qDebug() << "  RAMC Diff:" << ramcDiff << "deg =" << qPrintable(degToST(ramcDiff));
    qDebug() << "  Actual Advance (Diff + 360°):" << actualAdvance << "deg =" << qPrintable(degToST(actualAdvance));
    qDebug() << "  Actual Advance Hours:" << actualAdvanceHours << "hours";
    qDebug() << "  Mean Sun Annual Travel:" << meanSunAnnualTravel << "hours";
    qDebug() << "  Anniversary Second:" << anniversarySecond;

    return anniversarySecond;
}

double
calculatePSSRRAMC(const Houses&    returnHouses,
                  const QDateTime& returnTime,
                  const QDateTime& eventTime,
                  double           anniversarySecond,
                  bool             useMeanSun)
{
    // PSSR RAMC = Return RAMC + (Event RAMS - Return RAMS) × Anniversary Second

    double returnRAMS = calculateRAMS(returnTime, useMeanSun);
    double eventRAMS  = calculateRAMS(eventTime, useMeanSun);

    // Calculate elapsed Mean Sun between return and event
    double elapsedRAMS = swe_difdeg2n(eventRAMS, returnRAMS); // Signed difference

    // Convert to hours (degrees to hours: 360° = 24h)
    double elapsedHours = (elapsedRAMS / 360.0) * 24.0;

    // Multiply by anniversary second to get PSSR advancement
    double pssrAdvanceHours = elapsedHours * anniversarySecond;

    // Convert back to degrees
    double pssrAdvanceDegrees = (pssrAdvanceHours / 24.0) * 360.0;

    // Add to return RAMC
    double pssrRAMC = swe_degnorm(returnHouses.RAMC + pssrAdvanceDegrees);

    return pssrRAMC;
}

QDateTime
calculateReturnTime(PlanetId         id,
                    const InputData& native,
                    const InputData& locale,
                    double           harmonic)
{
    modalize<bool> mum(st_quiet, true);

    PlanetProfile poses;
    poses.push_back(new NatalLoc(id, native));
    poses.push_back(new TransitPosition(id, locale));

    return calculateClosestTime(poses, locale, harmonic);
}

PSSRContext
calculatePSSRContext(const Horoscope& returnChart, bool useMeanSun)
{
    PSSRContext ctx;
    ctx.useMeanSun = useMeanSun;
    
    // Store return chart info
    ctx.returnTime = returnChart.inputData.GMT();
    ctx.returnRAMC = returnChart.houses.RAMC;
    ctx.returnRAMS = calculateRAMS(ctx.returnTime, useMeanSun);
    
    // Calculate next return to get anniversary second
    // For solar return: find when Sun returns to its current position one year later
    const Planet* currentSun = nullptr;
    for (const Planet& p : returnChart.planets) {
        if (p.id == Planet_Sun) {
            currentSun = &p;
            break;
        }
    }
    
    if (!currentSun) {
        qDebug() << "calculatePSSRContext: Could not find Sun in return chart";
        return ctx; // Invalid context
    }
    
    double targetSunLon = currentSun->eclipticPos.x();
    double sunSpeed = currentSun->eclipticSpeed.x();
    
    if (sunSpeed <= 0.0) {
        sunSpeed = 0.9856; // Approximate mean motion of Sun in degrees/day
    }
    
    // Estimate next return time (approximately 365.25 days)
    double estimatedDays = 360.0 / sunSpeed;
    double currentJd = getJulianDate(ctx.returnTime);
    double targetJd = currentJd + estimatedDays;
    
    // Newton-Raphson iteration to find exact return time
    InputData tempInput = returnChart.inputData;
    Houses tempHouses = returnChart.houses;
    
    for (int iter = 0; iter < 10; iter++) {
        tempInput.setGMT(dateTimeFromJulian(targetJd));
        Planet testSun = calculatePlanet(Planet_Sun, tempInput, tempHouses, 
                                        getZodiac(tempInput.zodiac()));
        
        double sunLon = testSun.eclipticPos.x();
        double speed = testSun.eclipticSpeed.x();
        double diff = swe_difdeg2n(sunLon, targetSunLon);
        
        if (qAbs(diff) < 0.00001) break;
        
        if (speed > 0.0) {
            targetJd -= diff / speed;
        } else {
            qDebug() << "calculatePSSRContext: Invalid speed in iteration";
            return ctx; // Invalid context
        }
    }
    
    QDateTime nextReturnTime = dateTimeFromJulian(targetJd);
    
    // Calculate houses for next return
    InputData nextReturnInput = returnChart.inputData;
    nextReturnInput.setGMT(nextReturnTime);
    Houses nextReturnHouses = calculateHouses(nextReturnInput);
    
    // Calculate anniversary second
    ctx.anniversarySecond = calculateAnniversarySecond(returnChart.houses, 
                                                       nextReturnHouses);
    ctx.isValid = true;
    
    qDebug() << "calculatePSSRContext: Anniversary Second =" << ctx.anniversarySecond;
    
    return ctx;
}

QDateTime
calculateAngularDate(const QDateTime&   radixTime,
                     const QDateTime&   angleTime,
                     double             planetRA,
                     double             angleRA,
                     const PSSRContext* pssrCtx)
{
    if (pssrCtx && pssrCtx->isValid) {
        // PSSR mode: Find when Sun reaches the RAMS needed for planet to hit angle
        
        // Step 1: Calculate what RAMC is needed for planet to be on this angle
        double targetRAMC = angleRA; // Simplified: angle RA is the target RAMC
        
        // Step 2: Calculate how much RAMC differs from return
        // For converse (angleTime < radixTime), this will be negative (backwards)
        // For direct (angleTime > radixTime), this will be positive (forwards)
        double ramcDiff = swe_difdeg2n(targetRAMC, pssrCtx->returnRAMC);
        
        // For direct events, ensure we go forward
        if (angleTime >= radixTime && ramcDiff < 0.0) {
            ramcDiff += 360.0;
        }
        // For converse events, ensure we go backward
        else if (angleTime < radixTime && ramcDiff > 0.0) {
            ramcDiff -= 360.0;
        }
        
        // Step 3: Calculate target RAMS using anniversary second
        // targetRAMS = returnRAMS + ramcDiff / anniversarySecond
        double delta = ramcDiff / pssrCtx->anniversarySecond;
        double targetRAMS = swe_degnorm(pssrCtx->returnRAMS + delta);
        
        // Step 4: Find when the Sun reaches this RA position (past or future)
        // Initial estimate
        double estimatedDays = (delta / 360.0) * 365.25;
        double currentJd = getJulianDate(pssrCtx->returnTime);
        double targetJd = currentJd + estimatedDays; // Can be negative for converse
        
        // Newton-Raphson iteration to find exact time when RAMS = targetRAMS
        for (int iter = 0; iter < 10; iter++) {
            QDateTime testTime = dateTimeFromJulian(targetJd);
            double testRAMS = calculateRAMS(testTime, pssrCtx->useMeanSun);
            
            // Calculate difference (we want testRAMS to equal targetRAMS)
            double diff = swe_difdeg2n(testRAMS, targetRAMS);
            
            if (qAbs(diff) < 0.00001) {
                break;
            }
            
            // Newton-Raphson step: adjust JD by diff/speed
            // Sun advances ~0.9856 degrees per day in RA
            double sunRASpeed = 360.0 / 365.25; // degrees per day
            targetJd -= diff / sunRASpeed;
        }
        
        // Step 5: Calculate the time difference and apply it from radix
        QDateTime sunTime = dateTimeFromJulian(targetJd);
        qint64 offsetSeconds = pssrCtx->returnTime.secsTo(sunTime);
        
        // Apply absolute offset forward from radix (all results in future like PD)
        offsetSeconds = qAbs(offsetSeconds);
        QDateTime result = radixTime.addSecs(offsetSeconds);
        
        return result;
        
    } else {
        // Primary Direction mode - original formula
        // angleTime is when planet crosses angle (from angleTransit array)
        // The original formula used time difference in seconds, scaled by (365.25/240)
        // This appears to convert sidereal time difference to solar days
        double dist = qAbs(radixTime.secsTo(angleTime));
        int dayDiff = dist * (365.25 / 240.0);
        return radixTime.addDays(dayDiff);
    }
}

Horoscope
calculateAll(const InputData& input)
{
    Horoscope scope;
    scope.inputData = input;

    // For progressed charts, we need to calculate the progressed date
    // using secondary progressions (1 day = 1 year)
    double jd = getJulianDate(input.GMT());

    // Determine which InputData to use for planet/star calculations
    const InputData* calcInput = &input;
    InputData        progInput; // Will be used if this is a progressed chart

    if (input.hasBaseChart()) {
        double baseJd    = getJulianDate(input.baseGMT());
        double yearsDiff = (jd - baseJd) / 365.25;
        double progJd    = baseJd + yearsDiff; // 1 day per year

        // Create a temporary InputData for the progressed calculation date
        progInput = input;
        progInput.setGMT(dateTimeFromJulian(progJd));

        // HOUSE PROGRESSION METHOD: Solar Arc to MC (Traditional)
        //
        // Traditional secondary progressions use "solar arc" for houses:
        // 1. Calculate natal Sun position
        // 2. Calculate progressed Sun position (X days after birth where X =
        // years lived)
        // 3. Solar arc = Progressed Sun longitude - Natal Sun longitude
        // 4. Progressed MC = Natal MC + Solar arc
        // 5. Calculate houses from progressed MC at birth location (but using
        // progressed obliquity)
        //
        // This is the most commonly used method in traditional astrology.
        // The philosophy: House cusps (angles) progress at the same rate as the
        // Sun

        // Calculate natal chart to get natal Sun and natal MC
        // Create a clean natal InputData without base chart reference
        InputData natalInput = input;
        natalInput.setGMT(input.baseGMT()); // Use the natal date
        natalInput.clearBaseChart(); // Clear base chart so it's calculated as a
                                     // standalone natal chart

        Houses natalHouses = calculateHouses(natalInput);
        double natalSunLon = 0.0;
        {
            double       eps      = 0.0;
            unsigned int flags    = 0;
            double       ablong   = 0.0;
            Planet       natalSun = calculatePlanet(Planet_Sun,
                                              natalInput,
                                              natalHouses,
                                              getZodiac(natalInput.zodiac()));
            natalSunLon           = natalSun.eclipticPos.x();
        }

        // Calculate progressed Sun position
        // Clear base chart so progInput is calculated as standalone (not
        // recursively progressed!)
        progInput.clearBaseChart();

        double progSunLon = 0.0;
        {
            double       eps    = 0.0;
            unsigned int flags  = 0;
            double       ablong = 0.0;
            Houses       tempHouses; // Not used, but needed for function call
            Planet       progSun = calculatePlanet(Planet_Sun,
                                             progInput,
                                             tempHouses,
                                             getZodiac(progInput.zodiac()));
            progSunLon           = progSun.eclipticPos.x();
        }

        // Calculate solar arc
        double solarArc = swe_difdegn(progSunLon, natalSunLon);

        // Apply solar arc to natal MC to get progressed MC
        double progressedMC = swe_degnorm(natalHouses.MC + solarArc);

        // Calculate progressed houses using the progressed MC and progressed
        // obliquity
        scope.houses = calculateHouses(progInput, progressedMC);
        scope.zodiac = getZodiac(progInput.zodiac());

        // Use progInput for planet calculations
        calcInput = &progInput;
    } else {
        // Normal (non-progressed) chart calculation
        scope.houses = calculateHouses(input);
        scope.zodiac = getZodiac(input.zodiac());
    }

    // Calculate planets and stars (common code for both progressed and
    // non-progressed)
    for (PlanetId id : getPlanets(true, true)) {
        if (id == Planet_Asc) {
            Planet asc = Data::getPlanet(id);
            asc.eclipticPos.setX(scope.houses.Asc);
            asc.equatorialPos.setX(scope.houses.RAAC);
            asc.pvPos         = 0;
            scope.planets[id] = asc;
        } else if (id == Planet_Desc) {
            Planet desc = Data::getPlanet(id);
            desc.eclipticPos.setX(swe_degnorm(180. + scope.houses.Asc));
            desc.equatorialPos.setX(swe_degnorm(180. + scope.houses.RAAC));
            desc.pvPos        = 180;
            scope.planets[id] = desc;
        } else if (id == Planet_MC) {
            Planet mc = Data::getPlanet(id);
            mc.eclipticPos.setX(scope.houses.MC);
            mc.equatorialPos.setX(scope.houses.RAMC);
            mc.pvPos          = 270;
            scope.planets[id] = mc;
        } else if (id == Planet_IC) {
            Planet ic = Data::getPlanet(id);
            ic.eclipticPos.setX(swe_degnorm(180. + scope.houses.MC));
            ic.equatorialPos.setX(swe_degnorm(180. + scope.houses.RAMC));
            ic.pvPos          = 90;
            scope.planets[id] = ic;
        } else if (id > Planet_Asc && id <= House_12) {
            Planet hc = Data::getPlanet(id);
            hc.eclipticPos.setX(scope.houses.cusp[id - Planet_Asc]);
            hc.equatorialPos.setX(scope.houses.cusp[id - Planet_Asc]); // XXX
            hc.pvPos          = 30 * (id - Planet_Asc);
            hc.house          = id - Planet_Asc;
            scope.planets[id] = hc;
        } else {
            scope.planets[id] =
                calculatePlanet(id, *calcInput, scope.houses, scope.zodiac);
        }
    }

    for (const QString& name : std::as_const(getStars())) {
        scope.stars[name.toStdString()] =
            calculateStar(name, *calcInput, scope.houses, scope.zodiac);
    }

    if (scope.planets.contains(-1)) {
        qDebug() << "Wha?";
    }

    scope.housesOrig  = scope.houses;
    scope.planetsOrig = scope.planets;

    calculateBaseChartHarmonic(scope);

    if (scope.planets.contains(-1)) {
        qDebug() << "Wha?";
    }

    return scope;
}

/*static*/ EventOptions::DisplayMode EventOptions::s_transitBodyColMode =
    EventOptions::DisplayGlyphs;
/*static*/ EventOptions::DisplayMode EventOptions::s_natalTransitBodyColMode =
    EventOptions::DisplayGlyphs;

EventOptions::EventOptions() = default;

EventOptions::EventOptions(const QVariantMap& map)
{
    defaultTimespan      = map.value("Events/defaultTimespan").toString();
    expandShowOrb        = map.value("Events/secondaryOrb").toDouble();
    planetPairOrb        = map.value("Events/planetPairOrb").toDouble();
    patternsQuorum       = map.value("Events/patternsQuorum").toUInt();
    patternsSpreadOrb    = map.value("Events/patternsSpreadOrb").toDouble();
    patternsRestrictMoon = map.value("Events/patternsRestrictMoon").toBool();
    includeMidpoints     = map.value("Events/includeMidpoints").toBool();
    setShowStations(map.value("Events/showStations").toBool());
    includeShadowTransits = map.value("Events/includeShadowTransits").toBool();
    setShowTransitsToTransits(
        map.value("Events/showTransitsToTransits").toBool());
    limitLunarTransits = map.value("Events/limitLunarTransits").toBool();
    skipByDuration     = skipper(map.value("Events/skipByDuration").toUInt());
    setShowTransitsToNatalPlanets(
        map.value("Events/showTransitsToNatalPlanets").toBool());
    includeOnlyOuterTransitsToNatal =
        map.value("Events/includeOnlyOuterTransitsToNatal").toBool();
    includeAsteroids = map.value("Events/includeAsteroids").toBool();
    includeCentaurs  = map.value("Events/includeCentaurs").toBool();
    setShowTransitsToNatalAngles(
        map.value("Events/showTransitsToNatalAngles").toBool());
    setShowTransitsToHouseCusps(
        map.value("Events/showTransitsToHouseCusps").toBool());
    setShowReturns(map.value("Events/showReturns").toBool());
    setShowProgressionsToProgressions(
        map.value("Events/showProgressionsToProgressions").toBool());
    setShowProgressionsToNatal(
        map.value("Events/showProgressionsToNatal").toBool());
    includeOnlyInnerProgressionsToNatal =
        map.value("Events/includeOnlyInnerProgressionsToNatal").toBool();
    setShowTransitAspectPatterns(
        map.value("Events/showTransitAspectPatterns").toBool());
    setShowTransitNatalAspectPatterns(
        map.value("Events/showTransitNatalAspectPatterns").toBool());
    setShowIngresses(map.value("Events/showIngresses").toBool());
    setShowLunations(map.value("Events/showLunations").toBool());
    setShowHeliacalEvents(map.value("Events/showHeliacalEvents").toBool());
    setShowPrimaryDirections(
        map.value("Events/showPrimaryDirections").toBool());
    // showLifeEvents is computed from enabledEvents >= etcUserEventStart, not
    // loaded directly
    expandShowAspectPatterns =
        map.value("Events/expandShowAspectPatterns").toBool();
    expandShowHousePlacementsOfTransits =
        map.value("Events/expandShowHousePlacementsOfTransits").toBool();
    expandShowRulershipTips =
        map.value("Events/expandShowRulershipTips").toBool();
    expandShowStationAspectsToTransits =
        map.value("Events/expandShowStationAspectsToTransits").toBool();
    expandShowStationAspectsToNatal =
        map.value("Events/expandShowStationAspectsToNatal").toBool();
    expandShowReturnAspects =
        map.value("Events/expandShowReturnAspects").toBool();
    expandShowTransitAspectsToReturnPlanet =
        map.value("Events/expandShowTransitAspectsToReturnPlanet").toBool();
    showHarmonicDividend = map.value("Events/showHarmonicDividend").toBool();

    s_transitBodyColMode =
        DisplayMode(map.value("Events/transitBodyColMode").toUInt());
    s_natalTransitBodyColMode =
        DisplayMode(map.value("Events/natalTransitBodyColMode").toUInt());
}

EventOptions::EventOptions(const EventOptions& opts,
                           const EventTypeSet& exclude) :
    EventOptions(opts)
{
    // Simply remove excluded event types from the enabledEvents set
    for (auto excl : exclude) {
        enabledEvents.erase(excl);
    }
}

/*static*/
const QString&
EventOptions::zposPat()
{
    static QString s_pat;
    if (s_pat.isEmpty()) {
        QString plre    = "[a-zA-Z]+(-[a-zA-Z])?"; // e.g., planet-r, planet-p
        QString plmpre  = QString("(%1(/%1)?)").arg(plre); // planet/planet
        QString signsre = "(" + AstroFileEditor::signs.join("|") + ")";

        s_pat = "(?<deg>\\d+\\s+)?(?<sign>" + signsre
                + ")" "( ?((?<min>\\d+)'? ?((?<sec>\\d+)\"?)?))?";
    }
    return s_pat;
}

/*static*/
const QRegularExpression&
EventOptions::zposRE()
{
    static QRegularExpression s_re(zposPat(),
                                   QRegularExpression::CaseInsensitiveOption);
    return s_re;
}

/*static*/
const QString&
EventOptions::eventPat()
{
    static QString s_pat;
    if (s_pat.isEmpty()) {
        QString zposre   = zposPat();
        QString zposrea  = zposre;
        QString plre     = "[a-zA-Z]+(-[a-zA-Z])?"; // e.g., planet-r, planet-p
        QString plmpre   = QString("(%1(/%1)?)").arg(plre); // planet/planet
        QString plmpeqre = plmpre + "(=" + plmpre + ")*" + "(=(?<posa>"
                           + zposrea.replace(">", "a>") + "))?";
        // e.g., sun=moon=mars
        QString plmpzposre = "(?<body>" + plmpre + ") " + "("
                             + "(?<ingress>ingress (?<pos>" + zposre + "))"
                             + "|(?<ret>return)"
                             + ")"; // e.g., sun ingress capricorn, sun return
        QString asprestr  = "(?<aspect>" + plmpeqre + ")";
        QString stationre = "((?<station>(" + AstroFileEditor::planets.join("|")
                            + ")) station)";
        s_pat = "(" + stationre + "|" + "(H(?<harmonic>\\d+(\\.\\d+)?) )?("
                + plmpzposre + "|" + asprestr + ")" + ")";
    }
    return s_pat;
}

/*static*/
const QRegularExpression&
EventOptions::eventRE()
{
    static QRegularExpression s_re(eventPat(),
                                   QRegularExpression::CaseInsensitiveOption);
    return s_re;
}

QVariantMap
EventOptions::toMap()
{
    QVariantMap ret;
    ret.insert("Events/defaultTimespan", defaultTimespan.toString());
    ret.insert("Events/secondaryOrb", expandShowOrb);
    ret.insert("Events/planetPairOrb", planetPairOrb);
    ret.insert("Events/patternsQuorum", patternsQuorum);
    ret.insert("Events/patternsSpreadOrb", patternsSpreadOrb);
    ret.insert("Events/patternsRestrictMoon", patternsRestrictMoon);
    ret.insert("Events/includeMidpoints", includeMidpoints);
    ret.insert("Events/showStations", showStations());
    ret.insert("Events/includeShadowTransits", includeShadowTransits);
    ret.insert("Events/showTransitsToTransits", showTransitsToTransits());
    ret.insert("Events/limitLunarTransits", limitLunarTransits);
    ret.insert("Events/skipByDuration", skipByDuration);
    ret.insert("Events/showTransitsToNatalPlanets",
               showTransitsToNatalPlanets());
    ret.insert("Events/includeOnlyOuterTransitsToNatal",
               includeOnlyOuterTransitsToNatal);
    ret.insert("Events/includeAsteroids", includeAsteroids);
    ret.insert("Events/includeCentaurs", includeCentaurs);
    ret.insert("Events/showTransitsToNatalAngles", showTransitsToNatalAngles());
    ret.insert("Events/showTransitsToHouseCusps", showTransitsToHouseCusps());
    ret.insert("Events/showReturns", showReturns());
    ret.insert("Events/showProgressionsToProgressions",
               showProgressionsToProgressions());
    ret.insert("Events/showProgressionsToNatal", showProgressionsToNatal());
    ret.insert("Events/includeOnlyInnerProgressionsToNatal",
               includeOnlyInnerProgressionsToNatal);
    ret.insert("Events/showTransitAspectPatterns", showTransitAspectPatterns());
    ret.insert("Events/showTransitNatalAspectPatterns",
               showTransitNatalAspectPatterns());
    ret.insert("Events/showIngresses", showIngresses());
    ret.insert("Events/showLunations", showLunations());
    ret.insert("Events/showHeliacalEvents", showHeliacalEvents());
    ret.insert("Events/showPrimaryDirections", showPrimaryDirections());
    ret.insert("Events/showLifeEvents", showLifeEvents());

    ret.insert("Events/expandShowAspectPatterns", expandShowAspectPatterns);
    ret.insert("Events/expandShowHousePlacementsOfTransits",
               expandShowHousePlacementsOfTransits);
    ret.insert("Events/expandShowRulershipTips", expandShowRulershipTips);
    ret.insert("Events/expandShowStationAspectsToTransits",
               expandShowStationAspectsToTransits);
    ret.insert("Events/expandShowStationAspectsToNatal",
               expandShowStationAspectsToNatal);
    ret.insert("Events/expandShowReturnAspects", expandShowReturnAspects);
    ret.insert("Events/expandShowTransitAspectsToReturnPlanet",
               expandShowTransitAspectsToReturnPlanet);
    ret.insert("Events/showHarmonicDividend", showHarmonicDividend);
    ret.insert("Events/transitBodyColMode", s_transitBodyColMode);
    ret.insert("Events/natalTransitBodyColMode", s_natalTransitBodyColMode);
    return ret;
}

std::ostream&
operator<<(std::ostream& os, const PlanetClusterMap& pcm)
{
    os << "(";
    bool any = false;
    for (const auto& pc : pcm) {
        if (any) os << ",\n\t";
        os << pc.first.names().join("=").toStdString() << " spread "
           << pc.second;
        any = true;
    }
    os << ")";
    return os;
}

QDebug
operator<<(QDebug qd, const PlanetClusterMap& pcm)
{
    std::stringstream ss;
    ss << pcm;
    return qd << ss.str().c_str();
}

std::ostream&
operator<<(std::ostream& os, const PlanetProfile& pp)
{
    bool any = false;
    for (auto&& ploc : pp) {
        if (any) os << " ";
        any = true;
        os << ploc->description().toStdString();
    }
    return os;
}

QDebug
operator<<(QDebug qd, const PlanetProfile& pp)
{
    std::stringstream ss;
    ss << pp;
    return qd << ss.str().c_str();
}

std::string
formatPlanetsEtc(const planetsEtc& pe,
                 const PlanetProfile& alist,
                 const hsets& hsets)
{
    auto [i, j] = pe.planetPair;
    auto ai = dynamic_cast<PlanetLoc*>(alist[i]);
    auto aj = dynamic_cast<PlanetLoc*>(alist[j]);

    QStringList hs;
    for (auto h : hsets[pe.hsid]) hs << QString::number(h);

    return QString("H{%1} %2=%3 %4")
        .arg(hs.join(","))
        .arg(ai->description())
        .arg(aj->description())
        .arg(EventTypeManager::eventTypeToString(pe.et))
        .toStdString();
}

/// The intent is to build _staff which comprises the aspect search and _alist
/// Overloaded constructor that accepts EventOptions to apply before processing
OmnibusFinder::OmnibusFinder(HarmonicEvents&      evs,
                             const ADateRange&    range,
                             const uintSSet&      hset,
                             const AstroFileList& files,
                             const EventOptions&  options,
                             const EventTypeSet&  exclude /*={}*/) :
    AspectFinder(evs, range, hset, exclude, afcFindStuff)
{
    // Apply event options BEFORE processing files
    static_cast<EventOptions&>(*this) = options;
    
    // Debug: verify enabledEvents was copied
    qDebug() << "[OMNIBUS CONSTRUCTOR] After copy, enabledEvents.size():" << enabledEvents.size()
             << "showIngresses():" << showIngresses()
             << "isEnabled(etcSignIngress):" << isEnabled(etcSignIngress);
    
    // Continue with normal initialization
    initializeFromFiles(files);
}

/// Original constructor for backwards compatibility
OmnibusFinder::OmnibusFinder(HarmonicEvents&      evs,
                             const ADateRange&    range,
                             const uintSSet&      hset,
                             const AstroFileList& files,
                             const EventTypeSet&  exclude /*={}*/) :
    AspectFinder(evs, range, hset, exclude, afcFindStuff)
{
    initializeFromFiles(files);
}

void OmnibusFinder::initializeFromFiles(const AstroFileList& files)
{
    // This ugly jumble intends to generate the appropriate planet listings,
    // and then create the T-T T-N P-P P-N pairings. And the ingresses, etc.
    // Better to have some kind of factory scheme, but for now...
    bool natal = false, trans = false, prog = false;
    int  natus = -1, locus = -1, progr = -1;

    QMap<ChartPlanetId, unsigned> natalIndex, natalRevIndex;
    QMap<ChartPlanetId, unsigned> transitIndex, transitRevIndex;
    QMap<ChartPlanetId, unsigned> progressedIndex, progressedRevIndex;

    uintSSet conjSet { 1 };
    hsetId   conj = _hsets.size();
    _hsets.emplace_back(conjSet);

    uintSSet conjOppSet { 1, 2 };
    hsetId   conjOpp = _hsets.size();
    _hsets.emplace_back(conjOppSet);

    uintSSet conjOppSqSet { 1, 2, 4 };
    hsetId   conjOppSq = _hsets.size();
    _hsets.emplace_back(conjOppSqSet);

    hsetId allAsp = 0;
    //_hsets.emplace_back(hset);  // the all aspects set already there...

    double njd = 0;
    for (int i = 0, n = files.count(); i < n; ++i) {
        auto f = files.at(i);
        _ids.push_back(f->horoscope().inputData);

        const auto& ida  = f->horoscope().inputData;
        auto        type = f->getType();
        if (type == TypeMale || type == TypeFemale || type == TypeEvent) {
            // Only set natus to the FIRST Male/Female/Event file found
            if (!natal) {
                natus = i, natal = true;
                njd = getJulianDate(ida.GMT());
            } else if (!trans) {
                // Second Male/Female/Event file becomes the transit/comparison chart
                locus = i, trans = true;
            }
        } else if (type == TypeDerivedProg)
            progr = i, prog = true;
        else
            locus = i, trans = true;
    }

    QVector<ZodiacSign> signs = getZodiac(_ids[0].zodiac()).signs.toVector();

    auto getIngress = [&](PlanetId ingr, bool forward = true) {
        unsigned i = ingr - Ingresses_Start;
        if (!forward) ingr += 12;
        ChartPlanetId                  cpid(-1, ingr, Planet_None);
        QMap<ChartPlanetId, unsigned>& inx =
            forward ? transitIndex : transitRevIndex;
        if (!inx.contains(cpid)) {
            inx[cpid] = _alist.size();
            qreal loc =
                forward ? signs[i].startAngle : signs[(i + 1) % 12].startAngle;
            auto pl = new PlanetLoc(cpid, "I", loc);
            pl->allowAspects =
                forward ? PlanetLoc::aspOnlyDirect : PlanetLoc::aspOnlyRetro;
            _alist.push_back(pl);
        }
        return inx.value(cpid);
    };

    Houses houses; // natal houses if needed

    typedef std::function<unsigned(PlanetId)>       getter;
    typedef std::function<unsigned(PlanetId, bool)> getterAlt;

    getter    getNatalPlanet;
    getter    getTransitPlanet;
    getter    getProgressedPlanet;
    getterAlt getHouseIngress;

    if (natal) {
        getNatalPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(natus, pid, Planet_None);
            if (!natalIndex.contains(cpid)) {
                natalIndex[cpid] = _alist.size();
                auto pl          = new NatalPosition(cpid, _ids[natus], "r");
                if (pid >= Houses_Start && pid < Houses_End) {
                    pl->allowAspects = PlanetLoc::aspOnlyConj;
                }
                _alist.push_back(pl);
            }
            return natalIndex.value(cpid);
        };

        if (natal && showTransitsToHouseCusps()) {
            houses          = calculateHouses(_ids[natus]);
            getHouseIngress = [&](PlanetId ingr, bool forward = true) {
                ChartPlanetId cpid(-1, ingr, Planet_None);

                auto&& inx = forward ? transitIndex : transitRevIndex;
                if (!inx.contains(cpid)) {
                    inx[cpid]  = _alist.size();
                    unsigned i = ingr - Houses_Start;

                    qreal loc =
                        forward ? houses.cusp[i] : houses.cusp[(i + 1) % 12];
                    auto pl = new PlanetLoc(cpid, "HI", loc);

                    pl->allowAspects = forward ? PlanetLoc::aspOnlyDirect
                                               : PlanetLoc::aspOnlyRetro;
                    _alist.push_back(pl);
                }
                return inx.value(cpid);
            };
        }
    }
    if (!trans && prog) {
        locus = progr;
        trans = true;
    } else if (!trans && natal) {
        locus = natus;
        trans = true;
    }
    if (trans) {
        getTransitPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(locus, pid, Planet_None);
            if (!transitIndex.contains(cpid)) {
                transitIndex[cpid] = _alist.size();
                _alist.push_back(new TransitPosition(cpid, _ids[locus]));
            }
            return transitIndex.value(cpid);
        };
    }
    if (!prog && trans) {
        progr = locus;
        prog  = true;
    }
    if (natal) {
        getProgressedPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(progr, pid, Planet_None);
            if (!progressedIndex.contains(cpid)) {
                progressedIndex[cpid] = _alist.size();
                auto pl = new ProgressedPosition(cpid, _ids[natus], njd);
                if (pid >= Houses_Start && pid < Houses_End) {
                    pl->allowAspects = PlanetLoc::aspOnlyConj;
                }
                _alist.push_back(pl);
            }
            return progressedIndex.value(cpid);
        };
    }

    if (trans
        && (showTransitsToTransits() || showTransitAspectPatterns()
            || showTransitNatalAspectPatterns() || showStations()
            || showIngresses()))
    {
        QVector<unsigned> ppi, ppo;
        for (auto pid : getPlanets(includeAsteroids, includeCentaurs)) {
            ppi << getTransitPlanet(pid);
        }
        if (false && includeOnlyOuterTransitsToNatal) {
            for (auto pid : getOuterPlanets(includeCentaurs)) {
                ppo << getTransitPlanet(pid);
            }
            // if (!showTransitsToNatalPlanets) ppi = ppo; // only
            // outer-to-outer!
        } else
            ppo = ppi;
        // the above loop has added the planets to the list used
        // by pattern or station finder

        int in = ppi.size();
        int on = ppo.size();
        if (showTransitsToTransits()) {
            for (int i = 0; i < in; ++i) {
                hsetId hs = allAsp;
                auto   tp = dynamic_cast<TransitPosition*>(_alist[ppi[i]]);
                auto   pl = tp->planet.planetId();
                if (pl == Planet_NorthNode || pl == Planet_SouthNode
                    || (pl > Planet_Moon && pl <= Planet_Jupiter))
                {
                    hs = conj;
                } else if (limitLunarTransits && pl == Planet_Moon) {
                    continue;
                }
                for (int j = qMax(0, i + 1 - (in - on)); j < on; ++j) {
                    if (ppi[i] == ppo[j]) continue;
                    auto hst = hs;
                    auto tp  = dynamic_cast<TransitPosition*>(_alist[ppo[j]]);
                    auto opl = tp->planet.planetId();
                    if (opl == Planet_NorthNode || opl == Planet_SouthNode
                        || (opl > Planet_Moon && opl <= Planet_Jupiter))
                    {
                        if (hs == conj) continue;
                        hst = conj;
                    }
                    if (opl == Planet_Moon) {
                        if (pl > Planet_Sun && limitLunarTransits) continue;
                        hst = conjOppSq;
                    }
                    // else if (opl == Planet_Sun) hst = conjOpp;
                    _staff.emplace_back(ppi[i],
                                        ppo[j],
                                        hst,
                                        etcTransitToTransit);
                }
            }
        }

        if (showIngresses()) {
            for (auto i : std::as_const(ppi)) {
                auto tp = dynamic_cast<TransitPosition*>(_alist[i]);
                auto pl = tp->planet.planetId();
                for (PlanetId pid = Ingresses_Start; pid < Ingresses_End; ++pid)
                {
                    if (limitLunarTransits && pl == Planet_Moon
                        && ((pid - Ingresses_Start) % 3 != 0))
                        continue;

                    auto j = getIngress(pid);
                    _staff.emplace_back(i, j, conj, etcSignIngress);

                    // luminaries don't need the backwards ingress
                    if (pl == Planet_Sun || pl == Planet_Moon) continue;

                    j = getIngress(pid, false /*backward*/);
                    _staff.emplace_back(i, j, conj, etcSignIngress);
                }
            }
        }
    }

    if (natal) {
        if (showProgressionsToProgressions()) {
            QVector<unsigned> tpi;
            for (auto pid : getPlanets()) {
                tpi << getProgressedPlanet(pid);
            }
            for (int i = 0; i < tpi.size(); ++i) {
                hsetId hs = allAsp;
                auto   pp = dynamic_cast<ProgressedPosition*>(_alist[tpi[i]]);
                auto   pl = pp->planet.planetId();

                // North/South Node only use conjunction
                if (pl == Planet_NorthNode || pl == Planet_SouthNode) {
                    hs = conj;
                }

                for (int j = i + 1; j < tpi.size(); ++j) {
                    auto hst = hs;
                    auto pp2 = dynamic_cast<ProgressedPosition*>(_alist[tpi[j]]);
                    auto opl = pp2->planet.planetId();

                    // Skip North Node to South Node pairs - they're always opposite
                    if ((pl == Planet_NorthNode || pl == Planet_SouthNode)
                        && (opl == Planet_NorthNode || opl == Planet_SouthNode))
                    {
                        continue;
                    }

                    // If the other planet is also a node, use conjunction only
                    if (opl == Planet_NorthNode || opl == Planet_SouthNode) {
                        hst = conj;
                    }

                    _staff.emplace_back(tpi[i], tpi[j], hst, etcProgressedToProgressed);
                }
            }
        }

        if (anyEnabled(etcTransitToNatal, etcOuterTransitToNatal) || showTransitNatalAspectPatterns()
            || showTransitsToHouseCusps() 
            || showTransitsToNatalAngles()
            || showReturns())
        {
            QList<PlanetId> npl;
            if (anyEnabled(etcTransitToNatal, etcOuterTransitToNatal)
                || showTransitNatalAspectPatterns())
                npl << getPlanets();

            QVector<unsigned> ppn;
            for (auto pid : std::as_const(npl)) {
                ppn << getNatalPlanet(pid);
            }

            if (!anyEnabled(etcTransitToNatal, etcOuterTransitToNatal)) ppn.clear();

            if (anyEnabled(etcTransitToNatal, etcOuterTransitToNatal) 
                || (showTransitsToNatalAngles() && anyEnabled(etcTransitToNatal, etcOuterTransitToNatal))
                || showTransitsToHouseCusps())
            {
                QList<PlanetId> tpl;
                // Determine which planets to include based on which event type is enabled
                bool onlyOuter = isEnabled(etcOuterTransitToNatal) && !isEnabled(etcTransitToNatal);
                if (onlyOuter) {
                    tpl = getOuterPlanets(includeCentaurs);
                } else {
                    tpl = getPlanets(includeAsteroids, includeCentaurs);
                }

                for (auto pid : std::as_const(tpl)) {
                    auto i = getTransitPlanet(pid);
                    for (auto j : std::as_const(ppn)) {
                        auto tp = dynamic_cast<TransitPosition*>(_alist[i]);
                        auto np = dynamic_cast<NatalPosition*>(_alist[j]);
                        if (tp->planet.planetId() != np->planet.planetId()
                            || (!showReturns()
                                && showTransitsToNatalPlanets()))
                        {
                            auto hs = allAsp;
                            auto pl = np->planet.planetId();
                            if (pl == Planet_NorthNode
                                || pl == Planet_SouthNode)
                                hs = conj;
                            else {
                                pl = tp->planet.planetId();
                                if (pl == Planet_NorthNode
                                    || pl == Planet_SouthNode)
                                    hs = conj;
                            }
                            _staff.emplace_back(i, j, hs, etcTransitToNatal);
                        }
                    }
                    if (showTransitsToHouseCusps()) {
                        for (int h = Houses_Start; h < Houses_End; ++h) {
                            // FIXME retrograde to prior house, etc. like sign
                            // ingr
                            _staff.emplace_back(i,
                                                getHouseIngress(h, true),
                                                conj,
                                                etcHouseIngress);
                            _staff.emplace_back(i,
                                                getHouseIngress(h, false),
                                                conj,
                                                etcHouseIngress);
                        }
                    } else if (showTransitsToNatalAngles() && anyEnabled(etcTransitToNatal, etcOuterTransitToNatal)) {
                        for (auto a : getAngles()) {
                            _staff.emplace_back(i,
                                                getNatalPlanet(a),
                                                conj,
                                                etcTransitToNatalAngles);
                        }
                    }
                }
            }
            if (showReturns()) {
                QList<PlanetId> tpl = getPlanets();
                for (auto pid : std::as_const(tpl)) {
                    auto   i  = getTransitPlanet(pid);
                    auto   j  = getNatalPlanet(pid);
                    hsetId hs = conjOpp;
                    auto   tp = dynamic_cast<TransitPosition*>(_alist[i]);
                    auto   pl = tp->planet.planetId();
                    EventType etype = etcReturn;
                    if (pl == Planet_Sun) {
                        etype = etcSolarReturn;
                    } else if (pl == Planet_Moon) {
                        etype = etcLunarReturn;
                    }
                    if (pl == Planet_NorthNode || pl == Planet_SouthNode) {
                        hs = conj;
                    } else if (pl >= Planet_Jupiter && pl <= Planet_Chiron) {
                        hs = allAsp;
                    } else if (pl != Planet_Moon) {
                        hs = conjOppSq;
                    }
                    _staff.emplace_back(i, j, hs, etype);
                }
            }
        }

        if (showProgressionsToNatal()) {
            QList<PlanetId> ppl;
            // Determine which planets to include based on which event type is enabled
            bool onlyInner = isEnabled(etcInnerProgressedToNatal) && !isEnabled(etcProgressedToNatal);
            if (onlyInner)
                ppl = getInnerPlanets(includeAsteroids);
            else
                ppl = getPlanets(includeAsteroids, includeCentaurs);

            auto npl = getPlanets(includeAsteroids, includeCentaurs);
            npl << getAngles();

            for (auto pid : std::as_const(ppl)) {
                auto i = getProgressedPlanet(pid);
                for (auto npid : std::as_const(npl)) {
                    // Skip node-to-node aspects
                    if ((pid == Planet_NorthNode || pid == Planet_SouthNode) &&
                        (npid == Planet_NorthNode || npid == Planet_SouthNode)) {
                        continue;
                    }
                    auto j = getNatalPlanet(npid);
                    _staff.emplace_back(i, j, allAsp, etcProgressedToNatal);
                }
            }
        }
    }

#if 1
    for (const auto& pe : _staff) {
        auto [i, j] = pe.planetPair;
        auto ai = dynamic_cast<PlanetLoc*>(_alist[i]);
        auto aj = dynamic_cast<PlanetLoc*>(_alist[j]);
        const auto& pi = ai->planet;
        const auto& pj = aj->planet;
        if ((pi.fileId() < 0) != (pj.fileId() < 0)) {
            if (pi.planetId() == pj.planetId()) {
            qDebug()
                    << "Looking good"
                    << ai->description()
                    << aj->description();
            }
            continue;
        }

        qDebug() << formatPlanetsEtc(pe, _alist, _hsets).c_str();
    }
#endif
}

namespace
{
static thread_local bool s_inited = false;
}

void
AspectFinder::prepThread()
{
#if MSDOS
    char ephePath[] = "swe\\";
#else
    char ephePath[] = "swe/";
#endif
    if (!s_inited) {
        s_inited = true;
        swe_set_ephe_path(ephePath);
    }
}

void
AspectFinder::releaseThread()
{
    //    swe_close();
}

class TaskTracker {
    AspectFinder* f;

  public:
    TaskTracker(AspectFinder* f) : f(f) { f->startTask(); }
    ~TaskTracker() { f->endTask(); }
};

// Shadow transit timing heuristics for calculating search windows
struct ShadowTransitWindow {
    double retrogradePeriod;  // Average days planet spends in retrograde
    double arcCoverageTime;   // Average days to re-traverse the retrograde arc
};

// Get planet-specific shadow transit timing parameters
ShadowTransitWindow getShadowWindow(PlanetId pid) {
    switch (pid) {
    case Planet_Mercury: return { 21.0, 24.0 };
    case Planet_Venus:   return { 40.0, 50.0 };
    case Planet_Mars:    return { 80.0, 70.0 };
    case Planet_Jupiter: return { 120.0, 60.0 };
    case Planet_Saturn:  return { 139.0, 70.0 };
    case Planet_Uranus:  return { 151.0, 80.0 };
    case Planet_Neptune: return { 158.0, 90.0 };
    case Planet_Pluto:   return { 165.0, 100.0 };
    case Planet_Chiron:  return { 145.0, 75.0 };
    default:             return { 100.0, 60.0 }; // Default for asteroids/other
    }
}

class PairAspectFinder : public EventFinderTask {
    Loc*            _a;
    Loc*            _b;
    unsigned        _h;
    double          _pjd, _jd;
    qreal           _ad, _bd;
    qreal           _ispd, _jspd;
    QDateTime       _d;
    std::string     _which;
    EventType       _et;
    bool            _beQuiet;
    AspectFinder*   _finder;
    HarmonicEvents& _evs;
    bool            _useBZS;
    JDateRange      _useRange;
    bool            _ran = false;

  public:
    PairAspectFinder(Loc*             a,
                     Loc*             b,
                     unsigned         h,
                     double           pjd,
                     double           jd,
                     qreal            ad,
                     qreal            bd,
                     qreal            ispd,
                     qreal            jspd,
                     const QDateTime& d,
                     std::string      which,
                     EventType        et,
                     bool             quiet,
                     AspectFinder*    finder) :
        _a(a->clone()),
        _b(b->clone()),
        _h(h),
        _pjd(pjd),
        _jd(jd),
        _ad(ad),
        _bd(bd),
        _ispd(ispd),
        _jspd(jspd),
        _d(d),
        _which(std::move(which)),
        _et(et),
        _beQuiet(quiet),
        _finder(finder),
        _evs(_finder->_evs) //,
    //_ev(_evs.safe_emplace_back())
    {
        _useBZS = (a->inMotion() && ispd < .00001)
                  || (b->inMotion() && jspd < .00001);
    }

    ~PairAspectFinder()
    {
        if (!_ran) {
            delete _a;
            delete _b;
        }
    }

    EventType eventType() const override { return _et; }

    void setInOrbRange(const JDateRange& r) override { _useRange = r; }

    void run() override
    {
        TaskTracker tr(_finder);

        modalize<bool> mum(st_quiet, _beQuiet);
        PlanetProfile  poses { _a, _b };
        _ran = true; // otherwise we'll clean up stuff we don't want cleaned

        double    tjd {};
        uintmax_t iter;
        bool      bzhs      = false;
        bool      done      = false;
        bool      cancelled = false;
        if (!_useBZS) {
            try {
                auto cps = [this,
                            &poses](double jd) -> std::pair<qreal, qreal> {
                    if (_finder->_state == AspectFinder::cancelRequestedState)
                        throw int(1);
                    auto                    pos = poses.computePos(jd, _h);
                    std::pair<qreal, qreal> ret { pos, poses.speed() };
                    if (!_beQuiet) {
                        QDateTime dt(dateTimeFromJulian(jd));
                        qDebug() << "nri " << (_which + ":").c_str()
                                 << dtToString(dt) << "ret:" << ret;
                    }
                    return ret;
                };
                iter = 50;
                static constexpr int digits =
                    std::numeric_limits<double>::digits;

                double guess;
                if (_a && _a->inMotion() && _b && _b->inMotion()) {
                    guess = _pjd + (fabs(_ad) / (fabs(_ad) + fabs(_bd)));
                } else {
                    guess = _pjd + .5;
                }
                try {
                    tjd = newton_raphson_iterate(cps,
                                                 guess,
                                                 _pjd,
                                                 _jd,
                                                 digits,
                                                 iter);
                }
                catch (int) {
                    return;
                }

                auto psp = PlanetProfile::computeDelta(poses[0], poses[1], _h);
                if (std::abs(psp.first) <= calcLoop::tol) {
                    done = true;
                } else {
                    done = false;
                }
            }
            catch (...) {
                cancelled = false;
            }
            if (!done && !_beQuiet) {
                qDebug() << QString(cancelled ? "Cancelled" : "Failed")
                         << _d.date().toString() << _which.c_str() << "after"
                         << iter << "iteration(s) newton_raphson";
                qDebug() << "speed" << _ispd << _jspd << "respectively";
            }
        }
        if (!done && !cancelled) {
            bzhs           = true;
            unsigned count = 0;
            auto     cp    = [this, &poses, &count](double jd) {
                if (_finder->_state == AspectFinder::cancelRequestedState)
                    throw int(1);
                ++count;
                auto pos = poses.computePos(jd, _h);
                if (!_beQuiet) {
                    QDateTime dt(dateTimeFromJulian(jd));
                    qDebug() << "bzhs " << (_which + ":").c_str()
                             << dtToString(dt) << "ret:" << pos;
                }
                return pos;
            };
            try {
                done = brentZhangStage(cp, _pjd, _jd, _ad, _bd, tjd);
            }
            catch (int) {
                return;
            }

            iter     = count;
            auto psp = PlanetProfile::computeDelta(poses[0], poses[1], _h);
            if (std::abs(psp.first) <= calcLoop::tol) {
                done = true;
            } else {
                done = false;
            }
        }
        if (!done /*&& !_beQuiet*/) {
            qDebug() << "Failed" << _d.date().toString() << _which.c_str()
                     << "after" << iter << "iteration(s) brentStageZhang";
        } else {
            // Pack up event
            auto               qdt   = dateTimeFromJulian(tjd);
            auto               p1loc = dynamic_cast<PlanetLoc*>(poses[0]);
            auto               p2loc = dynamic_cast<PlanetLoc*>(poses[1]);
            PlanetRangeBySpeed plr { *p1loc, *p2loc };

            auto         ch = static_cast<unsigned char>(_h);
            QMutexLocker ml(_evs.mutex());
            auto&        ev = _evs.emplace_back(qdt, _et, ch, std::move(plr));

            if (_useRange != JDateRange()) {
                ev.setRange({ dateTimeFromJulian(_useRange.first),
                              dateTimeFromJulian(_useRange.second) });
            }

            if (!_beQuiet)
                qDebug() << dtToString(qdt).toLocal8Bit().constData()
                         << _which.c_str() << "with" << iter << "iteration(s)"
                         << (bzhs ? "brentZhangStage" : "newton_raphson");
        }
    }
};

void
AspectFinder::findStations()
{
    const auto& start = _range.first;
    const auto& end   = _range.second.addDays(1);
    auto        d     = start.startOfDay().toUTC();
    auto        e     = end.startOfDay().toUTC();

    modalize<bool> mum(st_quiet, true);

    double jd = getJulianDate(d);
    for (auto trans : _alist) (*trans)(jd, 1); // the horror

    PlanetProfile b = _alist;

    auto useRate = 15; // search every 15 days
    if (!st_quiet) qDebug() << "sta" << dtToString(d);

    double                pjd   = jd;
    int                   ndays = int(useRate);
    int                   nsecs = (useRate - double(ndays)) * 24. * 60. * 60.;
    auto                  nd    = d.addDays(ndays).addSecs(nsecs);
    std::list<PlanetLoc*> stations;
    unsigned              in = _alist.size();
    while (d < e) {
        QCoreApplication::processEvents();

        if (_state == cancelRequestedState) break;
        if (_state == pauseRequestedState) {
            QThread::usleep(100000);
            continue;
        }

        jd = getJulianDate(nd);
        if (!st_quiet) qDebug() << "sta" << dtToString(nd);

        // compute new positions
        for (auto pos : b) (*pos)(jd, 1);

        std::set<unsigned> stationChecked;
        for (unsigned i = 0; i < in; ++i) {
            if (stationChecked.count(i) != 0) continue;
            stationChecked.insert(i);

            auto pl = dynamic_cast<TransitPosition*>(_alist[i]);
            if (!pl) continue;

            auto pid = pl->planet.planetId();
            if (pid <= Planet_Moon || pid == Planet_NorthNode
                || pid == Planet_SouthNode || pid >= Planets_End)
            {
                continue;
            }

            auto aspd = _alist[i]->speed;
            auto bspd = b[i]->speed;
            if (!st_quiet) {
                qDebug() << "  " << _alist[i]->description() << aspd << bspd;
            }
            if (sgn(aspd) == sgn(bspd)) continue;

            bool  wasRetro = aspd < 0;
            auto& ev       = _evs.safe_emplace_back();
#if 1
            _tp->start([=, &stations, &ev] {
                startTask();
                modalize<bool> mum(st_quiet, true);
#endif

                auto pj        = dynamic_cast<PlanetLoc*>(_alist[i]->clone());
                pj->desc       = QString("S") + (wasRetro ? 'D' : 'R');
                unsigned iters = 0;
                auto     cspd  = [&](double jd) {
                    (*pj)(jd, 1);
                    ++iters;
                    return pj->speed;
                };

                double tjd = jd / 2. + pjd / 2.;
                if (brentZhangStage(cspd, pjd, jd, aspd, bspd, tjd)) {
                    auto qdt  = dateTimeFromJulian(tjd);
                    auto ploc = dynamic_cast<PlanetLoc*>(pj);

                    auto               dt = dtToString(qdt);
                    PlanetRangeBySpeed plr { *ploc };

                    ev = HarmonicEvent(qdt, etcStation, 1, std::move(plr));

                    qDebug() << dt << pj->description() << "found in" << iters
                             << "iterations(s)";

                    if (includeShadowTransits) {
                        // Add shadow-period transit lookup
                        auto kp = new KnownPosition(ploc, tjd,
                                                    wasRetro ? "IN" : "EX");
                        kp->planet.setFileId(-1);
                        kp->allowAspects = PlanetLoc::aspOnlyDirect;
                        kp->speed        = 0;

                        if (false) {
                            QMutexLocker mlb(&_ctm);
                            stations.emplace_back(kp);
                        }

                        // Create shadow transit search with time-bounded window
                        auto pid = ploc->planet.planetId();
                        auto window = getShadowWindow(pid);
                        
                        // Calculate search window that excludes station time
                        double stationJd = tjd;
                        JDateRange searchWindow;
                        
                        if (wasRetro) {
                            // Direct station (planet was retro) -> Shadow ENTRY (backward in time)
                            // Window: [T_rx - At*3/2, T_rx - At/2]
                            double start = stationJd - window.retrogradePeriod - window.arcCoverageTime * 1.75;
                            double end = stationJd - window.retrogradePeriod - window.arcCoverageTime * .5;
                            searchWindow = JDateRange(start, end);
                        } else {
                            // Retrograde station (planet was direct) -> Shadow EXIT (forward in time)
                            // Window: [T_d + At/2, T_d + At*3/2]
                            double start = stationJd + window.retrogradePeriod + window.arcCoverageTime * .5;
                            double end = stationJd + window.retrogradePeriod + window.arcCoverageTime * 1.75;
                            searchWindow = JDateRange(start, end);
                        }

#if 0
                        // Create KnownPosition for the shadow transit target
                        auto* kp = new KnownPosition(ploc, tjd, wasRetro ? "EX" : "IN");
                        kp->allowAspects = PlanetLoc::aspOnlyDirect;
                        kp->speed = 0;
#endif

                        // Compute positions at window boundaries to get proper deltas
                        auto transitClone = _alist[i]->clone();
                        (*transitClone)(searchWindow.first, 1);
                        qreal startSpd = transitClone->speed;
                        auto [startDelta, startDeltaSpd] = PlanetProfile::computeDelta(transitClone, kp, 1);
                        
                        (*transitClone)(searchWindow.second, 1);
                        qreal endSpd = transitClone->speed;
                        auto [endDelta, endDeltaSpd] = PlanetProfile::computeDelta(transitClone, kp, 1);
                        delete transitClone;
                        
                        qDebug() << "Starting shadow transit search for" 
                                 << _alist[i]->description()
                                 << (wasRetro ? "ENTRY" : "EXIT")
                                 << "window:" << dtToString(dateTimeFromJulian(searchWindow.first))
                                 << "to" << dtToString(dateTimeFromJulian(searchWindow.second))
                                 << "deltas:" << startDelta << "to" << endDelta;
                        
                        // Run PairAspectFinder directly (we're already in a thread pool task)
                        auto* task = new PairAspectFinder(
                            _alist[i],
                            kp,
                            1, // harmonic (conjunction)
                            searchWindow.first,
                            searchWindow.second,
                            startDelta,
                            endDelta,
                            startSpd,
                            endSpd,
                            dateTimeFromJulian(searchWindow.first),
                            pj->description().toStdString()
                                + (wasRetro ? " IN Shadow"
                                            : " EX Shadow"),
                            etcTransitToStation,
                            st_quiet, // quiet mode
                            this);

                        task->run();
                        delete task;
                    }
                } else {
                    qDebug() << "Couldn't find station for"
                             << _alist[i]->description() << "!";
                }
#if 1
                endTask();
            });
#endif
        }

        d  = nd;
        nd = d.addDays(ndays).addSecs(nsecs);
        // nd = d.addDays(_rate);
        pjd = jd;

        _alist.swap(b);
    }

    bool cleared = false;
    if (_state == cancelRequestedState) {
        _tp->clear();
        cleared = true;
    }

    int active(_numTasks);
    qDebug() << active << "activity/ies";
    while (!_tp->waitForDone(100)) {
        QCoreApplication::processEvents();
        if (!cleared && _state == cancelRequestedState) {
            _tp->clear();
            cleared = true;
        }
        int now(_numTasks);
        if (now != active) {
            qDebug() << now << "activity/ies";
            active = now;
        }
    }

    qDebug() << "Done with finding stations";

    if (_state == cancelRequestedState) return;

    for (PlanetLoc* pj : stations) {
        auto* kp = dynamic_cast<KnownPosition*>(pj);
        if (!kp) continue;
        auto jd = kp->julianDate();
        int  i  = pj->planet.fileId();
        int  j  = int(_alist.size());
        _alist.push_back(pj);
        pj->planet.setFileId(-1); // hides it from clusterer
        _staff.emplace_back(i, j, 0, etcTransitToStation);
        qDebug() << "Added transit search for" << i << j
                 << QString("H1 %1=%2")
                        .arg(_alist[i]->description())
                        .arg(_alist[j]->description());
    }
}

void
AspectFinder::findPriorStarts(AspectSearchState& state)
{
    //modalize<bool> mum2(st_quiet, false);
    state.nd = state.d;

    // Performance optimization for progressed aspect searches:
    // 1. When working set contains only progressed/natal positions (no transits),
    //    use a 100x larger time increment since progressed positions move ~1/365 as fast
    // 2. When transits are present, skip progressed position updates on most iterations
    //    (update only every 200th iteration) since they change negligibly

    // Helper to check if working set contains only progressed/natal positions
    auto hasOnlySlowPositions = [](const PlanetSet& ws) -> bool {
        for (const auto& pmid : ws) {
            if (pmid.mode() == plmTransit) return false;
        }
        return true;
    };

    int iterationCount = 0;
    const int progressedUpdateFrequency = 200; // Update progressed positions every Nth iteration
    decltype(state.work.size()) workSize = 0;
    decltype(state.tinOrb.size()) tinOrbSize = 0;

    while (!state.work.empty() || !state.tinOrb.empty()) {
        QCoreApplication::processEvents();

        if (_state == cancelRequestedState) break;
        if (_state == pauseRequestedState) {
            QThread::usleep(100000);
            continue;
        }

        // Build working set
        PlanetSet ws;
        bool showWork = (workSize != state.work.size());
        workSize = state.work.size();
        if (showWork && !st_quiet) {
            if (workSize == 0) {
                qDebug() << "PERF: No planets in work set";
            } else {
                qDebug() << "PERF: Updating planets in work set:";
            }
        }
        for (const auto& hpso : state.work) {
            for (const auto& pso : hpso.second) {
                ws.insert(pso.first.begin(), pso.first.end());
                if (showWork) {
                    qDebug() << "  +" << pso.first.names().join("=");
                }
            }
        }

        bool showTinOrb = (tinOrbSize != state.tinOrb.size());
        tinOrbSize = state.tinOrb.size();
        if (showTinOrb && !st_quiet) {
            if (tinOrbSize==0) {
                qDebug() << "PERF: No planets in tinOrb set";
            } else {
                qDebug() << "PERF: Updating planets from tinOrb set:";
            }
        }
        for (const auto& hijr : state.tinOrb) {
            ws.insert(hijr.first.planets.begin(), hijr.first.planets.end());
            if (showTinOrb) {
                qDebug() << "  +" << hijr.first.planets.names().join("=");
            }
        }

        // Safety check: if working set is empty, we're done
        if (ws.empty()) {
            qDebug() << "PERF: Working set empty, exiting findPriorStarts";
            break;
        }

        // Check if we've exceeded the backward search limit
        bool onlySlowPos = hasOnlySlowPositions(ws);

        // Optimize time increment for progressed-only working sets
        int localNdays = state.ndays;
        int localNsecs = state.nsecs;
        if (onlySlowPos) {
            // Progressed positions move 1/365th as fast as transits
            // So we can use a much larger time increment
            double progressedRate = state.ndays + state.nsecs / (24. * 60. * 60.);
            progressedRate *= 100.0; // Scale up by 100x for progressed-only searches
            localNdays = int(progressedRate);
            localNsecs = (progressedRate - double(localNdays)) * 24. * 60. * 60.;
        }

        state.nd = state.nd.addDays(-localNdays).addSecs(-localNsecs);

        // Separate working set into transit and progressed/natal positions
        PlanetSet transitPositions, progressedNatalPositions;
        for (const auto& pmid : ws) {
            auto mode = pmid.mode();
            if (mode == plmTransit) {
                transitPositions.insert(pmid);
            } else if (mode == plmProgressed || mode == plmNatal) {
                progressedNatalPositions.insert(pmid);
            }
        }

        auto pjd = getJulianDate(state.nd);

        // Selectively update positions based on iteration count
        PlanetProfile* wp = nullptr;
        bool shouldUpdateProgressed = (iterationCount % progressedUpdateFrequency == 0);

        if (shouldUpdateProgressed) {
            qDebug() << "PERF: findPriorStarts loop - state.work has"
                     << state.work.size() << "harmonics, state.tinOrb has"
                     << state.tinOrb.size() << "items, ws has" << ws.size()
                     << "planets";

            qDebug() << "PERF: Skipping progressed/natal position updates this "
                        "iteration";
            qDebug() << "PERF:" << dtToString(state.nd)
                     << "findPriorStarts creating profile from ws with"
                     << ws.size() << "planets:" << ws.names();
        }
        if (transitPositions.empty() || shouldUpdateProgressed) {
            // Update all positions
            wp = _alist.profile(ws);
            if (!wp) {
                qDebug() << "ERROR: _alist.profile(ws) returned nullptr!";
                break;
            }
            //qDebug() << "PERF" << dtToString(state.nd) << "Created profile with"
            //         << wp->size() << "planets, updating all positions to jd=" << pjd;

            // If profile is empty, the planets in ws don't exist in _alist
            // This means we can't compute their positions, so we should clear state.work and state.tinOrb
            if (wp->size() == 0) {
                qDebug()
                    << "PERF: Profile is empty - planets in ws don't exist in "
                       "_alist. Clearing state.work and state.tinOrb";
                state.work.clear();
                state.tinOrb.clear();
                delete wp;
                break;
            }

            (*wp)(pjd);
        } else {
            // Only update transit positions, reuse progressed/natal from previous iteration
            wp = _alist.profile(ws);
            for (auto loc : *wp) {
                auto ploc = dynamic_cast<PlanetLoc*>(loc);
                if (ploc && transitPositions.count(ploc->planetModeId()) > 0) {
                    (*ploc)(pjd, 1);
                }
                // Skip progressed/natal position updates
            }
        }

        ++iterationCount;

        for (auto hit = state.tinOrb.begin(); hit != state.tinOrb.end();) {
            const auto& hps = hit->first;
            const auto& ps  = hps.planets;
            auto        hwp = wp->profile(ps);
            auto        orb = computeSpread(hps.harmonic, *hwp);
            delete hwp;
            if (std::abs(orb) > planetPairOrb) {
                state.inOrb[hps] = hit->second;
                // if (!s_quiet)
                qDebug() << QString("Found H%1 start of %2 at %3 with orb %4")
                                .arg(hps.harmonic)
                                .arg(ps.names().join("="))
                                .arg(dtToString(state.nd))
                                .arg(orb)
                                .toStdString()
                                .c_str();
                state.tinOrb.erase(hit++);
            } else {
                if (!st_quiet)
                    qDebug()
                        << QString("Still looking for H%1 start"
                                   " of %2 "
                                   "at "
                                   "%3 "
                                   "with orb %4")
                               .arg(hps.harmonic)
                               .arg(ps.names().join("="))
                               .arg(dtToString(state.nd))
                               .arg(orb)
                               .toStdString()
                               .c_str();
                hit->second.range.first = pjd; // update range start
                ++hit;
            }
        }

        for (auto hit = state.work.begin(); hit != state.work.end();) {
            auto  h   = hit->first;
            auto& pso = hit->second;
            for (auto spit = pso.begin(); spit != pso.end();) {
                const auto& ps     = spit->first;
                auto        orbWas = spit->second;
                auto        hwp    = wp->profile(ps);
                auto        orb    = computeSpread(h, *hwp);
                delete hwp;
                if (orb > patternsSpreadOrb) {
                    state.starts[h].emplace(ps, ClusterOrbWhen(orb, pjd));
                    qDebug() << QString("Found H%1 prior start of %2 "
                                        "with %3 "
                                        "s"
                                        " "
                                        "at %4")
                                    .arg(h)
                                    .arg(ps.names().join("="))
                                    .arg(orbWas)
                                    .arg(dtToString(state.nd))
                                    .toStdString()
                                    .c_str();
                    pso.erase(spit++);
                } else {
                    if (orb < spit->second.orb) {
                        spit->second = { orb, pjd };
                    }
                    ++spit;
                }
            }
            if (pso.empty()) state.work.erase(hit++);
            else
                ++hit;
        }

        if (wp) delete wp;
    }
}

void
AspectFinder::findNewStarts(AspectSearchState&              state,
                            bool                            collectingStrays,
                            std::unique_ptr<PlanetProfile>& useProf)
{
    if (!state.showPatterns) return;

    if (collectingStrays) {
        qDebug() << "PERF: findNewStarts called in collectingStrays mode";
    }

    // 1. Patterns
    for (state.h = state.maxH; state.h >= 1; --state.h) {
        bool unsel = state.hs.count(state.h) == 0;
        if (unsel /*&& !_filterLowerUnselectedHarmonics*/) continue;
        if (collectingStrays) {
            bool any = false;
            auto hit = state.starts.find(state.h);
            if (hit != state.starts.end()) {
                if (hit->second.empty()) state.starts.erase(hit);
                else
                    any = true;
            }
            if (!any) continue;
        }
        PlanetClusterMap hpc;
        if (!collectingStrays) {
            PlanetProfile* prof = &state.b;
            if (useProf.get()) prof = useProf.get();
            // Build exclusion set: natal-only patterns AND progressed planets
            // (we don't support progressed aspect patterns yet)
            PlanetSet excludeFromPatterns = !showTransitNatalAspectPatterns() ? state.nats : PlanetSet {};
            excludeFromPatterns.insert(state.progs.begin(), state.progs.end());
            hpc = findClusters(state.h,
                               *prof,
                               patternsQuorum,
                               excludeFromPatterns,
                               state.skipAllNatalOnly,
                               patternsRestrictMoon,
                               patternsSpreadOrb,
                               /*excludeProgressed=*/true);
        }

        std::list<PlanetClusterMap::iterator> doomed;
        for (auto sit = state.starts[state.h].begin();
             sit != state.starts[state.h].end();)
        {
            const auto& ps   = sit->first;
            auto        prof = state.b.profile(ps);
            auto        hpcit = hpc.find(ps);
            if (hpcit != hpc.end()) {
                delete prof;
                hpc.erase(hpcit); // already have a start time
                ++sit;
                continue;
            }
            auto spread = computeSpread(state.h, *prof);
            if (spread <= patternsSpreadOrb) {
                if (collectingStrays && !st_quiet) {
                    qDebug() << QString("H%1 %2 with %3 spread at %4")
                                    .arg(state.h)
                                    .arg(ps.names().join("="))
                                    .arg(spread)
                                    .arg(dtToString(
                                        dateTimeFromJulian(state.jd)))
                                    .toStdString()
                                    .c_str();
                }
                // still good so keep
                delete prof;
                ++sit;
                continue;
            }

            double from = sit->second.when;
            double to   = state.jd;

            bool cancel = (from == 0);
            if (cancel) {
                qDebug() << QString("H%1").arg(state.h) << ps.names()
                         << "was previously hijacked, so skipping";
            } else {
                cancel = skippablePeriod({ from, to });
                qDebug() << QString("H%1").arg(state.h) << ps.names() << "from"
                         << dtToString(dateTimeFromJulian(from)) << "to"
                         << dtToString(state.nd)
                         << std::string(cancel ? "** too short **" : "");
            }

            unsigned useH = state.h;
            if (!cancel && state.h > 1) {
                // look for a pending pattern at a lower harmonic
                // because it's likely to be the same pattern but
                // now we have a tighter bounds. when we find this,
                // we set the start time to 0 to indicate that it
                // is no longer active.
                unsigned prev = 0;
                for (unsigned f : getAllFactors(state.h)) {
                    if (f == state.h) break;
                    if (prev == f) continue;
                    auto stit = state.starts.find(f);
                    if (stit == state.starts.end()) continue;
                    auto& startf = stit->second;
                    auto  lwrit  = startf.find(ps);
                    if (lwrit != startf.end()) {
                        cancel = true;
                        qDebug() << QString("H%1").arg(f) << ps.names()
                                 << "exists, so skipping"
                                 << QString("H%1").arg(state.h);
                        break;
                    }
                    prev = f;
                }
            }
            if (cancel) {
                delete prof;
                state.starts[state.h].erase(sit++);
                continue; // skipped!
            }

            static size_t clearing = 0;
            clearing++;
            qDebug() << clearing
                     << QString(
                            "Launching H%1 search for %2 with %3 spread at %4")
                            .arg(state.h)
                            .arg(ps.names().join("="))
                            .arg(spread)
                            .arg(dtToString(dateTimeFromJulian(state.jd)))
                            .toStdString()
                            .c_str();
            doomed.emplace_back(sit++);
            // starts[h].erase(sit++);

            ADateTimeRange range(dateTimeFromJulian(from),
                                 dateTimeFromJulian(state.jd));
            EventType      et = ps.heterogeneous()
                                    ? etcTransitNatalAspectPattern
                                    : etcTransitAspectPattern;
            auto& ev = _evs.safe_emplace_back(range, et, useH, PlanetSet(ps));
#if 1
            _tp->start(
                [this, prof, from, to, ps, useH, h = state.h, range, &ev] {
                    startTask();
#endif
                    auto csprd = [prof, h, this](double jd) {
                        if (_state == cancelRequestedState) throw int(1);
                        auto val = computeSpread(h, jd, *prof, _ids);
                        // qDebug() << dtToString(dateTimeFromJulian(jd)) <<
                        // ps.names() << val;
                        return val;
                    };
                    constexpr auto tol =
                        double(std::numeric_limits<float>::epsilon());

                    double   jd;
                    double   m  = 1000;
                    double   a  = from;
                    double   b  = to;
                    double   res;
                    unsigned it = 0;
                    try {
                        do {
                            double mid = a / 2. + b / 2.;
                            ++it;
                            res = brentGlobalMin(
                                csprd, a, b, mid, m, .0000001, tol, jd);
                            if (jd == a || jd == b) {
                                qDebug()
                                    << "Dubious result" << res
                                    << ((jd == b)
                                            ? "jd == to"
                                            : ((jd == a) ? "jd == from" : ""))
                                    << "for" << QString("H%1").arg(useH)
                                    << ps.names()
                                    << QList<qreal> { csprd(a),
                                                      csprd(mid),
                                                      csprd(b) };
                                if (it > 2) break;
                                m *= 10;
                                auto q = (mid - a) / 4.;
                                if (jd == b) a = b - q, b += q;
                                else
                                    b = a + q, a -= q;
                            } else
                                break;
                        } while (it < 3);

                        QMutexLocker ml(_evs.mutex());
                        auto         qdt = dateTimeFromJulian(jd);
                        ev.reset(qdt, res);
                    }
                    catch (int) {
                    }
                    delete prof;
#if 1
                    endTask();
                });
#endif
            // TODO keep track of this start and end and then search
        }

        // now hpc should only contain entries that are not in starts[h]
        // and starts[h] will contain entries that *were* initially in
        // hpc, so we need to add the remainder of hpc to starts[h] with
        // the new date.
        for (const auto& cl : hpc) {
            bool        add  = true;
            unsigned    prev = 0;
            const auto& ps(cl.first);
            for (unsigned f : getAllFactors(state.h)) {
                if (f == state.h) break;
                if (prev == f) continue;
                auto stit = state.starts.find(f);
                if (stit == state.starts.end()) continue;
                const auto& startf = stit->second;
                auto        lwrit  = startf.find(ps);
                if (lwrit != startf.end()) {
                    add = false;
                    break;
                }
            }
            if (add) {
                qDebug() << QString("Found H%1 start of %2 "
                                    "with %3 spread at %4")
                                .arg(state.h)
                                .arg(ps.names().join("="))
                                .arg(cl.second)
                                .arg(dtToString(dateTimeFromJulian(state.jd)))
                                .toStdString()
                                .c_str();
                state.starts[state.h].emplace(ps,
                                              ClusterOrbWhen(cl.second.orb,
                                                             state.pjd));
            }
        }
        for (const auto& it : doomed) state.starts[state.h].erase(it);
    }
}

void
AspectFinder::findTransitPairs(AspectSearchState& state)
{
    if (!includeTransitRange) return;

    qreal ad, asp;
    qreal bd, bsp;

    unsigned i, j;

    static int totalPairsChecked = 0;
    static int totalInOrbFound = 0;
    int localPairsChecked = 0;
    int localInOrbFound = 0;

    auto stuff = _staff;
    for (state.h = 1; state.h <= state.maxH; ++state.h) {
        for (auto it = stuff.begin(); it != stuff.end();) {
            bool unsel = state.hs.count(state.h) == 0;
            if ((unsel && !filterLowerUnselectedHarmonics)
                || (_hsets[it->hsid].count(state.h) == 0)
                || it->et == etcSignIngress || it->et == etcHouseIngress
                || it->et == etcTransitToStation)
            {
                ++it;
                continue;
            }
            std::tie(i, j) = it->planetPair;
            localPairsChecked++;
            std::tie(bd, bsp) =
                PlanetProfile::computeDelta(state.b[i], state.b[j], state.h);

            auto bi   = dynamic_cast<PlanetLoc*>(state.b[i]);
            auto bj   = dynamic_cast<PlanetLoc*>(state.b[j]);
            auto good = bi && bj && (bi->aspectable() || bj->aspectable());

            bool isInOrb = false;
            if (good) {
                HarmonicPlanetSet hij { state.h, { bi->planetModeId(), bj->planetModeId() }, it->et };

                auto hasit = state.inOrb.find(hij);
                isInOrb    = std::abs(bd) <= planetPairOrb;
#if 0
                            if (isInOrb)
                            qDebug() << QString("Found H%1 orb %2 of %3 at %4")
                                        .arg(state.h).arg(std::abs(bd))
                                        .arg(hij.second.names().join("="))
                                        .arg(dtToString(state.d))
                                        .toStdString().c_str();
#endif
                if (hasit == state.inOrb.end() && isInOrb) {
                    // if (!st_quiet)
                    qDebug() << QString("Found H%1 start of %2 at %3")
                                    .arg(state.h)
                                    .arg(hij.planets.names().join("="))
                                    .arg(dtToString(state.d))
                                    .toStdString()
                                    .c_str();
                    state.inOrb[hij] = { state.pjd, 0 };
                    isInOrb          = true;
                } else if (hasit != state.inOrb.end() && !isInOrb) {
                    hasit->second.range.second = state.jd;
                    if (hasit->second.tasks.empty()) {
                        state.proximityLog[hasit->first].emplace(hasit->second,
                                                                  0);
                    } else if (!skippable(hasit->second.range, it->et)) {
                        for (auto r : hasit->second.tasks) {
                            r->setInOrbRange(hasit->second.range);
                            _tp->start(r);
                        }
                    } else {
                        for (auto r : hasit->second.tasks) {
                            delete r;
                        }
                        hasit->second.tasks.clear();
                    }
                    // if (!st_quiet)
                    qDebug() << QString("Found H%1 range of %2 "
                                        "at "
                                        "%3 "
                                        "to "
                                        "%4")
                                    .arg(state.h)
                                    .arg(hij.planets.names().join("="))
                                    .arg(dtToString(dateTimeFromJulian(
                                        hasit->second.range.first)))
                                    .arg(dtToString(state.d))
                                    .toStdString()
                                    .c_str();
                    state.inOrb.erase(hasit);
                }
            }
            if (isInOrb) {
                localInOrbFound++;
                stuff.erase(it++);
            } else
                ++it;
        }
    }

    totalPairsChecked += localPairsChecked;
    totalInOrbFound += localInOrbFound;

    static int callCount = 0;
    if (++callCount % 50 == 0) {
        qDebug() << "PERF: findTransitPairs (last 50 calls): pairs checked=" << totalPairsChecked
                 << "in-orb found=" << totalInOrbFound
                 << "yield=" << (totalPairsChecked > 0 ? (100.0 * totalInOrbFound / totalPairsChecked) : 0) << "%";
        totalPairsChecked = totalInOrbFound = 0;
    }
}

void
AspectFinder::findAspects(AspectSearchState& state, modalize<bool>& mum)
{
    qreal ad, asp;
    qreal bd, bsp;

    unsigned i, j;
    QString  wha_;

    static int totalPairsChecked = 0;
    static int totalAspectsEnqueued = 0;
    static int totalSkippedUnselected = 0;
    static int totalSkippedSameSign = 0;
    static int totalSkippedInOrb = 0;
    int localPairsChecked = 0;
    int localEnqueued = 0;
    int localSkippedUnsel = 0;
    int localSkippedSign = 0;
    int localSkippedInOrb = 0;

    auto     what = [&] {
        if (wha_.isEmpty()) {
            wha_ = QString("H%1 %2=%3")
                       .arg(state.h)
                       .arg(_alist[i]->description())
                       .arg(_alist[j]->description());
        }
        return wha_.toStdString();
    };

    auto stuff = _staff;

    for (state.h = 1; state.h <= state.maxH; ++state.h) {
        bool unsel = state.hs.count(state.h) == 0;
        if (unsel && !filterLowerUnselectedHarmonics) continue;

        for (auto it = stuff.begin(); it != stuff.end();) {
            wha_.clear();
            localPairsChecked++;
            std::tie(i, j)   = it->planetPair;
            auto        et   = it->et;
            auto        hsid = it->hsid;
            const auto& hs   = _hsets[hsid];
            auto        ha   = hs.lower_bound(state.h);
            if (ha == hs.end()) {
                // qDebug() << "Snipping" << what().c_str()
                //          << "because no further harmonics";
                stuff.erase(it++);
                continue;
            } else if (*ha != state.h) {
                // qDebug() << "Skipping" << what().c_str()
                //          << "because not selecting h" << h;
                ++it;
                continue;
            }

            if (auto known = dynamic_cast<KnownPosition*>(_alist[j])) {
                if (std::abs(known->julianDate() - state.jd)
                    > windowOf(known))
                {
                    ++it;
                    continue;
                }
            }
            qreal ispd =
                qAbs(_alist[i]->speed / 2. + state.b[i]->speed / 2.);
            qreal jspd =
                qAbs(_alist[j]->speed / 2. + state.b[j]->speed / 2.);
            if (ispd > jspd) {
                std::swap(i, j);
                std::swap(ispd, jspd);
            }

            std::tie(ad, asp) =
                PlanetProfile::computeDelta(_alist[i], _alist[j], state.h);
            std::tie(bd, bsp) =
                PlanetProfile::computeDelta(state.b[i], state.b[j], state.h);
            if (sgn(ad) == sgn(bd) || (abs(ad) >= 90. || abs(bd) >= 90.)) {
                localSkippedSign++;
                if (!state.keep(i, false) || !state.keep(j, false)) {
                    stuff.erase(it++);
                } else {
                    ++it;
                }
                continue;
            }

#if 1
            auto pi = dynamic_cast<PlanetLoc*>(_alist[i]);
            if (pi && pi->allowAspects > PlanetLoc::aspOnlyConj) {
                qreal spd = _alist[j]->speed / 2. + state.b[j]->speed / 2.;
                if (pi->allowAspects
                    != ((spd < 0) ? PlanetLoc::aspOnlyRetro
                                  : PlanetLoc::aspOnlyDirect))
                {
                    qDebug() << "skipping wrong-way" << what().c_str();
                    stuff.erase(it++);
                    continue;
                }
            }
#endif

            if (true || !st_quiet) {
                QDateTime pdt(dateTimeFromJulian(state.pjd));
                QDateTime dt(dateTimeFromJulian(state.jd));
                qDebug() << what().c_str() << dtToString(pdt) << "delta" << ad
                         << asp << "vs" << dtToString(dt) << "delta" << bd
                         << bsp;
            }
            auto which = what();

            // At this point, we figure there's _alist transit.
            // If the harmonic is not on our list, let's clip the
            // stuff list so that it doesn't get recomputed at _alist
            // higher-order harmonic. This handles the case where
            // conjunction would show up on any higher harmonic
            // as an aspect at that harmonic.
            stuff.erase(it++);
            if (unsel) {
                localSkippedUnsel++;
                continue;
            }

            auto pj = dynamic_cast<PlanetLoc*>(_alist[j]);
            if (includeTransitRange) {
                auto hasit =
                    state.inOrb.find({ state.h, { pi->planetModeId(), pj->planetModeId() }, et });
                if (hasit == state.inOrb.end()) {
                    hasit = state.inOrb.find(
                        { state.h, { pj->planetModeId(), pi->planetModeId() }, et });
                }
                if (hasit != state.inOrb.end()) {
                    localSkippedInOrb++;
                    auto r = new PairAspectFinder(_alist[i],
                                                  _alist[j],
                                                  state.h,
                                                  state.pjd,
                                                  state.jd,
                                                  ad,
                                                  bd,
                                                  ispd,
                                                  jspd,
                                                  state.d,
                                                  which,
                                                  et,
                                                  mum,
                                                  this);
                    hasit->second.addTask(r);
                    continue;
                }
            }

            if (skipByDuration == SkipNone
                || (includeTransitRange
                    && (et == etcSignIngress || et == etcHouseIngress
                        || et == etcTransitToStation)))
            {
                localEnqueued++;
                // enqueue it now if we know it would not be skipped
                auto r = new PairAspectFinder(_alist[i],
                                              _alist[j],
                                              state.h,
                                              state.pjd,
                                              state.jd,
                                              ad,
                                              bd,
                                              ispd,
                                              jspd,
                                              state.d,
                                              which,
                                              et,
                                              mum,
                                              this);
                _tp->start(r);
            }
        }
    }

    totalPairsChecked += localPairsChecked;
    totalAspectsEnqueued += localEnqueued;
    totalSkippedUnselected += localSkippedUnsel;
    totalSkippedSameSign += localSkippedSign;
    totalSkippedInOrb += localSkippedInOrb;

    static int callCount = 0;
    if (++callCount % 50 == 0) {
        qDebug() << "PERF: findAspects (last 50 calls): pairs checked=" << totalPairsChecked
                 << "enqueued=" << totalAspectsEnqueued
                 << "skipped: unsel=" << totalSkippedUnselected
                 << "sign=" << totalSkippedSameSign
                 << "in-orb=" << totalSkippedInOrb
                 << "yield=" << (totalPairsChecked > 0 ? (100.0 * totalAspectsEnqueued / totalPairsChecked) : 0) << "%";
        totalPairsChecked = totalAspectsEnqueued = totalSkippedUnselected = totalSkippedSameSign = totalSkippedInOrb = 0;
    }
}

void
AspectFinder::findRemainingAspects(AspectSearchState& state)
{
    // ~.7 arcsecond (close enough to true perfection)
    constexpr double goodSeparationThreshold = 0.0003;

    //modalize<bool> mum2(st_quiet, false);

    bool any = false;
    for (auto hpsit = state.proximityLog.begin();
         _state != cancelRequestedState && hpsit != state.proximityLog.end();
         ++hpsit)
    {
        const auto& hps = hpsit->first;
        const auto& rm  = hpsit->second;
        for (auto rit = rm.begin(); rit != rm.end(); ++rit) {
            const auto& r    = rit->first;
            const auto& stat = rit->second;
            if (stat) continue;
            auto   h  = hps.harmonic;
            auto&& ps = hps.planets;
            if (ps.size() != 2) {
                continue; // Only for pairs
            }
            auto desc = QString("[%1 - %2]")
                            .arg(dtToString(dateTimeFromJulian(r.first)),
                                 dtToString(dateTimeFromJulian(r.second)));
            if (skipByDuration != SkipNone) {
                double duration  = r.second - r.first;
                double threshold = 0.0;
                switch (skipByDuration) {
                case SkipLessThanDay:   threshold = 1.0; break;
                case SkipLessThanWeek:  threshold = 7.0; break;
                case SkipLessThanMonth: threshold = 30.0; break;
                default:                threshold = 0.0; break;
                }
                if (duration < threshold) {
                    if (!st_quiet) {
                        qDebug() << "Skipping inexact aspect search for pair"
                                 << ps.names().join("=") << "harmonic" << h
                                 << "duration" << duration
                                 << "days (threshold:" << threshold << ")";
                    }
                    continue;
                }
            }
            if (ps.begin()->samePlanet({ Planet_Moon })
                || ps.rbegin()->samePlanet({ Planet_Moon }))
            {
#if 0
                    qDebug() << "Skipping lunar straggler "
                             << QString("H%1 %2: %3")
                                    .arg(h)
                                    .arg(ps.names().join("="))
                                    .arg(desc)
                                    .toStdString()
                                    .c_str();
#endif
                continue;
            }

            // Find the corresponding InputData for each planet
            auto     profile = _alist.profile(ps);
            unsigned iters   = 0;

            // Define the function to minimize: aspect separation at jd
            auto csprd = [&](double jd) {
                ++iters;
                return profile->computePos(jd, h);
            };

            auto cps = [&](double jd) -> std::pair<qreal, qreal> {
                auto pos = profile->computePos(jd, h);
                return { pos, profile->speed() };
            };

            // Recursive binary search to find ALL perfections in the range
            // Returns a list of narrowed brackets, each containing one
            // perfection
            struct PerfectionBracket {
                double left, right;
                bool   hasCrossing;
            };

            // Use the same time increment that found the aspect in the main
            // loop This accounts for harmonics, orb settings, and aspect types
            double minBracketSize = state.useRate;

            if (!st_quiet) {
                qDebug() << "Using minBracketSize =" << minBracketSize
                         << "days for" << ps.names().join("=") << "H" << h
                         << "(eventType:" << hps.eventType << ")";
            }

            std::function<std::vector<PerfectionBracket>(double, double, int)>
                findPerfections;
            findPerfections = [&](double left,
                                  double right,
                                  int depth) -> std::vector<PerfectionBracket> {
                constexpr int maxDepth = 15;

                if (depth >= maxDepth || (right - left) <= minBracketSize) {
                    // Base case: bracket small enough or too deep
                    // Check if there's a sign change in this bracket
                    profile->computePos(left, h);
                    qreal leftSep = PlanetProfile::computeDelta((*profile)[0],
                                                                (*profile)[1],
                                                                h)
                                        .first;
                    profile->computePos(right, h);
                    qreal rightSep = PlanetProfile::computeDelta((*profile)[0],
                                                                 (*profile)[1],
                                                                 h)
                                         .first;
                    bool hasCrossing = (sgn(leftSep) != sgn(rightSep));
                    return { PerfectionBracket { left, right, hasCrossing } };
                }

                double mid = (left + right) / 2.0;

                profile->computePos(left, h);
                qreal leftSep =
                    PlanetProfile::computeDelta((*profile)[0], (*profile)[1], h)
                        .first;

                profile->computePos(mid, h);
                qreal midSep =
                    PlanetProfile::computeDelta((*profile)[0], (*profile)[1], h)
                        .first;

                profile->computePos(right, h);
                qreal rightSep =
                    PlanetProfile::computeDelta((*profile)[0], (*profile)[1], h)
                        .first;

                if (!st_quiet && depth < 3) {
                    qDebug()
                        << "  Depth" << depth << ":"
                        << "left=" << dtToString(dateTimeFromJulian(left))
                        << "sep=" << leftSep << "|"
                        << "mid=" << dtToString(dateTimeFromJulian(mid))
                        << "sep=" << midSep << "|"
                        << "right=" << dtToString(dateTimeFromJulian(right))
                        << "sep=" << rightSep;
                }

                bool leftToMidCrossing  = (sgn(leftSep) != sgn(midSep));
                bool midToRightCrossing = (sgn(midSep) != sgn(rightSep));

                if (leftToMidCrossing && midToRightCrossing) {
                    // CASE 3: Both halves have crossings - recursively search
                    // BOTH
                    if (!st_quiet && depth < 3) {
                        qDebug() << "  -> Both halves have crossings, "
                                    "recursively searching both";
                    }
                    auto leftResults  = findPerfections(left, mid, depth + 1);
                    auto rightResults = findPerfections(mid, right, depth + 1);
                    leftResults.insert(leftResults.end(),
                                       rightResults.begin(),
                                       rightResults.end());
                    return leftResults;
                } else if (leftToMidCrossing) {
                    // CASE 2: Only left half has crossing
                    if (!st_quiet && depth < 3) {
                        qDebug()
                            << "  -> Left half has crossing, searching left";
                    }
                    return findPerfections(left, mid, depth + 1);
                } else if (midToRightCrossing) {
                    // CASE 2: Only right half has crossing
                    if (!st_quiet && depth < 3) {
                        qDebug()
                            << "  -> Right half has crossing, searching right";
                    }
                    return findPerfections(mid, right, depth + 1);
                } else {
                    // CASE 1: No crossing detected - find minimum using
                    // magnitude
                    qreal absLeft  = std::abs(leftSep);
                    qreal absMid   = std::abs(midSep);
                    qreal absRight = std::abs(rightSep);

                    if (absMid <= absLeft && absMid <= absRight) {
                        // Mid is smallest, narrow both sides toward it
                        return findPerfections(left + (mid - left) / 2.0,
                                               mid + (right - mid) / 2.0,
                                               depth + 1);
                    } else if (absLeft < absRight) {
                        return findPerfections(left, mid, depth + 1);
                    } else {
                        return findPerfections(mid, right, depth + 1);
                    }
                }
            };

            if (!st_quiet) {
                qDebug() << "Starting recursive perfection search for"
                         << ps.names().join("=") << "H" << h << "over range ["
                         << dtToString(dateTimeFromJulian(r.first)) << "to"
                         << dtToString(dateTimeFromJulian(r.second)) << "]"
                         << "(" << (r.second - r.first) << "days)";
            }

            auto perfectionBrackets = findPerfections(r.first, r.second, 0);

            if (!st_quiet) {
                qDebug() << "Found" << perfectionBrackets.size()
                         << "bracket(s) for" << ps.names().join("=")
                         << "after recursive search from"
                         << (r.second - r.first) << "days";
                for (size_t i = 0; i < perfectionBrackets.size(); ++i) {
                    const auto& bracket      = perfectionBrackets[i];
                    double      bracketDays  = bracket.right - bracket.left;
                    double      bracketHours = bracketDays * 24.0;
                    qDebug()
                        << "  Bracket" << i << ":"
                        << "[" << dtToString(dateTimeFromJulian(bracket.left))
                        << "to" << dtToString(dateTimeFromJulian(bracket.right))
                        << "]"
                        << "(" << bracketDays << "days =" << bracketHours
                        << "hours)"
                        << (bracket.hasCrossing ? "HAS crossing"
                                                : "no crossing");
                }
            }

            // Process each perfection bracket
            for (const auto& bracket : perfectionBrackets) {
                double left            = bracket.left;
                double right           = bracket.right;
                bool   foundSignChange = bracket.hasCrossing;

                constexpr int digits = std::numeric_limits<double>::digits;

                double minJD, minSep;
                bool   usedNewtonRaphson =
                    false; // Track if N-R was used for perfect aspect marking

                // If we never found a sign change (Case 1), skip Newton-Raphson
                // and go straight to Brent minimization
                if (!foundSignChange) {
                    if (!st_quiet) {
                        qDebug() << "No perfection detected (Case 1), using "
                                    "Brent minimization for"
                                 << ps.names().join("=");
                    }
                    minSep = -1; // Force Brent path
                } else {
                    // Found a crossing - try Newton-Raphson
                    try {
                        boost::uintmax_t iter = 30;

                        auto guess = (left + right)
                                     / 2.0; // Use narrowed bracket midpoint
                        minJD = newton_raphson_iterate(cps,
                                                       guess,
                                                       left,
                                                       right,
                                                       digits,
                                                       iter);

                        // Compute positions at the found minimum, then get
                        // separation
                        profile->computePos(minJD, h);
                        minSep = PlanetProfile::computeDelta(profile[0],
                                                             profile[1],
                                                             h)
                                     .first;

                        if (!st_quiet) {
                            qDebug() << "Newton-Raphson found minJD="
                                     << dtToString(dateTimeFromJulian(minJD))
                                     << "sep=" << minSep << "(" << iter
                                     << "iterations)";
                        }

                        // Check if Newton-Raphson converged to a boundary
                        constexpr double boundaryEps = 1e-6;

                        bool atBoundary =
                            (std::abs(minJD - left) < boundaryEps
                             || std::abs(minJD - right) < boundaryEps);
                        bool goodSeparation =
                            (std::abs(minSep) < goodSeparationThreshold);

                        if (atBoundary && !goodSeparation) {
                            // At boundary with poor separation - reject and use
                            // Brent
                            if (!st_quiet) {
                                qDebug()
                                    << "Newton-Raphson converged to boundary "
                                       "with poor separation"
                                    << QString("H%1 %2: %3 (sep=%4, %5 iters)")
                                           .arg(h)
                                           .arg(ps.names().join("="))
                                           .arg(desc)
                                           .arg(minSep)
                                           .arg(iter)
                                           .toStdString()
                                           .c_str();
                            }
                            minSep = -1; // Force fallback to Brent
                        } else if (goodSeparation) {
                            // Found perfection (separation close to zero)
                            usedNewtonRaphson = true;
                            if (!st_quiet) {
                                qDebug()
                                    << "Newton-Raphson found perfection"
                                    << QString("H%1 %2: %3 (sep=%4, %5 iters)")
                                           .arg(h)
                                           .arg(ps.names().join("="))
                                           .arg(desc)
                                           .arg(minSep)
                                           .arg(iter)
                                           .toStdString()
                                           .c_str();
                            }
                        } else {
                            // Not at boundary - Newton-Raphson found valid
                            // minimum (just not perfect) Keep this result as an
                            // imperfect aspect
                            usedNewtonRaphson =
                                true; // Mark as used for event creation
                            if (!st_quiet) {
                                qDebug()
                                    << "Newton-Raphson found imperfect minimum"
                                    << QString("H%1 %2: %3 (sep=%4, %5 iters)")
                                           .arg(h)
                                           .arg(ps.names().join("="))
                                           .arg(desc)
                                           .arg(minSep)
                                           .arg(iter)
                                           .toStdString()
                                           .c_str();
                            }
                            // Keep minSep and minJD - don't force Brent
                            // fallback
                        }
                    }
                    catch (...) {
                        if (!st_quiet) {
                            qDebug() << "Newton-Raphson threw exception for"
                                     << QString("H%1 %2: %3")
                                            .arg(h)
                                            .arg(ps.names().join("="))
                                            .arg(desc)
                                            .toStdString()
                                            .c_str();
                        }
                        minSep = -1; // Force brentZhangStage fallback
                    }

                    // If Newton-Raphson failed (at boundary OR exception), use
                    // brentZhangStage fallback
                    if (minSep == -1) {
                        // Use brentZhangStage (like PairAspectFinder) with
                        // endpoint values
                        profile->computePos(left, h);
                        qreal leftSep =
                            PlanetProfile::computeDelta((*profile)[0],
                                                        (*profile)[1],
                                                        h)
                                .first;
                        profile->computePos(right, h);
                        qreal rightSep =
                            PlanetProfile::computeDelta((*profile)[0],
                                                        (*profile)[1],
                                                        h)
                                .first;

                        unsigned count = 0;
                        auto     cp    = [&](double jd) {
                            ++count;
                            return profile->computePos(jd, h);
                        };

                        bool success = brentZhangStage(cp,
                                                       left,
                                                       right,
                                                       leftSep,
                                                       rightSep,
                                                       minJD);
                        if (success) {
                            profile->computePos(minJD, h);
                            minSep = PlanetProfile::computeDelta((*profile)[0],
                                                                 (*profile)[1],
                                                                 h)
                                         .first;
                            if (!st_quiet) {
                                qDebug()
                                    << "brentZhangStage found minJD="
                                    << dtToString(dateTimeFromJulian(minJD))
                                    << "sep=" << minSep << "(" << count
                                    << "iterations)";
                            }
                        } else {
                            minSep = qreal(); // Signal failure for
                                              // brentGlobalMin fallback
                        }
                    }
                }

                // No sign change was found - use brent_find_minima to find
                // closest approach
                if (!foundSignChange) {
                    std::tie(minJD, minSep) =
                        brent_find_minima(csprd, left, right, digits);
                    if (!st_quiet) {
                        qDebug()
                            << "Closest approach for"
                            << QString("H%1 %2").arg(h).arg(
                                   ps.names().join("="))
                            << "in [" << dtToString(dateTimeFromJulian(left))
                            << "-" << dtToString(dateTimeFromJulian(right))
                            << "]:" << minSep << "at"
                            << dtToString(dateTimeFromJulian(minJD))
                            << "(1 iters)";
                    }
                }
                if (minSep == qreal()) {
                    qDebug() << "Unable to find closest using brentZhangStage "
                             << QString("H%1 %2: bracket [%3 - %4] (%5 iters)")
                                    .arg(h)
                                    .arg(ps.names().join("="))
                                    .arg(dtToString(dateTimeFromJulian(left)))
                                    .arg(dtToString(dateTimeFromJulian(right)))
                                    .arg(iters)
                                    .toStdString()
                                    .c_str();
                    // Last resort: use global min with narrowed bracket
                    brentGlobalMin(csprd,
                                   left,
                                   right,
                                   (left + right) / 2,
                                   1 /*m*/,
                                   1e-7,
                                   1e-8,
                                   minJD);
                }
                if (minJD == r.first || minJD == r.second) {
                    qDebug() << "Unable to find closest (at original boundary)"
                             << QString("H%1 %2: bracket [%3 - %4] (%5 iters)")
                                    .arg(h)
                                    .arg(ps.names().join("="))
                                    .arg(dtToString(dateTimeFromJulian(left)))
                                    .arg(dtToString(dateTimeFromJulian(right)))
                                    .arg(iters)
                                    .toStdString()
                                    .c_str();
                    continue; // Skip this bracket, try next one
                }

                if (!any) {
                    qDebug() << "Ranges with no precise hits:";
                    any = true;
                }

                // Collect the planet positions at minJD
                minSep = profile->computePos(minJD, h);

                qDebug()
                    << QString(
                           "Closest approach for H%1 %2 in [%3 - %4]: " "%5 at "
                                                                        "%6 "
                                                                        "(%7 "
                                                                        "iters"
                                                                        ")")
                           .arg(h)
                           .arg(ps.names().join("="))
                           .arg(dtToString(dateTimeFromJulian(r.first)))
                           .arg(dtToString(dateTimeFromJulian(r.second)))
                           .arg(minSep)
                           .arg(dtToString(dateTimeFromJulian(minJD)))
                           .arg(iters)
                           .toStdString()
                           .c_str();

                PlanetRangeBySpeed plr;
                for (auto loc : *profile) {
                    if (auto ploc = dynamic_cast<PlanetLoc*>(loc)) {
                        plr.emplace(*ploc);
                    }
                }

                ADateTimeRange range { dateTimeFromJulian(r.first),
                                       dateTimeFromJulian(r.second) };

                QMutexLocker ml(_evs.mutex());

                bool perfect = usedNewtonRaphson
                               || (std::abs(minSep) < goodSeparationThreshold);
                auto& ev = _evs.emplace_back(dateTimeFromJulian(minJD),
                                             hps.eventType,
                                             h,
                                             std::move(plr),
                                             perfect ? qreal() : minSep);
                ev.setRange(range);
            } // end for each perfection bracket

            delete profile;
        }
    }

    for (const auto& hpc : state.starts) {
        for (const auto& pso : hpc.second) {
            if (pso.second.when == qreal()) continue;
            qDebug() << QString("Pending pattern H%1 %2 started at %3")
                            .arg(hpc.first)
                            .arg(pso.first.names().join("="))
                            .arg(
                                dtToString(dateTimeFromJulian(pso.second.when)))
                            .toStdString()
                            .c_str();
        }
    }
}

void
AspectFinder::findAspectsAndPatterns()
{
    if (_alist.empty()) return;

    AspectSearchState state;

    state.b                = PlanetProfile(_alist);
    state.skipAllNatalOnly = false;
    if (!showTransitAspectPatterns() && showTransitNatalAspectPatterns()) {
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<NatalPosition*>(pl);
            if (!pla || pla->inMotion()) continue;
            state.nats.emplace(pla->planetModeId());
        }
        state.skipAllNatalOnly = true;
    } else if (showTransitAspectPatterns()
               && (!showTransitNatalAspectPatterns() || _ids.size() == 1))
    {
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<TransitPosition*>(pl);
            if (pla && pla->inMotion()) {
                state.trans.emplace(pla->planetModeId());
            }
            auto prg = dynamic_cast<ProgressedPosition*>(pl);
            if (prg && prg->inMotion()) {
                state.progs.emplace(prg->planetModeId());
            }
        }
    } else if (showTransitAspectPatterns() && showTransitNatalAspectPatterns())
    {
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<PlanetLoc*>(pl);
            if (!pla) continue;
            if (pla->inMotion()) {
                // Separate transits from progressed
                if (pla->mode() == plmTransit) {
                    state.trans.emplace(pla->planetModeId());
                } else if (pla->mode() == plmProgressed) {
                    state.progs.emplace(pla->planetModeId());
                }
            } else {
                state.nats.emplace(pla->planetModeId());
            }
        }
        state.skipAllNatalOnly = true;
    }
    state.showPatterns = showTransitAspectPatterns() || !state.nats.empty();
    state.onlyProgressedAndNatal = false; // Track if only slow-moving aspects remain

    const auto& start = _range.first;
    auto        end   = _range.second;
    if (start == end) end = end.addDays(1);
    state.d = start.startOfDay().toUTC();
    state.e = end.startOfDay().toUTC();

    state.jd  = getJulianDate(state.d);
    state.bjd = state.jd;
    state.ejd = getJulianDate(state.e);
    for (auto tp : _alist) (*tp)(state.jd, 1); // the horror

    for (const auto& ij : _staff) {
        auto&& hset = _hsets.at(ij.hsid);
        state.hs.insert(hset.begin(), hset.end());
    }
    if (showTransitAspectPatterns() || showTransitNatalAspectPatterns()) {
        auto&& hset = _hsets.at(0);
        state.hs.insert(hset.begin(), hset.end());
    }
    state.maxH = 1;
    if (state.hs.empty()) {
        if (!state.trans.empty() || !state.nats.empty()) {
            for (auto&& hset : _hsets) {
                state.hs.insert(hset.begin(), hset.end());
            }
        }
    }
    if (!state.hs.empty()) {
        state.maxH = *state.hs.crbegin();
    }

    modalize<bool> mum(st_quiet, true);

    // a simplistic predicate for determining whether to prune the
    // planet pair list as the harmonics go up. We want to limit
    // the lunar aspects, and aspects to MC and Asc are somewhat
    // dubious. Could be good to make this somewhat more flexible
    // and programmable from the client. For example, if we were
    // generating stations but not transits, but did want
    // preview aspects for those stations, this could prune the list.
    // (Or maybe it's better to have a loop engine that various
    // disciplines could be applied to.)
    state.keep = [this, &state](unsigned i, bool derived = false) {
        if (derived) {
            if (auto p = dynamic_cast<PlanetLoc*>(_alist[i])) {
                if (p->planet.isMidpt()) {
                    return state.h < (derived ? 4u : 2u);
                }
            }
        }
        return keepLooking(state.h, i);
    };

    state.useRate = 1 / double(state.maxH); // XXX
    if (state.showPatterns) {
        state.useRate *= patternsSpreadOrb / 16.;
    } else if ((showTransitsToTransits() || showTransitsToNatalPlanets())
               && includeTransitRange)
    {
        // Increase sampling interval to reduce redundant checks (was /4, then *1.5, then *2.5)
        // Yield data shows 0.2% for findTransitPairs and 0.003% for findAspects
        // Need much larger interval: trying 10x multiplier to reach 1-2% yield target
        //state.useRate *= planetPairOrb * 10.0;
        state.useRate *= planetPairOrb;
    }
    if (skipByDuration == SkipLessThanMonth) {
        state.useRate *= 8;
    } else if (skipByDuration == SkipLessThanWeek) {
        state.useRate *= 2;
    } else if (skipByDuration == SkipLessThanDay) {
        // state.useRate *= 2;
    }

    state.ndays = int(state.useRate);
    state.nsecs = (state.useRate - double(state.ndays)) * 24. * 60. * 60.;

    if (!st_quiet) {
    qDebug() << "PERF: === ASPECT SEARCH DIAGNOSTICS ===";
    qDebug() << "PERF: Sampling interval: useRate=" << state.useRate << "days ("
             << state.ndays << "d" << state.nsecs << "s)";
    qDebug() << "PERF: Search features: maxH=" << state.maxH
             << "showPatterns=" << state.showPatterns
             << "includeTransitRange=" << includeTransitRange
             << "showTransitsToTransits=" << showTransitsToTransits()
             << "showTransitsToNatal=" << showTransitsToNatalPlanets();
    qDebug() << "PERF: Orb settings: patternsSpreadOrb=" << patternsSpreadOrb
             << "planetPairOrb=" << planetPairOrb;
    qDebug() << "PERF: Planet counts: total=" << _alist.size()
             << "trans=" << state.trans.size() << "nats=" << state.nats.size()
             << "progs=" << state.progs.size();
    qDebug() << "PERF: Planet pairs in _staff:" << _staff.size();
    qDebug() << "PERF: =================================";
    }

    if (state.showPatterns || includeTransitRange) {
        state.useProf = &_alist;
        state.stuff   = _staff;
        if (!state.trans.empty() && !state.skipAllNatalOnly) {
            state.doomed = std::unique_ptr<PlanetProfile>(
                _alist.profile(state.trans, state.stuff));
            state.useProf = state.doomed.get();
        }
        for (state.h = 1; state.h <= state.maxH; ++state.h) {
            bool unsel = state.hs.count(state.h) == 0;
            if (unsel /*&& !_filterLowerUnselectedHarmonics*/) continue;
            if (state.showPatterns) {
                auto found = findClusters(state.h,
                                          state.jd,
                                          *state.useProf,
                                          _ids,
                                          patternsQuorum,
                                          !showTransitNatalAspectPatterns()
                                              ? state.nats
                                              : PlanetSet {},
                                          patternsRestrictMoon,
                                          patternsSpreadOrb, 
                                          /*excludeProgressed=*/true);
                if (!found.empty()) {
                    state.work[state.h].swap(found);
                }
            }

            if (!includeTransitRange) continue;

            int   i, j;
            qreal bd, bsp;
            for (auto it = state.stuff.begin(); it != state.stuff.end();) {
                if (_hsets[it->hsid].count(state.h) == 0
                    || it->et == etcSignIngress || it->et == etcHouseIngress
                    || it->et == etcTransitToStation)
                {
                    ++it;
                    continue;
                }

                std::tie(i, j) = it->planetPair;
                auto bi        = dynamic_cast<PlanetLoc*>((*state.useProf)[i]);
                auto bj        = dynamic_cast<PlanetLoc*>((*state.useProf)[j]);
                if (!bi || !bj) continue;
                bool good = bi->aspectable() || bj->aspectable();
                std::tie(bd, bsp) =
                    PlanetProfile::computeDelta(bi, bj, state.h);
                if (!st_quiet)
                    qDebug() << QString("H%1 %2 at %3 with orb %4")
                                    .arg(state.h)
                                    .arg(PlanetSet({ bi->planetModeId(), bj->planetModeId() })
                                             .names()
                                             .join('='))
                                    .arg(dtToString(state.d))
                                    .arg(bd)
                                    .toStdString()
                                    .c_str();
                if (good && std::abs(bd) <= planetPairOrb) {
                    qDebug() << formatPlanetsEtc(*it, *state.useProf, _hsets)
                                    .c_str();

                    HarmonicPlanetSet hij { state.h,
                                            { bi->planetModeId(), bj->planetModeId() },
                                            it->et };
                    state.tinOrb[hij] = { state.jd, 0 };
                    // if (!s_quiet)
                    qDebug()
                        << QString(
                               "Found H%1 inital start of %2 at %3 with orb %4")
                               .arg(state.h)
                               .arg(hij.planets.names().join("="))
                               .arg(dtToString(state.d))
                               .arg(bd)
                               .toStdString()
                               .c_str();
                    state.stuff.erase(it++);
                    continue;
                }
                ++it;
            }
        }

        findPriorStarts(state);
    }
    if (_state == cancelRequestedState) return;

    state.pjd = state.jd;
    state.ljd = state.jd;
    state.nd  = state.d.addDays(state.ndays).addSecs(state.nsecs);
    while (state.d < state.e || !state.starts.empty() || !state.inOrb.empty()) {
        QCoreApplication::processEvents();

        if (_state == cancelRequestedState) break;
        if (_state == pauseRequestedState) {
            QThread::usleep(100000);
            continue;
        }

        state.jd = getJulianDate(state.nd);
        if (state.jd - state.ljd >= 5) {
            emit progress((state.ljd - state.bjd) / (state.ejd - state.bjd));
            state.ljd = state.jd;
            QCoreApplication::processEvents();
            if (_state == cancelRequestedState) break;
            if (_state == pauseRequestedState) continue;
        }

        std::unique_ptr<PlanetProfile> useProf;

        bool collectingStrays = (state.d >= state.e);
        if (collectingStrays) {
            if (!st_quiet)
                qDebug() << "PERF: collectingStrays at time" << dtToString(state.nd) << "(jd=" << state.jd << ")";
            PlanetSet ws;

            // Log state.starts details
            int totalStartsItems = 0;
            for (const auto& hpso : state.starts) {
                totalStartsItems += hpso.second.size();
                for (const auto& pso : hpso.second) {
                    ws.insert(pso.first.begin(), pso.first.end());
                }
            }

            // Log state.inOrb details
            int totalInOrbItems = state.inOrb.size();
            for (const auto& hijr : state.inOrb) {
                ws.insert(hijr.first.planets.begin(), hijr.first.planets.end());
            }

            if (!st_quiet) {
                qDebug() << "PERF: collectingStrays - state.starts has"
                         << state.starts.size() << "harmonics with"
                         << totalStartsItems << "total items, state.inOrb has"
                         << totalInOrbItems << "items, ws size:" << ws.size();
                qDebug() << "PERF: state.b.size() BEFORE any changes:"
                         << state.b.size();
            }

            if (ws.empty()) break; // all done
            if (ws.size() != state.b.size()) {
                qDebug() << "Pruning profile to" << ws.names();
                auto wp = state.b.profile(ws);
                wp->swap(state.b);
                if (state.b.size() == 0 && !st_quiet) {
                    qDebug() << "PERF: state.b AFTER pruning has"
                             << state.b.size() << "planets";
                }
                delete wp;
                
                // Check if only progressed/natal planets remain (no transits)
                state.onlyProgressedAndNatal = true;
                for (const auto& pmid : ws) {
                    if (pmid.mode() == plmTransit) {
                        state.onlyProgressedAndNatal = false;
                        break;
                    }
                }
                if (!st_quiet) {
                    qDebug() << "After pruning: onlyProgressedAndNatal =" << state.onlyProgressedAndNatal;
                }
            }
        }

        QElapsedTimer loopTimer;
        loopTimer.start();

        QElapsedTimer posTimer;
        posTimer.start();
        if (collectingStrays) {
            if (!st_quiet) {
                qDebug() << "PERF: state.b.size() BEFORE position updates:"
                         << state.b.size();
                qDebug() << "PERF: Updating positions to jd=" << state.jd << "("
                         << dtToString(dateTimeFromJulian(state.jd)) << ")";
            }
            // Sample first few planets to see their positions before update
            int sampleCount = state.b.size() < 3 ? state.b.size() : 3;
            for (int i = 0; i < sampleCount; i++) {
                auto pl = dynamic_cast<PlanetLoc*>(state.b[i]);
                if (pl && !st_quiet) {
                    qDebug() << "PERF:   Before update:" << pl->description()
                             << "pos=" << pl->loc << "speed=" << pl->speed;
                }
            }
        }
        for (auto tp : state.b) (*tp)(state.jd, 1);
        qint64 posUpdateMs = posTimer.elapsed();
        if (collectingStrays) {
            if (!st_quiet) 
            qDebug() << "PERF: state.b.size() AFTER position updates:" << state.b.size();
            // Sample first few planets to see their positions after update
            int sampleCount = state.b.size() < 3 ? state.b.size() : 3;
            for (int i = 0; i < sampleCount; i++) {
                auto pl = dynamic_cast<PlanetLoc*>(state.b[i]);
                if (pl && !st_quiet) {
                    qDebug() << "PERF:   After update:" << pl->description()
                             << "pos=" << pl->loc << "speed=" << pl->speed;
                }
            }

            // NOW show orbs/spreads AFTER position updates
            int sampledCount = 0;
            for (const auto& hpso : state.starts) {
                for (const auto& pso : hpso.second) {
                    if (sampledCount < 3) {
                        if (!st_quiet)
                        qDebug() << "PERF:   Requesting profile for H" << hpso.first << "pattern:" << pso.first.names().join("=");
                        auto prof = state.b.profile(pso.first);
                        if (!st_quiet)
                        qDebug() << "PERF:   Got profile with" << prof->size() << "planets (requested" << pso.first.size() << ")";
                        auto spread = computeSpread(hpso.first, *prof);
                        if (!st_quiet)
                        qDebug() << "PERF:   H" << hpso.first << pso.first.names().join("=") << "current spread:" << spread << "(threshold:" << patternsSpreadOrb << ")";
                        delete prof;
                        sampledCount++;
                    }
                }
                if (sampledCount >= 3) break;
            }
        }

        if (!st_quiet) qDebug() << "stuff" << dtToString(state.nd);

        qint64 profileCopyMs = 0;
        if (!collectingStrays && !state.trans.empty()
            && !state.skipAllNatalOnly)
        {
            QElapsedTimer copyTimer;
            copyTimer.start();
            useProf =
                std::unique_ptr<PlanetProfile>(state.b.profile(state.trans));
            profileCopyMs = copyTimer.elapsed();
        }

        // Do all the things HERE

        QElapsedTimer detectionTimer;
        detectionTimer.start();
        if (collectingStrays && !st_quiet) {
            qDebug() << "PERF: state.b.size() BEFORE findNewStarts:" << state.b.size();
        }
        findNewStarts(state, collectingStrays, useProf);
        qint64 detectionMs = detectionTimer.elapsed();
        if (collectingStrays && !st_quiet) {
            qDebug() << "PERF: state.b.size() AFTER findNewStarts:" << state.b.size();
        }

        if (collectingStrays && !state.inOrb.empty()) {
            if (!st_quiet)
            qDebug() << "PERF: Processing state.inOrb in collectingStrays "
                        "mode, size:"
                     << state.inOrb.size();
            int removedCount = 0;
            for (auto hit = state.inOrb.begin(); hit != state.inOrb.end();) {
                const auto& hps = hit->first;
                auto        hwp = state.b.profile(hps.planets);
                auto        orb = computeSpread(hps.harmonic /*harmonic*/, *hwp);
                delete hwp;
                if (!st_quiet)
                qDebug() << "PERF:   H" << hps.harmonic
                         << hps.planets.names().join("=")
                         << "current orb:" << orb
                         << "(threshold:" << planetPairOrb << ")"
                         << (orb > planetPairOrb ? "REMOVING" : "keeping");
                if (orb > planetPairOrb) { // leaving orb
                    hit->second.range.second = state.pjd;
                    bool tooShort = skippablePeriod(hit->second.range);
                    bool any      = false;
                    for (auto r : hit->second.tasks) {
                        if (tooShort && skippableEvent(r->eventType())) {
                            delete r;
                        } else {
                            r->setInOrbRange(hit->second.range);
                            _tp->start(r);
                            any = true;
                        }
                    }
                    hit->second.tasks.clear();
                    if (!any && !tooShort) {
                        // We didn't find a pending search, and it's not too
                        // short to search for an imperfect aspect
                        state.proximityLog[hps].emplace(hit->second, 0);
                    }
                    state.inOrb.erase(hit++);
                    removedCount++;
                } else {
                    ++hit;
                }
            }
            if (!st_quiet)
            qDebug() << "PERF: Removed" << removedCount << "items from state.inOrb, remaining:" << state.inOrb.size();
        }
        qint64 aspectFindingMs = 0;
        if (!collectingStrays && !_staff.empty()) {
            QElapsedTimer aspectTimer;
            aspectTimer.start();
            findTransitPairs(state);
            findAspects(state, mum);
            aspectFindingMs = aspectTimer.elapsed();
        } // if includeTransits

        qint64 totalLoopMs = loopTimer.elapsed();

        static int iterCount = 0;
        static qint64 cumPosUpdate = 0, cumProfileCopy = 0, cumDetection = 0, cumAspectFinding = 0, cumTotal = 0;
        cumPosUpdate += posUpdateMs;
        cumProfileCopy += profileCopyMs;
        cumDetection += detectionMs;
        cumAspectFinding += aspectFindingMs;
        cumTotal += totalLoopMs;

        if (++iterCount % 10 == 0) {
            if (!st_quiet)
            qDebug() << "PERF: Loop timing (last 10 iters avg): pos update" << (cumPosUpdate/10.0) << "ms, "
                     << "profile copy" << (cumProfileCopy/10.0) << "ms, "
                     << "detection" << (cumDetection/10.0) << "ms, "
                     << "aspect finding" << (cumAspectFinding/10.0) << "ms, "
                     << "total" << (cumTotal/10.0) << "ms";
            cumPosUpdate = cumProfileCopy = cumDetection = cumAspectFinding = cumTotal = 0;
        }

        state.d  = state.nd;
        if (collectingStrays) {
            // Use time step based on planet types remaining
            if (state.onlyProgressedAndNatal) {
                // Only slow-moving progressed/natal aspects remain - use large step
                state.nd = state.d.addDays(30);
                if (!st_quiet) {
                    qDebug() << "collectingStrays: only progressed/natal aspects remain, using 30-day increment";
                }
            } else {
                // Transit aspects still present - use normal increment for accuracy
                state.nd = state.d.addDays(state.ndays).addSecs(state.nsecs);
                if (!st_quiet) {
                    qDebug() << "collectingStrays: transit aspects present, using normal increment of" << state.useRate << "days";
                }
            }
        } else {
            state.nd = state.d.addDays(state.ndays).addSecs(state.nsecs);
        }
        // nd = d.addDays(_rate);
        state.pjd = state.jd;
        if (!collectingStrays) _alist.swap(state.b);
    }

    qDebug() << state.inOrb.size() << "pending pairs";
    qDebug() << state.proximityLog.size() << "completed range pairs";

    auto frameJob = [&](HarmonicEvent& e, bool prep = true) {
        if (e.planets().size() != 2) return;
        if (e.range().first != QDateTime()) return;

        HarmonicPlanetSet hps { e.harmonic(), e.planets() };
        auto              lit = state.proximityLog.find(hps);
        if (lit == state.proximityLog.end()) return;

        if (prep) prepThread();
        auto& ranges = lit->second;
        auto  jd     = getJulianDate(e.dateTime());
        auto  it     = ranges.upper_bound({ jd, jd });
        auto  rit    = std::make_reverse_iterator(it);
        while (rit != ranges.rend() && rit->first.first <= jd) {
            if (rit->first.second >= jd) {
                if (e.range() != ADateTimeRange()) {
                    e.setRange({ dateTimeFromJulian(rit->first.first),
                                 dateTimeFromJulian(rit->first.second) });
                }
                rit->second++;
                break;
            }
            ++rit;
        }
        if (prep) releaseThread();
    };

    if (_state != cancelRequestedState) {
        // get those planet pairs framed
        QMutexLocker ml(_evs.mutex()); // lock for swoosh through paired events
        for (auto& ev : _evs) {
            if (ev.eventType() != etcTransitToStation) {
                frameJob(ev, false);
            }
        }
    }

    // Process inexact aspects (those that never perfected during the main loop)
    findRemainingAspects(state);

    bool cleared = false;
    int  active(_numTasks);
    qDebug() << active << "activity/ies";
    while (!_tp->waitForDone(100)) {
        QCoreApplication::processEvents();
        if (!cleared && _state == cancelRequestedState) {
            _tp->clear();
            cleared = true;
        }
        int now(_numTasks);
        if (now != active) {
            qDebug() << now << "activity/ies";
            active = now;
        }
    }

    if (_state != cancelRequestedState) {
        // now get remaining
        QMutexLocker ml(_evs.mutex()); // lock for swoosh through paired events
        auto         fut = QtConcurrent::map(_evs, frameJob);
        fut.waitForFinished();
    }

    qDebug() << "Done with finding aspects and patterns";
}

#if 0
void AspectFinder::run()
{
    switch (_gt) {
    case afcFindPatterns:
        findAspectsAndPatterns(); break;
    case afcFindAspects:
        if (showStations) findStations();
        findAspectsAndPatterns(); break;
    case afcFindStations:
        findStations(); break;
    case afcFindStuff:
        if (showStations) findStations();
        findAspectsAndPatterns();
        break;
    }
}
#endif

void
AspectFinder::findStuff()
{
    prepThread();

    // Use the global thread pool instead of creating a new one
    // This allows proper cleanup and avoids orphaned threads
    _tp = QThreadPool::globalInstance();

    auto threadName = QThread::currentThread()->objectName();
    auto threadPtr = QThread::currentThread();
    
    qDebug() << "========================================";
    qDebug() << "[FINDER START]" << threadPtr << threadName;
    qDebug() << "Using GLOBAL thread pool, ideal thread count" << QThread::idealThreadCount();
    qDebug() << "========================================";

    _state = runningState;
    
#if DEBUG_FINDER_THREADS
    // Pseudo-finder mode: Just run for 15 seconds checking for cancellation
    qDebug() << "[FINDER DEBUG MODE]" << threadPtr << threadName << "- Running in pseudo mode for 15 seconds";
    QElapsedTimer timer;
    timer.start();
    int iteration = 0;
    while (timer.elapsed() < 15000 && _state != cancelRequestedState) {
        QThread::msleep(500);  // Check every 500ms
        if (++iteration % 4 == 0) {  // Log every 2 seconds
            qDebug() << "[FINDER ALIVE]" << threadPtr << threadName 
                     << "- Running" << timer.elapsed() / 1000.0 << "seconds";
        }
        emit progress(timer.elapsed() / 15000.0);
    }
    if (_state == cancelRequestedState) {
        qDebug() << "[FINDER CANCELED]" << threadPtr << threadName 
                 << "- Canceled after" << timer.elapsed() / 1000.0 << "seconds";
    } else {
        qDebug() << "[FINDER COMPLETED]" << threadPtr << threadName 
                 << "- Pseudo-run finished after 15 seconds";
    }
#else
    // Normal finder mode
    if (showStations()) findStations();
    if (_state != cancelRequestedState) findAspectsAndPatterns();
#endif
    
    _state = idleState;

    qDebug() << "[FINDER CLEANUP]" << threadPtr << threadName << "- Waiting for thread pool...";
    _tp->waitForDone();
    qDebug() << "[FINDER CLEANUP]" << threadPtr << threadName << "- Thread pool finished";
    
    qDebug() << "========================================";
    qDebug() << "[FINDER EXIT]" << threadPtr << threadName;
    qDebug() << "========================================";

    releaseThread();

    // Exit the thread event loop to trigger finished() signal
    // This allows proper cleanup via deleteLater()
    thread()->exit(0);
}

#if 0 // has midpoints code
TransitFinder::TransitFinder(HarmonicEvents& evs,
                             const ADateRange& range,
                             const uintSSet& hs,
                             const InputData& trainp,
                             const PlanetSet& tran,
                             unsigned eventsType /*=etcTransitToTransit*/) :
    AspectFinder(evs, range, hs,
                 eventsType==etcTransitAspectPattern
                 ? afcFindPatterns : afcFindAspects)
{
    _evType = eventsType & ~etcDerivedEventMask;

    expandShowStationAspectsToNatal = false;

    _ids.push_back(trainp);
    auto& ida = _ids.back();

    _hsets.emplace_back(hs);

    int m = 0, luna = -1;
    for (const auto& cpid: tran) {
        _alist.push_back(new TransitPosition(cpid,ida));
        if (luna == -1 && cpid.planetId()==Planet_Moon) luna = m;
        else ++m;
    }
    if (includeMidpoints) {
        for (auto a = tran.begin(); a != tran.end(); ++a) {
            if (a->planetId() == Planet_Moon) continue;
            for (auto b = std::next(a); b != tran.end(); ++b) {
                A::ChartPlanetId mp(a->fileId(),a->planetId(),b->planetId());
                _alist.push_back(new TransitPosition(mp,ida));
            }
        }
    }

    for (unsigned i = 0; i+1 < tran.size(); ++i) {
        // exclude moon to midpoint transits
        unsigned n = (i == luna)? tran.size() : _alist.size();
        auto hpl = dynamic_cast<PlanetLoc*>(_alist[i])->planet.planetId();
        for (unsigned j = i+1; j < n; ++j) {
            if (j >= tran.size()) {
                auto mpl = dynamic_cast<PlanetLoc*>(_alist[j])->planet;
                if (mpl.contains(hpl)) continue;
            }
            _staff.push_back( {{ i, j }, 0/*all*/} );
        }
    }

    //computeTransits(hs, prof, staff, range, ida, ida, ev);
}

NatalTransitFinder::NatalTransitFinder(HarmonicEvents& evs,
                                       const ADateRange& range,
                                       const uintSSet& hs,
                                       const InputData& natinp,
                                       const InputData& trainp,
                                       const PlanetSet& natal,
                                       const PlanetSet& tran,
                                       unsigned eventsType /*=etcTransitToNatal*/,
                                       bool includeTransitsToTransits /*=false*/) :
    AspectFinder(evs, range, hs,
                 eventsType==etcTransitNatalAspectPattern
                 ? afcFindPatterns : afcFindAspects)
{
    _evType = eventsType & ~etcDerivedEventMask;

    expandShowStationAspectsToNatal = true;
    expandShowStationAspectsToTransits = true;

    _ids.push_back(natinp);
    auto& ida = _ids.back();
    ida.harmonic = 1;

    _ids.push_back(trainp);
    auto& idb = _ids.back();
    idb.harmonic = 1;

    _hsets.emplace_back(hs);

    unsigned natalSize = natal.size();
    unsigned tranSize = tran.size();

    for (const auto& cpid: natal) {
        _alist.push_back(new NatalPosition(cpid, ida, "n"));
    }
    if (includeMidpoints) {
        for (auto a = natal.begin(); a != natal.end(); ++a) {
            //if (a->planetId() == Planet_Moon) continue;
            for (auto b = std::next(a); b != natal.end(); ++b) {
                A::ChartPlanetId mp(a->fileId(),a->planetId(), b->planetId());
                _alist.push_back(new NatalPosition(mp, ida, "n"));
                ++natalSize;
            }
        }
    }

    int m = 0, luna = -1;
    for (const auto& cpid: tran) {
        _alist.push_back(new TransitPosition(cpid, idb));
        if (luna == -1 && cpid.planetId()==Planet_Moon) luna = m;
        else ++m;
    }

    if (includeMidpoints) {
        unsigned i = _alist.size();
        for (auto a = tran.begin(); a != tran.end(); ++a) {
            if (a->planetId() == Planet_Moon) continue;
            for (auto b = std::next(a); b != tran.end(); ++b) {
                A::ChartPlanetId mp(a->fileId(),a->planetId(), b->planetId());
                _alist.push_back(new TransitPosition(mp,idb));
            }
        }
    }

    for (unsigned i = 0; i < tran.size(); ++i) {
        // exclude moon to midpoint natals
        unsigned n = (i == luna)? natal.size() : natalSize;
        for (unsigned j = 0; j < n; ++j) {
            _staff.push_back( {{ i + natalSize, j }, 0} );
        }
    }

    if (includeTransitsToTransits) {
        for (unsigned i = 0; i+1 < tran.size(); ++i) {
            // exclude moon to midpoint natals
            unsigned n = (i == luna)? tran.size() : tranSize;
            auto hpl = dynamic_cast<PlanetLoc*>(_alist[i])->planet.planetId();
            for (unsigned j = i+1; j < n; ++j) {
                if (auto mpl = dynamic_cast<TransitPosition*>(_alist[j])->planet) {
                    if (mpl.contains(hpl)) continue;
                }
                _staff.push_back( {{ i + natalSize, j + natalSize }, 0} );
            }
        }
    }

    //computeTransits(hs, _alist, staff, range, ida, idb, evs);
}
#endif

EventTypeManager::EventTypeManager()
{
    static std::vector<
        std::tuple<unsigned, unsigned char, const char*, const char*>>
        init {
            { etcUnknownEvent, 0, "?", "unknown" },
            { etcStation, 1, "S", "Stations" },
            { etcTransitToStation, 1, "T=S", "Transits to Station" },
            { etcTransitToTransit, 1, "T=T", "Transits to Transit" },
            { etcTransitToNatal, 2, "T=N", "Transits to Natal" },
            { etcTransitToNatalAngles, 2, "T=NA", "Transits to Natal Angles" },
            { etcOuterTransitToNatal, 2, "OT=N", "Outer Transits to Natal" },
            { etcReturn, 2, "R", "Returns" },
            { etcSolarReturn, 2, "SR", "Solar Returns" },
            { etcLunarReturn, 2, "LR", "Lunar Returns" },
            //{ etcAspectToReturn,        "R=T",  "Aspects to Return" },
            //{ etcReturnTransitToTransit,"RT=T", "Transits in Return" },
            //{ etcReturnTransitToNatal,  "RT=N", "Transits to Natal in Return"
            //},
            { etcProgressedToProgressed, 2, "P=P", "Progressed to Progressed" },
            { etcProgressedToNatal, 2, "P=N", "Progressed to Natal" },
            { etcInnerProgressedToNatal,
              2, "IP=N", "Inner Progressed to Natal" },
            { etcTransitToProgressed, 2, "T=P", "Transits to Progressed" },
            { etcSolarArcToNatal, 2, "D=N", "SA Direct to Natal" },
            { etcSignIngress, 1, "T=I", "Sign Ingresses" },
            { etcHouseIngress, 2, "T=H", "House ingresses" },
            { etcLunation, 1, "L", "Lunations" },
            { etcEclipse, 1, "E", "Eclipses" },
            { etcSolarEclipse, 1, "SE", "Solar Eclipses" },
            { etcLunarEclipse, 1, "LE", "Lunar Eclipses" },
            { etcHeliacalEvents, 1, "HRS", "Heliacal Risings/Settings" },
            { etcPrimaryDirections, 2, "PD", "Primary Directions" },
            { etcTransitAspectPattern, 1, "TA", "Transit Aspect Patterns" },
            { etcTransitNatalAspectPattern,
              2,
              "TNA",
              "Transit-Natal Aspect Patterns" },
            { etcParanatellonta, 1, "Par", "Paranatellonta" },
            { etcParanatellontaToNatal, 2, "Par=N", "Paran-Natal" }
        };

    unsigned      id;
    unsigned char chnum;
    QString       abbr, desc;
    for (const auto& tup : init) {
        std::tie(id, chnum, abbr, desc) = tup;
        _eventIdToString[id]            = { chnum, abbr, desc };
        _eventStringToId[abbr]          = id;
    }
    _numEvents = ++id;
}

EventTypeManager&
EventTypeManager::singleton()
{
    static EventTypeManager* _theMgr = new EventTypeManager();
    return *_theMgr;
}

unsigned
EventTypeManager::registerEventType(unsigned char  chnum,
                                    const QString& abbr,
                                    const QString& desc)
{
    auto& my                           = singleton();
    my._eventIdToString[my._numEvents] = { chnum, abbr, desc };
    my._eventStringToId[abbr]          = my._numEvents;
    return my._numEvents++;
}

unsigned
EventTypeManager::registerEventType(const eventTypeInfo& evtinf)
{
    return registerEventType(std::get<0>(evtinf),
                             std::get<1>(evtinf),
                             std::get<2>(evtinf));
}

namespace
{
typedef std::pair<qreal, ChartPlanetModeId> position;
struct lessPosit {
    bool operator()(const position& a, const position& b) const
    {
        return (a.first != b.first) ? a.first < b.first : a.second < b.second;
    }
};
typedef std::set<position, lessPosit> positions;

inline bool
containsAny(const positions& pos, const PlanetSet& of)
{
    return std::any_of(pos.begin(), pos.end(), [&](const position& p) {
        return of.contains(p.second);
    });
}

inline bool
containsAnyTrans(const positions& pos)
{
    return std::any_of(pos.begin(), pos.end(), [&](const position& p) {
        return p.second.fileId() == 1 && p.second.planetId() != Planet_Moon;
    });
}

inline PlanetSet
getSet(const positions& pos)
{
    PlanetSet ret;
    for (const auto& p : pos) {
        ret.emplace(ChartPlanetModeId(p.second));
    }
    return ret;
}

inline unsigned
sizeWithoutTransitingMoon(const positions& grp, bool moonIn1)
{
    unsigned ret = 0;
    for (const auto& p : grp) {
        auto pid = p.second.planetId();
        if ((pid != Planet_Moon && pid != Planet_NorthNode
             && pid != Planet_SouthNode)
            || (p.second.fileId() != (moonIn1 ? 1 : 0)))
            ++ret;
    }
    return ret;
}

std::ostream&
operator<<(std::ostream& os, const position& pos)
{
    return os << pos.second.name().toStdString() << " " << pos.first;
}

std::ostream&
operator<<(std::ostream& os, const positions& posits)
{
    os << "(";
    std::string next;
    for (const auto& pos : posits) {
        os << next << pos;
        if (next.empty()) next = ", ";
    }
    os << ")";
    return os;
}

template <typename T>
std::string
toString(const T& t)
{
    std::stringstream sstr;
    sstr << t;
    return sstr.str();
}

} // namespace

PlanetClusterMap
findClusters(const positions& posits,
             unsigned         quorum,
             const PlanetSet& need /*={}*/,
             bool             skipAllNatalOnly = false,
             bool             restrictMoon     = true,
             qreal            maxOrb           = 8.)
{
#if 0
    qDebug() << "quorum" << quorum
             << "need" << need.names()
             << "skipAllNatalOnly" << skipAllNatalOnly
             << "restrictMoon" << restrictMoon
             << "maxOrb" << maxOrb;
    qDebug() << toString(posits).c_str();
    //if (posits.size() > 1) qDebug() << "\n";
#endif

    bool moonIn1 = skipAllNatalOnly;
    if (restrictMoon && !skipAllNatalOnly) {
        for (const auto& pos : posits) {
            const auto& cpid = pos.second;
            if (cpid.fileId() == 1 && cpid.planetId() == Planet_Moon) {
                moonIn1 = true;
                break;
            }
        }
    }
    PlanetClusterMap ret;
    for (auto it = posits.begin(); it != posits.end(); ++it) {
        positions grp;
        auto      maybeAddGroup = [&] {
            if (grp.size() < quorum) return;
            if (restrictMoon
                && sizeWithoutTransitingMoon(grp, moonIn1) < quorum)
            {
                if (!st_quiet) qDebug() << "  Not actually quorum because moon";
                return;
            }

            bool needed {}, allNatal {};
            if ((needed = !need.empty() && !containsAny(grp, need))
                || (allNatal = skipAllNatalOnly && !containsAnyTrans(grp)))
            {
                if (!st_quiet) {
                    qDebug()
                        << "  Rejected here because missing"
                        << QString(needed ? "needed" : "transit")
                        << getSet(grp).names().join("=").toStdString().c_str();
                }
                return;
            }

            auto spread = angle(grp.begin()->first, grp.rbegin()->first);
            ret[getSet(grp)] = spread;
        };

        auto e =
            posits.lower_bound(position{it->first + maxOrb, ChartPlanetModeId()});

        if (!st_quiet) {
            unsigned n = 0;
            for (auto jit = it; jit != e; ++jit) ++n;
            if (n >= quorum && !st_quiet) {
                qDebug() << "Found potential quorum" << n;
            }
        }

        for (auto jit = it; jit != e; ++jit) {
            maybeAddGroup();
            grp.emplace(*jit);
        }
        maybeAddGroup();
    }
    return ret;
}

/// Find clusters of planets in harmonic h with known positions
PlanetClusterMap
findClusters(unsigned             h,
             const PlanetProfile& plist,
             unsigned             quorum,
             const PlanetSet&     need /*={}*/,
             bool                 skipAllNatalOnly /*=false*/,
             bool                 restrictMoon /*=true*/,
             qreal                maxOrb /*=8.*/,
             bool                 excludeProgressed /*=false*/)
{
    if (!st_quiet) {
#if 1
        qDebug() << QString("Finding H%1 ").arg(h).toStdString().c_str()
                 << plist;
#else
        qDebug() << QString("Finding H%1").arg(h).toStdString().c_str();
        for (auto p : plist) {
            auto ploc = dynamic_cast<PlanetLoc*>(p);
            if (!ploc) continue;
            qDebug() << "  "
                     << (ploc->description() + (ploc->inMotion() ? "*" : ""))
                            .toStdString()
                            .c_str()
                     << harmonic(h, ploc->rasiLoc());
        }
#endif
    }
    positions posits;
    for (auto loc : plist) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planetModeId();
        if (cpid.fileId() < 0) continue;
        if (excludeProgressed && cpid.mode() == plmProgressed) continue;

        auto pid = cpid.planetId();

        if (h > 1
            && ((pid >= Houses_Start && pid < Houses_End)
                || (pid == Planet_IC || pid == Planet_Desc)))
        {
            continue;
        }
        if (h % 2 == 0 && (pid == Planet_SouthNode)) continue;

        auto hloc = h == 1 ? ploc->rasiLoc() : harmonic(h, ploc->rasiLoc());
        auto ins  = posits.emplace(hloc, ploc->planetModeId());
        if (ins.first->first > 345) {
            posits.emplace(ins.first->first - 360., ploc->planetModeId());
        } else if (ins.first->first < 15) {
            posits.emplace(ins.first->first + 360., ploc->planetModeId());
        }
    }

    auto&& ret = findClusters(posits,
                              quorum,
                              need,
                              skipAllNatalOnly,
                              restrictMoon,
                              maxOrb);
    if (ret.empty() || st_quiet) return ret;
    qDebug() << QString("Found H%1").arg(h) << "cluster map" << ret;
    return ret;
}

/// Find clusters of planets in harmonic h with computed positions
PlanetClusterMap
findClusters(unsigned                h,
             double                  jd,
             const PlanetProfile&    plist,
             const QList<InputData>& ids,
             unsigned                quorum,
             const PlanetSet&        need /*={}*/,
             bool                    restrictMoon /*=true*/,
             qreal                   maxOrb /*=8.*/,
             bool                    excludeProgressed /*=false*/)
{
    if (!st_quiet) {
        qDebug() << QString("Finding H%1 with %2 ids")
                        .arg(h)
                        .arg(ids.size())
                        .toStdString()
                        .c_str()
                 << plist;
    }
    std::vector<unsigned> pfid { 0, 0, 0 };
    positions             posits;
    for (auto loc : plist) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planetModeId();
        if (excludeProgressed && cpid.mode() == plmProgressed) continue;
        if (cpid.fileId() < 0 || cpid.fileId() >= int(pfid.size())) continue;
        auto pid = cpid.planetId();
        if (h > 1
            && ((pid >= Houses_Start && pid < Houses_End)
                || (pid == Planet_IC || pid == Planet_Desc)))
        {
            continue;
        }
        if (h % 2 == 0 && (pid == Planet_SouthNode)) continue;

        ++pfid[cpid.fileId()];
        const auto& ida = ids.at(qMax(ids.size() - 1, cpid.fileId()));
        qreal pos = (ploc->inMotion()) ? PlanetLoc::compute(cpid, ida, jd).first
                                       : ploc->_rasiLoc;
        auto  hloc = h == 1 ? pos : harmonic(h, pos);
        auto  ins  = posits.emplace(hloc, ploc->planetModeId());
        if (ins.first->first > 345) {
            posits.emplace(ins.first->first - 360., ploc->planetModeId());
        } else if (ins.first->first < 15) {
            posits.emplace(ins.first->first + 360., ploc->planetModeId());
        }
    }

    auto&& ret = findClusters(posits,
                              quorum,
                              need,
                              pfid[0] && pfid[1],
                              restrictMoon,
                              maxOrb);
    if (ret.empty() || st_quiet) return ret;
    qDebug() << QString("Found H%1").arg(h) << "cluster map" << ret;
    return ret;
}

HarmonicPlanetClusters
findClusters(const uintSSet&      hs,
             const PlanetProfile& prof,
             unsigned             quorum,
             const PlanetSet&     need /*={}*/,
             bool                 skipAllNatalOnly /*=false*/,
             bool                 restrictMoon /*=true*/,
             qreal                maxOrb /*=8.*/)
{
    HarmonicPlanetClusters ret;
    for (auto h : hs) {
        auto pc = findClusters(h,
                               prof,
                               quorum,
                               need,
                               skipAllNatalOnly,
                               restrictMoon,
                               maxOrb);
        if (pc.empty()) continue;

        uintSSet fac { 1 };
        getAllFactors(h, fac);
        for (unsigned oh : fac) {
            auto rit = ret.find(oh);
            if (rit == ret.end()) continue;

            const auto& retoh = rit->second;
            // clean up patterns already in lower harmonics
            for (auto pit = pc.begin(); pit != pc.end();) {
                auto retpit = retoh.find(pit->first);
                if (retpit == retoh.end()) ++pit;
                else
                    pc.erase(pit++);
            }
        }
        if (!pc.empty()) ret.emplace(h, pc);
    }
    return ret;
}

qreal
computeSpread(unsigned                h,
              double                  jd,
              const PlanetProfile&    prof,
              const QList<InputData>& ids)
{
    std::vector<qreal>    sums(2, 0);
    std::vector<qreal>    locs;
    std::vector<unsigned> c(2, 0);
    for (auto loc : prof) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planet;
        if (cpid.fileId() < 0 && prof.size() > 2) continue;

        qreal    pos;
        unsigned vroom(ploc->inMotion());
        if (vroom && !ids.isEmpty()) {
            auto&& ida = ids.at(qMax(ids.size() - 1, cpid.fileId()));
            pos        = PlanetLoc::compute(cpid, ida, jd).first;
        } else {
            pos = ploc->_rasiLoc;
        }
        if (h > 1) pos = harmonic(h, pos);
        /*if (vroom)*/ locs.emplace_back(pos);
        sums[vroom] += pos;
        ++c[vroom];
    }

    if (c[1] == 0 /*&& !ids.empty()*/) return 0; // this is an error
#if 1
    if (c[0] != 0 /*&& !ids.empty()*/) {
#if 1
        // try to minimize distance to center of natal configuration
        locs.emplace_back(sums[0] / c[0]);
#else
        // try to minimize distance between center of moving
        // and center of natal configuration
        auto fixed = sums[0] / c[0];
        auto movie = sums[1] / c[1];
        return angle(fixed, movie);
#endif
    }
#endif
    qreal maxa = 0;
    for (unsigned i = 0, n = locs.size(); i + 1 < n; ++i) {
        for (unsigned j = i + 1; j < n; ++j) {
            auto a = angle(locs[i], locs[j]);
            if (a > maxa) maxa = a;
        }
    }
    return maxa;
}

} // namespace A
