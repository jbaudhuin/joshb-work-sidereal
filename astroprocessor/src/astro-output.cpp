#include <QObject>
#include <QStringList>
#include <QRegularExpression>
#include <stdio.h>
#include <math.h>
#include "astro-calc.h"
#include "astro-output.h"
//#include <QDebug>

namespace A {

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0)) && defined(_MSC_VER)
#define A_DECODE(s) QString::fromLocal8Bit(s)
#else
#define A_DECODE(s) QString::fromUtf8(s)
#endif

QString romanNum(int num)
{
    static const char* h[] = { "I", "II", "III", "IV",
                               "V", "VI", "VII", "VIII",
                               "IX", "X", "XI",  "XII" };

    if (num >= 1 && num <= 12) return h[--num];
    return "0";
}

QString houseTag(int num)
{
    static const char* h[] = { "Asc", "II", "III", "IV",
                               "V", "VI", "VII", "VIII",
                               "IX", "X", "XI",  "XII" };

    if (num >= 1 && num <= 12) return h[--num];
    return "0";
}

QString houseNum(const Planet& planet)
{
    return romanNum(planet.house);
}

QString getPositionName(PlanetPosition p)
{
    switch (p) {
    case Position_Normal: return QObject::tr("");
    case Position_Exaltation: return QObject::tr("Exaltation");
    case Position_Dwelling: return QObject::tr("House");
    case Position_Downfall: return QObject::tr("Fall");
    case Position_Exile: return QObject::tr("Detriment");
    }

    return "";
}

QString degreeToString(float deg, AnglePrecision precision)
{
    int d = 0, m = 0, s = 0, polarity = 0;

    if (deg < 0) {
        polarity = -1;
        deg = -deg;
    }

    d = (int)(deg);
    m = (int)(60.0*(deg - d));

    QString ret;
    if (precision == HighPrecision) {
        s = (int)((deg - (d + m / 60.0)) * 3600.0);
        ret = QString(A_DECODE("%1%2°%3'%4\""))
              .arg((polarity < 0) ? "-" : "")
              .arg(d)
              .arg(m,2,10,QChar('0'))
              .arg(s,2,10,QChar('0'));
    } else if (precision == LowPrecision) {
        ret = QString(A_DECODE("%1%2°"))
              .arg((polarity < 0) ? "-" : "")
              .arg(d + int(m >= 30));
    } else {
        if (m)
            ret = QString(A_DECODE("%1%2°%3'"))
                  .arg((polarity < 0) ? "-" : "")
                  .arg(d).arg(m,2,10,QChar('0'));
        else
            ret = QString(A_DECODE("%1%2°"))
                  .arg((polarity < 0) ? "-" : "")
                  .arg(d);
    }
    return ret;
}

QString
zodiacPosition(float deg,
               const Zodiac& zodiac,
               AnglePrecision precision)
{
    const ZodiacSign& sign = getSign(deg, zodiac);
    int ang = floor(deg) - sign.startAngle;
    if (ang < 0) ang += 360;

    if (precision) {
        QString str = degreeToString(deg, precision);
        str.remove(0, str.indexOf(A_DECODE("°")));
        return QString("%1%2 %3").arg(ang).arg(str).arg(sign.tag);
    } else {
        int m = (int)(60.0*(deg - (int)deg));
        return QString("%1 %2 %3%4").arg(ang).arg(sign.tag)
                .arg(m >= 10 ? "" : "0").arg(m);
    }
}

QString zodiacPosition(const Star& star, const Zodiac& zodiac, AnglePrecision precision)
{
    float deg;
    switch (aspectMode) {
    case amcEquatorial: deg = star.equatorialPos.x(); break;
    case amcPrimeVertical: deg = star.pvPos; break;
    default:
    case amcEcliptic: deg = star.eclipticPos.x(); break;
    }

    return zodiacPosition(deg, zodiac, precision);
}

