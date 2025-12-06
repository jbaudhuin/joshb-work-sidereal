#include <QActionGroup>
#include <QInputDialog>
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
#include <QDrag>
#include <QMimeData>
#include <QThread>

#include <filesystem>
#include <system_error>

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

/* =========================== FILE TREE VIEW
 * ======================================== */

FileTreeView::FileTreeView(AstroDatabase* parent)
  : QTreeView(parent)
  , database(parent)
{
}

void
FileTreeView::startDrag(Qt::DropActions supportedActions)
{
    // Start the drag but DON'T call the base implementation
    // This prevents Qt from trying to move items in the model
    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData();
    
    // Create mime data with selected file info
    QStringList files;
    auto selection = selectionModel()->selectedIndexes();
    for (const auto& index : selection) {
        files << index.data().toString();
    }
    mimeData->setText(files.join("\n"));
    
    drag->setMimeData(mimeData);
    
    // Support both move (default) and copy (with Ctrl)
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
}

void
FileTreeView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->source() == this) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void
FileTreeView::dragMoveEvent(QDragMoveEvent* event)
{
    QString targetDir;
    if (database->validateDropTarget(event->position().toPoint(), targetDir)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void
FileTreeView::dropEvent(QDropEvent* event)
{
    QString targetDir;
    if (database->validateDropTarget(event->position().toPoint(), targetDir)) {
        event->acceptProposedAction();
        
        // Check if Ctrl is pressed for copy operation
        bool copyOperation = (event->modifiers() & Qt::ControlModifier);
        
        if (copyOperation) {
            database->performCopy(targetDir);
        } else {
            database->performMove(targetDir);
        }
    } else {
        event->ignore();
    }
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

        dirit->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);

        fswatch->addPath(dir);
        dirModel->appendRow(dirit);
    }

    searchProxy = new QSortFilterProxyModel(this);
    searchProxy->setRecursiveFilteringEnabled(true);

    fileList = new FileTreeView(this);
    searchProxy->setSourceModel(dirModel);
    fileList->setModel(searchProxy);

    search = new QLineEdit;

    refresh->setIcon(QIcon("style/update.png"));
    refresh->setToolTip(tr("Refresh"));
    refresh->setCursor(Qt::PointingHandCursor);

    fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileList->setEditTriggers(QAbstractItemView::NoEditTriggers); // We'll trigger manually
    fileList->setDragEnabled(true);
    fileList->setAcceptDrops(true);
    fileList->setDropIndicatorShown(true);
    fileList->setDragDropMode(QAbstractItemView::DragDrop);
    fileList->setDefaultDropAction(Qt::MoveAction);
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
    connect(dirModel,
            SIGNAL(itemChanged(QStandardItem*)),
            this,
            SLOT(handleItemRenamed(QStandardItem*)));

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
    
    // Clear old expansion state
    settings.remove("expanded");
    
    // Recursively save expanded state for all directories
    std::function<void(const QModelIndex&)> saveExpansion = [&](const QModelIndex& index) {
        if (!index.isValid()) return;
        
        QStandardItem* item = dirModel->itemFromIndex(index);
        if (item && item->data(TypeRole).toUInt() == dirType) {
            QString path = item->data(PathRole).toString();
            QModelIndex proxyIndex = searchProxy->mapFromSource(index);
            bool expanded = fileList->isExpanded(proxyIndex);
            
            if (expanded && !path.isEmpty()) {
                settings.setValue(QString("expanded/%1").arg(path), true);
            }
            
            // Recursively save children
            for (int i = 0; i < item->rowCount(); ++i) {
                QStandardItem* child = item->child(i);
                if (child && child->data(TypeRole).toUInt() == dirType) {
                    saveExpansion(dirModel->indexFromItem(child));
                }
            }
        }
    };
    
    // Save expansion state for all top-level folders and their children
    for (int i = 0; i < dirModel->rowCount(); ++i) {
        saveExpansion(dirModel->index(i, 0));
    }
    
    settings.endGroup();
}

