
#include "transits.h"
#include "../../astroprocessor/src/astro-data.h"
#include "../../zodiac/src/mainwindow.h"
#include "../../zodiac/src/thememanager.h"
#include "geosearch.h"
#include <Astroprocessor/Calc>
#include <Astroprocessor/Output>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QDebug>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QSettings>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QThreadPool>
#include <QTimeZone>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <math.h>
#include <utility>
#include <vector>

using namespace std::chrono;

namespace
{
// Helper functions for event type strings
inline 
QString
eventTypeBrief(A::EventType et)
{
    return A::EventTypeManager::eventTypeToBrief(et);
}

inline 
QString
eventTypeDesc(A::EventType et)
{
    return A::EventTypeManager::eventTypeToString(et);
}

/// Describe a PlanetSet using the event type to infer mode suffixes
/// (-r natal, -p progressed, -sa solar arc) when mode() is unknown.
inline
QString
describePlanetsForEvent(const A::PlanetSet& ps, A::EventType et)
{
    QStringList res;
    bool hasOtherChart = ps.heterogeneous();

    for (const A::ChartPlanetModeId& cpid : ps) {
        auto name = cpid.isMidpt() ? cpid.name()
                                   : cpid.name().left(3);
        QString suff;

        // If mode is explicitly set, prefer it
        if (cpid.mode() != A::plmUnknown) {
            suff = A::modeToSuffix(cpid.mode());
        } else {
            int fid = cpid.fileId();

            // fid 0 in mixed-fid events is always natal
            if (fid == 0 && hasOtherChart) {
                suff = "r";
            } else {
                // Infer meaning of non-natal planets from event type
                switch (et) {
                case A::etcProgressedToProgressed:
                    suff = "p";
                    break;
                case A::etcProgressedToNatal:
                case A::etcInnerProgressedToNatal:
                    suff = (fid != 0) ? "p" : "r";
                    break;
                case A::etcTransitToProgressed:
                    if (fid == 0) suff = "p";
                    break;
                case A::etcSolarArcToNatal:
                    suff = (fid != 0) ? "sa" : "r";
                    break;
                default:
                    break;  // transit / station / etc. — no suffix
                }
            }
        }

        if (!suff.isEmpty())
            res << name + "-" + suff;
        else
            res << name;
    }
    return res.join("=");
}

#if 0
typedef QList<QStandardItem*> itemListBase;

/// comprise columns for a row in the tree view
class itemList : public itemListBase {
public:
    using itemListBase::itemListBase;

    itemList() : itemListBase() { }

    itemList(std::initializer_list<QString> its)
    { for (const auto& s: its) itemListBase::append(mksit(s)); }

    itemList(const QStringList& sl)
    { for (const auto& s: sl) itemListBase::append(mksit(s)); }

    itemList(const QString& a) :
        itemList( { a } )
    { }

    itemList(std::initializer_list<QVariant> itv)
    { for (const auto& v: itv) append(v); }

    operator bool() const { return !isEmpty(); }

    void append(const QVariant& v)
    { itemListBase::append(mksit(v)); }

    void append(const QVariant& erv,
                const QString& drs,
                const QString& ttrs = "",
                const QFont& font = QFont())
    {
        auto it = mksit(erv);
        it->setData(drs, Qt::DisplayRole);
        if (ttrs.isEmpty()) {
            it->setData(erv.toString(), Qt::ToolTipRole);
        } else {
            it->setData(ttrs, Qt::ToolTipRole);
        }
        if (font != QFont()) it->setFont(font);
        itemListBase::append(it);
    }

protected:
    static QStandardItem* mksit(const QString& s)
    {
        auto sit = new QStandardItem(s);
        sit->setFlags(Qt::ItemIsEnabled /*| Qt::ItemIsSelectable*/);
        return sit;
    }

    static QStandardItem* mksit(const QVariant& v)
    {
        auto sit = new QStandardItem();
        sit->setData(v, Qt::DisplayRole);
        sit->setFlags(Qt::ItemIsEnabled);
        return sit;
    }
};

QVariant
getFactors(int h)
{
    auto f = A::getPrimeFactors(h);
    if (f.empty() || f.size() < 2) return QVariant();

    QStringList sl;
    for (auto n : f) sl << QString::number(n);
    return sl.join("×");
}
#endif

} // namespace

class AChangeSignalFrame {
    EventsTableModel* _evm;

  public:
    AChangeSignalFrame(EventsTableModel* evm);
    AChangeSignalFrame(AChangeSignalFrame&& from);
    ~AChangeSignalFrame();
};

// Custom header view that intercepts Ctrl+click to prevent sorting
class TransitHeaderView : public QHeaderView {
    Q_OBJECT
  public:
    TransitHeaderView(Qt::Orientation orientation, QWidget* parent = nullptr) :
        QHeaderView(orientation, parent)
    {
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton
            && (event->modifiers() & Qt::ControlModifier))
        {
            // For Ctrl+click, emit our custom signal but don't call parent
            int section = logicalIndexAt(event->pos());
            if (section >= 0) {
                emit ctrlSectionClicked(section);
                event->accept();
                return; // Don't call parent - prevents sorting
            }
        }
        // For normal clicks, use default behavior
        QHeaderView::mousePressEvent(event);
    }

  signals:
    void ctrlSectionClicked(int logicalIndex);
};

class EventsTableModel : public QAbstractItemModel {
    Q_OBJECT

  public:
    enum {
        eventTypeCol = 0,
        dateCol,
        orbCol = dateCol,
        harmonicCol,
        transitBodyCol,
        natalTransitBodyCol,
        numCols
    };

    enum roles { SummaryRole = Qt::UserRole, RawRole };

    typedef A::EventOptions::DisplayMode DisplayMode;

    EventsTableModel(QObject* parent = nullptr) : QAbstractItemModel(parent) { }

    EventsTableModel(A::AspectSetId asps,
                     QObject*       parent = nullptr) :
        QAbstractItemModel(parent),
        _aspects(asps)
    {
    }
    
    void setNatalFile(AstroFile* file) { _natalFile = file; }

    QModelIndex index(int                row,
                      int                column,
                      const QModelIndex& parent = QModelIndex()) const override
    {
        return createIndex(row,
                           column,
                           parent.isValid() ? quintptr(parent.row())
                                            : quintptr(-1));
    }