void sortPlanets(PlanetList &planets, PlanetsOrder order)
{
    if (!planets.count()) return;
    if (order == Order_NoOrder) return;

    for (int i = 0; i < planets.count(); i++) {
        for (int j = i + 1; j < planets.count(); j++) {
            if (order == Order_House) {
                if (planets[i].house > planets[j].house || (planets[i].house == planets[j].house &&
                                                            isEarlier(planets[j], planets[i]))) {
                    Planet t = planets[i];
                    planets[i] = planets[j];
                    planets[j] = t;
                }
            } else if (order == Order_Power) {
                if ((planets[i].power.dignity > 0 && planets[j].power.dignity > 0 &&
                     planets[i].power.deficient + planets[i].power.dignity <
                     planets[j].power.deficient + planets[j].power.dignity)
                    ||
                    (planets[i].power.dignity == 0 && planets[i].power.dignity > 0)) {
                    Planet t = planets[i];
                    planets[i] = planets[j];
                    planets[j] = t;
                }
            } else if (order == Order_Element) {
                int sign1 = planets[i].sign->id;
                int sign2 = planets[j].sign->id;
                if ((sign1 - 1) % 4 > (sign2 - 1) % 4) {
                    Planet t = planets[i];
                    planets[i] = planets[j];
                    planets[j] = t;
                }
            }
        }
    }
}

QString
describeInput(const InputData& data)
{
    QString ret;

    auto date = QLocale().toString(data.GMT.date(),QLocale::LongFormat);
    auto time = QLocale().toString(data.GMT.time(),QLocale::LongFormat);
    QString dayOfWeek = data.GMT.date().toString("ddd");

    ret += QObject::tr("Date: %1, %2 %3 GMT\n").arg(dayOfWeek).arg(date).arg(time);
    ret += "\n";
    ret += QObject::tr("Location: %1N %2E").arg(degreeToString(data.location.y(), HighPrecision))
        .arg(degreeToString(data.location.x(), HighPrecision));
    return ret;
}

QString
describeHouses(const Houses& houses,
               const Zodiac& zodiac)
{
    QString ret;
    ret += QObject::tr("Houses (%1)\n").arg(houses.system->name);

    for (int i = 0; i < 12; i++) {
        ret += houseTag(i + 1).rightJustified(4, ' ') + " -" +
            zodiacPosition(houses.cusp[i], zodiac, HighPrecision).rightJustified(14, ' ');

        if (i < 11) ret += '\n';
    }

    return ret;
}

QString     describeAspect(const Aspect &aspect, bool monospace)
{
    QString ret = aspect.d->name;
    if (monospace) ret = ret.leftJustified(14, ' ', true);
    if (!ret.isEmpty()) ret += " ";

    ret += aspect.planet1->name + "-" + aspect.planet2->name;
    if (monospace) ret = ret.leftJustified(35, ' ', true);

    QString angleStr = degreeToString(aspect.angle);

    if (aspect.applying) ret += " >" + angleStr + "<";
    else               ret += " <" + angleStr + ">";

    return ret;
}

QString     describeAspectFull(const Aspect &asp, QString tag1, QString tag2)
{
 //if (!tag1.isEmpty()) tag1 = " (" + tag1 + ")";
 //if (!tag2.isEmpty()) tag1 = " (" + tag2 + ")";

    return QString("%1 (%2) %3%4-%5%6 [%7]\n").arg(asp.d->name)
        .arg(degreeToString(asp.angle))
        .arg(asp.planet1->name)
        .arg(tag1)
        .arg(asp.planet2->name)
        .arg(tag2)
        .arg(degreeToString(asp.d->angle)) +
        QObject::tr("Orb: %1 (max: %2)\n").arg(degreeToString(asp.orb))
        .arg(degreeToString(asp.d->orb())) +
        (asp.applying ? QObject::tr("Applying") : QObject::tr("Separating"));
}

QString     describePlanet(const Planet& planet, const Zodiac& zodiac)
{
    QString ret;

    QString name = planet.name.leftJustified(14, ' ', true);
    QString longitude = zodiacPosition(planet, zodiac, HighPrecision)
        .rightJustified(14, ' ', true);

    ret = name + " " + longitude + " " + houseNum(planet).leftJustified(4, ' ');

    if (planet.defaultEclipticSpeed.x() != 0) {
        float speed = planet.eclipticSpeed.x() / planet.defaultEclipticSpeed.x();
        ret += QString(" (%1%)").arg((int)(speed * 100)).leftJustified(8, ' ', true);
    } else
        ret += QString().leftJustified(8, ' ');

    if (planet.power.dignity != 0 || planet.power.deficient != 0) {
        QString plus = planet.power.dignity != 0 ? "+" : "";
        QString dignity = QString(plus + "%1").arg(planet.power.dignity).rightJustified(3, ' ');
        QString deficient = QString("%1").arg(planet.power.deficient).leftJustified(3, ' ');

        ret += " " + dignity + "|" + deficient;
    } else
        ret += QString().leftJustified(8, ' ');

    if (planet.houseRuler > 0)
        ret += QObject::tr(" ruler of %1").arg(romanNum(planet.houseRuler)).leftJustified(15, ' ');

    if (planet.position != Position_Normal)
        ret += getPositionName(planet.position);

    return ret;
}