void
AstroDatabase::restoreDatabaseState()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.beginGroup("Database");
    
    // Recursively restore expanded state for all directories
    std::function<void(const QModelIndex&)> restoreExpansion = [&](const QModelIndex& index) {
        if (!index.isValid()) return;
        
        QStandardItem* item = dirModel->itemFromIndex(index);
        if (item && item->data(TypeRole).toUInt() == dirType) {
            QString path = item->data(PathRole).toString();
            bool expanded = settings.value(QString("expanded/%1").arg(path), false).toBool();
            
            if (expanded) {
                QModelIndex proxyIndex = searchProxy->mapFromSource(index);
                fileList->setExpanded(proxyIndex, true);
            }
            
            // Recursively restore children
            for (int i = 0; i < item->rowCount(); ++i) {
                QStandardItem* child = item->child(i);
                if (child && child->data(TypeRole).toUInt() == dirType) {
                    restoreExpansion(dirModel->indexFromItem(child));
                }
            }
        }
    };
    
    // Restore expansion state for all top-level folders and their children
    for (int i = 0; i < dirModel->rowCount(); ++i) {
        restoreExpansion(dirModel->index(i, 0));
    }
    
    // Restore scroll position (must be done after expanding)
    QScrollBar* vbar = fileList->verticalScrollBar();
    if (vbar) {
        int scrollPos = settings.value("scrollPosition", 0).toInt();
        vbar->setValue(scrollPos);
    }
    
    settings.endGroup();
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
    // Save expansion state before clearing
    QSet<QString> expandedPaths;
    std::function<void(const QModelIndex&)> saveExpansion = [&](const QModelIndex& index) {
        if (!index.isValid()) return;
        
        QModelIndex proxyIndex = searchProxy->mapFromSource(index);
        if (fileList->isExpanded(proxyIndex)) {
            QStandardItem* item = dirModel->itemFromIndex(index);
            if (item) {
                QString path = item->data(PathRole).toString();
                if (!path.isEmpty()) {
                    expandedPaths.insert(path);
                }
            }
        }
        
        // Recursively check children
        QStandardItem* item = dirModel->itemFromIndex(index);
        if (item) {
            for (int i = 0; i < item->rowCount(); ++i) {
                QStandardItem* child = item->child(i);
                if (child && child->data(TypeRole).toUInt() == dirType) {
                    saveExpansion(dirModel->indexFromItem(child));
                }
            }
        }
    };
    
    // Save expansion state of all top-level folders and their children
    for (int i = 0; i < dirModel->rowCount(); ++i) {
        saveExpansion(dirModel->index(i, 0));
    }
    
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

    std::function<void(QModelIndex, int)> updir = [&](QModelIndex mi, int depth) {
        auto diritem = dirModel->itemFromIndex(mi);
        QDir dir(diritem->data().toString());
        diritem->removeRows(0, diritem->rowCount());

        for (const auto& dn : dir.entryList(QDir::Dirs)) {
            if (dn == "." || dn == "..") continue;

            QFileInfo fi(dir, dn);
            
            // Show immediate subdirectories at any level
            // For deeper nested folders, only show if they contain chart files
            if (depth > 1 && !directoryHasChartFiles(fi.absoluteFilePath())) {
                continue;
            }
            
            auto      subdirname = AFileInfo::decodeName(dn);
            auto      subdiritem = new QStandardItem(subdirname);
            subdiritem->setData(dirType, TypeRole);
            subdiritem->setData(fi.absoluteFilePath(), PathRole);
            subdiritem->setData(fi.absoluteFilePath(), Qt::ToolTipRole);
            subdiritem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
            QFont f = subdiritem->data(Qt::FontRole).value<QFont>();
            f.setBold(true);
            subdiritem->setData(f, Qt::FontRole);

            int n = diritem->rowCount();
            diritem->appendRow(subdiritem);

            QModelIndex sdmi = diritem->child(n)->index();
            updir(sdmi, depth + 1);
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
            child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
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
        updir(mi, 0);
    }
    if (!sl.empty()) sm->select(sl, QItemSelectionModel::ClearAndSelect);
    
    // Restore expansion state after rebuilding
    std::function<void(const QModelIndex&)> restoreExpansion =
        [&](const QModelIndex& index) {
            if (!index.isValid()) return;

            QStandardItem* item = dirModel->itemFromIndex(index);
            if (item) {
                QString path = item->data(PathRole).toString();
                if (!path.isEmpty() && expandedPaths.contains(path)) {
                    QModelIndex proxyIndex = searchProxy->mapFromSource(index);
                    fileList->setExpanded(proxyIndex, true);
                }

                // Recursively restore children
                for (int i = 0; i < item->rowCount(); ++i) {
                    QStandardItem* child = item->child(i);
                    if (child && child->data(TypeRole).toUInt() == dirType) {
                        restoreExpansion(dirModel->indexFromItem(child));
                    }
                }
            }
        };

    // Restore expansion state for all folders
    for (int i = 0; i < dirModel->rowCount(); ++i) {
        restoreExpansion(dirModel->index(i, 0));
    }
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
    QPoint pos = ((QWidget*) sender())->mapToGlobal(p);

    p        = fileList->mapFromGlobal(pos);
    auto proxyIndex = fileList->indexAt(p);
    if (!proxyIndex.isValid()) return;

    qDebug() << proxyIndex << proxyIndex.data() << proxyIndex.data(TypeRole);
    auto qmi = searchProxy->mapToSource(proxyIndex);
    qDebug() << qmi;

    auto item = dirModel->itemFromIndex(qmi);
    if (!item) return;

    auto type = entryType(item->data(TypeRole).toUInt());
    if (type != dirType && type != fileType) return;

    QMenu* mnu = new QMenu(this);

    if (type == dirType) {
        mnu->addAction(tr("Save here"), [this, proxyIndex] { saveCurrent(proxyIndex); });
        mnu->addAction(tr("New folder..."), [this, proxyIndex] { newDirectory(proxyIndex); });
        
        // Add delete folder action
        QString dirPath = item->data(PathRole).toString();
        QDir dir(dirPath);
        
        // Check if directory can be deleted (empty or only non-chart files)
        bool canDelete = true;
        QString reason;
        
        QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        if (entries.isEmpty()) {
            reason = tr("Delete this empty folder");
        } else {
            // Check if it has subdirectories
            for (const QFileInfo& entry : entries) {
                if (entry.isDir()) {
                    canDelete = false;
                    reason = tr("Cannot delete: folder contains subdirectories");
                    break;
                }
            }
            
            // Check if it has chart files
            if (canDelete) {
                QStringList chartFiles = dir.entryList(AFileInfo::wildcard(), QDir::Files);
                if (!chartFiles.isEmpty()) {
                    canDelete = false;
                    reason = tr("Cannot delete: folder contains chart files");
                } else {
                    reason = tr("Delete folder (contains only non-chart files)");
                }
            }
        }
        
        QAction* deleteAction = mnu->addAction(tr("Delete folder"), [this, proxyIndex] { deleteDirectory(proxyIndex); });
        deleteAction->setEnabled(canDelete);
        deleteAction->setToolTip(reason);
        
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
        mnu->addAction(tr("Set Type..."), this, SLOT(setTypeForSelected()));
        mnu->addAction(tr("Rename..."), this, SLOT(renameSelected()));
        mnu->addAction(tr("Move to folder..."), this, SLOT(moveToFolder()));
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
    auto sourceIndex = searchProxy->mapToSource(qmi);
    auto item = dirModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString targetDir;
    auto type = entryType(item->data(TypeRole).toUInt());
    
    if (type == dirType) {
        targetDir = item->data(PathRole).toString();
    } else if (type == fileType && item->parent()) {
        targetDir = item->parent()->data(PathRole).toString();
    } else {
        return;
    }

    // Don't allow saving to Sample Charts
    if (targetDir.contains("sampleCharts/") || targetDir.endsWith("sampleCharts")) {
        QMessageBox::information(
            this,
            tr("Save Failed"),
            tr("Cannot save charts to the Sample Charts folder."));
        return;
    }

    // Emit signal to request saving to this directory
    emit saveCurrentToDirectory(targetDir);
}

bool
AstroDatabase::directoryHasChartFiles(const QString& dirPath)
{
    QDir dir(dirPath);
    
    // Check for chart files in this directory
    QStringList files = dir.entryList(AFileInfo::wildcard(), QDir::Files);
    if (!files.isEmpty()) {
        return true;
    }
    
    // Recursively check subdirectories
    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& subdir : subdirs) {
        if (directoryHasChartFiles(dir.filePath(subdir))) {
            return true;
        }
    }
    
    return false;
}

