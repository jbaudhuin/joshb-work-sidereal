#include <QtConcurrent/QtConcurrent>
#include <QDebug>

#undef MSDOS     // undef macroses that made by SWE library
#undef UCHAR
#undef forward

#define min min

#include <math.h>
#include <tuple>

#include <boost/math/tools/minima.hpp>
#include <boost/math/tools/roots.hpp>

#include "astro-calc.h"
#include "astro-gui.h"

#include <swephexp.h>
#include <swehouse.h>

namespace A {

QString
dtToString(const QDateTime& dt)
{
    return dt.toString("yyyy-MM-dd hh:mm:ss.zzz");
}

template <typename T>
inline constexpr
int sgn(T x, std::false_type) {
    return T(0) < x;
}

template <typename T>
inline constexpr
int sgn(T x, std::true_type) {
    return (T(0) < x) - (x < T(0));
}

template <typename T>
inline constexpr
int sgn(T x) {
    return sgn(x, std::is_signed<T>());
}

aspectModeType aspectMode(amcEcliptic);

/*static*/
const
aspectModeType&
aspectModeType::current()
{
    return aspectMode;
}

double getJulianDate(QDateTime GMT,
                     bool ephemerisTime/*=false*/)
{
    char serr[256];
    double ret[2];
    const auto& date(GMT.date());
    const auto& time(GMT.time());
    swe_utc_to_jd(date.year(),
                  date.month(),
                  date.day(),
                  time.hour(),
                  time.minute(),
                  double(time.second()) + (time.msec() / 1000.),
                  1/*gregorian*/, ret, serr);
    return ret[ephemerisTime ? 0 : 1]; // ET or UT
}

double getUTfromET(double et)
{
    int32 iyear, imonth, iday, ihour, imin;
    double dsec;
    swe_jdut1_to_utc(et,1/*greg*/,&iyear,&imonth,&iday,&ihour,&imin,&dsec);

    double ret[2];
    char serr[256];
    swe_utc_to_jd(iyear,imonth,iday,ihour,imin,dsec,1/*greg*/,ret,serr);

    return ret[1];
}

unsigned
windowOf(PlanetLoc* pl)
{
    switch (pl->planet.planetId()) {
    case Planet_Mercury: return 45;
    case Planet_Venus: return 90;
    case Planet_Mars: return 210;
    default:
        return 360;
    }
};

inline
qreal
harmonic(double h, qreal value)
{
    return fmod(value*h,360.);
}

float roundDegree(float deg)
{
    deg = deg - (int(deg / 360)) * 360;
    if (deg < 0) deg += 360;
    return deg;
}

const
ZodiacSign&
getSign(float deg, const Zodiac& zodiac)
{
    for (const ZodiacSign& s : zodiac.signs)
        if (s.startAngle <= deg && s.endAngle > deg)
            return s;
    return zodiac.signs[zodiac.signs.count() - 1];
}

int
getHouse(const Houses& houses, float deg)
{
    for (int i = 0; i <= 10; i++)
        if ((deg >= houses.cusp[i] && deg < houses.cusp[i + 1])
            || (houses.cusp[i] > 180
                && houses.cusp[i + 1] < 180
                && (deg < houses.cusp[i + 1] || deg >= houses.cusp[i]))) 
        {
            return i + 1;
        }

    return 12;
}

int
getHouse(ZodiacSignId sign, const Houses &houses, const Zodiac& zodiac)
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

QList<int>
getHouses(ZodiacSignId sign, const Houses &houses, const Zodiac& zodiac)
{
    if (sign == Sign_None) return { };

    QList<int> ret;
    for (int i = 1; i <= 12; i++) {
        auto hs = getSign(houses.cusp[i - 1], zodiac).id;
        if (sign == hs) ret << i;
    }
    return ret;
}

float
angle(const Star& body1, const Star& body2)
{
    switch (aspectMode) {
    case amcGreatCircle:
    {
        float a = angle(body1.eclipticPos.x(), body2.eclipticPos.x());
        float b = angle(body1.eclipticPos.y(), body2.eclipticPos.y());
        return sqrt(a*a + b*b);
    }
    case amcEcliptic:
        return angle(body1.eclipticPos.x(), body2.eclipticPos.x());
    case amcEquatorial:
        return angle(body1.equatorialPos.x(), body2.equatorialPos.x());
    case amcPrimeVertical:
        return angle(body1.pvPos, body2.pvPos);
    }
    return 0;
}

float angle(const Star& body, float deg)
{
    switch (aspectMode) {
    case amcGreatCircle:
    case amcEcliptic:
        return angle(body.eclipticPos.x(), deg);
    case amcEquatorial:
        return angle(body.equatorialPos.x(), deg);
    case amcPrimeVertical:
        return angle(body.pvPos, deg);
    }
    return 0;
}

float   angle(const Star& body, QPointF coordinate)
{
    switch (aspectMode) {
    case amcGreatCircle:
        {
            float a = angle(body.eclipticPos.x(), coordinate.x());
            float b = angle(body.eclipticPos.y(), coordinate.y());
            return sqrt(pow(a, 2) + pow(b, 2));
        }
    case amcEcliptic:
        return angle(body.eclipticPos.x(), coordinate.x());
    case amcEquatorial:
        return angle(body.equatorialPos.x(), coordinate.x());
    case amcPrimeVertical:
        return angle(body.pvPos, coordinate.x());
    default:
        return 0;
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
aspect(const Star& planet1, const Star& planet2,
       const AspectsSet& aspectSet)
{
#if 0
    if (planet1.isPlanet() && planet2.isPlanet()
        && planet1.getSWENum() == planet2.getSWENum())
        return Aspect_None;
#endif
    return aspect(angle(planet1, planet2), aspectSet);
}

inline
AspectId
aspect(const Planet* p1, const Planet* p2, const AspectsSet& asps)
{ return aspect(*p1, *p2, asps); }

AspectId aspect(const Star& planet1, float degree, const AspectsSet& aspectSet)
{
    return aspect(angle(planet1, degree), aspectSet);
}

AspectId aspect(const Star& planet, QPointF coordinate, const AspectsSet& aspectSet)
{
    return aspect(angle(planet, coordinate), aspectSet);
}

AspectId aspect(float angle, const AspectsSet& aspectSet)
{
    AspectId closest = Aspect_None;
    float pct = 1;
    for (const AspectType& aspect : aspectSet.aspects) {
        auto npct = abs(aspect.angle)/aspect.orb();
        if (aspect.isEnabled()
            && aspect.angle - aspect.orb() <= angle
            && aspect.angle + aspect.orb() >= angle
            && (closest == Aspect_None? true : npct < pct))
        {
            closest = aspect.id;
            pct = npct;
            //return aspect.id;
        }
    }
    return closest;
}

bool
towardsMovement(const Planet& planet1, const Planet& planet2)
{
    const Planet *p1 = &planet1, *p2 = &planet2;

    if (!isEarlier(planet1, planet2))       // make first planet earlier than second
    {
        p1 = &planet2;
        p2 = &planet1;
    }

    switch (aspectMode) {
    case amcPrimeVertical:
    case amcEquatorial:
        return (p1->equatorialSpeed.x() > p2->equatorialSpeed.x());
    default:
    case amcEcliptic:
        return (p1->eclipticSpeed.x() > p2->eclipticSpeed.x());
    }
}

PlanetPosition getPosition(const Planet& planet, ZodiacSignId sign)
{
    if (planet.homeSigns.contains(sign))       return Position_Dwelling;
    if (planet.exaltationSigns.contains(sign)) return Position_Exaltation;
    if (planet.exileSigns.contains(sign))      return Position_Exile;
    if (planet.downfallSigns.contains(sign))   return Position_Downfall;
    return Position_Normal;
}

const Planet* almuten(const Horoscope& scope)
{
    int max = 0;
    const Planet* ret = 0;

    for (const Planet& p : scope.planets)
    {
        int val = p.power.dignity + p.power.deficient;
        if (val > max) {
            max = val;
            ret = &p;
        }
    }

    return ret;
}

const Planet* doryphoros(const Horoscope& scope)
{
    int minAngle = 180;
    const Planet* ret = 0;

    for (const Planet& p : scope.planets)
    {
        if (p.id == Planet_Sun || !p.isReal) continue;

        int val = angle(p, scope.sun);

        if (val < minAngle &&
            isEarlier(p, scope.sun)) {
            minAngle = val;
            ret = &p;
        }
    }

    return ret;
}

const Planet* auriga(const Horoscope& scope)
{
    int minAngle = 180;
    const Planet* ret = 0;

    for (const Planet& p : scope.planets)
    {
        if (p.id == Planet_Sun || !p.isReal) continue;

        int val = angle(p, scope.sun);

        if (val < minAngle &&
            !isEarlier(p, scope.sun)) {
            minAngle = val;
            ret = &p;
        }
    }

    return ret;
}

bool rulerDisposition(int house, int houseAuthority, const Horoscope& scope)
{
    if (house <= 0 || houseAuthority <= 0)
        return false;

    for (const Planet& p : scope.planets)
        if (p.house == house && p.houseRuler.contains(houseAuthority))
            return true;

    return false;
}

bool isEarlier(const Planet& planet, const Planet& sun)
{
 //float opposite = roundDegree(sun.eclipticPos.x() - 180);
    switch (aspectMode) {
    case amcPrimeVertical:
        return (roundDegree(planet.pvPos - sun.pvPos) > 180);
    case amcEquatorial:
        return (roundDegree(planet.equatorialPos.x()
                            - sun.equatorialPos.x()) > 180);
    default:
    case amcEcliptic:
        return (roundDegree(planet.eclipticPos.x()
                            - sun.eclipticPos.x()) > 180);
    }
}

/*const Planet& ruler ( int house, const Horoscope& scope )
 {
  if (house <= 0) return Planet();

  for (const Planet& p : scope.planets)
    if (p.houseRuler == house)
      return p;

  return Planet();
 }*/

PlanetId receptionWith(const Planet& planet, const Horoscope& scope)
{
    for (const Planet& p : scope.planets) {
        if (p != planet) {
            if (getPosition(p, planet.sign->id) == Position_Dwelling &&
                getPosition(planet, p.sign->id) == Position_Dwelling) return p.id;

            if (getPosition(p, planet.sign->id) == Position_Exaltation &&
                getPosition(planet, p.sign->id) == Position_Exaltation) return p.id;
        }
    }

    return Planet_None;
}

Planet
calculatePlanet(PlanetId planet,
                const InputData& input,
                double jd,
                double& eps,
                unsigned int& flags,
                double& ablong,
                double RAMC,
                ZodiacId zid)
{
    Planet ret = getPlanet(planet);

    uint    invertPositionFlag = 256 * 1024;
    char    errStr[256] = "";
    double  xx[6];

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
               qPrintable(ret.name), jd, errStr);
    }
    if (!(ret.sweFlags & invertPositionFlag))
        ret.eclipticPos.setX(xx[0]);
    else                               // found 'inverted position' flag
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
    geopos[2] = 199 /*meters*/; //input.location.z();

    ablong = xx[0];

    // A hack to calculate prime vertical longitude from the campanus house position
    if (zid <= 1
        || swe_calc_ut(jd, ret.sweNum, flags & ~SEFLG_SIDEREAL,
                       xx, errStr) != ERR)
    {
        ablong = xx[0];
        // We may have had to get tropical position to get the
        // house position -- this API wants tropical longitude.
        // From there we fudge a prime vertical coordinate.
        double housePos =
                swe_house_pos(RAMC, geopos[1], eps,
                'C', xx, errStr);
        ret.pvPos = (housePos - 1) / 12 * 360;
    }

    // calculate horizontal coordinates
    double hor[3];
    swe_azalt(jd, SE_ECL2HOR, geopos, 0, 0, xx, hor);
    ret.horizontalPos.setX(hor[0]);
    ret.horizontalPos.setY(hor[1]);

    if (swe_calc_ut(jd, ret.sweNum,
                    flags | SEFLG_EQUATORIAL | SEFLG_SPEED,
                    xx, errStr) != ERR) {
        ret.equatorialPos.setX(xx[0]);
        ret.equatorialPos.setY(xx[1]);
        ret.equatorialSpeed.setX(xx[3]);
        ret.equatorialSpeed.setY(xx[4]);
    }

    return ret;
}

Planet 
calculatePlanet(PlanetId planet,
                const InputData& input,
                const Houses& houses,
                const Zodiac& zodiac)
{
    double  jd = getJulianDate(input.GMT());

    char    errStr[256] = "";

    double eps, ablong;
    unsigned int flags;
    Planet ret = calculatePlanet(planet, input, jd, eps, flags, ablong,
                                 houses.RAMC, zodiac.id);

    double geopos[3];
    geopos[0] = input.location().x();
    geopos[1] = input.location().y();
    geopos[2] = 199 /*meters*/; //input.location.z();

    ret.sign = &getSign(ret.eclipticPos.x(), zodiac);
    ret.house = getHouse(houses, ret.eclipticPos.x());
    ret.position = getPosition(ret, ret.sign->id);
    int prev = -1;
    for (auto s: qAsConst(ret.homeSigns)) {
        if (s == prev) continue;
        ret.houseRuler << getHouses(s, houses, zodiac);
        prev = s;
    }
    std::sort(ret.houseRuler.begin(),
              ret.houseRuler.end());

    double rettm;
    int eflg = SEFLG_SWIEPH;

    int32 deg, min, sec, sgn;
    double frac;
    double st = swe_degnorm(ret.equatorialPos.x());
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    //qDebug("Planet %s RA %d %02d %02d:",
    //       qPrintable(getPlanetName(planet)), deg, min, sec);

    double at[4];
    double RA = at[Star::atMC] = ret.equatorialPos.x();
    double DD = asind(sind(eps) * sind(ablong));
    double AD = asind(tand(DD) * tand(input.location().y()));
    double OA = input.location().y() >= 0 ? (RA - AD) : (RA + AD);
    double OD =input.location().y() >= 0 ? (RA + AD) : (RA - AD);
    // RA - (OAAC - OA)
    at[Star::atAsc] = swe_degnorm(houses.RAMC - (houses.OAAC - OA));
    at[Star::atDesc] = swe_degnorm(houses.RAMC - (houses.ODDC - OD));
    //at[Star::atDesc] = RA + swe_difdegn(RA, at[Star::atAsc]);
    //at[Star::atDesc] = swe_degnorm(houses.RAMC + (houses.OAAC - OA));
    at[Star::atIC] = swe_degnorm(RA - 180);

    for (Star::angleTransitMode m = Star::atAsc;
         m < Star::numAngles;
         m = Star::angleTransitMode(m + 1))
    {
        static QString angleDesc[] = { "asc", "desc", "mc", "ic" };
        if (swe_rise_trans(houses.startSpeculum, ret.sweNum,
                           nullptr/*starname*/,
                           eflg, Star::angleTransitFlag(m),
                           geopos, 1013.25/*atpress*/, 10/*attemp*/,
                           &rettm, errStr) >= 0)
        {
            st = swe_degnorm(swe_sidtime(rettm) * 15 + input.location().x());
            swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
            //qDebug("  %s %3d %02d %02d", qPrintable(angleDesc[m]), deg, min, sec);
            ret.angleTransit[m] = Planet::timeToDT(rettm);
        }

        st = at[m];
#if 0
        if (rettm < houses.startSpeculum) rettm += 1;
        else if (rettm - houses.startSpeculum >= 1) rettm -= 1;
        st = swe_degnorm(swe_sidtime(rettm) * 15 + input.location.x());
#endif
        swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
        //qDebug("  FIXED TIME %s %3d %02d %02d", qPrintable(angleDesc[m]),
        //       deg, min, sec);
        ret.angleTransit[m] = Planet::timeToDT(rettm);
    }
    if (ret.id == Planet_SouthNode) {
        qSwap(ret.angleTransit[Star::atAsc], ret.angleTransit[Star::atDesc]);
        qSwap(ret.angleTransit[Star::atMC], ret.angleTransit[Star::atIC]);
    }

    return ret;
}

qreal
PlanetLoc::compute(const InputData& ida,
                   double jd,
                   int h)
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

std::pair<qreal,qreal>
PlanetLoc::compute(const ChartPlanetId& planet,
                   const InputData& ida,
                   double jd)
{
    constexpr uint invertPositionFlag = 256 * 1024;
    char errStr[256] = "";

    const Planet& p1(getPlanet(planet.planetId()));
    uint flags = (SEFLG_SWIEPH | p1.sweFlags) & ~SEFLG_TRUEPOS;
    bool trop = true;
    if (ida.zodiac() > 1) {
        trop = false;
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(ida.zodiac() - 2, 0, 0);
    }

    double xx[6];
    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    auto eps = xx[0];

    typedef std::pair<double,double> posSpd;
    auto getAscMC = [&](unsigned i, bool trop = false)
            -> posSpd
    {
        double cusps[14], cuspspd[14], ascmc[11], ascmcspd[11];
        auto jdut = getUTfromET(jd);
        uint flags = SEFLG_SWIEPH;
        if (!trop) flags |= SEFLG_SIDEREAL;
        swe_houses_ex2(jdut, flags,
                      ida.location().y(), ida.location().x(),
                      'C', cusps, ascmc, cuspspd, ascmcspd,
                       errStr);
        return { ascmc[i], ascmcspd[i] };
    };

    auto getPos = [&](const Planet& p, qreal& speed) -> qreal {
        int ret = ERR;
        qreal pos = 0.0;
        switch (aspectMode) {
        case amcEcliptic:
            if (p.id == Planet_Asc || p.id == Planet_Desc) {
                std::tie(pos,speed) = getAscMC(0, trop);
                if (p.id==Planet_Desc) pos = swe_degnorm(pos+180.);
                ret = OK;
                // FIXME speed?
            } else if (p.id == Planet_MC || p.id == Planet_IC) {
                std::tie(pos,speed) = getAscMC(1, trop);
                if (p.id==Planet_IC) pos = swe_degnorm(pos+180.);
                ret = OK;
                // FIXME speed?
            } else {
                ret = swe_calc_ut(jd, p.sweNum, flags, xx, errStr);
                pos = xx[0];
                if (p.id==Planet_SouthNode) pos = swe_degnorm(pos+180.);
                speed = xx[3];
            }
            break;

        case amcEquatorial:
            if (p.id == Planet_Asc || p.id == Planet_Desc) {
                double xx[6];
                std::tie(xx[0],speed) = getAscMC(0, true/*trop*/);
                xx[1] = 0, xx[2] = 1.0;
                swe_cotrans(xx, xx, -eps);
                pos = xx[0];
                if (p.id==Planet_Desc) pos = swe_degnorm(pos+180.);
                ret = OK;
                // FIXME speed?
            } else if (p.id == Planet_MC || p.id == Planet_IC) {
                // XXX do we not use cotrans here, too?
                std::tie(pos,speed) = getAscMC(2, true/*trop*/);
                if (p.id==Planet_IC) pos = swe_degnorm(pos+180.);
                ret = OK;
            } else {
                ret = swe_calc_ut(jd, p.sweNum,
                                  (flags & ~SEFLG_SIDEREAL)
                                  | SEFLG_EQUATORIAL | SEFLG_SPEED,
                                  xx, errStr);
                pos = xx[0];
                if (p.id==Planet_SouthNode) pos = swe_degnorm(pos+180.);
                speed = xx[3];
            }
            break;

        default:
        case amcPrimeVertical:
        {
            if (p.id == Planet_Asc) {
                pos = 0; ret = OK; // speed?
            } else if (p.id == Planet_Desc) {
                pos = 180; ret = OK;
            } else if (p.id == Planet_MC) {
                pos = 270; ret = OK; // speed?
            } else if (p.id == Planet_IC) {
                pos = 90; ret = OK;
            } else {
                ret = swe_calc_ut(jd, p.sweNum,
                                  flags & ~SEFLG_SIDEREAL,
                                  xx, errStr);
                auto housePos = swe_house_pos(getAscMC(2,true/*trop*/).first,
                                              ida.location().y(), eps,
                                              'C', xx, errStr);
                pos = (housePos - 1)/12*360;
            }
            break;
        }
        }
        if (ret != ERR) return pos;
        qDebug() << "Can't calculate position of " << p1.name
                 << "at jd" << jd << ":" << errStr;
        return 0;
    };

    qreal speed = 0;
    auto pos = getPos(p1,speed);
    if (planet.isMidpt()) {
        qreal speed2 = 0;
        auto pos2 = getPos(getPlanet(planet.planetId2()),speed2);
        if (pos - pos2 >= 180.) pos -= 360.;
        else if (pos2 - pos >= 180.) pos2 -= 360.;
        pos = swe_degnorm((pos + pos2)/2.);
        speed += speed2;
        if (planet.isOppMidpt()) pos = swe_degnorm(pos+180.);
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
    case amcPrimeVertical:
        return -360;
    default:
        return 0;
    }
}

/*static*/
std::pair<qreal, qreal>
PlanetProfile::computeDelta(const Loc* a,
                            const Loc* b,
                            unsigned int h /*=1*/)
{
    // It is not obvious what needs to happen here.
    // We certainly want to update the positions of the in-motion
    // entities, but if there is more than one of these, getting
    // an average position wouldn't allow us to converge, only
    // track an aspect to a moving midpoint. In contrast, in most
    // cases we want to find a "root", i.e., a zero-point in between
    // a negative and a positive result. Even if we use a spread
    // value to minimize (will never be less than zero), the
    // averaged speed value isn't necessary a proper derivative...
    // Hmm... Perhaps when there is more than one planet, we
    // need to compute spread and position? There is more than one
    // value, but we only have one degree of freedom (one input variable)
    // to control...
    qreal speed = (a->speed + b->speed);
    if (h>1) speed *= qreal(h);

    qreal apos = h>1? harmonic(h,a->loc) : a->loc;
    qreal bpos = h>1? harmonic(h,b->loc) : b->loc;

    if (bpos - apos > 180) bpos -= 360;
    else if (apos - bpos > 180) bpos += 360;

    return { bpos - apos, speed };
}

/*static*/
std::pair<qreal, qreal>
PlanetProfile::computeSpread(std::initializer_list<const Loc*> locs,
                             unsigned int h /*=1*/)
{
    std::vector<qreal> pos(locs.size());
    qreal spd = 0.0;
    unsigned i = 0;
    for (auto loc: locs) {
        pos[i++] = (h > 1? harmonic(h,loc->loc) : loc->loc);
        spd += (h > 1? loc->speed*qreal(h) : loc->speed);
    }
    if (false && locs.size()==2) {
        return { angle(pos[0],pos[1]), spd };
    }

    qreal maxa = 0;
    for (unsigned i = 0, n = pos.size(); i+1 < n; ++i) {
        for (unsigned j = i+1; j < n; ++j) {
            auto a = angle(pos[i], pos[j]);
            if (a > maxa) maxa = a;
        }
    }
    return { maxa, spd };   // XXX
}

qreal
PlanetProfile::computePos(double jd,
                          unsigned int h /*=1*/)
{
    Loc::loc = 0;
    Loc::speed = 0;
    for (auto loc : *this) {
        (*loc)(jd, 1);
    }
    if (size() == 1) {
        Loc::loc = h > 1? harmonic(h,front()->loc) : front()->loc;
        Loc::speed = h > 1? h * front()->speed : front()->speed;
    } else if (size() == 2) {
        std::tie(Loc::loc, Loc::speed) = computeDelta(front(),back(), h);
    }
    return Loc::loc;
}

PlanetProfile::PlanetProfile(std::initializer_list<QMap<int, Planet>*> pms)
{
    int fid = 0;
    for (auto ppm: pms) {
        auto& pm = *ppm;
        for (const auto& plan: pm) {
            auto pid = plan.id;
            qreal ploc = plan.getPrefPos();
            qreal spd = plan.getPrefSpd();
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
    if (size()==2) return angle(at(0)->loc,at(1)->loc);

    std::sort(begin(), end(),
              [](const Loc* a, const Loc* b)
    { return a->loc < b->loc; });

    qreal maxa = 0;
    for (unsigned i = 0, n = size(); i < n; ++i) {
        auto a = angle(at(i)->loc, at((i+1) % n)->loc);
        if (a > maxa) maxa = a;
    }

    return maxa;
}

Star calculateStar(const QString& name,
                   const InputData& input,
                   const Houses& houses,
                   const Zodiac& zodiac)
{
    Star ret = getStar(name);

    uint    invertPositionFlag = 256 * 1024;
    double  jd = getJulianDate(input.GMT());
    char    errStr[256] = "";
    double  xx[6];
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
        && strlen(errStr) != ERR) {
        if (!(ret.sweFlags & invertPositionFlag))
            ret.eclipticPos.setX(xx[0]);
        else                               // found 'inverted position' flag
            ret.eclipticPos.setX(roundDegree(xx[0] - 180));

        ret.eclipticPos.setY(xx[1]);
        ret.distance = xx[2];

        if (swe_fixstar_ut(starName, jd, flags | SEFLG_SWIEPH | SEFLG_EQUATORIAL,
                           xx, errStr) != ERR
            && strlen(errStr) == 0) {
            ret.equatorialPos.setX(xx[0]);
            ret.equatorialPos.setY(xx[1]);
        }

        double geopos[3];                  // calculate horizontal coordinates
        double hor[3];
        geopos[0] = input.location().x();
        geopos[1] = input.location().y();
        geopos[2] = input.location().z();
        swe_azalt(jd, SE_ECL2HOR, geopos, 0, 0, xx, hor);
        ret.horizontalPos.setX(hor[0]);
        ret.horizontalPos.setY(hor[1]);

        double rettm;
        int eflg = SEFLG_SWIEPH;

        for (auto m = Star::atAsc;
             m < Star::numAngles;
             m = Star::angleTransitMode(m + 1)) 
        {
            if (swe_rise_trans(houses.startSpeculum, -1, starName,
                               eflg, Star::angleTransitFlag(m),
                               geopos, 1013.25, 10,
                               &rettm, errStr) >= 0) {
                ret.angleTransit[m] = Planet::timeToDT(rettm);
            }
        }
    } else {
        qDebug("A: can't calculate position of '%s' at julian day %f: %s",
               qPrintable(ret.name), jd, errStr);
    }

    ret.house = getHouse(houses, ret.eclipticPos.x());

    return ret;
}

Houses
calculateHouses( const InputData& input )
{
    Houses ret;
    ret.system = &getHouseSystem(input.houseSystem());
    unsigned int flags = SEFLG_SWIEPH;
    if (input.zodiac() > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(input.zodiac() - 2, 0, 0);
    }

    double julianDay = getJulianDate(input.GMT(), false/*i.e., UT*/);
    double  jd = getJulianDate(input.GMT(), true/*i.e., ET*/);
    char    errStr[256] = "";
    double  xx[6];

    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];

    double hcusps[14], ascmc[11];

    // get the tropical ascendant...
    swe_houses_ex(julianDay, flags & ~SEFLG_SIDEREAL, 
                  input.location().y(), input.location().x(),
                  ret.system->sweCode, hcusps, ascmc);
    double asc = ascmc[0];   // tropical asc
    xx[0] = ascmc[0]; xx[1] = 0.0; xx[2] = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RAAC = xx[0];
    xx[0] = swe_degnorm(ascmc[0]+180); xx[1] = 0.0; xx[2] = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RADC = xx[0];
    // TODO could also get eastpoint and vertex in RA here...

    if (flags & SEFLG_SIDEREAL) {
        swe_houses_ex(julianDay, flags, input.location().y(), input.location().x(),
                      ret.system->sweCode, hcusps, ascmc);
    }

    for (int i = 0; i < 12; i++)
        ret.cusp[i] = hcusps[i+1];

    ret.Asc   = ascmc[0];
    ret.MC    = ascmc[1];
    ret.RAMC  = ascmc[2];
    ret.Vx    = ascmc[3];
    ret.EA    = ascmc[4];

    double st = swe_degnorm((swe_sidtime(julianDay)) * 15 + input.location().x());
    double frac;
    int deg, min, sec, sgn;
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    //qDebug("ST from GMT %3d %02d %02d", deg, min, sec);
    st = swe_degnorm(ret.RAMC);
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    //qDebug("ST from RAMC %3d %02d %02d", deg, min, sec);

    st = swe_degnorm(ret.RAAC);
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    //qDebug("RAAC %3d %02d %02d", deg, min, sec);
#if 1
    double DD = asind(sind(eps) * sind(asc));
    double AD = asind(tand(DD) * tand(input.location().y()));
    ret.OAAC = input.location().y() >= 0 ? (ret.RAAC - AD) : (ret.RAAC + AD);
    DD = asind(sind(eps) * sind(swe_degnorm(asc + 180)));
    AD = asind(tand(DD) * tand(input.location().y()));
    ret.ODDC = input.location().y() >= 0 ? (ret.RADC + AD) : (ret.RADC - AD);
#endif

    ret.halfMedium = swe_difdegn(ret.RAAC, ret.RAMC);
    ret.halfImum = 180 - ret.halfMedium;
    //ret.swneDelta = swe_degnorm(ret.RAMC - swe_degnorm(ret.RAAC - 180)) / 360.0;

    // compute house position of sun so we can see
    // whether it's closer to sunset or sunrise.

    // first get tropical position (well, latitude)
    swe_calc_ut(julianDay,SE_SUN,flags & ~SEFLG_SIDEREAL, xx, errStr);

    double geopos[3] = {
        input.location().x(),
        input.location().y(),
        input.location().z()
    };

    double housePos = swe_house_pos(ret.RAMC,geopos[1],eps, 'C',
                                    xx, errStr);

    // now get our speculum start
    int which = (housePos>=4 && housePos<10)? SE_CALC_RISE : SE_CALC_SET;
    swe_rise_trans(julianDay - 1, SE_SUN, NULL, SEFLG_SWIEPH,
                   which, geopos, 1013.25, 10,
                   &ret.startSpeculum, errStr);

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
    default: break;
    }