QString     describePlanetCoord(const Planet& planet)
{
    QString ret;

    ret += QObject::tr("Longitude: %1\n").arg(degreeToString(planet.eclipticPos.x(), HighPrecision));
    ret += QObject::tr("Latitude: %1\n").arg(degreeToString(planet.eclipticPos.y(), HighPrecision));
    ret += QObject::tr("Rectascension: %1\n").arg(degreeToString(planet.equatorialPos.x(), HighPrecision));
    ret += QObject::tr("Prime Vertical: %1\n").arg(degreeToString(planet.pvPos, HighPrecision));
    ret += QObject::tr("Declination: %1\n").arg(degreeToString(planet.equatorialPos.y(), HighPrecision));
    ret += QObject::tr("Distance: %1a.u.\n").arg(planet.distance);
    ret += QObject::tr("Azimuth: %1\n").arg(degreeToString(planet.horizontalPos.x(), HighPrecision));
    ret += QObject::tr("Height: %1\n").arg(degreeToString(planet.horizontalPos.y(), HighPrecision));

    if (planet.isReal)
        ret += QObject::tr("Speed: %1% (%2 per day)\n")
        .arg((int)(planet.eclipticSpeed.x() / planet.defaultEclipticSpeed.x() * 100))
        .arg(degreeToString(planet.eclipticSpeed.x(), HighPrecision));

    return ret;
}