void
AstroDatabase::newDirectory(const QModelIndex& qmi)
{
    auto sourceIndex = searchProxy->mapToSource(qmi);
    auto item = dirModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString parentDir;
    auto type = entryType(item->data(TypeRole).toUInt());
    
    if (type == dirType) {
        parentDir = item->data(PathRole).toString();
    } else if (type == fileType && item->parent()) {
        parentDir = item->parent()->data(PathRole).toString();
    } else {
        return;
    }

    // Don't allow creating directories in Sample Charts
    if (parentDir.contains("user/") || parentDir.endsWith("user")) {
        QMessageBox::information(
            this,
            tr("New Directory"),
            tr("Cannot create directories in the Sample Charts folder."));
        return;
    }

    bool ok;
    QString dirName = QInputDialog::getText(
        this,
        tr("New Folder"),
        tr("Folder name:"),
        QLineEdit::Normal,
        "New Folder",  // Default name
        &ok);

    if (!ok || dirName.isEmpty()) {
        return;
    }

    QDir dir(parentDir);
    
    // Find unique name if directory already exists
    QString uniqueName = dirName;
    int counter = 1;
    while (dir.exists(uniqueName)) {
        uniqueName = QString("%1 (%2)").arg(dirName).arg(counter++);
    }

    if (!dir.mkdir(uniqueName)) {
        QMessageBox::warning(
            this,
            tr("New Directory Failed"),
            tr("Could not create directory."));
        return;
    }

    // Add to file system watcher
    fswatch->addPath(dir.filePath(uniqueName));
    
    // Refresh the list to show the new folder
    updateList();
}

void
AstroDatabase::deleteDirectory(const QModelIndex& qmi)
{
    auto sourceIndex = searchProxy->mapToSource(qmi);
    auto item = dirModel->itemFromIndex(sourceIndex);
    if (!item) return;

    QString dirPath = item->data(PathRole).toString();
    
    // Double-check it's a directory
    auto type = entryType(item->data(TypeRole).toUInt());
    if (type != dirType) return;
    
    // Don't allow deleting Sample Charts directories
    if (dirPath.contains("user/") || dirPath.endsWith("user")) {
        QMessageBox::information(
            this,
            tr("Delete Directory"),
            tr("Cannot delete Sample Charts folders."));
        return;
    }
    
    QDir dir(dirPath);
    
    // Get file info for the directory
    QFileInfo dirInfo(dirPath);
    
    // Verify it's safe to delete
    QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    
    // Check for subdirectories
    for (const QFileInfo& entry : entries) {
        if (entry.isDir()) {
            QMessageBox::warning(
                this,
                tr("Cannot Delete Directory"),
                tr("The folder contains subdirectories and cannot be deleted."));
            return;
        }
    }
    
    // Check for chart files
    QStringList chartFiles = dir.entryList(AFileInfo::wildcard(), QDir::Files);
    if (!chartFiles.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Cannot Delete Directory"),
            tr("The folder contains chart files and cannot be deleted."));
        return;
    }
    
    // Confirm deletion
    QString folderName = dir.dirName();
    QString message;
    if (entries.isEmpty()) {
        message = tr("Delete empty folder '%1'?").arg(folderName);
    } else {
        message = tr("Delete folder '%1'?\n\nThe folder contains %2 non-chart file(s) which will also be deleted.")
                     .arg(folderName)
                     .arg(entries.count());
    }
    
    auto reply = QMessageBox::question(
        this,
        tr("Delete Folder"),
        message,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // The issue is likely that the file system watcher has a handle on the parent directory
    // We need to temporarily remove the parent from watching
    QString parentPath = dirInfo.absolutePath();
    bool wasWatchingParent = false;
    
    QStringList watchedPaths = fswatch->directories();
    
    if (watchedPaths.contains(dirPath)) {
        fswatch->removePath(dirPath);
    }
    
    // Temporarily remove parent from watcher
    if (watchedPaths.contains(parentPath)) {
        fswatch->removePath(parentPath);
        wasWatchingParent = true;
        
        // Give the system a moment to release file handles
        QThread::msleep(100);
    }
    
    // Check filesystem permissions and try to make it writable
    std::filesystem::path fsPath = dirPath.toStdString();
    try {
        std::filesystem::permissions(fsPath, 
            std::filesystem::perms::owner_all | std::filesystem::perms::group_all,
            std::filesystem::perm_options::add);
    } catch (const std::exception&) {
        // Ignore permission setting errors
    }
    
    // Delete the directory
    bool success = false;
    std::error_code ec;
    
    if (entries.isEmpty()) {
        // For empty directories
        success = std::filesystem::remove(fsPath, ec);
    } else {
        // For non-empty directories (with non-chart files)
        std::uintmax_t removed = std::filesystem::remove_all(fsPath, ec);
        success = (removed > 0 && !ec);
    }
    
    // Re-add parent to watcher if it was being watched
    if (wasWatchingParent) {
        fswatch->addPath(parentPath);
    }
    
    if (!success) {
        QMessageBox::warning(
            this,
            tr("Delete Failed"),
            tr("Could not delete the folder: %1\n\nThe folder may be in use or you may not have permission.").arg(dirPath));
        return;
    }
    
    // Refresh the view
    updateList();
}

