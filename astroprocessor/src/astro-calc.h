#ifndef A_CALC_H
#define A_CALC_H

#include "astro-data.h"


namespace A {  // Astrology, sort of :)
template <typename T>
class modalize {
    T _was;
    T& _state;

public:
    modalize(T& state, T newState) :
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
void findHarmonics(const ChartPlanetMap& cpm, PlanetHarmonics& hx);
void calculateBaseChartHarmonic(Horoscope& scope);

Planet      calculatePlanet      ( PlanetId planet, const InputData& input, const Houses& houses, const Zodiac& zodiac );
Star calculateStar(const QString&, const InputData& input, const Houses& houses, const Zodiac& zodiac);
PlanetPower calculatePlanetPower ( const Planet& planet, const Horoscope& scope );
Houses      calculateHouses      ( const InputData& input );
Aspect      calculateAspect      ( const AspectsSet& aspectSet, const Planet& planet1, const Planet& planet2 );
AspectList  calculateAspects     ( const AspectsSet& aspectSet, const PlanetMap& planets );
AspectList  calculateAspects     ( const AspectsSet& aspectSet, const PlanetMap& planets1, const PlanetMap& planets2 );   // synastry

QDateTime calculateReturnTime(PlanetId pid,
                              const InputData& native,
                              const InputData& locale);

Horoscope   calculateAll         ( const InputData& input );

}
#endif // A_CALC_H
