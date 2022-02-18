
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QAction>
#include <QFile>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QTreeView>
#include <QTreeWidgetItem>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QHeaderView>
#include <QTimer>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QDebug>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <Astroprocessor/Calc>
#include <Astroprocessor/Output>
#include "../../astroprocessor/src/astro-data.h"
#include "../../zodiac/src/mainwindow.h"
#include "harmonics.h"
#include <math.h>

static A::HarmonicSort s_harmonicsOrder = A::hscByHarmonic;
static bool s_showDegreeSpread = true;

namespace {

typedef QList<QStandardItem*> itemListBase;

/// comprise columns for a row in the tree view
class itemList : public itemListBase {
public:
    using itemListBase::itemListBase;

    itemList() : itemListBase() { }

    itemList(std::initializer_list<QString> its)
    { for (const auto& s: its) append(mksit(s)); }

    itemList(const QStringList& sl)
    { for (const auto& s: sl) append(mksit(s)); }

    itemList(const QString& a) :
        itemList( { a } )
    { }

    operator bool() const { return !isEmpty(); }

protected:
    static QStandardItem* mksit(const QString& s)
    {
        auto sit = new QStandardItem(s);
        sit->setFlags(Qt::ItemIsEnabled /*| Qt::ItemIsSelectable*/);
        return sit;
    }
};

bool _includeOvertones = true;
bool includeOvertones() { return _includeOvertones; }
void setIncludeOvertones(bool b = true) { _includeOvertones = b; }
unsigned _overtoneLimit = 16;
unsigned overtoneLimit() { return _overtoneLimit; }
void setOvertoneLimit(unsigned ol) { _overtoneLimit = ol; }

QVariant
getFactors(int h)
{
    auto f = A::getPrimeFactors(h);
    if (f.empty() || f.size() < 2) return QVariant();
    QStringList sl;
    for (auto n : f) sl << QString::number(n);
    return sl.join("×");
}

} // anonymous-namespace

Harmonics::Harmonics(QWidget* parent) : 
    AstroFileHandler(parent),
    _planet(A::Planet_None),
    _fileIndex(0),
    _inhibitUpdate(false),
    _hview(nullptr)
{
    _hview = new QTreeView;
    _hview->setSelectionMode(QAbstractItemView::ExtendedSelection);

    QAction* act = new QAction("Copy");
    act->setShortcut(QKeySequence::Copy);
    connect(act, SIGNAL(triggered()), this, SLOT(copySelection()));
    _hview->addAction(act);

    QVBoxLayout* l2 = new QVBoxLayout(this);
    l2->setContentsMargins(QMargins(0,0,0,0));
    l2->addWidget(_hview, 5);

    QFile cssfile("Details/style.css");
    cssfile.open(QIODevice::ReadOnly | QIODevice::Text);
    setStyleSheet(cssfile.readAll());

    QTimer::singleShot(0, [this]() {
        connect(this, SIGNAL(updateHarmonics(double)),
                MainWindow::theAstroWidget(), SLOT(setHarmonic(double)));
    });

    connect(_hview, SIGNAL(clicked(const QModelIndex&)),
            this, SLOT(clickedCell(const QModelIndex&)));
    connect(_hview, SIGNAL(doubleClicked(const QModelIndex&)),
            this, SLOT(doubleClickedCell(const QModelIndex&)));
    connect(_hview->header(), SIGNAL(sectionDoubleClicked(int)),
            this, SLOT(headerDoubleClicked(int)));

    connect(this, SIGNAL(needToFindIt(const QString&)), 
            this, SLOT(findIt(const QString&)));
}

void 
Harmonics::describePlanet()
{
    _fileIndex = qBound(0, _fileIndex, filesCount() - 1);
    updateHarmonics();
}

