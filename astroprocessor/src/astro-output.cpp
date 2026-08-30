#include "astro-output.h"
#include "astro-calc.h"
#include "citydb.h"
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

    // Calendar type annotation. Shown even under Cal_Auto (which silently
    // applies Julian before the 1582 cutover) so the effective calendar is
    // never ambiguous.
    QString calNote;
    if (data.calendarType() == Cal_Julian)
        calNote = " (OS)";
    else if (data.calendarType() == Cal_Gregorian)
        calNote = " (NS)";
    else if (data.resolvedSweCalFlag() == 0)
        calNote = " (OS)"; // Auto resolved to Julian (pre-1582 cutover)

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
// IAU 3-letter constellation abbreviations (as parsed into Star::constellation
// from sefstars.txt's Bayer designation) -> full name. For the 12 zodiacal
// constellations, signIndex is the 0-based sign order (Ari=0..Psc=11,
// matching astroprocessor/signs.csv row order) so the Almagest sign glyph can
// be looked up via Data::getSignGlyph(Ingresses_Start + signIndex); -1 marks
// a non-zodiacal constellation (no glyph, name-only tooltip).
struct ConstellationInfo {
    const char* name;
    int         signIndex;
};

const QHash<QString, ConstellationInfo>&
constellationTable()
{
    static const QHash<QString, ConstellationInfo> table {
        { "And", { "Andromeda", -1 } },
        { "Ant", { "Antlia", -1 } },
        { "Aps", { "Apus", -1 } },
        { "Aqr", { "Aquarius", 10 } },
        { "Aql", { "Aquila", -1 } },
        { "Ara", { "Ara", -1 } },
        { "Ari", { "Aries", 0 } },
        { "Aur", { "Auriga", -1 } },
        { "Boo", { "Bootes", -1 } },
        { "Cae", { "Caelum", -1 } },
        { "Cam", { "Camelopardalis", -1 } },
        { "Cnc", { "Cancer", 3 } },
        { "CVn", { "Canes Venatici", -1 } },
        { "CMa", { "Canis Major", -1 } },
        { "CMi", { "Canis Minor", -1 } },
        { "Cap", { "Capricornus", 9 } },
        { "Car", { "Carina", -1 } },
        { "Cas", { "Cassiopeia", -1 } },
        { "Cen", { "Centaurus", -1 } },
        { "Cep", { "Cepheus", -1 } },
        { "Cet", { "Cetus", -1 } },
        { "Cha", { "Chamaeleon", -1 } },
        { "Cir", { "Circinus", -1 } },
        { "Col", { "Columba", -1 } },
        { "Com", { "Coma Berenices", -1 } },
        { "CrA", { "Corona Australis", -1 } },
        { "CrB", { "Corona Borealis", -1 } },
        { "Crv", { "Corvus", -1 } },
        { "Crt", { "Crater", -1 } },
        { "Cru", { "Crux", -1 } },
        { "Cyg", { "Cygnus", -1 } },
        { "Del", { "Delphinus", -1 } },
        { "Dor", { "Dorado", -1 } },
        { "Dra", { "Draco", -1 } },
        { "Equ", { "Equuleus", -1 } },
        { "Eri", { "Eridanus", -1 } },
        { "For", { "Fornax", -1 } },
        { "Gem", { "Gemini", 2 } },
        { "Gru", { "Grus", -1 } },
        { "Her", { "Hercules", -1 } },
        { "Hor", { "Horologium", -1 } },
        { "Hya", { "Hydra", -1 } },
        { "Hyi", { "Hydrus", -1 } },
        { "Ind", { "Indus", -1 } },
        { "Lac", { "Lacerta", -1 } },
        { "Leo", { "Leo", 4 } },
        { "LMi", { "Leo Minor", -1 } },
        { "Lep", { "Lepus", -1 } },
        { "Lib", { "Libra", 6 } },
        { "Lup", { "Lupus", -1 } },
        { "Lyn", { "Lynx", -1 } },
        { "Lyr", { "Lyra", -1 } },
        { "Men", { "Mensa", -1 } },
        { "Mic", { "Microscopium", -1 } },
        { "Mon", { "Monoceros", -1 } },
        { "Mus", { "Musca", -1 } },
        { "Nor", { "Norma", -1 } },
        { "Oct", { "Octans", -1 } },
        { "Oph", { "Ophiuchus", -1 } },
        { "Ori", { "Orion", -1 } },
        { "Pav", { "Pavo", -1 } },
        { "Peg", { "Pegasus", -1 } },
        { "Per", { "Perseus", -1 } },
        { "Phe", { "Phoenix", -1 } },
        { "Pic", { "Pictor", -1 } },
        { "Psc", { "Pisces", 11 } },
        { "PsA", { "Piscis Austrinus", -1 } },
        { "Pup", { "Puppis", -1 } },
        { "Pyx", { "Pyxis", -1 } },
        { "Ret", { "Reticulum", -1 } },
        { "Sge", { "Sagitta", -1 } },
        { "Sgr", { "Sagittarius", 8 } },
        { "Sco", { "Scorpius", 7 } },
        { "Scl", { "Sculptor", -1 } },
        { "Sct", { "Scutum", -1 } },
        { "Ser", { "Serpens", -1 } },
        { "Sex", { "Sextans", -1 } },
        { "Tau", { "Taurus", 1 } },
        { "Tel", { "Telescopium", -1 } },
        { "Tri", { "Triangulum", -1 } },
        { "TrA", { "Triangulum Australe", -1 } },
        { "Tuc", { "Tucana", -1 } },
        { "UMa", { "Ursa Major", -1 } },
        { "UMi", { "Ursa Minor", -1 } },
        { "Vel", { "Vela", -1 } },
        { "Vir", { "Virgo", 5 } },
        { "Vol", { "Volans", -1 } },
        { "Vul", { "Vulpecula", -1 } },
    };
    return table;
}
} // namespace

