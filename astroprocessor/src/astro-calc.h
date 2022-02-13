#ifndef A_CALC_H
#define A_CALC_H

#include "astro-data.h"
#include <QRunnable>

// Forward
class AstroFile;
typedef QList<AstroFile*> AstroFileList;

struct ADateDelta {
    int numDays = 0;
    int numMonths = 0;
    int numYears = 0;

    ADateDelta(const QString& str);

    ADateDelta(int days = 0, int months = 0, int years = 0) :
        numDays(days), numMonths(months), numYears(years)
    { }

    ADateDelta(QDate from, QDate to);

    QString toString() const
    {
        QStringList sl;
        bool terse = numYears && numMonths && numDays;
        auto append = [&](const int& v, const QString& un) {
            if (!v) return;
            sl << (terse
                   ? QString("%1%2").arg(v).arg(un.at(0))
                   : QString("%1 %2%3").arg(v).arg(un).arg(v!=1?"s":""));
        };
        append(numYears,"yr");
        append(numMonths,"mo");
        append(numDays,"day");
        return sl.join(terse? " " : ", ");
    }

    QDate addTo(const QDate& d);
    QDate subtractFrom(const QDate& d);

    static ADateDelta fromString(const QString& str)
    { return ADateDelta(str); }

    bool operator==(const ADateDelta& other) const
    {
        return numYears == other.numYears
                && numMonths == other.numMonths
                && numDays == other.numDays;
    }

    bool operator!=(const ADateDelta& other) const
    { return !operator==(other); }

    operator bool() const { return numYears || numMonths || numDays; }

};

// A namespace
namespace A {  // Astrology, sort of :)

template <typename T> struct modalTrait
{ static const T& defaultNew() { static T dflt; return dflt; } };

template <>
struct modalTrait<bool> { static bool defaultNew() { return true; } };

template <typename T = bool>
class modalize {
    T _was;
    T& _state;

public:
    modalize(T& state, T newState = modalTrait<T>::defaultNew()) :
        _was(state),
        _state(state)
    { _state = newState; }

    ~modalize()
    { _state = _was;}

    operator const T&() const { return _state; }
    modalize& operator=(T newState) { _state = newState; return *this; }
};

enum aspectModeEnum {
    amcUnknown=-1,
    amcGreatCircle,
    amcEcliptic,
    amcEquatorial,
    amcPrimeVertical,
    amcEND
};

class aspectModeType {
    aspectModeEnum _value;

public:
    typedef aspectModeEnum valueType;

    aspectModeType(aspectModeEnum e = amcUnknown) : _value(e) { }
    aspectModeType(const QString& val) : _value(fromString(val)) { }

    static const aspectModeType& current();

    aspectModeType(const QVariant& var) :
        _value(amcUnknown)
    {
        if (var.isValid()) {
            if (var.type() == QVariant::String) {
                _value = fromString(var.toString());
            } else if (var.type() == QVariant::Int) {
                _value = aspectModeEnum(var.toInt());
            }
        }
    }

    static aspectModeEnum fromString(const QString& val)
    {
        for (int i = 0; i < amcEND; ++i) {
            if (val == strings()[i]) return aspectModeEnum(i);
        }
        return amcUnknown;
    }

    static const char* toString(int e)
    {
        if (e >= 0 && e < amcEND) {
            return strings()[e];
        }
        return "unknown";
    }

    static QString toUserString(unsigned e)
    {
        static QString aspectModeStr[] = {
            QObject::tr("Great Circle"),
            QObject::tr("Ecliptic"),
            QObject::tr("Equatorial"),
            QObject::tr("Prime Vertical")
        };
        return aspectModeStr[e];
    }

    static const QMap<QString, QVariant>& map()
    {
        static QMap<QString, QVariant> ret;
        if (ret.isEmpty()) {
            for (int i = 0; i < amcEND; ++i) {
                ret.insert(toUserString(aspectModeEnum(i)), i);
            }
        }
        return ret;
    }

    operator aspectModeEnum() const { return _value; }
    operator QVariant() const { return int(_value); }
    operator QString() const { return toString(_value); }