void
Harmonics::updateHarmonics()
{
    qDebug() << "filesCount()" << filesCount();
    QStringList expo;
    auto sim = tvm();
    if (sim) {
        for (int i = 0, n = sim->rowCount(); i < n; ++i) {
            auto it = sim->item(i);
            if (_hview->isExpanded(it->index())) {
                expo << it->text();
            }
        }
        sim->clear();
    } else {
        sim = new QStandardItemModel(this);
        _hview->setModel(sim);
    }
    
    if (!file(_fileIndex)) return;

    QFont astroFont("Almagest", 11);
    A::ChartPlanetMap cpm;
    for (int i = 0; i < filesCount(); ++i) {
        const A::Horoscope& scope(file(i)->horoscope());
        const A::ChartPlanetMap apm(scope.getOrigChartPlanets(i));
        for (auto it = apm.cbegin(); it != apm.cend(); ++it) {
            // Filter planet list...
            A::PlanetId pid = it.key().planetId();
            if ((pid >= A::Planet_Sun && pid <= A::Planet_Pluto)
                || ((pid == A::Planet_MC || pid == A::Planet_Asc)
                    && A::includeAscMC()
                    && A::aspectMode == A::amcEcliptic)
                || (pid == A::Planet_Chiron && A::includeChiron())
                || ((pid == A::Planet_SouthNode || pid == A::Planet_NorthNode)
                    && A::includeNodes()))
            {
                cpm[it.key()] = it.value();
            }
        }
    }
    A::PlanetHarmonics hx;
    A::findHarmonics(cpm, hx);

    switch (s_harmonicsOrder) {
    case A::hscByHarmonic:
    {
        sim->setColumnCount(2);
        sim->setHorizontalHeaderLabels({tr("Harmonic/Spread"),tr("Planets")});

        QMap<int, itemList> harm, over;
        typedef std::set<int> intSet;
        auto getDivs = [](int h, intSet& ret) {
            intSet is;
            for (int i = 2, n = int(sqrt(h));
                 i <= n; ++i)
            {
                if (h%i == 0) {
                    if (h/i <= overtoneLimit()) is.insert(i);
                    if (i <= overtoneLimit()) is.insert(h / i);
                }
            }
            ret.swap(is);
        };

        std::multimap<A::ChartPlanetBitmap, itemList> firstHxItems;
        typedef std::pair<int, A::ChartPlanetBitmap> harmInst;
        QHash<QStandardItem*, harmInst> prev;
        for (const auto& ph : hx) {
            intSet divs;
            QList<itemList> hits;
            auto factors = getFactors(ph.first);
            for (const auto& hp : ph.second) {
                if (_planet != A::Planet_None
                    && !hp.first.contains(_planet))
                {
                    continue;
                }

                qreal spread = getSpread(hp.second)
                    /*/ qreal(ph.first)*/;
                QString num = s_showDegreeSpread
                    ? A::degreeToString(spread, A::HighPrecision)
                    : QString::number(spread);

                itemList ip = { num, hp.first.glyphs() };
                ip[1]->setData(hp.first.names().join("-"), Qt::ToolTipRole);
                ip[1]->setData(astroFont, Qt::FontRole);
                hits << ip;

                if (ph.first == 1 && hp.first.containsMidPt()) {
                    // Permute A=B/C combinations. We will look for
                    // A,B,C in higher harmonics
                    A::PlanetSet planets, midpoints;
                    for (const auto& p : hp.first) {
                        if (p.isMidpt()) midpoints.insert(p);
                        else planets.insert(p);
                    }
                    for (const auto& p : planets) {
                        for (const auto& mp : midpoints) {
                            A::PlanetSet trio { 
                                p,
                                mp.chartPlanetId1(),
                                mp.chartPlanetId2()
                            };
                            firstHxItems.insert(std::make_pair(trio, ip));
                        }
                    }
                }
                if (ph.first == 1 || hp.first.containsMidPt()) {
                    continue;
                }

                QString normNum = A::degreeToString(spread, A::LowPrecision);

                auto createOvertone = [&](int sh, const auto& tip) {
                    itemList ip = {
                        "H" + QString::number(ph.first / sh) + ": " + normNum,
                        hp.first.glyphs()
                    };
                    ip[0]->setData(tip, Qt::ToolTipRole);
                    ip[0]->setData(ph.first, Qt::UserRole + 1);
                    ip[1]->setData(hp.first.names().join("-"), Qt::ToolTipRole);
                    ip[1]->setData(astroFont, Qt::FontRole);
                    return ip;
                };

                if (!firstHxItems.empty()) {
                    A::ChartPlanetBitmap bmp(hp.first);
                    for (const auto& item : firstHxItems) {
                        if ((!prev.contains(item.second[0])
                             || prev[item.second[0]].first != ph.first
                             || prev[item.second[0]].second != bmp)
                            && item.first.isContainedIn(bmp)) 
                        {
                            prev[item.second[0]] = harmInst(ph.first, bmp);
                            item.second[0]->appendRow(createOvertone(1, factors));
                        }
                    }
                }

                if (!includeOvertones()) continue;

                if (divs.empty()) getDivs(ph.first, divs);
                if (divs.empty()) continue;

                for (auto sh : divs) {
                    if (!harm.contains(sh)) {
                        itemList hit
                        { "H" + QString::number(sh), "Overtone(s)" };

                        ip[0]->setData(sh, Qt::UserRole + 1);
                        //harmonicsList->addTopLevelItem(hit);
                        over[sh] = harm[sh] = hit;
                    }
                    if (!over.contains(sh)) {
                        itemList overtones { "Overtones" };
                        harm[sh][0]->appendRow(overtones);
                        over[sh] = overtones;
                    }
                    over[sh][0]->appendRow(createOvertone(sh, num));
                }
            }
            if (hits.isEmpty()) continue;

            QStringList sl;
            sl << "H" + QString::number(ph.first);
            sl << QString("%1 item%2").arg(hits.size())
                .arg(hits.size() != 1 ? "s" : "");
            itemList hit = sl;
            hit[0]->setData(ph.first, Qt::UserRole + 1);
            hit[0]->setData(factors, Qt::ToolTipRole);
            for (const auto& h: hits) hit[0]->appendRow(h);

            harm[ph.first] = hit;  // for adding overtones
        }
        for (auto& item : harm.values()) { 
            sim->appendRow(item);
        }
        break;
    }
    case A::hscByPlanets:
    {
        sim->setColumnCount(2);
        sim->setHorizontalHeaderLabels({tr("Planets/Spread"), tr("Harmonic")});

        QList<itemList> items;
        QMap<A::PlanetSet, int> dir;
        int n = 0;
        for (const auto& ph : hx) {
            for (const auto& hp : ph.second) {
                if (_planet != A::Planet_None && !hp.first.contains(_planet))
                    continue;

                itemList it;
                if (!dir.contains(hp.first)) {
                    it = hp.first.glyphs();
                    it[0]->setData(hp.first.names().join("-"), Qt::ToolTipRole);
                    it[0]->setData(astroFont, Qt::FontRole);
                    //it->setFirstColumnSpanned(true);
                    //it->setFlags(Qt::ItemIsSelectable);
                    items << it;
                    dir[hp.first] = n++;
                } else {
                    it = items.at(dir.value(hp.first, 0));
                }
                if (!it) continue;  //silently fail

                qreal spread = getSpread(hp.second)
                    /*/ qreal(ph.first)*/;
                QString num = s_showDegreeSpread
                    ? A::degreeToString(spread, A::HighPrecision)
                    : QString::number(spread);

                itemList kid = { num, "H" + QString::number(ph.first) };
                kid[1]->setData(ph.first, Qt::UserRole + 1);
                kid[1]->setData(getFactors(ph.first), Qt::ToolTipRole);
                //kid->setFlags(Qt::ItemIsSelectable);
                it[0]->appendRow(kid);
            }
        }
        int r = 0;
        for (auto pl = dir.cbegin(); pl != dir.cend(); ++pl) {
            auto it = items.at(pl.value());
            r = sim->rowCount();
            sim->appendRow(it);
            _hview->setFirstColumnSpanned(r, QModelIndex(), true);
        }
        break;
    }
    case A::hscByOrb:
    {
        sim->setColumnCount(3);
        sim->setHorizontalHeaderLabels({tr("Spread"), tr("Harmonic"), tr("Planets")});

        QMultiMap<qreal, itemList> om;
        for (const auto& ph : hx) {
            for (const auto& hp : ph.second) {
                if (_planet != A::Planet_None && !hp.first.contains(_planet))
                    continue;

                qreal spread = getSpread(hp.second)
                    /*/ qreal(ph.first)*/;
                QString num = s_showDegreeSpread
                    ? A::degreeToString(spread, A::HighPrecision)
                    : QString::number(spread);
                itemList kid = { num,
                                 "H" + QString::number(ph.first),
                                 hp.first.glyphs() };
                kid[1]->setData(ph.first, Qt::UserRole + 1);
                kid[1]->setData(getFactors(ph.first), Qt::ToolTipRole);
                kid[2]->setData(astroFont, Qt::FontRole);
                kid[2]->setData(hp.first.names().join("-"), Qt::ToolTipRole);
                om.insert(spread, kid);
            }
        }

        for (auto el = om.cbegin(); el != om.cend(); ++el) {
            sim->appendRow(el.value());
        }
    }
    }

    for (const QString& str: qAsConst(expo)) {
        for (auto sit : sim->findItems(str, Qt::MatchExactly)) {
            htv()->setExpanded(sit->index(), true);
            break;
        }
    }
}

