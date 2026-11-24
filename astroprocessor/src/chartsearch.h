#ifndef A_CHARTSEARCH_H
#define A_CHARTSEARCH_H

#include "astro-data.h"
#include <QAtomicInt>
#include <QList>
#include <QString>
#include <QThread>
#include <QVector>
#include <QVector3D>

namespace A
{

/// One body the wanted chart must have at a given longitude.
struct ChartSearchTarget {
    PlanetId id  = Planet_None;
    double   lon = 0; ///< in the search zodiac, degrees 0..360
    double   orb = 1; ///< allowed deviation, degrees
    /// How closely the position was stated (half the last digit given),
    /// degrees. The orb decides whether a chart matches; this decides how
    /// well the matched moment and place are then pinned down.
    double precision = 0.5;
    /// Half-width of a band round `lon` anywhere inside which the body fits
    /// equally well: half a sign when only the sign is known, else 0. The
    /// orb then counts from the band's edges.
    double flat = 0;
};

/// What to look for: positions typed by the user, in `zodiac`.
struct ChartSearchSpec {
    QList<ChartSearchTarget> targets;

    bool   hasAsc = false, hasMC = false;
    double asc = 0, ascOrb = 1;
    double mc = 0, mcOrb = 1;
    double ascPrecision = 0.5, mcPrecision = 0.5; ///< as ChartSearchTarget
    double ascFlat = 0, mcFlat = 0;               ///< as ChartSearchTarget

    ZodiacId zodiac = Zodiac_Tropical;
    double   jdFrom = 0, jdTo = 0; ///< UT julian days
    /// How many of `targets` may be out of orb in an accepted chart.
    int allowedMisses = 0;

    /// With a known place the angles pin the time of day; without one they
    /// are solved for the place instead.
    bool      hasLocation = false;
    QVector3D location; ///< x = longitude, y = latitude (degrees)

    int maxHits = 200;

    /// Sample the ayanamsa across the range. Must run on the GUI thread
    /// before the search starts: the sidereal mode is process-global state
    /// in Swiss Ephemeris, so the worker only interpolates this table.
    void   prepare();
    double ayanamsaAt(double jdUt) const;

  private:
    double          _ayanStart = 0, _ayanStep = 0;
    QVector<double> _ayan;
};

struct ChartSearchHit {
    double jdUt = 0;         ///< best-fitting instant
    double jdLo = 0, jdHi = 0; ///< span over which the bodies stay in orb
    double score  = 0;       ///< RMS of (error / orb); 0 is a perfect fit
    int    misses = 0;       ///< bodies out of orb at jdUt
    bool   hasTime = false;  ///< false: only the date is determined
    /// Uncertainty of jdUt (days) that follows from how closely the
    /// positions were stated and how well they agree with one another.
    double timeErr = 0;

    bool   hasLmt = false;
    double lmtHours = 0;     ///< local mean time of day
    bool   hasLat = false;
    double lat = 0, latErr = 0;
    bool   hasLon = false;
    double lon = 0, lonErr = 0;

    /// Largest city consistent with lat/lon, when both are determined.
    QString place;
    QString placeTimezoneId;
    double  placeLat = 0, placeLon = 0;

    QString note;
};

/// Julian day (UT) of 1 January, 0h, of an astronomical year (0 = 1 BC),
/// in the Julian calendar before 1583.
double
julianDayForYear(int year);

/// Re-express an ecliptic longitude, as the app displays it in zodiac `from`
/// at `jdUt`, in zodiac `to`. Sets the Swiss Ephemeris sidereal mode, so
/// call it from the GUI thread only.
double
convertZodiacLongitude(double lon, ZodiacId from, ZodiacId to, double jdUt);

/// Finds the moments at which the bodies stand at the requested longitudes.
/// Bodies are swept slowest-first, each one narrowing the windows left by
/// the one before, so the fast bodies are only examined inside the rare
/// spans where the slow ones already agree.
class ChartSearcher : public QThread {
    Q_OBJECT

  public:
    explicit ChartSearcher(const ChartSearchSpec& spec, QObject* parent = nullptr);

    void cancel() { _cancel.storeRelaxed(1); }
    bool cancelled() const { return _cancel.loadRelaxed() != 0; }

    /// Valid once finished() has been emitted. Best fit first.
    const QList<ChartSearchHit>& hits() const { return _hits; }
    /// More matches existed than spec.maxHits.
    bool truncated() const { return _truncated; }

  signals:
    void progress(double fraction);

  protected:
    void run() override;

  private:
    struct Body {
        PlanetId id;
        int      sweNum;
        int      flags;
        bool     invert; ///< South Node: opposite the computed point
        double   target; ///< search-zodiac longitude
        double   orb;
        double   prec;   ///< stated precision of the target
        double   flat;   ///< see ChartSearchTarget::flat
        double   vmax;
    };
    struct Window {
        double lo, hi;
        int    misses;
    };

    ChartSearchSpec       _spec;
    QVector<Body>         _bodies; ///< slowest first
    QAtomicInt            _cancel;
    QList<ChartSearchHit> _hits;
    bool                  _truncated = false;
};

} // namespace A

#endif // A_CHARTSEARCH_H
