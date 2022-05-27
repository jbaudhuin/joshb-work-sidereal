#ifndef A_DATA_H
#define A_DATA_H

#include <QColor>
#include <QSet>
#include <QString>
#include <QDateTime>
#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QVariant>
#include <QMetaType>
#include <QDebug>
#include <QMutex>

#include <set>
#include <deque>
#include <algorithm>
#include <fstream>

#include <math.h>

enum FileType { TypeOther, TypeMale, TypeFemale, TypeSearch,
                TypeDerivedSA, TypeDerivedProg, TypeDerivedPD,
                TypeDerivedSearch, TypeCount };

namespace A {

// FIXME: move all these to Data or config or something?
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

qreal orbFactor();
void setOrbFactor(qreal ofac);

typedef QMap<unsigned,bool> uintBoolMap;
typedef std::set<unsigned> uintSSet;    /// solo-item sorted set

bool dynAspState(unsigned);
void setDynAspState(unsigned, bool);
uintSSet dynAspState();

QString dtToString(const QDateTime& dt);
QDateTime dateTimeFromJulian(double jd);

inline bool getDynAspState(QVariant& var)
{
    QVariant ret;
    auto asps = dynAspState();
    if (asps.size()==32) ret = "all";
    else {
        QVariantList vl;
        for (auto u : asps) vl << u;
        ret = vl;
    }
    ret.swap(var);
    return true;
}

void setDynAspState(const uintSSet&);

inline void setDynAspState(const QVariant& var)
{
    setDynAspState(uintSSet());
    if (var.isNull()) return;
    if (var.type()==QVariant::String) {
        auto str = var.toString();
        if (str == "all") {
            for (unsigned i = 1; i <= 32; ++i) setDynAspState(i,true);
        }
        return;
    }
    auto type = var.type();
    if (type==QVariant::List || type==QVariant::StringList) {
        for (const auto& v : var.toList()) {
            setDynAspState(v.toUInt(), true);
        }
    }
}

enum HarmonicSort {
    hscByHarmonic,
    hscByPlanets,
    hscByOrb,
    hscByAge
};

enum TransitSort {
    tscByDate,
    tscByTransitPlanet,
    tscByNatalPlanet,
    tscByHarmonic
};

typedef int ZodiacSignId;
typedef int ZodiacId;
typedef int AspectId;
typedef int HouseSystemId;
typedef int PlanetId;
typedef int AspectSetId;


const PlanetId      Planet_None          = -1;
const PlanetId  Planets_Start = 0;
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
const PlanetId  Planet_Ceres = 13;
const PlanetId  Planet_Pallas = 14;
const PlanetId  Planet_Juno = 15;
const PlanetId  Planet_Vesta = 16;
const PlanetId  Planets_End = Planet_Vesta + 1;

const PlanetId Angles_Start = Planets_End;
const PlanetId Planet_Asc = Angles_Start;
const PlanetId Planet_IC = Angles_Start + 1;
const PlanetId Planet_Desc = Angles_Start + 2;
const PlanetId Planet_MC = Angles_Start + 3;
const PlanetId Angles_End = Angles_Start + 4;

const PlanetId Houses_Start = Angles_End;
const PlanetId House_1 = Houses_Start;
const PlanetId House_2 = Houses_Start + 1;
const PlanetId House_3 = Houses_Start + 2;
const PlanetId House_4 = Houses_Start + 3;
const PlanetId House_5 = Houses_Start + 4;
const PlanetId House_6 = Houses_Start + 5;
const PlanetId House_7 = Houses_Start + 6;
const PlanetId House_8 = Houses_Start + 7;
const PlanetId House_9 = Houses_Start + 8;
const PlanetId House_10 = Houses_Start + 9;
const PlanetId House_11 = Houses_Start + 10;
const PlanetId House_12 = Houses_Start + 11;
const PlanetId Houses_End = Houses_Start + 12;

const PlanetId Ingresses_Start = Houses_End;
const PlanetId Ingress_Aries = Ingresses_Start;
const PlanetId Ingress_Taurus = Ingresses_Start + 1;
const PlanetId Ingress_Gemini = Ingresses_Start + 2;
const PlanetId Ingress_Cancer = Ingresses_Start + 3;
const PlanetId Ingress_Leo = Ingresses_Start + 4;
const PlanetId Ingress_Virgo = Ingresses_Start + 5;
const PlanetId Ingress_Libra = Ingresses_Start + 6;
const PlanetId Ingress_Scorpio = Ingresses_Start + 7;
const PlanetId Ingress_Sagittarius = Ingresses_Start + 8;
const PlanetId Ingress_Capricorn = Ingresses_Start + 9;
const PlanetId Ingress_Aquarius = Ingresses_Start + 10;
const PlanetId Ingress_Pisces = Ingresses_Start + 11;
const PlanetId Ingresses_End = Ingresses_Start + 12;

const PlanetId Regresses_Start = Ingresses_End;
const PlanetId Regress_Aries = Regresses_Start;
const PlanetId Regress_Taurus = Regresses_Start + 1;
const PlanetId Regress_Gemini = Regresses_Start + 2;
const PlanetId Regress_Cancer = Regresses_Start + 3;
const PlanetId Regress_Leo = Regresses_Start + 4;
const PlanetId Regress_Virgo = Regresses_Start + 5;
const PlanetId Regress_Libra = Regresses_Start + 6;
const PlanetId Regress_Scorpio = Regresses_Start + 7;
const PlanetId Regress_Sagittarius = Regresses_Start + 8;
const PlanetId Regress_Capricorn = Regresses_Start + 9;
const PlanetId Regress_Aquarius = Regresses_Start + 10;
const PlanetId Regress_Pisces = Regresses_Start + 11;
const PlanetId Regresses_End = Regresses_Start + 12;

const PlanetId Parts_Start = Regresses_End;
const PlanetId Part_of_Fortune = Parts_Start;
const PlanetId Part_of_Spirit = Part_of_Fortune + 1;
const PlanetId Parts_End = Part_of_Spirit + 1;

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
    Zodiac(const Zodiac& zod) :
        id(zod.id), name(zod.name), signs(zod.signs)
    { }