void 
Harmonics::setCurrentPlanet(A::PlanetId p, int file)
{
    if (_planet == p && _fileIndex == file) return;
    _planet = p;
    _fileIndex = file;
    describePlanet();
}

void
Harmonics::findIt(const QString& val)
{
    auto sim = tvm();
    if (!sim) return;

    for (auto item : sim->findItems(val, Qt::MatchExactly)) {
        htv()->scrollTo(item->index());
        //htv()->setCurrentItem(item,0,QItemSelectionModel::ClearAndSelect);
        if (item->rowCount() > 0) {
            htv()->setExpanded(item->index(), true);
        }
        break;
    }
}

void
Harmonics::clickedCell(const QModelIndex& inx)
{
    QString val;
    QVariant v = inx.data(Qt::UserRole + 1);
    auto getHarmonic = [&] {
        val = inx.data(Qt::DisplayRole).toString();
        double d(0);
        bool ok;
        val = val.split(":").first();
        if (val.startsWith("H")
                && (d = val.midRef(1).toDouble(&ok), ok)) {
            v = d;
            return true;
        }
        return false;
    };

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
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
            emit updateHarmonics(v.toDouble());
        }
        if (val.startsWith("H")) {
            QTimer::singleShot(250, [this, val]() {
                emit needToFindIt(val);
            });
        }
        return;
    }

    auto row = inx.row();
    auto col = inx.column();
    switch (s_harmonicsOrder) {
    case A::hscByHarmonic:
    {
        // col0 harmonic or spread, col1 planets
        bool isSub = inx.parent().isValid();
        bool isOvertone = isSub && inx.parent().parent().isValid();
        break;
    }

    case A::hscByPlanets:
        // col0 planets or spread, col1 harmonic
        break;

    case A::hscByOrb:
        break;
    }
}

