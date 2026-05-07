#ifndef A_DATA_H
#define A_DATA_H

#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QMetaType>
#include <QMutex>
#include <QPointF>
#include <QRunnable>
#include <QSet>
#include <QString>
#include <QTimeZone>
#include <QVariant>
#include <QVector2D>
#include <QVector3D>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <deque>
#include <fstream>
#include <set>

#include <math.h>

enum FileType {
    TypeOther,
    TypeEvent,
    TypeMale,
    TypeFemale,
    TypeReturn,
    TypeDerivedProg,
    TypeSearch,
    TypeDerivedSA,
    TypeDerivedPD,
    TypeDerivedSearch,
    TypeParan,
    TypeCount
};

#if QT_VERSION < QT_VERSION_CHECK(6, 7, 0)
#define VAR_TYPE(var) var.type()
#define VAR_TYPE_CHECK(var, val) (var.type() == QMetaType::val)
#define VAR_QTYPE_CHECK(var, val) (var.type() == QMetaType::val)
#define CHECK_VARTYPE(type, val) (type == QMetaType::val)
#define CHECK_VARQTYPE(type, val) (type == QMetaType::val)
#else
#define VAR_TYPE(var) var.typeId()
#define VAR_TYPE_CHECK(var, val) (var.typeId() == QMetaType::val)
#define VAR_QTYPE_CHECK(var, val) (var.typeId() == QMetaType::Q##val)
#define CHECK_VARTYPE(type, val) (type == QMetaType::val)
#define CHECK_VARQTYPE(type, val) (type == QMetaType::Q##val)
#endif

namespace A
{

// FIXME: move all these to Data or config or something?
void
setIncludeAscMC(bool = true);
bool
includeAscMC();

void
setIncludeChiron(bool = true);
bool
includeChiron();

void
setIncludeNodes(bool = true);
bool
includeNodes();

void
setIncludeMidpoints(bool = true);
bool
includeMidpoints();

void
setRequireAnchor(bool = true);
bool
requireAnchor();

void
resetPrimeFactorLimit(unsigned = 0);
unsigned
primeFactorLimit();

bool
isWithinPrimeFactorLimit(unsigned);
void
resetPFLCache();

void
setFilterFew(bool = true);
bool
filterFew();

void
setHarmonicsMinQuorum(unsigned);
unsigned
harmonicsMinQuorum();

void
setHarmonicsMinQOrb(double);
double
harmonicsMinQOrb();

void
setHarmonicsMaxQuorum(unsigned);
unsigned
harmonicsMaxQuorum();

void
setHarmonicsMaxQOrb(double);
double
harmonicsMaxQOrb();

void
setMaxHarmonic(int);
unsigned
maxHarmonic();

qreal
orbFactor();
void
setOrbFactor(qreal ofac);

typedef QMap<unsigned, bool> uintBoolMap;
typedef std::set<unsigned>   uintSSet; /// solo-item sorted set

bool
dynAspState(unsigned);
void
setDynAspState(unsigned, bool);
uintSSet
dynAspState();

QString
dtToString(const QDateTime& dt);

/// Calendar system for date interpretation.
/// Cal_Auto uses Julian before the Gregorian cutover (Oct 15 1582) and
/// Gregorian from that date onward.  The user can force either calendar
/// for charts in jurisdictions that adopted the Gregorian calendar later
/// (e.g. England 1752, Russia 1918).
enum CalendarType : int {
    Cal_Auto,      // Julian before cutover, Gregorian after
    Cal_Gregorian, // Force Gregorian (New Style)
    Cal_Julian     // Force Julian   (Old Style)
};

/// How the entered local time should be interpreted.
enum TimeMode {
    Time_ZoneTime, // Modern zone / clock time  (default)
    Time_LMT,      // Local Mean Time  (offset = longitude / 15)
    Time_LAT       // Local Apparent Time  (sundial time)
};

QDateTime
dateTimeFromJulian(double jd, CalendarType calType = Cal_Auto);

inline bool
getDynAspState(QVariant& var)
{
    QVariant ret;
    auto     asps = dynAspState();
    if (asps.size() == 32) ret = "all";
    else {
        QVariantList vl;
        for (auto u : asps) vl << u;
        ret = vl;
    }
    ret.swap(var);
    return true;
}

void
setDynAspState(const uintSSet&);

inline void
setDynAspState(const QVariant& var)
{
    setDynAspState(uintSSet());
    if (var.isNull()) return;
    if (VAR_TYPE_CHECK(var, QString)) {
        auto str = var.toString();
        if (str == "all") {
            for (unsigned i = 1; i <= 32; ++i) setDynAspState(i, true);
        }
        return;
    }
    auto type = VAR_TYPE(var);
    if (type == QMetaType::QVariantList || type == QMetaType::QStringList) {
        for (const auto& v : var.toList()) {
            setDynAspState(v.toUInt(), true);
        }
    }
}

enum HarmonicSort { hscByHarmonic, hscByPlanets, hscByOrb, hscByAge };

enum TransitSort {
    tscByDate,
    tscByTransitPlanet,
    tscByNatalPlanet,
    tscByHarmonic
};

enum SpeculumDisplayMode {
    DisplayLocalTime,     // Show local time (QDateTime format with day)
    DisplaySiderealTime,  // Show sidereal time (HH:MM:SS format)
    DisplayRightAscension // Show Right Ascension (degrees/minutes/seconds)
};

typedef int ZodiacSignId;
typedef int ZodiacId;
typedef int AspectId;
typedef int HouseSystemId;
typedef int PlanetId;
typedef int AspectSetId;

const PlanetId Planet_None      = -1;
const PlanetId Planets_Start    = 0;
const PlanetId Planet_Sun       = 0;
const PlanetId Planet_Moon      = 1;
const PlanetId Planet_Mercury   = 2;
const PlanetId Planet_Venus     = 3;
const PlanetId Planet_Mars      = 4;
const PlanetId Planet_Jupiter   = 5;
const PlanetId Planet_Saturn    = 6;
const PlanetId Planet_Uranus    = 7;
const PlanetId Planet_Neptune   = 8;
const PlanetId Planet_Pluto     = 9;
const PlanetId Planet_NorthNode = 10;
const PlanetId Planet_SouthNode = 11;
const PlanetId Planet_Chiron    = 12;
const PlanetId Planet_Ceres     = 13;
const PlanetId Planet_Pallas    = 14;
const PlanetId Planet_Juno      = 15;
const PlanetId Planet_Vesta     = 16;
const PlanetId Planets_End      = Planet_Vesta + 1;

const PlanetId Angles_Start = Planets_End;
const PlanetId Planet_Asc   = Angles_Start;
const PlanetId Planet_IC    = Angles_Start + 1;
const PlanetId Planet_Desc  = Angles_Start + 2;
const PlanetId Planet_MC    = Angles_Start + 3;
const PlanetId Angles_End   = Angles_Start + 4;

const PlanetId Houses_Start = Angles_End;
const PlanetId House_1      = Houses_Start;
const PlanetId House_2      = Houses_Start + 1;
const PlanetId House_3      = Houses_Start + 2;
const PlanetId House_4      = Houses_Start + 3;
const PlanetId House_5      = Houses_Start + 4;
const PlanetId House_6      = Houses_Start + 5;
const PlanetId House_7      = Houses_Start + 6;
const PlanetId House_8      = Houses_Start + 7;
const PlanetId House_9      = Houses_Start + 8;
const PlanetId House_10     = Houses_Start + 9;
const PlanetId House_11     = Houses_Start + 10;
const PlanetId House_12     = Houses_Start + 11;
const PlanetId Houses_End   = Houses_Start + 12;

const PlanetId Ingresses_Start     = Houses_End;
const PlanetId Ingress_Aries       = Ingresses_Start;
const PlanetId Ingress_Taurus      = Ingresses_Start + 1;
const PlanetId Ingress_Gemini      = Ingresses_Start + 2;
const PlanetId Ingress_Cancer      = Ingresses_Start + 3;
const PlanetId Ingress_Leo         = Ingresses_Start + 4;
const PlanetId Ingress_Virgo       = Ingresses_Start + 5;
const PlanetId Ingress_Libra       = Ingresses_Start + 6;
const PlanetId Ingress_Scorpio     = Ingresses_Start + 7;
const PlanetId Ingress_Sagittarius = Ingresses_Start + 8;
const PlanetId Ingress_Capricorn   = Ingresses_Start + 9;
const PlanetId Ingress_Aquarius    = Ingresses_Start + 10;
const PlanetId Ingress_Pisces      = Ingresses_Start + 11;
const PlanetId Ingresses_End       = Ingresses_Start + 12;

const PlanetId Regresses_Start     = Ingresses_End;
const PlanetId Regress_Aries       = Regresses_Start;
const PlanetId Regress_Taurus      = Regresses_Start + 1;
const PlanetId Regress_Gemini      = Regresses_Start + 2;
const PlanetId Regress_Cancer      = Regresses_Start + 3;
const PlanetId Regress_Leo         = Regresses_Start + 4;
const PlanetId Regress_Virgo       = Regresses_Start + 5;
const PlanetId Regress_Libra       = Regresses_Start + 6;
const PlanetId Regress_Scorpio     = Regresses_Start + 7;
const PlanetId Regress_Sagittarius = Regresses_Start + 8;
const PlanetId Regress_Capricorn   = Regresses_Start + 9;
const PlanetId Regress_Aquarius    = Regresses_Start + 10;
const PlanetId Regress_Pisces      = Regresses_Start + 11;
const PlanetId Regresses_End       = Regresses_Start + 12;

const PlanetId Parts_Start     = Regresses_End;
const PlanetId Part_of_Fortune = Parts_Start;
const PlanetId Part_of_Spirit  = Part_of_Fortune + 1;
const PlanetId Parts_End       = Part_of_Spirit + 1;

const AspectId Aspect_None        = -1;
const AspectId Aspect_Conjunction = 0;
const AspectId Aspect_Trine       = 1;
const AspectId Aspect_Sextile     = 2;
const AspectId Aspect_Opposition  = 3;
const AspectId Aspect_Quadrature  = 4;

const HouseSystemId Housesystem_None     = -1;
const HouseSystemId Housesystem_Placidus = 0;

const ZodiacId Zodiac_Tropical = 0;
const ZodiacId Zodiac_None     = -1;

const ZodiacSignId Sign_None = -1;

const AspectSetId AspectSet_Default = 0;

struct ZodiacSign {
    ZodiacSignId            id;
    ZodiacId                zodiacId;
    QString                 tag;
    QString                 name;
    float                   startAngle;
    float                   endAngle;
    PlanetId                ruler;
    QMap<QString, QVariant> userData;

