
#include <set>
#include <swephexp.h>
//#undef MSDOS     // undef macroses that made by SWE library
#undef UCHAR
#undef forward

#include <QDebug>
#include "csvreader.h"
#include "astro-data.h"
#include "astro-calc.h"

namespace A {


QMap<AspectSetId, AspectsSet> Data::aspectSets = QMap<AspectSetId, AspectsSet>();
QMap<PlanetId, Planet> Data::planets = QMap<PlanetId, Planet>();
QMap<std::string, Star> Data::stars;
QMap<HouseSystemId, HouseSystem> Data::houseSystems = QMap<HouseSystemId, HouseSystem>();
QMap<ZodiacId, Zodiac> Data::zodiacs = QMap<ZodiacId, Zodiac>();
AspectSetId Data::topAspSet = AspectSetId();
QString Data::usedLang = QString();

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
    while (f.readRow())
    {
        AspectsSet s;
        s.id = f.row(0).toInt();
        s.name = language == "ru" ? f.row(2) : f.row(1);

        //for (int i = 3; i < f.columnsCount(); i++)
        //  s.userData[f.header(i)] = f.row(i);

        aspectSets[s.id] = s;
        topAspSet = qMax(topAspSet, s.id);         // update top set
    }

    f.close();
    f.setFileName("astroprocessor/aspects.csv");
    if (!f.openForRead()) qDebug() << "A: Missing file" << f.fileName();
    while (f.readRow())
    {
        AspectType a;
        AspectSetId setId = f.row(0).toUInt();
        a.set = &aspectSets[setId];
        a.id = f.row(1).toInt();
        a.name = language == "ru" ? f.row(3) : f.row(2);
        a.angle = f.row(4).toFloat();
        a.orb = f.row(5).toFloat();

        for (int i = 6; i < f.columnsCount(); i++)
            a.userData[f.header(i)] = f.row(i);

        aspectSets[setId].aspects[a.id] = a;
    }

    f.close();
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
    QHash<QString, ZodiacSignId> signs;            // collect and find signs by tag
    while (f.readRow())
    {
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
    }

    // fill in sign data for 30deg sidereals
    foreach(int z, zodiacs.keys())
    {
        if (zodiacs[z].signs.isEmpty()) {
            zodiacs[z].signs = zodiacs[0].signs;
        }
    }

    f.close();
    f.setFileName("astroprocessor/planets.csv");
    if (!f.openForRead()) qDebug() << "A: Missing file" << f.fileName();
    while (f.readRow())
    {
        Planet p;
        p.id = f.row(0).toInt();
        p.name = language == "ru" ? f.row(2) : f.row(1);
        p.sweNum = f.row(3).toInt();
        p.sweFlags = f.row(4).toInt();
        p.defaultEclipticSpeed.setX(f.row(5).toFloat());
        p.isReal = f.row(6).toInt();
        foreach(QString s, f.row(7).split(',')) p.homeSigns << signs.values(s);
        foreach(QString s, f.row(8).split(',')) p.exaltationSigns << signs.values(s);
        foreach(QString s, f.row(9).split(',')) p.exileSigns << signs.values(s);
        foreach(QString s, f.row(10).split(',')) p.downfallSigns << signs.values(s);

        for (int i = 11; i < f.columnsCount(); i++)
            p.userData[f.header(i)] = f.row(i);

        planets[p.id] = p;
    }
    Planet asc;
    asc.id = Planet_Asc; 
    asc.name = "Asc";
    asc.userData["fontChar"] = 402;  // "As"
    planets[Planet_Asc] = asc;

    Planet mc;
    mc.id = Planet_MC; 
    mc.name = "MC"; 
    mc.userData["fontChar"] = 77;  // "M" -- no Mc available :(
    planets[Planet_MC] = mc;

    int i = 1, j = 0;
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
                        && mag <= 2.0);
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

const Planet&
Data::getPlanet(PlanetId id)
{
    if (Data::planets.contains(id))
        return Data::planets[id];

    return planets[Planet_None];
}