void 
Harmonics::doubleClickedCell(const QModelIndex& inx)
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
Harmonics::headerDoubleClicked(int col)
{
    auto sim = tvm();
    if (!sim) return;

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
        QTimer::singleShot(0, this, SLOT(updateHarmonics()));
    }
}

void
Harmonics::copySelection()
{
    auto sim = tvm();
    if (!sim) return;

    QClipboard* cb = QApplication::clipboard();
    if (const QMimeData* md = cb->mimeData()) {
        if (md->hasText()) {
            qDebug() << md->text();
        } else if (md->hasHtml()) {
            qDebug() << md->html();
        }
    }
    QItemSelectionModel* sm = _hview->selectionModel();
    QModelIndexList qmil = sm->selectedIndexes();
    qDebug() << qmil;
    QMimeData* md = sim->mimeData(qmil);
    qDebug() << md->formats();
    cb->setMimeData(sim->mimeData(qmil));
}

void 
Harmonics::clear()
{
    _planet = A::Planet_None;
}

QStandardItemModel *
Harmonics::tvm() const
{
    return qobject_cast<QStandardItemModel*>(_hview->model());
}

void 
Harmonics::filesUpdated(MembersList m)
{
    if (!filesCount()) {
        clear();
        return;
    }

    bool any = false;
    for (auto ml: m) {
        any |= (ml & ~(AstroFile::Harmonic
                       | AstroFile::AspectSet
                       | AstroFile::HouseSystem));
    }
    if (any) describePlanet();
}

AppSettings 
Harmonics::defaultSettings()
{
    AppSettings s;
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
    return s;
}

AppSettings Harmonics::currentSettings()
{
    AppSettings s;
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
    return s;
}

void Harmonics::applySettings(const AppSettings& s)
{
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
        updateHarmonics();
    }
}

void
Harmonics::setupSettingsEditor(AppSettingsEditor* ed)
{
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
}