// Full name of an IAU 3-letter constellation abbreviation (e.g. "CMa" ->
// "Canis Major"), or the abbreviation itself if not recognized.
QString
constellationFullName(const QString& abbrev)
{
    auto it = constellationTable().find(abbrev);
    return it != constellationTable().end() ? QString(it->name) : abbrev;
}

// Almagest zodiac-sign glyph codepoint for a constellation abbreviation, or 0
// when the constellation isn't one of the 12 zodiacal ones.
int
constellationSignGlyph(const QString& abbrev)
{
    auto it = constellationTable().find(abbrev);
    if (it == constellationTable().end() || it->signIndex < 0) return 0;
    return Data::getSignGlyph(Ingresses_Start + it->signIndex);
}

// Bayer designation for a star, formatted "<Constellation>-<Greek letter>"
// (e.g. "Orion-η") using the actual Greek glyph rather than the Latin
// genitive form (which reads oddly to non-specialists, e.g. "eta Orionis").
// Returns empty if the raw SE nomenclature (Star::bayer, e.g. "alTau")
// doesn't match the plain "<2-letter Greek code><3-letter constellation>"
// pattern -- multi-star suffixes and Flamsteed numbers (e.g. "b01Cyg",
// "10Vul") are left alone rather than risk mislabeling them.
QString
bayerDesignation(const Star& s)
{
    if (s.bayer.length() != 5 || s.constellation.isEmpty()) return QString();
    if (!s.bayer.endsWith(s.constellation)) return QString();

    static const QHash<QString, QChar> letters {
        { "al", QChar(0x03B1) }, { "be", QChar(0x03B2) }, { "ga", QChar(0x03B3) },
        { "de", QChar(0x03B4) }, { "ep", QChar(0x03B5) }, { "ze", QChar(0x03B6) },
        { "et", QChar(0x03B7) }, { "th", QChar(0x03B8) }, { "io", QChar(0x03B9) },
        { "ka", QChar(0x03BA) }, { "la", QChar(0x03BB) }, { "mu", QChar(0x03BC) },
        { "nu", QChar(0x03BD) }, { "xi", QChar(0x03BE) }, { "pi", QChar(0x03C0) },
        { "rh", QChar(0x03C1) }, { "si", QChar(0x03C3) }, { "ta", QChar(0x03C4) },
        { "up", QChar(0x03C5) }, { "ph", QChar(0x03C6) }, { "kh", QChar(0x03C7) },
        { "ch", QChar(0x03C7) }, { "ps", QChar(0x03C8) }, { "om", QChar(0x03C9) },
    };
    auto it = letters.find(s.bayer.left(2));
    if (it == letters.end()) return QString();
    return constellationFullName(s.constellation) + "-" + *it;
}