    bool isValid() const { return id != Zodiac_None; }
};

struct HouseSystem {
    HouseSystemId id;
    QString  name;
    char     sweCode;
    QMap<QString, QVariant> userData;

    HouseSystem() { id = Housesystem_None; }
};


struct InputData
{
    QDateTime      GMT;      // greenwich time & date
    QVector3D      location; // x - longitude (-180...180);
                             // y - latitude (-180...180), z - height
    HouseSystemId  houseSystem;
    ZodiacId       zodiac;
    AspectSetId    aspectSet;
    short          tz;
    //double         harmonic;
    //double         RAMC;

    InputData()
    {
        GMT.setTimeSpec(Qt::UTC);
        GMT.setSecsSinceEpoch(0);
        location    = QVector3D(0,0,0);
        houseSystem = Housesystem_Placidus;
        zodiac      = Zodiac_Tropical;
        aspectSet   = AspectSet_Default;
        tz          = 0;
        //harmonic    = 1;
    }

    //void            computeRAMC() { }
};

typedef std::pair<FileType, InputData> FileInput;
typedef std::vector<FileInput> FileInputs;

struct Houses
{
    double  cusp[12];   // angles of house cusps (0... 360)
    double  Asc, MC, RAMC, RAAC, RADC, OAAC, ODDC;
    double  Vx, EA, startSpeculum;
    double  halfMedium, halfImum;
    const HouseSystem* system;

    Houses()
    {
        for (auto& c : cusp) c = 0;
        system = nullptr;
        Asc = MC = RAMC = RAAC = RADC = OAAC = ODDC, Vx = EA =
            startSpeculum = 0;
        halfMedium = halfImum = 0;
    }

    Houses(const InputData& id);
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

    QPointF horizontalPos;       // x - azimuth (0... 360), y - height (0... 360)
    QPointF eclipticPos;         // x - longitude (0... 360), y - latitude (0... 360)
    QPointF equatorialPos;       // x - rectascension, y - declination
    double  distance;            // A.U. (astronomical units)
    qreal   pvPos;
    int     house;

    Star() : angleTransit(4)
    {
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
  int            dignity = 0;
  int            deficient = 0;

  PlanetPower() { }
};

enum PlanetPosition {
    Position_Normal,
    Position_Exaltation,
    Position_Dwelling,
    Position_Downfall,
    Position_Exile
};

struct Planet : public Star
{
    int                 sweNum = 0;
    bool                isReal = false;
    QVector2D           defaultEclipticSpeed = QVector2D(0,0);
    QList<ZodiacSignId> homeSigns, exaltationSigns, exileSigns, downfallSigns;

    QVector2D           eclipticSpeed = QVector2D(0,0);  // dx - (degree/day)
    QVector2D           equatorialSpeed = QVector2D(0,0);

    double              elongation = 0.0;
    double              phaseAngle = 0.0;
    PlanetPosition      position = Position_Normal;
    PlanetPower         power;
    const ZodiacSign  * sign = nullptr;
    QList<int>          houseRuler = { };

    Planet() { }

    Planet(PlanetId pid, const QString& pname, const QVariant& fontChar)
    { id = pid; name = pname; userData["fontChar"] = fontChar; }

    Planet& operator=(const Planet& other)
    {
        Star::operator=(other);
        sweNum = other.sweNum;
        isReal = other.isReal;
        defaultEclipticSpeed = other.defaultEclipticSpeed;
        homeSigns = other.homeSigns;
        exaltationSigns = other.exaltationSigns;
        exileSigns = other.exileSigns;
        downfallSigns = other.downfallSigns;
        eclipticSpeed = other.eclipticSpeed;
        elongation = other.elongation;
        position = other.position;
        power = other.power;
        sign = other.sign;
        houseRuler = other.houseRuler;
        return *this;
    }

    operator Planet*() { return this; }
    operator const Planet*() const { return this; }

    PlanetId            getPlanetId() const { return id; }
    virtual int         getSWENum() const { return sweNum; }
    virtual bool        isStar() const { return false; }
    //virtual bool      isAsteroid() const { return false; }
    virtual bool        isPlanet() const { return true; }

    double              getPrefPos() const;
    double              getPrefSpd() const;

    bool operator==(const Planet & other) const
    { return this->id == other.id && this->eclipticPos == other.eclipticPos; }
    bool operator!=(const Planet & other) const
    { return this->id != other.id || this->eclipticPos != other.eclipticPos; }
};

struct AspectsSet;

struct AspectType {
    AspectId    id;
    const AspectsSet* set;
    QString     name;
    float       angle;
    float       _orb;
    unsigned    _harmonic = 0;
    std::set<unsigned> factors;
    bool        _enabled;
    QMap<QString, QVariant> userData;

    AspectType() :
        id(Aspect_None),
        set(nullptr),
        angle(0),
        _orb(0),
        _enabled(true)
    { }

