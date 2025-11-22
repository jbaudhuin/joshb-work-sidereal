#include <QActionGroup>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QRadialGradient>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include <QFileSystemWatcher>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>

#include "../astroprocessor/src/astro-calc.h"
#include "../astroprocessor/src/astro-data.h"
#include "../chart/src/chart.h"
#include "../details/src/details.h"
#include "../details/src/harmonics.h"
#include "../details/src/speculum.h"
#include "../details/src/transits.h"
#include "../fileeditor/src/fileeditor.h"
#include "../fileeditor/src/geosearch.h"
#include "../plain/src/plain.h"
#include "../planets/src/planets.h"
#include "mainwindow.h"
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QGraphicsBlurEffect>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMetaObject>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QWidget>
#include <math.h>

/* =========================== ASTRO FILE INFO ============================== */

AstroFileInfo::AstroFileInfo(QWidget* parent) : AstroFileHandler(parent)
{
    currentIndex = 0;

    edit                    = new QPushButton(this);
    shadow                  = new QLabel(this);
    QGraphicsBlurEffect* ef = new QGraphicsBlurEffect();

    ef->setBlurRadius(5);
    shadow->setGraphicsEffect(ef);
    edit->setFlat(true);
    edit->raise();
    edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    edit->setCursor(Qt::PointingHandCursor);
    setStatusTip(tr("Input data"));

    QGridLayout* layout = new QGridLayout(this);
    layout->addWidget(edit, 0, 0, 1, 1);
    layout->addWidget(shadow, 0, 0, 1, 1);

    connect(edit, SIGNAL(clicked()), this, SIGNAL(clicked()));
}

void
AstroFileInfo::refresh()
{
    qDebug() << "AstroFileInfo::refresh";
    QDateTime dt   = currentFile()->getLocalTime();
    auto      date = QLocale().toString(dt.date(), QLocale::ShortFormat);

    QString dayOfWeek = dt.date().toString("ddd");
    QString time      = dt.time().toString();

    QString age;
    if (showAge) {
        float a1;
        
        // For progressed charts, show years since natal chart
        if (currentFile()->hasBaseChart() && filesCount() > 0) {
            // Calculate years from base chart (natal)
            QDateTime baseTime = currentFile()->getBaseChartGMT();
            QDateTime progTime = currentFile()->getGMT();
            a1 = baseTime.daysTo(progTime) / 365.25;
        } else {
            // Normal: years from birth to now
            a1 = dt.daysTo(QDateTime::currentDateTime()) / 365.25;
        }
        
        char  a[7];
        snprintf(a, sizeof(a), "%5.2f", a1);
        age = tr(", %1 years").arg(a);
    }

    QString timezone;
    if (currentFile()->getTimezone() > 0)
        timezone = QString("GMT +%1").arg(currentFile()->getTimezone());
    else if (currentFile()->getTimezone() < 0)
        timezone = QString("GMT %1").arg(currentFile()->getTimezone());
    else
        timezone = "GMT";

    QString place;
    if (currentFile()->getLocationName().isEmpty()) {
        QString longitude = A::degreeToString(currentFile()->getLocation().x(),
                                              A::HighPrecision);
        QString latitude  = A::degreeToString(currentFile()->getLocation().y(),
                                             A::HighPrecision);
        place             = QString("%1N  %2E").arg(latitude, longitude);
    } else {
        place = currentFile()->getLocationName();
    }

    setText(QString("%1\n").arg(currentFile()->getName())
            + tr("%1 %2 %3 (%4)%5\n").arg(date, dayOfWeek, time, timezone, age)
            + place);
}

void
AstroFileInfo::filesUpdated(MembersList m)
{
    if (currentIndex >= filesCount() /*|| currentIndex >= m.size()*/) {
        setText("");
        return;
    }
    while (currentIndex >= m.size()) m.append(AstroFile::Member());
    if (m[currentIndex]
        & (AstroFile::Name | AstroFile::GMT | AstroFile::Timezone
           | AstroFile::Location | AstroFile::LocationName))
        refresh();
}

void
AstroFileInfo::setText(const QString& str)
{
    edit->setText(str);
    shadow->setText(str);
}

AppSettings
AstroFileInfo::defaultSettings()
{
    AppSettings s;
    s.setValue("age", true);
    return s;
}

AppSettings
AstroFileInfo::currentSettings()
{
    AppSettings s;
    s.setValue("age", showAge);
    return s;
}

void
AstroFileInfo::applySettings(const AppSettings& s)
{
    showAge = s.value("age").toBool();
    if (currentIndex < filesCount()) refresh();
}

void
AstroFileInfo::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addCheckBox("age", tr("Show age:"));
}

/* =========================== ASTRO WIDGET
 * ========================================= */

