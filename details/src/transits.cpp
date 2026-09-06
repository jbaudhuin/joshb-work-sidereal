
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
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyleOptionToolButton>
#include <QStylePainter>
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
// QToolButton whose text is drawn left-aligned instead of centered.  Plain
// QToolButton always centers its label via the style (it ignores the
// `text-align` stylesheet property, unlike QPushButton), which leaves a front
// gap on split buttons that are sized for their widest label.  We keep the
// native frame + dropdown arrow but paint the label left-aligned, so any slack
// falls between the label and the arrow rather than in front of the text.
class LeftToolButton : public QToolButton
{
  public:
    using QToolButton::QToolButton;

    /// Force an explicit label color (overrides the palette role).  Used by the
    /// Refresh button, whose amber/green background needs a fixed black/white
    /// label.  Pass an invalid QColor to revert to palette-based coloring.
    void setTextColor(const QColor& c) { _textColor = c; update(); }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter            p(this);
        QStyleOptionToolButton   opt;
        initStyleOption(&opt);

        // Draw everything (frame, background, checked highlight, menu arrow)
        // except the auto-centered label.
        QStyleOptionToolButton bg = opt;
        bg.text.clear();
        bg.icon = QIcon();
        p.drawComplexControl(QStyle::CC_ToolButton, bg);

        if (opt.text.isEmpty()) return;

        // SC_ToolButton is the main (non-arrow) sub-area in MenuButtonPopup
        // mode; draw the label left-aligned within it.
        QRect r = style()->subControlRect(QStyle::CC_ToolButton, &opt,
                                          QStyle::SC_ToolButton, this);
        r.adjust(4, 0, -2, 0);   // small left inset, keep clear of the arrow

        if (_textColor.isValid()) {
            p.setPen(_textColor);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, opt.text);
            return;
        }
        // Use the highlighted text color when checked so the label stays legible
        // against the checked-state highlight background.
        QPalette::ColorRole role = (opt.state & QStyle::State_On)
                                 ? QPalette::HighlightedText
                                 : QPalette::ButtonText;
        p.drawItemText(r, Qt::AlignLeft | Qt::AlignVCenter, palette(),
                       isEnabled(), opt.text, role);
    }

  private:
    QColor _textColor;
};

