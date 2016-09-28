#include <swephexp.h>
#undef MSDOS     // undef macroses that made by SWE library
#undef UCHAR
#undef forward

#include <math.h>
#include "astro-calc.h"
#include <QDebug>

namespace A {

aspectModeType aspectMode(amcEcliptic);

double getJulianDate ( QDateTime GMT, bool ephemerisTime/*=false*/ )
 {
#if 1
  char serr[256];
  double ret[2];
  swe_utc_to_jd(GMT.date().year(),
                GMT.date().month(),
                GMT.date().day(),
                GMT.time().hour(),
                GMT.time().minute(),
                GMT.time().second()+GMT.time().msec()/1000,
                1/*gregorian*/, ret, serr);
  return ret[ephemerisTime? 0 : 1]; // ET or UT
#else
  int m     = GMT.date().month();
  int y     = GMT.date().year();
  int d     = GMT.date().day();
  double ut = GMT.time().hour()
            + GMT.time().minute() * 1.0 / 60
            + GMT.time().second() * 1.0 / 3600;

  if (m <= 2) { m = m + 12; y = y - 1; }
  return (int)(365.25*(y+4716)) + (int)(30.6001*(m+1)) + d - 13 - 1524.5 + (double)ut/24.0;
#endif
 }

float roundDegree  ( float deg )
 {
  deg = deg - ((int)(deg/360))*360;
  if ( deg < 0 ) deg += 360;
  return deg;
 }

const ZodiacSign& getSign  ( float deg, const Zodiac& zodiac )
 {
  foreach (const ZodiacSign& s, zodiac.signs)
    if (s.startAngle <= deg && s.endAngle > deg)
      return s;
  return zodiac.signs[zodiac.signs.count() - 1];
 }

int getHouse ( const Houses& houses, float deg )
 {
  for (int i = 0; i <= 10; i++)
    if ( (deg >= houses.cusp[i] && deg < houses.cusp[i+1]) ||
         (houses.cusp[i] > 180 && houses.cusp[i+1] < 180 && (deg < houses.cusp[i+1] || deg >= houses.cusp[i])) )
           return i + 1;

  return 12;
 }

int getHouse ( ZodiacSignId sign, const Houses &houses, const Zodiac& zodiac )
 {
  if (sign == Sign_None) return 0;

  for (int i = 1; i <= 12; i++)
    if ( sign         == getSign(houses.cusp[i-1], zodiac).id ||
        (sign+1) % 13 == getSign(houses.cusp[i % 12], zodiac).id ) return i;

  return 0;
 }

float  angle( const Star& body1, const Star& body2 )
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
        break;
    }
    return 0;
}

float   angle ( const Star& body, float deg )
{
    switch (aspectMode) {
    case amcGreatCircle:
    case amcEcliptic:
        return angle(body.eclipticPos.x(), deg);
    case amcEquatorial:
        return angle(body.equatorialPos.x(), deg);
    case amcPrimeVertical:
        break;
    }
    return 0;
 }

float   angle ( const Star& body, QPointF coordinate )
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
        break;
    }
    return 0;
 }

float   angle ( float deg1, float deg2 )
 {
  float ret = qAbs(deg1 - deg2);
  if (ret > 180) ret = 360 - ret;
  return ret;
 }

AspectId aspect ( const Star& planet1, const Star& planet2, const AspectsSet& aspectSet )
 {
  if ( planet1.isPlanet() && planet2.isPlanet()
       && planet1.getSWENum() == planet2.getSWENum() )
   return Aspect_None;

  return aspect(angle(planet1,planet2), aspectSet);
 }

AspectId aspect ( const Star& planet1, float degree, const AspectsSet& aspectSet )
 {
  return aspect(angle(planet1,degree), aspectSet);
 }

AspectId aspect ( const Star& planet, QPointF coordinate, const AspectsSet& aspectSet )
 {
  return aspect(angle(planet,coordinate), aspectSet);
 }

AspectId aspect ( float angle, const AspectsSet& aspectSet )
 {
  foreach ( const AspectType& aspect, aspectSet.aspects )
   {
    //if (aspect.id == Aspect_None) qDebug() << "aaa!";
    if (aspect.angle - aspect.orb <= angle &&
        aspect.angle + aspect.orb >= angle)
     return aspect.id;
   }

  return Aspect_None;
 }