    float orb() const
    {
        if (!_harmonic) return _orb * orbFactor();
        return harmonicsMaxQOrb()/_harmonic * orbFactor();
    }

    bool isEnabled() const { return _enabled; }
    void setEnabled(bool b = true) { _enabled = b; }
};

struct Aspect {
    const AspectType* d;
    const Planet* planet1;
    const Planet* planet2;
    float         angle;
    float         orb;
    bool          applying;

    Aspect() :
        d(nullptr),
        planet1(nullptr),
        planet2(nullptr),
        angle(0),
        orb(0),
        applying(false)
    { }

    ~Aspect() { }
};

struct AspectsSet {
    AspectSetId id;
    QString name;
    QMap<AspectId, AspectType> aspects;
    bool isEmpty() const { return aspects.isEmpty(); }

    AspectsSet(AspectSetId aid = AspectSet_Default,
               const QString& n = "default") :
        id(aid), name(n)
    { }

    ~AspectsSet() { }
};


typedef QList<Aspect> AspectList;
typedef QList<Planet> PlanetList;

class PlanetMap : public QMap<PlanetId, Planet> {
public:
    using Base = QMap<PlanetId, Planet>;
    using Base::Base;

#if 0
    using Base::keys;
    using Base::count;
#endif

    Base::mapped_type& operator[](const Base::key_type& key)
    {
        return Base::operator[](key);
    }

    const Base::mapped_type& operator[](const Base::key_type& key) const
    {
        return const_cast<PlanetMap*>(this)->Base::operator[](key);
    }
};

typedef QMap<std::string, Star> StarMap;
typedef std::pair<int, QString> GlyphName;

const PlanetMap& getPlanetMap();

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

    static QMap<PlanetId, GlyphName> signInfo;

    friend const PlanetMap& A::getPlanetMap();

public:
    static void load(QString language);
    static const QString usedLanguage() { return usedLang; }

    static const Planet& getPlanet(PlanetId id);
    static int getSignGlyph(PlanetId id);
    static QString getSignName(PlanetId id);

    static QList<PlanetId> getPlanets(bool includeAsteroids,
                                      bool includeCentaurs);
    static QList<PlanetId> getAngles();
    static QList<PlanetId> getInnerPlanets(bool includeAsteroids = false);
    static QList<PlanetId> getOuterPlanets(bool includeCentaurs = true);
    static QList<PlanetId> getSignIngresses();
    static QList<PlanetId> getHouses();
    static QList<PlanetId> getNonAngularHouses();

    static const Star& getStar(const QString& name);
    static const QList<QString>& getStars();

    static const HouseSystem& getHouseSystem(HouseSystemId id);
    static const QList<HouseSystem> getHouseSystems();

    static const Zodiac& getZodiac(ZodiacId id);
    static const QList<Zodiac> getZodiacs();

    static double getSignPos(ZodiacId zid, const QString& sign);

    static QColor getHarmonicColor(unsigned h);
    static const QMap<unsigned,QColor>& getHarmonicColors();

    static const AspectType& getAspect(AspectId id, const AspectsSet& set);
    //static const QList<AspectType> getAspects(AspectSetId set);

    static QList<AspectsSet> getAspectSets() { return aspectSets.values(); }
    static AspectsSet& getAspectSet(AspectSetId set);
    static const AspectsSet& topAspectSet() { return aspectSets[topAspSet]; }
    static const AspectsSet& tightConjunction();
};

class ChartPlanetId {
    int _fid;
    PlanetId _pid, _pid2;
    bool _oppMidpt;

public:
    ChartPlanetId(PlanetId planetId = Planet_None,
                  PlanetId planetId2 = Planet_None) :
        _fid(0), _pid(planetId), _pid2(planetId2), _oppMidpt(false)
    {
        if (_pid2 != Planet_None && _pid > _pid2) {
            _oppMidpt = true;
            std::swap(_pid, _pid2);
        }
    }

    ChartPlanetId(int fileId, PlanetId planetId, PlanetId planetId2) :
        _fid(fileId), _pid(planetId), _pid2(planetId2), _oppMidpt(false)
    {
        if (_pid2 != Planet_None && _pid > _pid2) {
            _oppMidpt = true;
            std::swap(_pid, _pid2);
        }
    }

    ChartPlanetId(const ChartPlanetId& otro) :
        _fid(otro._fid), _pid(otro._pid), _pid2(otro._pid2),
        _oppMidpt(otro._oppMidpt)
    { }

    QString name() const;
    QString glyph() const;

    bool contains(PlanetId pid) const
    { return _pid == pid || _pid2 == pid; }

    bool isMidpt() const { return _pid2 != Planet_None; }
    bool isOppMidpt() const { return isMidpt() && _oppMidpt; }
    bool isSolo() const { return !isMidpt(); }

    bool samePlanetDifferentChart(const ChartPlanetId& cpid) const
    { return _fid != cpid._fid && _pid==cpid._pid && _pid2 == cpid._pid2; }

    bool samePlanet(const ChartPlanetId& cpid) const
    { return _pid==cpid._pid && _pid2==cpid._pid2; }

    bool operator==(const ChartPlanetId& cpid) const
    { return _fid == cpid._fid && _pid == cpid._pid && _pid2 == cpid._pid2; }

