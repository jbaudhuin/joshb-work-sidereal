
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
    TransitEventsModel* _evm;
public:
    AChangeSignalFrame(TransitEventsModel* evm);
    ~AChangeSignalFrame();
};

class TransitEventsModel : public QAbstractItemModel {
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

    TransitEventsModel(QObject* parent = nullptr) :
        QAbstractItemModel(parent)
    { }

    TransitEventsModel(A::AspectSetId asps,
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

    typedef std::pair<const A::PlanetLoc*, const A::PlanetLoc*> planetPair;

    bool getPlanetPair(const A::PlanetRangeBySpeed& locs,
                       planetPair& pp) const
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
        int col = index.column();
        const auto& asp(prow==-1 ? (*_evs[row])
                                 : _evs[prow]->coincidence(row));
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
                    planetPair pp;
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
                A::PlanetRangeLess prless(true/*fast*/);
                if (prless(a->locations(),b->locations())) return true;
                if (prless(b->locations(),a->locations())) return false;
                if (a->dateTime() < b->dateTime()) return true;     // date-time
                if (a->dateTime() > b->dateTime()) return false;
                if (a->orb() < b->orb()) return true;   // orb
                if (a->orb() > b->orb()) return false;
                return (a->harmonic() < b->harmonic()); // harmonic
            }

            case natalTransitBodyCol:   // XXX
            {
                A::PlanetRangeLess prless(false/*not fast*/);
                if (prless(a->locations(),b->locations())) return true;
                if (prless(b->locations(),a->locations())) return false;
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
        std::sort(_evs.begin(), _evs.end(), less);
        endResetModel();

        delete _chs; _chs = nullptr;
    }

    void addEvents(const A::HarmonicEvents& evs)
    {
        auto ins = _evls.insert(evs);
        if (!ins.second) return;

        AChangeSignalFrame chs(this);

        beginResetModel();
        _evs.insert(_evs.end(),evs.cbegin(),evs.cend());
        sort();
        endResetModel();
    }