    if (receptionWith(planet, scope) != Planet_None)
        ret.dignity += 5;
    else if (peregrine)
        ret.deficient -= 5;


    switch (planet.house) {
    case 1: case 10:         ret.dignity += 5; break;
    case 4: case 7: case 11: ret.dignity += 4; break;
    case 2: case 5:          ret.dignity += 3; break;
    case 9:                  ret.dignity += 2; break;
    case 3:                  ret.dignity += 1; break;
    case 12:                 ret.deficient -= 5; break;
    case 8: case 6:          ret.deficient -= 2; break;
    default: break;
    }

    if (planet.eclipticSpeed.x() > 0
        && planet.id != Planet_Sun
        && planet.id != Planet_Moon) {
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
        if (isEarlier(planet, scope.sun))
            ret.dignity += 2;
        else
            ret.deficient -= 2;
        break;
    case Planet_Mercury:
    case Planet_Venus:
    case Planet_Moon:
        if (!isEarlier(planet, scope.sun))
            ret.dignity += 2;
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
    default: break;
    }

    if (planet.id != Planet_Venus)
    switch (aspect(planet, scope.venus, topAspectSet())) {
    case Aspect_Conjunction: ret.dignity += 5; break;
    case Aspect_Trine:       ret.dignity += 4; break;
    case Aspect_Sextile:     ret.dignity += 3; break;
    default: break;
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
    default: break;
    }

    if (planet.id != Planet_Saturn)
    switch (aspect(planet, scope.saturn, topAspectSet())) {
    case Aspect_Conjunction: ret.deficient -= 5; break;
    case Aspect_Opposition:  ret.deficient -= 4; break;
    case Aspect_Quadrature:  ret.deficient -= 3; break;
    default: break;
    }

    if (aspect(planet, scope.stars["Regulus"], tightConjunction()) == Aspect_Conjunction)
        ret.dignity += 6;                  // Regulus coordinates at 2000year: 29LEO50, +00.27'

    if (aspect(planet, scope.stars["Spica"], tightConjunction()) == Aspect_Conjunction)
        ret.dignity += 5;                  // Spica coordinates at 2000year: 23LIB50, -02.03'

    if (aspect(planet, scope.stars["Algol"], tightConjunction()) == Aspect_Conjunction)
        ret.deficient -= 5;                // Algol coordinates at 2000year: 26TAU10, +22.25'

    return ret;
}

Aspect
calculateAspect( const AspectsSet& aspectSet,
                 const Planet& planet1,
                 const Planet& planet2 )
{
    Aspect a;

    a.angle   = angle(planet1, planet2);
    a.d       = &getAspect(aspect(a.angle, aspectSet), aspectSet);
    a.orb     = fabs(a.d->angle - a.angle);
    a.planet1 = &planet1;
    a.planet2 = &planet2;
    a.applying  = towardsMovement(planet1, planet2) == (a.angle > a.d->angle);

    return a;
}