    ZodiacSign()
    {
        id         = Zodiac_None;
        zodiacId   = -1;
        startAngle = 0;
        endAngle   = 0;
        ruler      = Planet_None;
    }
};

struct Zodiac {
    ZodiacId          id;
    QString           name;
    QList<ZodiacSign> signs;

    Zodiac() { id = Zodiac_None; }
    Zodiac(const Zodiac& zod) : id(zod.id), name(zod.name), signs(zod.signs) { }

    bool isValid() const { return id != Zodiac_None; }
};

struct HouseSystem {
    HouseSystemId           id;
    QString                 name;
    char                    sweCode;
    QMap<QString, QVariant> userData;

    HouseSystem() { id = Housesystem_None; }
};

class InputData {
    QDateTime     _GMT; // greenwich time & date
    QDateTime     _baseGMT; // natal chart GMT for progressed charts
    bool          _hasBaseChart; // true if this is a progressed/derived chart
    bool          _isProgressed; // true if planet positions should be progressed
    QVector3D     _location; // x & y - long & lat (deg), z - height (meters)
    HouseSystemId _houseSystem;
    ZodiacId      _zodiac;
    AspectSetId   _aspectSet;
    double        _tz;           // timezone offset in fractional hours
    CalendarType  _calendarType; // Julian / Gregorian / Auto
    TimeMode      _timeMode;     // Zone Time / LMT / LAT
    // double         harmonic;
    // double         RAMC;

  public:
    InputData()
    {
        _GMT.setTimeZone(QTimeZone::UTC);
        _GMT.setSecsSinceEpoch(0);
        _baseGMT.setTimeZone(QTimeZone::UTC);
        _baseGMT.setSecsSinceEpoch(0);
        _hasBaseChart  = false;
        _isProgressed  = false;
        _location      = QVector3D(0, 0, 0);
        _houseSystem   = Housesystem_Placidus;
        _zodiac        = Zodiac_Tropical;
        _aspectSet     = AspectSet_Default;
        _tz            = 0.0;
        _calendarType  = Cal_Auto;
        _timeMode      = Time_ZoneTime;
        // harmonic    = 1;
    }

    InputData(const QDateTime& gmt,
              double           tz,
              const QVector3D& loc  = { 0, 0, 0 },
              HouseSystemId    hsys = Housesystem_Placidus,
              ZodiacId         zid  = Zodiac_Tropical,
              AspectSetId      asid = AspectSet_Default) :
        _GMT(gmt),
        _hasBaseChart(false),
        _isProgressed(false),
        _location(loc),
        _houseSystem(hsys),
        _zodiac(zid),
        _aspectSet(asid),
        _tz(tz),
        _calendarType(Cal_Auto),
        _timeMode(Time_ZoneTime)
    {
        _baseGMT.setTimeZone(QTimeZone::UTC);
        _baseGMT.setSecsSinceEpoch(0);
    }

    const QVector3D& location() const { return _location; }
    void             setLocation(const QVector3D& loc) { _location = loc; }

    const QDateTime& GMT() const { return dateTime(); }
    void             setGMT(const QDateTime& gmt) { _GMT = gmt; }
    const QDateTime& dateTime() const { return _GMT; }
    double           tz() const { return _tz; }
    void             setTZ(double tz) { _tz = tz; }

    CalendarType calendarType() const { return _calendarType; }
    void         setCalendarType(CalendarType ct) { _calendarType = ct; }
    TimeMode     timeMode() const { return _timeMode; }
    void         setTimeMode(TimeMode tm) { _timeMode = tm; }

    // Base chart support for progressed charts
    bool             hasBaseChart() const { return _hasBaseChart; }
    const QDateTime& baseGMT() const { return _baseGMT; }
    void setBaseChart(const QDateTime& baseGmt)
    {
        _baseGMT      = baseGmt;
        _hasBaseChart = true;
    }
    void clearBaseChart()
    {
        _baseGMT.setSecsSinceEpoch(0);
        _hasBaseChart = false;
    }
    
    // Progression flag - indicates if planet positions should be calculated as progressed
    // This is separate from hasBaseChart because base chart is also used for returns and transits
    bool isProgressed() const { return _isProgressed; }
    void setProgressed(bool prog) { 
        if (_isProgressed != prog) {
            qDebug() << "[PERF] setProgressed changing from" << _isProgressed << "to" << prog;
        }
        _isProgressed = prog; 
    }

    QDateTime getEffectiveDateTime() const
    {
        return dateTime(); /* XXX timezone etc? */
    }

    HouseSystemId houseSystem() const { return _houseSystem; }
    void          setHouseSystem(HouseSystemId hsys) { _houseSystem = hsys; }
    ZodiacId      zodiac() const { return _zodiac; }
    void          setZodiac(ZodiacId zid) { _zodiac = zid; }
    AspectSetId   aspectSet() const { return _aspectSet; }
    void          setAspectSet(AspectSetId asid) { _aspectSet = asid; }

    /// Resolve Cal_Auto to a concrete SE_GREG_CAL / SE_JUL_CAL flag.
    /// Standard Western cutover: Oct 15, 1582 Gregorian.
    int resolvedSweCalFlag() const
    {
        if (_calendarType == Cal_Gregorian) return 1; // SE_GREG_CAL
        if (_calendarType == Cal_Julian) return 0;    // SE_JUL_CAL
        // Cal_Auto — Julian before the Gregorian cutover
        const auto& d = _GMT.date();
        if (d.year() < 1582) return 0;
        if (d.year() > 1582) return 1;
        if (d.month() < 10) return 0;
        if (d.month() > 10) return 1;
        return (d.day() >= 15) ? 1 : 0;
    }

    // void            computeRAMC() { }
};

typedef std::pair<FileType, InputData> FileInput;
typedef std::vector<FileInput>         FileInputs;

struct Houses {
    double             cusp[12]; // angles of house cusps (0... 360)
    double             Asc, MC, RAMC, RAAC, RADC, OAAC, ODDC;
    double             Vx, EA, startSpeculum;
    double             halfMedium, halfImum;
    double             eps;
    const HouseSystem* system;

    Houses()
    {
        for (auto& c : cusp) c = 0;
        system = nullptr;
        Asc = MC = RAMC = RAAC = RADC = OAAC = ODDC = Vx = EA = startSpeculum =
            0;
        halfMedium = halfImum = 0;
        eps = 23.4366;
    }

    Houses(const InputData& id);
};

struct Star {
    PlanetId                id;
    QString                 name;
    int                     sweFlags;
    PlanetId                configuredWithPlanet;
    QMap<QString, QVariant> userData;

    enum angleTransitMode { atAsc, atDesc, atMC, atIC, numAngles };
    static int         angleTransitFlag(unsigned int mode) { return 1 << mode; }
    QVector<QDateTime> angleTransit; // Local times for Rise/MC/Set/IC
    double angleTransitRA[4]; // Right Ascension values (degrees 0-360) for
                              // Rise/MC/Set/IC

    static QDateTime timeToDT(double t, bool greg = true);

    virtual PlanetId getPlanetId() const { return Planet_None; }
    virtual int      getSWENum() const { return -10; }
    virtual bool     isStar() const { return true; }
    virtual bool     isAsteroid() const { return false; }
    virtual bool     isPlanet() const { return false; }
    virtual bool     isRetro() const { return false; }

    bool isConfiguredWithPlanet() const
    {
        return configuredWithPlanet != Planet_None;
    }

    QPointF horizontalPos;       // x - azimuth (0... 360), y - height (0... 360)
    QPointF eclipticPos;         // x - longitude (0... 360), y - latitude (0... 360)
    QPointF equatorialPos;       // x - rectascension, y - declination
    QPointF tropicalEclipticPos; // tropical ecl lon/lat for swe_house_pos; x=-1 means unset
    double  distance;            // A.U. (astronomical units)
    qreal   pvPos;
    int     house;