AstroWidget::AstroWidget(QWidget* parent) : QWidget(parent)
{
    editor = nullptr;

    toolBar     = new QToolBar(tr("Slides"), this);
    actionGroup = new QActionGroup(this);

    geoWdg      = new GeoSearchWidget;
    slides      = new SlideWidget;
    fileView    = new AstroFileInfo;
    fileView2nd = new AstroFileInfo;

    toolBar->setObjectName("slides");
    actionGroup->setExclusive(true);
    slides->setTransitionEffect(SlideWidget::Transition_HorizontalSlide);
    fileView2nd->setStatusTip(tr("Background data"));
    fileView2nd->setCurrentIndex(1);
    fileView2nd->setObjectName("secondFile");

    QGridLayout* layout = new QGridLayout(this);
    layout->setContentsMargins(QMargins(0, 0, 0, 0));
    layout->addWidget(slides, 0, 0, 1, 1);
    layout->addWidget(fileView, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(fileView2nd, 0, 0, 1, 1, Qt::AlignRight | Qt::AlignTop);

    addDockWidget(new Details, tr("Details"), true /*scrollable*/);
    addDockWidget(new Harmonics, tr("Harmonics"), false /*not scrollable*/);
    addDockWidget(new Transits, tr("Events"), false /*notScroll*/);
    addDockWidget(new Speculum, tr("Speculum"), false /*not scrollable*/);
    addSlide(new Chart, QIcon("style/natal.png"), tr("Chart"));
    addSlide(new Planets, QIcon("style/planets.png"), tr("Planets"));
    addSlide(new Plain, QIcon("style/plain.png"), tr("Text"));
    addHoroscopeControls();

    // Connect Speculum orb changes to Plain widget refresh
    if (auto speculum = findDockHdlr<Speculum>()) {
        if (auto plain = findSlide<Plain>()) {
            connect(speculum,
                    &Speculum::orbSettingChanged,
                    plain,
                    [plain](double orbDegrees) {
                        // Update the plain widget's paranOrb and trigger
                        // refresh
                        QMetaObject::invokeMethod(plain,
                                                  "setParanOrb",
                                                  Qt::QueuedConnection,
                                                  Q_ARG(double, orbDegrees));
                    });
        }
    }

    connect(fileView, SIGNAL(clicked()), this, SLOT(openEditor()));
    connect(fileView2nd, SIGNAL(clicked()), this, SLOT(openEditor()));
    connect(slides,
            SIGNAL(currentSlideChanged()),
            this,
            SLOT(currentSlideChanged()));
}

void
AstroWidget::setupFile(AstroFile* file, bool suspendUpdate)
{
    if (!file) return;
    bool hasChanges = file->hasUnsavedChanges();
    file->suspendUpdate();

    if (file->getGMT()
        == QDateTime::fromSecsSinceEpoch(0)) // set current date, time, timezone
    {
        QDateTime current    = QDateTime::currentDateTime();
        QTimeZone tz         = current.timeZone();
        QDateTime currentUTC = QDateTime(current.toUTC().date(),
                                         current.toUTC().time(),
                                         QTimeZone::UTC);
        file->setGMT(currentUTC);
        file->setTimezone(tz.offsetFromUtc(currentUTC) / 3600.);
    }

    if (file->getLocation().isNull()) // set default location
    {
        file->setLocation(geoWdg->location());
        file->setLocationName(geoWdg->locationName());
    }

    file->setZodiac(zodiacSelector->itemData(zodiacSelector->currentIndex())
                        .toInt()); // set zodiac
    file->setHouseSystem(
        hsystemSelector->itemData(hsystemSelector->currentIndex())
            .toInt()); // set house system
    auto aset =
        aspectsSelector->itemData(aspectsSelector->currentIndex()).toInt();
    file->setAspectSet(aset, _dynAspChange); // set aspect set
    file->setAspectMode(
        A::aspectModeEnum(aspectModeSelector->currentIndex())); // aspect mode
    auto val = harmonicSelector->currentText();
    bool ok  = false;
    auto h   = val.toDouble(&ok);
    if (!ok) {
        auto ops = val.split(QRegularExpression("\\s*\\*\\s*"));
        if (ops.size() >= 2) {
            double v = 1;
            for (auto m : ops) {
                auto mv = m.toDouble(&ok);
                if (ok) v *= mv;
                else
                    break;
            }
            if (ok) file->setHarmonic(v);
        } else {
            ops = val.split(QRegularExpression("\\s*/\\s*"));
            if (ops.size() >= 2) {
                double v = ops.takeFirst().toDouble(&ok);
                for (auto m : ops) {
                    auto mv = m.toDouble(&ok);
                    if (std::abs(mv) <= std::numeric_limits<double>::epsilon())
                    {
                        ok = false;
                        break;
                    }
                    if (ok) v /= mv;
                    else
                        break;
                }
                if (ok) file->setHarmonic(v);
            }
        }
    } else
        file->setHarmonic(h);

    if (!hasChanges) file->clearUnsavedState();
    if (!suspendUpdate) file->resumeUpdate();

    connect(file, SIGNAL(destroyRequested()), this, SLOT(destroyingFile()));
}

void
AstroWidget::switchToSingleAspectSet()
{
#if 1
    A::setOrbFactor(1);
#else
    aspectsSelector->blockSignals(true);
    A::AspectSetId set =
        aspectsSelector->itemData(aspectsSelector->currentIndex()).toInt();
    A::AspectSetId set2      = sqrtf(set);
    int            itemIndex = aspectsSelector->findData(set2);
    if (set2 * set2 == set && itemIndex >= 0) {
        qDebug() << "AstroWidget::restore aspect set to single";
        aspectsSelector->setCurrentIndex(itemIndex);
    }
    aspectsSelector->blockSignals(false);
#endif
}

void
AstroWidget::switchToSynastryAspectSet()
{
#if 1
    A::setOrbFactor(.25);
#else
    aspectsSelector->blockSignals(true);
    A::AspectSetId set =
        aspectsSelector->itemData(aspectsSelector->currentIndex()).toInt();
    A::AspectSetId set2      = set * set;
    int            itemIndex = aspectsSelector->findData(set2);
    if (itemIndex >= 0) {
        qDebug() << "AstroWidget::replace aspect set to synastry";
        aspectsSelector->setCurrentIndex(itemIndex);
    }
    aspectsSelector->blockSignals(false);
#endif
}

bool
AstroWidget::eventFilter(QObject* obj, QEvent* ev)
{
    bool rel    = false;
    auto evtype = ev->type();
    if (!(rel = (evtype == QEvent::MouseButtonRelease))
        && evtype != QEvent::MouseButtonPress)
    {
        return false;
    }

    auto mev = static_cast<QMouseEvent*>(ev);
    auto w   = qobject_cast<QWidget*>(obj);
    auto pt  = w->mapTo(dynAspectControls, mev->pos());
    qDebug() << "filtering click at" << pt;

    auto act = dynAspectControls->actionAt(pt);
    if (!act) return false;

    auto mods = QApplication::keyboardModifiers();
    bool ctrl = (mods & Qt::ControlModifier);
    bool alt  = (mods & Qt::AltModifier);
    if (ctrl == alt) return false; // must be either/or

    qDebug() << (ctrl ? "ctrl" : "alt")
             << (evtype == QEvent::MouseButtonPress ? "pressed" : "released")
             << "with" << act->objectName();

    bool              any = false;
    A::modalize<bool> change(_dynAspChange, true);

    if (ctrl) {
        if (!rel) {
            _clickedHarmonic = act;
            return true; // process on release
        }

        if (_clickedHarmonic == act || !_clickedHarmonic) {
            bool check = !act->isChecked();
            for (auto other : dynAspectControls->actions()) {
                if (act == other || other->isChecked() == check) continue;
                any = true;
                other->toggle();
            }
        } else {
            bool check    = !_clickedHarmonic->isChecked();
            auto clickedH = _clickedHarmonic->property("harmonic").toUInt();
            auto actH     = act->property("harmonic").toUInt();
            auto range    = std::minmax(clickedH, actH);
            for (auto other : dynAspectControls->actions()) {
                auto h = other->property("harmonic").toUInt();
                if (h < range.first || h > range.second) continue;
                if (other->isChecked() == check) continue;
                any = true;
                other->toggle();
            }
        }
    } else { // alt
        if (!rel) return true;

        bool check = !act->isChecked();
        auto h     = act->property("harmonic").toUInt();
        for (auto other : dynAspectControls->actions()) {
            auto oh = other->property("harmonic").toUInt();
            if ((oh >= h) && (oh % h == 0) && other->isChecked() != check) {
                any = true;
                other->toggle();
            }
        }
    }
    _clickedHarmonic = nullptr;
    if (any) horoscopeControlChanged();

    return true;
}

void
AstroWidget::setFiles(const AstroFileList& files)
{
    if (files.count() == 2) {
        switchToSynastryAspectSet();
    } else if (files.count() == 1) {
        switchToSingleAspectSet();
    }

    for (AstroFile* i : files) setupFile(i, true /*suspendUpdate*/);

    fileView->setFiles(files);
    fileView2nd->setFiles(files);
    if (editor) editor->setFiles(files);

    for (AstroFileHandler* h : handlers) h->setFiles(files);

    for (AstroFile* i : files) i->resumeUpdate();

    fileView->resumeUpdate();
    fileView2nd->resumeUpdate();
    if (editor) editor->resumeUpdate();

    for (AstroFileHandler* h : handlers) {
        if (h->isVisible()) h->resumeUpdate();
    }

    // Update visibility of second file widget based on file count
    fileView2nd->setVisible(files.count() > 1);
}

void
AstroWidget::openEditor()
{
    if (editor) {
        editor->raise();
    } else {
        editor = new AstroFileEditor();
        editor->setFiles(files());
        editor->move((topLevelWidget()->width() - editor->width()) / 2
                         + topLevelWidget()->geometry().left(),
                     (topLevelWidget()->height() - editor->height()) / 2
                         + topLevelWidget()->geometry().top());
        editor->show();
        connect(editor,
                SIGNAL(appendFile()),
                this,
                SIGNAL(appendFileRequested()));
        connect(editor,
                SIGNAL(swapFiles(int, int)),
                this,
                SIGNAL(swapFilesRequested(int, int)));
        connect(editor, SIGNAL(windowClosed()), this, SLOT(destroyEditor()));
    }

    if (sender() == fileView) editor->setCurrentFile(0);
    else if (sender() == fileView2nd)
        editor->setCurrentFile(1);
}

void
AstroWidget::setHarmonic(double h)
{
    QString ns = QString::number(h);
    int     i  = harmonicSelector->findText(ns);
    if (i == -1) {
        harmonicSelector->addItem(ns);
        i = harmonicSelector->findText(ns);
    }
    if (i != -1) {
        harmonicSelector->setCurrentIndex(i);
    }
}

void
AstroWidget::destroyingFile()
{
    if (auto file = qobject_cast<AstroFile*>(sender())) {
        if (!files().contains(file) || files().count() > 2) return;
        switchToSingleAspectSet();
    }
}

void
AstroWidget::destroyEditor()
{
    editor->deleteLater();
    editor = nullptr;
}

void
AstroWidget::addSlide(AstroFileHandler* w, const QIcon& icon, QString title)
{
    // qDebug() << "added slide" << w << title;
    QAction* act =
        toolBar->addAction(icon, title, this, SLOT(toolBarActionClicked()));
    act->setCheckable(true);
    act->setActionGroup(actionGroup);
    if (!slides->count()) act->setChecked(true);
    slides->addSlide(w);
    attachHandler(w);
#if 0
    if (w->metaObject()->indexOfSignal(SIGNAL(planetSelected(A::PlanetId, int))) == -1)
        return;
#endif

    for (AstroFileHandler* wdg : dockHandlers) {
        // qDebug() << w << "connected planetSelected() to" << wdg;
        connect(w,
                SIGNAL(planetSelected(A::PlanetId, int)),
                wdg,
                SLOT(setCurrentPlanet(A::PlanetId, int)));
    }

    for (QDockWidget* d : docks) {
        // qDebug() << w << "connected planetSelected() to dock" << d;
        connect(w, SIGNAL(planetSelected(A::PlanetId, int)), d, SLOT(show()));
    }
}

void
AstroWidget::addDockWidget(AstroFileHandler* w,
                           QString           title,
                           bool              scrollable,
                           QString           objectName)
{
    if (objectName.isEmpty()) objectName = w->metaObject()->className();
    QDockWidget* dock = new QDockWidget(title);
    dock->setObjectName(objectName);

    if (scrollable) {
        QScrollArea* area = new QScrollArea;
        area->setWidget(w);
        area->setWidgetResizable(true);
        dock->setWidget(area);
    } else {
        dock->setWidget(w);
    }
    for (QDockWidget* d : docks) {
        QWidget* o = d->widget();
        if (o->inherits("QScrollArea")) {
            o = qobject_cast<QScrollArea*>(o)->widget();
        }
        connect(o,
                SIGNAL(planetSelected(A::PlanetId, int)),
                w,
                SLOT(setCurrentPlanet(A::PlanetId, int)));
        connect(w,
                SIGNAL(planetSelected(A::PlanetId, int)),
                o,
                SLOT(setCurrentPlanet(A::PlanetId, int)));
    }
    docks << dock;
    dockHandlers << w;
    attachHandler(w);
}

void
AstroWidget::attachHandler(AstroFileHandler* w)
{
    handlers << w;
    connect(w,
            SIGNAL(requestHelp(QString)),
            this,
            SIGNAL(helpRequested(QString)));
}

void
AstroWidget::addHoroscopeControls()
{
    zodiacSelector = new QComboBox;
    zodiacSelector->setObjectName("zodiacSelector");
    hsystemSelector = new QComboBox;
    hsystemSelector->setObjectName("hsystemSelector");
    aspectsSelector = new QComboBox;
    aspectsSelector->setObjectName("aspectsSelector");
    aspectModeSelector = new QComboBox;
    aspectModeSelector->setObjectName("aspectModeSelector");
    harmonicSelector = new QComboBox;
    harmonicSelector->setObjectName("harmonicSelector");
    harmonicSelector->setMinimumWidth(100);
    harmonicSelector->setValidator(new QDoubleValidator(1, 360 * 360, 4, this));

    zodiacSelector->setToolTip(tr("Sign"));
    hsystemSelector->setToolTip(tr("House system"));
    aspectsSelector->setToolTip(tr("Aspect sets\n(by A.Podvodny)"));
    aspectModeSelector->setToolTip(tr("Aspect computation"));
    harmonicSelector->setToolTip("Harmonic");

    // create combo box with zodiacs
    for (const A::Zodiac& z : A::getZodiacs())
        zodiacSelector->addItem(z.name, z.id);

    // create combo box with house systems
    for (const A::HouseSystem& sys : A::getHouseSystems())
        hsystemSelector->addItem(sys.name, sys.id);

    // create combo box with aspect sets
    for (const A::AspectsSet& s : A::getAspectSets())
        aspectsSelector->addItem(s.name, s.id);

    A::AspectSetId daspId = -1;
    for (auto&& as : A::getAspectSets()) {
        if (as.name.startsWith("Dynamic")) {
            daspId = as.id;
        }
    }
    dynAspectControls = new QToolBar();
    dynAspectControls->setObjectName("dynAspectControls");

    auto& dasps = A::getAspectSet(daspId);
    auto  dit   = dasps.aspects.begin();
    for (unsigned i = 1; i <= 32; ++i) {
        auto num = QString::number(i);
        auto act = dynAspectControls->addAction(num);
        act->setObjectName("hasp" + num);
        act->setCheckable(true);
        act->setChecked(A::dynAspState(i));
        act->setProperty("harmonic", i);

        auto facs = A::getPrimeFactors(i);
        if (facs.size() > 1) {
            QStringList sl;
            for (auto u : facs) sl << QString::number(u);
            sl = QStringList() << sl.join("x");
            while (dit != dasps.aspects.end() && dit->_harmonic != i) {
                ++dit;
            }
            while (dit != dasps.aspects.end() && dit->_harmonic == i) {
                auto& asp = *dit++;
                asp.setEnabled(A::dynAspState(i));
                sl << asp.name;
            }
            act->setToolTip(sl.join("<br>"));
        }

        auto btn = dynAspectControls->widgetForAction(act);
        btn->setObjectName("hasp" + num + "btn");
        btn->installEventFilter(this);
        QColor clr = A::getHarmonicColor(i);
        double luma =
            0.2126 * clr.redF() + 0.7152 * clr.greenF() + 0.0722 * clr.blueF();
        bool   useBlack = (luma > 0.5);
        QColor darker   = clr.darker();
        auto   style =
            QString("QToolButton:checked { background-color: %1; color: %2; "
                    "font: bold; }"
                    "QToolButton { background-color: %3; color: %4; }")
                .arg(clr.name(QColor::HexArgb))
                .arg((useBlack ? "black" : "white"))
                .arg(darker.name(QColor::HexArgb))
                .arg((useBlack ? "darkgray" : "lightgray"));
        btn->setStyleSheet(style);
        btn->setMaximumWidth(20);

        connect(act, &QAction::toggled, [this, &dasps, i](bool b) {
            // bool b = act->isChecked();
            if (A::dynAspState(i) == b) return;

            A::setDynAspState(i, b);
            bool seen = false;
            for (auto aid : dasps.aspects.keys()) {
                A::AspectType& asp = dasps.aspects[aid];
                if (asp._harmonic == i) {
                    seen = true;
                    asp.setEnabled(b);
                } else if (seen)
                    break; // assumes they're bunched...
#if 0
                if (!b) {
                    if (asp._harmonic % i == 0) asp.setEnabled(false);
                    continue;
                }
                bool foundAll = true;
                for (auto h : asp.factors) {
                    foundAll = A::dynAspState(h);
                    if (!foundAll) break;
                }
                if (foundAll) asp.setEnabled(true);
#endif
            }
            if (_dynAspChange) return;
            A::modalize<bool> change(_dynAspChange, true);
            horoscopeControlChanged();
        });
        // act->dumpObjectInfo();
    }

    QStringList ssl {
        "QToolBar { padding: 0px; }",
        "QToolBar#dynAspectControls QToolButton { padding: 0px; margin: 0px; "
        "border-width: 0px; max-width: 45px; min-width: 15px; }",
    };

    dynAspectControls->setStyleSheet(ssl.join(" "));
#if 0
    qDebug() << ssh;
    dynAspectControls->dumpObjectInfo();
    dynAspectControls->dumpObjectTree();
#endif

    for (unsigned i = A::amcGreatCircle; i < A::amcEND; ++i) {
        aspectModeSelector->addItem(A::aspectModeType::toUserString(i), int(i));
    }

    for (int i = 1; i <= 16; ++i) {
        harmonicSelector->addItem(QString::number(i));
    }

    horoscopeControls << zodiacSelector << hsystemSelector << aspectsSelector
                      << aspectModeSelector << harmonicSelector;

    harmonicSelector->setEditable(true);

    for (QWidget* w : horoscopeControls) {
        if (auto c = qobject_cast<QComboBox*>(w)) {
            c->setEditable(c == harmonicSelector);
            connect(c,
                    SIGNAL(currentIndexChanged(int)),
                    this,
                    SLOT(horoscopeControlChanged()));
        }
    }
}

void
AstroWidget::toolBarActionClicked()
{
    QAction* s = static_cast<QAction*>(sender());
    int      i = toolBar->actions().indexOf(s);
    slides->setSlide(i);
}

void
AstroWidget::currentSlideChanged()
{
    // Show second file widget when there are 2 files loaded
    fileView2nd->setVisible(fileView->filesCount() > 1);
}

void
AstroWidget::applyGeoSettings(AppSettings& s)
{
    s.setValue("Scope/defaultLocation", vectorToString(geoWdg->location()));
    s.setValue("Scope/defaultLocationName", geoWdg->locationName());
}

QString
AstroWidget::vectorToString(const QVector3D& v)
{
    return QString("%1 %2 %3").arg(v.x()).arg(v.y()).arg(v.z());
}

QVector3D
AstroWidget::vectorFromString(const QString& str)
{
    QVector3D ret;
    ret.setX(str.section(" ", 0, 0).toFloat());
    ret.setY(str.section(" ", 1, 1).toFloat());
    ret.setZ(str.section(" ", 2, 2).toFloat());
    return ret;
}

void
AstroWidget::horoscopeControlChanged()
{
    for (AstroFile* i : files()) setupFile(i, true);

    for (AstroFile* i : files()) i->resumeUpdate();
}

void
AstroWidget::aspectSelectionChanged()
{
}

AppSettings
AstroWidget::defaultSettings()
{
    AppSettings s;

    s << fileView->defaultSettings();

    for (AstroFileHandler* h : handlers) s << h->defaultSettings();

    s.setValue("Scope/defaultLocation", "37.6184 55.7512 0");
    s.setValue("Scope/defaultLocationName", "Moscow, Russia");
    s.setValue("Scope/zodiac",
               0); // indexes of ComboBox items, not values itself
    s.setValue("Scope/houseSystem", 0);
    s.setValue("Scope/aspectSet", 0);
    s.setValue("Scope/dynamic", "all");
    s.setValue("Scope/aspectMode", 1); // ecliptic
    s.setValue("slide",
               slides->currentIndex()); // чтобы не возвращалась к первому
                                        // слайду после сброса настроек
    s.setValue("harmonic", 1);
    return s;
}

AppSettings
AstroWidget::currentSettings()
{
    AppSettings s;

    s << fileView->currentSettings();

    for (AstroFileHandler* h : handlers) s << h->currentSettings();

    applyGeoSettings(s);

    s.setValue("Scope/zodiac", zodiacSelector->currentIndex());
    s.setValue("Scope/houseSystem", hsystemSelector->currentIndex());
    s.setValue("Scope/aspectSet", aspectsSelector->currentIndex());
    s.setValue("Scope/aspectMode", aspectModeSelector->currentIndex());

    QVariant var;
    A::getDynAspState(var);
    s.setValue("Scope/dynamic", var);

    s.setValue("harmonic", harmonicSelector->currentText().toDouble());
    s.setValue("slide", slides->currentIndex());
    return s;
}

void
AstroWidget::applySettings(const AppSettings& s)
{
    geoWdg->setLocation(
        vectorFromString(s.value("Scope/defaultLocation").toString()));
    geoWdg->setLocationName(s.value("Scope/defaultLocationName").toString());

    zodiacSelector->setCurrentIndex(s.value("Scope/zodiac").toInt());
    hsystemSelector->setCurrentIndex(s.value("Scope/houseSystem").toInt());
    aspectsSelector->setCurrentIndex(s.value("Scope/aspectSet").toInt());
    aspectModeSelector->setCurrentIndex(s.value("Scope/aspectMode").toInt());

    QString harm  = s.value("harmonic", 1).toString();
    int     index = harmonicSelector->findText(harm);
    if (index == -1) {
        harmonicSelector->addItem(harm);
        index = harmonicSelector->findText(harm);
    }
    harmonicSelector->setCurrentIndex(index);

    if (auto dactrls = getDynAspectControls()) {
        A::AspectSetId daspId = -1;
        for (auto&& as : A::getAspectSets()) {
            if (as.name.startsWith("Dynamic")) {
                daspId = as.id;
            }
        }

        auto& dasps = A::getAspectSet(daspId);
        auto  dit   = dasps.aspects.begin();

        A::modalize<bool> inChange(_dynAspChange, true);
        A::setDynAspState(s.value("Scope/dynamic"));
        for (unsigned i = 1; i <= 32; ++i) {
            if (auto act =
                    dactrls->findChild<QAction*>(QString("hasp%1").arg(i)))
            {
                bool b = A::dynAspState(i);

                while (dit != dasps.aspects.end() && dit->_harmonic != i) {
                    ++dit;
                }
                while (dit != dasps.aspects.end() && dit->_harmonic == i) {
                    (*dit++).setEnabled(b);
                }

                if (act->isChecked() != b) {
                    act->toggle();
                }
            }
        }
    }

    slides->setSlide(s.value("slide").toInt());
    toolBar->actions()[slides->currentIndex()]->setChecked(true);

    fileView->applySettings(s);
    fileView2nd->applySettings(s);

    for (AstroFile* i : files()) setupFile(i);

    for (AstroFileHandler* h : handlers) h->applySettings(s);
}

void
AstroWidget::setupSettingsEditor(AppSettingsEditor* ed)
{
    // ed->addTab(tr("Data"));

    fileView->setupSettingsEditor(ed);
    ed->addCustomWidget(geoWdg,
                        tr("Default location:"),
                        SIGNAL(locationChanged()));

    for (AstroFileHandler* h : handlers) h->setupSettingsEditor(ed);

    connect(ed,
            SIGNAL(apply(AppSettings&)),
            this,
            SLOT(applyGeoSettings(AppSettings&)));
}

/* =========================== ASTRO FILE DATABASE
 * ================================== */

AstroDatabase::AstroDatabase(QWidget* parent /*=nullptr*/) : QFrame(parent)
{
    QPushButton* refresh = new QPushButton;

    fswatch = new QFileSystemWatcher(this);
    connect(fswatch, SIGNAL(directoryChanged()), this, SLOT(updateList()));
    connect(this,
            SIGNAL(fileRemoved(const AFileInfo&)),
            this,
            SLOT(updateList()));

    dirModel = new QStandardItemModel(this);

    for (const auto& name : AstroFile::fixedChartDirMapKeys()) {
        auto dir   = AstroFile::fixedChartDirMap().value(name);
        auto dirit = new QStandardItem(name);
        dirit->setData(dir);
        dirit->setData(dirType, TypeRole);
        dirit->setData(dir, Qt::ToolTipRole);

        QFont f = dirit->data(Qt::FontRole).value<QFont>();
        f.setBold(true);
        dirit->setData(f, Qt::FontRole);

        dirit->setFlags(Qt::ItemIsEnabled);

        fswatch->addPath(dir);
        dirModel->appendRow(dirit);
    }

    searchProxy = new QSortFilterProxyModel(this);
    searchProxy->setRecursiveFilteringEnabled(true);

    fileList = new QTreeView;
    searchProxy->setSourceModel(dirModel);
    fileList->setModel(searchProxy);

    search = new QLineEdit;

    refresh->setIcon(QIcon("style/update.png"));
    refresh->setToolTip(tr("Refresh"));
    refresh->setCursor(Qt::PointingHandCursor);

    fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileList->viewport()->installEventFilter(
        this); // for handling middle mouse button clicks
    fileList->header()->hide();

    search->setPlaceholderText(tr("Search"));
    setMinimumWidth(200);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setWindowTitle(tr("Database"));
    setWindowFlags(Qt::WindowStaysOnTopHint);

    QHBoxLayout* l = new QHBoxLayout;
    l->addWidget(search);
    l->addWidget(refresh);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(l);
    layout->addWidget(fileList);

    connect(refresh, SIGNAL(clicked()), this, SLOT(updateList()));
    connect(this,
            SIGNAL(customContextMenuRequested(QPoint)),
            this,
            SLOT(showContextMenu(QPoint)));
    connect(fileList,
            SIGNAL(doubleClicked(QModelIndex)),
            this,
            SLOT(openSelected()));
    connect(search,
            SIGNAL(textChanged(QString)),
            this,
            SLOT(searchFilter(QString)));

    updateList();
}

void
AstroDatabase::saveDatabaseState()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.beginGroup("Database");
    
    // Save scroll position
    QScrollBar* vbar = fileList->verticalScrollBar();
    if (vbar) {
        settings.setValue("scrollPosition", vbar->value());
    }
    
    // Save expanded state for each directory
    for (int i = 0; i < dirModel->rowCount(); ++i) {
        QStandardItem* item = dirModel->item(i);
        if (item) {
            QString dirName = item->text();
            QModelIndex proxyIndex = searchProxy->mapFromSource(dirModel->indexFromItem(item));
            bool expanded = fileList->isExpanded(proxyIndex);
            settings.setValue(QString("expanded/%1").arg(dirName), expanded);
        }
    }
    
    settings.endGroup();
    qDebug() << "Database state saved";
}