// Wraps a fixed star's already-formatted name for HTML display: an inert
// "star:<tooltip text>" hover-tooltip anchor (handled by ReportBrowser in
// Plain, not real navigation) -- the Bayer designation when recognized (e.g.
// "Eta Orion"), else just the full constellation name -- plus, for the 12
// zodiacal constellations, a small Almagest sign glyph. Falls back to
// nameHtml unchanged when the star has no recorded constellation (e.g. the
// synthetic natal-position Star objects used for ex-precessed Directions
// rows).
QString
formatStarNameHtml(const Star& s, const QString& nameHtml)
{
    if (s.constellation.isEmpty()) return nameHtml;
    QString tip = bayerDesignation(s);
    if (tip.isEmpty()) tip = constellationFullName(s.constellation);
    // Qt's rich-text CSS doesn't honor "color: inherit" on <a> (it falls back
    // to its own default anchor color instead of the surrounding cell's), so
    // the star's normal (non-bold) text color has to be spelled out here.
    QString ret = "<a href=\"star:" + tip.toHtmlEscaped()
                  + "\" style=\"text-decoration:none; color: "
                  + ThemeManager::instance().getTextColor() + ";\">" + nameHtml
                  + "</a>";
    int glyph = constellationSignGlyph(s.constellation);
    if (glyph != 0) {
        ret += " <span style=\"font-family:'Almagest';\">" + QString(QChar(glyph))
               + "</span>";
    }
    return ret;
}

namespace
{

struct event {
    QDateTime   _dt;
    const Star* _star;
    unsigned    _pivot;
    bool        _isNatal = false; // natal ex-precessed row — right-justified italic

    static int       _maxWidth;
    static QDateTime _radix;
    static double    _radixRA; // Radix Local Sidereal Time in RA degrees
    static const PSSRContext* _pssrCtx; // direction context for return/ingress charts
    static bool      _isProgressed; // Skip date calculations for progressed charts
    static DirMethod _dirMethod;    // resolved quotidian method (DirNone → PD)

    event() : _star(NULL), _pivot(0) { }

    event(const QDateTime& dt, const Star* planet, unsigned pivot,
          bool isNatal = false) :
        _dt(dt),
        _star(planet),
        _pivot(pivot),
        _isNatal(isNatal)
    {
    }

    event(const event& other) :
        _dt(other._dt),
        _star(other._star),
        _pivot(other._pivot),
        _isNatal(other._isNatal)
    {
    }

