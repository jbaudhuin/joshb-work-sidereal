#include "astro-output.h"
#include "astro-calc.h"
#include "../../zodiac/src/thememanager.h"
#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <math.h>
#include <stdio.h>
#include "../swe/swephexp.h"

// #include <QDebug>

namespace A
{

QString
romanNum(int num)
{
    static const char* h[] = { "I",   "II",   "III", "IV", "V",  "VI",
                               "VII", "VIII", "IX",  "X",  "XI", "XII" };

    if (num >= 1 && num <= 12) return h[--num];
    if (num < 0 && num >= -12) return QString("(%1)").arg(h[-num - 1]);
    return "0";
}

QString
houseTag(int num)
{
    static const char* h[] = { "Asc", "II",   "III", "IV", "V",  "VI",
                               "VII", "VIII", "IX",  "X",  "XI", "XII" };

    if (num >= 1 && num <= 12) return h[--num];
    if (num < 0 && num >= -12) return QString("(%1)").arg(h[-num - 1]);
    return "0";
}

QString
houseNum(const Planet& planet)
{
    return romanNum(planet.house);
}

QString
getPositionName(PlanetPosition p)
{
    switch (p) {
    case Position_Normal:     return QObject::tr("");
    case Position_Exaltation: return QObject::tr("Exaltation");
    case Position_Dwelling:   return QObject::tr("House");
    case Position_Downfall:   return QObject::tr("Fall");
    case Position_Exile:      return QObject::tr("Detriment");
    }

    return "";
}

QString
degreeToString(float deg, AnglePrecision precision)
{
    int d = 0, m = 0, s = 0, polarity = 0;

    if (deg < 0) {
        polarity = -1;
        deg      = -deg;
    }

    d = (int) (deg);
    m = (int) (60.0 * (deg - d));

    QString ret;
    if (precision == HighPrecision) {
        s   = (int) ((deg - (d + m / 60.0)) * 3600.0);
        ret = QString(A_DECODE("%1%2°%3'%4\""))
                  .arg((polarity < 0) ? "-" : "")
                  .arg(d)
                  .arg(m, 2, 10, QChar('0'))
                  .arg(s, 2, 10, QChar('0'));
    } else if (precision == LowPrecision) {
        ret = QString(A_DECODE("%1%2°"))
                  .arg((polarity < 0) ? "-" : "")
                  .arg(d + int(m >= 30));
    } else {
        if (m)
            ret = QString(A_DECODE("%1%2°%3'"))
                      .arg((polarity < 0) ? "-" : "")
                      .arg(d)
                      .arg(m, 2, 10, QChar('0'));
        else
            ret = QString(A_DECODE("%1%2°"))
                      .arg((polarity < 0) ? "-" : "")
                      .arg(d);
    }
    return ret;
}

QString
raToString(double raDegrees, AnglePrecision precision)
{
    // Convert RA from degrees (0-360) to degrees/minutes/seconds format
    // Normalize to 0-360 range
    while (raDegrees < 0) raDegrees += 360.0;
    while (raDegrees >= 360.0) raDegrees -= 360.0;

    int    deg       = (int) raDegrees;
    double remainder = (raDegrees - deg) * 60.0;
    int    arcmin    = (int) remainder;
    int    arcsec    = (int) ((remainder - arcmin) * 60.0);

    QString ret;
    if (precision == HighPrecision) {
        ret = QString("%1° %2' %3\"")
                  .arg(deg, 3, 10, QChar(' '))
                  .arg(arcmin, 2, 10, QChar('0'))
                  .arg(arcsec, 2, 10, QChar('0'));
    } else {
        ret = QString("%1° %2'")
                  .arg(deg, 3, 10, QChar(' '))
                  .arg(arcmin, 2, 10, QChar('0'));
    }
    return ret;
}

QString
siderealTimeToString(double raDegrees, AnglePrecision precision)
{
    // Convert RA from degrees (0-360) to sidereal time HH:MM:SS format
    double raHours = raDegrees / 15.0;

    int h = (int) raHours;
    int m = (int) ((raHours - h) * 60.0);
    int s = (int) (((raHours - h) * 60.0 - m) * 60.0);

    // Handle wrap-around
    if (h >= 24) h -= 24;
    if (h < 0) h += 24;

    QString ret;
    if (precision == HighPrecision) {
        ret = QString("%1h %2m %3s")
                  .arg(h, 2, 10, QChar('0'))
                  .arg(m, 2, 10, QChar('0'))
                  .arg(s, 2, 10, QChar('0'));
    } else {
        ret = QString("%1h %2m")
                  .arg(h, 2, 10, QChar('0'))
                  .arg(m, 2, 10, QChar('0'));
    }
    return ret;
}

QString
zodiacPosition(float          deg,
               const Zodiac&  zodiac,
               AnglePrecision precision /*=Normal*/,
               bool           isRetro /*=false*/)
{
    const ZodiacSign& sign = getSign(deg, zodiac);
    int               ang  = floor(deg) - sign.startAngle;
    if (ang < 0) ang += 360;

    QString ret;
    if (precision) {
        QString str = degreeToString(deg, precision);
        str.remove(0, str.indexOf(A_DECODE("°")));
        ret = QString("%1%2 %3").arg(ang).arg(str).arg(sign.tag);
    } else {
        int m = (int) (60.0 * (deg - (int) deg));
        ret   = QString("%1 %2 %3%4")
                  .arg(ang)
                  .arg(sign.tag)
                  .arg(m >= 10 ? "" : "0")
                  .arg(m);
    }
    if (isRetro) return ret + " R";
    return ret;
}

QString
zodiacPosition(const Star& star, const Zodiac& zodiac, AnglePrecision precision)
{
    float deg;
    switch (aspectMode) {
    case amcEquatorial:    deg = star.equatorialPos.x(); break;
    case amcPrimeVertical: deg = star.pvPos; break;
    default:
    case amcEcliptic:      deg = star.eclipticPos.x(); break;
    }

    return zodiacPosition(deg, zodiac, precision, star.isRetro());
}

void
sortPlanets(PlanetList& planets, PlanetsOrder order)
{
    if (!planets.count()) return;
    if (order == Order_NoOrder) return;

    for (int i = 0; i < planets.count(); i++) {
        for (int j = i + 1; j < planets.count(); j++) {
            if (order == Order_House) {
                if (planets[i].house > planets[j].house
                    || (planets[i].house == planets[j].house
                        && isEarlier(planets[j], planets[i])))
                {
                    Planet t   = planets[i];
                    planets[i] = planets[j];
                    planets[j] = t;
                }
            } else if (order == Order_Power) {
                if ((planets[i].power.dignity > 0
                     && planets[j].power.dignity > 0
                     && (planets[i].power.deficient + planets[i].power.dignity
                         < planets[j].power.deficient
                               + planets[j].power.dignity))
                    || (planets[i].power.dignity == 0
                        && planets[i].power.dignity > 0))
                {
                    Planet t   = planets[i];
                    planets[i] = planets[j];
                    planets[j] = t;
                }
            } else if (order == Order_Element) {
                int sign1 = planets[i].sign->id;
                int sign2 = planets[j].sign->id;
                if ((sign1 - 1) % 4 > (sign2 - 1) % 4) {
                    Planet t   = planets[i];
                    planets[i] = planets[j];
                    planets[j] = t;
                }
            }
        }
    }
}

QString
formatLatitude(float latitude, AnglePrecision precision)
{
    bool    north = latitude >= 0;
    return (north ? QObject::tr("%1N") : QObject::tr("%1S"))
        .arg(degreeToString((north ? 1 : -1) * latitude, precision));
}

QString
formatLongitude(float longitude, AnglePrecision precision)
{
    bool    east = longitude >= 0;
    return (east ? QObject::tr("%1E") : QObject::tr("%1W"))
        .arg(degreeToString((east ? 1 : -1) * longitude, precision));
}

QString
describeInput(const InputData& data)
{
    QString ret;

    auto    date = QLocale().toString(data.GMT().date(), QLocale::LongFormat);
    auto    time = QLocale().toString(data.GMT().time(), QLocale::LongFormat);
    QString dayOfWeek = data.GMT().date().toString("ddd");

    // Calendar type annotation
    QString calNote;
    if (data.calendarType() == Cal_Julian)
        calNote = " (OS)";
    else if (data.calendarType() == Cal_Gregorian)
        calNote = " (NS)";

    // Time mode annotation
    QString modeNote;
    if (data.timeMode() == Time_LMT)
        modeNote = " [LMT]";
    else if (data.timeMode() == Time_LAT)
        modeNote = " [LAT]";

    ret += "<p><strong>" + QObject::tr("Date:") + "</strong> "
           + QString("%1, %2 %3 GMT%4%5")
                 .arg(dayOfWeek, date, time, calNote, modeNote)
           + "</p>";

    QString lat = formatLatitude(data.location().y(), HighPrecision);
    QString lng = formatLongitude(data.location().x(), HighPrecision);
    
    ret += "<p><strong>" + QObject::tr("Location:") + "</strong> "
           + QString("%1 %2").arg(lat, lng) + "</p>";

    // Equation of Time
    double geolon = data.location().x();
    auto eot = computeEoT(data.GMT(), geolon, data.calendarType());
    if (eot.valid) {
        int eotMin = static_cast<int>(eot.eotSeconds) / 60;
        int eotSec = static_cast<int>(std::abs(eot.eotSeconds)) % 60;
        QString eotSign = (eot.eotSeconds >= 0) ? "+" : "-";
        ret += "<p><strong>" + QObject::tr("Equation of Time:") + "</strong> "
               + QString("%1%2m %3s").arg(eotSign)
                     .arg(std::abs(eotMin)).arg(eotSec, 2, 10, QChar('0'))
               + "</p>";
    }

    return ret;
}

QString
describeHouses(const Houses&    houses,
               const Zodiac&    zodiac,
               const PlanetMap& planets)
{
    // CONFIGURABLE: Cell padding for houses table
    const QString cellPadding = "0px";

    QString ret;
    ret +=
        "<h2>" + QObject::tr("Houses (%1)").arg(houses.system->name) + "</h2>";
    ret += "<table style='border-collapse: collapse; font-family: monospace;'>";

    // Table header
    ret += "<tr style='background-color: rgba(255,255,255,0.1);'>";
    ret += "<th style='padding: 4px 8px; text-align: left;'>"
           + QObject::tr("House") + "</th>";
    ret += "<th style='padding: 4px 8px;'></th>";
    ret += "<th style='padding: 4px 8px; text-align: right;'>"
           + QObject::tr("Cusp") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("Ruler") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("Ruler in") + "</th>";
    ret += "</tr>";

    for (int i = 0; i < 12; i++) {
        ret += "<tr>";
        ret += "<td style='padding: " + cellPadding
               + " 8px; text-align: right; font-weight: bold; color: " + ThemeManager::instance().getHeadingColor() + ";'>"
               + houseTag(i + 1) + "</td>";
        ret += "<td style='padding: " + cellPadding + " 8px;'>-</td>";
        ret +=
            "<td style='padding: " + cellPadding + " 8px; text-align: right;'>"
            + zodiacPosition(houses.cusp[i], zodiac, HighPrecision) + "</td>";

        // Find the sign on the house cusp and its ruler
        const ZodiacSign& sign       = getSign(houses.cusp[i], zodiac);
        QString           rulerName  = "";
        QString           rulerHouse = "";

        if (sign.ruler != Planet_None) {
            rulerName = getPlanet(sign.ruler).name;

            // Find which house the ruler is in
            foreach (const Planet& planet, planets) {
                if (planet.id == sign.ruler) {
                    rulerHouse = romanNum(planet.house);
                    break;
                }
            }

            if (rulerHouse.isEmpty()) {
                rulerHouse = "?";
            }
        }

        ret += "<td style='padding: " + cellPadding
               + " 8px; text-align: center;'>" + rulerName + "</td>";
        ret += "<td style='padding: " + cellPadding
               + " 8px; text-align: center;'>" + rulerHouse + "</td>";
        ret += "</tr>";
    }

    ret += "</table>";
    return ret;
}

// 2-parameter overload for backward compatibility
QString
describeHouses(const Houses& houses, const Zodiac& zodiac)
{
    PlanetMap emptyPlanets; // Empty planet map
    return describeHouses(houses, zodiac, emptyPlanets);
}

QString
describeAspect(const Aspect& aspect, bool monospace)
{
    QString ret = "<strong>" + aspect.d->name + "</strong> ";
    ret += aspect.planet1->name + "-" + aspect.planet2->name + " ";

    QString angleStr = degreeToString(aspect.angle);

    if (aspect.applying)
        ret += "<span style='color: #71aeec;'>&gt;" + angleStr + "&lt;</span>";
    else
        ret += "<span style='color: #dfb096;'>&lt;" + angleStr + "&gt;</span>";

    return ret;
}

QString
describeAspectsTable(const AspectList& aspects, AspectSortOrder sortOrder)
{
    if (aspects.isEmpty()) return "";

    // Create a copy for sorting
    AspectList sortedAspects = aspects;

    // Sort based on the specified order
    std::sort(sortedAspects.begin(),
              sortedAspects.end(),
              [sortOrder](const Aspect& a, const Aspect& b) {
                  switch (sortOrder) {
                  case SortByPlanets: {
                      // Primary: planet IDs (matches original calculateAspects
                      // order), Secondary: orb strength
                      if (a.planet1->id != b.planet1->id) {
                          return a.planet1->id < b.planet1->id;
                      }
                      if (a.planet2->id != b.planet2->id) {
                          return a.planet2->id < b.planet2->id;
                      }
                      double strengthA = a.orb / a.d->orb();
                      double strengthB = b.orb / b.d->orb();
                      return strengthA < strengthB;
                  }

                  case SortByOrbStrength: {
                      // Primary: orb strength (orb/maxOrb), tightest first
                      double strengthA = a.orb / a.d->orb();
                      double strengthB = b.orb / b.d->orb();
                      return strengthA < strengthB;
                  }

                  case SortByAspectType: {
                      // Primary: aspect type, Secondary: orb strength
                      if (a.d->name != b.d->name) {
                          return a.d->name < b.d->name;
                      }
                      double strengthA = a.orb / a.d->orb();
                      double strengthB = b.orb / b.d->orb();
                      return strengthA < strengthB;
                  }
                  }
                  return false;
              });

    // CONFIGURABLE: Cell padding for aspects table
    const QString cellPadding = "0px";

    QString ret = "<h2>" + QObject::tr("Aspects") + "</h2>";
    ret += "<table style='border-collapse: collapse; font-family: monospace; "
           "width: 100%;'>";

    // Table header
    ret += "<tr style='background-color: rgba(255,255,255,0.1);'>";
    ret += "<th style='padding: 4px 8px; text-align: left;'>"
           + QObject::tr("Aspect") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: left;'>"
           + QObject::tr("Planets") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: right;'>"
           + QObject::tr("Angle") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: right;'>"
           + QObject::tr("Orb") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("Status") + "</th>";
    ret += "</tr>";

    for (const Aspect& asp : sortedAspects) {
        // Skip aspects with invalid planet pointers
        if (!asp.planet1 || !asp.planet2 || !asp.d) {
            continue;
        }
        
        ret += "<tr>";

        // Aspect name
        ret += "<td style='padding: " + cellPadding
               + " 8px; font-weight: bold;'>" + asp.d->name + "</td>";

        // Planets
        ret += "<td style='padding: " + cellPadding + " 8px;'>"
               + asp.planet1->name + "-" + asp.planet2->name + "</td>";

        // Actual angle
        ret += "<td style='padding: " + cellPadding
               + " 8px; text-align: right;'>" + degreeToString(asp.angle)
               + "</td>";

        // Orb (current orb and max orb in degrees/minutes only, with padding
        // for alignment)
        QString currentOrbStr = degreeToString(asp.orb);
        QString maxOrbStr     = degreeToString(asp.d->orb(), NormalPrecision);

        // Pad single-digit degree numbers with &nbsp; for better alignment
        if (currentOrbStr.length() >= 2 && currentOrbStr.indexOf("°") == 1) {
            currentOrbStr = "&nbsp;" + currentOrbStr;
        }
        if (maxOrbStr.length() >= 2 && maxOrbStr.indexOf("°") == 1) {
            maxOrbStr = "&nbsp;" + maxOrbStr;
        }

        ret += "<td style='padding: " + cellPadding
               + " 8px; text-align: left;'>" + currentOrbStr
               + " (max: " + maxOrbStr + ")</td>";

        // Applying/Separating with color
        QString status =
            asp.applying ? QObject::tr("Applying") : QObject::tr("Separating");
        QString statusColor = asp.applying ? "#71aeec" : "#dfb096";
        ret += "<td style='padding: " + cellPadding
               + " 8px; text-align: center; color: " + statusColor + ";'>"
               + status + "</td>";

        ret += "</tr>";
    }

    ret += "</table>";
    return ret;
}

QString
describeAspectFull(const Aspect& asp, QString tag1, QString tag2)
{
    // if (!tag1.isEmpty()) tag1 = " (" + tag1 + ")";
    // if (!tag2.isEmpty()) tag1 = " (" + tag2 + ")";

    return QString("%1 (%2) %3%4-%5%6 [%7]\n")
               .arg(asp.d->name)
               .arg(degreeToString(asp.angle))
               .arg(asp.planet1->name)
               .arg(tag1)
               .arg(asp.planet2->name)
               .arg(tag2)
               .arg(degreeToString(asp.d->angle))
           + QObject::tr("Orb: %1 (max: %2)\n")
                 .arg(degreeToString(asp.orb))
                 .arg(degreeToString(asp.d->orb()))
           + (asp.applying ? QObject::tr("Applying")
                           : QObject::tr("Separating"));
}

QString
describePlanet(const Planet& planet, const Zodiac& zodiac)
{
    // CONFIGURABLE: Cell padding for planets table
    const QString cellPadding = "0px";

    QString ret = "<tr>";

    // Planet name
    ret += "<td style='padding: " + cellPadding
           + " 8px; font-weight: bold; color: " + ThemeManager::instance().getHeadingColor() + ";'>" + planet.name
           + "</td>";

    // Position
    auto zpos = zodiacPosition(planet, zodiac, HighPrecision);
    if (zpos.endsWith(" R")) {
        zpos = zpos.left(zpos.length() - 2)
               + "<span style='color: #ff6666;'> R</span>";
    } else {
        zpos += "&nbsp;&nbsp;";
    }
    ret += "<td style='padding: " + cellPadding + " 8px; text-align: right;'>"
           + zpos + "</td>";

    // House
    ret += "<td style='padding: " + cellPadding + " 8px; text-align: center;'>"
           + houseNum(planet) + "</td>";

    // Speed
    QString speedStr;
    if (planet.defaultEclipticSpeed.x() != 0) {
        float speed =
            planet.eclipticSpeed.x() / planet.defaultEclipticSpeed.x();
        speedStr = QString("(%1%)").arg((int) (speed * 100));
    }
    ret += "<td style='padding: " + cellPadding + " 8px; text-align: center;'>"
           + speedStr + "</td>";

    // Power
    QString powerStr;
    if (planet.power.dignity != 0 || planet.power.deficient != 0) {
        QString plus = planet.power.dignity != 0 ? "+" : "";
        powerStr     = QString("%1%2|%3")
                       .arg(plus)
                       .arg(planet.power.dignity)
                       .arg(planet.power.deficient);
        auto sp  = 3 - powerStr.indexOf('|');
        powerStr = QString(sp, ' ').replace(" ", "&nbsp;") + powerStr;
    }
    ret +=
        "<td style='padding: " + cellPadding + " 8px;'>" + powerStr + "</td>";

    // Ruler
    QStringList rs;
    for (auto r : planet.houseRuler) {
        rs << romanNum(r);
    }
    QString rulerStr = rs.isEmpty() ? "" : rs.join("+");
    ret +=
        "<td style='padding: " + cellPadding + " 8px;'>" + rulerStr + "</td>";

    // Position name
    QString positionStr;
    if (planet.position != Position_Normal) {
        positionStr = getPositionName(planet.position);
    }
    ret += "<td style='padding: " + cellPadding + " 8px; font-style: italic;'>"
           + positionStr + "</td>";

    ret += "</tr>";
    return ret;
}

QString
describePlanetCoord(const Planet& planet)
{
    QString ret;

    ret += QObject::tr("Longitude: %1\n")
               .arg(degreeToString(planet.eclipticPos.x(), HighPrecision));
    ret += QObject::tr("Latitude: %1\n")
               .arg(degreeToString(planet.eclipticPos.y(), HighPrecision));
    ret += QObject::tr("Right Ascension: %1\n")
               .arg(degreeToString(planet.equatorialPos.x(), HighPrecision));
    ret += QObject::tr("Prime Vertical: %1\n")
               .arg(degreeToString(planet.pvPos, HighPrecision));
    ret += QObject::tr("Declination: %1\n")
               .arg(degreeToString(planet.equatorialPos.y(), HighPrecision));
    ret += QObject::tr("Distance: %1a.u.\n").arg(planet.distance);
    ret += QObject::tr("Azimuth: %1\n")
               .arg(degreeToString(planet.horizontalPos.x(), HighPrecision));
    ret += QObject::tr("Height: %1\n")
               .arg(degreeToString(planet.horizontalPos.y(), HighPrecision));

    if (planet.isReal) {
        ret +=
            QObject::tr("Speed: %1% (%2 per day)\n")
                .arg((int) (planet.eclipticSpeed.x()
                            / planet.defaultEclipticSpeed.x() * 100))
                .arg(degreeToString(planet.eclipticSpeed.x(), HighPrecision));
    }

    return ret;
}

QString
describePlanetCoordInHtml(const Planet& planet)
{
    QString ret = describePlanetCoord(planet);
    ret.replace(QRegularExpression(QString::fromLocal8Bit("(: [!-° ]+)")),
                "<font color='#e9e9e4'>\\1</font>"); // replaces values
    ret.replace("\n", "<br>");
    return ret;
}

QString
describePower(const Planet& planet, const Horoscope& scope)
{
    if (!planet.isReal) return "";

    QStringList ret;

    // TODO: finally work out

    bool peregrine = false;
    switch (planet.position) {
    case Position_Dwelling:
        ret << QObject::tr("+5: Planet is in its own sign");
        break;
    case Position_Exaltation:
        ret << QObject::tr("+5: Planet is in exaltation");
        break;
    case Position_Exile:
        ret << QObject::tr("-5: Planet is in detriment");
        break;
    case Position_Downfall:
        ret << QObject::tr("-4: Planet is in its fall");
        break;
    case Position_Normal: peregrine = true;
    default:              break;
    }

    PlanetId pl = receptionWith(planet, scope);
    if (pl != Planet_None)
        ret << QObject::tr("+5: Planet is in mutual reception with %1")
                   .arg(getPlanetName(pl));
    else if (peregrine)
        ret << QObject::tr(
            "-5: Planet is peregrine (doesn't have an essential dignity)");

    int     h = planet.house;
    QString p;
    switch (h) {
    case 1:
    case 10: p = "+5"; break;
    case 4:
    case 7:
    case 11: p = "+4"; break;
    case 2:
    case 5:  p = "+3"; break;
    case 9:  p = "+2"; break;
    case 3:  p = "+1"; break;
    case 12: p = "-5"; break;
    case 8:
    case 6:  p = "-2"; break;
    default: break;
    }
    if (!p.isEmpty())
        ret << QObject::tr("%1: Planet is placed in %2 house")
                   .arg(p)
                   .arg(romanNum(h));

    if (planet.eclipticSpeed.x() > 0 && planet.id != Planet_Sun
        && planet.id != Planet_Moon)
        ret << QObject::tr("+4: Planet is direct");

    if (planet.eclipticSpeed.x() > planet.defaultEclipticSpeed.x())
        ret << QObject::tr("+2: Planet is fast");
    else if (planet.eclipticSpeed.x() > 0)
        ret << QObject::tr("-2: Planet is slow");
    else
        ret << QObject::tr("-5: Planet is retrograde");

    switch (planet.id) {
    case Planet_Mars:
    case Planet_Jupiter:
    case Planet_Saturn:

        if (isEarlier(planet, scope.sun))
            ret << QObject::tr("+2: %1 rises earlier than the Sun (oriental)")
                       .arg(planet.name);
        else
            ret << QObject::tr("-2: %1 rises later than the Sun (occidental)")
                       .arg(planet.name);
        break;

    case Planet_Mercury:
    case Planet_Venus:

        if (!isEarlier(planet, scope.sun))
            ret << QObject::tr("+2: %1 rises later than the Sun (occidental)")
                       .arg(planet.name);
        else
            ret << QObject::tr("-2: %1 rises earlier than the Sun (occidental)")
                       .arg(planet.name);
        break;

    case Planet_Moon:

        if (!isEarlier(planet, scope.sun))
            ret << QObject::tr("+2: Moon is waxing");
        else
            ret << QObject::tr("-2: Moon is waning");
        break;

    default: break;
    }

    if (planet.id != Planet_Sun) {
        if (angle(planet, scope.sun) > 9)
            ret << QObject::tr(
                "+5: Planet is neither combust nor under the beams");
        else if (angle(planet, scope.sun) < 0.4)
            ret << QObject::tr("+5: Planet is cazimi");
        else
            ret << QObject::tr(
                "-4: Planet is either combust or under the beams");
    }

    if (planet.id != Planet_Jupiter)
        switch (aspect(planet, scope.jupiter, topAspectSet())) {
        case Aspect_Conjunction:
            ret << QObject::tr(
                "+5: Planet is in partile conjunction with Jupiter");
            break;
        case Aspect_Trine:
            ret << QObject::tr("+4: Planet is in partile trine with Jupiter");
            break;
        case Aspect_Sextile:
            ret << QObject::tr("+3: Planet is in partile sextile with Jupiter");
            break;
        default: break;
        }

    if (planet.id != Planet_Venus) {
        switch (aspect(planet, scope.venus, topAspectSet())) {
        case Aspect_Conjunction:
            ret << QObject::tr(
                "+5: Planet is in partile conjunction with Venus");
            break;
        case Aspect_Trine:
            ret << QObject::tr("+4: Planet is in partile trine with Venus");
            break;
        case Aspect_Sextile:
            ret << QObject::tr("+3: Planet is in partile sextile with Venus");
            break;
        default: break;
        }
    }

    if (planet.id != Planet_NorthNode) {
        switch (aspect(planet, scope.northNode, topAspectSet())) {
        case Aspect_Conjunction:
            ret << QObject::tr(
                "+4: Planet is in partile conjunction with North Node");
            break;
        /*case Aspect_Trine:       ret << QObject::tr("+4: Planet is in partile
        trine with North Node"); break; case Aspect_Sextile:     ret <<
        QObject::tr("+4: Planet is in partile sextile with North Node"); break;
        case Aspect_Opposition:  ret << QObject::tr("-4: Planet is in partile
        opposition with North Node"); break;*/
        default: break;
        }
    }

    if (planet.id != Planet_Mars) {
        switch (aspect(planet, scope.mars, topAspectSet())) {
        case Aspect_Conjunction:
            ret << QObject::tr(
                "-5: Planet is in partile conjunction with Mars");
            break;
        case Aspect_Opposition:
            ret << QObject::tr("-4: Planet is in partile opposition with Mars");
            break;
        case Aspect_Quadrature:
            ret << QObject::tr("-3: Planet is in partile quadrature with Mars");
            break;
        default: break;
        }
    }

    if (planet.id != Planet_Saturn) {
        switch (aspect(planet, scope.saturn, topAspectSet())) {
        case Aspect_Conjunction:
            ret << QObject::tr(
                "-5: Planet is in partile conjunction with Saturn");
            break;
        case Aspect_Opposition:
            ret << QObject::tr(
                "-4: Planet is in partile opposition with Saturn");
            break;
        case Aspect_Quadrature:
            ret << QObject::tr(
                "-3: Planet is in partile quadrature with Saturn");
            break;
        default: break;
        }
    }

    if (aspect(planet, scope.stars["Regulus"], tightConjunction())
        == Aspect_Conjunction)
    {
        ret << QObject::tr("+6: Planet is in conjunction with Regulus");
    }

    if (aspect(planet, scope.stars["Spica"], tightConjunction())
        == Aspect_Conjunction)
    {
        ret << QObject::tr("+5: Planet is in conjunction with Spica");
    }

    if (aspect(planet, scope.stars["Algol"], tightConjunction())
        == Aspect_Conjunction)
    {
        ret << QObject::tr("-5: Planet is in conjunction with Algol");
    }

    // sort values from biggest to smallest

    for (int i = 0; i < ret.count(); i++) {
        for (int j = i + 1; j < ret.count(); j++) {
            int val1 = ret[i].at(1).digitValue();
            int val2 = ret[j].at(1).digitValue();

            if (ret[i][0] == '-') val1 = -val1;
            if (ret[j][0] == '-') val2 = -val2;

            if (val1 < val2) {
                QString t = ret[i];
                ret[i]    = ret[j];
                ret[j]    = t;
            }
        }
    }

    return ret.join(";\n") + '.';
}

QString
describePowerInHtml(const Planet& planet, const Horoscope& scope)
{
    QString ret = describePower(planet, scope);
    if (ret.isEmpty()) return ret;

    ret.replace("\n", "</p><p>");

    static QRegularExpression minus { "(-\\d:)" };
    ret.replace(
        minus,
        "<font color='#dfb096'><b>\\1</b></font>"); // replaces negative values

    static QRegularExpression plus { "(\\+\\d:)" };
    ret.replace(
        plus,
        "<font color='#71aeec'><b>\\1</b></font>"); // replaces positive values

    return "<p>" + ret + "</p>";
}

QString
_formatTime(const QDateTime& dt, double tz)
{
    if (!dt.isValid()) {
        return "    --    ";
    }
    QString   dow(QObject::tr("MtWTFsS"));
    QDateTime ldt = dt.addSecs(static_cast<qint64>(tz * 3600));
    return QString("%2 %1")
        .arg(ldt.time().toString())
        .arg(dow[ldt.date().dayOfWeek() - 1]);
}

namespace
{

struct event {
    QDateTime   _dt;
    const Star* _star;
    unsigned    _pivot;