    bool operator!=(const ChartPlanetId& cpid) const
    { return _fid != cpid._fid || _pid != cpid._pid || _pid2 != cpid._pid2; }

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
        if (_fid < cpid._fid) return true;
        if (_fid > cpid._fid) return false;
        if (isMidpt() < cpid.isMidpt()) return true;
        if (isMidpt() > cpid.isMidpt()) return false;
        if (isMidpt()) {
            if (_oppMidpt < cpid._oppMidpt) return true;
            if (_oppMidpt > cpid._oppMidpt) return false;
        }
        if (_pid < cpid._pid) return true;
        if (_pid > cpid._pid) return false;
        if (isMidpt()) {
            if (_pid2 < cpid._pid2) return true;
            if (_pid2 > cpid._pid2) return false;
        }
        return false;
#endif
    }

    bool operator<=(const ChartPlanetId& cpid) const 
    { return operator==(cpid) || operator<(cpid); }

    void setFileId(int fid) { _fid = fid; }
    int fileId() const { return _fid; }
    PlanetId planetId() const { return _pid; }

    ChartPlanetId chartPlanetId1() const
    { return ChartPlanetId(_fid, _pid, Planet_None); }

    PlanetId planetId2() const { return _pid2; }

    ChartPlanetId chartPlanetId2() const
    { return ChartPlanetId(_fid, _pid2, Planet_None); }

    bool operator==(PlanetId p) const
    { return _pid == p && (isSolo() || _pid2 == p); }

    bool operator!=(PlanetId p) const
    { return _pid != p && (isSolo() || _pid2 != p); }

    bool operator<(PlanetId p) const
    { return _pid < p && (isSolo() || _pid2 < p); }

    bool operator>(PlanetId p) const
    { return _pid > p && (isSolo() || _pid2 > p); }

    bool operator<=(PlanetId p) const
    { return _pid <= p && (isSolo() || _pid2 <= p); }

    bool operator>=(PlanetId p) const
    { return _pid >= p && (isSolo() || _pid2 >= p); }

    operator PlanetId() const { return _pid; }
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
    using Base = std::set<ChartPlanetId>;
    using Base::Base;

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

    bool containsAny(const PlanetSet& pset) const
    {
        for (const auto &cpid: pset) if (contains(cpid)) return true;
        return false;
    }

    bool containsAny(PlanetId start, PlanetId beyond)
    {
        return std::any_of(begin(), end(),
                           [&](const ChartPlanetId& cpid)
        { return cpid.planetId() >= start && cpid.planetId() < beyond; });
    }

    bool containsMidPt() const
    {
        return std::any_of(begin(), end(),
                           [](const ChartPlanetId& cpid) 
        { return !cpid.isSolo(); });
    }

    QString glyphs() const
    {
        QString res;
        int lastFid = empty()? 0 : begin()->fileId();
        for (const ChartPlanetId& cpid: *this) {
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
             ++thit, ++othit)
        {
            if (thit->first < othit->first) return true;
            if (thit->first > othit->first) return false;
            if (thit->second < othit->second) return true;
            if (thit->second > othit->second) return false;
        }
        return (size() < other.size());
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
                          | (cpid.isSolo()? 0
                                          : (1 << (maxShift()
                                                   - cpid.planetId2()))));
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
typedef std::map<ChartPlanetId, const Planet*> ChartPlanetPtrMap;

struct Loc {
    QString desc;
    qreal loc;
    qreal speed;

    Loc(qreal l = 0, qreal s = 0) : loc(l), speed(s) { }
    Loc(const QString& description, qreal l = 0, qreal s = 0) :
        desc(description), loc(l), speed(s)
    { }

    Loc(const Loc& other) :
        desc(other.desc),
        loc(other.loc),
        speed(other.speed)
    { }

    virtual ~Loc() { }

    operator Loc const*() const { return this; }

    virtual Loc* clone() const { return new Loc(*this); }

    virtual qreal defaultSpeed() const { return 0; }
    virtual qreal operator()(double /*jd*/, int /*h*/) { return loc; }
    virtual bool inMotion() const { return false; }
    virtual QString description() const { return desc; }

    virtual const InputData& input() const
    { static InputData dummy; return dummy; }
};

struct PlanetLoc : public Loc {
    ChartPlanetId planet;
    qreal _rasiLoc = 0;

    enum { aspAll, aspOnlyConj, aspOnlyRetro, aspOnlyDirect };
    unsigned allowAspects = aspAll;

#if 1
    PlanetLoc(int fid, PlanetId p, qreal l, qreal s = 0) :
        Loc(l,s), planet(fid, p, Planet_None), _rasiLoc(l)
    { }
    PlanetLoc(int fid, PlanetId p, PlanetId p2, qreal l, qreal s = 0) :
        Loc(l,s), planet(fid, p, p2), _rasiLoc(l)
    { }
#endif
    PlanetLoc(ChartPlanetId p = 0, qreal l = 0, qreal s = 0) :
        Loc(l,s), planet(p), _rasiLoc(l)
    { }

    PlanetLoc(const ChartPlanetId& p, const QString& desc, qreal l=0, qreal s=0) :
        Loc(l,s), planet(p), _rasiLoc(l)
    { if (!desc.isEmpty()) this->desc = desc; }

    PlanetLoc(const PlanetLoc& other) :
        Loc(other), planet(other.planet),
        _rasiLoc(other._rasiLoc),
        allowAspects(other.allowAspects)
    { }

    qreal rasiLoc() const { return _rasiLoc; }

    Loc* clone() const override { return new PlanetLoc(*this); }

    PlanetLoc& operator==(const PlanetLoc& other)
    { planet = other.planet; loc = other.loc; return *this; }

    bool operator<(const PlanetLoc& other) const
    { return loc < other.loc || (loc == other.loc && planet < other.planet); }