    Star() : angleTransit(4)
    {
        id                   = Planet_None;
        configuredWithPlanet = Planet_None;
        horizontalPos        = QPoint(0, 0);
        eclipticPos          = QPoint(0, 0);
        equatorialPos        = QPoint(0, 0);
        tropicalEclipticPos  = QPointF(-1, 0);
        sweFlags             = 0;
        pvPos                = 0;
        distance             = 0;
        house                = 0;

        // Initialize RA array
        for (int i = 0; i < 4; i++) {
            angleTransitRA[i] = 0.0;
        }
    }

    virtual ~Star() { }

    operator Star*() { return this; }
    operator const Star*() const { return this; }

    bool operator==(const Star& other) const
    {
        return name == other.name && this->eclipticPos == other.eclipticPos;
    }
    bool operator!=(const Star& other) const
    {
        return name != other.name || this->eclipticPos != other.eclipticPos;
    }
};

struct PlanetPower {
    int dignity   = 0;
    int deficient = 0;

    PlanetPower() { }
};

enum PlanetPosition {
    Position_Normal,
    Position_Exaltation,
    Position_Dwelling,
    Position_Downfall,
    Position_Exile
};

struct Planet : public Star {
    int                 sweNum               = 0;
    bool                isReal               = false;
    QVector2D           defaultEclipticSpeed = QVector2D(0, 0);
    QList<ZodiacSignId> homeSigns, exaltationSigns, exileSigns, downfallSigns;

    QVector2D eclipticSpeed   = QVector2D(0, 0); // dx - (degree/day)
    QVector2D equatorialSpeed = QVector2D(0, 0);

    double            elongation = 0.0;
    double            phaseAngle = 0.0;
    PlanetPosition    position   = Position_Normal;
    PlanetPower       power;
    const ZodiacSign* sign       = nullptr;
    QList<int>        houseRuler = {};

    Planet() { }

    Planet(PlanetId pid, const QString& pname, const QVariant& fontChar)
    {
        id                   = pid;
        name                 = pname;
        userData["fontChar"] = fontChar;
    }

    Planet& operator=(const Planet& other)
    {
        Star::operator=(other);
        sweNum               = other.sweNum;
        isReal               = other.isReal;
        defaultEclipticSpeed = other.defaultEclipticSpeed;
        homeSigns            = other.homeSigns;
        exaltationSigns      = other.exaltationSigns;
        exileSigns           = other.exileSigns;
        downfallSigns        = other.downfallSigns;
        eclipticSpeed        = other.eclipticSpeed;
        elongation           = other.elongation;
        position             = other.position;
        power                = other.power;
        sign                 = other.sign;
        houseRuler           = other.houseRuler;
        return *this;
    }

    operator Planet*() { return this; }
    operator const Planet*() const { return this; }

    PlanetId getPlanetId() const override { return id; }
    int      getSWENum() const override { return sweNum; }
    bool     isStar() const override { return false; }
    // virtual bool      isAsteroid() const { return false; }
    bool isPlanet() const override { return true; }

    bool isRetro() const override { return eclipticSpeed.x() < 0; }

    double getPrefPos() const;
    double getPrefSpd() const;

    bool operator==(const Planet& other) const
    {
        return this->id == other.id && this->eclipticPos == other.eclipticPos;
    }
    bool operator!=(const Planet& other) const
    {
        return this->id != other.id || this->eclipticPos != other.eclipticPos;
    }
};

struct AspectsSet;

struct AspectType {
    AspectId                id;
    const AspectsSet*       set;
    QString                 name;
    float                   angle;
    float                   _orb;
    unsigned                _harmonic = 0;
    std::set<unsigned>      factors;
    bool                    _enabled;
    QMap<QString, QVariant> userData;

    AspectType() :
        id(Aspect_None),
        set(nullptr),
        angle(0),
        _orb(0),
        _enabled(true)
    {
    }

    float orb() const
    {
        if (!_harmonic) return _orb * orbFactor();
        return harmonicsMaxQOrb() / _harmonic * orbFactor();
    }

    bool isEnabled() const { return _enabled; }
    void setEnabled(bool b = true) { _enabled = b; }
};

struct Aspect {
    const AspectType* d;
    const Planet*     planet1;
    const Planet*     planet2;
    float             angle;
    float             orb;
    bool              applying;

    Aspect() :
        d(nullptr),
        planet1(nullptr),
        planet2(nullptr),
        angle(0),
        orb(0),
        applying(false)
    {
    }

    ~Aspect() { }
};

struct AspectsSet {
    AspectSetId                id;
    QString                    name;
    QMap<AspectId, AspectType> aspects;
    bool                       isEmpty() const { return aspects.isEmpty(); }

    AspectsSet(AspectSetId    aid = AspectSet_Default,
               const QString& n   = "default") :
        id(aid),
        name(n)
    {
    }

    ~AspectsSet() { }
};

typedef QList<Aspect> AspectList;
typedef QList<Planet> PlanetList;

class PlanetMap : public QMap<PlanetId, Planet> {
  public:
    using Base = QMap<PlanetId, Planet>;
    using Base::Base;

    using Base::begin;
    using Base::constBegin;
    using Base::constEnd;
    using Base::count;
    using Base::end;
    using Base::keys;

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

const PlanetMap&
getPlanetMap();

class Data {
  private:
    static QString                          usedLang;
    static QMap<AspectSetId, AspectsSet>    aspectSets;
    static QMap<HouseSystemId, HouseSystem> houseSystems;
    static QMap<ZodiacId, Zodiac>           zodiacs;
    static PlanetMap                        planets;
    static StarMap                          stars;
    static AspectSetId                      topAspSet;

    static QMap<PlanetId, GlyphName> signInfo;

    friend const PlanetMap& A::getPlanetMap();

  public:
    static void          load(QString language);
    static const QString usedLanguage() { return usedLang; }

    static const Planet& getPlanet(PlanetId id);
    static int           getSignGlyph(PlanetId id);
    static QString       getSignName(PlanetId id);

    static QList<PlanetId> getPlanets(bool includeAsteroids,
                                      bool includeCentaurs);
    static QList<PlanetId> getAngles();
    static QList<PlanetId> getInnerPlanets(bool includeAsteroids = false);
    static QList<PlanetId> getOuterPlanets(bool includeCentaurs = true);
    static QList<PlanetId> getSignIngresses();
    static QList<PlanetId> getHouses();
    static QList<PlanetId> getNonAngularHouses();

    static const Star&           getStar(const QString& name);
    static const QList<QString>& getStars();

    static const HouseSystem&       getHouseSystem(HouseSystemId id);
    static const QList<HouseSystem> getHouseSystems();

    static const Zodiac&       getZodiac(ZodiacId id);
    static const QList<Zodiac> getZodiacs();

    static double getSignPos(ZodiacId zid, const QString& sign);

    static QColor                        getHarmonicColor(unsigned h);
    static const QMap<unsigned, QColor>& getHarmonicColors();

    static const AspectType& getAspect(AspectId id, const AspectsSet& set);
    // static const QList<AspectType> getAspects(AspectSetId set);

    static QList<AspectsSet> getAspectSets() { return aspectSets.values(); }
    static AspectsSet&       getAspectSet(AspectSetId set);
    static const AspectsSet& topAspectSet() { return aspectSets[topAspSet]; }
    static const AspectsSet& tightConjunction();
};

enum PlanetLocMode {
    plmUnknown = 0,
    plmKnown,        // KnownPosition - fixed position at specific JD
    plmNatal,        // NatalPosition - natal chart position
    plmTransit,      // TransitPosition - moving transit position
    plmProgressed,   // ProgressedPosition - secondary progressed position
    plmSolarArc,     // SolarArcPosition - solar arc directed position
    plmPrimaryDir    // Future: primary directions
};

// Helper to convert PlanetLocMode to suffix string
inline QString
modeToSuffix(PlanetLocMode mode)
{
    switch (mode) {
    case plmNatal:      return "r";  // radical (natal)
    case plmProgressed: return "p";  // progressed
    case plmTransit:    return "";   // no suffix for transits
    case plmSolarArc:   return "sa"; // solar arc
    case plmPrimaryDir: return "pd"; // primary directions
    default:            return "";              // unknown/other
    }
}

class ChartPlanetId {
    int      _fid;
    PlanetId _pid, _pid2;
    bool     _oppMidpt;

  public:
    ChartPlanetId(PlanetId planetId  = Planet_None,
                  PlanetId planetId2 = Planet_None) :
        _fid(0),
        _pid(planetId),
        _pid2(planetId2),
        _oppMidpt(false)
    {
        if (_pid2 != Planet_None && _pid > _pid2) {
            _oppMidpt = true;
            std::swap(_pid, _pid2);
        }
    }

    ChartPlanetId(int fileId, PlanetId planetId, PlanetId planetId2) :
        _fid(fileId),
        _pid(planetId),
        _pid2(planetId2),
        _oppMidpt(false)
    {
        if (_pid2 != Planet_None && _pid > _pid2) {
            _oppMidpt = true;
            std::swap(_pid, _pid2);
        }
    }

    ChartPlanetId(const ChartPlanetId& otro) :
        _fid(otro._fid),
        _pid(otro._pid),
        _pid2(otro._pid2),
        _oppMidpt(otro._oppMidpt)
    {
    }