    static int       _maxWidth;
    static QDateTime _radix;
    static double    _radixRA; // Radix Local Sidereal Time in RA degrees
    static const PSSRContext* _pssrCtx; // PSSR context for return charts
    static bool      _isProgressed; // Skip date calculations for progressed charts
    static bool      _isSolarReturn; // Is this a solar return chart?
    static bool      _isSolarIngress; // Is this a solar ingress chart?

    event() : _star(NULL), _pivot(0) { }

    event(const QDateTime& dt, const Star* planet, unsigned pivot) :
        _dt(dt),
        _star(planet),
        _pivot(pivot)
    {
    }

    event(const event& other) :
        _dt(other._dt),
        _star(other._star),
        _pivot(other._pivot)
    {
    }

    event& operator=(const event& other)
    {
        _dt    = other._dt;
        _star  = other._star;
        _pivot = other._pivot;
        return *this;
    }

    bool operator<(const event& other) const
    {
#if 0
        double jda = getJulianDate(_dt), jdo = getJulianDate(other._dt);
        std::string dta(_dt.toString(Qt::ISODate).toStdString()), dto(other._dt.toString(Qt::ISODate).toStdString());
        bool pluton = _star && _star->getPlanetId() == Planet_Pluto
            || other._star && other._star->getPlanetId() == Planet_Pluto;
#endif
        if (_dt != other._dt) return (_dt < other._dt);
        return (_pivot < other._pivot);
    }

