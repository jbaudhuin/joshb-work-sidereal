#ifndef A_CALC_H
#define A_CALC_H

#include "astro-data.h"
#include <QEventLoop>
#include <QRunnable>
#include <QThreadPool>

// Forward
class AstroFile;
typedef QList<AstroFile*> AstroFileList;

struct ADateDelta {
    int numDays   = 0;
    int numMonths = 0;
    int numYears  = 0;

    ADateDelta(const QString& str);

    ADateDelta(int days = 0, int months = 0, int years = 0) :
        numDays(days),
        numMonths(months),
        numYears(years)
    {
    }

    ADateDelta(QDate from, QDate to);

    QString toString() const
    {
        QStringList sl;
        bool        terse  = numYears && numMonths && numDays;
        auto        append = [&](const int& v, const QString& un) {
            if (!v) return;
            sl << (terse ? QString("%1%2").arg(v).arg(un.at(0))
                                : QString("%1 %2%3").arg(v).arg(un).arg(v != 1 ? "s"
                                                                        : ""));
        };
        append(numYears, "yr");
        append(numMonths, "mo");
        append(numDays, "day");
        return sl.join(terse ? " " : ", ");
    }

    QDate addTo(const QDate& d) const;
    QDate subtractFrom(const QDate& d) const;

    static ADateDelta fromString(const QString& str) { return ADateDelta(str); }

    bool operator==(const ADateDelta& other) const
    {
        return numYears == other.numYears && numMonths == other.numMonths
               && numDays == other.numDays;
    }

    bool operator!=(const ADateDelta& other) const
    {
        return !operator==(other);
    }

    operator bool() const { return numYears || numMonths || numDays; }
};

// A namespace
namespace A
{ // Astrology, sort of :)

template <typename T>
struct modalTrait {
    static const T& defaultNew()
    {
        static T dflt;
        return dflt;
    }
};

template <>
struct modalTrait<bool> {
    static bool defaultNew() { return true; }
};

template <typename T = bool>
class modalize {
    T  _was;
    T& _state;

  public:
    modalize(T& state, T newState = modalTrait<T>::defaultNew()) :
        _was(state),
        _state(state)
    {
        _state = newState;
    }

    ~modalize() { _state = _was; }

    operator const T&() const { return _state; }
    modalize& operator=(T newState)
    {
        _state = newState;
        return *this;
    }
};

/// RAII thread_local context.  Establishes a shared value that consumers
/// can query via active() / current().  Falls back gracefully when no
/// context is set (consumers must check active() first).
template <typename T>
class context {
    static thread_local T* _current;
    T                      _data;

  public:
    explicit context(T data) : _data(std::move(data))
    {
        _current = &_data;
    }
    ~context() { _current = nullptr; }

    context(const context&)            = delete;
    context& operator=(const context&) = delete;

    static bool     active() { return _current != nullptr; }
    static const T& current() { return *_current; }
};

template <typename T>
thread_local T* context<T>::_current = nullptr;

enum aspectModeEnum {
    amcUnknown = -1,
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