void
AstroDatabase::restoreDatabaseState()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.beginGroup("Database");
    
    // Restore expanded state for each directory
    for (int i = 0; i < dirModel->rowCount(); ++i) {
        QStandardItem* item = dirModel->item(i);
        if (item) {
            QString dirName = item->text();
            bool expanded = settings.value(QString("expanded/%1").arg(dirName), false).toBool();
            if (expanded) {
                QModelIndex proxyIndex = searchProxy->mapFromSource(dirModel->indexFromItem(item));
                fileList->setExpanded(proxyIndex, true);
            }
        }
    }
    
    // Restore scroll position (must be done after expanding)
    QScrollBar* vbar = fileList->verticalScrollBar();
    if (vbar) {
        int scrollPos = settings.value("scrollPosition", 0).toInt();
        vbar->setValue(scrollPos);
    }
    
    settings.endGroup();
    qDebug() << "Database state restored";
}

void
AstroDatabase::searchFilter(const QString& nf)
{
    searchProxy->setFilterRegularExpression(nf);
}

AFileInfoList
getSelectedItems(QTreeView* tv)
{
    QItemSelectionModel* sm = tv->selectionModel();
    if (!sm) return {};

    auto sfpModel = qobject_cast<QSortFilterProxyModel*>(tv->model());
    auto dirModel = qobject_cast<QStandardItemModel*>(
        sfpModel ? sfpModel->sourceModel() : tv->model());
    if (!dirModel) return {};

    AFileInfoList sel;
    for (const auto& mi : sm->selectedIndexes()) {
        auto dmi  = sfpModel->mapToSource(mi);
        auto pmi  = dmi.parent();
        auto sit  = dmi.data().toString();
        auto item = dirModel->itemFromIndex(dmi);
        if (auto pitem = item ? item->parent() : nullptr) {
            sel << AFileInfo(pitem->data().toString(), sit);
        }
    }
    return sel;
}