inline
Aspect
calculateAspect(const AspectsSet& asps,
                const Planet* p1,
                const Planet* p2)
{ return calculateAspect(asps, *p1, *p2); }

Aspect
calculateAspect(const AspectsSet& aspectSet,
                 const Loc *p1loc,
                 const Loc *p2loc )
{
    Aspect a;
    auto l1 = p1loc->loc;
    if (auto pl = dynamic_cast<const PlanetLoc*>(p1loc)) l1 = pl->rasiLoc();
    auto l2 = p2loc->loc;
    if (auto pl = dynamic_cast<const PlanetLoc*>(p2loc)) l2 = pl->rasiLoc();
    a.angle = angle(l1, l2);
    a.d = &getAspect(aspect(a.angle, aspectSet), aspectSet);
    a.orb = fabs(a.d->angle - a.angle);
    return a;
}

AspectList
calculateAspects( const AspectsSet& aspectSet,
                  const PlanetMap &planets )
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
                    &&*/ aspect(i.value(), j.value(), aspectSet) != Aspect_None)
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
calculateAspects(const AspectsSet& aspectSet,
                 const ChartPlanetPtrMap& planets)
{
    AspectList ret;

    for (auto it = planets.cbegin(); it != planets.cend(); ++it) {
        for (auto jit = std::next(it); jit != planets.cend(); ++jit) {
            if (aspect(it->second,jit->second,aspectSet)
                    != Aspect_None)
            {
                ret << calculateAspect(aspectSet, it->second, jit->second);
            }
        }
    }

    return ret;
}