QString
describePlanetCoordInHtml(const Planet& planet)
{
    QString ret = describePlanetCoord(planet);
    ret.replace(QRegularExpression(QString::fromLocal8Bit("(: [!-° ]+)")),
                "<font color='#e9e9e4'>\\1</font>");  // replaces values
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
    case Position_Dwelling:   ret << QObject::tr("+5: Planet is in its own sign"); break;
    case Position_Exaltation: ret << QObject::tr("+5: Planet is in exaltation"); break;
    case Position_Exile:      ret << QObject::tr("-5: Planet is in detriment"); break;
    case Position_Downfall:   ret << QObject::tr("-4: Planet is in its fall"); break;
    case Position_Normal:     peregrine = true;
    default: break;
    }

    PlanetId pl = receptionWith(planet, scope);
    if (pl != Planet_None)
        ret << QObject::tr("+5: Planet is in mutual reception with %1").arg(getPlanetName(pl));
    else if (peregrine)
        ret << QObject::tr("-5: Planet is peregrine (doesn't have an essential dignity)");


    int h = planet.house;
    QString p;
    switch (h) {
    case 1: case 10:         p = "+5"; break;
    case 4: case 7: case 11: p = "+4"; break;
    case 2: case 5:          p = "+3"; break;
    case 9:                  p = "+2"; break;
    case 3:                  p = "+1"; break;
    case 12:                 p = "-5"; break;
    case 8: case 6:          p = "-2"; break;
    default: break;
    }
    if (!p.isEmpty()) ret << QObject::tr("%1: Planet is placed in %2 house").arg(p).arg(romanNum(h));


    if (planet.eclipticSpeed.x() > 0 &&
        planet.id != Planet_Sun &&
        planet.id != Planet_Moon)
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
            ret << QObject::tr("+2: %1 rises earlier than the Sun (oriental)").arg(planet.name);
        else
            ret << QObject::tr("-2: %1 rises later than the Sun (occidental)").arg(planet.name);
        break;

    case Planet_Mercury:
    case Planet_Venus:

        if (!isEarlier(planet, scope.sun))
            ret << QObject::tr("+2: %1 rises later than the Sun (occidental)").arg(planet.name);
        else
            ret << QObject::tr("-2: %1 rises earlier than the Sun (occidental)").arg(planet.name);
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
            ret << QObject::tr("+5: Planet is neither combust nor under the beams");
        else if (angle(planet, scope.sun) < 0.4)
            ret << QObject::tr("+5: Planet is cazimi");
        else
            ret << QObject::tr("-4: Planet is either combust or under the beams");
    }


    switch (aspect(planet, scope.jupiter, topAspectSet())) {
    case Aspect_Conjunction: ret << QObject::tr("+5: Planet is in partile conjunction with Jupiter"); break;
    case Aspect_Trine:       ret << QObject::tr("+4: Planet is in partile trine with Jupiter"); break;
    case Aspect_Sextile:     ret << QObject::tr("+3: Planet is in partile sextile with Jupiter"); break;
    default: break;
    }

    switch (aspect(planet, scope.venus, topAspectSet())) {
    case Aspect_Conjunction: ret << QObject::tr("+5: Planet is in partile conjunction with Venus"); break;
    case Aspect_Trine:       ret << QObject::tr("+4: Planet is in partile trine with Venus"); break;
    case Aspect_Sextile:     ret << QObject::tr("+3: Planet is in partile sextile with Venus"); break;
    default: break;
    }

    switch (aspect(planet, scope.northNode, topAspectSet())) {
    case Aspect_Conjunction: ret << QObject::tr("+4: Planet is in partile conjunction with North Node"); break;
    /*case Aspect_Trine:       ret << QObject::tr("+4: Planet is in partile trine with North Node"); break;
    case Aspect_Sextile:     ret << QObject::tr("+4: Planet is in partile sextile with North Node"); break;
    case Aspect_Opposition:  ret << QObject::tr("-4: Planet is in partile opposition with North Node"); break;*/
    default: break;
    }

    switch (aspect(planet, scope.mars, topAspectSet())) {
    case Aspect_Conjunction: ret << QObject::tr("-5: Planet is in partile conjunction with Mars"); break;
    case Aspect_Opposition:  ret << QObject::tr("-4: Planet is in partile opposition with Mars"); break;
    case Aspect_Quadrature:  ret << QObject::tr("-3: Planet is in partile quadrature with Mars"); break;
    default: break;
    }

    switch (aspect(planet, scope.saturn, topAspectSet())) {
    case Aspect_Conjunction: ret << QObject::tr("-5: Planet is in partile conjunction with Saturn"); break;
    case Aspect_Opposition:  ret << QObject::tr("-4: Planet is in partile opposition with Saturn"); break;
    case Aspect_Quadrature:  ret << QObject::tr("-3: Planet is in partile quadrature with Saturn"); break;
    default: break;
    }


    if (aspect(planet, scope.stars["Regulus"], tightConjunction()) == Aspect_Conjunction)
        ret << QObject::tr("+6: Planet is in conjunction with Regulus");

    if (aspect(planet, scope.stars["Spica"], tightConjunction()) == Aspect_Conjunction)
        ret << QObject::tr("+5: Planet is in conjunction with Spica");

    if (aspect(planet, scope.stars["Algol"], tightConjunction()) == Aspect_Conjunction)
        ret << QObject::tr("-5: Planet is in conjunction with Algol");


      // sort values from biggest to smallest

    for (int i = 0; i < ret.count(); i++) {
        for (int j = i + 1; j < ret.count(); j++) {
            int val1 = ret[i].at(1).digitValue();
            int val2 = ret[j].at(1).digitValue();

            if (ret[i][0] == '-') val1 = -val1;
            if (ret[j][0] == '-') val2 = -val2;

            if (val1 < val2) {
                QString t = ret[i];
                ret[i] = ret[j];
                ret[j] = t;
            }
        }
    }


    return ret.join(";\n") + '.';
}

QString     describePowerInHtml(const Planet& planet, const Horoscope& scope)
{
    QString ret = describePower(planet, scope);
    if (ret.isEmpty()) return ret;

    ret.replace("\n", "</p><p>");
    ret.replace(QRegularExpression("(-\\d:)"), "<font color='#dfb096'><b>\\1</b></font>");  // replaces negative values
    ret.replace(QRegularExpression("(\\+\\d:)"), "<font color='#71aeec'><b>\\1</b></font>");  // replaces positive values

    return "<p>" + ret + "</p>";
}

