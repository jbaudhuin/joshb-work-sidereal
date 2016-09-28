#ifndef A_DATA_H
#define A_DATA_H

#include <QString>
#include <QDateTime>
#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QVariant>

namespace A {


typedef int ZodiacSignId;
typedef int ZodiacId;
typedef int AspectId;
typedef int HouseSystemId;
typedef int PlanetId;
typedef int AspectSetId;


const PlanetId      Planet_None          = -1;
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
  double         Asc, MC, RAMC, Vx, EA, startSpeculum;
  const HouseSystem* system;

  Houses() { for (int i = 0; i < 12; i++) cusp[i] = 0;
             system = 0;
             Asc = MC = RAMC = Vx = EA = startSpeculum = 0;
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

    QPointF        horizontalPos;       // x - azimuth (0... 360), y - height (0... 360)
    QPointF        eclipticPos;         // x - longitude (0... 360), y - latitude (0... 360)
    QPointF        equatorialPos;       // x - rectascension, y - declination
    double         distance;            // A.U. (astronomical units)
    int            house;

    Star() : angleTransit(4) {
        id = Planet_None;
        configuredWithPlanet = false;
        horizontalPos = QPoint(0,0);
        eclipticPos   = QPoint(0,0);
        equatorialPos = QPoint(0,0);
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
  PlanetPosition position;
  PlanetPower    power;
  const ZodiacSign* sign;
  int            houseRuler;

  Planet() { sweNum = 0;
             sweFlags = 0;
             isReal = false;
             eclipticSpeed = QVector2D(0,0);
             position = Position_Normal;
             sign = 0;
             houseRuler = 0; }

  operator Planet*() { return this; }
  operator const Planet*() const { return this; }

  PlanetId      getPlanetId() const { return id; }
  virtual int   getSWENum() const { return sweNum; }
  virtual bool  isStar() const { return false; }
  virtual bool  isAsteroid() const { return false; }
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

  Aspect()    { planet1 = 0;
                planet2 = 0;
                angle   = 0;
                orb     = 0;
                d       = 0;
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
typedef QMap<QString, Star> StarMap;

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

void load(QString language);
QString usedLanguage();
const Planet& getPlanet(PlanetId id);
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

  InputData() {
      GMT.setTimeSpec(Qt::UTC);
      GMT.setTime_t(0);
      location    = QVector3D(0,0,0);
      houseSystem = Housesystem_Placidus;
      zodiac      = Zodiac_Tropical;
      aspectSet   = AspectSet_Default;
      tz          = 0;
  }
};

struct Horoscope
{
  InputData  inputData;
  Zodiac     zodiac;
  Houses     houses;
  AspectList aspects;
  PlanetMap  planets;
  Planet     sun,
             moon,
             mercury,
             venus,
             mars,
             jupiter,
             saturn,
             uranus,
             neptune,
             pluto,
             northNode;
  StarMap    stars;
};

}
#endif // A_DATA_H