    ChartPlanetId& operator=(const ChartPlanetId& otro)
    {
        _fid      = otro._fid;
        _pid      = otro._pid;
        _pid2     = otro._pid2;
        _oppMidpt = otro._oppMidpt;
        return *this;
    }

    QString name() const;
    QString glyph() const;

    bool contains(PlanetId pid) const { return _pid == pid || _pid2 == pid; }

    bool isMidpt() const { return _pid2 != Planet_None; }
    bool isOppMidpt() const { return isMidpt() && _oppMidpt; }
    bool isSolo() const { return !isMidpt(); }

    bool samePlanetDifferentChart(const ChartPlanetId& cpid) const
    {
        return _fid != cpid._fid && _pid == cpid._pid && _pid2 == cpid._pid2;
    }

    bool samePlanet(const ChartPlanetId& cpid) const
    {
        return _pid == cpid._pid && _pid2 == cpid._pid2;
    }

    bool operator==(const ChartPlanetId& cpid) const
    {
        return _fid == cpid._fid && _pid == cpid._pid && _pid2 == cpid._pid2;
    }

    bool operator!=(const ChartPlanetId& cpid) const
    {
        return _fid != cpid._fid || _pid != cpid._pid || _pid2 != cpid._pid2;
    }

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
    {
        return operator==(cpid) || operator<(cpid);
    }

    void     setFileId(int fid) { _fid = fid; }
    int      fileId() const { return _fid; }
    PlanetId planetId() const { return _pid; }

    ChartPlanetId chartPlanetId1() const
    {
        return ChartPlanetId(_fid, _pid, Planet_None);
    }

    PlanetId planetId2() const { return _pid2; }

    ChartPlanetId chartPlanetId2() const
    {
        return ChartPlanetId(_fid, _pid2, Planet_None);
    }

    bool operator==(PlanetId p) const
    {
        return _pid == p && (isSolo() || _pid2 == p);
    }

    bool operator!=(PlanetId p) const
    {
        return _pid != p && (isSolo() || _pid2 != p);
    }

    bool operator<(PlanetId p) const
    {
        return _pid < p && (isSolo() || _pid2 < p);
    }

    bool operator>(PlanetId p) const
    {
        return _pid > p && (isSolo() || _pid2 > p);
    }

    bool operator<=(PlanetId p) const
    {
        return _pid <= p && (isSolo() || _pid2 <= p);
    }

    bool operator>=(PlanetId p) const
    {
        return _pid >= p && (isSolo() || _pid2 >= p);
    }

    operator PlanetId() const { return _pid; }
};

class ChartPlanetModeId : public ChartPlanetId {
    PlanetLocMode  _mode;

  public:
    ChartPlanetModeId() :
        ChartPlanetId(),
        _mode(plmUnknown)
    {
    }

    ChartPlanetModeId(const ChartPlanetId& cpid, PlanetLocMode mode) :
        ChartPlanetId(cpid),
        _mode(mode)
    {
    }

    ChartPlanetModeId(int fileId, PlanetId planetId, PlanetId planetId2,
                      PlanetLocMode mode) :
        ChartPlanetId(fileId, planetId, planetId2),
        _mode(mode)
    {
    }

    ChartPlanetModeId(const ChartPlanetModeId& other) :
        ChartPlanetId(other),
        _mode(other._mode)
    {
    }

    const ChartPlanetId& chartPlanetId() const { return *this; }

    PlanetLocMode mode() const { return _mode; }

    ChartPlanetModeId id1() const { return { chartPlanetId1(), _mode }; }
    ChartPlanetModeId id2() const { return { chartPlanetId2(), _mode }; }

    bool operator==(const ChartPlanetModeId& other) const
    {
        return ChartPlanetId::operator==(other) && _mode == other._mode;
    }

    bool operator!=(const ChartPlanetModeId& other) const
    {
        return !operator==(other);
    }

    bool operator<(const ChartPlanetModeId& other) const
    {
        if (ChartPlanetId::operator!=(other))
            return ChartPlanetId::operator<(other);
        return _mode < other._mode;
    }

    bool operator<=(const ChartPlanetModeId& other) const
    {
        return operator==(other) || operator<(other);
    }

    // PlanetId comparisons
    bool operator==(PlanetId p) const { return ChartPlanetId::operator==(p); }
    bool operator!=(PlanetId p) const { return ChartPlanetId::operator!=(p); }
    bool operator<(PlanetId p) const { return ChartPlanetId::operator<(p); }
    bool operator>(PlanetId p) const { return ChartPlanetId::operator>(p); }
    bool operator<=(PlanetId p) const { return ChartPlanetId::operator<=(p); }
    bool operator>=(PlanetId p) const { return ChartPlanetId::operator>=(p); }

    operator PlanetId() const { return *this; }
};

class samePlanet {
  public:
    samePlanet(const ChartPlanetId& cpid,
               bool                 isSolo = false,
               bool                 cmpFID = false) :
        _cpid(cpid),
        _isSolo(isSolo),
        _compareFileId(cmpFID)
    {
    }

    bool operator()(const ChartPlanetId& opid) const
    {
        if (_compareFileId && _cpid.fileId() != opid.fileId()) return false;
        return _isSolo ? (opid.isSolo() && _cpid.planetId() == opid.planetId())
                       : (_cpid.planetId() == opid.planetId()
                          || _cpid.planetId() == opid.planetId2());
    }

  private:
    ChartPlanetId _cpid;
    bool          _isSolo;
    bool          _compareFileId;
};

class PlanetSet : public std::set<ChartPlanetModeId> {
  public:
    using Base = std::set<ChartPlanetModeId>;
    using Base::Base;

    bool heterogeneous() const
    {
        std::set<int> seen;
        return std::any_of(begin(), end(), [&seen](const ChartPlanetModeId& cpid) {
            if (cpid.fileId() != -1) {
                seen.insert(cpid.fileId());
                return seen.size() > 1;
            }
            return false;
        });
    }

    bool contains(PlanetId pid) const
    {
        return std::any_of(begin(), end(), [pid](const ChartPlanetModeId& cpid) {
            return cpid.contains(pid);
        });
    }

    bool containsSolo(PlanetId pid) const
    {
        return std::any_of(begin(), end(), [pid](const ChartPlanetModeId& cpid) {
            return cpid.isSolo() && cpid.planetId() == pid;
        });
    }

    bool contains(const ChartPlanetId& pid) const
    {
        return std::any_of(begin(), end(), [&pid](const ChartPlanetModeId& cpid) {
            return cpid.chartPlanetId() == pid;
        });
    }

    bool containsSolo(const ChartPlanetId& pid) const
    {
        return std::any_of(begin(), end(), [&pid](const ChartPlanetModeId& cpid) {
            return cpid.isSolo() && cpid.chartPlanetId() == pid;
        });
    }

    bool contains(const ChartPlanetModeId& pmid) const
    {
        return find(pmid) != end();
    }

    bool containsAny(const PlanetSet& pset) const
    {
        for (const auto& cpid : pset)
            if (contains(cpid)) return true;
        return false;
    }

    bool containsAny(PlanetId start, PlanetId beyond)
    {
        return std::any_of(begin(), end(), [&](const ChartPlanetModeId& cpid) {
            return cpid.planetId() >= start && cpid.planetId() < beyond;
        });
    }

    bool containsMidPt() const
    {
        return std::any_of(begin(), end(), [](const ChartPlanetModeId& cpid) {
            return !cpid.isSolo();
        });
    }

    QString glyphs() const
    {
        QString res;
        int     lastFid = empty() ? 0 : begin()->fileId();
        for (const ChartPlanetModeId& cpid : *this) {
            if (cpid.fileId() != lastFid) res += " : ";
            else if (!res.isEmpty())
                res += ",";
            res += cpid.glyph();
            lastFid = cpid.fileId();
        }
        return res;
    }

    QStringList names(bool italicize = false) const
    {
        QStringList res;
        int         lastFid = 0;
        bool        ital    = false;
        for (const ChartPlanetModeId& cpid : *this) {
            auto n = cpid.name();
            if (auto suff = modeToSuffix(cpid.mode()); !suff.isEmpty()) {
                n += "-" + suff;
            }
            if (italicize && cpid.fileId() != lastFid) ital = !ital;
            if (ital) {
                res << "<i>" + n + "</i>";
            } else {
                res << n;
            }
            lastFid = cpid.fileId();
        }
        return res;
    }

    QString describe() const
    {
        QStringList res;

        std::map<int, unsigned> ids;
        for (const auto& cpid : *this) ids[cpid.fileId()]++;
        bool hasOtherChart = std::any_of(ids.begin(), ids.end(),
                                         [](auto& p) { return p.first > 0; });

        for (const ChartPlanetModeId& cpid : *this) {
            auto name = cpid.isMidpt() ? cpid.name()
                                       : cpid.name().left(3);
            if (cpid.fileId() == 0 && hasOtherChart) res << name + "-r";
            else
                res << name;
        }
        return res.join("=");
    }

