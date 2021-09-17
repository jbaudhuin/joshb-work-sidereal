
#include <set>
#include <swephexp.h>
//#undef MSDOS     // undef macroses that made by SWE library
#undef UCHAR
#undef forward

#include <QDebug>
#include <QColor>

#include "csvreader.h"
#include "astro-data.h"
#include "astro-calc.h"

namespace A {

QMap<AspectSetId, AspectsSet> Data::aspectSets;
QMap<PlanetId, Planet> Data::planets;
QMap<std::string, Star> Data::stars;
QMap<HouseSystemId, HouseSystem> Data::houseSystems;
QMap<ZodiacId, Zodiac> Data::zodiacs;
AspectSetId Data::topAspSet;
QString Data::usedLang;
/*static*/ QMap<PlanetId, GlyphName> Data::signInfo;

void Data::load(QString language)
{
    usedLang = language;
#if MSDOS
    char ephePath[] = "swe\\";
#else
    char ephePath[] = "swe/";
#endif
    swe_set_ephe_path(ephePath);
    CsvFile f;

    f.setFileName("astroprocessor/aspect_sets.csv");
    if (!f.openForRead()) qDebug() << "A: Missing file" << f.fileName();
    topAspSet = 0;
    AspectSetId dynAspSet = 0;
    while (f.readRow())
    {
        AspectsSet s;
        s.id = f.row(0).toInt();
        s.name = language == "ru" ? f.row(2) : f.row(1);
        if (s.name.startsWith("Dynamic")) dynAspSet = s.id;

        //for (int i = 3; i < f.columnsCount(); i++)
        //  s.userData[f.header(i)] = f.row(i);

        aspectSets[s.id] = s;
        topAspSet = qMax(topAspSet, s.id);         // update top set
    }

    int atype = 0;

    f.close();
    f.setFileName("astroprocessor/aspects.csv");
    if (!f.openForRead()) qDebug() << "A: Missing file" << f.fileName();
    while (f.readRow()) {
        AspectType a;
        auto setId = AspectSetId(f.row(0).toUInt());
        a.set = &aspectSets[setId];
        a.id = f.row(1).toInt();
        if (a.id > atype) atype = a.id;

        a.name = language == "ru" ? f.row(3) : f.row(2);
        a.angle = f.row(4).toFloat();
        a._orb = f.row(5).toFloat();

        for (int i = 6; i < f.columnsCount(); i++)
            a.userData[f.header(i)] = f.row(i);

        aspectSets[setId].aspects[a.id] = a;
    }
    f.close();

    if (dynAspSet) {
        // Dynamic aspect set (aspects can be enabled separately
        // under user control)
        auto& aset = aspectSets[dynAspSet];

        std::set<unsigned> harmonics;
        unsigned i = 1, j = 1;
        auto addAspect = [&](float angle) {
            AspectType a;
            a.set = &aset;
            a.id = ++atype;
            a.name = QString("%1/%2").arg(j).arg(i);
            a._harmonic = i;
            a.factors = harmonics;
            a.angle = angle;
            a._orb = float(16)/i;
            a.userData["good"] = QString::number(i);
            aset.aspects[a.id] = a;
        };
        addAspect(0);   // conjunction 1/1

        for (i = 2; i <= 32; ++i) {
            auto ifac = A::getAllFactors(i);
            ifac.erase(1);

            harmonics.clear();
            for (auto k : A::getPrimeFactors(i)) harmonics.insert(k);

            auto ang = float(360)/i;
            for (j = 1; j <= i/2; ++j) {
                auto jfac = A::getAllFactors(j);
                jfac.erase(1);
                bool common = false;
                for (auto k: jfac) if ((common = ifac.count(k))) break;
                if (!common && ifac.count(j)==0) addAspect(ang * j);
            }
        }
    }

    f.setFileName("astroprocessor/hsystems.csv");
    if (!f.openForRead()) qDebug() << "A: Missing file" << f.fileName();
    while (f.readRow())
    {
        HouseSystem h;
        h.id = f.row(0).toInt();
        h.name = language == "ru" ? f.row(2) : f.row(1);
        h.sweCode = f.row(3)[0].toLatin1();

        houseSystems[h.id] = h;
    }

    f.close();
    f.setFileName("astroprocessor/zodiac.csv");
    if (!f.openForRead()) qDebug() << "A: Missing file" << f.fileName();
    while (f.readRow())
    {
        Zodiac z;
        z.id = f.row(0).toInt();
        z.name = language == "ru" ? f.row(2) : f.row(1);

        zodiacs[z.id] = z;
    }

    f.close();
    f.setFileName("astroprocessor/signs.csv");
    if (!f.openForRead()) qDebug() << "A: Missing file" << f.fileName();
    QMultiHash<QString, ZodiacSignId> signs;    // collect and find signs by tag
    while (f.readRow()) {
        ZodiacSign s;
        s.zodiacId = f.row(0).toInt();
        s.id = f.row(1).toInt();
        s.tag = f.row(2);
        s.name = language == "ru" ? f.row(4) : f.row(3);
        s.startAngle = f.row(5).toFloat();
        s.endAngle = f.row(6).toFloat() + s.startAngle;
        if (s.endAngle > 360) s.endAngle -= 360;

        for (int i = 7; i < f.columnsCount(); i++)
            s.userData[f.header(i)] = f.row(i);

        zodiacs[s.zodiacId].signs << s;
        signs.insert(s.tag, s.id);

        auto sid = s.id + Ingresses_Start;
        if (!signInfo.contains(sid)) {
            signInfo.insert(sid, {s.userData["fontChar"].toInt(), s.name});
        }
    }

    // fill in sign data for 30deg sidereals
    for (int z : zodiacs.keys()) {
        if (zodiacs[z].signs.isEmpty()) {
            zodiacs[z].signs = zodiacs[0].signs;
        }
    }

    f.close();
    f.setFileName("astroprocessor/planets.csv");
    if (!f.openForRead()) qDebug() << "A: Missing file" << f.fileName();
    while (f.readRow()) {
        Planet p;
        p.id = f.row(0).toInt();
        p.name = language == "ru" ? f.row(2) : f.row(1);
        p.sweNum = f.row(3).toInt();
        p.sweFlags = f.row(4).toInt();
        p.defaultEclipticSpeed.setX(f.row(5).toFloat());
        p.isReal = f.row(6).toInt();
        for (const auto& s : f.row(7).split(','))
            p.homeSigns << signs.values(s);
        for (const auto& s : f.row(8).split(','))
            p.exaltationSigns << signs.values(s);
        for (const auto& s : f.row(9).split(','))
            p.exileSigns << signs.values(s);
        for (const auto& s : f.row(10).split(','))
            p.downfallSigns << signs.values(s);

        for (int i = 11; i < f.columnsCount(); i++)
            p.userData[f.header(i)] = f.row(i);

        planets[p.id] = p;
    }

    planets[Planet_Asc] = { Planet_Asc, "Asc", 402 };
    planets[House_2] = { House_2, "2H", 8230 };
    planets[House_3] = { House_3, "3H", 8224 };
    planets[Planet_IC] = { Planet_IC, "IC", 8225 };
    planets[House_5] = { House_5, "5H", 8240 };
    planets[House_6] = { House_6, "6H", 352 };
    planets[Planet_Desc] = { Planet_Desc, "Desc", 8249 };
    planets[House_8] = { House_8, "8H", "VIII" };
    planets[House_9] = { House_9, "9H", "IX" };
    planets[Planet_MC] = { Planet_MC, "MC", 0x4D };
    planets[House_11] = { House_11, "11H", 8216 };
    planets[House_12] = { House_12, "12H", 8217 };

    unsigned i = 1;
    int j = 0;
    char buf[256], errStr[256];
    double xx[6];
    std::set<std::string> seen { "GPol", "ICRS", "GP1958", "GPPlan", "ZE200" };
    QDateTime now(QDateTime::currentDateTimeUtc());
    double jd = A::getJulianDate(now);
    bool loadEcliptic = getenv("foo");
    while (strcpy(buf, QString::number(i++).toStdString().c_str()),
           swe_fixstar_ut(buf, jd, SEFLG_SWIEPH, xx, errStr) != ERR)
    {
        char* p = strchr(buf, ',');
        if (p && (!*(p + 1) || *(p + 1) == ' ' || seen.count(p + 1) == 1)) {
            continue;
        }
        if (p && p > buf) {
            seen.insert(p + 1);
            *p = '\0';
            std::string name(QString(buf).trimmed().toStdString());
            QString constellar(p + 1);

            double mag = 10;
            bool get = (swe_fixstar_mag(buf, &mag, errStr) != ERR
                        && mag <= 2.2);
            if (!get) {
                static const QString ecl("AriTauGemCncLeoVirLibScoSgrCapAqrPsc");
                get = ecl.indexOf(constellar.right(3), 0) != -1;
            }
            if (get) {
                //              fprintf(stderr,"Ecliptic star %s in %s with magnitude %g\n",
                //                      name.c_str(),
                //                      constellar.right(3).toAscii().constData(),
                //                      mag);
                //stars[name].name = (name + " (" + constellar.right(3).toStdString() + ")").c_str();
                stars[name].name = name.c_str();
                stars[name].id = --j; // use negative numbers to index the stars
            }
        }
    }

    qDebug() << "Astroprocessor: initialized";
}

QColor
Data::getHarmonicColor(unsigned h)
{ return getHarmonicColors()[h]; }

const QMap<unsigned, QColor>&
Data::getHarmonicColors()
{
    static QMap<unsigned, QColor> s_colors;
    if (s_colors.isEmpty()) {
        s_colors[1] = "blue";
        s_colors[2] = "red";
        s_colors[3] = QColor("green").lighter();
        s_colors[5] = "gold";
        s_colors[7] = "cyan";
        s_colors[11] = "magenta";
        s_colors[13] = "royalblue";
        s_colors[17] = "mediumslateblue";
        s_colors[19] = "purple";
        s_colors[23] = "teal";
        s_colors[29] = "lightcoral";
        s_colors[31] = "darkblue";

        QColor rgb;
        for (unsigned i=2, max=32; i <= max; ++i) {
            if (s_colors.contains(i)) continue;
            auto fac = A::getPrimeFactors(i);
            auto num = fac.size();
            qreal red = 0, green = 0, blue = 0;
            uintMSet::value_type lf = 0;
            unsigned u = 0;
            for (auto f: fac) {
                if (lf != f) ++u;
                const QColor& c = s_colors[f];
                red += c.redF()/num;
                green += c.greenF()/num;
                blue += c.blueF()/num;
                lf = f;
            }
            qreal n = fac.size();
            // u - unique numbers
            // n - number of factors
            auto alpha = 1+(u-n)/(2*(n-1));
            s_colors[i].setRgbF(red,green,blue,alpha);
        }
    }
    return s_colors;
}

const Planet&
Data::getPlanet(PlanetId id)
{
    if (Data::planets.contains(id)) return Data::planets[id];
    return planets[Planet_None];
}

int
Data::getSignGlyph(PlanetId id)
{
    return signInfo.value(id).first;
}

QString
Data::getSignName(PlanetId id)
{
    return signInfo.value(id).second;
}

QList<PlanetId>
Data::getPlanets()
{
    return {
        Planet_Sun, Planet_Moon, Planet_Mercury, Planet_Venus,
                Planet_Mars, Planet_Jupiter, Planet_Saturn, Planet_Uranus,
                Planet_Neptune, Planet_Pluto, Planet_Chiron
    };
}

QList<PlanetId>
Data::getAngles()
{
    return {
        Planet_Asc, Planet_IC, Planet_Desc, Planet_MC
    };
}

QList<PlanetId>
Data::getInnerPlanets()
{
    return {
        Planet_Sun, Planet_Moon, Planet_Mercury, Planet_Venus, Planet_Mars
    };
}

QList<PlanetId>
Data::getOuterPlanets()
{
    return {
        Planet_Jupiter, Planet_Saturn, Planet_Uranus,
                Planet_Neptune, Planet_Pluto, Planet_Chiron
    };
}

QList<PlanetId>
Data::getHouses()
{
    return {
        House_1, House_2, House_3, House_4, House_5, House_6,
                House_7, House_8, House_9, House_10, House_11, House_12
    };
}

QList<PlanetId> Data::getSignIngresses()
{
    return {
        Ingress_Aries, Ingress_Taurus, Ingress_Gemini,
                Ingress_Cancer, Ingress_Leo, Ingress_Virgo,
                Ingress_Libra, Ingress_Scorpio, Ingress_Sagittarius,
                Ingress_Capricorn, Ingress_Aquarius, Ingress_Pisces
    };
}

QList<PlanetId>
Data::getNonAngularHouses()
{
    return {
        House_2, House_3, House_5, House_6,
                House_8, House_9, House_11, House_12
    };
}

const Star&
Data::getStar(const QString& name)
{
    std::string stdName = name.toStdString();
    if (stars.contains(stdName)) {
        return stars[stdName];
    }

    static Star dummy;
    return dummy;
}

QList<QString>
Data::getStars()
{
    QList<QString> ret;
    for (const std::string& str : stars.keys()) {
        ret << str.c_str();
    }
    return ret;
}

const HouseSystem&
Data::getHouseSystem(HouseSystemId id)
{
    if (Data::houseSystems.contains(id))
        return Data::houseSystems[id];

    return houseSystems[Housesystem_None];
}

const Zodiac&
Data::getZodiac(ZodiacId id)
{
    if (Data::zodiacs.contains(id))
        return Data::zodiacs[id];

    return zodiacs[Zodiac_Tropical];
}

const QList<HouseSystem>
Data::getHouseSystems()
{
    return Data::houseSystems.values();
}

const QList<Zodiac>
Data::getZodiacs()
{
    return zodiacs.values();
}

double
Data::getSignPos(ZodiacId zid, const QString& sign)
{
    const auto& Z(getZodiac(zid));
    for (const auto& s : Z.signs) {
        if (s.name.startsWith(sign,Qt::CaseInsensitive)) return s.startAngle;
    }
    return 0;
}

/*const QList<AspectType>
Data::getAspects(AspectSetId set)
 {
  set = qBound(0, set, aspects.count());
  return aspects[set].values();
 }*/

const AspectType&
Data::getAspect(AspectId id, const AspectsSet& set)
{
    if (set.aspects.contains(id))
        return aspectSets[set.id].aspects[id];

    return aspectSets[AspectSet_Default].aspects[Aspect_None];
}

AspectsSet&
Data::getAspectSet(AspectSetId set)
{
    if (aspectSets.contains(set))
        return aspectSets[set];

    return aspectSets[AspectSet_Default];
}

/*QList<AspectsSet>&
Data::getAspectSets()
 {
  return aspectSets.values();
 }*/

const AspectsSet &
Data::tightConjunction()
{
    static AspectsSet ret;
    if (ret.isEmpty()) {
        AspectType at;
        at.set = &ret;
        at.id = 0;
        at.name = QObject::tr("Conjunction");
        at.angle = 0;
        at._orb = 1.5;
        ret.aspects[0] = at;
    }
    return ret;
}

QString
ChartPlanetId::glyph() const
{
    if (!isMidpt()) {
        if (_pid >= Ingresses_Start && _pid < Ingresses_End) {
            return QString(QChar(Data::getSignGlyph(_pid)));
        }
        auto var = Data::getPlanet(_pid).userData["fontChar"];
        if (var.canConvert<int>()) {
            return QString(QChar(var.toInt()));
        } else if (var.type()==QVariant::String) {
            return var.toString();
        }
        return "?";
    }
    return QString(QChar(_oppMidpt? 0xD1 : 0xC9))
        + QString(QChar(Data::getPlanet(_pid)
                        .userData["fontChar"].toInt()))
        + QString(QChar(Data::getPlanet(_pid2)
                        .userData["fontChar"].toInt()));
}

QString
ChartPlanetId::name() const
{
    if (!isMidpt()) {
        if (_pid >= Ingresses_Start && _pid < Ingresses_End) {
            return Data::getSignName(_pid);
        }
        return Data::getPlanet(_pid).name;
    }
    return Data::getPlanet(_pid).name.left(3)
        + "/" + Data::getPlanet(_pid2).name.left(3);
}

void load(QString language) { Data::load(language); }
QString usedLanguage()      { return Data::usedLanguage(); }
const Planet& getPlanet(PlanetId pid) { return Data::getPlanet(pid); }


PlanetId
getPlanetId(const QString& name)
{
    for (auto && it : getPlanets()) {
        if (getPlanet(it).name.startsWith(name,Qt::CaseInsensitive))
            return it;
    }
    return Planet_None;
}


QString getPlanetName(const ChartPlanetId& id) { return Data::getPlanet(id).name; }
QString getPlanetGlyph(const ChartPlanetId& id) { return id.name(); }
const Star& getStar(const QString& name) { return Data::getStar(name); }
QList<QString> getStars() { return Data::getStars(); }
const HouseSystem& getHouseSystem(HouseSystemId id) { return Data::getHouseSystem(id); }
const Zodiac& getZodiac(ZodiacId id) { return Data::getZodiac(id); }
const QList<HouseSystem> getHouseSystems() { return Data::getHouseSystems(); }
const QList<Zodiac> getZodiacs() { return Data::getZodiacs(); }
//const QList<AspectType> getAspects(AspectSetId set) { return Data::getAspects(set); }
const AspectType& getAspect(AspectId id, const AspectsSet& set) { return Data::getAspect(id, set); }
double getSignPos(ZodiacId zid,
                  const QString& sign,
                  unsigned degrees /*=0*/,
                  unsigned minutes /*=0*/,
                  unsigned seconds /*=0*/)
{
    return Data::getSignPos(zid,sign) + degrees + minutes/60. + seconds/3600.;
}

QList<AspectsSet> getAspectSets() { return Data::getAspectSets(); }
AspectsSet& getAspectSet(AspectSetId set) { return Data::getAspectSet(set); }
const AspectsSet& topAspectSet() { return Data::topAspectSet(); }
const AspectsSet& tightConjunction() { return Data::tightConjunction(); }


/*static*/
QDateTime
Star::timeToDT(double t, bool greg/*=true*/)
{
    int32 yy, mo, dd, hh, mm;
    double sec;
    swe_jdut1_to_utc(t, greg,
                     &yy, &mo, &dd, &hh, &mm,
                     &sec);
    int32 ss = int32(sec);
    sec -= ss;
    int32 ms = sec*1000;
    QDateTime ret(QDate(yy,mo,dd),QTime(hh,mm,ss,ms),Qt::UTC);
    return ret;
}

void
PlanetGroups::insert(const PlanetQueue & planets,
                     unsigned minQuorum)
{
    PlanetSet plist;
    getPlanetSet(planets, plist);

    PlanetSet pl, plcat;
    PlanetRange r;
    bool anySolo = false;
    for (const auto& p : planets) {
        if (!p.planet.isSolo()
            && (plist.containsSolo(p.planet.chartPlanetId1())
                || plist.containsSolo(p.planet.chartPlanetId2()))) {
            continue;
        }
        if (p.planet.isSolo()) {
            anySolo = true;
        } else if (!anySolo) {
            plcat.insert(p.planet.chartPlanetId1());
            plcat.insert(p.planet.chartPlanetId2());
        }
        pl.insert(p.planet);
        r.insert(p);
    }
    if (!anySolo && plcat.size() == 3 && pl.size() == 2) {
        // Prune a/b=a/c because b is conjunct with c
        return;
    }
    if (pl.size() > 1
        && (anySolo || (!requireAnchor()
                        && (!pl.empty() && !pl.begin()->isOppMidpt())))
        && pl.pop() >= minQuorum) {
        insert(value_type(pl, r));
    }
}

bool _filterFew, _includeMidpoints, _requireAnchor;
bool _includeAscMC, _includeChiron, _includeNodes;

void setFilterFew(bool b/*=true*/) { _filterFew = b; }
bool filterFew() { return _filterFew; }

void setIncludeMidpoints(bool b/*=true*/) { _includeMidpoints = b; }
bool includeMidpoints() { return _includeMidpoints; }

void setIncludeAscMC(bool b/*=true*/) { _includeAscMC = b; }
bool includeAscMC() { return _includeAscMC; }

void setIncludeChiron(bool b/*=true*/) { _includeChiron = b; }
bool includeChiron() { return _includeChiron; }

void setIncludeNodes(bool b/*=true*/) { _includeNodes = b; }
bool includeNodes() { return _includeNodes; }

void setRequireAnchor(bool b/*=true*/) { _requireAnchor = b; }
bool requireAnchor() { return _requireAnchor; }

unsigned _minQuorum, _maxQuorum;
unsigned _maxHarmonic;

unsigned _pfl = 32;
uintBoolMap _pflCache;

void resetPrimeFactorLimit(unsigned pfl /*= 0*/)
{ 
    if (pfl != _pfl) resetPFLCache();
    _pfl = pfl;
}

unsigned primeFactorLimit() { return _pfl; }

bool isWithinPrimeFactorLimit(unsigned h)
{
    if (_pfl == 0 || h <= _pfl) return true;
    if (_pflCache.contains(h)) return _pflCache[h];
    uintMSet pf = getPrimeFactors(h);
    return (_pflCache[h] = (!pf.empty() && *pf.rbegin() <= int(_pfl)));
}

void resetPFLCache() { _pflCache.clear(); }

void setHarmonicsMinQuorum(unsigned q) { _minQuorum = q;  }
unsigned harmonicsMinQuorum() { return _minQuorum; }

void setHarmonicsMaxQuorum(unsigned q) { _maxQuorum = q; }
unsigned harmonicsMaxQuorum() { return _maxQuorum;  }

double _minQOrb, _maxQOrb;

void setHarmonicsMinQOrb(double o) { _minQOrb = o; }
double harmonicsMinQOrb() { return _minQOrb; }

void setHarmonicsMaxQOrb(double o) { _maxQOrb = o; }
double harmonicsMaxQOrb() { return _maxQOrb; }

void setMaxHarmonic(int m) { _maxHarmonic = m; }
unsigned maxHarmonic() { return _maxHarmonic; }


std::vector<bool> _haspEnabled(33, true);

bool dynAspState(unsigned h)
{ if (h > 32) return false; return _haspEnabled[h]; }

void setDynAspState(unsigned h, bool b)
{ if (h > 32) return; _haspEnabled[h] = b; }

uintSSet dynAspState()
{
    uintSSet ret;
    for (unsigned i = 1; i <= 32; ++i) {
        if (_haspEnabled[i]) ret.insert(i);
    }
    return ret;
}

void setDynAspState(const uintSSet& state)
{
    _haspEnabled.assign(33,false);
    for (unsigned h: state) setDynAspState(h, true);
}

qreal _orbFactor = 1.0;
qreal orbFactor() { return _orbFactor; }
void setOrbFactor(qreal ofac) { if (ofac > 0) _orbFactor = ofac; }

/// Pare down scope based on what's already been computed. Essentially, we're
/// merging the input interval, and returning any part of it that is new.
// Assume we have 1..3 9..12. There are a few scenarios.
// (1) We want to insert 2..6. Already 1..3 exists, so we need to pare the
// insertion to 3..6, remove 1..3 and insert 1..6, and return 3..6.
// (2) We want to insert 0..2. Already 1..3 exists, so we need to pare the
// insertion to 0..1, remove 1..3, and insert 0..3, and return 0..1.
// (3) We want to insert 0..13. already 1..3 exists, so we keep a range 0..1,
// remove 1..3, and insert 0..3.

EventUpdateData
EventStore::getEventUpdateScope(EventScope evscope,
                                EventUpdateType uptype)
{
    auto it = find(evscope.eventType);
    if (it == end() || it->ranges.empty()) {
        it = insert(evscope.eventType, {{evscope.range},{}});
        return { it->ranges, it->events };
    }
    if (it->ranges.size()==1 && *it->ranges.begin() == evscope.range) {
        throw noNeed();
    }

    ADateRange addingRange = evscope.range;
    auto& ranges = it->ranges;
    if (uptype == etcUpdate) {
        ADateRangeSet upd = ranges;
        upd.insert(addingRange);
        ranges.clear();
        for (auto pit = upd.begin(); pit != upd.end(); ) {
            auto r = *pit;
            auto nit = std::next(pit);
            while (nit != upd.end() && r.second >= nit->first) {
                r.second = nit->second;
            }
            ranges.insert(r);
            pit = nit;
        }
        auto& evs = it->events;
        evs.remove_if([&addingRange](const HarmonicEvent& ev) {
            return addingRange.contains(ev.dateTime());
        });
        return { { addingRange }, evs };
    }

    ADateRangeSet upd, out;
    bool simple = true, beyondRange = false;
    for (auto rit = ranges.begin(); rit != ranges.end(); ++rit) {
        const auto& r = *rit;
        if (beyondRange || r.second < addingRange.first) {
            // existing range fully precedes input range
            upd.insert(r);
            continue;
        }
        if (r.first < addingRange.first && r.second > addingRange.first) {
            // existing range partly goes beyond input range
            simple = false;
            if (r.second > addingRange.second) {
                if (out.empty()) throw noNeed();
                upd.insert(r);
                beyondRange = true;
            } else {
                upd.insert(r);
                addingRange.first = r.second;
                auto nit = std::next(rit);
                if (nit == ranges.end()) {
                    upd.insert(addingRange);
                    out.insert(addingRange);
                }
            }
            continue;
        }
        if (r.first > addingRange.second) { // XXX is this an invariant?
            // existing range fully exceeds input range
            if (simple) break;
            upd.insert(r);
            upd.insert(addingRange);
            out.insert(addingRange);
            beyondRange = true;
            continue;
        }
    }
    if (simple) {
        ranges.insert(addingRange);
        return { { addingRange }, it->events };
    }
    if (upd.size()>1) {
        // Is this actually needed? Won't the above suffice?
        ranges.clear();
        for (auto pit = upd.begin(); pit != upd.end(); ) {
            auto r = *pit;
            auto nit = std::next(pit);
            while (nit != upd.end() && r.second >= nit->first) {
                r.second = nit->second;
            }
            ranges.insert(r);
            pit = nit;
        }
    } else {
        ranges.swap(upd);
    }

    return { out, it->events };
}

} // namespace A