    void removeEvents(const A::HarmonicEvents& evs)
    {
        auto it = _evls.find(&evs);
        if (it == _evls.end()) return;

        AChangeSignalFrame chs(this);

        beginResetModel();
        rebuild();
        sort();
        endResetModel();
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
        for (const auto& evs : _evls) {
            _evs.insert(_evs.end(), evs->begin(), evs->end());
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
    std::vector<const A::HarmonicEvent*> _evs;
    std::set<const A::HarmonicEvents*> _evls;

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

AChangeSignalFrame::AChangeSignalFrame(TransitEventsModel* evm) :
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
    _ddelta(0,1,0),
    _chs(nullptr)
{
    _tview = new QTreeView;
    _tview->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _tview->expandAll();

    _evm = new TransitEventsModel(this);
    _tview->setModel(_evm);

    auto hdr = _tview->header();
    connect(hdr, &QHeaderView::sortIndicatorChanged,
            _evm, &TransitEventsModel::onSortChange);
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

    QTimer::singleShot(0, [this]() {
        connect(this, SIGNAL(updateHarmonics(double)),
                MainWindow::theAstroWidget(), SLOT(setHarmonic(double)));
    });
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
    return (ftype != AstroFile::TypeMale
            && ftype != AstroFile::TypeFemale);
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

    qDebug() << "filesCount()" << filesCount();

    _evs.clear();

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

    transitsAF()->setLocation(_location->location());
    transitsAF()->setLocationName(_location->locationName());

    auto tp = QThreadPool::globalInstance();
    auto hs = A::dynAspState();
    ADateRange r { _start->date(), _end->date() };
    if (transOnly) {
        auto tf = new A::TransitFinder(_evs, r, hs,
                                       scope.inputData, pst);
        tf->setIncludeStations(true);
        tp->start(tf);
    } else {
        const auto& ida(transitsAF()->horoscope().inputData);
        auto tf = new A::TransitFinder(_evs, r, hs,
                                       scope.inputData, pst);
        tf->setIncludeStations(false);
        tp->start(tf);
        tp->start(new A::NatalTransitFinder(_evs, r, hs,
                                            scope.inputData,
                                            ida, psn, pst));
    }

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

    if (inx.column()==TransitEventsModel::harmonicCol)  {
        auto v = inx.data(TransitEventsModel::RawRole);
        qDebug() << v;
        if (v.canConvert<unsigned>()) {
            double h = v.toUInt();
            emit updateHarmonics(h);
        }
    } else {
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

TransitEventsModel*
Transits::tvm() const
{
    return qobject_cast<TransitEventsModel*>(_tview->model());
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
    if (_inhibitUpdate) return;
    if (!filesCount()) {
        clear();
        return;
    }

    bool any = false;
    int f = 0;
    for (auto ml: m) {
        AstroFile::FileType type = file(f++)->getType();
        if (type >= AstroFile::TypeSearch) continue;
        if (type == AstroFile::TypeMale
                || type == AstroFile::TypeFemale)
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
#if 0
    s.setValue("Harmonics/order", A::hscByHarmonic);
    s.setValue("Harmonics/includeAscMC", false);
    s.setValue("Harmonics/includeChiron", true);
    s.setValue("Harmonics/includeNodes", true);
    s.setValue("Harmonics/includeOvertones", true);
    s.setValue("Harmonics/overtoneLimit", 16);
    s.setValue("Harmonics/includeMidpoints", false);
    s.setValue("Harmonics/requireMidpointAnchor", true);
    s.setValue("Harmonics/filterFew", true);
    s.setValue("Harmonics/max", 240);
    s.setValue("Harmonics/primeFactorLimit", 32);
    s.setValue("Harmonics/minQuorum", 2);
    s.setValue("Harmonics/maxQuorum", 4);
    s.setValue("Harmonics/minQOrb", 4.0);
    s.setValue("Harmonics/maxQOrb", 20.0);
#endif
    return s;
}

AppSettings Transits::currentSettings()
{
    AppSettings s;
#if 0
    s.setValue("Harmonics/order", s_harmonicsOrder);
    s.setValue("Harmonics/includeAscMC", A::includeAscMC());
    s.setValue("Harmonics/includeChiron", A::includeChiron());
    s.setValue("Harmonics/includeNodes", A::includeNodes());
    s.setValue("Harmonics/includeOvertones", includeOvertones());
    s.setValue("Harmonics/overtoneLimit", overtoneLimit());
    s.setValue("Harmonics/includeMidpoints", A::includeMidpoints());
    s.setValue("Harmonics/requireMidpointAnchor", A::requireAnchor());
    s.setValue("Harmonics/filterFew", A::filterFew());
    s.setValue("Harmonics/primeFactorLimit", A::primeFactorLimit());
    s.setValue("Harmonics/max", A::maxHarmonic());
    s.setValue("Harmonics/minQuorum", A::harmonicsMinQuorum());
    s.setValue("Harmonics/maxQuorum", A::harmonicsMaxQuorum());
    s.setValue("Harmonics/minQOrb", A::harmonicsMinQOrb());
    s.setValue("Harmonics/maxQOrb", A::harmonicsMaxQOrb());
#endif
    return s;
}

void Transits::applySettings(const AppSettings& s)
{
#if 0
    A::HarmonicSort oldOrder = s_harmonicsOrder;
    s_harmonicsOrder = A::HarmonicSort(s.value("Harmonics/order").toUInt());

    bool ff = s.value("Harmonics/filterFew").toBool();
    bool ascMC = s.value("Harmonics/includeAscMC").toBool();
    bool chiron = s.value("Harmonics/includeChiron").toBool();
    bool nodes = s.value("Harmonics/includeNodes").toBool();
    bool over = s.value("Harmonics/includeOvertones").toBool();
    unsigned ol = s.value("Harmonics/overtoneLimit").toUInt();
    bool mp = s.value("Harmonics/includeMidpoints").toBool();
    bool amp = s.value("Harmonics/requireMidpointAnchor").toBool();
    unsigned pfl = s.value("Harmonics/primeFactorLimit").toUInt();
    int max = s.value("Harmonics/max").toInt();
    int minq = s.value("Harmonics/minQuorum").toInt();
    int maxq = s.value("Harmonics/maxQuorum").toInt();
    double minqo = s.value("Harmonics/minQOrb").toDouble();
    double maxqo = s.value("Harmonics/maxQOrb").toDouble();

    bool changed = A::filterFew() != ff
        || A::includeAscMC() != ascMC
        || A::includeChiron() != chiron
        || A::includeNodes() != nodes
        || includeOvertones() != over
        || overtoneLimit() != ol
        || A::includeMidpoints() != mp
        || A::requireAnchor() != amp
        || A::primeFactorLimit() != pfl
        || A::maxHarmonic() != max
        || A::harmonicsMinQuorum() != minq
        || A::harmonicsMinQOrb() != minqo
        || A::harmonicsMaxQuorum() != maxq
        || A::harmonicsMaxQOrb() != maxqo;

    A::setIncludeAscMC(ascMC);
    A::setIncludeChiron(chiron);
    A::setIncludeNodes(nodes);
    setIncludeOvertones(over);
    setOvertoneLimit(ol);
    A::setIncludeMidpoints(mp);
    A::setRequireAnchor(amp);
    A::setFilterFew(ff);
    A::resetPrimeFactorLimit(pfl);
    A::setMaxHarmonic(max);
    A::setHarmonicsMinQuorum(minq);
    A::setHarmonicsMinQOrb(minqo);
    A::setHarmonicsMaxQuorum(maxq);
    A::setHarmonicsMaxQOrb(maxqo);

    if (changed) {

    }
    if (changed || s_harmonicsOrder != oldOrder) {
        updateTransits();
    }
#endif
}

void
Transits::setupSettingsEditor(AppSettingsEditor* ed)
{
#if 0
    ed->addTab(tr("Harmonics"));

    QMap<QString, QVariant> values;
    values[tr("Harmonic")] = A::hscByHarmonic;
    values[tr("Planets")] = A::hscByPlanets;
    values[tr("Orb")] = A::hscByOrb;

    ed->addComboBox("Harmonics/order", tr("Sort by"), values);
    ed->addCheckBox("Harmonics/includeAscMC", tr("Include Asc & MC"));
    ed->addCheckBox("Harmonics/includeChiron", tr("Include Chiron"));
    ed->addCheckBox("Harmonics/includeNodes", tr("Include Nodes"));

    auto io = ed->addCheckBox("Harmonics/includeOvertones",
                    tr("Include Overtones"));
    auto ol = ed->addSpinBox("Harmonics/overtoneLimit",
                             tr("Overtone harmonic limit"),
                             2, 60);
    connect(io, &QAbstractButton::toggled,
            [ol](bool b) { ol->setEnabled(b); });

    ed->addCheckBox("Harmonics/filterFew", 
                    tr("Filter planet subsets [abc w/o ab]"));

    ed->addSpinBox("Harmonics/primeFactorLimit", tr("Maximum harmonic factor"), 
                   1, 102400);
    ed->addSpinBox("Harmonics/max", tr("Maximum harmonics to calculate"), 
                   1, 102400);

    auto minq = ed->addSpinBox("Harmonics/minQuorum", 
                               tr("Minimal-orb quorum"), 2, 8);
    auto maxq = ed->addSpinBox("Harmonics/maxQuorum", 
                               tr("Maximal-orb quorum"), 2, 8);
    connect(minq, QOverload<int>::of(&QSpinBox::valueChanged),
            [maxq](int min) { maxq->setMinimum(min); });
    connect(maxq, QOverload<int>::of(&QSpinBox::valueChanged),
            [minq](int max) { minq->setMaximum(max); });

    auto minQOrb = 
        ed->addDoubleSpinBox("Harmonics/minQOrb", tr("Minimal orb"), 
                             0.0, 24.0, 1);
    auto maxQOrb =
        ed->addDoubleSpinBox("Harmonics/maxQOrb", tr("Maximum orb"), 
                             0.0, 24.0, 1);
    connect(minQOrb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [maxQOrb](double min) { maxQOrb->setMinimum(min); });
    connect(maxQOrb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [minQOrb](double max) { minQOrb->setMaximum(max); });

    ed->addTab(tr("Midpoints"));

    auto mpt = ed->addCheckBox("Harmonics/includeMidpoints",
                               tr("Include Midpoints"));
    auto anchor = ed->addCheckBox("Harmonics/requireMidpointAnchor",
                                  tr("Require midpoint anchor"));
    connect(mpt, &QAbstractButton::toggled,
            [anchor](bool b) { anchor->setEnabled(b); });
#endif
}