void
AstroDatabase::setTypeForSelected()
{
    qDebug() << "AstroDatabase::setTypeForSelected() called";
    
    auto sm = fileList->selectionModel();
    if (!sm) return;

    auto sil = sm->selectedIndexes();
    if (sil.isEmpty()) return;

    // Get selected file paths
    QStringList filePaths;
    for (const auto& proxyIndex : sil) {
        auto sourceIndex = searchProxy->mapToSource(proxyIndex);
        auto item = dirModel->itemFromIndex(sourceIndex);
        if (!item) continue;
        
        auto type = entryType(item->data(TypeRole).toUInt());
        if (type == fileType) {
            filePaths << item->data(PathRole).toString();
        }
    }
    
    if (filePaths.isEmpty()) {
        QMessageBox::information(this, tr("Set Type"), 
            tr("No chart files selected."));
        return;
    }

    // Create dialog to select new type
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Set Chart Type"));
    dialog.setModal(true);
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    QLabel* label = new QLabel(
        tr("Set type for %n selected chart(s):", "", filePaths.count()), 
        &dialog);
    layout->addWidget(label);
    
    QComboBox* typeCombo = new QComboBox(&dialog);
    typeCombo->addItem(tr("Other"), TypeOther);
    typeCombo->addItem(tr("Event"), TypeEvent);
    typeCombo->addItem(tr("Male"), TypeMale);
    typeCombo->addItem(tr("Female"), TypeFemale);
    typeCombo->addItem(tr("Return"), TypeReturn);
    typeCombo->addItem(tr("Progressed"), TypeDerivedProg);
    typeCombo->addItem(tr("Search"), TypeSearch);
    typeCombo->addItem(tr("Solar Arc"), TypeDerivedSA);
    typeCombo->addItem(tr("Primary Directions"), TypeDerivedPD);
    typeCombo->addItem(tr("Derived Search"), TypeDerivedSearch);
    layout->addWidget(typeCombo);
    
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    
    FileType newType = static_cast<FileType>(typeCombo->currentData().toInt());
    
    // Apply the type to all selected files
    int successCount = 0;
    for (const QString& filePath : filePaths) {
        AFileInfo fi(filePath);
        if (!fi.exists()) continue;
        
        // Load the file, change the type, and save it
        QSettings file(filePath, QSettings::IniFormat);
        file.setValue("type", AstroFile::typeToString(newType));
        file.sync();
        
        qDebug() << "Set type for" << fi.baseName() << "to" << AstroFile::typeToString(newType);
        successCount++;
    }
    
    QMessageBox::information(this, tr("Set Type"), 
        tr("Updated type for %n chart(s).", "", successCount));
}

void
AstroDatabase::renameSelected()
{
    qDebug() << "AstroDatabase::renameSelected() called";
    
    auto sm = fileList->selectionModel();
    if (!sm) return;

    auto sil = sm->selectedIndexes();
    if (sil.count() != 1) return; // Only allow renaming one file at a time

    auto proxyIndex = sil.first();
    if (!proxyIndex.parent().isValid()) return; // Can't rename directories (yet)

    auto sourceIndex = searchProxy->mapToSource(proxyIndex);
    auto item = dirModel->itemFromIndex(sourceIndex);
    if (!item || !item->parent()) return;

    auto type = entryType(item->data(TypeRole).toUInt());
    if (type != fileType) return;

    // Store old name and directory for tracking
    _renamingOldName = item->text();
    _renamingDir = item->parent()->data(PathRole).toString();

    qDebug() << "Starting rename for:" << _renamingOldName << "in" << _renamingDir;

    // Ensure the item is visible and selected
    fileList->scrollTo(proxyIndex);
    fileList->setCurrentIndex(proxyIndex);
    
    // Block signals temporarily to avoid triggering itemChanged when we set the flag
    dirModel->blockSignals(true);
    
    // Make this item temporarily editable
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    
    // Unblock signals
    dirModel->blockSignals(false);
    
    qDebug() << "Item flags:" << item->flags();
    qDebug() << "Calling fileList->edit() on proxyIndex";
    
    // Start inline editing
    fileList->edit(proxyIndex);
}