bool
hasSelectedItems(QTreeView* tv)
{
    QItemSelectionModel* sm = tv->selectionModel();
    if (!sm) return false;
    return (sm->hasSelection());
}

void
AstroDatabase::updateList()
{
    QMap<QStandardItem*, QStringList> sel;
    QItemSelectionModel*              sm = fileList->selectionModel();
    if (sm) {
        for (const auto& mi : sm->selectedIndexes()) {
            auto sit  = mi.data().toString();
            auto item = dirModel->itemFromIndex(mi.parent());
            sel[item].append(sit);
        }
    }

    QItemSelection sl;

    std::function<void(QModelIndex)> updir = [&](QModelIndex mi) {
        auto diritem = dirModel->itemFromIndex(mi);
        QDir dir(diritem->data().toString());
        diritem->removeRows(0, diritem->rowCount());

        for (const auto& dn : dir.entryList(QDir::Dirs)) {
            if (dn == "." || dn == "..") continue;

            QFileInfo fi(dir, dn);
            auto      subdirname = AFileInfo::decodeName(dn);
            auto      subdiritem = new QStandardItem(subdirname);
            subdiritem->setData(dirType, TypeRole);
            subdiritem->setData(fi.absoluteFilePath(), PathRole);
            subdiritem->setData(fi.absoluteFilePath(), Qt::ToolTipRole);
            subdiritem->setFlags(Qt::ItemIsEnabled);
            QFont f = subdiritem->data(Qt::FontRole).value<QFont>();
            f.setBold(true);
            subdiritem->setData(f, Qt::FontRole);

            int n = diritem->rowCount();
            diritem->appendRow(subdiritem);

            QModelIndex sdmi = diritem->child(n)->index();
            updir(sdmi);
        }

        QStringList list = dir.entryList(AFileInfo::wildcard(),
                                         QDir::Files,
                                         QDir::Name | QDir::IgnoreCase);
        for (QString& fn : list) {
            fn.replace(AFileInfo::suff(), "");
            fn = AFileInfo::decodeName(fn);
        }
        list.sort();

        const QStringList& presel = sel[diritem];
        int                j      = 0;
        for (const QString& chit : list) {
            auto child = new QStandardItem(chit);
            child->setData(fileType, TypeRole);
            child->setData(chit, Qt::ToolTipRole);
            child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            diritem->appendRow(child);
            while (j < presel.count() && presel.at(j) < chit) ++j;
            if (j < presel.count() && presel.at(j) == chit) {
                QModelIndex qmi =
                    dirModel->index(diritem->rowCount() - 1, 0, mi);
                sl.select(qmi, qmi);
            }
        }
    };

    for (int i = 0, n = dirModel->rowCount(); i < n; ++i) {
        auto mi = dirModel->index(i, 0);
        updir(mi);
    }
    if (!sl.empty()) sm->select(sl, QItemSelectionModel::ClearAndSelect);
}

void
AstroDatabase::deleteSelected()
{
    auto sm = fileList->selectionModel();
    if (!sm) return;

    auto sil   = sm->selectedIndexes();
    int  count = sil.count();
    if (!count) return;

    QMessageBox msgBox;
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);

    if (count == 1)
        msgBox.setText(
            tr("Delete '%1' from list?").arg(sil.first().data().toString()));
    else
        msgBox.setText(tr("Delete %1 files from list?").arg(count));

    int ret = msgBox.exec();
    if (ret == QMessageBox::Cancel) return;

    bool any = false;
    for (const auto& mi : sil) {
        if (!mi.parent().isValid()) continue;
        auto        dir  = mi.parent().data(Qt::UserRole + 1).toString();
        const auto& chit = mi.data().toString();
        QString     file = AFileInfo(dir, chit).canonicalFilePath();
        // fswatch->blockSignals(true);
        QFile::remove(file);
        // fswatch->blockSignals(false);
        any = true;
#if 0
        auto item = dirModel->itemFromIndex(mi);
        auto parent = item->parent();
        parent->removeRow(mi.row());
#endif
    }
    if (any) emit fileRemoved(AFileInfo());
}

void
AstroDatabase::openSelected()
{
    auto sfi = getSelectedItems(fileList);
    if (sfi.empty()) return;

    auto count = sfi.count();
    if (count == 1) {
        emit openFile(sfi.first());
    } else {
        for (const auto& fi : sfi) {
            emit openFileInNewTab(fi);
        }
    }
}

void
AstroDatabase::openSelectedInNewTab()
{
    for (const auto& fi : getSelectedItems(fileList)) {
        emit openFileInNewTab(fi);
    }
}

void
AstroDatabase::openSelectedWithTransits()
{
    for (const auto& fi : getSelectedItems(fileList))
        emit openFileInNewTabWithTransits(fi);
}

void
AstroDatabase::openSelectedWithProgressions()
{
    for (const auto& fi : getSelectedItems(fileList))
        emit openFileInNewTabWithProgressions(fi);
}

void
AstroDatabase::openSelectedAsSecond()
{
    auto sfi = getSelectedItems(fileList);
    if (!sfi.empty()) emit openFileAsSecond(sfi.first());
}

void
AstroDatabase::openSelectedComposite()
{
    emit openFilesComposite(getSelectedItems(fileList));
}

void
AstroDatabase::findSelectedDerived()
{
    for (const auto& fi : getSelectedItems(fileList)) {
        emit findSelectedDerived(fi);
        break;
    }
}

void
AstroDatabase::openSelectedWithSolarReturn()
{
    for (const auto& fi : getSelectedItems(fileList)) {
        emit openFileInNewTabWithReturn(fi);
    }
}

void
AstroDatabase::openSelectedSolarReturnInNewTab()
{
    for (const auto& fi : getSelectedItems(fileList)) {
        emit openFileReturn(fi);
    }
}

void
AstroDatabase::showContextMenu(QPoint p)
{
    if (!hasSelectedItems(fileList)) return;

    QPoint pos = ((QWidget*) sender())->mapToGlobal(p);

    p        = fileList->mapFromGlobal(pos);
    auto qmi = fileList->indexAt(p);
    if (!qmi.isValid()) return;

    qDebug() << qmi << qmi.data() << qmi.data(TypeRole);
    qmi = searchProxy->mapToSource(qmi);
    qDebug() << qmi;

    auto item = dirModel->itemFromIndex(qmi);
    if (!item) return;

    auto type = entryType(item->data(TypeRole).toUInt());
    if (type != dirType && type != fileType) return;

    QMenu* mnu = new QMenu(this);

    if (type == dirType) {
        mnu->addAction(tr("Save here"), [&] { saveCurrent(qmi); });
        mnu->addAction(tr("New directory..."), [&] { newDirectory(qmi); });
    } else if (type == fileType) {
        auto getOpener = [&](const QString& name) {
            return [&, name] {
                for (const auto& fi : getSelectedItems(fileList)) {
                    emit openFileReturn(fi, name);
                }
            };
        };

        mnu->addAction(tr("Open"), this, SLOT(openSelected()));
        mnu->addAction(tr("Open in new tab"),
                       this,
                       SLOT(openSelectedInNewTab()));
        mnu->addAction(tr("Open with Transits"),
                       this,
                       SLOT(openSelectedWithTransits()));
        mnu->addAction(tr("Open with Progressions"),
                       this,
                       SLOT(openSelectedWithProgressions()));
        mnu->addAction(tr("Synastry view"), this, SLOT(openSelectedAsSecond()));

        mnu->addAction(tr("Composite"), this, SLOT(openSelectedComposite()));
        mnu->addAction(tr("Open Derived..."),
                       this,
                       SLOT(findSelectedDerived()));

        mnu->addSeparator();
        mnu->addAction(tr("Open with Solar Return"),
                       this,
                       SLOT(openSelectedWithSolarReturn()));
        auto smnu = mnu->addMenu("Open Return in new tab");
        smnu->addAction(tr("Solar"),
                        this,
                        SLOT(openSelectedSolarReturnInNewTab()));
        smnu->addAction(tr("Lunar"), getOpener("Moon"));
        smnu->addAction(tr("Mercury"), getOpener("Mercury"));
        smnu->addAction(tr("Venus"), getOpener("Venus"));
        smnu->addAction(tr("Mars"), getOpener("Mars"));
        smnu->addAction(tr("Jupiter"), getOpener("Jupiter"));
        smnu->addAction(tr("Saturn"), getOpener("Saturn"));
        smnu->addAction(tr("Uranus"), getOpener("Uranus"));
        smnu->addAction(tr("Neptune"), getOpener("Neptune"));
        smnu->addAction(tr("Pluto"), getOpener("Pluto"));
        smnu->addAction(tr("Chiron"), getOpener("Chiron"));

        mnu->addSeparator();
        mnu->addAction(QIcon("style/delete.png"),
                       tr("Delete"),
                       this,
                       SLOT(deleteSelected()));
        smnu->deleteLater();
    }

    mnu->exec(pos);
    mnu->deleteLater();
}

