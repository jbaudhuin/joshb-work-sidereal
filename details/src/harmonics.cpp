
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QAction>
#include <QFile>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
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
#include "../../zodiac/astroprocessor/src/astro-data.h"
#include "../../zodiac/src/mainwindow.h"
#include "harmonics.h"

enum HarmonicSort {
    hscByHarmonic,
    hscByPlanets,
    hscByOrb,
    hscByAge
};

static HarmonicSort s_harmonicsOrder = hscByHarmonic;
static bool s_showDegreeSpread = true;

namespace {

bool _includeOvertones = true;
bool includeOvertones() { return _includeOvertones; }
void setIncludeOvertones(bool b = true) { _includeOvertones = b; }

qreal
getSpread(const A::PlanetRange& range)
{
    qreal lo = range.cbegin()->loc;
    qreal hi = range.crbegin()->loc;
    if (hi - lo > A::harmonicsMaxQOrb()) {
        auto lit = range.cbegin();
        while (++lit != range.cend()) {
            if (lit->loc - lo > A::harmonicsMaxQOrb()) {
                hi = lo;
                lo = lit->loc;
                break;
            } else {
                lo = lit->loc;
            }
        }
    }
    return A::angle(lo, hi);
}

QVariant
getFactors(int h)
{
    auto f = A::getPrimeFactors(h);
    if (f.empty() || f.size() < 2) return QVariant();
    QStringList sl;
    for (auto n : f) sl << QString::number(n);
    return sl.join(" x ");
}

// Source: https://stackoverflow.com/questions/1956542/how-to-make-item-view-render-rich-html-text-in-qt
//
class HtmlDelegate : public QStyledItemDelegate
{
protected:
    void paint(QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index) const;
    QSize sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const;
};

void
HtmlDelegate::paint(QPainter *painter,
                    const QStyleOptionViewItem &option,
                    const QModelIndex &index) const
{
    QStyleOptionViewItem/*V4*/ optionV4 = option;
    initStyleOption(&optionV4, index);

    QStyle *style = /*optionV4.widget ? optionV4.widget->style() :*/ QApplication::style();

    QTextDocument doc;
    doc.setHtml(optionV4.text);

    /// Painting item without text
    optionV4.text = QString();
    style->drawControl(QStyle::CE_ItemViewItem, &optionV4, painter);

    QAbstractTextDocumentLayout::PaintContext ctx;

    // Highlighting text if item is selected
    if (optionV4.state & QStyle::State_Selected)
        ctx.palette.setColor(QPalette::Text, 
                             optionV4.palette.color(QPalette::Active, 
                                                    QPalette::HighlightedText));

    QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &optionV4);
    painter->save();
    painter->translate(textRect.topLeft());
    painter->setClipRect(textRect.translated(-textRect.topLeft()));
    doc.documentLayout()->draw(painter, ctx);
    painter->restore();
}

QSize
HtmlDelegate::sizeHint(const QStyleOptionViewItem &option,
                       const QModelIndex &index) const
{
    QStyleOptionViewItemV4 optionV4 = option;
    initStyleOption(&optionV4, index);

    QTextDocument doc;
    doc.setHtml(optionV4.text);
    doc.setTextWidth(optionV4.rect.width());
    return QSize(doc.idealWidth(), doc.size().height());
}

} // anonymous-namespace

