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

/*static*/ const aspectModeType& aspectModeType::current()
{
    return aspectMode;
}

double getJulianDate(QDateTime GMT, bool ephemerisTime/*=false*/)
{
    char serr[256];
    double ret[2];
    swe_utc_to_jd(GMT.date().year(),
                  GMT.date().month(),
                  GMT.date().day(),
                  GMT.time().hour(),
                  GMT.time().minute(),
                  GMT.time().second() + GMT.time().msec() / 1000,
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

    for (int i = 1; i <= 12; i++)
        if (sign == getSign(houses.cusp[i - 1], zodiac).id ||
            (sign + 1) % 13 == getSign(houses.cusp[i % 12], zodiac).id) return i;

    return 0;
}

float
angle(const Star& body1, const Star& body2)
{
    switch (aspectMode) {
    case amcGreatCircle:
    {
        float a = angle(body1.eclipticPos.x(), body2.eclipticPos.x());
        float b = angle(body1.eclipticPos.y(), body2.eclipticPos.y());
        return sqrt(pow(a, 2) + pow(b, 2));
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
    if (planet1.isPlanet() && planet2.isPlanet()
        && planet1.getSWENum() == planet2.getSWENum())
        return Aspect_None;

    return aspect(angle(planet1, planet2), aspectSet);
}

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
        if (aspect.enabled
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
        if (p.house == house && p.houseRuler == houseAuthority)
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
    geopos[0] = input.location.x();
    geopos[1] = input.location.y();
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
    double  jd = getJulianDate(input.GMT);

    char    errStr[256] = "";

    // turn off true pos
    double eps, ablong;
    unsigned int flags;
    Planet ret = calculatePlanet(planet, input, jd, eps, flags, ablong,
                                 houses.RAMC, zodiac.id);

    double geopos[3];
    geopos[0] = input.location.x();
    geopos[1] = input.location.y();
    geopos[2] = 199 /*meters*/; //input.location.z();

    ret.sign = &getSign(ret.eclipticPos.x(), zodiac);
    ret.house = getHouse(houses, ret.eclipticPos.x());
    ret.position = getPosition(ret, ret.sign->id);
    if (ret.homeSigns.count()) {
        ret.houseRuler = getHouse(ret.homeSigns.first(), houses, zodiac);
    }

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
    double AD = asind(tand(DD) * tand(input.location.y()));
    double OA = input.location.y() >= 0 ? (RA - AD) : (RA + AD);
    double OD =input.location.y() >= 0 ? (RA + AD) : (RA - AD);
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
            st = swe_degnorm(swe_sidtime(rettm) * 15 + input.location.x());
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
                   double jd)
{
    constexpr uint invertPositionFlag = 256 * 1024;
    char errStr[256] = "";

    const Planet& p1(getPlanet(planet.planetId()));
    uint flags = (SEFLG_SWIEPH | p1.sweFlags) & ~SEFLG_TRUEPOS;
    if (ida.zodiac > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(ida.zodiac - 2, 0, 0);
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
                      ida.location.y(), ida.location.x(),
                      'C', cusps, ascmc, cuspspd, ascmcspd,
                       errStr);
        return { ascmc[i], ascmcspd[i] };
    };

    auto getPos = [&](const Planet& p, qreal& speed) -> qreal {
        int ret = ERR;
        qreal pos = 0.0;
        switch (aspectMode) {
        case amcEcliptic:
            if (p.id == Planet_Asc) {
                std::tie(pos,speed) = getAscMC(0);
                speed *= ida.harmonic;
                ret = OK;
                // FIXME speed?
            } else if (p.id == Planet_MC) {
                std::tie(pos,speed) = getAscMC(1);
                speed *= ida.harmonic;
                ret = OK;
                // FIXME speed?
            } else {
                ret = swe_calc_ut(jd, p.sweNum, flags, xx, errStr);
                pos = xx[0];
                if (p.id==Planet_SouthNode) pos = swe_degnorm(pos+180.);
                speed = xx[3] * ida.harmonic;
            }
            break;

        case amcEquatorial:
            if (p.id == Planet_Asc) {
                double xx[6];
                std::tie(xx[0],speed) = getAscMC(0, true/*trop*/);
                xx[1] = 0, xx[2] = 1.0;
                swe_cotrans(xx, xx, -eps);
                pos = xx[0];
                speed *= ida.harmonic;
                ret = OK;
                // FIXME speed?
            } else if (p.id == Planet_MC) {
                std::tie(pos,speed) = getAscMC(2, true/*trop*/);
                speed *= ida.harmonic;
                ret = OK;
            } else {
                ret = swe_calc_ut(jd, p.sweNum,
                                  (flags & ~SEFLG_SIDEREAL)
                                  | SEFLG_EQUATORIAL | SEFLG_SPEED,
                                  xx, errStr);
                pos = xx[0];
                if (p.id==Planet_SouthNode) pos = swe_degnorm(pos+180.);
                speed = xx[3] * ida.harmonic;
            }
            break;

        default:
        case amcPrimeVertical:
        {
            if (p.id == Planet_Asc) {
                pos = 0; ret = OK; // speed?
            } else if (p.id == Planet_MC) {
                pos = 270; ret = OK; // speed?
            } else {
                ret = swe_calc_ut(jd, p.sweNum,
                                  flags & ~SEFLG_SIDEREAL,
                                  xx, errStr);
                auto housePos = swe_house_pos(getAscMC(2,true/*trop*/).first,
                                              ida.location.y(), eps,
                                              'C', xx, errStr);
                pos = (housePos - 1)/12*360;
            }
            break;
        }
        }
        if (ret != ERR) return harmonic(ida.harmonic, pos);
        qDebug() << "Can't calculate position of " << p1.name
                 << "at jd" << jd << ":" << errStr;
        return 0;
    };

    speed = 0;
    auto pos = getPos(p1,speed);
    if (planet.isMidpt()) {
        qreal speed2 = 0;
        auto pos2 = getPos(getPlanet(planet.planetId2()),speed2);
        pos = (pos + pos2)/2;
        speed += speed2;
        if (planet.isOppMidpt()) pos += 180;
    }
    loc = pos;
    return pos;
}

qreal
PlanetLoc::compute(const InputData& ida)
{
    return compute(ida, getJulianDate(ida.GMT));
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

qreal
PlanetProfile::computePos(double jd)
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
    std::vector<qreal> apos(size(),0);
    std::vector<qreal> aspd(size(),0);
    unsigned i = 0;
    Loc::speed = 0;
    for (auto& pl : *this) {
        Loc::speed += pl->speed;
#if 1
        apos[i] = pl->operator()(jd);
#else
        auto npos = pl->operator()(jd);
        unsigned fid = unsigned(pl->inMotion());
        unsigned char& i( sizes[fid] );
        aspd[fid] = (pl->speed + i*aspd[fid])/(i+1);
        if (i) {
            if (apos[fid] - npos > 180) apos[fid] -= 360;
            else if (npos - apos[fid] > 180) apos[fid] += 360;
            apos[fid] = (npos + i*apos[fid])/(i+1);
        } else apos[fid] = npos;
#endif
        ++i;
    }

    if (size()==1) {
        Loc::loc = front()->loc;
        Loc::speed = front()->speed;
        return Loc::loc;
    }
    qreal &a = apos[0], &b = apos[1];
#if 0
    qreal &spa = aspd[0], &spb = aspd[1];
    qreal p;
    if (spa > spb) p = b - a;
    else p = a - b;
    if (p > 180) p -= 360; else if (p < -180) p += 360;
#else
    if (b - a > 180) b -= 360;
    else if (a - b > 180) b += 360;
#endif
#if 0
    return p;
#else
    return Loc::loc = b - a;
#endif
}

qreal
PlanetProfile::computeSpread(double jd)
{
    computePos(jd);
    std::sort(begin(), end(),
              [](const Loc* a, const Loc* b)
    {
        return a->loc < b->loc;
    });
    qreal maxa = 0;
    for (unsigned i = 0, n = size(); i < n; ++i) {
        auto a = angle(operator[](i)->loc,
                operator[]((i+1) % n)->loc);
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
    double  jd = getJulianDate(input.GMT);
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
        geopos[0] = input.location.x();
        geopos[1] = input.location.y();
        geopos[2] = input.location.z();
        swe_azalt(jd, SE_ECL2HOR, geopos, 0, 0, xx, hor);
        ret.horizontalPos.setX(hor[0]);
        ret.horizontalPos.setY(hor[1]);

        double rettm;
        int eflg = SEFLG_SWIEPH;

        for (Star::angleTransitMode m = Star::atAsc;
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

Houses calculateHouses ( const InputData& input )
{
    Houses ret;
    ret.system = &getHouseSystem(input.houseSystem);
    unsigned int flags = SEFLG_SWIEPH;
    if (input.zodiac > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(input.zodiac - 2, 0, 0);
    }

    double julianDay = getJulianDate(input.GMT, false/*i.e., UT*/);
    double  jd = getJulianDate(input.GMT, true/*i.e., ET*/);
    char    errStr[256] = "";
    double  xx[6];

    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];

    double hcusps[14], ascmc[11];

    // get the tropical ascendant...
    swe_houses_ex(julianDay, flags & ~SEFLG_SIDEREAL, 
                  input.location.y(), input.location.x(),
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
        swe_houses_ex(julianDay, flags, input.location.y(), input.location.x(),
                      ret.system->sweCode, hcusps, ascmc);
    }

    for (int i = 0; i < 12; i++)
        ret.cusp[i] = hcusps[i+1];

    ret.Asc   = ascmc[0];
    ret.MC    = ascmc[1];
    ret.RAMC  = ascmc[2];
    ret.Vx    = ascmc[3];
    ret.EA    = ascmc[4];

    double st = swe_degnorm((swe_sidtime(julianDay)) * 15 + input.location.x());
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
    double AD = asind(tand(DD) * tand(input.location.y()));
    ret.OAAC = input.location.y() >= 0 ? (ret.RAAC - AD) : (ret.RAAC + AD);
    DD = asind(sind(eps) * sind(swe_degnorm(asc + 180)));
    AD = asind(tand(DD) * tand(input.location.y()));
    ret.ODDC = input.location.y() >= 0 ? (ret.RADC + AD) : (ret.RADC - AD);
#endif

    ret.halfMedium = swe_difdegn(ret.RAAC, ret.RAMC);
    ret.halfImum = 180 - ret.halfMedium;
    //ret.swneDelta = swe_degnorm(ret.RAMC - swe_degnorm(ret.RAAC - 180)) / 360.0;

    // compute house position of sun so we can see
    // whether it's closer to sunset or sunrise.

    // first get tropical position (well, latitude)
    swe_calc_ut(julianDay,SE_SUN,flags & ~SEFLG_SIDEREAL, xx, errStr);

    double geopos[3] = {
        input.location.x(),
        input.location.y(),
        input.location.z()
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
            ret.deficient -= 2; break;
    case Planet_Mercury:
    case Planet_Venus:
    case Planet_Moon:
        if (!isEarlier(planet, scope.sun))
            ret.dignity += 2;
        else
            ret.deficient -= 2; break;
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


    switch (aspect(planet, scope.jupiter, topAspectSet())) {
    case Aspect_Conjunction: ret.dignity += 5; break;
    case Aspect_Trine:       ret.dignity += 4; break;
    case Aspect_Sextile:     ret.dignity += 3; break;
    default: break;
    }

    switch (aspect(planet, scope.venus, topAspectSet())) {
    case Aspect_Conjunction: ret.dignity += 5; break;
    case Aspect_Trine:       ret.dignity += 4; break;
    case Aspect_Sextile:     ret.dignity += 3; break;
    default: break;
    }

    switch (aspect(planet, scope.northNode, topAspectSet())) {
    case Aspect_Conjunction:
    /*case Aspect_Trine:
    case Aspect_Sextile:     ret.dignity   += 4; break;
    case Aspect_Opposition:  ret.deficient -= 4; break;*/
    default: break;
    }

    switch (aspect(planet, scope.mars, topAspectSet())) {
    case Aspect_Conjunction: ret.deficient -= 5; break;
    case Aspect_Opposition:  ret.deficient -= 4; break;
    case Aspect_Quadrature:  ret.deficient -= 3; break;
    default: break;
    }

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

AspectList
calculateAspects( const AspectsSet& aspectSet,
                  const PlanetMap &planets )
{
    AspectList ret;

    PlanetMap::const_iterator i = planets.constBegin();
    while (i != planets.constEnd()) {
        if ((i->id >= Planet_Sun && i->id <= Planet_Pluto)
            || i->id == Planet_Chiron)
        {
            PlanetMap::const_iterator j = i + 1;
            while (j != planets.constEnd()) {
                if (((j->id >= Planet_Sun && j->id <= Planet_Pluto)
                     || j->id == Planet_Chiron)
                    && aspect(i.value(), j.value(), aspectSet) != Aspect_None) 
                {
                    ret << calculateAspect(aspectSet, i.value(), j.value());
                }
                ++j;
            }
        }
        ++i;
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
        if (i->id >= Planet_Sun && i->id <= Planet_Pluto || i->id == Planet_Chiron) {
            for (auto j = planets2.constBegin(); j != planets2.constEnd(); ++j) {
                if ((j->id >= Planet_Sun && j->id <= Planet_Pluto || j->id == Planet_Chiron)
                    && aspect(i.value(), j.value(), aspectSet) != Aspect_None) {
                    ret << calculateAspect(aspectSet, i.value(), j.value());
                }
            }
        }
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
                  PlanetMap&    planets)
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

    for (PlanetId id : getPlanets()) {
        calculateHarmonic(h, planets[id]);
    }
}

uintSet
getPrimeFactors(unsigned n)
{
    // Adapted from 
    // http://www.geeksforgeeks.org/print-all-prime-factors-of-a-given-number/

    uintSet ret;
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

uintSet
getAllFactors(unsigned h)
{
    uintSet fs;
    fs.insert(1);
    for (unsigned i = 2, m = sqrt(h); i <= m; ++i) {
        if (h % i == 0) {
            fs.insert(i);
            fs.insert(h/i);
        }
    }
    return fs;
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
    qCopy(in.cbegin(), in.cend(), std::back_inserter(qs.front()));
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
typedef PlanetHarmonics::value_type harmonicResult;

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

    void operator()(uintSet& completed, const harmonicResult& res)
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

        uintSet factors = getAllFactors(res.first);
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

uintSet getPrimes(unsigned top)
{
    uintSet ret;
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

            for (auto cpot = cpit + 1;
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
    QFuture<uintSet> f = /*uintSet foo =*/
        mappedReduced<uintSet>(seq, orbLoop, j, OrderedReduce);
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
    if (input.harmonic != 1.0 && aspectMode != amcGreatCircle) {
        calculateHarmonic(input.harmonic, scope.houses, scope.planets);
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

    scope.aspects =
        calculateAspects(getAspectSet(input.aspectSet), scope.planets);
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
// Yes, it uses gotos!

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
    return QDateTime(QDate(y,m,d), QTime(hr,min,sec,msec));
}

namespace { bool quiet = false; }

struct calcPos {
    PlanetProfile& poses;
    calcPos(PlanetProfile& p) : poses(p) { }

    qreal operator()(double jd)
    {
        auto ret = poses.computePos(jd);
        if (!quiet) {
            QDateTime dt(dateTimeFromJulian(jd));
            qDebug() << "calc iter:" << dtToString(dt) << "Ret:" << ret;
        }
        return ret;
    }
};

struct calcSpd {
    PlanetProfile& poses;
    calcSpd(PlanetProfile& p) : poses(p) { }

    qreal operator()(double jd)
    {
        poses.computePos(jd);
        if (!quiet) {
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

        QDateTime dt(dateTimeFromJulian(jd));
        qDebug() << "ncalc iter:" << dtToString(dt) << "Ret:" << ret;

        return ret;
    }
};

struct calcSpread {
    PlanetProfile& poses;
    int m = 1;

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
    calcSpread csprd;
    calcPosSpd ncpos;

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

    bool speedSignsEqual(qreal a, qreal b) const { return true; }

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
                    double& orb,
                    double& horb,
                    double& span)
{
    if (aspectMode == amcPrimeVertical) horb = .5;
    float speed = poses.defaultSpeed();
    orb = 360 / speed / locale.harmonic;
    horb = orb / 1.8;

    //auto plid = poses.back()->planet.planetId();
    if (true/*plid == Planet_Sun || plid==Planet_Moon*/) span = orb/4;
    else span = 1/speed;

    qDebug() << poses[0]->description() << "half-orbit" << horb << "days";
}

QDateTime
calculateClosestTime(PlanetProfile& poses,
                    const InputData& locale)
{

    double jdIn = getJulianDate(locale.GMT);


    double jd = jdIn;
    calcLoop looper(poses, jd);

    double orb, horb, span;
    calculateOrbAndSpan(poses, locale, orb, horb, span);

    double begin = jdIn-orb/2;
    double end = begin + horb*2;

    calcPos cpos(poses);
    double flo = cpos(begin);
    double splo = poses.speed();

    //if (looper(begin,end,span,flo,true))
    if (looper(begin, end, span, flo, splo, true/*cont*/))
        return dateTimeFromJulian(jd);

    return locale.GMT;
}

QList<QDateTime>
quotidianSearch(PlanetProfile& poses,
                const InputData& locale,
                const QDateTime& endDT,
                double span /*= 1.0*/)
{
    double jd1 = getJulianDate(locale.GMT);
    double jd2 = getJulianDate(endDT);

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
                    const InputData& locale)
{
    PlanetProfile poses;
    poses.push_back(new NatalPosition(id, native));
    poses.push_back(new TransitPosition(id, locale));

    return calculateClosestTime(poses, locale);
}


Horoscope
calculateAll(const InputData& input)
{
    Horoscope scope;
    scope.inputData = input;
    scope.houses = calculateHouses(input);
    scope.zodiac = getZodiac(input.zodiac);

    for (PlanetId id : getPlanets()) {
        if (id == Planet_Asc) {
            Planet asc = Data::getPlanet(id);
            asc.eclipticPos.setX(scope.houses.Asc);
            asc.equatorialPos.setX(scope.houses.RAAC);
            asc.pvPos = 0;
            scope.planets[id] = asc;
        } else if (id == Planet_MC) {
            Planet mc = Data::getPlanet(id);
            mc.eclipticPos.setX(scope.houses.MC);
            mc.equatorialPos.setX(scope.houses.RAMC);
            mc.pvPos = 270;
            scope.planets[id] = mc;
        } else {
            scope.planets[id] =
                calculatePlanet(id, input, scope.houses, scope.zodiac);
        }
    }

    for (const QString& name : getStars()) {
        scope.stars[name.toStdString()] =
            calculateStar(name, input, scope.houses, scope.zodiac);
    }

    scope.housesOrig = scope.houses;
    scope.planetsOrig = scope.planets;

    calculateBaseChartHarmonic(scope);

    return scope;
}

} // namespace A