bool towardsMovement ( const Planet& planet1, const Planet& planet2 )
 {
  const Planet *p1 = &planet1, *p2 = &planet2;

  if (!isEarlier(planet1, planet2))       // make first planet earlier than second
   {
    p1 = &planet2;
    p2 = &planet1;
   }

  return (p1->eclipticSpeed.x() > p2->eclipticSpeed.x());
 }

PlanetPosition getPosition ( const Planet& planet, ZodiacSignId sign )
 {
  if (planet.homeSigns.contains(sign))       return Position_Dwelling;
  if (planet.exaltationSigns.contains(sign)) return Position_Exaltation;
  if (planet.exileSigns.contains(sign))      return Position_Exile;
  if (planet.downfallSigns.contains(sign))   return Position_Downfall;
  return Position_Normal;
 }

const Planet* almuten ( const Horoscope& scope )
 {
  int max = 0;
  const Planet* ret = 0;

  foreach (const Planet& p, scope.planets)
   {
    int val = p.power.dignity + p.power.deficient;
    if (val > max)
     {
      max = val;
      ret = &p;
     }
   }

  return ret;
 }

const Planet* doryphoros ( const Horoscope& scope )
 {
  int minAngle = 180;
  const Planet* ret = 0;

  foreach (const Planet& p, scope.planets)
   {
    if (p.id == Planet_Sun || !p.isReal) continue;

    int val = angle(p, scope.sun);

    if (val < minAngle &&
        isEarlier(p, scope.sun))
     {
      minAngle = val;
      ret = &p;
     }
   }

  return ret;
 }

const Planet* auriga ( const Horoscope& scope )
 {
  int minAngle = 180;
  const Planet* ret = 0;

  foreach (const Planet& p, scope.planets)
   {
    if (p.id == Planet_Sun || !p.isReal) continue;

    int val = angle(p, scope.sun);

    if (val < minAngle &&
        !isEarlier(p, scope.sun))
     {
      minAngle = val;
      ret = &p;
     }
   }

  return ret;
 }

bool rulerDisposition ( int house, int houseAuthority, const Horoscope& scope )
 {
  if (house <= 0 || houseAuthority <= 0)
    return false;

  foreach (const Planet& p, scope.planets)
    if (p.house == house && p.houseRuler == houseAuthority)
      return true;

  return false;
 }

bool    isEarlier ( const Planet& planet, const Planet& sun )
 {
  //float opposite = roundDegree(sun.eclipticPos.x() - 180);
  return (roundDegree(planet.eclipticPos.x() - sun.eclipticPos.x()) > 180);
 }

/*const Planet& ruler ( int house, const Horoscope& scope )
 {
  if (house <= 0) return Planet();

  foreach (const Planet& p, scope.planets)
    if (p.houseRuler == house)
      return p;

  return Planet();
 }*/

PlanetId receptionWith   ( const Planet& planet, const Horoscope& scope )
 {
  foreach (const Planet& p, scope.planets)
   {
    if (p != planet)
     {
      if (getPosition(p, planet.sign->id) == Position_Dwelling &&
          getPosition(planet, p.sign->id) == Position_Dwelling) return p.id;

      if (getPosition(p, planet.sign->id) == Position_Exaltation &&
          getPosition(planet, p.sign->id) == Position_Exaltation) return p.id;
     }
   }

  return Planet_None;
 }