void
AstroDatabase::moveToFolder()
{
    auto sm = fileList->selectionModel();
    if (!sm) return;

    auto sil = sm->selectedIndexes();
    if (sil.isEmpty()) return;

    // Create a dialog with a tree view showing folder structure
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Move to Folder"));
    dialog.setModal(true);
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    QLabel* label = new QLabel(tr("Select destination folder:"), &dialog);
    layout->addWidget(label);
    
    // Create a tree view with the same model structure
    QTreeView* treeView = new QTreeView(&dialog);
    treeView->setObjectName("folderSelectorTree");
    
    // Override the dark styling with light dialog styling
    treeView->setStyleSheet(
        "QTreeView#folderSelectorTree {"
        "    color: #000;"
        "    background: #FFF;"
        "    border: 1px solid #CCC;"
        "}"
        "QTreeView#folderSelectorTree::item:hover {"
        "    background: #E0E0E0;"
        "}"
        "QTreeView#folderSelectorTree::item:selected {"
        "    background: #FFD700;"
        "    color: #000;"
        "}"
    );
    
    QStandardItemModel* folderModel = new QStandardItemModel(&dialog);
    
    // Build a folder-only model from dirModel
    std::function<void(QStandardItem*, QStandardItem*)> copyFolders = [&](QStandardItem* srcParent, QStandardItem* dstParent) {
        for (int i = 0; i < srcParent->rowCount(); ++i) {
            auto srcChild = srcParent->child(i);
            auto type = entryType(srcChild->data(TypeRole).toUInt());
            
            if (type == dirType) {
                auto dstChild = new QStandardItem(srcChild->text());
                dstChild->setData(srcChild->data(PathRole), PathRole);
                dstChild->setData(dirType, TypeRole);
                dstChild->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                
                QFont f = dstChild->data(Qt::FontRole).value<QFont>();
                f.setBold(true);
                dstChild->setData(f, Qt::FontRole);
                
                dstParent->appendRow(dstChild);
                
                // Recursively copy subdirectories
                copyFolders(srcChild, dstChild);
            }
        }
    };
    
    // Copy all top-level folders and their subdirectories
    for (int i = 0; i < dirModel->rowCount(); ++i) {
        auto topItem = dirModel->item(i);
        if (topItem) {
            auto rootItem = new QStandardItem(topItem->text());
            rootItem->setData(topItem->data(PathRole), PathRole);
            rootItem->setData(dirType, TypeRole);
            rootItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            
            QFont f = rootItem->data(Qt::FontRole).value<QFont>();
            f.setBold(true);
            rootItem->setData(f, Qt::FontRole);
            
            folderModel->appendRow(rootItem);
            copyFolders(topItem, rootItem);
        }
    }
    
    treeView->setModel(folderModel);
    treeView->setHeaderHidden(true);
    treeView->expandAll();
    treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(treeView);
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    
    auto selected = treeView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Move to Folder"), tr("No folder selected."));
        return;
    }
    
    auto selectedIndex = selected.first();
    auto selectedItem = folderModel->itemFromIndex(selectedIndex);
    if (!selectedItem) return;
    
    QString targetDir = selectedItem->data(PathRole).toString();
    
    // Check if target is Sample Charts
    if (targetDir.contains("user/") || targetDir.endsWith("user")) {
        QMessageBox::warning(
            this,
            tr("Move to Folder"),
            tr("Cannot move files to Sample Charts folder."));
        return;
    }
    
    // Move selected files
    moveSelected(targetDir);
}

void
AstroDatabase::handleItemRenamed(QStandardItem* item)
{
    qDebug() << "AstroDatabase::handleItemRenamed() called";
    
    if (!item) return;
    
    // Check if we're tracking a rename
    if (_renamingOldName.isEmpty()) {
        // Spurious change, ignore
        qDebug() << "No rename in progress, ignoring";
        return;
    }
    
    qDebug() << "Rename in progress. Old name:" << _renamingOldName << "New name:" << item->text();
    
    // Check if this is a file item (not a directory)
    auto type = entryType(item->data(TypeRole).toUInt());
    if (type != fileType) {
        _renamingOldName.clear();
        _renamingDir.clear();
        return;
    }
    
    QString newName = item->text();
    QString oldName = _renamingOldName;
    QString dir = _renamingDir;
    
    // Clear tracking variables
    _renamingOldName.clear();
    _renamingDir.clear();
    
    // Validate new name
    if (newName.isEmpty() || newName == oldName) {
        updateList();
        return;
    }
    
    AFileInfo oldFileInfo(dir, oldName);
    AFileInfo newFileInfo(dir, newName);
    
    // Check if target already exists
    if (newFileInfo.exists()) {
        QMessageBox::warning(
            this,
            tr("Rename Failed"),
            tr("A file named '%1' already exists.").arg(newName));
        updateList();
        return;
    }
    
    // Check if file is currently open
    AstroFile* openFile = nullptr;
    auto mainWin = qobject_cast<MainWindow*>(window());
    if (mainWin) {
        auto filesBar = mainWin->findChild<FilesBar*>();
        if (filesBar) {
            openFile = filesBar->findOpenFile(dir, oldName);
        }
    }
    
    // Perform the rename
    if (!QFile::rename(oldFileInfo.filePath(), newFileInfo.filePath())) {
        QMessageBox::warning(
            this,
            tr("Rename Failed"),
            tr("Could not rename file."));
        updateList();
        return;
    }
    
    // Update open file if needed
    if (openFile && mainWin) {
        openFile->setFileInfo(newFileInfo);
        auto filesBar = mainWin->findChild<FilesBar*>();
        if (filesBar) {
            filesBar->refreshTabForFile(openFile);
        }
    }
    
    // Refresh the list
    updateList();
    
    // Find and scroll to the renamed item
    for (int i = 0, n = dirModel->rowCount(); i < n; ++i) {
        auto dirItem = dirModel->item(i);
        if (!dirItem) continue;
        
        QString itemDir = dirItem->data(PathRole).toString();
        if (itemDir != dir) continue;
        
        // Search children for the new name
        for (int j = 0; j < dirItem->rowCount(); ++j) {
            auto child = dirItem->child(j);
            if (child && child->text() == newName) {
                QModelIndex sourceIndex = dirModel->indexFromItem(child);
                QModelIndex proxyIndex = searchProxy->mapFromSource(sourceIndex);
                fileList->scrollTo(proxyIndex);
                fileList->setCurrentIndex(proxyIndex);
                return;
            }
        }
    }
}