Harmonics::Harmonics(QWidget* parent) : 
    AstroFileHandler(parent),
    harmonicsList(nullptr),
    _inhibitUpdate(false)
{
    _planet = A::Planet_None;
    _fileIndex = 0;

    harmonicsList = new QTreeWidget;
    //harmonicsList->setItemDelegate(new HtmlDelegate);

    //harmonicsList->setSizePolicy(harmonicsList->sizePolicy().horizontalPolicy(),
    //                             QSizePolicy::Expanded);
    harmonicsList->setSelectionMode(QAbstractItemView::ExtendedSelection);

    QAction* act = new QAction("Copy");
    act->setShortcut(QKeySequence::Copy);
    connect(act, SIGNAL(triggered()), this, SLOT(copySelection()));
    harmonicsList->addAction(act);

    QVBoxLayout* l2 = new QVBoxLayout(this);
    l2->setMargin(0);
    l2->addWidget(harmonicsList, 5);

    QFile cssfile("Details/style.css");
    cssfile.open(QIODevice::ReadOnly | QIODevice::Text);
    setStyleSheet(cssfile.readAll());

    QTimer::singleShot(0, [this]() {
        connect(this, SIGNAL(updateHarmonics(double)),
                MainWindow::theAstroWidget(), SLOT(setHarmonic(double)));
    });

    connect(harmonicsList, SIGNAL(clicked(const QModelIndex&)),
            this, SLOT(clickedCell(const QModelIndex&)));
    connect(harmonicsList, SIGNAL(doubleClicked(const QModelIndex&)),
            this, SLOT(doubleClickedCell(const QModelIndex&)));
    connect(harmonicsList->header(), SIGNAL(sectionDoubleClicked(int)),
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
    QStringList expo;
    QAbstractItemModel* m = harmonicsList->model();
    for (int i = 0, n = m->rowCount(); i < n; ++i) {
        QTreeWidgetItem* it = harmonicsList->topLevelItem(i);
        if (it->isExpanded()) {
            expo << it->text(0);
        }
    }

    harmonicsList->clear();
    
    if (!file(_fileIndex)) return;

    QFont astroFont("Almagest", 11);
    A::ChartPlanetMap cpm;
    for (int i = 0; i < filesCount(); ++i) {
        const A::Horoscope& scope(file(i)->horoscope());
        const A::ChartPlanetMap apm(scope.getOrigChartPlanets(i));
        for (auto it = apm.cbegin(); it != apm.cend(); ++it) {
            // Filter planet list...
            A::PlanetId pid = it.key().planetId();
            if (pid >= A::Planet_Sun && pid <= A::Planet_Pluto
                || ((pid == A::Planet_MC || pid == A::Planet_Asc)
                    && A::includeAscMC()
                    && A::aspectMode == A::amcEcliptic)
                || pid == A::Planet_Chiron && A::includeChiron()
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
    case hscByHarmonic:
    {
        harmonicsList->setColumnCount(2);
        harmonicsList->setHeaderLabels({ tr("Harmonic/Spread"),
                                         tr("Planets") });
        QMap<int, QTreeWidgetItem*> harm, over;
        typedef std::set<int> intSet;
        auto getDivs = [](int h, intSet& ret) {
            intSet is;
            for (int i = 2, n = sqrt(h); i <= n; ++i) {
                if (h%i == 0) {
                    if (h/i <= A::primeFactorLimit()) is.insert(i);
                    if (i <= A::primeFactorLimit()) is.insert(h / i);
                }
            }
            ret.swap(is);
        };

        std::multimap<A::ChartPlanetBitmap, QTreeWidgetItem*> firstHxItems;
        typedef std::pair<int, A::ChartPlanetBitmap> harmInst;
        QMap<QTreeWidgetItem*, harmInst> prev;
        for (auto ph : hx) {
            intSet divs;
            QList<QTreeWidgetItem*> hits;
            auto factors = getFactors(ph.first);
            for (auto hp : ph.second) {
                if (_planet != A::Planet_None && !hp.first.contains(_planet))
                    continue;

                qreal spread = getSpread(hp.second)
                    /*/ qreal(ph.first)*/;
                QString num = s_showDegreeSpread
                    ? A::degreeToString(spread, A::HighPrecision)
                    : QString::number(spread);

                auto it = new QTreeWidgetItem({ num, hp.first.glyphs() });
                it->setData(1, Qt::ToolTipRole, hp.first.names().join("-"));
                it->setData(1, Qt::FontRole, astroFont);
                //it->setFlags(Qt::ItemIsSelectable);
                hits << it;

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
                            firstHxItems.insert(std::make_pair(trio, it));
                        }
                    }
                }
                if (ph.first == 1 || hp.first.containsMidPt()) {
                    continue;
                }

                QString normNum = A::degreeToString(spread, A::LowPrecision);

                auto createOvertone = [&](int sh, const auto& tip) 
                    -> QTreeWidgetItem* 
                {
                    auto overtone =
                        new QTreeWidgetItem({ "H" + QString::number(ph.first / sh)
                                            + ": " + normNum,
                                            hp.first.glyphs() });
                    overtone->setData(1, Qt::ToolTipRole, 
                                      hp.first.names().join("-"));
                    overtone->setData(1, Qt::FontRole, astroFont);
                    overtone->setData(0, Qt::ToolTipRole, tip);
                    overtone->setData(0, Qt::UserRole + 1, ph.first);
                    return overtone;
                };

                if (!firstHxItems.empty()) {
                    A::ChartPlanetBitmap bmp(hp.first);
                    for (const auto& item : firstHxItems) {
                        if ((!prev.contains(item.second)
                             || prev[item.second].first != ph.first
                             || prev[item.second].second != bmp)
                            && item.first.isContainedIn(bmp)) 
                        {
                            prev[item.second] = harmInst(ph.first, bmp);
                            item.second->addChild(createOvertone(1, factors));
                        }
                    }
                }

                if (!includeOvertones()) continue;

                if (divs.empty()) getDivs(ph.first, divs);
                if (divs.empty()) continue;

                for (auto sh : divs) {
                    if (!harm.contains(sh)) {
                        auto hit = 
                            new QTreeWidgetItem({ "H" + QString::number(sh),
                                                  "Overtone(s)" },
                                                QTreeWidgetItem::UserType);
                        hit->setData(0, Qt::UserRole + 1, sh);
                        //harmonicsList->addTopLevelItem(hit);
                        over[sh] = harm[sh] = hit;
                    }
                    if (!over.contains(sh)) {
                        auto overtones = 
                            new QTreeWidgetItem({ "Overtones" },
                                                QTreeWidgetItem::UserType);
                        harm[sh]->addChild(overtones);
                        over[sh] = overtones;
                    }
                    over[sh]->addChild(createOvertone(sh, num));
                }
            }
            if (hits.isEmpty()) continue;

            QStringList sl;
            sl << "H" + QString::number(ph.first);
            sl << QString("%1 item%2").arg(hits.size())
                .arg(hits.size() != 1 ? "s" : "");
            QTreeWidgetItem* hit = new QTreeWidgetItem(sl);
            hit->setData(0, Qt::UserRole + 1, ph.first);
            hit->setData(0, Qt::ToolTipRole, factors);
            hit->addChildren(hits);
            //hit->setFlags(Qt::ItemIsSelectable);
            //harmonicsList->addTopLevelItem(hit);

            harm[ph.first] = hit;  // for adding overtones
        }
        for (auto& item : harm.values()) { 
            harmonicsList->addTopLevelItem(item); 
        }
        break;
    }
    case hscByPlanets:
    {
        harmonicsList->setColumnCount(2);
        harmonicsList->setHeaderLabels(QStringList()
                                       << tr("Planets/Spread")
                                       << tr("Harmonic"));

        QList<QTreeWidgetItem*> items;
        QMap<A::PlanetSet, int> dir;
        int n = 0;
        for (auto ph : hx) {
            for (auto hp : ph.second) {
                if (_planet != A::Planet_None && !hp.first.contains(_planet))
                    continue;

                QTreeWidgetItem* it = NULL;
                if (!dir.contains(hp.first)) {
                    it = new QTreeWidgetItem(QStringList(hp.first.glyphs()));
                    it->setData(0, Qt::ToolTipRole, hp.first.names().join("-"));
                    it->setData(0, Qt::FontRole, astroFont);
                    //it->setFirstColumnSpanned(true);
                    //it->setFlags(Qt::ItemIsSelectable);
                    items << it;
                    dir[hp.first] = n++;
                } else {
                    it = items.at(dir.value(hp.first, NULL));
                }
                if (!it) continue;  //silently fail

                qreal spread = getSpread(hp.second)
                    /*/ qreal(ph.first)*/;
                QString num = s_showDegreeSpread
                    ? A::degreeToString(spread, A::HighPrecision)
                    : QString::number(spread);
                auto kid =
                    new QTreeWidgetItem(QStringList()
                                        << num
                                        << "H" + QString::number(ph.first));
                kid->setData(1, Qt::UserRole + 1, ph.first);
                kid->setData(1, Qt::ToolTipRole, getFactors(ph.first));
                //kid->setFlags(Qt::ItemIsSelectable);
                it->addChild(kid);
            }
        }
        QList<QTreeWidgetItem*> ordered;
        for (auto pl = dir.cbegin(); pl != dir.cend(); ++pl) {
            ordered << items.at(pl.value());
        }
        harmonicsList->addTopLevelItems(ordered);
        for (auto&& twit: items) {
            twit->setFirstColumnSpanned(true);
        }
        break;
    }
    case hscByOrb:
    {
        harmonicsList->setColumnCount(2);
        harmonicsList->setHeaderLabels(QStringList()
                                       << tr("Spread")
                                       << tr("Harmonic")
                                       << tr("Planets"));

        QMultiMap<qreal, QTreeWidgetItem*> om;
        for (auto ph : hx) {
            for (auto hp : ph.second) {
                if (_planet != A::Planet_None && !hp.first.contains(_planet))
                    continue;

                qreal spread = getSpread(hp.second)
                    /*/ qreal(ph.first)*/;
                QString num = s_showDegreeSpread
                    ? A::degreeToString(spread, A::HighPrecision)
                    : QString::number(spread);
                auto kid = new QTreeWidgetItem(QStringList()
                                               << num
                                               << "H" + QString::number(ph.first)
                                               << hp.first.glyphs());
                kid->setData(1, Qt::UserRole + 1, ph.first);
                kid->setData(1, Qt::ToolTipRole, getFactors(ph.first));
                kid->setData(2, Qt::ToolTipRole, hp.first.names().join("-"));
                kid->setData(2, Qt::FontRole, astroFont);
                om.insert(spread, kid);
            }
        }

        QList<QTreeWidgetItem*> hits;
        for (auto el = om.cbegin(); el != om.cend(); ++el) {
            hits << el.value();
        }

        harmonicsList->addTopLevelItems(hits);
    }
    }

    for (const QString& str: expo) {
        for (QTreeWidgetItem* it :
                 harmonicsList->findItems(str, Qt::MatchExactly)) 
        {
            it->setExpanded(true);
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
    for (auto item :
         listWidget()->findItems(val, Qt::MatchExactly)) {
        listWidget()->scrollToItem(item);
        listWidget()->setCurrentItem(item,0,QItemSelectionModel::ClearAndSelect);
        if (item->childCount() > 0) {
            item->setExpanded(true);
        }
        break;
    }
}

void
Harmonics::clickedCell(const QModelIndex& inx)
{
    if (!(QApplication::keyboardModifiers() & Qt::ControlModifier)) return;

    QString val;
    QVariant v = inx.data(Qt::UserRole + 1);
    if (!v.isValid() && inx.parent().isValid()) {
        v = inx.parent().data(Qt::UserRole + 1);
        if (!v.isValid() && inx.parent().parent().isValid()) {
            v = inx.parent().parent().data(Qt::UserRole + 1);
        }
    } else if (inx.parent().isValid()) {
        if (QApplication::keyboardModifiers() & Qt::AltModifier) {
            val = "H" + v.toString();
        } else {
            val = inx.data(Qt::DisplayRole).toString();
            double d;
            bool ok;
            val = val.split(":").first();
            if (val.startsWith("H")
                && (d = val.mid(1).toDouble(&ok), ok)) {
                v = d;
            } else {
                return;
            }
        }
    }
    if (v.isValid()) {
        emit updateHarmonics(v.toDouble());
    }
    if (val.startsWith("H")) {
        QTimer::singleShot(250, [this, val]() {
            emit needToFindIt(val);
        });
    }
}

void 
Harmonics::doubleClickedCell(const QModelIndex& inx)
{
    if (!inx.parent().isValid() && inx.column() == 0) return;

    QVariant var(inx.data(Qt::DisplayRole));
    if (var.isValid() && var.canConvert<QString>()) {
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
    auto h = harmonicsList->headerItem();
    //qDebug() << /*item <<*/ col << h->text(col);
    auto itemText = h->text(col).split("/").last();
    HarmonicSort newOrder = s_harmonicsOrder;
    if (itemText == "Spread") {
        newOrder = hscByOrb;
    } else if (itemText == "Harmonic") {
        newOrder = hscByHarmonic;
    } else if (itemText == "Planets") {
        newOrder = hscByPlanets;
    }
    if (newOrder != s_harmonicsOrder) {
        s_harmonicsOrder = newOrder;
        QTimer::singleShot(0, this, SLOT(updateHarmonics()));
    }
}

void
Harmonics::copySelection()
{
    if (QAbstractItemModel* m = harmonicsList->model()) {
        QClipboard* cb = QApplication::clipboard();
        if (const QMimeData* md = cb->mimeData()) {
            if (md->hasText()) {
                qDebug() << md->text();
            } else if (md->hasHtml()) {
                qDebug() << md->html();
            }
        }
        QItemSelectionModel* sm = harmonicsList->selectionModel();
        QModelIndexList qmil = sm->selectedIndexes();
        qDebug() << qmil;
        QMimeData* md = m->mimeData(qmil);
        qDebug() << md->formats();
        cb->setMimeData(m->mimeData(qmil));
    }
}

void 
Harmonics::clear()
{
    _planet = A::Planet_None;
}

void 
Harmonics::filesUpdated(MembersList m)
{
    if (!filesCount()) {
        clear();
        return;
    }

    if (m[0] & ~(AstroFile::Harmonic
                 | AstroFile::AspectSet
                 | AstroFile::HouseSystem)) 
    {
        describePlanet();
    }
}

AppSettings 
Harmonics::defaultSettings()
{
    AppSettings s;
    s.setValue("Harmonics/order", hscByHarmonic);
    s.setValue("Harmonics/includeAscMC", false);
    s.setValue("Harmonics/includeChiron", true);
    s.setValue("Harmonics/includeNodes", true);
    s.setValue("Harmonics/includeOvertones", true);
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
    HarmonicSort oldOrder = s_harmonicsOrder;
    s_harmonicsOrder = HarmonicSort(s.value("Harmonics/order").toUInt());

    bool ff = s.value("Harmonics/filterFew").toBool();
    bool ascMC = s.value("Harmonics/includeAscMC").toBool();
    bool chiron = s.value("Harmonics/includeChiron").toBool();
    bool nodes = s.value("Harmonics/includeNodes").toBool();
    bool over = s.value("Harmonics/includeOvertones").toBool();
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
    values[tr("Harmonic")] = hscByHarmonic;
    values[tr("Planets")] = hscByPlanets;
    values[tr("Orb")] = hscByOrb;

    ed->addComboBox("Harmonics/order", tr("Sort by"), values);
    ed->addCheckBox("Harmonics/includeAscMC", tr("Include Asc & MC"));
    ed->addCheckBox("Harmonics/includeChiron", tr("Include Chiron"));
    ed->addCheckBox("Harmonics/includeNodes", tr("Include Nodes"));

    auto over = ed->addCheckBox("Harmonics/includeOvertones",
                                tr("Include Overtones"));

    auto mpt = ed->addCheckBox("Harmonics/includeMidpoints", 
                               tr("Include Midpoints"));
    auto anchor = ed->addCheckBox("Harmonics/requireMidpointAnchor", 
                                  tr("Require midpoint anchor"));
    connect(mpt, SIGNAL(toggled(bool)), anchor, SLOT(setEnabled(bool)));

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
}