Planet calculatePlanet ( PlanetId planet,
                         const InputData& input,
                         const Houses& houses,
                         const Zodiac& zodiac )
 {
  Planet ret = getPlanet(planet);

  uint    invertPositionFlag = 256 * 1024;
  double  jd = getJulianDate(input.GMT);
  char    errStr[256] = "";
  double  xx[6];
  unsigned int flags = ret.sweFlags & ~SEFLG_TRUEPOS; // turn off true pos
  if (zodiac.id > 1) {
    flags |= SEFLG_SIDEREAL;
    swe_set_sid_mode(zodiac.id - 2, 0,0);
  }

  // TODO: wrong moon speed calculation
  // (flags: SEFLG_TRUEPOS|SEFLG_SPEED = 272)
  //         272|invertPositionFlag = 262416
  if (swe_calc_ut( jd, ret.sweNum, flags | SEFLG_SWIEPH, xx, errStr ) >= 0)
   {
    if (!(ret.sweFlags & invertPositionFlag))
      ret.eclipticPos.setX ( xx[0] );
    else                               // found 'inverted position' flag
      ret.eclipticPos.setX ( roundDegree(xx[0]-180) );

    ret.eclipticPos.setY ( xx[1] );
    ret.distance = xx[2];
    ret.eclipticSpeed.setX( xx[3] );
    ret.eclipticSpeed.setY( xx[4] );

    if (swe_calc_ut(jd, ret.sweNum, flags | SEFLG_SWIEPH | SEFLG_EQUATORIAL,
                xx, errStr) >= 0)
    {
        ret.equatorialPos.setX(xx[0]);
        ret.equatorialPos.setY(xx[1]);
    }

    double geopos[3];                  // calculate horizontal coordinates
    double hor[3];
    geopos[0] = input.location.x();
    geopos[1] = input.location.y();
    geopos[2] = input.location.z();
    swe_azalt( jd, SE_ECL2HOR, geopos, 0,0, xx, hor);
    ret.horizontalPos.setX(hor[0]);
    ret.horizontalPos.setY(hor[1]);

    double rettm;
    int eflg = SEFLG_SWIEPH;

    for (Star::angleTransitMode m = Star::atAsc;
         m < Star::numAngles;
         m = Star::angleTransitMode(m+1))
    {
        if (swe_rise_trans(houses.startSpeculum, ret.sweNum, NULL/*starname*/,
                           eflg, Star::angleTransitFlag(m),
                           geopos, 1013.25, 10,
                           &rettm, errStr) >= 0)
        {
            ret.angleTransit[m] = Planet::timeToDT(rettm);
        }
    }
    if (ret.id == Planet_SouthNode) {
        qSwap(ret.angleTransit[Star::atAsc],ret.angleTransit[Star::atDesc]);
        qSwap(ret.angleTransit[Star::atMC],ret.angleTransit[Star::atIC]);
    }
   }
  else
   {
    qDebug( "A: can't calculate position of '%s' at julian day %f: %s", qPrintable(ret.name), jd, errStr );
   }


  ret.sign          = &getSign(ret.eclipticPos.x(), zodiac);
  ret.house         = getHouse(houses, ret.eclipticPos.x());
  ret.position      = getPosition(ret, ret.sign->id);
  if (ret.homeSigns.count())
    ret.houseRuler = getHouse(ret.homeSigns.first(), houses, zodiac);

  return ret;
 }


Star calculateStar( const QString& name,
                    const InputData& input,
                    const Houses& houses,
                    const Zodiac& zodiac )
 {
  Star ret = getStar(name);

  uint    invertPositionFlag = 256 * 1024;
  double  jd = getJulianDate(input.GMT);
  char    errStr[256] = "";
  double  xx[6];
  unsigned int flags = ret.sweFlags & ~SEFLG_TRUEPOS; // turn off true pos
  if (zodiac.id > 1) {
    flags |= SEFLG_SIDEREAL;
    swe_set_sid_mode(zodiac.id - 2, 0,0);
  }

  // TODO: wrong moon speed calculation
  // (flags: SEFLG_TRUEPOS|SEFLG_SPEED = 272)
  //         272|invertPositionFlag = 262416
  char starName[256];
  strcpy(starName, ret.name.toAscii().constData());
  if (swe_fixstar_ut( starName, jd, flags | SEFLG_SWIEPH, xx, errStr ) != ERR
          && strlen(errStr)==0)
   {
    if (!(ret.sweFlags & invertPositionFlag))
      ret.eclipticPos.setX ( xx[0] );
    else                               // found 'inverted position' flag
      ret.eclipticPos.setX ( roundDegree(xx[0]-180) );

    ret.eclipticPos.setY ( xx[1] );
    ret.distance = xx[2];

    if (swe_fixstar_ut(starName, jd, flags | SEFLG_SWIEPH | SEFLG_EQUATORIAL,
                xx, errStr) != ERR
            && strlen(errStr)==0)
    {
        ret.equatorialPos.setX(xx[0]);
        ret.equatorialPos.setY(xx[1]);
    }

    double geopos[3];                  // calculate horizontal coordinates
    double hor[3];
    geopos[0] = input.location.x();
    geopos[1] = input.location.y();
    geopos[2] = input.location.z();
    swe_azalt( jd, SE_ECL2HOR, geopos, 0,0, xx, hor);
    ret.horizontalPos.setX(hor[0]);
    ret.horizontalPos.setY(hor[1]);

    double rettm;
    int eflg = SEFLG_SWIEPH;

    for (Star::angleTransitMode m = Star::atAsc;
         m < Star::numAngles;
         m = Star::angleTransitMode(m+1))
    {
        if (swe_rise_trans(houses.startSpeculum, -1, starName,
                           eflg, Star::angleTransitFlag(m),
                           geopos, 1013.25, 10,
                           &rettm, errStr) >= 0)
        {
            ret.angleTransit[m] = Planet::timeToDT(rettm);
        }
    }
   }
  else
   {
    qDebug( "A: can't calculate position of '%s' at julian day %f: %s",
            qPrintable(ret.name), jd, errStr );
   }

  ret.house         = getHouse(houses, ret.eclipticPos.x());

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
    double hcusps[14], ascmc[11];

    swe_houses_ex( julianDay, flags, input.location.y(), input.location.x(),
                   ret.system->sweCode, hcusps, ascmc );

    for (int i = 0; i < 12; i++)
        ret.cusp[i] = hcusps[i+1];

    ret.Asc   = ascmc[0];
    ret.MC    = ascmc[1];
    ret.RAMC  = ascmc[2];
    ret.Vx    = ascmc[3];
    ret.EA    = ascmc[4];

    double geopos[3] = {
        input.location.x(),
        input.location.y(),
        input.location.z()
    };

    char errStr[256];
    double xx[6];
    swe_calc_ut(julianDay, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];
    swe_calc_ut(julianDay,SE_SUN,SEFLG_SWIEPH|flags, xx, errStr);

    double housePos =
            swe_house_pos(ret.RAMC,geopos[1],eps,ret.system->sweCode,
            xx, errStr);

    int which = (housePos>=4 and housePos<10)? SE_CALC_RISE : SE_CALC_SET;
    swe_rise_trans(julianDay - 1, SE_SUN, NULL, SEFLG_SWIEPH,
                   which, geopos, 1013.25, 10,
                   &ret.startSpeculum, errStr);

    return ret;
}