    bool operator==(const PlanetLoc& other) const
    { return loc == other.loc && planet == other.planet; }

    qreal compute(const InputData& ida);
    qreal compute(const InputData& ida, double jd, int h);
    static std::pair<qreal,qreal> compute(const ChartPlanetId& pid,
                                          const InputData& ida,
                                          double jd);

    QString description() const override
    { return desc.isEmpty()? planet.name() : (planet.name() + "-" + desc) ; }

    virtual qreal defaultSpeed() const override;
};

typedef std::list<Loc*> Locs;

struct ClusterOrbWhen {
    qreal orb;
    qreal when;

    ClusterOrbWhen() : orb(), when() { }
    ClusterOrbWhen(qreal orb) : orb(orb), when() { }
    ClusterOrbWhen(qreal orb, qreal when) : orb(orb), when(when) { }

    operator QString() const
    {
        if (orb == qreal() && when == qreal()) return "0:0";
        if (when == qreal()) return QString::number(orb);
        return QString("%1 at %2").arg(orb)
                .arg(dtToString(dateTimeFromJulian(when)));
    }
};

inline
std::ostream &
operator<<(std::ostream& os, const ClusterOrbWhen& cow)
{ return os << QString(cow).toLocal8Bit().constData(); }

typedef std::map<PlanetSet, ClusterOrbWhen> PlanetClusterMap;
typedef std::map<unsigned, PlanetClusterMap> HarmonicPlanetClusters;

class NatalLoc : public PlanetLoc {
public:
    NatalLoc(const ChartPlanetId& cpid,
             const InputData& ida) :
        PlanetLoc(cpid)
    { compute(ida); speed = 0; }

    Loc* clone() const override { return new NatalLoc(*this); }
};

class InputPosition : public PlanetLoc {
public:
    InputPosition(const ChartPlanetId& cpid,
                  const InputData& ida,
                  const QString& tag = "") :
        PlanetLoc(cpid, tag), _ida(ida)
    { }

    const InputData& input() const override { return _ida; }

protected:
    const InputData& _ida;
};

class KnownPosition : public PlanetLoc {
    qreal jd;
public:
    KnownPosition(const ChartPlanetId& cpid,
                  qreal loc,
                  double jd,
                  const QString& tag = "") :
        PlanetLoc(cpid, tag, loc), jd(jd)
    { }

    KnownPosition(const PlanetLoc* ploc,
                  double jd,
                  const QString& tag = "") :
        PlanetLoc(*ploc), jd(jd)
    { if (!tag.isEmpty()) Loc::desc = tag; }

    qreal julianDate() const { return jd; };

    Loc* clone() const override { return new KnownPosition(*this); }
};

class NatalPosition : public InputPosition {
public:
    NatalPosition(const ChartPlanetId& cpid,
                  const InputData& ida,
                  const QString& tag = "") :
        InputPosition(cpid, ida, tag)
    { compute(ida); _rasiLoc = loc; speed = 0; }

    Loc* clone() const override
    { return new NatalPosition(*this); }

    qreal operator()(double, int h) override
    { return loc = h==1? _rasiLoc : fmod(_rasiLoc*h,360.); }
};

class TransitPosition : public InputPosition {
public:
    TransitPosition(const ChartPlanetId& cpid,
                    const InputData& ida,
                    const QString& tag = "") :
        InputPosition(cpid, ida, tag)
    { }

    TransitPosition(const ChartPlanetId& cpid,
                    const InputData& ida,
                    double jd) :
        InputPosition(cpid, ida)
    { compute(input(), jd, -1); }

    Loc* clone() const override { return new TransitPosition(*this); }

    bool inMotion() const override { return true; }

    qreal operator()(double jd, int h) override
    { return compute(input(), jd, h); }
};

class ProgressedPosition : public InputPosition {
    double _njd;
public:
    ProgressedPosition(const ChartPlanetId& cpid,
                       const InputData& ida,
                       double njd,
                       const QString& tag = "") :
        InputPosition(cpid, ida, tag), _njd(njd)
    { }
};

class SolarArcPosition : public InputPosition {
public:
};

template <typename T>
qreal
getSpread(const T& range)
{
    qreal lo = getLoc(*range.cbegin());
    qreal hi = getLoc(*range.crbegin());
    if (hi - lo > A::harmonicsMaxQOrb()) {
        auto lit = range.cbegin();
        while (++lit != range.cend()) {
            if (getLoc(*lit) - lo > A::harmonicsMaxQOrb()) {
                hi = lo;
                lo = getLoc(*lit);
                break;
            } else {
                lo = getLoc(*lit);
            }
        }
    }
    qreal ret = qAbs(hi - lo);
    return ret>180? 360-ret : ret;
}

