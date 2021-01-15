
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
#include <QHeaderView>
#include <QTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QDebug>
#include <QDateEdit>
#include <QTimeZone>
#include <QLineEdit>
#include <Astroprocessor/Calc>
#include <Astroprocessor/Output>
#include "../../astroprocessor/src/astro-data.h"
#include "../../zodiac/src/mainwindow.h"
#include "transits.h"
#include <math.h>

namespace {

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

#if 0
bool _includeOvertones = true;
bool includeOvertones() { return _includeOvertones; }
void setIncludeOvertones(bool b = true) { _includeOvertones = b; }
unsigned _overtoneLimit = 16;
unsigned overtoneLimit() { return _overtoneLimit; }
void setOvertoneLimit(unsigned ol) { _overtoneLimit = ol; }
#endif

QVariant
getFactors(int h)
{
    auto f = A::getPrimeFactors(h);
    if (f.empty() || f.size() < 2) return QVariant();
    QStringList sl;
    for (auto n : f) sl << QString::number(n);
    return sl.join(" x ");
}

} // anonymous-namespace

Transits::Transits(QWidget* parent) :
    AstroFileHandler(parent),
    _planet(A::Planet_None),
    _fileIndex(0),
    _inhibitUpdate(false),
    _tview(nullptr),
    _tm(nullptr)
{
    _tview = new QTreeView;
    _tview->setSelectionMode(QAbstractItemView::ExtendedSelection);

    QAction* act = new QAction("Copy");
    act->setShortcut(QKeySequence::Copy);
    connect(act, SIGNAL(triggered()), this, SLOT(copySelection()));
    _tview->addAction(act);

    _start = new QDateEdit;
    _duraRB = new QRadioButton(tr("for"));
    _duration = new QLineEdit;
    _duration->setText(tr("3 months"));
    _endRB = new QRadioButton(tr("til"));
    _end = new QDateEdit;

    _grp = new QButtonGroup(this);
    _grp->addButton(_duraRB,0);
    _grp->addButton(_endRB,1);

    auto l1 = new QHBoxLayout;
    l1->addWidget(new QLabel(tr("from")));
    l1->addWidget(_start);
    _start->setDate(QDate::currentDate());
    l1->addWidget(_duraRB);
    l1->addWidget(_duration);
    l1->addWidget(_endRB);
    l1->addWidget(_end);
    _end->setDate(QDate::currentDate().addMonths(3));

    _input = new QLineEdit;

    QVBoxLayout* l2 = new QVBoxLayout;
    l2->setMargin(0);
    l2->addWidget(_tview, 5);
    int i1 = l2->count();
    l2->addItem(l1);
    l2->setStretch(i1, 0);
    l2->addWidget(_input,0);

    setLayout(l2);

    QFile cssfile("Details/style.css");
    cssfile.open(QIODevice::ReadOnly | QIODevice::Text);
    setStyleSheet(cssfile.readAll());

#if 0
    QTimer::singleShot(0, [this]() {
        connect(this, SIGNAL(updateTransits(double)),
                MainWindow::theAstroWidget(), SLOT(setHarmonic(double)));
    });
#endif

    connect(_tview, SIGNAL(clicked(const QModelIndex&)),
            this, SLOT(clickedCell(const QModelIndex&)));
    connect(_tview, SIGNAL(doubleClicked(const QModelIndex&)),
            this, SLOT(doubleClickedCell(const QModelIndex&)));
    connect(_tview->header(), SIGNAL(sectionDoubleClicked(int)),
            this, SLOT(headerDoubleClicked(int)));

    connect(this, SIGNAL(needToFindIt(const QString&)), 
            this, SLOT(findIt(const QString&)));
}

void 
Transits::describePlanet()
{
    _fileIndex = qBound(0, _fileIndex, filesCount() - 1);
    updateTransits();
}

