#ifndef A_DATA_H
#define A_DATA_H

#include <QString>
#include <QDateTime>
#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QVariant>
#include <QMetaType>
#include <set>
#include <deque>
#include <algorithm>

namespace A {


typedef int ZodiacSignId;
typedef int ZodiacId;
typedef int AspectId;
typedef int HouseSystemId;
typedef int PlanetId;
typedef int AspectSetId;


const PlanetId      Planet_None          = -1;
const PlanetId      Planet_MC = 20;
const PlanetId      Planet_Asc = 21;
const PlanetId      Planet_Sun           =  0;
const PlanetId      Planet_Moon          =  1;
const PlanetId      Planet_Mercury       =  2;
const PlanetId      Planet_Venus         =  3;
const PlanetId      Planet_Mars          =  4;
const PlanetId      Planet_Jupiter       =  5;
const PlanetId      Planet_Saturn        =  6;
const PlanetId      Planet_Uranus        =  7;
const PlanetId      Planet_Neptune       =  8;
const PlanetId      Planet_Pluto         =  9;
const PlanetId      Planet_NorthNode     = 10;
const PlanetId      Planet_SouthNode     = 11;
const PlanetId      Planet_Chiron = 12;

const AspectId      Aspect_None          = -1;
const AspectId      Aspect_Conjunction   =  0;
const AspectId      Aspect_Trine         =  1;
const AspectId      Aspect_Sextile       =  2;
const AspectId      Aspect_Opposition    =  3;
const AspectId      Aspect_Quadrature    =  4;

const HouseSystemId Housesystem_None     = -1;
const HouseSystemId Housesystem_Placidus =  0;

const ZodiacId      Zodiac_Tropical      =  0;
const ZodiacId      Zodiac_None          = -1;

const ZodiacSignId  Sign_None            = -1;

const AspectSetId   AspectSet_Default    =  0;


struct ZodiacSign {
  ZodiacSignId id;
  ZodiacId zodiacId;
  QString tag;
  QString name;
  float startAngle;
  float endAngle;
  QMap<QString, QVariant> userData;

  ZodiacSign() { id         = Zodiac_None;
                 zodiacId   = -1;
                 startAngle = 0;
                 endAngle   = 0; }
};

struct Zodiac {
  ZodiacId id;
  QString name;
  QList<ZodiacSign> signs;

  Zodiac() { id = Zodiac_None; }
};

struct HouseSystem {
  HouseSystemId id;
  QString  name;
  char     sweCode;
  QMap<QString, QVariant> userData;

  HouseSystem() { id = Housesystem_None; }
};

struct Houses
{
    double         cusp[12];            // angles of cuspides (0... 360)
    double         Asc, MC, RAMC, RAAC, RADC, OAAC, ODDC, Vx, EA, startSpeculum;
    double         halfMedium, halfImum;
    const HouseSystem* system;

    Houses()
    {
        for (auto& c : cusp) c = 0;
        system = NULL;
        Asc = MC = RAMC = RAAC = RADC = OAAC = ODDC, Vx = EA =
            startSpeculum = 0;
        halfMedium = halfImum = 0;
    }
};

struct Star
{
    PlanetId       id;
    QString        name;
    int            sweFlags;
    PlanetId       configuredWithPlanet;
    QMap<QString, QVariant> userData;

    enum angleTransitMode { atAsc, atDesc, atMC, atIC, numAngles };
    static int     angleTransitFlag(unsigned int mode) { return 1<<mode; }
    QVector<QDateTime> angleTransit;

    static QDateTime timeToDT(double t, bool greg=true);

    virtual PlanetId getPlanetId() const { return Planet_None; }
    virtual int     getSWENum() const { return -10; }
    virtual bool    isStar() const { return true; }
    virtual bool    isAsteroid() const { return false; }
    virtual bool    isPlanet() const { return false; }

    bool            isConfiguredWithPlanet() const
    { return configuredWithPlanet != Planet_None; }