    QModelIndex parent(const QModelIndex& inx) const override
    {
        if (inx.isValid() && inx.internalId() != quintptr(-1)) {
            return createIndex(int(inx.internalId()), 0, quintptr(-1));
        }
        return QModelIndex();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid()) {
            int parentRow = parent.row();
            if (parentRow < 0 || size_t(parentRow) >= _evs.size()) {
                return 0;
            }
            return int(_evs[parentRow]->coincidences().size());
        }
        return int(_evs.size());
    }

    int columnCount(const QModelIndex& = QModelIndex()) const override
    {
        return numCols;
    }

    int topLevelRow(const QModelIndex& inx) const
    {
        return (inx.parent().isValid()) ? inx.parent().row() : inx.row();
    }

    QDateTime rowDate(int row) const
    {
        if (row < 0 || row >= int(_evs.size())) return QDateTime();
        return _evs[row]->dateTime();
    }

    QDateTime rowDate(QModelIndex inx) const
    {
        auto par = inx.parent();
        int r = par.isValid() ? par.row() : inx.row();
        if (r < 0 || r >= int(_evs.size())) return QDateTime();
        return _evs[r]->dateTime();
    }

    A::HarmonicEvent rowData(int row) const
    {
        if (row < 0 || row >= int(_evs.size())) return A::HarmonicEvent();
        return *_evs[row];
    }
    A::HarmonicEvent rowData(QModelIndex inx) const
    {
        auto par = inx.parent();
        int r = par.isValid() ? par.row() : inx.row();
        if (r < 0 || r >= int(_evs.size())) return A::HarmonicEvent();
        return *_evs[r];
    }

    int rowForData(const A::HarmonicEvent& haeda,
                   bool&                   matches,
                   int                     sortCol,
                   bool                    isMore) const
    {
        matches    = false;
        auto lwrit = std::lower_bound(_evs.begin(),
                                      _evs.end(),
                                      &haeda,
                                      hevLess(sortCol, isMore));
        if (lwrit == _evs.end()) return -1;
        matches = (*(*lwrit) == haeda);
        return std::distance(_evs.begin(), lwrit);
    }

    QString rowDesc(int row) const
    {
        if (row < 0 || row >= int(_evs.size())) return QString();
        QString h = index(row, harmonicCol).data(SummaryRole).toString();
        QString t = index(row, transitBodyCol).data(SummaryRole).toString();
        QString n = index(row, natalTransitBodyCol).data(SummaryRole).toString();
        if (!n.isEmpty()) {
            return h + " " + t + "=" + n;
        }
        auto dt = rowDate(row).toTimeZone(QTimeZone(_tzOffset * 3600));
        QString dateStr = dt.toString(QString("yyyy MMMM"));
        return h + " " + dateStr + " " + t;
    }

    QVariant headerData(int col,
                        Qt::Orientation /*dir*/,
                        int role = Qt::DisplayRole) const override
    {
        if (role == Qt::TextAlignmentRole) {
            return Qt::AlignCenter;
        }
        if (role != Qt::DisplayRole) return QVariant();
        switch (col) {
        case eventTypeCol:        return tr("ET");
        case dateCol:             return tr("Date");
        case harmonicCol:         return tr("Asp");
        case transitBodyCol:      return tr("T/P");
        case natalTransitBodyCol: return tr("T/P/N");
        }
        return QVariant();
    }

    void setZodiac(const A::Zodiac& zod) { _zodiac = zod; }

    void setTimezone(double tz) {
        if (_tzOffset != tz) {
            _tzOffset = tz;
            // Refresh date column display
            if (rowCount() > 0) {
                emit dataChanged(index(0, dateCol),
                                index(rowCount() - 1, dateCol),
                                { Qt::DisplayRole, Qt::ToolTipRole });
            }
        }
    }

    QString getPos(float deg) const
    {
        const auto& sign = A::getSign(deg, _zodiac);
        QString     glyph(QChar(sign.userData["fontChar"].toInt()));
        int         ang = floor(deg) - sign.startAngle;
        if (ang < 0) ang += 360;

        int m = (int) (60.0 * (deg - (int) deg));
        return QString("%1%2%3%4")
            .arg(ang)
            .arg(glyph)
            .arg(m >= 10 ? "" : "0")
            .arg(m);
    };

    static int fid(const A::ChartPlanetId& cpid) { return cpid.fileId(); }
    static int fid(const A::PlanetLoc& ploc) { return ploc.planet.fileId(); }
    static int fid(const A::Loc& loc) { return -1; }

    QString display(const A::ChartPlanetModeId& cpid) const
    {
        if (QString suff = modeToSuffix(cpid.mode()); suff.isEmpty()) {
            return cpid.name();
        } else {
            return cpid.name() + "-" + suff;
        }
    }

    QString display(const A::PlanetLoc& s) const
    {
        QString suff;
        if (auto suf = modeToSuffix(s.mode()); !suf.isEmpty()) {
            suff = "-" + suf;
        }
        return QString(s.planet.fileId() == 1 ? "<i>%1</i>" : "%1")
                   .arg(s.description() + suff)
               + " "
               + A::zodiacPosition(s.rasiLoc(),
                                   _zodiac,
                                   A::HighPrecision,
                                   s.speed < 0);
    }

    QString glyph(const A::ChartPlanetId& cpid) const { return cpid.glyph(); }

    QString glyph(const A::PlanetLoc& s) const
    {
        const A::ChartPlanetId& cpid = s.planet;
        auto                    g    = cpid.glyph();
        auto                    pid  = cpid.planetId();
        if ((pid >= A::Ingresses_Start && pid < A::Ingresses_End)
            || (pid >= A::Regresses_Start && pid < A::Regresses_End)
            || (pid >= A::Houses_Start && pid < A::Houses_End))
        {
            return g;
        }

        if (cpid.isMidpt()) g = g.mid(1); // skip conj/opp
        auto desc = s.desc;
        if (!desc.isEmpty()) {
            if (desc == "SD") desc = "%&";
            else if (desc == "SR") desc = "%#";
            else if (desc == "p") desc = "="; // almagest p
            else if (desc == "n" || desc == "r") desc = "";
        }
        if (s.speed < 0 && !s.desc.startsWith("S")) {
            desc = "#" + desc; // retrograde
        }
        if (cpid.fileId() < 0) return g + " " + desc;
        return g + " " + getPos(s.rasiLoc()) + " " + desc;
    }

    QString summary(const A::ChartPlanetId& cpid) const
    {
        // For ChartPlanetId without mode info, we can't determine suffix
        // This is used for aspect patterns where we don't have PlanetLoc
        return cpid.name();
    }

    QString summary(const A::PlanetLoc& s) const
    {
        auto str = s.planet.name();
        
        // Add mode suffix if available (-r for natal, -p for progressed, etc.)
        QString suffix = modeToSuffix(s.mode());
        if (!suffix.isEmpty()) {
            str += "-" + suffix;
        }
        
        // Add descriptor if present (SD, SR, etc.)
        if (!s.desc.isEmpty()) {
            str += "-" + s.desc;
        }
        
        return str;
    }

    template <typename T>
    bool singleColumn(const T& locs) const
    {
        return locs.size() == 1
               || (locs.size() > 2
                   && fid(*locs.begin()) == fid(*locs.rbegin()));
    }

    template <typename T>
    bool mixedMode(const T& locs) const
    {
        return locs.size() >= 2 && fid(*locs.begin()) != fid(*locs.rbegin());
    }

    template <typename Iter>
    QVariant glyphic(int role, Iter its) const
    {
        if (role == Qt::FontRole) {
            static QFont f("Almagest", 11);
            return f;
        }

        QStringList sl;
        for (auto it = its.first; it != its.second; ++it) {
            const auto& s = *it;
            if (role == Qt::DisplayRole || role == Qt::EditRole) {
                sl << glyph(s);
            } else if (role == Qt::ToolTipRole) {
                sl << display(s);
            } else if (role == SummaryRole) {
                sl << summary(s);
            }
        }

        QString joint = ",";
        if (role == Qt::ToolTipRole) joint = "-";
        else if (role == SummaryRole)
            joint = "=";
        return sl.join(joint);
    }

    // Helper method to get ChartPlanetId from different types
    const A::ChartPlanetId& extractChartPlanetId(
        const A::ChartPlanetId& cpid) const
    {
        return cpid;
    }

    const A::ChartPlanetId& extractChartPlanetId(const A::PlanetLoc& ploc) const
    {
        return ploc.planet;
    }

    // Helper to get the correct house rulership string depending on input type
    QString getCorrectHouseRulershipWithNatalHouseString(
        const A::PlanetLoc& ploc) const
    {
        return getHouseRulershipWithNatalHouseString(
            ploc); // Use the position-aware version
    }

    QString getCorrectHouseRulershipWithNatalHouseString(
        const A::ChartPlanetId& cpid) const
    {
        return getHouseRulershipWithNatalHouseString(
            cpid); // Use the legacy version for aspect patterns
    }

    template <typename Iter>
    QVariant glyphicWithMode(int role, Iter its, DisplayMode mode, unsigned eventType = 0, bool isNatalTransitColumn = false) const
    {
        if (mode == A::EventOptions::DisplayGlyphs) {
            return glyphic(role, its);
        }

        if (role == Qt::FontRole) {
            // Check if any planet has rulership info
            for (auto it = its.first; it != its.second; ++it) {
                const auto& s = *it;

                const A::ChartPlanetId& cpid = extractChartPlanetId(s);

                QString rulershipText = getHouseRulershipString(cpid);
                if (!rulershipText.isEmpty()) {
                    return QFont(); // Use default font for text modes
                }
            }
            // Fall back to glyph font if no rulership
            static QFont f("Almagest", 11);
            return f;
        }

        QStringList sl;
        for (auto it = its.first; it != its.second; ++it) {
            const auto& s = *it;
            if (role == Qt::DisplayRole || role == Qt::EditRole) {
                const A::ChartPlanetId& cpid = extractChartPlanetId(s);
                QString                 rulershipText;
                if (mode == A::EventOptions::DisplayRulership) {
                    rulershipText = getHouseRulershipString(cpid);
                } else if (mode
                           == A::EventOptions::DisplayRulershipWithNatalHouse)
                {
                    rulershipText =
                        getCorrectHouseRulershipWithNatalHouseString(s);
                }

                if (!rulershipText.isEmpty()) {
                    sl << rulershipText;
                } else {
                    // Fall back to glyph if no rulership
                    sl << glyph(s);
                }
            } else if (role == Qt::ToolTipRole) {
                sl << display(s);
            } else if (role == SummaryRole) {
                sl << summary(s);
            }
        }

        QString joint = ",";
        if (role == Qt::ToolTipRole) joint = "-";
        else if (role == SummaryRole)
            joint = "=";
        return sl.join(joint);
    }

    template <typename T>
    auto getNTColIters(const T& locs) const
    {
        if (singleColumn(locs)) {
            return std::make_pair(locs.rend(), locs.rend());
        }
        if (mixedMode(locs)) {
            auto it  = locs.rbegin();
            auto f   = fid(*it);
            auto end = it;
            while (end != locs.rend() && f == fid(*end)) ++end;
            return std::make_pair(it, end);
        }
        // For pairs: return the slower moving planet (last in set, first in reverse)
        auto rit = locs.rbegin();
        if (rit == locs.rend()) return std::make_pair(rit, rit);
        return std::make_pair(rit, std::next(rit));
    }

    template <typename T>
    auto getTColIters(const T& locs) const
    {
        if (singleColumn(locs)) {
            return std::make_pair(locs.begin(), locs.end());
        }
        if (mixedMode(locs)) {
            auto it  = locs.begin();
            auto f   = fid(*it);
            auto end = it;
            while (end != locs.end() && f == fid(*end)) ++end;
            return std::make_pair(it, end);
        }
        // For pairs: return the faster moving planet (first in set)
        auto it = locs.begin();
        if (it == locs.end()) return std::make_pair(it, it);
        return std::make_pair(it, std::next(it));
    }

    typedef std::pair<const A::Loc*, const A::Loc*> locPair;

    bool getPlanetPair(const A::PlanetRangeBySpeed& locs, locPair& pp) const
    {
        if (singleColumn(locs)) return false;
        pp.first  = &(*locs.begin());
        pp.second = &(*locs.rbegin());
        return true;
    }

    QVariant data(const QModelIndex& index,
                  int                role = Qt::DisplayRole) const override
    {
        int row  = index.row();
        int prow = int(index.internalId());

        auto         evr = prow == -1 ? _evs[row] : _evs[prow];
        QMutexLocker ml(getEvents(evr)->mutex());

        int col = index.column();

        const auto& asp(prow == -1 ? (*_evs[row])
                                   : _evs[prow]->coincidence(row));

        if (role == Qt::TextAlignmentRole) {
            if (col == harmonicCol || col == eventTypeCol) {
                return unsigned(Qt::AlignHCenter | Qt::AlignBaseline);
            }
            if (col == transitBodyCol && asp.locations().empty()) {
                return unsigned(Qt::AlignRight | Qt::AlignBaseline);
            }
            return unsigned(Qt::AlignLeft | Qt::AlignBaseline);
        }

        if (role != Qt::DisplayRole && role != Qt::FontRole
            && role != Qt::ToolTipRole && role != Qt::EditRole
            && role != SummaryRole && role != RawRole
            && (col < transitBodyCol
                || (role != Qt::FontRole && role != Qt::ForegroundRole)))
        {
            return QVariant();
        }

        auto et = asp.eventType();

        if (col >= transitBodyCol && role == RawRole) {
            return QVariant::fromValue<A::PlanetSet>(evr->planets());
        }

        switch (col) {
        case eventTypeCol:
            if (role == Qt::FontRole) return QFont();
            if (prow == -1) {
                auto type = _evs[row]->eventType();
                if (role == Qt::ToolTipRole) {
                    return A::EventTypeManager::eventTypeToString(type);
                }
                if (role == Qt::DisplayRole) {
                    return A::EventTypeManager::eventTypeToBrief(type);
                }
            }
            break;
        case dateCol:
            if (prow == -1) {
                // HarmonicEvent
                // Convert UTC to chart's timezone
                auto dt = _evs[row]->dateTime().toTimeZone(
                    QTimeZone(_tzOffset * 3600));
                if (role == RawRole) return dt;

                auto&& r = _evs[row]->range();
                if (role == Qt::FontRole) {
                    QFont f;
                    auto  days = r.days();
                    if (days >= 28) {
                        f.setBold(true);
                        f.setItalic(true);
                    } else if (days >= 7) {
                        f.setBold(true);
                    } else if (days >= 1) {
                        f.setItalic(true);
                    }
                    return f;
                }

                constexpr const char fmt[]   = "yyyy/MM/dd";
                constexpr const char sfmt[]  = "MM/dd hh:mm";
                constexpr const char ssfmt[] = "hh:mm";
                if (role != Qt::ToolTipRole) return dt.toString(fmt);

                QString res = dt.toString("ddd hh:mm:ss.zzz");
                if (r != A::ADateTimeRange()) {
                    auto dtfrom  = r.first.toTimeZone(QTimeZone(_tzOffset * 3600));
                    auto dtto    = r.second.toTimeZone(QTimeZone(_tzOffset * 3600));
                    
                    // Use full format if years differ, otherwise optimize by date
                    auto fmtFrom = (dtfrom.date().year() != dt.date().year()) ? fmt :
                                   (dtfrom.date() == dt.date() ? ssfmt : sfmt);
                    auto fmtTo   = (dtto.date().year() != dt.date().year()) ? fmt :
                                   (dtto.date() == dt.date() ? ssfmt : sfmt);
                    
                    res          = QString("%1 ->\n %2\n  -> %3")
                              .arg(dtfrom.toString(fmtFrom),
                                   res,
                                   dtto.toString(fmtTo));
                }
                return res;
            }

            if (role == Qt::FontRole) {
                return QFont();
            }

            // HarmonicAspect
            return A::degreeToString(asp.orb());

        case harmonicCol:
            if (role == Qt::ToolTipRole) {
                if (singleColumn(asp.locations())) return "station";
                if (asp.orb() != qreal() /*asp.locations().empty()*/) {
                    return QString("H%1 %2")
                        .arg(asp.harmonic())
                        .arg(A::degreeToString(asp.orb(), A::HighPrecision));
                } else {
                    locPair pp;
                    if (getPlanetPair(asp.locations(), pp)) {
                        auto a =
                            A::calculateAspect(aspects(), pp.first, pp.second);
                        return a.d->name;
                    }
                }
            }
            if (role == RawRole) return asp.harmonic();
            if (role == Qt::FontRole) {
                QFont f;
#if 1
                if (asp.orb() == qreal()) {
                    f.setItalic(true);
                    f.setBold(true);
                } else if (asp.orb() <= 0.5) {
                    f.setBold(true);
                } else if (asp.orb() <= 1.0) {
                    f.setItalic(true);
                }
#else
                if (asp.locations().empty() && asp.orb() < 1.0) {
                    if (asp.orb() >= 0.5) {
                        f.setBold(true);
                    }
                    f.setItalic(true);
                } else if (asp.orb() != qreal()) {
                    f.setItalic(true);
                }
#endif
                return f;
            }
            // Display H# (optionally with ratio in parentheses)
            // Orb is shown in tooltip, not in the cell
            {
                QString result = "H" + QString::number(asp.harmonic());
                if (A::EventOptions::current().showHarmonicDividend) {
                    locPair pp;
                    if (getPlanetPair(asp.locations(), pp)) {
                        auto a = A::calculateAspect(aspects(), pp.first, pp.second);
                        if (a.d && a.d->_harmonic > 0) {
                            // This is a harmonic aspect, show the ratio in parentheses
                            result += " (" + a.d->name + ")";
                        }
                    }
                }
                return result;
            }

        case transitBodyCol:
            // Default glyph display mode
            if (asp.locations().empty()) {
                // goofiness due to different sorts for planets
                // and locations
                if (singleColumn(asp.planets())) {
                    return glyphicWithMode(role,
                                           getTColIters(asp.planets()),
                                           _transitBodyColMode,
                                           et,
                                           false);
                }
                return glyphicWithMode(role,
                                       getNTColIters(asp.planets()),
                                       _transitBodyColMode,
                                       et,
                                       false);
            }
            return glyphicWithMode(role,
                                   getTColIters(asp.locations()),
                                   _transitBodyColMode,
                                   et,
                                   false);

        case natalTransitBodyCol:
            if (role == Qt::ForegroundRole) {
                if (mixedMode(asp.planets())) return ThemeManager::instance().getGoldColor();
                // else falls through to default return
                break;
            }

            if (asp.locations().empty()) {
                // goofiness due to different sorts for planets
                // and locations
                if (!singleColumn(asp.planets())) {
                    return glyphicWithMode(role,
                                           getTColIters(asp.planets()),
                                           _natalTransitBodyColMode,
                                           et,
                                           true);
                }
                break;
            }
            return glyphicWithMode(role,
                                   getNTColIters(asp.locations()),
                                   _natalTransitBodyColMode,
                                   et,
                                   true);

        default: break;
        }
        return QVariant();
    }

    // Extract the ChartPlanetModeIds displayed in a given column for
    // a particular aspect, mirroring the display logic in data().
    // This ensures that sort order matches what the user sees.
    //
    // Key insight: PlanetSet is ordered by planet ID (Sun<Moon<Merc...)
    // while PlanetRangeBySpeed is ordered by speed (fastest first).
    // The display code uses getTColIters/getNTColIters which split
    // differently depending on the container type, and additionally
    // "swaps" which iterator function is used for planets() vs
    // locations() (the "goofiness" noted in data()). This helper
    // replicates exactly that logic.
    static std::vector<A::ChartPlanetModeId>
    columnSortKey(const A::HarmonicAspect& asp, int col)
    {
        std::vector<A::ChartPlanetModeId> key;

        auto isSingleColumn = [](const auto& c) {
            return c.size() == 1
                   || (c.size() > 2
                       && fid(*c.begin()) == fid(*c.rbegin()));
        };
        auto isMixedMode = [](const auto& c) {
            return c.size() >= 2
                   && fid(*c.begin()) != fid(*c.rbegin());
        };

        // Helper lambdas that collect ChartPlanetModeIds from iterator
        // ranges, matching getTColIters / getNTColIters logic.
        auto collectBeginSide = [&](const auto& c) {
            if (isSingleColumn(c)) {
                // getTColIters returns [begin, end)
                for (auto it = c.begin(); it != c.end(); ++it)
                    key.push_back(it->planetModeId());
            } else if (isMixedMode(c)) {
                auto it  = c.begin();
                auto f   = fid(*it);
                for (auto end = it; end != c.end() && fid(*end) == f; ++end)
                    key.push_back(end->planetModeId());
            } else {
                // pair: first element only
                if (!c.empty())
                    key.push_back(c.begin()->planetModeId());
            }
        };
        auto collectRbeginSide = [&](const auto& c) {
            if (isSingleColumn(c)) {
                // getNTColIters returns [rend, rend) → empty
                return;
            }
            if (isMixedMode(c)) {
                auto it  = c.rbegin();
                auto f   = fid(*it);
                for (auto end = it; end != c.rend() && fid(*end) == f; ++end)
                    key.push_back(end->planetModeId());
            } else {
                // pair: last element only
                if (!c.empty())
                    key.push_back(c.rbegin()->planetModeId());
            }
        };

        // For PlanetSet (no locations), we need a different approach
        // since ChartPlanetModeId already IS the element type.
        auto collectPSBeginSide = [&](const A::PlanetSet& ps) {
            if (isSingleColumn(ps)) {
                for (auto it = ps.begin(); it != ps.end(); ++it)
                    key.push_back(*it);
            } else if (isMixedMode(ps)) {
                auto it = ps.begin();
                auto f  = fid(*it);
                for (auto end = it; end != ps.end() && fid(*end) == f; ++end)
                    key.push_back(*end);
            } else {
                if (!ps.empty())
                    key.push_back(*ps.begin());
            }
        };
        auto collectPSRbeginSide = [&](const A::PlanetSet& ps) {
            if (isSingleColumn(ps)) return;
            if (isMixedMode(ps)) {
                auto it = ps.rbegin();
                auto f  = fid(*it);
                for (auto end = it; end != ps.rend() && fid(*end) == f; ++end)
                    key.push_back(*end);
            } else {
                if (!ps.empty())
                    key.push_back(*ps.rbegin());
            }
        };

        if (col == transitBodyCol) {
            if (!asp.locations().empty()) {
                // locations present → begin side (fastest)
                collectBeginSide(asp.locations());
            } else if (isSingleColumn(asp.planets())) {
                // singleColumn → all planets (same as getTColIters)
                collectPSBeginSide(asp.planets());
            } else {
                // multi-column planets: display uses getNTColIters
                // (the "swap" — rbegin side = highest planet IDs)
                collectPSRbeginSide(asp.planets());
            }
        } else if (col == natalTransitBodyCol) {
            if (!asp.locations().empty()) {
                // locations present → rbegin side (slowest)
                collectRbeginSide(asp.locations());
            } else if (!isSingleColumn(asp.planets())) {
                // multi-column planets: display uses getTColIters
                // (the "swap" — begin side = lowest planet IDs)
                collectPSBeginSide(asp.planets());
            }
            // singleColumn → empty (nothing displayed in T/P/N)
        }

        return key;
    }

    struct hevLess {
        int  _col;
        bool _isMore;

        hevLess(int column = dateCol, bool isMore = false) :
            _col(column),
            _isMore(isMore)
        {
        }

        bool operator()(const A::HarmonicEvent* a,
                        const A::HarmonicEvent* b) const
        {
            if (_isMore) std::swap(a, b);
            switch (_col) {
            case eventTypeCol:
                if (a->eventType() < b->eventType()) return true; // event type
                if (a->eventType() > b->eventType()) return false;
                // fall through to dateCol

            case dateCol:
                if (a->dateTime() < b->dateTime()) return true; // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true; // orb
                if (a->orb() > b->orb()) return false;
                if (a->harmonic() < b->harmonic()) return true; // harmonic
                if (a->harmonic() > b->harmonic()) return false;
                if (a->locations().size() < b->locations().size()) // planetSet
                    return true;
                if (a->locations().size() > b->locations().size()) return false;
                return (a->locations() < b->locations()); // planetRange

            case harmonicCol:
                if (a->harmonic() < b->harmonic()) return true; // harmonic
                if (a->harmonic() > b->harmonic()) return false;
                if (a->dateTime() < b->dateTime()) return true; // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true; // orb
                if (a->orb() > b->orb()) return false;
                if (a->locations().size() < b->locations().size()) // planetSet
                    return true;
                if (a->locations().size() > b->locations().size()) return false;
                return (a->locations() < b->locations()); // planetRange

            case transitBodyCol:
            case natalTransitBodyCol:
            {
                auto ka = columnSortKey(*a, _col);
                auto kb = columnSortKey(*b, _col);
                if (ka < kb) return true;
                if (kb < ka) return false;
                if (a->dateTime() < b->dateTime()) return true; // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true; // orb
                if (a->orb() > b->orb()) return false;
                return (a->harmonic() < b->harmonic()); // harmonic
            }
            }
            return false;
        }
    };

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override
    {
        _sortPending = false;

        typedef const A::HarmonicEvent* HEv;
        std::function<bool(HEv, HEv)>   less =
            hevLess(column, order == Qt::DescendingOrder);

        beginResetModel();
#if 1
        _evs.clear();
        for (auto lievs : _evls) {
            A::modalize<eventListIndex> cev(evp::curr(), lievs.first);
            QMutexLocker ml(const_cast<QMutex*>(lievs.second->mutex()));
            for (auto& ev : *lievs.second) {
                if (!ev.dateTime().isValid()) continue;
                // Apply per-event-type harmonic restrictions
                if (!_harmonicRestrictions.isEmpty()) {
                    auto it = _harmonicRestrictions.constFind(ev.eventType());
                    if (it != _harmonicRestrictions.constEnd()
                        && ev.harmonic() > it.value()) {
                        continue;
                    }
                }
                _evs.emplace_back(ev);
            }
        }
#endif
        std::sort(_evs.begin(), _evs.end(), less);
        endResetModel();

        delete _chs;
        _chs = nullptr;
    }

    void addEvents(A::HarmonicEvents& evs)
    {
        auto li  = currentEvents()++;
        auto ins = _evls.emplace(li, &evs);
        if (!ins.second) return;

        AChangeSignalFrame chs(this);

        A::modalize<eventListIndex> cli(evp::curr(), li);
        beginResetModel();
        _evs.insert(_evs.end(), evs.cbegin(), evs.cend());
        sort();
        endResetModel();
    }

    void removeEvents(const A::HarmonicEvents& evs)
    {
        for (auto lievit = _evls.begin(); lievit != _evls.end(); ++lievit) {
            if (evs != lievit->second) continue;

            // Emit aboutToChange BEFORE any model modification so that
            // saveScrollPos() can safely access the model in its current state
            if (!_changeRef) emit aboutToChange();
            
            _evls.erase(lievit++);
            beginResetModel();
            rebuild();
            sort();
            endResetModel();
            
            if (!_changeRef) emit changeDone();

            break;
        }
    }

    void clearAllEvents()
    {
        if (_evls.empty()) return;
        if (!_changeRef) emit aboutToChange();
        beginResetModel();
        _evls.clear();
        _evs.clear();
        endResetModel();
    }

    void setAspectSet(A::AspectSetId asps) { _aspects = asps; }

    void setHarmonicRestrictions(const QMap<A::EventType, unsigned>& r)
    {
        _harmonicRestrictions = r;
    }

    const A::AspectsSet& aspects() const { return A::getAspectSet(_aspects); }

    int           sortColumn() const { return _sortBy; }
    Qt::SortOrder sortOrder() const { return _sortOrder; }

    // Display mode methods for cycling column content
    void cycleTransitBodyColMode()
    {
        _transitBodyColMode =
            static_cast<DisplayMode>((_transitBodyColMode + 1) % 3);
        emitColumnDataChanged(transitBodyCol);
    }

    void cycleNatalTransitBodyColMode()
    {
        _natalTransitBodyColMode =
            static_cast<DisplayMode>((_natalTransitBodyColMode + 1) % 3);
        emitColumnDataChanged(natalTransitBodyCol);
    }

    // Helper method to emit dataChanged for entire column including child rows
    void emitColumnDataChanged(int column)
    {
        if (rowCount() == 0) return;

        // Emit for all top-level rows
        emit dataChanged(index(0, column),
                         index(rowCount() - 1, column),
                         { Qt::DisplayRole, Qt::FontRole });

        // Also emit for all child rows (coincidences)
        for (int i = 0; i < rowCount(); ++i) {
            QModelIndex parentIdx = index(i, 0);
            int childCount = rowCount(parentIdx);
            if (childCount > 0) {
                emit dataChanged(index(0, column, parentIdx),
                                 index(childCount - 1, column, parentIdx),
                                 { Qt::DisplayRole, Qt::FontRole });
            }
        }
    }

    // Get natal horoscope for house rulership calculations
    const A::Horoscope& getNatalHoroscope() const
    {
        // Use the stored natal file pointer
        if (_natalFile) {
            auto fileType = _natalFile->getType();
            // Only return horoscope for natal charts (Male, Female, Event)
            if (fileType == TypeMale || fileType == TypeFemale
                || fileType == TypeEvent)
            {
                return _natalFile->horoscope();
            }
        }
        static const A::Horoscope
            empty; // fallback for non-natal charts or when not available
        return empty;
    }

    // Calculate house rulership display string for a planet
    QString getHouseRulershipString(const A::ChartPlanetId& cpid) const
    {
        const A::Horoscope& natal = getNatalHoroscope();
        if (natal.planets.empty()) return "";

        // Always use the base planet ID (ignoring file ID) to look up rulership
        // This means both transit Mars and natal Mars show natal Mars's
        // rulerships
        A::PlanetId basePlanetId = cpid.planetId();

        // Get the planet from natal chart using base planet ID
        if (!natal.planets.contains(basePlanetId)) {
            qDebug() << "Planet not found in natal chart for planetId="
                     << basePlanetId;
            return "";
        }
        const A::Planet& planet = natal.planets[basePlanetId];

        // Format as "R" + house numbers joined by "+"
        // Negative house numbers (intercepted signs) are shown in parentheses
        // Sort by absolute value to get proper numerical order
        QList<int> sortedHouses = planet.houseRuler;
        std::sort(sortedHouses.begin(), sortedHouses.end(), [](int a, int b) {
            return qAbs(a) < qAbs(b);
        });

        QStringList houses;
        for (int house : sortedHouses) {
            if (house < 0) {
                // Intercepted sign: show as (house number)
                houses << QString("(%1)").arg(-house);
            } else {
                // Normal cusp rulership: show as plain number
                houses << QString::number(house);
            }
        }

        // If planet has house rulerships, return "R" + houses
        if (!houses.isEmpty()) {
            return "R" + houses.join("+");
        }

        // For planets without rulerships, return abbreviated name (first 3
        // letters, no spaces)
        QString planetName = A::getPlanetName(cpid);
        QString abbrev     = planetName.remove(' ').left(3);
        return abbrev;
    }

    // Calculate which house a longitude falls into in the natal chart
    int getNatalHouseForLongitude(qreal longitude) const
    {
        const A::Horoscope& natal = getNatalHoroscope();
        if (natal.houses.system == nullptr) return 0;
        return A::getHouse(natal, longitude);
    }

    // Calculate house rulership + natal house string using PlanetLoc position
    QString getHouseRulershipWithNatalHouseString(
        const A::PlanetLoc& ploc) const
    {
        const A::ChartPlanetId& cpid      = ploc.planet;
        QString                 rulership = getHouseRulershipString(cpid);
        if (rulership.isEmpty()) return "";

        // Use the transiting planet's actual position to find its natal house
        int natalHouse = getNatalHouseForLongitude(ploc.loc);
        if (natalHouse > 0) {
            return QString("%1H ").arg(natalHouse) + rulership;
        }
        return rulership;
    }

    // Calculate house rulership string for aspect patterns (ChartPlanetId
    // version) For aspect patterns, we only show rulership without natal house
    // since we don't have position data
    QString getHouseRulershipWithNatalHouseString(
        const A::ChartPlanetId& cpid) const
    {
        // For aspect patterns, just return the rulership without natal house
        return getHouseRulershipString(cpid);
    }

    // Export table data to HTML with chart metadata
    QString exportToHtml(AstroFile* natalFile, AstroFile* transitFile) const;
    QString planetToText(const A::ChartPlanetModeId& cpid) const;
    QString planetToText(const A::PlanetLoc& ploc) const;

  public slots:
    void rebuild()
    {
        _evs.clear();
        for (const auto& liev : _evls) {
            _evs.insert(_evs.end(), liev.second->begin(), liev.second->end());
        }
        sort();
    }

    void triggerSort()
    {
        if (!_sortPending) {
            _sortPending = true;
            QTimer::singleShot(0, this, SLOT(sort()));
        }
    }

    void onSortChange(int col, Qt::SortOrder order)
    {
        if (col != _sortBy || _sortOrder != order) {
            if (!_chs) _chs = new AChangeSignalFrame(this);
            _sortBy    = col;
            _sortOrder = order;
            triggerSort();
        }
    }

    void sort() { sort(_sortBy, _sortOrder); }

  signals:
    void aboutToChange();
    void changeDone();

  private:
    typedef unsigned short int eventListIndex;

    struct evp : public std::pair<eventListIndex, const A::HarmonicEvent*> {
        using Base = std::pair<eventListIndex, const A::HarmonicEvent*>;

        static unsigned short int& curr()
        {
            static thread_local unsigned short int s_curr = 0;
            return s_curr;
        }

        using Base::Base;

        evp(const A::HarmonicEvent* ev) : Base(curr(), ev) { }

        using Base::operator=;

        evp& operator=(const A::HarmonicEvent* ev)
        {
            first  = curr();
            second = ev;
            return *this;
        }

        eventListIndex listIndex() const { return first; }

        const A::HarmonicEvent* operator->() const { return second; }
        const A::HarmonicEvent& operator*() const { return *second; }

        operator eventListIndex() const { return first; }
        operator const A::HarmonicEvent*() const { return second; }
    };

    static eventListIndex& currentEvents()
    {
        static eventListIndex s_curr = 0;
        return s_curr;
    }

    std::vector<evp>                             _evs;
    std::map<eventListIndex, A::HarmonicEvents*> _evls;

    A::HarmonicEvents* getEvents(eventListIndex li) const
    {
        auto evlit = _evls.find(li);
        if (evlit == _evls.end()) return nullptr;
        return evlit->second;
    }

    bool          _sortPending = false;
    int           _sortBy      = dateCol;
    Qt::SortOrder _sortOrder   = Qt::AscendingOrder;

    A::Zodiac      _zodiac;
    A::AspectSetId _aspects = 0;
    double         _tzOffset = 0; // Timezone offset in hours

    int _changeRef = 0;

    AChangeSignalFrame* _chs = nullptr;

    // Per-instance display modes - always start with glyphs (mode 0)
    // Users can Ctrl+click column headers to cycle through modes
    DisplayMode _transitBodyColMode = A::EventOptions::DisplayGlyphs;
    DisplayMode _natalTransitBodyColMode = A::EventOptions::DisplayGlyphs;
    
    AstroFile* _natalFile = nullptr; // Pointer to natal chart for rulership calculations

    // Per-event-type harmonic restrictions (event type → max harmonic)
    QMap<A::EventType, unsigned> _harmonicRestrictions;

    friend class AChangeSignalFrame;
};