class PlanetProfile :
        public Loc,
        public std::deque<Loc*>
{
    bool _forceMinimize = false;

public:
    typedef std::deque<Loc*> Base;
    using Base::Base;

    PlanetProfile() { }
    PlanetProfile(std::initializer_list<Loc*> locs) : Base(locs) { }
    PlanetProfile(std::initializer_list<QMap<int, Planet> *> pms);

    PlanetProfile(const PlanetProfile& other) :
        Loc(other),
        Base()
    { for (auto oloc : other) emplace_back(oloc->clone()); }

    PlanetProfile(PlanetProfile&& other) :
        Loc(other),
        Base(std::move(other))
    { }

    virtual ~PlanetProfile() { qDeleteAll(*this); }

    PlanetProfile& operator=(PlanetProfile&& other)
    {
        Loc::loc = other.loc;
        Loc::speed = other.Loc::speed;
        Base::operator=(std::move(other));
        return *this;
    }

    PlanetProfile* profile(PlanetSet psp) const
    {
        auto ret = new PlanetProfile;
        for (auto loc : *this) {
            if (auto ploc = dynamic_cast<const PlanetLoc*>(loc)) {
                auto psit = psp.find(ploc->planet);
                if (psit != psp.end()) {
                    ret->emplace_back(ploc->clone());
                    psp.erase(psit);
                    if (psp.empty()) break;
                }
            }
        }
        return ret;
    }

    qreal defaultSpeed() const
    {
        // FIXME should base on synodic cycle? Compounded, this
        // could be a high value, but harmonically, it will be rather
        // less
        int i = 0;
        qreal spd = 0;
        for (auto& pl : *this) {
            if (pl->inMotion()) {
                ++i;
                spd += pl->defaultSpeed();
            }
        }
        return i>1? spd/float(i) : spd;
    }

    qreal speed() const { return Loc::speed; }

    qreal computeSpread(double jd);
    qreal computeSpread();

    static std::pair<qreal, qreal> computeDelta(const Loc* a,
                                                const Loc* b,
                                                unsigned int harmonic = 1);
    static std::pair<qreal, qreal> computeSpread(std::initializer_list<const Loc *>,
                                                 unsigned int =1);

    qreal computePos(double jd, unsigned int harmonic = 1);

    qreal operator()(double jd) { return computePos(jd); }

    void setForceMinimize(bool b = true) { _forceMinimize = b; }

    bool needsFindMinimalSpread() const
    { return _forceMinimize || size() > 2; }
};

struct BySpeed {
    bool operator()(const PlanetLoc& a,
                    const PlanetLoc& b) const
    { return std::abs(a.speed) > std::abs(b.speed); }
};

typedef std::set<PlanetLoc> PlanetRange;
typedef std::set<PlanetLoc,BySpeed> PlanetRangeBySpeed;

struct PlanetClusterLess {
    bool _fast;

    PlanetClusterLess(bool fast = true) : _fast(fast) { }

    static int fileId(const ChartPlanetId& cpid)
    { return cpid.fileId(); }

    static int fileId(const PlanetLoc& ploc)
    { return ploc.planet.fileId(); }

    static int fileId(const Loc&)
    { return -1; }

    static const ChartPlanetId& planet(const ChartPlanetId& cpid)
    { return cpid; }

    static const ChartPlanetId& planet(const PlanetLoc& ploc)
    { return ploc.planet; }

    static const ChartPlanetId& planet(const Loc&)
    { static ChartPlanetId none; return none; }

    template <typename T>
    bool less(T ait, T aend, T bit, T bend) const
    {
        //if (aended() != bended()) return aent > bent;
        auto f = fileId(*ait);
        auto g = fileId(*bit);
        if (f != g) return f < g;
        while (ait != aend && bit != bend
               && fileId(*ait) == f
               && fileId(*bit) == f)
        {
            if (planet(*ait) != planet(*bit)) return planet(*ait) < planet(*bit);
            ++ait; ++bit;
        }
        return false;
    }

    bool operator()(const PlanetRangeBySpeed& a,
                    const PlanetRangeBySpeed& b) const
    {
        return _fast? less(a.begin(), a.end(), b.begin(), b.end())
                    : less(a.rbegin(), a.rend(), b.rbegin(), b.rend());
    }

    bool operator()(const PlanetSet& a,
                    const PlanetSet& b)
    {
        return _fast? less(a.rbegin(), a.rend(), b.rbegin(), b.rend())
                    : less(a.begin(), a.end(), b.begin(), b.end());
    }
};

typedef std::list<PlanetLoc> PlanetQueue;

inline qreal getLoc(const Loc& loc) { return loc.loc; }
inline qreal getLoc(const Loc* loc) { return loc->loc; }


class PlanetGroups : public std::map<PlanetSet, PlanetRange> {
public:
    using std::map<PlanetSet, PlanetRange>::insert;

    template <typename T>
    static void getPlanetSet(const T& planets,
                             PlanetSet& plist)
    {
        plist.clear();
        for (const PlanetLoc& loc : planets)
            plist.insert(loc.planet);
    }

    void insert(const PlanetRange& planets)
    {
        PlanetSet plist;
        getPlanetSet(planets, plist);
        insert(value_type(plist, planets));
    }

    void insert(const PlanetQueue& planets, unsigned minQuorum = 2);
};

enum DerivedEventFlag {
    etcNoDerivedEvent  = 0,
    etcEventToTransitAspect = 1,    // *T
    etcEventToNatalAspect   = 2,    // *N
    etcEventTransitToTransitAspect = 4, // *TT
    etcEventTransitToNatalAspect = 8, // *TN
    etcEventAspectPattern   = 16,    // *A
    etcEventTypeStart       = etcEventAspectPattern << 1,
    etcDerivedEventMask     = etcEventTypeStart - 1
};
Q_DECLARE_FLAGS(DerivedEventFlags, DerivedEventFlag);