    QPointF        horizontalPos;       // x - azimuth (0... 360), y - height (0... 360)
    QPointF        eclipticPos;         // x - longitude (0... 360), y - latitude (0... 360)
    QPointF        equatorialPos;       // x - rectascension, y - declination
    double         distance;            // A.U. (astronomical units)
    qreal          pvPos;
    int            house;

    Star() : angleTransit(4) {
        id = Planet_None;
        configuredWithPlanet = Planet_None;
        horizontalPos = QPoint(0,0);
        eclipticPos   = QPoint(0,0);
        equatorialPos = QPoint(0,0);
        sweFlags = 0;
        pvPos = 0;
        distance = 0;
        house = 0;
    }

    virtual ~Star() { }

    operator Star*() { return this; }
    operator const Star*() const { return this; }

    bool operator==(const Star & other) const
    { return name == other.name && this->eclipticPos == other.eclipticPos; }
    bool operator!=(const Star & other) const
    { return name != other.name || this->eclipticPos != other.eclipticPos; }
};

struct PlanetPower
{
  int            dignity;
  int            deficient;

  PlanetPower() { dignity = 0;
                  deficient = 0; }
};

enum PlanetPosition { Position_Normal,
                      Position_Exaltation,
                      Position_Dwelling,
                      Position_Downfall,
                      Position_Exile };

struct Planet : public Star
{
  int            sweNum;
  bool           isReal;
  QVector2D      defaultEclipticSpeed;
  QList<ZodiacSignId> homeSigns,
                 exaltationSigns,
                 exileSigns,
                 downfallSigns;

  QVector2D      eclipticSpeed;       // x - longitude speed (degree/day)
  QVector2D      equatorialSpeed;
  PlanetPosition position;
  PlanetPower    power;
  const ZodiacSign* sign;
  int            houseRuler;

  Planet() { sweNum = 0;
             isReal = false;
             eclipticSpeed = equatorialSpeed = QVector2D(0,0);
             position = Position_Normal;
             sign = 0;
             houseRuler = 0; }

  operator Planet*() { return this; }
  operator const Planet*() const { return this; }

  PlanetId      getPlanetId() const { return id; }
  virtual int   getSWENum() const { return sweNum; }
  virtual bool  isStar() const { return false; }
  //virtual bool  isAsteroid() const { return false; }
  virtual bool  isPlanet() const { return true; }

  bool operator==(const Planet & other) const
  { return this->id == other.id && this->eclipticPos == other.eclipticPos; }
  bool operator!=(const Planet & other) const
  { return this->id != other.id || this->eclipticPos != other.eclipticPos; }
};

//struct AspectsSet;

struct AspectType {
  AspectId id;
  const struct AspectsSet* set;
  QString  name;
  float    angle;
  float    orb;
  QMap<QString, QVariant> userData;

  AspectType() { id = Aspect_None;
                 set = 0;
                 angle = 0;
                 orb = 0; }
};

struct Aspect {
  const AspectType* d;
  const Planet* planet1;
  const Planet* planet2;
  float         angle;
  float         orb;
  bool          applying;

  Aspect()    { planet1 = nullptr;
                planet2 = nullptr;
                angle   = 0;
                orb     = 0;
                d       = nullptr;
                applying  = false; }
};

struct AspectsSet {
  AspectSetId id;
  QString name;
  QMap<AspectId, AspectType> aspects;
  bool isEmpty() const { return aspects.isEmpty(); }

  AspectsSet() { id = AspectSet_Default; }
};


typedef QList<Aspect> AspectList;
typedef QList<Planet> PlanetList;
typedef QMap<PlanetId, Planet> PlanetMap;
typedef QMap<std::string, Star> StarMap;

class Data
{
private:
    static QString usedLang;
    static QMap<AspectSetId, AspectsSet> aspectSets;
    static QMap<HouseSystemId, HouseSystem> houseSystems;
    static QMap<ZodiacId, Zodiac> zodiacs;
    static PlanetMap planets;
    static StarMap stars;
    static AspectSetId topAspSet;

public:
    static void load(QString language);
    static const QString usedLanguage() { return usedLang; }