AspectList
calculateAspects(const AspectsSet& aspectSet,
                 const PlanetMap& planets1,
                 const PlanetMap& planets2)
{
    AspectList ret;
    for (auto i = planets1.constBegin(); i != planets1.constEnd(); ++i) {
        //if ((i->id >= Planet_Sun && i->id <= Planet_Pluto) || i->id == Planet_Chiron) {
            for (auto j = planets2.constBegin(); j != planets2.constEnd(); ++j) {
                if (/*((j->id >= Planet_Sun && j->id <= Planet_Pluto)
                     || j->id == Planet_Chiron)
                    &&*/ aspect(i.value(), j.value(), aspectSet) != Aspect_None)
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
calculateHarmonic(double        h,
                  Houses&       houses,
                  PlanetMap&    planets,
                  bool          includeAsteroids = true,
                  bool          includeCentaurs = true)
{
    houses.cusp[0] = harmonic(h, houses.cusp[0]);
    for (int i = 1; i < 12; ++i) {
        // use equal house
        houses.cusp[i] = fmod(houses.cusp[i-1] + 30.0, 360.);
    }
    houses.Asc = harmonic(h, houses.Asc);
    houses.RAAC = harmonic(h, houses.RAAC);
    houses.MC = harmonic(h, houses.MC);
    houses.RAMC = harmonic(h, houses.RAMC);

    for (PlanetId id : getPlanets(includeAsteroids,includeCentaurs)) {
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

    for (unsigned i = 3; i <= sqrt(n); i = i + 2) {
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
    for (auto f: getPrimeFactors(n)) ss.emplace(f);
}

uintMSet
getAllFactors(unsigned h)
{
    uintMSet fs;
    fs.insert(1);
    for (unsigned i = 2, m = sqrt(h); i <= m; ++i) {
        if (h % i == 0) {
            fs.insert(i);
            fs.insert(h/i);
        }
    }
    return fs;
}

void
getAllFactors(unsigned n, uintSSet& ss)
{
    ss.clear();
    for (auto f: getAllFactors(n)) ss.emplace(f);
}

void
getAllFactorsAlt(unsigned n, uintSSet& ss)
{
    getAllFactors(n, ss);
    ss.emplace(n);
}


bool
hasPlanetGroupInLowerHarmonic(const PlanetHarmonics& harmonics,
                              unsigned int harmonic,
                              const PlanetSet& plist)
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
        if (hat != scope.harmonics.end()
            && hat->second.count(plist) != 0) 
        {
            return true;
        }
#endif
    }
    return false;
}

bool
meetsQuorum(const PlanetQueue& curr,
            const std::set<int>& needs,
            unsigned int quorum)
{
    std::map<int, int> counts;
    //std::map<int, bool> hasSolo;
    unsigned q = 0;
    //for (int need : needs) { counts[need] = 0; hasSolo[need] = false; }
    for (const PlanetLoc& pl : curr) {
        //if (pl.planet.notwo()) hasSolo[pl.planet.fileId()] = true;
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
         double orb,
         bool tightenForMidpoints = false)
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
                auto t = h; ++t;
                while (t != q.rend()
                       && !outOfOrb(*h, *t, orb, false)) {
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
typedef std::vector<quorumOrbCleanup> looper;
typedef std::pair<unsigned, PlanetGroups> harmonicResult;

class joiner {
    PlanetHarmonics& hx;
    std::vector<bool>& sieve;

    typedef std::list<ChartPlanetBitmap> cpbList;
    std::map<unsigned, cpbList> seen;

public:
    joiner(PlanetHarmonics& hxIn,
           std::vector<bool>& primeSieve) :
        hx(hxIn), sieve(primeSieve)
    { }

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
                bool found = false;
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
    std::vector<bool> ret(top+1,true);
    ret[0] = false;
    for (unsigned i = 2; i <= top; ++i) {
        if (!ret[i]) continue;
        for (unsigned j = 2*i; j <= top; j += i) {
            ret[j] = false;
        }
    }
    return ret;
}

uintMSet getPrimes(unsigned top)
{
    uintMSet ret;
    unsigned i = 0;
    for (auto&& p: getPrimeSieve(top)) {
        if (p) ret.insert(i);
        ++i;
    }
    return ret;
}

void
findHarmonics(const ChartPlanetMap& cpm,
              PlanetHarmonics& hx,
              const looper& loop,
              bool doMidpoints = false)
{
    std::set<int> needsFiles;
    for (const ChartPlanetId& cpi : cpm.keys()) {
        needsFiles.insert(cpi.fileId());
    }

    const unsigned minQuorum = harmonicsMinQuorum();
    const unsigned maxH = maxHarmonic();

    // Creates a prime sieve and a sequence of numbers which is has primes
    // less than or equal to the primeFactorLimit and non-primes with
    // no factors less than or equal to the primeFactorLimit.
    static std::vector<bool> primeSieve;
    static std::set<unsigned> seq;
    static unsigned lastMaxH = 0;
    static unsigned lastPFL = 0;
    if (maxH > lastMaxH || lastPFL != primeFactorLimit()) {
        std::set<unsigned> primes { 1 }, nonPrimes;
        primeSieve.assign(maxH+1,true);
        std::vector<bool> maxFactor(maxH+1,false);
        for (unsigned int i = 2; i <= maxH; ++i) {
            if (!primeSieve[i]) {
                if (!maxFactor[i]) nonPrimes.insert(i);
                continue;
            }
            bool beyond = i > primeFactorLimit();
            if (!beyond) primes.insert(i);
            for (unsigned int j = i+i; j <= maxH; j += i) {
                primeSieve[j] = false;
                maxFactor[j] = beyond;
            }
        }
        seq = primes;
        seq.insert(nonPrimes.cbegin(), nonPrimes.cend());
        lastPFL = primeFactorLimit();
    } else if (maxH < lastMaxH) {
        auto zap = seq.lower_bound(maxH+1);
        while (zap != seq.end()) {
            seq.erase(zap++);
        }
    }
    lastMaxH = maxH;

    auto computePositions = [&](unsigned h,
                                PlanetRange& allOfThem,
                                bool doMidpoints = false)
    {
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
                                           cpot.key().planetId(), loc2));
                allOfThem.insert(PlanetLoc(cpit.key().fileId(),
                                           cpot.key().planetId(),
                                           cpit.key().planetId(),
                                           fmod(loc2 + 180, 360.)));
            }
        }
    };

    auto cluster = [&](PlanetGroups& groups,
                       const quorumOrbCleanup& l,
                       const PlanetRange& allOfThem)
    {
        unsigned quorum;
        qreal orb;
        bool cleanup;
        std::tie(quorum,orb,cleanup) = l;

        PlanetSet plist;

        // Let's see if anything wraps around...
        ChartPlanetId seen;
        bool wrapped = false;
        PlanetQueue current;
        PlanetLoc last;
        if (!outOfOrb(*allOfThem.crbegin(), *allOfThem.cbegin(),  orb)) {
            auto next = allOfThem.crbegin();
            do {
                wrapped = true;
                last = *next;
                seen = last.planet;
                current.push_front(last);
            } while (++next != allOfThem.crend()
                     && !outOfOrb(*allOfThem.cbegin(), *next, orb));
        } else {
            last = *allOfThem.cbegin();
        }
        current.push_back(*allOfThem.cbegin());
        for (auto plit = allOfThem.cbegin(); ++plit != allOfThem.cend(); ) {
            const auto& pl = *plit;
            //const Planet& plan = getPlanet(pl.planet);
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

        if (meetsQuorum(current,needsFiles,quorum)) {
            groups.insert(current, minQuorum);
        }
    };

    std::function<harmonicResult(unsigned)> orbLoop = [&](unsigned h) {
        PlanetGroups groups;
        PlanetRange allOfThem;
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

    joiner j(hx, primeSieve);
    QFuture<uintMSet> f = /*uintSet foo =*/
        mappedReduced<uintMSet>(seq, orbLoop, j, OrderedReduce);
    f.waitForFinished();
}

void
findHarmonics(const ChartPlanetMap& cpm,
              PlanetHarmonics& hx)
{
    bool doMidpoints = includeMidpoints();
    int d = harmonicsMinQuorum() <= harmonicsMaxQuorum() ? 1 : -1;
    unsigned num = fabs(harmonicsMaxQuorum() - harmonicsMinQuorum()) + 1;
    qreal orb = harmonicsMinQOrb()*orbFactor();
    qreal lorbMin = log2(orb);
    qreal lorbMax = log2(harmonicsMaxQOrb()*orbFactor());
    qreal od = num < 2 ? 0 : pow(2, (lorbMax - lorbMin) / (num - 1));
    unsigned quorum = unsigned(harmonicsMinQuorum());
    unsigned i = 0;
    looper loop;
    while (i < num) {
        bool cleanup = (++i == num) && (num > 1) && filterFew();
        loop.push_back(quorumOrbCleanup(quorum,orb,cleanup));
        quorum = unsigned(int(quorum) + d);
        orb *= od;
    }
    findHarmonics(cpm,hx,loop,doMidpoints);
    for (auto it = hx.begin(); it != hx.end();) {
        if (it->second.empty()) hx.erase(it++); else ++it;
    }
}

void
calculateBaseChartHarmonic(Horoscope& scope)
{
    scope.houses = scope.housesOrig;
    scope.planets = scope.planetsOrig;

    const InputData& input(scope.inputData);
    if (scope.harmonic != 1.0 && aspectMode != amcGreatCircle) {
        calculateHarmonic(scope.harmonic, scope.houses, scope.planets);
    } else {
        findPlanetStarConfigs(scope.planets, scope.stars);
    }

    scope.sun = scope.planets[Planet_Sun];
    scope.moon = scope.planets[Planet_Moon];
    scope.mercury = scope.planets[Planet_Mercury];
    scope.venus = scope.planets[Planet_Venus];
    scope.mars = scope.planets[Planet_Mars];
    scope.jupiter = scope.planets[Planet_Jupiter];
    scope.saturn = scope.planets[Planet_Saturn];
    scope.uranus = scope.planets[Planet_Uranus];
    scope.neptune = scope.planets[Planet_Neptune];
    scope.pluto = scope.planets[Planet_Pluto];
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
Zhang, Z. (2011). An Improvement to the Brent’s Method. International Journal of Experimental Algorithms (IJEA), (2), 21–26.
Retrieved from http://www.cscjournals.org/csc/manuscript/Journals/IJEA/volume2/Issue1/IJEA-7.pdf
[This link appears to be invalid, though the artical is retrievable by googling
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
brentZhangStage(F& f,
                double lo, double hi,
                double f_lo, double f_hi,
                double& result,
                double tol = 1e-9)
{
    // Root not bound by the given guesses?
    if (f_lo * f_hi >= 0) return false;

    double s, f_s;
    do {
        double c = (lo + hi)/2;
        double f_c = f(c);
        if (fabs(f_lo - f_c) > tol
            && fabs(f_hi - f_c) > tol)
        {
            // f(a)!=f(c) and f(b)!=f(c)
            // Inverse quadratic interpolation
            s = lo*f_hi * f_c/((f_lo-f_hi)*(f_lo-f_c))
                + hi*f_lo * f_c/((f_hi-f_lo)*(f_hi-f_c))
                + c*f_lo * f_hi/((f_c-f_lo)*(f_c-f_hi));
            if (s < lo || s > hi) {
                // According to Stage, s can go awry in this
                // calculation (i.e., exceed lo-hi bounds),
                // so we need to get it back in
                // line one of three ways, of which
                s = c; // is the simplest option
            }
        } else {
            // Secant rule
            s = hi - f_hi*(hi-lo)/(f_hi-f_lo);
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
    } while (f_hi!=0 && f_lo!=0 && fabs(hi-lo) > tol);	// Convergence conditions

    result = hi; // either hi or lo could be returned,
    // as we are now at tolerated convergence.

    return true;
}

template <typename F>
bool
brentZhangStage(F& f,
                double lo, double hi,
                double& result,
                double tol = 1e-9)
{
    if (fabs(hi - lo) <= tol) return false;

    if (lo > hi) qSwap(lo,hi);

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
brentGlobalMin(F f, double lo, double hi, double guess,
               double m, double err, double tol, double& x)
{
    const auto macheps = std::numeric_limits<double>::epsilon();
    int k;
    double a0, a2, a3, d0, d1, d2, h, m2, p, q, qs, r, s,
            y, y0, y1, y2, y3, yb, z0, z1, z2;

    x = a0 = hi; a2 = lo;
    yb = y0 = f(hi);
    y = y2 = f(lo);
    if (y0 < y) y = y0; else x = lo;

    if ((m > 0) && (lo < hi)) {
        // nontrivial case
        m2 = 0.5 * (1 + 16 * macheps) * m;
        if ((guess <= lo) || (guess >= hi)) guess = 0.5 * (lo + hi);
        y1 = f(guess); k = 3; d0 = a2 - guess; h = 9/11;

        if (y1 < y) { x = guess; y = y1; }

        // main loop
        next:
        d1 = a2 - a0; d2 = guess - a0;
        z2 = hi - a2; z0 = y2 - y1; z1 = y2 - y0;
        p = r = d1 * d1 * z0 - d0 * d0 * z1;
        q = qs = 2 * (d0 * z1 - d1 * z0);

        // try to find lower value of f using quadratic interpolation
        if ((k > 100000) && (y < y2)) goto skip;

        retry:
        if (q * (r * (yb - y2) + z2 * q * ((y2-y)+tol))
            < z2*m2*r*(z2*q-r))
        {
            a3 = a2 + r/q;
            y3 = f(a3);
            if (y3 < y) x = a3, y = y3;
        }

        // with probability about 0.1 do a random search for a lower
        // value of f. Any reasonable random number generator can be
        // used in place of the one here (it need not be very good).

        skip:
        k = 1611*k;
        k = k - 1048576*(k/1048576);
        q = 1; r = (hi-lo)*(k/100000);

        if (r<z2) goto retry;

        // prepare to step as far as possible
        r = m2*d0*d1*d2; s = sqrt(((y2-y)+tol)/m2);
        h = 0.5*(1+h);
        p = h*(p+2*r*s); q = r+0.5*qs;
        r = -0.5*(d0+(z0+2.01*err)/(d0*m2));
        r = a2 + ((r<s || d0<0)? s : r);

        // it is safe to step to r, but we may try to step further
        a3 = (p*q>0)? a2+p/q : r;

        inner:
        if (a3<r) a3 = r;
        if (a3 > hi) { a3 = hi; y3 = yb; }
        else y3 = f(a3);
        if (y3 < y) { x = a3; y = y3; }
        d0 = a3 - a2;

        if (a3 > r) {
            // inspect the parabolic lower bound on f in (a2,a3)
            p = 2*(y2 - y3)/(m*d0);
            if ((fabs(p) < (1+9*macheps)*d0)
                && (0.5*m2*(d0*d0+p*p)
                    > (y2-y)+(y3-y)+2*tol))
            {
                // halve the step and try again
                a3 = 0.5*(a2+a3);
                h = 0.9*h;
                goto inner;
            }
        }
        if (a3 < hi) {
            // prepare for the next step
            a0 = guess; guess = a2; a2 = a3;
            y0 = y1; y1 = y2; y2 = y3;
            goto next;
        }
    }
    return y;
}

QDateTime
dateTimeFromJulian(double jd)
{
    int32 y, d, m;
    int32 hr, min, sec;
    double dsec;
    swe_jdut1_to_utc(jd, SE_GREG_CAL, &y, &m, &d, &hr, &min, &dsec);
    sec = dsec;
    int msec = int((dsec - double(sec)) * 1000.0);
    return QDateTime(QDate(y,m,d), QTime(hr,min,sec,msec), Qt::UTC);
}

namespace {
static bool s_quiet = false;
thread_local bool st_quiet = s_quiet;
}

struct calcPos {
    PlanetProfile& poses;
    uintmax_t _iter = 0;

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
            qDebug() << "calc iter:" << dtToString(dateTimeFromJulian(jd)) << "Ret:" << poses.speed();
        }
        return poses.speed();
    }
};

typedef std::pair<qreal,qreal> posSpd;
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
    int m = 100;

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
    calcPos cpos;
    calcSpd cspd;
    calcPosSpd ncpos;
    calcSpread csprd;

    PlanetProfile& poses;
    double& jd;

    static constexpr double tol = 5e-08;
    static constexpr int digits = std::numeric_limits<double>::digits;

    calcLoop(PlanetProfile& ps,
             double& jdate) :
        cpos(ps), cspd(ps), ncpos(ps), csprd(ps), poses(ps), jd(jdate)
    { }

#if 1 // old version
    bool operator()(double& begin,
                    double end,
                    double span,
                    double flo,
                    double splo,
                    bool cont)
    {
        qDebug() << "calcLoop: begin" << begin
                 << "end" << end
                 << "span" << span
                 << "flo" << flo
                 << "splo" << splo;

        using namespace boost::math::tools;

        bool done = false;
        double fhi, sphi;
        for (double& jdc = begin; !done && (cont || jdc<end);
             splo = sphi, flo = fhi, jdc += span)
        {
            fhi = cpos(jdc + span);
            sphi = poses.speed();
            if ((done = (fabs(poses.loc) <= tol))) {
                qDebug() << "  done by span convergence";
                jd = jdc+span/2;
                continue;
            }
            if (span <= tol) {
                qDebug() << "  zeno's paradox";
                return false;
            }
            if (sgn(flo)==sgn(fhi)) {
                qDebug() << "same sign"; continue;
            }
            if (abs(fhi) >= 170. && abs(flo) >= 170.) {
                qDebug() << "flo and fhi >= 170"; continue;
            }
            qDebug() << "sgn(flo)=" << sgn(flo)
                     << " sgn(splo)=" << sgn(splo)
                     << " sgn(fhi)=" << sgn(fhi)
                     << " sgn(sphi)=" << sgn(sphi);
            if (sgn(flo)!=sgn(splo) || sgn(fhi)!=-sgn(sphi)) {
#if 0
                done = brentZhangStage(cpos, jdc,jdc+span, flo, fhi, jd);
                if (done) qDebug() << "  done by brent";
#else
                double guess = jdc + (fabs(flo)/(fabs(flo)+fabs(fhi)))*span;
                uintmax_t iter = 20;
                try {
                    jd = newton_raphson_iterate(ncpos, guess, jdc, jdc + span,
                                                digits, iter);
                    done = fabs(poses[1]->loc - poses[0]->loc) <= tol
                            || span < tol;
                    if (done)
                        qDebug() << "  done by newton";
                } catch (...) { }
#endif
            }
            double b = jdc;
            if (!done) {
                done = operator()(b, jdc+span, span/4, flo, splo,false);
            }
        }
        if (!done) qDebug() << "  ran out clock";
        return done;
    }
#endif

    template <typename T> void calc(double j, T& ret);

    bool signsEqual(const posSpd& a, const posSpd& b) const
    { return sgn(a.first) == sgn(b.first);  }

    bool signsEqual(qreal a, qreal b) const
    { return sgn(a) == sgn(b); }

    bool speedSignsEqual(const posSpd& a, const posSpd& b) const
    { return sgn(a.second) == sgn(b.second); }

    bool speedSignsEqual(qreal, qreal) const { return true; }

    bool longDistance(const posSpd& a, const posSpd& b) const
    { return abs(a.first) >= 170. && abs(b.first) > 170.; }

    bool longDistance(qreal, qreal)
    { return false; }


    template <typename T> bool doIterativeCalc(double& jd,
                                               double jlo,
                                               double jhi,
                                               T lo,
                                               T hi);

    template <typename T>
    bool operator()(double& begin,
                    double end,
                    double span,
                    T lo,
                    bool cont)
    {
        qDebug() << "calcLoop: begin" << dtToString(dateTimeFromJulian(begin))
                 << "end" << dtToString(dateTimeFromJulian(end))
                 << "span" << span;

        bool done = false;
        T hi;
        for (double& jdc = begin; !done && (cont || jdc<end);
             lo = hi, jdc += span)
        {
            calc(jdc + span, hi);
            if (poses.needsFindMinimalSpread()) {
                if (doIterativeCalc(jd, jdc, jdc+span, lo, hi)) {
                    modalize<int> precise(csprd.m, 1000);
                    done = doIterativeCalc(jd, jdc, jdc+span, lo, hi);
                }
            } else {
                if ((done = (fabs(poses.loc) <= tol))) {
                    qDebug() << "  done by span convergence";
                    jd = jdc+span/2;
                    continue;
                }
                if (span <= tol) {
                    qDebug() << "  zeno's paradox";
                    return false;
                }
                if (signsEqual(lo, hi)) {
                    qDebug() << "same sign"; continue;
                }
                if (longDistance(lo, hi)) {
                    qDebug() << "flo and fhi >= 170"; continue;
                }
                //if (sgn(flo)!=sgn(splo) || sgn(fhi)!=-sgn(sphi)) {
                done = doIterativeCalc(jd, jdc, jdc+span, lo, hi);
                //}
                double b = jdc;
                if (!done) {
                    done = operator()(b, jdc+span, span/4, lo, false);
                }
            }
        }
        if (!done) qDebug() << "  ran out clock";
        return done;
    }
};

template <>
inline
void
calcLoop::calc<posSpd>(double j, posSpd& ret) { ret = ncpos(j); }

template <>
inline
void
calcLoop::calc<qreal>(double j, qreal& ret) { ret = cspd(j); }

template <>
inline
bool
calcLoop::doIterativeCalc<qreal>(double& jd,
                                 double jlo,
                                 double jhi,
                                 qreal lo,
                                 qreal hi)
{
    bool done = brentZhangStage(cspd, jlo, jhi, lo, hi, jd);
    if (done) qDebug() << "  done by brent";
    return done;
}

template <>
inline
bool
calcLoop::doIterativeCalc<posSpd>(double& jd,
                                  double jlo,
                                  double jhi,
                                  posSpd lo,
                                  posSpd hi)
{
    using namespace boost::math::tools;

    double span = jhi-jlo;
    double g = jlo + (fabs(lo.first)/(fabs(lo.first)+fabs(hi.first)))*span;

    uintmax_t iter = 20;
    try {
        bool done = false;
        if (poses.needsFindMinimalSpread()) {
            constexpr auto tol = double(std::numeric_limits<float>::epsilon());
            brentGlobalMin(csprd, jlo, jhi, jlo/2.+jhi/2.,
                           csprd.m, .0000001/*err*/, tol, jd);
            if (csprd.poses.computeSpread(jd) <= harmonicsMinQOrb()) {
                done = true;
                qDebug() << "  done by brentGlobalMin";
            }
        } else {
            jd = newton_raphson_iterate(ncpos, g, jlo, jhi, digits, iter);
            done = fabs(poses[1]->loc - poses[0]->loc) <= tol
                    || span < tol;
            if (done)
                qDebug() << "  done by newton";
        }
        return done;
    } catch (...) {
        return false;
    }
}

void
calculateOrbAndSpan(const PlanetProfile& poses,
                    const InputData& locale,
                    double harmonic,
                    double& orb,
                    double& horb,
                    double& span)
{
    if (aspectMode == amcPrimeVertical) horb = .5;
    float speed = poses.defaultSpeed();
    orb = 360 / speed / harmonic;
    horb = orb / 1.8;

    //auto plid = poses.back()->planet.planetId();
    if (true/*plid == Planet_Sun || plid==Planet_Moon*/) span = orb/4;
    else span = 1/speed;

    qDebug() << poses[0]->description() << "half-orbit" << horb << "days";
}

QDateTime
calculateClosestTime(PlanetProfile& poses,
                     const InputData& locale,
                     double harmonic)
{

    double jdIn = getJulianDate(locale.GMT());

    double jd = jdIn;
    calcLoop looper(poses, jd);

    double orb, horb, span;
    calculateOrbAndSpan(poses, locale, harmonic, orb, horb, span);

    double begin = jdIn-orb/2;
    double end = begin + horb*2;

    calcPos cpos(poses);
    double flo = cpos(begin);
    double splo = poses.speed();

    //if (looper(begin,end,span,flo,true))
    if (looper(begin, end, span, flo, splo, true/*cont*/))
        return dateTimeFromJulian(jd);

    return locale.GMT();
}

QList<QDateTime>
quotidianSearch(PlanetProfile& poses,
                const InputData& locale,
                const QDateTime& endDT,
                double span /*= 1.0*/,
                bool forceMin)
{
    modalize<bool> mum(st_quiet,true);

    double jd1 = getJulianDate(locale.GMT());
    double jd2 = getJulianDate(endDT);

    poses.setForceMinimize(forceMin);
    if (poses.needsFindMinimalSpread()) span *= 2.; else span /= 4.;

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

    double jd { };
    calcLoop looper(poses, jd);
    QList<QDateTime> ret;
    auto loop = [&](auto lo) {
        //modalize<int> precise(looper.csprd.m,1000);
        looper.calc(jd1, lo);
        do {
            if (looper(jd1,jd2,span,lo,false)) {
                auto dt = dateTimeFromJulian(jd);
                if (poses.needsFindMinimalSpread()
                        && !ret.isEmpty()
                        && qAbs(dt.secsTo(ret.back())) < 86400)
                {
                    auto ugh = qAbs(dt.secsTo(ret.back()));
                    qDebug() << ugh << "Let's scrunch up"
                             << dtToString(dt) << "and"
                             << dtToString(ret.back());
                    // XXX goofy hack to ignore adjacent values
                    auto hi = lo;
                    auto jda = jd - .5;
                    looper.calc(jda,lo);
                    auto jdb = jd + .5;
                    looper.calc(jdb,hi);
                    modalize<int> precise(looper.csprd.m,1000);
                    bool found = looper.doIterativeCalc(jd,jda,jdb,lo,hi);
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

    if (poses.size()==1) {
        auto l = *poses.begin();
        // looking for stations...
        if (l->inMotion()) {
            qreal lo { };
            loop(lo);
        }
    } else {
        posSpd lo;
        loop(lo);
    }
    return ret;
}

QDateTime
calculateReturnTime(PlanetId id,
                    const InputData& native,
                    const InputData& locale,
                    double harmonic)
{
    modalize<bool> mum(st_quiet,true);

    PlanetProfile poses;
    poses.push_back(new NatalLoc(id, native));
    poses.push_back(new TransitPosition(id, locale));

    return calculateClosestTime(poses, locale, harmonic);
}


Horoscope
calculateAll(const InputData& input)
{
    Horoscope scope;
    scope.inputData = input;
    scope.houses = calculateHouses(input);
    scope.zodiac = getZodiac(input.zodiac());

    for (PlanetId id : getPlanets(true,true)) {
        if (id == Planet_Asc) {
            Planet asc = Data::getPlanet(id);
            asc.eclipticPos.setX(scope.houses.Asc);
            asc.equatorialPos.setX(scope.houses.RAAC);
            asc.pvPos = 0;
            scope.planets[id] = asc;
        } else if (id == Planet_Desc) {
            Planet desc = Data::getPlanet(id);
            desc.eclipticPos.setX(swe_degnorm(180.+scope.houses.Asc));
            desc.equatorialPos.setX(swe_degnorm(180.+scope.houses.RAAC));
            desc.pvPos = 180;
            scope.planets[id] = desc;
        } else if (id == Planet_MC) {
            Planet mc = Data::getPlanet(id);
            mc.eclipticPos.setX(scope.houses.MC);
            mc.equatorialPos.setX(scope.houses.RAMC);
            mc.pvPos = 270;
            scope.planets[id] = mc;
        } else if (id == Planet_IC) {
            Planet ic = Data::getPlanet(id);
            ic.eclipticPos.setX(swe_degnorm(180.+scope.houses.MC));
            ic.equatorialPos.setX(swe_degnorm(180.+scope.houses.RAMC));
            ic.pvPos = 90;
            scope.planets[id] = ic;
        } else if (id > Planet_Asc && id <= House_12) {
            Planet hc = Data::getPlanet(id);
            hc.eclipticPos.setX(scope.houses.cusp[id - Planet_Asc]);
            hc.equatorialPos.setX(scope.houses.cusp[id - Planet_Asc]); // XXX
            hc.pvPos = 30 * (id - Planet_Asc);
            hc.house = id - Planet_Asc;
            scope.planets[id] = hc;
        } else {
            scope.planets[id] =
                    calculatePlanet(id, input,
                                    scope.houses,
                                    scope.zodiac);
        }
    }

    for (const QString& name : qAsConst(getStars())) {
        scope.stars[name.toStdString()] =
            calculateStar(name, input, scope.houses, scope.zodiac);
    }

    if (scope.planets.contains(-1)) {
        qDebug() << "Wha?";
    }

    scope.housesOrig = scope.houses;
    scope.planetsOrig = scope.planets;

    calculateBaseChartHarmonic(scope);

    if (scope.planets.contains(-1)) {
        qDebug() << "Wha?";
    }

    return scope;
}

EventOptions::EventOptions(const QVariantMap& map)
{
    defaultTimespan = map.value("Events/defaultTimespan").toString();
    expandShowOrb = map.value("Events/secondaryOrb").toDouble();
    planetPairOrb = map.value("Events/planetPairOrb").toDouble();
    patternsQuorum = map.value("Events/patternsQuorum").toUInt();
    patternsSpreadOrb = map.value("Events/patternsSpreadOrb").toDouble();
    patternsRestrictMoon = map.value("Events/patternsRestrictMoon").toBool();
    includeMidpoints = map.value("Events/includeMidpoints").toBool();
    showStations = map.value("Events/showStations").toBool();
    includeShadowTransits = map.value("Events/includeShadowTransits").toBool();
    showTransitsToTransits = map.value("Events/showTransitsToTransits").toBool();
    limitLunarTransits = map.value("Events/limitLunarTransits").toBool();
    showTransitsToNatalPlanets = map.value("Events/showTransitsToNatalPlanets").toBool();
    includeOnlyOuterTransitsToNatal = map.value("Events/includeOnlyOuterTransitsToNatal").toBool();
    includeAsteroids = map.value("Events/includeAsteroids").toBool();
    includeCentaurs = map.value("Events/includeCentaurs").toBool();
    showTransitsToNatalAngles = map.value("Events/showTransitsToNatalAngles").toBool();
    showTransitsToHouseCusps = map.value("Events/showTransitsToHouseCusps").toBool();
    showReturns = map.value("Events/showReturns").toBool();
    showProgressionsToProgressions = map.value("Events/showProgressionsToProgressions").toBool();
    showProgressionsToNatal = map.value("Events/showProgressionsToNatal").toBool();
    includeOnlyInnerProgressionsToNatal = map.value("Events/includeOnlyInnerProgressionsToNatal").toBool();
    showTransitAspectPatterns = map.value("Events/showTransitAspectPatterns").toBool();
    showTransitNatalAspectPatterns = map.value("Events/showTransitNatalAspectPatterns").toBool();
    showIngresses = map.value("Events/showIngresses").toBool();
    showLunations = map.value("Events/showLunations").toBool();
    showHeliacalEvents = map.value("Events/showHeliacalEvents").toBool();
    showPrimaryDirections = map.value("Events/showPrimaryDirections").toBool();
    showLifeEvents = map.value("Events/showLifeEvents").toBool();
    expandShowAspectPatterns = map.value("Events/expandShowAspectPatterns").toBool();
    expandShowHousePlacementsOfTransits = map.value("Events/expandShowHousePlacementsOfTransits").toBool();
    expandShowRulershipTips = map.value("Events/expandShowRulershipTips").toBool();
    expandShowStationAspectsToTransits = map.value("Events/expandShowStationAspectsToTransits").toBool();
    expandShowStationAspectsToNatal = map.value("Events/expandShowStationAspectsToNatal").toBool();
    expandShowReturnAspects = map.value("Events/expandShowReturnAspects").toBool();
    expandShowTransitAspectsToReturnPlanet = map.value("Events/expandShowTransitAspectsToReturnPlanet").toBool();
}

QVariantMap
EventOptions::toMap()
{
    QVariantMap ret;
    ret.insert("Events/defaultTimespan", defaultTimespan.toString());
    ret.insert("Events/secondaryOrb",                   expandShowOrb);
    ret.insert("Events/planetPairOrb",                  planetPairOrb);
    ret.insert("Events/patternsQuorum",                 patternsQuorum);
    ret.insert("Events/patternsSpreadOrb",              patternsSpreadOrb);
    ret.insert("Events/patternsRestrictMoon",           patternsRestrictMoon);
    ret.insert("Events/includeMidpoints",               includeMidpoints);
    ret.insert("Events/showStations",                   showStations);
    ret.insert("Events/includeShadowTransits",          includeShadowTransits);
    ret.insert("Events/showTransitsToTransits",         showTransitsToTransits);
    ret.insert("Events/limitLunarTransits",             limitLunarTransits);
    ret.insert("Events/showTransitsToNatalPlanets",     showTransitsToNatalPlanets);
    ret.insert("Events/includeOnlyOuterTransitsToNatal", includeOnlyOuterTransitsToNatal);
    ret.insert("Events/includeAsteroids",               includeAsteroids);
    ret.insert("Events/includeCentaurs",                includeCentaurs);
    ret.insert("Events/showTransitsToNatalAngles",      showTransitsToNatalAngles);
    ret.insert("Events/showTransitsToHouseCusps",       showTransitsToHouseCusps);
    ret.insert("Events/showReturns",                    showReturns);
    ret.insert("Events/showProgressionsToProgressions", showProgressionsToProgressions);
    ret.insert("Events/showProgressionsToNatal",        showProgressionsToNatal);
    ret.insert("Events/includeOnlyInnerProgressionsToNatal", includeOnlyInnerProgressionsToNatal);
    ret.insert("Events/showTransitAspectPatterns",      showTransitAspectPatterns);
    ret.insert("Events/showTransitNatalAspectPatterns", showTransitNatalAspectPatterns);
    ret.insert("Events/showIngresses",                  showIngresses);
    ret.insert("Events/showLunations",                  showLunations);
    ret.insert("Events/showHeliacalEvents",             showHeliacalEvents);
    ret.insert("Events/showPrimaryDirections",          showPrimaryDirections);
    ret.insert("Events/showLifeEvents",                 showLifeEvents);

    ret.insert("Events/expandShowAspectPatterns",       expandShowAspectPatterns);
    ret.insert("Events/expandShowHousePlacementsOfTransits", expandShowHousePlacementsOfTransits);
    ret.insert("Events/expandShowRulershipTips",        expandShowRulershipTips);
    ret.insert("Events/expandShowStationAspectsToTransits", expandShowStationAspectsToTransits);
    ret.insert("Events/expandShowStationAspectsToNatal", expandShowStationAspectsToNatal);
    ret.insert("Events/expandShowReturnAspects",        expandShowReturnAspects);
    ret.insert("Events/expandShowTransitAspectsToReturnPlanet", expandShowTransitAspectsToReturnPlanet);
    return ret;
}

OmnibusFinder::OmnibusFinder(HarmonicEvents& evs,
                             const ADateRange& range,
                             const uintSSet& hset,
                             const AstroFileList& files) :
    AspectFinder(evs, range, hset, afcFindStuff)
{
    // This ugly jumble intends to generate the appropriate planet listings,
    // and then create the T-T T-N P-P P-N pairings. And the ingresses, etc.
    // Better to have some kind of factory scheme, but for now...
    bool natal = false, trans = false, prog = false;
    int natus = -1, locus = -1, progr = -1;
    QMap<ChartPlanetId, unsigned> index, revIndex;

    uintSSet conjSet { 1 };
    hsetId conj = _hsets.size();
    _hsets.emplace_back(conjSet);

    uintSSet conjOppSet { 1, 2 };
    hsetId conjOpp = _hsets.size();
    _hsets.emplace_back(conjOppSet);

    uintSSet conjOppSqSet { 1, 2, 4 };
    hsetId conjOppSq = _hsets.size();
    _hsets.emplace_back(conjOppSqSet);

    hsetId allAsp = _hsets.size();
    _hsets.emplace_back(hset);  // the all aspects set...

    double njd = 0;
    for (int i = 0, n = files.count(); i < n; ++i) {
        auto f = files.at(i);
        _ids.push_back(f->horoscope().inputData);

        const auto& ida = f->horoscope().inputData;
        auto type = f->getType();
        if (type == TypeMale || type == TypeFemale || type == TypeEvent) {
            natus = i, natal = true;
            njd = getJulianDate(ida.GMT());
        }
        else if (type == TypeDerivedProg) progr = i, prog = true;
        else locus = i, trans = true;
    }

    QVector<ZodiacSign> signs = getZodiac(_ids[0].zodiac()).signs.toVector();
    auto getIngress = [&](PlanetId ingr, bool forward = true) {
        unsigned i = ingr - Ingresses_Start;
        if (!forward) ingr += 12;
        ChartPlanetId cpid(-1, ingr, Planet_None);
        QMap<ChartPlanetId, unsigned>& inx = forward? index : revIndex;
        if (!inx.contains(cpid)) {
            inx[cpid] = _alist.size();
            qreal loc = forward? signs[i].startAngle
                               : signs[(i+1)%12].startAngle;
            auto pl = new PlanetLoc(cpid, "I", loc);
            pl->allowAspects = forward
                    ? PlanetLoc::aspOnlyDirect
                    : PlanetLoc::aspOnlyRetro;
            _alist.push_back(pl);
        }
        return inx.value(cpid);
    };

    Houses houses;  // natal houses if needed

    typedef std::function<unsigned(PlanetId)> getter;
    getter getNatalPlanet = nullptr;
    getter getTransitPlanet = nullptr;
    getter getProgressedPlanet = nullptr;

    typedef std::function<unsigned(PlanetId,bool)> getterAlt;
    getterAlt getHouseIngress = nullptr;
    if (natal) {
        getNatalPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(natus, pid, Planet_None);
            if (!index.contains(cpid)) {
                index[cpid] = _alist.size();
                auto pl = new NatalPosition(cpid, _ids[natus], "r");
                if (pid >= Houses_Start && pid < Houses_End) {
                    pl->allowAspects = PlanetLoc::aspOnlyConj;
                }
                _alist.push_back(pl);
            }
            return index.value(cpid);
        };

        if (natal && showTransitsToHouseCusps) {
            houses = calculateHouses(_ids[natus]);
            getHouseIngress = [&](PlanetId ingr, bool forward = true) {
                ChartPlanetId cpid(-1, ingr, Planet_None);
                QMap<ChartPlanetId, unsigned>& inx = forward? index : revIndex;
                if (!inx.contains(cpid)) {
                    inx[cpid] = _alist.size();
                    unsigned i = ingr - Houses_Start;
                    qreal loc = forward? houses.cusp[i]
                                       : houses.cusp[(i+1)%12];
                    auto pl = new PlanetLoc(cpid, "HI", loc);
                    pl->allowAspects = forward
                            ? PlanetLoc::aspOnlyDirect
                            : PlanetLoc::aspOnlyRetro;
                    _alist.push_back(pl);
                }
                return inx.value(cpid);
            };
        }
    }
    if (!trans && prog) { locus = progr; trans = true; }
    else if (!trans && natal) { locus = natus; trans = true; }
    if (trans) {
        getTransitPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(locus, pid, Planet_None);
            if (!index.contains(cpid)) {
                index[cpid] = _alist.size();
                _alist.push_back(new TransitPosition(cpid, _ids[locus]));
            }
            return index.value(cpid);
        };
    }
    if (!prog && trans) { progr = locus; prog = true; }
    if (natal) {
        getProgressedPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(progr, pid, Planet_None);
            if (!index.contains(cpid)) {
                index[cpid] = _alist.size();
                auto pl = new ProgressedPosition(cpid, _ids[natus], njd);
                if (pid >= Houses_Start && pid < Houses_End) {
                    pl->allowAspects = PlanetLoc::aspOnlyConj;
                }
                _alist.push_back(pl);
            }
            return index.value(cpid);
        };
    }

    if (trans && (showTransitsToTransits
                  || showTransitAspectPatterns
                  || showTransitNatalAspectPatterns
                  || showStations
                  || showIngresses))
    {
        QVector<unsigned> ppi, ppo;
        for (auto pid: getPlanets(includeAsteroids,includeCentaurs)) {
            ppi << getTransitPlanet(pid);
        }
        if (false && includeOnlyOuterTransitsToNatal) {
            for (auto pid: getOuterPlanets(includeCentaurs)) {
                ppo << getTransitPlanet(pid);
            }
            //if (!showTransitsToNatalPlanets) ppi = ppo; // only outer-to-outer!
        } else ppo = ppi;
        // the above loop has added the planets to the list used
        // by pattern or station finder

        int in = ppi.size();
        int on = ppo.size();
        if (showTransitsToTransits) {
            for (int i = 0; i < in; ++i) {
                hsetId hs = allAsp;
                auto tp = dynamic_cast<TransitPosition*>(_alist[ppi[i]]);
                auto pl = tp->planet.planetId();
                if (pl == Planet_NorthNode || pl == Planet_SouthNode
                        || (pl > Planet_Moon && pl <= Planet_Jupiter))
                {
                    hs = conj;
                } else if (limitLunarTransits && pl == Planet_Moon) {
                    continue;
                }
                for (int j = qMax(0,i+1-(in-on)); j < on; ++j) {
                    if (ppi[i] == ppo[j]) continue;
                    auto hst = hs;
                    auto tp = dynamic_cast<TransitPosition*>(_alist[ppo[j]]);
                    auto opl = tp->planet.planetId();
                    if (opl == Planet_NorthNode || opl == Planet_SouthNode
                            || (opl > Planet_Moon && opl <= Planet_Jupiter)) {
                        if (hs == conj) continue;
                        hst = conj;
                    }
                    if (opl == Planet_Moon) {
                        hst = conjOppSq;
                    }
                    //else if (opl == Planet_Sun) hst = conjOpp;
                    _staff.emplace_back(ppi[i], ppo[j], hst,
                                        etcTransitToTransit);
                }
            }
        }

        if (showIngresses) {
            for (auto i: qAsConst(ppi)) {
                auto tp = dynamic_cast<TransitPosition*>(_alist[i]);
                auto pl = tp->planet.planetId();
                for (PlanetId pid = Ingresses_Start; pid < Ingresses_End; ++pid) {
                    if (limitLunarTransits && pl==Planet_Moon
                            && ((pid - Ingresses_Start) % 3 != 0))
                        continue;

                    auto j = getIngress(pid);
                    _staff.emplace_back(i, j, conj, etcSignIngress);

                    // luminaries don't need the backwards ingress
                    if (pl==Planet_Sun || pl==Planet_Moon) continue;

                    j = getIngress(pid, false/*backward*/);
                    _staff.emplace_back(i, j, conj, etcSignIngress);
                }
            }
        }
    }

    if (showProgressionsToProgressions) {
        QVector<unsigned> tpi;
        for (auto pid: getPlanets()) {
            tpi << getProgressedPlanet(pid);
        }
        for (int i = 0; i < tpi.size(); ++i) {
            hsetId hs = allAsp;
#if 0
            if (limitLunarTransits) {
                auto tp = dynamic_cast<TransitPosition*>(_alist[ppi[i]]);
                if (tp->planet.planetId() == Planet_Moon) hs = conjOpp;
            }
#endif
            for (int j = i+1; j < tpi.size(); ++j) {
                _staff.emplace_back(i, j, hs, etcProgressedToProgressed);
            }
        }
    }

    if (natal) {
        if (showTransitsToNatalPlanets
                || showTransitNatalAspectPatterns
                || showTransitsToHouseCusps
                || showTransitsToNatalAngles
                || showReturns)
        {
            QList<PlanetId> npl;
            if (showTransitsToNatalPlanets || showTransitNatalAspectPatterns)
                npl << getPlanets();

            QVector<unsigned> ppn;
            for (auto pid: qAsConst(npl)) {
                ppn << getNatalPlanet(pid);
            }

            if (!showTransitsToNatalPlanets) ppn.clear();

            if (showTransitsToNatalPlanets
                    || showTransitsToNatalAngles
                    || showTransitsToHouseCusps)
            {
                QList<PlanetId> tpl;
                if (includeOnlyOuterTransitsToNatal) {
                    tpl = getOuterPlanets(includeCentaurs);
                } else {
                    tpl = getPlanets(includeAsteroids,includeCentaurs);
                }

                for (auto pid: qAsConst(tpl)) {
                    auto i = getTransitPlanet(pid);
                    for (auto j: qAsConst(ppn)) {
                        auto tp = dynamic_cast<TransitPosition*>(_alist[i]);
                        auto np = dynamic_cast<NatalPosition*>(_alist[j]);
                        if (tp->planet.planetId() != np->planet.planetId()
                                || !showReturns)
                        {
                            auto hs = allAsp;
                            auto pl = np->planet.planetId();
                            if (pl == Planet_NorthNode || pl == Planet_SouthNode)
                                hs = conj;
                            else {
                                pl = tp->planet.planetId();
                                if (pl == Planet_NorthNode || pl == Planet_SouthNode)
                                    hs = conj;
                            }
                            _staff.emplace_back(i, j, hs, etcTransitToNatal);
                        }
                    }
                    if (showTransitsToHouseCusps) {
                        for (int h = Houses_Start; h < Houses_End; ++h) {
                            // FIXME retrograde to prior house, etc. like sign ingr
                            _staff.emplace_back(i, getHouseIngress(h,true), conj,
                                                etcHouseIngress);
                            _staff.emplace_back(i, getHouseIngress(h,false), conj,
                                                etcHouseIngress);
                        }
                    } else if (showTransitsToNatalAngles) {
                        for (auto a: getAngles()) {
                            _staff.emplace_back(i, getNatalPlanet(a), conj,
                                                etcTransitToNatal);
                        }
                    }
                }
            }
            if (showReturns) {
                QList<PlanetId> tpl = getPlanets();
                for (auto pid: qAsConst(tpl)) {
                    auto i = getTransitPlanet(pid);
                    auto j = getNatalPlanet(pid);
                    hsetId hs = conjOpp;
                    auto tp = dynamic_cast<TransitPosition*>(_alist[i]);
                    auto pl = tp->planet.planetId();
                    if (pl==Planet_Sun /*|| pl==Planet_Moon*/)
                        hs = conjOppSq;
                    else if (pl==Planet_NorthNode
                             || pl==Planet_SouthNode)
                        hs = conj;
                    _staff.emplace_back(i, j, hs, etcReturn);
                }
            }
        }

        if (showProgressionsToNatal) {
            QList<PlanetId> ppl;
            if (includeOnlyInnerProgressionsToNatal)
                ppl = getInnerPlanets(includeAsteroids);
            else ppl = getPlanets(includeAsteroids,includeCentaurs);

            auto npl = getPlanets(includeAsteroids,includeCentaurs);
            npl << getAngles();

            for (auto pid: qAsConst(ppl)) {
                auto i = getProgressedPlanet(pid);
                for (auto npid: qAsConst(npl)) {
                    auto j = getNatalPlanet(npid);
                    _staff.emplace_back(i, j,allAsp,etcProgressedToNatal);
                }
            }
        }
    }

#if 1
    QMap<hsetId,QStringList> hsm;
    for (const auto& ij : _staff) {
        unsigned i, j;
        std::tie(i,j) = ij;
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

        auto& hs = hsm[ij.hsid];
        if (hs.empty()) {
            for (auto h: _hsets[ij.hsid]) hs << QString::number(h);
        }
        auto what = QString("H{%1} %2=%3 %4")
                .arg(hs.join(","))
                .arg(_alist[i]->description())
                .arg(_alist[j]->description())
                .arg(EventTypeManager::eventTypeToString(ij.et));

        qDebug() << what;
    }
#endif
}

void AspectFinder::prepThread()
{
#if MSDOS
    char ephePath[] = "swe\\";
#else
    char ephePath[] = "swe/";
#endif
    swe_set_ephe_path(ephePath);
}

void AspectFinder::releaseThread()
{
    swe_close();
}


void
AspectFinder::findStations()
{
    QThreadPool tp;

    const auto& start = _range.first;
    const auto& end = _range.second.addDays(1);
    auto d = start.startOfDay().toUTC();
    auto e = end.startOfDay().toUTC();

    modalize<bool> mum(st_quiet, true);

    double jd = getJulianDate(d);
    for (auto tp: _alist) (*tp)(jd, 1);   // the horror

    PlanetProfile b = _alist;

    auto useRate = 15;  // search every 15 days
    if (!st_quiet) qDebug() << "sta" << dtToString(d);

    double pjd = jd;
    int ndays = int(useRate);
    int nsecs = (useRate - double(ndays)) * 24.*60.*60.;
    auto nd = d.addDays(ndays).addSecs(nsecs);
    std::list<PlanetLoc*> stations;
    unsigned in = _alist.size();
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
        for (auto tp: b) (*tp)(jd, 1);

        std::set<unsigned> stationChecked;
        for (unsigned i = 0; i < in; ++i) {
            if (stationChecked.count(i)!=0) continue;
            stationChecked.insert(i);

            auto pl = dynamic_cast<TransitPosition*>(_alist[i]);
            if (!pl) continue;

            auto pid = pl->planet.planetId();
            if (pid <= Planet_Moon
                    || pid == Planet_NorthNode || pid == Planet_SouthNode
                    || pid >= Planets_End)
            { continue; }

            auto aspd = _alist[i]->speed;
            auto bspd = b[i]->speed;
            if (!st_quiet) {
                qDebug() << "  " << _alist[i]->description() << aspd << bspd;
            }
            if (sgn(aspd) == sgn(bspd)) continue;

            bool wasRetro = aspd < 0;
            _evs.emplace_back();
            auto& ev = _evs.back();
#if 1
            tp.start([=, &stations, &ev] {
                startTask();
                modalize<bool> mum(st_quiet, true);
#endif

                auto pj = dynamic_cast<PlanetLoc*>(_alist[i]->clone());
                pj->desc = QString("S") + (wasRetro? 'D' : 'R');

                auto cspd = [&pj](double jd) {
                    (*pj)(jd,1);
                    return pj->speed;
                };

                double tjd = jd/2.+pjd/2.;
                if (brentZhangStage(cspd, pjd, jd, aspd, bspd, tjd)) {
                    auto qdt = dateTimeFromJulian(tjd);
                    auto ploc = dynamic_cast<PlanetLoc*>(pj);

                    auto dt = dtToString(qdt);
                    PlanetRangeBySpeed plr { *ploc };

                    ev = HarmonicEvent(qdt, etcStation, 1, std::move(plr));

                    if (!st_quiet) qDebug() << dt << pj->description();

                    if (includeShadowTransits) {
                        // Add shadow-period transit lookup
                        QMutexLocker mlb(&_ctm);
                        auto pj = new KnownPosition(ploc, tjd,
                                                    wasRetro? "IN" : "EX");
                        pj->planet.setFileId(i);
                        pj->allowAspects = PlanetLoc::aspOnlyDirect;
                        pj->speed = 0;
                        stations.emplace_back(pj);
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

        d = nd;
        nd = d.addDays(ndays).addSecs(nsecs);
        //nd = d.addDays(_rate);
        pjd = jd;

        _alist.swap(b);
    }

    bool cleared = false;
    if (_state == cancelRequestedState) {
        tp.clear();
        cleared = true;
    }

    int active(_numTasks);
    qDebug() << active << "activity/ies";
    while (!tp.waitForDone(100)) {
        QCoreApplication::processEvents();
        if (!cleared && _state == cancelRequestedState) {
            tp.clear();
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

    for (auto pj: stations) {
        int i = pj->planet.fileId();
        int j = int(_alist.size());
        _alist.push_back(pj);
        pj->planet.setFileId(-1);   // hides it from clusterer
        _staff.emplace_back(i, j,0,etcTransitToStation);
        qDebug() << "Added transit search for" << i << j
                 << QString("H1 %1=%2")
                    .arg(_alist[i]->description())
                    .arg(_alist[j]->description());
    }
}

std::ostream &
operator<<(std::ostream& os, const PlanetClusterMap& pcm)
{
    os << "(";
    bool any = false;
    for (const auto& pc : pcm) {
        if (any) os << ",\n\t";
        os << pc.first.names().join("=").toStdString()
           << " spread " << pc.second;
        any = true;
    }
    os << ")";
    return os;
}

void AspectFinder::findAspectsAndPatterns()
{
    if (_alist.empty()) return;
    
    PlanetSet nats, trans;
    PlanetProfile b = _alist;
    bool skipAllNatalOnly = false;
    if (!showTransitAspectPatterns && showTransitNatalAspectPatterns) {
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<NatalPosition*>(pl);
            if (!pla || pla->inMotion()) continue;
            nats.emplace(pla->planet);
        }
        skipAllNatalOnly = true;
    } else if (showTransitAspectPatterns
               && (!showTransitNatalAspectPatterns
                   || _ids.size()==1))
    {
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<TransitPosition*>(pl);
            if (!pla || !pla->inMotion()) continue;
            trans.emplace(pla->planet);
        }
    } else if (showTransitAspectPatterns && showTransitNatalAspectPatterns) {
        bool anyt = false, anyr = false;
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<PlanetLoc*>(pl);
            if (!pla) continue;
            (pla->inMotion()? anyt : anyr) = true;
            if (anyt && anyr) break;
        }
        skipAllNatalOnly = true;
    }
    bool showPatterns = showTransitAspectPatterns || !nats.empty();

    auto utp = std::unique_ptr<QThreadPool>(new QThreadPool);
    QThreadPool& tp = *utp.get();

    auto itc = QThread::idealThreadCount();
    qDebug() << "Ideal thread count" << itc;
    tp.setMaxThreadCount(itc);

    const auto& start = _range.first;
    auto end = _range.second;
    if (start == end) end = end.addDays(1);
    auto d = start.startOfDay().toUTC(), e = end.startOfDay().toUTC();

    double jd = getJulianDate(d);
    double bjd = jd;
    double ejd = getJulianDate(e);
    for (auto tp: _alist) (*tp)(jd, 1);   // the horror

    auto hs = *_hsets.crbegin();
    unsigned maxH =
            (hs.empty()
             || (!showTransitAspectPatterns
                 && !showTransitsToTransits
                 && (nats.empty() && !showTransitsToNatalPlanets)))
            ? 1
            : *hs.crbegin();

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
    unsigned int h { };
    auto keep = [&](unsigned i, bool derived=false) {
        if (derived) {
            if (auto p = dynamic_cast<PlanetLoc*>(_alist[i])) {
                if (p->planet.isMidpt()) {
                    return h < (derived? 4u : 2u);
                }
            }
        }
        return keepLooking(h, i);
    };

    HarmonicPlanetDateRangeMap inOrb;
    HarmonicPlanetDateRangesMap proximityLog;
    
    HarmonicPlanetClusters starts;
    auto useRate = 1 / double(maxH); // XXX
    if (showPatterns) {
        useRate *= patternsSpreadOrb/16.;
    }
    int ndays = int(useRate);
    int nsecs = (useRate - double(ndays)) * 24.*60.*60.;

    if (showPatterns || includeTransitRange) {
        HarmonicPlanetClusters work;
        PlanetProfile* useProf = &_alist;
        std::unique_ptr<PlanetProfile> doomed;
        auto stuff = _staff;
        if (!trans.empty()) {
            doomed = std::unique_ptr<PlanetProfile>(_alist.profile(trans, stuff));
            useProf = doomed.get();
        }
        HarmonicPlanetDateRangeMap tinOrb;
        for (unsigned h = 1; h <= maxH; ++h) {
            bool unsel = hs.count(h)==0;
            if (unsel /*&& !_filterLowerUnselectedHarmonics*/) continue;
            if (showPatterns) {
                auto found = findClusters(h, jd, *useProf, _ids,
                                       patternsQuorum,
                                       nats,
                                       patternsRestrictMoon,
                                       patternsSpreadOrb);
                if (!found.empty()) work[h].swap(found);
            }
            
            if (!includeTransitRange) continue;

            int i, j;
            qreal bd, bsp;
            for (auto it = stuff.begin(); it != stuff.end(); ) {
                if (_hsets[it->hsid].count(h) == 0) {
                    ++it;
                    continue;
                }
                std::tie(i,j) = *it;
                auto bi = dynamic_cast<PlanetLoc*>((*useProf)[i]);
                auto bj = dynamic_cast<PlanetLoc*>((*useProf)[j]);
                if (!bi || !bj) continue;
                bool good = bi->aspectable() || bj->aspectable();
                std::tie(bd, bsp) =
                        PlanetProfile::computeDelta(bi, bj, h);
                if (!st_quiet)
                qDebug() << QString("H%1 %2 at %3 with orb %4")
                            .arg(h)
                            .arg(PlanetSet({bi->planet,bj->planet}).names().join('='))
                            .arg(dtToString(d)).arg(bd)
                            .toStdString().c_str();
                if (good && std::abs(bd) <= planetPairOrb) {
                    HarmonicPlanetSet hij { h, { bi->planet, bj->planet }};
                    tinOrb[hij] = {jd, 0};
                    //if (!s_quiet)
                    qDebug() << QString("Found H%1 inital start of %2 at %3 with orb %4")
                                .arg(h)
                                .arg(hij.second.names().join("="))
                                .arg(dtToString(d)).arg(bd)
                                .toStdString().c_str();
                    stuff.erase(it++);
                    continue;
                }
                ++it;
            }
        }

        auto nd = d;
        while (!work.empty() || !tinOrb.empty()) {
            QCoreApplication::processEvents();

            if (_state == cancelRequestedState) break;
            if (_state == pauseRequestedState) {
                QThread::usleep(100000);
                continue;
            }

            nd = nd.addDays(-ndays).addSecs(-nsecs);
            PlanetSet ws;
            for (const auto& hpso: work) {
                for (const auto& pso: hpso.second) {
                    ws.insert(pso.first.begin(),pso.first.end());
                }
            }
            for (const auto& hijr: tinOrb) {
                ws.insert(hijr.first.second.begin(),
                          hijr.first.second.end());
            }

            auto wp = _alist.profile(ws);   // subset of planets
            auto pjd = getJulianDate(nd);
            (*wp)(pjd); // XXX compute once

            for (auto hit = tinOrb.begin(); hit != tinOrb.end(); ) {
                const auto& hps = hit->first;
                const auto& ps = hps.second;
                auto hwp = wp->profile(ps);
                auto orb = computeSpread(hps.first, *hwp);
                delete hwp;
                if (std::abs(orb) > planetPairOrb) {
                    inOrb[hps] = hit->second;
                    //if (!s_quiet)
                    qDebug() << QString("Found H%1 start of %2 at %3 with orb %4")
                                .arg(hps.first)
                                .arg(ps.names().join("="))
                                .arg(dtToString(nd))
                                .arg(orb)
                                .toStdString().c_str();
                    tinOrb.erase(hit++);
                } else {
                    if (!st_quiet)
                    qDebug() << QString("Still looking for H%1 start"
                                        " of %2 at %3 with orb %4")
                                .arg(hps.first)
                                .arg(ps.names().join("="))
                                .arg(dtToString(nd)).arg(orb)
                                .toStdString().c_str();
                    hit->second.first = pjd;    // update range start
                    ++hit;
                }
            }

            for (auto hit = work.begin(); hit != work.end(); ) {
                auto h = hit->first;
                auto& pso = hit->second;
                for (auto spit = pso.begin(); spit != pso.end(); ) {
                    const auto& ps = spit->first;
                    auto orbWas = spit->second;
                    auto hwp = wp->profile(ps);
                    auto orb = computeSpread(h,*hwp);
                    delete hwp;
                    if (orb > patternsSpreadOrb) {
                        starts[h].emplace(ps,ClusterOrbWhen(orb,pjd));
                        qDebug() << QString("Found H%1 prior start of %2 "
                                            "with %3 spread at %4")
                                    .arg(h).arg(ps.names().join("="))
                                    .arg(orbWas)
                                    .arg(dtToString(nd))
                                    .toStdString().c_str();
                        pso.erase(spit++);
                    } else {
                        if (orb < spit->second.orb) {
                            spit->second = { orb, pjd };
                        }
                        ++spit;
                    }
                }
                if (pso.empty()) work.erase(hit++);
                else ++hit;
            }
            delete wp;
        }
    }
    if (_state == cancelRequestedState) return;

    double pjd = jd;
    double ljd = jd;
    auto nd = d.addDays(ndays).addSecs(nsecs);
    while (d < e || !starts.empty() || !inOrb.empty()) {
        QCoreApplication::processEvents();

        if (_state == cancelRequestedState) break;
        if (_state == pauseRequestedState) {
            QThread::usleep(100000);
            continue;
        }

        jd = getJulianDate(nd);
        if (jd - ljd >= 5) {
            emit progress((ljd - bjd) / (ejd - bjd));
            ljd = jd;
            QCoreApplication::processEvents();
            if (_state == cancelRequestedState) break;
            if (_state == pauseRequestedState) continue;
        }

        std::unique_ptr<PlanetProfile> useProf;
        bool collectingStrays = (d >= e);
        if (collectingStrays) {
            PlanetSet ws;
            for (const auto& hpso: starts) {
                for (const auto& pso: hpso.second) {
                    ws.insert(pso.first.begin(),pso.first.end());
                }
            }
            for (const auto& hijr: inOrb) {
                ws.insert(hijr.first.second.begin(),
                          hijr.first.second.end());
            }
            if (ws.empty()) break;  // all done
            if (ws.size() != b.size()) {
                qDebug() << "Pruning profile to" << ws.names();
                auto wp = b.profile(ws);
                wp->swap(b);
                delete wp;
            }
        }

        for (auto tp: b) (*tp)(jd, 1);
        if (!st_quiet) qDebug() << "stuff" << dtToString(nd);

        if (!collectingStrays && !trans.empty()) {
            useProf = std::unique_ptr<PlanetProfile>(b.profile(trans));
        }

        // Do all the things HERE

        if (showPatterns) {
        // 1. Patterns
        for (h = maxH; h >= 1; --h) {
            bool unsel = hs.count(h)==0;
            if (unsel /*&& !_filterLowerUnselectedHarmonics*/) continue;
            if (collectingStrays) {
                bool any = false;
                auto hit = starts.find(h);
                if (hit != starts.end()) {
                    if (hit->second.empty()) starts.erase(hit);
                    else any = true;
                }
                if (!any) continue;
            }
            PlanetClusterMap hpc;
            if (!collectingStrays) {
                PlanetProfile* prof = &b;
                if (useProf.get()) prof = useProf.get();
                hpc = findClusters(h, *prof,
                                   patternsQuorum,
                                   nats, skipAllNatalOnly,
                                   patternsRestrictMoon,
                                   patternsSpreadOrb);
            }

            std::list<PlanetClusterMap::iterator> doomed;
            for (auto sit = starts[h].begin(); sit != starts[h].end(); ) {
                const auto& ps = sit->first;
                auto prof = b.profile(ps);
                auto hpcit = hpc.find(ps);
                if (hpcit != hpc.end()) {
                    delete prof;
                    hpc.erase(hpcit);   // already have a start time
                    ++sit;
                    continue;
                }
                auto spread = computeSpread(h,*prof);
                if (spread <= patternsSpreadOrb) {
                    if (collectingStrays) {
                        qDebug() << QString("H%1 %2 with %3 spread at %4")
                                    .arg(h).arg(ps.names().join("="))
                                    .arg(spread)
                                    .arg(dtToString(dateTimeFromJulian(jd)))
                                    .toStdString().c_str();
                    }
                    // still good so keep
                    delete prof;
                    ++sit;
                    continue;
                }

                double from = sit->second.when;
                double to = jd;

                bool cancel = (from == 0);
                if (cancel) {
                    qDebug() << QString("H%1").arg(h) << ps.names()
                             << "was previously hijacked, so skipping";
                } else {
                    qDebug() << QString("H%1").arg(h)
                             << ps.names()
                             << "from" << dtToString(dateTimeFromJulian(from))
                             << "to" << dtToString(nd);
                }

                unsigned useH = h;
                if (!cancel && h > 1) {
                    // look for a pending pattern at a lower harmonic
                    // because it's likely to be the same pattern but
                    // now we have a tighter bounds. when we find this,
                    // we set the start time to 0 to indicate that it
                    // is no longer active.
                    unsigned prev = 0;
                    for (unsigned f: getAllFactors(h)) {
                        if (f == h) break;
                        if (prev == f) continue;
                        auto stit = starts.find(f);
                        if (stit == starts.end()) continue;
                        auto& startf = stit->second;
                        auto lwrit = startf.find(ps);
                        if (lwrit != startf.end()) {
                            cancel = true;
                            qDebug() << QString("H%1").arg(f)
                                     << ps.names()
                                     << "exists, so skipping"
                                     << QString("H%1").arg(h);
                            break;
                        }
                        prev = f;
                    }
                }
                if (cancel) {
                    delete prof;
                    starts[h].erase(sit++);
                    continue;    // skipped!
                }

                static size_t clearing = 0; clearing++;
                qDebug() << clearing << QString("Launching H%1 search for %2 "
                                    "with %3 spread at %4")
                            .arg(h).arg(ps.names().join("="))
                            .arg(spread)
                            .arg(dtToString(dateTimeFromJulian(jd)))
                            .toStdString().c_str();
                doomed.emplace_back(sit++);
                //starts[h].erase(sit++);

                {
                QMutexLocker ml(&_evs.mutex);
                ADateTimeRange range(dateTimeFromJulian(from),
                                     dateTimeFromJulian(jd));
                EventType et = ps.heterogeneous()? etcTransitNatalAspectPattern
                                                 : etcTransitAspectPattern;
                _evs.emplace_back(range, et, useH, PlanetSet(ps));
                }
                auto& ev = _evs.back();
#if 1
                tp.start([=, &ev] {
                    startTask();
#endif
#if 0 // FIXME
                    if (prof->size()==2) {
                        qreal ad, asp, bd, bsp;

                        // check speed
                        prof->computePos(from,h);
                        std::tie(ad, asp) = PlanetProfile::computeDelta((*prof)[0], (*prof)[1], h);
                        prof->computePos(jd,h);
                        std::tie(bd, bsp) = PlanetProfile::computeDelta((*prof)[0], (*prof)[1], h);
                        if (sgn(ad) != sgn(bd)) {
                            auto cps = [prof, h](double jd)
                                    -> std::pair<qreal,qreal>
                            {
                                auto pos = prof->computePos(jd,h);
                                std::pair<qreal,qreal> ret { pos, prof->speed() };
                                if (false /*!s_quiet*/) {
                                    QDateTime dt(dateTimeFromJulian(jd));
                                    qDebug() << "nri " << dtToString(dt).toLatin1().constData()
                                             << "ret:" << ret;
                                }
                                return ret;
                            };
                            uintmax_t iter = 50;
                            static constexpr int digits =
                                    std::numeric_limits<double>::digits;
                            using namespace boost::math::tools;
                            auto tjd = newton_raphson_iterate(cps,(from+jd)/2,
                                                              from,jd,digits,iter);
                            auto psp = PlanetProfile::computeDelta((*prof)[0],
                                    (*prof)[1],h);
                            if (std::abs(psp.first) <= calcLoop::tol) {
                                qDebug() << "newton succeeded" << iter;
                                auto qdt = dateTimeFromJulian(tjd);
                                ev.reset(qdt, psp.first);
                                delete prof;
                            } else {
                                qDebug() << "newton failed" << iter
                                    << prof->description().toLatin1().constData();
                            }
                        }
                    }
#endif
                    auto csprd = [prof,h,this](double jd)
                    {
                        if (_state == cancelRequestedState) throw int(1);
                        auto val = computeSpread(h,jd,*prof,_ids);
                        //qDebug() << dtToString(dateTimeFromJulian(jd)) << ps.names() << val;
                        return val;
                    };
                    constexpr auto tol =
                            double(std::numeric_limits<float>::epsilon());

                    double jd;
                    double m = 1000;
                    double a = from;
                    double b = to;
                    double res;
                    unsigned it = 0;
                    try {
                    do {
                        double mid = a/2. + b/2.;
                        ++it;
                        res = brentGlobalMin(csprd, a, b, mid,
                                             m, .0000001, tol, jd);
                        if (jd == a || jd == b) {
                            qDebug() << "Dubious result" << res
                                     << ((jd == b)? "jd == to" : ((jd == a)? "jd == from" : ""))
                                     << "for" << QString("H%1").arg(h)
                                     << ps.names()
                                     << QList<qreal>({csprd(a),csprd(mid),csprd(b)});
                            if (it > 2) break;
                            m *= 10;
                            auto q = (mid - a)/4.;
                            if (jd == b) a = b - q, b += q;
                            else b = a + q, a -= q;
                        } else break;
                    } while (it < 3);

                    QMutexLocker ml(&_evs.mutex);
                    auto qdt = dateTimeFromJulian(jd);
                    ev.reset(qdt, res);
                    } catch (int) { }
                    delete prof;
#if 1
                    endTask();
                });
#endif
                // TODO keep track of this start and end and then search

            }

            // now hpc should only contain entries that are not in starts[h]
            // and starts[h] will contain entries that *were* initially in hpc,
            // so we need to add the remainder of hpc to starts[h] with the
            // new date.
            for (const auto& cl: hpc) {
                bool add = true;
                unsigned prev = 0;
                const auto& ps(cl.first);
                for (unsigned f: getAllFactors(h)) {
                    if (f == h) break;
                    if (prev == f) continue;
                    auto stit = starts.find(f);
                    if (stit == starts.end()) continue;
                    const auto& startf = stit->second;
                    auto lwrit = startf.find(ps);
                    if (lwrit != startf.end()) {
                        add = false;
                        break;
                    }
                }
                if (add) {
                    qDebug() << QString("Found H%1 start of %2 "
                                        "with %3 spread at %4")
                                .arg(h).arg(ps.names().join("="))
                                .arg(cl.second)
                                .arg(dtToString(dateTimeFromJulian(jd)))
                                .toStdString().c_str();
                    starts[h].emplace(ps, ClusterOrbWhen(cl.second.orb, pjd));
                }
            }
            for (const auto& it: doomed) starts[h].erase(it);
        }
        } // if includeAspectPatterns

        if (collectingStrays && !inOrb.empty()) {
            for (auto hit = inOrb.begin(); hit != inOrb.end(); ) {
                const auto& hps = hit->first;
                auto hwp = b.profile(hps.second);
                auto orb = computeSpread(hps.first/*harmonic*/, *hwp);
                delete hwp;
                if (orb > planetPairOrb) {
                    hit->second.second = pjd;
                    proximityLog[hps].emplace(hit->second,0);
                    inOrb.erase(hit++);
                } else {
                    ++hit;
                }
            }
        }
        if (!collectingStrays && !_staff.empty()) {
            qreal ad, asp;
            qreal bd, bsp;

            unsigned i, j;
            QString wha_;
            auto what = [&]{
                if (wha_.isEmpty()) {
                    wha_ = QString("H%1 %2=%3")
                        .arg(h)
                        .arg(_alist[i]->description())
                        .arg(_alist[j]->description());
                }
                return wha_.toStdString();
            };

            auto stuff = _staff;
            if (includeTransitRange) {
                for (h = 1; h <= maxH; ++h) {
                    for (auto it = stuff.begin(); it != stuff.end(); ) {
                        bool unsel = hs.count(h)==0;
                        if ((unsel && !filterLowerUnselectedHarmonics)
                                || (_hsets[it->hsid].count(h) == 0))
                        {
                            ++it;
                            continue;
                        }
                        std::tie(i,j) = *it;
                        std::tie(bd, bsp) =
                                PlanetProfile::computeDelta(b[i], b[j], h);

                        auto bi = dynamic_cast<PlanetLoc*>(b[i]);
                        auto bj = dynamic_cast<PlanetLoc*>(b[j]);
                        auto good = bi && bj
                                && (bi->aspectable() || bj->aspectable());

                        bool isInOrb = false;
                        if (good) {
                            HarmonicPlanetSet hij
                            { h, { bi->planet, bj->planet }};
                            HarmonicPlanetSet hji { h, { bj->planet, bi->planet }};

                            auto hasit = inOrb.find(hij);
                            isInOrb = std::abs(bd) <= planetPairOrb;
                            if (false) //if (isInOrb)
                            qDebug() << QString("Found H%1 orb %2 of %3 at %4")
                                        .arg(h).arg(std::abs(bd))
                                        .arg(hij.second.names().join("="))
                                        .arg(dtToString(d))
                                        .toStdString().c_str();
                            if (hasit == inOrb.end() && isInOrb) {
                                if (!st_quiet)
                                qDebug() << QString("Found H%1 start of %2 at %3")
                                            .arg(h)
                                            .arg(hij.second.names().join("="))
                                            .arg(dtToString(d))
                                            .toStdString().c_str();
                                inOrb[hij] = {pjd, 0};
                                isInOrb = true;
                            } else if (hasit != inOrb.end() && !isInOrb) {
                                hasit->second.second = jd;
                                if (it->et != etcTransitToStation) {
                                    proximityLog[hasit->first]
                                            .emplace(hasit->second,0);
                                }
                                if (!st_quiet)
                                qDebug() << QString("Found H%1 range of %2 at %3 to %4")
                                            .arg(h)
                                            .arg(hij.second.names().join("="))
                                            .arg(dtToString(dateTimeFromJulian(hasit->second.first)))
                                            .arg(dtToString(d))
                                            .toStdString().c_str();
                                inOrb.erase(hasit);
                            }
                        }
                        if (isInOrb) stuff.erase(it++);
                        else ++it;
                    }
                }

                stuff = _staff;
            }

            for (h = 1; h <= maxH; ++h) {
                bool unsel = hs.count(h)==0;
                if (unsel && !filterLowerUnselectedHarmonics) continue;

                for (auto it = stuff.begin(); it != stuff.end(); ) {
                    wha_.clear();
                    std::tie(i,j) = *it;
                    auto et = it->et;
                    auto hsid = it->hsid;
                    const auto& hs = _hsets[hsid];
                    auto ha = hs.lower_bound(h);
                    if (ha == hs.end()) {
                        //qDebug() << "Snipping" << what().c_str()
                        //         << "because no further harmonics";
                        stuff.erase(it++); continue;
                    } else if (*ha != h) {
                        //qDebug() << "Skipping" << what().c_str()
                        //         << "because not selecting h" << h;
                        ++it; continue;
                    }

                    if (auto known = dynamic_cast<KnownPosition*>(_alist[j])) {
                        if (std::abs(known->julianDate()-jd) > windowOf(known)) {
                            ++it; continue;
                        }
                    }
                    qreal ispd = qAbs(_alist[i]->speed/2. + b[i]->speed/2.);
                    qreal jspd = qAbs(_alist[j]->speed/2. + b[j]->speed/2.);
                    if (ispd > jspd) {
                        std::swap(i,j);
                        std::swap(ispd,jspd);
                    }

                    std::tie(ad, asp) = 
                            PlanetProfile::computeDelta(_alist[i],_alist[j],h);
                    std::tie(bd, bsp) = 
                            PlanetProfile::computeDelta(b[i], b[j], h);
                    if (sgn(ad)==sgn(bd) || (abs(ad)>=90. || abs(bd)>=90.)) {
                        if (!keep(i) || !keep(j)) {
                            stuff.erase(it++);
                        } else {
                            ++it;
                        }
                        continue;
                    }

#if 1
                    auto pi = dynamic_cast<PlanetLoc*>(_alist[i]);
                    if (pi && pi->allowAspects > PlanetLoc::aspOnlyConj) {
                        qreal spd = _alist[j]->speed/2. + b[j]->speed/2.;
                        if (pi->allowAspects !=
                                ((spd<0)? PlanetLoc::aspOnlyRetro
                                 : PlanetLoc::aspOnlyDirect))
                        {
                            qDebug() << "skipping wrong-way" << what().c_str();
                            stuff.erase(it++);
                            continue;
                        }
                    }
#endif

                    if (!st_quiet) {
                        QDateTime pdt(dateTimeFromJulian(pjd));
                        QDateTime dt(dateTimeFromJulian(jd));
                        qDebug() << what().c_str()
                                 << dtToString(pdt) << "delta" << ad << asp << "vs"
                             << dtToString(dt) << "delta" << bd << bsp;
                    }
                    auto which = what();

                    // At this point, we figure there's _alist transit.
                    // If the harmonic is not on our list, let's clip the
                    // stuff list so that it doesn't get recomputed at _alist
                    // higher-order harmonic. This handles the case where
                    // conjunction would show up on any higher harmonic
                    // as an aspect at that harmonic.
                    stuff.erase(it++);
                    if (unsel) { continue; }

                    bool useBZS = (_alist[i]->inMotion() && ispd < .00001)
                            || (_alist[j]->inMotion() && jspd < .00001);
                    {
                        QMutexLocker ml(&_evs.mutex);
                        _evs.emplace_back();
                    }
                    auto& ev = _evs.back();
#if 1
                    bool bQuiet = mum;
                    tp.start([=, &ev] {
                        startTask();
                        modalize<bool> mum(st_quiet, bQuiet);
#endif
                        PlanetProfile poses { _alist[i]->clone(), _alist[j]->clone() };

                        // for slow planetary motion we need to use BrentZhangStage.
                        // XXX replace magic number: might plausibly instead be
                        // _alist certain percentage of the default speed.
                        double tjd {};
                        uintmax_t iter;
                        bool bzhs = false;
                        bool done = false;
                        if (!useBZS) {
                            try {
                                auto cps = [&poses, &which, h, this](double jd)
                                -> std::pair<qreal,qreal>
                                {
                                    if (_state == cancelRequestedState) throw int(1);
                                    auto pos = poses.computePos(jd,h);
                                    std::pair<qreal,qreal> ret { pos, poses.speed() };
                                    if (!st_quiet) {
                                        QDateTime dt(dateTimeFromJulian(jd));
                                        qDebug() << "nri " << (which + ":").c_str()
                                                 << dtToString(dt)
                                                 << "ret:" << ret;
                                    }
                                    return ret;
                                };
                                iter = 50;  // TODO !? need to diagnose when we hit this
                                static constexpr int digits =
                                        std::numeric_limits<double>::digits;

                                using namespace boost::math::tools;
                                double guess;
                                if (_alist[i]->inMotion() && _alist[j]->inMotion()) {
                                    guess = pjd + (fabs(ad)/(fabs(ad)+fabs(bd)));
                                } else {
                                    guess = pjd + .5;
                                }
                                try {
                                tjd = newton_raphson_iterate(cps,
                                                             guess, pjd, jd,
                                                             digits, iter);
                                } catch (int) {
                                    endTask();
                                    return;
                                }

                                auto psp = PlanetProfile::computeDelta(poses[0],poses[1],h);
                                if (std::abs(psp.first) <= calcLoop::tol) {
                                    // check for actual hit per tolerance?
                                    done = true;
                                } else {
                                    done = false;
                                }
                            } catch (...) {
                                done = false;
                            }
                            if (!done && !st_quiet) {
                                qDebug() << "Failed"
                                << d.date().toString()
                                << which.c_str()
                                << "after" << iter
                                << "iteration(s) newton_raphson";
                                qDebug() << "speed" << ispd << jspd << "respectively";
                            }
                        }
                        if (!done) {
                            bzhs = true;
                            unsigned count = 0;
                            auto cp = [&poses, &count, &which,
                                    h, this](double jd)
                            {
                                if (_state == cancelRequestedState) throw int(1);
                                ++count;
                                auto pos = poses.computePos(jd, h);
                                if (!st_quiet) {
                                    QDateTime dt(dateTimeFromJulian(jd));
                                    qDebug() << "bzhs " << (which + ":").c_str()
                                             << dtToString(dt)
                                             << "ret:" << pos;
                                }
                                return pos;
                            };
                            try {
                            done = brentZhangStage(cp, pjd, jd, ad, bd, tjd);
                            } catch (int) {
                                endTask();
                                return;
                            }

                            iter = count;
                            auto psp = PlanetProfile::computeDelta(poses[0],poses[1],h);
                            if (std::abs(psp.first) <= calcLoop::tol) {
                                // check for actual hit per tolerance?
                                done = true;
                            } else {
                                done = false;
                            }
                        }
                        if (!done /*&& !s_quiet*/) {
                            qDebug() << "Failed"
                                << d.date().toString()
                                << which.c_str()
                                << "after" << iter
                                << "iteration(s) brentStageZhang";
                        } else {
                            // Pack up event
                            auto qdt = dateTimeFromJulian(tjd);
                            auto p1loc = dynamic_cast<PlanetLoc*>(poses[0]);
                            auto p2loc = dynamic_cast<PlanetLoc*>(poses[1]);
                            PlanetRangeBySpeed plr { *p1loc, *p2loc };

                            QMutexLocker ml(&_evs.mutex);
                            auto ch = static_cast<unsigned char>(h);
                            ev = HarmonicEvent(qdt, et, ch, std::move(plr));

                            //if (!s_quiet)
                                qDebug() << dtToString(qdt).toLocal8Bit().constData()
                                         << which.c_str()
                                         << "with" << iter
                                         << "iteration(s)"
                                     << (bzhs? "brentZhangStage" : "newton_raphson");
                        }

#if 1
                        endTask();
                    });
#endif
                }
            }
        } // if includeTransits

        d = nd;
        nd = d.addDays(ndays).addSecs(nsecs);
        //nd = d.addDays(_rate);
        pjd = jd;
        if (!collectingStrays) _alist.swap(b);
    }

    qDebug() << inOrb.size() << "pending pairs";
    qDebug() << proximityLog.size() << "completed range pairs";

    auto frameJob = [&](HarmonicEvent& e, bool prep = true)
    {
        if (e.planets().size()!=2) return;
        if (e.range().first != QDateTime()) return;

        HarmonicPlanetSet hps { e.harmonic(), e.planets() };
        auto lit = proximityLog.find(hps);
        if (lit == proximityLog.end()) return;

        if (prep) prepThread();
        auto& ranges = lit->second;
        auto jd = getJulianDate(e.dateTime());
        auto it = ranges.upper_bound({jd,jd});
        auto rit = std::make_reverse_iterator(it);
        while (rit != ranges.rend() && rit->first.first <= jd) {
            if (rit->first.second >= jd) {
                e.setRange({dateTimeFromJulian(rit->first.first),
                            dateTimeFromJulian(rit->first.second)});
                rit->second++;
                break;
            }
            ++rit;
        }
        if (prep) releaseThread();
    };

    {
    // get those planet pairs framed
    QMutexLocker ml(&_evs.mutex);   // lock for swoosh through paired events
    for (auto& ev: _evs) {
        if (ev.eventType() != etcTransitToStation) {
        frameJob(ev, false);
        }
    }
    }

    //if (!s_quiet)
    {
        bool any = false;
    for (const auto& pl: proximityLog) {
        QStringList sl;
        for (const auto& r: pl.second) {
            if (r.second) continue;
            sl << QString("[%1 - %2]")
                  .arg(dtToString(dateTimeFromJulian(r.first.first)),
                       dtToString(dateTimeFromJulian(r.first.second)));
        }
        if (sl.empty()) continue;
        if (!any) qDebug() << "Ranges with no precise hits:";
        any = true;
        qDebug() << QString("H%1 %2: %3").arg(pl.first.first)
                    .arg(pl.first.second.names().join("="))
                    .arg(sl.join(",\n  "))
                    .toStdString().c_str();
    }

    for (const auto& hpc : starts) {
        for (const auto& pso: hpc.second) {
            if (pso.second.when == qreal()) continue;
            qDebug() << QString("Pending pattern H%1 %2 started at %3")
                        .arg(hpc.first)
                        .arg(pso.first.names().join("="))
                        .arg(dtToString(dateTimeFromJulian(pso.second.when)))
                        .toStdString().c_str();

        }
    }
    }

    bool cleared = false;
    int active(_numTasks);
    qDebug() << active << "activity/ies";
    while (!tp.waitForDone(100)) {
        QCoreApplication::processEvents();
        if (!cleared && _state == cancelRequestedState) {
            tp.clear();
            cleared = true;
        }
        int now(_numTasks);
        if (now != active) {
            qDebug() << now << "activity/ies";
            active = now;
        }
    }
    utp.reset();    // release thread pool

    // now get remaining
    QMutexLocker ml(&_evs.mutex);   // lock for swoosh through paired events
    auto fut = QtConcurrent::map(_evs, frameJob);
    fut.waitForFinished();

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

void AspectFinder::findStuff()
{
    prepThread();

    _state = runningState;
    if (showStations) findStations();
    if (_state != cancelRequestedState) findAspectsAndPatterns();
    _state = idleState;

    qDebug() << "Exiting finder thread";

    releaseThread();

    thread()->exit();
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
    static std::vector<std::tuple<unsigned,unsigned char,const char*,const char*>> init
    {
        { etcUnknownEvent,          0, "?",    "unknown" },
        { etcStation,               1, "S",    "Stations" },
        { etcTransitToStation,      1, "T=S",  "Transits to Station" },
        { etcTransitToTransit,      1, "T=T",  "Transits to Transit" },
        { etcTransitToNatal,        2, "T=N",  "Transits to Natal" },
        { etcOuterTransitToNatal,   2, "OT=N", "Outer Transits to Natal" },
        { etcReturn,                2, "R",    "Returns" },
        //{ etcAspectToReturn,        "R=T",  "Aspects to Return" },
        //{ etcReturnTransitToTransit,"RT=T", "Transits in Return" },
        //{ etcReturnTransitToNatal,  "RT=N", "Transits to Natal in Return" },
        { etcProgressedToProgressed,2, "P=P",  "Progressed to Progressed" },
        { etcProgressedToNatal,     2, "P=N",  "Progressed to Natal" },
        { etcInnerProgressedToNatal,2, "IP=N", "Inner Progressed to Natal" },
        { etcTransitToProgressed,	2, "T=P",  "Transits to Progressed" },
        { etcSolarArcToNatal,       2, "D=N",  "SA Direct to Natal" },
        { etcSignIngress,           1, "T=I",  "Sign Ingresses" },
        { etcHouseIngress,          2, "T=H",  "House ingresses" },
        { etcLunation,              1, "L",    "Lunations" },
        { etcEclipse,               1, "E",    "Eclipses" },
        { etcSolarEclipse,          1, "SE",   "Solar Eclipses" },
        { etcLunarEclipse,          1, "LE",   "Lunar Eclipses" },
        { etcHeliacalEvents,        1, "HRS",   "Heliacal Risings/Settings" },
        { etcTransitAspectPattern,  1, "TA",    "Transit Aspect Patterns"},
        { etcTransitNatalAspectPattern,  2, "TNA", "Transit-Natal Aspect Patterns"},
        { etcParanatellonta,        2, "Par",   "Paranatellonta" }
    };

    unsigned id;
    unsigned char chnum;
    QString abbr, desc;
    for (const auto& tup : init) {
        std::tie(id, chnum, abbr, desc) = tup;
        _eventIdToString[id] = {chnum, abbr, desc};
        _eventStringToId[abbr] = id;
    }
    _numEvents = ++id;
}

EventTypeManager &EventTypeManager::singleton()
{
    static EventTypeManager* _theMgr = new EventTypeManager();
    return *_theMgr;
}

unsigned
EventTypeManager::registerEventType(unsigned char  chnum,
                                    const QString& abbr,
                                    const QString& desc)
{
    auto& my = singleton();
    my._eventIdToString[my._numEvents] = { chnum, abbr, desc };
    my._eventStringToId[abbr] = my._numEvents;
    return my._numEvents++;
}

unsigned
EventTypeManager::registerEventType(const eventTypeInfo &evtinf)
{
    return registerEventType(std::get<0>(evtinf),
                             std::get<1>(evtinf),
                             std::get<2>(evtinf));
}

namespace {
typedef std::pair<qreal, ChartPlanetId> position;
struct lessPosit {
    bool operator()(const position& a, const position& b) const
    { return (a.first != b.first)? a.first < b.first : a.second < b.second; }
};
typedef std::set<position,lessPosit> positions;

inline
bool
containsAny(const positions& pos, const PlanetSet& of)
{
    return std::any_of(pos.begin(), pos.end(),
                       [&](const position& p) {
        return of.contains(p.second);
    });
}

inline
bool
containsAnyTrans(const positions& pos)
{
    return std::any_of(pos.begin(), pos.end(),
                       [&](const position& p) {
        return p.second.fileId() == 1
                && p.second.planetId() != Planet_Moon;
    });
}

inline
PlanetSet
getSet(const positions& pos)
{
    PlanetSet ret;
    for (const auto& p: pos) {
        ret.emplace(p.second);
    }
    return ret;
}

inline
unsigned
sizeWithoutTransitingMoon(const positions& grp,
                          bool moonIn1)
{
    unsigned ret = 0;
    for (const auto& p: grp) {
        auto pid = p.second.planetId();
        if ((pid != Planet_Moon
             && pid != Planet_NorthNode
             && pid != Planet_SouthNode)
                || (p.second.fileId() != (moonIn1? 1 : 0)))
            ++ret;
    }
    return ret;
}

std::ostream &
operator<<(std::ostream& os, const position& pos)
{
    return os << pos.second.name().toStdString() << " " << pos.first;
}

std::ostream &
operator<<(std::ostream& os, const positions& posits)
{
    os << "(";
    std::string next;
    for (const auto& pos: posits) {
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

}

PlanetClusterMap
findClusters(const positions& posits,
             unsigned quorum,
             const PlanetSet& need /*={}*/,
             bool skipAllNatalOnly = false,
             bool restrictMoon = true,
             qreal maxOrb = 8.)
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
        for (const auto& pos: posits) {
            const auto& cpid = pos.second;
            if (cpid.fileId()==1 && cpid.planetId()==Planet_Moon) {
                moonIn1 = true;
                break;
            }
        }
    }
    PlanetClusterMap ret;
    for (auto it = posits.begin(); it != posits.end(); ++it) {
        positions grp;
        auto maybeAddGroup = [&] {
            if ((restrictMoon? sizeWithoutTransitingMoon(grp,moonIn1)
                 : grp.size()) < quorum)
            { return; }

            if ((!need.empty() && !containsAny(grp,need))
                    || (skipAllNatalOnly && !containsAnyTrans(grp)))
            { return; }

            auto spread = angle(grp.begin()->first, grp.rbegin()->first);
            ret[getSet(grp)] = spread;
        };

        auto e = posits.lower_bound(position(it->first + maxOrb,
                                             ChartPlanetId()));
        for (auto jit = it; jit != e; ++jit) {
            maybeAddGroup();
            grp.emplace(*jit);
        }
        maybeAddGroup();
    }
    return ret;
}

PlanetClusterMap
findClusters(unsigned h,
             const PlanetProfile& plist,
             unsigned quorum,
             const PlanetSet& need /*={}*/,
             bool skipAllNatalOnly /*=false*/,
             bool restrictMoon /*=true*/,
             qreal maxOrb /*=8.*/)
{
    positions posits;
    for (auto loc : plist) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planet;
        if (cpid.fileId() < 0) continue;

        auto pid = cpid.planetId();

        if (h>1 && ((pid >= Houses_Start
                    && pid < Houses_End)
                    || (pid == Planet_IC
                         || pid == Planet_Desc)))
        { continue; }
        if (h%2==0 && (pid == Planet_SouthNode)) continue;

        auto hloc = h==1? ploc->rasiLoc() : harmonic(h,ploc->rasiLoc());
        auto ins = posits.emplace(hloc,ploc->planet);
        if (ins.first->first > 345) {
            posits.emplace(ins.first->first-360.,ploc->planet);
        } else if (ins.first->first < 15) {
            posits.emplace(ins.first->first+360.,ploc->planet);
        }
    }

    return findClusters(posits, quorum, need,
                        skipAllNatalOnly, restrictMoon, maxOrb);
}

PlanetClusterMap
findClusters(unsigned h, double jd,
             const PlanetProfile& plist,
             const QList<InputData>& ids,
             unsigned quorum,
             const PlanetSet& need /*={}*/,
             bool restrictMoon /*=true*/,
             qreal maxOrb /*=8.*/)
{
    std::vector<unsigned> pfid { 0, 0, 0 };
    positions posits;
    for (auto loc : plist) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planet;
        if (cpid.fileId() < 0 || cpid.fileId() >= int(pfid.size())) continue;
        auto pid = cpid.planetId();
        if (h>1 && ((pid >= Houses_Start
                    && pid < Houses_End)
                    || (pid == Planet_IC
                         || pid == Planet_Desc)))
        { continue; }
        if (h%2==0 && (pid == Planet_SouthNode)) continue;

        ++pfid[cpid.fileId()];
        const auto& ida = ids.at( qMax(ids.size()-1,cpid.fileId()) );
        qreal pos = (ploc->inMotion())
                ? PlanetLoc::compute(cpid,ida,jd).first
                : ploc->_rasiLoc;
        auto hloc = h==1? pos : harmonic(h,pos);
        auto ins = posits.emplace(hloc,ploc->planet);
        if (ins.first->first > 345) {
            posits.emplace(ins.first->first-360.,ploc->planet);
        } else if (ins.first->first < 15) {
            posits.emplace(ins.first->first+360.,ploc->planet);
        }
    }

    return findClusters(posits, quorum, need,
                        pfid[0] && pfid[1], restrictMoon, maxOrb);
}

HarmonicPlanetClusters findClusters(const uintSSet& hs,
                                    const PlanetProfile& prof,
                                    unsigned quorum,
                                    const PlanetSet& need /*={}*/,
                                    bool skipAllNatalOnly /*=false*/,
                                    bool restrictMoon /*=true*/,
                                    qreal maxOrb /*=8.*/)
{
    HarmonicPlanetClusters ret;
    for (auto h: hs) {
        auto pc = findClusters(h, prof, quorum, need,
                               skipAllNatalOnly,
                               restrictMoon, maxOrb);
        if (pc.empty()) continue;

        uintSSet fac { 1 };
        getAllFactors(h,fac);
        for (unsigned oh: fac) {
            auto rit = ret.find(oh);
            if (rit == ret.end()) continue;

            const auto& retoh = rit->second;
            // clean up patterns already in lower harmonics
            for (auto pit = pc.begin(); pit != pc.end(); ) {
                auto retpit = retoh.find(pit->first);
                if (retpit == retoh.end()) ++pit;
                else pc.erase(pit++);
            }
        }
        if (!pc.empty()) ret.emplace(h, pc);
    }
    return ret;
}

qreal
computeSpread(unsigned h,
              double jd,
              const PlanetProfile& prof,
              const QList<InputData>& ids)
{
    std::vector<qreal> sums(2, 0);
    std::vector<qreal> locs;
    std::vector<unsigned> c(2, 0);
    for (auto loc: prof) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planet;
        if (cpid.fileId() < 0 && prof.size()>2) continue;

        qreal pos;
        unsigned vroom(ploc->inMotion());
        if (vroom && !ids.isEmpty()) {
            const auto& ida = ids.at( qMax(ids.size()-1,cpid.fileId()) );
            pos = PlanetLoc::compute(cpid,ida,jd).first;
        } else {
            pos = ploc->_rasiLoc;
        }
        if (h > 1) pos = harmonic(h,pos);
        /*if (vroom)*/ locs.emplace_back(pos);
        sums[vroom] += pos;
        ++c[vroom];
    }

    if (c[1] == 0 /*&& !ids.empty()*/) return 0;    // this is an error
#if 1
    if (c[0] != 0 /*&& !ids.empty()*/) {
#if 1
        // try to minimize distance to center of natal configuration
        locs.emplace_back(sums[0]/c[0]);
#else
        // try to minimize distance between center of moving
        // and center of natal configuration
        auto fixed = sums[0]/c[0];
        auto movie = sums[1]/c[1];
        return angle(fixed,movie);
#endif
    }
#endif
    qreal maxa = 0;
    for (unsigned i = 0, n = locs.size(); i+1 < n; ++i) {
        for (unsigned j = i+1; j < n; ++j) {
            auto a = angle(locs[i], locs[j]);
            if (a > maxa) maxa = a;
        }
    }
    return maxa;

}

} // namespace A
