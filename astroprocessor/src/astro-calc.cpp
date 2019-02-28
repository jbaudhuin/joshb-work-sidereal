#include <QtConcurrent/QtConcurrent>
#include <QDebug>

#undef MSDOS     // undef macroses that made by SWE library
#undef UCHAR
#undef forward

#include <math.h>
#include <tuple>

#include "astro-calc.h"
#include <swephexp.h>
#include <swehouse.h>

namespace A {

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

float roundDegree(float deg)
{
    deg = deg - ((int)(deg / 360)) * 360;
    if (deg < 0) deg += 360;
    return deg;
}

const ZodiacSign& getSign(float deg, const Zodiac& zodiac)
{
    for (const ZodiacSign& s : zodiac.signs)
        if (s.startAngle <= deg && s.endAngle > deg)
            return s;
    return zodiac.signs[zodiac.signs.count() - 1];
}

int getHouse(const Houses& houses, float deg)
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

int getHouse(ZodiacSignId sign, const Houses &houses, const Zodiac& zodiac)
{
    if (sign == Sign_None) return 0;

    for (int i = 1; i <= 12; i++)
        if (sign == getSign(houses.cusp[i - 1], zodiac).id ||
            (sign + 1) % 13 == getSign(houses.cusp[i % 12], zodiac).id) return i;

    return 0;
}

float angle(const Star& body1, const Star& body2)
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
        break;
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
        break;
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
        break;
    }
    return 0;
}

float   angle(float deg1, float deg2)
{
    float ret = qAbs(deg1 - deg2);
    if (ret > 180) ret = 360 - ret;
    return ret;
}

AspectId aspect(const Star& planet1, const Star& planet2, const AspectsSet& aspectSet)
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
    for (const AspectType& aspect : aspectSet.aspects)
    {
     //if (aspect.id == Aspect_None) qDebug() << "aaa!";
        if (aspect.angle - aspect.orb <= angle &&
            aspect.angle + aspect.orb >= angle)
            return aspect.id;
    }

    return Aspect_None;
}

