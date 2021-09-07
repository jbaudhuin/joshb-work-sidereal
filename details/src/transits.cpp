
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QAction>
#include <QFile>
#include <QComboBox>
#include <QLabel>
#include <QTreeView>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QStringListModel>
#include <QHeaderView>
#include <QTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QPushButton>
#include <QDebug>
#include <QDateEdit>
#include <QTimeZone>
#include <QLineEdit>
#include <QThreadPool>
#include <Astroprocessor/Calc>
#include <Astroprocessor/Output>
#include "../../astroprocessor/src/astro-data.h"
#include "../../zodiac/src/mainwindow.h"
#include "transits.h"
#include "geosearch.h"
#include <math.h>
#include <vector>

namespace {
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

} // anonymous-namespace

class AChangeSignalFrame {
    EventsTableModel* _evm;
public:
    AChangeSignalFrame(EventsTableModel* evm);
    ~AChangeSignalFrame();
};

class EventsTableModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum {
        sortByDate,
        sortByHarmonic,
        sortByTransitBody,
        sortByNatalBody
    };

    enum {
        dateCol             = 0,
        orbCol              = 0,
        harmonicCol         = 1,
        transitBodyCol      = 2,
        natalTransitBodyCol = 3
    };

    enum roles {
        SummaryRole = Qt::UserRole,
        RawRole
    };

    EventsTableModel(QObject* parent = nullptr) :
        QAbstractItemModel(parent)
    { }

    EventsTableModel(A::AspectSetId asps,
                       QObject* parent = nullptr) :
        QAbstractItemModel(parent),
        _aspects(asps)
    { }

    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override
    {
        return createIndex(row, column,
                           parent.isValid()? quintptr(parent.row())
                                           : quintptr(-1));
    }

    QModelIndex parent(const QModelIndex& inx) const override
    {
        if (inx.isValid() && inx.internalId() != quintptr(-1)) {
            return createIndex(int(inx.internalId()),0,quintptr(-1));
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

    int columnCount(const QModelIndex& =QModelIndex()) const override
    { return 4; }

    int topLevelRow(const QModelIndex& inx) const
    { return (inx.parent().isValid()) ? inx.parent().row() : inx.row(); }

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

    int rowForData(const A::HarmonicEvent& haeda, bool& matches,
                   int sortCol, bool isMore) const
    {
        matches = false;
        auto lwrit = std::lower_bound(_evs.begin(),_evs.end(),
                                      &haeda,hevLess(sortCol,isMore));
        if (lwrit == _evs.end()) return -1;
        matches = (*(*lwrit) == haeda);
        return std::distance(_evs.begin(), lwrit);
    }

    QString rowDesc(int row) const
    {
        auto h = index(row,harmonicCol).data(SummaryRole).toString();
        auto t = index(row,transitBodyCol).data(SummaryRole).toString();
        auto n = index(row,natalTransitBodyCol).data(SummaryRole).toString();
        if (!n.isEmpty()) {
            return QString("%1 %2=%3").arg(h,t,n);
        }
        return rowDate(row).toLocalTime().toString("yyyy MMMM") + " " + t;
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
        case dateCol: return tr("Date");
        case harmonicCol: return tr("Asp");
        case transitBodyCol: return tr("Trans");
        case natalTransitBodyCol: return tr("T/N");
        }
        return QVariant();
    }

    void setZodiac(const A::Zodiac& zod) { _zodiac = zod; }

    QString getPos(float deg) const
    {
        const auto& sign = A::getSign(deg, _zodiac);
        QString glyph(QChar(sign.userData["fontChar"].toInt()));
        int ang = floor(deg) - sign.startAngle;
        if (ang < 0) ang += 360;

        int m = (int)(60.0*(deg - (int)deg));
        return QString("%1%2%3%4").arg(ang).arg(glyph)
                .arg(m >= 10 ? "" : "0").arg(m);
    };

    static int fid(const A::ChartPlanetId& cpid) { return cpid.fileId(); }
    static int fid(const A::PlanetLoc& ploc) { return ploc.planet.fileId(); }
    static int fid(const A::Loc& loc) { return -1; }

    QString display(const A::ChartPlanetId& cpid) const
    {
        return cpid.name();
    }

    QString display(const A::PlanetLoc& s) const
    {
        return QString(s.planet.fileId()==1
                       ? "<i>%1</i>" : "%1").arg(s.description())
                + " " + A::zodiacPosition(s.rasiLoc(), _zodiac,
                                          A::HighPrecision);
    }

    QString glyph(const A::ChartPlanetId& cpid) const
    { return cpid.glyph(); }

    QString glyph(const A::PlanetLoc& s) const
    {
        const A::ChartPlanetId& cpid = s.planet;
        auto g = cpid.glyph();
        if (cpid.planetId() >= A::Ingresses_Start
                && cpid.planetId() < A::Ingresses_End)
            return g;
        if (cpid.isMidpt()) g = g.mid(1);   // skip conj/opp
        auto desc = s.desc;
        if (!desc.isEmpty()) {
            if (desc=="SD") desc = "%&";
            else if (desc=="SR") desc = "%#";
            else if (desc=="n") desc = "";
        }
        if (s.speed < 0 && !s.desc.startsWith("S")) {
            desc = "#" + desc; // retrograde
        }
        return g + " " + getPos(s.rasiLoc()) + " " + desc;
    }

    QString summary(const A::ChartPlanetId& cpid) const
    { return cpid.name(); }

    QString summary(const A::PlanetLoc& s) const
    {
        auto str = s.planet.name();
        if (!s.desc.isEmpty()) str += "-" + s.desc;
        return str;
    }

    template <typename T>
    bool singleColumn(const T& locs) const
    {
        return locs.size()==1
                || (locs.size()>2
                    && fid(*locs.begin()) == fid(*locs.rbegin()));
    }

    template <typename T>
    bool mixedMode(const T& locs) const
    { return locs.size()>=2 && fid(*locs.begin()) != fid(*locs.rbegin()); }

    template <typename Iter>
    QVariant glyphic(int role, Iter its) const
    {
        if (role==Qt::FontRole) {
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

        return sl.join((role == Qt::ToolTipRole)? "-" : ",");
    }

    template <typename T>
    auto getNTColIters(const T& locs) const
    {
        if (singleColumn(locs)) {
            return std::make_pair(locs.rend(),locs.rend());
        }
        if (mixedMode(locs)) {
            auto it = locs.rbegin();
            auto f = fid(*it);
            auto end = it;
            while (end != locs.rend() && f==fid(*end)) ++end;
            return std::make_pair(it, end);
        }
        auto it = locs.rbegin();
        return std::make_pair(it, std::next(it));
    }

    template <typename T>
    auto getTColIters(const T& locs) const
    {
        if (singleColumn(locs)) {
            return std::make_pair(locs.begin(),locs.end());
        }
        if (mixedMode(locs)) {
            auto it = locs.begin();
            auto f = fid(*it);
            auto end = it;
            while (end != locs.end() && f==fid(*end)) ++end;
            return std::make_pair(it, end);
        }
        auto it = locs.begin();
        return std::make_pair(it, std::next(it));
    }

    typedef std::pair<const A::Loc*, const A::Loc*> locPair;

    bool getPlanetPair(const A::PlanetRangeBySpeed& locs,
                       locPair& pp) const
    {
        if (singleColumn(locs)) return false;
        pp.first = &(*locs.begin());
        pp.second = &(*locs.rbegin());
        return true;
    }

    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override
    {
        if (role == Qt::TextAlignmentRole
                && index.column() == harmonicCol)
        {
            return Qt::AlignHCenter;
        }

        if (role  != Qt::DisplayRole
                && role != Qt::ToolTipRole
                && role != Qt::EditRole
                && role != SummaryRole
                && role != RawRole
                && (index.column() < transitBodyCol
                    || (role != Qt::FontRole && role != Qt::ForegroundRole)))
        { return QVariant(); }

        int row = index.row();
        int prow = int(index.internalId());

        auto evr = prow==-1? _evs[row] : _evs[prow];
        QMutexLocker ml(&getEvents(evr)->mutex);

        int col = index.column();
        const auto& asp(prow==-1 ? (*_evs[row])
                                 : _evs[prow]->coincidence(row));
        auto et = asp.eventType();

        switch (col) {
        case dateCol:
            if (prow == -1) {
                // HarmonicEvent
                auto dt = _evs[row]->dateTime().toLocalTime();
                if (role == RawRole) return dt;
                if (role == Qt::ToolTipRole)
                    return dt.toString();   // XXX format
                return dt.toString("yyyy/MM/dd");
            }

            // HarmonicAspect
            return A::degreeToString(asp.orb());

        case harmonicCol:
            if (role == Qt::ToolTipRole) {
                if (singleColumn(asp.locations())) return "station";
                if (!asp.locations().empty()) {
                    locPair pp;
                    if (getPlanetPair(asp.locations(), pp)) {
                        auto a = A::calculateAspect(aspects(),
                                                    pp.first,
                                                    pp.second);
                        return a.d->name;
                    }
                }
            }
            if (role == RawRole) return asp.harmonic();
            return "H" + QString::number(asp.harmonic());

        case transitBodyCol:
            if (asp.locations().empty()) {
                // goofiness due to different sorts for planets
                // and locations
                if (singleColumn(asp.planets())) {
                    return glyphic(role, getTColIters(asp.planets()));
                }
                return glyphic(role, getNTColIters(asp.planets()));
            }
            return glyphic(role, getTColIters(asp.locations()));

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
                    return glyphic(role, getTColIters(asp.planets()));
                }
                break;
            }
            return glyphic(role, getNTColIters(asp.locations()));

        default:
            break;
        }
        return QVariant();
    }

    struct hevLess {
        int _col;
        bool _isMore;

        hevLess(int column = dateCol, bool isMore = false) :
            _col(column),
            _isMore(isMore)
        { }

        bool operator()(const A::HarmonicEvent* a,
                        const A::HarmonicEvent* b) const
        {
            if (_isMore) std::swap(a,b);
            switch (_col) {
            case dateCol:
                if (a->dateTime() < b->dateTime()) return true; // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true;   // orb
                if (a->orb() > b->orb()) return false;
                if (a->harmonic() < b->harmonic()) return true; // harmonic
                if (a->harmonic() > b->harmonic()) return false;
                if (a->locations().size() < b->locations().size())  // planetSet
                    return true;
                if (a->locations().size() > b->locations().size())
                    return false;
                return (a->locations() < b->locations());       // planetRange

            case harmonicCol:
                if (a->harmonic() < b->harmonic()) return true; // harmonic
                if (a->harmonic() > b->harmonic()) return false;
                if (a->dateTime() < b->dateTime()) return true; // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true;   // orb
                if (a->orb() > b->orb()) return false;
                if (a->locations().size() < b->locations().size())  // planetSet
                    return true;
                if (a->locations().size() > b->locations().size())
                    return false;
                return (a->locations() < b->locations());       // planetRange

            case transitBodyCol:
            {
                A::PlanetClusterLess prless(true/*fast*/);
                if (true || a->locations().empty() && b->locations().empty()) {
                    if (prless(a->planets(),b->planets())) return true;
                    if (prless(b->planets(),a->planets())) return false;
                } else {
                    if (prless(a->locations(),b->locations())) return true;
                    if (prless(b->locations(),a->locations())) return false;
                }
                if (a->dateTime() < b->dateTime()) return true;     // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true;   // orb
                if (a->orb() > b->orb()) return false;
                return (a->harmonic() < b->harmonic()); // harmonic
            }

            case natalTransitBodyCol:   // XXX
            {
                A::PlanetClusterLess prless(false/*not fast*/);
                if (true || a->locations().empty() && b->locations().empty()) {
                    if (prless(a->planets(),b->planets())) return true;
                    if (prless(b->planets(),a->planets())) return false;
                } else {
                    if (prless(a->locations(),b->locations())) return true;
                    if (prless(b->locations(),a->locations())) return false;
                }
                if (a->dateTime() < b->dateTime()) return true;     // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true;   // orb
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
        std::function<bool(HEv, HEv)> less =
                hevLess(column, order==Qt::DescendingOrder);

        beginResetModel();
#if 1
        _evs.clear();
        for (auto lievs: _evls) {
            A::modalize<eventListIndex> cev(evp::curr(), lievs.first);
            QMutexLocker ml(const_cast<QMutex*>(&(lievs.second->mutex)));
            _evs.insert(_evs.end(),lievs.second->cbegin(),lievs.second->cend());
        }
#endif
        std::sort(_evs.begin(), _evs.end(), less);
        endResetModel();

        delete _chs; _chs = nullptr;
    }

    void addEvents(const A::HarmonicEvents& evs)
    {
        auto li = currentEvents()++;
        auto ins = _evls.emplace(li,evs);
        if (!ins.second) return;

        AChangeSignalFrame chs(this);

        A::modalize<eventListIndex> cli(evp::curr(), li);
        beginResetModel();
        _evs.insert(_evs.end(),evs.cbegin(),evs.cend());
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

    const A::AspectsSet& aspects() const
    { return A::getAspectSet(_aspects); }

    int sortColumn() const { return _sortBy; }
    Qt::SortOrder sortOrder() const { return _sortOrder; }

public slots:
    void rebuild()
    {
        _evs.clear();
        for (const auto& liev : _evls) {
            _evs.insert(_evs.end(),
                        liev.second->begin(), liev.second->end());
        }
        sort();
    }

    void triggerSort()
    {
        if (!_sortPending) {
            _sortPending = true;
            QTimer::singleShot(0,this,SLOT(sort()));
        }
    }

    void onSortChange(int col, Qt::SortOrder order)
    {
        if (col != _sortBy || _sortOrder != order) {
            if (!_chs) _chs = new AChangeSignalFrame(this);
            _sortBy = col;
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
        { static thread_local unsigned short int s_curr = 0; return s_curr; }

        using Base::Base;

        evp(const A::HarmonicEvent* ev) : Base(curr(), ev) { }

        eventListIndex listIndex() const { return first; }

        const A::HarmonicEvent* operator->() const { return second; }
        const A::HarmonicEvent& operator*() const { return *second; }

        operator eventListIndex() const { return first; }
        operator const A::HarmonicEvent*() const { return second; }
    };

    static eventListIndex& currentEvents()
    { static eventListIndex s_curr = 0; return s_curr; }

    std::vector<evp> _evs;
    std::map<eventListIndex, const A::HarmonicEvents*> _evls;

    A::HarmonicEvents* getEvents(eventListIndex li) const
    {
        auto evlit = _evls.find(li);
        if (evlit == _evls.end()) return nullptr;
        return const_cast<A::HarmonicEvents*>(evlit->second);
    }

    bool _sortPending           = false;
    int _sortBy                 = sortByDate;
    Qt::SortOrder _sortOrder    = Qt::AscendingOrder;

    A::Zodiac _zodiac;
    A::AspectSetId _aspects = 0;

    int _changeRef = 0;

    AChangeSignalFrame* _chs = nullptr;

    friend class AChangeSignalFrame;
};

#include "transits.moc"

AChangeSignalFrame::AChangeSignalFrame(EventsTableModel* evm) :
    _evm(evm)
{ if (!evm->_changeRef++) emit evm->aboutToChange(); }

AChangeSignalFrame::~AChangeSignalFrame()
{ if (!--_evm->_changeRef) emit _evm->changeDone(); }


ADateDelta::ADateDelta(const QString& str)
{
    QRegularExpression re("(\\d+) ?((y(ea)?r?|m(o(n(th)?)?)?|d(a?y)?)s?)");
    re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);

    auto mit = re.globalMatch(str);
    while (mit.hasNext()) {
        auto match = mit.next();
        auto val = match.captured(1).toInt();
        auto unit = match.captured(2).toLower();
        if (unit.startsWith("y")) numYears = val;
        else if (unit.startsWith("m")) numMonths = val;
        else if (unit.startsWith("d")) numDays = val;
    }
}

ADateDelta::ADateDelta(QDate from, QDate to)
{
    if (from > to) qSwap(from, to);
    auto dd = from.daysTo(to);
    numYears = dd / 365;
    dd %= 365;
    numMonths = dd / 30;
    dd %= 30;
    auto num = to.day() - from.day();
    numDays = (numDays >= 0)? num : dd;
}

QDate
ADateDelta::addTo(const QDate &d)
{
    return d.addYears(numYears).addMonths(numMonths).addDays(numDays);
}

QDate
ADateDelta::subtractFrom(const QDate &d)
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
    _chs(nullptr)
{
    _tview = new QTreeView;
    _tview->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _tview->expandAll();

    _evm = new EventsTableModel(this);
    _tview->setModel(_evm);

    auto hdr = _tview->header();
    connect(hdr, &QHeaderView::sortIndicatorChanged,
            _evm, &EventsTableModel::onSortChange);
    hdr->setSectionsClickable(true);
    hdr->setSortIndicatorShown(true);
    hdr->setSortIndicator(0, Qt::AscendingOrder);
    hdr->setSectionResizeMode(QHeaderView::ResizeToContents);

    QAction* act = new QAction("Copy");
    act->setShortcut(QKeySequence::Copy);
    connect(act, SIGNAL(triggered()), this, SLOT(copySelection()));
    _tview->addAction(act);

    _start = new QDateEdit;
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
    _grp->addButton(_duraRB,0);
    _grp->addButton(_endRB,1);
    _duraRB->setChecked(true);

    auto l1 = new QHBoxLayout;
    l1->addWidget(_back);
    l1->addWidget(_forth);
    //l1->addWidget(new QLabel(tr("from")));
    l1->addWidget(_start);
    l1->addWidget(_duraRB);
    l1->addWidget(_duration);
    l1->addWidget(_endRB);
    l1->addWidget(_end);
    l1->setSpacing(4);
    
    _input = new QLineEdit;

    auto l2 = new QVBoxLayout;
    l2->addItem(l1);
    l2->addWidget(_input,0);
    l2->setContentsMargins(QMargins(4,4,4,4));

    QVBoxLayout* l3 = new QVBoxLayout;
    l3->setContentsMargins(QMargins(0,0,0,0));
    int i1 = l3->count();
    l3->addItem(l2);
    l3->setStretch(i1, 0);
    l3->addWidget(_tview, 5);

    _location = new GeoSearchWidget(false/*hbox*/);
    connect(_location, &GeoSearchWidget::locationChanged,
            [this] {
        if (!transitsAF()) return;
        transitsAF()->setLocation(_location->location());
        transitsAF()->setLocationName(_location->locationName());
    });
    l3->addWidget(_location);

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

    connect(_tview, SIGNAL(doubleClicked(const QModelIndex&)),
            this, SLOT(doubleClickedCell(const QModelIndex&)));
    connect(_tview, SIGNAL(pressed(const QModelIndex&)),
            this, SLOT(clickedCell(const QModelIndex&)));
    connect(_tview->header(), SIGNAL(sectionDoubleClicked(int)),
            this, SLOT(headerDoubleClicked(int)));

    connect(this, SIGNAL(needToFindIt(const QString&)), 
            this, SLOT(findIt(const QString&)));
    
    _start->setCalendarPopup(true);
    _end->setCalendarPopup(true);

    connect(_back, &QAbstractButton::clicked, [this] {
        auto sd = _start->date();
        auto dd = sd.daysTo(_end->date()) / 2;
        if (dd) _start->setDate(sd.addDays(-dd));
    });

    connect(_forth, &QAbstractButton::clicked, [this] {
        auto ed = _end->date();
        auto dd = _start->date().daysTo(ed) / 2;
        if (dd) _end->setDate(ed.addDays(dd));
    });

    auto updateDelta = [this](const QDate& ed) {
        ADateDelta delta(_start->date(), ed);
        if (delta != _ddelta) {
            _ddelta = delta;
            /*block*/ {
                ASignalBlocker sb(_duration);
                _duration->setText(_ddelta.toString());
            }
            onDateRangeChanged();
        }
    };
    
    connect(_start, &QDateEdit::dateChanged,
            [this, updateDelta](const QDate& sd)
    {
        if (_grp->checkedId()==0) {
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
    });
    connect(_end, &QDateEdit::dateChanged,
            [this, updateDelta](const QDate& sd)
    {
        if (_grp->checkedId()==0) {
            auto newDate = _ddelta.subtractFrom(sd);
            if (_start->date() != newDate) {
                /*block*/ {
                    ASignalBlocker sb(_start); _start->setDate(newDate);
                }
                onDateRangeChanged();
            }
        } else {
            updateDelta(sd);
        }
    });
    connect(_duration, &QLineEdit::editingFinished,
            [this]
    {
        ADateDelta delta = _duration->text();
        if (!delta) delta = QString("1 mo"); // revert to default

        if (delta != _ddelta) {
            _ddelta = delta;
            auto str = _ddelta.toString();
            if (str != _duration->text()) {
                /*block*/ {
                ASignalBlocker sb(_duration); _duration->setText(str);
                }
            }
            _end->setDate(_ddelta.addTo(_start->date()));
            onDateRangeChanged();
        }
    });

    auto today = QDate::currentDate();
    auto startOfMonth = QDate(today.year(),today.month(),1);
    _start->setDate(startOfMonth);

    connect(_evm, SIGNAL(aboutToChange()), this, SLOT(saveScrollPos()));
    connect(_evm, SIGNAL(changeDone()), this, SLOT(restoreScrollPos()));

#if 1
    QTimer::singleShot(0, [this]() {
        connect(this, SIGNAL(updateHarmonics(double)),
                MainWindow::theAstroWidget(), SLOT(setHarmonic(double)));
    });
#endif
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

bool
Transits::transitsOnly() const
{
    auto ftype = file()->getType();
    return (ftype != TypeMale
            && ftype != TypeFemale);
}

AstroFile *
Transits::transitsAF()
{
    if (files().count() > 1) return files()[1];
    if (!_trans) {
        _trans = new AstroFile(this);
        MainWindow::theAstroWidget()->setupFile(_trans);
        auto s = MainWindow::theAstroWidget()->currentSettings();
        auto v = s.value("Scope/defaultLocation").toString().split(" ");
        _location->setLocation(QVector3D(v.at(0).toFloat(),
                                         v.at(1).toFloat(),
                                         v.at(2).toFloat()));
        _location->setLocationName(s.value("Scope/defaultLocationName")
                                   .toString());
    }
    return _trans;
}

void
Transits::updateTransits()
{
    if (filesCount() == 0) return;
    if (!isVisible()) return;

    qDebug() << "filesCount()" << filesCount();

    auto tp = QThreadPool::globalInstance();
    auto hs = A::dynAspState();
    ADateRange r { _start->date(), _end->date() };

    _evm->removeEvents(_evs);
    _evs.clear();

    transitsAF()->setLocation(_location->location());
    transitsAF()->setLocationName(_location->locationName());

#if 1
    A::AspectFinder* af = nullptr;
    if (filesCount() == 1) {
        auto type = file(0)->getType();
        if (type == TypeMale || type == TypeFemale) {
            af = new A::AspectFinder(_evs,r,hs,{file(0), transitsAF()});
        }
    }
    if (!af && filesCount() >= 1) {
        af = new A::AspectFinder(_evs, r, hs, files());
    }
    if (af) tp->start(af);
#else
    auto transOnly = transitsOnly();

    const A::Horoscope& scope(file()->horoscope());
    A::PlanetSet pst, psn;

    for (const auto& cpid: scope.getOrigChartPlanets(0).keys()) {
        auto pid = cpid.planetId();
        if ((pid >= A::Planet_Sun && pid <= A::Planet_Pluto)
            || (pid == A::Planet_Chiron && A::includeChiron())
            || ((pid == A::Planet_SouthNode || pid == A::Planet_NorthNode)
                && A::includeNodes()))
        {
            if (!transOnly) psn.insert(cpid);
            //if (pid==A::Planet_Moon) continue;

            pst.insert(A::ChartPlanetId(unsigned(!transOnly),
                                        cpid.planetId(),A::Planet_None));
        } else if (!transOnly
                   && (pid == A::Planet_MC || pid == A::Planet_Asc))
        {
            psn.insert(cpid);
        }
    }


    if (transOnly) {
        auto tf = new A::TransitFinder(_evs, r, hs,
                                       scope.inputData, pst);
        tf->showStations = true;
        tp->start(tf);
        tp->start(new A::TransitFinder(_evs, r, hs, scope.inputData, pst,
                                       A::etcTransitAspectPattern));
    } else {
        const auto& ida(transitsAF()->horoscope().inputData);
        auto tf = new A::TransitFinder(_evs, r, hs,
                                       scope.inputData, pst);
        tf->showStations = false;
        tp->start(tf);
        tp->start(new A::NatalTransitFinder(_evs, r, hs,
                                            scope.inputData,
                                            ida, psn, pst));
        tp->start(new A::NatalTransitFinder(_evs, r, hs,
                                            scope.inputData, ida,
                                            psn, pst,
                                            A::etcTransitNatalAspectPattern));
    }
#endif

    if (!_watcher) {
        _watcher = new QTimer(this);
        connect(_watcher, SIGNAL(timeout()), this, SLOT(checkComplete()));
    }
    _watcher->start(250);
}

void
Transits::checkComplete()
{
    if (QThreadPool::globalInstance()->activeThreadCount()) {
        return;
    }
    _watcher->stop();
    onCompleted();
}

void
Transits::onCompleted()
{
    const A::Horoscope& scope(file()->horoscope());
    const auto& ida(transitsOnly()? file()->horoscope().inputData
                             : transitsAF()->horoscope().inputData);

    QTimeZone tz(ida.tz * 3600);
    for (auto& ev: _evs) {
#if 1
        // XXX can't this just be done by the transit finder?
        ev.dateTime().setTimeSpec(Qt::UTC);
#else
        ev.dateTime().setTimeZone(tz);
#endif
    }

    _evm->setZodiac(scope.zodiac);
    _evm->addEvents(_evs);
    //QTimer::singleShot(0,[this]{ _tview->expandAll(); });

    delete _chs;
    _chs = nullptr;
}

void 
Transits::setCurrentPlanet(A::PlanetId p, int file)
{
    if (_planet == p && _fileIndex == file) return;
    _planet = p;
    _fileIndex = file;
    describePlanet();
}

void
Transits::findIt(const QString& val)
{
    auto sim = tvm();
    if (!sim) return;

    for (const auto& item :
         _evm->match(_evm->index(0,0), Qt::DisplayRole,
                     val, 1, Qt::MatchExactly))
    {
        ttv()->scrollTo(item);
        ttv()->setExpanded(item, true);
        break;
    }
}

void
Transits::saveScrollPos()
{
    _anchorTop.clear();
    _anchorCur.clear();

    _anchorSort = _evm->sortColumn();
    _anchorOrder = _evm->sortOrder();
    _anchorVisibleRow = -1;

    QModelIndex top = ttv()->indexAt(ttv()->rect().topLeft());
    if (top.isValid()) {
        _anchorTop = _evm->rowData(top);
    }

    auto cur = ttv()->currentIndex();
    if (cur.isValid()) {
        _anchorCur = _evm->rowData(cur);
        if (top.isValid()
                && ttv()->visualRect(cur).isValid())
        {
            _anchorVisibleRow = cur.row() - top.row();
        }
    }
}

void
Transits::restoreScrollPos()
{
    if (_anchorCur == A::HarmonicEvent()
            && _anchorTop == A::HarmonicEvent())
    {
        return;
    }

    int col = _evm->sortColumn();
    auto order = _evm->sortOrder();
    bool ident = false;
    if (col != _anchorSort
            || !_anchorCur.isNull() && _anchorVisibleRow>=0)
    {
        if (_anchorCur == A::HarmonicEvent()) return;
        auto drow = _evm->rowForData(_anchorCur, ident, col,
                                     _evm->sortOrder()==Qt::DescendingOrder);
        if (drow != -1) {
            if (_anchorVisibleRow != -1) {
                int useRow = std::max(0, drow-_anchorVisibleRow);
                ttv()->scrollTo(_evm->index(useRow,0),
                                QAbstractItemView::PositionAtTop);
            } else {
                ttv()->scrollTo(_evm->index(drow,0));
            }
            if (ident) {
                ttv()->setCurrentIndex(_evm->index(drow,col));
            }
            return;
        }
    }

    auto drow = _evm->rowForData(_anchorTop, ident, col,
                                 _evm->sortOrder()==Qt::DescendingOrder);
    if (drow != -1) {
        auto pos = (order == _anchorOrder
                    ? QAbstractItemView::PositionAtTop
                    : QAbstractItemView::PositionAtBottom);
        ttv()->scrollTo(_evm->index(drow,col), pos);
    }
    if (_anchorCur == A::HarmonicEvent()) return;
    auto crow = _evm->rowForData(_anchorCur, ident, col,
                                 _evm->sortOrder()==Qt::DescendingOrder);
    if (crow == -1 || !ident) return;
    auto ci = tvm()->index(crow,col);
    ttv()->setCurrentIndex(ci);
    auto rect = ttv()->visualRect(ci);
    if (!rect.isValid()) {
        ttv()->scrollTo(ci);
    }
}

void
Transits::clickedCell(QModelIndex inx)
{
    auto btns = QGuiApplication::mouseButtons();
    bool mbtn = (btns & Qt::MiddleButton);
    bool lbtn = (btns & Qt::LeftButton);
    bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier);
    if (lbtn && ctrl) lbtn = false, mbtn = true;

    if (inx.column()==EventsTableModel::harmonicCol)  {
        auto v = inx.data(EventsTableModel::RawRole);
        qDebug() << v;
        if (v.canConvert<unsigned>()) {
            double h = v.toUInt();
            emit updateHarmonics(h);
        }
    } else if (inx.column()==EventsTableModel::dateCol) {
        emit updateHarmonics(1);
    }

    auto par = inx.parent();
    if (par.isValid()) inx = par;
    if (mbtn) {
        doubleClickedCell(inx);
        return;
    }
    auto dt = _evm->rowDate(inx.row());
    auto desc = _evm->rowDesc(inx.row());
    A::modalize<bool> noup(_inhibitUpdate);
    if (transitsOnly()) {
        file()->setName(desc);
        file()->setGMT(dt);
        emit updateFirst(file());
    } else {
        transitsAF()->setName(desc);
        transitsAF()->setGMT(dt);
        emit updateSecond(transitsAF());
        if (_trans && _trans->parent() != this) _trans = nullptr;
    }
}

void 
Transits::doubleClickedCell(QModelIndex inx)
{
    auto par = inx.parent();
    if (par.isValid()) inx = par;
    auto dt = _evm->rowDate(inx.row());
    auto desc = _evm->rowDesc(inx.row());
    A::modalize<bool> noup(_inhibitUpdate);
    AstroFile* af = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(af);
    af->setLocation(_location->location());
    af->setLocationName(_location->locationName());
    af->setName(desc);
    af->setGMT(dt);
    //bool shift = (QApplication::keyboardModifiers() & Qt::ShiftModifier);
    if (transitsOnly() /*|| !shift*/) {
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
Transits::copySelection()
{
    if (auto sim = tvm()) {
        QClipboard* cb = QApplication::clipboard();
        if (const QMimeData* md = cb->mimeData()) {
            if (md->hasText()) {
                qDebug() << md->text();
            } else if (md->hasHtml()) {
                qDebug() << md->html();
            }
        }
        QItemSelectionModel* sm = _tview->selectionModel();
        QModelIndexList qmil = sm->selectedIndexes();
        qDebug() << qmil;
        QMimeData* md = sim->mimeData(qmil);
        qDebug() << md->formats();
        cb->setMimeData(sim->mimeData(qmil));
    }
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

void Transits::onEventSelectionChanged()
{
#if OLDMODEL
    if (_tm) { delete _tm; _tm = nullptr; }
#else
    if (!_chs) _chs = new AChangeSignalFrame(_evm);
    if (_evm) { _evm->clearAllEvents(); }
#endif
    updateTransits();
}

void Transits::onDateRangeChanged()
{
#if OLDMODEL
    if (_tm) { delete _tm; _tm = nullptr; }
#else
    if (!_chs) _chs = new AChangeSignalFrame(_evm);
    if (_evm) { _evm->clearAllEvents(); }
#endif
    updateTransits();
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

    bool any = false;
    int f = 0;
    for (auto ml: m) {
        FileType type = file(f++)->getType();
        if (type >= TypeSearch) continue;
        if (type == TypeMale
                || type == TypeFemale)
        { any |= (ml & AstroFile::GMT); }
        any |= (ml & (AstroFile::Timezone
                      | AstroFile::Zodiac
                      | AstroFile::AspectSet));
    }
    if (any) {
#if OLDMODEL
        auto zap = _tm;
        _tview->setModel(nullptr);
        _tm = nullptr;
        zap->deleteLater();
#else
        if (!_chs) _chs = new AChangeSignalFrame(_evm);
        _evm->setAspectSet(file()->getAspectSetId());
        _evm->clearAllEvents();
#endif
        describePlanet();
    }
}

AppSettings 
Transits::defaultSettings()
{
    AppSettings s;
    A::EventOptions dflt;
    s.setValue("Events/defaultTimespan", dflt.defaultTimespan.toString());
    s.setValue("Events/secondaryOrb",                   dflt.expandShowOrb);
    s.setValue("Events/patternsQuorum",                 dflt.patternsQuorum);
    s.setValue("Events/patternsSpreadOrb",              dflt.patternsSpreadOrb);
    s.setValue("Events/patternsRestrictMoon",           dflt.patternsRestrictMoon);
    s.setValue("Events/includeMidpoints",               dflt.includeMidpoints);
    s.setValue("Events/showStations",                   dflt.showStations);
    s.setValue("Events/showTransitsToTransits",         dflt.showTransitsToTransits);
    s.setValue("Events/showTransitsToNatal",            dflt.showTransitsToNatalPlanets);
    s.setValue("Events/showReturns",                    dflt.showReturns);
    s.setValue("Events/showProgressionsToProgressions", dflt.showProgressionsToProgressions);
    s.setValue("Events/showProgressionsToNatal",        dflt.showProgressionsToNatal);
    s.setValue("Events/showTransitAspectPatterns",      dflt.showTransitAspectPatterns);
    s.setValue("Events/showTransitNatalAspectPatterns", dflt.showTransitNatalAspectPatterns);
    s.setValue("Events/showIngresses",                  dflt.showIngresses);
    s.setValue("Events/showLunations",                  dflt.showLunations);
    s.setValue("Events/showHeliacalEvents",             dflt.showHeliacalEvents);
    s.setValue("Events/showPrimaryDirections",          dflt.showPrimaryDirections);
    s.setValue("Events/showLifeEvents",                 dflt.showLifeEvents);

    s.setValue("Events/expandShowAspectPatterns",       dflt.expandShowAspectPatterns);
    s.setValue("Events/expandShowHousePlacementsOfTransits", dflt.expandShowHousePlacementsOfTransits);
    s.setValue("Events/expandShowRulershipTips",        dflt.expandShowRulershipTips);
    s.setValue("Events/expandShowStationAspectsToTransits", dflt.expandShowStationAspectsToTransits);
    s.setValue("Events/expandShowStationAspectsToNatal", dflt.expandShowStationAspectsToNatal);
    s.setValue("Events/expandShowReturnAspects",        dflt.expandShowReturnAspects);
    s.setValue("Events/expandShowTransitAspectsToReturnPlanet", dflt.expandShowTransitAspectsToReturnPlanet);
    return s;
}

AppSettings
Transits::currentSettings()
{
    AppSettings s;
    const A::EventOptions& curr(A::EventOptions::current());
    s.setValue("Events/defaultTimespan", curr.defaultTimespan.toString());
    s.setValue("Events/secondaryOrb",                   curr.expandShowOrb);
    s.setValue("Events/patternsQuorum",                 curr.patternsQuorum);
    s.setValue("Events/patternsSpreadOrb",              curr.patternsSpreadOrb);
    s.setValue("Events/patternsRestrictMoon",           curr.patternsRestrictMoon);
    s.setValue("Events/includeMidpoints",               curr.includeMidpoints);
    s.setValue("Events/showStations",                   curr.showStations);
    s.setValue("Events/showTransitsToTransits",         curr.showTransitsToTransits);
    s.setValue("Events/showTransitsToNatal",            curr.showTransitsToNatalPlanets);
    s.setValue("Events/showReturns",                    curr.showReturns);
    s.setValue("Events/showProgressionsToProgressions", curr.showProgressionsToProgressions);
    s.setValue("Events/showProgressionsToNatal",        curr.showProgressionsToNatal);
    s.setValue("Events/showTransitAspectPatterns",      curr.showTransitAspectPatterns);
    s.setValue("Events/showTransitNatalAspectPatterns", curr.showTransitNatalAspectPatterns);
    s.setValue("Events/showIngresses",                  curr.showIngresses);
    s.setValue("Events/showLunations",                  curr.showLunations);
    s.setValue("Events/showHeliacalEvents",             curr.showHeliacalEvents);
    s.setValue("Events/showPrimaryDirections",          curr.showPrimaryDirections);
    s.setValue("Events/showLifeEvents",                 curr.showLifeEvents);

    s.setValue("Events/expandShowAspectPatterns",       curr.expandShowAspectPatterns);
    s.setValue("Events/expandShowHousePlacementsOfTransits", curr.expandShowHousePlacementsOfTransits);
    s.setValue("Events/expandShowRulershipTips",        curr.expandShowRulershipTips);
    s.setValue("Events/expandShowStationAspectsToTransits", curr.expandShowStationAspectsToTransits);
    s.setValue("Events/expandShowStationAspectsToNatal", curr.expandShowStationAspectsToNatal);
    s.setValue("Events/expandShowReturnAspects",        curr.expandShowReturnAspects);
    s.setValue("Events/expandShowTransitAspectsToReturnPlanet", curr.expandShowTransitAspectsToReturnPlanet);
    return s;
}

void Transits::applySettings(const AppSettings& s)
{
    A::EventOptions& curr(A::EventOptions::current());
    bool changed =
            (s.value("Events/secondaryOrb").toDouble() != curr.expandShowOrb
            || s.value("Events/patternsQuorum").toUInt() != curr.patternsQuorum
            || s.value("Events/patternsSpreadOrb").toDouble() != curr.patternsSpreadOrb
            || s.value("Events/patternsRestrictMoon").toBool() != curr.patternsRestrictMoon
            || s.value("Events/includeMidpoints").toBool() != curr.includeMidpoints
            || s.value("Events/showStations").toBool() != curr.showStations
            || s.value("Events/showTransitsToTransits").toBool() != curr.showTransitsToTransits
            || s.value("Events/showTransitsToNatal").toBool() != curr.showTransitsToNatalPlanets
            || s.value("Events/showReturns").toBool() != curr.showReturns
            || s.value("Events/showProgressionsToProgressions").toBool() != curr.showProgressionsToProgressions
            || s.value("Events/showProgressionsToNatal").toBool() != curr.showProgressionsToNatal
            || s.value("Events/showTransitAspectPatterns").toBool() != curr.showTransitAspectPatterns
            || s.value("Events/showTransitNatalAspectPatterns").toBool() != curr.showTransitNatalAspectPatterns
            || s.value("Events/showIngresses").toBool() != curr.showIngresses
            || s.value("Events/showLunations").toBool() != curr.showLunations
            || s.value("Events/showHeliacalEvents").toBool() != curr.showHeliacalEvents
            || s.value("Events/showPrimaryDirections").toBool() != curr.showPrimaryDirections
            || s.value("Events/showLifeEvents").toBool() != curr.showLifeEvents);
    bool changedExpanded =
            (s.value("Events/expandShowAspectPatterns").toBool() != curr.expandShowAspectPatterns
            || s.value("Events/expandShowHousePlacementsOfTransits").toBool() != curr.expandShowHousePlacementsOfTransits
            || s.value("Events/expandShowRulershipTips").toBool() != curr.expandShowRulershipTips
            || s.value("Events/expandShowStationAspectsToTransits").toBool() != curr.expandShowStationAspectsToTransits
            || s.value("Events/expandShowStationAspectsToNatal").toBool() != curr.expandShowStationAspectsToNatal
            || s.value("Events/expandShowReturnAspects").toBool() != curr.expandShowReturnAspects
            || s.value("Events/expandShowTransitAspectsToReturnPlanet").toBool() != curr.expandShowTransitAspectsToReturnPlanet);

    auto tsp = s.value("Events/defaultTimespan").toString();
    curr.defaultTimespan = tsp;
    if (filesCount()==0) this->_duration->setText(tsp);

    curr.expandShowOrb = s.value("Events/secondaryOrb").toDouble();
    curr.patternsQuorum = s.value("Events/patternsQuorum").toUInt();
    curr.patternsSpreadOrb = s.value("Events/patternsSpreadOrb").toDouble();
    curr.patternsRestrictMoon = s.value("Events/patternsRestrictMoon").toBool();
    curr.includeMidpoints = s.value("Events/includeMidpoints").toBool();
    curr.showStations = s.value("Events/showStations").toBool();
    curr.showTransitsToTransits = s.value("Events/showTransitsToTransits").toBool();
    curr.showTransitsToNatalPlanets = s.value("Events/showTransitsToNatal").toBool();
    curr.showReturns = s.value("Events/showReturns").toBool();
    curr.showProgressionsToProgressions = s.value("Events/showProgressionsToProgressions").toBool();
    curr.showProgressionsToNatal = s.value("Events/showProgressionsToNatal").toBool();
    curr.showTransitAspectPatterns = s.value("Events/showTransitAspectPatterns").toBool();
    curr.showTransitNatalAspectPatterns = s.value("Events/showTransitNatalAspectPatterns").toBool();
    curr.showIngresses = s.value("Events/showIngresses").toBool();
    curr.showLunations = s.value("Events/showLunations").toBool();
    curr.showHeliacalEvents = s.value("Events/showHeliacalEvents").toBool();
    curr.showPrimaryDirections = s.value("Events/showPrimaryDirections").toBool();
    curr.showLifeEvents = s.value("Events/showLifeEvents").toBool();
    curr.expandShowAspectPatterns = s.value("Events/expandShowAspectPatterns").toBool();
    curr.expandShowHousePlacementsOfTransits = s.value("Events/expandShowHousePlacementsOfTransits").toBool();
    curr.expandShowRulershipTips = s.value("Events/expandShowRulershipTips").toBool();
    curr.expandShowStationAspectsToTransits = s.value("Events/expandShowStationAspectsToTransits").toBool();
    curr.expandShowStationAspectsToNatal = s.value("Events/expandShowStationAspectsToNatal").toBool();
    curr.expandShowReturnAspects = s.value("Events/expandShowReturnAspects").toBool();
    curr.expandShowTransitAspectsToReturnPlanet = s.value("Events/expandShowTransitAspectsToReturnPlanet").toBool();

    if (changed) {
        updateTransits();
    } else if (changedExpanded) {
        // updateExpanded(); ?
    }
}

void
Transits::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Events"));

    ed->addLineEdit("Events/defaultTimespan", tr("Default timespan"));
    ed->addCheckBox("Events/showStations", tr("Show Stations"));
    ed->addCheckBox("Events/showTransitsToTransits", tr("Show Transits to Transits"));
    ed->addCheckBox("Events/showTransitsToNatal", tr("Show Transits to Natal"));
    ed->addDoubleSpinBox("Events/secondaryOrb", tr("Secondary Orb"), 0.1, 16.);
    ed->addCheckBox("Events/includeMidpoints", tr("Include Midpoints"));
    ed->addCheckBox("Events/showReturns", tr("Show Returns"));
    ed->addCheckBox("Events/showTransitAspectPatterns", tr("Show Transit Aspect Patterns"));
    ed->addCheckBox("Events/showTransitNatalAspectPatterns", tr("Show Transit Natal Aspect Patterns"));
    ed->addSpinBox("Events/patternsQuorum", tr("Patterns Quorum"),3,6);
    ed->addDoubleSpinBox("Events/patternsSpreadOrb", tr("Patterns Spread Orb"), 0.1, 16.);
    ed->addCheckBox("Events/patternsRestrictMoon", tr("Patterns Restrict Moon"));
    ed->addCheckBox("Events/showIngresses", tr("Show Ingresses"));
    ed->addCheckBox("Events/showProgressionsToProgressions", tr("Show Progressions to Progressions"));
    ed->addCheckBox("Events/showProgressionsToNatal", tr("Show Progressions to Natal"));
    ed->addCheckBox("Events/showLunations", tr("Show Lunations"));
    ed->addCheckBox("Events/showHeliacalEvents", tr("Show Heliacal Events"));
    ed->addCheckBox("Events/showPrimaryDirections", tr("Show Primary Directions"));
    ed->addCheckBox("Events/showLifeEvents", tr("Show Life Events"));

    ed->addCheckBox("Events/expandShowAspectPatterns", tr("Expand to Show Aspect Patterns"));
    ed->addCheckBox("Events/expandShowHousePlacementsOfTransits", tr("Expand to Show House Placements Of Transits"));
    ed->addCheckBox("Events/expandShowRulershipTips", tr("Expand to Show Rulership Tips"));
    ed->addCheckBox("Events/expandShowStationAspectsToTransits", tr("Expand to Show Station Aspects To Transits"));
    ed->addCheckBox("Events/expandShowStationAspectsToNatal", tr("Expand to Show Station Aspects To Natal"));
    ed->addCheckBox("Events/expandShowReturnAspects", tr("Expand to Show Return Aspects"));
    ed->addCheckBox("Events/expandShowTransitAspectsToReturnPlanet", tr("Expand to Show Transit Aspects To Return Planet"));
}