void
AstroDatabase::saveCurrent(const QModelIndex& qmi)
{
}

void
AstroDatabase::newDirectory(const QModelIndex& qmi)
{
}

void
AstroDatabase::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Delete) deleteSelected();
}

bool
AstroDatabase::eventFilter(QObject* o, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonRelease
        && ((QMouseEvent*) e)->button() == Qt::MiddleButton)
    {
        openSelectedInNewTab();
        return true;
    }
    return QObject::eventFilter(o, e);
}

/* =========================== FILES BAR
 * ============================================ */

FilesBar::FilesBar(QWidget* parent) : QTabBar(parent)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    connect(this, SIGNAL(tabMoved(int, int)), this, SLOT(swapTabs(int, int)));
    connect(this, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));
}

int
FilesBar::getTabIndex(AstroFile* f, bool seekFirstFileOnly)
{
    for (int i = 0; i < count(); i++)
        for (int j = 0; j < (seekFirstFileOnly ? 1 : files[i].count()); j++)
            if (f == files[i][j]) return i;
    return -1;
}

int
FilesBar::getTabIndex(QString name, bool seekFirstFileOnly)
{
    for (int i = 0; i < count(); i++)
        for (int j = 0; j < (seekFirstFileOnly ? 1 : files[i].count()); j++)
            if (name == files[i][j]->getName()) return i;
    return -1;
}

void
FilesBar::addFile(AstroFile* file)
{
    if (!file) {
        qWarning() << "FilesBar::addFile: failed to add an empty file";
        return;
    }

    AstroFileList list { file };
    files << list;
    file->setParent(this);
    addTab("new");
    updateTab(count() - 1);
    setCurrentIndex(count() - 1);

    connect(file,
            SIGNAL(changed(AstroFile::Members)),
            this,
            SLOT(fileUpdated(AstroFile::Members)));
    connect(file, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
}

void
FilesBar::editNewChart()
{
    auto dlg = new QDialog();
    auto lay = new QVBoxLayout(dlg);
    dlg->setLayout(lay);
    auto pafe = new AstroFileEditor(dlg);
    auto f    = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(f);
    pafe->setFiles({ f });
    lay->addWidget(pafe);
    pafe->layout()->setContentsMargins(QMargins(0, 0, 0, 0));
    auto dbb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             dlg);
    lay->addWidget(dbb);

    dlg->adjustSize();
    dlg->move((topLevelWidget()->width() - dlg->width()) / 2
                  + topLevelWidget()->geometry().left(),
              (topLevelWidget()->height() - dlg->height()) / 2
                  + topLevelWidget()->geometry().top());

    auto aw = topLevelWidget()->findChild<AstroWidget*>();
    qDebug() << aw;
    connect(dbb, SIGNAL(accepted()), dlg, SLOT(accept()));
    connect(dbb, SIGNAL(rejected()), dlg, SLOT(reject()));
    connect(dlg, &QDialog::accepted, [this, pafe] {
        pafe->applyToFile();
        addFile(pafe->file());
    });
    connect(dlg, SIGNAL(rejected()), dlg, SLOT(destroyLater()));
    connect(dlg, SIGNAL(accepted()), dlg, SLOT(destroyLater()));
    dlg->open();
}

void
FilesBar::findChart()
{
    auto dlg = new QDialog(nullptr, Qt::Dialog | Qt::WindowStaysOnTopHint);
    auto lay = new QVBoxLayout(dlg);
    dlg->setLayout(lay);
    auto pafe = new AstroFileEditor(dlg);
    auto f    = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(f, true /*suspendUpdate*/);
    f->setType(TypeSearch);
    pafe->setFiles({ f });

    lay->addWidget(pafe);
    pafe->layout()->setContentsMargins(QMargins(0, 0, 0, 0));
    auto dbb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             dlg);
    auto ok = dbb->button(QDialogButtonBox::Ok);
    connect(pafe, SIGNAL(hasSelection(bool)), ok, SLOT(setEnabled(bool)));
    ok->setEnabled(false);

    lay->addWidget(dbb);

    dlg->adjustSize();
    dlg->move((topLevelWidget()->width() - dlg->width()) / 2
                  + topLevelWidget()->geometry().left(),
              (topLevelWidget()->height() - dlg->height()) / 2
                  + topLevelWidget()->geometry().top());

    auto aw = topLevelWidget()->findChild<AstroWidget*>();
    qDebug() << aw;
    connect(dbb, SIGNAL(accepted()), dlg, SLOT(accept()));
    connect(dbb, SIGNAL(rejected()), dlg, SLOT(reject()));
    connect(dlg, &QDialog::accepted, [this, pafe] {
        pafe->applyToFile();
        addFile(pafe->file());
    });
    connect(dlg, SIGNAL(rejected()), dlg, SLOT(deleteLater()));
    connect(dlg, SIGNAL(accepted()), dlg, SLOT(deleteLater()));
    dlg->open();
}

void
FilesBar::updateTab(int index)
{
    if (index >= count()) return;
    QStringList names;

    for (AstroFile* i : files[index])
        if (i) names << i->getName() + (i->hasUnsavedChanges() ? "*" : "");

    setTabText(index, names.join(" | "));
}

void
FilesBar::fileUpdated(AstroFile::Members m)
{
    if (!(m & (AstroFile::Name | AstroFile::ChangedState))) return;
    qDebug() << "FilesBar::updateTab";
    AstroFile* file = (AstroFile*) sender();
    auto       tab  = getTabIndex(file);
    if (tab == -1) {
        qDebug() << "  Couldn't find sender in tab index!";
    } else {
        updateTab(tab);
    }
}

void
FilesBar::fileDestroyed() // called when AstroFile going to be destroyed
{
    auto file = static_cast<AstroFile*>(sender());
    int  tab  = getTabIndex(file);
    if (tab == -1) return; // tab with the single file has been removed already
    int index = files[tab].indexOf(file);
    files[tab].removeAt(index);
    updateTab(tab);
    file->deleteLater();
}

void
FilesBar::swapTabs(int f1, int f2)
{
    AstroFileList temp = files[f1];
    files[f1]          = files[f2];
    files[f2]          = temp;
}

void
FilesBar::swapCurrentFiles(int i, int j)
{
    AstroFile* temp          = files[currentIndex()][i];
    files[currentIndex()][i] = files[currentIndex()][j];
    files[currentIndex()][j] = temp;
    updateTab(currentIndex());
    currentChanged(currentIndex());
}

bool
FilesBar::closeTab(int index)
{
    int next = -1;
    int curr = currentIndex();
    if (index < curr) next = curr - 1;

    AstroFileList f    = files[index];
    AstroFile*    file = nullptr;
    if (f.count()) file = f[0];

    if (askToSave && file && file->hasUnsavedChanges()) {
        QMessageBox msgBox;
        msgBox.setText(
            tr("Save changes in '%1' before closing?").arg(file->getName()));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No
                                  | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Save);
        int ret = msgBox.exec();

        switch (ret) {
        case QMessageBox::Yes:    file->save(); break;
        case QMessageBox::Cancel: return false;
        default:                  break;
        }
    }

    if (currentIndex() != index) {
        // If this is not the active tab, we need to make it
        // so, otherwise there can be a race condition when
        // the removeTab() call makes it active in a partial way.
        // Make active now, but strictly speaking we should
        // try to prevent recomputation with some sort of
        // "doomed" state.
        setCurrentIndex(index);
    }

    files.removeAt(index);
    static_cast<QTabBar*>(this)->removeTab(index);

    // delete AstroFiles, because we do not need it
    for (AstroFile* i : f) i->destroy();

    if (!count()) {
        // No tabs left - app will likely close soon
        qDebug() << "Last tab closed, no tabs remaining";
    } else if (next != -1) {
        setCurrentIndex(next);
        // QTimer::singleShot(0, [this] {setCurrentIndex(next);});
    }
    return true;
}

void
FilesBar::openFile(const AFileInfo& fi)
{
    int i = getTabIndex(fi.baseName(), true /*firstFileOnly*/);
    if (i != -1) {
        setCurrentIndex(i); // focus if the file is currently opened
        emit currentChanged(
            currentIndex()); // force update even if tab was already active
        return;
    }

    /*if (currentFile()->hasUnsavedChanges())
      openFileInNewTab(name);
    else*/
    currentFiles()[0]->load(fi);
    i = getTabIndex(fi.baseName());
    if (i != -1) updateTab(i);
}

void
FilesBar::openFile(AstroFile* af)
{
    auto i = currentIndex();
    if (i != -1) {
        files[currentIndex()][0] = af;
        updateTab(i);
    }
}

void
FilesBar::openFileInNewTab(const AFileInfo& fi)
{
    // int i = getTabIndex(name, true);
    // if (i != -1) return setCurrentIndex(i);

    AstroFile* file = new AstroFile;
    file->load(fi);
    addFile(file);
}