    QString asUserString() const { return toUserString(_value); }

private:
    static const char** strings()
    {
        static const char* s_strings[] = {
            "greatCircle", "ecliptic", "equatorial", "primeVertical"
        };
        return s_strings;
    }
};

extern aspectModeType aspectMode;

double  getJulianDate            ( QDateTime GMT, bool ephemerisTime=false );
float   roundDegree              ( float deg );         // returns 0...360
const ZodiacSign& getSign        ( float deg, const Zodiac& zodiac );
int     getHouse                 ( const Houses& houses, float deg ); // returns 1...12
int     getHouse                 ( ZodiacSignId sign, const Houses& houses, const Zodiac& zodiac );

float   angle                    (const Star& body1, const Star& body2 );
float   angle                    ( const Star& body, float deg );
float   angle                    (const Star& body, QPointF coordinate );
float   angle                    ( float deg1, float deg2 );
AspectId aspect                  ( const Star& planet1, const Star& planet2, const AspectsSet& aspectSet );
AspectId aspect                  ( const Star& planet, QPointF coordinate, const AspectsSet& aspectSet );
AspectId aspect                  ( const Star& planet1, float degree, const AspectsSet& aspectSet );
AspectId aspect                  ( float angle, const AspectsSet& aspectSet );
bool    towardsMovement          ( const Star& planet1, const Star& planet2 );
PlanetPosition getPosition       ( const Planet& planet, ZodiacSignId sign );
const Planet* doryphoros         ( const Horoscope& scope );
const Planet* auriga             ( const Horoscope& scope );
const Planet* almuten            ( const Horoscope& scope );
bool    rulerDisposition         ( int house, int houseAuthority, const Horoscope& scope );
bool    isEarlier                ( const Planet& planet, const Planet& sun );
//const Planet& ruler          ( int house, const Horoscope& scope );
PlanetId receptionWith           ( const Planet& planet, const Horoscope& scope );

uintMSet getAllFactors(unsigned h);
void getAllFactors(unsigned h, uintSSet& ss);
void getAllFactorsAlt(unsigned h, uintSSet& ss);
uintMSet getPrimeFactors(unsigned h);
void getPrimeFactors(unsigned h, uintSSet& ss);

std::vector<bool> getPrimeSieve(unsigned top);
uintMSet getPrimes(unsigned top);

void findHarmonics(const ChartPlanetMap& cpm, PlanetHarmonics& hx);
void calculateBaseChartHarmonic(Horoscope& scope);

using uintPair = std::pair<unsigned,unsigned>;
using hsets = std::vector<uintSSet>;
using hsetId = unsigned short int;

struct planetsEtc : public uintPair {
    hsetId hsid;
    EventType et;

    using uintPair::uintPair;

    planetsEtc(const uintPair& ab, hsetId hs/*=0*/,
               EventType et /*= etcUnknownEvent*/) :
        uintPair(ab), hsid(hs), et(et)
    { }

    planetsEtc(unsigned a, unsigned b,
               hsetId hs /*= 0*/,
               EventType et /*= etcUnknownEvent*/) :
        uintPair(a,b), hsid(hs), et(et)
    { }

    unsigned a() const { return first; }
    unsigned b() const { return second; }
};

typedef QList<InputData> idlist;

class harmonize {
    idlist& _ids;
    QList<double> _was;

public:
    harmonize(idlist& ids, double harmonic) : _ids(ids)
    {
        for (auto idit = _ids.begin(); idit != _ids.end(); ++idit) {
            _was << idit->harmonic;
            idit->harmonic = harmonic;
        }
    }

    ~harmonize()
    {
        auto idit = _ids.begin();
        auto wit = _was.begin();
        while (idit != _ids.end()) (*idit++).harmonic = *wit++;
    }
};

struct EventOptions {
    EventOptions() { };
    EventOptions(const QVariantMap& map);

    ADateDelta  defaultTimespan { 0/*yr*/, 1/*mo*/, 0/*dy*/ };

    qreal       expandShowOrb = 2.;
    unsigned    patternsQuorum = 3;
    qreal       patternsSpreadOrb = 8.;

    bool        patternsRestrictMoon = true;
    bool        filterLowerUnselectedHarmonics = true;
    bool        includeMidpoints = false;

    bool        showStations = true;
    bool        includeShadowTransits = true;