enum EventType {
    etcUnknownEvent         = 0,
    etcStation              = etcEventTypeStart, // S
    etcTransitToStation,        // T=S
    etcTransitToTransit,        // T=T
    etcTransitToNatal,          // T=N
    etcOuterTransitToNatal,     // OT=N
    etcReturn,                  // R
    etcProgressedToProgressed,  // P=P
    etcProgressedToNatal,       // P=N
    etcInnerProgressedToNatal,  // IP=N
    etcTransitToProgressed,     // T=P
    etcSolarArcToNatal,         // D=N
    etcSignIngress,             // T=I
    etcHouseIngress,            // T=H
    etcLunation,                // L
    etcEclipse,                 // E
    etcSolarEclipse,            // SE
    etcLunarEclipse,            // LE
    etcHeliacalEvents,          // HRS
    etcTransitAspectPattern,    // TA
    etcTransitNatalAspectPattern,   // TNA
    etcUserEventStart
};

enum EventUpdateType {
    etcMerge,
    etcUpdate
};

template <typename T>
struct ARange : public QPair<T,T> {
    typedef QPair<T,T> Base;
    using Base::Base;
    using Base::first;
    using Base::second;

    ARange() : Base() { }

    ARange(QVariant& v)
    {
        QVariantList vl = v.toList();
        first = vl.takeFirst().value<T>();
        second = vl.takeFirst().value<T>();
    }

    ARange& operator=(const QVariant& v)
    {
        if (v.isNull() || v.type() != QVariant::List) {
            first = T();
            second = T();
        } else {
            QVariantList vl = v.toList();
            first = vl.takeFirst().value<T>();
            second = vl.takeFirst().value<T>();
        }
        return *this;
    }

    bool contains(const T& d) const
    { return d >= first && d <= second; }

    operator QVariant() const
    { QVariantList vl; vl << first << second; return vl; }
};

typedef ARange<QDate> ADateRange;
typedef ARange<QDateTime> ADateTimeRange;

typedef std::pair<unsigned, PlanetSet> HarmonicPlanetSet;
typedef std::pair<double, double> JDateRange;
typedef std::set<JDateRange> JDateRanges;
typedef std::map<HarmonicPlanetSet, JDateRange> HarmonicPlanetDateRangeMap;
typedef std::map<HarmonicPlanetSet, JDateRanges> HarmonicPlanetDateRangesMap;

class HarmonicAspect {
    unsigned            _eventType;  ///< type of event
    unsigned char       _harmonic;   ///< harmonic of aspect (or 1)
    PlanetSet           _pattern;    ///< pattern
    PlanetRangeBySpeed  _locations;  ///< locations of planets
    qreal               _orb;        ///< orb of aspect or span of assembly

public:
    HarmonicAspect(unsigned              et = 0,
                   unsigned char         h = 0,
                   PlanetRangeBySpeed && pr = { },
                   qreal                 delta = 0.0) :
        _eventType(et),
        _harmonic(h),
        _locations(pr),
        _orb(delta)
    {
        for (const auto& loc: _locations) {
            if (const auto ploc = dynamic_cast<const PlanetLoc*>(&loc))
                _pattern.insert(ploc->planet);
        }
    }

    HarmonicAspect(unsigned         et,
                   unsigned char    h,
                   PlanetSet     && ps,
                   qreal            delta = 0.0) :
        _eventType(et),
        _harmonic(h),
        _pattern(ps),
        _orb(delta)
    { }

    void reset()
    {
        _eventType = etcUnknownEvent;
        _harmonic = 0;
        _locations.clear();
        _pattern.clear();
        _orb = 0.;
    }

    void reset(PlanetRangeBySpeed&& pr,
               qreal delta = 0.0)
    {
        _locations = std::move(pr);
        _pattern.clear();
        for (const auto& loc: _locations) {
            if (const auto ploc = dynamic_cast<const PlanetLoc*>(&loc))
                _pattern.insert(ploc->planet);
        }
        _orb = delta;
    }

    void reset(PlanetSet&& ps,
               qreal delta = 0.0)
    {
        _locations.clear();
        _pattern = std::move(ps);
        _orb = delta;
    }

    unsigned int eventType() const { return _eventType; }
    unsigned int harmonic() const { return _harmonic; }
    qreal orb() const { return _orb; }
    const PlanetRangeBySpeed& locations() const { return _locations; }

    const PlanetSet& planets() const
    { return _pattern; }

    bool operator==(const HarmonicAspect& asp) const
    {
        return _harmonic == asp._harmonic
                && _eventType == asp._eventType
                && planets() == asp.planets();
    }

    friend class HarmonicEvent;
};

typedef std::list<HarmonicAspect> HarmonicAspects;

 class HarmonicEvent : public HarmonicAspect {
    QDateTime       _dateTime;      ///< time of event in UTC
    ADateTimeRange  _range;
    HarmonicAspects _coincidences;  ///< coincident events

public:
    HarmonicEvent(const QDateTime     & dt,
                  unsigned              et,
                  unsigned char         h,
                  PlanetRangeBySpeed && pr = { },
                  qreal                 delta = 0.0) :
        HarmonicAspect(et, h, std::move(pr), delta),
        _dateTime(dt)
    { }

    HarmonicEvent(const ADateTimeRange& range,
                  unsigned              et,
                  unsigned char         h,
                  PlanetSet          && ps,
                  qreal                 delta = 0.0) :
        HarmonicAspect(et, h, std::move(ps), delta),
        _range(range)
    { }

    HarmonicEvent(const QDateTime& dt = { }) :
        HarmonicAspect(),
        _dateTime(dt)
    { }

    void clear()
    {
        reset();
        _dateTime = { };
        _coincidences.clear();
    }

    using HarmonicAspect::reset;

    template <typename ...Args> void reset(const QDateTime& dt,
                                           Args&&... args)
    {
        _dateTime = dt;
        HarmonicAspect::reset(std::forward<Args>(args)...);
    }

    void reset(const QDateTime& dt, qreal delta)
    {
        _dateTime = dt;
        _orb = delta;
    }

    QDateTime& dateTime() { return _dateTime; }
    const QDateTime& dateTime() const { return _dateTime; }

    const ADateTimeRange& range() const { return _range; }
    void setRange(const ADateTimeRange& r) { _range = r; }

    HarmonicAspects& coincidences() { return _coincidences; }
    const HarmonicAspects& coincidences() const { return _coincidences; }

    const HarmonicAspect& coincidence(int n) const
    {
        static HarmonicAspect dummy;
        if (n < 0 || unsigned(n) >= _coincidences.size()) return dummy;
        auto it = _coincidences.begin();
        if (n==0) return *it;
        return *std::next(it, n);
    }

    operator const HarmonicEvent*() const { return this; }

    bool isNull() const
    { return operator==(HarmonicEvent()); }

    bool operator==(const HarmonicEvent& ev) const
    {
        return _dateTime == ev._dateTime
                && HarmonicAspect::operator==(ev);
    }
};