PlanetPower calculatePlanetPower ( const Planet& planet, const Horoscope& scope )
 {
  PlanetPower ret;

  // TODO: does this shit works properly at all?

  if (!planet.isReal) return ret;


  bool peregrine = false;
  switch (planet.position)
   {
    case Position_Dwelling:
    case Position_Exaltation: ret.dignity   += 5; break;
    case Position_Exile:      ret.deficient -= 5; break;
    case Position_Downfall:   ret.deficient -= 4; break;
    case Position_Normal:     peregrine = true;
    default: break;
   }

  if (receptionWith(planet, scope) != Planet_None)
    ret.dignity += 5;
  else if (peregrine)
    ret.deficient -= 5;


  switch (planet.house)
   {
    case 1: case 10:         ret.dignity   += 5; break;
    case 4: case 7: case 11: ret.dignity   += 4; break;
    case 2: case 5:          ret.dignity   += 3; break;
    case 9:                  ret.dignity   += 2; break;
    case 3:                  ret.dignity   += 1; break;
    case 12:                 ret.deficient -= 5; break;
    case 8: case 6:          ret.deficient -= 2; break;
    default: break;
   }

  if (planet.eclipticSpeed.x() > 0 &&
      planet.id != Planet_Sun &&
      planet.id != Planet_Moon)
    ret.dignity += 4;

  if (planet.eclipticSpeed.x() > planet.defaultEclipticSpeed.x())
    ret.dignity += 2;
  else if (planet.eclipticSpeed.x() > 0)
    ret.deficient -= 2;
  else
    ret.deficient -= 5;

  switch (planet.id)
   {
    case Planet_Mars:
    case Planet_Jupiter:
    case Planet_Saturn:   if (isEarlier(planet, scope.sun))
                            ret.dignity += 2;
                          else
                            ret.deficient -= 2; break;
    case Planet_Mercury:
    case Planet_Venus:
    case Planet_Moon :    if (!isEarlier(planet, scope.sun))
                            ret.dignity += 2;
                          else
                            ret.deficient -= 2; break;
    default: break;
   }

  if (planet.id != Planet_Sun)
   {
    if      ( angle(planet, scope.sun) > 9 ) // not burned by sun
      ret.dignity += 5;
    else if ( angle(planet, scope.sun) < 0.4 ) // 'in cazimo'
      ret.dignity += 5;
    else // burned by sun
      ret.deficient -= 4;
   }


  switch (aspect(planet, scope.jupiter, topAspectSet()))
   {
    case Aspect_Conjunction: ret.dignity += 5; break;
    case Aspect_Trine:       ret.dignity += 4; break;
    case Aspect_Sextile:     ret.dignity += 3; break;
    default: break;
   }

  switch (aspect(planet, scope.venus, topAspectSet()))
   {
    case Aspect_Conjunction: ret.dignity += 5; break;
    case Aspect_Trine:       ret.dignity += 4; break;
    case Aspect_Sextile:     ret.dignity += 3; break;
    default: break;
   }

  switch (aspect(planet, scope.northNode, topAspectSet()))
   {
    case Aspect_Conjunction:
    /*case Aspect_Trine:
    case Aspect_Sextile:     ret.dignity   += 4; break;
    case Aspect_Opposition:  ret.deficient -= 4; break;*/
    default: break;
   }

  switch (aspect(planet, scope.mars, topAspectSet()))
   {
    case Aspect_Conjunction: ret.deficient -= 5; break;
    case Aspect_Opposition:  ret.deficient -= 4; break;
    case Aspect_Quadrature:  ret.deficient -= 3; break;
    default: break;
   }

  switch (aspect(planet, scope.saturn, topAspectSet()))
   {
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

Aspect calculateAspect ( const AspectsSet& aspectSet, const Planet& planet1, const Planet& planet2 )
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

AspectList calculateAspects ( const AspectsSet& aspectSet, const PlanetMap &planets )
 {
  AspectList ret;

  PlanetMap::const_iterator i = planets.constBegin();
  while (i != planets.constEnd())
   {
    PlanetMap::const_iterator j = i+1;
    while (j != planets.constEnd())
     {
      if (aspect(i.value(), j.value(), aspectSet) != Aspect_None)
        ret << calculateAspect(aspectSet, i.value(), j.value());
      ++j;
     }
    ++i;
   }

  return ret;
 }

AspectList calculateAspects ( const AspectsSet& aspectSet,
                              const PlanetMap& planets1,
                              const PlanetMap& planets2 )
 {
  AspectList ret;

  PlanetMap::const_iterator i = planets1.constBegin();
  while (i != planets1.constEnd())
   {
    PlanetMap::const_iterator j = planets2.constBegin();
    while (j != planets2.constEnd())
     {
      if (aspect(i.value(), j.value(), aspectSet) != Aspect_None)
        ret << calculateAspect(aspectSet, i.value(), j.value());
      ++j;
     }
    ++i;
   }

  return ret;
 }

void findPlanetStarConfigs(const PlanetMap& planets, StarMap& stars)
{
    modalize<aspectModeType> aspects(aspectMode, amcGreatCircle);

    foreach (PlanetId pid, planets.keys()) {
        const Planet& p(planets[pid]);
        foreach (const QString& name, stars.keys()) {
            Star& s(stars[name]);
            if (aspect(p, s, tightConjunction()) != Aspect_None) {
                s.configuredWithPlanet = p.id;
            }
        }
    }
}

Horoscope calculateAll ( const InputData& input )
 {
  Horoscope scope;
  scope.inputData = input;
  scope.houses = calculateHouses(input);
  scope.zodiac = getZodiac(input.zodiac);

  foreach (PlanetId id, getPlanets())
    scope.planets[id] = calculatePlanet(id, input, scope.houses, scope.zodiac);
  foreach (const QString& name, getStars()) {
      scope.stars[name] = calculateStar(name, input, scope.houses, scope.zodiac);
  }

  scope.sun        = scope.planets[Planet_Sun];
  scope.moon       = scope.planets[Planet_Moon];
  scope.mercury    = scope.planets[Planet_Mercury];
  scope.venus      = scope.planets[Planet_Venus];
  scope.mars       = scope.planets[Planet_Mars];
  scope.jupiter    = scope.planets[Planet_Jupiter];
  scope.saturn     = scope.planets[Planet_Saturn];
  scope.uranus     = scope.planets[Planet_Uranus];
  scope.neptune    = scope.planets[Planet_Neptune];
  scope.pluto      = scope.planets[Planet_Pluto];
  scope.northNode  = scope.planets[Planet_NorthNode];

  foreach (PlanetId id, scope.planets.keys())
    scope.planets[id].power = calculatePlanetPower(scope.planets[id], scope);

  scope.aspects = calculateAspects(getAspectSet(input.aspectSet), scope.planets);
  findPlanetStarConfigs(scope.planets, scope.stars);

  return scope;
 }

}