#include "transits.moc"

AChangeSignalFrame::AChangeSignalFrame(EventsTableModel* evm) : _evm(evm)
{
    if (evm && !evm->_changeRef++) emit evm->aboutToChange();
}

AChangeSignalFrame::AChangeSignalFrame(AChangeSignalFrame&& from) :
    _evm(from._evm)
{
    from._evm = nullptr;
}

AChangeSignalFrame::~AChangeSignalFrame()
{
    if (_evm && !--_evm->_changeRef) emit _evm->changeDone();
}

ADateDelta::ADateDelta(const QString& str)
{
    QRegularExpression re("([+-]?\\d+) ?((y(ea)?r?|m(o(n(th)?)?)?|d(a?y)?)s?)");
    re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);

    auto mit = re.globalMatch(str);
    while (mit.hasNext()) {
        auto match = mit.next();
        auto val   = match.captured(1).toInt();
        auto unit  = match.captured(2).toLower();
        if (unit.startsWith("y")) numYears = val;
        else if (unit.startsWith("m"))
            numMonths = val;
        else if (unit.startsWith("d"))
            numDays = val;
    }
}

ADateDelta::ADateDelta(QDate from, QDate to)
{
    if (from > to) qSwap(from, to);
    auto dd  = from.daysTo(to);
    numYears = dd / 365;
    dd %= 365;
    numMonths = dd / 30;
    dd %= 30;
    auto num = to.day() - from.day();
    numDays  = (numDays >= 0) ? num : dd;
}

QDate
ADateDelta::addTo(const QDate& d) const
{
    return d.addYears(numYears).addMonths(numMonths).addDays(numDays);
}

QDate
ADateDelta::subtractFrom(const QDate& d) const
{
    return d.addYears(-numYears).addMonths(-numMonths).addDays(-numDays);
}

// ============================================================================
// Pattern MRU (Most Recently Used) helpers
// ============================================================================

// Item delegate that caps row height to font metrics + small margin,
// preventing the "double-spaced" look caused by QSS-styled QComboBox popups.
class CompactItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        int compact = option.fontMetrics.height() + 4;
        if (s.height() > compact) s.setHeight(compact);
        return s;
    }
};

static const int    kPatternMRUMax = 30;
static const QString kPatternMRUKey = QStringLiteral("Events/patternHistory");

static QStringList
loadPatternMRU()
{
    QSettings settings(SessionManager::settingsFile(), QSettings::IniFormat);
    return settings.value(kPatternMRUKey).toStringList();
}

static void
savePatternMRU(const QStringList& mru)
{
    QSettings settings(SessionManager::settingsFile(), QSettings::IniFormat);
    settings.setValue(kPatternMRUKey, mru);
    settings.sync();
}

static void
addPatternToMRU(const QString& pattern)
{
    if (pattern.isEmpty()) return;
    QStringList mru = loadPatternMRU();
    mru.removeAll(pattern);
    mru.prepend(pattern);
    while (mru.size() > kPatternMRUMax) mru.removeLast();
    savePatternMRU(mru);
}

Transits::Transits(QWidget* parent) :
    AstroFileHandler(parent),
    _planet(A::Planet_None),
    _fileIndex(0),
    _inhibitUpdate(false),
    _tview(nullptr),
#if OLDMODEL
    _tm(nullptr),
#endif
    _evm(nullptr),
    _ddelta(A::EventOptions::current().defaultTimespan),
    _chs(nullptr),
    _tabEventOptions(A::EventOptions::current().enabledEvents)  // Initialize from global defaults
{
    // Enable tooltips even when parent window doesn't have focus
    this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

    _tview = new TransitTreeView;
    _tview->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _tview->expandAll();
    _tview->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);

    // Model will be created and set in ensureEventsModel() when file(0) is available

    // Create and set custom header
    auto hdr = new TransitHeaderView(Qt::Horizontal, _tview);
    _tview->setHeader(hdr);

    // Note: Will connect sort signal after model is created in ensureEventsModel()
    connect(hdr,
            &TransitHeaderView::ctrlSectionClicked,
            this,
            &Transits::headerClicked);
    hdr->setSectionsClickable(true);
    hdr->setSortIndicatorShown(true);
    hdr->setSectionResizeMode(QHeaderView::ResizeToContents);
    hdr->setStretchLastSection(true);

    QAction* act = new QAction("Copy Selection");
    act->setShortcut(QKeySequence::Copy);
    act->setShortcutContext(Qt::WidgetShortcut);
    connect(act, SIGNAL(triggered()), this, SLOT(copySelection()));
    _tview->addAction(act);
    
    QAction* actCopyTable = new QAction("Copy Table as Report");
    actCopyTable->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    actCopyTable->setToolTip("Copy entire events table with chart information as formatted report");
    connect(actCopyTable, SIGNAL(triggered()), this, SLOT(copyTableAsRichText()));
    _tview->addAction(actCopyTable);

    _start  = new A::AstroDateTimeEdit(true /*dateOnly*/);
    _duraRB = new QRadioButton(tr("for"));
    _duraRB->setFocusPolicy(Qt::NoFocus);
    _duration = new QLineEdit;
    _duration->setText(_ddelta.toString());
    _endRB = new QRadioButton(tr("til"));
    _endRB->setFocusPolicy(Qt::NoFocus);
    _end = new A::AstroDateTimeEdit(true /*dateOnly*/);

    _back = new QPushButton("«");
    _back->setMaximumWidth(20);
    _back->setProperty("navButton", true);
    _forth = new QPushButton("»");
    _forth->setMaximumWidth(20);
    _forth->setProperty("navButton", true);

    _grp = new QButtonGroup(this);
    _grp->addButton(_duraRB, 0);
    _grp->addButton(_endRB, 1);
    _duraRB->setChecked(true);

    auto l1 = new QHBoxLayout;
    l1->addWidget(_back);
    l1->addWidget(_forth);
    // l1->addWidget(new QLabel(tr("from")));
    l1->addWidget(_start);
    l1->addWidget(_duraRB);
    l1->addWidget(_duration);
    l1->addWidget(_endRB);
    l1->addWidget(_end);
    l1->setSpacing(4);

    _input = new QComboBox;
    _input->setEditable(true);
    _input->setInsertPolicy(QComboBox::NoInsert);
    _input->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _input->lineEdit()->setPlaceholderText(tr("Pattern (e.g. Sun=Moon; Mars ingress Aries; Saturn station)"));
    // Use a compact delegate so popup items aren't double-spaced
    _input->setItemDelegate(new CompactItemDelegate(_input));

    // Populate from persisted MRU
    _input->addItems(loadPatternMRU());
    _input->setCurrentText(QString());  // start blank; per-tab pattern set in filesUpdated()

    // QCompleter with substring matching on the MRU list
    auto* completer = new QCompleter(loadPatternMRU(), _input);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    _input->setCompleter(completer);
    // Name the popup so it can be styled via QSS (it's a top-level widget,
    // not a child of QComboBox, so "QComboBox QListView" selectors don't reach it)
    completer->popup()->setObjectName(QStringLiteral("completerPopup"));
    completer->popup()->setItemDelegate(new CompactItemDelegate(completer->popup()));

    // Refresh combo items from global MRU when dropdown is about to show
    connect(_input, &QComboBox::activated, this, [this](int) {
        QString current = _input->currentText().trimmed();
        if (filesCount() > 0 && file(0))
            file(0)->setTransitPattern(current);
        if (current != _lastUsedPattern)
            updateTransits();
    });

    // Live visual feedback instead of a blocking QRegularExpressionValidator.
    // The validator rejected partial input (intermediate keystrokes) because
    // the complex regex couldn't partially match incomplete tokens.
    connect(_input->lineEdit(), &QLineEdit::textChanged, this, [this](const QString& text) {
        QString t = text.trimmed();
        if (t.isEmpty()) {
            _inputBorderStyle.clear();
        } else {
            bool ok = A::EventOptions::isValidPattern(t);
            _inputBorderStyle =
                ok ? QStringLiteral("border: 1px solid green;")
                   : QStringLiteral("border: 1px solid red;");
        }
        // Apply border on the QComboBox frame (inner QLineEdit border is hidden)
        _input->setStyleSheet(
            _inputBorderStyle.isEmpty()
                ? QString()
                : QStringLiteral("QComboBox { %1 }").arg(_inputBorderStyle));
    });

    auto l2 = new QVBoxLayout;
    l2->addItem(l1);
    l2->addWidget(_input, 0);
    l2->setContentsMargins(QMargins());

    QVBoxLayout* l3 = new QVBoxLayout;
    l3->setContentsMargins(QMargins(0, 0, 0, 0));
    
    // Create toolbar at the very top
    QToolBar* toolbar = new QToolBar(this);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    
    // Set toolbar button styling - bold text, minimal padding, compact
    toolbar->setStyleSheet(
        "QToolBar { border: none; spacing: 1px; padding: 0px; }"
        "QToolButton { "
        "  font-weight: bold; "
        "  font-size: 9pt; "
        "  padding: 1px 3px; "
        "  margin: 0px; "
        "  min-width: 26px !important; "
        "  max-width: 60px; "
        "  min-height: 24px; "
        "}"
        "QToolButton:checked { background-color: palette(highlight); color: palette(highlighted-text); }"
    );
    
    // Configure toolbar to not elide text on buttons
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    
    // Copy Report button - use text symbol since icons aren't showing
    QAction* copyTableAction = toolbar->addAction("📋");
    copyTableAction->setToolTip("Copy Report (Ctrl+Shift+C)");
    connect(copyTableAction, &QAction::triggered, this, &Transits::copyTableAsRichText);
    // Override styling for symbol button
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(copyTableAction))) {
        btn->setStyleSheet("QToolButton { font-weight: normal; font-size: 14pt; min-width: 28px; min-height: 24px; padding: 1px; margin: 0px; }");
    }
    
    // Auto-recalc toggle - use text symbol since icons aren't showing
    _actAutoRecalc = toolbar->addAction("↻");
    _actAutoRecalc->setCheckable(true);
    _actAutoRecalc->setChecked(true);  // Default to on
    _actAutoRecalc->setToolTip("Auto-recalculate when event filters change");
    // Override styling for the symbol button
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actAutoRecalc))) {
        btn->setStyleSheet("QToolButton { font-weight: normal; font-size: 14pt; min-width: 28px; min-height: 24px; padding: 1px; margin: 0px; }");
    }
    
    // When auto-recalc is re-enabled, check if there are pending changes
    connect(_actAutoRecalc, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            // Check if any files need recalc
            bool needsRecalc = false;
            for (int i = 0, n = filesCount(); i < n; ++i) {
                if (file(i)->needsEventsRecalc()) {
                    needsRecalc = true;
                    break;
                }
            }
            if (needsRecalc) {
                qDebug() << "Auto-recalc re-enabled with pending changes, triggering update";
                updateTransits();
            } else {
                qDebug() << "Auto-recalc re-enabled with no pending changes";
            }
        }
    });
    
    toolbar->addSeparator();
    
    // Helper to save event options and optionally trigger recalc.
    // filterAdded == true  → a new event type was enabled  → recalc needed
    // filterAdded == false → an event type was disabled     → keep existing
    //                         events visible, only persist the option change
    auto saveEventOptionsAndRecalc = [this](bool filterAdded) {
        // Save to file(0) immediately so it persists when switching files
        if (filesCount() > 0 && file(0)) {
            file(0)->setTransitEventOptions(_tabEventOptions);
        }
        if (filterAdded) {
            // Mark events for recalc since a new event type was added
            for (int i = 0, n = filesCount(); i < n; ++i) {
                file(i)->markEventsForRecalc();
            }
            if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
        }
    };
    
    // Stations button
    _actStations = toolbar->addAction("S");
    _actStations->setCheckable(true);
    _actStations->setToolTip("Stations");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actStations))) {
        btn->setStyleSheet("QToolButton { min-width: 20px !important; }");
    }
    connect(_actStations, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        qDebug() << "[STATIONS] Button toggled to:" << checked << "in tab" << (void*)this;
        if (checked) _tabEventOptions.insert(A::etcStation);
        else _tabEventOptions.erase(A::etcStation);
        qDebug() << "  _tabEventOptions now has" << _tabEventOptions.size() << "event types";
        
        saveEventOptionsAndRecalc(checked);
    });
    
    // Returns button (controls all return types)
    _actReturns = toolbar->addAction("R");
    _actReturns->setCheckable(true);
    _actReturns->setToolTip("Returns (Solar, Lunar, Planetary)");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actReturns))) {
        btn->setStyleSheet("QToolButton { min-width: 20px !important; }");
    }
    connect(_actReturns, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) {
            _tabEventOptions.insert(A::etcReturn);
            _tabEventOptions.insert(A::etcSolarReturn);
            _tabEventOptions.insert(A::etcLunarReturn);
        } else {
            _tabEventOptions.erase(A::etcReturn);
            _tabEventOptions.erase(A::etcSolarReturn);
            _tabEventOptions.erase(A::etcLunarReturn);
        }
        saveEventOptionsAndRecalc(checked);
    });
    
    toolbar->addSeparator();
    
    // Event filter buttons - checkable toggles
    _actTransitToTransit = toolbar->addAction("T=T");
    _actTransitToTransit->setCheckable(true);
    _actTransitToTransit->setToolTip("Transit to Transit aspects");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actTransitToTransit))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actTransitToTransit, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        qDebug() << "T=T button toggled:" << checked;
        if (checked) _tabEventOptions.insert(A::etcTransitToTransit);
        else _tabEventOptions.erase(A::etcTransitToTransit);
        saveEventOptionsAndRecalc(checked);
    });
    
    // T=N dropdown button with menu
    _btnTransitToNatal = new QToolButton(toolbar);
    _btnTransitToNatal->setText("T=N");
    _btnTransitToNatal->setCheckable(true);
    _btnTransitToNatal->setPopupMode(QToolButton::MenuButtonPopup);
    _btnTransitToNatal->setToolTip("Transit to Natal aspects - click to toggle, dropdown to select mode");
    _btnTransitToNatal->setStyleSheet("QToolButton { min-width: 48px !important; }");
    
    auto* transitNatalMenu = new QMenu(_btnTransitToNatal);
    
    // Radio button group for mode selection (which mode is the "default")
    auto* transitNatalGroup = new QActionGroup(transitNatalMenu);
    transitNatalGroup->setExclusive(true);  // Makes them behave like radio buttons
    
    _actTransitToNatal = transitNatalMenu->addAction(eventTypeBrief(A::etcTransitToNatal));
    _actTransitToNatal->setCheckable(true);
    _actTransitToNatal->setChecked(true);  // Default mode
    _actTransitToNatal->setToolTip(eventTypeDesc(A::etcTransitToNatal));
    transitNatalGroup->addAction(_actTransitToNatal);
    
    _actOuterTransitToNatal = transitNatalMenu->addAction(eventTypeBrief(A::etcOuterTransitToNatal));
    _actOuterTransitToNatal->setCheckable(true);
    _actOuterTransitToNatal->setToolTip(eventTypeDesc(A::etcOuterTransitToNatal));
    transitNatalGroup->addAction(_actOuterTransitToNatal);
    
    // Separator
    transitNatalMenu->addSeparator();
    
    // Independent checkbox for angles (store as member variable so we can restore state)
    _actIncludeAngles = transitNatalMenu->addAction(eventTypeBrief(A::etcTransitToNatalAngles));
    _actIncludeAngles->setCheckable(true);
    _actIncludeAngles->setChecked(true);  // Default to including angles
    _actIncludeAngles->setToolTip(eventTypeDesc(A::etcTransitToNatalAngles));
    
    _btnTransitToNatal->setMenu(transitNatalMenu);
    
    // Menu selection changes the mode (which is the "default", doesn't enable/disable)
    connect(transitNatalGroup, &QActionGroup::triggered, this, [this, saveEventOptionsAndRecalc](QAction* action) {
        // Just update the mode preference, don't change enabled state
        bool wasOuter = _transitToNatalShowsOuter;
        
        // Block signals to prevent button toggle from being triggered
        QSignalBlocker blocker(_btnTransitToNatal);
        
        if (action->text().contains("OT=N")) {
            _transitToNatalShowsOuter = true;
            _btnTransitToNatal->setText("OT=N");
        } else {
            _transitToNatalShowsOuter = false;
            _btnTransitToNatal->setText("T=N");
        }
        
        blocker.unblock();
        
        // Only recalc if button is currently checked AND mode actually changed
        if (_btnTransitToNatal->isChecked() && wasOuter != _transitToNatalShowsOuter) {
            // Button is on, so actually switch the active mode
            if (_transitToNatalShowsOuter) {
                _tabEventOptions.insert(A::etcOuterTransitToNatal);
                _tabEventOptions.erase(A::etcTransitToNatal);
            } else {
                _tabEventOptions.insert(A::etcTransitToNatal);
                _tabEventOptions.erase(A::etcOuterTransitToNatal);
            }
            saveEventOptionsAndRecalc(true);
        }
    });
    
    // Angles checkbox toggles independently
    connect(_actIncludeAngles, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) {
            _tabEventOptions.insert(A::etcTransitToNatalAngles);
        } else {
            _tabEventOptions.erase(A::etcTransitToNatalAngles);
        }
        saveEventOptionsAndRecalc(checked);
    });
    
    // Button click toggles the on/off state for the current mode
    connect(_btnTransitToNatal, &QToolButton::clicked, this, [this, saveEventOptionsAndRecalc]() {
        // isChecked() already reflects the new state after auto-toggle
        bool newState = _btnTransitToNatal->isChecked();
        
        qDebug() << "T=N button clicked, new state:" << newState << "outer mode:" << _transitToNatalShowsOuter;
        
        if (newState) {
            // Turning ON - restore the mode that was selected
            if (_transitToNatalShowsOuter) {
                _tabEventOptions.insert(A::etcOuterTransitToNatal);
                _tabEventOptions.erase(A::etcTransitToNatal);
            } else {
                _tabEventOptions.insert(A::etcTransitToNatal);
                _tabEventOptions.erase(A::etcOuterTransitToNatal);
            }
            // Restore angles if it was checked before
            if (_transitToNatalAnglesWasChecked) {
                _tabEventOptions.insert(A::etcTransitToNatalAngles);
            }
        } else {
            // Turning OFF - cache whether angles was checked
            _transitToNatalAnglesWasChecked = _tabEventOptions.count(A::etcTransitToNatalAngles) > 0;
            // Remove all three event types
            _tabEventOptions.erase(A::etcTransitToNatal);
            _tabEventOptions.erase(A::etcOuterTransitToNatal);
            _tabEventOptions.erase(A::etcTransitToNatalAngles);
        }
        qDebug() << "After T=N click: hasTransitToNatal=" << (_tabEventOptions.count(A::etcTransitToNatal) > 0)
                 << "hasOuter=" << (_tabEventOptions.count(A::etcOuterTransitToNatal) > 0);
        saveEventOptionsAndRecalc(newState);
    });
    
    toolbar->addWidget(_btnTransitToNatal);
    
    _actProgressedToProgressed = toolbar->addAction("P=P");
    _actProgressedToProgressed->setCheckable(true);
    _actProgressedToProgressed->setToolTip("Progressed to Progressed aspects");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actProgressedToProgressed))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actProgressedToProgressed, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcProgressedToProgressed);
        else _tabEventOptions.erase(A::etcProgressedToProgressed);
        saveEventOptionsAndRecalc(checked);
    });
    
    // IP=N/P=N dropdown button with menu
    _btnProgressedToNatal = new QToolButton(toolbar);
    _btnProgressedToNatal->setText("IP=N");
    _btnProgressedToNatal->setCheckable(true);
    _btnProgressedToNatal->setPopupMode(QToolButton::MenuButtonPopup);
    _btnProgressedToNatal->setToolTip("Progressed to Natal aspects - click to toggle, dropdown to select mode");
    _btnProgressedToNatal->setStyleSheet("QToolButton { min-width: 52px !important; }");
    
    auto* progressedNatalMenu = new QMenu(_btnProgressedToNatal);
    
    // Radio button group for mode selection
    auto* progressedNatalGroup = new QActionGroup(progressedNatalMenu);
    progressedNatalGroup->setExclusive(true);  // Makes them behave like radio buttons
    
    _actInnerProgressedToNatal = progressedNatalMenu->addAction(eventTypeBrief(A::etcInnerProgressedToNatal));
    _actInnerProgressedToNatal->setCheckable(true);
    _actInnerProgressedToNatal->setChecked(true);  // Default mode
    _actInnerProgressedToNatal->setToolTip(eventTypeDesc(A::etcInnerProgressedToNatal));
    progressedNatalGroup->addAction(_actInnerProgressedToNatal);
    
    _actAllProgressedToNatal = progressedNatalMenu->addAction(eventTypeBrief(A::etcProgressedToNatal));
    _actAllProgressedToNatal->setCheckable(true);
    _actAllProgressedToNatal->setToolTip(eventTypeDesc(A::etcProgressedToNatal));
    progressedNatalGroup->addAction(_actAllProgressedToNatal);
    
    _btnProgressedToNatal->setMenu(progressedNatalMenu);
    
    // Menu selection changes the mode (which is the "default", doesn't enable/disable)
    connect(progressedNatalGroup, &QActionGroup::triggered, this, [this, saveEventOptionsAndRecalc](QAction* action) {
        // Just update the mode preference, don't change enabled state
        bool wasInner = _progressedToNatalShowsInner;
        
        // Block signals to prevent button toggle from being triggered
        QSignalBlocker blocker(_btnProgressedToNatal);
        
        if (action->text().contains("IP=N")) {
            _progressedToNatalShowsInner = true;
            _btnProgressedToNatal->setText("IP=N");
        } else {
            _progressedToNatalShowsInner = false;
            _btnProgressedToNatal->setText("P=N");
        }
        
        blocker.unblock();
        
        // Only recalc if button is currently checked AND mode actually changed
        if (_btnProgressedToNatal->isChecked() && wasInner != _progressedToNatalShowsInner) {
            // Button is on, so actually switch the active mode
            if (_progressedToNatalShowsInner) {
                _tabEventOptions.insert(A::etcInnerProgressedToNatal);
                _tabEventOptions.erase(A::etcProgressedToNatal);
            } else {
                _tabEventOptions.insert(A::etcProgressedToNatal);
                _tabEventOptions.erase(A::etcInnerProgressedToNatal);
            }
            saveEventOptionsAndRecalc(true);
        }
    });
    
    // Button click toggles the on/off state for the current mode
    connect(_btnProgressedToNatal, &QToolButton::clicked, this, [this, saveEventOptionsAndRecalc]() {
        // isChecked() already reflects the new state after auto-toggle
        bool newState = _btnProgressedToNatal->isChecked();
        
        qDebug() << "IP=N/P=N button clicked, new state:" << newState << "inner mode:" << _progressedToNatalShowsInner;
        
        if (newState) {
            if (_progressedToNatalShowsInner) {
                _tabEventOptions.insert(A::etcInnerProgressedToNatal);
                _tabEventOptions.erase(A::etcProgressedToNatal);
            } else {
                _tabEventOptions.insert(A::etcProgressedToNatal);
                _tabEventOptions.erase(A::etcInnerProgressedToNatal);
            }
        } else {
            _tabEventOptions.erase(A::etcProgressedToNatal);
            _tabEventOptions.erase(A::etcInnerProgressedToNatal);
        }
        saveEventOptionsAndRecalc(newState);
    });
    
    toolbar->addWidget(_btnProgressedToNatal);
    
    _actTransitAspectPatterns = toolbar->addAction("TA");
    _actTransitAspectPatterns->setCheckable(true);
    _actTransitAspectPatterns->setToolTip("Transit Aspect Patterns");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actTransitAspectPatterns))) {
        btn->setStyleSheet("QToolButton { min-width: 28px !important; }");
    }
    connect(_actTransitAspectPatterns, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcTransitAspectPattern);
        else _tabEventOptions.erase(A::etcTransitAspectPattern);
        saveEventOptionsAndRecalc(checked);
    });
    
    _actTransitNatalAspectPatterns = toolbar->addAction("TNA");
    _actTransitNatalAspectPatterns->setCheckable(true);
    _actTransitNatalAspectPatterns->setToolTip("Transit-Natal Aspect Patterns");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actTransitNatalAspectPatterns))) {
        btn->setStyleSheet("QToolButton { min-width: 36px !important; }");
    }
    connect(_actTransitNatalAspectPatterns, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcTransitNatalAspectPattern);
        else _tabEventOptions.erase(A::etcTransitNatalAspectPattern);
        saveEventOptionsAndRecalc(checked);
    });
    
    // Sign Ingress button
    _actSignIngress = toolbar->addAction("T=I");
    _actSignIngress->setCheckable(true);
    _actSignIngress->setToolTip("Sign Ingresses");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actSignIngress))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actSignIngress, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcSignIngress);
        else _tabEventOptions.erase(A::etcSignIngress);
        saveEventOptionsAndRecalc(checked);
    });
    
    // House Ingress button
    _actHouseIngress = toolbar->addAction("T=H");
    _actHouseIngress->setCheckable(true);
    _actHouseIngress->setToolTip("House Ingresses");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actHouseIngress))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actHouseIngress, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcHouseIngress);
        else _tabEventOptions.erase(A::etcHouseIngress);
        saveEventOptionsAndRecalc(checked);
    });
    
    // Paranatellonta button
    _actParanatellonta = toolbar->addAction("Par");
    _actParanatellonta->setCheckable(true);
    _actParanatellonta->setToolTip("Paranatellonta");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actParanatellonta))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actParanatellonta, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcParanatellonta);
        else _tabEventOptions.erase(A::etcParanatellonta);
        saveEventOptionsAndRecalc(checked);
    });
    
    // Paranatellonta to Natal button
    _actParanatellontaToNatal = toolbar->addAction("Par=N");
    _actParanatellontaToNatal->setCheckable(true);
    _actParanatellontaToNatal->setToolTip("Paranatellonta to Natal");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actParanatellontaToNatal))) {
        btn->setStyleSheet("QToolButton { min-width: 48px !important; }");
    }
    connect(_actParanatellontaToNatal, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcParanatellontaToNatal);
        else _tabEventOptions.erase(A::etcParanatellontaToNatal);
        saveEventOptionsAndRecalc(checked);
    });
    
    // Initialize toolbar state from tab event options
    updateToolbarFromEventOptions();
    
    l3->addWidget(toolbar);
    
    int i1 = l3->count();
    l3->addItem(l2);
    l3->setStretch(i1, 0);
    l3->addWidget(_tview, 5);

    _location = new GeoSearchWidget(false /*hbox*/);