    static const Planet& getPlanet(PlanetId id);
    static QList<PlanetId> getPlanets();

    static const Star& getStar(const QString& name);
    static QList<QString> getStars();

    static const HouseSystem& getHouseSystem(HouseSystemId id);
    static const QList<HouseSystem> getHouseSystems();

    static const Zodiac& getZodiac(ZodiacId id);
    static const QList<Zodiac> getZodiacs();

    static const AspectType& getAspect(AspectId id, const AspectsSet& set);
    //static const QList<AspectType> getAspects(AspectSetId set);

    static QList<AspectsSet> getAspectSets() { return aspectSets.values(); }
    static const AspectsSet& getAspectSet(AspectSetId set);
    static const AspectsSet& topAspectSet() { return aspectSets[topAspSet]; }
    static const AspectsSet& tightConjunction();
};

class ChartPlanetId;

void load(QString language);
QString usedLanguage();
const Planet& getPlanet(PlanetId pid);
PlanetId getPlanet(const QString& name);
QString getPlanetName(const ChartPlanetId& id);
QString getPlanetGlyph(const ChartPlanetId& id);
QList<PlanetId> getPlanets();
const Star& getStar(const QString& name);
QList<QString> getStars();
const HouseSystem& getHouseSystem(HouseSystemId id);
const Zodiac& getZodiac(ZodiacId id);
const QList<HouseSystem> getHouseSystems();
const QList<Zodiac> getZodiacs();
//const QList<AspectType> getAspects(AspectSetId set);
const AspectType& getAspect(AspectId id, const AspectsSet& set);
QList<AspectsSet> getAspectSets();
const AspectsSet& getAspectSet(AspectSetId set);
const AspectsSet&  topAspectSet();
const AspectsSet& tightConjunction();


struct InputData
{
  QDateTime      GMT;                 // greenwich time & date
  QVector3D      location;            // x - longitude (-180...180); y - latitude (-180...180), z - height
  HouseSystemId  houseSystem;
  ZodiacId       zodiac;
  AspectSetId    aspectSet;
  short          tz;
  double         harmonic;

  InputData() {
      GMT.setTimeSpec(Qt::UTC);
      GMT.setTime_t(0);
      location    = QVector3D(0,0,0);
      houseSystem = Housesystem_Placidus;
      zodiac      = Zodiac_Tropical;
      aspectSet   = AspectSet_Default;
      tz          = 0;
      harmonic    = 1;
  }
};

class ChartPlanetId {
    int fid;
    bool oppMidpt;
    PlanetId pid, pid2;

public:
    ChartPlanetId(PlanetId planetId = Planet_None,
                  PlanetId planetId2 = Planet_None) :
        fid(0), pid(planetId), pid2(planetId2), oppMidpt(false)
    {
        if (pid2 != Planet_None && pid > pid2) {
            oppMidpt = true;
            std::swap(pid, pid2);
        }
    }

    ChartPlanetId(int fileId, PlanetId planetId, PlanetId planetId2) :
        fid(fileId), pid(planetId), pid2(planetId2), oppMidpt(false)
    {
        if (pid2 != Planet_None && pid > pid2) {
            oppMidpt = true;
            std::swap(pid, pid2);
        }
    }

    QString name() const;
    QString glyph() const;

    bool isMidpt() const { return pid2 != Planet_None; }
    bool isOppMidpt() const { return isMidpt() && oppMidpt; }
    bool isSolo() const { return !isMidpt(); }

    bool operator==(const ChartPlanetId& cpid) const
    { return fid == cpid.fid && pid == cpid.pid && pid2 == cpid.pid2; }

    bool operator!=(const ChartPlanetId& cpid) const
    { return fid != cpid.fid || pid != cpid.pid || pid2 != cpid.pid2; }

