#include "chartsearch.h"
#include "citydb.h"
#include "csvreader.h"
#include "../swe/swephexp.h"
#include <algorithm>
#include <cmath>

namespace A
{

namespace
{

const double kSiderealDegPerDay = 360.98564736629;
const double kMinStepDays       = 1e-4;  // ~9 s
const double kCostCap           = 9.0;   // a body 3 orbs out counts no worse
const int    kMaxWindows        = 20000;

inline double sind(double x) { return std::sin(x * DEGTORAD); }
inline double cosd(double x) { return std::cos(x * DEGTORAD); }
inline double tand(double x) { return std::tan(x * DEGTORAD); }

/// Fastest geocentric motion in longitude (deg/day), with headroom. Only
/// used to size steps and to skip ahead safely, so it must never be too low.
double
maxSpeed(PlanetId id, int sweNum)
{
    switch (id) {
    case Planet_Sun:       return 1.05;
    case Planet_Moon:      return 15.6;
    case Planet_Mercury:   return 2.3;
    case Planet_Venus:     return 1.32;
    case Planet_Mars:      return 0.85;
    case Planet_Jupiter:   return 0.26;
    case Planet_Saturn:    return 0.15;
    case Planet_Uranus:    return 0.075;
    case Planet_Neptune:   return 0.05;
    case Planet_Pluto:     return 0.05;
    case Planet_NorthNode:
    case Planet_SouthNode: return sweNum == SE_MEAN_NODE ? 0.06 : 0.35;
    case Planet_Chiron:    return 0.2;
    case Planet_Ceres:
    case Planet_Pallas:
    case Planet_Juno:
    case Planet_Vesta:     return 0.7;
    default:               return 8.0; // unknown body: assume it is fast
    }
}

void
obliquity(double jdUt, double& eps, double& dpsi)
{
    double xx[6] = { 23.44, 23.44, 0, 0, 0, 0 };
    char   serr[256];
    swe_calc_ut(jdUt, SE_ECL_NUT, 0, xx, serr);
    eps  = xx[0]; // true obliquity
    dpsi = xx[2]; // nutation in longitude
}

double
ramcFromMc(double mcTrop, double eps)
{
    return swe_degnorm(atan2(sind(mcTrop) * cosd(eps), cosd(mcTrop)) * RADTODEG);
}

/// Asc and MC (tropical, of date) for a meridian and latitude.
void
anglesFor(double ramc, double lat, double eps, double& asc, double& mc)
{
    double cusps[13], ascmc[10];
    // Porphyry: the angles are the same in every quadrant system, and this
    // one cannot fail inside the polar circles.
    swe_houses_armc(ramc, lat, eps, 'O', cusps, ascmc);
    asc = ascmc[0];
    mc  = ascmc[1];
}

/// Latitude at which `ascTrop` rises while `ramc` culminates.
bool
latitudeFor(double ramc, double ascTrop, double eps, double& lat)
{
    const double ta = tand(ascTrop);
    if (std::fabs(ta) < 1e-9 || std::fabs(sind(eps)) < 1e-9) return false;
    lat = atan(-(cosd(ramc) / ta + sind(ramc) * cosd(eps)) / sind(eps)) * RADTODEG;
    if (!std::isfinite(lat) || std::fabs(lat) > 89.0) return false;
    // tan() cannot tell the Ascendant from the Descendant: keep only the
    // solution where the given degree is actually the one rising.
    double asc, mc;
    anglesFor(ramc, lat, eps, asc, mc);
    return std::fabs(swe_difdeg2n(asc, ascTrop)) < 0.05;
}

template <typename F>
double
goldenMin(F&& f, double a, double b, int iters = 40)
{
    const double g = 0.6180339887498949;
    double       c = b - g * (b - a), d = a + g * (b - a);
    double       fc = f(c), fd = f(d);
    for (int i = 0; i < iters; ++i) {
        if (fc < fd) {
            b = d; d = c; fd = fc;
            c = b - g * (b - a); fc = f(c);
        } else {
            a = c; c = d; fc = fd;
            d = a + g * (b - a); fd = f(d);
        }
    }
    return 0.5 * (a + b);
}

} // namespace

double
julianDayForYear(int year)
{
    return swe_julday(year, 1, 1, 0.0, year < 1583 ? SE_JUL_CAL : SE_GREG_CAL);
}

double
convertZodiacLongitude(double lon, ZodiacId from, ZodiacId to, double jdUt)
{
    if (from == to) return lon;
    // Offset of a zodiac's displayed longitude from the tropical, true
    // equinox of date: sidereal values are mean-equinox longitudes less the
    // mean ayanamsa, so nutation comes off as well.
    auto offset = [jdUt](ZodiacId z) {
        if (z <= 1) return 0.0;
        double eps, dpsi;
        obliquity(jdUt, eps, dpsi);
        swe_set_sid_mode(z - 2, 0, 0);
        return swe_get_ayanamsa_ut(jdUt) + dpsi;
    };
    return swe_degnorm(lon + offset(from) - offset(to));
}

/* ============================== ChartSearchSpec =========================== */

void
ChartSearchSpec::prepare()
{
    _ayan.clear();
    if (zodiac <= 1) return; // tropical: no offset

    swe_set_sid_mode(zodiac - 2, 0, 0);
    _ayanStep  = 3652.5; // 10 years; precession is smooth enough to interpolate
    _ayanStart = jdFrom - _ayanStep;
    const int n = int(std::ceil((jdTo - _ayanStart) / _ayanStep)) + 2;
    _ayan.reserve(n);
    // Mean ayanamsa (no nutation): the sidereal positions the app displays
    // are mean-equinox longitudes less this value.
    for (int i = 0; i < n; ++i)
        _ayan << swe_get_ayanamsa_ut(_ayanStart + i * _ayanStep);
}

double
ChartSearchSpec::ayanamsaAt(double jdUt) const
{
    if (_ayan.isEmpty()) return 0.0;
    double x = (jdUt - _ayanStart) / _ayanStep;
    x        = qBound(0.0, x, double(_ayan.size() - 1));
    int i    = qMin(int(x), _ayan.size() - 2);
    if (i < 0) return _ayan.first();
    const double f = x - i;
    return _ayan[i] + f * (_ayan[i + 1] - _ayan[i]);
}

/* ============================== ChartSearcher ============================= */

ChartSearcher::ChartSearcher(const ChartSearchSpec& spec, QObject* parent) :
    QThread(parent),
    _spec(spec)
{
    // Built here, on the caller's thread: the planet catalogue is not
    // safe to consult from the worker.
    const bool sidereal = spec.zodiac > 1;
    for (const ChartSearchTarget& t : spec.targets) {
        if (t.id < Planets_Start || t.id >= Planets_End) continue;
        const Planet&  p          = getPlanet(t.id);
        const unsigned invertFlag = 256 * 1024; // see calculatePlanet()
        Body           b;
        b.id     = t.id;
        b.sweNum = p.sweNum;
        b.flags  = (SEFLG_SWIEPH | p.sweFlags) & ~SEFLG_TRUEPOS & ~SEFLG_SIDEREAL;
        // Sidereal targets are compared against mean-equinox longitudes
        // (see ChartSearchSpec::prepare).
        if (sidereal) b.flags |= SEFLG_NONUT;
        b.invert = (p.sweFlags & invertFlag) != 0;
        b.target = swe_degnorm(t.lon);
        b.orb    = qMax(t.orb, 0.001);
        b.prec   = qBound(1e-5, t.precision, b.orb);
        b.flat   = qMax(0.0, t.flat);
        b.vmax   = maxSpeed(t.id, p.sweNum);
        _bodies << b;
    }
    std::sort(_bodies.begin(), _bodies.end(),
              [](const Body& a, const Body& b) { return a.vmax < b.vmax; });
}

void
ChartSearcher::run()
{
    // Swiss Ephemeris keeps its file path per thread (see
    // AspectFinder::prepThread).
    {
        QByteArray ephePath =
            CsvFile::resolvePath(QStringLiteral("swe")).toLocal8Bit();
        swe_set_ephe_path(ephePath.data());
    }

    const ChartSearchSpec& spec     = _spec;
    const bool             sidereal = spec.zodiac > 1;

    const QVector<Body>& bodies = _bodies;
    if (bodies.isEmpty() || spec.jdTo <= spec.jdFrom) return;

    auto err = [&spec](const Body& b, double jd) -> double {
        double xx[6];
        char   serr[256];
        if (swe_calc_ut(jd, b.sweNum, b.flags, xx, serr) == ERR) return 999.0;
        const double lon = b.invert ? xx[0] - 180.0 : xx[0];
        // Distance from the target, or from the edge of its band (a body
        // known only by sign fits anywhere inside the sign).
        return qMax(0.0, std::fabs(swe_difdeg2n(lon, b.target + spec.ayanamsaAt(jd)))
                             - b.flat);
    };

    const int nStages = bodies.size() + 1;

    /* ---- 1. narrow the range one body at a time, slowest first ---- */

    QVector<Window> wins { { spec.jdFrom, spec.jdTo, 0 } };

    for (int bi = 0; bi < bodies.size(); ++bi) {
        const Body&  b    = bodies[bi];
        const double step = qMax(b.orb / b.vmax, kMinStepDays);
        const double pad  = 0.5 * b.orb; // sampling slack; exact test comes later

        double total = 0, done = 0;
        for (const Window& w : wins) total += w.hi - w.lo;

        QVector<Window> next;
        for (const Window& w : wins) {
            if (cancelled()) return;

            bool   open = false;
            double a = 0, z = 0;
            auto   mark = [&](double t) {
                const double lo = qMax(w.lo, t - step), hi = qMin(w.hi, t + step);
                if (open && lo <= z) {
                    z = hi;
                } else {
                    if (open) next.append({ a, z, w.misses });
                    a = lo; z = hi; open = true;
                }
            };

            double t        = w.lo;
            bool   lastDone = false;
            int    tick     = 0;
            while (!lastDone) {
                if (t >= w.hi) {
                    t        = w.hi;
                    lastDone = true;
                }
                const double e = err(b, t);
                if (e <= b.orb + pad) {
                    mark(t);
                    t += step;
                } else {
                    // The body cannot close the gap faster than vmax, so
                    // nothing in between can be in orb.
                    t += qMax(step, (e - b.orb - pad) / b.vmax);
                }
                if ((++tick & 0x3ff) == 0) {
                    if (cancelled()) return;
                    if (total > 0)
                        emit progress((bi + (done + t - w.lo) / total) / nStages);
                }
            }
            if (open) next.append({ a, z, w.misses });

            // This body may be one of the permitted misses.
            if (w.misses < spec.allowedMisses)
                next.append({ w.lo, w.hi, w.misses + 1 });

            done += w.hi - w.lo;
        }

        // Merge overlapping windows that carry the same miss count.
        std::sort(next.begin(), next.end(), [](const Window& x, const Window& y) {
            return x.misses != y.misses ? x.misses < y.misses : x.lo < y.lo;
        });
        QVector<Window> merged;
        for (const Window& w : std::as_const(next)) {
            if (!merged.isEmpty() && merged.last().misses == w.misses
                && w.lo <= merged.last().hi)
                merged.last().hi = qMax(merged.last().hi, w.hi);
            else
                merged.append(w);
        }
        if (merged.size() > kMaxWindows) {
            merged.resize(kMaxWindows); // fewest-misses windows sort first
            _truncated = true;
        }
        wins = merged;
        emit progress(double(bi + 1) / nStages);
        if (wins.isEmpty()) return;
    }

    /* ---- 2. best instant inside each surviving window ---- */

    double fine = 1e9;
    for (const Body& b : std::as_const(bodies))
        fine = qMin(fine, 0.5 * b.orb / b.vmax);
    fine = qMax(fine, kMinStepDays);

    auto cost = [&](double jd, int* missesOut = nullptr) -> double {
        double sum = 0;
        int    m   = 0;
        for (const Body& b : std::as_const(bodies)) {
            const double r = err(b, jd) / b.orb;
            if (r > 1.0001) ++m;
            sum += qMin(r * r, kCostCap);
        }
        if (missesOut) *missesOut = m;
        return sum;
    };

    QList<ChartSearchHit> found;
    for (int wi = 0; wi < wins.size(); ++wi) {
        if (cancelled()) return;
        const Window& w = wins[wi];
        const int     n = qBound(16, int(std::ceil((w.hi - w.lo) / fine)), 600);
        const double  dt = (w.hi - w.lo) / n;

        QVector<double> c(n + 1);
        QVector<int>    miss(n + 1);
        int             best = 0;
        for (int i = 0; i <= n; ++i) {
            c[i] = cost(w.lo + i * dt, &miss[i]);
            if (c[i] < c[best]) best = i;
        }
        const double tBest = goldenMin([&](double t) { return cost(t); },
                                       qMax(w.lo, w.lo + (best - 1) * dt),
                                       qMin(w.hi, w.lo + (best + 1) * dt));
        int          m = 0;
        const double cBest = cost(tBest, &m);
        if (m > spec.allowedMisses) continue;

        int i0 = best, i1 = best;
        while (i0 > 0 && miss[i0 - 1] <= spec.allowedMisses) --i0;
        while (i1 < n && miss[i1 + 1] <= spec.allowedMisses) ++i1;

        ChartSearchHit h;
        h.jdUt    = tBest;
        h.jdLo    = qMin(tBest, w.lo + i0 * dt);
        h.jdHi    = qMax(tBest, w.lo + i1 * dt);
        h.score   = std::sqrt(cBest / bodies.size());
        h.misses  = m;

        // How well is the moment known? Not "how long do the bodies stay in
        // orb" -- the orb is only the mismatch tolerated -- but what the
        // stated precision of each position allows, each body weighing in
        // by how fast it moves: a least-squares fit of the time.
        auto residual = [&](const Body& b, double jd, double* speed) {
            double xx[6] = { 0, 0, 0, 0, 0, 0 };
            char   serr[256];
            if (swe_calc_ut(jd, b.sweNum, b.flags | SEFLG_SPEED, xx, serr) == ERR)
                return 999.0;
            if (speed) *speed = xx[3];
            const double d = swe_difdeg2n(b.invert ? xx[0] - 180.0 : xx[0],
                                          b.target + spec.ayanamsaAt(jd));
            if (b.flat <= 0) return d;
            // Inside its band a body says nothing about the moment.
            if (std::fabs(d) <= b.flat) {
                if (speed) *speed = 0;
                return 0.0;
            }
            return d > 0 ? d - b.flat : d + b.flat;
        };
        auto chi2 = [&](double jd) {
            double sum = 0;
            for (const Body& b : std::as_const(bodies)) {
                const double r = residual(b, jd, nullptr);
                if (std::fabs(r) > b.orb) continue; // a permitted miss
                sum += (r / b.prec) * (r / b.prec);
            }
            return sum;
        };
        if (h.jdHi > h.jdLo) h.jdUt = goldenMin(chi2, h.jdLo, h.jdHi);
        double weight = 0, sumSq = 0;
        int    used = 0;
        for (const Body& b : std::as_const(bodies)) {
            double       v = 0;
            const double r = residual(b, h.jdUt, &v);
            if (std::fabs(r) > b.orb) continue;
            weight += (v / b.prec) * (v / b.prec);
            sumSq  += (r / b.prec) * (r / b.prec);
            ++used;
        }
        h.timeErr = weight > 0 ? 1.0 / std::sqrt(weight) : 0.5 * (h.jdHi - h.jdLo);
        // Nothing singles out a moment (every body is anywhere in its sign):
        // the match is the whole span, reported by its middle.
        if (weight <= 0) h.jdUt = 0.5 * (h.jdLo + h.jdHi);
        // Positions that disagree by more than their stated precision are
        // evidently less exact than stated: widen accordingly.
        if (used > 1) h.timeErr *= qMax(1.0, std::sqrt(sumSq / (used - 1)));
        h.timeErr = qMin(h.timeErr, qMax(0.5 * (h.jdHi - h.jdLo), kMinStepDays));
        h.hasTime = h.timeErr < 0.5;
        found << h;

        if ((wi & 0x3f) == 0)
            emit progress((bodies.size() + double(wi) / wins.size()) / nStages);
    }

    // The same chart is reached along several miss paths: keep the best.
    auto better = [](const ChartSearchHit& a, const ChartSearchHit& b) {
        return a.misses != b.misses ? a.misses < b.misses : a.score < b.score;
    };
    std::sort(found.begin(), found.end(),
              [](const ChartSearchHit& a, const ChartSearchHit& b) {
                  return a.jdUt < b.jdUt;
              });
    QList<ChartSearchHit> unique;
    for (const ChartSearchHit& h : std::as_const(found)) {
        if (!unique.isEmpty() && h.jdLo <= unique.last().jdHi
            && h.jdUt - unique.last().jdUt < 0.25)
        {
            if (better(h, unique.last())) unique.last() = h;
        } else {
            unique << h;
        }
    }

    /* ---- 3. angles: fix the time (known place) or solve for the place ---- */

    const bool anyAngle = spec.hasAsc || spec.hasMC;
    QList<ChartSearchHit> out;

    for (const ChartSearchHit& h0 : std::as_const(unique)) {
        if (cancelled()) return;
        if (!anyAngle) {
            out << h0;
            continue;
        }

        double eps, dpsi;
        obliquity(h0.jdUt, eps, dpsi);
        // Typed angle -> tropical, true equinox of date (what the house
        // routines work in).
        auto toTrop = [&](double lon) {
            return sidereal ? swe_degnorm(lon + spec.ayanamsaAt(h0.jdUt) + dpsi)
                            : swe_degnorm(lon);
        };
        const double ascT = toTrop(spec.asc), mcT = toTrop(spec.mc);

        if (spec.hasLocation) {
            const double glon = spec.location.x(), glat = spec.location.y();
            const bool   wide = (h0.jdHi - h0.jdLo) > 3.0;
            const double lo   = wide ? h0.jdUt - 0.5 : h0.jdLo;
            const double hi   = wide ? h0.jdUt + 0.5 : h0.jdHi;

            double eAsc = 0, eMc = 0;
            auto   angCost = [&](double t) {
                const double ramc = swe_degnorm(swe_sidtime(t) * 15.0 + glon);
                double       asc, mc;
                anglesFor(ramc, glat, eps, asc, mc);
                double sum = 0;
                eAsc = eMc = 0;
                if (spec.hasAsc) {
                    eAsc = qMax(0.0, std::fabs(swe_difdeg2n(asc, ascT)) - spec.ascFlat);
                    sum += (eAsc / spec.ascOrb) * (eAsc / spec.ascOrb);
                }
                if (spec.hasMC) {
                    eMc = qMax(0.0, std::fabs(swe_difdeg2n(mc, mcT)) - spec.mcFlat);
                    sum += (eMc / spec.mcOrb) * (eMc / spec.mcOrb);
                }
                return sum;
            };
            auto angOk = [&] {
                return (!spec.hasAsc || eAsc <= spec.ascOrb)
                       && (!spec.hasMC || eMc <= spec.mcOrb);
            };

            const double    dt = 1.0 / 1440.0; // one minute
            const int       n  = qMax(2, int(std::ceil((hi - lo) / dt)));
            QVector<double> c(n + 1);
            QVector<char>   ok(n + 1);
            for (int i = 0; i <= n; ++i) {
                c[i]  = angCost(lo + i * dt);
                ok[i] = angOk();
            }

            // Candidate times of day, each with the span it stands for
            // (tLo == tHi: a single moment).
            struct Cand {
                double t, tLo, tHi;
            };
            QVector<Cand> cands;
            // Angles known only by sign leave no best moment, only the
            // stretch of an hour or two during which the sign is on the
            // angle: report its middle and its length.
            const bool spans = (!spec.hasAsc || spec.ascFlat > 0)
                               && (!spec.hasMC || spec.mcFlat > 0);
            if (spans) {
                for (int i = 0; i <= n; ++i) {
                    if (!ok[i]) continue;
                    int j = i;
                    while (j < n && ok[j + 1]) ++j;
                    cands.append({ lo + 0.5 * (i + j) * dt, lo + i * dt, lo + j * dt });
                    i = j;
                }
            } else {
                for (int i = 0; i <= n; ++i) {
                    if ((i > 0 && c[i - 1] < c[i]) || (i < n && c[i + 1] <= c[i]))
                        continue; // not a local minimum
                    const double t = goldenMin(angCost, lo + qMax(0, i - 1) * dt,
                                               lo + qMin(n, i + 1) * dt);
                    cands.append({ t, t, t });
                }
            }

            for (const Cand& cd : std::as_const(cands)) {
                const double t  = cd.t;
                const double ca = angCost(t); // also refreshes eAsc/eMc
                if (!angOk()) continue;
                int          m  = 0;
                const double cb = cost(t, &m);
                if (m > spec.allowedMisses) continue;

                const int      nAng = int(spec.hasAsc) + int(spec.hasMC);
                ChartSearchHit h    = h0;
                h.jdUt    = t;
                h.hasTime = true;
                if (cd.tHi > cd.tLo) {
                    h.jdLo    = cd.tLo;
                    h.jdHi    = cd.tHi;
                    h.timeErr = 0.5 * (cd.tHi - cd.tLo);
                } else {
                    h.jdLo = t - qMin(spec.hasMC ? spec.mcOrb : spec.ascOrb, 15.0)
                                     / kSiderealDegPerDay;
                    h.jdHi = 2 * t - h.jdLo;
                    // The angles turn a degree in four minutes: the time is
                    // as good as they were stated.
                    h.timeErr =
                        qMin(spec.hasMC ? spec.mcPrecision : spec.ascPrecision,
                             spec.hasMC ? spec.mcOrb : spec.ascOrb)
                        / kSiderealDegPerDay;
                }
                h.misses  = m;
                h.score   = std::sqrt((cb + ca) / (bodies.size() + nAng));
                h.hasLat = h.hasLon = true;
                h.lat    = glat;
                h.lon    = glon;
                h.hasLmt = true;
                if (wide)
                    h.note = tr("Date is approximate; time shown is for the "
                                "best-fitting day");
                out << h;
            }
            continue;
        }

        ChartSearchHit h = h0;
        if (spec.hasMC) {
            const double ramc = ramcFromMc(mcT, eps);
            h.lon    = swe_difdeg2n(ramc, swe_sidtime(h.jdUt) * 15.0);
            const double mcPrec  = qMax(qMin(spec.mcPrecision, spec.mcOrb), spec.mcFlat);
            const double ascPrec = qMax(qMin(spec.ascPrecision, spec.ascOrb), spec.ascFlat);
            // Longitude = local sidereal time (from the MC) less Greenwich
            // sidereal time (from the UT the bodies give). The Earth turns
            // 27 times faster than the Moon moves, so the UT term dominates.
            h.lonErr = std::hypot(h.timeErr * kSiderealDegPerDay, mcPrec);
            h.hasLon = h.hasTime && h.lonErr < 90.0;
            // Local mean time hardly depends on the UT estimate: an error in
            // UT moves the solved longitude the other way by almost the
            // same amount.
            h.hasLmt = true;

            if (spec.hasAsc) {
                double lat;
                if (latitudeFor(ramc, ascT, eps, lat)) {
                    h.hasLat = true;
                    h.lat    = lat;
                    double worst = 0;
                    for (int k = 0; k < 4; ++k) {
                        const double a2 = ascT + ((k & 1) ? ascPrec : -ascPrec);
                        const double r2 =
                            ramcFromMc(mcT + ((k & 2) ? mcPrec : -mcPrec), eps);
                        double l2;
                        worst = qMax(worst, latitudeFor(r2, a2, eps, l2)
                                                ? std::fabs(l2 - lat)
                                                : 20.0);
                    }
                    h.latErr = worst;
                } else {
                    h.note = tr("This Asc cannot rise with this MC");
                }
            }
        } else {
            h.note = tr("Asc alone needs an MC or a known place");
        }

        if (h.hasLat && h.hasLon) {
            const double tolLat = qBound(1.0, h.latErr, 6.0);
            const double tolLon = qBound(1.5, h.lonErr, 30.0);
            CityFilter   filter;
            filter.longitudeBuckets = 0; // plain population order
            const auto cities = citiesNearLatitude(h.lat, tolLat, 0, filter);
            // `cities` is in population order. Prefer the largest city close
            // to the central estimate; only if there is none, the largest
            // anywhere inside the uncertainty.
            const CityRec* pick = nullptr;
            // A closely fixed position names its nearest city instead.
            const bool   sharp    = h.latErr < 0.5 && h.lonErr < 0.5;
            double       nearest  = 1e9;
            const double cosLat   = cosd(h.lat);
            for (const CityRec& c : cities) {
                const double dLon = std::fabs(swe_difdeg2n(c.longitude, h.lon));
                if (dLon > tolLon) continue;
                if (sharp) {
                    const double d = std::hypot(dLon * cosLat, c.latitude - h.lat);
                    if (d < nearest) {
                        nearest = d;
                        pick    = &c;
                    }
                    continue;
                }
                if (!pick) pick = &c;
                if (dLon <= 2.0 && std::fabs(c.latitude - h.lat) <= 1.0) {
                    pick = &c;
                    break;
                }
            }
            if (pick) {
                h.place           = cityDisplayName(*pick);
                h.placeTimezoneId = pick->timezoneId;
                h.placeLat        = pick->latitude;
                h.placeLon        = pick->longitude;
            }
        }
        out << h;
    }

    for (ChartSearchHit& h : out) {
        if (h.hasLmt) {
            double d   = h.jdUt + 0.5 + h.lon / 360.0;
            h.lmtHours = (d - std::floor(d)) * 24.0;
        }
    }

    std::sort(out.begin(), out.end(), better);
    if (out.size() > spec.maxHits) {
        out.resize(spec.maxHits);
        _truncated = true;
    }
    _hits = out;
    emit progress(1.0);
}

} // namespace A