void
AstroDatabase::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Delete) {
        deleteSelected();
    } else if (e->key() == Qt::Key_F2) {
        // Start inline editing
        auto sm = fileList->selectionModel();
        if (sm && sm->hasSelection()) {
            auto sil = sm->selectedIndexes();
            if (sil.count() == 1) {
                auto proxyIndex = sil.first();
                auto sourceIndex = searchProxy->mapToSource(proxyIndex);
                auto item = dirModel->itemFromIndex(sourceIndex);
                
                if (item && item->parent()) { // Only files, not directories
                    auto type = entryType(item->data(TypeRole).toUInt());
                    if (type == fileType) {
                        // Store old name and directory for tracking
                        _renamingOldName = item->text();
                        _renamingDir = item->parent()->data(PathRole).toString();
                        
                        // Make this item temporarily editable
                        item->setFlags(item->flags() | Qt::ItemIsEditable);
                        fileList->edit(proxyIndex);
                    }
                }
            }
        }
    }
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

bool
AstroDatabase::validateDropTarget(const QPoint& pos, QString& targetDir)
{
    QModelIndex dropIndex = fileList->indexAt(pos);
    if (!dropIndex.isValid()) {
        return false;
    }

    auto sourceIndex = searchProxy->mapToSource(dropIndex);
    auto targetItem = dirModel->itemFromIndex(sourceIndex);
    if (!targetItem) {
        return false;
    }

    // Determine target directory
    auto type = entryType(targetItem->data(TypeRole).toUInt());
    
    if (type == dirType) {
        targetDir = targetItem->data(PathRole).toString();
    } else if (type == fileType && targetItem->parent()) {
        targetDir = targetItem->parent()->data(PathRole).toString();
    } else {
        return false;
    }

    // Don't allow moving to Sample Charts
    if (targetDir.contains("user/") || targetDir.endsWith("user")) {
        return false;
    }

    return true;
}

void
AstroDatabase::performMove(const QString& targetDir)
{
    // Check one more time if target is Sample Charts (for the error message)
    if (targetDir.contains("user/") || targetDir.endsWith("user")) {
        QMessageBox::information(
            this,
            tr("Move Failed"),
            tr("Cannot move files to the Sample Charts folder."));
        return;
    }
    
    moveSelected(targetDir);
}

void
AstroDatabase::performCopy(const QString& targetDir)
{
    // Check one more time if target is Sample Charts (for the error message)
    if (targetDir.contains("user/") || targetDir.endsWith("user")) {
        QMessageBox::information(
            this,
            tr("Copy Failed"),
            tr("Cannot copy files to the Sample Charts folder."));
        return;
    }
    
    copySelected(targetDir);
}

void
AstroDatabase::moveSelected(const QString& targetDir)
{
    auto sm = fileList->selectionModel();
    if (!sm) return;

    auto sil = sm->selectedIndexes();
    if (sil.isEmpty()) return;

    QStringList filesToMove;
    QStringList sourceNames;
    QString sourceDir;

    // Collect files to move
    for (const auto& proxyIndex : sil) {
        if (!proxyIndex.parent().isValid()) continue; // Skip directories for now
        
        auto si = searchProxy->mapToSource(proxyIndex);
        auto item = dirModel->itemFromIndex(si);
        if (!item || !item->parent()) continue;
        
        auto itemType = entryType(item->data(TypeRole).toUInt());
        if (itemType != fileType) continue;
        
        QString dir = item->parent()->data(PathRole).toString();
        QString name = item->text();
        
        if (sourceDir.isEmpty()) {
            sourceDir = dir;
        } else if (sourceDir != dir) {
            QMessageBox::warning(
                this,
                tr("Move Failed"),
                tr("Cannot move files from multiple directories at once."));
            return;
        }
        
        filesToMove << name;
        sourceNames << name;
    }

    if (filesToMove.isEmpty()) return;
    
    // Check if moving to same directory
    if (sourceDir == targetDir) {
        return; // Nothing to do
    }

    // Confirm move
    QString message;
    if (filesToMove.count() == 1) {
        message = tr("Move '%1' to '%2'?").arg(filesToMove.first()).arg(QDir(targetDir).dirName());
    } else {
        message = tr("Move %1 files to '%2'?").arg(filesToMove.count()).arg(QDir(targetDir).dirName());
    }
    
    QMessageBox msgBox;
    msgBox.setText(message);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Yes);
    int ret = msgBox.exec();
    
    if (ret == QMessageBox::Cancel) {
        return;
    }

    // Get MainWindow for checking open files
    auto mainWin = qobject_cast<MainWindow*>(window());
    FilesBar* filesBar = nullptr;
    if (mainWin) {
        filesBar = mainWin->findChild<FilesBar*>();
    }

    // Move files
    bool anyMoved = false;
    for (const QString& fileName : filesToMove) {
        AFileInfo oldFileInfo(sourceDir, fileName);
        AFileInfo newFileInfo(targetDir, fileName);

        // Check if target exists
        if (newFileInfo.exists()) {
            QMessageBox::warning(
                this,
                tr("Move Failed"),
                tr("A file named '%1' already exists in the target directory.").arg(fileName));
            continue;
        }

        // Check if file is open
        AstroFile* openFile = nullptr;
        if (filesBar) {
            openFile = filesBar->findOpenFile(sourceDir, fileName);
        }

        // Move the file
        if (!QFile::rename(oldFileInfo.filePath(), newFileInfo.filePath())) {
            QMessageBox::warning(
                this,
                tr("Move Failed"),
                tr("Could not move file '%1'.").arg(fileName));
            continue;
        }

        // Update open file if needed
        if (openFile) {
            openFile->setFileInfo(newFileInfo);
            if (filesBar) {
                filesBar->refreshTabForFile(openFile);
            }
        }

        anyMoved = true;
    }

    if (anyMoved) {
        updateList();
    }
}