QString
_formatTime(const QDateTime& dt, short tz)
{
    if (!dt.isValid()) {
        return "    --    ";
    }
    QString dow(QObject::tr("MtWTFsS"));
    QDateTime ldt = dt.addSecs(tz * 3600);
    return QString("%2 %1")
        .arg(ldt.time().toString())
        .arg(dow[ldt.date().dayOfWeek() - 1]);
}

namespace {
struct event {
    QDateTime _dt;
    const Star* _star;
    unsigned _pivot;

    static int _maxWidth;
    static QDateTime _radix;

    event() : _star(NULL), _pivot(0) {}

    event(const QDateTime& dt,
          const Star* planet,
          unsigned pivot) :
        _dt(dt), _star(planet), _pivot(pivot)
    {
    }

    event(const event& other) :
        _dt(other._dt), _star(other._star), _pivot(other._pivot)
    {
    }

    event& operator=(const event& other)
    {
        _dt = other._dt;
        _star = other._star;
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
        if (_dt > other._dt) return false;
        if (_dt < other._dt) return true;
        if (_pivot >= other._pivot) return false;
        return true;
    }

    QString fmt(short tz) const
    {
        QString ret = QString("%1  %2  %3")
            .arg(_star ? _star->name : QObject::tr("*Radix*"), -_maxWidth)
            .arg((QStringList()
                  << QObject::tr("Rise")
                  << QObject::tr("Set")
                  << QObject::tr("MC")
                  << QObject::tr("IC")
                  << "----").at(_pivot), -6)
            .arg(_formatTime(_dt, tz), -9);
        if (_star) { // i.e., not radix
            double dist = qAbs(_radix.secsTo(_dt));
            int dayDiff = dist*(365.25 / 240.0);
            QDateTime newDate = _radix.addDays(dayDiff);
            ret += QString(" --> %1")
                .arg(newDate.toString("yyyy/MM/dd"));
        }
        return ret + "\n";
    }
};

int event::_maxWidth = 0;
QDateTime event::_radix;
}

QString
describeParans(const AstroFileList& scopes,
               bool showAll,
               bool showFixedStars,
               double paranOrb)
{
    bool showDates = scopes.count() == 1;
    auto scope = scopes.first()->horoscope();
    short tz = scope.inputData.tz;
    QVector<event> events;
    events << event(scope.inputData.GMT, NULL, 4);    // radix

    int& maxWidth(event::_maxWidth);
    maxWidth = 0;

    event::_radix = scope.inputData.GMT;

    for (const Planet& p: scope.planets) {
        if (p.id == Planet_MC || p.id == Planet_Asc) continue;
        unsigned u = 0;
        for (const QDateTime& dt: p.angleTransit) {
            if (p.name.length() > maxWidth) {
                maxWidth = p.name.length();
            }
            events << event(dt, p, u++);
        }
    }

    if (showFixedStars) {
        for (const Star& s: scope.stars) {
            unsigned u = 0;
            for (const QDateTime& dt: s.angleTransit) {
                if (!dt.isValid()) continue;
                if (s.name.length() > maxWidth) {
                    maxWidth = s.name.length();
                }
                events << event(dt, s, u++);
            }
        }
    }

    std::sort(events.begin(), events.end());

    QString ret = QString("%1  %2  %3\n")
        .arg(QObject::tr("Planet"), -maxWidth)
        .arg(QObject::tr("Event"), -6)
        .arg(QObject::tr("LT"), -9);

    double orb = paranOrb * 240;
    bool anyPrinted = false;
    QVector<event>::ConstIterator lastPrinted = events.constEnd();
    for (QVector<event>::ConstIterator it = events.constBegin();
         it != events.constEnd(); ++it)
    {
        if (showAll) {
            ret += it->fmt(tz);
            continue;
        }
        const Planet* p = dynamic_cast<const Planet*>(it->_star);
        if (p || !it->_star /*radix*/) {
            int j = 1;
            QVector<event>::ConstIterator bit = it;
            while (bit != events.constBegin()
                   && qAbs((*(bit - 1))._dt.secsTo(it->_dt)) <= orb) {
                if (bit - 1 == lastPrinted) {
                    anyPrinted = false;
                    break;
                }
                ++j; --bit;
            }
            QVector<event>::ConstIterator lastPlanet = it;
            QVector<event>::ConstIterator nit;
            for (nit = it + 1;
                 nit != events.constEnd()
                 && qAbs(nit->_dt.secsTo(lastPlanet->_dt)) <= orb;
                 ++nit) {
                if (dynamic_cast<const Planet*>(nit->_star)) {
                    lastPlanet = nit;
                }
                ++j;
            }
            if (anyPrinted) {
                ret += "\n";
            }
            it = nit;
            while (bit != events.constEnd() && j-- > 0) {
                ret += bit->fmt(tz);
                lastPrinted = it = bit++;
                anyPrinted = true;
            }
        }
    }

    return ret;
}