void
FilesBar::openFileInNewTabWithTransits(const AFileInfo& fi)
{
    AstroFile* file1 = new AstroFile;
    file1->load(fi);
    addFile(file1);
    AstroFile* file2 = new AstroFile;
    file2->setName("Transits " + QDate::currentDate().toString());
    file2->setParent(this);
    files[currentIndex()] << file2;
    updateTab(currentIndex());
    connect(file2,
            SIGNAL(changed(AstroFile::Members)),
            this,
            SLOT(fileUpdated(AstroFile::Members)));
    connect(file2, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
    emit currentChanged(currentIndex());
}

void
FilesBar::openFileInNewTabWithTransits(const AFileInfo& fi, AstroFile* af)
{
    AstroFile* file1 = new AstroFile;
    file1->load(fi);
    addFile(file1);
    af->setParent(this);
    files[currentIndex()] << af;
    updateTab(currentIndex());
    connect(af,
            SIGNAL(changed(AstroFile::Members)),
            this,
            SLOT(fileUpdated(AstroFile::Members)));
    connect(af, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
    emit currentChanged(currentIndex());
}

void
FilesBar::openFileInNewTabWithProgressions(const AFileInfo& fi)
{
    // Load the natal chart
    AstroFile* file1 = new AstroFile;
    file1->load(fi);
    addFile(file1);
    
    // Create a progressed chart based on this natal chart
    AstroFile* file2 = new AstroFile;
    file2->setType(TypeDerivedProg);
    file2->setName("Progressed " + file1->getName());
    file2->setGMT(QDateTime::currentDateTimeUtc());
    file2->setTimezone(file1->getTimezone());
    file2->setLocation(file1->getLocation());
    file2->setLocationName(file1->getLocationName());
    file2->setBaseChart(file1->getGMT());
    file2->setParent(this);
    
    // Calculate the progressed chart so it has data before displaying
    file2->calculate();
    
    files[currentIndex()] << file2;
    updateTab(currentIndex());
    connect(file2,
            SIGNAL(changed(AstroFile::Members)),
            this,
            SLOT(fileUpdated(AstroFile::Members)));
    connect(file2, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
    emit currentChanged(currentIndex());
}

void
FilesBar::openFileAsSecond(const AFileInfo& fi)
{
    if (files[currentIndex()].count() < 2) {
        AstroFile* file = new AstroFile;
        file->load(fi);
        file->setParent(this);
        files[currentIndex()] << file;
        updateTab(currentIndex());
        connect(file,
                SIGNAL(changed(AstroFile::Members)),
                this,
                SLOT(fileUpdated(AstroFile::Members)));
        connect(file, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
        emit currentChanged(currentIndex());
    } else {
        files[currentIndex()][1]->load(fi);
    }
}

void
FilesBar::openTransitsAsSecond(AstroFile* af)
{
    if (files[currentIndex()].count() < 2) {
        // XXX ownership changing?
        af->setParent(this);
        files[currentIndex()] << af;
        updateTab(currentIndex());
        connect(af,
                SIGNAL(changed(AstroFile::Members)),
                this,
                SLOT(fileUpdated(AstroFile::Members)));
        connect(af, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
        emit currentChanged(currentIndex());
    } else {
        if (files[currentIndex()][1] != af) {
            files[currentIndex()][1] = af; // need copy?
        }
        // files[currentIndex()][1]->setGMT(dt);
        emit currentChanged(currentIndex());
    }
}

void
FilesBar::openFileComposite(const AFileInfoList& fis)
{
    // XXX @todo
    AstroFile* file = new AstroFile;
    file->load(fis.first());
    addFile(file);
}

void
FilesBar::openFileReturn(const AFileInfo& fi, const QString& body)
{
    AstroFile* native = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(native, true);
    native->load(fi);

    QString planet = body == "Sun" ? "Solar" : body == "Moon" ? "Lunar" : body;
    if (native->getHarmonic() != 1.0) {
        planet += " H" + QString::number(native->getHarmonic());
    }

    A::PlanetId pid = A::getPlanetId(body);

    AstroFile* planetReturn = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(planetReturn, true);

    // planetReturn->setParent(this);
    planetReturn->setName("Return");
    // planetReturn->setGMT(QDateTime::currentDateTime());

    auto dt = A::calculateReturnTime(pid,
                                     native->data(),
                                     planetReturn->data(),
                                     native->getHarmonic());
    delete native;

    planetReturn->setGMT(dt);

    planetReturn->setName(QString("%1 %2 Return %3")
                              .arg(fi.baseName())
                              .arg(planet)
                              .arg(dt.toLocalTime().date().year()));
    planetReturn->clearUnsavedState();
    addFile(planetReturn);
}

void
FilesBar::findDerivedChart(const AFileInfo& fi)
{
}

void
FilesBar::openFileInNewTabWithReturn(const AFileInfo& fi, const QString& body)
{
    AstroFile* native = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(native);
    native->load(fi);
    addFile(native);

    QString planet = body == "Sun" ? "Solar" : body == "Moon" ? "Lunar" : body;
    if (native->getHarmonic() != 1.0) {
        planet += " H" + QString::number(native->getHarmonic());
    }

    A::PlanetId pid = A::getPlanetId(body);

    AstroFile* planetReturn = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(planetReturn, true);

    auto dt = A::calculateReturnTime(pid,
                                     native->data(),
                                     planetReturn->data(),
                                     native->getHarmonic());
    planetReturn->setGMT(dt);

    planetReturn->setName(QString("%1 %2 Return %3")
                              .arg(fi.baseName())
                              .arg(planet)
                              .arg(dt.toLocalTime().date().year()));

    planetReturn->setParent(this);

    files[currentIndex()] << planetReturn;
    updateTab(currentIndex());

    planetReturn->clearUnsavedState();

    connect(planetReturn,
            SIGNAL(changed(AstroFile::Members)),
            this,
            SLOT(fileUpdated(AstroFile::Members)));
    connect(planetReturn,
            SIGNAL(destroyRequested()),
            this,
            SLOT(fileDestroyed()));
    emit currentChanged(currentIndex());
}

void
FilesBar::openTransits(int i)
{
    Q_UNUSED(i);
}

/* =========================== MAIN WINDOW ================================== */

MainWindow::MainWindow(bool skipRestore, QWidget* parent) 
    : QMainWindow(parent), Customizable(), _skipRestore(skipRestore)
{
    HelpWidget* help = new HelpWidget("text/" + A::usedLanguage(), this);

    filesBar           = new FilesBar(this);
    astroWidget        = new AstroWidget(this);
    databaseDockWidget = new QDockWidget(this);
    astroDatabase      = new AstroDatabase();
    toolBar            = new QToolBar(tr("File"), this);
    toolBar2           = new QToolBar(tr("Options"), this);
    helpToolBar        = new QToolBar(tr("Hint"), this);
    panelsMenu         = new QMenu;

    toolBar->setObjectName("toolBar");
    toolBar2->setObjectName("toolBar2");
    helpToolBar->setObjectName("helpToolBar");
    helpToolBar->addWidget(help);
    databaseDockWidget->setObjectName("dbDockWidget");
    databaseDockWidget->setWidget(astroDatabase);
    databaseDockWidget->setWindowTitle(tr("Database"));
    databaseDockWidget->hide();
    help->setFixedHeight(70);
    this->setIconSize(QSize(48, 48));
    this->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    this->setWindowTitle(QApplication::applicationName());
    this->setMinimumHeight(480);

    // Enable tooltips even when window doesn't have focus (for
    // focus-follows-mouse)
    this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

    QWidget* wdg = new QWidget(this);
    wdg->setObjectName("centralWidget");
    wdg->setContextMenuPolicy(Qt::CustomContextMenu);
    QVBoxLayout* layout = new QVBoxLayout(wdg);
    layout->setSpacing(0);
    layout->setContentsMargins(QMargins(0, 0, 0, 0));
    layout->addWidget(filesBar, 0, Qt::AlignLeft);
    layout->addWidget(astroWidget);

    setCentralWidget(wdg);
    addToolBarActions();
    addToolBar(Qt::TopToolBarArea, toolBar);
    addToolBar(Qt::TopToolBarArea, astroWidget->getToolBar());
    addToolBar(Qt::TopToolBarArea, toolBar2);
    addToolBar(Qt::TopToolBarArea, helpToolBar);
    addDockWidget(Qt::LeftDockWidgetArea, databaseDockWidget);

    for (QDockWidget* w : astroWidget->getDockPanels()) {
        addDockWidget(Qt::RightDockWidgetArea, w);
        w->hide();
        createActionForPanel(w);
    }

    for (QWidget* w : astroWidget->getHoroscopeControls()) {
        auto name = w->objectName();
        qDebug() << "Permanent widget added:" << w;
        if (!qobject_cast<QComboBox*>(w)) {
            for (auto btn : w->findChildren<QPushButton*>()) {
                statusBar()->addPermanentWidget(btn);
            }
            continue;
        }
        statusBar()->addPermanentWidget(w);
    }

    A::AspectsSet* dynAspSet = nullptr;
    for (auto&& as : A::getAspectSets()) {
        if (as.name.startsWith("Dynamic")) {
            dynAspSet = &as;
            break;
        }
    }

    auto aspectsSelector = astroWidget->getAspectsSelector();
    connect(aspectsSelector,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this, aspectsSelector](int i) {
                A::AspectId id      = aspectsSelector->itemData(i).toInt();
                const auto& asp     = A::getAspectSet(id);
                bool        add     = asp.name.startsWith("Dynamic");
                auto        dactrls = astroWidget->getDynAspectControls();
                if (add) {
                    if (dactrls->parent()) dactrls->setVisible(true);
                    else
                        statusBar()->insertPermanentWidget(0, dactrls);
                } else {
                    if (dactrls->parent()) dactrls->setVisible(false);
                }
            });

    connect(wdg,
            SIGNAL(customContextMenuRequested(QPoint)),
            this,
            SLOT(contextMenu(QPoint)));
    connect(filesBar,
            SIGNAL(currentChanged(int)),
            this,
            SLOT(currentTabChanged()));
    connect(astroDatabase,
            SIGNAL(openFile(const AFileInfo&)),
            filesBar,
            SLOT(openFile(const AFileInfo&)));
    connect(astroDatabase,
            SIGNAL(openFileInNewTab(const AFileInfo&)),
            filesBar,
            SLOT(openFileInNewTab(const AFileInfo&)));
    connect(astroDatabase,
            SIGNAL(openFileInNewTabWithTransits(const AFileInfo&)),
            filesBar,
            SLOT(openFileInNewTabWithTransits(const AFileInfo&)));
    connect(astroDatabase,
            SIGNAL(openFileInNewTabWithProgressions(const AFileInfo&)),
            filesBar,
            SLOT(openFileInNewTabWithProgressions(const AFileInfo&)));
    connect(astroDatabase,
            SIGNAL(openFileAsSecond(const AFileInfo&)),
            filesBar,
            SLOT(openFileAsSecond(const AFileInfo&)));
    connect(astroWidget,
            SIGNAL(appendFileRequested()),
            filesBar,
            SLOT(openFileAsSecond()));
    connect(astroWidget,
            SIGNAL(helpRequested(QString)),
            help,
            SLOT(searchFor(QString)));
    connect(astroWidget,
            SIGNAL(swapFilesRequested(int, int)),
            filesBar,
            SLOT(swapCurrentFiles(int, int)));
    connect(statusBar(),
            SIGNAL(messageChanged(QString)),
            help,
            SLOT(searchFor(QString)));
    connect(new QShortcut(QKeySequence("CTRL+TAB"), this),
            SIGNAL(activated()),
            filesBar,
            SLOT(nextTab()));
    connect(astroDatabase,
            SIGNAL(openFileReturn(const AFileInfo&, const QString&)),
            filesBar,
            SLOT(openFileReturn(const AFileInfo&, const QString&)));
    connect(
        astroDatabase,
        SIGNAL(openFileInNewTabWithReturn(const AFileInfo&, const QString&)),
        filesBar,
        SLOT(openFileInNewTabWithReturn(const AFileInfo&, const QString&)));

    if (auto transits = astroWidget->findDockHdlr<Transits>()) {
        connect(transits,
                SIGNAL(updateFirst(AstroFile*)),
                filesBar,
                SLOT(openFile(AstroFile*)));
        connect(transits,
                &Transits::updateSecond,
                filesBar,
                &FilesBar::openTransitsAsSecond);
        connect(transits,
                SIGNAL(addChart(AstroFile*)),
                filesBar,
                SLOT(addFile(AstroFile*)));
        connect(
            transits,
            SIGNAL(addChartWithTransits(const AFileInfo&, AstroFile*)),
            filesBar,
            SLOT(openFileInNewTabWithTransits(const AFileInfo&, AstroFile*)));
    }

    loadSettings();
    if (!_skipRestore) {
        restoreSession();
        astroDatabase->restoreDatabaseState();
    } else {
        filesBar->addNewFile();
    }
}

/*static*/
MainWindow*
MainWindow::instance(bool skipRestore)
{
    static MainWindow* theMainWindow = nullptr;
    if (!theMainWindow) {
        theMainWindow = new MainWindow(skipRestore);
    }
    return theMainWindow;
}

void
MainWindow::contextMenu(QPoint p)
{
    QPoint pos = ((QWidget*) sender())->mapToGlobal(p);
    panelsMenu->exec(pos);
}

void
MainWindow::addToolBarActions()
{
    auto tbNew = new QToolButton(this);
    tbNew->setPopupMode(QToolButton::MenuButtonPopup);
    tbNew->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    auto newAct = new QAction(QIcon("style/file.png"), tr("New"));
    tbNew->addAction(newAct);
    tbNew->setDefaultAction(newAct);
    connect(newAct, SIGNAL(triggered()), filesBar, SLOT(addNewFile()));

    auto newEditAct = new QAction(QIcon("style/file.png"), tr("New chart..."));
    tbNew->addAction(newEditAct);
    connect(newEditAct, SIGNAL(triggered()), filesBar, SLOT(editNewChart()));

    auto newFindAct = new QAction(QIcon(), tr("Find chart..."));
    tbNew->addAction(newFindAct);
    connect(newFindAct, SIGNAL(triggered()), filesBar, SLOT(findChart()));

    toolBar->addWidget(tbNew);
    toolBar->addAction(QIcon("style/save.png"),
                       tr("Save"),
                       this,
                       SLOT(saveFile()));
    toolBar->addAction(QIcon("style/edit.png"),
                       tr("Edit"),
                       astroWidget,
                       SLOT(openEditor()));
    // toolBar  -> addAction(QIcon("style/print.png"), tr("Экспорт"));

    newAct->setShortcut(QKeySequence("CTRL+N"));
    newEditAct->setShortcut(QKeySequence("Ctrl+Shift+N"));
    toolBar->actions()[1]->setShortcut(QKeySequence("CTRL+S"));
    toolBar->actions()[2]->setShortcut(QKeySequence("F2"));
    // toolBar  -> actions()[3]->setShortcut(QKeySequence("CTRL+P"));

    newAct->setStatusTip(tr("New data") + "\n Ctrl+N");
    newEditAct->setStatusTip(tr("Edit new data") + "\n Ctrl+Shift+N");
    toolBar->actions()[1]->setStatusTip(tr("Save data") + "\n Ctrl+S");
    toolBar->actions()[2]->setStatusTip(tr("Edit data...") + "\n F2");
    // toolBar  -> actions()[3]->setStatusTip(tr("Печать или экспорт \n
    // Ctrl+P"));

    QToolButton* b = new QToolButton; // panels toggle button
    b->setText(tr("Panels"));
    b->setIcon(QIcon("style/panels.png"));
    b->setToolButtonStyle(toolButtonStyle());
    b->setMenu(panelsMenu);
    b->setPopupMode(QToolButton::InstantPopup);

    toolBar2->addWidget(b);
    toolBar2->addAction(QIcon("style/tools.png"),
                        tr("Options"),
                        this,
                        SLOT(showSettingsEditor()));
    toolBar2->addAction(QIcon("style/info.png"),
                        tr("About"),
                        this,
                        SLOT(showAbout()));
    // toolBar2 -> addAction(QIcon("style/coffee.png"), tr("Справка"));

    QAction* dbToggle = createActionForPanel(
        databaseDockWidget /*, QIcon("style/database.png")*/);
    dbToggle->setShortcut(QKeySequence("CTRL+O"));
    dbToggle->setStatusTip(tr("Toggle database") + "\n Ctrl+O");

    createActionForPanel(helpToolBar /*, QIcon("style/help.png")*/);
}

QAction*
MainWindow::createActionForPanel(QWidget* w)
{
    QAction* a = panelsMenu->addAction(/*icon, */ w->windowTitle());
    a->setCheckable(true);
    connect(a, SIGNAL(triggered(bool)), w, SLOT(setVisible(bool)));
    connect(w, SIGNAL(visibilityChanged(bool)), a, SLOT(setChecked(bool)));
    return a;
}

void
MainWindow::currentTabChanged()
{
    if (!filesBar->count()) return;
    astroWidget->setFiles(filesBar->currentFiles());
}

AppSettings
MainWindow::defaultSettings()
{
    AppSettings s;
    s << astroWidget->defaultSettings();
    s.setValue("Window/Geometry", 0);
    s.setValue("Window/State", 0);
    s.setValue("askToSave", false);
    s.setValue("Key", "");
    return s;
}

AppSettings
MainWindow::currentSettings()
{
    AppSettings s;
    s << astroWidget->currentSettings();
    s.setValue("Window/Geometry", this->saveGeometry());
    s.setValue("Window/State", this->saveState());
    s.setValue("askToSave", askToSave);
    s.setValue("Key", this->APIKey().c_str());
    return s;
}

void
MainWindow::applySettings(const AppSettings& s)
{
    astroWidget->applySettings(s);
    this->restoreGeometry(s.value("Window/Geometry").toByteArray());
    this->restoreState(s.value("Window/State").toByteArray());
    askToSave = s.value("askToSave").toBool();
    _APIKey   = s.value("Key").toString().toStdString();
    
    // Auto-save settings immediately after applying
    saveSettings();
}

void
MainWindow::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addControl("askToSave", tr("Ask about unsaved files"));
    ed->addControl("Key", "Timezone and Geography API");
    astroWidget->setupSettingsEditor(ed);
}

void
MainWindow::closeEvent(QCloseEvent* ev)
{
    while (askToSave && filesBar->count() && filesBar->currentFiles().count()
           && filesBar->currentFiles()[0]->hasUnsavedChanges())
    {
        if (!filesBar->closeTab(filesBar->currentIndex())) return ev->ignore();
    }

    saveSession();
    saveSettings();

    QMainWindow::closeEvent(ev);
    QApplication::quit();
}

void
MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    
    // Dynamic radial gradient background
    QPainter painter(this);
    
    // Create a radial gradient centered in the window
    // The radius is 70% of the larger dimension to ensure good coverage
    QRadialGradient gradient(rect().center(), qMax(width(), height()) * 0.7);
    gradient.setColorAt(0, QColor(32, 32, 32));    // Lighter center
    gradient.setColorAt(1, QColor(80, 80, 80));    // Darker edges
    
    painter.fillRect(rect(), gradient);
}

void
MainWindow::gotoUrl(QString url)
{
    if (url.isEmpty()) url = ((QWidget*) sender())->toolTip();
    QDesktopServices::openUrl(QUrl(url));
}

void
MainWindow::showAbout()
{
    QDialog*     d  = new QDialog(this);
    QLabel*      l  = new QLabel;
    QLabel*      l2 = new QLabel;
    SlideWidget* s  = new SlideWidget;
    QPushButton* b  = new QPushButton(tr("Credits"));
    QPushButton* b1 = new QPushButton(tr("Developer's\nblog"));
    QPushButton* b2 = new QPushButton;
    QPushButton* b3 = new QPushButton;

    b->setCheckable(true);
    b1->setIconSize(QSize(32, 32));
    b2->setIconSize(QSize(32, 32));
    b3->setIconSize(QSize(32, 32));
    b1->setIcon(QIcon("style/wp.png"));
    b2->setIcon(QIcon("style/github.png"));
    b3->setIcon(QIcon("style/sourceforge.png"));
    b1->setToolTip("http://www.syslog.pro/tag/zodiac");
    b2->setToolTip("http://github.com/atten/zodiac");
    b3->setToolTip("http://sourceforge.net/projects/zodiac-app/");
    b1->setCursor(Qt::PointingHandCursor);
    b2->setCursor(Qt::PointingHandCursor);
    b3->setCursor(Qt::PointingHandCursor);
    d->setObjectName("about");
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::LinksAccessibleByMouse
                               | Qt::TextSelectableByMouse);
    l2->setWordWrap(true);
    l2->setTextInteractionFlags(Qt::LinksAccessibleByMouse
                                | Qt::TextSelectableByMouse);
    s->addSlide(l);
    s->addSlide(l2);
    s->setTransitionEffect(SlideWidget::Transition_Overlay);

    QHBoxLayout* h = new QHBoxLayout;
    h->setContentsMargins(QMargins(10, 10, 10, 10));
    h->addWidget(b);
    h->addStretch();
    h->addWidget(b1);
    h->addWidget(b2);
    h->addWidget(b3);

    QVBoxLayout* v = new QVBoxLayout(d);
    v->setContentsMargins(QMargins(0, 0, 0, 0));
    v->addWidget(s);
    v->addLayout(h);

    l->setText(
        "<center><b><big>" + QApplication::applicationVersion() + "</big></b>>"
        + tr("Astrological software for personal use.")
        + "</p>"
          //"<p><a style='color:yellow'
          // href=\"http://www.syslog.pro/tag/zodiac\">Watch
          // developer's blog</a>" " | <a style='color:yellow'
          // href=\"https://github.com/atten/zodiac\"><img
          // src=\"style/github.png\">Follow on GitHub</a></p>"
          "<p>Copyright (C) 2012-2014 Artem Vasilev<br> " "style" "=':" "white'" " hr" "ef" "=\"mailto:atten@" "syslog.pro\">atten@/" "p><br>"
        + tr(
            "This application is provided AS IS and distributed in " "the  " "t" "h" "a" "t" " " "i" "t" " " "w" "i" "l" "l" " " "b" "e" " " "u" "s" "e" "f" "u" "l" "," " " "ANY WARRANTY;  " "even the implied  " "MEC" "HAT" "ABL" "ITY" " or FITNESS FOR A .")
        + "</center>");

    l2->setText(
        "<p><b>Swiss Ephemerides library</b><br> " "Astrodienst AG,  " "res" "e"
                                                                             "r"
                                                                             "v" "ed." "<br" "> " "style=''" " href=\"ftp:/" "www.astro./" "swisseph/\">" "ftp://." "ch/pub//" "LICENSE</>" "<p>>" "Pri " "Ico " "Set/" "b>  " "Webs" "ign " "Dep<" "br>\"https://www.iconfinder.com/iconsets/Primo_Icons#readme\">www.iconfinder.com/iconsets/Primo_Icons#readme</a></p>>" "<p>Additional thanks to authors of <b>\"SymSolon\"</b> project<br>\"http://sf.net/projects/symsolon\">sf.net/projects/symsolon</a></p>");

    connect(l, SIGNAL(linkActivated(QString)), this, SLOT(gotoUrl(QString)));
    connect(l2, SIGNAL(linkActivated(QString)), this, SLOT(gotoUrl(QString)));
    connect(b1, SIGNAL(clicked()), this, SLOT(gotoUrl()));
    connect(b2, SIGNAL(clicked()), this, SLOT(gotoUrl()));
    connect(b3, SIGNAL(clicked()), this, SLOT(gotoUrl()));
    connect(b, SIGNAL(clicked()), s, SLOT(nextSlide()));
    d->exec();
}

void
MainWindow::saveSession()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.beginGroup("Session");
    
    if (filesBar->count() == 0) {
        // No tabs - save empty session
        settings.setValue("tabCount", 0);
        settings.setValue("currentTab", 0);
        qDebug() << "Session saved: empty (no tabs)";
    } else {
        // Save session to FilesBar (this clears old data first)
        filesBar->saveFilesToSession();
        
        // Save current tab index AFTER filesBar saves (so it doesn't get cleared)
        settings.setValue("currentTab", filesBar->currentIndex());
        
        qDebug() << "Session saved:" << filesBar->count() << "tabs";
    }
    
    settings.endGroup();
    
    // Save database tree state
    astroDatabase->saveDatabaseState();
}