    QString fmt(double              tz,
                const QString&      padding        = "1px",
                bool                isFirstInGroup = false,
                SpeculumDisplayMode displayMode    = DisplayLocalTime) const
    {
        static QStringList AT { QObject::tr("Rise"),
                                QObject::tr("Set"),
                                QObject::tr("MC"),
                                QObject::tr("IC"),
                                "----" };

        QString       planetName = _star ? _star->name : QObject::tr("*Radix*");
        const Planet* planet     = dynamic_cast<const Planet*>(_star);

        QString borderStyle =
            isFirstInGroup ? " border-top: 1px solid #777;" : "";
        QString backgroundColor =
            planet ? " background-color: rgba(255,255,255,0.03);" : "";
        QString ret = "<tr>";

        // Planet name cell gets bold styling, other cells do not
        QString nameCellStyle =
            "padding: " + padding + " 8px;" + borderStyle + backgroundColor;
        if (planet) {
            nameCellStyle += " font-weight: bold;";
        }

        // Data cells don't get bold, just border and background
        QString dataCellStyle =
            "padding: " + padding + " 8px;" + borderStyle + backgroundColor;

        // Planet name with color emphasis for planets
        if (planet) {
            ret += "<td style='" + nameCellStyle + " color: " + ThemeManager::instance().getHeadingColor() + ";'>"
                   + planetName + "</td>";
        } else {
            ret += "<td style='" + nameCellStyle + "'>" + planetName + "</td>";
        }

        ret += "<td style='" + dataCellStyle + " text-align: center;'>"
               + AT.at(_pivot) + "</td>";

        // Display time/RA based on mode
        if (displayMode == DisplaySiderealTime) {
            // Sidereal Time mode: show HH:MM:SS format
            if (_star) {
                if (!_dt.isValid()) {
                    ret += "<td style='" + dataCellStyle
                           + " text-align: right;'>    --    </td>";
                } else {
                    ret += "<td style='" + dataCellStyle
                           + " text-align: right;'>"
                           + siderealTimeToString(_star->angleTransitRA[_pivot],
                                                  HighPrecision)
                           + "</td>";
                }
            } else {
                // Radix: show radix sidereal time
                ret += "<td style='" + dataCellStyle + " text-align: right;'>"
                       + siderealTimeToString(_radixRA, HighPrecision)
                       + "</td>";
            }
        } else if (displayMode == DisplayRightAscension) {
            // RA mode: show degrees/minutes/seconds
            if (_star) {
                if (!_dt.isValid()) {
                    ret += "<td style='" + dataCellStyle
                           + " text-align: right;'>    --    </td>";
                } else {
                    ret += "<td style='" + dataCellStyle
                           + " text-align: right;'>"
                           + raToString(_star->angleTransitRA[_pivot],
                                        HighPrecision)
                           + "</td>";
                }
            } else {
                // Radix: show radix RA in degrees
                ret += "<td style='" + dataCellStyle + " text-align: right;'>"
                       + raToString(_radixRA, HighPrecision) + "</td>";
            }
        } else {
            // Local time mode: show formatted local time
            ret += "<td style='" + dataCellStyle + " text-align: right;'>"
                   + _formatTime(_dt, tz) + "</td>";
        }

        // Angular date calculation (PD or PSSR) - skip in sidereal/RA modes and progressed charts
        if (_star && displayMode == DisplayLocalTime && !_isProgressed) {
            // i.e., not radix, not progressed, and local time mode
            // Get the star/planet's RA and the angle RA
            double planetRA = _star->equatorialPos.x();
            double angleRA = _star->angleTransitRA[_pivot];
            
            QDateTime angularDateGMT = calculateAngularDate(_radix, _dt, planetRA, angleRA, _pssrCtx);
            QString method = "PD"; // Default to Primary Directions
            QString dateFormat = "yyyy/MM/dd";
            if (_pssrCtx) {
                method = _isSolarReturn ? "PSSR" : "NeoSQ";
                dateFormat = "ddd yyyy-MM-dd hh:mm";
            }
            
            // Convert to local time using proper timezone offset
            int offsetSeconds = tz * 3600;
            QTimeZone timeZone = QTimeZone::fromSecondsAheadOfUtc(offsetSeconds);
            QDateTime localDate = angularDateGMT.toTimeZone(timeZone);
            
            ret += "<td style='" + dataCellStyle + "'> --&gt; "
                   + localDate.toString(dateFormat)
                   + " (" + method + ")</td>";
        } else {
            ret += "<td style='" + dataCellStyle + "'></td>";
        }

        ret += "</tr>";
        return ret;
    }
};

int       event::_maxWidth = 0;
QDateTime event::_radix;
double    event::_radixRA = 0.0;
const PSSRContext* event::_pssrCtx = nullptr;
bool      event::_isProgressed = false;
bool      event::_isSolarReturn = false;
bool      event::_isSolarIngress = false;
} // namespace

QString
describeParans(const AstroFileList& scopes,
               bool                 showAll,
               bool                 showFixedStars,
               double               paranOrb,
               SpeculumDisplayMode  displayMode)
{
    // CONFIGURABLE: Cell padding for parans table - change this to adjust row
    // spacing Suggested values: "0px" (tight), "1px" (normal), "2px" (loose)
    const QString cellPadding = "0px";

    bool  showDates = scopes.count() == 1;
    auto  scope     = scopes.first()->horoscope();
    double tz        = scope.inputData.tz();

    // Check if this is a return chart and get PSSR context
    event::_pssrCtx = nullptr;
    event::_isProgressed = false;
    event::_isSolarReturn = false;
    event::_isSolarIngress = false;
    AstroFile* file = scopes.first();
    
    // Check if it's a progressed chart based on the file TYPE, not just presence of base chart
    // Base chart is just metadata about natal relationship - doesn't mean it's progressed
    if (file && file->hasBaseChart()) {
        FileType ftype = file->getType();
        if (ftype == TypeDerivedProg || ftype == TypeDerivedSA || ftype == TypeDerivedPD) {
            event::_isProgressed = true;
        }
    }
    
    // Check if it's a solar-based chart (solar return or solar ingress)
    if (file && isSolarBasedChart(file->getName())) {
        // It's a solar-based chart - try to get or calculate PSSR/NeoSQ context
        if (!file->hasPSSRContext()) {
            auto ctx = calculatePSSRContext(file->horoscope(), useApparentSun);
            file->setPSSRContext(ctx);
        }
        if (file->hasPSSRContext()) {
            event::_pssrCtx = &file->pssrContext();
            event::_isSolarReturn = isSolarReturn(file->getName());
            event::_isSolarIngress = isSolarIngress(file->getName());
        }
    }

    QVector<event> events;
    events << event(scope.inputData.GMT(), NULL, 4); // radix

    int& maxWidth(event::_maxWidth);
    maxWidth = 0;

    event::_radix = scope.inputData.GMT();

    // Get Local Sidereal Time (RAMC) from houses - already calculated
    event::_radixRA = scope.houses.RAMC;

    for (const Planet& p : std::as_const(scope.planets)) {
        if (p.id == Planet_MC || p.id == Planet_Asc) continue;
        unsigned u = 0;
        for (const QDateTime& dt : p.angleTransit) {
            if (!dt.isValid()) {
                u++;
                continue;
            }
            if (p.name.length() > maxWidth) {
                maxWidth = p.name.length();
            }
            events << event(dt, p, u++);
        }
    }

    if (showFixedStars) {
        for (const Star& s : std::as_const(scope.stars)) {
            unsigned u = 0;
            for (const QDateTime& dt : s.angleTransit) {
                if (!dt.isValid()) {
                    u++;
                    continue;
                }
                if (s.name.length() > maxWidth) {
                    maxWidth = s.name.length();
                }
                events << event(dt, s, u++);
            }
        }
    }

    std::sort(events.begin(), events.end());

    QString ret = "<h2>" + QObject::tr("Parans") + "</h2>";
    ret += "<table style='border-collapse: collapse; font-family: monospace;'>";
    ret += "<tr style='background-color: rgba(255,255,255,0.1);'>";
    ret += "<th style='padding: 4px 8px; text-align: left;'>"
           + QObject::tr("Planet") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("Event") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: right;'>";
    if (displayMode == DisplaySiderealTime) {
        ret += QObject::tr("Sidereal Time");
    } else if (displayMode == DisplayRightAscension) {
        ret += QObject::tr("RA");
    } else {
        ret += QObject::tr("LT");
    }
    ret += "</th>";
    ret += "<th style='padding: 4px 8px;'></th>";
    ret += "</tr>";

    double orb        = paranOrb * 240;
    bool   anyPrinted = false;

    auto lastPrinted = events.constEnd();
    for (auto it = events.constBegin();
         it != events.constEnd();) // Note: no ++it here, we manage it manually
    {
        if (showAll) {
            ret += it->fmt(tz, cellPadding, false, displayMode);
            ++it;
            continue;
        }
        const Planet* p = dynamic_cast<const Planet*>(it->_star);
        if (p || !it->_star /*radix*/) {
            int                           j   = 1;
            QVector<event>::ConstIterator bit = it;
            while (bit != events.constBegin()
                   && qAbs((*(bit - 1))._dt.secsTo(it->_dt)) <= orb)
            {
                if (bit - 1 == lastPrinted) {
                    anyPrinted = false;
                    break;
                }
                ++j;
                --bit;
            }
            QVector<event>::ConstIterator lastPlanet = it;
            QVector<event>::ConstIterator nit;
            for (nit = it + 1; nit != events.constEnd()
                               && qAbs(nit->_dt.secsTo(lastPlanet->_dt)) <= orb;
                 ++nit)
            {
                if (dynamic_cast<const Planet*>(nit->_star)) {
                    lastPlanet = nit;
                }
                ++j;
            }
            // Remove the separate divider row approach
            // if (anyPrinted) {
            //     ret += "<tr style='height: 0; line-height: 0;'><td
            //     colspan='4' style='padding: 0; margin: 0; border-top: 1px
            //     solid #777; height: 0; line-height: 0; font-size:
            //     0;'></td></tr>"; // visual divider
            // }
            // Process events in this group
            bool isFirstInThisGroup = true;
            while (bit != events.constEnd() && j-- > 0) {
                // Add border to first row of group if there was a previous
                // group
                bool addBorder = (isFirstInThisGroup && anyPrinted);
                ret += bit->fmt(tz, cellPadding, addBorder, displayMode);
                lastPrinted = bit;
                ++bit;
                anyPrinted         = true;
                isFirstInThisGroup = false;
            }
            // Move iterator to the end of this group
            it = nit;
        } else {
            ++it; // Skip non-planet, non-radix events when not showing all
        }
    }

    ret += "</table>";

    // Clean up static context
    event::_pssrCtx = nullptr;
    event::_isProgressed = false;    return ret;
}

QString
describeSpeculum(const Horoscope&    scope,
                 bool                showFixedStars,
                 SpeculumDisplayMode displayMode)
{
    double tz = scope.inputData.tz();
    int&  maxWidth(event::_maxWidth);
    if (maxWidth == 0) {
        for (const Planet& p : scope.planets) {
            if (p.id != Planet_MC && p.id != Planet_Asc
                && p.name.length() > maxWidth)
            {
                maxWidth = p.name.length();
            }
        }
        for (const Star& s : scope.stars) {
            if (s.name.length() > maxWidth) {
                maxWidth = s.name.length();
            }
        }
    }

    QString ret = "<h2>" + QObject::tr("Speculum") + "</h2>";
    ret += "<table style='border-collapse: collapse; font-family: monospace;'>";
    ret += "<tr style='background-color: rgba(255,255,255,0.1);'>";
    ret += "<th style='padding: 4px 8px; text-align: left;'>"
           + QObject::tr("Planet") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("Rise") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("MC") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("Set") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("IC") + "</th>";
    ret += "</tr>";

    for (const Planet& p : scope.planets) {
        if (p.id == Planet_MC || p.id == Planet_Asc) continue;
        ret += "<tr>";
        ret +=
            "<td style='padding: 0px 8px; font-weight: bold; color: " + ThemeManager::instance().getHeadingColor() + ";'>"
            + p.name + "</td>";
        for (int i : QList<int>({ 0, 2, 1, 3 })) {
            QString timeStr;
            // Check if transit time is valid
            if (!p.angleTransit.at(i).isValid()) {
                timeStr = "    --    ";
            } else if (displayMode == DisplaySiderealTime) {
                timeStr =
                    siderealTimeToString(p.angleTransitRA[i], HighPrecision);
            } else if (displayMode == DisplayRightAscension) {
                timeStr = raToString(p.angleTransitRA[i], HighPrecision);
            } else {
                timeStr = _formatTime(p.angleTransit.at(i), tz);
            }
            ret += "<td style='padding: 0px 8px; text-align: left;'>" + timeStr
                   + "</td>";
        }
        ret += "</tr>";
    }

    if (showFixedStars) {
        for (const Star& s : scope.stars) {
            ret += "<tr>";
            ret += "<td style='padding: 0px 8px;'>" + s.name + "</td>";
            for (int i : QList<int>({ 0, 2, 1, 3 })) {
                QString timeStr;
                // Check if transit time is valid
                if (!s.angleTransit.at(i).isValid()) {
                    timeStr = "    --    ";
                } else if (displayMode == DisplaySiderealTime) {
                    timeStr = siderealTimeToString(s.angleTransitRA[i],
                                                   HighPrecision);
                } else if (displayMode == DisplayRightAscension) {
                    timeStr = raToString(s.angleTransitRA[i], HighPrecision);
                } else {
                    timeStr = _formatTime(s.angleTransit.at(i), tz);
                }
                ret += "<td style='padding: 0px 8px; text-align: left;'>"
                       + timeStr + "</td>";
            }
            ret += "</tr>";
        }
    }

    ret += "</table>";
    maxWidth = 0; // reset for next time...
    return ret;
}

QString
describe(AstroFileList&& scopes,
         Articles        article /*=All*/,
         double          paranOrb /*=1.0*/)
{
    // Get theme-appropriate colors
    QString textColor = ThemeManager::instance().getTextColor();
    QString headingColor = ThemeManager::instance().getHeadingColor();
    
    QString ret;
    ret += "<!DOCTYPE html><html><head>";
    ret += "<meta charset='utf-8'>";
    ret += "<style>";
    ret += "body { font-family: 'Consolas', 'Courier New', courier, 'DejaVu "
           "Sans Mono', 'Lucida Console'; margin: 10px; color: " + textColor + "; "
           "background-color: transparent; }";
    ret +=
        "h1, h2, h3 { color: " + headingColor + "; margin-top: 20px; margin-bottom: 10px; }";
    ret += "h1 { font-size: 1.4em; }";
    ret += "h2 { font-size: 1.2em; }";
    ret += "h3 { font-size: 1.1em; }";
    ret += "h4 { color: " + headingColor + "; font-size: 1.0em; margin-top: 12px; "
           "margin-bottom: 4px; }";
    ret += "table { margin: 10px 0; border-collapse: collapse; "
           "background-color: transparent; }";
    ret += "th { background-color: rgba(255,255,255,0.1); font-weight: bold; "
           "color: " + headingColor + "; border: 1px solid #555; }";
    ret += "td { border: 1px solid #555; color: " + textColor + "; }";
    ret += "tr:nth-child(even) { background-color: rgba(255,255,255,0.05); }";
    ret +=
        ".planets-table td:first-child { font-weight: bold; color: " + headingColor + "; }";
    ret += "p { color: " + textColor + "; margin: 2px 0; line-height: 1.2; }";
    ret += "ul { color: " + textColor + "; margin: 4px 0; }";
    ret += "li { margin: 1px 0; line-height: 1.2; }";
    ret += "strong { color: " + headingColor + "; }";
    ret += ".dignity-list { margin: 4px 0; }";
    ret += ".dignity-list p { margin: 1px 0; padding: 0; line-height: 1.1; }";
    ret += "</style>";
    ret += "</head><body>";

    auto scope = scopes.first()->horoscope();

    ret += "<h2>" + QObject::tr("%1 sign").arg(scope.zodiac.name) + "</h2>";

    if (article & Article_Input) {
        ret += describeInput(scope.inputData);
    }

    if ((article & Article_Planet) && scope.planets.count()) {
        ret += "<h2>" + QObject::tr("Planets") + "</h2>";
        ret +=
            "<table class='planets-table' style='border-collapse: collapse; >";
        ret += "<tr style='background-color: rgba(255,255,255,0.1);'>";
        ret += "<th style='padding: 4px 8px; text-align: left;'>"
               + QObject::tr("Planet") + "</th>";
        ret += "<th style='padding: 4px 8px; text-align: right;'>"
               + QObject::tr("Position") + "</th>";
        ret += "<th style='padding: 4px 8px; text-align: center;'>"
               + QObject::tr("House") + "</th>";
        ret += "<th style='padding: 4px 8px; text-align: center;'>"
               + QObject::tr("Speed") + "</th>";
        ret += "<th style='padding: 4px 8px; text-align: center;'>"
               + QObject::tr("Power") + "</th>";
        ret += "<th style='padding: 4px 8px;'>" + QObject::tr("Ruler of")
               + "</th>";
        ret +=
            "<th style='padding: 4px 8px;'>" + QObject::tr("Status") + "</th>";
        ret += "</tr>";

        foreach (const Planet& p, scope.planets)
            ret += describePlanet(p, scope.zodiac);

        ret += "</table>";

        if (auto p = auriga(scope)) {
            ret += "<p><strong>" + QObject::tr("Auriga:") + "</strong> "
                   + p->name + "</p>";
        }
        if (auto p = almuten(scope)) {
            ret += "<p><strong>" + QObject::tr("Almuten:") + "</strong> "
                   + p->name + "</p>";
        }
        if (auto p = doryphoros(scope)) {
            ret += "<p><strong>" + QObject::tr("Doryphoros:") + "</strong> "
                   + p->name + "</p>";
        }
    }

    if ((article & Article_Houses) && scope.houses.system)
        ret += describeHouses(scope.houses, scope.zodiac, scope.planets);

    if ((article & Article_Aspects) && scope.aspects.count()) {
        ret += describeAspectsTable(scope.aspects);
    }

    if ((article & Article_Power) && scope.planets.count()) {
        ret += "<h2>" + QObject::tr("Planetary Dignities") + "</h2>";
        foreach (const Planet& p, scope.planets) {
            if (p.isReal) {
                ret += "<h4>" + p.name + "</h4>";
                ret += "<div class='dignity-list'>"
                       + describePowerInHtml(p, scope) + "</div>";
            }
        }
    }

    if ((article & Article_Parans) && scope.planets.count()) {
        ret += describeParans(scopes,
                              bool(article & Article_DiurnalEvents),
                              bool(article & Article_FixedStars),
                              paranOrb);
    }

    if ((article & Article_Speculum) && scope.planets.count()) {
        ret += describeSpeculum(scope, bool(article & Article_FixedStars));
    }

    ret += "</body></html>";
    return ret;
}

} // namespace A