void
AstroDatabase::copySelected(const QString& targetDir)
{
    auto sm = fileList->selectionModel();
    if (!sm) return;

    auto sil = sm->selectedIndexes();
    if (sil.isEmpty()) return;

    QStringList filesToCopy;
    QString sourceDir;

    // Collect files to copy
    for (const auto& proxyIndex : sil) {
        if (!proxyIndex.parent().isValid()) continue; // Skip directories for now
        
        auto si = searchProxy->mapToSource(proxyIndex);
        auto item = dirModel->itemFromIndex(si);
        if (!item || !item->parent()) continue;
        
        auto itemType = entryType(item->data(TypeRole).toUInt());
        if (itemType != fileType) continue;
        
        QString dir = item->parent()->data(PathRole).toString();
        QString name = item->text();
        
        if (sourceDir.isEmpty()) {
            sourceDir = dir;
        } else if (sourceDir != dir) {
            // Mixed source directories - skip for simplicity
            QMessageBox::warning(
                this,
                tr("Copy Failed"),
                tr("Cannot copy files from multiple directories at once."));
            return;
        }
        
        filesToCopy << name;
    }

    if (filesToCopy.isEmpty()) return;
    
    // Check if copying to same directory
    if (sourceDir == targetDir) {
        // Create copies with " - Copy" suffix
        QMessageBox msgBox;
        QString message;
        if (filesToCopy.count() == 1) {
            message = tr("Create copy of '%1' in same directory?").arg(filesToCopy.first());
        } else {
            message = tr("Create copies of %1 files in same directory?").arg(filesToCopy.count());
        }
        msgBox.setText(message);
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Yes);
        int ret = msgBox.exec();
        
        if (ret == QMessageBox::Cancel) {
            return;
        }
    } else {
        // Confirm copy to different directory
        QString message;
        if (filesToCopy.count() == 1) {
            message = tr("Copy '%1' to '%2'?").arg(filesToCopy.first()).arg(QDir(targetDir).dirName());
        } else {
            message = tr("Copy %1 files to '%2'?").arg(filesToCopy.count()).arg(QDir(targetDir).dirName());
        }
        
        QMessageBox msgBox;
        msgBox.setText(message);
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Yes);
        int ret = msgBox.exec();
        
        if (ret == QMessageBox::Cancel) {
            return;
        }
    }

    // Copy files
    bool anyCopied = false;
    for (const QString& fileName : filesToCopy) {
        AFileInfo sourceFileInfo(sourceDir, fileName);
        QString targetFileName = fileName;
        
        // If copying to same directory, add " - Copy" suffix
        if (sourceDir == targetDir) {
            // Find unique name with " - Copy" or " - Copy (n)" suffix
            int copyNum = 1;
            AFileInfo testInfo(targetDir, fileName + " - Copy");
            if (!testInfo.exists()) {
                targetFileName = fileName + " - Copy";
            } else {
                while (true) {
                    testInfo = AFileInfo(targetDir, fileName + QString(" - Copy (%1)").arg(copyNum));
                    if (!testInfo.exists()) {
                        targetFileName = fileName + QString(" - Copy (%1)").arg(copyNum);
                        break;
                    }
                    copyNum++;
                }
            }
        }
        
        AFileInfo targetFileInfo(targetDir, targetFileName);

        // Check if target exists (shouldn't happen with our naming scheme for same dir)
        if (targetFileInfo.exists() && sourceDir != targetDir) {
            QMessageBox::warning(
                this,
                tr("Copy Failed"),
                tr("A file named '%1' already exists in the target directory.").arg(targetFileName));
            continue;
        }

        // Copy the file
        if (!QFile::copy(sourceFileInfo.filePath(), targetFileInfo.filePath())) {
            QMessageBox::warning(
                this,
                tr("Copy Failed"),
                tr("Could not copy file '%1'.").arg(fileName));
            continue;
        }

        anyCopied = true;
    }

    if (anyCopied) {
        updateList();
    }
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

AstroFile*
FilesBar::findOpenFile(const QString& dir, const QString& name)
{
    for (int i = 0; i < count(); ++i) {
        if (i >= files.count()) continue;
        const auto& tabFiles = files[i];
        if (!tabFiles.isEmpty()) {
            auto file = tabFiles.first();
            if (file && file->fileInfo().baseName() == name &&
                file->fileInfo().path() == dir) {
                return file;
            }
        }
    }
    return nullptr;
}