void
MainWindow::restoreSession()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.beginGroup("Session");
    
    int tabCount = settings.value("tabCount", 0).toInt();
    
    if (tabCount == 0) {
        // No saved session, create default new file
        filesBar->addNewFile();
        settings.endGroup();
        return;
    }
    
    // Restore each tab
    for (int i = 0; i < tabCount; ++i) {
        settings.beginGroup(QString("Tab%1").arg(i));
        
        int fileCount = settings.value("fileCount", 0).toInt();
        
        if (fileCount == 0) {
            qDebug() << "Tab" << i << "has no files, skipping";
            settings.endGroup();
            continue;
        }
        
        for (int j = 0; j < fileCount; ++j) {
            settings.beginGroup(QString("File%1").arg(j));
            
            // Read directory and filename
            QString directory = settings.value("directory").toString();
            QString filename = settings.value("filename").toString();
            bool hasUnsavedChanges = settings.value("hasUnsavedChanges", false).toBool();
            
            AstroFile* af = nullptr;
            bool hasCurrentData = settings.contains("name") && settings.contains("gmt");
            
            try {
                if (!directory.isEmpty() && !filename.isEmpty() && !hasCurrentData) {
                    // Restore saved file without modifications using directory + filename
                    af = new AstroFile;
                    astroWidget->setupFile(af);
                    AFileInfo fileInfo(QDir(directory), filename);
                    af->load(fileInfo);
                    qDebug() << "Restored saved file - dir:" << directory 
                             << "filename:" << filename
                             << "af->getName():" << af->getName()
                             << "GMT:" << af->getGMT();
                } else if (hasCurrentData) {
                    // Restore file from saved current data (unsaved or modified)
                    af = new AstroFile;
                    astroWidget->setupFile(af);
                    
                    af->suspendUpdate();
                    af->setName(settings.value("name").toString());
                    af->setGMT(settings.value("gmt").toDateTime());
                    af->setType((FileType)settings.value("type", TypeEvent).toInt());
                    af->setLocation(settings.value("location").value<QVector3D>());
                    af->setLocationName(settings.value("locationName").toString());
                    af->setTimezone(settings.value("timezone").toInt());
                    af->setHarmonic(settings.value("harmonic", 1.0).toDouble());
                    af->setComment(settings.value("comment").toString());
                    
                    if (settings.value("hasBaseChart", false).toBool()) {
                        af->setBaseChart(settings.value("baseChart").toDateTime());
                    }
                    
                    af->resumeUpdate();
                    
                    // If this file had unsaved changes, DON'T clear the unsaved flag
                    // (it was set by the property changes above)
                    if (!hasUnsavedChanges) {
                        // This was an unsaved new file that never had a saved version
                        af->clearUnsavedState();
                    }
                    
                    qDebug() << "Restored file from current data:" << af->getName()
                             << "unsavedChanges:" << hasUnsavedChanges;
                } else {
                    qDebug() << "Skipping file with insufficient data";
                    settings.endGroup();
                    continue;
                }
                
                // Restore transit date range (per-tab UI state) for all files
                if (af) {
                    if (settings.contains("transitStartDate")) {
                        af->setTransitStartDate(settings.value("transitStartDate").toDate());
                    }
                    if (settings.contains("transitDuration")) {
                        af->setTransitDuration(settings.value("transitDuration").toString());
                    }
                    
                    if (j == 0) {
                        filesBar->addFile(af);
                    } else {
                        // Add as secondary file
                        filesBar->openTransitsAsSecond(af);
                    }
                }
            } catch (const std::exception& e) {
                qDebug() << "Error restoring file" << j << "in tab" << i << ":" << e.what();
                if (af) delete af;
            }
            
            settings.endGroup(); // File%1
        }
        
        settings.endGroup(); // Tab%1
    }
    
    // Restore current tab
    int currentTab = settings.value("currentTab", 0).toInt();
    if (currentTab < filesBar->count()) {
        filesBar->setCurrentIndex(currentTab);
    }
    
    settings.endGroup();
    
    // If no tabs were successfully restored, create a new file
    if (filesBar->count() == 0) {
        qDebug() << "No tabs restored, creating new file";
        filesBar->addNewFile();
    } else {
        qDebug() << "Session restored:" << filesBar->count() << "tabs";
    }
}