// Short label for the skip-by-duration toolbar button.
inline
QString
skipLabel(A::EventOptions::skipper s)
{
    switch (s) {
    case A::EventOptions::SkipLessThanDay:   return QStringLiteral("1d");
    case A::EventOptions::SkipLessThanWeek:  return QStringLiteral("1w");
    case A::EventOptions::SkipLessThanMonth: return QStringLiteral("1m");
    default:                                 return QStringLiteral("1w");
    }
}

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
        return rowSortDate(_evs[row].second, _evs[row].occ);
    }

    QDateTime rowDate(QModelIndex inx) const
    {
        auto par = inx.parent();
        int r = par.isValid() ? par.row() : inx.row();
        if (r < 0 || r >= int(_evs.size())) return QDateTime();
        return rowSortDate(_evs[r].second, _evs[r].occ);
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
            // h reads as a connector for named aspects (e.g. "tri" ->
            // "Sat-r tri Sun"); Dynamic mode's "H4" reads fine in the same
            // slot ("Sat-r H4 Sun").
            return t + " " + h + " " + n;
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
        if (role == Qt::ToolTipRole) {
            switch (col) {
            case eventTypeCol:
                return tr("Event Type");
            case dateCol:
                return tr("Date / time of the event");
            case harmonicCol:
                return tr("Aspect harmonic, or — for parans / heliacal events "
                          "— the angle or phase");
            case transitBodyCol:
                return tr("Transiting, Progressed or Star body");
            case natalTransitBodyCol:
                return tr("Transiting, Progressed or Natal body");
            }
            return QVariant();
        }
        if (role != Qt::DisplayRole) return QVariant();
        switch (col) {
        case eventTypeCol:        return tr("ET");
        case dateCol:             return tr("Date");
        case harmonicCol:         return tr("Asp");
        case transitBodyCol:      return tr("T/P/S");
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

    // Translate a paran angle-glyph desc (Almagest codepoint) to the
    // human-readable abbreviation used in text contexts (tooltip, summary).
    // Returns an empty string when desc is not a paran angle glyph.
    static QString paranAngleToText(const QString& desc)
    {
        if (desc.length() != 1) return {};
        switch (desc[0].unicode()) {
        case 402:  return QStringLiteral("As");  // Asc  (ƒ)
        case 8249: return QStringLiteral("Ds");  // Desc (‹)
        case 77:   return QStringLiteral("Mc");  // MC   (M)
        case 8225: return QStringLiteral("Ic");  // IC   (‡)
        default:   return {};
        }
    }

    // Translate a Primary Direction ray glyph (Almagest aspect codepoint, set
    // by findPrimaryDirections in the significator's PlanetLoc::desc) to the
    // human-readable aspect name used in text contexts (tooltip, summary).
    // Only the first character is examined, since the significator's desc
    // may carry a trailing "D"/"S" dexter/sinister marker (see
    // pdDexSinToText). Returns an empty string when desc is not a PD ray
    // glyph (e.g. the promissor's "Dir"/"Con").
    static QString pdRayGlyphToText(const QString& desc)
    {
        if (desc.isEmpty()) return {};
        switch (desc[0].unicode()) {
        case 0x00C9: return QStringLiteral("Conjunction");
        case 0x00CB: return QStringLiteral("Sextile");
        case 0x00CD: return QStringLiteral("Square");
        case 0x00CF: return QStringLiteral("Trine");
        case 0x00D1: return QStringLiteral("Opposition");
        default:     return {};
        }
    }

    // Translate the significator's trailing dexter/sinister marker ("D"/"S",
    // set by findPrimaryDirections after the ray glyph — never present on
    // the conjunction ray) to its full classical word. Returns an empty
    // string when desc carries no such marker.
    static QString pdDexSinToText(const QString& desc)
    {
        if (desc.length() < 2) return {};
        QChar c = desc[desc.length() - 1];
        if (c == QLatin1Char('D')) return QStringLiteral("Dexter");
        if (c == QLatin1Char('S')) return QStringLiteral("Sinister");
        return {};
    }

    // Map a heliacal phase tag (set by findHeliacalEvents in PlanetLoc::desc) to
    // readable text: MF/EL/EF/ML → morning-first / evening-last / evening-first
    // / morning-last. Returns {} when desc is not a heliacal tag.
    static QString heliacalPhaseToText(const QString& desc)
    {
        // Apparition anchors (planets/stars):
        if (desc == "Cul") return QStringLiteral("Culmination");
        if (desc == "Acr") return QStringLiteral("Acronychal rising");
        if (desc == "Cs")  return QStringLiteral("Cosmic setting");
        if (desc == "GWe") return QStringLiteral("Greatest western elongation (morning)");
        if (desc == "GEe") return QStringLiteral("Greatest eastern elongation (evening)");
        // Discrete phase brackets (the Moon; and the apparition occurrences):
        if (desc == "MF") return QStringLiteral("Morning First");
        if (desc == "EL") return QStringLiteral("Evening Last");
        if (desc == "EF") return QStringLiteral("Evening First");
        if (desc == "ML") return QStringLiteral("Morning Last");
        return {};
    }

    // Map an apparition phase tag to its per-chart selection bit (0 if none).
    // Inner planets (elongation model) group EF/MF, GEe/GWe, EL/ML into the
    // *F / G*e / *L bits; the culmination model maps each tag individually.
    static unsigned heliacalPhaseBit(const QString& tag, bool inner)
    {
        if (inner) {
            if (tag == QLatin1String("EF") || tag == QLatin1String("MF"))
                return AstroFile::hpFirst;
            if (tag == QLatin1String("GEe") || tag == QLatin1String("GWe"))
                return AstroFile::hpElong;
            if (tag == QLatin1String("EL") || tag == QLatin1String("ML"))
                return AstroFile::hpLast;
            return 0;
        }
        if (tag == QLatin1String("MF"))  return AstroFile::hpMF;
        if (tag == QLatin1String("Acr")) return AstroFile::hpAcr;
        if (tag == QLatin1String("Cul")) return AstroFile::hpCul;
        if (tag == QLatin1String("Cs"))  return AstroFile::hpCs;
        if (tag == QLatin1String("EL"))  return AstroFile::hpEL;
        return 0;
    }

    // Map a paran angle-glyph desc (Almagest codepoint) to an angle index:
    // 0=Asc, 1=Desc, 2=MC, 3=IC; -1 when desc is not an angle glyph.
    static int paranAngleIndex(const QString& desc)
    {
        if (desc.length() != 1) return -1;
        switch (desc[0].unicode()) {
        case 402:  return 0;  // Asc
        case 8249: return 1;  // Desc
        case 77:   return 2;  // MC
        case 8225: return 3;  // IC
        default:   return -1;
        }
    }

    // Build the Asp-column string for a paran from its body locations.
    // Emits one letter per occupied angle (A/D/M/I, in that order),
    // lowercase when a single body sits on the angle and uppercase when two
    // or more do.  For Par=N the transit-side and natal-side letters are
    // split with a colon (transit:natal); natal bodies are those on fileId 0.
    template <typename Locs>
    static QString paranAngleString(const Locs& locs, A::EventType et)
    {
        // counts[side][angleIdx]: side 0 = transit, side 1 = natal.
        int counts[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
        for (const auto& loc : locs) {
            int ai = paranAngleIndex(loc.desc);
            if (ai < 0) continue;
            int side = (loc.planet.fileId() == 0) ? 1 : 0;
            ++counts[side][ai];
        }
        static const char letters[4] = {'A', 'D', 'M', 'I'};
        auto sideStr = [&](int s0, int s1) {
            QString s;
            for (int i = 0; i < 4; ++i) {
                int n = counts[s0][i] + (s1 >= 0 ? counts[s1][i] : 0);
                if (n == 0) continue;
                QChar c = QLatin1Char(letters[i]);
                s += (n >= 2) ? c : c.toLower();
            }
            return s;
        };
        if (et == A::etcParanatellontaToNatal) {
            // U+22C5 DOT OPERATOR, spaced; reads as transit . natal
            return sideStr(0, -1) + " " + QChar(0x22C5) + " " + sideStr(1, -1);
        }
        // Plain paran: all bodies are transit; combine both fileId buckets.
        return sideStr(0, 1);
    }

    // Verbose, tooltip-friendly version of paranAngleString(): spells out the
    // angle names (asc/desc/mc/ic), capitalised when two or more bodies sit on
    // the angle.  Within a chart side multiple angles are joined with '+';
    // Par=N splits the transit and natal sides with a spaced dot operator.
    template <typename Locs>
    static QString paranAngleVerbose(const Locs& locs, A::EventType et)
    {
        int counts[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
        for (const auto& loc : locs) {
            int ai = paranAngleIndex(loc.desc);
            if (ai < 0) continue;
            int side = (loc.planet.fileId() == 0) ? 1 : 0;
            ++counts[side][ai];
        }
        static const char* names[4] = {"asc", "desc", "mc", "ic"};
        auto sideStr = [&](int s0, int s1) {
            QStringList parts;
            for (int i = 0; i < 4; ++i) {
                int n = counts[s0][i] + (s1 >= 0 ? counts[s1][i] : 0);
                if (n == 0) continue;
                QString w = QString::fromLatin1(names[i]);
                if (n >= 2) w[0] = w[0].toUpper();
                parts << w;
            }
            return parts.join("+");
        };
        if (et == A::etcParanatellontaToNatal) {
            return sideStr(0, -1) + " " + QChar(0x22C5) + " " + sideStr(1, -1);
        }
        return sideStr(0, 1);
    }

    QString display(const A::ChartPlanetModeId& cpid) const
    {
        if (QString suff = modeToSuffix(cpid.mode()); suff.isEmpty()) {
            return cpid.name();
        } else {
            return cpid.name() + "-" + suff;
        }
    }

    QString display(const A::PlanetLoc& s,
                    const QString& descOverride = {},
                    unsigned       eventType    = 0) const
    {
        // A decomposed apparition row overrides the shared anchor tag (s.desc)
        // with its own phase occurrence (e.g. "Cs" rather than the anchor "Cul").
        const QString eff = descOverride.isEmpty() ? s.desc : descOverride;
        // Fixed-star heliacal body: show "Name (Con) — Phase" with the star's
        // constellation, and no zodiac position (a star's rasiLoc isn't computed
        // for heliacal events, so it would read a bogus 0°Aries).
        // TODO: map the 3-letter IAU abbrev (e.g. "CMa") to a full constellation
        // name ("Canis Major") for a friendlier tooltip.
        if (s.planet.planetId() >= A::Stars_Start) {
            QString r   = s.planet.name();
            QString con = A::getStar(s.planet.name()).constellation;
            if (!con.isEmpty()) r += " (" + con + ")";
            QString ph = heliacalPhaseToText(eff);
            if (!ph.isEmpty()) r += " — " + ph;
            return r;
        }
        QString suff;
        if (auto suf = modeToSuffix(s.mode()); !suf.isEmpty()) {
            suff = "-" + suf;
        }
        // Primary directions: rasiLoc() holds right ascension, not ecliptic
        // longitude (see glyph()'s matching branch) — show it as RA rather
        // than feeding it through zodiacPosition(), which would print a
        // meaningless sign/degree, and only when the user has opted in.
        if (eventType == A::etcPrimaryDirections) {
            QString r = s.planet.name() + suff;
            if (A::EventOptions::current().showPDRightAscension)
                r += " " + A::raToString(s.rasiLoc(), A::HighPrecision);
            if (!eff.isEmpty()) {
                // Significator: eff is the ray's Almagest glyph, optionally
                // followed by a "D"/"S" dexter/sinister marker -> readable
                // ray name (+ ", Dexter"/", Sinister"). Promissor: eff is
                // already "Dir"/"Con" -> shown as-is.
                QString rayText = pdRayGlyphToText(eff);
                if (rayText.isEmpty()) {
                    r += " (" + eff + ")";
                } else {
                    QString dexSin = pdDexSinToText(eff);
                    r += " (" + rayText + (dexSin.isEmpty() ? "" : ", " + dexSin) + ")";
                }
            }
            return r;
        }
        // For paran entries whose desc is an Almagest angle codepoint (or a
        // heliacal phase tag), substitute readable text so the tooltip renders
        // correctly in a normal (non-glyph) font.
        QString angleText = paranAngleToText(eff);
        if (angleText.isEmpty()) angleText = heliacalPhaseToText(eff);
        const QString bodyDesc  = angleText.isEmpty()
                                      ? s.description()
                                      : s.planet.name() + "-" + angleText;
        return QString(s.planet.fileId() == 1 ? "<i>%1</i>" : "%1")
                   .arg(bodyDesc + suff)
               + " "
               + A::zodiacPosition(s.rasiLoc(),
                                   _zodiac,
                                   A::HighPrecision,
                                   s.speed < 0);
    }

    QString glyph(const A::ChartPlanetId& cpid) const { return cpid.glyph(); }

    QString glyph(const A::PlanetLoc& s, unsigned eventType = 0) const
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
        // Parans render as <body-glyph> <angle-glyph> only — no longitude,
        // no retrograde marker. desc already carries the Almagest angle
        // codepoint set by findParans.
        const bool isParan = (eventType == A::etcParanatellonta
                              || eventType == A::etcParanatellontaToNatal);
        const bool isPattern =
            (eventType == A::etcTransitAspectPattern
             || eventType == A::etcTransitNatalAspectPattern);
        // Heliacal: the phase tag (MF/EL/GWe/EF/ML/…) renders in the Asp column
        // via heliacalPhaseToText, so it is NOT appended to the body here.
        if (isParan) return g + " " + desc;
        // Fixed-star heliacal body: show the star NAME (the astro font has no
        // per-star glyph, and a star heliacal event has no meaningful zodiac
        // position). The T/P/S column renders it in the default font via the
        // FontRole branch below.
        if (eventType == A::etcHeliacalStars) return cpid.name();
        // Planet / Moon heliacal body: glyph + zodiac position (+ retrograde
        // marker). The phase tag (MF/EL/…) lives in the Asp column, so it is not
        // appended here.
        if (eventType == A::etcHeliacalEvents
            || eventType == A::etcHeliacalLunar) {
            QString out = g + " " + getPos(s.rasiLoc());
            if (s.speed < 0) out += " #"; // Almagest retrograde glyph
            return out;
        }
        // Primary directions: nothing moves in ecliptic longitude during a
        // direction (only the RAMC rotates through the fixed sphere), and RA
        // is what the arc is actually computed from — so show right ascension
        // (findPrimaryDirections() stores it in rasiLoc() for these events)
        // rather than a zodiac position, and only when the user has opted in
        // (off by default — the two values are each body's own fixed natal
        // RA and don't "match" at exact contact, which reads as confusing
        // without the semi-arc explanation).
        if (eventType == A::etcPrimaryDirections) {
            QString out = g;
            if (A::EventOptions::current().showPDRightAscension)
                out += " " + A::raToString(s.rasiLoc(), A::NormalPrecision);
            // Significator's desc is a single-char Almagest aspect-glyph
            // codepoint (the ray), optionally followed by a literal "D"/"S"
            // dexter/sinister marker — append literally; this cell already
            // renders in Almagest font, where uppercase letters render as
            // literal text rather than glyphs. The conjunction carries no
            // glyph at all (empty desc — it isn't a projected ray, just the
            // significator's own place), so there's nothing to append.
            // Promissor's desc ("Dir"/"Con", length 3) is suppressed here;
            // it surfaces via the Asp column instead (see harmonicCol
            // handling in data()).
            if (!s.desc.isEmpty() && s.desc.length() <= 2) out += " " + s.desc;
            return out;
        }
        if (isPattern) return g;
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

    QString summary(const A::PlanetLoc& s, unsigned eventType = 0) const
    {
        auto str = s.planet.name();

        // Add mode suffix if available (-r for natal, -p for progressed, etc.)
        QString suffix = modeToSuffix(s.mode());
        if (!suffix.isEmpty()) {
            str += "-" + suffix;
        }

        if (eventType == A::etcPrimaryDirections) {
            // Significator: desc is the ray's Almagest glyph, optionally
            // followed by a "D"/"S" dexter/sinister marker -> readable name.
            // Promissor: desc is "Dir"/"Con" but that's shown via the Asp
            // column (harmonicCol) instead, so suppress it here to avoid
            // "Asc-Dir Dir" duplicating what rowDesc()'s connector already says.
            QString rayText = pdRayGlyphToText(s.desc);
            if (!rayText.isEmpty()) {
                str += "-" + rayText;
                QString dexSin = pdDexSinToText(s.desc);
                if (!dexSin.isEmpty()) str += "-" + dexSin;
            }
            return str;
        }

        // Add descriptor; translate paran angle glyphs / heliacal phase tags to
        // readable text.
        if (!s.desc.isEmpty()) {
            QString angleText = paranAngleToText(s.desc);
            if (angleText.isEmpty()) angleText = heliacalPhaseToText(s.desc);
            str += "-" + (angleText.isEmpty() ? s.desc : angleText);
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
    QVariant glyphic(int role, Iter its, unsigned eventType = 0) const
    {
        if (role == Qt::FontRole) {
            // Heliacal-star rows show the star NAME (text), not a glyph, so use
            // the default font rather than the Almagest glyph font.
            if (eventType == A::etcHeliacalStars) return QFont();
            static QFont f("Almagest", 11);
            return f;
        }

        QStringList sl;
        for (auto it = its.first; it != its.second; ++it) {
            const auto& s = *it;
            if (role == Qt::DisplayRole || role == Qt::EditRole) {
                sl << glyph(s, eventType);
            } else if (role == Qt::ToolTipRole) {
                sl << display(s, {}, eventType);
            } else if (role == SummaryRole) {
                sl << summary(s, eventType);
            }
        }

        QString joint = ",";
        if (eventType == A::etcParanatellonta || eventType == A::etcParanatellontaToNatal) {
            joint += " ";
        }
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
        const A::PlanetLoc& ploc, unsigned eventType) const
    {
        return getHouseRulershipWithNatalHouseString(
            ploc, eventType); // Use the position-aware version
    }

    QString getCorrectHouseRulershipWithNatalHouseString(
        const A::ChartPlanetId& cpid, unsigned /*eventType*/) const
    {
        return getHouseRulershipWithNatalHouseString(
            cpid); // Use the legacy version for aspect patterns
    }

    template <typename Iter>
    QVariant glyphicWithMode(int            role,
                             Iter           its,
                             DisplayMode    mode,
                             unsigned       eventType            = 0,
                             bool           isNatalTransitColumn = false,
                             const QString& descOverride         = {}) const
    {
        if (mode == A::EventOptions::DisplayGlyphs) {
            return glyphic(role, its, eventType);
        }

        if (role == Qt::FontRole) {
            // Heliacal-star rows render the star name as text.
            if (eventType == A::etcHeliacalStars) return QFont();
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
                auto&&  cpid = extractChartPlanetId(s);
                QString rulershipText;
                if (mode == A::EventOptions::DisplayRulership) {
                    rulershipText = getHouseRulershipString(cpid);
                } else if (mode
                           == A::EventOptions::DisplayRulershipWithNatalHouse)
                {
                    rulershipText =
                        getCorrectHouseRulershipWithNatalHouseString(s, eventType);
                }

                if (!rulershipText.isEmpty()) {
                    // For parans the ruler text (e.g. "R1") replaces the body
                    // glyph, which would otherwise carry the Almagest angle
                    // codepoint. Ruler text renders in the default font, so
                    // append the readable angle designator (As/Ds/Mc/Ic) rather
                    // than lose it.
                    const bool isParan =
                        (eventType == A::etcParanatellonta
                         || eventType == A::etcParanatellontaToNatal);
                    if constexpr (std::is_same_v<
                                      std::decay_t<decltype(s)>,
                                      A::PlanetLoc>) {
                        if (isParan) {
                            const QString angleText = paranAngleToText(s.desc);
                            if (!angleText.isEmpty())
                                rulershipText += " " + angleText;
                        }
                    }
                    sl << rulershipText;
                } else {
                    // Fall back to glyph if no rulership
                    sl << glyph(s, eventType);
                }
            } else if (role == Qt::ToolTipRole) {
                if constexpr (std::is_same_v<std::decay_t<decltype(s)>,
                                             A::PlanetLoc>) {
                    sl << display(s, descOverride, eventType);
                } else {
                    sl << display(s);
                }
            } else if (role == SummaryRole) {
                sl << summary(s, eventType);
            }
        }

        QString joint = ", ";
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
        // Pattern events (e.g. TNA) carry their bodies in asp.planets(),
        // not asp.locations() -- locs is empty there. singleColumn() only
        // rejects the exactly-1 case, so an empty locs would otherwise fall
        // through to begin()/rbegin() on an empty range (UB/crash).
        if (locs.empty() || singleColumn(locs)) return false;
        pp.first  = &(*locs.begin());
        pp.second = &(*locs.rbegin());
        return true;
    }

    // Resolve the named (non-Harmonic) aspect matching this row, for
    // display (icon/name/abbreviation) in harmonicCol. Two-body hits go
    // through calculateAspect() for an exact angle match; pattern events
    // (TA/TNA — no locations() pair, just a shared harmonic across all
    // bodies) fall back to a representative aspect for that harmonic via
    // aspectForHarmonic(), since there's no specific angle to match.
    // Returns nullptr for Harmonic/Dynamic mode or when nothing matches.
    const A::AspectType* resolveNamedAspectType(const A::HarmonicAspect& asp) const
    {
        if (aspects().name.startsWith("Harmonic")) return nullptr;
        locPair pp;
        if (getPlanetPair(asp.locations(), pp)) {
            auto a = A::calculateAspect(aspects(), pp.first, pp.second);
            if (a.d && a.d->id != A::Aspect_None) return a.d;
        }
        return A::aspectForHarmonic(aspects(), asp.harmonic());
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
            && role != Qt::DecorationRole
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
                // HarmonicEvent (or one decomposed apparition-phase row of it)
                // Convert UTC to chart's timezone
                const int occi = _evs[row].occ;
                const auto& occv = _evs[row]->occurrences();
                const QDateTime srcDt =
                    (occi >= 0 && occi < occv.size()) ? occv[occi].first
                                                      : _evs[row]->dateTime();
                auto dt = srcDt.toTimeZone(QTimeZone(_tzOffset * 3600));
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
            // Primary Directions: the ray (conjunction/sextile/square/trine/
            // opposition) renders as an Almagest glyph directly in the
            // significator's own T/P/N cell (see EventsTableModel::glyph()
            // — the significator carries the ray, the promissor is always
            // bare, per findPrimaryDirections), so this column shows
            // direct/converse instead — the one piece of information about
            // a PD row that isn't visible anywhere else, since
            // converse/direct is a property of the whole arc, not either
            // body individually.
            if (et == A::etcPrimaryDirections) {
                if (role == Qt::DecorationRole) return QVariant();
                if (role == Qt::FontRole) return QFont(); // default, not Almagest
                QString dirCon;
                locPair pp;
                if (getPlanetPair(asp.locations(), pp) && pp.first) {
                    if (auto* ploc = dynamic_cast<const A::PlanetLoc*>(pp.first))
                        dirCon = ploc->desc; // "Dir" or "Con", on the promissor (T/P/S)
                }
                if (role == Qt::ToolTipRole)
                    return dirCon == "Con" ? tr("Converse") : tr("Direct");
                if (role == RawRole) return asp.harmonic();
                if (role == Qt::DisplayRole || role == SummaryRole) return dirCon;
                return QVariant();
            }
            // Named aspect sets (Basic, Reasonable, ...) show the aspect's
            // own icon from aspects.csv instead of the "H4" text — Dynamic
            // mode has no named icon, so it keeps the H# cell below.
            if (role == Qt::DecorationRole) {
                if (singleColumn(asp.locations())) return QVariant();
                if (et == A::etcParanatellonta
                    || et == A::etcParanatellontaToNatal
                    || et == A::etcHeliacalEvents || et == A::etcHeliacalStars
                    || et == A::etcHeliacalLunar)
                    return QVariant();
                if (auto* d = resolveNamedAspectType(asp)) {
                    QString icon = d->userData["icon"].toString();
                    if (!icon.isEmpty()) return QIcon(icon);
                }
                return QVariant();
            }
            if (role == Qt::ToolTipRole) {
                if (et == A::etcParanatellonta
                    || et == A::etcParanatellontaToNatal)
                    return A::degreeToString(asp.orb(), A::HighPrecision)
                        + "\n" + paranAngleVerbose(asp.locations(), et);
                if (et == A::etcHeliacalEvents || et == A::etcHeliacalStars
                    || et == A::etcHeliacalLunar) {
                    QString anchor;
                    // A decomposed row is annotated with its own phase.
                    if (prow == -1) {
                        const int occi = _evs[row].occ;
                        const auto& lbls = _evs[row]->occurrenceLabels();
                        if (occi >= 0 && occi < lbls.size()) {
                            const QString t = heliacalPhaseToText(lbls[occi]);
                            anchor = t.isEmpty() ? lbls[occi] : t;
                        }
                    }
                    for (const auto& loc : asp.locations())
                        if (anchor.isEmpty() && !loc.desc.isEmpty()) {
                            const QString t = heliacalPhaseToText(loc.desc);
                            anchor = t.isEmpty() ? loc.desc : t;
                            break;
                        }
                    // Apparition rows carry the bracket moments (first
                    // appearance / anchor / last appearance) as occurrences;
                    // annotate the visible window with those bracket dates.
                    // Heliacal events are always top-level rows (prow == -1),
                    // so the HarmonicEvent is _evs[row].
                    if (prow == -1 && row >= 0 && row < int(_evs.size())) {
                        const auto& occ = _evs[row]->occurrences();
                        if (occ.size() >= 2) {
                            const QTimeZone tz(int(_tzOffset * 3600));
                            const QString from = occ.first().first.toTimeZone(tz)
                                                     .toString("d MMM yyyy");
                            const QString to = occ.last().first.toTimeZone(tz)
                                                   .toString("d MMM yyyy");
                            return anchor
                                 + QString("\nvisible %1 – %2").arg(from, to);
                        }
                    }
                    return anchor;
                }
                if (singleColumn(asp.locations())) return "station";
                if (asp.orb() != qreal() /*asp.locations().empty()*/) {
                    // Named aspect sets (Basic, Reasonable, ...) get their
                    // own aspect name + orb; the harmonic-number notation
                    // (H4, H12, ...) is reserved for the Dynamic set, whose
                    // "aspects" are generated on the fly rather than named.
                    if (auto* d = resolveNamedAspectType(asp)) {
                        return d->name + " "
                             + A::degreeToString(asp.orb(), A::HighPrecision);
                    }
                    return QString("H%1 %2")
                        .arg(asp.harmonic())
                        .arg(A::degreeToString(asp.orb(), A::HighPrecision));
                } else {
                    if (auto* d = resolveNamedAspectType(asp)) return d->name;
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
            // Parans: show the angle-position abbreviation (e.g. "am",
            // "aM", "m:a") rather than the meaningless H1.
            if (et == A::etcParanatellonta
                || et == A::etcParanatellontaToNatal)
                return paranAngleString(asp.locations(), et);
            // Heliacal: show the phase tag (MF/EL/EF/ML) from the body's desc
            // instead of the meaningless H1. (The star name shows in T/P/S.)
            // A decomposed row shows its own occurrence's phase tag.
            if (et == A::etcHeliacalEvents || et == A::etcHeliacalStars
                || et == A::etcHeliacalLunar) {
                if (prow == -1) {
                    const int occi = _evs[row].occ;
                    const auto& lbls = _evs[row]->occurrenceLabels();
                    if (occi >= 0 && occi < lbls.size()) return lbls[occi];
                }
                for (const auto& loc : asp.locations())
                    if (!loc.desc.isEmpty()) return loc.desc;
                return QString();
            }
            // Named aspect sets show their icon (Qt::DecorationRole above)
            // instead of text in the cell — leave the cell text empty so the
            // icon isn't paired with a redundant/misleading "H#". Full name +
            // orb is still available in the tooltip. SummaryRole (used to
            // build compact chart-title text like "Sat-r tri Sun" in
            // rowDesc()) isn't a table cell, so it gets the abbreviation
            // instead of going blank.
            if (auto* d = resolveNamedAspectType(asp)) {
                if (role == SummaryRole) return A::aspectAbbrev(d->name);
                if (!d->userData["icon"].toString().isEmpty()) return QString();
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
            // Parans with no natal participation: render every body in the
            // transit column. The default getTColIters/getNTColIters split
            // for size==2 same-fid (faster vs slower) is wrong here because
            // all bodies are transit; the user wants them grouped.
            if ((et == A::etcParanatellonta || et == A::etcParanatellontaToNatal)
                && !asp.locations().empty() && !mixedMode(asp.locations()))
            {
                return glyphicWithMode(role,
                                       std::make_pair(asp.locations().begin(),
                                                      asp.locations().end()),
                                       _transitBodyColMode,
                                       et,
                                       false);
            }
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
            {
                // A decomposed apparition row renders its own occurrence — its
                // phase tag (Asp) AND its own zodiac position — rather than the
                // shared culmination anchor. Build a one-element set carrying the
                // overridden PlanetLoc so every display mode (glyph/text, cell and
                // tooltip) picks it up uniformly.
                const int occi =
                    (prow == -1
                     && (et == A::etcHeliacalEvents || et == A::etcHeliacalStars))
                        ? _evs[row].occ : -1;
                if (occi >= 0 && !asp.locations().empty()) {
                    const auto& lbls = _evs[row]->occurrenceLabels();
                    const auto& lons = _evs[row]->occurrenceLons();
                    const auto& spds = _evs[row]->occurrenceSpeeds();
                    A::PlanetLoc s = *asp.locations().begin();
                    if (occi < lbls.size()) s.desc = lbls[occi];
                    // Stars carry no meaningful ecliptic position; leave theirs.
                    if (occi < lons.size()
                        && s.planet.planetId() < A::Stars_Start)
                        s._rasiLoc = lons[occi];
                    if (occi < spds.size()) s.speed = spds[occi];
                    A::PlanetRangeBySpeed one;
                    one.insert(s);
                    return glyphicWithMode(role,
                                           getTColIters(one),
                                           _transitBodyColMode,
                                           et,
                                           false);
                }
                return glyphicWithMode(role,
                                       getTColIters(asp.locations()),
                                       _transitBodyColMode,
                                       et,
                                       false);
            }

        case natalTransitBodyCol:
            if (role == Qt::ForegroundRole) {
                if (mixedMode(asp.planets())) return ThemeManager::instance().getGoldColor();
                // else falls through to default return
                break;
            }

            // Parans with no natal participation: leave the natal column empty.
            if ((et == A::etcParanatellonta || et == A::etcParanatellontaToNatal)
                && !asp.locations().empty() && !mixedMode(asp.locations()))
            {
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

            case harmonicCol: {
                // Primary: group by event type
                if (a->eventType() < b->eventType()) return true;
                if (a->eventType() > b->eventType()) return false;

                const auto et        = a->eventType();
                const bool isParan   = (et == A::etcParanatellonta
                                        || et == A::etcParanatellontaToNatal);
                const bool isPattern = (et == A::etcTransitAspectPattern
                                        || et == A::etcTransitNatalAspectPattern);

                // Transits: harmonic before orb; patterns/parans: orb first
                if (!isParan && !isPattern) {
                    if (a->harmonic() < b->harmonic()) return true;
                    if (a->harmonic() > b->harmonic()) return false;
                }
                if (a->orb() < b->orb()) return true;
                if (a->orb() > b->orb()) return false;

                // Parans: Asp string as next tiebreaker
                if (isParan) {
                    auto sa = paranAngleString(a->locations(), et);
                    auto sb = paranAngleString(b->locations(), et);
                    if (sa < sb) return true;
                    if (sa > sb) return false;
                }

                // Duration tiebreaker (longer duration = more powerful = earlier)
                auto dura = a->range().first.isValid() ? a->range().days() : 0.0;
                auto durb = b->range().first.isValid() ? b->range().days() : 0.0;
                if (dura > durb) return true;
                if (dura < durb) return false;

                if (a->locations().size() < b->locations().size()) return true;
                if (a->locations().size() > b->locations().size()) return false;
                return (a->locations() < b->locations());
            }

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

    // Effective sort/display date for a row: a decomposed apparition-phase row
    // uses its own occurrence instant; everything else the event's headline.
    static QDateTime rowSortDate(const A::HarmonicEvent* ev, int occ)
    {
        if (occ >= 0) {
            const auto& ov = ev->occurrences();
            if (occ < ov.size()) return ov[occ].first;
        }
        return ev->dateTime();
    }

    // Push one row per selected phase of a heliacal apparition (fallback: the
    // culmination, else the first occurrence). Reads the per-chart phase mask.
    void emitPhaseRows(const A::HarmonicEvent& ev, const QStringList& labels)
    {
        const bool inner = labels.contains(QLatin1String("GEe"))
                        || labels.contains(QLatin1String("GWe"));
        const auto& occ = ev.occurrences();
        int  anchorIdx = -1;
        bool any       = false;
        for (int i = 0; i < labels.size(); ++i) {
            // The anchor stop carries magnitude 0 (Cul, or GEe/GWe for inner).
            if (i < occ.size() && occ[i].second == 0.0) anchorIdx = i;
            const unsigned bit = heliacalPhaseBit(labels[i], inner);
            if (bit && (_heliacalPhaseMask & bit)) {
                _evs.emplace_back(&ev, i);
                any = true;
            }
        }
        if (!any)
            _evs.emplace_back(&ev, anchorIdx >= 0 ? anchorIdx : 0);
    }

    // Internal helper: rebuild _evs from _evls (applying filters) and sort.
    // Emits NO model signals — caller is responsible for framing with
    // either layoutAboutToBeChanged/layoutChanged or beginResetModel/endResetModel.
    void doSortInternal(int column, Qt::SortOrder order)
    {
        _sortPending = false;

        typedef const A::HarmonicEvent* HEv;
        std::function<bool(HEv, HEv)>   less =
            hevLess(column, order == Qt::DescendingOrder);

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
                // Heliacal apparitions (fixed stars + inner/outer planets)
                // carry labelled phase occurrences; surface the per-chart-
                // selected phases as their own rows. The Moon's discrete
                // crescents carry no labels and stay single rows.
                const auto& labels = ev.occurrenceLabels();
                if (!labels.isEmpty()
                    && (ev.eventType() == A::etcHeliacalStars
                        || ev.eventType() == A::etcHeliacalEvents))
                    emitPhaseRows(ev, labels);
                else
                    _evs.emplace_back(ev);
            }
        }
        // Decomposed phase rows sort by their own occurrence instant (so MF/EL
        // land far from the culmination); other rows keep the standard order.
        std::sort(_evs.begin(), _evs.end(),
                  [&](const evp& a, const evp& b) {
            if (column == dateCol && (a.occ >= 0 || b.occ >= 0)) {
                const QDateTime da = rowSortDate(a.second, a.occ);
                const QDateTime db = rowSortDate(b.second, b.occ);
                if (da != db)
                    return order == Qt::DescendingOrder ? (db < da) : (da < db);
            }
            // Phase rows of one apparition share the anchor's sort key under
            // non-date columns; keep them in chronological (occ) order so
            // Acr/Cul/Cs never scramble.
            if (a.second == b.second && a.occ != b.occ)
                return a.occ < b.occ;
            return less(a, b);
        });
    }

    // Public sort: uses layoutAboutToBeChanged/layoutChanged so the view
    // preserves hover state, selection, and scroll position.
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override
    {
        emit layoutAboutToBeChanged();
        doSortInternal(column, order);
        emit layoutChanged();

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
        doSortInternal(_sortBy, _sortOrder);
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
            doSortInternal(_sortBy, _sortOrder);
            endResetModel();
            
            if (!_changeRef) emit changeDone();

            break;
        }
    }

    void clearAllEvents()
    {
        if (_evls.empty()) return;
        qDebug() << "[EVM] clearAllEvents() called, had" << _evls.size() << "events";
        if (!_changeRef) emit aboutToChange();
        beginResetModel();
        _evls.clear();
        _evs.clear();
        endResetModel();
    }

    /// Clear internal data without emitting aboutToChange.
    /// Used during file switches where the old data is already destroyed.
    void quietClear()
    {
        qDebug() << "[EVM] quietClear() called, had" << _evls.size() << "events";
        beginResetModel();
        _evls.clear();
        _evs.clear();
        endResetModel();
    }

    void setAspectSet(A::AspectSetId asps) { _aspects = asps; }

    int eventListCount() const { return static_cast<int>(_evls.size()); }

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
            // and composites (whose synthesized positions carry houseRuler)
            if (fileType == TypeMale || fileType == TypeFemale
                || fileType == TypeEvent || fileType == TypeComposite)
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
        const A::PlanetLoc& ploc, unsigned eventType) const
    {
        const A::ChartPlanetId& cpid      = ploc.planet;
        QString                 rulership = getHouseRulershipString(cpid);
        if (rulership.isEmpty()) return "";

        int natalHouse = 0;
        if (eventType == A::etcPrimaryDirections) {
            // Primary Direction rows deliberately store right ascension in
            // ploc.loc (see findPrimaryDirections()'s PlanetLoc construction
            // -- PD never leaves the radix, so there's no "current transit
            // position", only the RA the arc is computed from). Feeding
            // that RA through getHouse() (which expects ecliptic longitude)
            // silently gave a wrong house -- close, since RA and longitude
            // aren't wildly different, but often off by one. PD bodies never
            // move from their natal place, so just read the natal Planet's
            // own already-correct house instead of recomputing anything.
            const A::Horoscope& natal        = getNatalHoroscope();
            A::PlanetId         basePlanetId = cpid.planetId();
            if (natal.planets.contains(basePlanetId))
                natalHouse = natal.planets[basePlanetId].house;
        } else {
            // Use the transiting planet's actual position to find its natal house
            natalHouse = getNatalHouseForLongitude(ploc.loc);
        }
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
    QString planetToText(const A::PlanetLoc& ploc,
                         const QString& descOverride = QString(),
                         unsigned       eventType    = 0) const;

  public slots:
    void rebuild()
    {
        doSortInternal(_sortBy, _sortOrder);
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

    unsigned heliacalPhaseMask() const { return _heliacalPhaseMask; }

    // Display-only: changing which apparition phases surface changes the row
    // set, so rebuild via a model reset (no finder recompute).
    void setHeliacalPhaseMask(unsigned m)
    {
        if (_heliacalPhaseMask == m) return;
        _heliacalPhaseMask = m;
        beginResetModel();
        doSortInternal(_sortBy, _sortOrder);
        endResetModel();
    }

    // Occurrence index of a row (>=0 for a decomposed apparition-phase row).
    int rowOcc(int row) const
    {
        if (row < 0 || row >= int(_evs.size())) return -1;
        return _evs[row].occ;
    }

  signals:
    void aboutToChange();
    void changeDone();

  private:
    typedef unsigned short int eventListIndex;

    struct evp : public std::pair<eventListIndex, const A::HarmonicEvent*> {
        using Base = std::pair<eventListIndex, const A::HarmonicEvent*>;

        // For a decomposed heliacal apparition, the index of the phase
        // occurrence this row represents; -1 for an ordinary whole-event row.
        int occ = -1;

        static unsigned short int& curr()
        {
            static thread_local unsigned short int s_curr = 0;
            return s_curr;
        }

        using Base::Base;

        evp(const A::HarmonicEvent* ev) : Base(curr(), ev) { }
        evp(const A::HarmonicEvent* ev, int occ_) : Base(curr(), ev), occ(occ_) { }

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

    // Per-chart, display-only heliacal apparition phase selection (bit mask of
    // AstroFile::HeliacalPhaseBit). Decides which phases decompose into rows.
    unsigned _heliacalPhaseMask = AstroFile::kHeliacalPhaseDefault;
    
    AstroFile* _natalFile = nullptr; // Pointer to natal chart for rulership calculations

    // Per-event-type harmonic restrictions (event type → max harmonic)
    QMap<A::EventType, unsigned> _harmonicRestrictions;

    friend class AChangeSignalFrame;
};

// ---------------------------------------------------------------------------
// EventTypeFilterProxy — sits between EventsTableModel and the tree view,
// hiding rows whose event type is not in the enabled set.
// ---------------------------------------------------------------------------
class EventTypeFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

  public:
    explicit EventTypeFilterProxy(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setDynamicSortFilter(false);   // We manage invalidation explicitly
        setRecursiveFilteringEnabled(false);
    }

    void setEnabledEventTypes(const A::EventTypeSet& types)
    {
        if (_enabled == types) return;
        _enabled = types;
        if (!_patternActive) {
            beginFilterChange();
            endFilterChange();
        }
    }

    const A::EventTypeSet& enabledEventTypes() const { return _enabled; }

    /// When a pattern is active, disable event-type filtering — the pattern
    /// already controls what the finder computes.
    void setPatternActive(bool active)
    {
        if (_patternActive == active) return;
        _patternActive = active;
        beginFilterChange();
        endFilterChange();
    }

    bool patternActive() const { return _patternActive; }

    // --- Harmonic (dynAspState) filtering ---

    void setEnabledHarmonics(const A::uintSSet& hs)
    {
        if (_enabledHarmonics == hs) return;
        _enabledHarmonics = hs;
        if (!_patternActive) {
            beginFilterChange();
            endFilterChange();
        }
    }

    const A::uintSSet& enabledHarmonics() const { return _enabledHarmonics; }

    /// Check whether the source model contains any event with the given harmonic.
    bool sourceHasHarmonic(unsigned h) const
    {
        auto* src = qobject_cast<EventsTableModel*>(sourceModel());
        if (!src) return false;
        for (int r = 0, n = src->rowCount(); r < n; ++r) {
            if (src->rowData(r).harmonic() == h)
                return true;
        }
        return false;
    }

    // --- Duration (skipByDuration) filtering ---

    void setSkipByDuration(A::EventOptions::skipper s)
    {
        if (_skipByDuration == s) return;
        _skipByDuration = s;
        // Re-run the filter even in pattern mode: the duration gate now
        // applies there too (see filterAcceptsRow), so a skip-level change
        // must invalidate the proxy regardless of pattern state.
        beginFilterChange();
        endFilterChange();
    }

    A::EventOptions::skipper skipByDuration() const { return _skipByDuration; }

    /// Record the skip level that was used when the finder last computed events.
    /// This lets us determine whether a filter change needs recomputation.
    void setComputedSkipLevel(A::EventOptions::skipper s) { _computedSkipLevel = s; }
    A::EventOptions::skipper computedSkipLevel() const { return _computedSkipLevel; }

    /// Check whether the source model contains any events of the given type.
    /// Applies the same alias logic as filterAcceptsRow.
    bool sourceHasEventType(A::EventType et) const
    {
        auto* src = qobject_cast<EventsTableModel*>(sourceModel());
        if (!src) return false;

        // Map variant flags to the base type the finder actually tags
        A::EventType searchFor = et;
        if (et == A::etcOuterTransitToNatal) searchFor = A::etcTransitToNatal;
        else if (et == A::etcInnerProgressedToNatal) searchFor = A::etcProgressedToNatal;

        for (int r = 0, n = src->rowCount(); r < n; ++r) {
            if (src->rowData(r).eventType() == searchFor)
                return true;
        }
        return false;
    }

    // --- Date-range filtering ---

    /// Set a strict date range.  Events whose date falls outside
    /// [range.first, range.second] are hidden.  Pass a default-constructed
    /// (null) range to disable the gate.
    void setStrictDateRange(const A::ADateRange& range)
    {
        if (_strictRange == range) return;
        _strictRange = range;
        if (!_patternActive) {
            beginFilterChange();
            endFilterChange();
        }
    }

    const A::ADateRange& strictDateRange() const { return _strictRange; }

    bool hasStrictDateRange() const
    {
        return !_strictRange.first.isNull() && !_strictRange.second.isNull();
    }

  protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override
    {
        // Always accept child (coincidence) rows
        if (sourceParent.isValid()) return true;

        auto* src = qobject_cast<EventsTableModel*>(sourceModel());
        if (!src) return true;

        // Pattern mode: finder already computed exactly the event TYPES (and
        // harmonics) asked for, so the type/harmonic gates don't apply. But
        // "Skip by duration" is an independent display preference that should
        // still hide short-lived events the same way it does on the toolbar
        // path — otherwise pattern searches leak sub-threshold parans/transits.
        if (_patternActive) {
            if (_skipByDuration != A::EventOptions::SkipNone
                && !durationAccepted(sourceRow, src))
                return false;
            return true;
        }

        // --- Event-type gate ---
        // An empty enabled set means "nothing enabled" → show no events, NOT
        // "no filter / show everything". Otherwise disabling the last event
        // type reveals the entire cached set instead of clearing the table.
        {
            auto et = src->rowData(sourceRow).eventType();

            bool typeOk = _enabled.count(et) > 0;

            // Alias handling: etcOuterTransitToNatal and etcInnerProgressedToNatal
            // are selection-mode flags controlling which planets the finder computes,
            // but the resulting events are tagged with the base type.
            if (!typeOk && et == A::etcTransitToNatal
                && _enabled.count(A::etcOuterTransitToNatal) > 0)
                typeOk = true;
            if (!typeOk && et == A::etcProgressedToNatal
                && _enabled.count(A::etcInnerProgressedToNatal) > 0)
                typeOk = true;

            if (!typeOk) return false;
        }

        // --- Harmonic gate ---
        if (!_enabledHarmonics.empty()) {
            if (!harmonicAccepted(sourceRow)) return false;
        }

        // --- Duration gate ---
        if (_skipByDuration != A::EventOptions::SkipNone) {
            if (!durationAccepted(sourceRow, src)) return false;
        }

        // --- Date-range gate ---
        if (hasStrictDateRange()) {
            QDate d = src->rowData(sourceRow).dateTime().date();
            if (!_strictRange.contains(d)) return false;
        }

        return true;
    }

    /// Second-pass filter: after event type is accepted, check harmonic.
    /// Called only from filterAcceptsRow when _enabledHarmonics is non-empty.
    bool harmonicAccepted(int sourceRow) const
    {
        auto* src = qobject_cast<EventsTableModel*>(sourceModel());
        if (!src) return true;
        unsigned h = src->rowData(sourceRow).harmonic();
        return _enabledHarmonics.count(h) > 0;
    }

    /// Duration gate: hide events of skippable types whose duration is
    /// below the threshold.
    bool durationAccepted(int sourceRow, EventsTableModel* src) const
    {
        auto et = src->rowData(sourceRow).eventType();
        // Skippable event types for duration filtering
        if (et != A::etcTransitToTransit
            && et != A::etcTransitToNatal
            && et != A::etcTransitAspectPattern
            && et != A::etcTransitNatalAspectPattern
            && et != A::etcParanatellonta
            && et != A::etcParanatellontaToNatal)
            return true;

        double days = src->rowData(sourceRow).range().days();
        switch (_skipByDuration) {
        case A::EventOptions::SkipLessThanDay:   return days >= 1.0;
        case A::EventOptions::SkipLessThanWeek:  return days >= 7.0;
        case A::EventOptions::SkipLessThanMonth: return days >= 28.0;
        default: return true;
        }
    }

    // Disable proxy-level sorting — sorting is handled internally by
    // EventsTableModel::sort() which emits layoutChanged.
    bool lessThan(const QModelIndex&, const QModelIndex&) const override
    {
        return false;
    }

  private:
    A::EventTypeSet          _enabled;
    A::uintSSet              _enabledHarmonics;
    A::EventOptions::skipper _skipByDuration    = A::EventOptions::SkipNone;
    A::EventOptions::skipper _computedSkipLevel = A::EventOptions::SkipNone;
    bool                     _patternActive     = false;
    A::ADateRange            _strictRange;      ///< hide events outside this range (null = no filter)
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

// Aspect-set icons (Qt::DecorationRole, harmonicCol) are drawn full-size and
// left-anchored by the default delegate's icon-then-text layout, which — with
// the cell's text left empty (the icon carries the identity) — reads as an
// oversized, off-center glyph. Shrink the decoration to a text-line-sized box
// and center it; rows with no icon (Dynamic mode's "H4" text) are untouched.
class HarmonicColDelegate : public QStyledItemDelegate
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

    // decorationAlignment only centers the icon within its own decoration
    // sub-rect; that sub-rect is still left-anchored by the standard
    // icon-then-text layout even with empty text (there's no style flag for
    // "expect no text, center in the full cell"). So for icon rows, paint
    // the background/selection via the base delegate (with icon & text
    // cleared) and then draw the icon by hand, centered in the whole cell.
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        if (opt.icon.isNull()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        // QStyledItemDelegate::paint() calls initStyleOption() again
        // internally, re-fetching the icon from the model regardless of
        // what we clear here — so go through the style directly for the
        // background/selection/focus rect instead of the delegate.
        QStyleOptionViewItem bgOpt = opt;
        bgOpt.icon = QIcon();
        bgOpt.text.clear();
        QStyle* style = bgOpt.widget ? bgOpt.widget->style()
                                      : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &bgOpt, painter,
                           bgOpt.widget);

        QRect iconRect(0, 0, opt.decorationSize.width(),
                        opt.decorationSize.height());
        iconRect.moveCenter(opt.rect.center());
        opt.icon.paint(painter, iconRect, Qt::AlignCenter,
                       (opt.state & QStyle::State_Enabled) ? QIcon::Normal
                                                            : QIcon::Disabled,
                       (opt.state & QStyle::State_Open) ? QIcon::On
                                                         : QIcon::Off);
    }

protected:
    void initStyleOption(QStyleOptionViewItem* option,
                          const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (!option->icon.isNull()) {
            int edge = option->fontMetrics.height();
            option->decorationSize      = QSize(edge, edge);
            option->decorationAlignment = Qt::AlignCenter;
        }
    }
};

// Lightweight progress indicator painted directly over the pattern combo.
//
// Previously the search progress bar was drawn by calling
// QComboBox::setStyleSheet() on every progress tick. Each such call forces a
// full QStyleSheetStyle re-polish and a *synchronous* relayout of the combo's
// popup view (QListView::doItemsLayout); under the stream of progress events a
// search emits, that saturated the GUI thread and froze the UI. This overlay
// replaces that approach: it paints a translucent fill in paintEvent() and
// updates via cheap, coalesced update() calls — no stylesheet, no relayout. It
// is transparent to mouse events so the combo stays fully interactive, and it
// tracks its parent's geometry via an event filter.
class InputProgressOverlay : public QWidget
{
public:
    explicit InputProgressOverlay(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(parent->rect());
        parent->installEventFilter(this);
        hide();
    }

    // prog < 0      : indeterminate "waiting for pool" wash (muted full bar)
    // 0 <= prog < 1 : determinate fill growing from the left
    // prog >= 1     : finished — hide
    void setProgress(double prog)
    {
        _prog = prog;
        if (prog >= 1.0) {
            if (isVisible()) hide();
            return;
        }
        if (!isVisible()) show();
        raise();
        update();
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == parent()
            && (e->type() == QEvent::Resize || e->type() == QEvent::Move)) {
            setGeometry(static_cast<QWidget*>(parent())->rect());
        }
        return QWidget::eventFilter(o, e);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const QRectF r = rect();
        if (_prog < 0.0) {
            // Waiting phase: a muted, static wash across the whole field.
            p.fillRect(r, QColor(100, 149, 237, 45));
        } else {
            const qreal w = r.width() * qBound(0.0, _prog, 1.0);
            p.fillRect(QRectF(r.left(), r.top(), w, r.height()),
                       QColor(100, 149, 237, 80));
        }
    }

private:
    double _prog = -1;
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

    _filterProxy = new EventTypeFilterProxy(this);
    _filterProxy->setEnabledEventTypes(_tabEventOptions);

    // Model will be created and set in ensureEventsModel() when file(0) is available

    // Create and set custom header
    auto hdr = new TransitHeaderView(Qt::Horizontal, _tview);
    _tview->setHeader(hdr);
    _tview->setItemDelegateForColumn(EventsTableModel::harmonicCol,
                                      new HarmonicColDelegate(_tview));

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

    // Search-progress indicator overlaid on the combo (see InputProgressOverlay).
    _inputProgress = new InputProgressOverlay(_input);

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

    // Skip-by-duration split button (1d / 1w / 1m).  Placed second — right after
    // the copy-report button — so it stays visible rather than being buried at
    // the end of the long filter row.  Checkable main button toggles duration
    // filtering on/off; the dropdown picks the threshold; the main text shows the
    // active level.  Per-tab filter that mirrors AstroFile::_transitSkipByDuration.
    _btnSkipDuration = new LeftToolButton(toolbar);
    _btnSkipDuration->setText(skipLabel(_lastSkipLevel));
    _btnSkipDuration->setCheckable(true);
    _btnSkipDuration->setPopupMode(QToolButton::MenuButtonPopup);
    _btnSkipDuration->setToolTip("Hide short-lived events — click to toggle, dropdown to pick threshold (1d/1w/1m)");
    _btnSkipDuration->setStyleSheet("QToolButton { min-width: 30px !important; }");

    auto* skipMenu = new QMenu(_btnSkipDuration);
    auto* skipGroup = new QActionGroup(skipMenu);
    skipGroup->setExclusive(true);
    _actSkip1d = skipMenu->addAction("1d");
    _actSkip1d->setCheckable(true);
    _actSkip1d->setToolTip("Hide events shorter than 1 day");
    skipGroup->addAction(_actSkip1d);
    _actSkip1w = skipMenu->addAction("1w");
    _actSkip1w->setCheckable(true);
    _actSkip1w->setToolTip("Hide events shorter than 1 week");
    skipGroup->addAction(_actSkip1w);
    _actSkip1m = skipMenu->addAction("1m");
    _actSkip1m->setCheckable(true);
    _actSkip1m->setToolTip("Hide events shorter than 1 month");
    skipGroup->addAction(_actSkip1m);
    _btnSkipDuration->setMenu(skipMenu);
    toolbar->addWidget(_btnSkipDuration);

    // Main toggle: on → apply remembered level; off → SkipNone (show all).
    connect(_btnSkipDuration, &QToolButton::toggled, this, [this](bool on) {
        applySkipByDuration(on ? _lastSkipLevel : A::EventOptions::SkipNone);
        updateSkipDurationButton();
    });

    // Dropdown: pick the threshold.  Selecting a level remembers it and, if the
    // button is on, applies it; if off, turns filtering on at that level.
    connect(skipGroup, &QActionGroup::triggered, this, [this](QAction* a) {
        if      (a == _actSkip1d) _lastSkipLevel = A::EventOptions::SkipLessThanDay;
        else if (a == _actSkip1w) _lastSkipLevel = A::EventOptions::SkipLessThanWeek;
        else if (a == _actSkip1m) _lastSkipLevel = A::EventOptions::SkipLessThanMonth;
        applySkipByDuration(_lastSkipLevel);
        updateSkipDurationButton();
    });

    // Refresh / auto-reconcile split button (replaces the old ↻ auto-recalc
    // toggle).  Main button click reconciles pending changes now; its background
    // is yellow when a recompute is pending and green when up to date (set in
    // updateRefreshButtonState).  The dropdown holds the "Auto" toggle that
    // pauses/resumes automatic reconciliation.
    _btnRefresh = new LeftToolButton(toolbar);
    _btnRefresh->setText("⟳");
    _btnRefresh->setPopupMode(QToolButton::MenuButtonPopup);
    _btnRefresh->setToolTip("Refresh now — recompute pending changes.\nYellow = recompute pending, green = up to date.\nDropdown: toggle auto-refresh.");

    auto* refreshMenu = new QMenu(_btnRefresh);
    _actAuto = refreshMenu->addAction("Auto");
    _actAuto->setCheckable(true);
    _actAuto->setChecked(_autoReconcile);
    _actAuto->setToolTip("Automatically recompute when event filters change");
    _btnRefresh->setMenu(refreshMenu);
    toolbar->addWidget(_btnRefresh);

    // Main button: explicit user refresh — reconcile even if Auto is off.
    connect(_btnRefresh, &QToolButton::clicked, this, [this](bool) {
        A::modalize<bool> forceAuto(_autoReconcile, true);
        reconcile();
        updateRefreshButtonState();
    });

    // Dropdown Auto toggle: pause/resume; flush any pending delta on resume.
    connect(_actAuto, &QAction::triggered, this, [this](bool checked) {
        _autoReconcile = checked;
        if (filesCount() > 0 && file(0)) {
            A::modalize<bool> guard(_inhibitUpdate, true);
            file(0)->setTransitAutoReconcile(checked);   // persist per-tab
        }
        if (checked) reconcile();
        updateRefreshButtonState();
    });

    toolbar->addSeparator();

    // Thin forwarder retained so the event-filter button connects below can
    // keep their (bool checked) signature; the new member reconciles based on
    // the derived EventStore manifest state rather than a passed-in flag.
    auto saveEventOptionsAndRecalc = [this](bool checked) {
        saveEventOptionsAndReconcile(checked);
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

    // Event filter buttons: grouped split-buttons. Each consolidates several
    // related event types behind a master on/off; the dropdown menu holds the
    // members (some in optional-exclusive radio subgroups). See
    // buildEventGroupButton() for the interaction model.

    // [T▼] Transits
    buildEventGroupButton(toolbar, "T", "Transits", {
        { A::etcTransitToTransit },
        { A::etcSignIngress },
        { A::etcUnknownEvent },                        // separator
        { A::etcTransitToNatal,       0 },             // radio subgroup 0
        { A::etcOuterTransitToNatal,  0 },             // radio subgroup 0
        { A::etcUnknownEvent },                        // separator
        { A::etcTransitToNatalAngles },
        { A::etcHouseIngress },
    });

    // [P▼] Progressions
    buildEventGroupButton(toolbar, "P", "Progressions", {
        { A::etcProgressedToProgressed },
        { A::etcUnknownEvent },                        // separator
        { A::etcProgressedToNatal,      0 },           // radio subgroup 0
        { A::etcInnerProgressedToNatal, 0 },           // radio subgroup 0
    });

    // [AP▼] Aspect patterns
    buildEventGroupButton(toolbar, "AP", "Aspect Patterns", {
        { A::etcTransitAspectPattern },
        { A::etcTransitNatalAspectPattern },
    });

    // Primary Directions button. Classical Placidian semi-arc directions
    // (promissor bodies + Ptolemaic rays, directed to natal planets/angles as
    // significators, dated via the Naibod/Ptolemy key) — a closed-form
    // enumeration over the radix, computed by
    // AspectFinder::findPrimaryDirections().
    _actPrimaryDirections = toolbar->addAction("PD");
    _actPrimaryDirections->setCheckable(true);
    _actPrimaryDirections->setToolTip(tr("Primary Directions"));
    if (auto* btn = qobject_cast<QToolButton*>(toolbar->widgetForAction(_actPrimaryDirections))) {
        btn->setStyleSheet("QToolButton { min-width: 20px !important; }");
    }
    connect(_actPrimaryDirections, &QAction::triggered, this,
            [this, saveEventOptionsAndRecalc](bool checked) {
        if (checked) _tabEventOptions.insert(A::etcPrimaryDirections);
        else _tabEventOptions.erase(A::etcPrimaryDirections);
        saveEventOptionsAndRecalc(checked);
    });

    // [Par▼] Paranatellonta
    buildEventGroupButton(toolbar, "Par", "Paranatellonta", {
        { A::etcParanatellonta,       -1, "Par=T" },   // relabel the transit-only item
        { A::etcParanatellontaToNatal },
    });

    // [HE▼] Heliacal risings/settings: Planets (default on) + optional fixed
    // Stars and Moon crescents (both default off — Stars' catalog is sizable
    // and slow; the Moon's EF/ML are frequent and noisy).
    QToolButton* heBtn =
        buildEventGroupButton(toolbar, "Hel", "Heliacal Risings/Settings", {
            { A::etcHeliacalEvents, -1, "Planets" },
            { A::etcHeliacalStars,  -1, "Stars", false },
            { A::etcHeliacalLunar,  -1, "Moon",  false },
        });

    // Display-only phase decomposition: which apparition phases surface as their
    // own rows in the list. These are NOT event types — toggling them only
    // re-lists the already-computed apparition, never recomputes, and is saved
    // per-chart. Two families: the star/outer 5-stop culmination model and the
    // inner-planet elongation model.
    if (QMenu* hm = heBtn->menu()) {
        auto addPhase = [&](unsigned bit, const QString& label) {
            QAction* act = hm->addAction(label);
            act->setCheckable(true);
            act->setChecked((AstroFile::kHeliacalPhaseDefault & bit) != 0);
            _heliacalPhaseActions.append({ bit, act });
            connect(act, &QAction::toggled, this, [this, bit](bool on) {
                unsigned mask = (filesCount() > 0 && file(0))
                    ? file(0)->getHeliacalPhaseMask()
                    : (_evm ? _evm->heliacalPhaseMask()
                            : unsigned(AstroFile::kHeliacalPhaseDefault));
                if (on) mask |= bit; else mask &= ~bit;
                if (filesCount() > 0 && file(0))
                    file(0)->setHeliacalPhaseMask(mask);
                if (_evm) _evm->setHeliacalPhaseMask(mask);  // re-list only
            });
        };

        hm->addSeparator();
        QAction* h1 = hm->addAction(tr("Star / outer-planet phases:"));
        h1->setEnabled(false);
        addPhase(AstroFile::hpMF,  tr("Morning First (MF)"));
        addPhase(AstroFile::hpAcr, tr("Acronychal rising (Acr)"));
        addPhase(AstroFile::hpCul, tr("Culmination (Cul)"));
        addPhase(AstroFile::hpCs,  tr("Cosmic setting (Cs)"));
        addPhase(AstroFile::hpEL,  tr("Evening Last (EL)"));

        hm->addSeparator();
        QAction* h2 = hm->addAction(tr("Inner-planet phases:"));
        h2->setEnabled(false);
        addPhase(AstroFile::hpElong, tr("Greatest elongation (G*e)"));
        addPhase(AstroFile::hpFirst, tr("First visibility (*F)"));
        addPhase(AstroFile::hpLast,  tr("Last visibility (*L)"));
    }

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
            // Date-range change is a hard invalidation (manifest can't model it)
            for (int i = 0, n = filesCount(); i < n; ++i) {
                file(i)->markEventsForRecalc();
            }
            // reconcile() recomputes now if Auto is on, else lights Refresh.
            reconcile();
        }
    });

    connect(_forth, &QAbstractButton::clicked, [this] {
        auto ed = _end->date();
        auto dd = _start->date().daysTo(ed) / 2;
        if (dd) {
            _end->setDate(ed.addDays(dd));
            // Date-range change is a hard invalidation (manifest can't model it)
            for (int i = 0, n = filesCount(); i < n; ++i) {
                file(i)->markEventsForRecalc();
            }
            // reconcile() recomputes now if Auto is on, else lights Refresh.
            reconcile();
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
    // Do NOT touch file(0) here. During application shutdown the AstroFile
    // objects are destroyed before this child widget's destructor runs (MainWindow
    // members are freed, then ~QObject deletes the dock/Transits children), so
    // file(0) would be a dangling pointer and getName()/setTransit*() would be a
    // use-after-free. We don't need to save anything at destruction anyway:
    // _tabEventOptions and the pattern are already persisted to file(0) at every
    // change point (saveEventOptionsAndReconcile, plus the combo's activated /
    // editingFinished handlers) and whenever the active file switches.

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
    qDebug() << "[DESCRIBE PLANET] called, filesCount:" << filesCount();
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

bool
Transits::resumeActiveFinder()
{
    if (filesCount() == 0 || !file(0) || !_evm) return false;
    auto fit = _finders.find(file(0));
    if (fit == _finders.end()) return false;
    auto& fs = fit.value();
    if (!fs.finder || !fs.thread || fs.thread->isFinished()) return false;

    qDebug() << "[RESUME FINDER] Resuming existing finder for"
             << file(0)->getName() << "state:" << fs.finder->getState();

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
        qDebug() << "[RESUME FINDER] Calling resume() on paused finder";
        fs.finder->resume();
    }
    return true;
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
    return (ftype != TypeMale && ftype != TypeFemale && ftype != TypeEvent
            && ftype != TypeReturn && ftype != TypeComposite);
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

    // The events model is owned per-file, so on a tab switch/restore the
    // newly-activated model must be re-seeded from the file's saved heliacal
    // phase selection before it sorts — otherwise it decomposes with its stale
    // default mask even though the toolbar checkboxes (read straight from the
    // file) look correct. Target evm directly, not _evm, which may still point
    // at the previous tab's model at this point.
    evm->setHeliacalPhaseMask(file(0)->getHeliacalPhaseMask());

    // Update our local pointer and view if needed
    if (_evm != evm) {
        _evm = evm;
        _filterProxy->setSourceModel(_evm);
        _tview->setModel(_filterProxy);
        
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

    auto applyTimezone = [this](double tz, const QString& sourceLabel) {
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

        qDebug() << "Timezone for location is" << tz << "from" << sourceLabel;

        if (transitsOnly()) {
            // Event times are stored in UTC and displayed via the model's
            // _tzOffset, which we already updated above.  Most transit
            // event types (T=T, stations, ingresses, returns, etc.) are
            // location-independent, so a full recalc is unnecessary.
            //
            // Only house-ingress and paranatellonta depend on the
            // observer's location.  Recalc only when those are active.
            // House-ingress, paranatellonta AND heliacal events all depend on
            // the observer's location, so a location change must recompute them.
            bool needsRecalc =
                _tabEventOptions.count(A::etcHouseIngress)
                || _tabEventOptions.count(A::etcParanatellonta)
                || _tabEventOptions.count(A::etcHeliacalEvents)
                || _tabEventOptions.count(A::etcHeliacalStars)
                || _tabEventOptions.count(A::etcHeliacalLunar);

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
            // Natal + transit tab. The dock widget is the authoritative observer
            // location (file(0)->transitLocation), so ALWAYS push it to
            // transitsAF (file(1)) and recompute location-dependent events
            // (heliacal, parans, ingresses).  A manual timezone lock preserves
            // only the clock, not the observer location — otherwise a relocated
            // bi-wheel keeps computing heliacal events at the stale (e.g. natal)
            // location while the widget shows the intended one.
            //
            // Block filesUpdated so the changed() signal from resumeUpdate
            // doesn't trigger updateTransits() early — before markEventsForRecalc().
            // filesUpdated only recognises Location on file(1) as needing recalc
            // when the file type is natal, which the transit file is not, so
            // without _inhibitUpdate the cache check in updateTransits() would
            // return the stale (old-location) events.
            {
                A::modalize<bool> noup(_inhibitUpdate);
                transitsAF()->suspendUpdate();
                transitsAF()->setLocation(_location->location());
                transitsAF()->setLocationName(_location->locationName());
                if (!transitsAF()->isTimezoneLocked())
                    transitsAF()->setTimezone(short(tz));
                transitsAF()->resumeUpdate();
            }

            stopThreads();
            file(0)->markEventsForRecalc();
            emit updateSecond(transitsAF());
            // Redrawing file(1) does NOT recompute — filesUpdated ignores a
            // Location change on a transit-type file — so trigger the recompute
            // explicitly, exactly like a date-range change: recompute now if
            // Auto is on, else light the Refresh button. Without this, a natal
            // tab's location-dependent events (parans, heliacal, ingresses)
            // stay stale after the observer location moves.
            reconcile();
        }
    };

    const QString timezoneId = _location->selectedTimezoneId();
    if (!timezoneId.isEmpty()) {
        const QTimeZone tz(timezoneId.toUtf8());
        if (tz.isValid()) {
            applyTimezone(tz.offsetFromUtc(transitsAF()->getGMT()) / 3600.0,
                          QString("local city DB (%1)").arg(timezoneId));
            return;
        }
    }

    auto nm = new QNetworkAccessManager(this);
    connect(nm, &QNetworkAccessManager::finished, [this, applyTimezone](QNetworkReply* reply) {
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
                    + response["dstOffset"].toInt())
                   / 3600;
        applyTimezone(tz, response["timeZoneName"].toString());
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

    // Restore location from the canonical tab source FIRST — before the
    // auto-reconcile gate below.  This is a pure display/state sync (no event
    // recompute), so it must run on every tab switch even when Auto is off;
    // otherwise the _location widget keeps showing the previous tab's location.
    // The canonical events-table location lives in file(0)->transitLocation /
    // the _location dock-widget.  file(1) is a *consumer* of that location —
    // we never read file(1)'s location back into the canonical store.
    AstroFile* locFile = nullptr;
    if (filesCount() >= 2) {
        // Two files present: the dock widget / file(0)->transitLocation is
        // authoritative.  Push it to transitsAF() below via the locFile==nullptr
        // path (hasTransitLocation branch).  Do NOT read from file(1).
        locFile = nullptr; // handled by the hasTransitLocation branch below
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
    } else if (file(0)->hasTransitLocation()) {
        // Use file(0)'s stored transit location as the canonical source.
        // This covers both the 2-file case (where file(1) is a downstream
        // consumer, not a source) and the 1-file case after file-2 close.
        _pendingLocationChange = true;
        _location->setLocation(file(0)->getTransitLocation());
        _location->setLocationName(file(0)->getTransitLocationName());
        _pendingLocationChange = false;

        // The stored transit location is the authoritative observer location;
        // push it to transitsAF() so the finder (and location-dependent events
        // like heliacal/parans) observe from it. Respect a manual timezone lock
        // only for the clock.
        if (transitsAF()->getLocation() != file(0)->getTransitLocation()) {
            transitsAF()->suspendUpdate();
            transitsAF()->setLocation(file(0)->getTransitLocation());
            transitsAF()->setLocationName(file(0)->getTransitLocationName());
            if (!transitsAF()->isTimezoneLocked())
                transitsAF()->setTimezone(file(0)->getTransitTimezone());
            transitsAF()->resumeUpdate();
            // Invalidate the cache only when the observer file is this tab's
            // own (a bi-wheel's file(1) that carried a stale, e.g. natal,
            // location) — there the mismatch means location-dependent events
            // really were computed elsewhere.  For a single-file natal tab
            // transitsAF() is the SHARED _trans scratch file, whose location
            // is just "whatever natal tab was current last"; invalidating on
            // that mismatch threw away every natal tab's cache on each tab
            // switch (and canceled in-flight finders).  Genuine location
            // edits invalidate explicitly in updateTimezone().
            if (filesCount() >= 2)
                file(0)->markEventsForRecalc();
        }
    }

    // Auto-reconcile off ("compute on demand"): don't recompute automatically.
    // Any existing events stay as-is and the Refresh button surfaces the pending
    // state (yellow).  The explicit Refresh button force-enables _autoReconcile,
    // so a user-initiated refresh still computes.  (Location sync above already
    // ran, so the widget reflects the current tab regardless.)
    if (!_autoReconcile) {
        qDebug() << "[UPDATE TRANSITS] Auto-reconcile off — skipping automatic recompute";
        ensureEventsModel();
        // A live finder for this tab (paused when the tab was switched away,
        // or still running in background mode) must be reconnected and
        // resumed even though Auto is off — the user explicitly started that
        // search (Refresh force-enables Auto only for the click).  Without
        // this it sat paused forever: partial table, dead progress overlay,
        // and only a hard restart could clear it.
        if (resumeActiveFinder()) {
            updateRefreshButtonState();
            return;
        }
        // "Compute on demand" defers only the *recompute*. A tab switch
        // quietClear()s the model, so still repopulate it from cache when it's
        // empty — otherwise the switched-to tab shows nothing until Refresh.
        if (_evm && _evm->rowCount() == 0 && !file(0)->events().empty()) {
            const A::Horoscope& scope(file()->horoscope());
            _evm->setZodiac(scope.zodiac);
            _evm->setTimezone(transitsAF()->getTimezone());
            _evm->clearAllEvents();
            _evm->addEvents(file(0)->events());
            _evm->sort();
        }
        updateRefreshButtonState();
        return;
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
                if (resumeActiveFinder()) return;
            }
        } else {
            // Thread's run() has returned (isFinished() is true) but its queued
            // finished() signal may not have been delivered yet — the main
            // thread can go a long time between event-loop pumps during
            // restoreSession()'s back-to-back property-setter cascade, so
            // onCompleted() hasn't necessarily run for this finder yet. If we
            // just erase the bookkeeping and leave the connections live, that
            // signal fires later and onCompleted() — unable to find this
            // (already erased) entry by thread pointer — falls back to
            // treating it as "current tab" and clobbers _active/_activeFinder,
            // orphaning whatever finder we start below (which by then owns
            // those aliases) while it's still writing into the shared events
            // list by reference. Disconnect first, exactly like
            // cancelAndRemoveFinder(), so that belated signal can't fire.
            qDebug() << "[UPDATE TRANSITS] Stale finder entry for" << file(0)->getName() << ", removing";
            disconnectFinder(fs);
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

    auto* evmForAspects = ensureEventsModel();
    auto  hs = evmForAspects ? A::activeHarmonicSet(evmForAspects->aspects())
                              : A::dynAspState();
    _filterProxy->setEnabledHarmonics(hs);
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
    _filterProxy->setPatternActive(usePattern);

    // Pattern is valid (or empty) — now safe to clear events
    _evm->clearAllEvents();
    evs.clear();
    file(0)->clearManifest();  // reset manifest for fresh computation

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
            opts.skipByDuration = _tabSkipByDuration;  // tab-specific duration filter
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
        opts.skipByDuration = _tabSkipByDuration;  // tab-specific duration filter
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

    // Sync proxy with the skip level used for this computation (tab-specific)
    _filterProxy->setSkipByDuration(_tabSkipByDuration);
    _filterProxy->setComputedSkipLevel(_tabSkipByDuration);

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

    // Store in per-file finder map (including manifest metadata)
    AstroFile* ownerFile = file(0);
    if (ownerFile) {
        FinderState fs;
        fs.thread             = thread;
        fs.finder             = af;
        fs.chs                = _chs;
        fs.searchedTypes      = usePattern ? A::EventTypeSet{} : _tabEventOptions;
        fs.searchedRange      = r;
        fs.searchedHarmonics  = hs;
        fs.searchedSkip       = static_cast<unsigned>(_tabSkipByDuration);
        _finders[ownerFile]   = fs;

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

A::ADateRange
Transits::currentRange() const
{
    return A::ADateRange { _start->date(), _end->date() };
}

A::EventTypeSet
Transits::desiredStale() const
{
    if (filesCount() == 0 || !file(0)) return {};
    // Pattern mode: the pattern finder computes its own set and the proxy
    // accepts all rows, so there's no type/skip-based recompute to derive.
    if (_filterProxy && _filterProxy->patternActive()) return {};
    return file(0)->staleTypes(_tabEventOptions, currentRange(),
                               static_cast<unsigned>(_tabSkipByDuration));
}

bool
Transits::needsRefresh() const
{
    if (filesCount() == 0 || !file(0)) return false;
    // Hard invalidations (orbs/location/date-range) keep using the existing
    // flag; the manifest covers the type/range/skip axes.
    return file(0)->needsEventsRecalc() || !desiredStale().empty();
}

void
Transits::updateRefreshButtonState()
{
    if (!_btnRefresh) return;
    // Yellow when a recompute is pending, green when the view is up to date.
    bool stale = needsRefresh();
    const char* bg = stale ? "#E0B000" /*amber*/ : "#3FA34D" /*green*/;
    // Background via stylesheet; the label color is applied by LeftToolButton's
    // custom paint (the stylesheet `color` doesn't reach its drawText).
    _btnRefresh->setStyleSheet(QString(
        "QToolButton { font-weight: normal; font-size: 13pt; "
        "min-width: 28px; min-height: 24px; padding: 1px; margin: 0px; "
        "background-color: %1; }").arg(bg));
    if (auto* b = static_cast<LeftToolButton*>(_btnRefresh))
        b->setTextColor(stale ? Qt::black : Qt::white);
}

void
Transits::reconcile()
{
    if (!_autoReconcile || filesCount() == 0 || !file(0)) {
        updateRefreshButtonState();
        return;
    }

    // Any staleness — hard invalidation (orbs/location/date-range) or a derived
    // type/range/skip change — triggers a full recompute.  A full updateTransits
    // is correct for every event type; the scoped-append optimization is not,
    // because several types (stations, ingresses, returns) are only produced as
    // a byproduct of the full transit scan and compute to nothing in isolation.
    // Batching (pause Auto, toggle several filters, Refresh once) is the cure for
    // the cost of repeated full recomputes.
    if (file(0)->needsEventsRecalc() || !desiredStale().empty())
        updateTransits();
    else
        updateRefreshButtonState();
}

void
Transits::onChartTimeDragged(AstroFile* draggedFile)
{
    // Only react when the dragged chart is the natal reference (file(0) of this
    // tab and a natal/event type). Dragging a transit/derived chart's display
    // moment doesn't change the (date-range-based) event list, so leave it.
    if (filesCount() == 0 || !file(0) || draggedFile != file(0)) return;
    const FileType t = file(0)->getType();
    // Composite included: dragging the ref time moves the composite houses/
    // angles and the paran epoch, so angle/cusp/paran events go stale.
    if (t != TypeMale && t != TypeFemale && t != TypeEvent
        && t != TypeComposite) return;

    // The natal moment moved → natal-relative events are stale. Mark and
    // reconcile (recomputes now if Auto is on, else lights the Refresh button).
    file(0)->markEventsForRecalc();
    reconcile();
}

void
Transits::saveEventOptionsAndReconcile(bool added)
{
    // Guard: prevent changed() signals from setTransit* cascading into
    // filesUpdated() and clobbering the model — we drive the proxy ourselves.
    {
        A::modalize<bool> guard(_inhibitUpdate, true);
        if (filesCount() > 0 && file(0)) {
            file(0)->setTransitPattern(_input->currentText().trimmed());
            file(0)->setTransitEventOptions(_tabEventOptions);
            file(0)->setTransitSkipByDuration(_tabSkipByDuration);
        }
    }

    // If a pattern is active, toolbar changes only affect saved prefs; the
    // proxy accepts all rows, so there's nothing to filter/recompute.
    if (_filterProxy && _filterProxy->patternActive()) {
        updateRefreshButtonState();
        return;
    }

    // Update the proxy filter so rows hide/show immediately.
    if (_filterProxy) _filterProxy->setEnabledEventTypes(_tabEventOptions);

    // Enabling a type always needs a (full) recompute to bring in its events —
    // mark it explicitly rather than trusting manifest-derived staleness, since
    // some types (stations/ingresses/returns) compute only as part of the full
    // scan.  Disabling a type is filter-only (the proxy already hid the rows).
    if (added && filesCount() > 0 && file(0))
        file(0)->markEventsForRecalc();

    reconcile();
}

void
Transits::applySkipByDuration(A::EventOptions::skipper s)
{
    if (_tabSkipByDuration == s) {
        updateRefreshButtonState();
        return;
    }
    // Relaxing (lower enum value = shorter threshold = more events kept) needs a
    // recompute: the now-wanted shorter events were never computed.  Tightening
    // is filter-only — the proxy just hides more rows.
    bool relaxing = static_cast<unsigned>(s)
                  < static_cast<unsigned>(_tabSkipByDuration);
    _tabSkipByDuration = s;
    if (filesCount() > 0 && file(0)) {
        A::modalize<bool> guard(_inhibitUpdate, true);
        file(0)->setTransitSkipByDuration(s);
    }
    // Instant hide/show in the proxy regardless of whether a recompute follows.
    if (_filterProxy) _filterProxy->setSkipByDuration(s);
    if (relaxing && filesCount() > 0 && file(0))
        file(0)->markEventsForRecalc();
    reconcile();
}

void
Transits::updateSkipDurationButton()
{
    if (!_btnSkipDuration) return;
    bool on = (_tabSkipByDuration != A::EventOptions::SkipNone);
    auto lvl = on ? _tabSkipByDuration : _lastSkipLevel;
    QSignalBlocker block(_btnSkipDuration);
    _btnSkipDuration->setChecked(on);
    _btnSkipDuration->setText(skipLabel(lvl));
    if (_actSkip1d) _actSkip1d->setChecked(lvl == A::EventOptions::SkipLessThanDay);
    if (_actSkip1w) _actSkip1w->setChecked(lvl == A::EventOptions::SkipLessThanWeek);
    if (_actSkip1m) _actSkip1m->setChecked(lvl == A::EventOptions::SkipLessThanMonth);
}

/// Launch a scoped finder that computes only the specified event types.
/// Unlike updateTransits(), this does NOT clear existing events — results
/// are appended to file(0)->events() and the model is updated incrementally.
void
Transits::launchScopedFinder(const A::EventTypeSet& types)
{
    if (types.empty() || filesCount() == 0 || !file(0)) return;
    if (!isVisible()) return;

    // Don't launch if there's already an active finder for this file
    {
        auto fit = _finders.find(file(0));
        if (fit != _finders.end() && fit.value().thread
            && !fit.value().thread->isFinished()) {
            qDebug() << "[SCOPED FINDER] Active finder exists, falling back to full updateTransits";
            updateTransits();
            return;
        }
    }

    ensureEventsModel();
    if (!_evm) return;

    auto       hs  = A::activeHarmonicSet(_evm->aspects());
    ADateRange r { _start->date(), _end->date() };
    auto&      evs = file(0)->events();

    // Build EventOptions restricted to only the missing types
    A::EventOptions opts = A::EventOptions::current();
    opts.enabledEvents        = types;
    opts.skipByDuration       = _tabSkipByDuration;  // tab-specific duration filter
    opts.harmonicRestrictions = file(0)->getTransitHarmonicRestrictions();

    qDebug() << "========================================";
    qDebug() << "[SCOPED FINDER] Launching for" << types.size() << "missing type(s)"
             << "range:" << r.first << "-" << r.second;

    A::AspectFinder* af = nullptr;
    auto ftype = file(0)->getType();
    if (ftype != TypeOther) {
        af = new A::OmnibusFinder(evs, r, hs, { file(0), transitsAF() }, opts);
    } else {
        af = new A::OmnibusFinder(evs, r, hs, files(), opts);
    }
    if (!af) return;

    // Sync proxy settings (tab-specific skip level)
    _filterProxy->setSkipByDuration(_tabSkipByDuration);
    _filterProxy->setComputedSkipLevel(_tabSkipByDuration);

    // The model already has events — ensure evs reference is registered
    // (it may have been cleared on a prior tab switch and re-added).
    const A::Horoscope& scope(file()->horoscope());
    _evm->setZodiac(scope.zodiac);
    _evm->setTimezone(transitsAF()->getTimezone());
    // Only re-register if the model has no event lists (was cleared)
    if (_evm->eventListCount() == 0) {
        _evm->addEvents(evs);
    }

    if (!_chs) {
        _chs = new AChangeSignalFrame(_evm);
    }

    auto thread = new QThread(this);
    QString chartName = file(0)->getName();
    thread->setObjectName(QString("scoped-finder:%1").arg(chartName));
    af->moveToThread(thread);

    connect(this, SIGNAL(cancelActive()), af, SLOT(cancel()));
    connect(thread, SIGNAL(started()), af, SLOT(findStuff()));
    connect(af, SIGNAL(progress(double)), this, SLOT(onProgress(double)));
    connect(thread, SIGNAL(finished()), this, SLOT(onCompleted()));
    connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
    connect(thread, &QThread::finished, this, [af]() {
        delete af;
    });

    thread->start();
    _active       = thread;
    _activeFinder = af;

    // Store in finder map with manifest metadata for just the scoped types
    AstroFile* ownerFile = file(0);
    FinderState fs;
    fs.thread             = thread;
    fs.finder             = af;
    fs.chs                = _chs;
    fs.searchedTypes      = types;
    fs.searchedRange      = r;
    fs.searchedHarmonics  = hs;
    fs.searchedSkip       = static_cast<unsigned>(_tabSkipByDuration);
    _finders[ownerFile]   = fs;

    // Clean up on file destruction
    disconnect(ownerFile, &QObject::destroyed, this, nullptr);
    connect(ownerFile, &QObject::destroyed, this, [this, ownerFile]() {
        auto it = _finders.find(ownerFile);
        if (it != _finders.end()) {
            if (it.value().finder) it.value().finder->cancel();
            delete it.value().chs;
            if (it.value().thread && !it.value().thread->isFinished())
                it.value().thread->wait();
            if (_active == it.value().thread) {
                _active       = nullptr;
                _activeFinder = nullptr;
                _chs          = nullptr;
            }
            _finders.erase(it);
        }
    });

    qDebug() << "[SCOPED FINDER] Started thread" << thread;
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
            // Defensive: stop ourselves if the finder finished but
            // onCompleted() somehow missed stopping us.
            if (!_activeFinder) {
                _progressSortTimer->stop();
                return;
            }
            if (_evm) {
                _evm->sort();
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

    // Find which file this finder belongs to and remove from map. Match
    // strictly by sender() (the QThread that actually emitted finished()) —
    // NOT by falling back to "thread == _active / finder == _activeFinder",
    // which matches whichever finder currently owns those aliases regardless
    // of whether it's the one that actually emitted this signal. A belated
    // finished() signal from an old finder (queued before its map entry was
    // erased by updateTransits()'s stale-entry cleanup, but not delivered
    // until a later event-loop pump — disconnecting doesn't retroactively
    // cancel an already-queued invocation) would otherwise be misattributed
    // to whatever finder is *currently* active/current-tab, erasing its
    // live map entry and nulling _active/_activeFinder out from under it —
    // orphaning a still-running finder that keeps writing into the shared
    // events list by reference while a subsequent updateTransits() call,
    // seeing no active finder, starts yet another one. That's what produced
    // duplicated events on session restore (see project memory).
    auto* senderThread = sender();
    AstroFile* ownerFile = nullptr;
    FinderState completedState;  // capture metadata before erasing
    for (auto it = _finders.begin(); it != _finders.end(); ++it) {
        if (it.value().thread == senderThread) {
            ownerFile = it.key();
            completedState = it.value();
            // Delete _chs stored in the map entry
            delete it.value().chs;
            it.value().chs = nullptr;
            completedState.chs = nullptr;
            _finders.erase(it);
            break;
        }
    }

    if (!ownerFile) {
        // No live entry for this thread — it was already cleaned up
        // elsewhere (canceled or superseded). _active/_activeFinder may by
        // now belong to a different, still-running finder for this same
        // file; leave them alone.
        qDebug() << "[ON COMPLETED] No matching finder entry for sender"
                 << senderThread << "(stale/belated signal), ignoring";
        return;
    }

    bool isCurrentTab = (filesCount() > 0 && ownerFile == file(0));
    qDebug() << "[ON COMPLETED] ownerFile:" << ownerFile->getName()
             << "isCurrentTab:" << isCurrentTab;

    if (!isCurrentTab) {
        // Background finder finished for a non-current tab.
        // Events are already written to ownerFile->events() by reference.
        // Just clear recalc flag, populate manifest, and clean up.
        qDebug() << "[ON COMPLETED] Background finder done, no UI update needed";
        if (ownerFile) {
            ownerFile->clearEventsRecalcFlag();
            if (!completedState.searchedTypes.empty()) {
                ownerFile->ingestEvents(ownerFile->events());
                ownerFile->recordSearch(completedState.searchedTypes,
                                        completedState.searchedRange,
                                        completedState.searchedHarmonics,
                                        completedState.searchedSkip);
            }
        }
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
        // Populate EventStore manifest from completed finder
        if (!completedState.searchedTypes.empty()) {
            file(0)->ingestEvents(file(0)->events());
            file(0)->recordSearch(completedState.searchedTypes,
                                  completedState.searchedRange,
                                  completedState.searchedHarmonics,
                                  completedState.searchedSkip);
            qDebug() << "[ON COMPLETED] Manifest recorded:"
                     << completedState.searchedTypes.size() << "types,"
                     << file(0)->events().size() << "events";
        }
    }
    
    // Final restore attempt - if anchor can't be found now, it won't be found
    if (_anchor.isValid() || _anchor.hasSelEvent) {
        restoreScrollPos();

        int col = _evm->sortColumn();
        auto order = _evm->sortOrder();

        // The remembered selection's event no longer exists (e.g. a location
        // change shifted every event time): drop it for good, quietly.
        if (_anchor.hasSelEvent) {
            bool selMatches = false;
            int selRow = _evm->rowForData(_anchor.selEvent, selMatches, col,
                                          order == Qt::DescendingOrder);
            if (!selMatches || selRow < 0) {
                qDebug() << "[ON COMPLETED] Selected event not found, dropping selection";
                _anchor.selEvent = A::HarmonicEvent();
                _anchor.hasSelEvent = false;
            }
        }

        // Try one more time to see if we can find the scroll-anchor event
        if (_anchor.isValid()) {
            bool matches = false;
            int targetRow = _evm->rowForData(_anchor.event, matches, col, order == Qt::DescendingOrder);

            if (!matches || targetRow < 0) {
                // Event not found after completion - clear the scroll anchor
                // (a still-valid selection was vetted just above and is kept)
                qDebug() << "[ON COMPLETED] Anchor event not found, clearing anchor";
                _anchor.clearScroll();
            }
        }
    }
    
    // Recompute finished — the manifest now covers the searched scope, so
    // clear the Refresh button's stale highlight.
    updateRefreshButtonState();

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
    if (!_evm) return;

    for (const auto& item : _evm->match(_evm->index(0, 0),
                                        Qt::DisplayRole,
                                        val,
                                        1,
                                        Qt::MatchExactly))
    {
        auto proxyItem = _filterProxy->mapFromSource(item);
        if (!proxyItem.isValid()) break;  // Filtered out
        ttv()->scrollTo(proxyItem);
        ttv()->setExpanded(proxyItem, true);
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
        // Transiently empty during a rebuild (clearAllEvents → addEvents):
        // keep the remembered selection so it can be restored on repopulation;
        // onCompleted drops it if the event is truly gone.
        _anchor.clearScroll();
        return;
    }

    // Determine anchor type based on what triggered the save
    auto cur = ttv()->currentIndex();
    bool hasSelection = cur.isValid();

    // Record the selected row's identity regardless of viewport visibility so
    // restoreScrollPos() can re-select it after a model rebuild. Deliberately
    // left untouched when nothing is current: a restore may still be pending
    // from a cycle whose rows haven't been re-added yet.
    if (hasSelection) {
        _anchor.selEvent = _evm->rowData(_filterProxy->mapToSource(cur));
        _anchor.hasSelEvent = true;
    }

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
        A::HarmonicEvent currentEvent = _evm->rowData(_filterProxy->mapToSource(cur));
        
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
                _anchor.event = _evm->rowData(_filterProxy->mapToSource(bottom));
                _anchor.type = AnchorType::Bottom;
                _anchor.sortColumn = _evm->sortColumn();
                _anchor.sortOrder = _evm->sortOrder();
                _anchor.visibleRowOffset = -1;
            }
        } else {
            // Scrolled down or other case - use Top anchor
            QModelIndex top = ttv()->indexAt(ttv()->rect().topLeft());
            if (top.isValid()) {
                _anchor.event = _evm->rowData(_filterProxy->mapToSource(top));
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

    if (!_anchor.isValid() && !_anchor.hasSelEvent) return;

    if (_inRestoreScrollPos) return;

    A::modalize<bool> irsp(_inRestoreScrollPos);
    int               col   = _evm->sortColumn();
    auto              order = _evm->sortOrder();

    // Check if sort order changed - if so, we need to find the item again
    bool sortChanged = (col != _anchor.sortColumn || order != _anchor.sortOrder);

    // Scroll-anchor restore. A miss here (event not found yet during
    // progressive updates, or filtered out) is normal — just leave the scroll
    // alone and still attempt the selection restore below; after final
    // completion, onCompleted will clear invalid anchors.
    if (_anchor.isValid()) do {
        bool matches = false;
        int targetRow = _evm->rowForData(_anchor.event, matches, col, order == Qt::DescendingOrder);
        if (!matches || targetRow < 0) break;

        // Found the event - restore based on anchor type
        QModelIndex sourceIndex = _evm->index(targetRow, 0);
        QModelIndex targetIndex = _filterProxy->mapFromSource(sourceIndex);
        if (!targetIndex.isValid()) break;  // Filtered out — nothing to restore

        switch (_anchor.type) {
        case AnchorType::Selection:
            // Restore selected row at its previous visual offset
            {
                QSignalBlocker blocker(ttv()->selectionModel());
                ttv()->setCurrentIndex(targetIndex);
            }

            // Scroll to maintain the same visual offset from top
            if (_anchor.visibleRowOffset >= 0) {
                int proxyRow = targetIndex.row();
                int scrollToProxyRow = proxyRow - _anchor.visibleRowOffset;
                if (scrollToProxyRow >= 0) {
                    QModelIndex scrollToIndex = _filterProxy->index(scrollToProxyRow, 0);
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
    } while (false);

    // Selection restore, independent of the scroll anchor: re-select the
    // remembered row even when it sits outside the viewport, WITHOUT scrolling
    // to it (the scroll anchor above owns the viewport). Skipped when a valid
    // current index exists — Qt preserved it through a layoutChanged sort, the
    // Selection branch above just restored it, or the user has since picked
    // another row (never override). Blocked selection-model signals keep this
    // free of side effects (no currently → clickedCell chart switch).
    if (_anchor.hasSelEvent && !ttv()->currentIndex().isValid()) {
        bool selMatches = false;
        int  selRow     = _evm->rowForData(_anchor.selEvent, selMatches, col,
                                           order == Qt::DescendingOrder);
        if (selMatches && selRow >= 0) {
            QModelIndex selIndex =
                _filterProxy->mapFromSource(_evm->index(selRow, 0));
            if (selIndex.isValid()) {
                int scrollPos = ttv()->verticalScrollBar()->value();
                {
                    QSignalBlocker blocker(ttv()->selectionModel());
                    ttv()->setCurrentIndex(selIndex);
                }
                // Defensive: setCurrentIndex must not move the viewport.
                ttv()->verticalScrollBar()->setValue(scrollPos);
                // Blocked signals suppress the repaint too.
                ttv()->viewport()->update();
            }
        }
    }
}

static QString paranAngleAbbrev(const QString& desc) {
    if (!desc.isEmpty()) {
        if (desc[0] == QChar(402))  return QStringLiteral("Asc");
        if (desc[0] == QChar(8249)) return QStringLiteral("Ds");
        if (desc == QLatin1String("M")) return QStringLiteral("Mc");
        if (desc[0] == QChar(8225)) return QStringLiteral("Ic");
    }
    return QString();
}

static QString buildParanChartName(const A::HarmonicEvent& ev, bool biwheel) {
    QStringList parts;
    for (const auto& loc : ev.locations()) {
        // For midpoint bodies keep the full "A/B" name; for solo planets use
        // the standard 3-char abbreviation (removes trailing spaces first).
        QString pname = loc.planet.isMidpt()
                            ? loc.planet.name().remove(QLatin1Char(' '))
                            : loc.planet.name().remove(QLatin1Char(' ')).left(3);
        if (biwheel && loc.planet.fileId() == 0) pname += QStringLiteral("-r");
        QString angle = paranAngleAbbrev(loc.desc);
        parts << pname + QLatin1Char(' ') + angle;
    }
    return QStringLiteral("Paran ") + parts.join(QStringLiteral(" + "));
}

void
Transits::clickedCell(QModelIndex inx)
{
    if (!inx.isValid()) return;
    if (!_evm) return;

    // Map view (proxy) index to source model for _evm access
    QModelIndex srcInx = _filterProxy->mapToSource(inx);
    if (!srcInx.isValid()) return;
    if (srcInx.row() < 0 || srcInx.row() >= _evm->rowCount()) return;
    
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
    // Alt widens the purview: instead of arming the event's H_h override
    // aspect set (which draws *only* that one focal aspect), leave the
    // override at -1 so calculateAspects()/calculateSynastryAspects() take
    // the cluster-expansion path and draw all the tight aspects/interaspects
    // around the focal bodies.  (Matches the post-click Alt+zoom behavior,
    // where the override has already been restored to -1.)
    bool alt  = (QApplication::keyboardModifiers() & Qt::AltModifier);
    if (lbtn && ctrl) lbtn = false, mbtn = true;

    auto* aw = MainWindow::theAstroWidget();
    if (!aw) return;
    A::modalize<A::AspectSetId> aset(aw->overrideAspectSet(), -1);
    // Default to "exactly this" drawing; enabled below for the event types
    // that want the bigger picture (every focal type except the TA/TNA
    // aspect-pattern events, which draw precisely the clicked pattern).
    A::modalize<bool> fexp(aw->focalExpand(), false);
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
                // Set override BEFORE setHarmonicQuietly — it triggers
                // change(Harmonic) which redraws the chart synchronously.
                if (!alt) aset = A::topAspectSet().id + 1;
                MainWindow::theAstroWidget()->setHarmonicQuietly(h);
                clickHarmonic = h;
            } else {
                // Non-midpoint focal: show at H1 with override aspect set
                // (unless Alt asks for the expanded purview — see above).
                if (!alt) aset = A::topAspectSet().id + h;
                MainWindow::theAstroWidget()->setHarmonicQuietly(1);
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

    // Re-map to source after potential parent promotion
    srcInx = _filterProxy->mapToSource(inx);
    if (!srcInx.isValid()) return;

    // Re-validate row after potential model changes from signals above
    if (srcInx.row() < 0 || srcInx.row() >= _evm->rowCount()) return;

    auto    dt = _evm->rowDate(srcInx.row());
    if (!dt.isValid()) return;
    auto    ev = _evm->rowData(srcInx.row());
    auto    et = ev.eventType();
    if (et == A::etcPrimaryDirections) {
        // A primary direction's computed date has no real astronomical event
        // at it (see findPrimaryDirections()) — it's an age converted from an
        // arc via a timing key, not a real transit/return moment — so this
        // never navigates the tab, opens a biwheel, or touches file()'s GMT
        // *or Type* the way other event types below do (an earlier version
        // of this used setType(TypeDirection) purely to piggyback on the
        // Type-change redraw, but FileType turned out to be load-bearing in
        // enough other places — classifyDirChart(), Transits::filesUpdated()'s
        // isTransitLike check, etc. — that borrowing it here broke unrelated
        // features, including PD generation itself going quiet). Clicking the
        // T/P/S or T/P/N cell instead previews the legacy Directions table
        // pruned to rows whose own directed date falls near this one, purely
        // via the dedicated direction-focus fields (see
        // AstroFile::setDirectionFocusRange(), whose setter emits the
        // DirectionFocus member bit to drive the repaint). Any other column
        // click on a PD row just clears that preview, if active.
        const bool pdFocalClick =
            srcInx.column() >= EventsTableModel::transitBodyCol;
        if (!pdFocalClick) {
            if (file()->hasDirectionFocus()) {
                file()->setOriginEventType(A::etcUnknownEvent);
                file()->setDirectionFocusRange(A::ADateTimeRange());
                file()->setDirectionFocusDate(QDateTime());
                file()->setDirectionFocusLabel(QString());
            }
            return;
        }
        // Use the *live* Primary Direction orb setting, the same one the
        // Events table itself uses -- not ev.range(), which was baked in
        // whenever findPrimaryDirections() last ran and goes stale the
        // moment the user changes Events/pdOrbDegrees without triggering a
        // recompute. Mirrors the exact formula findPrimaryDirections() uses
        // (astro-calc.cpp, AspectFinder::findPrimaryDirections()) so the
        // preview's window always matches "the same as the table".
        const double pdOrb = A::EventOptions::current().pdOrbDegrees;
        const qint64 orbSecs = pdOrb > 0.0
            ? qint64(pdOrb * A::pdDaysPerDegree(A::pdTimingKey) * 86400.0)
            : qint64(3 * 86400); // orb disabled: fall back to a small window
        A::ADateTimeRange focusRange = { dt.addSecs(-orbSecs), dt.addSecs(orbSecs) };
        // Label for the clicked PD event itself, e.g. "(Con) Neptune -> Uranus"
        // or, when the significator carries a ray, "(Dir) Sun -> Uranus-Trine,
        // Dexter" -- built from the same model columns rowDesc() reads,
        // but reordered/parenthesized to read as a direction rather than a
        // generic aspect row, so the focused Directions table can show the
        // PD event itself as an anchor alongside the rows clustered near it.
        const QString promissor =
            _evm->index(srcInx.row(), EventsTableModel::transitBodyCol)
                .data(EventsTableModel::SummaryRole)
                .toString();
        const QString connector =
            _evm->index(srcInx.row(), EventsTableModel::harmonicCol)
                .data(EventsTableModel::SummaryRole)
                .toString(); // "Dir" or "Con"
        const QString significator =
            _evm->index(srcInx.row(), EventsTableModel::natalTransitBodyCol)
                .data(EventsTableModel::SummaryRole)
                .toString();
        const QString focusLabel = QString("(%1) %2 → %3")
                                        .arg(connector, promissor, significator);
        file()->suspendUpdate();
        file()->setParanGroupPlanets({});
        file()->setParanOccurrences({});
        file()->setAspectRange(A::ADateTimeRange());
        file()->setAspectExact(QDateTime());
        file()->setOriginEventType(A::etcPrimaryDirections);
        file()->setDirectionFocusLabel(focusLabel);
        file()->setDirectionFocusDate(dt);
        file()->setDirectionFocusRange(focusRange);
        file()->resumeUpdate();
        return;
    }
    // TA/TNA are harmonic aspect-PATTERN events: draw precisely the clicked
    // pattern ("exactly this").  Every other focal event (T=N, returns,
    // parans, …) wants the clicked aspect plus the other aspects involving its
    // bodies — the bigger picture — so enable focal expansion for those.
    aw->focalExpand() = (et != A::etcTransitAspectPattern
                         && et != A::etcTransitNatalAspectPattern);
    // Focal-column click (transitBodyCol or natalTransitBodyCol) enables
    // paran-chart pruning mode; other columns show the full paran table.
    const bool paranFocalClick =
        (et == A::etcParanatellonta || et == A::etcParanatellontaToNatal)
        && (srcInx.column() >= EventsTableModel::transitBodyCol);
    QString desc;
    if (et == A::etcParanatellonta || et == A::etcParanatellontaToNatal) {
        desc = buildParanChartName(ev, !transitsOnly());
    } else if (et == A::etcHeliacalEvents || et == A::etcHeliacalStars
               || et == A::etcHeliacalLunar) {
        // Compact "Body-MF" chart name (body + short phase tag). A decomposed
        // row names its own clicked phase rather than the apparition anchor.
        const int occ = _evm->rowOcc(srcInx.row());
        QString tag;
        if (occ >= 0 && occ < ev.occurrenceLabels().size())
            tag = ev.occurrenceLabels().at(occ);
        for (const auto& loc : ev.locations()) {
            desc = loc.planet.name() + "-" + (tag.isEmpty() ? loc.desc : tag);
            break;
        }
    } else if (focal.empty()) {
        desc = _evm->rowDesc(srcInx.row());
    } else {
        // SummaryRole, not the default DisplayRole: DisplayRole is blank for
        // named-aspect rows (the Events table shows the icon there instead;
        // see EventsTableModel::data(), harmonicCol) — SummaryRole carries
        // the abbreviation ("sqr") or "H4" text regardless of display mode.
        QString h = inx.siblingAtColumn(EventsTableModel::harmonicCol)
                        .data(EventsTableModel::SummaryRole)
                        .toString();
        QString bodies = describePlanetsForEvent(focal, et);
        if (focal.size() == 2 && !_evm->aspects().name.startsWith("Harmonic")) {
            // Two-body named-aspect hits read as "Chi-r ssq Chi" — the
            // abbreviation as a connector, matching rowDesc()'s format —
            // rather than prefixed. describePlanetsForEvent() joins exactly
            // two bodies with a single "=", so swap that in place.
            desc = bodies.replace('=', ' ' + h + ' ');
        } else {
            desc = h + " " + bodies;
        }
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
        // In transitsOnly mode there is only one file (fileId=0), but TAP
        // events store transit-body planets with fileId=1.  Remap so that
        // calculateAspects() doesn't reject them via the fileId bounds check.
        A::PlanetSet focalFixed;
        for (auto cpid : focal) {
            if (cpid.fileId() == 1) cpid.setFileId(0);
            focalFixed.emplace(cpid);
        }
        focal = std::move(focalFixed);
        // Batch all member changes into a single filesUpdated emitted on
        // resume.  Otherwise setGMT() (a tracked member) triggers the chart
        // redraw before setParanGroupPlanets() below runs, so the wheel draws
        // the previous click's paran group — an off-by-one stale figure.
        file()->suspendUpdate();
        file()->setFocalPlanets(focal);
        file()->setName(desc);
        file()->setGMT(dt);
        // Set file type based on event type.
        // Always reset to TypeOther when not entering a specific typed mode so
        // that a previous TypeParan doesn't persist and break filesUpdated().
        // Clear originEventType up front so a prior paran-focal selection
        // doesn't keep the chart wheel's cusps/axes hidden on the next click.
        file()->setOriginEventType(A::etcUnknownEvent);
        // Reset navigable state so a prior paran/aspect selection on this reused
        // chart doesn't leak into the new event (e.g. stale paran occurrences
        // making navMovingFile() treat a ranged event as a discrete paran).
        file()->setParanGroupPlanets({});
        file()->setParanOccurrences({});
        file()->setAspectRange(A::ADateTimeRange());
        file()->setAspectExact(QDateTime());
        // A prior PD-focal click shouldn't leak into whatever this click is
        // about to select either.
        file()->setDirectionFocusRange(A::ADateTimeRange());
        file()->setDirectionFocusDate(QDateTime());
        file()->setDirectionFocusLabel(QString());
        // A heliacal APPARITION carries navigable occurrences (first-appearance,
        // anchor, last-appearance); the Moon's discrete crescent events do not.
        const bool isHeliacalApparition =
            (et == A::etcHeliacalEvents || et == A::etcHeliacalStars)
            && !ev.occurrences().isEmpty();
        if (et == A::etcSolarReturn || et == A::etcLunarReturn) {
            file()->setType(TypeReturn);
        } else if (et == A::etcParanatellonta || et == A::etcParanatellontaToNatal) {
            if (paranFocalClick) {
                file()->setType(TypeParan);
                file()->setOriginEventType(et);
                QVector<AstroFile::ParanGroupEntry> group;
                for (const auto& loc : ev.locations()) {
                    qDebug() << "paranGroup fileId=" << loc.planet.fileId()
                             << "planet=" << loc.planet.name();
                    group.append(loc.planet);
                }
                file()->setParanGroupPlanets(group);
                file()->setParanOccurrences(ev.occurrences());
            } else {
                file()->setType(TypeOther);
            }
        } else if (isHeliacalApparition) {
            // Reuse the discrete occurrence transport (navMovingFile accepts
            // TypeApparition too) to step first-appearance → anchor → last.
            file()->setType(TypeApparition);
            file()->setOriginEventType(et);
            file()->setParanOccurrences(ev.occurrences());
            file()->setParanOccurrenceLabels(ev.occurrenceLabels());
        } else {
            file()->setType(TypeOther);
        }
        // Aspect Range Navigator: record the event's in-orb range + draw-context
        // so the navigator can offer Play / reproduce the aspect rendering.
        // Parans and heliacal apparitions use discrete occurrence stepping, not
        // the continuous range, so they are excluded here.
        if (et != A::etcParanatellonta && et != A::etcParanatellontaToNatal
            && !isHeliacalApparition
            && ev.range().first.isValid() && ev.range().second.isValid()
            && ev.range().first < ev.range().second) {
            file()->setAspectRange(ev.range());
            file()->setAspectExact(dt);
        }
        file()->setDrawFocalExpand(aw->focalExpand());
        file()->setDrawOverrideAspectSet(aw->overrideAspectSet());
        // Flush the batched changes (group/type now in place) as one update.
        file()->resumeUpdate();
        // Trigger a full chart rebuild so Chart picks up the focal planets
        // and overrideAspectSet. FilesBar::openFile now guards stopThreads()
        // and date-range reset behind !sameFile, so emitting here is safe.
        emit updateFirst(file());
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
        // Clear any manual timezone lock from the previous event so the new
        // event uses the tab's default transit location, not a one-off override.
        taf->setTimezoneLocked(false);
        if (clickHarmonic > 0)
            taf->setHarmonic(clickHarmonic);
        taf->setFocalPlanets(focal);
        taf->setName(desc);
        taf->setGMT(dt);
        // Reset to the tab's default transit location so a manually-relocated
        // previous event doesn't bleed its location into the next selection.
        if (file(0)->hasTransitLocation()) {
            taf->setLocation(file(0)->getTransitLocation());
            taf->setLocationName(file(0)->getTransitLocationName());
            taf->setTimezone((double) file(0)->getTransitTimezone());
        }
        // Set file type and base chart based on event type
        // Base chart stores the natal chart relationship for all event types
        // Clear originEventType up front so a prior paran-focal selection
        // doesn't keep the chart wheel's cusps/axes hidden on the next click.
        taf->setOriginEventType(A::etcUnknownEvent);
        // Reset navigable state so a prior paran/aspect selection on this reused
        // transit chart doesn't leak into the new event.
        taf->setParanGroupPlanets({});
        taf->setParanOccurrences({});
        taf->setAspectRange(A::ADateTimeRange());
        taf->setAspectExact(QDateTime());
        taf->setDirectionFocusRange(A::ADateTimeRange());
        taf->setDirectionFocusDate(QDateTime());
        taf->setDirectionFocusLabel(QString());
        // The PD-focal click (see the etcPrimaryDirections branch above)
        // always sets its focus state on file() -- Chart #1 -- even though
        // this branch otherwise operates on taf. Clear it here too, or a
        // lingering PD focus on file() keeps showing its filtered
        // Directions table after clicking away to an unrelated event on
        // this (taf) side.
        if (file()) {
            file()->setOriginEventType(A::etcUnknownEvent);
            file()->setDirectionFocusRange(A::ADateTimeRange());
            file()->setDirectionFocusDate(QDateTime());
            file()->setDirectionFocusLabel(QString());
        }
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
            if (file()->getType() == TypeComposite
                && file()->hasCompositeSources()) {
                // Progressed composite: render the midpoint of the two
                // progressed components — the same positions the event
                // search used. Sources first: setBaseChart recalculates
                // immediately, and composite+baseChart ⇒ progressed.
                taf->setCompositeSources(file()->compositeFile(0),
                                         file()->compositeFile(1));
            } else {
                taf->setType(TypeDerivedProg);
            }
            taf->setBaseChart(file()->getGMT());
        } else if (et == A::etcParanatellonta
                   || et == A::etcParanatellontaToNatal)
        {
            if (paranFocalClick) {
                taf->setType(TypeParan);
                taf->setOriginEventType(et);
                taf->setBaseChart(file()->getGMT());
                QVector<AstroFile::ParanGroupEntry> group;
                for (const auto& loc : ev.locations()) {
                    qDebug() << "paranGroup fileId=" << loc.planet.fileId()
                             << "planet=" << loc.planet.name();
                    group.append(loc.planet);
                }
                taf->setParanGroupPlanets(group);
                taf->setParanOccurrences(ev.occurrences());
            } else {
                taf->setType(TypeOther);
                taf->setBaseChart(file()->getGMT());
            }
        } else if ((et == A::etcHeliacalEvents || et == A::etcHeliacalStars)
                   && !ev.occurrences().isEmpty()) {
            // Heliacal apparition: discrete occurrence stepping (see the
            // transitsOnly branch above).
            taf->setType(TypeApparition);
            taf->setOriginEventType(et);
            taf->setBaseChart(file()->getGMT());
            taf->setParanOccurrences(ev.occurrences());
            taf->setParanOccurrenceLabels(ev.occurrenceLabels());
        } else {
            // For transit events (T=T, T=N, patterns, ingresses, etc.)
            // Set base chart to track natal relationship, but use TypeOther
            taf->setType(TypeOther);
            taf->setBaseChart(file()->getGMT());
        }
        // Aspect Range Navigator: record the event's in-orb range so the
        // navigator can offer continuous Play across it (non-paran events).
        // Heliacal apparitions with occurrences use discrete stepping instead.
        const bool tafHeliacalApparition =
            (et == A::etcHeliacalEvents || et == A::etcHeliacalStars)
            && !ev.occurrences().isEmpty();
        if (et != A::etcParanatellonta && et != A::etcParanatellontaToNatal
            && !tafHeliacalApparition
            && ev.range().first.isValid() && ev.range().second.isValid()
            && ev.range().first < ev.range().second) {
            taf->setAspectRange(ev.range());
            taf->setAspectExact(dt);
        }
        // Capture the click's draw-context so navigator steps/animation can
        // reproduce the same aspect rendering (esp. TA/TNA patterns, which want
        // focalExpand=false + their override aspect set).
        taf->setDrawFocalExpand(aw->focalExpand());
        taf->setDrawOverrideAspectSet(aw->overrideAspectSet());
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

    //bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier);
    bool shift = (QApplication::keyboardModifiers() & Qt::ShiftModifier);

    auto par = inx.parent();
    if (par.isValid()) inx = par;

    // Map view (proxy) index to source model
    QModelIndex srcInx = _filterProxy->mapToSource(inx);
    if (!srcInx.isValid()) return;
    int row = srcInx.row();
    if (row < 0 || row >= _evm->rowCount()) return;
    auto              dt   = _evm->rowDate(row);
    if (!dt.isValid()) return;
    auto              ev   = _evm->rowData(row);
    auto              et   = ev.eventType();
    if (et == A::etcPrimaryDirections) {
        // See the matching guard in clickedCell() — deliberately a no-op.
        return;
    }
    const QString desc = (et == A::etcParanatellonta || et == A::etcParanatellontaToNatal)
                         ? buildParanChartName(ev, !transitsOnly())
                         : _evm->rowDesc(row);
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
        if (!transitsOnly() && file()
            && file()->getType() == TypeComposite
            && file()->hasCompositeSources()) {
            // Progressed composite (see the transit-chart path above)
            af->setCompositeSources(file()->compositeFile(0),
                                    file()->compositeFile(1));
            af->setBaseChart(file()->getGMT());
        } else {
            af->setType(TypeDerivedProg);
            // Set the natal chart as the base for progressions
            if (!transitsOnly() && file()) {
                af->setBaseChart(file()->getGMT());
            } else {
                af->clearBaseChart();
            }
        }
    } else if (et == A::etcParanatellonta || et == A::etcParanatellontaToNatal) {
        af->setType(TypeParan);
        // Store the paran group so the speculum can filter to these bodies
        QVector<AstroFile::ParanGroupEntry> group;
        for (const auto& loc : ev.locations())
            group.append(loc.planet);
        af->setParanGroupPlanets(group);
        af->setParanOccurrences(ev.occurrences());
    }

    // Aspect Range Navigator: record the event's in-orb range (non-paran ranged
    // events) so the navigator can offer continuous Play across it.
    if (et != A::etcParanatellonta && et != A::etcParanatellontaToNatal
        && ev.range().first.isValid() && ev.range().second.isValid()
        && ev.range().first < ev.range().second) {
        af->setAspectRange(ev.range());
        af->setAspectExact(dt);
    }
    af->setDrawFocalExpand(aw->focalExpand());
    af->setDrawOverrideAspectSet(aw->overrideAspectSet());

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
        
        // A decomposed apparition row reports its own phase occurrence (date +
        // label), not the shared culmination anchor.
        const int occ = _evs[row].occ;
        const QString occLabel =
            (occ >= 0 && occ < _evs[row]->occurrenceLabels().size())
                ? _evs[row]->occurrenceLabels().at(occ)
                : QString();

        // Date/time with range if available
        auto dt = rowSortDate(_evs[row].second, occ)
                      .toTimeZone(QTimeZone(_tzOffset * 3600));
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
        typedef std::pair<const A::Loc*, const A::Loc*> locPair;

        // Named aspect sets (Basic, Reasonable, ...) show their own aspect
        // name (two-body angle match, or a representative aspect for the
        // shared harmonic on pattern events); harmonic-number notation
        // (H4, H12, ...) is reserved for the Harmonic/Dynamic set.
        QString harmonicStr;
        if (auto* d = resolveNamedAspectType(asp)) {
            harmonicStr = d->name;
        } else {
            harmonicStr = QString("H%1").arg(asp.harmonic());

            // Add aspect ratio if available and setting is enabled
            if (A::EventOptions::current().showHarmonicDividend) {
                locPair pp;
                if (getPlanetPair(asp.locations(), pp)) {
                    auto a = A::calculateAspect(aspects(), pp.first, pp.second);
                    if (a.d && a.d->_harmonic > 0) {
                        harmonicStr += " (" + a.d->name + ")";
                    }
                }
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
            // Use locations (PlanetLoc) - faster planet goes in Transit column.
            // A decomposed apparition row reports its own occurrence's position.
            const auto& occLons = _evs[row]->occurrenceLons();
            const auto& occSpds = _evs[row]->occurrenceSpeeds();
            const bool haveOccLon = (occ >= 0 && occ < occLons.size());
            auto [begin, end] = getTColIters(asp.locations());
            for (auto it = begin; it != end; ++it) {
                if (haveOccLon && it->planet.planetId() < A::Stars_Start) {
                    A::PlanetLoc s = *it;
                    s._rasiLoc = occLons[occ];
                    if (occ < occSpds.size()) s.speed = occSpds[occ];
                    transitBodies << this->planetToText(s, occLabel, asp.eventType());
                } else {
                    transitBodies << this->planetToText(*it, occLabel, asp.eventType());
                }
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
                    natalTransitBodies << this->planetToText(*it, {}, asp.eventType());
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
EventsTableModel::planetToText(const A::PlanetLoc& ploc,
                               const QString& descOverride,
                               unsigned       eventType) const
{
    // Use 3-letter abbreviation
    QString name = ploc.planet.name().remove(' ').left(3);

    // Add mode suffix
    QString suffix = modeToSuffix(ploc.mode());
    if (!suffix.isEmpty()) {
        name += "-" + suffix;
    }

    const QString& desc = descOverride.isEmpty() ? ploc.desc : descOverride;

    // Primary directions: rasiLoc() holds right ascension, not ecliptic
    // longitude (see EventsTableModel::glyph()'s matching branch), shown
    // only when the user has opted in. Significator's desc is the ray's
    // Almagest glyph, optionally followed by a "D"/"S" dexter/sinister
    // marker -> readable name here (plain text, not glyph font);
    // promissor's desc ("Dir"/"Con") is shown via the Asp column instead,
    // so suppressed here too.
    if (eventType == A::etcPrimaryDirections) {
        QString rayText = pdRayGlyphToText(desc);
        if (!rayText.isEmpty()) {
            name += "-" + rayText;
            QString dexSin = pdDexSinToText(desc);
            if (!dexSin.isEmpty()) name += "-" + dexSin;
        }
        if (A::EventOptions::current().showPDRightAscension)
            name += " " + A::raToString(ploc.rasiLoc(), A::HighPrecision);
        return name;
    }

    // Add descriptor (SD, SR, etc.). A decomposed apparition row overrides the
    // anchor tag with its own phase (Acr/Cul/Cs/MF/EL).
    if (!desc.isEmpty()) {
        name += "-" + desc;
    }

    // Add position
    name += " " + A::zodiacPosition(ploc.rasiLoc(), _zodiac, A::HighPrecision, ploc.speed < 0);

    // Add retrograde indicator
    if (ploc.speed < 0 && !desc.startsWith("S")) {
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
        // Map proxy indices to source model indices for mimeData
        QModelIndexList srcList;
        srcList.reserve(qmil.size());
        for (const auto& idx : qmil)
            srcList.append(_filterProxy->mapToSource(idx));
        QMimeData* md = sim->mimeData(srcList);
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
    return _evm;
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
        
        // Mark that events need recalculation (hard invalidation)
        file(0)->markEventsForRecalc();
    }
    // reconcile() recomputes now if Auto is on, else lights the Refresh button.
    reconcile();
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
    // While scrubbing (wheel drag) or animating, skip the event finder. Events
    // are independent of the chart's display moment, so a moment-scrub must not
    // recompute them. `animating` stays true through the post-playback catch-up
    // (when scrubbing is already false) so the catch-up doesn't re-run the finder.
    if (A::isScrubbing() || A::isAnimating()) return;
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
            _previousFile->setTransitSkipByDuration(_tabSkipByDuration);
            _previousFile->setTransitPattern(_input->currentText());
        }
        
        // Load event options from new file(0) — only on actual tab switch.
        // On same-file updates (e.g. file(1) location change) the per-tab
        // state is already correct and quietClear() would destroy the
        // current events list without repopulating it.
        if (file(0) && fileChanged) {
            qDebug() << "[FILES UPDATED] Loading event options for file" << file(0)->getName();
            _tabEventOptions = file(0)->getTransitEventOptions();
            
            // If file has no saved options (empty set), initialize from global defaults
            if (_tabEventOptions.empty()) {
                qDebug() << "  No saved options, using global defaults";
                _tabEventOptions = A::EventOptions::globalDefaults();
                file(0)->setTransitEventOptions(_tabEventOptions);
            }

            // Load per-tab skip-by-duration level for the new file
            _tabSkipByDuration = file(0)->getTransitSkipByDuration();
            if (_tabSkipByDuration != A::EventOptions::SkipNone)
                _lastSkipLevel = _tabSkipByDuration;

            // Load per-tab auto-reconcile preference for the new file
            _autoReconcile = file(0)->getTransitAutoReconcile();
            if (_actAuto) {
                QSignalBlocker b(_actAuto);
                _actAuto->setChecked(_autoReconcile);
            }

            // Restore per-event-type harmonic restrictions
            if (_evm) {
                // Clear stale event pointers before updating the filter proxy.
                // The old file's HarmonicEvents may have been destroyed, and
                // setEnabledEventTypes triggers filterAcceptsRow which would
                // dereference dangling pointers in the source model.
                // Use quietClear to avoid aboutToChange → saveScrollPos
                // accessing the same dangling data.
                _evm->quietClear();

                _evm->setHarmonicRestrictions(
                    file(0)->getTransitHarmonicRestrictions());
            }
            
            // Update toolbar to reflect the loaded event options
            updateToolbarFromEventOptions();
            
            // Sync the proxy filter with the loaded event options
            _filterProxy->setEnabledEventTypes(_tabEventOptions);
            
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
            _filterProxy->setPatternActive(!_lastUsedPattern.isEmpty());
            _filterProxy->setEnabledHarmonics(
                _evm ? A::activeHarmonicSet(_evm->aspects()) : A::dynAspState());
            _filterProxy->setSkipByDuration(_tabSkipByDuration);  // tab-specific

            _previousFile = file(0);
        } else if (file(0) && !_previousFile) {
            // First time seeing any file — initialize _previousFile
            _previousFile = file(0);
        }
    }  // ~guard restores _inhibitUpdate

#if 0
    // XXX need a better division of in-process update and final update
    if (QApplication::mouseButtons() & Qt::LeftButton) return;
#endif

    // Restore date range from file(0) when switching tabs (BEFORE any updates)
    if (fileChanged) {
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
    }

    while (m.size() < filesCount()) m.append(AstroFile::Member());

    bool any = false;
    bool needsRecalc = false;
    int  f   = 0;
    for (auto ml : m) {
        FileType type = file(f)->getType();
        // TypeParan is used on transits-only single-file tabs; treat it like
        // TypeOther so that GMT/Location changes and tab-switch diffs correctly
        // trigger describePlanet().  Without this, type >= TypeSearch causes the
        // entire block to be skipped, leaving the model empty after tab switch.
        // TypeComposite (also > TypeSearch) acts as a natal reference and its
        // GMT/Location changes move the houses reference and paran epoch.
        bool isTransitLike = (type < TypeSearch || type == TypeParan
                              || type == TypeComposite);
        if (isTransitLike) {
            if (f == 0 ? (type <= TypeReturn || type == TypeParan
                          || type == TypeComposite)
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
    // A tab switch quietClear()s the model above, so it must always run the
    // repopulate/recompute path even when no member-data flag changed —
    // otherwise the switched-to tab shows an empty list.
    if (any || fileChanged) {
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
    bool aspectSetOnly = false;
    for (int fi = 0; fi < filesCount(); ++fi) {
        auto ml = m[fi];
        any |= (ml & (AstroFile::Zodiac | AstroFile::AspectSet
                      | AstroFile::AspectMode | AstroFile::HouseSystem));
        needsRecalc |= (ml & (AstroFile::Zodiac | AstroFile::AspectSet
                              | AstroFile::AspectMode | AstroFile::HouseSystem));
        // Check if it's purely an AspectSet content change (dynAspState toggle)
        // with no Zodiac/HouseSystem/AspectMode change
        if ((ml & AstroFile::AspectSet)
            && !(ml & (AstroFile::Zodiac | AstroFile::HouseSystem
                       | AstroFile::AspectMode)))
            aspectSetOnly = true;
    }

    qDebug() << "[TRANSITS viewSettingsUpdated] any=" << any << "needsRecalc=" << needsRecalc;
    if (any) {
        auto* evm = ensureEventsModel();
        if (!evm) return;

        // Detect dynAspState-only change: the aspect set ID didn't change,
        // only individual harmonics were toggled.  Try filter-only update.
        if (aspectSetOnly && !_fileJustSwitched && _filterProxy) {
            auto newHs = A::activeHarmonicSet(evm->aspects());
            auto oldHs = _filterProxy->enabledHarmonics();
            bool sameSetId = (evm->aspects().id
                              == file()->getAspectSetId());

            if (sameSetId && newHs != oldHs) {
                // Check if any newly-enabled harmonics lack events
                bool needNewData = false;
                for (unsigned h : newHs) {
                    if (oldHs.count(h) == 0
                        && !_filterProxy->sourceHasHarmonic(h)) {
                        needNewData = true;
                        break;
                    }
                }

                if (!needNewData) {
                    qDebug() << "[VIEW SETTINGS] dynAspState filter-only update";
                    _filterProxy->setEnabledHarmonics(newHs);
                    return;
                }
                qDebug() << "[VIEW SETTINGS] dynAspState change needs recompute"
                         << "— new harmonics have no cached events";
            }
        }

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
    _filterProxy->setEnabledEventTypes(_tabEventOptions);

    // Compute (or repopulate the model from cache) now that we're visible. On
    // session restore, files are set while the tab is hidden, so filesUpdated()
    // bails at its !isVisible() guard and never computes; nothing else
    // re-triggers it for a transits-only tab, leaving Refresh stuck amber with
    // an empty table. updateTransits() recomputes when stale and otherwise just
    // repopulates the view from cached events, so it's safe on every show (the
    // _inUpdateTransits guard prevents any re-entrant double-run).
    updateTransits();
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

    // Note: skipByDuration is now a per-tab setting controlled by the toolbar
    // duration button; the dialog combo only sets the default for newly opened
    // files (via the global singleton below).  It does NOT recompute the
    // current tab.

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
         || s.value("Events/includeAsteroids").toBool() != curr.includeAsteroids
         || s.value("Events/includeCentaurs").toBool() != curr.includeCentaurs
         || s.value("Events/includeOnlyInnerProgressionsToNatal").toBool()
                != curr.includeOnlyInnerProgressionsToNatal
         || s.value("Events/pdIncludeRays", true).toBool() != curr.pdIncludeRays
         || s.value("Events/pdAnglesAsPromissors", true).toBool()
                != curr.pdAnglesAsPromissors
         || A::EventOptions::PDDirectionScope(
                s.value("Events/pdDirectionScope",
                        unsigned(A::EventOptions::PDBothDirections)).toUInt())
                != curr.pdDirectionScope
         || s.value("Events/pdOrbDegrees", 0.5).toDouble() != curr.pdOrbDegrees);

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
    ed->addCheckBox("Events/showPDRightAscension",
                    tr("Show right ascension in Primary Directions rows"));
    ed->addCheckBox("Events/pdIncludeRays",
                    tr("Primary Directions: include aspect rays\n(sextile/square/trine/opposition, not just conjunction)"));
    ed->addCheckBox("Events/pdAnglesAsPromissors",
                    tr("Primary Directions: allow angles (Asc/Desc/MC/IC)\nas promissors, not just significators"));
    ed->addComboBox("Events/pdDirectionScope",
                    tr("Primary Directions: direct/converse"),
                    { { "Both", unsigned(A::EventOptions::PDBothDirections) },
                      { "Direct only", unsigned(A::EventOptions::PDDirectOnly) },
                      { "Converse only", unsigned(A::EventOptions::PDConverseOnly) } });
    ed->addDoubleSpinBox("Events/pdOrbDegrees",
                         tr("Primary Directions: orb of effect (RA degrees)"),
                         0.0, 5.0);
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

QToolButton*
Transits::buildEventGroupButton(QToolBar*                    tb,
                                const QString&               baseLabel,
                                const QString&               tooltip,
                                const QList<EventGroupSpec>& spec)
{
    auto* btn = new LeftToolButton(tb);
    btn->setText(baseLabel);
    btn->setCheckable(true);
    btn->setPopupMode(QToolButton::MenuButtonPopup);

    auto* menu = new QMenu(btn);

    EventGroupButton grp;
    grp.button      = btn;
    grp.baseLabel   = baseLabel;
    grp.baseTooltip = tooltip +
        " — click to toggle the whole group; use the dropdown to choose members";

    // Radio subgroups created lazily by id as members reference them. Uses
    // ExclusiveOptional so the pair may be all-off, with at most one on.
    QHash<int, QActionGroup*> radioById;
    for (const auto& s : spec) {
        if (s.et == A::etcUnknownEvent) { menu->addSeparator(); continue; }

        const QString label = s.overrideLabel.isEmpty() ? eventTypeBrief(s.et)
                                                         : s.overrideLabel;
        QAction* act = menu->addAction(label);
        act->setCheckable(true);
        act->setToolTip(eventTypeDesc(s.et));

        if (s.radioGroup >= 0) {
            auto it = radioById.find(s.radioGroup);
            QActionGroup* rg;
            if (it == radioById.end()) {
                rg = new QActionGroup(menu);
                rg->setExclusionPolicy(
                    QActionGroup::ExclusionPolicy::ExclusiveOptional);
                radioById.insert(s.radioGroup, rg);
                grp.radioGroups.append(rg);
            } else {
                rg = it.value();
            }
            rg->addAction(act);
        }

        grp.members.append({ s.et, s.radioGroup, act, s.defaultOn });
    }

    btn->setMenu(menu);

    // Register the group; handlers reference it by index (QList elements are
    // never removed after construction, so the index stays valid).
    const int gi = _eventGroups.size();
    _eventGroups.append(std::move(grp));

    // Per-member toggle: edits the selection, and — only while the master is
    // on — mirrors the selection into the live option set.
    for (const auto& m : _eventGroups[gi].members) {
        const A::EventType et    = m.et;
        const int          rgIdx = m.radioGroup;
        connect(m.action, &QAction::toggled, this,
                [this, gi, et, rgIdx](bool checked) {
            auto& g = _eventGroups[gi];
            if (checked) {
                // Radio: enforce exclusivity ourselves. We can't rely on the
                // QActionGroup — syncGroupFromOptions sets the restored item's
                // checkmark with signals blocked, so the group's current-action
                // tracking goes stale and it fails to uncheck the old sibling.
                if (rgIdx >= 0)
                    for (const auto& mm : g.members)
                        if (mm.radioGroup == rgIdx && mm.et != et) {
                            g.selection.erase(mm.et);
                            if (mm.action && mm.action->isChecked()) {
                                QSignalBlocker b(mm.action);
                                mm.action->setChecked(false);
                            }
                        }
                g.selection.insert(et);
            } else {
                g.selection.erase(et);
            }
            refreshGroupTooltip(g);

            if (!g.button->isChecked())
                return;  // master OFF: selection edited, nothing shown

            // Master ON: the live set tracks the selection exactly.
            for (const auto& mm : g.members) {
                if (g.selection.count(mm.et) > 0) _tabEventOptions.insert(mm.et);
                else                              _tabEventOptions.erase(mm.et);
            }
            // Emptied selection ⇒ nothing displayed ⇒ turn the master off.
            if (g.selection.empty()) {
                QSignalBlocker b(g.button);
                g.button->setChecked(false);
            }
            saveEventOptionsAndReconcile(checked);
        });
    }

    // Master button: pushes the selection to the display (on) or removes the
    // group from the display while retaining the selection (off).
    connect(btn, &QToolButton::clicked, this, [this, gi]() {
        auto&      g        = _eventGroups[gi];
        const bool newState = g.button->isChecked();  // post auto-toggle
        if (newState) {
            if (g.selection.empty())  // default seed: independent default-on members
                for (const auto& mm : g.members)
                    if (mm.radioGroup < 0 && mm.defaultOn)
                        g.selection.insert(mm.et);
            for (A::EventType et : g.selection) _tabEventOptions.insert(et);
        } else {
            for (const auto& mm : g.members) _tabEventOptions.erase(mm.et);
        }
        // Reflect the (possibly just-seeded) selection onto the menu checks.
        for (const auto& mm : g.members) {
            if (!mm.action) continue;
            QSignalBlocker b(mm.action);
            mm.action->setChecked(g.selection.count(mm.et) > 0);
        }
        refreshGroupTooltip(g);
        saveEventOptionsAndReconcile(newState);
    });

    refreshGroupTooltip(_eventGroups[gi]);
    tb->addWidget(btn);
    return btn;
}

bool
Transits::groupHasEnabledMember(const EventGroupButton& grp) const
{
    for (const auto& m : grp.members)
        if (_tabEventOptions.count(m.et) > 0) return true;
    return false;
}

void
Transits::refreshGroupTooltip(EventGroupButton& grp)
{
    if (!grp.button) return;
    QStringList sel;
    for (const auto& m : grp.members)
        if (m.action && grp.selection.count(m.et) > 0)
            sel.append(m.action->text());
    // Second line lists the selection; note when the master is off that the
    // selection isn't currently displayed.
    const QString what = sel.isEmpty() ? tr("(none)") : sel.join(", ");
    const QString line2 = grp.button->isChecked()
                        ? tr("Selected: %1").arg(what)
                        : tr("Selected (off): %1").arg(what);
    grp.button->setToolTip(grp.baseTooltip + "\n" + line2);
}

void
Transits::syncGroupFromOptions(EventGroupButton& grp)
{
    if (!grp.button) return;

    // present = the group's members currently enabled in _tabEventOptions.
    A::EventTypeSet present;
    for (const auto& m : grp.members)
        if (_tabEventOptions.count(m.et) > 0) present.insert(m.et);

    if (!present.empty()) {
        // Master ON: the live set IS the selection.
        grp.selection = present;
    } else if (grp.selection.empty()) {
        // Master OFF with no remembered selection: seed a default (the
        // independent, non-radio, default-on members) so turning the master on
        // shows something rather than nothing.
        for (const auto& m : grp.members)
            if (m.radioGroup < 0 && m.defaultOn) grp.selection.insert(m.et);
    }
    // else: master OFF, keep the remembered in-memory selection.

    // Reflect selection onto the menu checkmarks and master onto the button.
    for (auto& m : grp.members) {
        if (!m.action) continue;
        QSignalBlocker b(m.action);
        m.action->setChecked(grp.selection.count(m.et) > 0);
    }
    {
        QSignalBlocker bb(grp.button);
        grp.button->setChecked(!present.empty());
    }
    refreshGroupTooltip(grp);
}

void
Transits::updateToolbarFromEventOptions()
{
    if (!_actStations) return;  // Toolbar not initialized yet (S is built first)

    // Block signals during bulk updates for the standalone QActions.
    ASignalBlocker block({_actStations, _actReturns, _actPrimaryDirections});

    if (_actStations)
        _actStations->setChecked(_tabEventOptions.count(A::etcStation) > 0);
    if (_actReturns)
        _actReturns->setChecked(_tabEventOptions.count(A::etcReturn) > 0 ||
                                _tabEventOptions.count(A::etcSolarReturn) > 0 ||
                                _tabEventOptions.count(A::etcLunarReturn) > 0);
    if (_actPrimaryDirections)
        _actPrimaryDirections->setChecked(
            _tabEventOptions.count(A::etcPrimaryDirections) > 0);

    // Reconcile each grouped dropdown button from the option set.
    for (auto& grp : _eventGroups) syncGroupFromOptions(grp);

    // Sync the heliacal phase-decomposition checks and the model's mask to the
    // active chart (display-only; no recompute).
    if (!_heliacalPhaseActions.isEmpty()) {
        const unsigned mask = (filesCount() > 0 && file(0))
            ? file(0)->getHeliacalPhaseMask()
            : unsigned(AstroFile::kHeliacalPhaseDefault);
        for (const auto& pa : _heliacalPhaseActions) {
            QSignalBlocker b(pa.second);
            pa.second->setChecked((mask & pa.first) != 0);
        }
        if (_evm) _evm->setHeliacalPhaseMask(mask);
    }

    // Sync the duration split button and the Refresh button's lit state.
    updateSkipDurationButton();
    updateRefreshButtonState();
}

void
Transits::updateInputProgress(double prog)
{
    if (!_inputProgress) return;

    // Coalesce redundant updates so we don't queue needless repaints: the
    // determinate phase only advances in whole-percent steps, and the waiting
    // and finished phases are idempotent.
    if (prog >= 0 && prog < 1.0) {
        int pctNow  = int(prog * 100);
        int pctLast = int(_lastProgress * 100);
        if (_lastProgress >= 0 && _lastProgress < 1.0 && pctNow == pctLast)
            return;
    } else if (prog < 0 && _lastProgress < 0) {
        return;  // already showing the waiting wash
    } else if (prog >= 1.0 && _lastProgress >= 1.0) {
        return;  // already finished/hidden
    }
    _lastProgress = prog;

    // Cheap, relayout-free repaint (see InputProgressOverlay).
    _inputProgress->setProgress(prog);
}