    unsigned pop() const
    {
        unsigned n     = 0;
        bool     snode = false, nnode = false;
        for (const ChartPlanetModeId& it : *this) {
            if (it.isSolo()) {
                n += 1;
                if (it.planetId() == Planet_SouthNode) snode = true;
                else if (it.planetId() == Planet_NorthNode)
                    nnode = true;
            } else {
                n += 2;
                if (it.planetId() == Planet_SouthNode
                    || it.planetId2() == Planet_SouthNode)
                    snode = true;
                if (it.planetId() == Planet_NorthNode
                    || it.planetId2() == Planet_NorthNode)
                    nnode = true;
            }
        }
        return (snode && nnode) ? n - 1 : n;
    }
};

class ChartPlanetBitmap : public std::map<int, unsigned> {
    static unsigned maxShift()
    {
        static unsigned numBits = sizeof(mapped_type) * 8;
        return numBits - 1;
    }

  public:
    ChartPlanetBitmap() { }

    ChartPlanetBitmap(const PlanetSet& planets) { operator|=(planets); }

    ChartPlanetBitmap& operator|=(const PlanetSet& planets)
    {
        for (const auto& p : planets) operator|=(p.chartPlanetId());
        return *this;
    }

    ChartPlanetBitmap& operator|=(const ChartPlanetId& cpid)
    {
        value_type val(planetBit(cpid));
        (*this)[val.first] |= val.second;
        return *this;
    }