bool towardsMovement(const Planet& planet1, const Planet& planet2)
{
    const Planet *p1 = &planet1, *p2 = &planet2;

    if (!isEarlier(planet1, planet2))       // make first planet earlier than second
    {
        p1 = &planet2;
        p2 = &planet1;
    }

    return (p1->eclipticSpeed.x() > p2->eclipticSpeed.x());
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

bool    isEarlier(const Planet& planet, const Planet& sun)
{
 //float opposite = roundDegree(sun.eclipticPos.x() - 180);
    return (roundDegree(planet.eclipticPos.x() - sun.eclipticPos.x()) > 180);
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
                const Houses& houses,
                const Zodiac& zodiac)
{
    Planet ret = getPlanet(planet);

    uint    invertPositionFlag = 256 * 1024;
    double  jd = getJulianDate(input.GMT);
    char    errStr[256] = "";
    double  xx[6];

    // turn off true pos
    unsigned int flags = (SEFLG_SWIEPH | ret.sweFlags) & ~SEFLG_TRUEPOS;

    if (zodiac.id > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(zodiac.id - 2, 0, 0);
    }

    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];

    // TODO: wrong moon speed calculation
    // (flags: SEFLG_TRUEPOS|SEFLG_SPEED = 272)
    //         272|invertPositionFlag = 262416
    if (swe_calc_ut(jd, ret.sweNum, flags, xx, errStr) != ERR) {
        if (!(ret.sweFlags & invertPositionFlag))
            ret.eclipticPos.setX(xx[0]);
        else                               // found 'inverted position' flag
            ret.eclipticPos.setX(roundDegree(xx[0] - 180));

        ret.eclipticPos.setY(xx[1]);
        ret.distance = xx[2];
        ret.eclipticSpeed.setX(xx[3]);
        ret.eclipticSpeed.setY(xx[4]);

        double geopos[3];
        geopos[0] = input.location.x();
        geopos[1] = input.location.y();
        geopos[2] = 199 /*meters*/; //input.location.z();

        double ablong = xx[0];

        // A hack to calculate prime vertical longitude from the campanus house position
        if (zodiac.id <= 1
            || swe_calc_ut(jd, ret.sweNum, flags & ~SEFLG_SIDEREAL,
                           xx, errStr) != ERR) 
        {
            ablong = xx[0];
            // We may have had to get tropical position to get the
            // house position -- this API wants tropical longitude.
            // From there we fudge a prime vertical coordinate.
            double housePos =
                swe_house_pos(houses.RAMC, geopos[1], eps,
                              'C', xx, errStr);
            ret.pvPos = (housePos - 1) / 12 * 360;
        }

        // calculate horizontal coordinates
        double hor[3];
        swe_azalt(jd, SE_ECL2HOR, geopos, 0, 0, xx, hor);
        ret.horizontalPos.setX(hor[0]);
        ret.horizontalPos.setY(hor[1]);

        if (swe_calc_ut(jd, ret.sweNum, flags | SEFLG_EQUATORIAL,
                        xx, errStr) != ERR) {
            ret.equatorialPos.setX(xx[0]);
            ret.equatorialPos.setY(xx[1]);
        }

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
        double OA = 
            input.location.y() >= 0 ? (RA - AD) : (RA + AD);
        double OD =
            input.location.y() >= 0 ? (RA + AD) : (RA - AD);
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
                               NULL/*starname*/,
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
    } else {
        qDebug("A: can't calculate position of '%s' at julian day %f: %s", qPrintable(ret.name), jd, errStr);
    }

    return ret;
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

    // TODO: wrong moon speed calculation
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

    double julianDay = getJulianDate(input.GMT, false/*need UT*/);
    double  jd = getJulianDate(input.GMT);
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
    a.orb     = qAbs(a.d->angle - a.angle);
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
        if (i->id >= Planet_Sun && i->id <= Planet_Pluto
                || i->id == Planet_Chiron)
        {
            PlanetMap::const_iterator j = i + 1;
            while (j != planets.constEnd()) {
                if ((j->id >= Planet_Sun && j->id <= Planet_Pluto || j->id == Planet_Chiron)
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

inline
qreal
harmonic(double h, qreal value)
{
    return fmod(value*h,360.);
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
    houses.MC = harmonic(h, houses.MC);

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
        std::set<unsigned> primes, nonPrimes;
        primeSieve.assign(maxH+1,true);
        std::vector<bool> maxFactor(maxH+1,false);
        primes = std::set<unsigned>({1});
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
    joiner j(hx, primeSieve);
    QFuture<uintSet> f = /*uintSet foo =*/
            QtConcurrent::mappedReduced<uintSet>(seq, orbLoop, j,
                                                 QtConcurrent::OrderedReduce);
    f.waitForFinished();
}

void
findHarmonics(const ChartPlanetMap& cpm,
              PlanetHarmonics& hx)
{
    bool doMidpoints = includeMidpoints();
    int d = harmonicsMinQuorum() <= harmonicsMaxQuorum() ? 1 : -1;
    unsigned num = qAbs(harmonicsMaxQuorum() - harmonicsMinQuorum()) + 1;
    qreal orb = harmonicsMinQOrb();
    qreal lorbMin = log2(orb);
    qreal lorbMax = log2(harmonicsMaxQOrb());
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

#if 0
Horoscope
calculateSolarReturn(const InputData& scope,
                     const InputData& locale,
                     int useYear)
{
    Horoscope foo;
    return foo;
}
#endif


Horoscope
calculateAll(const InputData& input)
{
    Horoscope scope;
    scope.inputData = input;
    scope.houses = calculateHouses(input);
    scope.zodiac = getZodiac(input.zodiac);

#if 0
    double  jd = getJulianDate(input.GMT);
    double  xx[6];
    char    errStr[256] = "";

    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];
#endif

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