QString
describeSpeculum(const Horoscope &scope,
                 bool showFixedStars)
{
    short tz = scope.inputData.tz;
    int& maxWidth(event::_maxWidth);
    if (maxWidth == 0) {
        for (const Planet& p: scope.planets) {
            if (p.id != Planet_MC
                    && p.id != Planet_Asc
                    && p.name.length() > maxWidth)
            {
                maxWidth = p.name.length();
            }
        }
        for (const Star& s: scope.stars) {
            if (s.name.length() > maxWidth) {
                maxWidth = s.name.length();
            }
        }
    }
    QString ret = QString("%1  %2  %3  %4  %5\n")
        .arg(QObject::tr("Planet"), -maxWidth)
        .arg(QObject::tr("Rise"), -9)
        .arg(QObject::tr("MC"), -9)
        .arg(QObject::tr("Set"), -9)
        .arg(QObject::tr("IC"), -9);
    for (const Planet& p: scope.planets) {
        if (p.id == Planet_MC || p.id == Planet_Asc) continue;
        ret += QString("%1").arg(p.name, -maxWidth);
        for (int i : QList<int>({0, 2, 1, 3})) {
            ret += " " + _formatTime(p.angleTransit.at(i), tz);
        }
        ret += "\n";
    }
    if (!showFixedStars) {
        return ret;
    }
    for (const Star& s: scope.stars) {
        ret += QString("%1").arg(s.name, -maxWidth);
        for (int i : QList<int>({0, 2, 1, 3})) {
            ret += " " + _formatTime(s.angleTransit.at(i), tz);
        }
        ret += "\n";
    }
    maxWidth = 0;   // reset for next time...
    return ret;
}

QString
describe(AstroFileList&& scopes,
         Articles article /*=All*/,
         double paranOrb /*=1.0*/)
{
    QString ret;

    auto scope = scopes.first()->horoscope();

    ret += QObject::tr("%1 sign").arg(scope.zodiac.name) + "\n\n";

    if (article & Article_Input)
        ret += describeInput(scope.inputData) + "\n\n";

    if ((article & Article_Planet) && scope.planets.count()) {
        foreach(const Planet& p, scope.planets)
            ret += describePlanet(p, scope.zodiac) + "\n";

        ret += "\n";
        if (auto p = auriga(scope)) {
            ret += QObject::tr("Auriga:     %1").arg(p->name) + "\n";
        }
        if (auto p = almuten(scope)) {
            ret += QObject::tr("Almuten:    %1").arg(p->name) + "\n";
        }
        if (auto p = doryphoros(scope)) {
            ret += QObject::tr("Doryphoros: %1").arg(p->name) + "\n";
        }
        ret += "\n";
    }


    if ((article & Article_Houses) && scope.houses.system)
        ret += describeHouses(scope.houses, scope.zodiac) + "\n\n";


    if ((article & Article_Aspects) && scope.aspects.count()) {
        foreach(const Aspect& asp, scope.aspects)
            ret += describeAspect(asp, true) + "\n";
        ret += "\n";
    }


    if ((article & Article_Power) && scope.planets.count())
        foreach(const Planet& p, scope.planets)
        if (p.isReal)
            ret += p.name + "\n" + describePower(p, scope) + "\n\n";

    if ((article & Article_Parans) && scope.planets.count()) {
        ret += describeParans(scopes,
                              bool(article & Article_DiurnalEvents),
                              bool(article & Article_FixedStars),
                              paranOrb) + "\n\n";
    }

    if ((article & Article_Speculum) && scope.planets.count()) {
        ret += describeSpeculum(scope, bool(article & Article_FixedStars)) + "\n\n";
    }

    return ret;
}


}