void
Transits::updateTransits()
{
    qDebug() << "filesCount()" << filesCount();
    if (_tm) return;

    auto ftype = file()->getType();
    bool transOnly = (ftype != AstroFile::TypeMale
            && ftype != AstroFile::TypeFemale);

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

    AstroFile f;
    MainWindow::theAstroWidget()->setupFile(&f);
    const auto& ida(f.horoscope().inputData);

    A::HarmonicEvents evs;

    auto hs = A::dynAspState();
    if (transOnly) {
        A::calculateTransits(hs, _start->date(), _end->date(),
                             file()->horoscope().inputData,
                             pst, evs);
    } else {
        A::calculateTransitsToNatal(hs, _start->date(), _end->date(),
                                    file()->horoscope().inputData,
                                    ida,
                                    psn, pst, evs);
    }

    _tm = new QStandardItemModel(this);
    _tm->setColumnCount(4);
    _tm->setHorizontalHeaderLabels({"Date", "H", "T",
                                    transOnly? "T" : "N/T"});

    auto byDate = [](const auto& a, const auto& b)
    { return std::get<0>(a) < std::get<0>(b); };

    std::stable_sort(evs.begin(), evs.end(), byDate);

    const A::Zodiac& zodiac(scope.zodiac);
    auto getPos = [&zodiac](float deg) {
        const auto& sign = A::getSign(deg, zodiac);
        QString glyph(sign.userData["fontChar"].toInt());
        int ang = floor(deg) - sign.startAngle;
        if (ang < 0) ang += 360;

        int m = (int)(60.0*(deg - (int)deg));
        return QString("%1%2%3%4").arg(ang).arg(glyph)
                .arg(m >= 10 ? "" : "0").arg(m);
    };

    QFont astroFont("Almagest", 11);

    QDateTime prev;
    QStandardItem* previt = nullptr;
    for (const auto& ev: evs) {
        QDateTime dt =             std::get<0>(ev);
        dt.setTimeZone( QTimeZone(ida.tz) );

        if (dt != prev) previt = nullptr;

        unsigned char ch =         std::get<1>(ev);
        const A::PlanetSet& ps =   std::get<2>(ev);
        const A::PlanetRange& pr = std::get<3>(ev);

        itemList il;

        if (prev == dt) { il.append(""); }
        else {
            il.append(dt.toLocalTime().date().toString("yyyy/MM/dd"));
            il.last()->setToolTip(dt.toLocalTime().toString());
        }
        il.append(QString("H%1").arg(ch));

        bool station = false;
        for (auto pit = ps.crbegin(); pit != ps.crend(); ++pit) {
            const auto& cpid = *pit;
            auto it = std::find_if(pr.begin(),pr.end(),
                                 [&](const A::PlanetLoc& pl) {
                return pl.planet == cpid;
            });
            if (it == pr.end()) continue;
            const auto& s = *it;
            auto g = s.planet.glyph();
            auto desc = s.desc;
            if (!desc.isEmpty()) {
                if (desc=="SD" || desc=="SR") station = true;
                if (desc=="SD") desc = "%&";
                else if (desc=="SR") desc = "%#";
                else if (desc=="r") desc = "";
            }
            if (s.speed < 0 && !s.desc.startsWith("S")) {
                desc = "#" + desc; // retro
            }
            il.append(g + " " + getPos(s.rasiLoc()) + " " + desc);
            il.last()->setFont(astroFont);
            auto tt = QString(s.planet.fileId()==1
                              ? "<i>%1</i>" : "%1")
                    .arg(s.description()) + " "
                    + A::zodiacPosition(s.rasiLoc(),zodiac,A::HighPrecision);
            il.last()->setToolTip(tt);
        }
        if (il.size() < 4) il.append("");

        if (previt) previt->appendRow(il);
        else {
            if (station) previt = il.first();
            _tm->appendRow(il);
        }
        prev = dt;
    }
    _tview->setModel(_tm);
    _tview->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _tview->expandAll();


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

    for (auto item : sim->findItems(val, Qt::MatchExactly)) {
        ttv()->scrollTo(item->index());
        //htv()->setCurrentItem(item,0,QItemSelectionModel::ClearAndSelect);
        if (item->rowCount() > 0) {
            ttv()->setExpanded(item->index(), true);
        }
        break;
    }
}