void
FilesBar::refreshTabForFile(AstroFile* file)
{
    if (!file) return;
    
    for (int i = 0; i < count(); ++i) {
        if (i >= files.count()) continue;
        const auto& tabFiles = files[i];
        if (!tabFiles.isEmpty() && tabFiles.first() == file) {
            updateTab(i);
            return;
        }
    }
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

    if (currentFiles().count() == 0 || currentFiles()[0]->hasUnsavedChanges()) {
        openFileInNewTab(fi);
    } else {
        currentFiles()[0]->load(fi);
    }
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
FilesBar::saveAsCurrentFile()
{
    if (currentFiles().isEmpty()) return;
    
    AstroFile* file = currentFiles().first();
    if (!file) return;
    
    file->saveAs();
    
    // Update the tab with the new name
    int idx = currentIndex();
    if (idx >= 0) {
        updateTab(idx);
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
    connect(astroDatabase,
            SIGNAL(saveCurrentToDirectory(const QString&)),
            this,
            SLOT(handleSaveToDirectory(const QString&)));
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
MainWindow::handleSaveToDirectory(const QString& directory)
{
    // Get the current file
    auto files = filesBar->currentFiles();
    if (files.isEmpty() || !files[0]) {
        QMessageBox::warning(this,
                             tr("No Chart Open"),
                             tr("Please open a chart before saving to a directory."));
        return;
    }

    AstroFile* currentFile = files[0];
    
    // Get the current file info
    AFileInfo currentInfo = currentFile->fileInfo();
    QString fileName = currentInfo.fileName();
    
    // Create new file info with target directory
    QDir targetDir(directory);
    AFileInfo newInfo(targetDir, fileName);
    
    // Update the file's location
    currentFile->setFileInfo(newInfo);
    
    // Save the file
    currentFile->save();
    
    // Refresh the database to show the file in the new location
    astroDatabase->updateList();
    
    statusBar()->showMessage(tr("Chart saved to %1").arg(directory), 3000);
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

    // Set dialog size to match background image aspect ratio (640x400 = 16:10)
    d->setMinimumSize(640, 500);  // Minimum size to ensure background covers dialog
    d->resize(800, 600);           // Default size - larger but maintains aspect ratio
    
    // Set background image with scaling using palette instead of CSS
    QPalette palette;
    QPixmap background("style/about.jpg");
    palette.setBrush(QPalette::Window, QBrush(background.scaled(d->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)));
    d->setPalette(palette);
    d->setAutoFillBackground(true);
    
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
        "<center><b><big>" + QApplication::applicationVersion() + "</big></b>"
        "<br><b>Zodiac Sidereal</b></center>"
        "<p>" + tr("Astrological software for personal use.") + "</p>"
        "<p><b>Original Application:</b><br>"
        "Copyright © 2012-2014 Artem Vasilev<br>"
        "<a style='color:yellow' href='mailto:atten@syslog.pro'>atten@syslog.pro</a></p>"
        "<p><b>Sidereal Branch Enhancements:</b><br>"
        "Copyright © 2016-2025 Josh Baudhuin/Turtle Crescent Graphics<br>"
        "<a style='color:yellow' href='https://github.com/jbaudhuin/joshb-work-sidereal'>github.com/jbaudhuin/joshb-work-sidereal</a></p>"
        "<p><b>Key Features Added:</b><br>"
        "• Sidereal zodiac support (Fagan-Bradley & Lahiri ayanamshas)<br>"
        "• Harmonic charts and patterns (H1-H32)<br>"
        "• Fixed star conjunctions and parans<br>"
        "• Primary directions and speculum<br>"
        "• Comprehensive event search and timing<br>"
        "• Transit and progression tracking<br>"
        "• Equatorial and prime vertical aspects</p>"
        "<center>" + tr(
            "This application is provided AS IS with ABSOLUTELY NO WARRANTY. "
            "It is distributed in the hope that it will be useful, but WITHOUT "
            "ANY WARRANTY; without even the implied warranty of MERCHANTABILITY "
            "or FITNESS FOR A PARTICULAR PURPOSE.")
        + "<br><br>"
        "<b>License:</b> GNU General Public License v3<br>"
        "This is free software: you are free to use, modify, and redistribute it.</center>");

    l2->setText(
        "<center><b>Third-Party Libraries & Credits</b></center>"
        "<p><b>Swiss Ephemeris</b><br>"
        "© Astrodienst AG, Switzerland. All rights reserved.<br>"
        "Authors: Dieter Koch and Alois Treindl<br>"
        "Licensed under GPL v2+<br>"
        "<a style='color:yellow' href='https://www.astro.com/swisseph/'>www.astro.com/swisseph/</a><br>"
        "See swe/LICENSE for details</p>"
        "<p><b>Qt Framework</b><br>"
        "The Qt Company Ltd.<br>"
        "Licensed under LGPL v3<br>"
        "<a style='color:yellow' href='https://www.qt.io/'>www.qt.io</a></p>"
        "<p><b>Boost C++ Libraries</b><br>"
        "Boost Software License<br>"
        "<a style='color:yellow' href='https://www.boost.org/'>www.boost.org</a></p>"
        "<p><b>Primo Icon Set</b><br>"
        "Webdesigner Depot<br>"
        "<a style='color:yellow' href='https://www.iconfinder.com/iconsets/Primo_Icons'>www.iconfinder.com/iconsets/Primo_Icons</a></p>"
        "<p><b>Additional Thanks:</b><br>"
        "SymSolon project contributors<br>"
        "<a style='color:yellow' href='http://sf.net/projects/symsolon'>sf.net/projects/symsolon</a></p>");

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