    aspectModeType(const QVariant& var) : _value(amcUnknown)
    {
        if (var.isValid()) {
            if (VAR_TYPE(var) == QMetaType::QString) {
                _value = fromString(var.toString());
            } else if (VAR_TYPE(var) == QMetaType::Int) {
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
        static QString aspectModeStr[] = { QObject::tr("Great Circle"),
                                           QObject::tr("Ecliptic"),
                                           QObject::tr("Equatorial"),
                                           QObject::tr("Prime Vertical") };
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
        static const char* s_strings[] = { "greatCircle",
                                           "ecliptic",
                                           "equatorial",
                                           "primeVertical" };
        return s_strings;
    }
};

extern aspectModeType aspectMode;

enum PrimDirMode { prdMundane, prdZodiacal, prdActive };
extern PrimDirMode primDirMode;
extern bool        useApparentSun;

bool
isSolarReturn(const QString& chartName);
bool
isSolarIngress(const QString& chartName);
bool
isSolarBasedChart(const QString& chartName);

double
getJulianDate(QDateTime    GMT,
             bool         ephemerisTime = false,
             CalendarType calType       = Cal_Auto);

/// Equation of time in fractional days (add to LMT to get LAT).
/// Returns 0 on error.
double
equationOfTime(double jdUT);

/// Convert a Local Mean Time JD to Local Apparent Time JD.
double
lmtToLat(double jdLMT, double geolon);

/// Convert a Local Apparent Time JD to Local Mean Time JD.
double
latToLmt(double jdLAT, double geolon);

/// Convert a local time QDateTime to UTC, respecting the TimeMode.
/// For Time_ZoneTime: subtract tz hours.
/// For Time_LMT:     subtract longitude/15 hours.
/// For Time_LAT:     convert LAT→LMT then LMT→UT.
QDateTime
localToUTC(const QDateTime& localDt,
           double           tz,
           double           geolon,
           TimeMode         mode,
           CalendarType     calType = Cal_Auto);

/// Compute the equation of time in seconds for a given UTC moment and
/// longitude, returning both the EoT value and the LAT equivalent.
struct EoTInfo {
    double eotSeconds;   ///< Equation of time in seconds (+means sundial ahead)
    double latJD;        ///< Local Apparent Time as JD
    double lmtJD;        ///< Local Mean Time as JD
    bool   valid;
};
EoTInfo
computeEoT(const QDateTime& utcDt, double geolon,
           CalendarType calType = Cal_Auto);

// ---------------------------------------------------------------------------
// Ex-precession: shift natal equatorial positions to a transit epoch
// ---------------------------------------------------------------------------

/// Cached natal-epoch values shared across all ex-precessed positions
/// for the same natal chart.  Used with context<ExprecessNatalEpoch>.
struct ExprecessNatalEpoch {
    double jdNatal;
    double obliquity;   ///< Mean obliquity at natal JD (degrees)
    double ayanamsa;    ///< Ayanamsa at natal JD (degrees)
};

/// Result of a single ex-precession computation.
struct ExprecessedEquatorial {
    double ra;          ///< Ex-precessed RA at target epoch (degrees)
    double dec;         ///< Ex-precessed declination at target epoch (degrees)
    double raSpeed;     ///< dRA/dt at target epoch (degrees/day)
    double decSpeed;    ///< dDec/dt at target epoch (degrees/day)
};

/// Compute ex-precessed RA/Dec and their rates at jd_t2, given natal
/// RA/Dec at jd_t1.  Uses pre-cached natal-epoch ecliptic coordinates
/// to avoid redundant conversions.
///
/// If a context<ExprecessNatalEpoch> is active AND its jdNatal matches
/// jd_t1, the cached obliquity/ayanamsa are used; otherwise they are
/// computed from scratch.
ExprecessedEquatorial
exprecess_equatorial(double ra_t1_deg,
                     double dec_t1_deg,
                     double eclLon_t1_deg,
                     double eclLat_t1_deg,
                     double jd_t1,
                     double jd_t2,
                     double dt_days = 0.01);

/// Convenience overload: computes natal ecliptic coords internally.
ExprecessedEquatorial
exprecess_equatorial(double ra_t1_deg,
                     double dec_t1_deg,
                     double jd_t1,
                     double jd_t2,
                     double dt_days = 0.01);

float
roundDegree(float deg); // returns 0...360
const ZodiacSign&
getSign(float deg, const Zodiac& zodiac);
int
getHouse(const Houses& houses, float deg); // returns 1...12
int
getHouse(ZodiacSignId sign, const Houses& houses, const Zodiac& zodiac);
int
getHouse(const Horoscope& scope, float deg);

float
angle(const Star& body1, const Star& body2);
float
angle(const Star& body, float deg);
float
angle(const Star& body, QPointF coordinate);
float
angle(float deg1, float deg2);
AspectId
aspect(const Star& planet1, const Star& planet2, const AspectsSet& aspectSet);
AspectId
aspect(const Star& planet, QPointF coordinate, const AspectsSet& aspectSet);
AspectId
aspect(const Star& planet1, float degree, const AspectsSet& aspectSet);
AspectId
aspect(float angle, const AspectsSet& aspectSet);
bool
towardsMovement(const Star& planet1, const Star& planet2);
PlanetPosition
getPosition(const Planet& planet, ZodiacSignId sign);
const Planet*
doryphoros(const Horoscope& scope);
const Planet*
auriga(const Horoscope& scope);
const Planet*
almuten(const Horoscope& scope);
bool
rulerDisposition(int house, int houseAuthority, const Horoscope& scope);
bool
isEarlier(const Planet& planet, const Planet& sun);
const Planet&
ruler(int house, const Horoscope& scope);
PlanetId
receptionWith(const Planet& planet, const Horoscope& scope);

uintMSet
getAllFactors(unsigned h);
void
getAllFactors(unsigned h, uintSSet& ss);
void
getAllFactorsAlt(unsigned h, uintSSet& ss);
uintMSet
getPrimeFactors(unsigned h);
void
getPrimeFactors(unsigned h, uintSSet& ss);

std::vector<bool>
getPrimeSieve(unsigned top);
uintMSet
getPrimes(unsigned top);

void
findHarmonics(const ChartPlanetMap& cpm, PlanetHarmonics& hx);
void
calculateBaseChartHarmonic(Horoscope& scope);

typedef QList<InputData> idlist;

struct EventOptions {
    EventOptions();
    EventOptions(const QVariantMap& map);
    EventOptions(const EventOptions& orig) = default;
    EventOptions(const EventOptions& orig, const EventTypeSet& exclude);

    ADateDelta defaultTimespan { 0 /*yr*/, 1 /*mo*/, 0 /*dy*/ };

    static const QString&            zposPat();
    static const QRegularExpression& zposRE();
    static const QString&            eventPat();
    static const QRegularExpression& eventRE();
    static const QString&            eventMultiPat();
    static const QRegularExpression& eventMultiRE();
    static bool                      isValidPattern(const QString& text);

    qreal    expandShowOrb     = 2.;
    unsigned patternsQuorum    = 3;
    qreal    patternsSpreadOrb = 8.;
    qreal    planetPairOrb     = 2.;

    bool patternsRestrictMoon           = true;
    bool filterLowerUnselectedHarmonics = true;
    bool includeMidpoints               = false;

    bool includeShadowTransits = true;

    bool includeTransitRange             = true;
    bool limitLunarTransits              = true;
    bool includeOnlyOuterTransitsToNatal = false;
    bool includeAsteroids                = false;
    bool includeCentaurs                 = true;

    bool includeOnlyInnerProgressionsToNatal = true;

    enum skipper {
        SkipNone,
        SkipLessThanDay,
        SkipLessThanWeek,
        SkipLessThanMonth
    };
    skipper skipByDuration = SkipNone;

    bool skippable(const JDateRange& r, EventType et) const
    {
        return skippableEvent(et) && skippablePeriod(r);
    }

    bool skippablePeriod(const JDateRange& r) const
    {
        switch (skipByDuration) {
        case SkipNone:          return false;
        case SkipLessThanDay:   return r.second - r.first < 1.0;
        case SkipLessThanWeek:  return r.second - r.first < 7.0;
        case SkipLessThanMonth: return r.second - r.first < 28.0;
        }
        return false;
    }

    bool skippableEvent(EventType et) const
    {
        return et == etcTransitToTransit || et == etcTransitToNatal;
    }

    bool expandShowAspectPatterns            = true;
    bool expandShowHousePlacementsOfTransits = true;
    bool expandShowRulershipTips             = true;

    bool expandShowStationAspectsToTransits = true;
    bool expandShowStationAspectsToNatal    = true;

    bool expandShowReturnAspects                = true;
    bool expandShowTransitAspectsToReturnPlanet = true;

    bool showHarmonicDividend = false;

    // Display modes for column content: 0=default glyphs, 1=rulership,
    // 2=rulership+natal_house
    enum DisplayMode {
        DisplayGlyphs                  = 0,
        DisplayRulership               = 1,
        DisplayRulershipWithNatalHouse = 2
    };
    static DisplayMode s_transitBodyColMode;
    static DisplayMode s_natalTransitBodyColMode;

    // Core enabled events set
    EventTypeSet enabledEvents;

    // Per-event-type harmonic restrictions (event type → max harmonic allowed)
    // If an event type is present in this map, events with harmonic > max are filtered out
    QMap<EventType, unsigned> harmonicRestrictions;

    // Event type accessors
    bool isEnabled(EventType et) const { return enabledEvents.count(et) > 0; }
    void setEnabled(EventType et, bool enabled = true)
    {
        if (enabled) enabledEvents.insert(et);
        else
            enabledEvents.erase(et);
    }
    void enable(EventType et) { enabledEvents.insert(et); }
    void disable(EventType et) { enabledEvents.erase(et); }

    // Variadic template accessors using C++17 fold expressions
    template <typename... EventTypes>
    bool allEnabled(EventTypes... events) const
    {
        return (... && isEnabled(events));
    }

    template <typename... EventTypes>
    bool anyEnabled(EventTypes... events) const
    {
        return (... || isEnabled(events));
    }

    template <typename... EventTypes>
    void setEnabled(bool enabled, EventTypes... events)
    {
        (setEnabled(events, enabled), ...);
    }

    // Backward-compatible accessors (map to EventType checks)
    bool showStations() const { return isEnabled(etcStation); }
    void setShowStations(bool b) { setEnabled(etcStation, b); }

    bool showTransitsToTransits() const
    {
        return isEnabled(etcTransitToTransit);
    }
    void setShowTransitsToTransits(bool b)
    {
        setEnabled(etcTransitToTransit, b);
    }

    bool showTransitsToNatalPlanets() const
    {
        return isEnabled(etcTransitToNatal);
    }
    void setShowTransitsToNatalPlanets(bool b)
    {
        setEnabled(etcTransitToNatal, b);
    }

    bool showTransitsToNatalAngles() const
    {
        return isEnabled(etcTransitToNatalAngles);
    }
    void setShowTransitsToNatalAngles(bool b)
    {
        setEnabled(etcTransitToNatalAngles, b);
    }

    bool showTransitsToHouseCusps() const { return isEnabled(etcHouseIngress); }
    void setShowTransitsToHouseCusps(bool b) { setEnabled(etcHouseIngress, b); }

    bool includeTransits() const
    {
        return anyEnabled(etcTransitToTransit,
                          etcTransitToNatal,
                          etcTransitToNatalAngles,
                          etcOuterTransitToNatal,
                          etcHouseIngress);
    }

    bool showReturns() const { return isEnabled(etcReturn); }
    void setShowReturns(bool b)
    {
        setEnabled(etcReturn, b);
        setEnabled(etcSolarReturn, b);
        setEnabled(etcLunarReturn, b);
    }

    bool showProgressionsToProgressions() const
    {
        return isEnabled(etcProgressedToProgressed);
    }
    void setShowProgressionsToProgressions(bool b)
    {
        setEnabled(etcProgressedToProgressed, b);
    }

    bool showProgressionsToNatal() const
    {
        return isEnabled(etcProgressedToNatal)
               || isEnabled(etcInnerProgressedToNatal);
    }
    void setShowProgressionsToNatal(bool b)
    {
        setEnabled(etcProgressedToNatal, b);
        setEnabled(etcInnerProgressedToNatal, b);
    }

    bool includeProgressions() const
    {
        return anyEnabled(etcProgressedToProgressed,
                          etcProgressedToNatal,
                          etcInnerProgressedToNatal);
    }

    bool showTransitAspectPatterns() const
    {
        return isEnabled(etcTransitAspectPattern);
    }
    void setShowTransitAspectPatterns(bool b)
    {
        setEnabled(etcTransitAspectPattern, b);
    }

    bool showTransitNatalAspectPatterns() const
    {
        return isEnabled(etcTransitNatalAspectPattern);
    }
    void setShowTransitNatalAspectPatterns(bool b)
    {
        setEnabled(etcTransitNatalAspectPattern, b);
    }

    bool includeAspectPatterns() const
    {
        return isEnabled(etcTransitAspectPattern)
               || isEnabled(etcTransitNatalAspectPattern);
    }

    bool showIngresses() const { return isEnabled(etcSignIngress); }
    void setShowIngresses(bool b) { setEnabled(etcSignIngress, b); }

    bool showLunations() const { return isEnabled(etcLunation); }
    void setShowLunations(bool b) { setEnabled(etcLunation, b); }

    bool showHeliacalEvents() const { return isEnabled(etcHeliacalEvents); }
    void setShowHeliacalEvents(bool b) { setEnabled(etcHeliacalEvents, b); }

    bool showParanatellonta() const { return isEnabled(etcParanatellonta); }
    void setShowParanatellonta(bool b) { setEnabled(etcParanatellonta, b); }

    bool showParanatellontaToNatal() const { return isEnabled(etcParanatellontaToNatal); }
    void setShowParanatellontaToNatal(bool b) { setEnabled(etcParanatellontaToNatal, b); }

    bool showPrimaryDirections() const
    {
        return isEnabled(etcPrimaryDirections);
    }
    void setShowPrimaryDirections(bool b)
    {
        setEnabled(etcPrimaryDirections, b);
    }

    bool showLifeEvents() const
    {
        // User events are >= etcUserEventStart
        for (auto et : enabledEvents) {
            if (et >= etcUserEventStart) return true;
        }
        return false;
    }

    QVariantMap toMap();

    // Global settings singleton (orbs, durations, expand options, etc.)
    // Used for reading/writing persistent settings and as template for new tabs
    static EventOptions& current()
    {
        static EventOptions s_;
        return s_;
    }

    // Global defaults for event types, used when initializing new chart tabs
    // This controls which events are enabled by default in newly opened charts
    static EventTypeSet& globalDefaults()
    {
        return current().enabledEvents;
    }
};

class EventFinderBase : public QRunnable {
  public:
    virtual void find() = 0;
    void         run(); // sets ephemeris path and then runs
};

class AspectFinder : public QObject, public EventOptions {
    Q_OBJECT

  public:
    enum state {
        idleState,
        runningState,
        cancelRequestedState,
        pauseRequestedState
    };
    enum goalType {
        afcFindStuff,
        afcFindAspects,
        afcFindPatterns,
        afcFindStations
    };

    AspectFinder(HarmonicEvents& evs, const ADateRange& range) :
        _evs(evs),
        _range(range)
    {
    }

    AspectFinder(HarmonicEvents&     evs,
                 const ADateRange&   range,
                 const uintSSet&     hset,
                 const EventTypeSet& exclude = {},
                 goalType            gt      = afcFindAspects) :
        EventOptions(current(), exclude),
        _evs(evs),
        _range(range),
        _gt(gt),
        _hsets({ hset })
    {
        if (!exclude.empty()) _restrictiveTimeRange = true;
        // if (includeMidpoints || *hset.rbegin()>4) _rate = .5;
    }

    virtual ~AspectFinder() { }

    void findAspects();
    void findPatterns();

    void findStations();
    void findAspectsAndPatterns();

    bool isActive() const { return _numTasks != 0; }
    bool isPaused() const { return _state.loadRelaxed() == pauseRequestedState; }
    int  getState() const { return _state.loadRelaxed(); }

    struct AspectSearchState {
        PlanetSet     nats;
        PlanetSet     trans;
        PlanetSet     progs;  // Progressed planets (excluded from aspect patterns for now)
        PlanetProfile b;
        bool          skipAllNatalOnly;
        bool          showPatterns;
        bool          onlyProgressedAndNatal; // True when only slow-moving P=P/P=N aspects remain

        QDateTime d, e;
        double    jd, bjd, ejd, pjd, ljd;
        QDateTime nd;

        uintSSet hs;
        unsigned maxH;
        unsigned h;

        std::function<bool(unsigned, bool)> keep;
        double                              useRate;
        int                                 ndays;
        int                                 nsecs;

        std::map<HarmonicPlanetSet, JDateRangeTasks> inOrb;
        std::map<HarmonicPlanetSet, JDateRangeHits>  proximityLog;
        std::map<unsigned, PlanetClusterMap>         starts;
        std::map<unsigned, PlanetClusterMap>         work;

        PlanetProfile*                               useProf;
        std::unique_ptr<PlanetProfile>               doomed;
        searchPairList                               stuff;
        std::map<HarmonicPlanetSet, JDateRangeTasks> tinOrb;

        // Exact-pattern tracking: per-ExactPatternSpec, per-harmonic
        // Keyed by index into _exactPatterns, then harmonic → ClusterOrbWhen
        // When a pattern is in-orb, its start jd is recorded here.
        std::map<std::pair<unsigned /*specIdx*/, unsigned /*h*/>, ClusterOrbWhen> exactStarts;
        std::map<std::pair<unsigned /*specIdx*/, unsigned /*h*/>, ClusterOrbWhen> exactWork;
    };

  signals:
    void progress(double p);

  public slots:
    void pause()
    {
        if (_state == runningState) _state = pauseRequestedState;
    }
    void resume()
    {
        if (_state == pauseRequestedState) _state = runningState;
    }
    void cancel()
    {
        auto thread = QThread::currentThread();
        qDebug() << "[CANCEL SLOT]" << thread << thread->objectName() << "- cancel() called, current state:" << _state.loadRelaxed();
        int s = _state.loadRelaxed();
        if (s == runningState || s == pauseRequestedState) {
            _state = cancelRequestedState;
            qDebug() << "[CANCEL SLOT]" << thread << thread->objectName() << "- State changed to cancelRequestedState";
        } else {
            qDebug() << "[CANCEL SLOT]" << thread << thread->objectName() << "- State was NOT runningState/pauseRequestedState, no change made";
        }
    }
    void findStuff();

  protected:
    static void prepThread();
    static void releaseThread();

    void startTask()
    {
        prepThread();
        ++_numTasks;
    }
    void endTask()
    {
        releaseThread();
        --_numTasks;
    }

    bool outOfOrb(unsigned                          h,
                  std::initializer_list<const Loc*> locs,
                  qreal&                            d) const

    {
        auto delta = PlanetProfile::computeSpread(locs, h);
        d          = delta.first;

        bool anyMidPoints = false;
        for (auto loc : locs) {
            auto ploc = dynamic_cast<const PlanetLoc*>(loc);
            if (ploc && ploc->planet.isMidpt()) {
                anyMidPoints = true;
                break;
            }
        }
        return d > (anyMidPoints ? patternsSpreadOrb / 8 : patternsSpreadOrb);
    }

    bool keepLooking(unsigned h, unsigned i) const
    {
        auto p = dynamic_cast<const PlanetLoc*>(_alist[i]);
        if (!p) return true;
        if (p->allowAspects >= PlanetLoc::aspOnlyConj) return false;

        PlanetId pid = p->planet.planetId();
        if (!_includeAspectsToAngles && (pid == Planet_MC || pid == Planet_Asc))
        {
            return h < 2;
        }

        if (p->inMotion() && pid == Planet_Moon) return h < 4;
        return true;
    }

    void findPriorStarts(AspectSearchState& state);
    void findNewStarts(AspectSearchState&             state,
                       bool                           collectingStrays,
                       std::unique_ptr<PlanetProfile>& useProf);
    void findExactPatterns(AspectSearchState& state);
    void findTransitPairs(AspectSearchState& state);
    void findAspects(AspectSearchState& state, modalize<bool>& mum);
    void findRemainingAspects(AspectSearchState& state);

    QThreadPool* _tp;  // Points to global thread pool, not owned

    HarmonicEvents& _evs;
    ADateRange      _range;

    QList<InputData> _ids;
    PlanetProfile    _alist; ///< the planet objects to compute

    unsigned _gt;

    QMutex     _ctm;
    QAtomicInt _numTasks;

    // having both here allows us to include aspects to stationary planets...
    bool _includeAspectsToAngles = true;

    QAtomicInt _state = idleState;

    bool _restrictiveTimeRange = false;

    double           _rate = 4.0; // # days
    hsets            _hsets;      ///< harmonic profiles
    searchPairList   _staff;
    ExactPatternList _exactPatterns; ///< exact N-body pattern searches
    bool             _generalClustersEnabled = false; ///< true when findClusters() should scan
    bool             _patternMode = false; ///< true when initialized via pattern (not toolbar)
    unsigned         _evType = etcUnknownEvent;

    ExprecessNatalEpoch _exprecessCtx {};    ///< cached natal-epoch values for ex-precession
    bool                _hasExprecessCtx = false; ///< true when equatorial mode with natal file

    friend class PairAspectFinder;
    friend class TaskTracker;

  private:
};

class OmnibusFinder : public AspectFinder {
  private:
    void initializeFromFiles(const AstroFileList& files);
    void initializeFromPattern(const QString& pattern, const AstroFileList& files);

  public:
    OmnibusFinder(HarmonicEvents&      evs,
                  const ADateRange&    range,
                  const uintSSet&      hset,
                  const AstroFileList& files,
                  const EventTypeSet&  exclude = {});

    OmnibusFinder(HarmonicEvents&      evs,
                  const ADateRange&    range,
                  const uintSSet&      hset,
                  const AstroFileList& files,
                  const EventOptions&  options,
                  const EventTypeSet&  exclude = {});

    OmnibusFinder(HarmonicEvents&      evs,
                  const ADateRange&    range,
                  const uintSSet&      hset,
                  const AstroFileList& files,
                  const QString&       pattern,
                  const EventTypeSet&  exclude = {});
};

class CoincidenceFinder : public QRunnable {
    HarmonicAspects& _coins;

  public:
    CoincidenceFinder(AspectFinder* from, HarmonicAspects& coincidents);
    void run() override;
};

class EventTypeManager {
  public:
    typedef std::tuple<unsigned char, QString, QString> eventTypeInfo;

    static EventTypeManager& singleton();
    static unsigned          registerEventType(unsigned char  chnum,
                                               const QString& abbr,
                                               const QString& desc);
    static unsigned          registerEventType(const eventTypeInfo& evtinf);
    static QString           eventTypeToString(EventType et)
    {
        return std::get<2>(singleton()._eventIdToString.value(et));
    }

    static QString eventTypeToBrief(EventType et)
    {
        return std::get<1>(singleton()._eventIdToString.value(et));
    }

    static EventType briefToEventType(const QString& brief)
    {
        return static_cast<EventType>(singleton()._eventStringToId.value(brief, etcUnknownEvent));
    }

  private:
    unsigned                      _numEvents = etcUserEventStart;
    QMap<unsigned, eventTypeInfo> _eventIdToString;
    QMap<QString, unsigned>       _eventStringToId;

    EventTypeManager();
    ~EventTypeManager() { }
};

class EventFinderFactory {
  public:
    typedef QMap<EventType, QString>                eventNameMap;
    typedef QMap<QString, QString>                  eventGlossMap;
    typedef QMap<QString, EventType>                namedEventMap;
    typedef std::tuple<EventType, QString, QString> eventRegistration;

    static unsigned addEventType(const eventRegistration& ev);
    static void     addEventTypes(std::initializer_list<eventRegistration> evs);
    static AspectFinder* makeFinder(unsigned et);

  private:
    static eventNameMap  _eventNames;
    static eventGlossMap _eventDescrs;
    static namedEventMap _eventTypes;
};

PlanetClusterMap
findClusters(unsigned             h,
             const PlanetProfile& prof,
             unsigned             quorum,
             const PlanetSet&     need              = {},
             bool                 skipAllNatalOnly  = false,
             bool                 restrictMoon      = true,
             qreal                maxOrb            = 8.,
             bool                 excludeProgressed = false);

PlanetClusterMap
findClusters(unsigned                h,
             double                  jd,
             PlanetProfile&          prof,
             const QList<InputData>& ids,
             unsigned                quorum,
             const PlanetSet&        need         = {},
             bool                    restrictMoon = true,
             qreal                   maxOrb       = 8.,
             bool                    excludeProgressed = false);

HarmonicPlanetClusters
findClusters(const uintSSet&      hs,
             const PlanetProfile& prof,
             unsigned             quorum,
             const PlanetSet&     need             = {},
             bool                 skipAllNatalOnly = false,
             bool                 restrictMoon     = true,
             qreal                maxOrb           = 8.);

qreal
computeSpread(unsigned                h,
              double                  jd,
              PlanetProfile&          prof,
              const QList<InputData>& ids);

inline qreal
computeSpread(unsigned h, const PlanetProfile& prof)
{
    // jd=0, ids={} → all bodies use cached _rasiLoc (no mutation)
    auto& mprof = const_cast<PlanetProfile&>(prof);
    return computeSpread(h, 0, mprof, {});
}

Planet
calculatePlanet(PlanetId         planet,
                const InputData& input,
                const Houses&    houses,
                const Zodiac&    zodiac);
Star
calculateStar(const QString&,
              const InputData& input,
              const Houses&    houses,
              const Zodiac&    zodiac);
PlanetPower
calculatePlanetPower(const Planet& planet, const Horoscope& scope);
Houses
calculateHouses(const InputData& input);
Houses
calculateHouses(const InputData& input, double progressedMC);
Aspect
calculateAspect(const AspectsSet& aspectSet,
                const Planet&     planet1,
                const Planet&     planet2);
Aspect
calculateAspect(const AspectsSet&, const Loc*, const Loc*);
AspectList
calculateAspects(const AspectsSet& aspectSet, const PlanetMap& planets);
AspectList
calculateAspects(const AspectsSet& aspectSet,
                 const PlanetMap&  planets1,
                 const PlanetMap&  planets2); // synastry
AspectList
calculateAspects(const AspectsSet&, const ChartPlanetPtrMap& planets);

void
calculateOrbAndSpan(const PlanetProfile& poses,
                    const InputData&     locale,
                    double               harmonic,
                    double&              orb,
                    double&              horb,
                    double&              span);
QDateTime
calculateReturnTime(PlanetId         pid,
                    const InputData& native,
                    const InputData& locale,
                    double           harmonic);
QDateTime
calculateClosestTime(PlanetProfile&   poses,
                     const InputData& locale,
                     double           harmonic);
QList<QDateTime>
quotidianSearch(PlanetProfile&   poses,
                const InputData& locale,
                const QDateTime& endDt,
                double           span,
                bool             forceMin);

// PSSR (Progressed Sidereal Solar Return) functions

// Context structure for caching PSSR calculations
struct PSSRContext {
    double    anniversarySecond = 0.0; // Rate: degrees per day
    QDateTime returnTime;              // Time of the return chart
    double    returnRAMS = 0.0;        // Return Sun's RA (mean or apparent)
    double    returnRAMC = 0.0;        // Return chart RAMC
    double    nextReturnRAMC = 0.0;    // Following year's return RAMC
    bool      isValid = false;         // Whether this context is valid
    bool      useApparentSun = true;   // true = RAAS (apparent), false = RAMS (mean)
    
    PSSRContext() = default;
};

double
calculateRAMS(const QDateTime& dt, bool useApparentSun = true);

double
calculateAnniversarySecond(const Houses& return1, const Houses& return2);

double
calculatePSSRRAMC(const Houses&    returnHouses,
                  const QDateTime& returnTime,
                  const QDateTime& eventTime,
                  double           anniversarySecond,
                  bool             useApparentSun = true);

// Calculate PSSR context for a return chart (caches anniversary second)
PSSRContext
calculatePSSRContext(const Horoscope& returnChart, bool useApparentSun = true);

// Convert angleTransitMode index (0=Rise, 1=Set, 2=MC, 3=IC) to string
inline QString angleTransitName(int index)
{
    static const char* names[] = { "Rise", "Set", "MC", "IC" };
    return (index >= 0 && index < 4) ? names[index] : "?";
}

// Unified function to calculate angular dates using either PD or PSSR
// If pssrCtx is null or invalid, uses Primary Direction (Naibod rate: 1°/day)
// If pssrCtx is valid, uses PSSR: finds when Sun reaches the RAMS position
//   needed for the planet to cross the specified angle
// Parameters:
//   radixTime: The return chart time (or natal time for PD)
//   angleTime: The actual transit time of the planet crossing the angle
//   planetRA: The planet's Right Ascension in degrees
//   angleRA: The angle's Right Ascension in degrees (Asc RAAC, MC RAMC, etc.)
//   pssrCtx: PSSR context (null for PD mode)
QDateTime
calculateAngularDate(const QDateTime&   radixTime,
                     const QDateTime&   angleTime,
                     double             planetRA,
                     double             angleRA,
                     const PSSRContext* pssrCtx = nullptr,
                     const QString&     debugLabel = {});

Horoscope
calculateAll(const InputData& input);

// ============================================================================
// Chart Presets — loaded from bin/astroprocessor/chart-presets.csv
// ============================================================================

struct ChartPreset {
    EventTypeSet                enabledEvents;
    ADateDelta                  timespan;
    ADateDelta                  startOffset;
    QMap<EventType, unsigned>   harmonicFilters;   // event type → max harmonic (0 = unrestricted)
    QString                     pattern;

    // Look up a preset by originating event type; returns nullptr if none
    static const ChartPreset* forEvent(EventType et);

    // Load all presets from CSV (call once at startup)
    static void loadAll();

  private:
    static QMap<EventType, ChartPreset>& presets();
};

} // namespace A
#endif // A_CALC_H