typedef std::list<HarmonicEvent> HarmonicEventsBase;

class HarmonicEvents : public HarmonicEventsBase {
public:
    using HarmonicEventsBase::HarmonicEventsBase;

    bool syncDateRange = true;
    unsigned eventsType = etcUnknownEvent;

    HarmonicEvents() { }

    HarmonicEvents(const HarmonicEvents& other) :
        HarmonicEventsBase(other),
        syncDateRange(other.syncDateRange),
        eventsType(other.eventsType)
    { }

    HarmonicEvents& operator=(const HarmonicEvents& other)
    {
        HarmonicEventsBase& me(*this);
        const HarmonicEventsBase& you(other);
        me = you;
        syncDateRange = other.syncDateRange;
        eventsType = other.eventsType;
        return *this;
    }

    operator const HarmonicEvents*() const { return this; }

    QMutex mutex;
};

typedef std::set<ADateRange> ADateRangeSet;
struct EventStoreData {
    ADateRangeSet   ranges;
    HarmonicEvents  events;
};

struct EventUpdateData {
    ADateRangeSet   ranges;
    HarmonicEvents& events;
};

struct EventScope {
    unsigned        eventType;
    ADateRange      range;
};

// @todo need to include harmonic list, right?

typedef QMap<unsigned, EventStoreData> EventStoreBase;
class EventStore : public EventStoreBase {
public:
    using EventStoreBase::EventStoreBase;

    class noNeed : public std::exception {
    public:
        using std::exception::exception;
    };

    EventUpdateData getEventUpdateScope(EventScope evscope,
                                        EventUpdateType uptype);
};

typedef std::map<unsigned, PlanetGroups> PlanetHarmonics;
typedef PlanetHarmonics::iterator HarmonicIter;
typedef PlanetHarmonics::const_iterator HarmonicCIter;

typedef std::multiset<unsigned> uintMSet;   /// Multi-item sorted set
typedef uintMSet::iterator intIter;
typedef uintMSet::const_iterator intCIter;

typedef std::map<unsigned, uintMSet> factorMap;
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
    double     harmonic = 1.0;

    ChartPlanetMap getOrigChartPlanets(int fileId) const
    {
        ChartPlanetMap ret;
        for (PlanetId pid : planetsOrig.keys()) {
            ret.insert(ChartPlanetId(fileId, pid, Planet_None), 
                       planetsOrig[pid]);
        }
        return ret;
    }

    const Planet* getPlanet(PlanetId pid) const
    { return &const_cast<Horoscope*>(this)->planets[pid]; }
};

void load(QString language);
QString usedLanguage();

inline QColor getHarmonicColor(unsigned h)
{ return Data::getHarmonicColor(h); }

const Planet& getPlanet(PlanetId pid);
PlanetId getPlanetId(const QString& name);
double getSignPos(ZodiacId zid,
                  const QString& sign,
                  unsigned degrees = 0,
                  unsigned minutes = 0,
                  unsigned seconds = 0);
QString getPlanetName(const ChartPlanetId& id);
QString getPlanetGlyph(const ChartPlanetId& id);

inline QList<PlanetId> getPlanets(bool includeAsteroids = false,
                                  bool includeCentaurs = true)
{ return Data::getPlanets(includeAsteroids, includeCentaurs); }

inline QList<PlanetId> getAngles() { return Data::getAngles(); }

inline QList<PlanetId> getInnerPlanets(bool includeAsteroids)
{ return Data::getInnerPlanets(includeAsteroids); }

inline QList<PlanetId> getOuterPlanets(bool includeCentaurs)
{ return Data::getOuterPlanets(includeCentaurs); }

inline QList<PlanetId> getSignIngresses() { return Data::getSignIngresses(); }
inline QList<PlanetId> getHouses() { return Data::getHouses(); }
const Star& getStar(const QString& name);
const QList<QString>& getStars();
const HouseSystem& getHouseSystem(HouseSystemId id);
const Zodiac& getZodiac(ZodiacId id);
const QList<HouseSystem> getHouseSystems();
const QList<Zodiac> getZodiacs();
//const QList<AspectType> getAspects(AspectSetId set);
const AspectType& getAspect(AspectId id, const AspectsSet& set);
QList<AspectsSet> getAspectSets();
AspectsSet& getAspectSet(AspectSetId set);
const AspectsSet& topAspectSet();
const AspectsSet& tightConjunction();

} // namespace

Q_DECLARE_METATYPE(A::PlanetSet);

#endif // A_DATA_H