void
Transits::clickedCell(const QModelIndex& inx)
{
    if (!(QApplication::keyboardModifiers() & Qt::ControlModifier)) return;

    QString val;
    QVariant v = inx.data(Qt::UserRole + 1);

#if 0
    auto getHarmonic = [&] {
        val = inx.data(Qt::DisplayRole).toString();
        double d(0);
        bool ok;
        val = val.split(":").first();
        if (val.startsWith("H")
            && (d = val.mid(1).toDouble(&ok), ok)) {
            v = d;
            return true;
        }
        return false;
    };
    if (!v.isValid() && inx.parent().isValid()) {
        v = inx.parent().data(Qt::UserRole + 1);
        if (!v.isValid() && inx.parent().parent().isValid()) {
            v = inx.parent().parent().data(Qt::UserRole + 1);
        }
    } else if (inx.parent().isValid()) {
        if (QApplication::keyboardModifiers() & Qt::AltModifier) {
            val = "H" + v.toString();
        } else if (!getHarmonic()) return;
    }
    if (v.isValid() || getHarmonic()) {
        emit updateTransits(v.toDouble());
    }
    if (val.startsWith("H")) {
        QTimer::singleShot(250, [this, val]() {
            emit needToFindIt(val);
        });
    }
#endif
}

void 
Transits::doubleClickedCell(const QModelIndex& inx)
{
    if (!inx.parent().isValid() && inx.column() == 0) return;

    auto sim = tvm();
    if (!sim) return;

    auto hit = sim->itemFromIndex(inx);
    if (hit->hasChildren()) {
        // Ignore if it's the 'overtones' [or some other future] parent.
        // Don't need to expand/contract because that's the default behavior
        //htv()->setExpanded(inx,!htv()->isExpanded(inx));
        return;
    }

    QVariant var(inx.data(Qt::DisplayRole));
    if (var.isValid() && var.canConvert<QString>()) {
        // Double-click an entry leads to opening it up in another sort.
        headerDoubleClicked(inx.column());
        QString val(var.toString());
        QTimer::singleShot(250, [this, val]() {
            emit needToFindIt(val);
        });
    }
}

void 
Transits::headerDoubleClicked(int col)
{
    auto sim = tvm();
    if (!sim) return;
#if 0
    auto hit = sim->horizontalHeaderItem(col);
    //qDebug() << /*item <<*/ col << h->text(col);
    QString itemText = hit->text().split("/").last();
    A::HarmonicSort newOrder = s_harmonicsOrder;
    if (itemText == "Spread") {
        newOrder = A::hscByOrb;
    } else if (itemText == "Harmonic") {
        newOrder = A::hscByHarmonic;
    } else if (itemText == "Planets") {
        newOrder = A::hscByPlanets;
    }
    if (newOrder != s_harmonicsOrder) {
        s_harmonicsOrder = newOrder;
        QTimer::singleShot(0, this, SLOT(updateTransits()));
    }
#endif
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

QStandardItemModel *
Transits::tvm() const
{
    return qobject_cast<QStandardItemModel*>(_tview->model());
}

void 
Transits::filesUpdated(MembersList m)
{
    if (!filesCount()) {
        clear();
        return;
    }

    bool any = false;
    for (auto ml: m) {
        any |= (ml & ~(AstroFile::GMT
                       | AstroFile::Timezone));
    }
    if (any) {
        auto zap = _tm;
        _tview->setModel(nullptr);
        _tm = nullptr;
        zap->deleteLater();
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