    event& operator=(const event& other)
    {
        _dt     = other._dt;
        _star   = other._star;
        _pivot  = other._pivot;
        _isNatal = other._isNatal;
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
                SpeculumDisplayMode displayMode    = DisplayLocalTime,
                int                 pgroup         = -1) const
    {
        static QStringList AT { QObject::tr("Rise"),
                                QObject::tr("Set"),
                                QObject::tr("MC"),
                                QObject::tr("IC"),
                                "----" };

        QString       planetName = _star ? _star->name : QObject::tr("*Radix*");
        const Planet* planet     = dynamic_cast<const Planet*>(_star);
        // Genuine fixed star (not a Planet, not the radix placeholder, and not
        // one of the synthetic natal-position Star objects used for
        // ex-precessed rows, which carry no constellation): gloss it with a
        // constellation tooltip and, for zodiacal stars, a sign glyph.
        if (_star && !planet) {
            planetName = formatStarNameHtml(*_star, planetName);
        }

        QString borderStyle =
            isFirstInGroup ? " border-top: 1px solid #777;" : "";
        QString backgroundColor =
            planet ? " background-color: rgba(255,255,255,0.03);" : "";
        // data-pgroup marks which paran cluster this row belongs to, so the
        // Plain search filter can keep/highlight a whole cluster when any one
        // row in it matches, without having to re-derive the grouping itself.
        QString ret = (pgroup >= 0)
            ? QString("<tr data-pgroup=\"%1\">").arg(pgroup)
            : "<tr>";

        // Planet name cell gets bold styling, other cells do not
        QString nameCellStyle =
            "padding: " + padding + " 8px;" + borderStyle + backgroundColor;
        if (planet) {
            nameCellStyle += " font-weight: bold;";
        }
        // Natal ex-precessed rows: right-justified italic to distinguish from transit
        if (_isNatal) {
            nameCellStyle += " font-style: italic; text-align: right;";
        }

        // Data cells don't get bold, just border and background
        QString dataCellStyle =
            "padding: " + padding + " 8px;" + borderStyle + backgroundColor;
        if (_isNatal) {
            dataCellStyle += " font-style: italic;";
        }

        // Planet name with color emphasis for planets
        if (planet && !_isNatal) {
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

        // Angular date calculation (PD or PSSR) - all display modes
        if (_star && !_isProgressed) {
            // Get the star/planet's RA and the angle RA
            double planetRA = _star->equatorialPos.x();
            double angleRA = _star->angleTransitRA[_pivot];
            
            QString label = _star->name + " @ " + angleTransitName(_pivot);
            QDateTime angularDateGMT = calculateAngularDate(_radix, _dt, planetRA, angleRA, _pssrCtx, label, _radixRA);
            QString method = "PD"; // Default to Primary Directions
            QString dateFormat = "yyyy/MM/dd";
            if (_pssrCtx && _dirMethod != DirNone) {
                method = dirMethodLabel(_dirMethod);
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
DirMethod event::_dirMethod = DirNone;
} // namespace

QString
describeParans(const AstroFileList& scopes,
               bool                 showAll,
               bool                 showFixedStars,
               double               paranOrb,
               SpeculumDisplayMode  displayMode,
               bool                 showParanNatalRows,
               bool                 includeOutOfOrbNatalRows,
               AstroFile*           natalContext)
{
    // CONFIGURABLE: Cell padding for parans table - change this to adjust row
    // spacing Suggested values: "0px" (tight), "1px" (normal), "2px" (loose)
    const QString cellPadding = "0px";

    bool  showDates = scopes.count() == 1;
    auto  scope     = scopes.first()->horoscope();
    double tz        = scope.inputData.tz();

    // Check if this is a return/ingress chart and get its direction context
    event::_pssrCtx = nullptr;
    event::_isProgressed = false;
    event::_dirMethod = DirNone;
    AstroFile* file = scopes.first();

    // Check if it's a progressed chart based on the file TYPE, not just presence of base chart
    // Base chart is just metadata about natal relationship - doesn't mean it's progressed
    if (file && file->hasBaseChart()) {
        FileType ftype = file->getType();
        if (ftype == TypeDerivedProg || ftype == TypeDerivedSA || ftype == TypeDerivedPD) {
            event::_isProgressed = true;
        }
    }

    // Resolve the quotidian method for this chart (solar returns → PSSR/NeoPSSR/SQ/
    // NeoSQ; lunar returns & ingresses → SQ/NeoSQ; otherwise DirNone → plain PD).
    DirMethod method = resolveDirMethod(file);
    if (method != DirNone) {
        // (Re)build the cached context if missing or if the method changed.
        if (!file->hasPSSRContext() || file->pssrContext().method != method) {
            file->setPSSRContext(buildDirContext(file->horoscope(), method));
        }
        if (file->hasPSSRContext()) {
            event::_pssrCtx = &file->pssrContext();
            event::_dirMethod = method;
        }
    }

    // For paran charts the focused view shows exactly the cluster of
    // angle-transits anchored on the radix (the chart's GMT) — i.e. the same
    // group the full-listing path would render around that radix.  We collect
    // all candidate transits up-front and let the post-sort cluster walk
    // determine membership; no static time-proximity filter is needed.
    const bool isParanChart = file && file->getType() == TypeParan;
    const qint64 paranOrbSecs = qint64(paranOrb * 240); // 1° = 240 sidereal seconds ≈ clock seconds

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
            if (!dt.isValid()) { u++; continue; }
            if (p.name.length() > maxWidth) maxWidth = p.name.length();
            events << event(dt, p, u++);
        }
    }

    if (showFixedStars) {
        for (const Star& s : std::as_const(scope.stars)) {
            unsigned u = 0;
            for (const QDateTime& dt : s.angleTransit) {
                if (!dt.isValid()) { u++; continue; }
                if (s.name.length() > maxWidth) maxWidth = s.name.length();
                events << event(dt, s, u++);
            }
        }
    }

    // Natal ex-precessed rows: shown in focused Par=N panels AND in the full
    // listing when showParanNatalRows is set and a natal context is available.
    // Focused mode filters by the specific paran event time; full-listing mode
    // filters each natal angle transit by proximity to any return-planet transit.
    //
    // For a transit-only Par chart presented as the second wheel of a biwheel
    // whose first wheel is a natal/Event chart, also include the radix bodies
    // that happen to be in the focal paran — even though they aren't part of the
    // (transit-only) event — when showParanNatalRows is set. The focused-cluster
    // filter below then keeps only those actually in the radix cluster.
    const bool natalCtxIsRadix =
        natalContext
        && (natalContext->getType() == TypeMale
            || natalContext->getType() == TypeFemale
            || natalContext->getType() == TypeEvent
            || natalContext->getType() == TypeComposite);
    QVector<Star> natalStarStorage;
    const bool runNatalRows =
        natalContext
        && (isParanChart
                ? (file->getOriginEventType() == etcParanatellontaToNatal
                   || (file->getOriginEventType() == etcParanatellonta
                       && showParanNatalRows && natalCtxIsRadix))
                : showParanNatalRows);
    if (runNatalRows) {
        // Full listing: collect return-planet event times for orb-filtering.
        QVector<QDateTime> returnTimes;
        if (!isParanChart) {
            for (const auto& ev : events)
                if (ev._star && dynamic_cast<const Planet*>(ev._star))
                    returnTimes.append(ev._dt);
        }

        natalStarStorage.reserve(natalContext->horoscope().planets.size());
        double jdNatal = getJulianDate(natalContext->getGMT());
        double jdParanRaw = getJulianDate(file->getGMT());
        // Focused Par=N: truncate to midnight UTC of the chart day so
        // computeNatalParanTransits searches [midnight, midnight+1), matching
        // the same daily grid findParans() used to find the event — otherwise
        // the display could pick a different diurnal occurrence than the one
        // actually reported. JD convention: midnight UTC = floor(jd+0.5)-0.5.
        //
        // Full listing (non-paran chart): transiting-body angle transits
        // (calculatePlanet's default/zodiacal mode) are computed via
        // jd0 + swe_difdeg2n(...)/360*siderealDay, i.e. a window centered on
        // the radix moment (±12h). A midnight-anchored natal window is offset
        // from that by up to half a day, which skews natal ex-precessed rows
        // to one side of the local-time axis — starving one end of the table
        // and stacking the other. Center the window on the radix here too so
        // natal rows distribute the same way transiting rows already do.
        double jdParan = isParanChart
            ? (std::floor(jdParanRaw + 0.5) - 0.5)
            : (jdParanRaw - 0.5);
        double lat = scope.inputData.location().y();
        double lon = scope.inputData.location().x();

        for (const Planet& np : natalContext->horoscope().planets) {
            PlanetId pid = np.id;

            double tropRA, tropDec;
            if (natalContext->getType() == TypeComposite) {
                // Synthesized positions: no real natal sky to consult — use
                // the midpointed positions from the composite horoscope.
                if (pid <= Planet_None || pid >= Angles_Start) continue;
                if (!horoscopeTropicalEquatorialPos(
                        np, natalContext->horoscope(), tropRA, tropDec))
                    continue;
            } else if (!natalTropicalEquatorialPos(pid, jdNatal, tropRA, tropDec))
                continue;

            QDateTime angleTransit[4];
            double    angleTransitRA[4];
            computeNatalParanTransits(
                tropRA, tropDec,
                jdNatal, jdParan, lat, lon,
                angleTransit, angleTransitRA,
                /*jdAnchor=*/jdParanRaw);

            Star ns;
            ns.name = np.name;
            // computeNatalParanTransits reports RAMC at each transit time, so
            // sidereal-time/RA display lines up with other entries in the same
            // paran group (all share the same RAMC within 1°).
            for (int i = 0; i < 4; ++i) {
                if (angleTransit[i].isValid())
                    ns.angleTransitRA[i] = angleTransitRA[i];
            }
            natalStarStorage.append(ns);
            const Star* starPtr = &natalStarStorage.last();

            unsigned u = 0;
            for (int m = 0; m < 4; ++m) {
                if (!angleTransit[m].isValid()) { u++; continue; }
                // Focused paran view: collect all valid natal angle-transits;
                // the post-sort cluster walk decides which ones belong.
                // Full listing: filter by proximity to any return-planet transit,
                // unless includeOutOfOrbNatalRows requests every natal
                // ex-precessed body regardless of whether it happens to paran
                // with a transiting planet that day (e.g. so the Directions/PD
                // arc-dating always includes every natal point).
                bool pass;
                if (isParanChart || includeOutOfOrbNatalRows) {
                    pass = true;
                } else {
                    pass = std::any_of(
                        returnTimes.constBegin(), returnTimes.constEnd(),
                        [&](const QDateTime& rt) {
                            return qAbs(rt.secsTo(angleTransit[m])) <= paranOrbSecs;
                        });
                }
                if (!pass) { u++; continue; }
                if (starPtr->name.length() > maxWidth)
                    maxWidth = starPtr->name.length();
                events << event(angleTransit[m], starPtr, u++, /*isNatal=*/true);
            }
        }
    }

    std::sort(events.begin(), events.end());

    // Focused paran chart: keep only the cluster anchored on the *Radix* event.
    // Radix, transit planets, AND natal ex-precessed bodies all act as anchors
    // (a fixed star that paranatellons a natal body is a genuine paran), so the
    // chain extends through any of them; only fixed stars ride along without
    // extending it. Every row in the resulting range is a cluster member.
    if (isParanChart) {
        const qint64 clusterOrbSecs = qint64(paranOrb * 240);

        int radixIdx = -1;
        for (int i = 0; i < events.size(); ++i) {
            if (!events[i]._star) { radixIdx = i; break; }
        }
        if (radixIdx >= 0) {
            QVector<ParanClusterCandidate> cands;
            cands.reserve(events.size());
            for (const event& e : std::as_const(events))
                cands.append({ e._dt,
                               !e._star
                                   || dynamic_cast<const Planet*>(e._star)
                                          != nullptr
                                   || e._isNatal });

            const auto range =
                radixParanClusterRange(cands, radixIdx, clusterOrbSecs);

            QVector<event> pruned;
            pruned.reserve(range.second - range.first + 1);
            for (int i = range.first; i <= range.second; ++i)
                pruned.append(events[i]);
            events = std::move(pruned);
        }
    }

    // Note: the section header (e.g. "Directions - Chart #2: …") is supplied
    // by the caller so it can include chart context; we emit only the table.
    QString ret = "<table style='border-collapse: collapse; font-family: monospace;'>";
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

    // pgroup/groupOpen assign each rendered row the id of the paran cluster it
    // belongs to (see event::fmt's data-pgroup). A new id is cut in whenever
    // the existing grouping logic below would otherwise draw a fresh border
    // (or on the very first row), so the numbering tracks the visual clusters
    // exactly without duplicating their orb/adjacency logic.
    int  pgroup     = -1;
    bool groupOpen  = false;
    auto pgroupFor  = [&](bool newGroup) {
        if (newGroup || !groupOpen) {
            ++pgroup;
            groupOpen = true;
        }
        return pgroup;
    };

    if (isParanChart) {
        // events is already pruned to the radix-anchored cluster, so every row
        // is a member — render them all in time order. No barrier grouping:
        // that would drop legitimate fringe stars paranatellonting a natal or
        // transit anchor on the cluster's edge.
        const int gid = pgroupFor(true);
        for (auto it = events.constBegin(); it != events.constEnd(); ++it)
            ret += it->fmt(tz, cellPadding, false, displayMode, gid);
    } else {
    double orb        = paranOrb * 240;
    bool   anyPrinted = false;

    auto lastPrinted = events.constEnd();
    for (auto it = events.constBegin();
         it != events.constEnd();) // Note: no ++it here, we manage it manually
    {
        if (showAll) {
            ret += it->fmt(tz, cellPadding, false, displayMode, pgroupFor(true));
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
                ret += bit->fmt(tz, cellPadding, addBorder, displayMode,
                                pgroupFor(addBorder));
                lastPrinted = bit;
                ++bit;
                anyPrinted         = true;
                isFirstInThisGroup = false;
            }
            // Move iterator to the end of this group
            it = nit;
        } else if (it->_isNatal) {
            // Natal ex-precessed entries already passed the paranTime filter;
            // render them directly. Only add a group separator if the previous
            // printed entry is outside orb (i.e., this is a new cluster).
            bool addBorder = anyPrinted
                             && (lastPrinted == events.constEnd()
                                 || qAbs(lastPrinted->_dt.secsTo(it->_dt)) > orb);
            ret += it->fmt(tz, cellPadding, addBorder, displayMode,
                           pgroupFor(addBorder));
            lastPrinted = it;
            anyPrinted  = true;
            ++it;
        } else {
            ++it; // Skip non-planet, non-radix events when not showing all
        }
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
            ret += "<td style='padding: 0px 8px;'>" + formatStarNameHtml(s, s.name)
                   + "</td>";
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

namespace
{

static QString
angleAbbrev(int angle)
{
    switch (angle) {
    case 0: return QObject::tr("Asc");
    case 1: return QObject::tr("Dsc");
    case 2: return QObject::tr("MC");
    case 3: return QObject::tr("IC");
    }
    return QString();
}

// "HH:MM:SS"-ish for a clock-second delta — terse, dimmed when out of orb.
static QString
formatClockDelta(qint64 secs)
{
    const qint64 abss = std::abs(secs);
    const int    m    = int(abss / 60);
    const int    s    = int(abss % 60);
    return QString("%1m %2s")
        .arg(m)
        .arg(s, 2, 10, QChar('0'));
}

} // namespace

QString
describeParanLatitudes(const Horoscope&    natal,
                       double              paranOrbDeg,
                       double              cityLatTolDeg,
                       int                 maxCitiesPerRow,
                       bool                showAbsent,
                       unsigned            cityPopMask,
                       unsigned            cityContinentMask,
                       SpeculumDisplayMode displayMode,
                       const Horoscope*    transitCtx,
                       bool                natalSynthesized)
{
    CityFilter cityFilter;
    cityFilter.popTiers   = cityPopMask;
    cityFilter.continents = cityContinentMask;

    QVector<ParanLatitudeRow> rows;
    enumerateNatalParanLatitudes(natal, paranOrbDeg, rows, transitCtx,
                                 natalSynthesized);

    // Transit×natal rows use the same full paran orb as transit×transit rows so
    // the latitude table's membership agrees with the focal paran cluster in the
    // Directions table (findParans clusters with the full paranOrb). The
    // present-test was already applied against paranOrbDeg by the enumerator.

    if (!showAbsent) {
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [](const ParanLatitudeRow& r) { return !r.present; }),
                   rows.end());
    }

    // Sort strictly North → South (latitude descending).
    std::sort(rows.begin(), rows.end(),
              [](const ParanLatitudeRow& a, const ParanLatitudeRow& b) {
                  // Latitude-dependent (horizon) rows first, N→S; the
                  // latitude-independent meridional rows collect at the bottom,
                  // ordered by tightness of their (constant) gap.
                  if (a.meridional != b.meridional) return b.meridional;
                  if (a.meridional) return a.natalOrbDeg < b.natalOrbDeg;
                  return a.latitude > b.latitude;
              });

    // Note: the section header (e.g. "Parans - Chart #2: …") is supplied by the
    // caller so it can include chart context; we emit only the description+table.
    QString ret = "<p>"
           + QObject::tr("Latitudes at which each natal-body pair forms a paran. "
                         "The smaller second line gives the latitude band over "
                         "which the paran stays within orb. MC/IC-only pairs are "
                         "latitude-independent and shown as “All”.")
           + "</p>";

    if (rows.isEmpty()) {
        ret += "<p><em>" + QObject::tr("No parans found.") + "</em></p>";
        return ret;
    }

    ret += "<table style='border-collapse: collapse; font-family: monospace;'>";
    ret += "<tr style='background-color: rgba(255,255,255,0.1);'>";
    ret += "<th style='padding: 4px 8px; text-align: left;'>"
           + QObject::tr("Paran") + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: center;'>"
           + QObject::tr("Latitude") + "</th>";
    QString orbHeader;
    if (displayMode == DisplaySiderealTime)
        orbHeader = QObject::tr("Natal Δ (ST)");
    else if (displayMode == DisplayRightAscension)
        orbHeader = QObject::tr("Natal Δ (RA)");
    else
        orbHeader = QObject::tr("Natal Δ (LT)");
    ret += "<th style='padding: 4px 8px; text-align: right;'>" + orbHeader + "</th>";
    ret += "<th style='padding: 4px 8px; text-align: left;'>"
           + QObject::tr("Cities (±%1°)").arg(QString::number(cityLatTolDeg, 'f', 2)) + "</th>";
    ret += "</tr>";

    const QString headingColor = ThemeManager::instance().getHeadingColor();

    const bool labelEpoch = (transitCtx != nullptr);
    for (const ParanLatitudeRow& r : std::as_const(rows)) {
        QString nameA = getPlanet(r.a).name;
        QString nameB = getPlanet(r.b).name;
        if (labelEpoch) {
            nameA += r.aIsNatal ? QStringLiteral("-r") : QStringLiteral("-t");
            nameB += r.bIsNatal ? QStringLiteral("-r") : QStringLiteral("-t");
        }
        const QString paranText = QString("%1 %2  +  %3 %4")
                                      .arg(nameA, angleAbbrev(r.angleA),
                                           nameB, angleAbbrev(r.angleB));

        // Latitude cell. Meridional pairs are latitude-independent ("All").
        // Horizon pairs show the exact-paran latitude, with the in-orb band on
        // a smaller second line.
        // Degrees+minutes, e.g. 44°03′N -> "44N03".
        auto fmtLatDM = [](double latDeg) {
            const QString hemi = latDeg >= 0 ? QObject::tr("N") : QObject::tr("S");
            int deg = int(std::abs(latDeg));
            int min = int(std::round((std::abs(latDeg) - deg) * 60.0));
            if (min >= 60) { min -= 60; ++deg; }
            return QString("%1%2%3").arg(deg).arg(hemi).arg(min, 2, 10, QChar('0'));
        };
        // Whole-degree, e.g. "32N".
        auto fmtLatDeg = [](double latDeg) {
            const QString hemi = latDeg >= 0 ? QObject::tr("N") : QObject::tr("S");
            return QString("%1%2").arg(int(std::round(std::abs(latDeg)))).arg(hemi);
        };
        QString latText;
        if (r.meridional) {
            latText = QObject::tr("All");
        } else {
            latText = fmtLatDM(r.latitude);
            if (r.hasRange) {
                // Band on a smaller line, south→north, rounded to whole degrees.
                // Collapse to a single value when both bounds round the same.
                const int loR = int(std::round(r.latSouth));
                const int hiR = int(std::round(r.latNorth));
                const QString band = (loR == hiR)
                    ? fmtLatDeg(r.latSouth)
                    : fmtLatDeg(r.latSouth) + "–" + fmtLatDeg(r.latNorth);
                latText += "<br><span style='font-size: 0.85em; color: #888;'>"
                           + band + "</span>";
            }
        }

        // Natal-orb cell.
        QString orbText;
        QString orbStyle = "padding: 2px 8px; text-align: right;";
        if (!r.hasNatalOrb) {
            orbText = QStringLiteral("—");
            orbStyle += " color: #888;";
        } else if (displayMode == DisplayRightAscension) {
            orbText = QString("%1°").arg(r.natalOrbDeg, 0, 'f', 3);
            if (!r.present) orbStyle += " color: #888;";
        } else if (displayMode == DisplaySiderealTime) {
            // 1° sidereal ≈ 4 minutes of sidereal time.
            const double siderealSec = r.natalOrbDeg * 240.0;
            orbText = formatClockDelta(static_cast<qint64>(std::round(siderealSec)));
            if (!r.present) orbStyle += " color: #888;";
        } else {
            // Clock-time delta (LT mode): use the actual angle-transit clock difference.
            orbText = formatClockDelta(r.natalOrbSec);
            if (!r.present) orbStyle += " color: #888;";
        }

        // Cities list. Meridional parans are latitude-independent, so a city
        // list is meaningless. Horizon pairs with a band draw cities from the
        // whole in-orb span (center ± half-width); otherwise fall back to the
        // fixed display tolerance around the exact latitude.
        QString citiesText;
        double cityCenter = r.latitude;
        double cityTol    = cityLatTolDeg;
        if (r.hasRange) {
            cityCenter = 0.5 * (r.latNorth + r.latSouth);
            cityTol    = 0.5 * std::abs(r.latNorth - r.latSouth);
        }
        const QVector<CityRec> cities = r.meridional
            ? QVector<CityRec>{}
            : citiesNearLatitude(cityCenter, cityTol, maxCitiesPerRow + 1, cityFilter);
        if (r.meridional) {
            citiesText = QObject::tr("N/A");
        } else if (cities.isEmpty()) {
            citiesText = QStringLiteral("—");
        } else {
            QStringList parts;
            parts.reserve(qMin(cities.size(), maxCitiesPerRow));
            for (int i = 0; i < cities.size() && i < maxCitiesPerRow; ++i) {
                parts << QString("%1, %2").arg(cities[i].name, cities[i].countryCode);
            }
            if (cities.size() > maxCitiesPerRow) parts << QStringLiteral("…");
            citiesText = parts.join(QStringLiteral("; "));
        }

        QString rowStyle;
        if (r.present) {
            const bool isTxR = !r.aIsNatal && r.bIsNatal;
            rowStyle = isTxR
                ? " style='background-color: rgba(230,150,60,0.12);'"  // orange-tint for transit×natal
                : " style='background-color: rgba(120,200,140,0.10);'"; // green for self-self
        }
        ret += "<tr" + rowStyle + ">";
        ret += "<td style='padding: 2px 8px; font-weight: bold; color: " + headingColor + ";'>"
               + paranText + "</td>";
        ret += "<td style='padding: 2px 8px; text-align: center;'>" + latText + "</td>";
        ret += "<td style='" + orbStyle + "'>" + orbText + "</td>";
        ret += "<td style='padding: 2px 8px;'>" + citiesText + "</td>";
        ret += "</tr>";
    }

    ret += "</table>";
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