#if 1
    connect(_location, &GeoSearchWidget::locationChanged, [this] {
        if (!_pendingLocationChange) {
            _pendingLocationChange = true;
            QTimer::singleShot(0, this, SLOT(onLocationChange()));
        }
    });
#endif
    l3->addWidget(_location);

    // We have to defer the installation of the default loc
    // because we might be in the process of building the
    // astroWidget...
    // IMPORTANT: Only set default location if no files have been loaded yet
    // This prevents overwriting the location from loaded chart files during session restore
    QTimer::singleShot(0, [this]() {
        // Check if we already have files loaded (e.g., from session restore)
        // If so, updateTransits() will have already set the correct location
        if (filesCount() > 0) {
            qDebug() << "Skipping default location init - files already loaded";
            return;
        }
        
        _pendingLocationChange = true;
        auto s = MainWindow::theAstroWidget()->currentSettings();
        auto v = s.value("Scope/defaultLocation").toString().split(" ");
        _location->setLocation(
            QVector3D(v.at(0).toFloat(), v.at(1).toFloat(), v.at(2).toFloat()));
        _location->setLocationName(
            s.value("Scope/defaultLocationName").toString());
        _pendingLocationChange = false;
    });
    // connect(_location, SIGNAL(coordinateUpdated()), this,
    // SLOT(onLocationChange()));

    setLayout(l3);

    // Component-specific CSS loading disabled - now using global theme system
    // QFile cssfile("Details/style.css");
    // cssfile.open(QIODevice::ReadOnly | QIODevice::Text);
    // setStyleSheet(cssfile.readAll());
    // cssfile.close();

#if 0
    QTimer::singleShot(0, [this]() {
        connect(this, SIGNAL(updateTransits(double)),
                MainWindow::theAstroWidget(), SLOT(setHarmonic(double)));
    });
#endif

    connect(_tview,
            SIGNAL(doubleClicked(const QModelIndex&)),
            this,
            SLOT(doubleClickedCell(const QModelIndex&)));
#if 0
    connect(_tview, SIGNAL(pressed(const QModelIndex&)),
            this, SLOT(clickedCell(const QModelIndex&)));
#endif
    connect(hdr,
            SIGNAL(sectionDoubleClicked(int)),
            this,
            SLOT(headerDoubleClicked(int)));
    connect(_tview,
            SIGNAL(currently(const QModelIndex&)),
            this,
            SLOT(clickedCell(const QModelIndex&)));

    connect(this,
            SIGNAL(needToFindIt(const QString&)),
            this,
            SLOT(findIt(const QString&)));

    // Calendar popup is built-in for AstroDateTimeEdit

    connect(_back, &QAbstractButton::clicked, [this] {
        auto sd = _start->date();
        auto dd = sd.daysTo(_end->date()) / 2;
        if (dd) {
            _start->setDate(sd.addDays(-dd));
            // Mark events for recalc and honor auto-recalc state
            for (int i = 0, n = filesCount(); i < n; ++i) {
                file(i)->markEventsForRecalc();
            }
            if (_actAutoRecalc && _actAutoRecalc->isChecked()) {
                updateTransits();
            }
        }
    });

    connect(_forth, &QAbstractButton::clicked, [this] {
        auto ed = _end->date();
        auto dd = _start->date().daysTo(ed) / 2;
        if (dd) {
            _end->setDate(ed.addDays(dd));
            // Mark events for recalc and honor auto-recalc state
            for (int i = 0, n = filesCount(); i < n; ++i) {
                file(i)->markEventsForRecalc();
            }
            if (_actAutoRecalc && _actAutoRecalc->isChecked()) {
                updateTransits();
            }
        }
    });

    connect(_start, SIGNAL(editingFinished()), this, SLOT(onStartChanged()));
    connect(_start,
            SIGNAL(dateChanged(const QDate&)),
            this,
            SLOT(onStartChanged(const QDate&)));

    connect(_end, SIGNAL(editingFinished()), this, SLOT(onEndChanged()));
    connect(_end,
            SIGNAL(dateChanged(const QDate&)),
            this,
            SLOT(onEndChanged(const QDate&)));

    connect(_duration,
            SIGNAL(editingFinished()),
            this,
            SLOT(onDurationChanged()));
    connect(_duration,
            SIGNAL(textChanged(const QString&)),
            this,
            SLOT(onDurationChanged(const QString&)));

    connect(_input->lineEdit(), &QLineEdit::editingFinished,
            this, [this]() {
                // Only restart if the pattern text actually changed.
                // editingFinished fires on every focus-out; without this
                // guard, clicking on a result row while a search is running
                // would trigger a redundant updateTransits() call.
                QString current = _input->currentText().trimmed();
                // Persist pattern to file(0) so it survives tab switches
                if (filesCount() > 0 && file(0)) {
                    file(0)->setTransitPattern(current);
                }
                // Add valid, non-empty patterns to the global MRU
                if (!current.isEmpty() && A::EventOptions::isValidPattern(current)) {
                    addPatternToMRU(current);
                    // Refresh combo items & completer from updated MRU
                    QStringList mru = loadPatternMRU();
                    _input->blockSignals(true);
                    _input->clear();
                    _input->addItems(mru);
                    _input->setCurrentText(current);
                    _input->blockSignals(false);
                    if (auto* c = _input->completer())
                        static_cast<QStringListModel*>(c->model())->setStringList(mru);
                }
                if (current != _lastUsedPattern) {
                    updateTransits();
                }
            });

    auto today        = QDate::currentDate();
    auto startOfMonth = QDate(today.year(), today.month(), 1);
    _start->setDate(startOfMonth);

    connect(ttv()->verticalScrollBar(), &QScrollBar::valueChanged, [this](int value) {
        qDebug() << "[SCROLL] valueChanged:" << value << "lastScrollValue:" << _lastScrollValue;
        saveScrollPos();
        _lastScrollValue = value;
    });
    connect(ttv()->verticalScrollBar(),
            SIGNAL(rangeChanged(int, int)),
            this,
            SLOT(restoreScrollPos()));

    QTimer::singleShot(0, [this]() {
        connect(this,
                SIGNAL(updateHarmonics(double)),
                MainWindow::theAstroWidget(),
                SLOT(setHarmonic(double)));
    });
}

Transits::~Transits() { 
    // Save current event options and pattern to file(0) before destruction
    if (filesCount() > 0 && file(0)) {
        qDebug() << "[DESTRUCTOR] Saving event options to file" << file(0)->getName();
        file(0)->setTransitEventOptions(_tabEventOptions);
        file(0)->setTransitPattern(_input->currentText());
    }
    
    // Disconnect scroll bar signals to prevent crashes during destruction
    if (_tview && _tview->verticalScrollBar()) {
        disconnect(_tview->verticalScrollBar(), nullptr, this, nullptr);
    }
    stopThreads(); 
}

QTreeView*
Transits::ttv() const
{
    return _tview;
}

void
Transits::describePlanet()
{
#if 0
    // TODO filter by planet?
#endif

    _fileIndex = qBound(0, _fileIndex, filesCount() - 1);
    updateTransits();
}

void
Transits::stopThreads()
{
    // Prevent any pending restart from firing during shutdown
    _pendingRestart = false;
    
    if (_progressSortTimer && _progressSortTimer->isActive()) {
        _progressSortTimer->stop();
    }

    // Cancel and wait on ALL finders (current + paused/background)
    QList<AstroFile*> keys = _finders.keys();
    for (auto* af : keys) {
        auto& fs = _finders[af];
        qDebug() << "========================================";
        qDebug() << "[STOP THREADS] Cancelling finder for" << af->getName()
                 << "thread:" << fs.thread << "finder:" << fs.finder.data();
        qDebug() << "========================================";
        if (fs.finder) fs.finder->cancel();
        delete fs.chs;
        fs.chs = nullptr;
        if (fs.thread && !fs.thread->isFinished()) {
            qDebug() << "[STOP THREADS] Waiting for finder thread to finish...";
            fs.thread->wait();
            qDebug() << "[STOP THREADS] Finder thread finished";
        }
    }
    _finders.clear();
    _active       = nullptr;
    _activeFinder = nullptr;
    _chs          = nullptr;
    _previousFile = nullptr;
}

void
Transits::disconnectFinder(FinderState& fs)
{
    if (fs.finder) {
        fs.finder->disconnect();        // Disconnect all signals FROM the finder
        disconnect(fs.finder.data());   // Disconnect all signals TO the finder
    }
    if (fs.thread) {
        disconnect(fs.thread, SIGNAL(finished()), this, SLOT(onCompleted()));
    }
    if (_progressSortTimer && _progressSortTimer->isActive()) {
        _progressSortTimer->stop();
    }
}

void
Transits::cancelAndRemoveFinder(AstroFile* af)
{
    auto it = _finders.find(af);
    if (it == _finders.end()) return;

    auto& fs = it.value();
    qDebug() << "[CANCEL FINDER] Canceling finder for" << af->getName();
    disconnectFinder(fs);

    if (fs.finder) fs.finder->cancel();
    delete fs.chs;
    fs.chs = nullptr;

    if (fs.thread && !fs.thread->isFinished()) {
        fs.thread->wait();
    }

    // Clear current-tab aliases if they point to this finder
    if (_active == fs.thread) {
        _active       = nullptr;
        _activeFinder = nullptr;
        _chs          = nullptr;
    }

    _finders.erase(it);
}

void
Transits::refreshLocationUI()
{
    // Lightweight refresh: update the location widget from file(0)'s stored
    // transit location without triggering event recomputation.
    if (filesCount() == 0 || !file(0)) return;
    if (!file(0)->hasTransitLocation()) return;

    _pendingLocationChange = true;
    _location->setLocation(file(0)->getTransitLocation());
    _location->setLocationName(file(0)->getTransitLocationName());
    _pendingLocationChange = false;

    // Also sync transitsAF() so future calculations use correct location
    transitsAF()->suspendUpdate();
    transitsAF()->setLocation(file(0)->getTransitLocation());
    transitsAF()->setLocationName(file(0)->getTransitLocationName());
    transitsAF()->setTimezone(file(0)->getTransitTimezone());
    transitsAF()->resumeUpdate();
}

bool
Transits::transitsOnly() const
{
    // Transits-only mode: single file that is NOT a person's natal chart
    // If we have 2+ files, we're in synastry/comparison mode, not transits-only
    if (filesCount() != 1) return false;
    
    auto ftype = file()->getType();
    return (ftype != TypeMale && ftype != TypeFemale && ftype != TypeEvent && ftype != TypeReturn);
}

EventsTableModel*
Transits::ensureEventsModel()
{
    if (filesCount() == 0) return nullptr;
    
    // Check if file(0) already has a model
    auto* evm = qobject_cast<EventsTableModel*>(file(0)->eventsModel());
    if (!evm) {
        // Create model owned by file(0)
        evm = new EventsTableModel(file(0));
        file(0)->setEventsModel(evm);
    }
    
    // Set the natal file for house rulership calculations
    evm->setNatalFile(file(0));
    
    // Update our local pointer and view if needed
    if (_evm != evm) {
        _evm = evm;
        _tview->setModel(_evm);
        
        // Connect to custom signals for event recalculation
        QObject::connect(_evm, &EventsTableModel::aboutToChange, this, [this] {
            saveScrollPos();
        });
        QObject::connect(_evm, &EventsTableModel::changeDone, this, &Transits::restoreScrollPos);

        // Connect to standard model reset signals for sorting/filtering
        QObject::connect(_evm, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
            saveScrollPos();
        });
        QObject::connect(_evm, &QAbstractItemModel::modelReset, this, &Transits::restoreScrollPos);
        
        // Connect sort signal
        auto hdr = qobject_cast<TransitHeaderView*>(_tview->header());
        if (hdr) {
            connect(hdr,
                    &QHeaderView::sortIndicatorChanged,
                    _evm,
                    &EventsTableModel::onSortChange,
                    Qt::UniqueConnection);
            connect(hdr,
                    &TransitHeaderView::ctrlSectionClicked,
                    this,
                    &Transits::headerClicked,
                    Qt::UniqueConnection);
            hdr->setSortIndicator(_evm->sortColumn(), _evm->sortOrder());
        }
    }
    
    return _evm;
}

AstroFile*
Transits::transitsAF()
{
    // For tabs with 2 files (natal + transits), use file(1) so each tab
    // has its own independent transit location
    if (filesCount() > 1) return file(1);
    
    // For single-file transits-only tabs, use file(0)
    if (transitsOnly()) return file();
    
    // For other cases, use the shared _trans object (legacy behavior)
    if (!_trans) {
        _trans = new AstroFile(this);
        MainWindow::theAstroWidget()->setupFile(_trans);
    }
    return _trans;
}

