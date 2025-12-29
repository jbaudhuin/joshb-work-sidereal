
#include "transits.h"
#include "../../astroprocessor/src/astro-data.h"
#include "../../zodiac/src/mainwindow.h"
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
#include <QDateEdit>
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
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringListModel>
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

    QDateTime rowDate(int row) const { return _evs[row]->dateTime(); }

    QDateTime rowDate(QModelIndex inx) const
    {
        auto par = inx.parent();
        if (par.isValid()) return _evs[par.row()]->dateTime();
        return _evs[inx.row()]->dateTime();
    }

    A::HarmonicEvent rowData(int row) const { return *_evs[row]; }
    A::HarmonicEvent rowData(QModelIndex inx) const
    {
        auto par = inx.parent();
        if (par.isValid()) return *_evs[par.row()];
        return *_evs[inx.row()];
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

    void setTimezone(short tz) {
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
                if (mixedMode(asp.planets())) return QColor("gold");
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

            case transitBodyCol: {
                A::PlanetClusterLess prless(true /*fast*/);
                if (true || (a->locations().empty() && b->locations().empty()))
                {
                    if (prless(a->planets(), b->planets())) return true;
                    if (prless(b->planets(), a->planets())) return false;
                } else {
                    if (prless(a->locations(), b->locations())) return true;
                    if (prless(b->locations(), a->locations())) return false;
                }
                if (a->dateTime() < b->dateTime()) return true; // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true; // orb
                if (a->orb() > b->orb()) return false;
                return (a->harmonic() < b->harmonic()); // harmonic
            }

            case natalTransitBodyCol: // XXX
            {
                A::PlanetClusterLess prless(false /*not fast*/);
                if (true || (a->locations().empty() && b->locations().empty()))
                {
                    if (prless(a->planets(), b->planets())) return true;
                    if (prless(b->planets(), a->planets())) return false;
                } else {
                    if (prless(a->locations(), b->locations())) return true;
                    if (prless(b->locations(), a->locations())) return false;
                }
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
                if (ev.dateTime().isValid()) _evs.emplace_back(ev);
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

            _evls.erase(lievit++);
            AChangeSignalFrame chs(this);

            beginResetModel();
            rebuild();
            sort();
            endResetModel();

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
    short          _tzOffset = 0; // Timezone offset in hours

    int _changeRef = 0;

    AChangeSignalFrame* _chs = nullptr;

    // Per-instance display modes - always start with glyphs (mode 0)
    // Users can Ctrl+click column headers to cycle through modes
    DisplayMode _transitBodyColMode = A::EventOptions::DisplayGlyphs;
    DisplayMode _natalTransitBodyColMode = A::EventOptions::DisplayGlyphs;
    
    AstroFile* _natalFile = nullptr; // Pointer to natal chart for rulership calculations

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
    QRegularExpression re("(\\d+) ?((y(ea)?r?|m(o(n(th)?)?)?|d(a?y)?)s?)");
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
ADateDelta::addTo(const QDate& d)
{
    return d.addYears(numYears).addMonths(numMonths).addDays(numDays);
}

QDate
ADateDelta::subtractFrom(const QDate& d)
{
    return d.addYears(-numYears).addMonths(-numMonths).addDays(-numDays);
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

    _start  = new QDateEdit;
    _duraRB = new QRadioButton(tr("for"));
    _duraRB->setFocusPolicy(Qt::NoFocus);
    _duration = new QLineEdit;
    _duration->setText(_ddelta.toString());
    _endRB = new QRadioButton(tr("til"));
    _endRB->setFocusPolicy(Qt::NoFocus);
    _end = new QDateEdit;

    _back = new QPushButton("«");
    _back->setMaximumWidth(20);
    _forth = new QPushButton("»");
    _forth->setMaximumWidth(20);

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

    _input = new QLineEdit;

    auto l2 = new QVBoxLayout;
    l2->addItem(l1);
    l2->addWidget(_input, 0);
    l2->setContentsMargins(QMargins(4, 4, 4, 4));

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
    
    // Helper to save event options and trigger recalc
    auto saveEventOptionsAndRecalc = [this]() {
        // Save to file(0) immediately so it persists when switching files
        if (filesCount() > 0 && file(0)) {
            file(0)->setTransitEventOptions(_tabEventOptions);
        }
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
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
        
        saveEventOptionsAndRecalc();
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
        saveEventOptionsAndRecalc();
    });
    
    toolbar->addSeparator();
    
    // Event filter buttons - checkable toggles
    _actTransitToTransit = toolbar->addAction("T=T");
    _actTransitToTransit->setCheckable(true);
    _actTransitToTransit->setToolTip("Transit to Transit aspects");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actTransitToTransit))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actTransitToTransit, &QAction::triggered, this, [this](bool checked) {
        qDebug() << "T=T button toggled:" << checked;
        if (checked) _tabEventOptions.insert(A::etcTransitToTransit);
        else _tabEventOptions.erase(A::etcTransitToTransit);
        qDebug() << "  _actAutoRecalc:" << _actAutoRecalc << "isChecked:" << (_actAutoRecalc ? _actAutoRecalc->isChecked() : false);
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) {
            qDebug() << "  Calling updateTransits()";
            updateTransits();
        }
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
        if (action->text().contains("OT=N")) {
            _transitToNatalShowsOuter = true;
            _btnTransitToNatal->setText("OT=N");
        } else {
            _transitToNatalShowsOuter = false;
            _btnTransitToNatal->setText("T=N");
        }
        // Don't change enabled state, just update which mode would be enabled if button is on
        if (_btnTransitToNatal->isChecked()) {
            // Button is already on, so switch modes
            if (_transitToNatalShowsOuter) {
                _tabEventOptions.insert(A::etcOuterTransitToNatal);
                _tabEventOptions.erase(A::etcTransitToNatal);
            } else {
                _tabEventOptions.insert(A::etcTransitToNatal);
                _tabEventOptions.erase(A::etcOuterTransitToNatal);
            }
            saveEventOptionsAndRecalc();
        }
    });
    
    // Angles checkbox toggles independently
    connect(_actIncludeAngles, &QAction::triggered, this, [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) {
            _tabEventOptions.insert(A::etcTransitToNatalAngles);
        } else {
            _tabEventOptions.erase(A::etcTransitToNatalAngles);
        }
        saveEventOptionsAndRecalc();
    });
    
    // Button toggle changes the on/off state for the current mode
    connect(_btnTransitToNatal, &QToolButton::toggled, this, [this, saveEventOptionsAndRecalc](bool checked) {
        qDebug() << "T=N button toggled:" << checked << "outer mode:" << _transitToNatalShowsOuter;
        if (checked) {
            if (_transitToNatalShowsOuter) {
                _tabEventOptions.insert(A::etcOuterTransitToNatal);
                _tabEventOptions.erase(A::etcTransitToNatal);
            } else {
                _tabEventOptions.insert(A::etcTransitToNatal);
                _tabEventOptions.erase(A::etcOuterTransitToNatal);
            }
        } else {
            _tabEventOptions.erase(A::etcTransitToNatal);
            _tabEventOptions.erase(A::etcOuterTransitToNatal);
        }
        saveEventOptionsAndRecalc();
    });
    
    toolbar->addWidget(_btnTransitToNatal);
    
    _actProgressedToProgressed = toolbar->addAction("P=P");
    _actProgressedToProgressed->setCheckable(true);
    _actProgressedToProgressed->setToolTip("Progressed to Progressed aspects");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actProgressedToProgressed))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actProgressedToProgressed, &QAction::triggered, this, [this](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcProgressedToProgressed);
        else _tabEventOptions.erase(A::etcProgressedToProgressed);
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
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
        if (action->text().contains("IP=N")) {
            _progressedToNatalShowsInner = true;
            _btnProgressedToNatal->setText("IP=N");
        } else {
            _progressedToNatalShowsInner = false;
            _btnProgressedToNatal->setText("P=N");
        }
        // Don't change enabled state, just update which mode would be enabled if button is on
        if (_btnProgressedToNatal->isChecked()) {
            // Button is already on, so switch modes
            if (_progressedToNatalShowsInner) {
                _tabEventOptions.insert(A::etcInnerProgressedToNatal);
                _tabEventOptions.erase(A::etcProgressedToNatal);
            } else {
                _tabEventOptions.insert(A::etcProgressedToNatal);
                _tabEventOptions.erase(A::etcInnerProgressedToNatal);
            }
            saveEventOptionsAndRecalc();
        }
    });
    
    // Button toggle changes the on/off state for the current mode
    connect(_btnProgressedToNatal, &QToolButton::toggled, this, [this, saveEventOptionsAndRecalc](bool checked) {
        qDebug() << "IP=N/P=N button toggled:" << checked << "inner mode:" << _progressedToNatalShowsInner;
        if (checked) {
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
        saveEventOptionsAndRecalc();
    });
    
    toolbar->addWidget(_btnProgressedToNatal);
    
    _actTransitAspectPatterns = toolbar->addAction("TA");
    _actTransitAspectPatterns->setCheckable(true);
    _actTransitAspectPatterns->setToolTip("Transit Aspect Patterns");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actTransitAspectPatterns))) {
        btn->setStyleSheet("QToolButton { min-width: 28px !important; }");
    }
    connect(_actTransitAspectPatterns, &QAction::triggered, this, [this](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcTransitAspectPattern);
        else _tabEventOptions.erase(A::etcTransitAspectPattern);
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
    });
    
    _actTransitNatalAspectPatterns = toolbar->addAction("TNA");
    _actTransitNatalAspectPatterns->setCheckable(true);
    _actTransitNatalAspectPatterns->setToolTip("Transit-Natal Aspect Patterns");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actTransitNatalAspectPatterns))) {
        btn->setStyleSheet("QToolButton { min-width: 36px !important; }");
    }
    connect(_actTransitNatalAspectPatterns, &QAction::triggered, this, [this](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcTransitNatalAspectPattern);
        else _tabEventOptions.erase(A::etcTransitNatalAspectPattern);
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
    });
    
    // Sign Ingress button
    _actSignIngress = toolbar->addAction("T=I");
    _actSignIngress->setCheckable(true);
    _actSignIngress->setToolTip("Sign Ingresses");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actSignIngress))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actSignIngress, &QAction::triggered, this, [this](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcSignIngress);
        else _tabEventOptions.erase(A::etcSignIngress);
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
    });
    
    // House Ingress button
    _actHouseIngress = toolbar->addAction("T=H");
    _actHouseIngress->setCheckable(true);
    _actHouseIngress->setToolTip("House Ingresses");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actHouseIngress))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actHouseIngress, &QAction::triggered, this, [this](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcHouseIngress);
        else _tabEventOptions.erase(A::etcHouseIngress);
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
    });
    
    // Paranatellonta button
    _actParanatellonta = toolbar->addAction("Par");
    _actParanatellonta->setCheckable(true);
    _actParanatellonta->setToolTip("Paranatellonta");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actParanatellonta))) {
        btn->setStyleSheet("QToolButton { min-width: 32px !important; }");
    }
    connect(_actParanatellonta, &QAction::triggered, this, [this](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcParanatellonta);
        else _tabEventOptions.erase(A::etcParanatellonta);
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
    });
    
    // Paranatellonta to Natal button
    _actParanatellontaToNatal = toolbar->addAction("Par=N");
    _actParanatellontaToNatal->setCheckable(true);
    _actParanatellontaToNatal->setToolTip("Paranatellonta to Natal");
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actParanatellontaToNatal))) {
        btn->setStyleSheet("QToolButton { min-width: 48px !important; }");
    }
    connect(_actParanatellontaToNatal, &QAction::triggered, this, [this](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcParanatellontaToNatal);
        else _tabEventOptions.erase(A::etcParanatellontaToNatal);
        // Mark events for recalc since event filter changed
        for (int i = 0, n = filesCount(); i < n; ++i) {
            file(i)->markEventsForRecalc();
        }
        if (_actAutoRecalc && _actAutoRecalc->isChecked()) updateTransits();
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

    QFile cssfile("Details/style.css");
    cssfile.open(QIODevice::ReadOnly | QIODevice::Text);
    setStyleSheet(cssfile.readAll());
    cssfile.close();

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

    _start->setCalendarPopup(true);
    _end->setCalendarPopup(true);

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
    if (_progressSortTimer && _progressSortTimer->isActive()) {
        _progressSortTimer->stop();
    }
    if (_active && !_active->isFinished()) {
        qDebug() << "========================================";
        qDebug() << "[STOP THREADS] Cancelling active finder thread" << _active << _active->objectName();
        qDebug() << "[STOP THREADS] AspectFinder:" << _activeFinder.data();
        qDebug() << "========================================";
        if (_activeFinder) {
            _activeFinder->cancel();
        }
        // Wait for the thread to actually finish completely
        // The finder thread's waitForDone loop will ensure thread pool tasks finish
        qDebug() << "[STOP THREADS] Waiting for finder thread to finish...";
        _active->wait();
        qDebug() << "========================================";
        qDebug() << "[STOP THREADS] Finder thread finished and cleaned up";
        qDebug() << "========================================";
    }
}