    ChartPlanetBitmap& operator|=(const ChartPlanetModeId& pmid)
    {
        return operator|=(pmid.chartPlanetId());
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
    {
        return other.contains(*this);
    }

    static value_type planetBit(const ChartPlanetId& cpid)
    {
        return value_type(
            cpid.fileId(),
            (1 << (maxShift() - cpid.planetId()))
                | (cpid.isSolo() ? 0 : (1 << (maxShift() - cpid.planetId2()))));
    }

    static ChartPlanetId cpid(const value_type& val)
    {
        int         num = 0;
        mapped_type v(val.second);
        while (!(v & 1)) {
            v >>= 1;
            ++num;
        }
        return ChartPlanetId(val.first, num, Planet_None);
    }

    void getPlanetSet(PlanetSet& ps) const
    {
        PlanetSet pset;
        int       num = 0;
        for (const auto& val : *this) {
            mapped_type v(val.second);
            while (v) {
                if (v & 1) {
                    PlanetId pid = int(maxShift()) - num;
                    pset.insert(ChartPlanetModeId(ChartPlanetId(val.first, pid, Planet_None), plmUnknown));
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

typedef QMap<ChartPlanetId, Planet>            ChartPlanetMap;
typedef std::map<ChartPlanetId, const Planet*> ChartPlanetPtrMap;

struct Loc {
    QString desc;
    qreal   loc;
    qreal   speed;

    Loc(qreal l = 0, qreal s = 0) : loc(l), speed(s) { }
    Loc(const QString& description, qreal l = 0, qreal s = 0) :
        desc(description),
        loc(l),
        speed(s)
    {
    }

    Loc(const Loc& other) : desc(other.desc), loc(other.loc), speed(other.speed)
    {
    }

    virtual ~Loc() { }

    operator const Loc*() const { return this; }

    virtual Loc* clone() const { return new Loc(*this); }

    virtual qreal   defaultSpeed() const { return 0; }
    virtual qreal   operator()(double /*jd*/, int /*h*/) { return loc; }
    virtual bool    inMotion() const { return false; }
    virtual QString description() const { return desc; }
    virtual PlanetLocMode mode() const { return plmUnknown; }

    virtual const InputData& input() const
    {
        static InputData dummy;
        return dummy;
    }
};

struct PlanetLoc : public Loc {
    ChartPlanetId planet;
    qreal         _rasiLoc = 0;

    enum { aspAll, aspOnlyConj, aspOnlyRetro, aspOnlyDirect };
    unsigned allowAspects = aspAll;

#if 1
    PlanetLoc(int fid, PlanetId p, qreal l, qreal s = 0) :
        Loc(l, s),
        planet(fid, p, Planet_None),
        _rasiLoc(l)
    {
    }
    PlanetLoc(int fid, PlanetId p, PlanetId p2, qreal l, qreal s = 0) :
        Loc(l, s),
        planet(fid, p, p2),
        _rasiLoc(l)
    {
    }
#endif
    PlanetLoc(ChartPlanetId p = 0, qreal l = 0, qreal s = 0) :
        Loc(l, s),
        planet(p),
        _rasiLoc(l)
    {
    }

    PlanetLoc(const ChartPlanetId& p,
              const QString&       desc,
              qreal                l = 0,
              qreal                s = 0) :
        Loc(l, s),
        planet(p),
        _rasiLoc(l)
    {
        if (!desc.isEmpty()) this->desc = desc;
    }

    PlanetLoc(const PlanetLoc& other) :
        Loc(other),
        planet(other.planet),
        _rasiLoc(other._rasiLoc),
        allowAspects(other.allowAspects)
    {
    }

    qreal rasiLoc() const { return _rasiLoc; }

    Loc* clone() const override { return new PlanetLoc(*this); }

    ChartPlanetModeId planetModeId() const { 
        return ChartPlanetModeId(planet, mode()); 
    }

    static bool aspectable(PlanetLoc* p1, PlanetLoc* p2)
    {
        if (!p1 || !p2) return false;
        if (p1 == p2) return false;
        if (p1->inMotion() && p2->inMotion()) return true;
        return false;
    }

    bool aspectable() const
    {
        return inMotion() || allowAspects <= aspOnlyConj
               || (speed <= 0 && allowAspects == aspOnlyRetro)
               || (speed >= 0 && allowAspects == aspOnlyDirect);
    }

    PlanetLoc& operator=(const PlanetLoc& other)
    {
        planet = other.planet;
        loc    = other.loc;
        return *this;
    }

    bool operator<(const PlanetLoc& other) const
    {
        return loc < other.loc || (loc == other.loc && planet < other.planet);
    }

    bool operator==(const PlanetLoc& other) const
    {
        return loc == other.loc && planet == other.planet;
    }

    qreal compute(const InputData& ida);
    qreal compute(const InputData& ida, double jd, int h);
    static std::pair<qreal, qreal> compute(const ChartPlanetId& pid,
                                           const InputData&     ida,
                                           double               jd);

    QString description() const override
    {
        return desc.isEmpty() ? planet.name() : (planet.name() + "-" + desc);
    }

    virtual qreal defaultSpeed() const override;
    virtual PlanetLocMode mode() const override { return plmUnknown; }
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
        return QString("%1 at %2")
            .arg(orb)
            .arg(dtToString(dateTimeFromJulian(when)));
    }
};

inline std::ostream&
operator<<(std::ostream& os, const ClusterOrbWhen& cow)
{
    return os << QString(cow).toLocal8Bit().constData();
}

typedef std::map<PlanetSet, ClusterOrbWhen>  PlanetClusterMap;
typedef std::map<unsigned, PlanetClusterMap> HarmonicPlanetClusters;

class NatalLoc : public PlanetLoc {
  public:
    NatalLoc(const ChartPlanetId& cpid, const InputData& ida) : PlanetLoc(cpid)
    {
        compute(ida);
        speed = 0;
    }

    Loc* clone() const override { return new NatalLoc(*this); }
    PlanetLocMode mode() const override { return plmNatal; }
};

class InputPosition : public PlanetLoc {
  public:
    InputPosition(const ChartPlanetId& cpid,
                  const InputData&     ida,
                  const QString&       tag = "") :
        PlanetLoc(cpid, tag),
        _ida(ida)
    {
    }

    const InputData& input() const override { return _ida; }

  protected:
    const InputData& _ida;
};

class KnownPosition : public PlanetLoc {
    qreal jd;

  public:
    KnownPosition(const ChartPlanetId& cpid,
                  qreal                loc,
                  double               jd,
                  const QString&       tag = "") :
        PlanetLoc(cpid, tag, loc),
        jd(jd)
    {
    }

    KnownPosition(const PlanetLoc* ploc, double jd, const QString& tag = "") :
        PlanetLoc(*ploc),
        jd(jd)
    {
        if (!tag.isEmpty()) Loc::desc = tag;
    }

    qreal julianDate() const { return jd; };

    Loc* clone() const override { return new KnownPosition(*this); }
    PlanetLocMode mode() const override { return plmKnown; }
};

class NatalPosition : public InputPosition {
  public:
    NatalPosition(const ChartPlanetId& cpid,
                  const InputData&     ida,
                  const QString&       tag = "") :
        InputPosition(cpid, ida, tag)
    {
        compute(ida);
        _rasiLoc = loc;
        speed    = 0;
    }

    Loc* clone() const override { return new NatalPosition(*this); }

    qreal operator()(double, int h) override
    {
        return loc = h == 1 ? _rasiLoc : fmod(_rasiLoc * h, 360.);
    }

    PlanetLocMode mode() const override { return plmNatal; }
};

/// Natal position that dynamically ex-precesses equatorial coordinates
/// to the transit epoch.  Used for mundane transits in equatorial mode.
/// The operator() is implemented out-of-line in astro-calc.cpp because
/// it depends on ex-precession functions declared in astro-calc.h.
class NatalExprecessedPosition : public NatalPosition {
    double _natalRA;         ///< Natal RA (degrees)
    double _natalDec;        ///< Natal declination (degrees)
    double _eclLon;          ///< Natal ecliptic longitude (degrees, cached)
    double _eclLat;          ///< Natal ecliptic latitude (degrees, cached)
    double _jdNatal;         ///< Julian date of natal epoch

  public:
    NatalExprecessedPosition(const ChartPlanetId& cpid,
                             const InputData&     ida,
                             const QString&       tag = "");

    Loc* clone() const override
    {
        return new NatalExprecessedPosition(*this);
    }

    bool          inMotion() const override { return true; }
    qreal         operator()(double jd, int h) override;
    PlanetLocMode mode() const override { return plmNatal; }

    /// Ex-precessed RA/Dec at target epoch, without touching `loc`/`speed`.
    /// Used by paran finder which needs natal RA/Dec for the mundane
    /// angle-transit formula independent of the ecliptic-mode `loc` state.
    void radecAt(double jd, double& ra, double& dec) const;

    /// Like radecAt, but also returns dRA/dt and dDec/dt (degrees/day) at
    /// the target epoch. Returns true on success.
    bool radecSpeedAt(double jd,
                      double& ra,    double& dec,
                      double& dRAdt, double& dDecdt) const;
};

class TransitPosition : public InputPosition {
  public:
    TransitPosition(const ChartPlanetId& cpid,
                    const InputData&     ida,
                    const QString&       tag = "") :
        InputPosition(cpid, ida, tag)
    {
    }

    TransitPosition(const ChartPlanetId& cpid,
                    const InputData&     ida,
                    double               jd) :
        InputPosition(cpid, ida)
    {
        compute(input(), jd, -1);
    }

    Loc* clone() const override { return new TransitPosition(*this); }

    bool inMotion() const override { return true; }

    qreal operator()(double jd, int h) override
    {
        return compute(input(), jd, h);
    }

    PlanetLocMode mode() const override { return plmTransit; }
};

class ProgressedPosition : public InputPosition {
    double _njd;        // natal birth time julian date
    double _sunSpeed;   // for future use with variable progression rates

  public:
    ProgressedPosition(const ChartPlanetId& cpid,
                       const InputData&     ida,
                       double               njd,
                       const QString&       tag = "p") :
        InputPosition(cpid, ida, tag),
        _njd(njd)
    {
        TransitPosition sunPos { { cpid.fileId(), Planet_Sun, Planet_None },
                                 ida };
        sunPos.compute(input(), _njd, -1);
        _sunSpeed = sunPos.speed;
    }

    Loc* clone() const override { return new ProgressedPosition(*this); }

    bool inMotion() const override { return true; }

    qreal operator()(double jd, int h) override
    {
        // Secondary progressions: 1 day after birth = 1 year of life
        auto pjd = _njd + (jd - _njd) / 365.25;
        auto pos = compute(input(), pjd, h);
        // Scale speed by progression rate (1 day per year)
        speed /= 365.25;
        return pos;
    }

    PlanetLocMode mode() const override { return plmProgressed; }
};

class SolarArcPosition : public InputPosition {
  public:
    PlanetLocMode mode() const override { return plmSolarArc; }
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
    return ret > 180 ? 360 - ret : ret;
}

enum DerivedEventFlag {
    etcNoDerivedEvent              = 0,
    etcEventToTransitAspect        = 1,  // *T
    etcEventToNatalAspect          = 2,  // *N
    etcEventTransitToTransitAspect = 4,  // *TT
    etcEventTransitToNatalAspect   = 8,  // *TN
    etcEventAspectPattern          = 16, // *A
    etcEventTypeStart              = etcEventAspectPattern << 1,
    etcDerivedEventMask            = etcEventTypeStart - 1
};
Q_DECLARE_FLAGS(DerivedEventFlags, DerivedEventFlag);

enum EventType {
    etcUnknownEvent = 0,
    etcStation      = etcEventTypeStart, // S
    etcTransitToStation,                 // T=S
    etcTransitToTransit,                 // T=T
    etcTransitToNatal,                   // T=N
    etcTransitToNatalAngles,             // T=NA
    etcOuterTransitToNatal,              // OT=N
    etcReturn,                           // R
    etcSolarReturn,                      // SR
    etcLunarReturn,                      // LR
    etcProgressedToProgressed,           // P=P
    etcProgressedToNatal,                // P=N
    etcInnerProgressedToNatal,           // IP=N
    etcTransitToProgressed,              // T=P
    etcSolarArcToNatal,                  // D=N
    etcSignIngress,                      // T=I
    etcHouseIngress,                     // T=H
    etcLunation,                         // L
    etcEclipse,                          // E
    etcSolarEclipse,                     // SE
    etcLunarEclipse,                     // LE
    etcHeliacalEvents,                   // HRS
    etcPrimaryDirections,                // PD
    etcTransitAspectPattern,             // TA
    etcTransitNatalAspectPattern,        // TNA
    etcParanatellonta,                   // Par
    etcParanatellontaToNatal,            // Par=N
    etcNumStandardEvents,
    etcUserEventStart = 64
};

typedef std::set<EventType> EventTypeSet;

using uintPair = std::pair<unsigned, unsigned>;
using hsets    = std::vector<uintSSet>;
using hsetId   = unsigned short int;

struct planetsEtc {
    uintPair  planetPair;
    hsetId    hsid;
    EventType et;

    planetsEtc(const uintPair& ab,
               hsetId          hs /*=0*/,
               EventType       et /*= etcUnknownEvent*/) :
        planetPair(ab),
        hsid(hs),
        et(et)
    {
    }

    planetsEtc(unsigned  a,
               unsigned  b,
               hsetId    hs /*= 0*/,
               EventType et /*= etcUnknownEvent*/) :
        planetPair(a, b),
        hsid(hs),
        et(et)
    {
    }

    unsigned a() const { return planetPair.first; }
    void     setA(unsigned a) { planetPair.first = a; }
    unsigned b() const { return planetPair.second; }
    void     setB(unsigned b) { planetPair.second = b; }
};

typedef std::list<planetsEtc> searchPairList;

/// Specifies an exact N-body pattern to search for.
/// Unlike general cluster detection (which discovers any quorum-sized groups),
/// this targets a specific set of bodies — the quorum equals the body count.
struct ExactPatternSpec {
    std::vector<unsigned> alistIndices; ///< indices into _alist
    PlanetSet             bodies;       ///< ChartPlanetModeIds (for cluster comparison)
    hsetId                hsid;         ///< index into _hsets
    EventType             et;           ///< event type for results
    bool                  hasMidpoints = false; ///< true when any body is a midpoint

    unsigned quorum() const { return static_cast<unsigned>(alistIndices.size()); }

    /// Effective orb for this pattern: tighter when midpoints are involved,
    /// mirroring the outOfOrb() logic (patternsSpreadOrb / 8 for midpoints).
    qreal effectiveOrb(qreal baseOrb) const
    {
        return hasMidpoints ? baseOrb / 4. : baseOrb;
    }
};

typedef std::vector<ExactPatternSpec> ExactPatternList;

class PlanetProfile : public Loc, public std::deque<Loc*> {
    bool _forceMinimize = false;

  public:
    typedef std::deque<Loc*> Base;
    using Base::Base;

    PlanetProfile() { }
    PlanetProfile(std::initializer_list<Loc*> locs) : Base(locs) { }
    PlanetProfile(std::initializer_list<QMap<int, Planet>*> pms);

    PlanetProfile(const PlanetProfile& other) : Loc(other), Base()
    {
        for (auto oloc : other) emplace_back(oloc->clone());
    }

    PlanetProfile(PlanetProfile&& other) :
        Loc(other),
        Base(std::move(other)) { }

    virtual ~PlanetProfile() { qDeleteAll(*this); }

    PlanetProfile& operator=(PlanetProfile&& other)
    {
        Loc::loc   = other.loc;
        Loc::speed = other.Loc::speed;
        Base::operator=(std::move(other));
        return *this;
    }

    static searchPairList& ignore()
    {
        static searchPairList s_dummy;
        return s_dummy;
    }

    PlanetProfile* profile(PlanetSet       psp,
                           searchPairList& stuff = ignore()) const
    {
        auto           ret = new PlanetProfile;
        QMap<int, int> inxUpd;
        int            inxOld = 0, inxNew = 0;
        for (auto loc : *this) {
            if (auto ploc = dynamic_cast<const PlanetLoc*>(loc)) {
                auto pmid = ploc->planetModeId();
                auto psit = psp.find(pmid);
                if (psit != psp.end()) {
                    ret->emplace_back(ploc->clone());
                    inxUpd[inxOld] = inxNew;
                    ++inxNew;
                    psp.erase(psit);
                    if (psp.empty()) break;
                }
            }
            ++inxOld;
        }
        searchPairList retsp;
        for (auto petc : stuff) {
            if (inxUpd.contains(petc.a()) && inxUpd.contains(petc.b())) {
                petc.setA(inxUpd.value(petc.a()));
                petc.setB(inxUpd.value(petc.b()));
                retsp.emplace_back(petc);
            }
        }
        if (!stuff.empty()) stuff.swap(retsp);
        return ret;
    }

    qreal defaultSpeed() const
    {
        // FIXME should base on synodic cycle? Compounded, this
        // could be a high value, but harmonically, it will be rather
        // less
        int   i   = 0;
        qreal spd = 0;
        for (auto& pl : *this) {
            if (pl->inMotion()) {
                ++i;
                spd += pl->defaultSpeed();
            }
        }
        return i > 1 ? spd / float(i) : spd;
    }

    qreal speed() const { return Loc::speed; }

    qreal computeSpread(double jd);
    qreal computeSpread();

    static std::pair<qreal, qreal> computeDelta(const Loc*   a,
                                                const Loc*   b,
                                                unsigned int harmonic = 1);
    static std::pair<qreal, qreal> computeSpread(
        std::initializer_list<const Loc*>,
        unsigned int = 1);

    qreal computePos(double jd, unsigned int harmonic = 1);

    qreal operator()(double jd) { return computePos(jd); }

    void setForceMinimize(bool b = true) { _forceMinimize = b; }

    bool needsFindMinimalSpread() const { return _forceMinimize || size() > 2; }
};

struct BySpeed {
    bool operator()(const PlanetLoc& a, const PlanetLoc& b) const
    {
        return std::abs(a.speed) > std::abs(b.speed);
    }
};

typedef std::set<PlanetLoc>          PlanetRange;
typedef std::set<PlanetLoc, BySpeed> PlanetRangeBySpeed;

struct PlanetClusterLess {
    bool _fast;

    PlanetClusterLess(bool fast = true) : _fast(fast) { }

    static int fileId(const ChartPlanetId& cpid) { return cpid.fileId(); }

    static int fileId(const PlanetLoc& ploc) { return ploc.planet.fileId(); }

    static int fileId(const Loc&) { return -1; }

    static const ChartPlanetId& planet(const ChartPlanetId& cpid)
    {
        return cpid;
    }

    static const ChartPlanetId& planet(const PlanetLoc& ploc)
    {
        return ploc.planet;
    }

    static const ChartPlanetId& planet(const Loc&)
    {
        static ChartPlanetId none;
        return none;
    }

    template <typename T>
    bool less(T ait, T aend, T bit, T bend) const
    {
        // if (aended() != bended()) return aent > bent;
        auto f = fileId(*ait);
        auto g = fileId(*bit);
        if (f != g) return f < g;
        while (ait != aend && bit != bend && fileId(*ait) == f
               && fileId(*bit) == f)
        {
            if (planet(*ait) != planet(*bit))
                return planet(*ait) < planet(*bit);
            ++ait;
            ++bit;
        }
        return false;
    }

    bool operator()(const PlanetRangeBySpeed& a,
                    const PlanetRangeBySpeed& b) const
    {
        return _fast ? less(a.begin(), a.end(), b.begin(), b.end())
                     : less(a.rbegin(), a.rend(), b.rbegin(), b.rend());
    }

    bool operator()(const PlanetSet& a, const PlanetSet& b)
    {
        return _fast ? less(a.rbegin(), a.rend(), b.rbegin(), b.rend())
                     : less(a.begin(), a.end(), b.begin(), b.end());
    }
};

typedef std::list<PlanetLoc> PlanetQueue;

inline qreal
getLoc(const Loc& loc)
{
    return loc.loc;
}
inline qreal
getLoc(const Loc* loc)
{
    return loc->loc;
}

class PlanetGroups : public std::map<PlanetSet, PlanetRange> {
  public:
    using std::map<PlanetSet, PlanetRange>::insert;

    template <typename T>
    static void getPlanetSet(const T& planets, PlanetSet& plist)
    {
        plist.clear();
        for (const PlanetLoc& loc : planets) 
            plist.insert(loc.planetModeId());
    }

    void insert(const PlanetRange& planets)
    {
        PlanetSet plist;
        getPlanetSet(planets, plist);
        insert(value_type(plist, planets));
    }

    void insert(const PlanetQueue& planets, unsigned minQuorum = 2);
};

enum EventUpdateType { etcMerge, etcUpdate };

template <typename T>
struct ARange : public QPair<T, T> {
    typedef QPair<T, T> Base;
    using Base::Base;
    using Base::first;
    using Base::second;

    ARange() : Base() { }

    ARange(QVariant& v)
    {
        QVariantList vl = v.toList();
        first           = vl.takeFirst().value<T>();
        second          = vl.takeFirst().value<T>();
    }

    ARange& operator=(const QVariant& v)
    {
        if (v.isNull() || VAR_TYPE(v) != QMetaType::QVariantList) {
            first  = T();
            second = T();
        } else {
            QVariantList vl = v.toList();
            first           = vl.takeFirst().value<T>();
            second          = vl.takeFirst().value<T>();
        }
        return *this;
    }

    bool contains(const T& d) const { return d >= first && d <= second; }

    operator QVariant() const
    {
        QVariantList vl;
        vl << first << second;
        return vl;
    }
};

typedef ARange<QDate> ADateRange;

class ADateTimeRange : public ARange<QDateTime> {
  public:
    using Base = ARange<QDateTime>;
    using Base::Base;

    double days() const
    {
        using namespace std::chrono;
        return double(duration_cast<hours>(second - first).count()) / 24.0;
    }
};

struct HarmonicPlanetSet {
    unsigned  harmonic;
    PlanetSet planets;
    EventType eventType;

    HarmonicPlanetSet(unsigned h = 1, const PlanetSet& ps = {}, EventType et = etcUnknownEvent) :
        harmonic(h), planets(ps), eventType(et) { }

    bool operator<(const HarmonicPlanetSet& other) const {
        if (harmonic != other.harmonic) return harmonic < other.harmonic;
        if (planets != other.planets) return planets < other.planets;
        return eventType < other.eventType;
    }
};

typedef std::pair<double, double> JDateRange;
typedef std::set<JDateRange>      JDateRanges;

class EventFinderTask : public QRunnable {
  public:
    EventFinderTask() { }
    virtual ~EventFinderTask() { }

    virtual EventType eventType() const { return etcUnknownEvent; }
    virtual void      setInOrbRange(const JDateRange&) { }
};

typedef std::vector<EventFinderTask*> RunnableTasks;

struct JDateRangeTasks {
    JDateRange    range;
    RunnableTasks tasks;

    JDateRangeTasks() : range(), tasks() { }

    JDateRangeTasks(double               start,
                    double               finish,
                    const RunnableTasks& tasks = {}) :
        range(start, finish),
        tasks(tasks)
    {
    }

    operator JDateRange() const { return range; }

    void addTask(EventFinderTask* task) { tasks.push_back(task); }

    JDateRangeTasks& operator<<(EventFinderTask* task)
    {
        addTask(task);
        return *this;
    }
};
typedef std::map<HarmonicPlanetSet, JDateRangeTasks> HarmonicPlanetDateRangeMap;

typedef std::map<JDateRange, unsigned char>         JDateRangeHits;
typedef std::map<HarmonicPlanetSet, JDateRangeHits> HarmonicPlanetDateRangesMap;

class HarmonicAspect {
    unsigned           _eventType; ///< type of event
    unsigned char      _harmonic;  ///< harmonic of aspect (or 1)
    PlanetSet          _pattern;   ///< pattern
    PlanetRangeBySpeed _locations; ///< locations of planets
    qreal              _orb;       ///< orb of aspect or span of assembly

  public:
    HarmonicAspect(unsigned             et    = 0,
                   unsigned char        h     = 0,
                   PlanetRangeBySpeed&& pr    = {},
                   qreal                delta = 0.0) :
        _eventType(et),
        _harmonic(h),
        _locations(pr),
        _orb(delta)
    {
        for (const auto& loc : _locations) {
            if (const auto ploc = dynamic_cast<const PlanetLoc*>(&loc))
                _pattern.insert(ploc->planetModeId());
        }
    }

    HarmonicAspect(unsigned      et,
                   unsigned char h,
                   PlanetSet&&   ps,
                   qreal         delta = 0.0) :
        _eventType(et),
        _harmonic(h),
        _pattern(ps),
        _orb(delta)
    {
    }

    void reset()
    {
        _eventType = etcUnknownEvent;
        _harmonic  = 0;
        _locations.clear();
        _pattern.clear();
        _orb = 0.;
    }

    void reset(PlanetRangeBySpeed&& pr, qreal delta = 0.0)
    {
        _locations = std::move(pr);
        _pattern.clear();
        for (const auto& loc : _locations) {
            if (const auto ploc = dynamic_cast<const PlanetLoc*>(&loc))
                _pattern.insert(ploc->planetModeId());
        }
        _orb = delta;
    }

    void reset(PlanetSet&& ps, qreal delta = 0.0)
    {
        _locations.clear();
        _pattern = std::move(ps);
        _orb     = delta;
    }

    EventType    eventType() const { return EventType(_eventType); }
    unsigned int harmonic() const { return _harmonic; }
    qreal        orb() const { return qAbs(_orb); }
    const PlanetRangeBySpeed& locations() const { return _locations; }

    const PlanetSet& planets() const { return _pattern; }

    bool operator==(const HarmonicAspect& asp) const
    {
        return _harmonic == asp._harmonic && _eventType == asp._eventType
               && planets() == asp.planets();
    }

    friend class HarmonicEvent;
};

typedef std::list<HarmonicAspect> HarmonicAspects;

class HarmonicEvent : public HarmonicAspect {
    QDateTime       _dateTime; ///< time of event in UTC
    ADateTimeRange  _range;
    HarmonicAspects _coincidences; ///< coincident events

  public:
    HarmonicEvent(const QDateTime&     dt,
                  unsigned             et,
                  unsigned char        h,
                  PlanetRangeBySpeed&& pr    = {},
                  qreal                delta = 0.0) :
        HarmonicAspect(et, h, std::move(pr), delta),
        _dateTime(dt)
    {
    }

    HarmonicEvent(const ADateTimeRange& range,
                  unsigned              et,
                  unsigned char         h,
                  PlanetSet&&           ps,
                  qreal                 delta = 0.0) :
        HarmonicAspect(et, h, std::move(ps), delta),
        _range(range)
    {
    }

    HarmonicEvent(const QDateTime& dt = {}) :
        HarmonicAspect(),
        _dateTime(dt) { }

    void clear()
    {
        reset();
        _dateTime = {};
        _coincidences.clear();
    }

    using HarmonicAspect::reset;

    template <typename... Args>
    void reset(const QDateTime& dt, Args&&... args)
    {
        _dateTime = dt;
        HarmonicAspect::reset(std::forward<Args>(args)...);
    }

    void reset(const QDateTime& dt, qreal delta)
    {
        _dateTime = dt;
        _orb      = delta;
    }

    QDateTime&       dateTime() { return _dateTime; }
    const QDateTime& dateTime() const { return _dateTime; }

    const ADateTimeRange& range() const { return _range; }
    void                  setRange(const ADateTimeRange& r) { _range = r; }

    HarmonicAspects&       coincidences() { return _coincidences; }
    const HarmonicAspects& coincidences() const { return _coincidences; }

    const HarmonicAspect& coincidence(int n) const
    {
        static HarmonicAspect dummy;
        if (n < 0 || unsigned(n) >= _coincidences.size()) return dummy;
        auto it = _coincidences.begin();
        if (n == 0) return *it;
        return *std::next(it, n);
    }

    operator const HarmonicEvent*() const { return this; }

    bool isNull() const { return operator==(HarmonicEvent()); }

    bool operator==(const HarmonicEvent& ev) const
    {
        return _dateTime == ev._dateTime && HarmonicAspect::operator==(ev);
    }
};

typedef std::list<HarmonicEvent> HarmonicEventsBase;

class HarmonicEvents : public HarmonicEventsBase {
  public:
    using HarmonicEventsBase::HarmonicEventsBase;

    bool     syncDateRange = true;
    unsigned eventsType    = etcUnknownEvent;

    HarmonicEvents() { }

    HarmonicEvents(const HarmonicEvents& other) :
        HarmonicEventsBase(other),
        syncDateRange(other.syncDateRange),
        eventsType(other.eventsType)
    {
    }

    HarmonicEvents& operator=(const HarmonicEvents& other)
    {
        HarmonicEventsBase&       me(*this);
        const HarmonicEventsBase& you(other);
        me            = you;
        syncDateRange = other.syncDateRange;
        eventsType    = other.eventsType;
        return *this;
    }

    operator const HarmonicEvents*() const { return this; }

    template <typename... Args>
    HarmonicEvent& safe_emplace_back(Args&&... args)
    {
        QMutexLocker ml { &_mutex };
        return HarmonicEventsBase::emplace_back(std::forward<Args>(args)...);
    }

    QMutex* mutex() { return &_mutex; }

  private:
    QMutex _mutex;
};

typedef std::set<ADateRange> ADateRangeSet;
struct EventStoreData {
    ADateRangeSet  ranges;
    HarmonicEvents events;
    uintSSet       harmonics;   ///< which harmonics were searched
    bool           searched = false; ///< true after a finder run included this type
};

struct EventUpdateData {
    ADateRangeSet   ranges;
    HarmonicEvents& events;
};

struct EventScope {
    unsigned   eventType;
    ADateRange range;
};

typedef QMap<unsigned, EventStoreData> EventStoreBase;
class EventStore : public EventStoreBase {
  public:
    using EventStoreBase::EventStoreBase;

    class noNeed : public std::exception {
      public:
        using std::exception::exception;
    };

    EventUpdateData getEventUpdateScope(EventScope      evscope,
                                        EventUpdateType uptype);

    // Manifest API — tracks which event types have been computed
    bool         wasSearched(unsigned eventType) const;
    bool         wasSearched(unsigned eventType,
                             const ADateRange& range) const;
    EventTypeSet searchedTypes() const;
    EventTypeSet missingTypes(const EventTypeSet& wanted) const;

    /// Record that a finder searched for the given event types over the
    /// specified date range using the given harmonic set.
    void recordSearch(const EventTypeSet& types,
                      const ADateRange&   range,
                      const uintSSet&     harmonics);

    /// Clear all manifest metadata (marks everything as unsearched).
    /// Call when a full recomputation is required.
    void clearManifest();

    /// Ingest a list of events, bucketing each into the appropriate
    /// EventStoreData slot by eventType().
    void ingestEvents(const HarmonicEvents& evs);
};

typedef std::map<unsigned, PlanetGroups> PlanetHarmonics;
typedef PlanetHarmonics::iterator        HarmonicIter;
typedef PlanetHarmonics::const_iterator  HarmonicCIter;

typedef std::multiset<unsigned>  uintMSet; /// Multi-item sorted set
typedef uintMSet::iterator       intIter;
typedef uintMSet::const_iterator intCIter;

typedef std::map<unsigned, uintMSet> factorMap;
typedef factorMap::iterator          factorIter;
typedef factorMap::const_iterator    factorCIter;

struct Horoscope {
    InputData  inputData;
    Zodiac     zodiac;
    Houses     houses, housesOrig;
    AspectList aspects;
    PlanetMap  planets, planetsOrig;
    Planet sun, moon, mercury, venus, mars, jupiter, saturn, uranus, neptune,
        pluto, northNode;
    StarMap stars;
    double  harmonic = 1.0;

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
    {
        return &const_cast<Horoscope*>(this)->planets[pid];
    }

    /// Replace equatorial coordinates with ex-precessed values at targetJD.
    /// Original values are saved so clearExprecession() can restore them.
    void applyExprecession(double targetJD);

    /// Restore original (natal-epoch) equatorial coordinates.
    void clearExprecession();

    /// True when ex-precessed equatorial overlays are active.
    bool exprecessApplied() const { return _exprecessApplied; }

    /// Returns the natal-epoch (pre-ex-precession) equatorial position for
    /// planet pid.  When ex-precession is not active, returns the planet's
    /// current equatorialPos unchanged.
    QPointF natalEquatorialPos(PlanetId pid) const {
        if (_exprecessApplied) {
            auto it = _savedPlanetEq.find(pid);
            if (it != _savedPlanetEq.end()) return it->pos;
        }
        auto it2 = planets.find(pid);
        return it2 != planets.end() ? it2->equatorialPos : QPointF{};
    }

  private:
    struct SavedEquatorial {
        QPointF   pos;
        QVector2D speed;
    };
    QMap<PlanetId, SavedEquatorial>    _savedPlanetEq;
    QMap<std::string, SavedEquatorial> _savedStarEq;
    // [ANGLE_PRECESSION] Saved natal-epoch house RA values for chart axes.
    double _savedRAAC = 0, _savedRAMC = 0, _savedRADC = 0;
    bool                               _exprecessApplied = false;
};

void
load(QString language);
QString
usedLanguage();

inline QColor
getHarmonicColor(unsigned h)
{
    return Data::getHarmonicColor(h);
}

const Planet&
getPlanet(PlanetId pid);
PlanetId
getPlanetId(const QString& name);
double
getSignPos(ZodiacId       zid,
           const QString& sign,
           unsigned       degrees = 0,
           unsigned       minutes = 0,
           unsigned       seconds = 0);
QString
getPlanetName(const ChartPlanetId& id);
QString
getPlanetGlyph(const ChartPlanetId& id);

inline QList<PlanetId>
getPlanets(bool includeAsteroids = false, bool includeCentaurs = true)
{
    return Data::getPlanets(includeAsteroids, includeCentaurs);
}

inline QList<PlanetId>
getAngles()
{
    return Data::getAngles();
}

inline QList<PlanetId>
getInnerPlanets(bool includeAsteroids)
{
    return Data::getInnerPlanets(includeAsteroids);
}

inline QList<PlanetId>
getOuterPlanets(bool includeCentaurs)
{
    return Data::getOuterPlanets(includeCentaurs);
}

inline QList<PlanetId>
getSignIngresses()
{
    return Data::getSignIngresses();
}
inline QList<PlanetId>
getHouses()
{
    return Data::getHouses();
}
const Star&
getStar(const QString& name);
const QList<QString>&
getStars();
const HouseSystem&
getHouseSystem(HouseSystemId id);
const Zodiac&
getZodiac(ZodiacId id);
const QList<HouseSystem>
getHouseSystems();
const QList<Zodiac>
getZodiacs();
// const QList<AspectType> getAspects(AspectSetId set);
const AspectType&
getAspect(AspectId id, const AspectsSet& set);
QList<AspectsSet>
getAspectSets();
AspectsSet&
getAspectSet(AspectSetId set);
const AspectsSet&
topAspectSet();
const AspectsSet&
tightConjunction();

} // namespace A

Q_DECLARE_METATYPE(A::PlanetSet);

#endif // A_DATA_H