void
Transits::updateTimezone()
{
    QVector3D vec(_location->location());

    auto nm = new QNetworkAccessManager(this);
    connect(nm, &QNetworkAccessManager::finished, [this](QNetworkReply* reply) {
        reply->deleteLater();
        if (auto nm = sender()) {
            nm->deleteLater();
        }
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument response = QJsonDocument::fromJson(reply->readAll());
        if (response["status"].toString() != "OK") {
            qDebug() << "Timezone request failed:"
                     << response["status"].toString()
                     << response["errorMessage"].toString();
            return;
        }
        qreal tz = (response["rawOffset"].toInt()
                    /*+ response["dstOffset"].toInt()*/)
                   / 3600;
        
        // Update the model's timezone first so display refreshes
        if (_evm) {
            _evm->setTimezone(short(tz));
        }

        // Persist to file(0)'s per-tab transit location so it survives
        // file-2 close and session save/restore.
        if (filesCount() > 0 && file(0)) {
            file(0)->setTransitLocation(_location->location());
            file(0)->setTransitLocationName(_location->locationName());
            file(0)->setTransitTimezone(short(tz));
        }

        qDebug() << "Timezone for location is"
                 << response["rawOffset"].toInt() / 60 /*minsPerSec*/
                 << "with dstOffset" << response["dstOffset"].toInt() / 60
                 << "in" << response["timeZoneName"].toString();

        if (transitsOnly()) {
            // Event times are stored in UTC and displayed via the model's
            // _tzOffset, which we already updated above.  Most transit
            // event types (T=T, stations, ingresses, returns, etc.) are
            // location-independent, so a full recalc is unnecessary.
            //
            // Only house-ingress and paranatellonta depend on the
            // observer's location.  Recalc only when those are active.
            bool needsRecalc =
                _tabEventOptions.count(A::etcHouseIngress)
                || _tabEventOptions.count(A::etcParanatellonta);

            // Update transitsAF() (== file(0) here) with new location/tz.
            // Block filesUpdated so the change() → recalculate chain
            // doesn't trigger a redundant event search.
            {
                A::modalize<bool> noup(_inhibitUpdate);
                transitsAF()->suspendUpdate();
                transitsAF()->setLocation(_location->location());
                transitsAF()->setLocationName(_location->locationName());
                transitsAF()->setTimezone(short(tz));
                transitsAF()->resumeUpdate();
            }

            if (needsRecalc) {
                stopThreads();
                file(0)->markEventsForRecalc();
                describePlanet();
            }
        } else {
            // For natal + transit tabs, update transitsAF (file(1)) and
            // signal FilesBar to refresh the chart.
            transitsAF()->suspendUpdate();
            transitsAF()->setLocation(_location->location());
            transitsAF()->setLocationName(_location->locationName());
            transitsAF()->setTimezone(short(tz));
            transitsAF()->resumeUpdate();

            stopThreads();
            emit updateSecond(transitsAF());
        }
    });

    QString url =
        QString(A::googMapURL + "/timezone/json?location=%1,%2&key=%3&timestamp=%4&language=en")
            .arg(vec.y())
            .arg(vec.x())
            .arg(MainWindow::instance()->APIKey().c_str())
            .arg(transitsAF()->getGMT().toSecsSinceEpoch());
    qDebug() << "Issuing TZ URL:" << url;
    nm->get(QNetworkRequest(url));
}

void
Transits::updateTransits()
{
    qDebug() << "========================================";
    qDebug() << "[UPDATE TRANSITS START] filesCount:" << filesCount();
    if (filesCount() > 0 && file(0)) {
        qDebug() << "[UPDATE TRANSITS START] file(0):" << file(0)->getName() << "type:" << file(0)->getType();
    }
    if (filesCount() > 1 && file(1)) {
        qDebug() << "[UPDATE TRANSITS START] file(1):" << file(1)->getName() << "type:" << file(1)->getType();
    }
    qDebug() << "========================================";
    
    if (filesCount() == 0) return;

    // Guard against re-entrancy: setting the location widget below can
    // trigger changed() → fileUpdatedSlot → filesUpdated → describePlanet
    // → updateTransits again.  Let the outer call handle everything.
    if (_inUpdateTransits) {
        qDebug() << "[UPDATE TRANSITS] Re-entrant call blocked";
        return;
    }
    A::modalize<bool> utGuard(_inUpdateTransits, true);
    if (!isVisible()) return;
    if (transitsAF()->isSuspendedUpdate()) return;

    // If we're already waiting for an old thread to finish before restarting,
    // don't queue another computation — the pending restart will handle it.
    if (_pendingRestart) {
        qDebug() << "[UPDATE TRANSITS] Already pending restart from canceled thread, skipping";
        return;
    }

    // Restore location from the appropriate file FIRST (before cache check)
    // This ensures the location widget updates even when using cached events
    AstroFile* locFile = nullptr;
    if (filesCount() >= 2) {
        // If we have 2+ files, use file(1) for location (the transit/return chart)
        locFile = file(1);
        // Sync to file(0)'s per-tab transit location so it survives file-2 close
        file(0)->setTransitLocation(locFile->getLocation());
        file(0)->setTransitLocationName(locFile->getLocationName());
        file(0)->setTransitTimezone(locFile->getTimezone());
    } else if (filesCount() == 1 && transitsOnly()) {
        // Single file that is transits-only, use it
        locFile = file(0);
    } else if (filesCount() == 1 && file(0)->hasTransitLocation()) {
        // Single natal chart with stored transit location — use it
        // (preserves location after file-2 was closed)
        locFile = nullptr; // handled below
    } else if (filesCount() == 1) {
        // Single natal/event chart with no stored transit location —
        // default to the chart's own birth location (not the shared _trans
        // which may carry a stale location from a different tab).
        locFile = file(0);
    }
    
    if (locFile) {
        // Update the location widget and transitsAF to match the current chart
        qDebug() << "updateTransits: Setting location from file" << locFile->getName() 
                 << "to" << locFile->getLocationName();
        _pendingLocationChange = true;
        _location->setLocation(locFile->getLocation());
        _location->setLocationName(locFile->getLocationName());
        _pendingLocationChange = false;
        
        // Update transitsAF if it's a different object than locFile
        if (transitsAF() != locFile) {
            transitsAF()->suspendUpdate();
            transitsAF()->setLocation(locFile->getLocation());
            transitsAF()->setLocationName(locFile->getLocationName());
            transitsAF()->setTimezone(locFile->getTimezone());
            transitsAF()->resumeUpdate();
        }
    } else if (filesCount() == 1 && file(0)->hasTransitLocation()) {
        // Restore location from file(0)'s stored transit fields
        qDebug() << "updateTransits: Restoring transit location from file(0)"
                 << file(0)->getTransitLocationName();
        _pendingLocationChange = true;
        _location->setLocation(file(0)->getTransitLocation());
        _location->setLocationName(file(0)->getTransitLocationName());
        _pendingLocationChange = false;

        // Sync to transitsAF() so the finder uses the right location
        transitsAF()->suspendUpdate();
        transitsAF()->setLocation(file(0)->getTransitLocation());
        transitsAF()->setLocationName(file(0)->getTransitLocationName());
        transitsAF()->setTimezone(file(0)->getTransitTimezone());
        transitsAF()->resumeUpdate();
    }

    // Process pending UI events (repaints, etc.) before doing heavy work
    // This ensures the chart and UI update before we start calculating events
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // Now check cache before doing any heavy calculation work
    ensureEventsModel();
    if (!_evm) return;
    
    auto& evs = file(0)->events();
    bool hasEvents = !evs.empty();
    bool needsRecalc = file(0)->needsEventsRecalc();

    // Check if there's a finder still active (paused or background) for this file
    bool hasActiveFinder = false;
    {
        auto fit = _finders.find(file(0));
        if (fit != _finders.end() && fit.value().thread && !fit.value().thread->isFinished()) {
            hasActiveFinder = true;
        }
    }

    // Detect if the pattern text changed since last calculation
    QString currentPattern = _input->currentText().trimmed();
    if (currentPattern != _lastUsedPattern) {
        needsRecalc = true;
        qDebug() << "[UPDATE TRANSITS] Pattern changed from"
                 << _lastUsedPattern << "to" << currentPattern
                 << "- forcing recalc";
    }
    
    qDebug() << "[UPDATE TRANSITS] file(0):" << file(0)->getName() 
             << "hasEvents:" << hasEvents << "evs.size():" << evs.size()
             << "needsRecalc:" << needsRecalc
             << "hasActiveFinder:" << hasActiveFinder;
    
    if (hasEvents && !needsRecalc && !hasActiveFinder) {
        // Events already cached and no finder running — repopulate the model and done
        qDebug() << "[UPDATE TRANSITS] Using cached events for file" << file(0)->getName();
        
        // Update model settings to match current file
        const A::Horoscope& scope(file()->horoscope());
        const auto& ida(transitsOnly() ? file()->horoscope().inputData
                                       : transitsAF()->horoscope().inputData);
        _evm->setZodiac(scope.zodiac);
        _evm->setTimezone(transitsAF()->getTimezone());
        
        // Repopulate model with cached events
        _evm->clearAllEvents();
        _evm->addEvents(evs);
        _evm->sort();
        
        return;
    }
    
    qDebug() << "[UPDATE TRANSITS] Recalculating events for file" << file(0)->getName();

    // Check if there's already a paused or background finder for this file
    auto fit = _finders.find(file(0));
    if (fit != _finders.end()) {
        auto& fs = fit.value();
        if (fs.finder && fs.thread && !fs.thread->isFinished()) {
            if (needsRecalc) {
                // Settings/dates changed — cancel old finder and start fresh
                qDebug() << "[UPDATE TRANSITS] Canceling stale finder for" << file(0)->getName()
                         << "(recalc needed)";
                cancelAndRemoveFinder(file(0));
                // Entry already removed from map — fall through to create new finder
            } else {
                // Finder is still alive (paused or running) — resume it
                qDebug() << "[UPDATE TRANSITS] Resuming existing finder for" << file(0)->getName()
                         << "state:" << fs.finder->getState();

                // Reconnect signals
                connect(fs.finder, SIGNAL(progress(double)), this, SLOT(onProgress(double)));
                connect(fs.thread, SIGNAL(finished()), this, SLOT(onCompleted()),
                        Qt::UniqueConnection);
                connect(this, SIGNAL(cancelActive()), fs.finder, SLOT(cancel()));

                // Set as current-tab active finder
                _active       = fs.thread;
                _activeFinder = fs.finder;
                _chs          = fs.chs;

                // Update model to show events accumulated so far
                const A::Horoscope& scope(file()->horoscope());
                _evm->setZodiac(scope.zodiac);
                _evm->setTimezone(transitsAF()->getTimezone());
                _evm->clearAllEvents();
                _evm->addEvents(file(0)->events());
                _evm->sort();

                // Resume if paused
                if (fs.finder->isPaused()) {
                    qDebug() << "[UPDATE TRANSITS] Calling resume() on paused finder";
                    fs.finder->resume();
                }
                return;
            }
        } else {
            // Thread finished while we weren't looking — clean up stale entry
            qDebug() << "[UPDATE TRANSITS] Stale finder entry for" << file(0)->getName() << ", removing";
            delete fs.chs;
            _finders.erase(fit);
        }
    }

    // If the current tab's finder is a different file's (shouldn't happen
    // after filesUpdated pauses it, but be safe), clear the aliases
    if (_active) {
        saveScrollPos();
        // The current _active belongs to a different file — it was already
        // paused/disconnected in filesUpdated(). Just clear the aliases.
        _active       = nullptr;
        _activeFinder = nullptr;
        _chs          = nullptr;
    } else {
        saveScrollPos();
    }

    // Clear the recalc flag only when we are actually about to start a new
    // computation (not in the deferred-restart path above, where we need the
    // flag to remain set so the restart knows to recompute).
    file(0)->clearEventsRecalcFlag();

    if (!_chs) {
        auto* evm = ensureEventsModel();
        if (evm) _chs = new AChangeSignalFrame(evm);
    }

    qDebug() << "filesCount()" << filesCount();

    auto       hs = A::dynAspState();
    ADateRange r { _start->date(), _end->date() };

    // Validate pattern BEFORE clearing events so that an invalid pattern
    // leaves the current event list intact (user can clear the field to
    // get back to toolbar-based computation).
    A::AspectFinder* af = nullptr;
    QString pattern = _input->currentText().trimmed();
    bool usePattern = false;
    if (!pattern.isEmpty()) {
        usePattern = A::EventOptions::isValidPattern(pattern);
        if (!usePattern) {
            qDebug() << "[UPDATE TRANSITS] Invalid pattern, skipping recomputation:" << pattern;
            return;
        }
    }
    _lastUsedPattern = pattern;

    // Pattern is valid (or empty) — now safe to clear events
    _evm->clearAllEvents();
    evs.clear();

#if 0
    transitsAF()->suspendUpdate();
    transitsAF()->setLocation(_location->location());
    transitsAF()->setLocationName(_location->locationName());
    transitsAF()->resumeUpdate();
#endif

#if 1
    if (usePattern && filesCount() >= 1) {
        qDebug() << "[UPDATE TRANSITS] Using pattern:" << pattern;
        auto type = file(0)->getType();
        if (type != TypeOther) {
            af = new A::OmnibusFinder(evs, r, hs,
                                      { file(0), transitsAF() }, pattern);
        } else {
            af = new A::OmnibusFinder(evs, r, hs, files(), pattern);
        }
    }
    if (!af && filesCount() >= 1) {
        auto type = file(0)->getType();
        qDebug() << "[UPDATE TRANSITS] filesCount:" << filesCount();
        qDebug() << "[UPDATE TRANSITS] file(0) name:" << file(0)->getName() << "type:" << type;
        if (filesCount() > 1) {
            qDebug() << "[UPDATE TRANSITS] file(1) name:" << file(1)->getName() << "type:" << file(1)->getType();
        }
        qDebug() << "[UPDATE TRANSITS] transitsAF() name:" << transitsAF()->getName() << "type:" << transitsAF()->getType();
        if (type != TypeOther) {
            qDebug() << "[UPDATE TRANSITS] Creating OmnibusFinder with file(0) and transitsAF()";
            // Create EventOptions with global settings but tab-specific event filter
            A::EventOptions opts = A::EventOptions::current();
            opts.enabledEvents = _tabEventOptions;
            opts.harmonicRestrictions = file(0)->getTransitHarmonicRestrictions();
            qDebug() << "[UPDATE TRANSITS] EventOptions: T=T:" << (opts.enabledEvents.count(A::etcTransitToTransit) > 0)
                     << "T=N:" << (opts.enabledEvents.count(A::etcTransitToNatal) > 0)
                     << "OT=N:" << (opts.enabledEvents.count(A::etcOuterTransitToNatal) > 0)
                     << "T=I:" << (opts.enabledEvents.count(A::etcSignIngress) > 0)
                     << "enabledEvents.size():" << opts.enabledEvents.size();
            af = new A::OmnibusFinder(evs, r, hs, { file(0), transitsAF() }, opts);
        }
    }
    if (!af && filesCount() >= 1) {
        qDebug() << "[UPDATE TRANSITS] Creating OmnibusFinder with files()";
        // Create EventOptions with global settings but tab-specific event filter
        A::EventOptions opts = A::EventOptions::current();
        opts.enabledEvents = _tabEventOptions;
        opts.harmonicRestrictions = file(0)->getTransitHarmonicRestrictions();
        qDebug() << "[UPDATE TRANSITS] EventOptions: T=T:" << (opts.enabledEvents.count(A::etcTransitToTransit) > 0)
                 << "T=N:" << (opts.enabledEvents.count(A::etcTransitToNatal) > 0)
                 << "OT=N:" << (opts.enabledEvents.count(A::etcOuterTransitToNatal) > 0)
                 << "T=I:" << (opts.enabledEvents.count(A::etcSignIngress) > 0)
                 << "enabledEvents.size():" << opts.enabledEvents.size();
        af = new A::OmnibusFinder(evs, r, hs, files(), opts);
    }
#else
    if (filesCount() == 1) {
        auto type = file(0)->getType();
        if (type == TypeMale || type == TypeFemale) {
            af = new A::OmnibusFinder(evs, r, hs, { file(0), transitsAF() }, _tabEventOptions);
        }
    }
    if (!af && filesCount() >= 1) {
        af = new A::OmnibusFinder(evs, r, hs, files(), _tabEventOptions);
    }
#endif
    if (!af) return;
    
    qDebug() << "[UPDATE TRANSITS] OmnibusFinder created with EventOptions:";
    qDebug() << "  showTransitsToTransits:" << af->showTransitsToTransits();
    qDebug() << "  showTransitsToNatalPlanets:" << af->showTransitsToNatalPlanets();
    qDebug() << "  includeOnlyOuterTransitsToNatal:" << af->includeOnlyOuterTransitsToNatal;

    const A::Horoscope& scope(file()->horoscope());
    const auto&         ida(transitsOnly() ? file()->horoscope().inputData
                                   : transitsAF()->horoscope().inputData);
    _evm->setZodiac(scope.zodiac);
    _evm->setTimezone(transitsAF()->getTimezone());
    _evm->addEvents(evs);

    auto thread = new QThread(this);
    QString chartName = file(0)->getName();
    QString threadName = QString("finder:%1").arg(chartName);
    thread->setObjectName(threadName);
    af->moveToThread(thread);

    qDebug() << "========================================";
    qDebug() << "[CREATE FINDER] Creating new finder thread for chart:" << chartName;
    qDebug() << "[CREATE FINDER] Thread:" << thread << threadName;
    qDebug() << "[CREATE FINDER] AspectFinder:" << af;
    
    // CRITICAL: These signal/slot connections cross thread boundaries
    // The finder thread emits signals that the main thread receives
    connect(this, SIGNAL(cancelActive()), af, SLOT(cancel()));
    connect(thread, SIGNAL(started()), af, SLOT(findStuff()));
    connect(af, SIGNAL(progress(double)), this, SLOT(onProgress(double)));
    connect(thread, SIGNAL(finished()), this, SLOT(onCompleted()));
    // Delete thread and finder after thread finishes (true completion or cancel)
    connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
    connect(thread, &QThread::finished, this, [af]() {
        qDebug() << "[DELETE FINDER] Deleting AspectFinder:" << af;
        delete af;
    });
    
    thread->start();
    _active       = thread;
    _activeFinder = af;

    // Store in per-file finder map
    AstroFile* ownerFile = file(0);
    if (ownerFile) {
        _finders[ownerFile] = FinderState { thread, af, _chs };

        // Clean up the map entry if the file is destroyed while finder is paused
        // Note: Qt::UniqueConnection can't be used with lambdas, so we
        // disconnect any prior destroyed-signal connection before reconnecting.
        disconnect(ownerFile, &QObject::destroyed, this, nullptr);
        connect(
            ownerFile,
            &QObject::destroyed,
            this,
            [this, ownerFile]() {
                qDebug()
                    << "[FILE DESTROYED] Cleaning up finder for destroyed file";
                auto it = _finders.find(ownerFile);
                if (it != _finders.end()) {
                    auto& fs = it.value();
                    if (fs.finder) fs.finder->cancel();
                    delete fs.chs;
                    if (fs.thread && !fs.thread->isFinished())
                        fs.thread->wait();
                    if (_active == fs.thread) {
                        _active       = nullptr;
                        _activeFinder = nullptr;
                        _chs          = nullptr;
                    }
                    _finders.erase(it);
                }
            });
    }
    
    qDebug() << "[CREATE FINDER] Started finder thread" << thread;
    qDebug() << "========================================";
}