    bool        showTransitsToTransits = true;
    bool        limitLunarTransits = true;
    bool        showTransitsToNatalPlanets = true;
    bool        showTransitsToNatalAngles = true;
    bool        showTransitsToHouseCusps = false;
    bool        includeOnlyOuterTransitsToNatal = false;

    bool        includeTransits() const
    {
        return showTransitsToTransits
                || showTransitsToNatalPlanets
                || showTransitsToNatalAngles
                || showTransitsToHouseCusps;
    }

    bool        showReturns = true;

    bool        showProgressionsToProgressions = false;
    bool        showProgressionsToNatal = false;
    bool        includeOnlyInnerProgressionsToNatal = true;

    bool        includeProgressions() const
    { return showProgressionsToNatal || showProgressionsToProgressions; }

    bool        showTransitAspectPatterns = true;
    bool        showTransitNatalAspectPatterns = true;

    bool        includeAspectPatterns() const
    { return showTransitAspectPatterns || showTransitNatalAspectPatterns; }

    bool        showIngresses = false;
    bool        showLunations = false;
    bool        showHeliacalEvents = false;
    bool        showPrimaryDirections = false;
    bool        showLifeEvents = false;

    bool        expandShowAspectPatterns = true;
    bool        expandShowHousePlacementsOfTransits = true;
    bool        expandShowRulershipTips = true;

    bool        expandShowStationAspectsToTransits = true;
    bool        expandShowStationAspectsToNatal = true;

    bool        expandShowReturnAspects = true;
    bool        expandShowTransitAspectsToReturnPlanet = true;

    QVariantMap toMap();

    static EventOptions& current() { static EventOptions s_; return s_; }
};

class EventFinderBase : public QRunnable {
public:
    virtual void find() = 0;
    void run(); // sets ephemeris path and then runs
};

struct EventFinder :
        public EventOptions,
        public QRunnable
{
public:
    EventFinder(HarmonicEvents& evs,
                const ADateRange& range) :
        EventOptions(current()), _evs(evs), _range(range)
    { }

    virtual ~EventFinder() { }

    HarmonicEvents& _evs;
    ADateRange _range;

    QList<InputData> _ids;
    PlanetProfile _alist;   ///< the planet objects to compute
};

class AspectFinder : public EventFinder {
public:
    enum goalType {
        afcFindStuff, afcFindAspects, afcFindPatterns, afcFindStations
    };

    AspectFinder(HarmonicEvents& evs,
                 const ADateRange& range,
                 const uintSSet& hset,
                 goalType gt = afcFindAspects) :
        EventFinder(evs, range),
        _gt(gt),
        _hsets({hset})
    {
        if (includeMidpoints || *hset.rbegin()>4) _rate = .5;
    }

    AspectFinder(HarmonicEvents& evs,
                 const ADateRange& range,
                 const uintSSet& hset,
                 const AstroFileList& files);

    static void prepThread();

    void findAspects();
    void findPatterns();

    void run() override;

    void findStations();
    void findStuff();

protected:
    unsigned _gt;
    QMutex _ctm;

    // having both here allows us to include aspects to stationary planets...
    bool _includeAspectsToAngles = true;
    double _rate = 4.0;  // # days

    bool outOfOrb(unsigned h,
                  std::initializer_list<const Loc*> locs,
                  qreal& d) const
    {
        auto delta = PlanetProfile::computeSpread(locs, h);
        d = delta.first;
        bool anyMidPoints = false;
        for (auto loc : locs) {
            auto ploc = dynamic_cast<const PlanetLoc*>(loc);
            if (ploc && ploc->planet.isMidpt()) {
                anyMidPoints = true;
                break;
            }
        }
        return d > (anyMidPoints? expandShowOrb/8 : expandShowOrb);
    }

    bool keepLooking(unsigned h, unsigned i) const
    {
        auto p = dynamic_cast<const PlanetLoc*>(_alist[i]);
        if (!p) return true;
        if (p->allowAspects >= PlanetLoc::aspOnlyConj) return false;
        PlanetId pid = p->planet.planetId();
        if (!_includeAspectsToAngles
                && (pid == Planet_MC || pid == Planet_Asc))
        { return h < 2; }
        if (p->inMotion() && pid == Planet_Moon) return h < 4;
        return true;
    }