    bool operator<(const ChartPlanetId& cpid) const
    {
#if 0
        // this version sorts by file and the first and then second planet
        return fid < cpid.fid || (fid == cpid.fid && pid < cpid.pid)
            || (fid == cpid.fid && pid == cpid.pid && pid2 < cpid.pid2)
            || (fid == cpid.fid && pid == cpid.pid && pid2 == cpid.pid2
                && oppMidpt < cpid.oppMidpt);
#else
        // this version sorts by file, single planets, conjoined midpoints,
        // and lastly opposing midpoints.
        if (fid < cpid.fid) return true;
        if (fid > cpid.fid) return false;
        if (isMidpt() < cpid.isMidpt()) return true;
        if (isMidpt() > cpid.isMidpt()) return false;
        if (isMidpt()) {
            if (oppMidpt < cpid.oppMidpt) return true;
            if (oppMidpt > cpid.oppMidpt) return false;
        }
        if (pid < cpid.pid) return true;
        if (pid > cpid.pid) return false;
        if (isMidpt()) {
            if (pid2 < cpid.pid2) return true;
            if (pid2 > cpid.pid2) return false;
        }
        return false;
#endif
    }

    bool operator<=(const ChartPlanetId& cpid) const 
    { return operator==(cpid) || operator<(cpid); }

    int fileId() const { return fid; }
    PlanetId planetId() const { return pid; }
    ChartPlanetId chartPlanetId1() const { return ChartPlanetId(fid, pid, Planet_None); }
    PlanetId planetId2() const { return pid2; }
    ChartPlanetId chartPlanetId2() const { return ChartPlanetId(fid, pid2, Planet_None); }

    bool operator==(PlanetId p) const { return pid == p && (isSolo() || pid2 == p); }
    bool operator!=(PlanetId p) const { return pid != p && (isSolo() || pid2 != p); }
    bool operator<(PlanetId p) const { return pid < p && (isSolo() || pid2 < p); }
    bool operator>(PlanetId p) const { return pid > p && (isSolo() || pid2 > p); }
    bool operator<=(PlanetId p) const { return pid <= p && (isSolo() || pid2 <= p); }
    bool operator>=(PlanetId p) const { return pid >= p && (isSolo() || pid2 >= p); }

    operator PlanetId() const { return pid; }
};

class samePlanet {
public:
    samePlanet(const ChartPlanetId& cpid, 
               bool isSolo=false, 
               bool cmpFID=false) : 
        _cpid(cpid), _isSolo(isSolo), _compareFileId(cmpFID)
    { }

    bool operator()(const ChartPlanetId& opid) const
    {
        if (_compareFileId && _cpid.fileId() != opid.fileId()) return false;
        return _isSolo ? (opid.isSolo() && _cpid.planetId() == opid.planetId())
            : (_cpid.planetId() == opid.planetId() 
               || _cpid.planetId() == opid.planetId2());
    }

private:
    ChartPlanetId _cpid;
    bool _isSolo;
    bool _compareFileId;
};

class PlanetSet : public std::set<ChartPlanetId> {
public:
    using std::set<ChartPlanetId>::set;

    bool contains(PlanetId pid) const
    { return std::any_of(begin(), end(), samePlanet(pid)); }

    bool containsSolo(PlanetId pid) const
    { return std::any_of(begin(), end(), samePlanet(pid, true/*notwo*/)); }

    bool contains(const ChartPlanetId& pid) const
    { return std::any_of(begin(), end(), samePlanet(pid, false/*either*/, 
                                                    true/*cmpFID*/)); }

    bool containsSolo(const ChartPlanetId& pid) const
    { return std::any_of(begin(), end(), samePlanet(pid, true/*notwo*/, 
                                                    true/*cmpFID*/)); }

    bool containsMidPt() const
    {
        return std::any_of(begin(), end(),
                           [](const ChartPlanetId& cpid) 
        { return !cpid.isSolo(); });
    }

    QString glyphs() const
    {
        QString res;
        int lastFid = 0;
        foreach(const ChartPlanetId& cpid, *this)
        {
            if (cpid.fileId() != lastFid) res += " : ";
            else if (!res.isEmpty()) res += ",";
            res += cpid.glyph();
            lastFid = cpid.fileId();
        }
        return res;
    }