void
Transits::onProgress(double prog)
{
    // Belt-and-suspenders: only accept progress from the current tab's finder.
    // After a tab switch _activeFinder is null, but queued cross-thread
    // progress() signals from the background finder may still arrive.
    if (!_activeFinder) return;
    auto* senderObj = sender();
    if (senderObj && senderObj != _activeFinder.data()) {
        return;
    }

    // Update the progress bar in the pattern input field
    updateInputProgress(prog);

    // Throttle sort operations during progress updates.
    // Uses a repeating timer so that the UI updates at a steady rate
    // regardless of how fast progress signals arrive.  Previous approach
    // used a single-shot "debounce" that restarted on every signal —
    // in release builds the worker is fast enough that signals arrived
    // faster than 100ms, so the timer never fired until computation ended.
    if (!_progressSortTimer) {
        _progressSortTimer = new QTimer(this);  // Parent ensures cleanup
        _progressSortTimer->setInterval(250);   // Update UI ~4 times/sec
        connect(_progressSortTimer, &QTimer::timeout, this, [this]() {
            if (_evm) {
                _evm->sort();
                // NOTE: restoreScrollPos() is NOT called here because the
                // model-reset signals (modelAboutToBeReset/modelReset) already
                // handle save/restore.  Calling it again here would overwrite
                // a user's active scroll position.
            }
        });
    }
    if (!_progressSortTimer->isActive()) {
        // Fire immediately on first progress signal, then every 250ms
        if (_evm) {
            _evm->sort();
        }
        _progressSortTimer->start();
    }
}

void
Transits::onCompleted()
{
#if 1
    qDebug() << "[ON COMPLETED] Starting cleanup, thread:" << _active.data() << "finder:" << _activeFinder.data();

    // Find which file this finder belongs to and remove from map.
    // Use sender() (the QThread that emitted finished()) to match,
    // because for background finders _active/_activeFinder point to
    // the *current* tab, not the background one.
    auto* senderThread = sender();
    AstroFile* ownerFile = nullptr;
    for (auto it = _finders.begin(); it != _finders.end(); ++it) {
        if (it.value().thread == senderThread
            || it.value().thread == _active
            || it.value().finder == _activeFinder) {
            ownerFile = it.key();
            // Delete _chs stored in the map entry
            delete it.value().chs;
            it.value().chs = nullptr;
            _finders.erase(it);
            break;
        }
    }

    bool isCurrentTab = (ownerFile == nullptr || (filesCount() > 0 && ownerFile == file(0)));
    qDebug() << "[ON COMPLETED] ownerFile:" << (ownerFile ? ownerFile->getName() : "unknown")
             << "isCurrentTab:" << isCurrentTab;

    if (!isCurrentTab) {
        // Background finder finished for a non-current tab.
        // Events are already written to ownerFile->events() by reference.
        // Just clear recalc flag and clean up — no UI updates needed.
        qDebug() << "[ON COMPLETED] Background finder done, no UI update needed";
        if (ownerFile) ownerFile->clearEventsRecalcFlag();
        // Don't touch _active/_activeFinder — they belong to the CURRENT tab.
        return;
    }

    // Current tab's finder completed
    if (_progressSortTimer && _progressSortTimer->isActive()) {
        _progressSortTimer->stop();
    }
    
    // Clear the progress bar in the pattern input field
    updateInputProgress(1.0);
    _lastProgress = -1;

    // Disconnect all signals from the finder before deletion to prevent
    // any queued progress() signals from being processed after deletion
    if (_activeFinder) {
        qDebug() << "[ON COMPLETED] Disconnecting ALL finder signals/slots";
        _activeFinder->disconnect();  // Disconnect all signals FROM the finder
        disconnect(_activeFinder.data());  // Disconnect all signals TO the finder
    }
    
    // _chs already deleted when removing from map above
    _chs = nullptr;
    
    qDebug() << "[ON COMPLETED] Sorting model";
    _evm->sort();
    qDebug() << "[ON COMPLETED] Sort complete, clearing pointers";
    _active = nullptr;
    _activeFinder = nullptr;
    
    // Mark that events are now calculated and cached
    if (filesCount() > 0) {
        file(0)->clearEventsRecalcFlag();
    }
    
    // Final restore attempt - if anchor can't be found now, it won't be found
    if (_anchor.isValid()) {
        restoreScrollPos();
        
        // Try one more time to see if we can find it
        bool matches = false;
        int col = _evm->sortColumn();
        auto order = _evm->sortOrder();
        int targetRow = _evm->rowForData(_anchor.event, matches, col, order == Qt::DescendingOrder);
        
        if (!matches || targetRow < 0) {
            // Event not found after completion - clear anchor
            qDebug() << "[ON COMPLETED] Anchor event not found, clearing anchor";
            _anchor.clear();
        }
    }
    
    qDebug() << "[ON COMPLETED] Cleanup complete";
#else
    const A::Horoscope& scope(file()->horoscope());
    const auto&         ida(transitsOnly() ? file()->horoscope().inputData
                                   : transitsAF()->horoscope().inputData);
    _evm->setZodiac(scope.zodiac);
    _evm->addEvents(_evs);
    // QTimer::singleShot(0,[this]{ _tview->expandAll(); });

#endif
    // _chs is already deleted earlier
}

void
Transits::setCurrentPlanet(A::PlanetId p, int file)
{
    if (_planet == p && _fileIndex == file) return;
    _planet    = p;
    _fileIndex = file;
    describePlanet();
}

void
Transits::onLocationChange()
{
    _pendingLocationChange = false;
    if (!transitsAF()) return;
    
    // Update the timezone which will refresh the time display
    // and potentially recalculate if needed
    updateTimezone();
}

void
Transits::findIt(const QString& val)
{
    auto sim = tvm();
    if (!sim) return;

    for (const auto& item : _evm->match(_evm->index(0, 0),
                                        Qt::DisplayRole,
                                        val,
                                        1,
                                        Qt::MatchExactly))
    {
        ttv()->scrollTo(item);
        ttv()->setExpanded(item, true);
        break;
    }
}

void
Transits::saveScrollPos()
{
    qDebug() << "[SAVE ANCHOR] Called";
    
    if (_inRestoreScrollPos) return;
    
    // Guard against calls during destruction
    if (!_evm || !_tview) return;

    if (_evm->rowCount() == 0) {
        _anchor.clear();
        return;
    }

    // Determine anchor type based on what triggered the save
    auto cur = ttv()->currentIndex();
    bool hasSelection = cur.isValid();
    
    // Check if this is from a scroll event by examining scroll bar position
    int currentScrollValue = ttv()->verticalScrollBar()->value();
    bool isScrollEvent = (_lastScrollValue >= 0 && currentScrollValue != _lastScrollValue);
    
    // Check whether the current selection is visible in the viewport.
    // If the user has scrolled the selection off-screen, we should NOT
    // use a Selection anchor (which would jerk the view back to it on
    // every progressive sort).  Instead fall through to the scroll-
    // position anchor below.
    bool selectionVisible = false;
    if (hasSelection) {
        QModelIndex topIndex  = ttv()->indexAt(ttv()->rect().topLeft());
        QModelIndex botIndex  = ttv()->indexAt(ttv()->rect().bottomLeft());
        if (topIndex.isValid() && botIndex.isValid()) {
            selectionVisible = (cur.row() >= topIndex.row()
                                && cur.row() <= botIndex.row());
        } else if (topIndex.isValid()) {
            // bottomLeft might be invalid if viewport is larger than model
            selectionVisible = (cur.row() >= topIndex.row());
        }
    }

    if (hasSelection && selectionVisible && !isScrollEvent) {
        // Selection anchor: preserve the selected row's position in viewport
        A::HarmonicEvent currentEvent = _evm->rowData(cur);
        
        // If we already have a Selection anchor for the same event, preserve its offset
        // This prevents offset drift when sorting/filtering without scrolling
        if (_anchor.type == AnchorType::Selection && _anchor.event == currentEvent) {
            // Just update sort info, keep existing offset
            _anchor.sortColumn = _evm->sortColumn();
            _anchor.sortOrder = _evm->sortOrder();
        } else {
            // New selection or different event - recalculate everything
            _anchor.event = currentEvent;
            _anchor.type = AnchorType::Selection;
            _anchor.sortColumn = _evm->sortColumn();
            _anchor.sortOrder = _evm->sortOrder();
            
            // Calculate offset from top of viewport
            QRect viewportRect = ttv()->rect();
            QModelIndex topIndex = ttv()->indexAt(viewportRect.topLeft());
            if (topIndex.isValid()) {
                _anchor.visibleRowOffset = cur.row() - topIndex.row();
            } else {
                _anchor.visibleRowOffset = 0;
            }
        }
    } else {
        // Scroll anchor: determine if Top or Bottom based on scroll direction
        if (isScrollEvent && currentScrollValue < _lastScrollValue) {
            // Scrolled up - use Bottom anchor
            QModelIndex bottom = ttv()->indexAt(ttv()->rect().bottomLeft());
            if (bottom.isValid()) {
                _anchor.event = _evm->rowData(bottom);
                _anchor.type = AnchorType::Bottom;
                _anchor.sortColumn = _evm->sortColumn();
                _anchor.sortOrder = _evm->sortOrder();
                _anchor.visibleRowOffset = -1;
            }
        } else {
            // Scrolled down or other case - use Top anchor
            QModelIndex top = ttv()->indexAt(ttv()->rect().topLeft());
            if (top.isValid()) {
                _anchor.event = _evm->rowData(top);
                _anchor.type = AnchorType::Top;
                _anchor.sortColumn = _evm->sortColumn();
                _anchor.sortOrder = _evm->sortOrder();
                _anchor.visibleRowOffset = -1;
            }
        }
    }
    
    _lastScrollValue = currentScrollValue;
}

void
Transits::restoreScrollPos()
{
    // Guard against calls during destruction
    if (!_evm || !_tview) return;
    
    if (!_anchor.isValid()) return;

    if (_inRestoreScrollPos) return;

    A::modalize<bool> irsp(_inRestoreScrollPos);
    int               col   = _evm->sortColumn();
    auto              order = _evm->sortOrder();
    
    // Check if sort order changed - if so, we need to find the item again
    bool sortChanged = (col != _anchor.sortColumn || order != _anchor.sortOrder);
    
    // Try to find the anchored event in the current model
    bool matches = false;
    int targetRow = _evm->rowForData(_anchor.event, matches, col, order == Qt::DescendingOrder);
    
    if (!matches || targetRow < 0) {
        // Event not found yet (still calculating) or not found at all
        // During progressive updates, this is normal - just return and wait
        // After final completion, onCompleted will clear invalid anchors
        return;
    }
    
    // Found the event - restore based on anchor type
    QModelIndex targetIndex = _evm->index(targetRow, 0);
    
    switch (_anchor.type) {
        case AnchorType::Selection:
            // Restore selected row at its previous visual offset
            {
                QSignalBlocker blocker(ttv()->selectionModel());
                ttv()->setCurrentIndex(targetIndex);
            }
            
            // Scroll to maintain the same visual offset from top
            if (_anchor.visibleRowOffset >= 0) {
                int scrollToRow = targetRow - _anchor.visibleRowOffset;
                if (scrollToRow >= 0) {
                    QModelIndex scrollToIndex = _evm->index(scrollToRow, 0);
                    ttv()->scrollTo(scrollToIndex, QAbstractItemView::PositionAtTop);
                } else {
                    ttv()->scrollTo(targetIndex, QAbstractItemView::PositionAtTop);
                }
            } else {
                ttv()->scrollTo(targetIndex, QAbstractItemView::PositionAtCenter);
            }
            break;
            
        case AnchorType::Top:
            // Scroll so this item is at the top of the viewport
            ttv()->scrollTo(targetIndex, QAbstractItemView::PositionAtTop);
            break;
            
        case AnchorType::Bottom:
            // Scroll so this item is at the bottom of the viewport
            ttv()->scrollTo(targetIndex, QAbstractItemView::PositionAtBottom);
            break;
            
        case AnchorType::None:
        default:
            break;
    }
    
    // Update anchor's sort info if it changed
    if (sortChanged) {
        _anchor.sortColumn = col;
        _anchor.sortOrder = order;
    }
}

void
Transits::clickedCell(QModelIndex inx)
{
    if (!inx.isValid()) return;
    if (!_evm) return;
    if (inx.row() < 0 || inx.row() >= _evm->rowCount()) return;
    
    // Inhibit filesUpdated → updateTransits for the ENTIRE duration of this
    // handler.  Several signals emitted below (updateHarmonics, updateFirst,
    // updateSecond) can bounce back through AstroFileHandler::filesUpdated.
    // Without this guard the running search gets cancelled on every click.
    A::modalize<bool> noup(_inhibitUpdate);

    // Save scroll position when user clicks a cell (creates selection anchor)
    saveScrollPos();
    
    auto btns = QGuiApplication::mouseButtons();
    bool mbtn = (btns & Qt::MiddleButton);
    bool lbtn = (btns & Qt::LeftButton);
    bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier);
    if (lbtn && ctrl) lbtn = false, mbtn = true;

    auto* aw = MainWindow::theAstroWidget();
    if (!aw) return;
    A::modalize<A::AspectSetId> aset(aw->overrideAspectSet(), -1);
    A::PlanetSet focal;
    double clickHarmonic = 0;  // non-zero when we quietly set harmonic
    if (inx.column() == EventsTableModel::harmonicCol) {
        auto v = inx.data(EventsTableModel::RawRole);
        qDebug() << v;
        if (v.canConvert<unsigned>()) {
            double h = v.toUInt();
            emit   updateHarmonics(h);
        }
    } else if (inx.column() == EventsTableModel::dateCol) {
        emit updateHarmonics(1);
    } else if (inx.column() >= EventsTableModel::transitBodyCol) {
        auto v = inx.sibling(inx.row(), EventsTableModel::harmonicCol)
                     .data(EventsTableModel::RawRole);
        focal = inx.data(EventsTableModel::RawRole).value<A::PlanetSet>();
        if (focal.size() > 1) {
            unsigned h = v.toUInt();
            // Check whether the focal set contains any midpoints (A=B/C).
            bool hasMidpoint = std::any_of(focal.begin(), focal.end(),
                [](const auto& cpid) { return cpid.isMidpt(); });
            if (hasMidpoint) {
                // Switch the chart wheel to the event's harmonic so that
                // midpoint chords and harmonic aspect lines are naturally
                // visible.  At H(h) positions the standard (H1) aspect set
                // finds the conjunctions that represent the original H(h)
                // pattern.
                //
                // Use setHarmonicQuietly to update the combo box and
                // apply harmonic to current files WITHOUT triggering
                // horoscopeControlChanged -> ds.apply -> premature chart
                // redraw (which would fire before focal planets are set).
                MainWindow::theAstroWidget()->setHarmonicQuietly(h);
                clickHarmonic = h;
                aset = A::topAspectSet().id + 1;
            } else {
                aset = A::topAspectSet().id + h;
            }
        }
    } else {
        // Any other column (e.g., Event Type): revert to H1
        emit updateHarmonics(1);
    }

    auto par = inx.parent();
    if (par.isValid()) inx = par;
    if (mbtn) {
        doubleClickedCell(inx);
        return;
    }

    // Re-validate row after potential model changes from signals above
    if (inx.row() < 0 || inx.row() >= _evm->rowCount()) return;

    auto    dt = _evm->rowDate(inx.row());
    if (!dt.isValid()) return;
    auto    ev = _evm->rowData(inx.row());
    auto    et = ev.eventType();
    QString desc;
    if (focal.empty()) desc = _evm->rowDesc(inx.row());
    else {
        desc =
            inx.siblingAtColumn(EventsTableModel::harmonicCol).data().toString()
            + " " + describePlanetsForEvent(focal, et);
    }
    qDebug() << "[MIDPT-NAME] clickedCell: focal.size()=" << focal.size()
             << "desc=" << desc;
    for (const auto& cpid : focal) {
        qDebug() << "[MIDPT-NAME]   cpid: fid=" << cpid.fileId()
                 << "pid=" << int(cpid.planetId())
                 << "pid2=" << int(cpid.planetId2())
                 << "isMidpt=" << cpid.isMidpt()
                 << "name=" << cpid.name();
    }
    if (!file()) return;  // guard against no file
    if (transitsOnly()) {
        file()->setFocalPlanets(focal);
        file()->setName(desc);
        file()->setGMT(dt);
        // Set file type to Return for return events
        if (et == A::etcSolarReturn || et == A::etcLunarReturn) {
            file()->setType(TypeReturn);
        }
        // Don't emit updateFirst here — setGMT/setName/setFocalPlanets
        // already fire changed() which propagates to Chart and all other
        // handlers.  updateFirst would route through FilesBar::openFile()
        // which stops the running finder thread and resets the date range.
    } else {
        auto* taf = transitsAF();
        if (!taf) return;
        // Grr make transit planets be in fileId 1
        A::PlanetSet shift;
        for (auto cpid : focal) {
            if (cpid.fileId() == 0) {
                cpid.setFileId(1);
                shift.emplace(cpid);
            }
        }
        if (shift.size() == focal.size()) focal.swap(shift);

        taf->suspendUpdate();
        if (clickHarmonic > 0)
            taf->setHarmonic(clickHarmonic);
        taf->setFocalPlanets(focal);
        taf->setName(desc);
        taf->setGMT(dt);
        // Set file type and base chart based on event type
        // Base chart stores the natal chart relationship for all event types
        if (et == A::etcSolarReturn || et == A::etcLunarReturn
            || et == A::etcReturn)
        {
            taf->setType(TypeReturn);
            taf->setBaseChart(file()->getGMT());
        } else if (et == A::etcProgressedToProgressed
                   || et == A::etcProgressedToNatal
                   || et == A::etcInnerProgressedToNatal
                   || et == A::etcTransitToProgressed)
        {
            taf->setType(TypeDerivedProg);
            taf->setBaseChart(file()->getGMT());
        } else {
            // For transit events (T=T, T=N, patterns, ingresses, etc.)
            // Set base chart to track natal relationship, but use TypeOther
            taf->setType(TypeOther);
            taf->setBaseChart(file()->getGMT());
        }
        taf->resumeUpdate();

        // Clear unsaved state since this is a generated chart from an event
        taf->clearUnsavedState();
        
        emit updateSecond(taf);
        if (_trans && _trans->parent() != this) _trans = nullptr;
    }
    ttv()->scrollTo(inx);
    if (_chs) saveScrollPos();
}

void
Transits::doubleClickedCell(QModelIndex inx)
{
    if (!inx.isValid()) return;
    if (!_evm) return;
    if (inx.row() < 0 || inx.row() >= _evm->rowCount()) return;

    //bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier);
    bool shift = (QApplication::keyboardModifiers() & Qt::ShiftModifier);

    auto par = inx.parent();
    if (par.isValid()) inx = par;
    int row = inx.row();
    if (row < 0 || row >= _evm->rowCount()) return;
    auto              dt   = _evm->rowDate(row);
    if (!dt.isValid()) return;
    auto              ev   = _evm->rowData(row);
    auto              et   = ev.eventType();
    auto              desc = _evm->rowDesc(row);
    A::modalize<bool> noup(_inhibitUpdate);
    auto* aw = MainWindow::theAstroWidget();
    if (!aw) return;
    AstroFile*        af = new AstroFile;
    aw->setupFile(af);
    af->suspendUpdate();
    af->setLocation(_location->location());
    af->setLocationName(_location->locationName());
    if (!transitsOnly() && file() && !shift) {
        af->setName(file()->getName() + " - " + desc);
    } else {
        af->setName(desc);
    }
    af->setGMT(dt);
    
    // Set file type and base chart based on event type
    if (et == A::etcSolarReturn || et == A::etcLunarReturn) {
        af->setType(TypeReturn);
        // Set the natal chart as the base for return calculations
        if (!transitsOnly() && file()) {
            af->setBaseChart(file()->getGMT());
        } else {
            af->clearBaseChart();
        }
    } else if (et == A::etcProgressedToProgressed 
        || et == A::etcProgressedToNatal
        || et == A::etcInnerProgressedToNatal
        || et == A::etcTransitToProgressed) {
        af->setType(TypeDerivedProg);
        // Set the natal chart as the base for progressions
        if (!transitsOnly() && file()) {
            af->setBaseChart(file()->getGMT());
        } else {
            af->clearBaseChart();
        }
    }

    // Apply chart preset if one exists for this event type
    af->setOriginEventType(et);
    if (auto* preset = A::ChartPreset::forEvent(et)) {
        if (!preset->enabledEvents.empty())
            af->setTransitEventOptions(preset->enabledEvents);
        if (preset->timespan)
            af->setTransitDuration(preset->timespan.toString());
        if (preset->startOffset) {
            QDate chartDate = dt.date();
            af->setTransitStartDate(preset->startOffset.addTo(chartDate));
        }
        if (!preset->harmonicFilters.isEmpty())
            af->setTransitHarmonicRestrictions(preset->harmonicFilters);
        if (!preset->pattern.isEmpty())
            af->setTransitPattern(preset->pattern);
    }

    af->resumeUpdate();
    
    // Clear unsaved state since this is a generated chart from an event
    af->clearUnsavedState();
    
    // Allow filesUpdated() to run during tab creation so the new tab
    // gets its own event search and Transits state is properly updated.
    // The guard is no longer needed past this point — all file mutations
    // are done and only the tab-creation signals remain.
    noup = false;

    // bool shift = (QApplication::keyboardModifiers() & Qt::ShiftModifier);
    if (transitsOnly() || !shift || !file()) {
        emit addChart(af);
    } else {
        emit addChartWithTransits(file()->fileInfo(), af);
        if (_trans && _trans->parent() != this) _trans = nullptr;
    }
}