    hsets _hsets;         ///< harmonic profiles
    std::list<planetsEtc> _staff;
    unsigned _evType = etcUnknownEvent;

private:
};

class CoincidenceFinder : public QRunnable {
    HarmonicAspects& _coins;

public:
    CoincidenceFinder(AspectFinder* from,
                      HarmonicAspects& coincidents);
    void run() override;
};

class EventTypeManager {
public:
    typedef std::tuple<unsigned char,QString,QString> eventTypeInfo;

    static EventTypeManager& singleton();
    static unsigned registerEventType(unsigned char chnum,
                                      const QString& abbr,
                                      const QString& desc);
    static unsigned registerEventType(const eventTypeInfo& evtinf);

private:
    unsigned _numEvents = etcUserEventStart;
    QMap<unsigned,eventTypeInfo> _eventIdToString;
    QMap<QString,unsigned> _eventStringToId;

    EventTypeManager();
    ~EventTypeManager() { }
};

class EventFinderFactory {
public:
    typedef QMap<EventType,QString>                 eventNameMap;
    typedef QMap<QString,QString>                   eventGlossMap;
    typedef QMap<QString,EventType>                 namedEventMap;
    typedef std::tuple<EventType,QString,QString>   eventRegistration;

    static unsigned addEventType(const eventRegistration& ev);
    static void addEventTypes(std::initializer_list<eventRegistration> evs);
    static EventFinder* makeFinder(unsigned et);

private:
    static eventNameMap     _eventNames;
    static eventGlossMap    _eventDescrs;
    static namedEventMap    _eventTypes;
};

PlanetClusterMap findClusters(unsigned h,
                              const PlanetProfile& prof,
                              unsigned quorum,
                              const PlanetSet& need = {},
                              bool skipAllNatalOnly = false,
                              bool restrictMoon = true,
                              qreal maxOrb = 8.);

PlanetClusterMap findClusters(unsigned h, double jd,
                              const PlanetProfile& prof,
                              const QList<InputData>& ids,
                              unsigned quorum,
                              const PlanetSet& need = {},
                              bool restrictMoon = true,
                              qreal maxOrb = 8.);

HarmonicPlanetClusters findClusters(const uintSSet& hs,
                                    const PlanetProfile& prof,
                                    unsigned quorum,
                                    const PlanetSet& need = {},
                                    bool skipAllNatalOnly = false,
                                    bool restrictMoon = true,
                                    qreal maxOrb = 8.);

qreal computeSpread(unsigned h,
                    double jd,
                    const PlanetProfile& prof,
                    const QList<InputData>& ids = {});

Planet      calculatePlanet      ( PlanetId planet, const InputData& input, const Houses& houses, const Zodiac& zodiac );
Star calculateStar(const QString&, const InputData& input, const Houses& houses, const Zodiac& zodiac);
PlanetPower calculatePlanetPower ( const Planet& planet, const Horoscope& scope );
Houses      calculateHouses      ( const InputData& input );
Aspect      calculateAspect      ( const AspectsSet& aspectSet, const Planet& planet1, const Planet& planet2 );
Aspect calculateAspect(const AspectsSet&, const Loc*, const Loc*);
AspectList  calculateAspects     ( const AspectsSet& aspectSet, const PlanetMap& planets );
AspectList  calculateAspects     ( const AspectsSet& aspectSet, const PlanetMap& planets1, const PlanetMap& planets2 );   // synastry
AspectList calculateAspects(const AspectsSet&, const ChartPlanetPtrMap& planets);

void calculateOrbAndSpan(const PlanetProfile& poses,
                         const InputData& locale,
                         double& orb,
                         double& horb,
                         double& span);
QDateTime calculateReturnTime(PlanetId pid,
                              const InputData& native,
                              const InputData& locale);
QDateTime calculateClosestTime(PlanetProfile& poses,
                               const InputData& locale);
QList<QDateTime> quotidianSearch(PlanetProfile& poses,
                                 const InputData& locale,
                                 const QDateTime& endDt,
                                 double span = 1.0);

Horoscope   calculateAll         ( const InputData& input );

}
#endif // A_CALC_H
