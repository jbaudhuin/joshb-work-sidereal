#include "astro-data.h"
#include <QDebug>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>

#undef MSDOS // undef macros made by SWE library
#undef UCHAR
#undef forward

#define min min

#include <math.h>
#include <cmath>
#include <tuple>

#include <boost/math/tools/minima.hpp>
#include <boost/math/tools/roots.hpp>

#include "astro-calc.h"
#include "astro-gui.h"
#include "astro-output.h"
#include "csvreader.h"
#include "fileeditor.h"

#include <sweodef.h> // the swe.h files are a little squirrely about include order
#include <swehouse.h>
#include <swephexp.h>

// Set to 1 to enable pseudo-finder debug mode (runs for 15 sec, no actual work)
#define DEBUG_FINDER_THREADS 0

using namespace boost::math::tools;

namespace A {

static QString angleDesc[] = { "asc", "desc", "mc", "ic" };

QString
dtToString(const QDateTime& dt)
{
    return dt.toString("yyyy-MM-dd hh:mm:ss.zzz");
}

template <typename T>
inline constexpr int
sgn(T x, std::false_type)
{
    return T(0) < x;
}

template <typename T>
inline constexpr int
sgn(T x, std::true_type)
{
    return (T(0) < x) - (x < T(0));
}

template <typename T>
inline constexpr int
sgn(T x)
{
    return sgn(x, std::is_signed<T>());
}

aspectModeType aspectMode { amcEcliptic };

/*static*/
const aspectModeType&
aspectModeType::current()
{
    return aspectMode;
}

PrimDirMode primDirMode = prdMundane;
bool useApparentSun = true;

bool
isSolarReturn(const QString& chartName)
{
    return chartName.contains("Sun-r=Sun");
}

bool
isSolarIngress(const QString& chartName)
{
    // Match patterns like "Ari=Sun", "Can=Sun", "Lib=Sun", "Cap=Sun", etc.
    static QRegularExpression rx("(Ari|Tau|Gem|Can|Leo|Vir|Lib|Sco|Sag|Cap|Aqu|Pis)=Sun");
    return rx.match(chartName).hasMatch();
}

bool
isSolarBasedChart(const QString& chartName)
{
    return isSolarReturn(chartName) || isSolarIngress(chartName);
}

double
getJulianDate(QDateTime GMT, bool ephemerisTime /*=false*/,
              CalendarType calType /*=Cal_Auto*/)
{
    char        serr[256];
    double      ret[2];
    const auto& date(GMT.date());
    const auto& time(GMT.time());

    // Resolve calendar flag
    int gregFlag = 1; // SE_GREG_CAL
    if (calType == Cal_Julian) {
        gregFlag = 0; // SE_JUL_CAL
    } else if (calType == Cal_Auto) {
        // Julian before the standard Western cutover (Oct 15, 1582)
        int y = date.year(), m = date.month(), d = date.day();
        if (y < 1582 || (y == 1582 && (m < 10 || (m == 10 && d < 15))))
            gregFlag = 0;
    }
    // Cal_Gregorian keeps gregFlag = 1

    swe_utc_to_jd(date.year(),
                  date.month(),
                  date.day(),
                  time.hour(),
                  time.minute(),
                  double(time.second()) + (time.msec() / 1000.),
                  gregFlag,
                  ret,
                  serr);
    return ret[ephemerisTime ? 0 : 1]; // ET or UT
}

double
getUTfromET(double et, CalendarType calType = Cal_Auto)
{
    // For ET→UT conversion, resolve calendar from the JD itself when Auto
    int gregFlag = 1;
    if (calType == Cal_Julian) {
        gregFlag = 0;
    } else if (calType == Cal_Auto) {
        // JD 2299161 = Oct 15, 1582 Gregorian cutover
        gregFlag = (et >= 2299161.0) ? 1 : 0;
    }

    int32  iyear, imonth, iday, ihour, imin;
    double dsec;
    swe_jdut1_to_utc(et,
                     gregFlag,
                     &iyear,
                     &imonth,
                     &iday,
                     &ihour,
                     &imin,
                     &dsec);

    double ret[2];
    char   serr[256];
    swe_utc_to_jd(iyear,
                  imonth,
                  iday,
                  ihour,
                  imin,
                  dsec,
                  gregFlag,
                  ret,
                  serr);

    return ret[1];
}

constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / M_PI;

// ------------------------------------------------------------
// Convert equatorial → ecliptic coordinates
// ------------------------------------------------------------
static void
equatorialToEcliptic(double  ra_deg,
                     double  dec_deg,
                     double  obliquity_deg,
                     double& eclLon_deg,
                     double& eclLat_deg)
{
    double ra  = ra_deg  * DEG_TO_RAD;
    double dec = dec_deg * DEG_TO_RAD;
    double eps = obliquity_deg * DEG_TO_RAD;

    double sinLat = sin(dec) * cos(eps) - cos(dec) * sin(eps) * sin(ra);
    eclLat_deg = asin(sinLat) * RAD_TO_DEG;

    double y = sin(ra) * cos(eps) + tan(dec) * sin(eps);
    double x = cos(ra);
    eclLon_deg = atan2(y, x) * RAD_TO_DEG;

    if (eclLon_deg < 0)
        eclLon_deg += 360.0;
}

// ------------------------------------------------------------
// Convert ecliptic → equatorial coordinates
// ------------------------------------------------------------
static void
eclipticToEquatorial(double  eclLon_deg,
                     double  eclLat_deg,
                     double  obliquity_deg,
                     double& ra_deg,
                     double& dec_deg)
{
    double lon = eclLon_deg * DEG_TO_RAD;
    double lat = eclLat_deg * DEG_TO_RAD;
    double eps = obliquity_deg * DEG_TO_RAD;

    double sinDec = sin(lat) * cos(eps) + cos(lat) * sin(eps) * sin(lon);
    dec_deg = asin(sinDec) * RAD_TO_DEG;

    double y = sin(lon) * cos(eps) - tan(lat) * sin(eps);
    double x = cos(lon);
    ra_deg = atan2(y, x) * RAD_TO_DEG;

    if (ra_deg < 0)
        ra_deg += 360.0;
}

// ------------------------------------------------------------
// Internal: ex-precess ecliptic longitude from t1 to t2, return
// RA/Dec at t2.  Reuses pre-computed natal ecliptic coordinates
// and (optionally) cached natal-epoch obliquity/ayanamsa.
// ------------------------------------------------------------
static std::pair<double, double>
exprecess_core(double eclLon_t1_deg,
               double eclLat_t1_deg,
               double ayanamsa_t1,
               double obliquity_t2_deg,
               double ayanamsa_t2)
{
    double deltaAyanamsa = ayanamsa_t2 - ayanamsa_t1;

    double eclLon_t2 = eclLon_t1_deg + deltaAyanamsa;
    if (eclLon_t2 < 0)     eclLon_t2 += 360.0;
    if (eclLon_t2 >= 360.0) eclLon_t2 -= 360.0;

    double ra_t2, dec_t2;
    eclipticToEquatorial(eclLon_t2, eclLat_t1_deg,
                         obliquity_t2_deg, ra_t2, dec_t2);
    return { ra_t2, dec_t2 };
}

// ------------------------------------------------------------
// Unified ex-precession: position + rate in one call.
// Primary overload — accepts pre-computed natal ecliptic coords.
// Uses context<ExprecessNatalEpoch> when available.
// ------------------------------------------------------------
ExprecessedEquatorial
exprecess_equatorial(double ra_t1_deg,
                     double dec_t1_deg,
                     double eclLon_t1_deg,
                     double eclLat_t1_deg,
                     double jd_t1,
                     double jd_t2,
                     double dt_days /*= 0.01*/)
{
    double xx[6];
    char   serr[256];

    // --- Natal-epoch values (from context or computed) ---
    double ayanamsa_t1;
    if (context<ExprecessNatalEpoch>::active()
        && context<ExprecessNatalEpoch>::current().jdNatal == jd_t1)
    {
        ayanamsa_t1 = context<ExprecessNatalEpoch>::current().ayanamsa;
    } else {
        ayanamsa_t1 = swe_get_ayanamsa(jd_t1);
    }

    // --- Target-epoch values ---
    swe_calc(jd_t2, SE_ECL_NUT, 0, xx, serr);
    double obliquity_t2 = xx[0];
    double ayanamsa_t2  = swe_get_ayanamsa(jd_t2);

    // --- Position at t2 ---
    auto [ra_t2, dec_t2] = exprecess_core(
        eclLon_t1_deg, eclLat_t1_deg,
        ayanamsa_t1, obliquity_t2, ayanamsa_t2);

    // --- Position at t2+dt (for numerical derivative) ---
    double jd_t2dt = jd_t2 + dt_days;
    swe_calc(jd_t2dt, SE_ECL_NUT, 0, xx, serr);
    double obliquity_t2dt = xx[0];
    double ayanamsa_t2dt  = swe_get_ayanamsa(jd_t2dt);

    auto [ra_t2dt, dec_t2dt] = exprecess_core(
        eclLon_t1_deg, eclLat_t1_deg,
        ayanamsa_t1, obliquity_t2dt, ayanamsa_t2dt);

    // --- Numerical derivatives (deg/day) ---
    double dra = ra_t2dt - ra_t2;
    if (dra > 180.0)  dra -= 360.0;
    if (dra < -180.0) dra += 360.0;

    return { ra_t2, dec_t2, dra / dt_days, (dec_t2dt - dec_t2) / dt_days };
}

// ------------------------------------------------------------
// Convenience overload: computes natal ecliptic coords internally.
// ------------------------------------------------------------
ExprecessedEquatorial
exprecess_equatorial(double ra_t1_deg,
                     double dec_t1_deg,
                     double jd_t1,
                     double jd_t2,
                     double dt_days /*= 0.01*/)
{
    double xx[6];
    char   serr[256];

    // Get natal obliquity (from context or computed)
    double obliquity_t1;
    if (context<ExprecessNatalEpoch>::active()
        && context<ExprecessNatalEpoch>::current().jdNatal == jd_t1)
    {
        obliquity_t1 = context<ExprecessNatalEpoch>::current().obliquity;
    } else {
        swe_calc(jd_t1, SE_ECL_NUT, 0, xx, serr);
        obliquity_t1 = xx[0];
    }

    double eclLon_t1, eclLat_t1;
    equatorialToEcliptic(ra_t1_deg, dec_t1_deg, obliquity_t1,
                         eclLon_t1, eclLat_t1);

    return exprecess_equatorial(
        ra_t1_deg, dec_t1_deg,
        eclLon_t1, eclLat_t1,
        jd_t1, jd_t2, dt_days);
}

// ------------------------------------------------------------
// Legacy wrappers (kept for backward compatibility)
// ------------------------------------------------------------
std::tuple<double, double>
exprecess_ra_dec_swe(double ra_t1_deg,
                     double dec_t1_deg,
                     double jd_t1,
                     double jd_t2)
{
    auto r = exprecess_equatorial(ra_t1_deg, dec_t1_deg, jd_t1, jd_t2);
    return { r.ra, r.dec };
}

std::tuple<double, double>
exprecess_ra_dec_rate_swe(double ra_t1_deg,
                          double dec_t1_deg,
                          double jd_t1,
                          double jd_t,
                          double dt_days = 0.01)
{
    auto r = exprecess_equatorial(ra_t1_deg, dec_t1_deg, jd_t1, jd_t, dt_days);
    return { r.raSpeed, r.decSpeed };
}

// ---------------------------------------------------------------------------
// NatalExprecessedPosition — constructor and operator()
// ---------------------------------------------------------------------------

NatalExprecessedPosition::NatalExprecessedPosition(
    const ChartPlanetId& cpid,
    const InputData&     ida,
    const QString&       tag) :
    NatalPosition(cpid, ida, tag)
{
    // Base NatalPosition already called compute(ida), set _rasiLoc & speed=0.

    // 2. Get natal JD
    _jdNatal = getJulianDate(ida.GMT(), false, ida.calendarType());

    // 3. Get natal obliquity (needed by both planets and angles)
    double xx[6];
    char   errStr[256];
    double obliquity_t1;
    if (context<ExprecessNatalEpoch>::active()
        && context<ExprecessNatalEpoch>::current().jdNatal == _jdNatal)
    {
        obliquity_t1 = context<ExprecessNatalEpoch>::current().obliquity;
    } else {
        swe_calc(_jdNatal, SE_ECL_NUT, 0, xx, errStr);
        obliquity_t1 = xx[0];
    }

    const PlanetId pid = cpid.planetId();

    // ---------------------------------------------------------------
    // [ANGLE_PRECESSION] Cardinal angles (Asc, IC, Desc, MC):
    // sweNum = 0 for angles, so swe_calc_ut would give the Sun's
    // position.  Instead, compute tropical houses and derive the
    // angle's true RA/Dec from its ecliptic longitude (lat = 0).
    // If this approach is wrong, search for ANGLE_PRECESSION to
    // find every related site and revert.
    // ---------------------------------------------------------------
    if (pid >= Angles_Start && pid < Angles_End) {
        double cusps[14], ascmc[11];
        swe_houses_ex(_jdNatal,
                      SEFLG_SWIEPH,   // always tropical
                      ida.location().y(),
                      ida.location().x(),
                      'C',
                      cusps,
                      ascmc);

        switch (pid) {
        case Planet_Asc:  _eclLon = ascmc[0]; break;
        case Planet_MC:   _eclLon = ascmc[1]; break;
        case Planet_Desc: _eclLon = swe_degnorm(ascmc[0] + 180.); break;
        case Planet_IC:   _eclLon = swe_degnorm(ascmc[1] + 180.); break;
        default:          _eclLon = 0; break; // unreachable
        }
        _eclLat = 0.0;

        eclipticToEquatorial(_eclLon, 0.0, obliquity_t1,
                             _natalRA, _natalDec);
    } else if (cpid.isMidpt()) {
        // 4M. Midpoint: compute each constituent's natal ecliptic (lon, lat),
        //     midpoint both components, then convert to RA/Dec.
        const Planet& p1    = getPlanet(pid);
        const Planet& p2    = getPlanet(cpid.planetId2());
        uint          eclFlags = (SEFLG_SWIEPH | SEFLG_SPEED) & ~SEFLG_TRUEPOS
                               & ~SEFLG_SIDEREAL & ~SEFLG_EQUATORIAL;
        double        xx1[6], xx2[6];
        swe_calc_ut(_jdNatal, p1.sweNum, eclFlags | (p1.sweFlags & ~SEFLG_EQUATORIAL), xx1, errStr);
        swe_calc_ut(_jdNatal, p2.sweNum, eclFlags | (p2.sweFlags & ~SEFLG_EQUATORIAL), xx2, errStr);

        double lon1 = xx1[0], lat1 = xx1[1];
        double lon2 = xx2[0], lat2 = xx2[1];

        // Longitude midpoint with ±180° shortest-arc wrap
        if (lon1 - lon2 >= 180.0)  lon1 -= 360.0;
        else if (lon2 - lon1 >= 180.0) lon2 -= 360.0;
        _eclLon = swe_degnorm((lon1 + lon2) / 2.0);
        if (cpid.isOppMidpt()) _eclLon = swe_degnorm(_eclLon + 180.0);

        // Latitude midpoint: plain average (no wrap needed)
        _eclLat = (lat1 + lat2) / 2.0;

        eclipticToEquatorial(_eclLon, _eclLat, obliquity_t1, _natalRA, _natalDec);

        qDebug().noquote() << "[MPNAT]"
            << "cpid=" << cpid.name()
            << "jdNatal=" << QString::number(_jdNatal, 'f', 4)
            << "eps=" << QString::number(obliquity_t1, 'f', 4)
            << "| p1=" << p1.name << "sweNum=" << p1.sweNum
            << "sweFlags=0x" + QString::number(p1.sweFlags, 16)
            << "lon1=" << QString::number(xx1[0], 'f', 4)
            << "lat1=" << QString::number(xx1[1], 'f', 4)
            << "| p2=" << p2.name << "sweNum=" << p2.sweNum
            << "sweFlags=0x" + QString::number(p2.sweFlags, 16)
            << "lon2=" << QString::number(xx2[0], 'f', 4)
            << "lat2=" << QString::number(xx2[1], 'f', 4)
            << "| post-wrap lon1=" << QString::number(lon1, 'f', 4)
            << "lon2=" << QString::number(lon2, 'f', 4)
            << "| _eclLon=" << QString::number(_eclLon, 'f', 4)
            << "_eclLat=" << QString::number(_eclLat, 'f', 4)
            << "_natalRA=" << QString::number(_natalRA, 'f', 4)
            << "_natalDec=" << QString::number(_natalDec, 'f', 4);
    } else {
        // 4. True planets: get natal RA/Dec via SWE (equatorial, tropical)
        const Planet& p = getPlanet(pid);
        uint          flags = (SEFLG_SWIEPH | p.sweFlags | SEFLG_EQUATORIAL
                               | SEFLG_SPEED)
                     & ~SEFLG_TRUEPOS & ~SEFLG_SIDEREAL;
        swe_calc_ut(_jdNatal, p.sweNum, flags, xx, errStr);

        _natalRA  = xx[0];
        _natalDec = xx[1];

        if (pid == Planet_SouthNode) {
            _natalRA  = swe_degnorm(_natalRA + 180.);
            _natalDec = -_natalDec;
        }

        // 5. Compute natal ecliptic lon/lat (cached for the NR loop)
        equatorialToEcliptic(_natalRA, _natalDec, obliquity_t1,
                             _eclLon, _eclLat);
    }

    // 6. At construction time, the position is the natal RA (no precession)
    //    Speed is zero (will be recomputed in operator())
    speed = 0;
}

qreal
NatalExprecessedPosition::operator()(double jd, int h)
{
    auto ep = exprecess_equatorial(
        _natalRA, _natalDec,
        _eclLon, _eclLat,
        _jdNatal, jd);

    loc = ep.ra;
    // Negate speed: computeDelta uses (a.speed + b.speed) as derivative
    // of (b - a), so natal body's speed contribution must be -d(loc)/dt.
    speed = -ep.raSpeed;

    _rasiLoc = loc;
    if (h > 1) {
        loc = fmod(loc * h, 360.);
        speed *= h;
    }
    return loc;
}

void
NatalExprecessedPosition::radecAt(double jd, double& ra, double& dec) const
{
    double dRAdt, dDecdt;
    radecSpeedAt(jd, ra, dec, dRAdt, dDecdt);
}

bool
NatalExprecessedPosition::radecSpeedAt(double jd,
                                       double& ra, double& dec,
                                       double& dRAdt, double& dDecdt) const
{
    auto ep = exprecess_equatorial(
        _natalRA, _natalDec,
        _eclLon, _eclLat,
        _jdNatal, jd);
    ra     = ep.ra;
    dec    = ep.dec;
    dRAdt  = ep.raSpeed;
    dDecdt = ep.decSpeed;
    return true;
}

// ---------------------------------------------------------------------------
// Horoscope::applyExprecession / clearExprecession
// ---------------------------------------------------------------------------

void
Horoscope::applyExprecession(double targetJD)
{
    if (_exprecessApplied) clearExprecession();

    double natalJD = getJulianDate(inputData.GMT(), false,
                                   inputData.calendarType());

    // ------------------------------------------------------------------
    // Pre-compute natal-epoch obliquity + ayanamsa for angle precession.
    // [ANGLE_PRECESSION] — see breadcrumb comments below.
    // ------------------------------------------------------------------
    double xx_ep[6];
    char   serr_ep[256];
    swe_calc(natalJD, SE_ECL_NUT, 0, xx_ep, serr_ep);
    double obliquity_natal = xx_ep[0];
    double ayanamsa_natal  = swe_get_ayanamsa(natalJD);

    // Save and replace planet equatorial coordinates.
    for (auto it = planets.begin(); it != planets.end(); ++it) {

        // House cusps (id >= Angles_End): skip entirely — their
        // equatorialPos is a copy of eclipticPos, not true RA/Dec.
        if (it.key() >= Angles_End) continue;

        // --- True planets (id < Angles_Start): full ex-precession ---
        Planet& p = it.value();
        _savedPlanetEq[it.key()] = { p.equatorialPos, p.equatorialSpeed };

        auto ep = exprecess_equatorial(
            p.equatorialPos.x(), p.equatorialPos.y(),
            natalJD, targetJD);

        p.equatorialPos.setX(ep.ra);
        p.equatorialPos.setY(ep.dec);
        p.equatorialSpeed.setX(ep.raSpeed);
        p.equatorialSpeed.setY(ep.decSpeed);

        // Also update planetsOrig so that findClusters (via PlanetProfile)
        // sees ex-precessed equatorial positions.
        if (planetsOrig.contains(it.key())) {
            planetsOrig[it.key()].equatorialPos   = p.equatorialPos;
            planetsOrig[it.key()].equatorialSpeed  = p.equatorialSpeed;
        }
    }

    // Save and replace star equatorial coordinates
    for (auto it = stars.begin(); it != stars.end(); ++it) {
        Star& s = it.value();
        _savedStarEq[it.key()] = { s.equatorialPos, {} };

        auto ep = exprecess_equatorial(
            s.equatorialPos.x(), s.equatorialPos.y(),
            natalJD, targetJD);

        s.equatorialPos.setX(ep.ra);
        s.equatorialPos.setY(ep.dec);
    }

    // [ANGLE_PRECESSION] Precess houses.RAAC / RAMC / RADC so the chart
    // widget draws axis lines at the precessed positions.
    //
    // Angles are NOT in the planets map — they live only in the Houses
    // struct.  We take each angle's ecliptic longitude (houses.Asc,
    // houses.MC), convert to tropical if sidereal, derive natal RA/Dec,
    // then run the full exprecess_equatorial() pipeline with eclLat = 0.
    //
    // If this approach is wrong, search for ANGLE_PRECESSION to find
    // every related site and revert.
    _savedRAAC = houses.RAAC;
    _savedRAMC = houses.RAMC;
    _savedRADC = houses.RADC;
    {
        // --- Ascendant ---
        double ascTrop = houses.Asc;
        if (inputData.zodiac() > 1)
            ascTrop = swe_degnorm(ascTrop + ayanamsa_natal);
        double ascRA, ascDec;
        eclipticToEquatorial(ascTrop, 0.0, obliquity_natal, ascRA, ascDec);
        auto epAsc = exprecess_equatorial(
            ascRA, ascDec, ascTrop, 0.0, natalJD, targetJD);
        houses.RAAC = epAsc.ra;

        // --- Descendant (Asc + 180) ---
        double descTrop = swe_degnorm(ascTrop + 180.0);
        double descRA, descDec;
        eclipticToEquatorial(descTrop, 0.0, obliquity_natal, descRA, descDec);
        auto epDesc = exprecess_equatorial(
            descRA, descDec, descTrop, 0.0, natalJD, targetJD);
        houses.RADC = epDesc.ra;

        // --- MC ---
        double mcTrop = houses.MC;
        if (inputData.zodiac() > 1)
            mcTrop = swe_degnorm(mcTrop + ayanamsa_natal);
        double mcRA, mcDec;
        eclipticToEquatorial(mcTrop, 0.0, obliquity_natal, mcRA, mcDec);
        auto epMC = exprecess_equatorial(
            mcRA, mcDec, mcTrop, 0.0, natalJD, targetJD);
        houses.RAMC = epMC.ra;
    }

    qDebug() << "[ANGLE_PRECESSION] applyExprecession:"
             << "RAAC" << _savedRAAC << "->" << houses.RAAC
             << "RAMC" << _savedRAMC << "->" << houses.RAMC;

    _exprecessApplied = true;
}

void
Horoscope::clearExprecession()
{
    if (!_exprecessApplied) return;

    for (auto it = _savedPlanetEq.constBegin();
         it != _savedPlanetEq.constEnd(); ++it)
    {
        if (planets.contains(it.key())) {
            planets[it.key()].equatorialPos   = it.value().pos;
            planets[it.key()].equatorialSpeed  = it.value().speed;
        }
        if (planetsOrig.contains(it.key())) {
            planetsOrig[it.key()].equatorialPos   = it.value().pos;
            planetsOrig[it.key()].equatorialSpeed  = it.value().speed;
        }
    }

    for (auto it = _savedStarEq.constBegin();
         it != _savedStarEq.constEnd(); ++it)
    {
        if (stars.contains(it.key())) {
            stars[it.key()].equatorialPos = it.value().pos;
        }
    }

    _savedPlanetEq.clear();
    _savedStarEq.clear();

    // [ANGLE_PRECESSION] Restore natal-epoch house RA values.
    houses.RAAC = _savedRAAC;
    houses.RAMC = _savedRAMC;
    houses.RADC = _savedRADC;

    _exprecessApplied = false;
}

// ---------------------------------------------------------------------------
// Equation-of-time / LAT / LMT helpers
// ---------------------------------------------------------------------------

double
equationOfTime(double jdUT)
{
    double eot = 0.0;
    char   serr[256];
    if (swe_time_equ(jdUT, &eot, serr) == ERR) {
        qDebug() << "swe_time_equ failed:" << serr;
        return 0.0;
    }
    return eot; // fractional days
}

double
lmtToLat(double jdLMT, double geolon)
{
    double jdLAT = jdLMT;
    char   serr[256];
    if (swe_lmt_to_lat(jdLMT, geolon, &jdLAT, serr) == ERR) {
        qDebug() << "swe_lmt_to_lat failed:" << serr;
    }
    return jdLAT;
}

double
latToLmt(double jdLAT, double geolon)
{
    double jdLMT = jdLAT;
    char   serr[256];
    if (swe_lat_to_lmt(jdLAT, geolon, &jdLMT, serr) == ERR) {
        qDebug() << "swe_lat_to_lmt failed:" << serr;
    }
    return jdLMT;
}

QDateTime
localToUTC(const QDateTime& localDt,
           double           tz,
           double           geolon,
           TimeMode         mode,
           CalendarType     calType /*=Cal_Auto*/)
{
    switch (mode) {
    case Time_ZoneTime: {
        // Standard zone-time path: local – tz → UTC
        QTimeZone qtz(static_cast<int>(tz * 3600));
        QDateTime local(localDt.date(), localDt.time(), qtz);
        return local.toUTC();
    }
    case Time_LMT: {
        // LMT offset = longitude / 15  (hours, east-positive)
        double lmtOffsetSec = (geolon / 15.0) * 3600.0;
        QTimeZone qtz(static_cast<int>(lmtOffsetSec));
        QDateTime local(localDt.date(), localDt.time(), qtz);
        return local.toUTC();
    }
    case Time_LAT: {
        // LAT → LMT → UT
        // 1. Treat entered time as LAT at the given longitude
        double lmtOffsetSec = (geolon / 15.0) * 3600.0;
        // Rough initial UT estimate (needed to seed swe_lat_to_lmt)
        QTimeZone qtz(static_cast<int>(lmtOffsetSec));
        QDateTime roughUTC(localDt.date(), localDt.time(), qtz);
        // Convert entered LAT (as if it were LMT) to JD
        double jdLAT =
            getJulianDate(roughUTC.toUTC(), false, calType)
            + lmtOffsetSec / 86400.0; // make it an LMT-like JD
        // SWE: LAT → LMT
        double jdLMT = latToLmt(jdLAT, geolon);
        // LMT → UT
        double jdUT = jdLMT - (geolon / 15.0) / 24.0;
        return dateTimeFromJulian(jdUT, calType);
    }
    }
    // fallback
    QTimeZone qtz(static_cast<int>(tz * 3600));
    QDateTime local(localDt.date(), localDt.time(), qtz);
    return local.toUTC();
}

EoTInfo
computeEoT(const QDateTime& utcDt, double geolon,
           CalendarType calType /*=Cal_Auto*/)
{
    EoTInfo info {};
    info.valid = false;
    double jdUT = getJulianDate(utcDt, false, calType);
    if (jdUT == 0.0) return info;

    // LMT JD  = UT JD + longitude/15 (hours → days)
    info.lmtJD = jdUT + (geolon / 15.0) / 24.0;
    // LAT JD
    info.latJD = lmtToLat(info.lmtJD, geolon);
    // EoT  = LAT – LMT  expressed in seconds
    info.eotSeconds = (info.latJD - info.lmtJD) * 86400.0;
    info.valid      = true;
    return info;
}

unsigned
windowOf(PlanetLoc* pl)
{
    switch (pl->planet.planetId()) {
    case Planet_Mercury: return 45;
    case Planet_Venus:   return 90;
    case Planet_Mars:    return 210;
    default:             return 360;
    }
};

inline qreal
harmonic(double h, qreal value)
{
    return fmod(value * h, 360.);
}

float
roundDegree(float deg)
{
    deg = deg - (int(deg / 360)) * 360;
    if (deg < 0) deg += 360;
    return deg;
}

const ZodiacSign&
getSign(float deg, const Zodiac& zodiac)
{
    for (const ZodiacSign& s : zodiac.signs)
        if (s.startAngle <= deg && s.endAngle > deg) return s;
    return zodiac.signs[zodiac.signs.count() - 1];
}

int
getHouse(const Houses& houses, float deg)
{
    for (int i = 0; i <= 10; i++)
        if ((deg >= houses.cusp[i] && deg < houses.cusp[i + 1])
            || (houses.cusp[i] > 180 && houses.cusp[i + 1] < 180
                && (deg < houses.cusp[i + 1] || deg >= houses.cusp[i])))
        {
            return i + 1;
        }

    return 12;
}

int
getHouse(ZodiacSignId sign, const Houses& houses, const Zodiac& zodiac)
{
    if (sign == Sign_None) return 0;

    for (int i = 1; i <= 12; i++) {
        auto hs = getSign(houses.cusp[i - 1], zodiac).id;
        if (sign == hs
            /*|| (sign + 1) % 13 == getSign(houses.cusp[i % 12], zodiac).id*/)
            return i;
    }
    return 0;
}

int
getHouse(const Horoscope& scope, float deg)
{
    return getHouse(scope.houses, deg);
}

QList<int>
getHouses(ZodiacSignId sign, const Houses& houses, const Zodiac& zodiac)
{
    if (sign == Sign_None) return {};
    QList<int> ret;

    // Helper to check if angle 'x' is strictly inside the interval (a, b)
    // moving forward around the circle (0..360). Handles wraparound.
    auto angleInInterval = [](float x, float a, float b) -> bool {
        x = fmod(x + 360.0f, 360.0f);
        a = fmod(a + 360.0f, 360.0f);
        b = fmod(b + 360.0f, 360.0f);
        if (a < b) return (x > a && x < b);
        // wraparound
        return (x > a && x < 360.0f) || (x >= 0.0f && x < b);
    };

    // Get sign's angular span from the zodiac
    const ZodiacSign* ssign = nullptr;
    for (const ZodiacSign& zs : zodiac.signs) {
        if (zs.id == sign) {
            ssign = &zs;
            break;
        }
    }
    if (!ssign) return ret;

    float sstart = ssign->startAngle;
    float send   = ssign->endAngle;

    // For each house, check two cases:
    // 1) The sign is the sign at the house start cusp -> normal rulership
    // (positive house) 2) The sign's entire span lies strictly within the house
    // span -> intercepted (negative house)
    for (int i = 1; i <= 12; ++i) {
        float houseStart = houses.cusp[i - 1];
        float houseEnd   = houses.cusp[i % 12]; // wraps to cusp[0] for house 12

        // Case 1: cusp start sign equals the target sign
        if (getSign(houseStart, zodiac).id == sign) {
            if (!ret.contains(i)) ret << i;
            continue;
        }

        // Case 2: intercepted sign - sign's start and end both strictly inside
        // the house span
        if (angleInInterval(sstart, houseStart, houseEnd)
            && angleInInterval(send, houseStart, houseEnd))
        {
            if (!ret.contains(-i)) ret << -i;
        }
    }

    return ret;
}

float
angle(const Star& body1, const Star& body2)
{
    switch (aspectMode) {
    case amcGreatCircle: {
        float a = angle(body1.eclipticPos.x(), body2.eclipticPos.x());
        float b = angle(body1.eclipticPos.y(), body2.eclipticPos.y());
        return sqrt(a * a + b * b);
    }
    case amcEcliptic:
        return angle(body1.eclipticPos.x(), body2.eclipticPos.x());
    case amcEquatorial:
        return angle(body1.equatorialPos.x(), body2.equatorialPos.x());
    case amcPrimeVertical: return angle(body1.pvPos, body2.pvPos);

    case amcUnknown:
    case amcEND:           break;
    }
    return 0;
}

float
angle(const Star& body, float deg)
{
    switch (aspectMode) {
    case amcGreatCircle:
    case amcEcliptic:      return angle(body.eclipticPos.x(), deg);
    case amcEquatorial:    return angle(body.equatorialPos.x(), deg);
    case amcPrimeVertical: return angle(body.pvPos, deg);

    case amcUnknown:
    case amcEND:           break;
    }
    return 0;
}

float
angle(const Star& body, QPointF coordinate)
{
    switch (aspectMode) {
    case amcGreatCircle: {
        float a = angle(body.eclipticPos.x(), coordinate.x());
        float b = angle(body.eclipticPos.y(), coordinate.y());
        return sqrt(pow(a, 2) + pow(b, 2));
    }
    case amcEcliptic:      return angle(body.eclipticPos.x(), coordinate.x());
    case amcEquatorial:    return angle(body.equatorialPos.x(), coordinate.x());
    case amcPrimeVertical: return angle(body.pvPos, coordinate.x());
    default:               return 0;
    }
}

float
angle(float deg1, float deg2)
{
    float ret = fabs(deg1 - deg2);
    if (ret > 180) ret = 360 - ret;
    return ret;
}

AspectId
aspect(const Star& planet1, const Star& planet2, const AspectsSet& aspectSet)
{
#if 0
    if (planet1.isPlanet() && planet2.isPlanet()
        && planet1.getSWENum() == planet2.getSWENum())
        return Aspect_None;
#endif
    return aspect(angle(planet1, planet2), aspectSet);
}

inline AspectId
aspect(const Planet* p1, const Planet* p2, const AspectsSet& asps)
{
    return aspect(*p1, *p2, asps);
}

AspectId
aspect(const Star& planet1, float degree, const AspectsSet& aspectSet)
{
    return aspect(angle(planet1, degree), aspectSet);
}

AspectId
aspect(const Star& planet, QPointF coordinate, const AspectsSet& aspectSet)
{
    return aspect(angle(planet, coordinate), aspectSet);
}

AspectId
aspect(float angle, const AspectsSet& aspectSet)
{
    AspectId closest = Aspect_None;
    float    pct     = 1;
    for (const AspectType& aspect : aspectSet.aspects) {
        auto npct = abs(aspect.angle) / aspect.orb();
        if (aspect.isEnabled() && aspect.angle - aspect.orb() <= angle
            && aspect.angle + aspect.orb() >= angle
            && (closest == Aspect_None ? true : npct < pct))
        {
            closest = aspect.id;
            pct     = npct;
            // return aspect.id;
        }
    }
    return closest;
}

bool
towardsMovement(const Planet& planet1, const Planet& planet2)
{
    const Planet *p1 = &planet1, *p2 = &planet2;

    if (!isEarlier(planet1, planet2)) // make first planet earlier than second
    {
        p1 = &planet2;
        p2 = &planet1;
    }

    switch (aspectMode) {
    case amcPrimeVertical:
    case amcEquatorial:
        return (p1->equatorialSpeed.x() > p2->equatorialSpeed.x());
    default:
    case amcEcliptic: return (p1->eclipticSpeed.x() > p2->eclipticSpeed.x());
    }
}

PlanetPosition
getPosition(const Planet& planet, ZodiacSignId sign)
{
    if (planet.homeSigns.contains(sign)) return Position_Dwelling;
    if (planet.exaltationSigns.contains(sign)) return Position_Exaltation;
    if (planet.exileSigns.contains(sign)) return Position_Exile;
    if (planet.downfallSigns.contains(sign)) return Position_Downfall;
    return Position_Normal;
}

const Planet*
almuten(const Horoscope& scope)
{
    int           max = 0;
    const Planet* ret = 0;

    for (const Planet& p : scope.planets) {
        int val = p.power.dignity + p.power.deficient;
        if (val > max) {
            max = val;
            ret = &p;
        }
    }

    return ret;
}

const Planet*
doryphoros(const Horoscope& scope)
{
    int           minAngle = 180;
    const Planet* ret      = 0;

    for (const Planet& p : scope.planets) {
        if (p.id == Planet_Sun || !p.isReal) continue;

        int val = angle(p, scope.sun);

        if (val < minAngle && isEarlier(p, scope.sun)) {
            minAngle = val;
            ret      = &p;
        }
    }

    return ret;
}

const Planet*
auriga(const Horoscope& scope)
{
    int           minAngle = 180;
    const Planet* ret      = 0;

    for (const Planet& p : scope.planets) {
        if (p.id == Planet_Sun || !p.isReal) continue;

        int val = angle(p, scope.sun);

        if (val < minAngle && !isEarlier(p, scope.sun)) {
            minAngle = val;
            ret      = &p;
        }
    }

    return ret;
}

bool
rulerDisposition(int house, int houseAuthority, const Horoscope& scope)
{
    if (house <= 0 || houseAuthority <= 0) return false;

    for (const Planet& p : scope.planets)
        if (p.house == house && p.houseRuler.contains(houseAuthority))
            return true;

    return false;
}

bool
isEarlier(const Planet& planet, const Planet& sun)
{
    // float opposite = roundDegree(sun.eclipticPos.x() - 180);
    switch (aspectMode) {
    case amcPrimeVertical: return (roundDegree(planet.pvPos - sun.pvPos) > 180);
    case amcEquatorial:
        return (roundDegree(planet.equatorialPos.x() - sun.equatorialPos.x())
                > 180);
    default:
    case amcEcliptic:
        return (roundDegree(planet.eclipticPos.x() - sun.eclipticPos.x())
                > 180);
    }
}

const Planet&
ruler(int house, const Horoscope& scope)
{
    static Planet emptyPlanet;
    if (house <= 0 || house > 12) return emptyPlanet;

    // Iterate using keys() method like elsewhere in the code
    for (PlanetId id : scope.planets.keys()) {
        const Planet& p = scope.planets[id];
        if (p.houseRuler.contains(house)) return p;
    }

    return emptyPlanet;
}

PlanetId
receptionWith(const Planet& planet, const Horoscope& scope)
{
    for (const Planet& p : scope.planets) {
        if (p != planet) {
            if (getPosition(p, planet.sign->id) == Position_Dwelling
                && getPosition(planet, p.sign->id) == Position_Dwelling)
                return p.id;

            if (getPosition(p, planet.sign->id) == Position_Exaltation
                && getPosition(planet, p.sign->id) == Position_Exaltation)
                return p.id;
        }
    }

    return Planet_None;
}

Planet
calculatePlanet(PlanetId         planet,
                const InputData& input,
                double           jd,
                double&          eps,
                unsigned int&    flags,
                double&          ablong,
                double           RAMC,
                ZodiacId         zid)
{
    Planet ret = getPlanet(planet);

    uint   invertPositionFlag = 256 * 1024;
    char   errStr[256]        = "";
    double xx[6];

    // turn off true pos
    flags = (SEFLG_SWIEPH | ret.sweFlags) & ~SEFLG_TRUEPOS;

    if (zid > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(zid - 2, 0, 0);
    }
    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    eps = xx[0];

    // TODO: wrong moon speed calculation
    // (flags: SEFLG_TRUEPOS|SEFLG_SPEED = 272)
    //         272|invertPositionFlag = 262416
    if (swe_calc_ut(jd, ret.sweNum, flags, xx, errStr) == ERR) {
        qDebug("A: can't calculate position of '%s' at julian day %f: %s",
               qPrintable(ret.name),
               jd,
               errStr);
    }
    if (!(ret.sweFlags & invertPositionFlag)) ret.eclipticPos.setX(xx[0]);
    else // found 'inverted position' flag
        ret.eclipticPos.setX(roundDegree(xx[0] - 180));

    ret.eclipticPos.setY(xx[1]);
    ret.distance = xx[2];
    ret.eclipticSpeed.setX(xx[3]);
    ret.eclipticSpeed.setY(xx[4]);

    if (/*ret.sweNum != SE_MOON &&*/ ret.sweNum != SE_SUN) {
        double phaen[20];
        swe_pheno_ut(jd, ret.sweNum, flags, phaen, errStr);
        ret.phaseAngle = phaen[0];
        ret.elongation = phaen[2];
    }

    double geopos[3];
    geopos[0] = input.location().x();
    geopos[1] = input.location().y();
    geopos[2] = 199 /*meters*/; // input.location.z();

    ablong = xx[0];

    // A hack to calculate prime vertical longitude from the campanus house
    // position
    if (zid <= 1
        || swe_calc_ut(jd, ret.sweNum, flags & ~SEFLG_SIDEREAL, xx, errStr)
               != ERR)
    {
        ablong = xx[0];
        // We may have had to get tropical position to get the
        // house position -- this API wants tropical longitude.
        // From there we fudge a prime vertical coordinate.
        ret.tropicalEclipticPos = QPointF(xx[0], xx[1]);
        double housePos = swe_house_pos(RAMC, geopos[1], eps, 'C', xx, errStr);
        ret.pvPos       = (housePos - 1) / 12 * 360;
        if (ret.id == Planet_SouthNode) ret.pvPos = swe_degnorm(ret.pvPos + 180.);
    }

    // calculate horizontal coordinates
    double hor[3];
    swe_azalt(jd, SE_ECL2HOR, geopos, 0, 0, xx, hor);
    ret.horizontalPos.setX(hor[0]);
    ret.horizontalPos.setY(hor[1]);

    if (swe_calc_ut(jd,
                    ret.sweNum,
                    flags | SEFLG_EQUATORIAL | SEFLG_SPEED,
                    xx,
                    errStr)
        != ERR)
    {
        ret.equatorialPos.setX(xx[0]);
        ret.equatorialPos.setY(xx[1]);
        ret.equatorialSpeed.setX(xx[3]);
        ret.equatorialSpeed.setY(xx[4]);

        // SWE returns North Node's equatorial coords for both nodes;
        // mirror RA by 180° and negate declination for the South Node.
        if (ret.id == Planet_SouthNode) {
            ret.equatorialPos.setX(swe_degnorm(xx[0] + 180.));
            ret.equatorialPos.setY(-xx[1]);
            ret.equatorialSpeed.setX(xx[3]);
            ret.equatorialSpeed.setY(-xx[4]);
        }
    }

    return ret;
}

Planet
calculatePlanet(PlanetId         planet,
                const InputData& input,
                const Houses&    houses,
                const Zodiac&    zodiac)
{
    double jd = getJulianDate(input.GMT(), false, input.calendarType());

    double       eps, ablong;
    unsigned int flags;
    char         errStr[256] = "";
    Planet       ret = calculatePlanet(planet,
                                 input,
                                 jd,
                                 eps,
                                 flags,
                                 ablong,
                                 houses.RAMC,
                                 zodiac.id);

    double geopos[3];
    geopos[0] = input.location().x();
    geopos[1] = input.location().y();
    geopos[2] = 199 /*meters*/; // input.location.z();

    ret.sign     = &getSign(ret.eclipticPos.x(), zodiac);
    ret.house    = getHouse(houses, ret.eclipticPos.x());
    ret.position = getPosition(ret, ret.sign->id);
    int prev     = -1;
    for (auto s : std::as_const(ret.homeSigns)) {
        if (s == prev) continue;
        ret.houseRuler << getHouses(s, houses, zodiac);
        prev = s;
    }
    std::sort(ret.houseRuler.begin(), ret.houseRuler.end());
    // Remove duplicate house entries (can occur when multiple home signs map
    // to the same house or interception logic produced overlaps)
    ret.houseRuler.erase(
        std::unique(ret.houseRuler.begin(), ret.houseRuler.end()),
        ret.houseRuler.end());

    double rettm;
    int    eflg = SEFLG_SWIEPH;

    int32  deg, min, sec, sgn;
    double frac;
    double st = swe_degnorm(ret.equatorialPos.x());
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);

    if (primDirMode == prdActive) {
        for (Star::angleTransitMode m = Star::atAsc; m < Star::numAngles;
             m                        = Star::angleTransitMode(m + 1))
        {
            if (swe_rise_trans(houses.startSpeculum,
                               ret.sweNum,
                               nullptr /*starname*/,
                               eflg,
                               Star::angleTransitFlag(m),
                               geopos,
                               1013.25 /*atpress*/,
                               10 /*attemp*/,
                               &rettm,
                               errStr)
                >= 0)
            {
                st =
                    swe_degnorm(swe_sidtime(rettm) * 15 + input.location().x());
                swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
                qDebug("  ACTUAL TIME %s %3d %02d %02d",
                       qPrintable(angleDesc[m]),
                       deg,
                       min,
                       sec);
                ret.angleTransit[m] = Planet::timeToDT(rettm);

                // Calculate RA at this transit time for prdActive mode
                double xx[6];
                char   serr[AS_MAXCH];
                int    iflgret = swe_calc_ut(rettm,
                                          ret.sweNum,
                                          SEFLG_EQUATORIAL | SEFLG_SPEED,
                                          xx,
                                          serr);
                if (iflgret >= 0) {
                    ret.angleTransitRA[m] = xx[0]; // RA in degrees
                } else {
                    ret.angleTransitRA[m] = 0.0;
                }
            }
        }
        // SWE uses the North Node's sweNum for both nodes, so its transit times
        // correspond to the North Node. Swap Rise↔Set and MC↔IC to get South
        // Node timings, then add 180° to the RA values (SWE returned North Node
        // RA; South Node RA is always 180° opposite).
        if (ret.id == Planet_SouthNode) {
            qSwap(ret.angleTransit[Star::atAsc], ret.angleTransit[Star::atDesc]);
            qSwap(ret.angleTransit[Star::atMC], ret.angleTransit[Star::atIC]);
            qSwap(ret.angleTransitRA[Star::atAsc],
                  ret.angleTransitRA[Star::atDesc]);
            qSwap(ret.angleTransitRA[Star::atMC], ret.angleTransitRA[Star::atIC]);
            for (int i = 0; i < Star::numAngles; ++i) {
                auto mi              = Star::angleTransitMode(i);
                ret.angleTransitRA[mi] = swe_degnorm(ret.angleTransitRA[mi] + 180.0);
            }
        }
    } else {
        std::array<double,Star::numAngles> at;

        double RA = ret.equatorialPos.x();
        double DD;
        if (primDirMode == prdZodiacal) {
            DD = asind(sind(eps) * sind(ret.eclipticPos.x()));
        } else {
            DD = ret.equatorialPos.y();
        }
        double AD   = asind(tand(DD) * tand(input.location().y()));
        double OA   = RA - AD;
        double OD   = RA + AD;
        double HDor = swe_degnorm(OA - houses.OAAC);
        double HDoc = swe_degnorm(OD - houses.ODDC);

        at[Star::atMC]   = RA;
        at[Star::atAsc]  = swe_degnorm(houses.RAMC + HDor);
        at[Star::atDesc] = swe_degnorm(houses.RAMC + HDoc);
        at[Star::atIC]   = swe_degnorm(RA - 180);

        double jd0          = getJulianDate(input.GMT(), false, input.calendarType());
        double RAMC0        = houses.RAMC; // in degrees
        double sidereal_day = /*1; / */0.99726958;  // days

        QString rastr = "Planet Angle Transits in RA of " + ret.name.left(3);

        char delim = ':';
        for (unsigned i = 0; i < Star::numAngles; ++i) {
            auto m = Star::angleTransitMode(i);
            double RA_target = at[m];
            double delta_deg =
                swe_difdeg2n(RA_target, RAMC0); // signed difference
            double delta_jd     = delta_deg / 360.0 * sidereal_day;
            double jd_target    = jd0 + delta_jd;

            ret.angleTransit[m] = dateTimeFromJulian(jd_target);
            
            // Store the RA value for this transit
            ret.angleTransitRA[m] = RA_target;
            
            QString desc = angleDesc[m];

            swe_split_deg(RA_target, 0, &deg, &min, &sec, &frac, &sgn);
            rastr += QString("%1 %2 %3 %4'%5\"")
                         .arg(delim)
                         .arg(desc)
                         .arg(deg, 3, 10, QChar(' '))
                         .arg(min, 2, 10, QChar('0'))
                         .arg(sec, 2, 10, QChar('0'));
            delim = ',';
        }
#if 0 // !DEBUG_FINDER_THREADS
        qDebug() << qPrintable(rastr);
#endif
    }

    return ret;
}

qreal
PlanetLoc::compute(const InputData& ida, double jd, int h)
{
    qreal pos;
    std::tie(pos, speed) = compute(planet, ida, jd);
    loc = _rasiLoc = pos;
    if (h > 1) {
        loc = harmonic(h, pos);
        speed *= h;
    }
    return pos;
}

std::pair<qreal, qreal>
PlanetLoc::compute(const ChartPlanetId& planet, const InputData& ida, double jd)
{
    constexpr uint invertPositionFlag = 256 * 1024;
    char           errStr[256]        = "";

    const Planet& p1(getPlanet(planet.planetId()));
    uint          flags = (SEFLG_SWIEPH | p1.sweFlags) & ~SEFLG_TRUEPOS;
    bool          trop  = true;
    if (ida.zodiac() > 1) {
        trop = false;
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(ida.zodiac() - 2, 0, 0);
    }

    double xx[6];
    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    auto eps = xx[0];

    typedef std::pair<double, double> posSpd;
    auto getAscMC = [&](unsigned i, bool trop = false) -> posSpd {
        double cusps[14], cuspspd[14], ascmc[11], ascmcspd[11];
        auto   jdut  = getUTfromET(jd, ida.calendarType());
        uint   flags = SEFLG_SWIEPH;
        if (!trop) flags |= SEFLG_SIDEREAL;
        swe_houses_ex2(jdut,
                       flags,
                       ida.location().y(),
                       ida.location().x(),
                       'C',
                       cusps,
                       ascmc,
                       cuspspd,
                       ascmcspd,
                       errStr);
        return { ascmc[i], ascmcspd[i] };
    };

    auto getPos = [&](const Planet& p, qreal& speed) -> qreal {
        int   ret = ERR;
        qreal pos = 0.0;
        switch (aspectMode) {
        case amcEcliptic:
            if (p.id == Planet_Asc || p.id == Planet_Desc) {
                std::tie(pos, speed) = getAscMC(0, trop);
                if (p.id == Planet_Desc) pos = swe_degnorm(pos + 180.);
                ret = OK;
                // FIXME speed?
            } else if (p.id == Planet_MC || p.id == Planet_IC) {
                std::tie(pos, speed) = getAscMC(1, trop);
                if (p.id == Planet_IC) pos = swe_degnorm(pos + 180.);
                ret = OK;
                // FIXME speed?
            } else {
                ret = swe_calc_ut(jd, p.sweNum, flags, xx, errStr);
                pos = xx[0];
                if (p.id == Planet_SouthNode) pos = swe_degnorm(pos + 180.);
                speed = xx[3];
            }
            break;

        case amcEquatorial:
            if (p.id == Planet_Asc || p.id == Planet_Desc) {
                double xx[6];
                std::tie(xx[0], speed) = getAscMC(0, true /*trop*/);
                xx[1] = 0, xx[2] = 1.0;
                swe_cotrans(xx, xx, -eps);
                pos = xx[0];
                if (p.id == Planet_Desc) pos = swe_degnorm(pos + 180.);
                ret = OK;
                // FIXME speed?
            } else if (p.id == Planet_MC || p.id == Planet_IC) {
                // XXX do we not use cotrans here, too?
                std::tie(pos, speed) = getAscMC(2, true /*trop*/);
                if (p.id == Planet_IC) pos = swe_degnorm(pos + 180.);
                ret = OK;
            } else {
                ret = swe_calc_ut(jd,
                                  p.sweNum,
                                  (flags & ~SEFLG_SIDEREAL) | SEFLG_EQUATORIAL
                                      | SEFLG_SPEED,
                                  xx,
                                  errStr);
                pos = xx[0];
                if (p.id == Planet_SouthNode) pos = swe_degnorm(pos + 180.);
                speed = xx[3];
            }
            break;

        default:
        case amcPrimeVertical: {
            if (p.id == Planet_Asc) {
                pos = 0;
                ret = OK; // speed?
            } else if (p.id == Planet_Desc) {
                pos = 180;
                ret = OK;
            } else if (p.id == Planet_MC) {
                pos = 270;
                ret = OK; // speed?
            } else if (p.id == Planet_IC) {
                pos = 90;
                ret = OK;
            } else {
                ret           = swe_calc_ut(jd,
                                  p.sweNum,
                                  flags & ~SEFLG_SIDEREAL,
                                  xx,
                                  errStr);
                auto housePos = swe_house_pos(getAscMC(2, true /*trop*/).first,
                                              ida.location().y(),
                                              eps,
                                              'C',
                                              xx,
                                              errStr);
                pos           = (housePos - 1) / 12 * 360;
                if (p.id == Planet_SouthNode) pos = swe_degnorm(pos + 180.);
                speed         = xx[3];
            }
            break;
        }
        }
        if (ret != ERR) return pos;
        qDebug() << "Can't calculate position of " << p1.name << "at jd" << jd
                 << ":" << errStr;
        return 0;
    };

    qreal speed = 0;
    auto  pos   = getPos(p1, speed);
    if (planet.isMidpt()) {
        qreal speed2 = 0;
        auto  pos2   = getPos(getPlanet(planet.planetId2()), speed2);
        if (pos - pos2 >= 180.) pos -= 360.;
        else if (pos2 - pos >= 180.)
            pos2 -= 360.;
        pos = swe_degnorm((pos + pos2) / 2.);
        speed += speed2;
        if (planet.isOppMidpt()) pos = swe_degnorm(pos + 180.);
    }
    return { pos, speed };
}

qreal
PlanetLoc::compute(const InputData& ida)
{
    return compute(ida, getJulianDate(ida.GMT(), false, ida.calendarType()), -1);
}

qreal
PlanetLoc::defaultSpeed() const
{
    switch (aspectMode) {
    case amcEquatorial:
    case amcEcliptic:
        if (planet.isMidpt()) {
            auto p1 = getPlanet(planet.planetId());
            auto p2 = getPlanet(planet.planetId2());
            return std::abs(p1.defaultEclipticSpeed.x()
                            - p2.defaultEclipticSpeed.y());
        }
        return getPlanet(planet.planetId()).defaultEclipticSpeed.x();
    case amcPrimeVertical: return -360;
    default:               return 0;
    }
}

/*static*/
std::pair<qreal, qreal>
PlanetProfile::computeDelta(const Loc* a, const Loc* b, unsigned int h /*=1*/)
{
    // It is not obvious what needs to happen here.
    // We certainly want to update the positions of the in-motion
    // entities, but if there is more than one of these, getting
    // an average position wouldn't allow us to converge, only
    // track an aspect to a moving midpoint. In contrast, in most
    // cases we want to find a "root", i.e., a zero-point in between
    // a negative and a positive result. Even if we use a spread
    // value to minimize (will never be less than zero), the
    // averaged speed value isn't necessarily a proper derivative...
    // Hmm... Perhaps when there is more than one planet, we
    // need to compute spread and position? There is more than one
    // value, but we only have one degree of freedom (one input variable)
    // to control...
    qreal speed = (a->speed + b->speed);
    if (h > 1) speed *= qreal(h);

    qreal apos = h > 1 ? harmonic(h, a->loc) : a->loc;
    qreal bpos = h > 1 ? harmonic(h, b->loc) : b->loc;

    if (bpos - apos > 180) bpos -= 360;
    else if (apos - bpos > 180)
        bpos += 360;

    return { bpos - apos, speed };
}

/*static*/
std::pair<qreal, qreal>
PlanetProfile::computeSpread(std::initializer_list<const Loc*> locs,
                             unsigned int                      h /*=1*/)
{
    std::vector<qreal> pos(locs.size());
    qreal              spd = 0.0;
    unsigned           i   = 0;
    for (auto loc : locs) {
        pos[i++] = (h > 1 ? harmonic(h, loc->loc) : loc->loc);
        spd += (h > 1 ? loc->speed * qreal(h) : loc->speed);
    }
    if (false && locs.size() == 2) {
        return { angle(pos[0], pos[1]), spd };
    }

    qreal maxa = 0;
    for (unsigned i = 0, n = pos.size(); i + 1 < n; ++i) {
        for (unsigned j = i + 1; j < n; ++j) {
            auto a = angle(pos[i], pos[j]);
            if (a > maxa) maxa = a;
        }
    }
    return { maxa, spd }; // XXX
}

qreal
PlanetProfile::computePos(double jd, unsigned int h /*=1*/)
{
    Loc::loc   = 0;
    Loc::speed = 0;
    for (auto loc : *this) {
        (*loc)(jd, 1);
    }
    if (size() == 1) {
        Loc::loc   = h > 1 ? harmonic(h, front()->loc) : front()->loc;
        Loc::speed = h > 1 ? h * front()->speed : front()->speed;
    } else if (size() == 2) {
        std::tie(Loc::loc, Loc::speed) = computeDelta(front(), back(), h);
    }
    return Loc::loc;
}

PlanetProfile::PlanetProfile(std::initializer_list<QMap<int, Planet>*> pms)
{
    int fid = 0;
    for (auto ppm : pms) {
        auto& pm = *ppm;
        for (const auto& plan : pm) {
            auto  pid  = plan.id;
            qreal ploc = plan.getPrefPos();
            qreal spd  = plan.getPrefSpd();
            emplace_back(new PlanetLoc(fid, pid, ploc, spd));
        }
        ++fid;
    }
}

qreal
PlanetProfile::computeSpread(double jd)
{
    computePos(jd);
    return computeSpread();
}

qreal
PlanetProfile::computeSpread()
{
    if (size() == 2) return angle(at(0)->loc, at(1)->loc);

    std::sort(begin(), end(), [](const Loc* a, const Loc* b) {
        return a->loc < b->loc;
    });

    qreal maxa = 0;
    for (unsigned i = 0, n = size(); i < n; ++i) {
        auto a = angle(at(i)->loc, at((i + 1) % n)->loc);
        if (a > maxa) maxa = a;
    }

    return maxa;
}

Star
calculateStar(const QString&   name,
              const InputData& input,
              const Houses&    houses,
              const Zodiac&    zodiac)
{
    Star ret = getStar(name);

    uint   invertPositionFlag = 256 * 1024;
    double jd                 = getJulianDate(input.GMT(), false, input.calendarType());
    char   errStr[256]        = "";

    double xx[6];
    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];

    unsigned int flags = ret.sweFlags & ~SEFLG_TRUEPOS; // turn off true pos
    if (zodiac.id > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(zodiac.id - 2, 0, 0);
    }

    // (flags: SEFLG_TRUEPOS|SEFLG_SPEED = 272)
    //         272|invertPositionFlag = 262416
    char starName[256];
    strcpy(starName, ret.name.toStdString().c_str());
    if (swe_fixstar_ut(starName, jd, flags | SEFLG_SWIEPH, xx, errStr) != ERR
        && strlen(errStr) != ERR)
    {
        if (!(ret.sweFlags & invertPositionFlag)) ret.eclipticPos.setX(xx[0]);
        else // found 'inverted position' flag
            ret.eclipticPos.setX(roundDegree(xx[0] - 180));

        ret.eclipticPos.setY(xx[1]);
        ret.distance = xx[2];

        if (swe_fixstar_ut(starName,
                           jd,
                           (flags & ~SEFLG_SIDEREAL) | SEFLG_SWIEPH
                               | SEFLG_EQUATORIAL,
                           xx,
                           errStr)
                != ERR
            && strlen(errStr) == 0)
        {
            ret.equatorialPos.setX(xx[0]);
            ret.equatorialPos.setY(xx[1]);
        }

        double geopos[3]; // calculate horizontal coordinates
        double hor[3];
        geopos[0] = input.location().x();
        geopos[1] = input.location().y();
        geopos[2] = input.location().z();
        swe_azalt(jd, SE_ECL2HOR, geopos, 0, 0, xx, hor);
        ret.horizontalPos.setX(hor[0]);
        ret.horizontalPos.setY(hor[1]);

        // Calculate prime vertical longitude from the Campanus house position,
        // same method as calculatePlanet(). We need the tropical ecliptic
        // longitude, so strip SEFLG_SIDEREAL.
        {
            double pvxx[6];
            char   pvStarName[256];
            strcpy(pvStarName, ret.name.toStdString().c_str());
            if (swe_fixstar_ut(pvStarName,
                               jd,
                               (flags & ~SEFLG_SIDEREAL) | SEFLG_SWIEPH,
                               pvxx,
                               errStr)
                    != ERR
                && strlen(errStr) == 0)
            {
                double housePos =
                    swe_house_pos(houses.RAMC, geopos[1], eps, 'C', pvxx, errStr);
                ret.pvPos               = (housePos - 1) / 12 * 360;
                ret.tropicalEclipticPos = QPointF(pvxx[0], pvxx[1]);
            }
        }

        if (primDirMode == prdActive) {
            double rettm;
            int    eflg = SEFLG_SWIEPH;

            for (auto m = Star::atAsc; m < Star::numAngles;
                 m      = Star::angleTransitMode(m + 1))
            {
                if (swe_rise_trans(houses.startSpeculum,
                                   -1,
                                   starName,
                                   eflg,
                                   Star::angleTransitFlag(m),
                                   geopos,
                                   1013.25,
                                   10,
                                   &rettm,
                                   errStr)
                    >= 0)
                {
                    ret.angleTransit[m] = Planet::timeToDT(rettm);

                    // Calculate RA at this transit time for prdActive mode
                    double xx[6];
                    char   serr[AS_MAXCH];
                    int    iflgret = swe_fixstar_ut(starName,
                                                 rettm,
                                                 SEFLG_EQUATORIAL | SEFLG_SPEED,
                                                 xx,
                                                 serr);
                    if (iflgret >= 0) {
                        ret.angleTransitRA[m] = xx[0]; // RA in degrees
                    } else {
                        ret.angleTransitRA[m] = 0.0;
                    }
                }
            }
        } else {
            std::array<double,Star::numAngles> at;

            double RA = ret.equatorialPos.x();
            double DD;
            if (primDirMode == prdZodiacal) {
                DD = asind(sind(eps) * sind(ret.eclipticPos.x()));
            } else {
                DD = ret.equatorialPos.y();
            }
            double AD = asind(tand(DD) * tand(input.location().y()));
            double OA = RA - AD;
            double OD = RA + AD;

            at[Star::atMC]   = RA;
            at[Star::atAsc]  = swe_degnorm(houses.RAMC + (OA - houses.OAAC));
            at[Star::atDesc] = swe_degnorm(houses.RAMC + (OD - houses.ODDC));
            at[Star::atIC]   = swe_degnorm(RA - 180);

            double jd0          = getJulianDate(input.GMT(), false, input.calendarType());
            double RAMC0        = houses.RAMC; // in degrees
            double sidereal_day = /*1; / */0.99726958;  // days

#if 0
            QString rastr = "Star Angle Transits in RA of " + ret.name.left(3);
            char    delim = ':';
            int32   deg, min, sec, sgn;
            double  frac;
#endif
            for (unsigned i = 0; i < Star::numAngles; ++i) {
                auto m = Star::angleTransitMode(i);
                double RA_target = at[m];
                double delta_deg =
                    swe_difdeg2n(RA_target, RAMC0); // signed difference
                double delta_jd     = delta_deg / 360.0 * sidereal_day;
                double jd_target    = jd0 + delta_jd;
                ret.angleTransit[m] = dateTimeFromJulian(jd_target);

                // Store the RA value for this transit
                ret.angleTransitRA[m] = RA_target;
#if 0
                swe_split_deg(RA_target, 0, &deg, &min, &sec, &frac, &sgn);
                rastr += QString(A_DECODE("%1 %2 %3 %4'%5\""))
                             .arg(delim)
                             .arg(angleDesc[m])
                             .arg(deg)
                             .arg(min, 2, 10, QChar('0'))
                             .arg(sec, 2, 10, QChar('0'));
                delim = ',';
#endif
            }
            // qDebug() << qPrintable(rastr);
        }
    } else {
        qDebug("A: can't calculate position of '%s' at julian day %f: %s",
               qPrintable(ret.name),
               jd,
               errStr);
    }

    ret.house = getHouse(houses, ret.eclipticPos.x());

    return ret;
}

Houses
calculateHouses(const InputData& input)
{
    Houses ret;
    ret.system         = &getHouseSystem(input.houseSystem());
    unsigned int flags = SEFLG_SWIEPH;
    if (input.zodiac() > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(input.zodiac() - 2, 0, 0);
    }

    double julianDay   = getJulianDate(input.GMT(), false /*i.e., UT*/, input.calendarType());
    double jd          = getJulianDate(input.GMT(), true /*i.e., ET*/, input.calendarType());
    char   errStr[256] = "";
    double xx[6];

    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];
    ret.eps    = eps;

    double hcusps[14], ascmc[11];

    // get the tropical ascendant...
    swe_houses_ex(julianDay,
                  flags & ~SEFLG_SIDEREAL,
                  input.location().y(),
                  input.location().x(),
                  ret.system->sweCode,
                  hcusps,
                  ascmc);
    double asc = ascmc[0]; // tropical asc
    ret.RAMC = ascmc[2];
    xx[0]      = ascmc[0];
    xx[1]      = 0.0;
    xx[2]      = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RAAC = xx[0];
    xx[0]    = swe_degnorm(ascmc[0] + 180);
    xx[1]    = 0.0;
    xx[2]    = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RADC = xx[0];
    // TODO could also get eastpoint and vertex in RA here...
    
    // Calculate OAAC and ODDC using declination and geographic latitude
    double DD = asind(sind(eps) * sind(asc));
    double AD = asind(tand(DD) * tand(input.location().y()));
    ret.OAAC  = ret.RAAC - AD;
    
    DD = asind(sind(eps) * sind(swe_degnorm(asc + 180)));
    AD = asind(tand(DD) * tand(input.location().y()));
    ret.ODDC  = ret.RADC + AD;

    if (flags & SEFLG_SIDEREAL) {
        swe_houses_ex(julianDay,
                      flags,
                      input.location().y(),
                      input.location().x(),
                      ret.system->sweCode,
                      hcusps,
                      ascmc);
    }

    for (int i = 0; i < 12; i++) ret.cusp[i] = hcusps[i + 1];

    ret.Asc  = ascmc[0];
    ret.MC   = ascmc[1];
    //ret.RAMC = ascmc[2];
    ret.Vx   = ascmc[3];
    ret.EA   = ascmc[4];

    double st =
        swe_degnorm((swe_sidtime(julianDay)) * 15 + input.location().x());
    double frac;
    int    deg, min, sec, sgn;
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    // qDebug("ST from GMT %3d %02d %02d", deg, min, sec);
    st = swe_degnorm(ret.RAMC);
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    // qDebug("ST from RAMC %3d %02d %02d", deg, min, sec);

    st = swe_degnorm(ret.RAAC);
    swe_split_deg(st, 0, &deg, &min, &sec, &frac, &sgn);
    // qDebug("RAAC %3d %02d %02d", deg, min, sec);

    ret.halfMedium = swe_difdegn(ret.RAAC, ret.RAMC);
    ret.halfImum   = 180 - ret.halfMedium;
    // ret.swneDelta = swe_degnorm(ret.RAMC - swe_degnorm(ret.RAAC - 180)) /
    // 360.0;

    // compute house position of sun so we can see
    // whether it's closer to sunset or sunrise.

    // first get tropical position (well, latitude)
    swe_calc_ut(julianDay, SE_SUN, flags & ~SEFLG_SIDEREAL, xx, errStr);

    double geopos[3] = { input.location().x(),
                         input.location().y(),
                         input.location().z() };

    double housePos = swe_house_pos(ret.RAMC, geopos[1], eps, 'C', xx, errStr);

    // now get our speculum start
    int which = (housePos >= 4 && housePos < 10) ? SE_CALC_RISE : SE_CALC_SET;
    swe_rise_trans(julianDay - 1,
                   SE_SUN,
                   NULL,
                   SEFLG_SWIEPH,
                   which,
                   geopos,
                   1013.25,
                   10,
                   &ret.startSpeculum,
                   errStr);

    return ret;
}

Houses
calculateHouses(const InputData& input, double progressedMC)
{
    // This version calculates houses using a specified MC value
    // Used for progressed charts where MC is calculated via solar arc

    Houses ret;
    ret.system         = &getHouseSystem(input.houseSystem());
    unsigned int flags = SEFLG_SWIEPH;
    if (input.zodiac() > 1) {
        flags |= SEFLG_SIDEREAL;
        swe_set_sid_mode(input.zodiac() - 2, 0, 0);
    }

    double julianDay   = getJulianDate(input.GMT(), false /*i.e., UT*/, input.calendarType());
    double jd          = getJulianDate(input.GMT(), true /*i.e., ET*/, input.calendarType());
    char   errStr[256] = "";
    double xx[6];

    swe_calc_ut(jd, SE_ECL_NUT, 0, xx, errStr);
    double eps = xx[0];
    ret.eps    = eps;

    // Use the provided progressed MC
    ret.MC = progressedMC;

    // Convert MC to RAMC
    xx[0] = progressedMC;
    xx[1] = 0.0;
    xx[2] = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RAMC = xx[0];

    // Calculate Ascendant and house cusps from the progressed RAMC
    double geopos[3] = { input.location().x(),
                         input.location().y(),
                         input.location().z() };

    double hcusps[14], ascmc[11];

    // Use swe_houses_armc to calculate houses from RAMC
    swe_houses_armc(ret.RAMC,
                    geopos[1],
                    eps,
                    ret.system->sweCode,
                    hcusps,
                    ascmc);

    for (int i = 0; i < 12; i++) ret.cusp[i] = hcusps[i + 1];

    ret.Asc = ascmc[0];
    ret.Vx  = ascmc[3];
    ret.EA  = ascmc[4];

    // Calculate RAAC (RA of Ascendant)
    xx[0] = ret.Asc;
    xx[1] = 0.0;
    xx[2] = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RAAC = xx[0];

    xx[0] = swe_degnorm(ret.Asc + 180);
    xx[1] = 0.0;
    xx[2] = 1.0;
    swe_cotrans(xx, xx, -eps);
    ret.RADC = xx[0];

    // Calculate oblique ascension
    double DD = asind(sind(eps) * sind(ret.Asc));
    double AD = asind(tand(DD) * tand(input.location().y()));
    ret.OAAC  = input.location().y() >= 0 ? (ret.RAAC - AD) : (ret.RAAC + AD);
    DD        = asind(sind(eps) * sind(swe_degnorm(ret.Asc + 180)));
    AD        = asind(tand(DD) * tand(input.location().y()));
    ret.ODDC  = input.location().y() >= 0 ? (ret.RADC + AD) : (ret.RADC - AD);

    ret.halfMedium = swe_difdegn(ret.RAAC, ret.RAMC);
    ret.halfImum   = 180 - ret.halfMedium;

    // Speculum start (use current time for this)
    swe_calc_ut(julianDay, SE_SUN, flags & ~SEFLG_SIDEREAL, xx, errStr);
    double housePos = swe_house_pos(ret.RAMC, geopos[1], eps, 'C', xx, errStr);
    int which = (housePos >= 4 && housePos < 10) ? SE_CALC_RISE : SE_CALC_SET;
    swe_rise_trans(julianDay - 1,
                   SE_SUN,
                   NULL,
                   SEFLG_SWIEPH,
                   which,
                   geopos,
                   1013.25,
                   10,
                   &ret.startSpeculum,
                   errStr);

    return ret;
}

PlanetPower
calculatePlanetPower(const Planet& planet, const Horoscope& scope)
{
    PlanetPower ret;

    // TODO: does this shit works properly at all?

    if (!planet.isReal) return ret;

    bool peregrine = false;
    switch (planet.position) {
    case Position_Dwelling:
    case Position_Exaltation: ret.dignity += 5; break;
    case Position_Exile:      ret.deficient -= 5; break;
    case Position_Downfall:   ret.deficient -= 4; break;
    case Position_Normal:     peregrine = true;
    default:                  break;
    }

    if (receptionWith(planet, scope) != Planet_None) ret.dignity += 5;
    else if (peregrine)
        ret.deficient -= 5;

    switch (planet.house) {
    case 1:
    case 10: ret.dignity += 5; break;
    case 4:
    case 7:
    case 11: ret.dignity += 4; break;
    case 2:
    case 5:  ret.dignity += 3; break;
    case 9:  ret.dignity += 2; break;
    case 3:  ret.dignity += 1; break;
    case 12: ret.deficient -= 5; break;
    case 8:
    case 6:  ret.deficient -= 2; break;
    default: break;
    }

    if (planet.eclipticSpeed.x() > 0 && planet.id != Planet_Sun
        && planet.id != Planet_Moon)
    {
        ret.dignity += 4;
    }

    if (planet.eclipticSpeed.x() > planet.defaultEclipticSpeed.x())
        ret.dignity += 2;
    else if (planet.eclipticSpeed.x() > 0)
        ret.deficient -= 2;
    else
        ret.deficient -= 5;

    switch (planet.id) {
    case Planet_Mars:
    case Planet_Jupiter:
    case Planet_Saturn:
        if (isEarlier(planet, scope.sun)) ret.dignity += 2;
        else
            ret.deficient -= 2;
        break;
    case Planet_Mercury:
    case Planet_Venus:
    case Planet_Moon:
        if (!isEarlier(planet, scope.sun)) ret.dignity += 2;
        else
            ret.deficient -= 2;
        break;
    default: break;
    }

    if (planet.id != Planet_Sun) {
        if (angle(planet, scope.sun) > 9) // not burned by sun
            ret.dignity += 5;
        else if (angle(planet, scope.sun) < 0.4) // 'in cazimo'
            ret.dignity += 5;
        else // burned by sun
            ret.deficient -= 4;
    }

    if (planet.id != Planet_Jupiter)
        switch (aspect(planet, scope.jupiter, topAspectSet())) {
        case Aspect_Conjunction: ret.dignity += 5; break;
        case Aspect_Trine:       ret.dignity += 4; break;
        case Aspect_Sextile:     ret.dignity += 3; break;
        default:                 break;
        }

    if (planet.id != Planet_Venus)
        switch (aspect(planet, scope.venus, topAspectSet())) {
        case Aspect_Conjunction: ret.dignity += 5; break;
        case Aspect_Trine:       ret.dignity += 4; break;
        case Aspect_Sextile:     ret.dignity += 3; break;
        default:                 break;
        }

    if (planet.id != Planet_NorthNode)
        switch (aspect(planet, scope.northNode, topAspectSet())) {
        case Aspect_Conjunction:
        /*case Aspect_Trine:
        case Aspect_Sextile:     ret.dignity   += 4; break;
        case Aspect_Opposition:  ret.deficient -= 4; break;*/
        default: break;
        }

    if (planet.id != Planet_Mars)
        switch (aspect(planet, scope.mars, topAspectSet())) {
        case Aspect_Conjunction: ret.deficient -= 5; break;
        case Aspect_Opposition:  ret.deficient -= 4; break;
        case Aspect_Quadrature:  ret.deficient -= 3; break;
        default:                 break;
        }

    if (planet.id != Planet_Saturn)
        switch (aspect(planet, scope.saturn, topAspectSet())) {
        case Aspect_Conjunction: ret.deficient -= 5; break;
        case Aspect_Opposition:  ret.deficient -= 4; break;
        case Aspect_Quadrature:  ret.deficient -= 3; break;
        default:                 break;
        }

    if (aspect(planet, scope.stars["Regulus"], tightConjunction())
        == Aspect_Conjunction)
        ret.dignity += 6; // Regulus coordinates at 2000year: 29LEO50, +00.27'

    if (aspect(planet, scope.stars["Spica"], tightConjunction())
        == Aspect_Conjunction)
        ret.dignity += 5; // Spica coordinates at 2000year: 23LIB50, -02.03'

    if (aspect(planet, scope.stars["Algol"], tightConjunction())
        == Aspect_Conjunction)
        ret.deficient -= 5; // Algol coordinates at 2000year: 26TAU10, +22.25'

    return ret;
}

Aspect
calculateAspect(const AspectsSet& aspectSet,
                const Planet&     planet1,
                const Planet&     planet2)
{
    Aspect a;

    a.angle    = angle(planet1, planet2);
    a.d        = &getAspect(aspect(a.angle, aspectSet), aspectSet);
    a.orb      = fabs(a.d->angle - a.angle);
    a.planet1  = &planet1;
    a.planet2  = &planet2;
    a.applying = towardsMovement(planet1, planet2) == (a.angle > a.d->angle);

    return a;
}

inline Aspect
calculateAspect(const AspectsSet& asps, const Planet* p1, const Planet* p2)
{
    return calculateAspect(asps, *p1, *p2);
}

Aspect
calculateAspect(const AspectsSet& aspectSet, const Loc* p1loc, const Loc* p2loc)
{
    Aspect a;
    auto   l1 = p1loc->loc;
    if (auto pl = dynamic_cast<const PlanetLoc*>(p1loc)) l1 = pl->rasiLoc();
    auto l2 = p2loc->loc;
    if (auto pl = dynamic_cast<const PlanetLoc*>(p2loc)) l2 = pl->rasiLoc();
    a.angle = angle(l1, l2);
    a.d     = &getAspect(aspect(a.angle, aspectSet), aspectSet);
    a.orb   = fabs(a.d->angle - a.angle);
    return a;
}

AspectList
calculateAspects(const AspectsSet& aspectSet, const PlanetMap& planets)
{
    AspectList ret;

    PlanetMap::const_iterator i = planets.constBegin();
    while (i != planets.constEnd()) {
#if 0
        if ((i->id >= Planet_Sun && i->id <= Planet_Pluto)
            || i->id == Planet_Chiron)
        {
#endif
        PlanetMap::const_iterator j = std::next(i);
        while (j != planets.constEnd()) {
            if (/*((j->id >= Planet_Sun && j->id <= Planet_Pluto)
                 || j->id == Planet_Chiron)
                &&*/
                aspect(i.value(), j.value(), aspectSet) != Aspect_None)
            {
                ret << calculateAspect(aspectSet, i.value(), j.value());
            }
            ++j;
        }
#if 0
        }
#endif
        ++i;
    }

    return ret;
}

AspectList
calculateAspects(const AspectsSet& aspectSet, const ChartPlanetPtrMap& planets)
{
    AspectList ret;

    for (auto it = planets.cbegin(); it != planets.cend(); ++it) {
        for (auto jit = std::next(it); jit != planets.cend(); ++jit) {
            if (aspect(it->second, jit->second, aspectSet) != Aspect_None) {
                ret << calculateAspect(aspectSet, it->second, jit->second);
            }
        }
    }

    return ret;
}

AspectList
calculateAspects(const AspectsSet& aspectSet,
                 const PlanetMap&  planets1,
                 const PlanetMap&  planets2)
{
    AspectList ret;
    for (auto i = planets1.constBegin(); i != planets1.constEnd(); ++i) {
        // if ((i->id >= Planet_Sun && i->id <= Planet_Pluto) || i->id ==
        // Planet_Chiron) {
        for (auto j = planets2.constBegin(); j != planets2.constEnd(); ++j) {
            if (/*((j->id >= Planet_Sun && j->id <= Planet_Pluto)
                 || j->id == Planet_Chiron)
                &&*/
                aspect(i.value(), j.value(), aspectSet) != Aspect_None)
            {
                ret << calculateAspect(aspectSet, i.value(), j.value());
            }
        }
        //}
    }

    return ret;
}

void
findPlanetStarConfigs(const PlanetMap& planets, StarMap& stars)
{
    modalize<aspectModeType> aspects(aspectMode, amcGreatCircle);

    for (Star& s : stars.values()) s.configuredWithPlanet = Planet_None;

    for (PlanetId pid : planets.keys()) {
        const Planet& p(planets[pid]);
        for (const std::string& name : stars.keys()) {
            Star& s(stars[name]);
            if (aspect(p, s, tightConjunction()) != Aspect_None) {
                s.configuredWithPlanet = p.id;
            }
        }
    }
}

void
calculateHarmonic(double h, Planet& p)
{
    qreal& ecliPos(p.eclipticPos.rx());
    ecliPos = harmonic(h, ecliPos);

    qreal& equiPos(p.equatorialPos.rx());
    equiPos = harmonic(h, equiPos);

    qreal& pvPos(p.pvPos);
    pvPos = harmonic(h, pvPos);
}

void
calculateHarmonic(double     h,
                  Houses&    houses,
                  PlanetMap& planets,
                  bool       includeAsteroids = true,
                  bool       includeCentaurs  = true)
{
    houses.cusp[0] = harmonic(h, houses.cusp[0]);
    for (int i = 1; i < 12; ++i) {
        // use equal house
        houses.cusp[i] = fmod(houses.cusp[i - 1] + 30.0, 360.);
    }
    houses.Asc  = harmonic(h, houses.Asc);
    houses.RAAC = harmonic(h, houses.RAAC);
    houses.MC   = harmonic(h, houses.MC);
    houses.RAMC = harmonic(h, houses.RAMC);

    for (PlanetId id : getPlanets(includeAsteroids, includeCentaurs)) {
        calculateHarmonic(h, planets[id]);
    }
}

uintMSet
getPrimeFactors(unsigned n)
{
    // Adapted from
    // http://www.geeksforgeeks.org/print-all-prime-factors-of-a-given-number/

    uintMSet ret;
    while (!(n & 0x1)) {
        ret.insert(2);
        n >>= 1;
    }

    for (unsigned i = 3, sn = sqrt(n); i <= sn; i = i + 2) {
        while ((n % i) == 0) {
            ret.insert(i);
            n /= i;
        }
    }

    if (n > 2) ret.insert(n);
    return ret;
}

void
getPrimeFactors(unsigned n, uintSSet& ss)
{
    ss.clear();
    for (auto f : getPrimeFactors(n)) ss.emplace(f);
}

uintMSet
getAllFactors(unsigned h)
{
    uintMSet fs;
    fs.insert(1);
    for (unsigned i = 2, m = sqrt(h); i <= m; ++i) {
        if (h % i == 0) {
            fs.insert(i);
            fs.insert(h / i);
        }
    }
    return fs;
}

void
getAllFactors(unsigned n, uintSSet& ss)
{
    ss.clear();
    for (auto f : getAllFactors(n)) ss.emplace(f);
}

void
getAllFactorsAlt(unsigned n, uintSSet& ss)
{
    getAllFactors(n, ss);
    ss.emplace(n);
}

bool
hasPlanetGroupInLowerHarmonic(const PlanetHarmonics& harmonics,
                              unsigned int           harmonic,
                              const PlanetSet&       plist)
{
    for (unsigned lower : getAllFactors(harmonic)) {
        auto hat = harmonics.find(lower);
        if (hat == harmonics.end()) continue;
#if 1
        for (auto hit : hat->second) {
            bool foundAll = true;
            for (auto pit : plist) {
                if (hit.first.count(pit) == 0) {
                    foundAll = false;
                    break;
                }
            }
            if (foundAll) {
                return true;
            }
        }
#else
        if (hat != scope.harmonics.end() && hat->second.count(plist) != 0) {
            return true;
        }
#endif
    }
    return false;
}

bool
meetsQuorum(const PlanetQueue&   curr,
            const std::set<int>& needs,
            unsigned int         quorum)
{
    std::map<int, int> counts;
    // std::map<int, bool> hasSolo;
    unsigned q = 0;
    // for (int need : needs) { counts[need] = 0; hasSolo[need] = false; }
    for (const PlanetLoc& pl : curr) {
        // if (pl.planet.notwo()) hasSolo[pl.planet.fileId()] = true;
        int n = pl.planet.isSolo() ? 1 : 2;
        q += n, counts[pl.planet.fileId()] += n;
        //++q, ++counts[pl.planet.fileId()];
    }
    if (q < quorum) return false;
    for (int need : needs) {
        if (counts[need] == 0 /*|| !hasSolo[need]*/) return false;
    }
    return true;
}

bool
outOfOrb(const PlanetLoc& a,
         const PlanetLoc& b,
         double           orb,
         bool             tightenForMidpoints = false)
{
    if (tightenForMidpoints && (!a.planet.isSolo() || !b.planet.isSolo())) {
        orb /= 10.;
    }
    return angle(a.loc, b.loc) > orb;
}

std::list<PlanetQueue>
cluster(const PlanetRange& in, qreal orb)
{
    std::list<PlanetQueue> qs;
    qs.push_back(PlanetQueue());
    std::copy(in.cbegin(), in.cend(), std::back_inserter(qs.front()));
    while (!qs.empty()) {
        for (auto q : qs) {
            auto h = q.rbegin();
            while (h != q.rend()) {
                auto t = h;
                ++t;
                while (t != q.rend() && !outOfOrb(*h, *t, orb, false)) {
                    ++t;
                }
                if (t == q.rend()) break;
                PlanetQueue qn;
                qn.splice(qn.end(), q, t.base(), h.base());
                if (q.empty()) q.swap(qn);
            }
        }
    }
    return qs;
}

typedef std::tuple<unsigned, qreal, bool> quorumOrbCleanup;
typedef std::vector<quorumOrbCleanup>     looper;
typedef std::pair<unsigned, PlanetGroups> harmonicResult;

class joiner {
    PlanetHarmonics&   hx;
    std::vector<bool>& sieve;

    typedef std::list<ChartPlanetBitmap> cpbList;
    std::map<unsigned, cpbList>          seen;

  public:
    joiner(PlanetHarmonics& hxIn, std::vector<bool>& primeSieve) :
        hx(hxIn),
        sieve(primeSieve)
    {
    }

    void operator()(uintMSet& completed, const harmonicResult& res)
    {
        completed.insert(res.first);
        if (sieve[res.first]) {
            hx[res.first] = res.second;
            for (auto g : res.second) {
                if (!g.first.containsMidPt())
                    seen[res.first].push_back(g.first);
            }
            return;
        }

        uintMSet factors = getAllFactors(res.first);
        for (auto g : res.second) {
            if (!g.first.containsMidPt()) {
                ChartPlanetBitmap cpb(g.first);
                bool              found = false;
                for (unsigned lh : factors) {
                    for (const auto& lcpb : seen[lh]) {
                        if (lcpb.contains(cpb)) {
#if 0
                            qDebug() << QString("H%1").arg(lh)
                                     << PlanetSet(lcpb).names()
                                     << "already contains"
                                     << QString("H%1").arg(res.first)
                                     << PlanetSet(cpb).names();
#endif
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                }
                if (found) continue;
                seen[res.first].push_back(cpb);
            }

            hx[res.first].insert(g);
        }
    }
};

std::vector<bool>
getPrimeSieve(unsigned top)
{
    std::vector<bool> ret(top + 1, true);
    ret[0] = false;
    for (unsigned i = 2; i <= top; ++i) {
        if (!ret[i]) continue;
        for (unsigned j = 2 * i; j <= top; j += i) {
            ret[j] = false;
        }
    }
    return ret;
}

uintMSet
getPrimes(unsigned top)
{
    uintMSet ret;
    unsigned i = 0;
    for (auto&& p : getPrimeSieve(top)) {
        if (p) ret.insert(i);
        ++i;
    }
    return ret;
}

void
findHarmonics(const ChartPlanetMap& cpm,
              PlanetHarmonics&      hx,
              const looper&         loop,
              bool                  doMidpoints = false)
{
    std::set<int> needsFiles;
    for (const ChartPlanetId& cpi : cpm.keys()) {
        needsFiles.insert(cpi.fileId());
    }

    const unsigned minQuorum = harmonicsMinQuorum();
    const unsigned maxH      = maxHarmonic();

    // Creates a prime sieve and a sequence of numbers which is has primes
    // less than or equal to the primeFactorLimit and non-primes with
    // no factors less than or equal to the primeFactorLimit.
    static std::vector<bool>  primeSieve;
    static std::set<unsigned> seq;
    static unsigned           lastMaxH = 0;
    static unsigned           lastPFL  = 0;
    if (maxH > lastMaxH || lastPFL != primeFactorLimit()) {
        std::set<unsigned> primes { 1 }, nonPrimes;
        primeSieve.assign(maxH + 1, true);
        std::vector<bool> maxFactor(maxH + 1, false);
        for (unsigned int i = 2; i <= maxH; ++i) {
            if (!primeSieve[i]) {
                if (!maxFactor[i]) nonPrimes.insert(i);
                continue;
            }
            bool beyond = i > primeFactorLimit();
            if (!beyond) primes.insert(i);
            for (unsigned int j = i + i; j <= maxH; j += i) {
                primeSieve[j] = false;
                maxFactor[j]  = beyond;
            }
        }
        seq = primes;
        seq.insert(nonPrimes.cbegin(), nonPrimes.cend());
        lastPFL = primeFactorLimit();
    } else if (maxH < lastMaxH) {
        auto zap = seq.lower_bound(maxH + 1);
        while (zap != seq.end()) {
            seq.erase(zap++);
        }
    }
    lastMaxH = maxH;

    auto computePositions =
        [&](unsigned h, PlanetRange& allOfThem, bool doMidpoints = false) {
            for (auto cpit = cpm.cbegin(); cpit != cpm.cend(); ++cpit) {
                Planet p = cpit.value();
                if (h > 1) calculateHarmonic(h, p);

                qreal loc;
                if (aspectMode == amcEcliptic) {
                    loc = p.eclipticPos.x();
                } else if (aspectMode == amcEquatorial) {
                    loc = p.equatorialPos.x();
                } else {
                    loc = p.pvPos;
                }
                allOfThem.insert(PlanetLoc(cpit.key(), loc));

                if (!doMidpoints) continue;

                for (auto cpot = std::next(cpit);
                     cpot != cpm.cend()
                     && cpit.key().fileId() == cpot.key().fileId();
                     ++cpot)
                {
                    Planet p2 = cpot.value();
                    if (h > 1) calculateHarmonic(h, p2);

                    qreal loc1 = loc;
                    qreal loc2;
                    if (aspectMode == amcEcliptic) {
                        loc2 = p2.eclipticPos.x();
                    } else if (aspectMode == amcEquatorial) {
                        loc2 = p2.equatorialPos.x();
                    } else {
                        loc2 = p2.pvPos;
                    }

                    if (loc1 > loc2) qSwap(loc1, loc2);
                    if (loc2 - loc1 > 180) loc1 += 360;
                    loc2 = fmod((loc1 + loc2) / 2, 360.);

                    allOfThem.insert(PlanetLoc(cpit.key().fileId(),
                                               cpit.key().planetId(),
                                               cpot.key().planetId(),
                                               loc2));
                    allOfThem.insert(PlanetLoc(cpit.key().fileId(),
                                               cpot.key().planetId(),
                                               cpit.key().planetId(),
                                               fmod(loc2 + 180, 360.)));
                }
            }
        };

    auto cluster = [&](PlanetGroups&           groups,
                       const quorumOrbCleanup& l,
                       const PlanetRange&      allOfThem) {
        unsigned quorum;
        qreal    orb;
        bool     cleanup;
        std::tie(quorum, orb, cleanup) = l;

        PlanetSet plist;

        // Let's see if anything wraps around...
        ChartPlanetId seen;
        bool          wrapped = false;
        PlanetQueue   current;
        PlanetLoc     last;
        if (!outOfOrb(*allOfThem.crbegin(), *allOfThem.cbegin(), orb)) {
            auto next = allOfThem.crbegin();
            do {
                wrapped = true;
                last    = *next;
                seen    = last.planet;
                current.push_front(last);
            } while (++next != allOfThem.crend()
                     && !outOfOrb(*allOfThem.cbegin(), *next, orb));
        } else {
            last = *allOfThem.cbegin();
        }
        current.push_back(*allOfThem.cbegin());
        for (auto plit = allOfThem.cbegin(); ++plit != allOfThem.cend();) {
            const auto& pl = *plit;
            // const Planet& plan = getPlanet(pl.planet);
            if (/* DISABLES CODE */ (false) && wrapped && pl.planet == seen) {
                break;
            }
            if (outOfOrb(pl, last, orb)) {
                auto next = plit;
                ++next;

                if (meetsQuorum(current, needsFiles, quorum)) {
                    groups.insert(current, minQuorum);
                }
                do {
                    current.pop_front();
                    if (current.empty()) {
                        last = pl;
                        break;
                    }
                    last = *current.cbegin();
                } while (outOfOrb(pl, last, orb));
            }
            current.push_back(pl);
        }

        if (meetsQuorum(current, needsFiles, quorum)) {
            groups.insert(current, minQuorum);
        }
    };

    std::function<harmonicResult(unsigned)> orbLoop = [&](unsigned h) {
        PlanetGroups groups;
        PlanetRange  allOfThem;
        if (doMidpoints) {
            computePositions(h, allOfThem, true);
            for (auto l : loop) {
                std::get<1>(l) /= 10;
                cluster(groups, l, allOfThem);
            }
            allOfThem.clear();
        }
        computePositions(h, allOfThem);
        for (const auto& l : loop) {
            cluster(groups, l, allOfThem);
        }
        return harmonicResult(h, groups);
    };

    using namespace QtConcurrent;

    joiner            j(hx, primeSieve);
    QFuture<uintMSet> f = /*uintSet foo =*/
        mappedReduced<uintMSet>(seq, orbLoop, j, OrderedReduce);
    f.waitForFinished();
}

void
findHarmonics(const ChartPlanetMap& cpm, PlanetHarmonics& hx)
{
    bool     doMidpoints = includeMidpoints();
    int      d       = harmonicsMinQuorum() <= harmonicsMaxQuorum() ? 1 : -1;
    unsigned num     = fabs(harmonicsMaxQuorum() - harmonicsMinQuorum()) + 1;
    qreal    orb     = harmonicsMinQOrb() * orbFactor();
    qreal    lorbMin = log2(orb);
    qreal    lorbMax = log2(harmonicsMaxQOrb() * orbFactor());
    qreal    od      = num < 2 ? 0 : pow(2, (lorbMax - lorbMin) / (num - 1));
    unsigned quorum  = unsigned(harmonicsMinQuorum());
    unsigned i       = 0;
    looper   loop;
    while (i < num) {
        bool cleanup = (++i == num) && (num > 1) && filterFew();
        loop.push_back(quorumOrbCleanup(quorum, orb, cleanup));
        quorum = unsigned(int(quorum) + d);
        orb *= od;
    }
    findHarmonics(cpm, hx, loop, doMidpoints);
    for (auto it = hx.begin(); it != hx.end();) {
        if (it->second.empty()) hx.erase(it++);
        else
            ++it;
    }
}

void
calculateBaseChartHarmonic(Horoscope& scope)
{
    scope.houses  = scope.housesOrig;
    scope.planets = scope.planetsOrig;

    const InputData& input(scope.inputData);
    if (scope.harmonic != 1.0 && aspectMode != amcGreatCircle) {
        calculateHarmonic(scope.harmonic, scope.houses, scope.planets);
    } else {
        findPlanetStarConfigs(scope.planets, scope.stars);
    }

    scope.sun       = scope.planets[Planet_Sun];
    scope.moon      = scope.planets[Planet_Moon];
    scope.mercury   = scope.planets[Planet_Mercury];
    scope.venus     = scope.planets[Planet_Venus];
    scope.mars      = scope.planets[Planet_Mars];
    scope.jupiter   = scope.planets[Planet_Jupiter];
    scope.saturn    = scope.planets[Planet_Saturn];
    scope.uranus    = scope.planets[Planet_Uranus];
    scope.neptune   = scope.planets[Planet_Neptune];
    scope.pluto     = scope.planets[Planet_Pluto];
    scope.northNode = scope.planets[Planet_NorthNode];

    for (PlanetId id : scope.planets.keys()) {
        if (id >= Planet_Sun) {
            scope.planets[id].power =
                calculatePlanetPower(scope.planets[id], scope);
        }
    }
}

/*
An implementation of an improved & simplified Brent's Method.
Calculates root of a function f(x) in the interval [a,b].
Zhang, Z. (2011). An Improvement to the Brent’s Method. International Journal of
Experimental Algorithms (IJEA), (2), 21–26. Retrieved from
http://www.cscjournals.org/csc/manuscript/Journals/IJEA/volume2/Issue1/IJEA-7.pdf
[This link appears to be invalid, though the article is retrievable by googling
the title.]

I've adapted it with the further corrections from Steven Stage, whose analysis
seems to indicate that the method is not quite an improvement in practical
terms, but is clearly a simplification. See:

https://www.cscjournals.org/manuscript/Journals/IJEA/Volume4/Issue1/IJEA-33.pdf

f -> Functor to be evaluated
lo -> Starting point of interval
hi -> Ending point of interval
result -> This will contain the root when the functiuon is complete
tol -> tolerance . Set to a low value like 1e-6
Returns bool indicating success or failure
*/

template <typename F>
bool
brentZhangStage(F&      f,
                double  lo,
                double  hi,
                double  f_lo,
                double  f_hi,
                double& result,
                double  tol = 1e-9)
{
    // Root not bound by the given guesses?
    if (f_lo * f_hi >= 0) return false;

    double s, f_s;
    do {
        double c   = (lo + hi) / 2;
        double f_c = f(c);
        if (fabs(f_lo - f_c) > tol && fabs(f_hi - f_c) > tol) {
            // f(a)!=f(c) and f(b)!=f(c)
            // Inverse quadratic interpolation
            s = lo * f_hi * f_c / ((f_lo - f_hi) * (f_lo - f_c))
                + hi * f_lo * f_c / ((f_hi - f_lo) * (f_hi - f_c))
                + c * f_lo * f_hi / ((f_c - f_lo) * (f_c - f_hi));
            if (s < lo || s > hi) {
                // According to Stage, s can go awry in this
                // calculation (i.e., exceed lo-hi bounds),
                // so we need to get it back in
                // line one of three ways, of which
                s = c; // is the simplest option
            }
        } else {
            // Secant rule
            s = hi - f_hi * (hi - lo) / (f_hi - f_lo);
        }
        f_s = f(s);

        if (c > s) qSwap(s, c);

        if (f_c * f_s < 0) {
            f_lo = f(lo = c);
            f_hi = f(hi = s);
        } else if (f_lo * f_c < 0) {
            f_hi = f(hi = c);
        } else {
            f_lo = f(lo = s);
        }
    } while (f_hi != 0 && f_lo != 0
             && fabs(hi - lo) > tol); // Convergence conditions

    result = hi; // either hi or lo could be returned,
    // as we are now at tolerated convergence.

    return true;
}

template <typename F>
bool
brentZhangStage(F& f, double lo, double hi, double& result, double tol = 1e-9)
{
    if (fabs(hi - lo) <= tol) return false;

    if (lo > hi) qSwap(lo, hi);

    double f_lo = f(lo);
    double f_hi = f(hi);
    return brentZhangStage(f, lo, hi, f_lo, f_hi, result, tol);
}

// Adapted from the Algol60 code in "Algorithms for Minimization
// without Derivatives" by Richard P. Brent (2002).
// Comments inline generally copied verbatim. Yes, it uses gotos!
// I don't quite understand the distinction between err and tol,
// or what 'm' means. (The latter seems to help in bracketing: the
// higher the number, the more likely it can find a tolerable
// solution.)

template <typename F>
double
brentGlobalMin(F       f,
               double  lo,
               double  hi,
               double  guess,
               double  m,
               double  err,
               double  tol,
               double& x)
{
    const auto macheps = std::numeric_limits<double>::epsilon();
    int        k;
    double a0, a2, a3, d0, d1, d2, h, m2, p, q, qs, r, s, y, y0, y1, y2, y3, yb,
        z0, z1, z2;

    x = a0 = hi;
    a2     = lo;
    yb = y0 = f(hi);
    y = y2 = f(lo);
    if (y0 < y) y = y0;
    else
        x = lo;

    if ((m > 0) && (lo < hi)) {
        // nontrivial case
        m2 = 0.5 * (1 + 16 * macheps) * m;
        if ((guess <= lo) || (guess >= hi)) guess = 0.5 * (lo + hi);
        y1 = f(guess);
        k  = 3;
        d0 = a2 - guess;
        h  = 9 / 11;

        if (y1 < y) {
            x = guess;
            y = y1;
        }

    // main loop
    next:
        d1 = a2 - a0;
        d2 = guess - a0;
        z2 = hi - a2;
        z0 = y2 - y1;
        z1 = y2 - y0;
        p = r = d1 * d1 * z0 - d0 * d0 * z1;
        q = qs = 2 * (d0 * z1 - d1 * z0);

        // try to find lower value of f using quadratic interpolation
        if ((k > 100000) && (y < y2)) goto skip;

    retry:
        if (q * (r * (yb - y2) + z2 * q * ((y2 - y) + tol))
            < z2 * m2 * r * (z2 * q - r))
        {
            a3 = a2 + r / q;
            y3 = f(a3);
            if (y3 < y) x = a3, y = y3;
        }

        // with probability about 0.1 do a random search for a lower
        // value of f. Any reasonable random number generator can be
        // used in place of the one here (it need not be very good).

    skip:
        k = 1611 * k;
        k = k - 1048576 * (k / 1048576);
        q = 1;
        r = (hi - lo) * (k / 100000);

        if (r < z2) goto retry;

        // prepare to step as far as possible
        r = m2 * d0 * d1 * d2;
        s = sqrt(((y2 - y) + tol) / m2);
        h = 0.5 * (1 + h);
        p = h * (p + 2 * r * s);
        q = r + 0.5 * qs;
        r = -0.5 * (d0 + (z0 + 2.01 * err) / (d0 * m2));
        r = a2 + ((r < s || d0 < 0) ? s : r);

        // it is safe to step to r, but we may try to step further
        a3 = (p * q > 0) ? a2 + p / q : r;

    inner:
        if (a3 < r) a3 = r;
        if (a3 > hi) {
            a3 = hi;
            y3 = yb;
        } else
            y3 = f(a3);
        if (y3 < y) {
            x = a3;
            y = y3;
        }
        d0 = a3 - a2;

        if (a3 > r) {
            // inspect the parabolic lower bound on f in (a2,a3)
            p = 2 * (y2 - y3) / (m * d0);
            if ((fabs(p) < (1 + 9 * macheps) * d0)
                && (0.5 * m2 * (d0 * d0 + p * p)
                    > (y2 - y) + (y3 - y) + 2 * tol))
            {
                // halve the step and try again
                a3 = 0.5 * (a2 + a3);
                h  = 0.9 * h;
                goto inner;
            }
        }
        if (a3 < hi) {
            // prepare for the next step
            a0    = guess;
            guess = a2;
            a2    = a3;
            y0    = y1;
            y1    = y2;
            y2    = y3;
            goto next;
        }
    }
    return y;
}

QDateTime
dateTimeFromJulian(double jd, CalendarType calType /*=Cal_Auto*/)
{
    // Resolve calendar from JD when Auto
    int gregFlag = 1;
    if (calType == Cal_Julian) {
        gregFlag = 0;
    } else if (calType == Cal_Auto) {
        gregFlag = (jd >= 2299161.0) ? 1 : 0;
    }

    int32  y, d, m;
    int32  hr, min, sec;
    double dsec;
    swe_jdut1_to_utc(jd, gregFlag, &y, &m, &d, &hr, &min, &dsec);
    sec      = dsec;
    int msec = int((dsec - double(sec)) * 1000.0);
    return QDateTime(QDate(y, m, d), QTime(hr, min, sec, msec), QTimeZone::UTC);
}

namespace
{
static bool       s_quiet  = true;
thread_local bool st_quiet = s_quiet;
} // namespace

struct calcPos {
    PlanetProfile& poses;
    uintmax_t      _iter = 0;

    calcPos(PlanetProfile& p) : poses(p) { }

    qreal operator()(double jd)
    {
        ++_iter;
        auto ret = poses.computePos(jd);
        if (!st_quiet) {
            QDateTime dt(dateTimeFromJulian(jd));
            qDebug() << "calc iter:" << dtToString(dt) << "Ret:" << ret;
        }
        return ret;
    }

    uintmax_t& count() { return _iter; }
};

struct calcSpd {
    PlanetProfile& poses;
    calcSpd(PlanetProfile& p) : poses(p) { }

    qreal operator()(double jd)
    {
        poses.computePos(jd);
        if (!st_quiet) {
            qDebug() << "calc iter:" << dtToString(dateTimeFromJulian(jd))
                     << "Ret:" << poses.speed();
        }
        return poses.speed();
    }
};

typedef std::pair<qreal, qreal> posSpd;
struct calcPosSpd {
    PlanetProfile& poses;
    calcPosSpd(PlanetProfile& p) : poses(p) { }

    posSpd operator()(double jd)
    {
        auto pos = poses.computePos(jd);
        auto ret = std::make_pair(pos, poses.speed());

        if (!st_quiet) {
            QDateTime dt(dateTimeFromJulian(jd));
            qDebug() << "ncalc iter:" << dtToString(dt) << "Ret:" << ret;
        }

        return ret;
    }
};

struct calcSpread {
    PlanetProfile& poses;
    int            m = 100;

    calcSpread(PlanetProfile& p) : poses(p) { }

    qreal operator()(double jd)
    {
#if 1
        auto ret = poses.computeSpread(jd);
#else
        for (auto& pos : poses) pos->operator()(jd);
        auto ret = getSpread(poses);
#endif

        QDateTime dt(dateTimeFromJulian(jd));
        qDebug() << "spread iter:" << dtToString(dt) << "Ret:" << ret;
        return ret;
    }
};

struct calcLoop {
    calcPos    cpos;
    calcSpd    cspd;
    calcPosSpd ncpos;
    calcSpread csprd;

    PlanetProfile& poses;
    double&        jd;

    static constexpr double tol    = 5e-08;
    static constexpr int    digits = std::numeric_limits<double>::digits;

    calcLoop(PlanetProfile& ps, double& jdate) :
        cpos(ps),
        cspd(ps),
        ncpos(ps),
        csprd(ps),
        poses(ps),
        jd(jdate)
    {
    }

#if 1 // old version
    bool operator()(double& begin,
                    double  end,
                    double  span,
                    double  flo,
                    double  splo,
                    bool    cont)
    {
        qDebug() << "calcLoop: begin" << begin << "end" << end << "span" << span
                 << "flo" << flo << "splo" << splo;

        bool   done = false;
        double fhi, sphi;
        for (double& jdc = begin; !done && (cont || jdc < end);
             splo = sphi, flo = fhi, jdc += span)
        {
            fhi  = cpos(jdc + span);
            sphi = poses.speed();
            if ((done = (fabs(poses.loc) <= tol))) {
                qDebug() << "  done by span convergence";
                jd = jdc + span / 2;
                continue;
            }
            if (span <= tol) {
                qDebug() << "  zeno's paradox";
                return false;
            }
            if (sgn(flo) == sgn(fhi)) {
                qDebug() << "same sign";
                continue;
            }
            if (abs(fhi) >= 170. && abs(flo) >= 170.) {
                qDebug() << "flo and fhi >= 170";
                continue;
            }
            qDebug() << "sgn(flo)=" << sgn(flo) << " sgn(splo)=" << sgn(splo)
                     << " sgn(fhi)=" << sgn(fhi) << " sgn(sphi)=" << sgn(sphi);
            if (sgn(flo) != sgn(splo) || sgn(fhi) != -sgn(sphi)) {
#if 0
                done = brentZhangStage(cpos, jdc,jdc+span, flo, fhi, jd);
                if (done) qDebug() << "  done by brent";
#else
                double guess =
                    jdc + (fabs(flo) / (fabs(flo) + fabs(fhi))) * span;
                uintmax_t iter = 20;
                try {
                    jd = newton_raphson_iterate(ncpos,
                                                guess,
                                                jdc,
                                                jdc + span,
                                                digits,
                                                iter);

                    done = fabs(poses[1]->loc - poses[0]->loc) <= tol
                           || span < tol;
                    if (done) qDebug() << "  done by newton";
                }
                catch (...) {
                }
#endif
            }
            double b = jdc;
            if (!done) {
                done = operator()(b, jdc + span, span / 4, flo, splo, false);
            }
        }
        if (!done) qDebug() << "  ran out clock";
        return done;
    }
#endif

    template <typename T>
    void calc(double j, T& ret);

    bool signsEqual(const posSpd& a, const posSpd& b) const
    {
        return sgn(a.first) == sgn(b.first);
    }

    bool signsEqual(qreal a, qreal b) const { return sgn(a) == sgn(b); }

    bool speedSignsEqual(const posSpd& a, const posSpd& b) const
    {
        return sgn(a.second) == sgn(b.second);
    }

    bool speedSignsEqual(qreal, qreal) const { return true; }

    bool longDistance(const posSpd& a, const posSpd& b) const
    {
        return abs(a.first) >= 170. && abs(b.first) > 170.;
    }

    bool longDistance(qreal, qreal) { return false; }

    template <typename T>
    bool doIterativeCalc(double& jd, double jlo, double jhi, T lo, T hi);

    template <typename T>
    bool operator()(double& begin, double end, double span, T lo, bool cont)
    {
        qDebug() << "calcLoop: begin" << dtToString(dateTimeFromJulian(begin))
                 << "end" << dtToString(dateTimeFromJulian(end)) << "span"
                 << span;

        bool done = false;
        T    hi;
        for (double& jdc = begin; !done && (cont || jdc < end);
             lo          = hi, jdc += span)
        {
            calc(jdc + span, hi);
            if (poses.needsFindMinimalSpread()) {
                if (doIterativeCalc(jd, jdc, jdc + span, lo, hi)) {
                    modalize<int> precise(csprd.m, 1000);
                    done = doIterativeCalc(jd, jdc, jdc + span, lo, hi);
                }
            } else {
                if ((done = (fabs(poses.loc) <= tol))) {
                    qDebug() << "  done by span convergence";
                    jd = jdc + span / 2;
                    continue;
                }
                if (span <= tol) {
                    qDebug() << "  zeno's paradox";
                    return false;
                }
                if (signsEqual(lo, hi)) {
                    qDebug() << "same sign";
                    continue;
                }
                if (longDistance(lo, hi)) {
                    qDebug() << "flo and fhi >= 170";
                    continue;
                }
                // if (sgn(flo)!=sgn(splo) || sgn(fhi)!=-sgn(sphi)) {
                done = doIterativeCalc(jd, jdc, jdc + span, lo, hi);
                //}
                double b = jdc;
                if (!done) {
                    done = operator()(b, jdc + span, span / 4, lo, false);
                }
            }
        }
        if (!done) qDebug() << "  ran out clock";
        return done;
    }
};

template <>
inline void
calcLoop::calc<posSpd>(double j, posSpd& ret)
{
    ret = ncpos(j);
}

template <>
inline void
calcLoop::calc<qreal>(double j, qreal& ret)
{
    ret = cspd(j);
}

template <>
inline bool
calcLoop::doIterativeCalc<qreal>(double& jd,
                                 double  jlo,
                                 double  jhi,
                                 qreal   lo,
                                 qreal   hi)
{
    bool done = brentZhangStage(cspd, jlo, jhi, lo, hi, jd);
    if (done) qDebug() << "  done by brent";
    return done;
}

template <>
inline bool
calcLoop::doIterativeCalc<posSpd>(double& jd,
                                  double  jlo,
                                  double  jhi,
                                  posSpd  lo,
                                  posSpd  hi)
{
    double span = jhi - jlo;
    double g =
        jlo + (fabs(lo.first) / (fabs(lo.first) + fabs(hi.first))) * span;

    uintmax_t iter = 20;
    try {
        bool done = false;
        if (poses.needsFindMinimalSpread()) {
            constexpr auto tol = double(std::numeric_limits<float>::epsilon());
            brentGlobalMin(csprd,
                           jlo,
                           jhi,
                           jlo / 2. + jhi / 2.,
                           csprd.m,
                           .0000001 /*err*/,
                           tol,
                           jd);
            if (csprd.poses.computeSpread(jd) <= harmonicsMinQOrb()) {
                done = true;
                qDebug() << "  done by brentGlobalMin";
            }
        } else {
            jd   = newton_raphson_iterate(ncpos, g, jlo, jhi, digits, iter);
            done = fabs(poses[1]->loc - poses[0]->loc) <= tol || span < tol;
            if (done) qDebug() << "  done by newton";
        }
        return done;
    }
    catch (...) {
        return false;
    }
}

void
calculateOrbAndSpan(const PlanetProfile& poses,
                    const InputData&     locale,
                    double               harmonic,
                    double&              orb,
                    double&              horb,
                    double&              span)
{
    if (aspectMode == amcPrimeVertical) horb = .5;
    float speed = poses.defaultSpeed();
    orb         = 360 / speed / harmonic;
    horb        = orb / 1.8;

    // auto plid = poses.back()->planet.planetId();
    if (true /*plid == Planet_Sun || plid==Planet_Moon*/) span = orb / 4;
    else
        span = 1 / speed;

    qDebug() << poses[0]->description() << "half-orbit" << horb << "days";
}

QDateTime
calculateClosestTime(PlanetProfile&   poses,
                     const InputData& locale,
                     double           harmonic)
{

    double jdIn = getJulianDate(locale.GMT(), false, locale.calendarType());

    double   jd = jdIn;
    calcLoop looper(poses, jd);

    double orb, horb, span;
    calculateOrbAndSpan(poses, locale, harmonic, orb, horb, span);

    double begin = jdIn - orb / 2;
    double end   = begin + horb * 2;

    calcPos cpos(poses);
    double  flo  = cpos(begin);
    double  splo = poses.speed();

    // if (looper(begin,end,span,flo,true))
    if (looper(begin, end, span, flo, splo, true /*cont*/))
        return dateTimeFromJulian(jd);

    return locale.GMT();
}

QList<QDateTime>
quotidianSearch(PlanetProfile&   poses,
                const InputData& locale,
                const QDateTime& endDT,
                double           span /*= 1.0*/,
                bool             forceMin)
{
    modalize<bool> mum(st_quiet, true);

    double jd1 = getJulianDate(locale.GMT(), false, locale.calendarType());
    double jd2 = getJulianDate(endDT, false, locale.calendarType());

    poses.setForceMinimize(forceMin);
    if (poses.needsFindMinimalSpread()) span *= 2.;
    else
        span /= 4.;

#if 0
    if (poses.needsFindMinimalSpread()) {
#if 1
        calcSpread fsprd(poses);
#else
        auto fsprd = [&] (double j) {
            auto ret = poses.computeSpread(j);
            auto dt = dateTimeFromJulian(j);
            qDebug() << "spreadIter:" << dtToString(dt) << "Ret:" << ret;
            return ret;
        };
#endif

        double x;
        constexpr auto tol = double(std::numeric_limits<float>::epsilon());
        brentGlobalMin(fsprd, jd1, jd2, jd1/2.+jd2/2.,
                            1/*m*/, .0000001/*err*/, tol, x);
        auto ret = QList<QDateTime>() << dateTimeFromJulian(x);
        return ret;
    }
#endif

    double           jd {};
    calcLoop         looper(poses, jd);
    QList<QDateTime> ret;
    auto             loop = [&](auto lo) {
        // modalize<int> precise(looper.csprd.m,1000);
        looper.calc(jd1, lo);
        do {
            if (looper(jd1, jd2, span, lo, false)) {
                auto dt = dateTimeFromJulian(jd);
                if (poses.needsFindMinimalSpread() && !ret.isEmpty()
                    && qAbs(dt.secsTo(ret.back())) < 86400)
                {
                    auto ugh = qAbs(dt.secsTo(ret.back()));
                    qDebug() << ugh << "Let's scrunch up" << dtToString(dt)
                             << "and" << dtToString(ret.back());
                    // XXX goofy hack to ignore adjacent values
                    auto hi  = lo;
                    auto jda = jd - .5;
                    looper.calc(jda, lo);
                    auto jdb = jd + .5;
                    looper.calc(jdb, hi);
                    modalize<int> precise(looper.csprd.m, 1000);
                    bool found = looper.doIterativeCalc(jd, jda, jdb, lo, hi);
                    if (found) {
                        ret.pop_back();
                        dt = dateTimeFromJulian(jd);
                    }
                }
                ret << dt;

                qDebug() << "** Finding:" << dtToString(dt);
                looper.calc(jd1, lo);
            }
        } while (jd1 < jd2);
    };

    if (poses.size() == 1) {
        auto l = *poses.begin();
        // looking for stations...
        if (l->inMotion()) {
            qreal lo {};
            loop(lo);
        }
    } else {
        posSpd lo;
        loop(lo);
    }
    return ret;
}

// ============================================================================
// PSSR (Progressed Sidereal Solar Return) Functions
// ============================================================================

double
calculateRAMS(const QDateTime& dt, bool useApparentSun)
{
    // Calculate Right Ascension of Mean (or Apparent) Sun.
    //
    // RAMS: We use the equatorial RA of the true geometric Sun (SEFLG_EQUATORIAL |
    // SEFLG_NONUT | SEFLG_TRUEPOS). This matches Fagan's tabular approach, where
    // "RAMS" is read directly from a solar ephemeris giving the Sun's RA in
    // equatorial coordinates. Although the strict definition of the Mean Sun's RA
    // equals the mean ecliptic longitude (L0), Fagan's published calculations use
    // the true equatorial RA (read from tables), and that is what produces the
    // correct PSSR dates. Using raw L0 (ecliptic longitude without the obliquity
    // projection) overstates the elapsed RA by ~3° over a 54-day period and pushes
    // the resulting PSSR date ~2 days too early.
    //
    // RAAS: apparent equatorial RA with nutation and aberration, for practitioners
    // who prefer the apparent Sun position.
    double       jd  = getJulianDate(dt, false); // UT
    char         errStr[256];
    double       xx[6];
    unsigned int flags = SEFLG_SWIEPH | SEFLG_EQUATORIAL;

    if (!useApparentSun) {
        // RAMS: geometric equatorial RA, no aberration, no nutation
        flags |= SEFLG_NONUT | SEFLG_TRUEPOS;
    }
    // RAAS: apparent RA — no additional flags needed (nutation/aberration included by default)

    int ret = swe_calc_ut(jd, SE_SUN, flags, xx, errStr);
    if (ret < 0) {
        qWarning() << "calculateRAMS error:" << errStr;
        return 0.0;
    }

    // Format as HH:MM:SS for debug
    auto raToHMS = [](double deg) {
        double hours = deg / 15.0;
        int h = (int)hours;
        int m = (int)((hours - h) * 60.0);
        double s = (((hours - h) * 60.0) - m) * 60.0;
        if (h < 0) h += 24;
        return QString("%1h %2m %3s")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 0, 'f', 3);
    };
    qDebug() << "calculateRAMS:" << (useApparentSun ? "RAAS" : "RAMS")
             << "for" << dt.toString(Qt::ISODate)
             << "=" << xx[0] << "deg (" << qPrintable(raToHMS(xx[0])) << ")";

    return xx[0]; // equatorial RA (both RAMS geometric and RAAS apparent)
}

double
calculateAnniversarySecond(const Houses& return1, const Houses& return2)
{
    // Anniversary Second = (RAMC₂ - RAMC₁ + 24h) / (Mean Sun annual travel)
    // where Mean Sun travels approximately 24h 00m 03s per year

    double ramc1 = return1.RAMC;
    double ramc2 = return2.RAMC;

    // Calculate the difference, accounting for the full 24-hour cycle
    double ramcDiff = swe_difdegn(ramc2, ramc1); // Normalized difference
    if (ramcDiff < 0)
        ramcDiff += 360.0;

    // Add 24 hours (360 degrees) to get the actual advance
    double actualAdvance = ramcDiff + 360.0;

    // Convert to hours (360 degrees = 24 hours)
    double actualAdvanceHours = (actualAdvance / 360.0) * 24.0;

    // Mean Sun's annual travel: approximately 24h 00m 03s = 24.000833 hours
    // This accounts for precession (~50.29" per year ≈ 3.35 seconds per year)
    double meanSunAnnualTravel = 24.000833; // hours

    // Anniversary second
    double anniversarySecond = actualAdvanceHours / meanSunAnnualTravel;

    // Helper lambda to convert degrees to sidereal time HH:MM:SS
    auto degToST = [](double deg) {
        double hours = deg / 15.0;
        int h = (int)hours;
        int m = (int)((hours - h) * 60.0);
        double s = (((hours - h) * 60.0 - m) * 60.0);
        if (h >= 24) h -= 24;
        if (h < 0) h += 24;
        return QString("%1h %2m %3s")
                              .arg(h, 2, 10, QChar('0'))
                              .arg(m, 2, 10, QChar('0'))
                              .arg(s, 0, 'f', 3);
    };

    qDebug() << "=== calculateAnniversarySecond ===";
    qDebug() << "  Return 1 RAMC:" << ramc1 << "deg =" << qPrintable(degToST(ramc1));
    qDebug() << "  Return 2 RAMC:" << ramc2 << "deg =" << qPrintable(degToST(ramc2));
    qDebug() << "  RAMC Diff:" << ramcDiff << "deg =" << qPrintable(degToST(ramcDiff));
    qDebug() << "  Actual Advance (Diff + 360°):" << actualAdvance << "deg =" << qPrintable(degToST(actualAdvance));
    qDebug() << "  Actual Advance Hours:" << actualAdvanceHours << "hours";
    qDebug() << "  Mean Sun Annual Travel:" << meanSunAnnualTravel << "hours";
    qDebug() << "  Anniversary Second:" << anniversarySecond;

    return anniversarySecond;
}

double
calculatePSSRRAMC(const Houses&    returnHouses,
                  const QDateTime& returnTime,
                  const QDateTime& eventTime,
                  double           anniversarySecond,
                  bool             useApparentSun)
{
    // PSSR RAMC = Return RAMC + (Event RAMS - Return RAMS) × Anniversary Second

    double returnRAMS = calculateRAMS(returnTime, useApparentSun);
    double eventRAMS  = calculateRAMS(eventTime, useApparentSun);

    // Calculate elapsed Mean Sun between return and event
    double elapsedRAMS = swe_difdeg2n(eventRAMS, returnRAMS); // Signed difference

    // Convert to hours (degrees to hours: 360° = 24h)
    double elapsedHours = (elapsedRAMS / 360.0) * 24.0;

    // Multiply by anniversary second to get PSSR advancement
    double pssrAdvanceHours = elapsedHours * anniversarySecond;

    // Convert back to degrees
    double pssrAdvanceDegrees = (pssrAdvanceHours / 24.0) * 360.0;

    // Add to return RAMC
    double pssrRAMC = swe_degnorm(returnHouses.RAMC + pssrAdvanceDegrees);

    return pssrRAMC;
}

QDateTime
calculateReturnTime(PlanetId         id,
                    const InputData& native,
                    const InputData& locale,
                    double           harmonic)
{
    modalize<bool> mum(st_quiet, true);

    PlanetProfile poses;
    poses.push_back(new NatalLoc(id, native));
    poses.push_back(new TransitPosition(id, locale));

    return calculateClosestTime(poses, locale, harmonic);
}

PSSRContext
calculatePSSRContext(const Horoscope& returnChart, bool useApparentSun)
{
    PSSRContext ctx;
    ctx.useApparentSun = useApparentSun;
    
    // Store return chart info
    ctx.returnTime = returnChart.inputData.GMT();
    ctx.returnRAMC = returnChart.houses.RAMC;
    ctx.returnRAMS = calculateRAMS(ctx.returnTime, useApparentSun);
    
    // Calculate next return to get anniversary second
    // For solar return: find when Sun returns to its current position one year later
    const Planet* currentSun = nullptr;
    for (const Planet& p : returnChart.planets) {
        if (p.id == Planet_Sun) {
            currentSun = &p;
            break;
        }
    }
    
    if (!currentSun) {
        qDebug() << "calculatePSSRContext: Could not find Sun in return chart";
        return ctx; // Invalid context
    }
    
    double targetSunLon = currentSun->eclipticPos.x();
    double sunSpeed = currentSun->eclipticSpeed.x();
    
    if (sunSpeed <= 0.0) {
        sunSpeed = 0.9856; // Approximate mean motion of Sun in degrees/day
    }
    
    // Estimate next return time (approximately 365.25 days)
    double estimatedDays = 360.0 / sunSpeed;
    double currentJd = getJulianDate(ctx.returnTime, false, returnChart.inputData.calendarType());
    double targetJd = currentJd + estimatedDays;
    
    // Newton-Raphson iteration to find exact return time
    InputData tempInput = returnChart.inputData;
    Houses tempHouses = returnChart.houses;
    
    for (int iter = 0; iter < 10; iter++) {
        tempInput.setGMT(dateTimeFromJulian(targetJd));
        Planet testSun = calculatePlanet(Planet_Sun, tempInput, tempHouses, 
                                        getZodiac(tempInput.zodiac()));
        
        double sunLon = testSun.eclipticPos.x();
        double speed = testSun.eclipticSpeed.x();
        double diff = swe_difdeg2n(sunLon, targetSunLon);
        
        if (qAbs(diff) < 0.00001) break;
        
        if (speed > 0.0) {
            targetJd -= diff / speed;
        } else {
            qDebug() << "calculatePSSRContext: Invalid speed in iteration";
            return ctx; // Invalid context
        }
    }
    
    QDateTime nextReturnTime = dateTimeFromJulian(targetJd);
    
    // Calculate houses for next return
    InputData nextReturnInput = returnChart.inputData;
    nextReturnInput.setGMT(nextReturnTime);
    Houses nextReturnHouses = calculateHouses(nextReturnInput);
    
    // Calculate anniversary second
    ctx.anniversarySecond = calculateAnniversarySecond(returnChart.houses, 
                                                       nextReturnHouses);
    ctx.nextReturnRAMC = nextReturnHouses.RAMC;
    ctx.isValid = true;
    
    qDebug() << "=== calculatePSSRContext ===";
    qDebug() << "  Mode:" << (useApparentSun ? "RAAS (Apparent Sun)" : "RAMS (Mean Sun)");
    qDebug() << "  Return time:" << ctx.returnTime.toString(Qt::ISODate);
    qDebug() << "  Return RAMC:" << ctx.returnRAMC;
    qDebug() << "  Return Sun RA:" << ctx.returnRAMS;
    qDebug() << "  Next return:" << nextReturnTime.toString(Qt::ISODate);
    qDebug() << "  Next return RAMC:" << nextReturnHouses.RAMC;
    qDebug() << "  Anniversary Second:" << ctx.anniversarySecond;
    
    return ctx;
}

QDateTime
calculateAngularDate(const QDateTime&   radixTime,
                     const QDateTime&   angleTime,
                     double             planetRA,
                     double             angleRA,
                     const PSSRContext* pssrCtx,
                     const QString&     debugLabel)
{
    if (pssrCtx && pssrCtx->isValid) {
        // PSSR mode: linear formula — the Mean Sun moves uniformly, so no
        // iterative search is needed.  The anniversary second already captures
        // the RAMC-advance / mean-sun-travel ratio, so we simply invert it:
        //
        //   elapsed_RAMC   = radixTime → angleTime  (in degrees, via 240 s/°)
        //   elapsed_RAMS°  = elapsed_RAMC / anniversarySecond
        //   elapsed_days   = elapsed_RAMS° / 360° × 365.25
        //   event_date     = radixTime + |elapsed_days|   (converse pivots forward)

        // Compute the RAMC arc as the direct sidereal difference between the
        // angle's transit RA and the return RAMC.  This is the canonical
        // Bowser/Fagan formula: accrued_ST = angleRA − returnRAMC.
        // Positive = direct (angle transits after the return), negative = converse.
        double ramcDiff = angleRA - pssrCtx->returnRAMC;
        if (ramcDiff > 180.0) ramcDiff -= 360.0;
        else if (ramcDiff <= -180.0) ramcDiff += 360.0;

        // Invert the anniversary second to get elapsed RAMS, then convert to days.
        double elapsedRAMS = ramcDiff / pssrCtx->anniversarySecond; // degrees
        double elapsedDays = (elapsedRAMS / 360.0) * 365.25;

        // Converse contacts have a negative elapsed time; take the absolute value
        // so the result lands in the future from the return (standard convention).
        qint64 offsetSeconds = static_cast<qint64>(qAbs(elapsedDays) * 86400.0);
        QDateTime result = pssrCtx->returnTime.addSecs(offsetSeconds);

        QString direction = (ramcDiff >= 0) ? "Direct" : "Converse";
        qDebug() << "=== calculateAngularDate (PSSR)" << debugLabel << "===";
        qDebug() << "  Mode:" << (pssrCtx->useApparentSun ? "RAAS" : "RAMS");
        qDebug() << "  returnTime:" << pssrCtx->returnTime.toString(Qt::ISODate);
        qDebug() << "  angleRA:" << angleRA << "°  returnRAMC:" << pssrCtx->returnRAMC << "°";
        qDebug() << "  ramcDiff:" << ramcDiff << "°  anniversarySecond:" << pssrCtx->anniversarySecond;
        qDebug() << "  elapsedRAMS:" << elapsedRAMS << "°  elapsedDays:" << elapsedDays;
        qDebug() << "  result:" << result.toString(Qt::ISODate) << direction;
        qDebug() << "===================================";

        return result;
        
    } else {
        // Primary Direction mode - original formula
        // angleTime is when planet crosses angle (from angleTransit array)
        // The original formula used time difference in seconds, scaled by (365.25/240)
        // This appears to convert sidereal time difference to solar days
        double dist = qAbs(radixTime.secsTo(angleTime));
        int dayDiff = dist * (365.25 / 240.0);
        return radixTime.addDays(dayDiff);
    }
}

Horoscope
calculateAll(const InputData& input)
{
    Horoscope scope;
    scope.inputData = input;

    // For progressed charts, we need to calculate the progressed date
    // using secondary progressions (1 day = 1 year)
    double jd = getJulianDate(input.GMT(), false, input.calendarType());

    // Determine which InputData to use for planet/star calculations
    const InputData* calcInput = &input;
    InputData        progInput; // Will be used if this is a progressed chart

    // Check if this should be calculated as a progressed chart
    // Note: base chart is also used for returns and transits, so we check the progression flag
    if (input.hasBaseChart() && input.isProgressed()) {
        double baseJd    = getJulianDate(input.baseGMT(), false, input.calendarType());
        double yearsDiff = (jd - baseJd) / 365.25;
        double progJd    = baseJd + yearsDiff; // 1 day per year

        // Create a temporary InputData for the progressed calculation date
        progInput = input;
        progInput.setGMT(dateTimeFromJulian(progJd));

        // HOUSE PROGRESSION METHOD: Solar Arc to MC (Traditional)
        //
        // Traditional secondary progressions use "solar arc" for houses:
        // 1. Calculate natal Sun position
        // 2. Calculate progressed Sun position (X days after birth where X =
        // years lived)
        // 3. Solar arc = Progressed Sun longitude - Natal Sun longitude
        // 4. Progressed MC = Natal MC + Solar arc
        // 5. Calculate houses from progressed MC at birth location (but using
        // progressed obliquity)
        //
        // This is the most commonly used method in traditional astrology.
        // The philosophy: House cusps (angles) progress at the same rate as the
        // Sun

        // Calculate natal chart to get natal Sun and natal MC
        // Create a clean natal InputData without base chart reference
        InputData natalInput = input;
        natalInput.setGMT(input.baseGMT()); // Use the natal date
        natalInput.clearBaseChart(); // Clear base chart so it's calculated as a
                                     // standalone natal chart

        Houses natalHouses = calculateHouses(natalInput);
        double natalSunLon = 0.0;
        {
            double       eps      = 0.0;
            unsigned int flags    = 0;
            double       ablong   = 0.0;
            Planet       natalSun = calculatePlanet(Planet_Sun,
                                              natalInput,
                                              natalHouses,
                                              getZodiac(natalInput.zodiac()));
            natalSunLon           = natalSun.eclipticPos.x();
        }

        // Calculate progressed Sun position
        // Clear base chart so progInput is calculated as standalone (not
        // recursively progressed!)
        progInput.clearBaseChart();

        double progSunLon = 0.0;
        {
            double       eps    = 0.0;
            unsigned int flags  = 0;
            double       ablong = 0.0;
            Houses       tempHouses; // Not used, but needed for function call
            Planet       progSun = calculatePlanet(Planet_Sun,
                                             progInput,
                                             tempHouses,
                                             getZodiac(progInput.zodiac()));
            progSunLon           = progSun.eclipticPos.x();
        }

        // Calculate solar arc
        double solarArc = swe_difdegn(progSunLon, natalSunLon);

        // Apply solar arc to natal MC to get progressed MC
        double progressedMC = swe_degnorm(natalHouses.MC + solarArc);

        // Calculate progressed houses using the progressed MC and progressed
        // obliquity
        scope.houses = calculateHouses(progInput, progressedMC);
        scope.zodiac = getZodiac(progInput.zodiac());

        // Use progInput for planet calculations
        calcInput = &progInput;
    } else {
        // Normal (non-progressed) chart calculation
        scope.houses = calculateHouses(input);
        scope.zodiac = getZodiac(input.zodiac());
    }

    // Calculate planets and stars (common code for both progressed and
    // non-progressed)
    for (PlanetId id : getPlanets(true, true)) {
        if (id == Planet_Asc) {
            Planet asc = Data::getPlanet(id);
            asc.eclipticPos.setX(scope.houses.Asc);
            asc.equatorialPos.setX(scope.houses.RAAC);
            asc.pvPos         = 0;
            scope.planets[id] = asc;
        } else if (id == Planet_Desc) {
            Planet desc = Data::getPlanet(id);
            desc.eclipticPos.setX(swe_degnorm(180. + scope.houses.Asc));
            desc.equatorialPos.setX(swe_degnorm(180. + scope.houses.RAAC));
            desc.pvPos        = 180;
            scope.planets[id] = desc;
        } else if (id == Planet_MC) {
            Planet mc = Data::getPlanet(id);
            mc.eclipticPos.setX(scope.houses.MC);
            mc.equatorialPos.setX(scope.houses.RAMC);
            mc.pvPos          = 270;
            scope.planets[id] = mc;
        } else if (id == Planet_IC) {
            Planet ic = Data::getPlanet(id);
            ic.eclipticPos.setX(swe_degnorm(180. + scope.houses.MC));
            ic.equatorialPos.setX(swe_degnorm(180. + scope.houses.RAMC));
            ic.pvPos          = 90;
            scope.planets[id] = ic;
        } else if (id > Planet_Asc && id <= House_12) {
            Planet hc = Data::getPlanet(id);
            hc.eclipticPos.setX(scope.houses.cusp[id - Planet_Asc]);
            hc.equatorialPos.setX(scope.houses.cusp[id - Planet_Asc]); // XXX
            hc.pvPos          = 30 * (id - Planet_Asc);
            hc.house          = id - Planet_Asc;
            scope.planets[id] = hc;
        } else {
            scope.planets[id] =
                calculatePlanet(id, *calcInput, scope.houses, scope.zodiac);
        }
    }

    for (const QString& name : std::as_const(getStars())) {
        scope.stars[name.toStdString()] =
            calculateStar(name, *calcInput, scope.houses, scope.zodiac);
    }

    if (scope.planets.contains(-1)) {
        qDebug() << "Wha?";
    }

    scope.housesOrig  = scope.houses;
    scope.planetsOrig = scope.planets;

    calculateBaseChartHarmonic(scope);

    if (scope.planets.contains(-1)) {
        qDebug() << "Wha?";
    }

    return scope;
}

/*static*/ EventOptions::DisplayMode EventOptions::s_transitBodyColMode =
    EventOptions::DisplayGlyphs;
/*static*/ EventOptions::DisplayMode EventOptions::s_natalTransitBodyColMode =
    EventOptions::DisplayGlyphs;

EventOptions::EventOptions()
{
    // Set reasonable defaults for which event types are enabled
    // These serve as defaults for new chart tabs
    enabledEvents = {
        etcStation,
        etcSignIngress,
        etcTransitToNatal,
        etcTransitToNatalAngles,
        etcReturn,
        etcSolarReturn,
        etcLunarReturn,
        etcProgressedToNatal,
        etcTransitNatalAspectPattern
    };
}

EventOptions::EventOptions(const QVariantMap& map)
{
    defaultTimespan      = map.value("Events/defaultTimespan").toString();
    expandShowOrb        = map.value("Events/secondaryOrb").toDouble();
    planetPairOrb        = map.value("Events/planetPairOrb").toDouble();
    patternsQuorum       = map.value("Events/patternsQuorum").toUInt();
    patternsSpreadOrb    = map.value("Events/patternsSpreadOrb").toDouble();
    paranOrb             = map.value("Mundane/paranOrb", 1.0).toDouble();
    patternsRestrictMoon = map.value("Events/patternsRestrictMoon").toBool();
    includeMidpoints     = map.value("Events/includeMidpoints").toBool();
    setShowStations(map.value("Events/showStations").toBool());
    includeShadowTransits = map.value("Events/includeShadowTransits").toBool();
    setShowTransitsToTransits(
        map.value("Events/showTransitsToTransits").toBool());
    limitLunarTransits = map.value("Events/limitLunarTransits").toBool();
    skipByDuration     = skipper(map.value("Events/skipByDuration").toUInt());
    setShowTransitsToNatalPlanets(
        map.value("Events/showTransitsToNatalPlanets").toBool());
    includeOnlyOuterTransitsToNatal =
        map.value("Events/includeOnlyOuterTransitsToNatal").toBool();
    includeAsteroids = map.value("Events/includeAsteroids").toBool();
    includeCentaurs  = map.value("Events/includeCentaurs").toBool();
    setShowTransitsToNatalAngles(
        map.value("Events/showTransitsToNatalAngles").toBool());
    setShowTransitsToHouseCusps(
        map.value("Events/showTransitsToHouseCusps").toBool());
    setShowReturns(map.value("Events/showReturns").toBool());
    setShowProgressionsToProgressions(
        map.value("Events/showProgressionsToProgressions").toBool());
    setShowProgressionsToNatal(
        map.value("Events/showProgressionsToNatal").toBool());
    includeOnlyInnerProgressionsToNatal =
        map.value("Events/includeOnlyInnerProgressionsToNatal").toBool();
    setShowTransitAspectPatterns(
        map.value("Events/showTransitAspectPatterns").toBool());
    setShowTransitNatalAspectPatterns(
        map.value("Events/showTransitNatalAspectPatterns").toBool());
    setShowIngresses(map.value("Events/showIngresses").toBool());
    setShowLunations(map.value("Events/showLunations").toBool());
    setShowHeliacalEvents(map.value("Events/showHeliacalEvents").toBool());
    setShowPrimaryDirections(
        map.value("Events/showPrimaryDirections").toBool());
    
    // NOTE: The above event type setters are kept for backward compatibility
    // when loading old settings files, but these settings are no longer saved
    // to new files (not in dialog) - event visibility is per-chart via toolbar
    
    // showLifeEvents is computed from enabledEvents >= etcUserEventStart, not
    // loaded directly
    expandShowAspectPatterns =
        map.value("Events/expandShowAspectPatterns").toBool();
    expandShowHousePlacementsOfTransits =
        map.value("Events/expandShowHousePlacementsOfTransits").toBool();
    expandShowRulershipTips =
        map.value("Events/expandShowRulershipTips").toBool();
    expandShowStationAspectsToTransits =
        map.value("Events/expandShowStationAspectsToTransits").toBool();
    expandShowStationAspectsToNatal =
        map.value("Events/expandShowStationAspectsToNatal").toBool();
    expandShowReturnAspects =
        map.value("Events/expandShowReturnAspects").toBool();
    expandShowTransitAspectsToReturnPlanet =
        map.value("Events/expandShowTransitAspectsToReturnPlanet").toBool();
    showHarmonicDividend = map.value("Events/showHarmonicDividend").toBool();

    s_transitBodyColMode =
        DisplayMode(map.value("Events/transitBodyColMode").toUInt());
    s_natalTransitBodyColMode =
        DisplayMode(map.value("Events/natalTransitBodyColMode").toUInt());
}

EventOptions::EventOptions(const EventOptions& opts,
                           const EventTypeSet& exclude) :
    EventOptions(opts)
{
    // Simply remove excluded event types from the enabledEvents set
    for (auto excl : exclude) {
        enabledEvents.erase(excl);
    }
}

/*static*/
const QString&
EventOptions::zposPat()
{
    static QString s_pat;
    if (s_pat.isEmpty()) {
        if (AstroFileEditor::signs.empty()) {
            AstroFileEditor::signs = QStringList(
                {"Aries", "Taurus", "Gemini", "Cancer",
                 "Leo", "Virgo", "Libra", "Scorpio",
                 "Sagittarius", "Capricorn", "Aquarius", "Pisces"});
        }
        QString plre    = "[a-zA-Z]+(-[a-zA-Z])?"; // e.g., planet-r, planet-p
        QString plmpre  = QString("(%1(/%1)?)").arg(plre); // planet/planet
        QString signsre = "(" + AstroFileEditor::signs.join("|") + ")";

        s_pat = "(?<deg>\\d+\\s+)?(?<sign>" + signsre
                + ")" "( ?((?<min>\\d+)'? ?((?<sec>\\d+)\"?)?))?";
    }
    return s_pat;
}

/*static*/
const QRegularExpression&
EventOptions::zposRE()
{
    static QRegularExpression s_re(zposPat(),
                                   QRegularExpression::CaseInsensitiveOption);
    return s_re;
}

/*static*/
const QString&
EventOptions::eventPat()
{
    static QString s_pat;
    if (s_pat.isEmpty()) {
        QString zposre   = zposPat();
        QString zposrea  = zposre;
        QString plre     = "[a-zA-Z]+(-[a-zA-Z])?"; // e.g., planet-r, planet-p
        QString plmpre   = QString("(%1(/%1)?)").arg(plre); // planet/planet
        // Equals-delimited: A=B=C (exact pattern for 3+, pairwise for 2)
        QString plmpeqre = plmpre + "(=" + plmpre + ")*" + "(=(?<posa>"
                           + zposrea.replace(">", "a>") + "))?";
        // Comma-delimited: A,B,C (pairwise pairs + general cluster detection)
        QString plmpcommre = plmpre + "(," + plmpre + ")*";
        // Combined: either equals-delimited or comma-delimited
        // e.g., sun=moon=mars  OR  sun,moon,mars
        // Use \\s+ for whitespace leniency between tokens
        QString plmpzposre = "(?<body>" + plmpre + ")\\s+" + "("
                             + "(?<ingress>ingress\\s+(?<pos>" + zposre + "))"
                             + "|(?<ret>return)"
                             + ")"; // e.g., sun ingress capricorn, sun return
        QString commastr  = "(?<comma>" + plmpcommre + ")";
        QString asprestr  = "(?<aspect>" + plmpeqre + ")";
        // Use generic plre so abbreviations like "Sat station" work
        QString stationre = "((?<station>(" + plre + "))\\s+station)";
        // Paran clause: "Par Mars-t = Jup-r" or "Par Mars-t Asc = Jup-r MC"
        // angleTok: longest alternatives first to avoid partial matches.
        QString angleTok  = "(?:Asc|Desc|Ds|MC|IC|A|D|M|I)";
        QString bodyAngle = "(?:" + plmpre + "(?:\\s+" + angleTok + ")?)";
        QString paranre   = "(?<paran>(?:Par|Paran)\\s+" + bodyAngle
                            + "(?:\\s*[=+]\\s*" + bodyAngle + ")+)";
        // Harmonic specifiers:
        //   H4      → hstrict="4"               (strict single harmonic)
        //   H4*     → hstrict="4", hstar="*"     (all divisors via getAllFactorsAlt)
        //   H-6     → hmax="6"                   (dynamic selection capped at 6)
        //   H{1,5,9}→ hset="1,5,9"              (explicit set)
        QString hre = "(H((?<hstrict>\\d+(\\.\\d+)?)(?<hstar>\\*)?" 
                      "|-(?<hmax>\\d+)"
                      "|\\{(?<hset>\\d+(,\\d+)*)\\})\\s+)?";
        s_pat = "(" + stationre + "|" + paranre + "|" + hre + "("
                + plmpzposre + "|" + asprestr + "|" + commastr + ")" + ")";
    }
    return s_pat;
}

/*static*/
const QRegularExpression&
EventOptions::eventRE()
{
    // Anchor so the regex must match the ENTIRE subject.  Without this,
    // the equals-delimited branch (which can match a single planet name)
    // wins on the first token and the comma-delimited branch is never tried.
    static QRegularExpression s_re("^(?:" + eventPat() + ")$",
                                   QRegularExpression::CaseInsensitiveOption);
    return s_re;
}

/*static*/
const QString&
EventOptions::eventMultiPat()
{
    static QString s_mpat;
    if (s_mpat.isEmpty()) {
        const QString& single = eventPat();
        // Allow one or more patterns separated by ';' with optional whitespace
        s_mpat = single + "(\\s*;\\s*" + single + ")*";
    }
    return s_mpat;
}

/*static*/
const QRegularExpression&
EventOptions::eventMultiRE()
{
    static QRegularExpression s_re(eventMultiPat(),
                                   QRegularExpression::CaseInsensitiveOption);
    return s_re;
}

/*static*/
bool
EventOptions::isValidPattern(const QString& text)
{
    QString t = text.trimmed();
    if (t.isEmpty()) return false;
    const auto& re = eventRE();
    for (const auto& sp : t.split(';')) {
        auto piece = sp.trimmed();
        if (piece.isEmpty()) continue;
        auto m = re.match(piece);
        if (!m.hasMatch() || m.capturedLength() != piece.length())
            return false;
    }
    return true;
}

QVariantMap
EventOptions::toMap()
{
    QVariantMap ret;
    ret.insert("Events/defaultTimespan", defaultTimespan.toString());
    ret.insert("Events/secondaryOrb", expandShowOrb);
    ret.insert("Events/planetPairOrb", planetPairOrb);
    ret.insert("Events/patternsQuorum", patternsQuorum);
    ret.insert("Events/patternsSpreadOrb", patternsSpreadOrb);
    ret.insert("Events/patternsRestrictMoon", patternsRestrictMoon);
    ret.insert("Events/includeMidpoints", includeMidpoints);
    
    // Event type visibility settings (showStations, showTransitsToTransits, etc.)
    // are NOT saved here - they are managed per-chart via toolbar and saved per-file
    // Only global calculation settings are saved to the settings file
    
    ret.insert("Events/includeShadowTransits", includeShadowTransits);
    ret.insert("Events/limitLunarTransits", limitLunarTransits);
    ret.insert("Events/skipByDuration", skipByDuration);
    ret.insert("Events/includeOnlyOuterTransitsToNatal",
               includeOnlyOuterTransitsToNatal);
    ret.insert("Events/includeAsteroids", includeAsteroids);
    ret.insert("Events/includeCentaurs", includeCentaurs);
    ret.insert("Events/includeOnlyInnerProgressionsToNatal",
               includeOnlyInnerProgressionsToNatal);

    ret.insert("Events/expandShowAspectPatterns", expandShowAspectPatterns);
    ret.insert("Events/expandShowHousePlacementsOfTransits",
               expandShowHousePlacementsOfTransits);
    ret.insert("Events/expandShowRulershipTips", expandShowRulershipTips);
    ret.insert("Events/expandShowStationAspectsToTransits",
               expandShowStationAspectsToTransits);
    ret.insert("Events/expandShowStationAspectsToNatal",
               expandShowStationAspectsToNatal);
    ret.insert("Events/expandShowReturnAspects", expandShowReturnAspects);
    ret.insert("Events/expandShowTransitAspectsToReturnPlanet",
               expandShowTransitAspectsToReturnPlanet);
    ret.insert("Events/showHarmonicDividend", showHarmonicDividend);
    ret.insert("Events/transitBodyColMode", s_transitBodyColMode);
    ret.insert("Events/natalTransitBodyColMode", s_natalTransitBodyColMode);
    return ret;
}

std::ostream&
operator<<(std::ostream& os, const PlanetClusterMap& pcm)
{
    os << "(";
    bool any = false;
    for (const auto& pc : pcm) {
        if (any) os << ",\n\t";
        os << pc.first.names().join("=").toStdString() << " spread "
           << pc.second;
        any = true;
    }
    os << ")";
    return os;
}

QDebug
operator<<(QDebug qd, const PlanetClusterMap& pcm)
{
    std::stringstream ss;
    ss << pcm;
    return qd << ss.str().c_str();
}

std::ostream&
operator<<(std::ostream& os, const PlanetProfile& pp)
{
    bool any = false;
    for (auto&& ploc : pp) {
        if (any) os << " ";
        any = true;
        os << ploc->description().toStdString();
    }
    return os;
}

QDebug
operator<<(QDebug qd, const PlanetProfile& pp)
{
    std::stringstream ss;
    ss << pp;
    return qd << ss.str().c_str();
}

std::string
formatPlanetsEtc(const planetsEtc& pe,
                 const PlanetProfile& alist,
                 const hsets& hsets)
{
    auto [i, j] = pe.planetPair;
    auto ai = dynamic_cast<PlanetLoc*>(alist[i]);
    auto aj = dynamic_cast<PlanetLoc*>(alist[j]);

    QStringList hs;
    for (auto h : hsets[pe.hsid]) hs << QString::number(h);

    return QString("H{%1} %2=%3 %4")
        .arg(hs.join(","))
        .arg(ai->description())
        .arg(aj->description())
        .arg(EventTypeManager::eventTypeToString(pe.et))
        .toStdString();
}

/// The intent is to build _staff which comprises the aspect search and _alist
/// Overloaded constructor that accepts EventOptions to apply before processing
OmnibusFinder::OmnibusFinder(HarmonicEvents&      evs,
                             const ADateRange&    range,
                             const uintSSet&      hset,
                             const AstroFileList& files,
                             const EventOptions&  options,
                             const EventTypeSet&  exclude /*={}*/) :
    AspectFinder(evs, range, hset, exclude, afcFindStuff)
{
    // Apply event options BEFORE processing files
    static_cast<EventOptions&>(*this) = options;
    
    // Debug: verify enabledEvents was copied
    qDebug() << "[OMNIBUS CONSTRUCTOR] After copy, enabledEvents.size():" << enabledEvents.size()
             << "showIngresses():" << showIngresses()
             << "isEnabled(etcSignIngress):" << isEnabled(etcSignIngress);
    
    // Continue with normal initialization
    initializeFromFiles(files);
}

/// Original constructor for backwards compatibility
OmnibusFinder::OmnibusFinder(HarmonicEvents&      evs,
                             const ADateRange&    range,
                             const uintSSet&      hset,
                             const AstroFileList& files,
                             const EventTypeSet&  exclude /*={}*/) :
    AspectFinder(evs, range, hset, exclude, afcFindStuff)
{
    initializeFromFiles(files);
}

/// Constructor with pattern support
OmnibusFinder::OmnibusFinder(HarmonicEvents&      evs,
                             const ADateRange&    range,
                             const uintSSet&      hset,
                             const AstroFileList& files,
                             const QString&       pattern,
                             const EventTypeSet&  exclude /*={}*/) :
    AspectFinder(evs, range, hset, exclude, afcFindStuff)
{
    qDebug() << "[OMNIBUS CONSTRUCTOR] Pattern set to:" << pattern;

    enabledEvents.clear();
    initializeFromPattern(pattern, files);
}

void
OmnibusFinder::initializeFromFiles(const AstroFileList& files)

{
    // This ugly jumble intends to generate the appropriate planet listings,
    // and then create the T-T T-N P-P P-N pairings. And the ingresses, etc.
    // Better to have some kind of factory scheme, but for now...

    // Toolbar-driven path: enable general cluster scanning when TA/TNA buttons
    // are active (pattern path sets this selectively in initializeFromPattern).
    _generalClustersEnabled =
        showTransitAspectPatterns() || showTransitNatalAspectPatterns();
    _patternMode = false;

    bool natal = false, trans = false, prog = false;
    int  natus = -1, locus = -1, progr = -1;

    QMap<ChartPlanetId, unsigned> natalIndex, natalRevIndex;
    QMap<ChartPlanetId, unsigned> transitIndex, transitRevIndex;
    QMap<ChartPlanetId, unsigned> progressedIndex, progressedRevIndex;

    uintSSet conjSet { 1 };
    hsetId   conj = _hsets.size();
    _hsets.emplace_back(conjSet);

    uintSSet conjOppSet { 1, 2 };
    hsetId   conjOpp = _hsets.size();
    _hsets.emplace_back(conjOppSet);

    uintSSet conjOppSqSet { 1, 2, 4 };
    hsetId   conjOppSq = _hsets.size();
    _hsets.emplace_back(conjOppSqSet);

    hsetId allAsp = 0;
    //_hsets.emplace_back(hset);  // the all aspects set already there...

    double njd = 0;
    for (int i = 0, n = files.count(); i < n; ++i) {
        auto f = files.at(i);
        _ids.push_back(f->horoscope().inputData);

        const auto& ida  = f->horoscope().inputData;
        auto        type = f->getType();
        if (type == TypeMale || type == TypeFemale || type == TypeEvent || type == TypeReturn) {
            // Only set natus to the FIRST Male/Female/Event/Return file found
            if (!natal) {
                natus = i, natal = true;
                njd = getJulianDate(ida.GMT(), false, ida.calendarType());
            } else if (!trans) {
                // Second Male/Female/Event file becomes the transit/comparison chart
                locus = i, trans = true;
            }
        } else if (type == TypeDerivedProg)
            progr = i, prog = true;
        else
            locus = i, trans = true;
    }

    // Cache natal-epoch values for ex-precession context (equatorial mode)
    if (natal && aspectMode == amcEquatorial) {
        double xx[6];
        char   errStr[256];
        swe_calc(njd, SE_ECL_NUT, 0, xx, errStr);
        _exprecessCtx.jdNatal   = njd;
        _exprecessCtx.obliquity = xx[0];
        _exprecessCtx.ayanamsa  = swe_get_ayanamsa(njd);
        _hasExprecessCtx        = true;
    }

    QVector<ZodiacSign> signs = getZodiac(_ids[0].zodiac()).signs.toVector();

    auto getIngress = [&](PlanetId ingr, bool forward = true) {
        unsigned i = ingr - Ingresses_Start;
        if (!forward) ingr += 12;
        ChartPlanetId                  cpid(-1, ingr, Planet_None);
        QMap<ChartPlanetId, unsigned>& inx =
            forward ? transitIndex : transitRevIndex;
        if (!inx.contains(cpid)) {
            inx[cpid] = _alist.size();
            qreal loc =
                forward ? signs[i].startAngle : signs[(i + 1) % 12].startAngle;
            auto pl = new PlanetLoc(cpid, "I", loc);
            pl->allowAspects =
                forward ? PlanetLoc::aspOnlyDirect : PlanetLoc::aspOnlyRetro;
            _alist.push_back(pl);
        }
        return inx.value(cpid);
    };

    Houses houses; // natal houses if needed

    typedef std::function<unsigned(PlanetId)>       getter;
    typedef std::function<unsigned(PlanetId, bool)> getterAlt;

    getter    getNatalPlanet;
    getter    getTransitPlanet;
    getter    getProgressedPlanet;
    getterAlt getHouseIngress;

    if (natal) {
        getNatalPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(natus, pid, Planet_None);
            if (!natalIndex.contains(cpid)) {
                natalIndex[cpid] = _alist.size();
                NatalPosition* pl;
                // [ANGLE_PRECESSION] Angles (Asc–MC) are now included in
                // NatalExprecessedPosition — the constructor computes their
                // true RA/Dec from tropical ecliptic lon (lat=0).
                // House cusps (>= Angles_End) remain excluded.
                if (aspectMode == amcEquatorial && pid < Angles_End)
                    pl = new NatalExprecessedPosition(cpid, _ids[natus], "r");
                else
                    pl = new NatalPosition(cpid, _ids[natus], "r");
                if (pid >= Houses_Start && pid < Houses_End) {
                    pl->allowAspects = PlanetLoc::aspOnlyConj;
                }
                _alist.push_back(pl);
            }
            return natalIndex.value(cpid);
        };

        if (natal && showTransitsToHouseCusps()) {
            houses          = calculateHouses(_ids[natus]);
            getHouseIngress = [&](PlanetId ingr, bool forward = true) {
                ChartPlanetId cpid(-1, ingr, Planet_None);

                auto&& inx = forward ? transitIndex : transitRevIndex;
                if (!inx.contains(cpid)) {
                    inx[cpid]  = _alist.size();
                    unsigned i = ingr - Houses_Start;

                    qreal loc =
                        forward ? houses.cusp[i] : houses.cusp[(i + 1) % 12];
                    auto pl = new PlanetLoc(cpid, "HI", loc);

                    pl->allowAspects = forward ? PlanetLoc::aspOnlyDirect
                                               : PlanetLoc::aspOnlyRetro;
                    _alist.push_back(pl);
                }
                return inx.value(cpid);
            };
        }
    }
    if (!trans && prog) {
        locus = progr;
        trans = true;
    } else if (!trans && natal) {
        locus = natus;
        trans = true;
    }
    if (trans) {
        getTransitPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(locus, pid, Planet_None);
            if (!transitIndex.contains(cpid)) {
                transitIndex[cpid] = _alist.size();
                _alist.push_back(new TransitPosition(cpid, _ids[locus]));
            }
            return transitIndex.value(cpid);
        };
    }
    if (!prog && trans) {
        progr = locus;
        prog  = true;
    }
    if (natal) {
        getProgressedPlanet = [&](PlanetId pid) {
            ChartPlanetId cpid(progr, pid, Planet_None);
            if (!progressedIndex.contains(cpid)) {
                progressedIndex[cpid] = _alist.size();
                auto pl = new ProgressedPosition(cpid, _ids[natus], njd);
                if (pid >= Houses_Start && pid < Houses_End) {
                    pl->allowAspects = PlanetLoc::aspOnlyConj;
                }
                _alist.push_back(pl);
            }
            return progressedIndex.value(cpid);
        };
    }

    // Paranatellonta: ensure transit (and, for Par=N, natal) bodies are in
    // _alist. findParans() walks _alist directly and does not need _staff
    // entries, so we just call the existing getters for their side effect.
    // Idempotent against the calls below: getTransitPlanet/getNatalPlanet
    // dedupe via transitIndex/natalIndex.
    if (trans && (showParanatellonta() || showParanatellontaToNatal())) {
        for (auto pid : getPlanets(includeAsteroids, includeCentaurs)) {
            getTransitPlanet(pid);
        }
    }
    if (natal && showParanatellontaToNatal()) {
        for (auto pid : getPlanets(includeAsteroids, includeCentaurs)) {
            getNatalPlanet(pid);
        }
    }

    if (trans
        && (showTransitsToTransits() || showTransitAspectPatterns()
            || showTransitNatalAspectPatterns() || showStations()
            || showIngresses()))
    {
        QVector<unsigned> ppi, ppo;
        for (auto pid : getPlanets(includeAsteroids, includeCentaurs)) {
            ppi << getTransitPlanet(pid);
        }
        if (false && includeOnlyOuterTransitsToNatal) {
            for (auto pid : getOuterPlanets(includeCentaurs)) {
                ppo << getTransitPlanet(pid);
            }
            // if (!showTransitsToNatalPlanets) ppi = ppo; // only
            // outer-to-outer!
        } else
            ppo = ppi;
        // the above loop has added the planets to the list used
        // by pattern or station finder

        int in = ppi.size();
        int on = ppo.size();
        if (showTransitsToTransits()) {
            for (int i = 0; i < in; ++i) {
                hsetId hs = allAsp;
                auto   tp = dynamic_cast<TransitPosition*>(_alist[ppi[i]]);
                auto   pl = tp->planet.planetId();
                if (pl == Planet_NorthNode || pl == Planet_SouthNode
                    || (pl > Planet_Moon && pl <= Planet_Jupiter))
                {
                    hs = conj;
                } else if (limitLunarTransits && pl == Planet_Moon) {
                    continue;
                }
                for (int j = qMax(0, i + 1 - (in - on)); j < on; ++j) {
                    if (ppi[i] == ppo[j]) continue;
                    auto hst = hs;
                    auto tp  = dynamic_cast<TransitPosition*>(_alist[ppo[j]]);
                    auto opl = tp->planet.planetId();
                    if (opl == Planet_NorthNode || opl == Planet_SouthNode
                        || (opl > Planet_Moon && opl <= Planet_Jupiter))
                    {
                        if (hs == conj) continue;
                        hst = conj;
                    }
                    if (opl == Planet_Moon) {
                        if (pl > Planet_Sun && limitLunarTransits) continue;
                        hst = conjOppSq;
                    }
                    // else if (opl == Planet_Sun) hst = conjOpp;
                    _staff.emplace_back(ppi[i],
                                        ppo[j],
                                        hst,
                                        etcTransitToTransit);
                }
            }
        }

        if (showIngresses()) {
            for (auto i : std::as_const(ppi)) {
                auto tp = dynamic_cast<TransitPosition*>(_alist[i]);
                auto pl = tp->planet.planetId();
                for (PlanetId pid = Ingresses_Start; pid < Ingresses_End; ++pid)
                {
                    if (limitLunarTransits && pl == Planet_Moon
                        && ((pid - Ingresses_Start) % 3 != 0))
                        continue;

                    auto j = getIngress(pid);
                    _staff.emplace_back(i, j, conj, etcSignIngress);

                    // luminaries don't need the backwards ingress
                    if (pl == Planet_Sun || pl == Planet_Moon) continue;

                    j = getIngress(pid, false /*backward*/);
                    _staff.emplace_back(i, j, conj, etcSignIngress);
                }
            }
        }
    }

    if (natal) {
        if (showProgressionsToProgressions()) {
            QVector<unsigned> tpi;
            for (auto pid : getPlanets()) {
                tpi << getProgressedPlanet(pid);
            }
            for (int i = 0; i < tpi.size(); ++i) {
                hsetId hs = allAsp;
                auto   pp = dynamic_cast<ProgressedPosition*>(_alist[tpi[i]]);
                auto   pl = pp->planet.planetId();

                // North/South Node only use conjunction
                if (pl == Planet_NorthNode || pl == Planet_SouthNode) {
                    hs = conj;
                }

                for (int j = i + 1; j < tpi.size(); ++j) {
                    auto hst = hs;
                    auto pp2 = dynamic_cast<ProgressedPosition*>(_alist[tpi[j]]);
                    auto opl = pp2->planet.planetId();

                    // Skip North Node to South Node pairs - they're always opposite
                    if ((pl == Planet_NorthNode || pl == Planet_SouthNode)
                        && (opl == Planet_NorthNode || opl == Planet_SouthNode))
                    {
                        continue;
                    }

                    // If the other planet is also a node, use conjunction only
                    if (opl == Planet_NorthNode || opl == Planet_SouthNode) {
                        hst = conj;
                    }

                    _staff.emplace_back(tpi[i], tpi[j], hst, etcProgressedToProgressed);
                }
            }
        }

        if (anyEnabled(etcTransitToNatal, etcOuterTransitToNatal) || showTransitNatalAspectPatterns()
            || showTransitsToHouseCusps() 
            || showTransitsToNatalAngles()
            || showReturns())
        {
            QList<PlanetId> npl;
            if (anyEnabled(etcTransitToNatal, etcOuterTransitToNatal)
                || showTransitNatalAspectPatterns())
                npl << getPlanets();

            QVector<unsigned> ppn;
            for (auto pid : std::as_const(npl)) {
                ppn << getNatalPlanet(pid);
            }

            if (!anyEnabled(etcTransitToNatal, etcOuterTransitToNatal)) ppn.clear();

            if (anyEnabled(etcTransitToNatal, etcOuterTransitToNatal) 
                || (showTransitsToNatalAngles() && anyEnabled(etcTransitToNatal, etcOuterTransitToNatal))
                || showTransitsToHouseCusps())
            {
                QList<PlanetId> tpl;
                // Determine which planets to include based on which event type is enabled
                bool onlyOuter = isEnabled(etcOuterTransitToNatal) && !isEnabled(etcTransitToNatal);
                if (onlyOuter) {
                    tpl = getOuterPlanets(includeCentaurs);
                } else {
                    tpl = getPlanets(includeAsteroids, includeCentaurs);
                }

                for (auto pid : std::as_const(tpl)) {
                    auto i = getTransitPlanet(pid);
                    for (auto j : std::as_const(ppn)) {
                        auto tp = dynamic_cast<TransitPosition*>(_alist[i]);
                        auto np = dynamic_cast<NatalPosition*>(_alist[j]);
                        if (tp->planet.planetId() != np->planet.planetId()
                            || (!showReturns()
                                && showTransitsToNatalPlanets()))
                        {
                            auto hs = allAsp;
                            auto pl = np->planet.planetId();
                            if (pl == Planet_NorthNode
                                || pl == Planet_SouthNode)
                                hs = conj;
                            else {
                                pl = tp->planet.planetId();
                                if (pl == Planet_NorthNode
                                    || pl == Planet_SouthNode)
                                    hs = conj;
                            }
                            _staff.emplace_back(i, j, hs, etcTransitToNatal);
                        }
                    }
                    if (showTransitsToHouseCusps()) {
                        for (int h = Houses_Start; h < Houses_End; ++h) {
                            // FIXME retrograde to prior house, etc. like sign
                            // ingr
                            _staff.emplace_back(i,
                                                getHouseIngress(h, true),
                                                conj,
                                                etcHouseIngress);
                            _staff.emplace_back(i,
                                                getHouseIngress(h, false),
                                                conj,
                                                etcHouseIngress);
                        }
                    } else if (showTransitsToNatalAngles() && anyEnabled(etcTransitToNatal, etcOuterTransitToNatal)) {
                        for (auto a : getAngles()) {
                            _staff.emplace_back(i,
                                                getNatalPlanet(a),
                                                conj,
                                                etcTransitToNatalAngles);
                        }
                    }
                }
            }
            if (showReturns()) {
                QList<PlanetId> tpl = getPlanets();
                for (auto pid : std::as_const(tpl)) {
                    auto   i  = getTransitPlanet(pid);
                    auto   j  = getNatalPlanet(pid);
                    hsetId hs = conjOpp;
                    auto   tp = dynamic_cast<TransitPosition*>(_alist[i]);
                    auto   pl = tp->planet.planetId();
                    EventType etype = etcReturn;
                    if (pl == Planet_Sun) {
                        etype = etcSolarReturn;
                    } else if (pl == Planet_Moon) {
                        etype = etcLunarReturn;
                    }
                    if (pl == Planet_NorthNode || pl == Planet_SouthNode) {
                        hs = conj;
                    } else if (pl >= Planet_Jupiter && pl <= Planet_Chiron) {
                        hs = allAsp;
                    } else if (pl != Planet_Moon) {
                        hs = conjOppSq;
                    }
                    _staff.emplace_back(i, j, hs, etype);
                }
            }
        }

        if (showProgressionsToNatal()) {
            QList<PlanetId> ppl;
            // Determine which planets to include based on which event type is enabled
            bool onlyInner = isEnabled(etcInnerProgressedToNatal) && !isEnabled(etcProgressedToNatal);
            if (onlyInner)
                ppl = getInnerPlanets(includeAsteroids);
            else
                ppl = getPlanets(includeAsteroids, includeCentaurs);

            auto npl = getPlanets(includeAsteroids, includeCentaurs);
            npl << getAngles();

            for (auto pid : std::as_const(ppl)) {
                auto i = getProgressedPlanet(pid);
                for (auto npid : std::as_const(npl)) {
                    // Skip node-to-node aspects
                    if ((pid == Planet_NorthNode || pid == Planet_SouthNode) &&
                        (npid == Planet_NorthNode || npid == Planet_SouthNode)) {
                        continue;
                    }
                    auto j = getNatalPlanet(npid);
                    _staff.emplace_back(i, j, allAsp, etcProgressedToNatal);
                }
            }
        }
    }

#if 1
    for (const auto& pe : _staff) {
        auto [i, j] = pe.planetPair;
        auto ai = dynamic_cast<PlanetLoc*>(_alist[i]);
        auto aj = dynamic_cast<PlanetLoc*>(_alist[j]);
        const auto& pi = ai->planet;
        const auto& pj = aj->planet;
        if ((pi.fileId() < 0) != (pj.fileId() < 0)) {
            if (pi.planetId() == pj.planetId()) {
            qDebug()
                    << "Looking good"
                    << ai->description()
                    << aj->description();
            }
            continue;
        }

        qDebug() << formatPlanetsEtc(pe, _alist, _hsets).c_str();
    }
#endif
}

void
OmnibusFinder::initializeFromPattern(const QString&       pattern,
                                     const AstroFileList& files)
{
    // Parse pattern; fall back to full initialization if invalid
    if (pattern.isEmpty()) return;

    // Split on ';' to support multiple sub-patterns, trim whitespace
    QStringList subPatterns;
    for (const auto& sp : pattern.split(';'))
        if (!sp.trimmed().isEmpty()) subPatterns << sp.trimmed();
    if (subPatterns.isEmpty()) return;

    // Validate every sub-pattern against eventRE before proceeding
    for (const auto& sp : std::as_const(subPatterns)) {
        QRegularExpressionMatch m = eventRE().match(sp);
        if (!m.hasMatch() || m.capturedLength() != sp.length()) {
            qDebug() << "[PATTERN] Invalid sub-pattern:" << sp
                     << "falling back to initializeFromFiles";
            initializeFromFiles(files);
            return;
        }
    }

    enabledEvents.clear();
    _generalClustersEnabled = false;   // pattern path sets this selectively
    _patternMode = true;

    // --- Identify natal/transit files (shared across all sub-patterns) ---
    bool natal = false, trans = false;
    int  natus = -1, locus = -1;
    for (int i = 0, n = files.count(); i < n; ++i) {
        auto f = files.at(i);
        _ids.push_back(f->horoscope().inputData);
        auto type = f->getType();
        if (type == TypeMale || type == TypeFemale || type == TypeEvent || type == TypeReturn) {
            if (!natal) { natus = i; natal = true; }
            else if (!trans) { locus = i; trans = true; }
        } else {
            locus = i; trans = true;
        }
    }
    if (!trans && natal) { locus = natus; trans = true; }

    // --- Shared deduplicating planet getters ---
    double njd = natal ? getJulianDate(_ids[natus].GMT()) : 0;

    // Cache natal-epoch values for ex-precession context (equatorial mode)
    if (natal && aspectMode == amcEquatorial) {
        double xx[6];
        char   errStr[256];
        swe_calc(njd, SE_ECL_NUT, 0, xx, errStr);
        _exprecessCtx.jdNatal   = njd;
        _exprecessCtx.obliquity = xx[0];
        _exprecessCtx.ayanamsa  = swe_get_ayanamsa(njd);
        _hasExprecessCtx        = true;
    }

    QMap<ChartPlanetId, unsigned> transitIndex, natalIndex, progressedIndex;

    auto getTransitPlanet = [&](PlanetId pid) -> unsigned {
        ChartPlanetId cpid(locus, pid, Planet_None);
        if (!transitIndex.contains(cpid)) {
            transitIndex[cpid] = _alist.size();
            _alist.push_back(new TransitPosition(cpid, _ids[locus]));
        }
        return transitIndex.value(cpid);
    };

    auto getNatalPlanet = [&](PlanetId pid) -> unsigned {
        ChartPlanetId cpid(natus, pid, Planet_None);
        if (!natalIndex.contains(cpid)) {
            natalIndex[cpid] = _alist.size();
            // [ANGLE_PRECESSION] Angles (Asc–MC) now included; house cusps excluded.
            if (aspectMode == amcEquatorial && pid < Angles_End)
                _alist.push_back(new NatalExprecessedPosition(cpid, _ids[natus], "r"));
            else
                _alist.push_back(new NatalPosition(cpid, _ids[natus], "r"));
        }
        return natalIndex.value(cpid);
    };

    auto getProgressedPlanet = [&](PlanetId pid) -> unsigned {
        ChartPlanetId cpid(natus, pid, Planet_None);
        if (!progressedIndex.contains(cpid)) {
            progressedIndex[cpid] = _alist.size();
            _alist.push_back(new ProgressedPosition(cpid, _ids[natus], njd));
        }
        return progressedIndex.value(cpid);
    };

    // Resolve "Planet", "Planet-t/-r/-p", or "Planet/Planet" (midpoint) to an
    // _alist index. Returns {index, mode} where mode is 't', 'r', or 'p'.
    // Midpoint tokens may be "Ura/Plu", "Moo-r/Jup-r", etc.
    // We must check for '/' BEFORE splitting on '-', because a token like
    // "Moo-r/Jup-r" split on '-' yields ["Moo","r/Jup","r"], losing the
    // midpoint structure.
    auto resolvePlanet = [&](const QString& tok,
                             QChar defaultMode = 't')
        -> std::pair<unsigned, QChar /*mode*/> {

        // --- Midpoint notation (contains '/') ---
        if (tok.contains('/')) {
            auto mpls = tok.split('/');
            if (mpls.size() != 2)
                return { unsigned(-1), '\0' };

            // Parse each half: "Moo-r" → name "Moo", suffix 'r'
            auto parsePart = [](const QString& part)
                -> std::pair<QString, QChar> {
                auto dp = part.trimmed().split('-');
                QString name = dp.first().trimmed();
                QChar suf = (dp.size() > 1 && !dp[1].isEmpty())
                                ? dp[1].at(0).toLower() : QChar('\0');
                return { name, suf };
            };

            auto [name1, suf1] = parsePart(mpls[0]);
            auto [name2, suf2] = parsePart(mpls[1]);

            auto pid1 = getPlanetId(name1);
            auto pid2 = getPlanetId(name2);
            if (pid1 == Planet_None || pid2 == Planet_None)
                return { unsigned(-1), '\0' };
            // ChartPlanetId's default ctor sets _oppMidpt=true whenever
            // pid1 > pid2 (then swaps), so a user-typed "Ura/Mar" pattern
            // would be silently converted to the opposition midpoint.
            // Pre-sort so we always hit the non-opposition canonical path.
            if (pid1 > pid2) std::swap(pid1, pid2);

            // Mode: prefer first suffix, then second, then default
            QChar suffix = (suf1 != '\0') ? suf1
                         : (suf2 != '\0') ? suf2 : QChar('\0');
            QChar mode = (suffix != '\0') ? suffix : defaultMode;

            ChartPlanetId cpid(pid1, pid2);
            if (mode == 'r' && natal) {
                if (!natalIndex.contains(cpid)) {
                    natalIndex[cpid] = _alist.size();
                    _alist.push_back(new NatalPosition(
                        ChartPlanetId(natus, pid1, pid2), _ids[natus], "r"));
                }
                return { natalIndex.value(cpid), 'r' };
            }
            if (mode == 'p' && natal) {
                if (!progressedIndex.contains(cpid)) {
                    progressedIndex[cpid] = _alist.size();
                    _alist.push_back(new ProgressedPosition(
                        ChartPlanetId(natus, pid1, pid2), _ids[natus], njd));
                }
                return { progressedIndex.value(cpid), 'p' };
            }
            if (trans) {
                if (!transitIndex.contains(cpid)) {
                    transitIndex[cpid] = _alist.size();
                    _alist.push_back(new TransitPosition(
                        ChartPlanetId(locus, pid1, pid2), _ids[locus]));
                }
                return { transitIndex.value(cpid), 't' };
            }
            return { unsigned(-1), '\0' };
        }

        // --- Simple planet (no '/') ---
        auto parts = tok.split('-');
        QString body = parts.first().trimmed();
        QChar suffix = (parts.size() > 1 && !parts[1].isEmpty())
                           ? parts[1].at(0).toLower() : QChar('\0');
        QChar mode = (suffix != '\0') ? suffix : defaultMode;

        auto pid = getPlanetId(body);
        if (pid == Planet_None) return { unsigned(-1), '\0' };

        if (mode == 'r' && natal)
            return { getNatalPlanet(pid), 'r' };
        if (mode == 'p' && natal)
            return { getProgressedPlanet(pid), 'p' };
        if (trans)
            return { getTransitPlanet(pid), 't' };
        return { unsigned(-1), '\0' };
    };

    // Paranatellonta: ensure transit (and, for Par=N, natal) bodies are in
    // _alist whenever paran event types are enabled. Pattern syntax does
    // not currently express paran clusters, so we rely on the toolbar
    // toggles. findParans walks _alist directly; no _staff entries needed.
    if (trans && (showParanatellonta() || showParanatellontaToNatal())) {
        for (auto pid : getPlanets(includeAsteroids, includeCentaurs)) {
            getTransitPlanet(pid);
        }
    }
    if (natal && showParanatellontaToNatal()) {
        for (auto pid : getPlanets(includeAsteroids, includeCentaurs)) {
            getNatalPlanet(pid);
        }
    }

    // --- Shared deduplication across all sub-patterns ---
    QSet<QPair<unsigned,unsigned>> seen;

    // ===== Process each sub-pattern =====
    for (const auto& subPat : std::as_const(subPatterns)) {
    QRegularExpressionMatch match = eventRE().match(subPat);
    // Already validated above, but guard anyway
    if (!match.hasMatch()) continue;

    qDebug() << "[PATTERN] sub-pattern:" << subPat;
    qDebug() << "[PATTERN] captured groups:"
             << "station=" << match.captured("station")
             << "hstrict=" << match.captured("hstrict")
             << "hstar=" << match.captured("hstar")
             << "hmax=" << match.captured("hmax")
             << "hset=" << match.captured("hset")
             << "body=" << match.captured("body")
             << "ingress=" << match.captured("ingress")
             << "ret=" << match.captured("ret")
             << "aspect=" << match.captured("aspect")
             << "comma=" << match.captured("comma")
             << "pos=" << match.captured("pos")
             << "sign=" << match.captured("sign")
             << "posa=" << match.captured("posa")
             << "paran=" << match.captured("paran");

    // Per-sub-pattern harmonic: use base hset unless H specifier is given
    //   H4       → strict single harmonic {4}
    //   H4*      → all divisors of 4 → {1,2,4}
    //   H-6      → dynamic selection capped at 6
    //   H{1,5,9} → explicit set {1,5,9}
    hsetId useHset = 0;
    QString hstrict = match.captured("hstrict");
    QString hstar   = match.captured("hstar");
    QString hmaxStr = match.captured("hmax");
    QString hsetStr = match.captured("hset");

    if (!hstrict.isEmpty()) {
        unsigned h = hstrict.toUInt();
        if (h >= 1) {
            if (!hstar.isEmpty()) {
                // H4* → expand to all divisors: {1,2,4}
                uintSSet factors;
                getAllFactorsAlt(h, factors);
                useHset = _hsets.size();
                _hsets.emplace_back(std::move(factors));
                QStringList fl;
                for (unsigned f : _hsets.back()) fl << QString::number(f);
                qDebug() << "[PATTERN] H" << h << "* → factors: {" << fl.join(",") << "}";
            } else {
                // H4 → strict single harmonic {4}
                useHset = _hsets.size();
                _hsets.emplace_back(uintSSet { h });
            }
        }
    } else if (!hmaxStr.isEmpty()) {
        // H-6 → dynamic selection capped at specified maximum
        unsigned hmax = hmaxStr.toUInt();
        if (hmax >= 1 && !_hsets.empty()) {
            uintSSet capped;
            for (unsigned v : _hsets[0]) {
                if (v <= hmax) capped.insert(v);
            }
            if (!capped.empty()) {
                useHset = _hsets.size();
                _hsets.emplace_back(std::move(capped));
                QStringList cl;
                for (unsigned v : _hsets.back()) cl << QString::number(v);
                qDebug() << "[PATTERN] H-" << hmax << " → capped: {" << cl.join(",") << "}";
            }
        }
    } else if (!hsetStr.isEmpty()) {
        // H{1,5,9} → explicit harmonic set
        uintSSet explicit_hs;
        for (const auto& s : hsetStr.split(',')) {
            unsigned v = s.trimmed().toUInt();
            if (v >= 1) explicit_hs.insert(v);
        }
        if (!explicit_hs.empty()) {
            useHset = _hsets.size();
            _hsets.emplace_back(std::move(explicit_hs));
            QStringList sl;
            for (unsigned v : _hsets.back()) sl << QString::number(v);
            qDebug() << "[PATTERN] H{" << hsetStr << "} → set: {" << sl.join(",") << "}";
        }
    }

    // --- Extract pattern components ---
    QString stationPat = match.captured("station");
    QString paranPat   = match.captured("paran");
    QString bodyPat    = match.captured("body");
    QString aspectPat  = match.captured("aspect");
    QString commaPat   = match.captured("comma");
    bool    hasIngress = !match.captured("ingress").isEmpty();
    bool    hasReturn  = !match.captured("ret").isEmpty();

    // --- Helper shared by paran and aspect/comma dispatch ---
    // Resolves a group token to a planet list + implicit mode.
    // Group tokens are exact, case-insensitive matches — checked BEFORE
    // getPlanetId() which uses startsWith (so "N" would match Neptune).
    //   OT = outer transit planets       IP = inner progressed planets
    //   T  = all transit planets         P  = all progressed planets
    //   N  = all natal planets           NA = natal angles (Asc/IC/Desc/MC)
    auto resolveGroup = [&](const QString& body)
        -> std::pair<QList<PlanetId>, QChar /*implicitMode*/> {
        QString b = body.trimmed().toUpper();
        if (b == "OT") return { getOuterPlanets(includeCentaurs), 't' };
        if (b == "IP") return { getInnerPlanets(includeAsteroids), 'p' };
        if (b == "T")  return { getPlanets(includeAsteroids, includeCentaurs), 't' };
        if (b == "P")  return { getPlanets(includeAsteroids, includeCentaurs), 'p' };
        if (b == "N")  return { getPlanets(includeAsteroids, includeCentaurs), 'r' };
        if (b == "NA") return { getAngles(), 'r' };
        return { {}, '\0' };
    };

    // --- Dispatch by pattern type ---

    if (!stationPat.isEmpty()) {
        // "Mars station" → just put Mars in _alist; station finder scans it
        enable(etcStation);
        if (trans) {
            auto pid = getPlanetId(stationPat);
            if (pid != Planet_None) getTransitPlanet(pid);
        }

    } else if (!paranPat.isEmpty()) {
        // "Par Mars-t = Jup-r", "Par OT + Ven-r", "Par Mars-t Asc + Jup-r MC", etc.
        // Separators '=' and '+' are equivalent.
        // Group tokens (OT, T, N, NA, IP, P) expand into one spec per member.
        static QRegularExpression paranPfxRe("^(?:Par|Paran)\\s+",
                                             QRegularExpression::CaseInsensitiveOption);
        static QRegularExpression angleSfxRe("\\s+(Asc|Desc|Ds|MC|IC|A|D|M|I)$",
                                             QRegularExpression::CaseInsensitiveOption);
        // Map angle abbreviation → bitmask (bit N = angle index N: 0=Asc,1=Desc,2=MC,3=IC).
        auto angleToMask = [](const QString& tok) -> int {
            QString t = tok.trimmed().toLower();
            if (t == "asc" || t == "a")               return 1;  // Asc  (m=0)
            if (t == "desc" || t == "ds" || t == "d") return 2;  // Desc (m=1)
            if (t == "mc"   || t == "m")               return 4;  // MC   (m=2)
            if (t == "ic"   || t == "i")               return 8;  // IC   (m=3)
            return 0xF;                                            // any
        };

        QString operandStr = paranPat;
        operandStr.remove(paranPfxRe);
        QStringList entryStrs = operandStr.split(QRegularExpression("\\s*[=+]\\s*"));
        if (entryStrs.size() < 2) {
            qWarning() << "[PATTERN] paran needs >= 2 entries:" << paranPat;
            continue;
        }

        // Pre-scan: derive default mode.  Explicit -t present → untagged = -r;
        // explicit -r only → untagged = -t (transit).  Group tokens' implicit
        // modes also count (OT/T/IP/P → 't'; N/NA/P → check grpMode).
        QChar defaultMode = 't';
        {
            bool anyR = false, anyT = false;
            for (const auto& raw : std::as_const(entryStrs)) {
                QString e = raw.trimmed();
                QRegularExpressionMatch am = angleSfxRe.match(e);
                if (am.hasMatch()) e = e.left(am.capturedStart()).trimmed();
                if (e.contains('/')) continue;
                // Group token?
                auto [grpPlanets, grpMode] = resolveGroup(e);
                if (!grpPlanets.isEmpty()) {
                    if (grpMode == 'r') anyR = true;
                    else if (grpMode == 't') anyT = true;
                    continue;
                }
                auto parts = e.split('-');
                if (parts.size() > 1 && !parts[1].trimmed().isEmpty()) {
                    QChar suf = parts[1].trimmed().toLower().at(0);
                    if (suf == 'r') anyR = true;
                    else if (suf == 't') anyT = true;
                }
            }
            if (anyT && !anyR) defaultMode = 'r';
        }

        // Parse each entry into (resolved _alist indices, angleMask).
        // Group tokens expand to multiple indices; singletons give one.
        // isGroup=true → contributes one member per spec (distribution);
        // isGroup=false → every spec gets this index.
        struct ParsedEntry {
            QVector<unsigned> indices;
            int               angleMask;
            bool              isGroup;
        };
        QVector<ParsedEntry> parsedEntries;
        bool anyNatal   = false;
        bool anyTransit = false;
        bool valid      = true;

        for (const auto& raw : std::as_const(entryStrs)) {
            QString entryStr  = raw.trimmed();
            int     angleMask = 0xF;

            QRegularExpressionMatch angleM = angleSfxRe.match(entryStr);
            if (angleM.hasMatch()) {
                angleMask = angleToMask(angleM.captured(1));
                entryStr  = entryStr.left(angleM.capturedStart()).trimmed();
            }

            // Group token? Expand to all members, one per spec (distributed).
            auto [grpPlanets, grpImplicitMode] = resolveGroup(entryStr);
            if (!grpPlanets.isEmpty()) {
                QChar mode = (grpImplicitMode != '\0') ? grpImplicitMode : defaultMode;
                QVector<unsigned> idxs;
                for (PlanetId pid : grpPlanets) {
                    unsigned idx = unsigned(-1);
                    if (mode == 'r' && natal) idx = getNatalPlanet(pid);
                    else if (trans)           idx = getTransitPlanet(pid);
                    if (idx != unsigned(-1)) idxs << idx;
                }
                if (idxs.isEmpty()) { valid = false; break; }
                if (mode == 'r') anyNatal   = true;
                else             anyTransit = true;
                parsedEntries.append({ idxs, angleMask, true });
                continue;
            }

            // Reject progressed.
            {
                auto parts = entryStr.split('-');
                if (parts.size() > 1 && parts[1].trimmed().toLower() == "p") {
                    qWarning() << "[PATTERN] progressed bodies not supported in paran:" << entryStr;
                    valid = false;
                    break;
                }
            }

            auto [idx, mode] = resolvePlanet(entryStr, defaultMode);
            if (idx == unsigned(-1)) {
                qWarning() << "[PATTERN] unknown planet in paran pattern:" << entryStr;
                valid = false;
                break;
            }
            if (mode == 'r') anyNatal   = true;
            else             anyTransit = true;
            parsedEntries.append({ { idx }, angleMask, false });
        }

        if (!valid || parsedEntries.size() < 2) {
            qDebug() << "[PATTERN] paran spec invalid or < 2 parsed entries, skipping";
            continue;
        }

        EventType specEt = anyNatal ? etcParanatellontaToNatal : etcParanatellonta;
        enable(specEt);

        // Separate singleton entries (always present) from group entries
        // (each contributes one member per spec via cross-product).
        QVector<int> groupEIs, singletonEIs;
        for (int i = 0; i < parsedEntries.size(); ++i)
            (parsedEntries[i].isGroup ? groupEIs : singletonEIs) << i;

        // Build cross-product of group entries.
        QVector<QVector<unsigned>> combos;
        combos << QVector<unsigned>{};
        for (int gi : groupEIs) {
            QVector<QVector<unsigned>> expanded;
            for (const auto& combo : std::as_const(combos))
                for (unsigned idx : parsedEntries[gi].indices) {
                    auto ext = combo;
                    ext << idx;
                    expanded << ext;
                }
            combos = std::move(expanded);
        }

        // Emit one ParanPatternSpec per combo (or one if no groups).
        int specsBefore = int(_paranPatterns.size());
        for (const auto& combo : std::as_const(combos)) {
            ParanPatternSpec spec;
            spec.et = specEt;
            for (int si : singletonEIs)
                spec.entries.push_back({ parsedEntries[si].indices[0],
                                         parsedEntries[si].angleMask });
            int comboPos = 0;
            for (int gi : groupEIs)
                spec.entries.push_back({ combo[comboPos++],
                                         parsedEntries[gi].angleMask });
            if (spec.entries.size() >= 2)
                _paranPatterns.push_back(std::move(spec));
        }

        qDebug() << "[PATTERN] paran registered:" << paranPat
                 << "specs added:" << (int(_paranPatterns.size()) - specsBefore)
                 << "et:" << specEt;

    } else if (!bodyPat.isEmpty() && hasIngress) {
        // "Mars ingress Aries" → transit planet paired with fixed sign position
        enable(etcSignIngress);
        if (trans) {
            auto pid = getPlanetId(bodyPat);
            if (pid != Planet_None) {
                unsigned ti = getTransitPlanet(pid);

                ZodiacId zid = _ids[0].zodiac();
                QString signName = match.captured("sign");
                qreal signPos = getSignPos(zid,
                                           signName,
                                           match.captured("deg").trimmed().toUInt(),
                                           match.captured("min").toUInt(),
                                           match.captured("sec").toUInt());

                // Resolve sign index (0-11) from sign name
                int signIdx = 0;
                const auto& Z = getZodiac(zid);
                for (int si = 0; si < Z.signs.size(); ++si) {
                    if (Z.signs[si].name.startsWith(signName, Qt::CaseInsensitive)) {
                        signIdx = si;
                        break;
                    }
                }
                PlanetId ingrFwd = Ingresses_Start + signIdx;
                PlanetId ingrRev = ingrFwd + 12;

                // Forward ingress
                unsigned ji = _alist.size();
                auto pl = new PlanetLoc({-1, ingrFwd, Planet_None}, "I", signPos);
                pl->allowAspects = PlanetLoc::aspOnlyDirect;
                _alist.push_back(pl);
                _staff.emplace_back(ti, ji, useHset, etcSignIngress);

                // Retrograde ingress
                unsigned ri = _alist.size();
                auto plR = new PlanetLoc({-1, ingrRev, Planet_None}, "I", signPos);
                plR->allowAspects = PlanetLoc::aspOnlyRetro;
                _alist.push_back(plR);
                _staff.emplace_back(ti, ri, useHset, etcSignIngress);
            }
        }

    } else if (!bodyPat.isEmpty() && hasReturn) {
        // "Sun return" → transit planet conjunct natal planet
        enable(etcReturn);
        if (trans && natal) {
            auto pid = getPlanetId(bodyPat);
            if (pid != Planet_None) {
                unsigned ti = getTransitPlanet(pid);
                unsigned ni = getNatalPlanet(pid);

                EventType etype = etcReturn;
                if (pid == Planet_Sun)       { etype = etcSolarReturn; enable(etcSolarReturn); }
                else if (pid == Planet_Moon)  { etype = etcLunarReturn; enable(etcLunarReturn); }

                _staff.emplace_back(ti, ni, useHset, etype);
            }
        }

    } else if (!aspectPat.isEmpty() || !commaPat.isEmpty()) {
        // Unified handling for both = and , delimited patterns.
        // "Sun=Moon"       → pairwise (2-body, unchanged)
        // "Sun=Moon=Mars"  → exact pattern (3+ body, quorum=N, no pairwise)
        // "Sun,Moon,Mars"  → pairwise pairs with correct per-pair event types
        //                    + general cluster detection with patternsQuorum
        bool isCommaDelimited = !commaPat.isEmpty();
        QString rawPat = isCommaDelimited ? commaPat : aspectPat;
        QChar   delim  = isCommaDelimited ? ',' : '=';

        auto tokens = rawPat.split(delim);

        // --- Helper: parse a raw token into body + suffix.
        // "Mars-t" → ("Mars", 't');  "OT" → ("OT", '\0')
        auto parseToken = [](const QString& tok)
            -> std::pair<QString, QChar> {
            // Don't split midpoint tokens (contain '/') on '-'
            if (tok.contains('/')) return { tok.trimmed(), QChar('\0') };
            auto parts = tok.split('-');
            QString body = parts.first().trimmed();
            QChar suf = (parts.size() > 1 && !parts[1].isEmpty())
                            ? parts[1].at(0).toLower() : QChar('\0');
            return { body, suf };
        };

        // Determine default mode for untagged planets.
        // If some tokens are explicitly tagged (via suffix or group token),
        // untagged ones get the "other" role.
        QChar defaultMode = 't';
        bool anyR = false, anyT = false, anyP = false;
        for (const auto& tok : std::as_const(tokens)) {
            auto [body, suf] = parseToken(tok);
            QChar effectiveMode = suf;
            if (effectiveMode == '\0') {
                // Check if it's a group token with an implicit mode
                auto [grpPlanets, grpMode] = resolveGroup(body);
                if (!grpPlanets.isEmpty()) effectiveMode = grpMode;
            }
            if (effectiveMode == 'r') anyR = true;
            else if (effectiveMode == 't') anyT = true;
            else if (effectiveMode == 'p') anyP = true;
        }
        if (anyT && !anyR && !anyP) defaultMode = 'r';

        // --- Resolve tokens into per-side index lists + per-index mode ---
        // Each delimited token becomes one "side" with one or more indices.
        QVector<QVector<unsigned>> sides;
        QMap<unsigned, QChar>      indexMode; // _alist index → mode char
        bool hasNatal = false, hasTransit = false, hasProgressed = false;
        bool hasOT = false, hasIP = false;  // track specific group tokens

        for (const auto& tok : std::as_const(tokens)) {
            // Skip zodiac position tokens (e.g. "15 Aries" appended by posa)
            auto zpm = zposRE().match(tok);
            if (zpm.hasMatch() && zpm.capturedLength() == tok.length()) {
                qDebug() << "[PATTERN] skipping zpos token:" << tok;
                continue;
            }

            auto [body, suf] = parseToken(tok);
            auto [grpPlanets, grpImplicitMode] = resolveGroup(body);

            QVector<unsigned> sideIndices;

            if (!grpPlanets.isEmpty()) {
                // --- Group token: expand to multiple planets ---
                QChar mode = (suf != '\0') ? suf : grpImplicitMode;
                qDebug() << "[PATTERN] group token:" << body << "→"
                         << grpPlanets.size() << "planets, mode:" << mode;

                // Track which specific group was used
                QString bu = body.trimmed().toUpper();
                if (bu == "OT") hasOT = true;
                if (bu == "IP") hasIP = true;

                for (PlanetId pid : grpPlanets) {
                    unsigned idx = unsigned(-1);
                    if (mode == 'r' && natal)
                        idx = getNatalPlanet(pid);
                    else if (mode == 'p' && natal)
                        idx = getProgressedPlanet(pid);
                    else if (trans)
                        idx = getTransitPlanet(pid);
                    if (idx != unsigned(-1)) {
                        sideIndices << idx;
                        indexMode[idx] = mode;
                    }
                }
                if (mode == 'r') hasNatal = true;
                else if (mode == 'p') hasProgressed = true;
                else hasTransit = true;
            } else {
                // --- Single planet (or midpoint) token ---
                auto [idx, mode] = resolvePlanet(tok, defaultMode);
                qDebug() << "[PATTERN] token:" << tok << "→ idx:" << idx
                         << "mode:" << mode << "natal:" << natal << "trans:" << trans;
                if (idx != unsigned(-1)) {
                    sideIndices << idx;
                    indexMode[idx] = mode;
                    if (mode == 'r') hasNatal = true;
                    else if (mode == 'p') hasProgressed = true;
                    else hasTransit = true;
                }
            }

            if (!sideIndices.isEmpty())
                sides << sideIndices;
        }

        // Flatten all sides for total index count
        QVector<unsigned> allIndices;
        for (const auto& s : sides)
            allIndices << s;

        // --- Helper: determine per-pair event type from two body modes ---
        auto pairEventType = [&](unsigned a, unsigned b) -> EventType {
            QChar mA = indexMode.value(a, 't');
            QChar mB = indexMode.value(b, 't');
            // Normalize so that the "faster" mode is first
            // Order: t > p > r
            if (mA == 'r' && mB != 'r') std::swap(mA, mB);
            if (mA == 'p' && mB == 't') std::swap(mA, mB);
            // Now mA is the "faster" body
            if (mA == 't' && mB == 't') return etcTransitToTransit;
            if (mA == 't' && mB == 'r') return etcTransitToNatal;
            if (mA == 't' && mB == 'p') return etcTransitToNatal; // T=P uses T=N search
            if (mA == 'p' && mB == 'r') return etcProgressedToNatal;
            if (mA == 'p' && mB == 'p') return etcProgressedToProgressed;
            if (mA == 'r' && mB == 'r') return etcTransitToNatal; // fallback
            return etcTransitToTransit;
        };

        // --- Helper: determine a bulk event type for the whole group ---
        auto bulkEventType = [&]() -> EventType {
            bool hasSlowComponent = hasNatal || hasProgressed;
            if (hasOT && hasNatal)            return etcOuterTransitToNatal;
            if (hasIP && hasNatal)            return etcInnerProgressedToNatal;
            if (hasProgressed && hasNatal)    return etcProgressedToNatal;
            if (hasProgressed && hasTransit)  return etcTransitToNatal;
            if (hasProgressed)                return etcProgressedToProgressed;
            if (hasSlowComponent && hasTransit) return etcTransitToNatal;
            if (hasSlowComponent)             return etcTransitToNatal;
            return etcTransitToTransit;
        };

        // --- Build staff pairs with deduplication ---
        // Uses the shared `seen` set of normalized (min,max) pairs to avoid
        // duplicates across sub-patterns.
        auto addPair = [&](unsigned a, unsigned b, EventType et) {
            if (a == b) return;                 // skip self-pairs
            // Skip NNode↔SNode pairs: they are always in exact
            // opposition and will never leave aspect, causing
            // findPriorStarts to loop back to the epoch.
            auto idA = static_cast<PlanetLoc*>(_alist[a])->planet.planetId();
            auto idB = static_cast<PlanetLoc*>(_alist[b])->planet.planetId();
            if ((idA == Planet_NorthNode && idB == Planet_SouthNode)
                || (idA == Planet_SouthNode && idB == Planet_NorthNode))
                return;
            auto key = qMakePair(qMin(a, b), qMax(a, b));
            if (seen.contains(key)) return;     // skip duplicates
            seen.insert(key);
            _staff.emplace_back(a, b, useHset, et);
        };

        if (isCommaDelimited) {
            // ====== COMMA syntax: A,B,C ======
            // Generate all pairwise staff entries with per-pair event types.
            // Also enable general cluster detection so findClusters() runs
            // with the user's patternsQuorum setting.

            qDebug() << "[PATTERN] comma-delimited:" << rawPat
                     << "→" << allIndices.size() << "bodies,"
                     << sides.size() << "sides";

            // Enable per-pair event types
            if (sides.size() == 2) {
                // Cross-product between two sides
                for (unsigned a : sides[0]) {
                    for (unsigned b : sides[1]) {
                        EventType et = pairEventType(a, b);
                        enable(et);
                        addPair(a, b, et);
                    }
                }
            } else {
                // Triangular all-pairs among all indices
                for (int i = 0; i < allIndices.size(); ++i) {
                    for (int j = i + 1; j < allIndices.size(); ++j) {
                        EventType et = pairEventType(allIndices[i], allIndices[j]);
                        enable(et);
                        addPair(allIndices[i], allIndices[j], et);
                    }
                }
            }

            // Enable general cluster detection (uses patternsQuorum)
            if (allIndices.size() >= patternsQuorum) {
                bool hasSlowComponent = hasNatal || hasProgressed;
                if (hasSlowComponent) {
                    enable(etcTransitNatalAspectPattern);
                } else {
                    enable(etcTransitAspectPattern);
                }
                _generalClustersEnabled = true;
            }

        } else {
            // ====== EQUALS syntax: A=B or A=B=C ======
            bool isTwoBody = (allIndices.size() <= 2 || sides.size() <= 2);

            if (isTwoBody) {
                // --- Two-body pairwise: unchanged behavior ---
                EventType etype = bulkEventType();
                enable(etype);

                if (sides.size() == 2) {
                    for (unsigned a : sides[0])
                        for (unsigned b : sides[1])
                            addPair(a, b, etype);
                } else {
                    for (int i = 0; i < allIndices.size(); ++i)
                        for (int j = i + 1; j < allIndices.size(); ++j)
                            addPair(allIndices[i], allIndices[j], etype);
                }
            } else {
                // --- Three+-body exact pattern ---
                // Register as an exact pattern spec (quorum = N).
                // No pairwise staff entries are generated.
                bool hasSlowComponent = hasNatal || hasProgressed;
                EventType etype = hasSlowComponent
                                      ? etcTransitNatalAspectPattern
                                      : etcTransitAspectPattern;
                enable(etype);

                // Check if any side is a group (multiple indices from a
                // group token like OT, IP, T, P, N, NA).  If so, distribute:
                // create one exact pattern per group-member combined with
                // all singleton sides.  E.g. "OT=Sun/Nep=Chi/Sat" becomes
                // five 3-body patterns: Jup=Sun/Nep=Chi/Sat, etc.
                QVector<int> groupSideIdx, singletonSideIdx;
                for (int si = 0; si < sides.size(); ++si) {
                    if (sides[si].size() > 1)
                        groupSideIdx << si;
                    else
                        singletonSideIdx << si;
                }

                // Helper: register one ExactPatternSpec from a set of indices
                auto registerExactPattern = [&](const std::vector<unsigned>& indices) {
                    ExactPatternSpec spec;
                    spec.alistIndices = indices;
                    spec.hsid         = useHset;
                    spec.et           = etype;
                    for (unsigned idx : spec.alistIndices) {
                        if (auto ploc = dynamic_cast<PlanetLoc*>(_alist[idx])) {
                            spec.bodies.emplace(ploc->planetModeId());
                            if (ploc->planet.isMidpt())
                                spec.hasMidpoints = true;
                        }
                    }
                    _exactPatterns.push_back(std::move(spec));
                };

                if (!groupSideIdx.isEmpty()) {
                    // Distribute group sides: cross-product of all group
                    // sides, combined with all singleton side indices.
                    QVector<unsigned> singletons;
                    for (int si : singletonSideIdx)
                        singletons << sides[si][0];

                    // Build cross-product of group sides iteratively
                    QVector<QVector<unsigned>> combos;
                    combos << QVector<unsigned> {};
                    for (int gi : groupSideIdx) {
                        QVector<QVector<unsigned>> expanded;
                        for (const auto& combo : std::as_const(combos)) {
                            for (unsigned idx : sides[gi]) {
                                auto ext = combo;
                                ext << idx;
                                expanded << ext;
                            }
                        }
                        combos = std::move(expanded);
                    }

                    for (const auto& combo : std::as_const(combos)) {
                        std::vector<unsigned> indices;
                        for (unsigned idx : combo) indices.push_back(idx);
                        for (unsigned idx : singletons) indices.push_back(idx);
                        // Deduplicate
                        std::sort(indices.begin(), indices.end());
                        indices.erase(std::unique(indices.begin(),
                                                  indices.end()),
                                      indices.end());
                        if (indices.size() < 2) continue;
                        registerExactPattern(indices);
                    }

                    qDebug() << "[PATTERN] distributed group pattern:"
                             << rawPat << "→" << combos.size()
                             << "exact patterns of quorum"
                             << (singletons.size() + 1) << ", etype:" << etype;
                } else {
                    // No group sides — single exact pattern (original path)
                    std::vector<unsigned> indices;
                    for (unsigned idx : allIndices) {
                        if (std::find(indices.begin(), indices.end(), idx) == indices.end())
                            indices.push_back(idx);
                    }
                    registerExactPattern(indices);

                    qDebug() << "[PATTERN] exact pattern registered:"
                             << rawPat << "→" << _exactPatterns.back().quorum()
                             << "bodies, etype:" << etype;
                }
            }
        }
    }

    } // end for each sub-pattern

    qDebug() << "[PATTERN] _alist:" << _alist.size()
             << "_staff:" << _staff.size()
             << "_exactPatterns:" << _exactPatterns.size()
             << "events:" << enabledEvents.size();
    for (const auto& pe : _staff)
        qDebug() << "[PATTERN]" << formatPlanetsEtc(pe, _alist, _hsets).c_str();
    for (size_t i = 0; i < _exactPatterns.size(); ++i) {
        const auto& ep = _exactPatterns[i];
        QStringList names;
        for (unsigned idx : ep.alistIndices) {
            if (idx < _alist.size())
                names << _alist[idx]->description();
        }
        qDebug() << "[PATTERN] exact #" << i << ":" << names.join("=")
                 << "quorum:" << ep.quorum() << "etype:" << ep.et;
    }
}

namespace
{
static thread_local bool s_inited = false;
}

void
AspectFinder::prepThread()
{
#if MSDOS
    char ephePath[] = "swe\\";
#else
    char ephePath[] = "swe/";
#endif
    if (!s_inited) {
        s_inited = true;
        swe_set_ephe_path(ephePath);
    }
}

void
AspectFinder::releaseThread()
{
    //    swe_close();
}

class TaskTracker {
    AspectFinder* f;

  public:
    TaskTracker(AspectFinder* f) : f(f) { f->startTask(); }
    ~TaskTracker() { f->endTask(); }
};

// Shadow transit timing heuristics for calculating search windows
struct ShadowTransitWindow {
    std::pair<double, double> timing;  // {retrogradePeriod, arcCoverageTime}
    
    // Get planet-specific shadow transit timing parameters
    ShadowTransitWindow(PlanetId pid)
    {
        switch (pid) {
        case Planet_Mercury: timing = { 21.0, 24.0 }; return;
        case Planet_Venus:   timing = { 40.0, 50.0 }; return;
        case Planet_Mars:    timing = { 80.0, 70.0 }; return;
        case Planet_Jupiter: timing = { 120.0, 60.0 }; return;
        case Planet_Saturn:  timing = { 139.0, 70.0 }; return;
        case Planet_Uranus:  timing = { 151.0, 80.0 }; return;
        case Planet_Neptune: timing = { 158.0, 90.0 }; return;
        case Planet_Pluto:   timing = { 165.0, 100.0 }; return;
        case Planet_Chiron:  timing = { 145.0, 75.0 }; return;
        default:
            timing = { 100.0, 60.0 };
            return; // Default for asteroids/other
        }
    }

    JDateRange getSearchWindow(double jd, bool direct) const
    {
        if (direct) {
            return { jd - timing.first - timing.second * 1.75,
                     jd - timing.first - timing.second * 0.5 };
        } else {
            return { jd + timing.first + timing.second * 0.5,
                     jd + timing.first + timing.second * 1.75 };
        }
    }
};

class PairAspectFinder : public EventFinderTask {
    Loc*            _loc1;
    Loc*            _loc2;
    unsigned        _h;
    double          _jdStart, _jdEnd;  // Julian date bracket [start, end]
    qreal           _deltaStart, _deltaEnd;  // Angular delta at start/end
    qreal           _speed1, _speed2;  // Planet speeds at bracket endpoints
    QDateTime       _d;
    std::string     _which;
    EventType       _et;
    bool            _beQuiet;
    AspectFinder*   _finder;
    HarmonicEvents& _evs;
    bool            _useBZS;
    JDateRange      _useRange;
    bool            _ran = false;

  public:
    // Full constructor with precomputed deltas and speeds
    // - jdStart/jdEnd: Julian date bracket defining the search interval
    // - deltaStart/deltaEnd: Angular delta between planets at bracket endpoints
    // - speed1/speed2: Planet speeds at the bracket endpoints
    PairAspectFinder(Loc*             loc1,
                     Loc*             loc2,
                     unsigned         h,
                     double           jdStart,
                     double           jdEnd,
                     qreal            deltaStart,
                     qreal            deltaEnd,
                     qreal            speed1,
                     qreal            speed2,
                     const QDateTime& d,
                     std::string      which,
                     EventType        et,
                     bool             quiet,
                     AspectFinder*    finder) :
        _loc1(loc1->clone()),
        _loc2(loc2->clone()),
        _h(h),
        _jdStart(jdStart),
        _jdEnd(jdEnd),
        _deltaStart(deltaStart),
        _deltaEnd(deltaEnd),
        _speed1(speed1),
        _speed2(speed2),
        _d(d),
        _which(std::move(which)),
        _et(et),
        _beQuiet(quiet),
        _finder(finder),
        _evs(_finder->_evs)
    {
        _useBZS = (loc1->inMotion() && speed1 < .00001)
                  || (loc2->inMotion() && speed2 < .00001);
    }
    
    // Simplified constructor that computes deltas and speeds from positions
    PairAspectFinder(Loc*             loc1,
                     Loc*             loc2,
                     unsigned         h,
                     double           jdStart,
                     double           jdEnd,
                     const QDateTime& d,
                     std::string      which,
                     EventType        et,
                     bool             quiet,
                     AspectFinder*    finder) :
        _loc1(loc1->clone()),
        _loc2(loc2->clone()),
        _h(h),
        _jdStart(jdStart),
        _jdEnd(jdEnd),
        _d(d),
        _which(std::move(which)),
        _et(et),
        _beQuiet(quiet),
        _finder(finder),
        _evs(_finder->_evs)
    {
        PlanetProfile poses { _loc1->clone(), _loc2->clone() };

        double speedA, speedB;
        poses.computePos(jdStart, h);
        std::tie(_deltaStart, speedA) =
            PlanetProfile::computeDelta(poses[0], poses[1], h);
        _speed1 = qAbs(poses[0]->speed / 2. + poses[1]->speed / 2.);

        poses.computePos(jdEnd, h); // compute end pos/speed
        std::tie(_deltaEnd, speedB) =
            PlanetProfile::computeDelta(poses[0], poses[1], h);
        _speed2 = qAbs(poses[0]->speed / 2. + poses[1]->speed / 2.);

        _useBZS = (_loc1->inMotion() && _speed1 < .00001)
                  || (_loc2->inMotion() && _speed2 < .00001);
    }

    ~PairAspectFinder()
    {
        if (!_ran) {
            delete _loc1;
            delete _loc2;
        }
    }

    EventType eventType() const override { return _et; }

    void setInOrbRange(const JDateRange& r) override { _useRange = r; }

    void run() override
    {
        TaskTracker tr(_finder);

        modalize<bool> mum(st_quiet, _beQuiet);
        PlanetProfile  poses { _loc1, _loc2 };
        _ran = true; // otherwise we'll clean up stuff we don't want cleaned

        double    tjd {};
        uintmax_t iter;
        bool      bzhs      = false;
        bool      done      = false;
        bool      cancelled = false;
        if (!_useBZS) {
            try {
                auto cps = [this,
                            &poses](double jd) -> std::pair<qreal, qreal> {
                    if (_finder->_state == AspectFinder::cancelRequestedState)
                        throw int(1);
                    auto                    pos = poses.computePos(jd, _h);
                    std::pair<qreal, qreal> ret { pos, poses.speed() };
                    if (!_beQuiet) {
                        QDateTime dt(dateTimeFromJulian(jd));
                        qDebug() << "nri " << (_which + ":").c_str()
                                 << dtToString(dt) << "ret:" << ret;
                    }
                    return ret;
                };
                iter = 50;
                static constexpr int digits =
                    std::numeric_limits<double>::digits;

                double guess;
                if (_loc1 && _loc1->inMotion() && _loc2 && _loc2->inMotion()) {
                    guess = _jdStart + (fabs(_deltaStart) / (fabs(_deltaStart) + fabs(_deltaEnd)));
                } else {
                    guess = _jdStart + .5;
                }
                try {
                    tjd = newton_raphson_iterate(cps,
                                                 guess,
                                                 _jdStart,
                                                 _jdEnd,
                                                 digits,
                                                 iter);
                }
                catch (int) {
                    return;
                }

                auto psp = PlanetProfile::computeDelta(poses[0], poses[1], _h);
                if (std::abs(psp.first) <= calcLoop::tol) {
                    done = true;
                } else {
                    done = false;
                }
            }
            catch (...) {
                cancelled = false;
            }
            if (!done && !_beQuiet) {
                qDebug() << QString(cancelled ? "Cancelled" : "Failed")
                         << _d.date().toString() << _which.c_str() << "after"
                         << iter << "iteration(s) newton_raphson";
                qDebug() << "speed" << _speed1 << _speed2 << "respectively";
            }
        }
        if (!done && !cancelled) {
            bzhs           = true;
            unsigned count = 0;
            auto     cp    = [this, &poses, &count](double jd) {
                if (_finder->_state == AspectFinder::cancelRequestedState)
                    throw int(1);
                ++count;
                auto pos = poses.computePos(jd, _h);
                if (!_beQuiet) {
                    QDateTime dt(dateTimeFromJulian(jd));
                    qDebug() << "bzhs " << (_which + ":").c_str()
                             << dtToString(dt) << "ret:" << pos;
                }
                return pos;
            };
            try {
                done = brentZhangStage(cp, _jdStart, _jdEnd, _deltaStart, _deltaEnd, tjd);
            }
            catch (int) {
                return;
            }

            iter     = count;
            auto psp = PlanetProfile::computeDelta(poses[0], poses[1], _h);
            if (std::abs(psp.first) <= calcLoop::tol) {
                done = true;
            } else {
                done = false;
            }
        }
        if (!done /*&& !_beQuiet*/) {
            qDebug() << "Failed" << _d.date().toString() << _which.c_str()
                     << "after" << iter << "iteration(s) brentStageZhang";
        } else {
            // Pack up event
            auto               qdt   = dateTimeFromJulian(tjd);
            auto               p1loc = dynamic_cast<PlanetLoc*>(poses[0]);
            auto               p2loc = dynamic_cast<PlanetLoc*>(poses[1]);
            PlanetRangeBySpeed plr { *p1loc, *p2loc };

            auto         ch = static_cast<unsigned char>(_h);
            QMutexLocker ml(_evs.mutex());
            auto&        ev = _evs.emplace_back(qdt, _et, ch, std::move(plr));

            if (_useRange != JDateRange()) {
                ev.setRange({ dateTimeFromJulian(_useRange.first),
                              dateTimeFromJulian(_useRange.second) });
            }

            if (!_beQuiet)
                qDebug() << dtToString(qdt).toLocal8Bit().constData()
                         << _which.c_str() << "with" << iter << "iteration(s)"
                         << (bzhs ? "brentZhangStage" : "newton_raphson");
        }
    }
};

void
AspectFinder::findStations()
{
    const auto& start = _range.first;
    const auto& end   = _range.second.addDays(1);
    auto        d     = start.startOfDay().toUTC();
    auto        e     = end.startOfDay().toUTC();

    modalize<bool> mum(st_quiet, true);

    double jd = getJulianDate(d);
    for (auto trans : _alist) (*trans)(jd, 1); // the horror

    PlanetProfile b = _alist;

    auto useRate = 15; // search every 15 days
    if (!st_quiet) qDebug() << "sta" << dtToString(d);

    double                pjd   = jd;
    int                   ndays = int(useRate);
    int                   nsecs = (useRate - double(ndays)) * 24. * 60. * 60.;
    auto                  nd    = d.addDays(ndays).addSecs(nsecs);
    std::list<PlanetLoc*> stations;
    unsigned              in = _alist.size();
    while (d < e) {
        QCoreApplication::processEvents();

        if (_state == cancelRequestedState) break;
        if (_state == pauseRequestedState) {
            QThread::usleep(100000);
            continue;
        }

        jd = getJulianDate(nd);
        if (!st_quiet) qDebug() << "sta" << dtToString(nd);

        // compute new positions
        for (auto pos : b) (*pos)(jd, 1);

        std::set<unsigned> stationChecked;
        for (unsigned i = 0; i < in; ++i) {
            if (stationChecked.count(i) != 0) continue;
            stationChecked.insert(i);

            auto pl = dynamic_cast<TransitPosition*>(_alist[i]);
            if (!pl) continue;

            auto pid = pl->planet.planetId();
            if (pid <= Planet_Moon || pid == Planet_NorthNode
                || pid == Planet_SouthNode || pid >= Planets_End)
            {
                continue;
            }

            auto aspd = _alist[i]->speed;
            auto bspd = b[i]->speed;
            if (!st_quiet) {
                qDebug() << "  " << _alist[i]->description() << aspd << bspd;
            }
            if (sgn(aspd) == sgn(bspd)) continue;

            bool  wasRetro = aspd < 0;
            auto& ev       = _evs.safe_emplace_back();
#if 1
            _tp->start([=, &stations, &ev] {
                startTask();
                modalize<bool> mum(st_quiet, true);
#endif

                auto pj        = dynamic_cast<PlanetLoc*>(_alist[i]->clone());
                pj->desc       = QString("S") + (wasRetro ? 'D' : 'R');
                unsigned iters = 0;
                auto     cspd  = [&](double jd) {
                    (*pj)(jd, 1);
                    ++iters;
                    return pj->speed;
                };

                double tjd = jd / 2. + pjd / 2.;
                if (brentZhangStage(cspd, pjd, jd, aspd, bspd, tjd)) {
                    auto qdt  = dateTimeFromJulian(tjd);
                    auto ploc = dynamic_cast<PlanetLoc*>(pj);

                    auto               dt = dtToString(qdt);
                    PlanetRangeBySpeed plr { *ploc };

                    ev = HarmonicEvent(qdt, etcStation, 1, std::move(plr));

                    qDebug() << dt << pj->description() << "found in" << iters
                             << "iterations(s)";

                    if (includeShadowTransits) {
                        // Add shadow-period transit lookup
                        auto kp = new KnownPosition(ploc, tjd,
                                                    wasRetro ? "IN" : "EX");
                        kp->planet.setFileId(-1); // hides position in events table
                        kp->allowAspects = PlanetLoc::aspOnlyDirect;
                        kp->speed        = 0;

                        if (false) { // the old way started here
                            QMutexLocker mlb(&_ctm);
                            stations.emplace_back(kp);
                        }

                        // Create shadow transit search with time-bounded window
                        auto pid = ploc->planet.planetId();
                        ShadowTransitWindow window(pid);
                        
                        // Calculate search window that excludes station time
                        double stationJd = tjd;
                        JDateRange searchWindow = window.getSearchWindow(stationJd, wasRetro);
                        
                        qDebug() << "Starting shadow transit search for" 
                                 << _alist[i]->description()
                                 << (wasRetro ? "ENTRY" : "EXIT")
                                 << "window:" << dtToString(dateTimeFromJulian(searchWindow.first))
                                 << "to" << dtToString(dateTimeFromJulian(searchWindow.second));
                        
                        // Use simplified constructor that computes deltas/speeds internally
                        auto* task = new PairAspectFinder(
                            _alist[i],
                            kp,
                            1, // harmonic (conjunction)
                            searchWindow.first,
                            searchWindow.second,
                            dateTimeFromJulian(searchWindow.first),
                            pj->description().toStdString()
                                + (wasRetro ? " IN Shadow"
                                            : " EX Shadow"),
                            etcTransitToStation,
                            st_quiet, // quiet mode
                            this);

                        task->run();
                        delete task;
                    }
                } else {
                    qDebug() << "Couldn't find station for"
                             << _alist[i]->description() << "!";
                }
#if 1
                endTask();
            });
#endif
        }

        d  = nd;
        nd = d.addDays(ndays).addSecs(nsecs);
        // nd = d.addDays(_rate);
        pjd = jd;

        _alist.swap(b);
    }

    bool cleared = false;
    if (_state == cancelRequestedState) {
        _tp->clear();
        cleared = true;
    }

    int active(_numTasks);
    qDebug() << active << "activity/ies";
    while (!_tp->waitForDone(100)) {
        QCoreApplication::processEvents();
        if (!cleared && _state == cancelRequestedState) {
            _tp->clear();
            cleared = true;
        }
        // Emit progress so the UI can sort & display station events found so far
        emit progress(-3.0);  // negative signals "waiting for stations" phase
        int now(_numTasks);
        if (now != active) {
            qDebug() << now << "activity/ies";
            active = now;
        }
    }

    qDebug() << "Done with finding stations";

    if (_state == cancelRequestedState) return;

    for (PlanetLoc* pj : stations) {
        auto* kp = dynamic_cast<KnownPosition*>(pj);
        if (!kp) continue;
        auto jd = kp->julianDate();
        int  i  = pj->planet.fileId();
        int  j  = int(_alist.size());
        _alist.push_back(pj);
        pj->planet.setFileId(-1); // hides it from clusterer
        _staff.emplace_back(i, j, 0, etcTransitToStation);
        qDebug() << "Added transit search for" << i << j
                 << QString("H1 %1=%2")
                        .arg(_alist[i]->description())
                        .arg(_alist[j]->description());
    }
}

// ---------------------------------------------------------------------------
// Paranatellonta finder
// ---------------------------------------------------------------------------
// Daily at UTC midnight, for each transit body call swe_rise_trans to get the
// 24-hour Asc/Desc/MC/IC times; for each natal body, ex-precess RA/Dec to the
// day and apply the mundane formula at the natal location to derive transit
// times. Cluster events whose times are within paranOrb*240 sec; track each
// cluster's persistence across days; emit HarmonicEvent ranges. aspectMode is
// modalized to amcEquatorial during detection so NatalExprecessedPosition
// behaves uniformly regardless of the user's globally-selected mode.
//
// Angle index convention (matches Star::angleTransitMode):
//   0 = Asc, 1 = Desc, 2 = MC, 3 = IC

namespace {

// Almagest-font codepoints used by `desc` so the existing glyphic renderer
// (details/src/transits.cpp glyph()) draws angle markers without needing a
// new translation table. The font already maps these codepoints to the
// Asc/Desc/MC/IC glyphs (see astro-data.cpp:252-261).
QString
paranAngleDesc(int angle)
{
    switch (angle) {
    case 0:  return QString(QChar(402));   // Asc (ƒ)
    case 1:  return QString(QChar(8249));  // Desc (‹)
    case 2:  return QStringLiteral("M");   // MC
    case 3:  return QString(QChar(8225));  // IC (‡)
    default: return QString();
    }
}

struct ParanEntry {
    int       aIdx;     ///< index into _alist
    int       angle;    ///< 0=Asc 1=Desc 2=MC 3=IC
    qint64    secOfDay; ///< seconds since UTC midnight of day d
    QDateTime when;     ///< absolute datetime of the transit
    bool      isNatal;
};

struct ParanState {
    QDateTime    startDate;       ///< first day the paran was active (UTC midnight)
    QDateTime    endDate;         ///< last day the paran was active (UTC midnight)
    QDateTime    firstActiveDT;   ///< cluster-mean datetime on the first active day
    QDateTime    lastActiveDT;    ///< cluster-mean datetime on the last active day
    QDateTime    peakDateTime;    ///< calendar moment on the tightest day
    double       peakJd;          ///< JD at peak (used to recompute display state)
    qint64       tightestSpread;  ///< smallest seen seconds-spread of the cluster
    EventType    type;
    QVector<int> indices;         ///< _alist indices, sorted (aIdx, angle)
    QVector<int> angles;          ///< parallel to indices
};

// Stable key identifying the same paran across days. Built from the cluster's
// (aIdx, angle) pairs (sorted) plus the event type.
QString
makeParanKey(EventType type, const QVector<int>& sortedAIdx,
             const QVector<int>& sortedAngles)
{
    QStringList parts;
    parts.reserve(sortedAIdx.size() + 1);
    parts << QString::number(int(type));
    for (int i = 0; i < sortedAIdx.size(); ++i) {
        parts << QStringLiteral("%1:%2").arg(sortedAIdx[i]).arg(sortedAngles[i]);
    }
    return parts.join(QLatin1Char('|'));
}

// Circular mean of cluster times-of-day, returned as seconds since UTC
// midnight. Handles wraparound so a cluster straddling 23:50/00:10 yields
// 00:00 rather than 12:00. Equivalent (up to the 360°/86400s scale) to
// taking the circular mean of the cluster's RA values.
qint64
circularMeanSeconds(const QVector<int>& clusterEntries,
                    const QVector<ParanEntry>& entries)
{
    double sumX = 0.0, sumY = 0.0;
    for (int k : clusterEntries) {
        double a = (double(entries[k].secOfDay) / 86400.0) * 2.0 * M_PI;
        sumX += std::cos(a);
        sumY += std::sin(a);
    }
    double mean = std::atan2(sumY, sumX);
    if (mean < 0) mean += 2.0 * M_PI;
    return qint64((mean / (2.0 * M_PI)) * 86400.0);
}

} // namespace

// Body position provider: given a JD, populates ra, dec (degrees) and
// dRAdt, dDecdt (degrees per day). Returns true on success.
using BodyAtFn = std::function<bool(double jd,
                                    double& ra,    double& dec,
                                    double& dRAdt, double& dDecdt)>;

// Find the JD within [d_jd, d_jd + 1) at which the body crosses angle m
// (0=Asc, 1=Desc, 2=MC, 3=IC) at the given location. Returns false on
// circumpolar (Asc/Desc only), non-convergence, or out-of-window result.
static bool
findAngleTransitJD(const BodyAtFn& bodyAt,
                   double d_jd,
                   double latitude,   // degrees
                   double longitudeE, // degrees
                   int    angleIdx,   // 0..3
                   double& t_out)
{
    static constexpr double kSiderealRate = 360.98564736629; // degrees/day

    double ra0, dec0, dRA0, dDec0;
    if (!bodyAt(d_jd, ra0, dec0, dRA0, dDec0))
        return false;

    // Circumpolar check for Asc/Desc
    if (angleIdx == 0 || angleIdx == 1) {
        const double u0 = tand(dec0) * tand(latitude);
        if (std::abs(u0) >= 1.0)
            return false;
    }

    // Compute target LST (degrees) at which the body sits on angle m.
    //
    // Derivation: hour angle H = LST - RA; for a body at declination Dec
    // and observer latitude φ, the diurnal-arc horizon condition is
    // cos(H) = -tan(φ)·tan(Dec), giving H_rise = -(90° + AD) and
    // H_set = +(90° + AD), where AD = asin(tan(φ)·tan(Dec)). Hence:
    //   LST_Asc  = RA − AD − 90°
    //   LST_Desc = RA + AD + 90°
    //   LST_MC   = RA
    //   LST_IC   = RA + 180°
    // The existing calculatePlanet path at astro-calc.cpp:1809 encodes the
    // same relation as RAMC + (OA - OAAC), which simplifies to OA - 90°
    // since OAAC = RAMC + 90° always.
    auto computeTarget = [&](double ra, double dec) -> double {
        switch (angleIdx) {
        case 0: { // Asc
            const double AD = asind(tand(dec) * tand(latitude));
            return swe_degnorm(ra - AD - 90.0);
        }
        case 1: { // Desc
            const double AD = asind(tand(dec) * tand(latitude));
            return swe_degnorm(ra + AD + 90.0);
        }
        case 2: // MC
            return ra;
        case 3: // IC
            return swe_degnorm(ra + 180.0);
        default:
            return ra;
        }
    };

    const double target0 = computeTarget(ra0, dec0);
    const double LST0    = swe_degnorm(swe_sidtime(d_jd) * 15.0 + longitudeE);

    double t = d_jd + swe_difdeg2n(target0, LST0) / kSiderealRate;
    if (t < d_jd)       t += 1.0;
    if (t >= d_jd + 1.0) t -= 1.0;

    bool converged = false;
    for (int iter = 0; iter < 6; ++iter) {
        double ra, dec, dRAdt, dDecdt;
        if (!bodyAt(t, ra, dec, dRAdt, dDecdt))
            return false;

        // Circumpolar check mid-iteration for Asc/Desc
        if (angleIdx == 0 || angleIdx == 1) {
            const double u = tand(dec) * tand(latitude);
            if (std::abs(u) >= 1.0)
                return false;
        }

        const double target = computeTarget(ra, dec);
        const double LSTt   = swe_degnorm(swe_sidtime(t) * 15.0 + longitudeE);
        const double f      = swe_difdeg2n(LSTt, target);

        if (std::abs(f) < 1e-4) {
            converged = true;
            break;
        }

        // Derivative of target w.r.t. t
        double dtarget_dt = dRAdt;
        if (angleIdx == 0 || angleIdx == 1) {
            const double u    = tand(dec) * tand(latitude);
            const double cosd_dec = std::cos(dec * M_PI / 180.0);
            const double sec2_dec = 1.0 / (cosd_dec * cosd_dec);
            const double dAD_dt   = (sec2_dec * tand(latitude) / std::sqrt(1.0 - u * u)) * dDecdt;
            dtarget_dt = (angleIdx == 0) ? dRAdt - dAD_dt : dRAdt + dAD_dt;
        }

        const double f_prime = kSiderealRate - dtarget_dt;
        if (std::abs(f_prime) < 1e-10)
            break;
        t -= f / f_prime;
    }

    if (!converged) {
        qDebug() << "findAngleTransitJD: Newton-Raphson did not converge for angleIdx"
                 << angleIdx << "d_jd" << d_jd;
        return false;
    }

    if (t < d_jd || t >= d_jd + 1.0)
        return false;

    t_out = t;
    return true;
}

bool
natalTropicalEquatorialPos(PlanetId pid, double jdNatal,
                           double& ra_out, double& dec_out)
{
    const Planet& pDef = getPlanet(pid);
    if (pDef.sweNum < 0) return false;

    uint flags = (SEFLG_SWIEPH | pDef.sweFlags | SEFLG_EQUATORIAL | SEFLG_SPEED)
                 & ~SEFLG_TRUEPOS & ~SEFLG_SIDEREAL;
    double xx[6];
    char   err[256] = "";
    if (swe_calc_ut(jdNatal, pDef.sweNum, flags, xx, err) < 0)
        return false;

    ra_out  = xx[0];
    dec_out = xx[1];
    if (pid == Planet_SouthNode) {
        ra_out  = swe_degnorm(ra_out + 180.0);
        dec_out = -dec_out;
    }
    return true;
}

bool
computeNatalParanTransits(double natalRA,
                              double natalDec,
                              double jdNatal,
                              double d_jd,
                              double latitude,
                              double longitudeE,
                              QDateTime angleTransit_out[4],
                              double    angleTransitRA_out[4])
{
    for (int m = 0; m < 4; ++m) {
        angleTransit_out[m]  = QDateTime();
        angleTransitRA_out[m] = 0.0;
    }

    // Use the 4-param overload so ecliptic coords are derived from the tropical
    // RA/Dec internally — avoids passing sidereal eclipticPos in sidereal mode.
    BodyAtFn bodyAt = [natalRA, natalDec, jdNatal](
        double tjd, double& ra, double& dec, double& dRAdt, double& dDecdt) -> bool
    {
        auto ep = exprecess_equatorial(natalRA, natalDec, jdNatal, tjd);
        ra     = ep.ra;
        dec    = ep.dec;
        dRAdt  = ep.raSpeed;
        dDecdt = ep.decSpeed;
        return true;
    };

    bool any = false;
    for (int m = 0; m < 4; ++m) {
        double tjd;
        if (!findAngleTransitJD(bodyAt, d_jd, latitude, longitudeE, m, tjd))
            continue;
        angleTransit_out[m] = dateTimeFromJulian(tjd);
        double ra, dec, dRA, dDec;
        if (bodyAt(tjd, ra, dec, dRA, dDec))
            angleTransitRA_out[m] = ra;
        any = true;
    }
    return any;
}

namespace
{

// Polynomial-LST form for a body at an angle:  LST = raOffset + signAD · AD(lat)
// where AD(lat) = asin(tan(dec)·tan(lat)).  signAD is 0 for MC/IC.
struct AngleLST {
    double raOffset; // degrees, 0..360
    int    signAD;   // -1 / 0 / +1
};

static AngleLST
makeAngleLST(double ra, int angle)
{
    switch (angle) {
    case 0: return { swe_degnorm(ra - 90.0),  -1 }; // Asc:  LST = RA − AD − 90°
    case 1: return { swe_degnorm(ra + 90.0),  +1 }; // Desc: LST = RA + AD + 90°
    case 2: return { swe_degnorm(ra),          0 }; // MC:   LST = RA
    case 3: return { swe_degnorm(ra + 180.0),  0 }; // IC:   LST = RA + 180°
    }
    return { 0.0, 0 };
}

// Reduce signed degree difference to (−180°, 180°].
static double
wrapDelta(double d)
{
    while (d  >  180.0) d -= 360.0;
    while (d <= -180.0) d += 360.0;
    return d;
}

// Solve for latitude φ (degrees) s.t.
//   sA·AD_A(φ) − sB·AD_B(φ) ≡ D  (mod 360°),
// with AD_p(φ) = asin(tan(dec_p)·tan(φ)).  Limited to (−66.5°, 66.5°).
// Returns false when no real solution exists (circumpolar, near-degenerate
// declinations, or no root in the feasible range).
static bool
solveParanLatitude(double decA, double decB,
                   int sA, int sB, double D,
                   double& latOut)
{
    D = wrapDelta(D);

    if (sA == 0 && sB == 0)
        return false; // pure MC/IC: latitude-independent

    if (sA == 0 || sB == 0) {
        // One side latitude-independent. Solve the other directly.
        const int    sNz   = (sA != 0) ? sA   : -sB; // sign on the AD term
        const double decNz = (sA != 0) ? decA :  decB;
        const double tanDec = tand(decNz);
        if (std::abs(tanDec) < 1e-9) return false; // dec ~ 0: sin(AD)/tan(dec) blows up

        const double targetAD = D / sNz; // degrees, ought to be in [−90,90]
        if (std::abs(targetAD) > 90.0) return false;
        const double tanLat = sind(targetAD) / tanDec;
        const double lat    = atand(tanLat);
        if (std::abs(lat) > 66.5) return false;
        latOut = lat;
        return true;
    }

    // Both bodies on Asc/Desc — Newton's method in φ.
    double lat = 0.0;
    for (int iter = 0; iter < 30; ++iter) {
        const double tA = tand(decA) * tand(lat);
        const double tB = tand(decB) * tand(lat);
        if (std::abs(tA) >= 0.9999 || std::abs(tB) >= 0.9999) return false;

        const double AD_A = asind(tA);
        const double AD_B = asind(tB);
        const double f    = wrapDelta(sA * AD_A - sB * AD_B - D);
        if (std::abs(f) < 1e-5) {
            if (std::abs(lat) > 66.5) return false;
            latOut = lat;
            return true;
        }
        const double sec2_lat = 1.0 / std::pow(std::cos(lat * DEGTORAD), 2);
        const double dA = (tand(decA) * sec2_lat) / std::sqrt(1.0 - tA*tA);
        const double dB = (tand(decB) * sec2_lat) / std::sqrt(1.0 - tB*tB);
        const double fprime = sA * dA - sB * dB;
        if (std::abs(fprime) < 1e-10) return false;

        double step = f / fprime;
        if (step >  20.0) step =  20.0;
        if (step < -20.0) step = -20.0;
        lat -= step;
        if (lat >  66.5) lat =  66.5 - 0.01;
        if (lat < -66.5) lat = -66.5 + 0.01;
    }
    return false;
}

} // namespace

void
enumerateNatalParanLatitudes(const Horoscope& natal,
                             double           paranOrbDeg,
                             QVector<ParanLatitudeRow>& out)
{
    out.clear();

    const double jdNatal = getJulianDate(natal.inputData.GMT());
    const double natalLat = natal.inputData.location().y();
    const double natalLon = natal.inputData.location().x();

    // Collect ex-precessed natal RA/Dec for every planet with a usable sweNum.
    struct BodyEntry {
        PlanetId pid;
        double   ra;
        double   dec;
    };
    QVector<BodyEntry> bodies;
    bodies.reserve(natal.planets.size());

    for (const Planet& p : std::as_const(natal.planets)) {
        if (p.id >= Angles_Start) continue;
        if (p.id <= Planet_None)  continue;
        if (p.id == Planet_NorthNode || p.id == Planet_SouthNode) continue;
        const Planet& pDef = getPlanet(p.id);
        if (pDef.sweNum < 0) continue;

        double ra, dec;
        if (!natalTropicalEquatorialPos(p.id, jdNatal, ra, dec)) continue;
        bodies.append({ p.id, ra, dec });
    }

    // Truncate natal JD to midnight UTC so computeNatalParanTransits() searches
    // the day containing the natal moment (it scans [d_jd, d_jd + 1)).
    const double jdNatalDay = std::floor(jdNatal + 0.5) - 0.5;

    // Cache angle-transit times at the natal location, per body — used for
    // the "natal orb" column.  Returns invalid QDateTime / NaN RA when the
    // angle is circumpolar at the natal latitude.
    QHash<PlanetId, std::array<QDateTime, 4>> natalTransits;
    QHash<PlanetId, std::array<double, 4>>    natalTransitRA;
    auto getNatalTransits = [&](PlanetId pid, double ra, double dec)
        -> const std::array<QDateTime, 4>&
    {
        auto it = natalTransits.find(pid);
        if (it != natalTransits.end()) return *it;
        QDateTime at[4]; double rax[4];
        computeNatalParanTransits(ra, dec, jdNatal, jdNatalDay,
                                  natalLat, natalLon, at, rax);
        std::array<QDateTime, 4> aArr;
        std::array<double, 4>    rArr;
        for (int i = 0; i < 4; ++i) { aArr[i] = at[i]; rArr[i] = rax[i]; }
        natalTransitRA.insert(pid, rArr);
        return *natalTransits.insert(pid, aArr);
    };

    for (int i = 0; i < bodies.size(); ++i) {
        for (int j = i + 1; j < bodies.size(); ++j) {
            const BodyEntry& A = bodies[i];
            const BodyEntry& B = bodies[j];
            for (int mA = 0; mA < 4; ++mA) {
                for (int mB = 0; mB < 4; ++mB) {
                    // Drop the pure RA-only cases (both participants are MC or IC).
                    const bool aIsAxis = (mA == 2 || mA == 3);
                    const bool bIsAxis = (mB == 2 || mB == 3);
                    if (aIsAxis && bIsAxis) continue;

                    const AngleLST la = makeAngleLST(A.ra, mA);
                    const AngleLST lb = makeAngleLST(B.ra, mB);
                    const double   D  = wrapDelta(lb.raOffset - la.raOffset);

                    double lat;
                    if (!solveParanLatitude(A.dec, B.dec, la.signAD, lb.signAD, D, lat))
                        continue;

                    ParanLatitudeRow row{};
                    row.a       = A.pid;
                    row.angleA  = mA;
                    row.b       = B.pid;
                    row.angleB  = mB;
                    row.latitude = lat;

                    // Compute natal-orb at the chart's actual location.
                    const auto& atA = getNatalTransits(A.pid, A.ra, A.dec);
                    const auto& atB = getNatalTransits(B.pid, B.ra, B.dec);
                    const QDateTime& tA = atA[mA];
                    const QDateTime& tB = atB[mB];
                    if (tA.isValid() && tB.isValid()) {
                        row.natalOrbSec = qAbs(tA.secsTo(tB));
                        // RA delta: pull each body's RA at its own angle-transit.
                        const double raA = natalTransitRA[A.pid][mA];
                        const double raB = natalTransitRA[B.pid][mB];
                        // Measure the LST delta: this is the clock-equivalent
                        // angular distance between the two angle-transit moments.
                        const AngleLST llA = makeAngleLST(raA, mA);
                        const AngleLST llB = makeAngleLST(raB, mB);
                        // Recompute AD at natal lat for the LST signature.
                        auto lstAt = [&](const AngleLST& lp, double dec) {
                            if (lp.signAD == 0) return lp.raOffset;
                            return swe_degnorm(lp.raOffset
                                + lp.signAD * asind(tand(dec) * tand(natalLat)));
                        };
                        const double lstA = lstAt(llA, A.dec);
                        const double lstB = lstAt(llB, B.dec);
                        row.natalOrbDeg = std::abs(wrapDelta(lstB - lstA));
                        row.hasNatalOrb = true;
                        row.present     = row.natalOrbDeg <= paranOrbDeg;
                    } else {
                        row.hasNatalOrb = false;
                        row.present     = false;
                        row.natalOrbSec = 0;
                        row.natalOrbDeg = 0.0;
                    }
                    out.append(row);
                }
            }
        }
    }
}

void
AspectFinder::findParans()
{
    if (_alist.empty()) return;
    bool wantTransitOnly  = showParanatellonta();
    bool wantTransitNatal = showParanatellontaToNatal();
    if (!wantTransitOnly && !wantTransitNatal) return;

    // When paran patterns are specified, only process the listed bodies.
    const bool patternMode = !_paranPatterns.empty();
    auto bodyAllowed = [&](unsigned alistIdx) -> bool {
        if (!patternMode) return true;
        for (const auto& spec : _paranPatterns)
            for (const auto& e : spec.entries)
                if (e.alistIdx == alistIdx) return true;
        return false;
    };

    // ------------------------------------------------------------------
    // Locate the natal InputData (for ex-precession epoch) and the locus
    // InputData (the current/transit location used for house/geopos calcs).
    // OmnibusFinder pushes natal first; locus is derived from TransitPositions.
    // ------------------------------------------------------------------
    if (_ids.isEmpty()) return;
    const InputData& natalIda = _ids.first();

    // 1° = 4 min = 240 s sidereal time, mirroring astro-output.cpp:1208.
    const double paranOrbDeg  = paranOrb;
    const qint64 paranOrbSecs = qint64(paranOrbDeg * 240.0);

    // ------------------------------------------------------------------
    // Partition _alist into transit-body and natal-body indices, skipping
    // angles, house cusps, ingresses, midpoints, and bodies that don't
    // represent a celestial point with a swe_rise_trans-compatible sweNum.
    // For natals in ecliptic mode, build a sidecar
    // NatalExprecessedPosition that gives us ex-precessed RA/Dec via
    // radecAt(); in equatorial mode the original entry is already a
    // NatalExprecessedPosition.
    // ------------------------------------------------------------------
    QVector<int> transitIndices;
    QVector<int> natalIndices;
    QHash<int, std::shared_ptr<NatalExprecessedPosition>> natalSidecars;

    for (int i = 0; i < int(_alist.size()); ++i) {
        auto* base = _alist[i];
        auto* pl   = dynamic_cast<PlanetLoc*>(base);
        if (!pl) continue;
        const PlanetId pid = pl->planet.planetId();
        if (pid >= Angles_Start) continue;       // skip Asc/MC/Desc/IC, houses, ingresses
        if (pid <= Planet_None) continue;
        // Lunar nodes don't rise/set in a meaningful astrological way;
        // exclude them from paran detection.
        if (pid == Planet_NorthNode || pid == Planet_SouthNode) continue;

        // Pattern filter: when paran patterns are active, skip bodies not in any spec.
        if (patternMode && !bodyAllowed(unsigned(i))) continue;

        if (auto* trans = dynamic_cast<TransitPosition*>(pl)) {
            (void)trans;
            transitIndices.append(i);
        } else if (dynamic_cast<NatalExprecessedPosition*>(pl)) {
            if (!wantTransitNatal) continue;
            natalIndices.append(i);
        } else if (dynamic_cast<NatalPosition*>(pl)) {
            if (!wantTransitNatal) continue;
            natalIndices.append(i);
            natalSidecars[i] = std::make_shared<NatalExprecessedPosition>(
                pl->planet, natalIda, "r");
        }
    }

    if (transitIndices.isEmpty()) return; // need at least one transit for any paran type

    // Resolve the locus (current/transit) location from the first TransitPosition.
    // This is the geographic location used for all house/geopos calculations —
    // NOT the natal birth location, which is only needed for ex-precession epoch.
    const InputData* locusIdaPtr = nullptr;
    for (int i : transitIndices) {
        if (auto* tp = dynamic_cast<TransitPosition*>(_alist[i]))
            { locusIdaPtr = &tp->input(); break; }
    }
    if (!locusIdaPtr) return;
    const InputData& locusIda = *locusIdaPtr;
    const double     locusLat = locusIda.location().y();
    const double     locusLon = locusIda.location().x();
    const double     locusAlt = locusIda.location().z();

    auto getNatalRADec = [&](int aIdx, double jd, double& ra, double& dec) {
        if (auto it = natalSidecars.find(aIdx); it != natalSidecars.end()) {
            it.value()->radecAt(jd, ra, dec);
            return true;
        }
        auto* base = _alist[aIdx];
        if (auto* nep = dynamic_cast<NatalExprecessedPosition*>(base)) {
            nep->radecAt(jd, ra, dec);
            return true;
        }
        return false;
    };

    auto getNatalRADecSpeed = [&](int aIdx, double tjd,
                                  double& ra, double& dec,
                                  double& dRAdt, double& dDecdt) -> bool {
        if (auto it = natalSidecars.find(aIdx); it != natalSidecars.end()) {
            return it.value()->radecSpeedAt(tjd, ra, dec, dRAdt, dDecdt);
        }
        if (auto* nep = dynamic_cast<NatalExprecessedPosition*>(_alist[aIdx])) {
            return nep->radecSpeedAt(tjd, ra, dec, dRAdt, dDecdt);
        }
        return false;
    };

    // ------------------------------------------------------------------
    // Daily loop. aspectMode is modalized to equatorial so any (*trans)(jd,1)
    // calls compute equatorial coordinates uniformly. The modalize is scoped
    // strictly around the loop; event-payload finalization happens after it
    // destructs so cloned PlanetLocs see _rasiLoc in the user's mode.
    // ------------------------------------------------------------------
    QMap<QString, ParanState> activeParans;
    QVector<ParanState>       toEmit;

    {
        modalize<aspectModeType> amOverride(aspectMode, amcEquatorial);

        const auto start = _range.first;
        const auto end   = _range.second.addDays(1);
        auto       d     = start.startOfDay().toUTC();
        const auto e     = end.startOfDay().toUTC();

        const double bjd = getJulianDate(d);
        const double ejd = getJulianDate(e);
        double       ljd = bjd;

        auto pairwiseArc = [](qint64 sA, qint64 sB) -> qint64 {
            qint64 delta = qAbs(sA - sB);
            return qMin(delta, qint64(86400) - delta);
        };

        while (d < e) {
            QCoreApplication::processEvents();
            if (_state == cancelRequestedState) break;
            if (_state == pauseRequestedState) {
                QThread::usleep(100000);
                continue;
            }

            const double jd = getJulianDate(d);

            // Progress emission (positive fraction during the daily walk).
            if (jd - ljd >= 5.0) {
                emit progress((ljd - bjd) / std::max(1.0, ejd - bjd));
                ljd = jd;
            }

            QVector<ParanEntry> entries;
            entries.reserve((transitIndices.size() + natalIndices.size()) * 4);

            // Each (body, angle) has ~1.00274 sidereal transits per solar
            // day, so on the ~one-day-per-year when LST_target crosses UTC
            // midnight, a single solar day contains *two* transits.
            // findAngleTransitJD's initial-guess clamping returns just one of
            // them. Search a second anchor at jd+0.5 to catch the other; the
            // late-day root will fall in [jd+0.5, jd+1) when present. Dedupe
            // when both anchors converge to the same root.
            auto collectDaily = [&](const BodyAtFn& bodyAt, int alistIdx,
                                    bool isNatal) {
                for (int m = 0; m < 4; ++m) {
                    qint64 firstSec = -1;
                    for (double anchor : { jd, jd + 0.5 }) {
                        double tjd;
                        if (!findAngleTransitJD(bodyAt, anchor, locusLat,
                                                locusLon, m, tjd))
                            continue;
                        const QDateTime when = dateTimeFromJulian(tjd);
                        const qint64    sec  = d.secsTo(when);
                        if (sec < 0 || sec >= 86400) continue;
                        if (firstSec >= 0 && qAbs(sec - firstSec) < 60)
                            continue;
                        entries.append({ alistIdx, m, sec, when, isNatal });
                        firstSec = sec;
                    }
                }
            };

            // -- Transit angle-transit times via Newton–Raphson -----------
            for (int i : transitIndices) {
                auto* trans = dynamic_cast<TransitPosition*>(_alist[i]);
                if (!trans) continue;
                const Planet& p      = getPlanet(trans->planet.planetId());
                const int     sweNum = p.sweNum;
                if (sweNum < 0) continue;
                const PlanetId pid = trans->planet.planetId();

                // ---- Midpoint transit body: 2-body BodyAtFn ----
                if (trans->planet.isMidpt()) {
                    const Planet& p2      = getPlanet(trans->planet.planetId2());
                    const int     sweNum2 = p2.sweNum;
                    if (sweNum2 < 0) continue;
                    const bool     isOpp  = trans->planet.isOppMidpt();

                    BodyAtFn bodyAt = [sweNum, sweNum2, isOpp](
                        double tjd, double& ra, double& dec,
                        double& dRAdt, double& dDecdt) -> bool {
                        double xx1[6], xx2[6];
                        char   errStr[256] = "";
                        if (swe_calc_ut(tjd, sweNum,
                                        SEFLG_SWIEPH | SEFLG_EQUATORIAL | SEFLG_SPEED,
                                        xx1, errStr) < 0) return false;
                        if (swe_calc_ut(tjd, sweNum2,
                                        SEFLG_SWIEPH | SEFLG_EQUATORIAL | SEFLG_SPEED,
                                        xx2, errStr) < 0) return false;
                        double ra1 = xx1[0], dec1 = xx1[1], dRA1 = xx1[3], dDec1 = xx1[4];
                        double ra2 = xx2[0], dec2 = xx2[1], dRA2 = xx2[3], dDec2 = xx2[4];
                        // Shortest-arc midpoint in RA
                        if (ra1 - ra2 >= 180.0)  ra1 -= 360.0;
                        else if (ra2 - ra1 >= 180.0) ra2 -= 360.0;
                        ra  = swe_degnorm((ra1 + ra2) / 2.0);
                        if (isOpp) ra = swe_degnorm(ra + 180.0);
                        dec    = (dec1 + dec2) / 2.0;
                        dRAdt  = (dRA1 + dRA2) / 2.0;
                        dDecdt = (dDec1 + dDec2) / 2.0;
                        return true;
                    };

                    collectDaily(bodyAt, i, false);
                    continue; // handled — skip the single-body path below
                }

                BodyAtFn bodyAt = [sweNum, pid](double tjd,
                                                double& ra, double& dec,
                                                double& dRAdt, double& dDecdt) -> bool {
                    double xx[6];
                    char errStr[256] = "";
                    if (swe_calc_ut(tjd, sweNum,
                                    SEFLG_SWIEPH | SEFLG_EQUATORIAL | SEFLG_SPEED,
                                    xx, errStr) < 0)
                        return false;
                    ra     = xx[0];
                    dec    = xx[1];
                    dRAdt  = xx[3];
                    dDecdt = xx[4];
                    if (pid == Planet_SouthNode) {
                        ra     = swe_degnorm(ra + 180.0);
                        dec    = -dec;
                        dDecdt = -dDecdt;
                    }
                    return true;
                };

                collectDaily(bodyAt, i, false);
            }

            // -- Natal angle-transit times via Newton–Raphson ------------
            for (int i : natalIndices) {
                BodyAtFn bodyAt = [&, i](double tjd,
                                         double& ra, double& dec,
                                         double& dRAdt, double& dDecdt) -> bool {
                    return getNatalRADecSpeed(i, tjd, ra, dec, dRAdt, dDecdt);
                };

                collectDaily(bodyAt, i, true);
            }

            // -- Cluster by secOfDay within paranOrbSecs (circular) ------
            std::sort(entries.begin(), entries.end(),
                      [](const ParanEntry& a, const ParanEntry& b) {
                          return a.secOfDay < b.secOfDay;
                      });

            static bool paranDbg = true;

            QVector<QVector<int>> clusters;
            if (!entries.isEmpty()) {
                QVector<int> cur;
                cur.append(0);
                for (int k = 1; k < entries.size(); ++k) {
                    if (entries[k].secOfDay - entries[k - 1].secOfDay
                        <= paranOrbSecs)
                    {
                        cur.append(k);
                    } else {
                        clusters.append(cur);
                        cur.clear();
                        cur.append(k);
                    }
                }
                if (!cur.isEmpty()) clusters.append(cur);

                // Wrap: merge tail and head if last and first are within orb
                // across midnight.
                if (clusters.size() >= 2) {
                    const ParanEntry& first =
                        entries[clusters.first().first()];
                    const ParanEntry& last = entries[clusters.last().last()];
                    const qint64 wrapDelta =
                        (86400 - last.secOfDay) + first.secOfDay;
                    if (wrapDelta <= paranOrbSecs) {
                        QVector<int> tail = clusters.takeLast();
                        clusters.first() = tail + clusters.first();
                    }
                }
            }

            // -- Classify clusters and update active map -----------------
            QSet<QString> currentKeys;

            // Track one sub-cluster (any size ≥ 2) given the entry indices.
            // Handles quorum/type checks, key generation, spread, and
            // activeParans insertion/update.  Idempotent: calling it for the
            // same set of entries twice on the same day just inserts the key
            // into currentKeys a second time, which is harmless.
            auto trackSubCluster = [&](const QVector<int>& cEntriesRaw) {
                // Dedupe by (aIdx, angle): when the same body+angle appears
                // more than once in the cluster — true double-transit days
                // (a body's LST_target crossing local midnight gives two
                // solar-day transits), or wrap-merge pulling in a far-day
                // sibling — keep one representative per (aIdx, angle), the
                // entry whose secOfDay is closest (circularly) to the
                // cluster's tentative mean. Without this, the cluster's key
                // contains the body twice and splinters off from the long-
                // running event's key.
                QVector<int> cEntries;
                {
                    const qint64 meanSec0 =
                        circularMeanSeconds(cEntriesRaw, entries);
                    QHash<QPair<int, int>, int> rep;
                    for (int k : cEntriesRaw) {
                        QPair<int, int> bk(entries[k].aIdx, entries[k].angle);
                        auto it = rep.find(bk);
                        if (it == rep.end()) { rep.insert(bk, k); continue; }
                        const qint64 dNew =
                            pairwiseArc(entries[k].secOfDay, meanSec0);
                        const qint64 dOld =
                            pairwiseArc(entries[it.value()].secOfDay, meanSec0);
                        if (dNew < dOld) it.value() = k;
                    }
                    cEntries.reserve(rep.size());
                    for (auto it = rep.begin(); it != rep.end(); ++it)
                        cEntries.append(it.value());
                    std::sort(cEntries.begin(), cEntries.end(),
                              [&](int a, int b) {
                                  return entries[a].secOfDay
                                       < entries[b].secOfDay;
                              });
                }

                QSet<int> distinctBodies;
                bool      hasTrans = false, hasNatal = false;
                for (int k : cEntries) {
                    distinctBodies.insert(entries[k].aIdx);
                    if (entries[k].isNatal) hasNatal = true;
                    else hasTrans = true;
                }
                if (distinctBodies.size() < 2) return;

                EventType type;
                if (hasTrans && !hasNatal) {
                    if (!wantTransitOnly) return;
                    type = etcParanatellonta;
                } else if (hasTrans && hasNatal) {
                    if (!wantTransitNatal) return;
                    type = etcParanatellontaToNatal;
                } else {
                    return; // pure natal
                }

                // Pattern filter: when _paranPatterns is set, this cluster must satisfy
                // at least one spec.  Phase 2: the angle mask on each entry is honored.
                if (patternMode) {
                    bool satisfiedAnySpec = false;
                    for (const auto& spec : _paranPatterns) {
                        if (spec.et != type) continue;
                        bool satisfiesSpec = true;
                        for (const auto& se : spec.entries) {
                            bool found = false;
                            for (int k : cEntries) {
                                if (unsigned(entries[k].aIdx) == se.alistIdx) {
                                    // Check angle mask (bit N = 1 << angle-index N).
                                    if (se.angleMask & (1 << entries[k].angle)) {
                                        found = true;
                                        break;
                                    }
                                }
                            }
                            if (!found) { satisfiesSpec = false; break; }
                        }
                        if (satisfiesSpec) { satisfiedAnySpec = true; break; }
                    }
                    if (!satisfiedAnySpec) return;
                }

                // Stable key from sorted (aIdx, angle) pairs.
                QVector<std::pair<int, int>> kvpairs;
                kvpairs.reserve(cEntries.size());
                for (int k : cEntries)
                    kvpairs.append({ entries[k].aIdx, entries[k].angle });
                std::sort(kvpairs.begin(), kvpairs.end());
                QVector<int> sIdx, sAng;
                sIdx.reserve(kvpairs.size());
                sAng.reserve(kvpairs.size());
                for (const auto& pr : kvpairs) {
                    sIdx.append(pr.first);
                    sAng.append(pr.second);
                }
                const QString key = makeParanKey(type, sIdx, sAng);
                currentKeys.insert(key);

                // Spread = circular arc across all entries (max pairwise arc).
                qint64 spread = 0;
                for (int ai = 0; ai < cEntries.size(); ++ai) {
                    for (int bi = ai + 1; bi < cEntries.size(); ++bi) {
                        spread = qMax(spread,
                                      pairwiseArc(entries[cEntries[ai]].secOfDay,
                                                  entries[cEntries[bi]].secOfDay));
                    }
                }

                const qint64    meanSec = circularMeanSeconds(cEntries, entries);
                const QDateTime peakDT  = d.addSecs(meanSec);
                const double    peakJd  = jd + double(meanSec) / 86400.0;

                if (paranDbg) {
                    static const char* pName[] = {
                        "Sun","Moon","Mercury","Venus","Mars","Jupiter","Saturn",
                        "Uranus","Neptune","Pluto","NNode","SNode","Chiron",
                        "Ceres","Pallas","Juno","Vesta"
                    };
                    static const char* aName[] = { "Asc","Desc","MC","IC" };
                    auto bodyLabel = [&](int k) -> QString {
                        const auto& e2 = entries[k];
                        auto* pl = dynamic_cast<PlanetLoc*>(_alist[e2.aIdx]);
                        int pid = pl ? (int)pl->planet.planetId() : -1;
                        QString name = (pid >= 0 && pid < 17) ? pName[pid]
                                                              : QString::number(pid);
                        if (e2.isNatal) name += "-r";
                        return name + " " + aName[e2.angle]
                               + " (" + e2.when.toUTC().toString("HH:mm:ss") + " UTC)";
                    };
                    QStringList parts;
                    for (int k : cEntries) parts << bodyLabel(k);
                    // Consecutive gaps so we can see which pairs bridge the orb.
                    QStringList gaps;
                    for (int ki = 1; ki < cEntries.size(); ++ki) {
                        qint64 g = entries[cEntries[ki]].secOfDay
                                 - entries[cEntries[ki-1]].secOfDay;
                        gaps << QString::number(g) + "s";
                    }
                    qDebug().noquote()
                        << "[paran]" << d.toString("yyyy-MM-dd")
                        << "spread=" << spread << "s"
                        << "mean=" << peakDT.toUTC().toString("HH:mm:ss") << "UTC\n"
                        << "  " << parts.join("\n   ")
                        << "\n  gaps:" << gaps.join(", ");
                }

                auto it = activeParans.find(key);
                if (it == activeParans.end()) {
                    ParanState ps;
                    ps.startDate      = d;
                    ps.endDate        = d;
                    ps.firstActiveDT  = peakDT;
                    ps.lastActiveDT   = peakDT;
                    ps.peakDateTime   = peakDT;
                    ps.peakJd         = peakJd;
                    ps.tightestSpread = spread;
                    ps.type           = type;
                    ps.indices        = std::move(sIdx);
                    ps.angles         = std::move(sAng);
                    activeParans.insert(key, std::move(ps));
                } else {
                    ParanState& cur = it.value();
                    cur.endDate     = d;
                    cur.lastActiveDT = peakDT;
                    if (spread < cur.tightestSpread) {
                        cur.tightestSpread = spread;
                        cur.peakDateTime   = peakDT;
                        cur.peakJd         = peakJd;
                    }
                }
            };

            for (const auto& cluster : clusters) {
                // Track the full cluster as one event.
                trackSubCluster(cluster);

                // Also track every valid sub-tuple of size 2..N-1
                // (the full N-body cluster was tracked above). Without this,
                // a long-running K-body paran disappears from currentKeys on
                // a day when a (K+1)-th body briefly joins and enlarges the
                // cluster, which incorrectly closes the long-running event.
                //
                // Subset rule: a sub-tuple is admissible iff, after sorting
                // by secOfDay, all consecutive gaps are within paranOrbSecs
                // (with circular wrap). This matches the chain-adjacency
                // semantics the cluster-build itself uses — a stricter
                // pairwise-all-in-orb rule would reject Sat-Plu chained via
                // Jup, splintering long-running events that pattern-search
                // (which restricts the body set so the cluster IS the
                // subset) handles correctly.
                //
                // Cluster sizes >7 are gated to size-2 and size-(N-1) subsets
                // to keep the power-set bounded; clusters that large are
                // already pathological.
                const int N = cluster.size();
                if (N > 2) {
                    auto pairsOK = [&](const QVector<int>& subEntries) {
                        if (subEntries.size() < 2) return false;
                        QVector<qint64> secs;
                        secs.reserve(subEntries.size());
                        for (int k : subEntries)
                            secs.append(entries[k].secOfDay);
                        std::sort(secs.begin(), secs.end());
                        // Compute all gaps (linear + wrap) and find the
                        // largest. The largest is the "outside" of the
                        // cycle that the chain skips; the rest must each
                        // be within orb.  Equivalent: second-largest <= orb.
                        QVector<qint64> gaps;
                        gaps.reserve(secs.size());
                        for (int j = 1; j < secs.size(); ++j)
                            gaps.append(secs[j] - secs[j - 1]);
                        gaps.append((86400 - secs.last()) + secs.first());
                        std::sort(gaps.begin(), gaps.end(),
                                  std::greater<qint64>());
                        return gaps.size() < 2 || gaps[1] <= paranOrbSecs;
                    };

                    const int maxFullEnum = 7;
                    const int hiSize = (N <= maxFullEnum) ? (N - 1) : 2;

                    for (int subSize = 2; subSize <= hiSize; ++subSize) {
                        QVector<int> idx(subSize);
                        for (int j = 0; j < subSize; ++j) idx[j] = j;
                        while (true) {
                            QVector<int> sub;
                            sub.reserve(subSize);
                            for (int j = 0; j < subSize; ++j)
                                sub.append(cluster[idx[j]]);
                            if (pairsOK(sub)) trackSubCluster(sub);

                            int k = subSize - 1;
                            while (k >= 0 && idx[k] == N - subSize + k) --k;
                            if (k < 0) break;
                            ++idx[k];
                            for (int j = k + 1; j < subSize; ++j)
                                idx[j] = idx[j - 1] + 1;
                        }
                    }

                    // For large clusters, also track the (N-1)-body subsets
                    // explicitly so a long-running (N-1)-body event survives
                    // the transient join of an Nth body.
                    if (N > maxFullEnum) {
                        for (int drop = 0; drop < N; ++drop) {
                            QVector<int> sub;
                            sub.reserve(N - 1);
                            for (int j = 0; j < N; ++j)
                                if (j != drop) sub.append(cluster[j]);
                            if (pairsOK(sub)) trackSubCluster(sub);
                        }
                    }
                }
            }

            // Close any parans no longer present today.
            for (auto it = activeParans.begin(); it != activeParans.end();) {
                if (!currentKeys.contains(it.key())) {
                    toEmit.append(it.value());
                    it = activeParans.erase(it);
                } else {
                    ++it;
                }
            }

            d = d.addDays(1);
        }

        // Flush any parans still active at end of range.
        for (const auto& ps : activeParans) toEmit.append(ps);

        // ================================================================
        // Optional boundary extension.  Walk backward from the range start
        // and forward from the range end to find the true in-orb brackets
        // for parans that touched either boundary.  Only computes the body
        // subset involved in pending clusters; stops per-paran as soon as
        // it dissolves.  Capped at 183 days each direction.
        //
        // Controlled by includeTransitRange — set to false by callers that
        // are supplementing an existing cache to avoid redundant searches.
        // ================================================================
        if (includeTransitRange && _state != cancelRequestedState) {

            const QDateTime rangeStartDay = _range.first.startOfDay().toUTC();
            const QDateTime rangeLastDay  = _range.second.startOfDay().toUTC();

            // Returns entries for one day, limited to bodies in bodySubset.
            auto computeDayEntriesExt = [&](const QDateTime& dayDT, double jd2,
                                            const QSet<int>& bodySubset)
                                            -> QVector<ParanEntry> {
                QVector<ParanEntry> ent;
                ent.reserve(int(bodySubset.size()) * 4);

                for (int i : transitIndices) {
                    if (!bodySubset.contains(i)) continue;
                    auto* trans = dynamic_cast<TransitPosition*>(_alist[i]);
                    if (!trans) continue;
                    const Planet& p    = getPlanet(trans->planet.planetId());
                    const int  swn     = p.sweNum;
                    if (swn < 0) continue;
                    const PlanetId pid2 = trans->planet.planetId();
                    BodyAtFn bodyAt2 = [swn, pid2](double tjd,
                                                    double& ra2, double& dec2,
                                                    double& dRAdt2, double& dDecdt2) -> bool {
                        double xx2[6]; char err2[256] = "";
                        if (swe_calc_ut(tjd, swn,
                                        SEFLG_SWIEPH | SEFLG_EQUATORIAL | SEFLG_SPEED,
                                        xx2, err2) < 0)
                            return false;
                        ra2 = xx2[0]; dec2 = xx2[1]; dRAdt2 = xx2[3]; dDecdt2 = xx2[4];
                        if (pid2 == Planet_SouthNode) {
                            ra2 = swe_degnorm(ra2 + 180.0); dec2 = -dec2; dDecdt2 = -dDecdt2;
                        }
                        return true;
                    };
                    for (int m = 0; m < 4; ++m) {
                        double tjd;
                        if (!findAngleTransitJD(bodyAt2, jd2, locusLat, locusLon, m, tjd)) continue;
                        const QDateTime when = dateTimeFromJulian(tjd);
                        const qint64 sec = dayDT.secsTo(when);
                        if (sec < 0 || sec >= 86400) continue;
                        ent.append({ i, m, sec, when, false });
                    }
                }

                for (int i : natalIndices) {
                    if (!bodySubset.contains(i)) continue;
                    BodyAtFn nBodyAt = [&, i](double tjd,
                                              double& ra2, double& dec2,
                                              double& dRAdt2, double& dDecdt2) -> bool {
                        return getNatalRADecSpeed(i, tjd, ra2, dec2, dRAdt2, dDecdt2);
                    };
                    for (int m = 0; m < 4; ++m) {
                        double tjd;
                        if (!findAngleTransitJD(nBodyAt, jd2, locusLat, locusLon, m, tjd)) continue;
                        const QDateTime when = dateTimeFromJulian(tjd);
                        const qint64 sec = dayDT.secsTo(when);
                        if (sec < 0 || sec >= 86400) continue;
                        ent.append({ i, m, sec, when, true });
                    }
                }
                return ent;
            };

            // Info about a paran cluster on one day.
            struct ParanKeyInfo {
                qint64    spread;
                QDateTime peakDT;
                double    peakJd;
            };

            // Clusters one day and returns map: paranKey -> ParanKeyInfo.
            auto keysForDay = [&](const QDateTime& dayDT,
                                   const QSet<int>& bodySubset)
                                   -> QMap<QString, ParanKeyInfo> {
                const double jd2 = getJulianDate(dayDT);
                auto ent = computeDayEntriesExt(dayDT, jd2, bodySubset);
                std::sort(ent.begin(), ent.end(), [](const ParanEntry& a, const ParanEntry& b) {
                    return a.secOfDay < b.secOfDay;
                });

                QVector<QVector<int>> cls;
                if (!ent.isEmpty()) {
                    QVector<int> cur; cur.append(0);
                    for (int k = 1; k < ent.size(); ++k) {
                        if (ent[k].secOfDay - ent[k-1].secOfDay <= paranOrbSecs)
                            cur.append(k);
                        else { cls.append(cur); cur.clear(); cur.append(k); }
                    }
                    if (!cur.isEmpty()) cls.append(cur);
                    if (cls.size() >= 2) {
                        const qint64 wrapDelta =
                            (86400 - ent[cls.last().last()].secOfDay) +
                            ent[cls.first().first()].secOfDay;
                        if (wrapDelta <= paranOrbSecs) {
                            QVector<int> tail = cls.takeLast();
                            cls.first() = tail + cls.first();
                        }
                    }
                }

                QMap<QString, ParanKeyInfo> result;
                auto processCluster2 = [&](const QVector<int>& cEnt) {
                    QSet<int> distinct; bool hasTr = false, hasNat = false;
                    for (int k : cEnt) {
                        distinct.insert(ent[k].aIdx);
                        if (ent[k].isNatal) hasNat = true; else hasTr = true;
                    }
                    if (distinct.size() < 2) return;
                    EventType et2;
                    if      (hasTr && !hasNat) { if (!wantTransitOnly)  return; et2 = etcParanatellonta; }
                    else if (hasTr &&  hasNat) { if (!wantTransitNatal) return; et2 = etcParanatellontaToNatal; }
                    else return;

                    QVector<std::pair<int,int>> kv;
                    for (int k : cEnt) kv.append({ ent[k].aIdx, ent[k].angle });
                    std::sort(kv.begin(), kv.end());
                    QVector<int> sI, sA;
                    for (const auto& pr : kv) { sI.append(pr.first); sA.append(pr.second); }
                    const QString key = makeParanKey(et2, sI, sA);

                    qint64 sp = 0;
                    for (int ai = 0; ai < cEnt.size(); ++ai)
                        for (int bi = ai+1; bi < cEnt.size(); ++bi)
                            sp = qMax(sp, pairwiseArc(ent[cEnt[ai]].secOfDay,
                                                       ent[cEnt[bi]].secOfDay));
                    const qint64 ms  = circularMeanSeconds(cEnt, ent);
                    const QDateTime pDT = dayDT.addSecs(ms);
                    const double    pJd = jd2 + double(ms) / 86400.0;
                    if (!result.contains(key))
                        result.insert(key, { sp, pDT, pJd });
                };

                for (const auto& cl : cls) {
                    processCluster2(cl);
                    if (cl.size() > 2) {
                        for (int ai = 0; ai < cl.size(); ++ai)
                            for (int bi = ai+1; bi < cl.size(); ++bi) {
                                if (pairwiseArc(ent[cl[ai]].secOfDay,
                                                ent[cl[bi]].secOfDay) > paranOrbSecs)
                                    continue;
                                processCluster2({ cl[ai], cl[bi] });
                            }
                    }
                }
                return result;
            };

            // ---- BACKWARD EXTENSION ------------------------------------
            {
                QSet<int> backBodies;
                struct BExt { ParanState* ps; bool done; };
                QVector<BExt> backPending;
                for (auto& ps : toEmit) {
                    if (ps.startDate == rangeStartDay) {
                        for (int idx : ps.indices) backBodies.insert(idx);
                        backPending.append({ &ps, false });
                    }
                }

                if (!backPending.isEmpty()) {
                    QDateTime bd = rangeStartDay.addDays(-1);
                    const QDateTime bLimit = rangeStartDay.addDays(-183);
                    while (bd >= bLimit && _state != cancelRequestedState) {
                        const bool anyAlive = std::any_of(
                            backPending.constBegin(), backPending.constEnd(),
                            [](const BExt& be) { return !be.done; });
                        if (!anyAlive) break;
                        QCoreApplication::processEvents();
                        if (_state == pauseRequestedState) {
                            QThread::usleep(100000);
                            continue;
                        }
                        const auto dayKeys = keysForDay(bd, backBodies);
                        for (auto& be : backPending) {
                            if (be.done) continue;
                            const QString key = makeParanKey(be.ps->type, be.ps->indices,
                                                              be.ps->angles);
                            const auto it = dayKeys.find(key);
                            if (it == dayKeys.end()) { be.done = true; continue; }
                            be.ps->startDate     = bd;
                            be.ps->firstActiveDT = it->peakDT;
                            if (it->spread < be.ps->tightestSpread) {
                                be.ps->tightestSpread = it->spread;
                                be.ps->peakDateTime   = it->peakDT;
                                be.ps->peakJd         = it->peakJd;
                            }
                        }
                        bd = bd.addDays(-1);
                    }
                }
            }

            // ---- FORWARD EXTENSION -------------------------------------
            {
                QSet<int> fwdBodies;
                struct FExt { ParanState* ps; bool done; };
                QVector<FExt> fwdPending;
                for (auto& ps : toEmit) {
                    if (ps.endDate == rangeLastDay) {
                        for (int idx : ps.indices) fwdBodies.insert(idx);
                        fwdPending.append({ &ps, false });
                    }
                }

                if (!fwdPending.isEmpty()) {
                    QDateTime fd = rangeLastDay.addDays(1);
                    const QDateTime fLimit = rangeLastDay.addDays(183);
                    while (fd <= fLimit && _state != cancelRequestedState) {
                        const bool anyAlive = std::any_of(
                            fwdPending.constBegin(), fwdPending.constEnd(),
                            [](const FExt& fe) { return !fe.done; });
                        if (!anyAlive) break;
                        QCoreApplication::processEvents();
                        if (_state == pauseRequestedState) {
                            QThread::usleep(100000);
                            continue;
                        }
                        const auto dayKeys = keysForDay(fd, fwdBodies);
                        for (auto& fe : fwdPending) {
                            if (fe.done) continue;
                            const QString key = makeParanKey(fe.ps->type, fe.ps->indices,
                                                              fe.ps->angles);
                            const auto it = dayKeys.find(key);
                            if (it == dayKeys.end()) { fe.done = true; continue; }
                            fe.ps->endDate      = fd;
                            fe.ps->lastActiveDT = it->peakDT;
                            if (it->spread < fe.ps->tightestSpread) {
                                fe.ps->tightestSpread = it->spread;
                                fe.ps->peakDateTime   = it->peakDT;
                                fe.ps->peakJd         = it->peakJd;
                            }
                        }
                        fd = fd.addDays(1);
                    }
                }
            }

        } // end if (includeTransitRange)

    }
    // modalize<> destructs here; aspectMode restored to user setting.

    if (_state == cancelRequestedState) return;

    // ------------------------------------------------------------------
    // Emit events. We re-evaluate transit positions at peak_jd in the
    // user's now-restored aspectMode so cloned PlanetLocs carry _rasiLoc
    // appropriate for downstream display (ecliptic for ecliptic users,
    // equatorial for equatorial users). Natal positions already carry the
    // correct _rasiLoc from chart calc time.
    // ------------------------------------------------------------------
    for (const auto& ps : toEmit) {
        PlanetRangeBySpeed locs;

        for (int n = 0; n < ps.indices.size(); ++n) {
            const int aIdx  = ps.indices[n];
            const int angle = ps.angles[n];
            auto*     base  = _alist[aIdx];
            auto*     orig  = dynamic_cast<PlanetLoc*>(base);
            if (!orig) continue;

            // Recompute transit positions at the peak moment in user's mode.
            // Natal entries preserve their construction-time _rasiLoc.
            if (auto* trans = dynamic_cast<TransitPosition*>(orig)) {
                (*trans)(ps.peakJd, 1);
            }

            PlanetLoc payload(*orig); // sliced copy preserves planet, _rasiLoc, desc
            payload.desc  = paranAngleDesc(angle);
            // PlanetRangeBySpeed is a std::set keyed on |speed|; nudge each
            // entry by a tiny per-slot epsilon to keep otherwise-equal speeds
            // distinct (e.g. all-natal speeds are 0). The nudge preserves
            // sign so the retrograde indicator (speed<0) keeps its meaning.
            const qreal eps = 1e-9 * qreal(n + 1);
            payload.speed   = orig->speed
                            + (orig->speed < 0 ? -eps : eps);
            locs.insert(payload);
        }

        if (locs.size() < 2) continue; // safety: quorum already enforced above

        // Convert tightest cluster spread (seconds of LST) to degrees for the
        // event's orb display: 1° = 4 min = 240 s sidereal time.
        const qreal orbDeg = qreal(ps.tightestSpread) / 240.0;

        auto& ev = _evs.safe_emplace_back(ps.peakDateTime,
                                          unsigned(ps.type),
                                          (unsigned char)1,
                                          std::move(locs),
                                          orbDeg);
        // Range = first/last cluster-mean datetimes ± 2 × paran orb. Using
        // the actual transit times (rather than UTC midnights) keeps short
        // parans from getting an artificial 1-day duration when they sort
        // by duration. The 2 × orb buffer represents the unsampled time
        // before/after the first/last detected day during which the cluster
        // was approaching or leaving the orb region — without it a paran
        // detected on only one day would collapse to a single point.
        const qint64    bufferSecs = 2 * paranOrbSecs;
        const QDateTime rangeStart = ps.firstActiveDT.addSecs(-bufferSecs);
        const QDateTime rangeEnd   = ps.lastActiveDT.addSecs(bufferSecs);
        ev.setRange({ rangeStart, rangeEnd });

        // [PARANLBL] One line per finalized paran event (pre-UI-filter). For
        // each participant: name, labelled angle (Asc/Desc/MC/IC), and — for
        // midpoint bodies — the body's RA at peakJd plus locus RAasc/RAdsc/
        // RAMC/RAIC, with the closest angle tagged so a label/wheel-position
        // mismatch is obvious. Includes durationDays so you can filter to
        // events that survive the SkipLessThanWeek (≥ 7d) proxy filter.
        {
            const double durationDays =
                double(rangeStart.secsTo(rangeEnd)) / 86400.0;
            double cusps[14], ascmc[11];
            swe_houses_ex(ps.peakJd, SEFLG_SWIEPH,
                          locusLat, locusLon, 'C', cusps, ascmc);
            const double RAMC  = ascmc[2];
            const double RAIC  = swe_degnorm(RAMC + 180.0);
            const double RAasc = ascmc[4];
            const double RAdsc = swe_degnorm(RAasc + 180.0);
            const char*  lbl[4] = { "Asc", "Desc", "MC", "IC" };
            const double targetRA[4] = { RAasc, RAdsc, RAMC, RAIC };

            QStringList parts;
            for (int n = 0; n < ps.indices.size(); ++n) {
                const int aIdx  = ps.indices[n];
                const int angle = ps.angles[n];
                auto*     pl    = dynamic_cast<PlanetLoc*>(_alist[aIdx]);
                if (!pl) continue;
                QString tag = pl->planet.name() + " "
                            + (dynamic_cast<NatalExprecessedPosition*>(pl) ||
                               dynamic_cast<NatalPosition*>(pl) ? "-r " : "-t ")
                            + lbl[angle];
                if (pl->planet.isMidpt()) {
                    double ra = 0, dec = 0, dRA = 0, dDec = 0;
                    bool got = false;
                    if (auto* nep =
                            dynamic_cast<NatalExprecessedPosition*>(pl)) {
                        got = nep->radecSpeedAt(ps.peakJd, ra, dec, dRA, dDec);
                    } else if (auto it = natalSidecars.find(aIdx);
                               it != natalSidecars.end()) {
                        got = it.value()->radecSpeedAt(
                            ps.peakJd, ra, dec, dRA, dDec);
                    }
                    // (Transit-midpoint RA/dec recomputed via swe in
                    //  trackSubCluster; not re-derived here.)
                    if (got) {
                        double diff[4];
                        for (int k = 0; k < 4; ++k)
                            diff[k] = fabs(swe_difdeg2n(ra, targetRA[k]));
                        int closestK = 0;
                        for (int k = 1; k < 4; ++k)
                            if (diff[k] < diff[closestK]) closestK = k;
                        tag += QString(" [ra=%1 closest=%2%3]")
                                   .arg(ra, 0, 'f', 3)
                                   .arg(lbl[closestK])
                                   .arg(closestK == angle ? "" : " MISMATCH");
                    }
                }
                parts << tag;
            }
            qDebug().noquote() << "[PARANLBL]"
                << "peak=" << ps.peakDateTime.toString(Qt::ISODate)
                << "dur=" << QString::number(durationDays, 'f', 2) << "d"
                << "spread=" << ps.tightestSpread << "s"
                << "RAasc=" << QString::number(RAasc, 'f', 3)
                << "RAMC="  << QString::number(RAMC,  'f', 3)
                << "|" << parts.join(" + ");
        }
    }

    emit progress(1.0);
}

void
AspectFinder::findPriorStarts(AspectSearchState& state)
{
    //modalize<bool> mum2(st_quiet, false);
    state.nd = state.d;

    // Performance optimization for progressed aspect searches:
    // 1. When working set contains only progressed/natal positions (no transits),
    //    use a 100x larger time increment since progressed positions move ~1/365 as fast
    // 2. When transits are present, skip progressed position updates on most iterations
    //    (update only every 200th iteration) since they change negligibly

    // Helper to check if working set contains only progressed/natal positions
    auto hasOnlySlowPositions = [](const PlanetSet& ws) -> bool {
        for (const auto& pmid : ws) {
            if (pmid.mode() == plmTransit) return false;
        }
        return true;
    };

    int iterationCount = 0;
    const int progressedUpdateFrequency = 200; // Update progressed positions every Nth iteration
    decltype(state.work.size()) workSize = 0;
    decltype(state.tinOrb.size()) tinOrbSize = 0;

    while (!state.work.empty() || !state.tinOrb.empty()
           || !state.exactWork.empty()) {
        QCoreApplication::processEvents();

        if (_state == cancelRequestedState) break;
        if (_state == pauseRequestedState) {
            QThread::usleep(100000);
            continue;
        }

        // Emit progress periodically so the UI can display events found so far
        if (iterationCount % 20 == 0) {
            emit progress(-4.0);  // negative signals "findPriorStarts" phase
        }

        // Build working set
        PlanetSet ws;
        bool showWork = (workSize != state.work.size());
        workSize = state.work.size();
        if (showWork && !st_quiet) {
            if (workSize == 0) {
                qDebug() << "PERF: No planets in work set";
            } else {
                qDebug() << "PERF: Updating planets in work set:";
            }
        }
        for (const auto& hpso : state.work) {
            for (const auto& pso : hpso.second) {
                ws.insert(pso.first.begin(), pso.first.end());
                if (showWork) {
                    qDebug() << "  +" << pso.first.names().join("=");
                }
            }
        }

        bool showTinOrb = (tinOrbSize != state.tinOrb.size());
        tinOrbSize = state.tinOrb.size();
        if (showTinOrb && !st_quiet) {
            if (tinOrbSize==0) {
                qDebug() << "PERF: No planets in tinOrb set";
            } else {
                qDebug() << "PERF: Updating planets from tinOrb set:";
            }
        }
        for (const auto& hijr : state.tinOrb) {
            ws.insert(hijr.first.planets.begin(), hijr.first.planets.end());
            if (showTinOrb) {
                qDebug() << "  +" << hijr.first.planets.names().join("=");
            }
        }

        // Add planets from exact patterns that need backward search
        for (const auto& ewp : state.exactWork) {
            unsigned specIdx = ewp.first.first;
            if (specIdx < _exactPatterns.size()) {
                const auto& spec = _exactPatterns[specIdx];
                for (unsigned idx : spec.alistIndices) {
                    if (auto ploc = dynamic_cast<PlanetLoc*>(_alist[idx]))
                        ws.emplace(ploc->planetModeId());
                }
            }
        }

        // Safety check: if working set is empty, we're done
        if (ws.empty()) {
            qDebug() << "PERF: Working set empty, exiting findPriorStarts";
            break;
        }

        // Check if we've exceeded the backward search limit
        bool onlySlowPos = hasOnlySlowPositions(ws);

        // Optimize time increment for progressed-only working sets
        int localNdays = state.ndays;
        int localNsecs = state.nsecs;
        if (onlySlowPos) {
            // Progressed positions move 1/365th as fast as transits
            // So we can use a much larger time increment
            double progressedRate = state.ndays + state.nsecs / (24. * 60. * 60.);
            progressedRate *= 100.0; // Scale up by 100x for progressed-only searches
            localNdays = int(progressedRate);
            localNsecs = (progressedRate - double(localNdays)) * 24. * 60. * 60.;
        }

        state.nd = state.nd.addDays(-localNdays).addSecs(-localNsecs);

        // Separate working set into transit and progressed/natal positions
        PlanetSet transitPositions, progressedNatalPositions;
        for (const auto& pmid : ws) {
            auto mode = pmid.mode();
            if (mode == plmTransit) {
                transitPositions.insert(pmid);
            } else if (mode == plmProgressed || mode == plmNatal) {
                progressedNatalPositions.insert(pmid);
            }
        }

        auto pjd = getJulianDate(state.nd);

        // Selectively update positions based on iteration count
        PlanetProfile* wp = nullptr;
        bool shouldUpdateProgressed = (iterationCount % progressedUpdateFrequency == 0);

        if (shouldUpdateProgressed) {
            qDebug() << "PERF: findPriorStarts loop - state.work has"
                     << state.work.size() << "harmonics, state.tinOrb has"
                     << state.tinOrb.size() << "items, ws has" << ws.size()
                     << "planets";

            qDebug() << "PERF: Skipping progressed/natal position updates this "
                        "iteration";
            qDebug() << "PERF:" << dtToString(state.nd)
                     << "findPriorStarts creating profile from ws with"
                     << ws.size() << "planets:" << ws.names();
        }
        if (transitPositions.empty() || shouldUpdateProgressed) {
            // Update all positions
            wp = _alist.profile(ws);
            if (!wp) {
                qDebug() << "ERROR: _alist.profile(ws) returned nullptr!";
                break;
            }
            //qDebug() << "PERF" << dtToString(state.nd) << "Created profile with"
            //         << wp->size() << "planets, updating all positions to jd=" << pjd;

            // If profile is empty, the planets in ws don't exist in _alist
            // This means we can't compute their positions, so we should clear state.work and state.tinOrb
            if (wp->size() == 0) {
                qDebug()
                    << "PERF: Profile is empty - planets in ws don't exist in "
                       "_alist. Clearing state.work and state.tinOrb";
                state.work.clear();
                state.tinOrb.clear();
                delete wp;
                break;
            }

            (*wp)(pjd);
        } else {
            // Only update transit positions, reuse progressed/natal from previous iteration
            wp = _alist.profile(ws);
            for (auto loc : *wp) {
                auto ploc = dynamic_cast<PlanetLoc*>(loc);
                if (ploc && transitPositions.count(ploc->planetModeId()) > 0) {
                    (*ploc)(pjd, 1);
                }
                // Skip progressed/natal position updates
            }
        }

        ++iterationCount;

        for (auto hit = state.tinOrb.begin(); hit != state.tinOrb.end();) {
            const auto& hps = hit->first;
            const auto& ps  = hps.planets;
            auto        hwp = wp->profile(ps);
            auto        orb = computeSpread(hps.harmonic, *hwp);
            delete hwp;
            if (std::abs(orb) > planetPairOrb) {
                state.inOrb[hps] = hit->second;
                // if (!s_quiet)
                qDebug() << QString("Found H%1 start of %2 at %3 with orb %4")
                                .arg(hps.harmonic)
                                .arg(ps.names().join("="))
                                .arg(dtToString(state.nd))
                                .arg(orb)
                                .toStdString()
                                .c_str();
                state.tinOrb.erase(hit++);
            } else {
                if (!st_quiet)
                    qDebug()
                        << QString("Still looking for H%1 start"
                                   " of %2 "
                                   "at "
                                   "%3 "
                                   "with orb %4")
                               .arg(hps.harmonic)
                               .arg(ps.names().join("="))
                               .arg(dtToString(state.nd))
                               .arg(orb)
                               .toStdString()
                               .c_str();
                hit->second.range.first = pjd; // update range start
                ++hit;
            }
        }

        for (auto hit = state.work.begin(); hit != state.work.end();) {
            auto  h   = hit->first;
            auto& pso = hit->second;
            for (auto spit = pso.begin(); spit != pso.end();) {
                const auto& ps     = spit->first;
                auto        orbWas = spit->second;
                auto        hwp    = wp->profile(ps);
                auto        orb    = computeSpread(h, *hwp);
                delete hwp;
                if (orb > patternsSpreadOrb) {
                    state.starts[h].emplace(ps, ClusterOrbWhen(orb, pjd));
                    qDebug() << QString("Found H%1 prior start of %2 "
                                        "with %3 "
                                        "s"
                                        " "
                                        "at %4")
                                    .arg(h)
                                    .arg(ps.names().join("="))
                                    .arg(orbWas)
                                    .arg(dtToString(state.nd))
                                    .toStdString()
                                    .c_str();
                    pso.erase(spit++);
                } else {
                    if (orb < spit->second.orb) {
                        spit->second = { orb, pjd };
                    }
                    ++spit;
                }
            }
            if (pso.empty()) state.work.erase(hit++);
            else
                ++hit;
        }

        // Backward search for exact patterns
        for (auto ewit = state.exactWork.begin();
             ewit != state.exactWork.end();) {
            unsigned specIdx = ewit->first.first;
            unsigned h       = ewit->first.second;
            if (specIdx >= _exactPatterns.size()) {
                state.exactWork.erase(ewit++);
                continue;
            }
            const auto& spec = _exactPatterns[specIdx];
            // Build subset profile for this exact pattern
            PlanetProfile eprof;
            for (unsigned idx : spec.alistIndices) {
                if (auto ploc = dynamic_cast<PlanetLoc*>(_alist[idx]))
                    eprof.emplace_back(ploc->clone());
            }
            auto orb = computeSpread(h, pjd, eprof, _ids);
            if (orb > spec.effectiveOrb(patternsSpreadOrb)) {
                // Found prior start
                state.exactStarts[ewit->first] =
                    ClusterOrbWhen(orb, pjd);
                qDebug() << QString("[EXACT] Found H%1 spec#%2 prior start "
                                    "with spread %3 at %4")
                                .arg(h)
                                .arg(specIdx)
                                .arg(orb)
                                .arg(dtToString(state.nd));
                state.exactWork.erase(ewit++);
            } else {
                if (orb < ewit->second.orb) {
                    ewit->second = { orb, pjd };
                }
                ++ewit;
            }
        }

        if (wp) delete wp;
    }
}

void
AspectFinder::findNewStarts(AspectSearchState&              state,
                            bool                            collectingStrays,
                            std::unique_ptr<PlanetProfile>& useProf)
{
    if (!state.showPatterns) return;

    if (collectingStrays) {
        qDebug() << "PERF: findNewStarts called in collectingStrays mode";
    }

    // 1. Patterns
    for (state.h = state.maxH; state.h >= 1; --state.h) {
        bool unsel = state.hs.count(state.h) == 0;
        if (unsel /*&& !_filterLowerUnselectedHarmonics*/) continue;
        if (collectingStrays) {
            bool any = false;
            auto hit = state.starts.find(state.h);
            if (hit != state.starts.end()) {
                if (hit->second.empty()) state.starts.erase(hit);
                else
                    any = true;
            }
            if (!any) continue;
        }
        PlanetClusterMap hpc;
        if (!collectingStrays && _generalClustersEnabled) {
            PlanetProfile* prof = &state.b;
            if (useProf.get()) prof = useProf.get();
            // Build exclusion set: natal-only patterns AND progressed planets
            // (we don't support progressed aspect patterns yet)
            PlanetSet excludeFromPatterns = !showTransitNatalAspectPatterns() ? state.nats : PlanetSet {};
            excludeFromPatterns.insert(state.progs.begin(), state.progs.end());
            hpc = findClusters(state.h,
                               *prof,
                               patternsQuorum,
                               excludeFromPatterns,
                               state.skipAllNatalOnly,
                               patternsRestrictMoon,
                               patternsSpreadOrb,
                               /*excludeProgressed=*/true);
        }

        std::list<PlanetClusterMap::iterator> doomed;
        for (auto sit = state.starts[state.h].begin();
             sit != state.starts[state.h].end();)
        {
            const auto& ps   = sit->first;
            auto        prof = state.b.profile(ps);
            auto        hpcit = hpc.find(ps);
            if (hpcit != hpc.end()) {
                delete prof;
                hpc.erase(hpcit); // already have a start time
                ++sit;
                continue;
            }
            auto spread = computeSpread(state.h, *prof);
            if (spread <= patternsSpreadOrb) {
                if (collectingStrays && !st_quiet) {
                    qDebug() << QString("H%1 %2 with %3 spread at %4")
                                    .arg(state.h)
                                    .arg(ps.names().join("="))
                                    .arg(spread)
                                    .arg(dtToString(
                                        dateTimeFromJulian(state.jd)))
                                    .toStdString()
                                    .c_str();
                }
                // still good so keep
                delete prof;
                ++sit;
                continue;
            }

            double from = sit->second.when;
            double to   = state.jd;

            bool cancel = (from == 0);
            if (cancel) {
                qDebug() << QString("H%1").arg(state.h) << ps.names()
                         << "was previously hijacked, so skipping";
            } else {
                cancel = skippablePeriod({ from, to });
                qDebug() << QString("H%1").arg(state.h) << ps.names() << "from"
                         << dtToString(dateTimeFromJulian(from)) << "to"
                         << dtToString(state.nd)
                         << std::string(cancel ? "** too short **" : "");
            }

            unsigned useH = state.h;
            if (!cancel && state.h > 1) {
                // look for a pending pattern at a lower harmonic
                // because it's likely to be the same pattern but
                // now we have a tighter bounds. when we find this,
                // we set the start time to 0 to indicate that it
                // is no longer active.
                unsigned prev = 0;
                for (unsigned f : getAllFactors(state.h)) {
                    if (f == state.h) break;
                    if (prev == f) continue;
                    auto stit = state.starts.find(f);
                    if (stit == state.starts.end()) continue;
                    auto& startf = stit->second;
                    auto  lwrit  = startf.find(ps);
                    if (lwrit != startf.end()) {
                        cancel = true;
                        qDebug() << QString("H%1").arg(f) << ps.names()
                                 << "exists, so skipping"
                                 << QString("H%1").arg(state.h);
                        break;
                    }
                    prev = f;
                }
            }
            if (cancel) {
                delete prof;
                state.starts[state.h].erase(sit++);
                continue; // skipped!
            }

            static size_t clearing = 0;
            clearing++;
            qDebug() << clearing
                     << QString(
                            "Launching H%1 search for %2 with %3 spread at %4")
                            .arg(state.h)
                            .arg(ps.names().join("="))
                            .arg(spread)
                            .arg(dtToString(dateTimeFromJulian(state.jd)))
                            .toStdString()
                            .c_str();
            doomed.emplace_back(sit++);
            // starts[h].erase(sit++);

            ADateTimeRange range(dateTimeFromJulian(from),
                                 dateTimeFromJulian(state.jd));
            EventType      et = ps.heterogeneous()
                                    ? etcTransitNatalAspectPattern
                                    : etcTransitAspectPattern;
            auto& ev = _evs.safe_emplace_back(range, et, useH, PlanetSet(ps));
#if 1
            _tp->start(
                [this, prof, from, to, ps, useH, h = state.h, range, &ev] {
                    startTask();
#endif
                    auto csprd = [prof, h, this](double jd) {
                        if (_state == cancelRequestedState) throw int(1);
                        auto val = computeSpread(h, jd, *prof, _ids);
                        // qDebug() << dtToString(dateTimeFromJulian(jd)) <<
                        // ps.names() << val;
                        return val;
                    };
                    constexpr auto tol =
                        double(std::numeric_limits<float>::epsilon());

                    double   jd;
                    double   m  = 1000;
                    double   a  = from;
                    double   b  = to;
                    double   res;
                    unsigned it = 0;
                    try {
                        do {
                            double mid = a / 2. + b / 2.;
                            ++it;
                            res = brentGlobalMin(
                                csprd, a, b, mid, m, .0000001, tol, jd);
                            if (jd == a || jd == b) {
                                qDebug()
                                    << "Dubious result" << res
                                    << ((jd == b)
                                            ? "jd == to"
                                            : ((jd == a) ? "jd == from" : ""))
                                    << "for" << QString("H%1").arg(useH)
                                    << ps.names()
                                    << QList<qreal> { csprd(a),
                                                      csprd(mid),
                                                      csprd(b) };
                                if (it > 2) break;
                                m *= 10;
                                auto q = (mid - a) / 4.;
                                if (jd == b) a = b - q, b += q;
                                else
                                    b = a + q, a -= q;
                            } else
                                break;
                        } while (it < 3);

                        QMutexLocker ml(_evs.mutex());
                        auto         qdt = dateTimeFromJulian(jd);
                        ev.reset(qdt, res);
                    }
                    catch (int) {
                    }
                    delete prof;
#if 1
                    endTask();
                });
#endif
            // TODO keep track of this start and end and then search
        }

        // now hpc should only contain entries that are not in starts[h]
        // and starts[h] will contain entries that *were* initially in
        // hpc, so we need to add the remainder of hpc to starts[h] with
        // the new date.
        for (const auto& cl : hpc) {
            bool        add  = true;
            unsigned    prev = 0;
            const auto& ps(cl.first);
            for (unsigned f : getAllFactors(state.h)) {
                if (f == state.h) break;
                if (prev == f) continue;
                auto stit = state.starts.find(f);
                if (stit == state.starts.end()) continue;
                const auto& startf = stit->second;
                auto        lwrit  = startf.find(ps);
                if (lwrit != startf.end()) {
                    add = false;
                    break;
                }
            }
            if (add) {
                qDebug() << QString("Found H%1 start of %2 "
                                    "with %3 spread at %4")
                                .arg(state.h)
                                .arg(ps.names().join("="))
                                .arg(cl.second)
                                .arg(dtToString(dateTimeFromJulian(state.jd)))
                                .toStdString()
                                .c_str();
                state.starts[state.h].emplace(ps,
                                              ClusterOrbWhen(cl.second.orb,
                                                             state.pjd));
            }
        }
        for (const auto& it : doomed) state.starts[state.h].erase(it);
    }
}

void
AspectFinder::findExactPatterns(AspectSearchState& state)
{
    if (_exactPatterns.empty()) return;

    for (unsigned specIdx = 0; specIdx < _exactPatterns.size(); ++specIdx) {
        const auto& spec = _exactPatterns[specIdx];

        // Build a PlanetProfile subset for this exact pattern
        PlanetProfile prof;
        for (unsigned idx : spec.alistIndices) {
            if (idx < state.b.size())
                prof.emplace_back(state.b[idx]->clone());
        }
        if (prof.size() < spec.quorum()) continue;

        // Check each harmonic in the pattern's hset, highest first
        // so that lower harmonics get priority (same order as findNewStarts)
        const auto& hset = _hsets.at(spec.hsid);
        for (auto hit = hset.rbegin(); hit != hset.rend(); ++hit) {
            unsigned h   = *hit;
            auto     key = std::make_pair(specIdx, h);
            auto     spread = computeSpread(h, prof);

            bool inOrb = (spread <= spec.effectiveOrb(patternsSpreadOrb));
            auto sit   = state.exactStarts.find(key);
            bool wasIn = (sit != state.exactStarts.end());

            if (inOrb && !wasIn) {
                // Pattern just entered orb — record start
                state.exactStarts[key] = ClusterOrbWhen(spread, state.pjd);
                qDebug() << QString("[EXACT] H%1 spec#%2 entered orb "
                                    "with spread %3 at %4")
                                .arg(h)
                                .arg(specIdx)
                                .arg(spread)
                                .arg(dtToString(dateTimeFromJulian(state.jd)));
            } else if (inOrb && wasIn) {
                // Still in orb — update if tighter
                if (spread < sit->second.orb) {
                    sit->second.orb = spread;
                }
            } else if (!inOrb && wasIn) {
                // Pattern just left orb — check harmonic dedup before launching
                double from = sit->second.when;
                double to   = state.jd;

                bool cancel = (from == 0) || skippablePeriod({ from, to });

                // Harmonic dedup: skip if a lower factor harmonic still has
                // a pending start for this same spec (it will produce a
                // tighter result)
                if (!cancel && h > 1) {
                    for (auto fit = hset.begin(); *fit < h; ++fit) {
                        unsigned f    = *fit;
                        auto     fkey = std::make_pair(specIdx, f);
                        if (state.exactStarts.count(fkey)) {
                            cancel = true;
                            qDebug() << QString("[EXACT] H%1 spec#%2 "
                                                "skipped — H%3 pending")
                                            .arg(h).arg(specIdx).arg(f);
                            break;
                        }
                    }
                }

                if (!cancel) {
                    qDebug() << QString("[EXACT] H%1 spec#%2 left orb "
                                        "with spread %3 at %4, "
                                        "launching search from %5")
                                    .arg(h)
                                    .arg(specIdx)
                                    .arg(spread)
                                    .arg(dtToString(dateTimeFromJulian(state.jd)))
                                    .arg(dtToString(dateTimeFromJulian(from)));

                    ADateTimeRange range(dateTimeFromJulian(from),
                                         dateTimeFromJulian(to));
                    unsigned useH = h;
                    auto& ev = _evs.safe_emplace_back(range, spec.et, useH,
                                                      PlanetSet(spec.bodies));

                    // Build a fresh profile for the search lambda (owned by lambda)
                    auto* searchProf = new PlanetProfile;
                    for (unsigned idx : spec.alistIndices) {
                        if (idx < _alist.size())
                            searchProf->emplace_back(_alist[idx]->clone());
                    }

                    _tp->start(
                        [this, searchProf, from, to, useH, h, &ev] {
                            startTask();
                            auto csprd = [searchProf, h, this](double jd) {
                                if (_state == cancelRequestedState)
                                    throw int(1);
                                return computeSpread(h, jd, *searchProf, _ids);
                            };
                            constexpr auto tol =
                                double(std::numeric_limits<float>::epsilon());

                            double   jd;
                            double   m  = 1000;
                            double   a  = from;
                            double   b  = to;
                            double   res;
                            unsigned it = 0;
                            try {
                                do {
                                    double mid = a / 2. + b / 2.;
                                    ++it;
                                    res = brentGlobalMin(
                                        csprd, a, b, mid, m, .0000001, tol, jd);
                                    if (jd == a || jd == b) {
                                        if (it > 2) break;
                                        m *= 10;
                                        auto q = (mid - a) / 4.;
                                        if (jd == b) a = b - q, b += q;
                                        else
                                            b = a + q, a -= q;
                                    } else
                                        break;
                                } while (it < 3);

                                QMutexLocker ml(_evs.mutex());
                                auto         qdt = dateTimeFromJulian(jd);
                                ev.reset(qdt, res);
                            }
                            catch (int) {
                            }
                            delete searchProf;
                            endTask();
                        });
                }
                state.exactStarts.erase(sit);
            }
        }
    }
}

void
AspectFinder::findTransitPairs(AspectSearchState& state)
{
    if (!includeTransitRange) return;

    qreal ad, asp;
    qreal bd, bsp;

    unsigned i, j;

    static int totalPairsChecked = 0;
    static int totalInOrbFound = 0;
    int localPairsChecked = 0;
    int localInOrbFound = 0;

    auto stuff = _staff;
    for (state.h = 1; state.h <= state.maxH; ++state.h) {
        for (auto it = stuff.begin(); it != stuff.end();) {
            bool unsel = state.hs.count(state.h) == 0;
            if ((unsel && !filterLowerUnselectedHarmonics)
                || (_hsets[it->hsid].count(state.h) == 0)
                || it->et == etcSignIngress || it->et == etcHouseIngress
                || it->et == etcTransitToStation)
            {
                ++it;
                continue;
            }
            std::tie(i, j) = it->planetPair;
            localPairsChecked++;
            std::tie(bd, bsp) =
                PlanetProfile::computeDelta(state.b[i], state.b[j], state.h);

            auto bi   = dynamic_cast<PlanetLoc*>(state.b[i]);
            auto bj   = dynamic_cast<PlanetLoc*>(state.b[j]);
            auto good = bi && bj && (bi->aspectable() || bj->aspectable());

            bool isInOrb = false;
            if (good) {
                HarmonicPlanetSet hij { state.h, { bi->planetModeId(), bj->planetModeId() }, it->et };

                auto hasit = state.inOrb.find(hij);
                isInOrb    = std::abs(bd) <= planetPairOrb;
#if 0
                            if (isInOrb)
                            qDebug() << QString("Found H%1 orb %2 of %3 at %4")
                                        .arg(state.h).arg(std::abs(bd))
                                        .arg(hij.second.names().join("="))
                                        .arg(dtToString(state.d))
                                        .toStdString().c_str();
#endif
                if (hasit == state.inOrb.end() && isInOrb) {
                    // if (!st_quiet)
                    qDebug() << QString("Found H%1 start of %2 at %3")
                                    .arg(state.h)
                                    .arg(hij.planets.names().join("="))
                                    .arg(dtToString(state.d))
                                    .toStdString()
                                    .c_str();
                    state.inOrb[hij] = { state.pjd, 0 };
                    isInOrb          = true;
                } else if (hasit != state.inOrb.end() && !isInOrb) {
                    hasit->second.range.second = state.jd;
                    if (hasit->second.tasks.empty()) {
                        state.proximityLog[hasit->first].emplace(hasit->second,
                                                                  0);
                    } else if (!skippable(hasit->second.range, it->et)) {
                        for (auto r : hasit->second.tasks) {
                            r->setInOrbRange(hasit->second.range);
                            _tp->start(r);
                        }
                    } else {
                        for (auto r : hasit->second.tasks) {
                            delete r;
                        }
                        hasit->second.tasks.clear();
                    }
                    // if (!st_quiet)
                    qDebug() << QString("Found H%1 range of %2 "
                                        "at "
                                        "%3 "
                                        "to "
                                        "%4")
                                    .arg(state.h)
                                    .arg(hij.planets.names().join("="))
                                    .arg(dtToString(dateTimeFromJulian(
                                        hasit->second.range.first)))
                                    .arg(dtToString(state.d))
                                    .toStdString()
                                    .c_str();
                    state.inOrb.erase(hasit);
                }
            }
            if (isInOrb) {
                localInOrbFound++;
                stuff.erase(it++);
            } else
                ++it;
        }
    }

    totalPairsChecked += localPairsChecked;
    totalInOrbFound += localInOrbFound;

    static int callCount = 0;
    if (++callCount % 50 == 0) {
        qDebug() << "PERF: findTransitPairs (last 50 calls): pairs checked=" << totalPairsChecked
                 << "in-orb found=" << totalInOrbFound
                 << "yield=" << (totalPairsChecked > 0 ? (100.0 * totalInOrbFound / totalPairsChecked) : 0) << "%";
        totalPairsChecked = totalInOrbFound = 0;
    }
}

void
AspectFinder::findAspects(AspectSearchState& state, modalize<bool>& mum)
{
    qreal ad, asp;
    qreal bd, bsp;

    unsigned i, j;
    QString  wha_;

    static int totalPairsChecked = 0;
    static int totalAspectsEnqueued = 0;
    static int totalSkippedUnselected = 0;
    static int totalSkippedSameSign = 0;
    static int totalSkippedInOrb = 0;
    int localPairsChecked = 0;
    int localEnqueued = 0;
    int localSkippedUnsel = 0;
    int localSkippedSign = 0;
    int localSkippedInOrb = 0;

    auto     what = [&] {
        if (wha_.isEmpty()) {
            wha_ = QString("H%1 %2=%3")
                       .arg(state.h)
                       .arg(_alist[i]->description())
                       .arg(_alist[j]->description());
        }
        return wha_.toStdString();
    };

    auto stuff = _staff;

    for (state.h = 1; state.h <= state.maxH; ++state.h) {
        bool unsel = state.hs.count(state.h) == 0;
        if (unsel && !filterLowerUnselectedHarmonics) continue;

        for (auto it = stuff.begin(); it != stuff.end();) {
            wha_.clear();
            localPairsChecked++;
            std::tie(i, j)   = it->planetPair;
            auto        et   = it->et;
            auto        hsid = it->hsid;
            const auto& hs   = _hsets[hsid];
            auto        ha   = hs.lower_bound(state.h);
            if (ha == hs.end()) {
                // qDebug() << "Snipping" << what().c_str()
                //          << "because no further harmonics";
                stuff.erase(it++);
                continue;
            } else if (*ha != state.h) {
                // qDebug() << "Skipping" << what().c_str()
                //          << "because not selecting h" << h;
                ++it;
                continue;
            }

            if (auto known = dynamic_cast<KnownPosition*>(_alist[j])) {
                if (std::abs(known->julianDate() - state.jd)
                    > windowOf(known))
                {
                    ++it;
                    continue;
                }
            }
            qreal ispdSigned = _alist[i]->speed / 2. + state.b[i]->speed / 2.;
            qreal jspdSigned = _alist[j]->speed / 2. + state.b[j]->speed / 2.;
            qreal ispd = qAbs(ispdSigned);
            qreal jspd = qAbs(jspdSigned);
            if (ispd > jspd) {
                std::swap(i, j);
                std::swap(ispd, jspd);
                std::swap(ispdSigned, jspdSigned);
            }

            std::tie(ad, asp) =
                PlanetProfile::computeDelta(_alist[i], _alist[j], state.h);
            std::tie(bd, bsp) =
                PlanetProfile::computeDelta(state.b[i], state.b[j], state.h);
            if (sgn(ad) == sgn(bd) || (abs(ad) >= 90. || abs(bd) >= 90.)) {
                localSkippedSign++;
                if (!state.keep(i, false) || !state.keep(j, false)) {
                    stuff.erase(it++);
                } else {
                    ++it;
                }
                continue;
            }

#if 1
            auto pi = dynamic_cast<PlanetLoc*>(_alist[i]);
            if (pi && pi->allowAspects > PlanetLoc::aspOnlyConj) {
                if (pi->allowAspects
                    != ((jspdSigned < 0) ? PlanetLoc::aspOnlyRetro
                                         : PlanetLoc::aspOnlyDirect))
                {
                    qDebug() << "skipping wrong-way" << what().c_str();
                    stuff.erase(it++);
                    continue;
                }
            }
#endif

            if (true || !st_quiet) {
                QDateTime pdt(dateTimeFromJulian(state.pjd));
                QDateTime dt(dateTimeFromJulian(state.jd));
                qDebug() << what().c_str() << dtToString(pdt) << "delta" << ad
                         << asp << "vs" << dtToString(dt) << "delta" << bd
                         << bsp;
            }
            auto which = what();

            // At this point, we figure there's _alist transit.
            // If the harmonic is not on our list, let's clip the
            // stuff list so that it doesn't get recomputed at _alist
            // higher-order harmonic. This handles the case where
            // conjunction would show up on any higher harmonic
            // as an aspect at that harmonic.
            stuff.erase(it++);
            if (unsel) {
                localSkippedUnsel++;
                continue;
            }

            auto pj = dynamic_cast<PlanetLoc*>(_alist[j]);
            if (includeTransitRange) {
                auto hasit =
                    state.inOrb.find({ state.h, { pi->planetModeId(), pj->planetModeId() }, et });
                if (hasit == state.inOrb.end()) {
                    hasit = state.inOrb.find(
                        { state.h, { pj->planetModeId(), pi->planetModeId() }, et });
                }
                if (hasit != state.inOrb.end()) {
                    localSkippedInOrb++;
                    auto r = new PairAspectFinder(_alist[i],
                                                  _alist[j],
                                                  state.h,
                                                  state.pjd,
                                                  state.jd,
                                                  ad,
                                                  bd,
                                                  ispd,
                                                  jspd,
                                                  state.d,
                                                  which,
                                                  et,
                                                  mum,
                                                  this);
                    hasit->second.addTask(r);
                    continue;
                }
            }

            if (skipByDuration == SkipNone
                || (includeTransitRange
                    && (et == etcSignIngress || et == etcHouseIngress
                        || et == etcTransitToStation)))
            {
                localEnqueued++;
                // enqueue it now if we know it would not be skipped
                auto r = new PairAspectFinder(_alist[i],
                                              _alist[j],
                                              state.h,
                                              state.pjd,
                                              state.jd,
                                              ad,
                                              bd,
                                              ispd,
                                              jspd,
                                              state.d,
                                              which,
                                              et,
                                              mum,
                                              this);
                _tp->start(r);
            }
        }
    }

    totalPairsChecked += localPairsChecked;
    totalAspectsEnqueued += localEnqueued;
    totalSkippedUnselected += localSkippedUnsel;
    totalSkippedSameSign += localSkippedSign;
    totalSkippedInOrb += localSkippedInOrb;

    static int callCount = 0;
    if (++callCount % 50 == 0) {
        qDebug() << "PERF: findAspects (last 50 calls): pairs checked=" << totalPairsChecked
                 << "enqueued=" << totalAspectsEnqueued
                 << "skipped: unsel=" << totalSkippedUnselected
                 << "sign=" << totalSkippedSameSign
                 << "in-orb=" << totalSkippedInOrb
                 << "yield=" << (totalPairsChecked > 0 ? (100.0 * totalAspectsEnqueued / totalPairsChecked) : 0) << "%";
        totalPairsChecked = totalAspectsEnqueued = totalSkippedUnselected = totalSkippedSameSign = totalSkippedInOrb = 0;
    }
}

void
AspectFinder::findRemainingAspects(AspectSearchState& state)
{
    // ~.7 arcsecond (close enough to true perfection)
    constexpr double goodSeparationThreshold = 0.0003;

    //modalize<bool> mum2(st_quiet, false);

    bool any = false;
    int remainingCount = 0;
    for (auto hpsit = state.proximityLog.begin();
         _state != cancelRequestedState && hpsit != state.proximityLog.end();
         ++hpsit)
    {
        const auto& hps = hpsit->first;
        const auto& rm  = hpsit->second;
        for (auto rit = rm.begin(); rit != rm.end(); ++rit) {
            // Emit progress periodically so UI can update incrementally
            if (++remainingCount % 10 == 0) {
                emit progress(-1.0);  // negative signals "remaining aspects" phase
                QCoreApplication::processEvents();
                if (_state == cancelRequestedState) return;
            }
            const auto& r    = rit->first;
            const auto& stat = rit->second;
            if (stat) continue;
            auto   h  = hps.harmonic;
            auto&& ps = hps.planets;
            if (ps.size() != 2) {
                continue; // Only for pairs
            }
            auto desc = QString("[%1 - %2]")
                            .arg(dtToString(dateTimeFromJulian(r.first)),
                                 dtToString(dateTimeFromJulian(r.second)));
            if (skipByDuration != SkipNone) {
                double duration  = r.second - r.first;
                double threshold = 0.0;
                switch (skipByDuration) {
                case SkipLessThanDay:   threshold = 1.0; break;
                case SkipLessThanWeek:  threshold = 7.0; break;
                case SkipLessThanMonth: threshold = 30.0; break;
                default:                threshold = 0.0; break;
                }
                if (duration < threshold) {
                    if (!st_quiet) {
                        qDebug() << "Skipping inexact aspect search for pair"
                                 << ps.names().join("=") << "harmonic" << h
                                 << "duration" << duration
                                 << "days (threshold:" << threshold << ")";
                    }
                    continue;
                }
            }
            if (ps.begin()->samePlanet({ Planet_Moon })
                || ps.rbegin()->samePlanet({ Planet_Moon }))
            {
#if 0
                    qDebug() << "Skipping lunar straggler "
                             << QString("H%1 %2: %3")
                                    .arg(h)
                                    .arg(ps.names().join("="))
                                    .arg(desc)
                                    .toStdString()
                                    .c_str();
#endif
                continue;
            }

            // Find the corresponding InputData for each planet
            auto     profile = _alist.profile(ps);
            unsigned iters   = 0;

            // Define the function to minimize: aspect separation at jd
            auto csprd = [&](double jd) {
                ++iters;
                return profile->computePos(jd, h);
            };

            auto cps = [&](double jd) -> std::pair<qreal, qreal> {
                auto pos = profile->computePos(jd, h);
                return { pos, profile->speed() };
            };

            // Recursive binary search to find ALL perfections in the range
            // Returns a list of narrowed brackets, each containing one
            // perfection
            struct PerfectionBracket {
                double left, right;
                bool   hasCrossing;
            };

            // Use the same time increment that found the aspect in the main
            // loop This accounts for harmonics, orb settings, and aspect types
            double minBracketSize = state.useRate;

            if (!st_quiet) {
                qDebug() << "Using minBracketSize =" << minBracketSize
                         << "days for" << ps.names().join("=") << "H" << h
                         << "(eventType:" << hps.eventType << ")";
            }

            std::function<std::vector<PerfectionBracket>(double, double, int)>
                findPerfections;
            findPerfections = [&](double left,
                                  double right,
                                  int depth) -> std::vector<PerfectionBracket> {
                constexpr int maxDepth = 15;

                if (depth >= maxDepth || (right - left) <= minBracketSize) {
                    // Base case: bracket small enough or too deep
                    // Check if there's a sign change in this bracket
                    profile->computePos(left, h);
                    qreal leftSep = PlanetProfile::computeDelta((*profile)[0],
                                                                (*profile)[1],
                                                                h)
                                        .first;
                    profile->computePos(right, h);
                    qreal rightSep = PlanetProfile::computeDelta((*profile)[0],
                                                                 (*profile)[1],
                                                                 h)
                                         .first;
                    bool hasCrossing = (sgn(leftSep) != sgn(rightSep));
                    return { PerfectionBracket { left, right, hasCrossing } };
                }

                double mid = (left + right) / 2.0;

                profile->computePos(left, h);
                qreal leftSep =
                    PlanetProfile::computeDelta((*profile)[0], (*profile)[1], h)
                        .first;

                profile->computePos(mid, h);
                qreal midSep =
                    PlanetProfile::computeDelta((*profile)[0], (*profile)[1], h)
                        .first;

                profile->computePos(right, h);
                qreal rightSep =
                    PlanetProfile::computeDelta((*profile)[0], (*profile)[1], h)
                        .first;

                if (!st_quiet && depth < 3) {
                    qDebug()
                        << "  Depth" << depth << ":"
                        << "left=" << dtToString(dateTimeFromJulian(left))
                        << "sep=" << leftSep << "|"
                        << "mid=" << dtToString(dateTimeFromJulian(mid))
                        << "sep=" << midSep << "|"
                        << "right=" << dtToString(dateTimeFromJulian(right))
                        << "sep=" << rightSep;
                }

                bool leftToMidCrossing  = (sgn(leftSep) != sgn(midSep));
                bool midToRightCrossing = (sgn(midSep) != sgn(rightSep));

                if (leftToMidCrossing && midToRightCrossing) {
                    // CASE 3: Both halves have crossings - recursively search
                    // BOTH
                    if (!st_quiet && depth < 3) {
                        qDebug() << "  -> Both halves have crossings, "
                                    "recursively searching both";
                    }
                    auto leftResults  = findPerfections(left, mid, depth + 1);
                    auto rightResults = findPerfections(mid, right, depth + 1);
                    leftResults.insert(leftResults.end(),
                                       rightResults.begin(),
                                       rightResults.end());
                    return leftResults;
                } else if (leftToMidCrossing) {
                    // CASE 2: Only left half has crossing
                    if (!st_quiet && depth < 3) {
                        qDebug()
                            << "  -> Left half has crossing, searching left";
                    }
                    return findPerfections(left, mid, depth + 1);
                } else if (midToRightCrossing) {
                    // CASE 2: Only right half has crossing
                    if (!st_quiet && depth < 3) {
                        qDebug()
                            << "  -> Right half has crossing, searching right";
                    }
                    return findPerfections(mid, right, depth + 1);
                } else {
                    // CASE 1: No crossing detected - find minimum using
                    // magnitude
                    qreal absLeft  = std::abs(leftSep);
                    qreal absMid   = std::abs(midSep);
                    qreal absRight = std::abs(rightSep);

                    if (absMid <= absLeft && absMid <= absRight) {
                        // Mid is smallest, narrow both sides toward it
                        return findPerfections(left + (mid - left) / 2.0,
                                               mid + (right - mid) / 2.0,
                                               depth + 1);
                    } else if (absLeft < absRight) {
                        return findPerfections(left, mid, depth + 1);
                    } else {
                        return findPerfections(mid, right, depth + 1);
                    }
                }
            };

            if (!st_quiet) {
                qDebug() << "Starting recursive perfection search for"
                         << ps.names().join("=") << "H" << h << "over range ["
                         << dtToString(dateTimeFromJulian(r.first)) << "to"
                         << dtToString(dateTimeFromJulian(r.second)) << "]"
                         << "(" << (r.second - r.first) << "days)";
            }

            auto perfectionBrackets = findPerfections(r.first, r.second, 0);

            if (!st_quiet) {
                qDebug() << "Found" << perfectionBrackets.size()
                         << "bracket(s) for" << ps.names().join("=")
                         << "after recursive search from"
                         << (r.second - r.first) << "days";
                for (size_t i = 0; i < perfectionBrackets.size(); ++i) {
                    const auto& bracket      = perfectionBrackets[i];
                    double      bracketDays  = bracket.right - bracket.left;
                    double      bracketHours = bracketDays * 24.0;
                    qDebug()
                        << "  Bracket" << i << ":"
                        << "[" << dtToString(dateTimeFromJulian(bracket.left))
                        << "to" << dtToString(dateTimeFromJulian(bracket.right))
                        << "]"
                        << "(" << bracketDays << "days =" << bracketHours
                        << "hours)"
                        << (bracket.hasCrossing ? "HAS crossing"
                                                : "no crossing");
                }
            }

            // Process each perfection bracket
            for (const auto& bracket : perfectionBrackets) {
                double left            = bracket.left;
                double right           = bracket.right;
                bool   foundSignChange = bracket.hasCrossing;

                constexpr int digits = std::numeric_limits<double>::digits;

                double minJD, minSep;
                bool   usedNewtonRaphson =
                    false; // Track if N-R was used for perfect aspect marking

                // If we never found a sign change (Case 1), skip Newton-Raphson
                // and go straight to Brent minimization
                if (!foundSignChange) {
                    if (!st_quiet) {
                        qDebug() << "No perfection detected (Case 1), using "
                                    "Brent minimization for"
                                 << ps.names().join("=");
                    }
                    minSep = -1; // Force Brent path
                } else {
                    // Found a crossing - try Newton-Raphson
                    try {
                        boost::uintmax_t iter = 30;

                        auto guess = (left + right)
                                     / 2.0; // Use narrowed bracket midpoint
                        minJD = newton_raphson_iterate(cps,
                                                       guess,
                                                       left,
                                                       right,
                                                       digits,
                                                       iter);

                        // Compute positions at the found minimum, then get
                        // separation
                        profile->computePos(minJD, h);
                        minSep = PlanetProfile::computeDelta(profile[0],
                                                             profile[1],
                                                             h)
                                     .first;

                        if (!st_quiet) {
                            qDebug() << "Newton-Raphson found minJD="
                                     << dtToString(dateTimeFromJulian(minJD))
                                     << "sep=" << minSep << "(" << iter
                                     << "iterations)";
                        }

                        // Check if Newton-Raphson converged to a boundary
                        constexpr double boundaryEps = 1e-6;

                        bool atBoundary =
                            (std::abs(minJD - left) < boundaryEps
                             || std::abs(minJD - right) < boundaryEps);
                        bool goodSeparation =
                            (std::abs(minSep) < goodSeparationThreshold);

                        if (atBoundary && !goodSeparation) {
                            // At boundary with poor separation - reject and use
                            // Brent
                            if (!st_quiet) {
                                qDebug()
                                    << "Newton-Raphson converged to boundary "
                                       "with poor separation"
                                    << QString("H%1 %2: %3 (sep=%4, %5 iters)")
                                           .arg(h)
                                           .arg(ps.names().join("="))
                                           .arg(desc)
                                           .arg(minSep)
                                           .arg(iter)
                                           .toStdString()
                                           .c_str();
                            }
                            minSep = -1; // Force fallback to Brent
                        } else if (goodSeparation) {
                            // Found perfection (separation close to zero)
                            usedNewtonRaphson = true;
                            if (!st_quiet) {
                                qDebug()
                                    << "Newton-Raphson found perfection"
                                    << QString("H%1 %2: %3 (sep=%4, %5 iters)")
                                           .arg(h)
                                           .arg(ps.names().join("="))
                                           .arg(desc)
                                           .arg(minSep)
                                           .arg(iter)
                                           .toStdString()
                                           .c_str();
                            }
                        } else {
                            // Not at boundary - Newton-Raphson found valid
                            // minimum (just not perfect) Keep this result as an
                            // imperfect aspect
                            usedNewtonRaphson =
                                true; // Mark as used for event creation
                            if (!st_quiet) {
                                qDebug()
                                    << "Newton-Raphson found imperfect minimum"
                                    << QString("H%1 %2: %3 (sep=%4, %5 iters)")
                                           .arg(h)
                                           .arg(ps.names().join("="))
                                           .arg(desc)
                                           .arg(minSep)
                                           .arg(iter)
                                           .toStdString()
                                           .c_str();
                            }
                            // Keep minSep and minJD - don't force Brent
                            // fallback
                        }
                    }
                    catch (...) {
                        if (!st_quiet) {
                            qDebug() << "Newton-Raphson threw exception for"
                                     << QString("H%1 %2: %3")
                                            .arg(h)
                                            .arg(ps.names().join("="))
                                            .arg(desc)
                                            .toStdString()
                                            .c_str();
                        }
                        minSep = -1; // Force brentZhangStage fallback
                    }

                    // If Newton-Raphson failed (at boundary OR exception), use
                    // brentZhangStage fallback
                    if (minSep == -1) {
                        // Use brentZhangStage (like PairAspectFinder) with
                        // endpoint values
                        profile->computePos(left, h);
                        qreal leftSep =
                            PlanetProfile::computeDelta((*profile)[0],
                                                        (*profile)[1],
                                                        h)
                                .first;
                        profile->computePos(right, h);
                        qreal rightSep =
                            PlanetProfile::computeDelta((*profile)[0],
                                                        (*profile)[1],
                                                        h)
                                .first;

                        unsigned count = 0;
                        auto     cp    = [&](double jd) {
                            ++count;
                            return profile->computePos(jd, h);
                        };

                        bool success = brentZhangStage(cp,
                                                       left,
                                                       right,
                                                       leftSep,
                                                       rightSep,
                                                       minJD);
                        if (success) {
                            profile->computePos(minJD, h);
                            minSep = PlanetProfile::computeDelta((*profile)[0],
                                                                 (*profile)[1],
                                                                 h)
                                         .first;
                            if (!st_quiet) {
                                qDebug()
                                    << "brentZhangStage found minJD="
                                    << dtToString(dateTimeFromJulian(minJD))
                                    << "sep=" << minSep << "(" << count
                                    << "iterations)";
                            }
                        } else {
                            minSep = qreal(); // Signal failure for
                                              // brentGlobalMin fallback
                        }
                    }
                }

                // No sign change was found - use brent_find_minima to find
                // closest approach
                if (!foundSignChange) {
                    std::tie(minJD, minSep) =
                        brent_find_minima(csprd, left, right, digits);
                    if (!st_quiet) {
                        qDebug()
                            << "Closest approach for"
                            << QString("H%1 %2").arg(h).arg(
                                   ps.names().join("="))
                            << "in [" << dtToString(dateTimeFromJulian(left))
                            << "-" << dtToString(dateTimeFromJulian(right))
                            << "]:" << minSep << "at"
                            << dtToString(dateTimeFromJulian(minJD))
                            << "(1 iters)";
                    }
                }
                if (minSep == qreal()) {
                    qDebug() << "Unable to find closest using brentZhangStage "
                             << QString("H%1 %2: bracket [%3 - %4] (%5 iters)")
                                    .arg(h)
                                    .arg(ps.names().join("="))
                                    .arg(dtToString(dateTimeFromJulian(left)))
                                    .arg(dtToString(dateTimeFromJulian(right)))
                                    .arg(iters)
                                    .toStdString()
                                    .c_str();
                    // Last resort: use global min with narrowed bracket
                    brentGlobalMin(csprd,
                                   left,
                                   right,
                                   (left + right) / 2,
                                   1 /*m*/,
                                   1e-7,
                                   1e-8,
                                   minJD);
                }
                if (minJD == r.first || minJD == r.second) {
                    qDebug() << "Unable to find closest (at original boundary)"
                             << QString("H%1 %2: bracket [%3 - %4] (%5 iters)")
                                    .arg(h)
                                    .arg(ps.names().join("="))
                                    .arg(dtToString(dateTimeFromJulian(left)))
                                    .arg(dtToString(dateTimeFromJulian(right)))
                                    .arg(iters)
                                    .toStdString()
                                    .c_str();
                    continue; // Skip this bracket, try next one
                }

                if (!any) {
                    qDebug() << "Ranges with no precise hits:";
                    any = true;
                }

                // Collect the planet positions at minJD
                minSep = profile->computePos(minJD, h);

                qDebug()
                    << QString(
                           "Closest approach for H%1 %2 in [%3 - %4]: " "%5 at "
                                                                        "%6 "
                                                                        "(%7 "
                                                                        "iters"
                                                                        ")")
                           .arg(h)
                           .arg(ps.names().join("="))
                           .arg(dtToString(dateTimeFromJulian(r.first)))
                           .arg(dtToString(dateTimeFromJulian(r.second)))
                           .arg(minSep)
                           .arg(dtToString(dateTimeFromJulian(minJD)))
                           .arg(iters)
                           .toStdString()
                           .c_str();

                PlanetRangeBySpeed plr;
                for (auto loc : *profile) {
                    if (auto ploc = dynamic_cast<PlanetLoc*>(loc)) {
                        plr.emplace(*ploc);
                    }
                }

                ADateTimeRange range { dateTimeFromJulian(r.first),
                                       dateTimeFromJulian(r.second) };

                QMutexLocker ml(_evs.mutex());

                bool perfect = usedNewtonRaphson
                               || (std::abs(minSep) < goodSeparationThreshold);
                auto& ev = _evs.emplace_back(dateTimeFromJulian(minJD),
                                             hps.eventType,
                                             h,
                                             std::move(plr),
                                             perfect ? qreal() : minSep);
                ev.setRange(range);
            } // end for each perfection bracket

            delete profile;
        }
    }

    for (const auto& hpc : state.starts) {
        for (const auto& pso : hpc.second) {
            if (pso.second.when == qreal()) continue;
            qDebug() << QString("Pending pattern H%1 %2 started at %3")
                            .arg(hpc.first)
                            .arg(pso.first.names().join("="))
                            .arg(
                                dtToString(dateTimeFromJulian(pso.second.when)))
                            .toStdString()
                            .c_str();
        }
    }
}

void
AspectFinder::findAspectsAndPatterns()
{
    if (_alist.empty()) return;

    AspectSearchState state;

    state.b                = PlanetProfile(_alist);
    state.skipAllNatalOnly = false;
    if (!showTransitAspectPatterns() && showTransitNatalAspectPatterns()) {
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<NatalPosition*>(pl);
            if (!pla) continue;
            state.nats.emplace(pla->planetModeId());
        }
        state.skipAllNatalOnly = true;
    } else if (showTransitAspectPatterns()
               && (!showTransitNatalAspectPatterns() || _ids.size() == 1))
    {
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<TransitPosition*>(pl);
            if (pla && pla->inMotion()) {
                state.trans.emplace(pla->planetModeId());
            }
            auto prg = dynamic_cast<ProgressedPosition*>(pl);
            if (prg && prg->inMotion()) {
                state.progs.emplace(prg->planetModeId());
            }
        }
    } else if (showTransitAspectPatterns() && showTransitNatalAspectPatterns())
    {
        for (auto&& pl : _alist) {
            auto pla = dynamic_cast<PlanetLoc*>(pl);
            if (!pla) continue;
            if (pla->inMotion()) {
                // Separate transits from progressed; ex-precessed natals
                // have inMotion()==true but mode()==plmNatal
                if (pla->mode() == plmTransit) {
                    state.trans.emplace(pla->planetModeId());
                } else if (pla->mode() == plmProgressed) {
                    state.progs.emplace(pla->planetModeId());
                } else if (pla->mode() == plmNatal) {
                    state.nats.emplace(pla->planetModeId());
                }
            } else {
                state.nats.emplace(pla->planetModeId());
            }
        }
        state.skipAllNatalOnly = true;
    }
    state.showPatterns = showTransitAspectPatterns() || !state.nats.empty()
                         || !_exactPatterns.empty();
    state.onlyProgressedAndNatal = false; // Track if only slow-moving aspects remain

    const auto& start = _range.first;
    auto        end   = _range.second;
    if (start == end) end = end.addDays(1);
    state.d = start.startOfDay().toUTC();
    state.e = end.startOfDay().toUTC();

    state.jd  = getJulianDate(state.d);
    state.bjd = state.jd;
    state.ejd = getJulianDate(state.e);
    for (auto tp : _alist) (*tp)(state.jd, 1); // the horror

    for (const auto& ij : _staff) {
        auto&& hset = _hsets.at(ij.hsid);
        state.hs.insert(hset.begin(), hset.end());
    }
    if (!_patternMode
        && (showTransitAspectPatterns() || showTransitNatalAspectPatterns()))
    {
        auto&& hset = _hsets.at(0);
        state.hs.insert(hset.begin(), hset.end());
    }
    state.maxH = 1;
    if (state.hs.empty()) {
        if (!state.trans.empty() || !state.nats.empty()) {
            for (auto&& hset : _hsets) {
                state.hs.insert(hset.begin(), hset.end());
            }
        }
    }
    if (!state.hs.empty()) {
        state.maxH = *state.hs.crbegin();
    }

    modalize<bool> mum(st_quiet, true);

    // a simplistic predicate for determining whether to prune the
    // planet pair list as the harmonics go up. We want to limit
    // the lunar aspects, and aspects to MC and Asc are somewhat
    // dubious. Could be good to make this somewhat more flexible
    // and programmable from the client. For example, if we were
    // generating stations but not transits, but did want
    // preview aspects for those stations, this could prune the list.
    // (Or maybe it's better to have a loop engine that various
    // disciplines could be applied to.)
    state.keep = [this, &state](unsigned i, bool derived = false) {
        if (derived) {
            if (auto p = dynamic_cast<PlanetLoc*>(_alist[i])) {
                if (p->planet.isMidpt()) {
                    return state.h < (derived ? 4u : 2u);
                }
            }
        }
        return keepLooking(state.h, i);
    };

    state.useRate = 1 / double(state.maxH); // XXX
    if (state.showPatterns) {
        state.useRate *= patternsSpreadOrb / 16.;
    } else if ((showTransitsToTransits() || showTransitsToNatalPlanets())
               && includeTransitRange)
    {
        // Increase sampling interval to reduce redundant checks (was /4, then *1.5, then *2.5)
        // Yield data shows 0.2% for findTransitPairs and 0.003% for findAspects
        // Need much larger interval: trying 10x multiplier to reach 1-2% yield target
        //state.useRate *= planetPairOrb * 10.0;
        state.useRate *= planetPairOrb;
    }
    if (skipByDuration == SkipLessThanMonth) {
        state.useRate *= 8;
    } else if (skipByDuration == SkipLessThanWeek) {
        state.useRate *= 2;
    } else if (skipByDuration == SkipLessThanDay) {
        // state.useRate *= 2;
    }

    state.ndays = int(state.useRate);
    state.nsecs = (state.useRate - double(state.ndays)) * 24. * 60. * 60.;

    if (!st_quiet) {
    qDebug() << "PERF: === ASPECT SEARCH DIAGNOSTICS ===";
    qDebug() << "PERF: Sampling interval: useRate=" << state.useRate << "days ("
             << state.ndays << "d" << state.nsecs << "s)";
    qDebug() << "PERF: Search features: maxH=" << state.maxH
             << "showPatterns=" << state.showPatterns
             << "includeTransitRange=" << includeTransitRange
             << "showTransitsToTransits=" << showTransitsToTransits()
             << "showTransitsToNatal=" << showTransitsToNatalPlanets();
    qDebug() << "PERF: Orb settings: patternsSpreadOrb=" << patternsSpreadOrb
             << "planetPairOrb=" << planetPairOrb;
    qDebug() << "PERF: Planet counts: total=" << _alist.size()
             << "trans=" << state.trans.size() << "nats=" << state.nats.size()
             << "progs=" << state.progs.size();
    qDebug() << "PERF: Planet pairs in _staff:" << _staff.size();
    qDebug() << "PERF: =================================";
    }

    if (state.showPatterns || includeTransitRange) {
        state.useProf = &_alist;
        state.stuff   = _staff;
        if (!state.trans.empty() && !state.skipAllNatalOnly) {
            state.doomed = std::unique_ptr<PlanetProfile>(
                _alist.profile(state.trans, state.stuff));
            state.useProf = state.doomed.get();
        }
        for (state.h = 1; state.h <= state.maxH; ++state.h) {
            bool unsel = state.hs.count(state.h) == 0;
            if (unsel /*&& !_filterLowerUnselectedHarmonics*/) continue;
            if (state.showPatterns && _generalClustersEnabled) {
                auto found = findClusters(state.h,
                                          state.jd,
                                          *state.useProf,
                                          _ids,
                                          patternsQuorum,
                                          !showTransitNatalAspectPatterns()
                                              ? state.nats
                                              : PlanetSet {},
                                          patternsRestrictMoon,
                                          patternsSpreadOrb, 
                                          /*excludeProgressed=*/true);
                if (!found.empty())
                    state.work[state.h].swap(found);
            }

            // Check if any exact patterns are already in orb at start
            for (unsigned specIdx = 0; specIdx < _exactPatterns.size(); ++specIdx) {
                const auto& spec = _exactPatterns[specIdx];
                const auto& hset = _hsets.at(spec.hsid);
                if (hset.count(state.h) == 0) continue;

                // Build a profile subset and compute positions at start jd
                PlanetProfile prof;
                for (unsigned idx : spec.alistIndices) {
                    if (idx < _alist.size())
                        prof.emplace_back(_alist[idx]->clone());
                }
                auto spread = computeSpread(state.h, state.jd, prof, _ids);
                if (spread <= spec.effectiveOrb(patternsSpreadOrb)) {
                    auto key = std::make_pair(specIdx, state.h);
                    state.exactWork[key] = ClusterOrbWhen(spread, state.jd);
                    qDebug() << QString("[EXACT] H%1 spec#%2 already in orb "
                                        "at start with spread %3")
                                    .arg(state.h)
                                    .arg(specIdx)
                                    .arg(spread);
                }
            }

            if (!includeTransitRange) continue;

            int   i, j;
            qreal bd, bsp;
            for (auto it = state.stuff.begin(); it != state.stuff.end();) {
                if (_hsets[it->hsid].count(state.h) == 0
                    || it->et == etcSignIngress || it->et == etcHouseIngress
                    || it->et == etcTransitToStation)
                {
                    ++it;
                    continue;
                }

                std::tie(i, j) = it->planetPair;
                auto bi        = dynamic_cast<PlanetLoc*>((*state.useProf)[i]);
                auto bj        = dynamic_cast<PlanetLoc*>((*state.useProf)[j]);
                if (!bi || !bj) continue;
                bool good = bi->aspectable() || bj->aspectable();
                std::tie(bd, bsp) =
                    PlanetProfile::computeDelta(bi, bj, state.h);
                if (!st_quiet)
                    qDebug() << QString("H%1 %2 at %3 with orb %4")
                                    .arg(state.h)
                                    .arg(PlanetSet({ bi->planetModeId(), bj->planetModeId() })
                                             .names()
                                             .join('='))
                                    .arg(dtToString(state.d))
                                    .arg(bd)
                                    .toStdString()
                                    .c_str();
                if (good && std::abs(bd) <= planetPairOrb) {
                    qDebug() << formatPlanetsEtc(*it, *state.useProf, _hsets)
                                    .c_str();

                    HarmonicPlanetSet hij { state.h,
                                            { bi->planetModeId(), bj->planetModeId() },
                                            it->et };
                    state.tinOrb[hij] = { state.jd, 0 };
                    // if (!s_quiet)
                    qDebug()
                        << QString(
                               "Found H%1 inital start of %2 at %3 with orb %4")
                               .arg(state.h)
                               .arg(hij.planets.names().join("="))
                               .arg(dtToString(state.d))
                               .arg(bd)
                               .toStdString()
                               .c_str();
                    state.stuff.erase(it++);
                    continue;
                }
                ++it;
            }
        }

        // Emit progress before potentially slow backward search so the UI
        // can display any events already found (e.g. stations).
        emit progress(0.0);

        findPriorStarts(state);
    }
    if (_state == cancelRequestedState) return;

    state.pjd = state.jd;
    state.ljd = state.jd;
    state.nd  = state.d.addDays(state.ndays).addSecs(state.nsecs);
    while (state.d < state.e || !state.starts.empty() || !state.inOrb.empty()
           || !state.exactStarts.empty()) {
        QCoreApplication::processEvents();

        if (_state == cancelRequestedState) break;
        if (_state == pauseRequestedState) {
            QThread::usleep(100000);
            continue;
        }

        state.jd = getJulianDate(state.nd);
        if (state.jd - state.ljd >= 5) {
            emit progress((state.ljd - state.bjd) / (state.ejd - state.bjd));
            state.ljd = state.jd;
            QCoreApplication::processEvents();
            if (_state == cancelRequestedState) break;
            if (_state == pauseRequestedState) continue;
        }

        std::unique_ptr<PlanetProfile> useProf;

        bool collectingStrays = (state.d >= state.e);
        if (collectingStrays) {
            if (!st_quiet)
                qDebug() << "PERF: collectingStrays at time" << dtToString(state.nd) << "(jd=" << state.jd << ")";
            PlanetSet ws;

            // Log state.starts details
            int totalStartsItems = 0;
            for (const auto& hpso : state.starts) {
                totalStartsItems += hpso.second.size();
                for (const auto& pso : hpso.second) {
                    ws.insert(pso.first.begin(), pso.first.end());
                }
            }

            // Log state.inOrb details
            int totalInOrbItems = state.inOrb.size();
            for (const auto& hijr : state.inOrb) {
                ws.insert(hijr.first.planets.begin(), hijr.first.planets.end());
            }

            // Include planets from pending exact-pattern starts so they
            // are not pruned out of the profile while still being tracked.
            for (const auto& [key, cow] : state.exactStarts) {
                unsigned specIdx = key.first;
                if (specIdx < _exactPatterns.size()) {
                    const auto& spec = _exactPatterns[specIdx];
                    ws.insert(spec.bodies.begin(), spec.bodies.end());
                }
            }

            if (!st_quiet) {
                qDebug() << "PERF: collectingStrays - state.starts has"
                         << state.starts.size() << "harmonics with"
                         << totalStartsItems << "total items, state.inOrb has"
                         << totalInOrbItems << "items, exactStarts has"
                         << state.exactStarts.size() << "items, ws size:" << ws.size();
                qDebug() << "PERF: state.b.size() BEFORE any changes:"
                         << state.b.size();
            }

            if (ws.empty()) break; // all done
            if (ws.size() != state.b.size()) {
                qDebug() << "Pruning profile to" << ws.names();
                auto wp = state.b.profile(ws);
                wp->swap(state.b);
                if (state.b.size() == 0 && !st_quiet) {
                    qDebug() << "PERF: state.b AFTER pruning has"
                             << state.b.size() << "planets";
                }
                delete wp;
                
                // Check if only progressed/natal planets remain (no transits)
                state.onlyProgressedAndNatal = true;
                for (const auto& pmid : ws) {
                    if (pmid.mode() == plmTransit) {
                        state.onlyProgressedAndNatal = false;
                        break;
                    }
                }
                if (!st_quiet) {
                    qDebug() << "After pruning: onlyProgressedAndNatal =" << state.onlyProgressedAndNatal;
                }
            }
        }

        QElapsedTimer loopTimer;
        loopTimer.start();

        QElapsedTimer posTimer;
        posTimer.start();
        if (collectingStrays) {
            if (!st_quiet) {
                qDebug() << "PERF: state.b.size() BEFORE position updates:"
                         << state.b.size();
                qDebug() << "PERF: Updating positions to jd=" << state.jd << "("
                         << dtToString(dateTimeFromJulian(state.jd)) << ")";
            }
            // Sample first few planets to see their positions before update
            int sampleCount = state.b.size() < 3 ? state.b.size() : 3;
            for (int i = 0; i < sampleCount; i++) {
                auto pl = dynamic_cast<PlanetLoc*>(state.b[i]);
                if (pl && !st_quiet) {
                    qDebug() << "PERF:   Before update:" << pl->description()
                             << "pos=" << pl->loc << "speed=" << pl->speed;
                }
            }
        }
        for (auto tp : state.b) (*tp)(state.jd, 1);
        qint64 posUpdateMs = posTimer.elapsed();
        if (collectingStrays) {
            if (!st_quiet) 
            qDebug() << "PERF: state.b.size() AFTER position updates:" << state.b.size();
            // Sample first few planets to see their positions after update
            int sampleCount = state.b.size() < 3 ? state.b.size() : 3;
            for (int i = 0; i < sampleCount; i++) {
                auto pl = dynamic_cast<PlanetLoc*>(state.b[i]);
                if (pl && !st_quiet) {
                    qDebug() << "PERF:   After update:" << pl->description()
                             << "pos=" << pl->loc << "speed=" << pl->speed;
                }
            }

            // NOW show orbs/spreads AFTER position updates
            int sampledCount = 0;
            for (const auto& hpso : state.starts) {
                for (const auto& pso : hpso.second) {
                    if (sampledCount < 3) {
                        if (!st_quiet)
                        qDebug() << "PERF:   Requesting profile for H" << hpso.first << "pattern:" << pso.first.names().join("=");
                        auto prof = state.b.profile(pso.first);
                        if (!st_quiet)
                        qDebug() << "PERF:   Got profile with" << prof->size() << "planets (requested" << pso.first.size() << ")";
                        auto spread = computeSpread(hpso.first, *prof);
                        if (!st_quiet)
                        qDebug() << "PERF:   H" << hpso.first << pso.first.names().join("=") << "current spread:" << spread << "(threshold:" << patternsSpreadOrb << ")";
                        delete prof;
                        sampledCount++;
                    }
                }
                if (sampledCount >= 3) break;
            }
        }

        if (!st_quiet) qDebug() << "stuff" << dtToString(state.nd);

        qint64 profileCopyMs = 0;
        if (!collectingStrays && !state.trans.empty()
            && !state.skipAllNatalOnly)
        {
            QElapsedTimer copyTimer;
            copyTimer.start();
            useProf =
                std::unique_ptr<PlanetProfile>(state.b.profile(state.trans));
            profileCopyMs = copyTimer.elapsed();
        }

        // Do all the things HERE

        QElapsedTimer detectionTimer;
        detectionTimer.start();
        if (collectingStrays && !st_quiet) {
            qDebug() << "PERF: state.b.size() BEFORE findNewStarts:" << state.b.size();
        }
        findNewStarts(state, collectingStrays, useProf);
        findExactPatterns(state);
        qint64 detectionMs = detectionTimer.elapsed();
        if (collectingStrays && !st_quiet) {
            qDebug() << "PERF: state.b.size() AFTER findNewStarts:" << state.b.size();
        }

        if (collectingStrays && !state.inOrb.empty()) {
            if (!st_quiet)
            qDebug() << "PERF: Processing state.inOrb in collectingStrays "
                        "mode, size:"
                     << state.inOrb.size();
            int removedCount = 0;
            for (auto hit = state.inOrb.begin(); hit != state.inOrb.end();) {
                const auto& hps = hit->first;
                auto        hwp = state.b.profile(hps.planets);
                auto        orb = computeSpread(hps.harmonic /*harmonic*/, *hwp);
                delete hwp;
                if (!st_quiet)
                qDebug() << "PERF:   H" << hps.harmonic
                         << hps.planets.names().join("=")
                         << "current orb:" << orb
                         << "(threshold:" << planetPairOrb << ")"
                         << (orb > planetPairOrb ? "REMOVING" : "keeping");
                if (orb > planetPairOrb) { // leaving orb
                    hit->second.range.second = state.pjd;
                    bool tooShort = skippablePeriod(hit->second.range);
                    bool any      = false;
                    for (auto r : hit->second.tasks) {
                        if (tooShort && skippableEvent(r->eventType())) {
                            delete r;
                        } else {
                            r->setInOrbRange(hit->second.range);
                            _tp->start(r);
                            any = true;
                        }
                    }
                    hit->second.tasks.clear();
                    if (!any && !tooShort) {
                        // We didn't find a pending search, and it's not too
                        // short to search for an imperfect aspect
                        state.proximityLog[hps].emplace(hit->second, 0);
                    }
                    state.inOrb.erase(hit++);
                    removedCount++;
                } else {
                    ++hit;
                }
            }
            if (!st_quiet)
            qDebug() << "PERF: Removed" << removedCount << "items from state.inOrb, remaining:" << state.inOrb.size();
        }
        qint64 aspectFindingMs = 0;
        if (!collectingStrays && !_staff.empty()) {
            QElapsedTimer aspectTimer;
            aspectTimer.start();
            findTransitPairs(state);
            findAspects(state, mum);
            aspectFindingMs = aspectTimer.elapsed();
        } // if includeTransits

        qint64 totalLoopMs = loopTimer.elapsed();

        static int iterCount = 0;
        static qint64 cumPosUpdate = 0, cumProfileCopy = 0, cumDetection = 0, cumAspectFinding = 0, cumTotal = 0;
        cumPosUpdate += posUpdateMs;
        cumProfileCopy += profileCopyMs;
        cumDetection += detectionMs;
        cumAspectFinding += aspectFindingMs;
        cumTotal += totalLoopMs;

        if (++iterCount % 10 == 0) {
            if (!st_quiet)
            qDebug() << "PERF: Loop timing (last 10 iters avg): pos update" << (cumPosUpdate/10.0) << "ms, "
                     << "profile copy" << (cumProfileCopy/10.0) << "ms, "
                     << "detection" << (cumDetection/10.0) << "ms, "
                     << "aspect finding" << (cumAspectFinding/10.0) << "ms, "
                     << "total" << (cumTotal/10.0) << "ms";
            cumPosUpdate = cumProfileCopy = cumDetection = cumAspectFinding = cumTotal = 0;
        }

        state.d  = state.nd;
        if (collectingStrays) {
            // Use time step based on planet types remaining
            if (state.onlyProgressedAndNatal) {
                // Only slow-moving progressed/natal aspects remain - use large step
                state.nd = state.d.addDays(30);
                if (!st_quiet) {
                    qDebug() << "collectingStrays: only progressed/natal aspects remain, using 30-day increment";
                }
            } else {
                // Transit aspects still present - use normal increment for accuracy
                state.nd = state.d.addDays(state.ndays).addSecs(state.nsecs);
                if (!st_quiet) {
                    qDebug() << "collectingStrays: transit aspects present, using normal increment of" << state.useRate << "days";
                }
            }
        } else {
            state.nd = state.d.addDays(state.ndays).addSecs(state.nsecs);
        }
        // nd = d.addDays(_rate);
        state.pjd = state.jd;
        if (!collectingStrays) _alist.swap(state.b);
    }

    // Diagnostic: log why the main loop exited
    if (_state == cancelRequestedState) {
        qDebug() << "[MAIN LOOP] Exited due to cancel request";
    } else {
        qDebug() << "[MAIN LOOP] Exited normally —"
                 << "date past end:" << (state.d >= state.e)
                 << "starts empty:" << state.starts.empty()
                 << "inOrb empty:" << state.inOrb.empty()
                 << "exactStarts empty:" << state.exactStarts.empty()
                 << "last date:" << dtToString(state.d)
                 << "end date:" << dtToString(state.e);
    }

    qDebug() << state.inOrb.size() << "pending pairs";
    qDebug() << state.proximityLog.size() << "completed range pairs";

    auto frameJob = [&](HarmonicEvent& e, bool prep = true) {
        if (e.planets().size() != 2) return;
        if (e.range().first != QDateTime()) return;

        HarmonicPlanetSet hps { e.harmonic(), e.planets() };
        auto              lit = state.proximityLog.find(hps);
        if (lit == state.proximityLog.end()) return;

        if (prep) prepThread();
        auto& ranges = lit->second;
        auto  jd     = getJulianDate(e.dateTime());
        auto  it     = ranges.upper_bound({ jd, jd });
        auto  rit    = std::make_reverse_iterator(it);
        while (rit != ranges.rend() && rit->first.first <= jd) {
            if (rit->first.second >= jd) {
                if (e.range() != ADateTimeRange()) {
                    e.setRange({ dateTimeFromJulian(rit->first.first),
                                 dateTimeFromJulian(rit->first.second) });
                }
                rit->second++;
                break;
            }
            ++rit;
        }
        if (prep) releaseThread();
    };

    if (_state != cancelRequestedState) {
        // get those planet pairs framed
        QMutexLocker ml(_evs.mutex()); // lock for swoosh through paired events
        for (auto& ev : _evs) {
            if (ev.eventType() != etcTransitToStation) {
                frameJob(ev, false);
            }
        }
    }

    // Process inexact aspects (those that never perfected during the main loop)
    findRemainingAspects(state);

    bool cleared = false;
    int  active(_numTasks);
    qDebug() << active << "activity/ies";
    while (!_tp->waitForDone(100)) {
        QCoreApplication::processEvents();
        if (!cleared && _state == cancelRequestedState) {
            _tp->clear();
            cleared = true;
        }
        // Emit progress so the UI can sort & display events found so far
        emit progress(-2.0);  // negative signals "waiting for pool" phase
        int now(_numTasks);
        if (now != active) {
            qDebug() << now << "activity/ies";
            active = now;
        }
    }

    if (_state != cancelRequestedState) {
        // now get remaining
        QMutexLocker ml(_evs.mutex()); // lock for swoosh through paired events
        auto         fut = QtConcurrent::map(_evs, frameJob);
        fut.waitForFinished();
    }

    qDebug() << "Done with finding aspects and patterns";
}

#if 0
void AspectFinder::run()
{
    switch (_gt) {
    case afcFindPatterns:
        findAspectsAndPatterns(); break;
    case afcFindAspects:
        if (showStations) findStations();
        findAspectsAndPatterns(); break;
    case afcFindStations:
        findStations(); break;
    case afcFindStuff:
        if (showStations) findStations();
        findAspectsAndPatterns();
        break;
    }
}
#endif

void
AspectFinder::findStuff()
{
    prepThread();

    // Use the global thread pool instead of creating a new one
    // This allows proper cleanup and avoids orphaned threads
    _tp = QThreadPool::globalInstance();

    auto threadName = QThread::currentThread()->objectName();
    auto threadPtr = QThread::currentThread();
    
    qDebug() << "========================================";
    qDebug() << "[FINDER START]" << threadPtr << threadName;
    qDebug() << "Using GLOBAL thread pool, ideal thread count" << QThread::idealThreadCount();
    qDebug() << "========================================";

    _state = runningState;
    
#if DEBUG_FINDER_THREADS
    // Pseudo-finder mode: Just run for 15 seconds checking for cancellation
    qDebug() << "[FINDER DEBUG MODE]" << threadPtr << threadName << "- Running in pseudo mode for 15 seconds";
    QElapsedTimer timer;
    timer.start();
    int iteration = 0;
    while (timer.elapsed() < 15000 && _state != cancelRequestedState) {
        QThread::msleep(500);  // Check every 500ms
        if (++iteration % 4 == 0) {  // Log every 2 seconds
            qDebug() << "[FINDER ALIVE]" << threadPtr << threadName 
                     << "- Running" << timer.elapsed() / 1000.0 << "seconds";
        }
        emit progress(timer.elapsed() / 15000.0);
    }
    if (_state == cancelRequestedState) {
        qDebug() << "[FINDER CANCELED]" << threadPtr << threadName 
                 << "- Canceled after" << timer.elapsed() / 1000.0 << "seconds";
    } else {
        qDebug() << "[FINDER COMPLETED]" << threadPtr << threadName 
                 << "- Pseudo-run finished after 15 seconds";
    }
#else
    // Normal finder mode
    // Wrap in lambda so we can conditionally establish the RAII context
    // for ex-precessed natal positions (equatorial mode).
    auto runFinder = [&]() {
        if (showStations()) findStations();
        if (_state != cancelRequestedState
            && (showParanatellonta() || showParanatellontaToNatal()))
        {
            findParans();
        }
        if (_state != cancelRequestedState) findAspectsAndPatterns();
    };

    if (_hasExprecessCtx) {
        context<ExprecessNatalEpoch> exCtx(_exprecessCtx);
        runFinder();
    } else {
        runFinder();
    }
#endif
    
    _state = idleState;

    qDebug() << "[FINDER CLEANUP]" << threadPtr << threadName << "- Waiting for thread pool...";
    _tp->waitForDone();
    qDebug() << "[FINDER CLEANUP]" << threadPtr << threadName << "- Thread pool finished";
    
    qDebug() << "========================================";
    qDebug() << "[FINDER EXIT]" << threadPtr << threadName;
    qDebug() << "========================================";

    releaseThread();

    // Exit the thread event loop to trigger finished() signal
    // This allows proper cleanup via deleteLater()
    thread()->exit(0);
}

#if 0 // has midpoints code
TransitFinder::TransitFinder(HarmonicEvents& evs,
                             const ADateRange& range,
                             const uintSSet& hs,
                             const InputData& trainp,
                             const PlanetSet& tran,
                             unsigned eventsType /*=etcTransitToTransit*/) :
    AspectFinder(evs, range, hs,
                 eventsType==etcTransitAspectPattern
                 ? afcFindPatterns : afcFindAspects)
{
    _evType = eventsType & ~etcDerivedEventMask;

    expandShowStationAspectsToNatal = false;

    _ids.push_back(trainp);
    auto& ida = _ids.back();

    _hsets.emplace_back(hs);

    int m = 0, luna = -1;
    for (const auto& cpid: tran) {
        _alist.push_back(new TransitPosition(cpid,ida));
        if (luna == -1 && cpid.planetId()==Planet_Moon) luna = m;
        else ++m;
    }
    if (includeMidpoints) {
        for (auto a = tran.begin(); a != tran.end(); ++a) {
            if (a->planetId() == Planet_Moon) continue;
            for (auto b = std::next(a); b != tran.end(); ++b) {
                A::ChartPlanetId mp(a->fileId(),a->planetId(),b->planetId());
                _alist.push_back(new TransitPosition(mp,ida));
            }
        }
    }

    for (unsigned i = 0; i+1 < tran.size(); ++i) {
        // exclude moon to midpoint transits
        unsigned n = (i == luna)? tran.size() : _alist.size();
        auto hpl = dynamic_cast<PlanetLoc*>(_alist[i])->planet.planetId();
        for (unsigned j = i+1; j < n; ++j) {
            if (j >= tran.size()) {
                auto mpl = dynamic_cast<PlanetLoc*>(_alist[j])->planet;
                if (mpl.contains(hpl)) continue;
            }
            _staff.push_back( {{ i, j }, 0/*all*/} );
        }
    }

    //computeTransits(hs, prof, staff, range, ida, ida, ev);
}

NatalTransitFinder::NatalTransitFinder(HarmonicEvents& evs,
                                       const ADateRange& range,
                                       const uintSSet& hs,
                                       const InputData& natinp,
                                       const InputData& trainp,
                                       const PlanetSet& natal,
                                       const PlanetSet& tran,
                                       unsigned eventsType /*=etcTransitToNatal*/,
                                       bool includeTransitsToTransits /*=false*/) :
    AspectFinder(evs, range, hs,
                 eventsType==etcTransitNatalAspectPattern
                 ? afcFindPatterns : afcFindAspects)
{
    _evType = eventsType & ~etcDerivedEventMask;

    expandShowStationAspectsToNatal = true;
    expandShowStationAspectsToTransits = true;

    _ids.push_back(natinp);
    auto& ida = _ids.back();
    ida.harmonic = 1;

    _ids.push_back(trainp);
    auto& idb = _ids.back();
    idb.harmonic = 1;

    _hsets.emplace_back(hs);

    unsigned natalSize = natal.size();
    unsigned tranSize = tran.size();

    for (const auto& cpid: natal) {
        _alist.push_back(new NatalPosition(cpid, ida, "n"));
    }
    if (includeMidpoints) {
        for (auto a = natal.begin(); a != natal.end(); ++a) {
            //if (a->planetId() == Planet_Moon) continue;
            for (auto b = std::next(a); b != natal.end(); ++b) {
                A::ChartPlanetId mp(a->fileId(),a->planetId(), b->planetId());
                _alist.push_back(new NatalPosition(mp, ida, "n"));
                ++natalSize;
            }
        }
    }

    int m = 0, luna = -1;
    for (const auto& cpid: tran) {
        _alist.push_back(new TransitPosition(cpid, idb));
        if (luna == -1 && cpid.planetId()==Planet_Moon) luna = m;
        else ++m;
    }

    if (includeMidpoints) {
        unsigned i = _alist.size();
        for (auto a = tran.begin(); a != tran.end(); ++a) {
            if (a->planetId() == Planet_Moon) continue;
            for (auto b = std::next(a); b != tran.end(); ++b) {
                A::ChartPlanetId mp(a->fileId(),a->planetId(), b->planetId());
                _alist.push_back(new TransitPosition(mp,idb));
            }
        }
    }

    for (unsigned i = 0; i < tran.size(); ++i) {
        // exclude moon to midpoint natals
        unsigned n = (i == luna)? natal.size() : natalSize;
        for (unsigned j = 0; j < n; ++j) {
            _staff.push_back( {{ i + natalSize, j }, 0} );
        }
    }

    if (includeTransitsToTransits) {
        for (unsigned i = 0; i+1 < tran.size(); ++i) {
            // exclude moon to midpoint natals
            unsigned n = (i == luna)? tran.size() : tranSize;
            auto hpl = dynamic_cast<PlanetLoc*>(_alist[i])->planet.planetId();
            for (unsigned j = i+1; j < n; ++j) {
                if (auto mpl = dynamic_cast<TransitPosition*>(_alist[j])->planet) {
                    if (mpl.contains(hpl)) continue;
                }
                _staff.push_back( {{ i + natalSize, j + natalSize }, 0} );
            }
        }
    }

    //computeTransits(hs, _alist, staff, range, ida, idb, evs);
}
#endif

EventTypeManager::EventTypeManager()
{
    static std::vector<
        std::tuple<unsigned, unsigned char, const char*, const char*>>
        init {
            { etcUnknownEvent, 0, "?", "unknown" },
            { etcStation, 1, "S", "Stations" },
            { etcTransitToStation, 1, "T=S", "Transits to Station" },
            { etcTransitToTransit, 1, "T=T", "Transits to Transit" },
            { etcTransitToNatal, 2, "T=N", "Transits to Natal" },
            { etcTransitToNatalAngles, 2, "T=NA", "Transits to Natal Angles" },
            { etcOuterTransitToNatal, 2, "OT=N", "Outer Transits to Natal" },
            { etcReturn, 2, "R", "Returns" },
            { etcSolarReturn, 2, "SR", "Solar Returns" },
            { etcLunarReturn, 2, "LR", "Lunar Returns" },
            //{ etcAspectToReturn,        "R=T",  "Aspects to Return" },
            //{ etcReturnTransitToTransit,"RT=T", "Transits in Return" },
            //{ etcReturnTransitToNatal,  "RT=N", "Transits to Natal in Return"
            //},
            { etcProgressedToProgressed, 2, "P=P", "Progressed to Progressed" },
            { etcProgressedToNatal, 2, "P=N", "Progressed to Natal" },
            { etcInnerProgressedToNatal,
              2, "IP=N", "Inner Progressed to Natal" },
            { etcTransitToProgressed, 2, "T=P", "Transits to Progressed" },
            { etcSolarArcToNatal, 2, "D=N", "SA Direct to Natal" },
            { etcSignIngress, 1, "T=I", "Sign Ingresses" },
            { etcHouseIngress, 2, "T=H", "House ingresses" },
            { etcLunation, 1, "L", "Lunations" },
            { etcEclipse, 1, "E", "Eclipses" },
            { etcSolarEclipse, 1, "SE", "Solar Eclipses" },
            { etcLunarEclipse, 1, "LE", "Lunar Eclipses" },
            { etcHeliacalEvents, 1, "HRS", "Heliacal Risings/Settings" },
            { etcPrimaryDirections, 2, "PD", "Primary Directions" },
            { etcTransitAspectPattern, 1, "TA", "Transit Aspect Patterns" },
            { etcTransitNatalAspectPattern,
              2,
              "TNA",
              "Transit-Natal Aspect Patterns" },
            { etcParanatellonta, 1, "Par", "Paranatellonta" },
            { etcParanatellontaToNatal, 2, "Par=N", "Paran-Natal" }
        };

    unsigned      id;
    unsigned char chnum;
    QString       abbr, desc;
    for (const auto& tup : init) {
        std::tie(id, chnum, abbr, desc) = tup;
        _eventIdToString[id]            = { chnum, abbr, desc };
        _eventStringToId[abbr]          = id;
    }
    _numEvents = ++id;
}

EventTypeManager&
EventTypeManager::singleton()
{
    static EventTypeManager* _theMgr = new EventTypeManager();
    return *_theMgr;
}

unsigned
EventTypeManager::registerEventType(unsigned char  chnum,
                                    const QString& abbr,
                                    const QString& desc)
{
    auto& my                           = singleton();
    my._eventIdToString[my._numEvents] = { chnum, abbr, desc };
    my._eventStringToId[abbr]          = my._numEvents;
    return my._numEvents++;
}

unsigned
EventTypeManager::registerEventType(const eventTypeInfo& evtinf)
{
    return registerEventType(std::get<0>(evtinf),
                             std::get<1>(evtinf),
                             std::get<2>(evtinf));
}

// ============================================================================
// ChartPreset
// ============================================================================

QMap<EventType, ChartPreset>&
ChartPreset::presets()
{
    static QMap<EventType, ChartPreset> s_presets;
    return s_presets;
}

const ChartPreset*
ChartPreset::forEvent(EventType et)
{
    auto& p = presets();
    auto  it = p.constFind(et);
    return (it != p.constEnd()) ? &it.value() : nullptr;
}

void
ChartPreset::loadAll()
{
    CsvFile csv("astroprocessor/chart-presets.csv");
    if (!csv.openForRead()) {
        qDebug() << "ChartPreset: could not open chart-presets.csv";
        return;
    }

    auto& mgr = EventTypeManager::singleton();
    Q_UNUSED(mgr);

    while (csv.readRow()) {
        if (csv.columnsCount() < 6) continue;

        auto originBrief = csv.row(0).trimmed();
        auto originET    = EventTypeManager::briefToEventType(originBrief);
        if (originET == etcUnknownEvent) {
            qDebug() << "ChartPreset: unknown origin event type:" << originBrief;
            continue;
        }

        ChartPreset preset;

        // EnabledEvents: pipe-delimited briefs
        auto eventBriefs = csv.row(1).split('|', Qt::SkipEmptyParts);
        for (auto& brief : eventBriefs) {
            auto et = EventTypeManager::briefToEventType(brief.trimmed());
            if (et != etcUnknownEvent) preset.enabledEvents.insert(et);
        }

        // Timespan
        preset.timespan = ADateDelta::fromString(csv.row(2).trimmed());

        // StartOffset (signed, e.g. "+1d", "-7d")
        preset.startOffset = ADateDelta::fromString(csv.row(3).trimmed());

        // HarmonicFilter: comma-separated "TYPE:maxH" pairs
        auto filters = csv.row(4).split(',', Qt::SkipEmptyParts);
        for (auto& f : filters) {
            auto parts = f.trimmed().split(':');
            if (parts.size() != 2) continue;
            auto filterET = EventTypeManager::briefToEventType(parts[0].trimmed());
            bool ok       = false;
            unsigned maxH  = parts[1].trimmed().toUInt(&ok);
            if (filterET != etcUnknownEvent && ok) {
                preset.harmonicFilters[filterET] = maxH;
            }
        }

        // Pattern (optional)
        preset.pattern = csv.row(5).trimmed();

        presets()[originET] = preset;
    }
    csv.close();
    qDebug() << "ChartPreset: loaded" << presets().size() << "presets";
}

namespace
{
typedef std::pair<qreal, ChartPlanetModeId> position;
struct lessPosit {
    bool operator()(const position& a, const position& b) const
    {
        return (a.first != b.first) ? a.first < b.first : a.second < b.second;
    }
};
typedef std::set<position, lessPosit> positions;

inline bool
containsAny(const positions& pos, const PlanetSet& of)
{
    return std::any_of(pos.begin(), pos.end(), [&](const position& p) {
        return of.contains(p.second);
    });
}

inline bool
containsAnyTrans(const positions& pos)
{
    return std::any_of(pos.begin(), pos.end(), [&](const position& p) {
        return p.second.fileId() == 1 && p.second.planetId() != Planet_Moon;
    });
}

inline PlanetSet
getSet(const positions& pos)
{
    PlanetSet ret;
    for (const auto& p : pos) {
        ret.emplace(ChartPlanetModeId(p.second));
    }
    return ret;
}

inline unsigned
sizeWithoutTransitingMoon(const positions& grp, bool moonIn1)
{
    unsigned ret = 0;
    for (const auto& p : grp) {
        auto pid = p.second.planetId();
        if ((pid != Planet_Moon && pid != Planet_NorthNode
             && pid != Planet_SouthNode)
            || (p.second.fileId() != (moonIn1 ? 1 : 0)))
            ++ret;
    }
    return ret;
}

std::ostream&
operator<<(std::ostream& os, const position& pos)
{
    return os << pos.second.name().toStdString() << " " << pos.first;
}

std::ostream&
operator<<(std::ostream& os, const positions& posits)
{
    os << "(";
    std::string next;
    for (const auto& pos : posits) {
        os << next << pos;
        if (next.empty()) next = ", ";
    }
    os << ")";
    return os;
}

template <typename T>
std::string
toString(const T& t)
{
    std::stringstream sstr;
    sstr << t;
    return sstr.str();
}

} // namespace

PlanetClusterMap
findClusters(const positions& posits,
             unsigned         quorum,
             const PlanetSet& need /*={}*/,
             bool             skipAllNatalOnly = false,
             bool             restrictMoon     = true,
             qreal            maxOrb           = 8.)
{
#if 0
    qDebug() << "quorum" << quorum
             << "need" << need.names()
             << "skipAllNatalOnly" << skipAllNatalOnly
             << "restrictMoon" << restrictMoon
             << "maxOrb" << maxOrb;
    qDebug() << toString(posits).c_str();
    //if (posits.size() > 1) qDebug() << "\n";
#endif

    bool moonIn1 = skipAllNatalOnly;
    if (restrictMoon && !skipAllNatalOnly) {
        for (const auto& pos : posits) {
            const auto& cpid = pos.second;
            if (cpid.fileId() == 1 && cpid.planetId() == Planet_Moon) {
                moonIn1 = true;
                break;
            }
        }
    }
    PlanetClusterMap ret;
    for (auto it = posits.begin(); it != posits.end(); ++it) {
        positions grp;
        auto      maybeAddGroup = [&] {
            if (grp.size() < quorum) return;
            if (restrictMoon
                && sizeWithoutTransitingMoon(grp, moonIn1) < quorum)
            {
                if (!st_quiet) qDebug() << "  Not actually quorum because moon";
                return;
            }

            bool needed {}, allNatal {};
            if ((needed = !need.empty() && !containsAny(grp, need))
                || (allNatal = skipAllNatalOnly && !containsAnyTrans(grp)))
            {
                if (!st_quiet) {
                    qDebug()
                        << "  Rejected here because missing"
                        << QString(needed ? "needed" : "transit")
                        << getSet(grp).names().join("=").toStdString().c_str();
                }
                return;
            }

            auto spread = angle(grp.begin()->first, grp.rbegin()->first);
            ret[getSet(grp)] = spread;
        };

        auto e =
            posits.lower_bound(position{it->first + maxOrb, ChartPlanetModeId()});

        if (!st_quiet) {
            unsigned n = 0;
            for (auto jit = it; jit != e; ++jit) ++n;
            if (n >= quorum && !st_quiet) {
                qDebug() << "Found potential quorum" << n;
            }
        }

        for (auto jit = it; jit != e; ++jit) {
            maybeAddGroup();
            grp.emplace(*jit);
        }
        maybeAddGroup();
    }
    return ret;
}

/// Find clusters of planets in harmonic h with known positions
PlanetClusterMap
findClusters(unsigned             h,
             const PlanetProfile& plist,
             unsigned             quorum,
             const PlanetSet&     need /*={}*/,
             bool                 skipAllNatalOnly /*=false*/,
             bool                 restrictMoon /*=true*/,
             qreal                maxOrb /*=8.*/,
             bool                 excludeProgressed /*=false*/)
{
    if (!st_quiet) {
#if 1
        qDebug() << QString("Finding H%1 ").arg(h).toStdString().c_str()
                 << plist;
#else
        qDebug() << QString("Finding H%1").arg(h).toStdString().c_str();
        for (auto p : plist) {
            auto ploc = dynamic_cast<PlanetLoc*>(p);
            if (!ploc) continue;
            qDebug() << "  "
                     << (ploc->description() + (ploc->inMotion() ? "*" : ""))
                            .toStdString()
                            .c_str()
                     << harmonic(h, ploc->rasiLoc());
        }
#endif
    }
    positions posits;
    for (auto loc : plist) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planetModeId();
        if (cpid.fileId() < 0) continue;
        if (excludeProgressed && cpid.mode() == plmProgressed) continue;

        auto pid = cpid.planetId();

        if (h > 1
            && ((pid >= Houses_Start && pid < Houses_End)
                || (pid == Planet_IC || pid == Planet_Desc)))
        {
            continue;
        }
        if (h % 2 == 0 && (pid == Planet_SouthNode)) continue;

        auto hloc = h == 1 ? ploc->rasiLoc() : harmonic(h, ploc->rasiLoc());
        auto ins  = posits.emplace(hloc, ploc->planetModeId());
        if (ins.first->first > 345) {
            posits.emplace(ins.first->first - 360., ploc->planetModeId());
        } else if (ins.first->first < 15) {
            posits.emplace(ins.first->first + 360., ploc->planetModeId());
        }
    }

    auto&& ret = findClusters(posits,
                              quorum,
                              need,
                              skipAllNatalOnly,
                              restrictMoon,
                              maxOrb);
    if (ret.empty() || st_quiet) return ret;
    qDebug() << QString("Found H%1").arg(h) << "cluster map" << ret;
    return ret;
}

/// Find clusters of planets in harmonic h with computed positions
PlanetClusterMap
findClusters(unsigned                h,
             double                  jd,
             PlanetProfile&          plist,
             const QList<InputData>& ids,
             unsigned                quorum,
             const PlanetSet&        need /*={}*/,
             bool                    restrictMoon /*=true*/,
             qreal                   maxOrb /*=8.*/,
             bool                    excludeProgressed /*=false*/)
{
    if (!st_quiet) {
        qDebug() << QString("Finding H%1 with %2 ids")
                        .arg(h)
                        .arg(ids.size())
                        .toStdString()
                        .c_str()
                 << plist;
    }
    std::vector<unsigned> pfid { 0, 0, 0 };
    positions             posits;
    for (auto loc : plist) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planetModeId();
        if (excludeProgressed && cpid.mode() == plmProgressed) continue;
        if (cpid.fileId() < 0 || cpid.fileId() >= int(pfid.size())) continue;
        auto pid = cpid.planetId();
        if (h > 1
            && ((pid >= Houses_Start && pid < Houses_End)
                || (pid == Planet_IC || pid == Planet_Desc)))
        {
            continue;
        }
        if (h % 2 == 0 && (pid == Planet_SouthNode)) continue;

        ++pfid[cpid.fileId()];
        qreal pos = (ploc->inMotion() && !ids.isEmpty())
                        ? (*ploc)(jd, 1)
                        : ploc->_rasiLoc;
        auto  hloc = h == 1 ? pos : harmonic(h, pos);
        auto  ins  = posits.emplace(hloc, ploc->planetModeId());
        if (ins.first->first > 345) {
            posits.emplace(ins.first->first - 360., ploc->planetModeId());
        } else if (ins.first->first < 15) {
            posits.emplace(ins.first->first + 360., ploc->planetModeId());
        }
    }

    auto&& ret = findClusters(posits,
                              quorum,
                              need,
                              pfid[0] && pfid[1],
                              restrictMoon,
                              maxOrb);
    if (ret.empty() || st_quiet) return ret;
    qDebug() << QString("Found H%1").arg(h) << "cluster map" << ret;
    return ret;
}

HarmonicPlanetClusters
findClusters(const uintSSet&      hs,
             const PlanetProfile& prof,
             unsigned             quorum,
             const PlanetSet&     need /*={}*/,
             bool                 skipAllNatalOnly /*=false*/,
             bool                 restrictMoon /*=true*/,
             qreal                maxOrb /*=8.*/)
{
    HarmonicPlanetClusters ret;
    for (auto h : hs) {
        auto pc = findClusters(h,
                               prof,
                               quorum,
                               need,
                               skipAllNatalOnly,
                               restrictMoon,
                               maxOrb);
        if (pc.empty()) continue;

        uintSSet fac { 1 };
        getAllFactors(h, fac);
        for (unsigned oh : fac) {
            auto rit = ret.find(oh);
            if (rit == ret.end()) continue;

            const auto& retoh = rit->second;
            // clean up patterns already in lower harmonics
            for (auto pit = pc.begin(); pit != pc.end();) {
                auto retpit = retoh.find(pit->first);
                if (retpit == retoh.end()) ++pit;
                else
                    pc.erase(pit++);
            }
        }
        if (!pc.empty()) ret.emplace(h, pc);
    }
    return ret;
}

qreal
computeSpread(unsigned                h,
              double                  jd,
              PlanetProfile&          prof,
              const QList<InputData>& ids)
{
    std::vector<qreal>    sums(2, 0);
    std::vector<qreal>    locs;
    std::vector<unsigned> c(2, 0);
    for (auto loc : prof) {
        auto ploc = dynamic_cast<PlanetLoc*>(loc);
        if (!ploc) continue;

        auto cpid = ploc->planet;
        if (cpid.fileId() < 0 && prof.size() > 2) continue;

        qreal    pos;
        unsigned vroom(ploc->inMotion());
        if (vroom && !ids.isEmpty()) {
            pos = (*ploc)(jd, 1);
        } else {
            pos = ploc->_rasiLoc;
        }
        if (h > 1) pos = harmonic(h, pos);
        /*if (vroom)*/ locs.emplace_back(pos);
        sums[vroom] += pos;
        ++c[vroom];
    }

    if (c[1] == 0 /*&& !ids.empty()*/) return 0; // this is an error
#if 1
    if (c[0] != 0 /*&& !ids.empty()*/) {
#if 1
        // try to minimize distance to center of natal configuration
        locs.emplace_back(sums[0] / c[0]);
#else
        // try to minimize distance between center of moving
        // and center of natal configuration
        auto fixed = sums[0] / c[0];
        auto movie = sums[1] / c[1];
        return angle(fixed, movie);
#endif
    }
#endif
    qreal maxa = 0;
    for (unsigned i = 0, n = locs.size(); i + 1 < n; ++i) {
        for (unsigned j = i + 1; j < n; ++j) {
            auto a = angle(locs[i], locs[j]);
            if (a > maxa) maxa = a;
        }
    }
    return maxa;
}

} // namespace A