void
Transits::headerDoubleClicked(int col)
{
    // nothing doing now...
}

void
Transits::headerClicked(int col)
{
    // This method is now called only for Ctrl+click events from our custom
    // header
    if (!_evm) return;
    
    if (col == EventsTableModel::transitBodyCol) {
        _evm->cycleTransitBodyColMode();
        // Force immediate visual update
        _tview->viewport()->update();
        _tview->update();
    } else if (col == EventsTableModel::natalTransitBodyCol) {
        _evm->cycleNatalTransitBodyColMode();
        // Force immediate visual update
        _tview->viewport()->update();
        _tview->update();
    }
}

QString
EventsTableModel::exportToHtml(AstroFile* natalFile, AstroFile* transitFile) const
{
    QString html;
    html += "<!DOCTYPE html>\n<html>\n<head>\n";
    html += "<meta charset=\"UTF-8\">\n";
    html += "<style>\n";
    html += "body { font-family: Arial, sans-serif; margin: 20px; font-size: 9pt; line-height: 1.2; }\n";
    html += "h2 { color: #333; border-bottom: 2px solid #666; padding-bottom: 5px; font-size: 11pt; margin: 10px 0; }\n";
    html += "h3 { color: #555; margin-top: 15px; margin-bottom: 8px; font-size: 10pt; }\n";
    html += ".chart-info { background: #f5f5f5; padding: 8px; margin: 8px 0; border-radius: 5px; font-size: 9pt; }\n";
    html += ".chart-info p { margin: 3px 0; }\n";
    html += "table { border-collapse: collapse; width: 100%; margin-top: 15px; font-size: 8pt; line-height: 1.1; }\n";
    html += "th { background: #666; color: white; padding: 3px 4px; text-align: left; font-weight: bold; }\n";
    html += "td { border: 1px solid #ddd; padding: 2px 4px; }\n";
    html += "tr:nth-child(even) { background: #f9f9f9; }\n";
    html += "tr:hover { background: #e9e9e9; }\n";
    html += ".date-range { font-size: 0.9em; color: #666; }\n";
    html += "</style>\n</head>\n<body>\n";
    
    // Add title
    html += "<h2>Astrological Events Report</h2>\n";
    
    // Natal chart information
    if (natalFile) {
        html += "<h3>Natal Chart</h3>\n";
        html += "<div class=\"chart-info\">\n";
        html += QString("<p><strong>Name:</strong> %1</p>\n").arg(natalFile->getName());
        
        auto dt = natalFile->getLocalTime();
        html += QString("<p><strong>Date:</strong> %1 %2</p>\n")
            .arg(QLocale().toString(dt.date(), QLocale::LongFormat))
            .arg(dt.time().toString());
        
        QString tzStr;
        double tz = natalFile->getTimezone();
        if (tz > 0) tzStr = QString("GMT +%1").arg(tz);
        else if (tz < 0) tzStr = QString("GMT %1").arg(tz);
        else tzStr = "GMT";
        html += QString("<p><strong>Timezone:</strong> %1</p>\n").arg(tzStr);
        
        QString location = natalFile->getLocationName();
        if (location.isEmpty()) {
            location = QString("%1, %2")
                .arg(A::formatLatitude(natalFile->getLocation().y()))
                .arg(A::formatLongitude(natalFile->getLocation().x()));
        }
        html += QString("<p><strong>Location:</strong> %1</p>\n").arg(location);
        html += "</div>\n";
    }
    
    // Transit/progression location information
    if (transitFile && transitFile != natalFile) {
        html += "<h3>Transit/Progression Location</h3>\n";
        html += "<div class=\"chart-info\">\n";
        
        QString location = transitFile->getLocationName();
        if (location.isEmpty()) {
            location = QString("%1, %2")
                .arg(A::formatLatitude(transitFile->getLocation().y()))
                .arg(A::formatLongitude(transitFile->getLocation().x()));
        }
        html += QString("<p><strong>Location:</strong> %1</p>\n").arg(location);
        
        QString tzStr;
        double tz = transitFile->getTimezone();
        if (tz > 0) tzStr = QString("GMT +%1").arg(tz);
        else if (tz < 0) tzStr = QString("GMT %1").arg(tz);
        else tzStr = "GMT";
        html += QString("<p><strong>Timezone:</strong> %1</p>\n").arg(tzStr);
        html += "</div>\n";
    }
    
    // Events table
    html += "<h3>Events</h3>\n";
    html += "<table>\n<thead>\n<tr>\n";
    html += "<th>Event Type</th>\n";
    html += "<th>Date/Time</th>\n";
    html += "<th>Harmonic/Orb</th>\n";
    html += "<th>Transit Bodies</th>\n";
    html += "<th>Natal/Transit Bodies</th>\n";
    html += "</tr>\n</thead>\n<tbody>\n";
    
    // Add each event row
    for (int row = 0; row < _evs.size(); ++row) {
        html += "<tr>\n";
        
        // Event type (bold)
        auto et = _evs[row]->eventType();
        html += QString("<td><strong>%1</strong></td>\n").arg(A::EventTypeManager::eventTypeToBrief(et));
        
        // Date/time with range if available
        auto dt = _evs[row]->dateTime().toTimeZone(QTimeZone(_tzOffset * 3600));
        QString dateStr = dt.toString("yyyy-MM-dd hh:mm:ss");
        
        auto&& r = _evs[row]->range();
        if (r != A::ADateTimeRange()) {
            auto dtfrom = r.first.toTimeZone(QTimeZone(_tzOffset * 3600));
            auto dtto = r.second.toTimeZone(QTimeZone(_tzOffset * 3600));
            dateStr += QString("<br/><span class=\"date-range\">Range: %1 to %2</span>")
                .arg(dtfrom.toString("yyyy-MM-dd hh:mm"))
                .arg(dtto.toString("yyyy-MM-dd hh:mm"));
        }
        html += QString("<td>%1</td>\n").arg(dateStr);
        
        // Harmonic/Orb - include ratio and only show orb if non-zero
        auto& asp = *_evs[row];
        QString harmonicStr = QString("H%1").arg(asp.harmonic());
        
        // Add aspect ratio if available and setting is enabled
        typedef std::pair<const A::Loc*, const A::Loc*> locPair;
        locPair pp;
        if (A::EventOptions::current().showHarmonicDividend && getPlanetPair(asp.locations(), pp)) {
            auto a = A::calculateAspect(aspects(), pp.first, pp.second);
            if (a.d && a.d->_harmonic > 0) {
                harmonicStr += " (" + a.d->name + ")";
            }
        }
        
        // Only show orb if non-zero
        if (asp.orb() != qreal()) {
            harmonicStr += " " + A::degreeToString(asp.orb(), A::HighPrecision);
        }
        
        html += QString("<td>%1</td>\n").arg(harmonicStr);
        
        // Transit bodies (text names) - use column separation logic
        QStringList transitBodies;
        if (asp.locations().empty()) {
            // Use planets
            if (!singleColumn(asp.planets())) {
                auto [begin, end] = getTColIters(asp.planets());
                for (auto it = begin; it != end; ++it) {
                    transitBodies << this->planetToText(*it);
                }
            } else {
                for (const auto& cpid : asp.planets()) {
                    transitBodies << this->planetToText(cpid);
                }
            }
        } else {
            // Use locations (PlanetLoc) - faster planet goes in Transit column
            auto [begin, end] = getTColIters(asp.locations());
            for (auto it = begin; it != end; ++it) {
                transitBodies << this->planetToText(*it);
            }
        }
        html += QString("<td>%1</td>\n").arg(transitBodies.join(", "));
        
        // Natal/transit bodies (slower planet or natal planet)
        QStringList natalTransitBodies;
        if (asp.locations().empty()) {
            // Use planets
            if (!singleColumn(asp.planets())) {
                auto [begin, end] = getNTColIters(asp.planets());
                for (auto it = begin; it != end; ++it) {
                    natalTransitBodies << this->planetToText(*it);
                }
            }
        } else {
            // Use locations (PlanetLoc) - slower planet goes in T/N column
            if (!singleColumn(asp.locations())) {
                auto [begin, end] = getNTColIters(asp.locations());
                for (auto it = begin; it != end; ++it) {
                    natalTransitBodies << this->planetToText(*it);
                }
            }
        }
        html += QString("<td>%1</td>\n").arg(natalTransitBodies.join(", "));
        
        html += "</tr>\n";
    }
    
    html += "</tbody>\n</table>\n";
    html += "</body>\n</html>\n";
    
    return html;
}

QString
EventsTableModel::planetToText(const A::ChartPlanetModeId& cpid) const
{
    // Use 3-letter abbreviation
    QString name = cpid.name().remove(' ').left(3);
    
    // Add mode suffix if applicable
    QString suffix = modeToSuffix(cpid.mode());
    if (!suffix.isEmpty()) {
        name += "-" + suffix;
    }
    
    return name;
}

QString
EventsTableModel::planetToText(const A::PlanetLoc& ploc) const
{
    // Use 3-letter abbreviation
    QString name = ploc.planet.name().remove(' ').left(3);
    
    // Add mode suffix
    QString suffix = modeToSuffix(ploc.mode());
    if (!suffix.isEmpty()) {
        name += "-" + suffix;
    }
    
    // Add descriptor (SD, SR, etc.)
    if (!ploc.desc.isEmpty()) {
        name += "-" + ploc.desc;
    }
    
    // Add position
    name += " " + A::zodiacPosition(ploc.rasiLoc(), _zodiac, A::HighPrecision, ploc.speed < 0);
    
    // Add retrograde indicator
    if (ploc.speed < 0 && !ploc.desc.startsWith("S")) {
        name += " (R)";
    }
    
    return name;
}

void
Transits::copySelection()
{
    if (auto sim = tvm()) {
        if (!_tview) return;
        
        QClipboard* cb = QApplication::clipboard();
        if (const QMimeData* md = cb->mimeData()) {
            if (md->hasText()) {
                qDebug() << md->text();
            } else if (md->hasHtml()) {
                qDebug() << md->html();
            }
        }
        QItemSelectionModel* sm   = _tview->selectionModel();
        QModelIndexList      qmil = sm->selectedIndexes();
        qDebug() << qmil;
        QMimeData* md = sim->mimeData(qmil);
        if (md) {
            qDebug() << md->formats();
            cb->setMimeData(md);
        }
    }
}

void
Transits::copyTableAsRichText()
{
    if (!_evm || filesCount() == 0) return;
    
    // Determine natal and transit files
    AstroFile* natal = file(0);
    AstroFile* transit = transitsAF();
    
    // Generate HTML
    QString html = _evm->exportToHtml(natal, transit);
    
    // Copy to clipboard as both HTML and plain text
    QClipboard* clipboard = QApplication::clipboard();
    QMimeData* mimeData = new QMimeData();
    
    mimeData->setHtml(html);
    
    // Also provide plain text version (strip HTML tags for basic compatibility)
    QTextDocument doc;
    doc.setHtml(html);
    mimeData->setText(doc.toPlainText());
    
    clipboard->setMimeData(mimeData);
}

void
Transits::clear()
{
    _planet = A::Planet_None;
}

EventsTableModel*
Transits::tvm() const
{
    return qobject_cast<EventsTableModel*>(_tview->model());
}

void
Transits::onEventSelectionChanged()
{
    updateTransits();
}

void
Transits::onDateRangeChanged()
{
    // Save date range to file(0) for per-tab persistence
    if (filesCount() > 0) {
        file(0)->setTransitStartDate(_start->date());
        file(0)->setTransitDuration(_duration->text());
        
        // Mark that events need recalculation
        file(0)->markEventsForRecalc();
    }
    // Honor auto-recalc state
    if (_actAutoRecalc && _actAutoRecalc->isChecked()) {
        updateTransits();
    }
}

void
Transits::updateDelta(const QDate& ed)
{
    ADateDelta delta(_start->date(), ed);
    if (delta != _ddelta) {
        _ddelta = delta;
        /*block*/ {
            ASignalBlocker sb(_duration);
            _duration->setText(_ddelta.toString());
        }
        onDateRangeChanged();
    }
}

void
Transits::onStartChanged()
{
    auto sd = _start->date();
    if (_grp->checkedId() == 0) {
        auto newDate = _ddelta.addTo(sd);
        if (_end->date() != newDate) {
            /*block*/ {
                ASignalBlocker sb(_end);
                _end->setDate(newDate);
            }
            onDateRangeChanged();
        }
    } else {
        updateDelta(_end->date());
    }
}

void
Transits::onEndChanged()
{
    auto sd = _end->date();
    if (_grp->checkedId() == 0) {
        auto newDate = _ddelta.subtractFrom(sd);
        if (_start->date() != newDate) {
            /*block*/ {
                ASignalBlocker sb(_start);
                _start->setDate(newDate);
            }
            onDateRangeChanged();
        }
    } else {
        updateDelta(sd);
    }
}

void
Transits::onDurationChanged()
{
    ADateDelta delta = _duration->text();
    if (!delta) delta = QString("1 mo"); // revert to default

    if (delta != _ddelta) {
        _ddelta  = delta;
        auto str = _ddelta.toString();
        if (str != _duration->text()) {
            /*block*/ {
                ASignalBlocker sb(_duration);
                _duration->setText(str);
            }
        }
        _end->setDate(_ddelta.addTo(_start->date()));
        onDateRangeChanged();
    }
}

void
Transits::filesUpdated(MembersList m)
{
    qDebug() << "========================================";
    qDebug() << "[TRANSITS filesUpdated] Called with" << m.size() << "files";
    for (int i = 0; i < m.size() && i < filesCount(); ++i) {
        auto ml = m[i];
        qDebug() << "  File" << i << "members:" << QString::number(ml, 16);
        if (ml & AstroFile::GMT) qDebug() << "    - GMT changed";
        if (ml & AstroFile::Location) qDebug() << "    - Location changed";
    }
    qDebug() << "========================================";
    
    if (!isVisible()) return;
    if (_inhibitUpdate) return;
    if (!filesCount()) {
        clear();
        return;
    }
    
    // Save current event options to previous file(0) if it exists
    bool fileChanged = (_previousFile != file(0));
    // Use |= so that a re-entrant filesUpdated() (triggered by
    // setTransitEventOptions → changed() → fileUpdatedSlot) cannot
    // clobber the flag before viewSettingsUpdated() reads it.
    _fileJustSwitched |= fileChanged;
    
    // Pause/disconnect the previous tab's finder on tab switch
    if (fileChanged && _previousFile) {
        auto fit = _finders.find(_previousFile);
        if (fit != _finders.end()) {
            auto& fs = fit.value();
            if (fs.finder && fs.thread && !fs.thread->isFinished()) {
                qDebug() << "[FILES UPDATED] Tab switch: disconnecting finder for" << _previousFile->getName();
                if (!_backgroundFinders) {
                    disconnectFinder(fs);
                    qDebug() << "[FILES UPDATED] Pausing finder for" << _previousFile->getName();
                    fs.finder->pause();
                } else {
                    // Background mode: disconnect UI signals but keep
                    // thread→onCompleted() so cleanup runs when it finishes.
                    if (fs.finder) {
                        disconnect(fs.finder.data(), SIGNAL(progress(double)),
                                   this, SLOT(onProgress(double)));
                        disconnect(this, SIGNAL(cancelActive()),
                                   fs.finder.data(), SLOT(cancel()));
                    }
                    if (_progressSortTimer && _progressSortTimer->isActive())
                        _progressSortTimer->stop();
                    qDebug() << "[FILES UPDATED] Background mode: leaving finder running for" << _previousFile->getName();
                }
            }
        }
        // Clear current-tab aliases — updateTransits() will set them for the new file
        _active       = nullptr;
        _activeFinder = nullptr;
        _chs          = nullptr;
        updateInputProgress(1.0);
        _lastProgress = -1;
    }
    
    // Guard file mutations that emit changed() to prevent re-entrant
    // dispatches back into this handler.  The ChangedState signal is
    // irrelevant for Transits so nothing is lost.
    {
        A::modalize<bool> guard(_inhibitUpdate, true);

        if (_previousFile && _previousFile != file(0)) {
            qDebug() << "[FILES UPDATED] Saving event options from previous file" << _previousFile->getName();
            _previousFile->setTransitEventOptions(_tabEventOptions);
            _previousFile->setTransitPattern(_input->currentText());
        }
        
        // Load event options from new file(0)
        if (file(0)) {
            qDebug() << "[FILES UPDATED] Loading event options for file" << file(0)->getName();
            _tabEventOptions = file(0)->getTransitEventOptions();
            
            // If file has no saved options (empty set), initialize from global defaults
            if (_tabEventOptions.empty()) {
                qDebug() << "  No saved options, using global defaults";
                _tabEventOptions = A::EventOptions::globalDefaults();
                file(0)->setTransitEventOptions(_tabEventOptions);
            }

            // Restore per-event-type harmonic restrictions
            if (_evm) {
                _evm->setHarmonicRestrictions(
                    file(0)->getTransitHarmonicRestrictions());
            }
            
            // Update toolbar to reflect the loaded event options
            updateToolbarFromEventOptions();
            
            // Restore per-tab pattern input field
            // Refresh combo items from global MRU before restoring per-tab text
            {
                QStringList mru = loadPatternMRU();
                _input->blockSignals(true);
                _input->clear();
                _input->addItems(mru);
                _input->blockSignals(false);
                if (auto* c = _input->completer())
                    static_cast<QStringListModel*>(c->model())->setStringList(mru);
            }
            _input->setCurrentText(file(0)->getTransitPattern());
            _lastUsedPattern = file(0)->getTransitPattern();
            
            _previousFile = file(0);
        }
    }  // ~guard restores _inhibitUpdate

#if 0
    // XXX need a better division of in-process update and final update
    if (QApplication::mouseButtons() & Qt::LeftButton) return;
#endif

    // Restore date range from file(0) when switching tabs (BEFORE any updates)
    QDate transitStart = file(0)->getTransitStartDate();
    QString transitDuration = file(0)->getTransitDuration();
    
    // If no saved date range, initialize with defaults
    if (transitStart.isNull() || transitDuration.isEmpty()) {
        auto today = QDate::currentDate();
        auto startOfMonth = QDate(today.year(), today.month(), 1);
        QString defaultDuration = "1 mo";
        
        // Save defaults to file so it has its own state
        file(0)->setTransitStartDate(startOfMonth);
        file(0)->setTransitDuration(defaultDuration);
        
        transitStart = startOfMonth;
        transitDuration = defaultDuration;
    }
    
    // Always restore from file(0) to ensure each tab has independent dates
    {
        ASignalBlocker sb({_start, _duration, _end});
        _start->setDate(transitStart);
        _duration->setText(transitDuration);
        _ddelta = ADateDelta::fromString(transitDuration);
        _end->setDate(_ddelta.addTo(_start->date()));
    }

    while (m.size() < filesCount()) m.append(AstroFile::Member());

    bool any = false;
    bool needsRecalc = false;
    int  f   = 0;
    for (auto ml : m) {
        FileType type = file(f)->getType();
        if (type < TypeSearch) {
            if (f == 0 ? (type <= TypeReturn)
                       : (type == TypeMale || type == TypeFemale
                          || type == TypeEvent))
            {
                // For natal/event charts (file 0), check GMT and Location
                any |= (ml & (AstroFile::GMT | AstroFile::Location));
                needsRecalc |= (ml & (AstroFile::GMT | AstroFile::Location));
            }

            // Timezone is file-data that affects display
            any |= (ml & AstroFile::Timezone);
        }
        f++;
    }
    qDebug() << "[TRANSITS filesUpdated] any=" << any << "needsRecalc=" << needsRecalc;
    if (any) {
#if OLDMODEL
        auto zap = _tm;
        _tview->setModel(nullptr);
        _tm = nullptr;
        zap->deleteLater();
#else
        auto* evm = ensureEventsModel();
        if (!evm) return;
        
        bool hasActiveFinderForFile = false;
        {
            auto fit2 = _finders.find(file(0));
            if (fit2 != _finders.end() && fit2.value().thread
                && !fit2.value().thread->isFinished()) {
                hasActiveFinderForFile = true;
            }
        }
        bool shouldRecalc = (file(0)->events().empty() && !hasActiveFinderForFile)
                            || (needsRecalc && !fileChanged);
        
        if (filesCount() > 0 && shouldRecalc) {
            if (needsRecalc && !fileChanged) {
                qDebug() << "[FILES UPDATED] Marking for recalc due to data change";
            } else if (file(0)->events().empty()) {
                qDebug() << "[FILES UPDATED] Marking for recalc due to empty events cache";
            }
            file(0)->markEventsForRecalc();
            // Note: Don't call updateTransits() here — describePlanet() below
            // will call it, avoiding a double-call that would block the main
            // thread waiting for the first finder to finish.
        }
        
        if (!_chs) _chs = new AChangeSignalFrame(evm);
        evm->setAspectSet(file()->getAspectSetId());
#endif
        describePlanet();
    }
}