    QStringList names() const
    {
        QStringList res;
        int lastFid = 0;
        bool ital = false;
        for (const ChartPlanetId& cpid: *this) {
            if (cpid.fileId() != lastFid) ital = !ital;
            if (ital) {
                res << "<i>" + cpid.name() + "</i>";
            } else {
                res << cpid.name();
            }
            lastFid = cpid.fileId();
        }
        return res;
    }

    unsigned pop() const
    {
        unsigned n = 0;
        bool snode = false, nnode = false;
        for (const ChartPlanetId& it: *this) {
            if (it.isSolo()) {
                n += 1;
                if (it.planetId()==Planet_SouthNode) snode = true;
                else if (it.planetId()==Planet_NorthNode) nnode = true;
            } else {
                n += 2;
                if (it.planetId()==Planet_SouthNode
                        || it.planetId2()==Planet_SouthNode) snode = true;
                if (it.planetId()==Planet_NorthNode
                        || it.planetId2()==Planet_NorthNode) nnode = true;
            }
        }
        return (snode && nnode)? n - 1 : n;
    }
};

class ChartPlanetBitmap : public std::map<int, unsigned> {
    static unsigned maxShift()
    {
        static unsigned numBits = sizeof(mapped_type)*8;
        return numBits-1;
    }

public:
    ChartPlanetBitmap() { }

    ChartPlanetBitmap(const PlanetSet& planets)
    { operator|=(planets); }

    ChartPlanetBitmap& operator|=(const PlanetSet& planets)
    { for (const auto& p : planets) operator|=(p); return *this; }

    ChartPlanetBitmap& operator|=(const ChartPlanetId& cpid)
    {
        value_type val(planetBit(cpid)); 
        (*this)[val.first] |= val.second;
        return *this;
    }

    bool operator<(const ChartPlanetBitmap& other) const
    {
        if (empty() > other.empty()) return true;
        if (empty() < other.empty()) return false;
        if (empty() && other.empty()) return false;
        for (auto thit = cbegin(), othit = other.cbegin();
             thit != cend() && othit != other.cend();
             ++thit, ++othit) {
            if (thit->first < othit->first) return true;
            if (thit->first > othit->first) return false;
            if (thit->second < othit->second) return true;
            if (thit->second > othit->second) return false;
        }
        if (size() < other.size()) return true;
        return false;
    }

    bool contains(const ChartPlanetBitmap& other) const
    {
        if (other.empty()) return false;
        for (const auto& val : *this) {
            const auto& othit(other.find(val.first));
            if (othit == other.end()) continue;
            if (othit->second != (val.second & othit->second)) return false;
        }
        return true;
    }

    bool isContainedIn(const ChartPlanetBitmap& other) const
    { return other.contains(*this); }

    static value_type planetBit(const ChartPlanetId& cpid)
    {
        return value_type(cpid.fileId(),
               (1 << (maxShift() - cpid.planetId()))
               | (cpid.isSolo()? 0 : (1 << (maxShift() - cpid.planetId2()))));
    }

    static ChartPlanetId cpid(const value_type& val)
    {
        int num = 0;
        mapped_type v(val.second);
        while (!(v & 1)) { v >>= 1; ++num; }
        return ChartPlanetId(val.first, num, Planet_None);
    }

    void getPlanetSet(PlanetSet& ps) const
    {
        PlanetSet pset;
        int num = 0;
        for (const auto& val : *this) {
            mapped_type v(val.second);
            while (v) {
                if (v & 1) {
                    PlanetId pid = int(maxShift()) - num;
                    pset.insert(ChartPlanetId(val.first,pid,Planet_None));
                }
                v >>= 1;
                ++num;
            }
        }
        ps.swap(pset);
    }

