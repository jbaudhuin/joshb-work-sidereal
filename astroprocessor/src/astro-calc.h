#ifndef A_CALC_H
#define A_CALC_H

#include "astro-data.h"
#include <QFuture>

namespace A {  // Astrology, sort of :)

template <typename T> struct modalTrait
{ static const T& defaultNew() { static T dflt; return dflt; } };

template <>
struct modalTrait<bool> {
    static bool defaultNew() { return true; }
};

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
            if (val == strings()[i]) {
                return aspectModeEnum(i);
            }
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

uintSet getAllFactors(unsigned h);
uintSet getPrimeFactors(unsigned h);
std::vector<bool> getPrimeSieve(unsigned top);
uintSet getPrimes(unsigned top);

void findHarmonics(const ChartPlanetMap& cpm, PlanetHarmonics& hx);
void calculateBaseChartHarmonic(Horoscope& scope);

using uintPair = std::pair<unsigned,unsigned>;

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

struct EventFinder : public QRunnable {
public:
    EventFinder(HarmonicEvents& evs,
                const ADateRange& range) :
        _evs(evs), _range(range)
    { }

    ~EventFinder()
    { }

    HarmonicEvents& _evs;
    ADateRange _range;

    QList<InputData> _ids;
    PlanetProfile _alist;   ///< the planet objects to compute
};

class AspectFinder : public EventFinder {
public:
    AspectFinder(HarmonicEvents& evs,
                 const ADateRange& range,
                 const uintQSet& hset) :
        EventFinder(evs, range),
        _hset(hset)
    { }

    void run() override;

    void setIncludeStations(bool b)
    {
        if (!b) {
            _includeStations =
                    _includeStationAspectsToNatal =
                    _includeStationAspectsToTransits = false;
        } else  _includeStations = true;
    }

protected:
    // ** Future config options or control options for transit set **
    bool _includeTransits = true; // todo: could have separate func for stns
    // having both here allows us to include aspects to stationary planets...
    bool _includeAspectsToAngles = true;
    bool _includeStations = true;
    bool _includeStationAspectsToTransits = true;
    bool _includeStationAspectsToNatal = true;
    bool _includeReturnAspects = true; // todo: solar, solar+lunar, all, none
    bool _includeTransitAspectsToReturnPlanet = true;
    bool _filterLowerUnselectedHarmonics = true;
    unsigned _rate = 4;  // # days
    double _orb = 2.0;   // orb for aspects to stations or return aspects

    bool keepLooking(unsigned h, unsigned i) const
    {
        auto p = dynamic_cast<PlanetLoc*>(_alist[i]);
        PlanetId pid = p->planet.planetId();
        if (!_includeAspectsToAngles
                && (pid == Planet_MC || pid == Planet_Asc))
        { return h < 2; }
        if (p->inMotion() && pid == Planet_Moon) return h < 2;
        return true;
    }

    uintQSet _hset;         ///< harmonic profile
    std::list<uintPair> _staff;
    EventType _evType = etcUnknownEvent;

private:
};

class TransitFinder : public AspectFinder {
public:
    TransitFinder(HarmonicEvents& ev,
                  const ADateRange& range,
                  const uintQSet& hs,
                  const InputData& trainp,
                  const PlanetSet& tran);
};

class NatalTransitFinder : public AspectFinder {
public:
    NatalTransitFinder(HarmonicEvents& ev,
                       const ADateRange& range,
                       const uintQSet& hs,
                       const InputData& natinp,
                       const InputData& trainp,
                       const PlanetSet& natal,
                       const PlanetSet& tran,
                       bool includeTransitsToTransits = false);
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

Planet      calculatePlanet      ( PlanetId planet, const InputData& input, const Houses& houses, const Zodiac& zodiac );
Star calculateStar(const QString&, const InputData& input, const Houses& houses, const Zodiac& zodiac);
PlanetPower calculatePlanetPower ( const Planet& planet, const Horoscope& scope );
Houses      calculateHouses      ( const InputData& input );
Aspect      calculateAspect      ( const AspectsSet& aspectSet, const Planet& planet1, const Planet& planet2 );
AspectList  calculateAspects     ( const AspectsSet& aspectSet, const PlanetMap& planets );
AspectList  calculateAspects     ( const AspectsSet& aspectSet, const PlanetMap& planets1, const PlanetMap& planets2 );   // synastry

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