void
Transits::viewSettingsUpdated(MembersList m)
{
    qDebug() << "========================================";
    qDebug() << "[TRANSITS viewSettingsUpdated] Called";
    for (int i = 0; i < m.size() && i < filesCount(); ++i) {
        auto ml = m[i];
        qDebug() << "  File" << i << "view flags:" << QString::number(ml, 16);
        if (ml & AstroFile::AspectMode) qDebug() << "    - AspectMode changed";
        if (ml & AstroFile::AspectSet) qDebug() << "    - AspectSet changed";
        if (ml & AstroFile::Zodiac) qDebug() << "    - Zodiac changed";
        if (ml & AstroFile::HouseSystem) qDebug() << "    - HouseSystem changed";
        if (ml & AstroFile::Harmonic) qDebug() << "    - Harmonic changed";
    }
    qDebug() << "========================================";

    if (!isVisible()) return;
    if (_inhibitUpdate) return;
    if (!filesCount()) return;

    while (m.size() < filesCount()) m.append(AstroFile::Member());

    // View settings that affect event calculation and require recalc
    bool any = false;
    bool needsRecalc = false;
    for (int fi = 0; fi < filesCount(); ++fi) {
        auto ml = m[fi];
        any |= (ml & (AstroFile::Zodiac | AstroFile::AspectSet
                      | AstroFile::AspectMode | AstroFile::HouseSystem));
        needsRecalc |= (ml & (AstroFile::Zodiac | AstroFile::AspectSet
                              | AstroFile::AspectMode | AstroFile::HouseSystem));
    }

    qDebug() << "[TRANSITS viewSettingsUpdated] any=" << any << "needsRecalc=" << needsRecalc;
    if (any) {
        auto* evm = ensureEventsModel();
        if (!evm) return;

        // When file(0) just changed (tab switch), the "All" diff flags from
        // a new file(1) include ViewSettings bits that look like Zodiac/
        // AspectSet/etc. changed.  They didn't — ViewSettings are global.
        // Skip the false-positive recalc in that case.
        if (filesCount() > 0 && needsRecalc && !_fileJustSwitched) {
            qDebug() << "[VIEW SETTINGS] Marking for recalc due to settings change";
            file(0)->markEventsForRecalc();
            // Note: Don't call updateTransits() here — describePlanet() below
            // will call it, avoiding a double-call that would block the main
            // thread waiting for the first finder to finish.
        } else if (_fileJustSwitched) {
            qDebug() << "[VIEW SETTINGS] Skipping recalc mark — tab switch, not a real settings change";
            // filesUpdated() already called describePlanet() which started
            // the finder thread.  Don't call it again — that would cancel
            // the running thread and schedule a redundant restart.
            _fileJustSwitched = false;
            return;
        }
        _fileJustSwitched = false;  // Clear after use

        if (!_chs) _chs = new AChangeSignalFrame(evm);
        evm->setAspectSet(file()->getAspectSetId());
        describePlanet();
    }
}

void
Transits::showEvent(QShowEvent* e)
{
    // Call base class implementation first (handles resumeUpdate)
    AstroFileHandler::showEvent(e);
    
    qDebug() << "[SHOW EVENT] Tab becoming visible, restoring toolbar";
    qDebug() << "  _tabEventOptions has" << _tabEventOptions.size() << "event types";
    qDebug() << "  T=T:" << (_tabEventOptions.count(A::etcTransitToTransit) > 0);
    qDebug() << "  T=N:" << (_tabEventOptions.count(A::etcTransitToNatal) > 0);
    qDebug() << "  Stations:" << (_tabEventOptions.count(A::etcStation) > 0);
    
    // Restore toolbar to match this tab's event options when becoming visible
    updateToolbarFromEventOptions();
}

void
Transits::hideEvent(QHideEvent* e)
{
    // Save current event options to file(0) before tab becomes invisible
    // This ensures state is persisted when app closes or tab is switched
    if (filesCount() > 0 && file(0)) {
        qDebug() << "[HIDE EVENT] Tab becoming invisible, saving event options to file" << file(0)->getName();
        qDebug() << "  _tabEventOptions has" << _tabEventOptions.size() << "event types";
        file(0)->setTransitEventOptions(_tabEventOptions);
    }
    
    // Call base class implementation
    AstroFileHandler::hideEvent(e);
}

AppSettings
Transits::defaultSettings()
{
    AppSettings s = A::EventOptions().toMap();
    s.setValue("Events/backgroundFinders", false);
    return s;
}

AppSettings
Transits::currentSettings()
{
    // Get global settings and replace event types with tab-specific ones
    A::EventOptions opts = A::EventOptions::current();
    opts.enabledEvents = _tabEventOptions;
    AppSettings s = opts.toMap();
    s.setValue("Events/backgroundFinders", _backgroundFinders);
    return s;
}

void
Transits::applySettings(const AppSettings& s)
{
    qDebug() << "[APPLY SETTINGS] Applying global event calculation settings";
    
    _backgroundFinders = s.value("Events/backgroundFinders", false).toBool();
    
    // Extract settings from dialog (no event type settings anymore)
    A::EventOptions opts(s.values());
    
    // Get reference to global settings singleton
    A::EventOptions& curr(A::EventOptions::current());

    // Check if any settings changed that would require recalculation
    bool changed =
        (s.value("Events/patternsQuorum").toUInt() != curr.patternsQuorum
         || s.value("Events/patternsSpreadOrb").toDouble()
                != curr.patternsSpreadOrb
         || s.value("Events/planetPairOrb").toDouble() != curr.planetPairOrb
         || s.value("Events/patternsRestrictMoon").toBool()
                != curr.patternsRestrictMoon
         || s.value("Events/includeMidpoints").toBool() != curr.includeMidpoints
         || s.value("Events/includeShadowTransits").toBool()
                != curr.includeShadowTransits
         || s.value("Events/includeOnlyOuterTransitsToNatal").toBool()
                != curr.includeOnlyOuterTransitsToNatal
         || s.value("Events/limitLunarTransits").toBool()
                != curr.limitLunarTransits
         || A::EventOptions::skipper(s.value("Events/skipByDuration").toUInt())
                != curr.skipByDuration
         || s.value("Events/includeAsteroids").toBool() != curr.includeAsteroids
         || s.value("Events/includeCentaurs").toBool() != curr.includeCentaurs
         || s.value("Events/includeOnlyInnerProgressionsToNatal").toBool()
                != curr.includeOnlyInnerProgressionsToNatal);
    bool changedExpanded =
        (s.value("Events/secondaryOrb").toDouble() != curr.expandShowOrb
         || s.value("Events/expandShowAspectPatterns").toBool()
                != curr.expandShowAspectPatterns
         || s.value("Events/expandShowHousePlacementsOfTransits").toBool()
                != curr.expandShowHousePlacementsOfTransits
         || s.value("Events/expandShowRulershipTips").toBool()
                != curr.expandShowRulershipTips
         || s.value("Events/expandShowStationAspectsToTransits").toBool()
                != curr.expandShowStationAspectsToTransits
         || s.value("Events/expandShowStationAspectsToNatal").toBool()
                != curr.expandShowStationAspectsToNatal
         || s.value("Events/expandShowReturnAspects").toBool()
                != curr.expandShowReturnAspects
         || s.value("Events/expandShowTransitAspectsToReturnPlanet").toBool()
                != curr.expandShowTransitAspectsToReturnPlanet);

    auto tsp = s.value("Events/defaultTimespan").toString();
    if (filesCount() == 0) {
        _duration->setText(tsp);
        _ddelta = ADateDelta::fromString(tsp);
    }

    // Update global settings singleton with new values from dialog
    qDebug() << "  Updating global settings";
    curr = A::EventOptions(s.values());

    // If calculation settings changed, recalculate events for THIS tab
    if (changed) {
        if (filesCount() > 0) {
            file(0)->markEventsForRecalc();
            updateTransits();
        }
    } else if (changedExpanded) {
        // updateExpanded(); ?
    }
}

void
Transits::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Events"));

    // Note: Event type visibility (which events to show) is controlled per-chart via toolbar
    // This dialog only contains global settings that apply to event calculation
    ed->addLabel(tr("<b>Event visibility</b> (which event types to display)<br/>"
                    "is controlled by the toolbar in each chart tab and saved per-chart."));

    ed->addLineEdit("Events/defaultTimespan", tr("Default timespan"));
    ed->addCheckBox("Events/includeShadowTransits",
                    tr("Include retro shadow IN/EX"));
    ed->addCheckBox("Events/limitLunarTransits", tr("Limit Lunar Transits"));

    QKeyValueList vals {
        { tr("Show all"), A::EventOptions::SkipNone },
        { tr("Skip <1day"), A::EventOptions::SkipLessThanDay },
        { tr("Skip <1wk"), A::EventOptions::SkipLessThanWeek },
        { tr("Skip <1mo"), A::EventOptions::SkipLessThanMonth }
    };
    ed->addComboBox("Events/skipByDuration", "Skip by duration", vals);

    ed->addCheckBox("Events/includeAsteroids", tr("Include asteroids"));
    ed->addCheckBox("Events/includeCentaurs", tr("Include centaurs"));
    //ed->addCheckBox("Events/includeMidpoints", tr("Include Midpoints"));
    ed->addSpinBox("Events/patternsQuorum", tr("Patterns Quorum"), 2, 6);
    ed->addDoubleSpinBox("Events/patternsSpreadOrb",
                         tr("Patterns Spread Orb"),
                         .1,
                         16.);
    ed->addDoubleSpinBox("Events/planetPairOrb",
                         tr("Planet pair orb"),
                         0.1,
                         16.);
    ed->addCheckBox("Events/patternsRestrictMoon",
                    tr("Patterns Restrict Moon"));
    ed->addCheckBox("Events/backgroundFinders",
                    tr("Continue event search in background on tab switch"));
#if 0
    ed->addCheckBox("Events/showLunations", tr("Show Lunations"));
    ed->addCheckBox("Events/showHeliacalEvents", tr("Show Heliacal Events"));
    ed->addCheckBox("Events/showPrimaryDirections",
                    tr("[Session] Show Primary Directions"));
    ed->addCheckBox("Events/showLifeEvents", tr("Default Show Life Events"));
    ed->addDoubleSpinBox("Events/secondaryOrb", tr("Secondary Orb"), .25, 16.);


    ed->addTab("Events II");

    ed->addCheckBox("Events/expandShowAspectPatterns",
                    tr("Expand to Show Aspect Patterns"));
    ed->addCheckBox("Events/expandShowHousePlacementsOfTransits",
                    tr("Expand to Show House Placements Of Transits"));
    ed->addCheckBox("Events/expandShowRulershipTips",
                    tr("Expand to Show Rulership Tips"));
    ed->addCheckBox("Events/expandShowStationAspectsToTransits",
                    tr("Expand to Show Station Aspects To Transits"));
    ed->addCheckBox("Events/expandShowStationAspectsToNatal",
                    tr("Expand to Show Station Aspects To Natal"));
    ed->addCheckBox("Events/expandShowReturnAspects",
                    tr("Expand to Show Return Aspects"));
    ed->addCheckBox("Events/expandShowTransitAspectsToReturnPlanet",
                    tr("Expand to Show Transit Aspects To Return Planet"));
#endif
}

void
Transits::updateToolbarFromEventOptions()
{
    if (!_actTransitToTransit) return;  // Toolbar not initialized yet
    
    // Block signals during bulk updates for QActions
    ASignalBlocker block({_actStations, _actReturns, _actTransitToTransit, 
                          _actProgressedToProgressed, _actTransitAspectPatterns, 
                          _actTransitNatalAspectPatterns, _actSignIngress, 
                          _actHouseIngress, _actParanatellonta, _actParanatellontaToNatal});
    
    _actStations->setChecked(_tabEventOptions.count(A::etcStation) > 0);
    _actReturns->setChecked(_tabEventOptions.count(A::etcReturn) > 0 ||
                            _tabEventOptions.count(A::etcSolarReturn) > 0 ||
                            _tabEventOptions.count(A::etcLunarReturn) > 0);
    _actTransitToTransit->setChecked(_tabEventOptions.count(A::etcTransitToTransit) > 0);
    _actProgressedToProgressed->setChecked(_tabEventOptions.count(A::etcProgressedToProgressed) > 0);
    _actTransitAspectPatterns->setChecked(_tabEventOptions.count(A::etcTransitAspectPattern) > 0);
    _actTransitNatalAspectPatterns->setChecked(_tabEventOptions.count(A::etcTransitNatalAspectPattern) > 0);
    _actSignIngress->setChecked(_tabEventOptions.count(A::etcSignIngress) > 0);
    _actHouseIngress->setChecked(_tabEventOptions.count(A::etcHouseIngress) > 0);
    _actParanatellonta->setChecked(_tabEventOptions.count(A::etcParanatellonta) > 0);
    _actParanatellontaToNatal->setChecked(_tabEventOptions.count(A::etcParanatellontaToNatal) > 0);
    
    // Update dropdown button states
    bool hasTransitToNatal = _tabEventOptions.count(A::etcTransitToNatal) > 0 ||
                             _tabEventOptions.count(A::etcOuterTransitToNatal) > 0 ||
                             _tabEventOptions.count(A::etcTransitToNatalAngles) > 0;
    if (_btnTransitToNatal) {
        _btnTransitToNatal->blockSignals(true);
        _btnTransitToNatal->setChecked(hasTransitToNatal);
        _btnTransitToNatal->blockSignals(false);
    }
    
    // Update angles checkbox state
    if (_actIncludeAngles) {
        _actIncludeAngles->blockSignals(true);
        _actIncludeAngles->setChecked(_tabEventOptions.count(A::etcTransitToNatalAngles) > 0);
        _actIncludeAngles->blockSignals(false);
    }
    
    bool hasProgressedToNatal = _tabEventOptions.count(A::etcProgressedToNatal) > 0 ||
                                _tabEventOptions.count(A::etcInnerProgressedToNatal) > 0;
    if (_btnProgressedToNatal) {
        _btnProgressedToNatal->blockSignals(true);
        _btnProgressedToNatal->setChecked(hasProgressedToNatal);
        _btnProgressedToNatal->blockSignals(false);
    }
    
    // Update alternating button states based on which event types are present
    // Only update the mode flag if that mode's event type is actually in the set
    // Don't change the flag just because both are absent (button is off)
    if (_tabEventOptions.count(A::etcOuterTransitToNatal) > 0) {
        _transitToNatalShowsOuter = true;
    } else if (_tabEventOptions.count(A::etcTransitToNatal) > 0) {
        _transitToNatalShowsOuter = false;
    }
    // Otherwise leave _transitToNatalShowsOuter unchanged (preserves user's last selection)
    
    if (_tabEventOptions.count(A::etcInnerProgressedToNatal) > 0) {
        _progressedToNatalShowsInner = true;
    } else if (_tabEventOptions.count(A::etcProgressedToNatal) > 0) {
        _progressedToNatalShowsInner = false;
    }
    // Otherwise leave _progressedToNatalShowsInner unchanged
    
    // Update radio button states in menus
    if (_actTransitToNatal && _actOuterTransitToNatal) {
        _actTransitToNatal->blockSignals(true);
        _actOuterTransitToNatal->blockSignals(true);
        _actTransitToNatal->setChecked(!_transitToNatalShowsOuter);
        _actOuterTransitToNatal->setChecked(_transitToNatalShowsOuter);
        _actTransitToNatal->blockSignals(false);
        _actOuterTransitToNatal->blockSignals(false);
    }
    
    // Update button text to match current mode
    if (_btnTransitToNatal) {
        _btnTransitToNatal->setText(_transitToNatalShowsOuter ? "OT=N" : "T=N");
    }
    
    if (_actInnerProgressedToNatal && _actAllProgressedToNatal) {
        _actInnerProgressedToNatal->blockSignals(true);
        _actAllProgressedToNatal->blockSignals(true);
        _actInnerProgressedToNatal->setChecked(_progressedToNatalShowsInner);
        _actAllProgressedToNatal->setChecked(!_progressedToNatalShowsInner);
        _actInnerProgressedToNatal->blockSignals(false);
        _actAllProgressedToNatal->blockSignals(false);
    }
    
    // Update button text to match current mode
    if (_btnProgressedToNatal) {
        _btnProgressedToNatal->setText(_progressedToNatalShowsInner ? "IP=N" : "P=N");
    }
    
    updateTransitToNatalButtonState();
    updateProgressedToNatalButtonState();
}

void
Transits::updateTransitToNatalButtonState()
{
    if (!_btnTransitToNatal) return;
    
    if (_transitToNatalShowsOuter) {
        _btnTransitToNatal->setText("OT=N");
    } else {
        _btnTransitToNatal->setText("T=N");
    }
}

void
Transits::updateProgressedToNatalButtonState()
{
    if (!_btnProgressedToNatal) return;
    
    if (_progressedToNatalShowsInner) {
        _btnProgressedToNatal->setText("IP=N");
    } else {
        _btnProgressedToNatal->setText("P=N");
    }
}

void
Transits::updateInputProgress(double prog)
{
    // Throttle stylesheet updates — skip if progress hasn't moved enough
    // to produce a visible 1% change, unless we're clearing (prog < 0 or >= 1).
    if (prog >= 0 && prog < 1.0) {
        int pctNow  = int(prog * 100);
        int pctLast = int(_lastProgress * 100);
        if (pctNow == pctLast) return;
    }
    _lastProgress = prog;

    if (prog < 0) {
        // Waiting-for-pool phase: show pulsing indicator (full bar, muted)
        QString bg = QStringLiteral(
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
            "  stop:0 rgba(100,149,237,60), stop:1 rgba(100,149,237,30)); ");
        _input->setStyleSheet(
            QStringLiteral("QComboBox { %1%2 }")
                .arg(bg, _inputBorderStyle));
    } else if (prog < 1.0) {
        int pct = qBound(0, int(prog * 100), 100);
        // Two-tone gradient: filled portion | unfilled
        double stopL = pct / 100.0;
        double stopR = stopL + 0.001;
        QString bg = QStringLiteral(
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
            "  stop:0 rgba(100,149,237,80), "
            "  stop:%1 rgba(100,149,237,80), "
            "  stop:%2 transparent, "
            "  stop:1 transparent); ")
            .arg(stopL, 0, 'f', 4)
            .arg(stopR, 0, 'f', 4);
        _input->setStyleSheet(
            QStringLiteral("QComboBox { %1%2 }")
                .arg(bg, _inputBorderStyle));
    } else {
        // Computation finished — restore validation-only style
        _input->setStyleSheet(
            _inputBorderStyle.isEmpty()
                ? QString()
                : QStringLiteral("QComboBox { %1 }").arg(_inputBorderStyle));
    }
}