QList<PlanetId>
Data::getPlanets()
{
    return planets.keys();
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

QList<QString> Data::getStars()
{
    QList<QString> ret;
    foreach (const std::string str, stars.keys()) {
        ret << str.c_str();
    }
    return ret;
}

const HouseSystem& Data :: getHouseSystem(HouseSystemId id)
 {
  if (Data::houseSystems.contains(id))
    return Data::houseSystems[id];

  return houseSystems[Housesystem_None];
 }

const Zodiac& Data :: getZodiac(ZodiacId id)
 {
  if (Data::zodiacs.contains(id))
    return Data::zodiacs[id];

  return zodiacs[Zodiac_Tropical];
 }

const QList<HouseSystem> Data :: getHouseSystems()
 {
  return Data::houseSystems.values();
 }

const QList<Zodiac> Data :: getZodiacs()
 {
  return zodiacs.values();
 }

/*const QList<AspectType> Data :: getAspects(AspectSetId set)
 {
  set = qBound(0, set, aspects.count());
  return aspects[set].values();
 }*/

const AspectType& Data :: getAspect(AspectId id, const AspectsSet& set)
 {
  if (set.aspects.contains(id))
    return aspectSets[set.id].aspects[id];

  return aspectSets[AspectSet_Default].aspects[Aspect_None];
 }

const AspectsSet& Data :: getAspectSet(AspectSetId set)
 {
  if (aspectSets.contains(set))
    return aspectSets[set];

  return aspectSets[AspectSet_Default];
 }

/*QList<AspectsSet>& Data :: getAspectSets()
 {
  return aspectSets.values();
 }*/

const AspectsSet &
Data::tightConjunction()
{
    static AspectsSet ret;
    if (ret.isEmpty()) {
        AspectType at;
        at.set = NULL;
        at.id = 0;
        at.name = QObject::tr("Conjunction");
        at.angle = 0;
        at.orb = 1.5;
        ret.aspects[0] = at;
    }
    return ret;
}

QString
ChartPlanetId::glyph() const
{
    if (!isMidpt())
        return QString(Data::getPlanet(pid).userData["fontChar"].toInt());
    return QString(oppMidpt? 0xD1 : 0xC9)
        + QString(Data::getPlanet(pid).userData["fontChar"].toInt())
        + QString(Data::getPlanet(pid2).userData["fontChar"].toInt());
}

QString
ChartPlanetId::name() const
{
    if (!isMidpt()) return Data::getPlanet(pid).name;
    return Data::getPlanet(pid).name.left(3)
        + "/" + Data::getPlanet(pid2).name.left(3);
}

void load(QString language) { Data::load(language); }
QString usedLanguage()      { return Data::usedLanguage(); }
const Planet& getPlanet(PlanetId pid) { return Data::getPlanet(pid); }


PlanetId
getPlanet(const QString& name)
{
    for (auto && it : getPlanets()) {
        if (getPlanet(it).name == name) return it;
    }
    return Planet_None;
}


QString getPlanetName(const ChartPlanetId& id) { return Data::getPlanet(id).name; }
QString getPlanetGlyph(const ChartPlanetId& id) { return id.name(); }
const Star& getStar(const QString& name) { return Data::getStar(name); }
QList<PlanetId> getPlanets() { return Data::getPlanets(); }
QList<QString> getStars() { return Data::getStars(); }
const HouseSystem& getHouseSystem(HouseSystemId id) { return Data::getHouseSystem(id); }
const Zodiac& getZodiac(ZodiacId id) { return Data::getZodiac(id); }
const QList<HouseSystem> getHouseSystems() { return Data::getHouseSystems(); }
const QList<Zodiac> getZodiacs() { return Data::getZodiacs(); }
//const QList<AspectType> getAspects(AspectSetId set) { return Data::getAspects(set); }
const AspectType& getAspect(AspectId id, const AspectsSet& set) { return Data::getAspect(id, set); }
QList<AspectsSet> getAspectSets() { return Data::getAspectSets(); }
const AspectsSet& getAspectSet(AspectSetId set) { return Data::getAspectSet(set); }
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
QMap<unsigned, bool> _pflCache;

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
    uintSet pf = getPrimeFactors(h);
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

}