void
FilesBar::saveFilesToSession()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.beginGroup("Session");
    
    // Clear all old session data before saving new
    settings.remove(""); // Removes all keys in current group
    
    settings.setValue("tabCount", count());
    
    for (int i = 0; i < count(); ++i) {
        settings.beginGroup(QString("Tab%1").arg(i));
        
        const AstroFileList& fileList = files[i];
        settings.setValue("fileCount", fileList.count());
        
        for (int j = 0; j < fileList.count(); ++j) {
            settings.beginGroup(QString("File%1").arg(j));
            
            AstroFile* af = fileList[j];
            
            // Save file path for saved files
            // AFileInfo stores files as: directory + encodedName + ".dat"
            // So we need to save: directory + "/" + baseName (without encoding or .dat)
            // When restoring, we'll pass this to AFileInfo which will encode and add .dat
            AFileInfo fileInfo = af->fileInfo();
            QString filePath;
            
            // Check if this is a saved file with a valid path
            bool hasSavedFile = fileInfo.exists() 
                             && !af->getName().startsWith("Untitled")
                             && !fileInfo.absoluteFilePath().isEmpty();
            
            bool hasModifications = af->hasUnsavedChanges();
            
            if (hasSavedFile) {
                // Save directory and filename separately
                // baseName() already decodes and removes .dat
                QString dir = fileInfo.absolutePath();
                QString name = fileInfo.baseName(); // decoded, no .dat
                
                settings.setValue("directory", dir);
                settings.setValue("filename", name);
                
                qDebug() << "Saving file to session:" << af->getName() 
                         << "dir:" << dir << "filename:" << name
                         << "modified:" << hasModifications;
            } else {
                qDebug() << "Saving unsaved file to session:" << af->getName();
            }
            
            settings.setValue("hasUnsavedChanges", hasModifications);
            
            // Save current chart data if:
            // 1. It's a new/unsaved file, OR
            // 2. It has modifications that haven't been saved
            if (!hasSavedFile || hasModifications) {
                // Save current file state
                settings.setValue("name", af->getName());
                settings.setValue("gmt", af->getGMT());
                settings.setValue("type", (int)af->getType());
                settings.setValue("location", af->getLocation());
                settings.setValue("locationName", af->getLocationName());
                settings.setValue("timezone", af->getTimezone());
                settings.setValue("harmonic", af->getHarmonic());
                settings.setValue("comment", af->getComment());
                
                if (af->hasBaseChart()) {
                    settings.setValue("hasBaseChart", true);
                    settings.setValue("baseChart", af->getBaseChartGMT());
                } else {
                    settings.setValue("hasBaseChart", false);
                }
            }
            
            // Save transit date range (per-tab UI state, always save regardless of file state)
            if (!af->getTransitStartDate().isNull()) {
                settings.setValue("transitStartDate", af->getTransitStartDate());
            }
            if (!af->getTransitDuration().isEmpty()) {
                settings.setValue("transitDuration", af->getTransitDuration());
            }
            
            settings.endGroup(); // File%1
        }
        
        settings.endGroup(); // Tab%1
    }
    
    settings.endGroup();
}