    operator PlanetSet() const
    { 
        PlanetSet pset;
        getPlanetSet(pset);
        return pset;
    }
};

typedef QMap<ChartPlanetId, Planet> ChartPlanetMap;

struct PlanetLoc {
    ChartPlanetId planet;
    qreal loc;

#if 1
    PlanetLoc(int fid, PlanetId p, qreal l) :
        planet(fid, p, Planet_None), loc(l)
    { }
    PlanetLoc(int fid, PlanetId p, PlanetId p2, qreal l) :
        planet(fid, p, p2), loc(l)
    { }
#endif
    PlanetLoc(ChartPlanetId p = 0, qreal l = 0.0) :
        planet(p), loc(l)
    { }

    PlanetLoc(const PlanetLoc& other) :
        planet(other.planet), loc(other.loc)
    { }

    PlanetLoc& operator==(const PlanetLoc& other)
    { planet = other.planet; loc = other.loc; return *this; }

    bool operator<(const PlanetLoc& other) const
    { return loc < other.loc || (loc == other.loc && planet < other.planet); }

    bool operator==(const PlanetLoc& other) const
    { return loc == other.loc && planet == other.planet; }
};

typedef std::set<PlanetLoc> PlanetRange;
typedef std::list<PlanetLoc> PlanetQueue;

class PlanetGroups : public std::map<PlanetSet, PlanetRange> {
public:
    using std::map<PlanetSet, PlanetRange>::insert;

    template <typename T>
    static void getPlanetSet(const T& planets,
                             PlanetSet& plist)
    {
        plist.clear();
        foreach (const PlanetLoc& loc, planets) 
            plist.insert(loc.planet);
    }

    void insert(const PlanetRange& planets)
    {
        PlanetSet plist;
        getPlanetSet(planets, plist);
        insert(value_type(plist, planets));
    }

    void insert(const PlanetQueue& planets,
                unsigned minQuorum = 2);
};

typedef std::map<unsigned, PlanetGroups> PlanetHarmonics;
typedef PlanetHarmonics::iterator HarmonicIter;
typedef PlanetHarmonics::const_iterator HarmonicCIter;

typedef std::multiset<unsigned> uintSet;
typedef uintSet::iterator intIter;
typedef uintSet::const_iterator intCIter;

typedef std::map<unsigned, uintSet> factorMap;
typedef factorMap::iterator factorIter;
typedef factorMap::const_iterator factorCIter;

struct Horoscope
{
    InputData  inputData;
    Zodiac     zodiac;
    Houses     houses, housesOrig;
    AspectList aspects;
    PlanetMap  planets, planetsOrig;
    Planet     sun, moon, mercury, venus,
               mars, jupiter, saturn, uranus,
               neptune, pluto, northNode;
    StarMap    stars;

    ChartPlanetMap getOrigChartPlanets(int fileId) const
    {
        ChartPlanetMap ret;
        foreach (PlanetId pid, planetsOrig.keys()) {
            ret.insert(ChartPlanetId(fileId, pid, Planet_None), 
                       planetsOrig[pid]);
        }
        return ret;
    }
};

void setIncludeAscMC(bool = true);
bool includeAscMC();

void setIncludeChiron(bool = true);
bool includeChiron();

void setIncludeNodes(bool = true);
bool includeNodes();

void setIncludeMidpoints(bool = true);
bool includeMidpoints();

void setRequireAnchor(bool = true);
bool requireAnchor();

void resetPrimeFactorLimit(unsigned = 0);
unsigned primeFactorLimit();

bool isWithinPrimeFactorLimit(unsigned);
void resetPFLCache();

void setFilterFew(bool = true);
bool filterFew();

void setHarmonicsMinQuorum(unsigned);
unsigned harmonicsMinQuorum();

void setHarmonicsMinQOrb(double);
double harmonicsMinQOrb();

void setHarmonicsMaxQuorum(unsigned);
unsigned harmonicsMaxQuorum();

void setHarmonicsMaxQOrb(double);
double harmonicsMaxQOrb();

void setMaxHarmonic(int);
unsigned maxHarmonic();

} // namespace

Q_DECLARE_METATYPE(A::PlanetSet);

#endif // A_DATA_H