bool
Transits::transitsOnly() const
{
    // Transits-only mode: single file that is NOT a person's natal chart
    // If we have 2+ files, we're in synastry/comparison mode, not transits-only
    if (filesCount() != 1) return false;
    
    auto ftype = file()->getType();
    return (ftype != TypeMale && ftype != TypeFemale && ftype != TypeEvent);
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
        
        transitsAF()->suspendUpdate();
        transitsAF()->setLocation(_location->location());
        transitsAF()->setLocationName(_location->locationName());
        transitsAF()->setTimezone(short(tz));
        transitsAF()->resumeUpdate();

        qDebug() << "Timezone for location is"
                 << response["rawOffset"].toInt() / 60 /*minsPerSec*/
                 << "with dstOffset" << response["dstOffset"].toInt() / 60
                 << "in" << response["timeZoneName"].toString();

        // Signal that the chart needs updating with new location/timezone
        // This will cause the chart to redraw with the new location
        // Stop any active finder threads before updating the chart
        stopThreads();
        if (transitsOnly()) {
            emit updateFirst(file());
        } else {
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
    if (!isVisible()) return;
    if (transitsAF()->isSuspendedUpdate()) return;

    // Restore location from the appropriate file FIRST (before cache check)
    // This ensures the location widget updates even when using cached events
    AstroFile* locFile = nullptr;
    if (filesCount() >= 2) {
        // If we have 2+ files, use file(1) for location (the transit/return chart)
        locFile = file(1);
    } else if (filesCount() == 1 && transitsOnly()) {
        // Single file that is transits-only, use it
        locFile = file(0);
    } else if (filesCount() == 1) {
        // Single natal/event chart, use transitsAF() which may have custom location
        locFile = transitsAF();
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
    }

    // Now check cache before doing any heavy calculation work
    ensureEventsModel();
    if (!_evm) return;
    
    auto& evs = file(0)->events();
    bool hasEvents = !evs.empty();
    bool needsRecalc = file(0)->needsEventsRecalc();
    
    if (hasEvents && !needsRecalc) {
        // Events already cached, nothing more to do
        qDebug() << "Using cached events for file" << file(0)->getName();
        return;
    }
    
    qDebug() << "Recalculating events for file" << file(0)->getName();

    if (!_active) {
        saveScrollPos();
    } else {
        qDebug() << "========================================";
        qDebug() << "[CLEANUP OLD] Found existing finder thread" << _active << _active->objectName();
        qDebug() << "[CLEANUP OLD] AspectFinder:" << _activeFinder.data();
        qDebug() << "[CLEANUP OLD] Disconnecting and canceling...";
        disconnect(_active, SIGNAL(finished()), this, SLOT(onCompleted()));
        if (_activeFinder) {
            _activeFinder->cancel();
        }
        if (_active) {
            qDebug() << "[CLEANUP OLD] Waiting for thread" << _active;
            _active->wait();
            qDebug() << "[CLEANUP OLD] Thread finished and will be deleted";
        }
        _active = nullptr;
        qDebug() << "========================================";
    }

    if (!_chs) {
        auto* evm = ensureEventsModel();
        if (evm) _chs = new AChangeSignalFrame(evm);
    }

    qDebug() << "filesCount()" << filesCount();

    auto       hs = A::dynAspState();
    ADateRange r { _start->date(), _end->date() };

    _evm->removeEvents(evs);
    evs.clear();

#if 0
    transitsAF()->suspendUpdate();
    transitsAF()->setLocation(_location->location());
    transitsAF()->setLocationName(_location->locationName());
    transitsAF()->resumeUpdate();
#endif

    A::AspectFinder* af = nullptr;
#if 1
    if (filesCount() >= 1) {
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
    connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
    // Delete AspectFinder after thread finishes
    // We can't use moveToThread() here because the worker thread has exited
    // and moveToThread() must be called from the object's current thread.
    // Since the finder is no longer doing work, we can delete it directly.
    connect(thread, &QThread::finished, this, [af]() {
        qDebug() << "[DELETE FINDER] Deleting AspectFinder:" << af;
        delete af;
    });
    
    thread->start();
    _active = thread;
    _activeFinder = af;
    
    qDebug() << "[CREATE FINDER] Started finder thread" << thread;
    qDebug() << "========================================";
}

void
Transits::onProgress(double prog)
{
    // Debounce sort operations during progress updates to prevent
    // runaway sorting when many progress signals are queued
    if (!_progressSortTimer) {
        _progressSortTimer = new QTimer(this);  // Parent ensures cleanup
        _progressSortTimer->setSingleShot(true);
        _progressSortTimer->setInterval(100); // 100ms debounce
        connect(_progressSortTimer, &QTimer::timeout, [this]() {
            _evm->sort();
            if (_chs) restoreScrollPos();
        });
    }
    _progressSortTimer->start();  // Restarts timer if already running
}

void
Transits::onCompleted()
{
#if 1
    qDebug() << "[ON COMPLETED] Starting cleanup, thread:" << _active.data() << "finder:" << _activeFinder.data();
    if (_progressSortTimer && _progressSortTimer->isActive()) {
        _progressSortTimer->stop();
    }
    
    // Disconnect all signals from the finder before deletion to prevent
    // any queued progress() signals from being processed after deletion
    if (_activeFinder) {
        qDebug() << "[ON COMPLETED] Disconnecting ALL finder signals/slots";
        _activeFinder->disconnect();  // Disconnect all signals FROM the finder
        disconnect(_activeFinder.data());  // Disconnect all signals TO the finder
    }
    
    // Delete _chs BEFORE sorting to emit changeDone() signal first
    qDebug() << "[ON COMPLETED] Deleting change signal frame";
    delete _chs;
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
    
    if (hasSelection && !isScrollEvent) {
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
    
    // Save scroll position when user clicks a cell (creates selection anchor)
    saveScrollPos();
    
    auto btns = QGuiApplication::mouseButtons();
    bool mbtn = (btns & Qt::MiddleButton);
    bool lbtn = (btns & Qt::LeftButton);
    bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier);
    if (lbtn && ctrl) lbtn = false, mbtn = true;

    A::modalize<A::AspectSetId> aset(
        MainWindow::theAstroWidget()->overrideAspectSet(),
        -1);
    A::PlanetSet focal;
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
            aset = A::topAspectSet().id + v.toUInt();
        }
    }

    auto par = inx.parent();
    if (par.isValid()) inx = par;
    if (mbtn) {
        doubleClickedCell(inx);
        return;
    }
    auto    dt = _evm->rowDate(inx.row());
    auto    ev = _evm->rowData(inx.row());
    auto    et = ev.eventType();
    QString desc;
    if (focal.empty()) desc = _evm->rowDesc(inx.row());
    else {
        desc =
            inx.siblingAtColumn(EventsTableModel::harmonicCol).data().toString()
            + " " + focal.describe();
    }
    A::modalize<bool> noup(_inhibitUpdate);
    if (transitsOnly()) {
        file()->setFocalPlanets(focal);
        file()->setName(desc);
        file()->setGMT(dt);
        // Set file type to Return for return events
        if (et == A::etcSolarReturn || et == A::etcLunarReturn) {
            file()->setType(TypeReturn);
        }
        // Stop any active finder threads before updating the chart
        stopThreads();
        emit updateFirst(file());
    } else {
        // Grr make transit planets be in fileId 1
        A::PlanetSet shift;
        for (auto cpid : focal) {
            if (cpid.fileId() == 0) {
                cpid.setFileId(1);
                shift.emplace(cpid);
            }
        }
        if (shift.size() == focal.size()) focal.swap(shift);

        transitsAF()->suspendUpdate();
        transitsAF()->setFocalPlanets(focal);
        transitsAF()->setName(desc);
        transitsAF()->setGMT(dt);
        // Set file type to Return for return events
        if (et == A::etcSolarReturn || et == A::etcLunarReturn) {
            transitsAF()->setType(TypeReturn);
            transitsAF()->clearBaseChart();
        } else if (et == A::etcProgressedToProgressed 
            || et == A::etcProgressedToNatal
            || et == A::etcInnerProgressedToNatal
            || et == A::etcTransitToProgressed) {
            transitsAF()->setType(TypeDerivedProg);
            // Set the natal chart as the base for progressions
            transitsAF()->setBaseChart(file()->getGMT());
        } else {
            // For transit events, reset to TypeOther and clear base chart
            transitsAF()->setType(TypeOther);
            transitsAF()->clearBaseChart();
        }
        transitsAF()->resumeUpdate();
        
        // Clear unsaved state since this is a generated chart from an event
        transitsAF()->clearUnsavedState();
        
        // Stop any active finder threads before updating the chart
        stopThreads();
        emit updateSecond(transitsAF());
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
    auto              dt   = _evm->rowDate(inx.row());
    auto              ev   = _evm->rowData(inx.row());
    auto              et   = ev.eventType();
    auto              desc = _evm->rowDesc(inx.row());
    A::modalize<bool> noup(_inhibitUpdate);
    AstroFile*        af = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(af);
    af->suspendUpdate();
    af->setLocation(_location->location());
    af->setLocationName(_location->locationName());
    if (!transitsOnly() && !shift) {
        af->setName(file()->getName() + " - " + desc);
    } else {
        af->setName(desc);
    }
    af->setGMT(dt);
    
    // Set file type to Return for return events
    if (et == A::etcSolarReturn || et == A::etcLunarReturn) {
        af->setType(TypeReturn);
        af->clearBaseChart();
    } else if (et == A::etcProgressedToProgressed 
        || et == A::etcProgressedToNatal
        || et == A::etcInnerProgressedToNatal
        || et == A::etcTransitToProgressed) {
        af->setType(TypeDerivedProg);
        // Set the natal chart as the base for progressions
        af->setBaseChart(file()->getGMT());
    }
    af->resumeUpdate();
    
    // Clear unsaved state since this is a generated chart from an event
    af->clearUnsavedState();
    
    // bool shift = (QApplication::keyboardModifiers() & Qt::ShiftModifier);
    if (transitsOnly() || !shift) {
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
        short tz = natalFile->getTimezone();
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
        short tz = transitFile->getTimezone();
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
    if (!isVisible()) return;
    if (_inhibitUpdate) return;
    if (!filesCount()) {
        clear();
        return;
    }
    
    // Save current event options to previous file(0) if it exists
    static AstroFile* previousFile = nullptr;
    if (previousFile && previousFile != file(0)) {
        qDebug() << "[FILES UPDATED] Saving event options from previous file" << previousFile->getName();
        previousFile->setTransitEventOptions(_tabEventOptions);
    }
    
    // Load event options from new file(0)
    if (file(0)) {
        qDebug() << "[FILES UPDATED] Loading event options for file" << file(0)->getName();
        _tabEventOptions = file(0)->getTransitEventOptions();
        
        // If file has no saved options (empty set), initialize from global defaults
        if (_tabEventOptions.empty()) {
            qDebug() << "  No saved options, using global defaults";
            _tabEventOptions = A::EventOptions::current().enabledEvents;
            file(0)->setTransitEventOptions(_tabEventOptions);
        }
        
        // Update toolbar to reflect the loaded event options
        updateToolbarFromEventOptions();
        
        previousFile = file(0);
    }

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
    int  f   = 0;
    for (auto ml : m) {
        FileType type = file(f)->getType();
        if (type < TypeSearch) {
            if (f == 0 ? (type <= TypeReturn)
                       : (type == TypeMale || type == TypeFemale
                          || type == TypeEvent))
            {
                // For natal/event charts (file 0), check GMT and Location
                // changes
                any |= (ml & (AstroFile::GMT | AstroFile::Location));
            }

            any |= (ml
                    & (AstroFile::Timezone | AstroFile::Zodiac
                       | AstroFile::AspectSet | AstroFile::AspectMode));
        }
        f++;
    }
    if (any) {
#if OLDMODEL
        auto zap = _tm;
        _tview->setModel(nullptr);
        _tm = nullptr;
        zap->deleteLater();
#else
        auto* evm = ensureEventsModel();
        if (!evm) return;
        
        // Only mark for recalc if we don't already have valid cached events
        // This prevents unnecessary recalc when just switching tabs
        if (filesCount() > 0 && file(0)->events().empty()) {
            file(0)->markEventsForRecalc();
        }
        
        if (!_chs) _chs = new AChangeSignalFrame(evm);
        evm->setAspectSet(file()->getAspectSetId());
        // Don't clear events here - they're stored in file(0) now
        // evm->clearAllEvents();
#endif
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

AppSettings
Transits::defaultSettings()
{
    return A::EventOptions().toMap();
}

AppSettings
Transits::currentSettings()
{
    // Get global settings and replace event types with tab-specific ones
    A::EventOptions opts = A::EventOptions::current();
    opts.enabledEvents = _tabEventOptions;
    return opts.toMap();
}

void
Transits::applySettings(const AppSettings& s)
{
    // Extract event types from restored settings
    A::EventOptions opts(s.values());
    _tabEventOptions = opts.enabledEvents;
    
    // Update toolbar to reflect restored settings
    updateToolbarFromEventOptions();
    
    // Also update global defaults for comparison
    A::EventOptions& curr(A::EventOptions::current());

    bool changed =
        (s.value("Events/patternsQuorum").toUInt() != curr.patternsQuorum
         || s.value("Events/patternsSpreadOrb").toDouble()
                != curr.patternsSpreadOrb
         || s.value("Events/planetPairOrb").toDouble() != curr.planetPairOrb
         || s.value("Events/patternsRestrictMoon").toBool()
                != curr.patternsRestrictMoon
         || s.value("Events/includeMidpoints").toBool() != curr.includeMidpoints
         || s.value("Events/showStations").toBool() != curr.showStations()
         || s.value("Events/includeShadowTransits").toBool()
                != curr.includeShadowTransits
         || s.value("Events/showTransitsToTransits").toBool()
                != curr.showTransitsToTransits()
         || s.value("Events/includeOnlyOuterTransitsToNatal").toBool()
                != curr.includeOnlyOuterTransitsToNatal
         || s.value("Events/limitLunarTransits").toBool()
                != curr.limitLunarTransits
         || A::EventOptions::skipper(s.value("Events/skipByDuration").toUInt())
                != curr.skipByDuration
         || s.value("Events/showTransitsToNatalPlanets").toBool()
                != curr.showTransitsToNatalPlanets()
         || s.value("Events/showTransitsToNatalAngles").toBool()
                != curr.showTransitsToNatalAngles()
         || s.value("Events/includeAsteroids").toBool() != curr.includeAsteroids
         || s.value("Events/includeCentaurs").toBool() != curr.includeCentaurs
         || s.value("Events/showTransitsToHouseCusps").toBool()
                != curr.showTransitsToHouseCusps()
         || s.value("Events/showReturns").toBool() != curr.showReturns()
         || s.value("Events/showProgressionsToProgressions").toBool()
                != curr.showProgressionsToProgressions()
         || s.value("Events/showProgressionsToNatal").toBool()
                != curr.showProgressionsToNatal()
         || s.value("Events/includeOnlyInnerProgressionsToNatal").toBool()
                != curr.includeOnlyInnerProgressionsToNatal
         || s.value("Events/showTransitAspectPatterns").toBool()
                != curr.showTransitAspectPatterns()
         || s.value("Events/showTransitNatalAspectPatterns").toBool()
                != curr.showTransitNatalAspectPatterns()
         || s.value("Events/showIngresses").toBool() != curr.showIngresses()
         || s.value("Events/showLunations").toBool() != curr.showLunations()
         || s.value("Events/showHeliacalEvents").toBool()
                != curr.showHeliacalEvents()
         || s.value("Events/showPrimaryDirections").toBool()
                != curr.showPrimaryDirections()
         || s.value("Events/showLifeEvents").toBool() != curr.showLifeEvents()
         || s.value("Events/showHarmonicDividend").toBool() != curr.showHarmonicDividend);
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

    curr = A::EventOptions(s.values());

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
    ed->addTab(tr("Events I"));

    ed->addLineEdit("Events/defaultTimespan", tr("Default timespan"));
    ed->addCheckBox("Events/showStations", tr("[Session] Show Stations"));
    ed->addCheckBox("Events/includeShadowTransits",
                    tr("Include retro shadow IN/EX"));
    ed->addCheckBox("Events/showReturns", tr("[Session] Show Returns"));
    ed->addCheckBox("Events/showTransitsToTransits",
                    tr("[Session] Show Transits to Transits"));
    ed->addCheckBox("Events/showTransitsToNatalPlanets",
                    tr("[Session] Show Transits to Natal"));
    ed->addCheckBox("Events/showTransitsToNatalAngles",
                    tr("[Session] Show Transits to natal angles"));
    ed->addCheckBox("Events/includeOnlyOuterTransitsToNatal",
                    tr("[Session] Include only outer planet transits to natal"));
    ed->addCheckBox("Events/limitLunarTransits", tr("Default Limit Lunar Transits"));

    QVariantMap vals { { tr("Show all"), A::EventOptions::SkipNone },
                       { tr("Skip <1day"), A::EventOptions::SkipLessThanDay },
                       { tr("Skip <1wk"), A::EventOptions::SkipLessThanWeek },
                       { tr("Skip <1mo"),
                         A::EventOptions::SkipLessThanMonth } };
    ed->addComboBox("Events/skipByDuration", "Skip by duration", vals);

    ed->addCheckBox("Events/includeAsteroids", tr("Include asteroids"));
    ed->addCheckBox("Events/includeCentaurs", tr("Include centaurs"));
    ed->addCheckBox("Events/showTransitsToHouseCusps",
                    tr("Show Transits to all house cusps"));
    ed->addCheckBox("Events/includeMidpoints", tr("Include Midpoints"));
    ed->addCheckBox("Events/showTransitAspectPatterns",
                    tr("[Session] Show Transit Aspect Patterns"));
    ed->addCheckBox("Events/showTransitNatalAspectPatterns",
                    tr("[Session] Show Transit Natal Aspect Patterns"));
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
                    tr("Default Patterns Restrict Moon"));
    ed->addCheckBox("Events/showIngresses", tr("[Session] Show Ingresses"));
    ed->addCheckBox("Events/showProgressionsToProgressions",
                    tr("[Session] Show Progressions to Progressions"));
    ed->addCheckBox("Events/showProgressionsToNatal",
                    tr("[Session] Show Progressions to Natal"));
    ed->addCheckBox("Events/includeOnlyInnerProgressionsToNatal",
                    tr("[Session] Include only inner planet progressions to natal"));
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
    _transitToNatalShowsOuter = _tabEventOptions.count(A::etcOuterTransitToNatal) > 0;
    _progressedToNatalShowsInner = _tabEventOptions.count(A::etcInnerProgressedToNatal) > 0;
    
    // Update radio button states in menus
    if (_actTransitToNatal && _actOuterTransitToNatal) {
        _actTransitToNatal->blockSignals(true);
        _actOuterTransitToNatal->blockSignals(true);
        _actTransitToNatal->setChecked(!_transitToNatalShowsOuter);
        _actOuterTransitToNatal->setChecked(_transitToNatalShowsOuter);
        _actTransitToNatal->blockSignals(false);
        _actOuterTransitToNatal->blockSignals(false);
    }
    
    if (_actInnerProgressedToNatal && _actAllProgressedToNatal) {
        _actInnerProgressedToNatal->blockSignals(true);
        _actAllProgressedToNatal->blockSignals(true);
        _actInnerProgressedToNatal->setChecked(_progressedToNatalShowsInner);
        _actAllProgressedToNatal->setChecked(!_progressedToNatalShowsInner);
        _actInnerProgressedToNatal->blockSignals(false);
        _actAllProgressedToNatal->blockSignals(false);
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
