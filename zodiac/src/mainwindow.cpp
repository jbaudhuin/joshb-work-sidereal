﻿#include <QToolBar>
#include <QToolButton>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTreeWidget>
#include <QHeaderView>
#include <QKeyEvent>
#include <QShortcut>
#include <QScrollArea>
#include <QMenu>
#include <QDir>
#include <QDialogButtonBox>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QGraphicsBlurEffect>
#include <QWidget>
#include <math.h>
#include <QDebug>
#include <QMetaObject>
#include "../plain/src/plain.h"
#include "../chart/src/chart.h"
#include "../astroprocessor/src/astro-calc.h"
#include "../astroprocessor/src/astro-data.h"
#include "../fileeditor/src/fileeditor.h"
#include "../fileeditor/src/geosearch.h"
#include "../planets/src/planets.h"
#include "../details/src/details.h"
#include "../details/src/harmonics.h"
#include "mainwindow.h"


/* =========================== ASTRO FILE INFO ====================================== */

AstroFileInfo::AstroFileInfo(QWidget *parent) : AstroFileHandler(parent)
{
    currentIndex = 0;

    edit = new QPushButton(this);
    shadow = new QLabel(this);
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

void AstroFileInfo::refresh()
{
    qDebug() << "AstroFileInfo::refresh";
    QDateTime dt = currentFile()->getLocalTime();

    QString date = dt.date().toString(Qt::DefaultLocaleShortDate);
    QString dayOfWeek = dt.date().toString("ddd");
    QString time = dt.time().toString();

    QString age;
    if (showAge)
    {
        float a1 = dt.daysTo(QDateTime::currentDateTime()) / 365.25;
        char a[7];
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
    if (currentFile()->getLocationName().isEmpty())
    {
        QString longitude = A::degreeToString(currentFile()->getLocation().x(), A::HighPrecision);
        QString latitude = A::degreeToString(currentFile()->getLocation().y(), A::HighPrecision);
        place = QString("%1N  %2E").arg(latitude, longitude);
    } else
    {
        place = currentFile()->getLocationName();
    }


    setText(QString("%1\n").arg(currentFile()->getName()) +
            tr("%1 %2 %3 (%4)%5\n").arg(date, dayOfWeek, time, timezone, age) +
            place);
}

void AstroFileInfo::filesUpdated(MembersList m)
{
    if (currentIndex >= filesCount()) {
        setText("");
        return;
    }

    if (m[currentIndex] & (AstroFile::Name
                           | AstroFile::GMT
                           | AstroFile::Timezone
                           | AstroFile::Location
                           | AstroFile::LocationName))
        refresh();
}

void AstroFileInfo::setText(const QString& str)
{
    edit->setText(str);
    shadow->setText(str);
}

AppSettings AstroFileInfo::defaultSettings()
{
    AppSettings s;
    s.setValue("age", true);
    return s;
}

AppSettings AstroFileInfo::currentSettings()
{
    AppSettings s;
    s.setValue("age", showAge);
    return s;
}

void AstroFileInfo::applySettings(const AppSettings& s)
{
    showAge = s.value("age").toBool();
    if (currentIndex < filesCount()) refresh();
}

void AstroFileInfo::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addCheckBox("age", tr("Show age:"));
}


/* =========================== ASTRO WIDGET ========================================= */

AstroWidget::AstroWidget(QWidget *parent) : QWidget(parent)
{
    editor = nullptr;

    toolBar = new QToolBar(tr("Slides"), this);
    actionGroup = new QActionGroup(this);

    geoWdg = new GeoSearchWidget;
    slides = new SlideWidget;
    fileView = new AstroFileInfo;
    fileView2nd = new AstroFileInfo;

    toolBar->setObjectName("slides");
    actionGroup->setExclusive(true);
    slides->setTransitionEffect(SlideWidget::Transition_HorizontalSlide);
    fileView2nd->setStatusTip(tr("Background data"));
    fileView2nd->setCurrentIndex(1);
    fileView2nd->setObjectName("secondFile");

    QGridLayout* layout = new QGridLayout(this);
    layout->setMargin(0);
    layout->addWidget(slides, 0, 0, 1, 1);
    layout->addWidget(fileView, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(fileView2nd, 0, 0, 1, 1, Qt::AlignRight | Qt::AlignTop);

    addDockWidget(new Details, tr("Details"), true/*scrollable*/);
    addDockWidget(new Harmonics, tr("Harmonics"), false/*not scrollable*/);
    addSlide(new Chart, QIcon("style/natal.png"), tr("Chart"));
    addSlide(new Planets, QIcon("style/planets.png"), tr("Planets"));
    addSlide(new Plain, QIcon("style/plain.png"), tr("Text"));
    addHoroscopeControls();

    connect(fileView, SIGNAL(clicked()), this, SLOT(openEditor()));
    connect(fileView2nd, SIGNAL(clicked()), this, SLOT(openEditor()));
    connect(slides, SIGNAL(currentSlideChanged()), this, SLOT(currentSlideChanged()));
}

void AstroWidget::setupFile(AstroFile* file, bool suspendUpdate)
{
    if (!file) return;
    bool hasChanges = file->hasUnsavedChanges();
    file->suspendUpdate();

    if (file->getGMT() == QDateTime::fromTime_t(0))  // set current date, time, timezone
    {
        QDateTime current = QDateTime::currentDateTime();
        QTimeZone tz = current.timeZone();
        QDateTime currentUTC = QDateTime(current.toUTC().date(), current.toUTC().time(), Qt::UTC);
        file->setGMT(currentUTC);
        file->setTimezone(tz.offsetFromUtc(currentUTC)/3600.);
    }

    if (file->getLocation().isNull())                // set default location
    {
        file->setLocation(geoWdg->location());
        file->setLocationName(geoWdg->locationName());
    }

    file->setZodiac(zodiacSelector->itemData(zodiacSelector->currentIndex()).toInt());   // set zodiac
    file->setHouseSystem(hsystemSelector->itemData(hsystemSelector->currentIndex()).toInt()); // set house system
    file->setAspectSet(aspectsSelector->itemData(aspectsSelector->currentIndex()).toInt(), _dynAspChange); // set aspect set
    file->setAspectMode(A::aspectModeEnum(aspectModeSelector->currentIndex()));    // aspect mode
    auto val = harmonicSelector->currentText();
    bool ok = false;
    auto h = val.toDouble(&ok);
    if (!ok) {
        auto ops = val.split(QRegExp("\\s*\\*\\s*"));
        if (ops.size()>=2) {
            double v = 1;
            for (auto m : ops) {
                auto mv = m.toDouble(&ok);
                if (ok) v *= mv; else break;
            }
            if (ok) file->setHarmonic(v);
        } else {
            ops = val.split(QRegExp("\\s*/\\s*"));
            if (ops.size()>=2) {
                double v = ops.takeFirst().toDouble(&ok);
                for (auto m : ops) {
                    auto mv = m.toDouble(&ok);
                    if (std::abs(mv)<=std::numeric_limits<double>::epsilon()) {
                        ok = false; break;
                    }
                    if (ok) v /= mv; else break;
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

void AstroWidget::switchToSingleAspectSet()
{
#if 1
    A::setOrbFactor(1);
#else
    aspectsSelector->blockSignals(true);
    A::AspectSetId set = aspectsSelector->itemData(aspectsSelector->currentIndex()).toInt();
    A::AspectSetId set2 = sqrtf(set);
    int itemIndex = aspectsSelector->findData(set2);
    if (set2 * set2 == set && itemIndex >= 0)
    {
        qDebug() << "AstroWidget::restore aspect set to single";
        aspectsSelector->setCurrentIndex(itemIndex);
    }
    aspectsSelector->blockSignals(false);
#endif
}

void AstroWidget::switchToSynastryAspectSet()
{
#if 1
    A::setOrbFactor(.25);
#else
    aspectsSelector->blockSignals(true);
    A::AspectSetId set = aspectsSelector->itemData(aspectsSelector->currentIndex()).toInt();
    A::AspectSetId set2 = set * set;
    int itemIndex = aspectsSelector->findData(set2);
    if (itemIndex >= 0)
    {
        qDebug() << "AstroWidget::replace aspect set to synastry";
        aspectsSelector->setCurrentIndex(itemIndex);
    }
    aspectsSelector->blockSignals(false);
#endif
}

void AstroWidget::setFiles(const AstroFileList& files)
{
    if (files.count() == 2) {
        switchToSynastryAspectSet();
    } else if (files.count() == 1) {
        switchToSingleAspectSet();
    }

    for (AstroFile* i : files) setupFile(i, true);

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
}

void AstroWidget::openEditor()
{
    if (editor) {
        editor->raise();
    } else {
        editor = new AstroFileEditor();
        editor->setFiles(files());
        editor->move((topLevelWidget()->width() - editor->width()) / 2 + topLevelWidget()->geometry().left(),
            (topLevelWidget()->height() - editor->height()) / 2 + topLevelWidget()->geometry().top());
        editor->show();
        connect(editor, SIGNAL(appendFile()), this, SIGNAL(appendFileRequested()));
        connect(editor, SIGNAL(swapFiles(int, int)), this, SIGNAL(swapFilesRequested(int, int)));
        connect(editor, SIGNAL(windowClosed()), this, SLOT(destroyEditor()));
    }

    if (sender() == fileView)
        editor->setCurrentFile(0);
    else if (sender() == fileView2nd)
        editor->setCurrentFile(1);
}

void
AstroWidget::setHarmonic(double h)
{
    QString ns = QString::number(h);
    int i = harmonicSelector->findText(ns);
    if (i == -1) {
        harmonicSelector->addItem(ns);
        i = harmonicSelector->findText(ns);
    } 
    if (i != -1) {
        harmonicSelector->setCurrentIndex(i);
    }
}

void AstroWidget::destroyingFile()
{
    if (auto file = qobject_cast<AstroFile*>(sender())) {
        if (!files().contains(file) || files().count() > 2) return;
        switchToSingleAspectSet();
    }
}

void AstroWidget::destroyEditor()
{
    editor->deleteLater();
    editor = nullptr;
}

void
AstroWidget::addSlide(AstroFileHandler* w,
                      const QIcon& icon,
                      QString title)
{
    qDebug() << "added slide" << w << title;
    QAction* act = toolBar->addAction(icon, title, this, SLOT(toolBarActionClicked()));
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
        qDebug() << w << "connected planetSelected() to" << wdg;
        connect(w, SIGNAL(planetSelected(A::PlanetId, int)),
                wdg, SLOT(setCurrentPlanet(A::PlanetId, int)));
    }

    for (QDockWidget* d : docks) {
        qDebug() << w << "connected planetSelected() to dock" << d;
        connect(w, SIGNAL(planetSelected(A::PlanetId, int)),
                d, SLOT(show()));
    }
}

void AstroWidget::addDockWidget(AstroFileHandler* w, QString title, bool scrollable, QString objectName)
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
        connect(o, SIGNAL(planetSelected(A::PlanetId, int)),
                w, SLOT(setCurrentPlanet(A::PlanetId, int)));
        connect(w, SIGNAL(planetSelected(A::PlanetId, int)),
                o, SLOT(setCurrentPlanet(A::PlanetId, int)));
    }
    docks << dock;
    dockHandlers << w;
    attachHandler(w);
}

void AstroWidget::attachHandler(AstroFileHandler* w)
{
    handlers << w;
    connect(w, SIGNAL(requestHelp(QString)), this, SIGNAL(helpRequested(QString)));
}

void AstroWidget::addHoroscopeControls()
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
    harmonicSelector->setValidator(new QDoubleValidator(1, 360*360, 4, this));

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
    QStringList ssl {
        "QToolBar { padding: 0px; }",
        "QToolBar#dynAspectControls QToolButton { "
            "padding: 0px; margin: 0px; border-width: 0px; "
            //"width: 15px; "
            "max-width: 45px; "
            "min-width: 15px; "
        "}",
    };

    auto& dasps = A::getAspectSet(daspId);
    QMapIterator<A::AspectId,A::AspectType> dit(dasps.aspects);
    for (unsigned i = 1; i <= 32; ++i) {
        auto num = QString::number(i);
        auto act = dynAspectControls->addAction(num);
        act->setObjectName("hasp" + num);
        act->setCheckable(true);
        act->setChecked(A::dynAspState(i));

        auto facs = A::getPrimeFactors(i);
        if (facs.size()>1) {
            QStringList sl;
            for (auto u : facs) sl << QString::number(u);
            sl = QStringList() << sl.join("x");
            while (dit.hasNext()
                   && dit.peekNext().value()._harmonic != i)
            { dit.next(); }
            while (dit.hasNext() && dit.peekNext().value()._harmonic==i) {
                const auto& asp = dit.next().value();
                sl << asp.name;
            }
            act->setToolTip(sl.join("<br>"));
        }

        auto btn = dynAspectControls->widgetForAction(act);
        btn->setObjectName("hasp" + num + "btn");
        QColor clr = A::getHarmonicColor(i);
        double luma = 0.2126 * clr.redF()
                     + 0.7152 * clr.greenF()
                     + 0.0722 * clr.blueF();
        bool useBlack = ( luma > 0.5 );
        QColor darker = clr.darker();
        auto style =
                QString("QToolButton:checked "
                        "{ background-color: %1;  color: %2; } "
                        "QToolButton { background-color: %3; color: %4; }")
                .arg(clr.name(QColor::HexArgb))
                .arg((useBlack? "black" : "white"))
                .arg(darker.name(QColor::HexArgb))
                .arg((useBlack? "darkgray" : "lightgray"));
        btn->setStyleSheet(style);
        btn->setMaximumWidth(20);

        connect(act, &QAction::toggled,
                [this, &dasps, i](bool b)
        {
            //bool b = act->isChecked();
            if (A::dynAspState(i) == b) return;

            A::setDynAspState(i, b);
            bool seen = false;
            for (auto aid : dasps.aspects.keys()) {
                A::AspectType& asp = dasps.aspects[aid];
                if (asp._harmonic == i) {
                    seen = true;
                    asp.setEnabled(b);
                } else if (seen) break;    // assumes they're bunched...
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
        //act->dumpObjectInfo();
    }
    auto ssh = ssl.join(" ");
    dynAspectControls->setStyleSheet(ssh);
#if 0
    qDebug() << ssh;
    dynAspectControls->dumpObjectInfo();
    dynAspectControls->dumpObjectTree();
#endif

    for (unsigned i = A::amcGreatCircle; i < A::amcEND; ++i) {
        aspectModeSelector->addItem(A::aspectModeType::toUserString(i),
                                    int(i));
    }

    for (int i = 1; i <= 16; ++i) {
        harmonicSelector->addItem(QString::number(i));
    }

    horoscopeControls
            << zodiacSelector
            << hsystemSelector
            << aspectsSelector
            << aspectModeSelector
            << harmonicSelector;

    harmonicSelector->setEditable(true);

    for (QWidget* w: horoscopeControls) {
        if (auto c = qobject_cast<QComboBox*>(w)) {
            c->setEditable(c == harmonicSelector);
            connect(c, SIGNAL(currentIndexChanged(int)),
                    this, SLOT(horoscopeControlChanged()));
        }
    }
}

void AstroWidget::toolBarActionClicked()
{
    QAction* s = static_cast<QAction*>(sender());
    int i = toolBar->actions().indexOf(s);
    slides->setSlide(i);
}

void AstroWidget::currentSlideChanged()
{
    fileView2nd->setVisible(slides->currentIndex() == 0);   // show background data only in chart view
}

void AstroWidget::applyGeoSettings(AppSettings& s)
{
    s.setValue("Scope/defaultLocation", vectorToString(geoWdg->location()));
    s.setValue("Scope/defaultLocationName", geoWdg->locationName());
}

QString AstroWidget::vectorToString(const QVector3D& v)
{
    return QString("%1 %2 %3").arg(v.x()).arg(v.y()).arg(v.z());
}

QVector3D AstroWidget::vectorFromString(const QString& str)
{
    QVector3D ret;
    ret.setX(str.section(" ", 0, 0).toFloat());
    ret.setY(str.section(" ", 1, 1).toFloat());
    ret.setZ(str.section(" ", 2, 2).toFloat());
    return ret;
}

void AstroWidget::horoscopeControlChanged()
{
    for (AstroFile* i : files())
        setupFile(i, true);

    for (AstroFile* i : files())
        i->resumeUpdate();
}

void AstroWidget::aspectSelectionChanged()
{

}

AppSettings AstroWidget::defaultSettings()
{
    AppSettings s;

    s << fileView->defaultSettings();

    for (AstroFileHandler* h : handlers)
        s << h->defaultSettings();

    s.setValue("Scope/defaultLocation", "37.6184 55.7512 0");
    s.setValue("Scope/defaultLocationName", "Moscow, Russia");
    s.setValue("Scope/zodiac", 0);          // indexes of ComboBox items, not values itself
    s.setValue("Scope/houseSystem", 0);
    s.setValue("Scope/aspectSet", 0);
    s.setValue("Scope/dynamic", "all");
    s.setValue("Scope/aspectMode", 1);   // ecliptic
    s.setValue("slide", slides->currentIndex());    // чтобы не возвращалась к первому слайду после сброса настроек
    s.setValue("harmonic", 1);
    return s;
}

AppSettings
AstroWidget::currentSettings()
{
    AppSettings s;

    s << fileView->currentSettings();

    for (AstroFileHandler* h : handlers)
        s << h->currentSettings();

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

void AstroWidget::applySettings(const AppSettings& s)
{
    geoWdg->setLocation(vectorFromString(s.value("Scope/defaultLocation").toString()));
    geoWdg->setLocationName(s.value("Scope/defaultLocationName").toString());

    zodiacSelector->setCurrentIndex(s.value("Scope/zodiac").toInt());
    hsystemSelector->setCurrentIndex(s.value("Scope/houseSystem").toInt());
    aspectsSelector->setCurrentIndex(s.value("Scope/aspectSet").toInt());
    aspectModeSelector->setCurrentIndex(s.value("Scope/aspectMode").toInt());

    QString harm = s.value("harmonic", 1).toString();
    int index = harmonicSelector->findText(harm);
    if (index == -1) {
        harmonicSelector->addItem(harm);
        index = harmonicSelector->findText(harm);
    }
    harmonicSelector->setCurrentIndex(index);

    if (auto dactrls = getDynAspectControls()) {
        A::modalize<bool> inChange(_dynAspChange, true);
        A::setDynAspState(s.value("Scope/dynamic"));
        for (unsigned i = 1; i <= 32; ++i) {
            if (auto act =
                dactrls->findChild<QAction*>(QString("hasp%1").arg(i)))
            {
                bool b = A::dynAspState(i);
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

    for (AstroFile* i : files())
        setupFile(i);

    for (AstroFileHandler* h : handlers)
        h->applySettings(s);
}

void AstroWidget::setupSettingsEditor(AppSettingsEditor* ed)
{
    //ed->addTab(tr("Data"));

    fileView->setupSettingsEditor(ed);
    ed->addCustomWidget(geoWdg, tr("Default location:"), SIGNAL(locationChanged()));

    for (AstroFileHandler* h : handlers)
        h->setupSettingsEditor(ed);

    connect(ed, SIGNAL(apply(AppSettings&)), this, SLOT(applyGeoSettings(AppSettings&)));
}


/* =========================== ASTRO FILE DATABASE ================================== */

AstroDatabase::AstroDatabase(QWidget *parent) : QFrame(parent)
{
    QPushButton* refresh = new QPushButton;

    fileList = new QTreeWidget;
    search = new QLineEdit;

    refresh->setIcon(QIcon("style/update.png"));
    refresh->setToolTip(tr("Refresh"));
    refresh->setCursor(Qt::PointingHandCursor);

    fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileList->viewport()->installEventFilter(this); // for handling middle mouse button clicks
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
    connect(this, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(showContextMenu(QPoint)));
    connect(fileList, SIGNAL(doubleClicked(QModelIndex)),
            this, SLOT(openSelected()));
    connect(search, SIGNAL(textChanged(QString)),
            this, SLOT(searchFilter(QString)));

    updateList();
}

void AstroDatabase::searchFilter(QString s)
{
    auto top = fileList->invisibleRootItem();
    for (int i = 0; i < top->childCount(); i++) {
        auto item = top->child(i);
        item->setHidden(!item->text(0).contains(s, Qt::CaseInsensitive));
    }
}

QStringList
getSelectedItems(QTreeWidgetItem* tw)
{
    QStringList sl;
    for (int i = 0; i < tw->childCount(); ++i) {
        if (tw->child(i)->isSelected()) sl << tw->child(i)->text(0);
    }
    return sl;
}

void AstroDatabase::updateList()
{
    QDir dir("user/");

    QStringList list = dir.entryList(QStringList("*.dat"),
                                     QDir::Files, QDir::Name | QDir::IgnoreCase);
    list.replaceInStrings(".dat", "");

    auto top = fileList->invisibleRootItem();
    QStringList selectedItems(getSelectedItems(top));

    fileList->clear();

    for (const auto& itemString : list) {
        auto item = new QTreeWidgetItem(top, {itemString});
        if (selectedItems.contains(itemString))
            item->setSelected(true);
    }

    searchFilter(search->text());
}

void AstroDatabase::deleteSelected()
{
    int count = fileList->selectedItems().count();
    if (!count) return;

    QMessageBox msgBox;
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);

    if (count == 1)
        msgBox.setText(tr("Delete '%1' from list?").arg(fileList->selectedItems()[0]->text(0)));
    else
        msgBox.setText(tr("Delete %1 files from list?").arg(count));


    int ret = msgBox.exec();
    if (ret == QMessageBox::Cancel) return;

    for (auto item : fileList->selectedItems()) {
        QString file = "user/" + item->text(0) + ".dat";
        QFile::remove(file);
        emit fileRemoved(item->text(0));
        fileList->removeItemWidget(item, 0);
        delete item;
    }
}

void
AstroDatabase::openSelected()
{
    int count = fileList->selectedItems().count();
    if (!count) return;

    if (count == 1) {
        emit openFile(fileList->selectedItems()[0]->text(0));
    } else {
        for (auto item : fileList->selectedItems()) {
            emit openFileInNewTab(item->text(0));
        }
    }
}

void
AstroDatabase::openSelectedInNewTab()
{
    for (auto item : fileList->selectedItems())
        emit openFileInNewTab(item->text(0));
}

void
AstroDatabase::openSelectedWithTransits()
{
    for (auto item : fileList->selectedItems())
        emit openFileInNewTabWithTransits(item->text(0));
}

void AstroDatabase::openSelectedAsSecond()
{
    auto item = fileList->selectedItems().first();
    emit openFileAsSecond(item->text(0));
}

void AstroDatabase::openSelectedComposite()
{
    QStringList items;
    for (auto item : fileList->selectedItems())
        items << item->text(0);
    emit openFilesComposite(items);
}

void
AstroDatabase::findSelectedDerived()
{
    auto item = fileList->selectedItems().first()->text(0);
    emit findSelectedDerived(item);
}

void AstroDatabase::openSelectedWithSolarReturn()
{
    int count = fileList->selectedItems().count();
    if (!count) return;

    if (count == 1) {
        QString file(fileList->selectedItems().first()->text(0));
        emit openFileInNewTabWithReturn(file);
    } else {
        for (auto item : fileList->selectedItems()) {
            emit openFileInNewTabWithReturn(item->text(0));
        }
    }
}

void AstroDatabase::openSelectedSolarReturnInNewTab()
{
    for (auto item : fileList->selectedItems()) {
        emit openFileReturn(item->text(0));
    }
}

void AstroDatabase::showContextMenu(QPoint p)
{
    QMenu* mnu = new QMenu(this);
    QPoint pos = ((QWidget*)sender())->mapToGlobal(p);

    mnu->addAction(tr("Open"), this, SLOT(openSedlected()));
    mnu->addAction(tr("Open in new tab"), this, SLOT(openSelectedInNewTab()));
    mnu->addAction(tr("Open with Transits"), this, SLOT(openSelectedWithTransits()));
    mnu->addAction(tr("Synastry view"), this, SLOT(openSelectedAsSecond()));

    mnu->addAction(tr("Composite"), this, SLOT(openSelectedComposite()));
    mnu->addAction(tr("Open Derived..."), this, SLOT(findSelectedDerived()));
    mnu->addSeparator();
    mnu->addAction(tr("Open with Solar Return"),
                   this, SLOT(openSelectedWithSolarReturn()));
    mnu->addAction(tr("Open Solar Return in new tab"),
                   this, SLOT(openSelectedSolarReturnInNewTab()));
    mnu->addAction(tr("Open Lunar Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Moon");
    });

    mnu->addAction(tr("Open Mercury Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Mercury");
    });

    mnu->addAction(tr("Open Venus Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Venus");
    });

    mnu->addAction(tr("Open Mars Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Mars");
    });

    mnu->addAction(tr("Open Jupiter Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Jupiter");
    });

    mnu->addAction(tr("Open Saturn Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Saturn");
    });

    mnu->addAction(tr("Open Uranus Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Uranus");
    });

    mnu->addAction(tr("Open Neptune Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Neptune");
    });

    mnu->addAction(tr("Open Pluto Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Pluto");
    });

    mnu->addAction(tr("Open Chiron Return in new tab"),
                   [this]()
    {
        for (auto item: fileList->selectedItems())
            emit openFileReturn(item->text(0), "Chiron");
    });

    mnu->addSeparator();
    mnu->addAction(QIcon("style/delete.png"), tr("Delete"),
                   this, SLOT(deleteSelected()));

    mnu->exec(pos);
    mnu->deleteLater();
}

void AstroDatabase::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Delete)
        deleteSelected();
}

bool AstroDatabase::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonRelease &&
        ((QMouseEvent*)e)->button() == Qt::MiddleButton)
    {
        openSelectedInNewTab();
        return true;
    }
    return QObject::eventFilter(o, e);
}


/* =========================== FILES BAR ============================================ */

FilesBar::FilesBar(QWidget *parent) : QTabBar(parent)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    connect(this, SIGNAL(tabMoved(int, int)), this, SLOT(swapTabs(int, int)));
    connect(this, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));
}

int FilesBar::getTabIndex(AstroFile* f, bool seekFirstFileOnly)
{
    for (int i = 0; i < count(); i++)
        for (int j = 0; j < (seekFirstFileOnly ? 1 : files[i].count()); j++)
            if (f == files[i][j])
                return i;
    return -1;
}

int FilesBar::getTabIndex(QString name, bool seekFirstFileOnly)
{
    for (int i = 0; i < count(); i++)
        for (int j = 0; j < (seekFirstFileOnly ? 1 : files[i].count()); j++)
            if (name == files[i][j]->getName())
                return i;
    return -1;
}

void FilesBar::addFile(AstroFile* file)
{
    if (!file)
    {
        qWarning() << "FilesBar::addFile: failed to add an empty file";
        return;
    }

    AstroFileList list;
    list << file;
    files << list;
    file->setParent(this);
    addTab("new");
    updateTab(count() - 1);
    setCurrentIndex(count() - 1);

    connect(file, SIGNAL(changed(AstroFile::Members)), this, SLOT(fileUpdated(AstroFile::Members)));
    connect(file, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
}

void
FilesBar::editNewChart()
{
    auto dlg = new QDialog();
    auto lay = new QVBoxLayout(dlg);
    dlg->setLayout(lay);
    auto pafe = new AstroFileEditor(dlg);
    auto f = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(f);
    pafe->setFiles( {f} );
    lay->addWidget(pafe);
    pafe->layout()->setMargin(0);
    auto dbb = new QDialogButtonBox(QDialogButtonBox::Ok
                                    |QDialogButtonBox::Cancel,
                                    dlg);
    lay->addWidget(dbb);

    dlg->adjustSize();
    dlg->move((topLevelWidget()->width() - dlg->width())/2
             + topLevelWidget()->geometry().left(),
             (topLevelWidget()->height() - dlg->height())/2
             + topLevelWidget()->geometry().top());

    auto aw = topLevelWidget()->findChild<AstroWidget*>();
    qDebug() << aw;
    connect(dbb, SIGNAL(accepted()), dlg, SLOT(accept()));
    connect(dbb, SIGNAL(rejected()), dlg, SLOT(reject()));
    connect(dlg, &QDialog::accepted,
            [this,pafe] { pafe->applyToFile(); addFile(pafe->file()); });
    connect(dlg, SIGNAL(rejected()), dlg, SLOT(destroyLater()));
    connect(dlg, SIGNAL(accepted()), dlg, SLOT(destroyLater()));
    dlg->open();
}

void
FilesBar::findChart()
{
    auto dlg = new QDialog();
    auto lay = new QVBoxLayout(dlg);
    dlg->setLayout(lay);
    auto pafe = new AstroFindEditor(dlg);
    auto f = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(f);
    pafe->setFiles( {f} );
    lay->addWidget(pafe);
    pafe->layout()->setMargin(0);
    auto dbb = new QDialogButtonBox(QDialogButtonBox::Ok
                                    | QDialogButtonBox::Cancel,
                                    dlg);
    auto ok = dbb->button(QDialogButtonBox::Ok);
    connect(pafe, SIGNAL(hasSelection(bool)),
            ok, SLOT(setEnabled(bool)));
    ok->setEnabled(false);

    lay->addWidget(dbb);

    dlg->adjustSize();
    dlg->move((topLevelWidget()->width() - dlg->width())/2
             + topLevelWidget()->geometry().left(),
             (topLevelWidget()->height() - dlg->height())/2
             + topLevelWidget()->geometry().top());

    auto aw = topLevelWidget()->findChild<AstroWidget*>();
    qDebug() << aw;
    connect(dbb, SIGNAL(accepted()), dlg, SLOT(accept()));
    connect(dbb, SIGNAL(rejected()), dlg, SLOT(reject()));
    connect(dlg, &QDialog::accepted,
            [this,pafe] { pafe->applyToFile(); addFile(pafe->file()); });
    connect(dlg, SIGNAL(rejected()), dlg, SLOT(deleteLater()));
    connect(dlg, SIGNAL(accepted()), dlg, SLOT(deleteLater()));
    dlg->open();
}

void FilesBar::updateTab(int index)
{
    if (index >= count()) return;
    QStringList names;

    for (AstroFile* i : files[index])
        if (i) names << i->getName() + (i->hasUnsavedChanges() ? "*" : "");

    setTabText(index, names.join(" | "));
}

void FilesBar::fileUpdated(AstroFile::Members m)
{
    if (!(m & (AstroFile::Name | AstroFile::ChangedState))) return;
    qDebug() << "FilesBar::updateTab";
    AstroFile* file = (AstroFile*)sender();
    updateTab(getTabIndex(file));
}

void FilesBar::fileDestroyed()                // called when AstroFile going to be destroyed
{
    auto file = static_cast<AstroFile*>(sender());
    int tab = getTabIndex(file);
    if (tab == -1) return;                        // tab with the single file has been removed already
    int index = files[tab].indexOf(file);
    files[tab].removeAt(index);
    updateTab(tab);
    file->deleteLater();
}

void FilesBar::swapTabs(int f1, int f2)
{
    AstroFileList temp = files[f1];
    files[f1] = files[f2];
    files[f2] = temp;
}

void FilesBar::swapCurrentFiles(int i, int j)
{
    AstroFile* temp = files[currentIndex()][i];
    files[currentIndex()][i] = files[currentIndex()][j];
    files[currentIndex()][j] = temp;
    updateTab(currentIndex());
    currentChanged(currentIndex());
}

bool FilesBar::closeTab(int index)
{
    AstroFileList f = files[index];
    AstroFile* file = nullptr;
    if (f.count()) file = f[0];

    if (askToSave && file && file->hasUnsavedChanges()) {
        QMessageBox msgBox;
        msgBox.setText(tr("Save changes in '%1' before closing?").arg(file->getName()));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Save);
        int ret = msgBox.exec();

        switch (ret)
        {
        case QMessageBox::Yes: file->save(); break;
        case QMessageBox::Cancel: return false;
        default: break;
        }
    } else if (count() == 1) {
        return false;                               // TODO: make an empty file instead last tab
    }

    files.removeAt(index);
    static_cast<QTabBar*>(this)->removeTab(index);
    
    // delete AstroFiles, because we do not need it
    for (AstroFile* i : f) i->destroy();
    
    if (!count()) addNewFile();
    return true;
}

void FilesBar::openFile(const QString& name)
{
    int i = getTabIndex(name, true/*firstFileOnly*/);
    if (i != -1) return setCurrentIndex(i); // focus if the file is currently opened

    /*if (currentFile()->hasUnsavedChanges())
      openFileInNewTab(name);
    else*/
    currentFiles()[0]->load(name);

}

void FilesBar::openFileInNewTab(const QString& name)
{
    //int i = getTabIndex(name, true);
    //if (i != -1) return setCurrentIndex(i);

    AstroFile* file = new AstroFile;
    file->load(name);
    addFile(file);
}

void
FilesBar::openFileInNewTabWithTransits(const QString& name)
{
    AstroFile* file1 = new AstroFile;
    file1->load(name);
    addFile(file1);
    AstroFile* file2 = new AstroFile;
    file2->setName("Transits " + QDate::currentDate().toString());
    file2->setParent(this);
    files[currentIndex()] << file2;
    updateTab(currentIndex());
    connect(file2, SIGNAL(changed(AstroFile::Members)), this, SLOT(fileUpdated(AstroFile::Members)));
    connect(file2, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
    emit currentChanged(currentIndex());
}

void FilesBar::openFileAsSecond(const QString& name)
{
    if (files[currentIndex()].count() < 2) {
        AstroFile* file = new AstroFile;
        file->load(name);
        file->setParent(this);
        files[currentIndex()] << file;
        updateTab(currentIndex());
        connect(file, SIGNAL(changed(AstroFile::Members)), this, SLOT(fileUpdated(AstroFile::Members)));
        connect(file, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
        emit currentChanged(currentIndex());
    } else {
        files[currentIndex()][1]->load(name);
    }
}

void
FilesBar::openFileComposite(const QStringList& names)
{
    AstroFile* file = new AstroFile;
    file->load(names.first());
    addFile(file);

}

void
FilesBar::openFileReturn(const QString& name, const QString& body)
{
    AstroFile* native = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(native, true);
    native->load(name);

    QString planet = body=="Sun"? "Solar"
                                : body=="Moon"? "Lunar"
                                              : body;
    if (native->data().harmonic != 1.0) {
        planet += " H" + QString::number(native->data().harmonic);
    }

    A::PlanetId pid = A::getPlanetId(body);

    AstroFile* planetReturn = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(planetReturn, true);

    //planetReturn->setParent(this);
    planetReturn->setName("Return");
    //planetReturn->setGMT(QDateTime::currentDateTime());

    auto dt = A::calculateReturnTime(pid, native->data(),
                                     planetReturn->data());
    delete native;

    planetReturn->setGMT(dt);

    planetReturn->setName(QString("%1 %2 Return %3")
                          .arg(name)
                          .arg(planet)
                          .arg(dt.toLocalTime().date().year()));
    planetReturn->clearUnsavedState();
    addFile(planetReturn);
}

void
FilesBar::findDerivedChart(const QString& body)
{
    
}

void
FilesBar::openFileInNewTabWithReturn(const QString& name,
                                     const QString& body)
{
    AstroFile* native = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(native);
    native->load(name);
    addFile(native);

    QString planet = body=="Sun"? "Solar" : body=="Moon"? "Lunar" : body;
    if (native->data().harmonic != 1.0) {
        planet += " H" + QString::number(native->data().harmonic);
    }

    A::PlanetId pid = A::getPlanetId(body);

    AstroFile* planetReturn = new AstroFile;
    MainWindow::theAstroWidget()->setupFile(planetReturn, true);

    auto dt = A::calculateReturnTime(pid, native->data(),
                                     planetReturn->data());
    planetReturn->setGMT(dt);

    planetReturn->setName(QString("%1 %2 Return %3")
                          .arg(name)
                          .arg(planet)
                          .arg(dt.toLocalTime().date().year()));

    planetReturn->setParent(this);

    files[currentIndex()] << planetReturn;
    updateTab(currentIndex());

    planetReturn->clearUnsavedState();

    connect(planetReturn, SIGNAL(changed(AstroFile::Members)), this, SLOT(fileUpdated(AstroFile::Members)));
    connect(planetReturn, SIGNAL(destroyRequested()), this, SLOT(fileDestroyed()));
    emit currentChanged(currentIndex());
}

void
FilesBar::openTransits(int i)
{
Q_UNUSED(i);
}

/* =========================== MAIN WINDOW ================================== */

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    Customizable()
{
    HelpWidget* help = new HelpWidget("text/" + A::usedLanguage(), this);

    filesBar = new FilesBar(this);
    astroWidget = new AstroWidget(this);
    databaseDockWidget = new QDockWidget(this);
    astroDatabase = new AstroDatabase();
    toolBar = new QToolBar(tr("File"), this);
    toolBar2 = new QToolBar(tr("Options"), this);
    helpToolBar = new QToolBar(tr("Hint"), this);
    panelsMenu = new QMenu;

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


    QWidget* wdg = new QWidget(this);
    wdg->setObjectName("centralWidget");
    wdg->setContextMenuPolicy(Qt::CustomContextMenu);
    QVBoxLayout* layout = new QVBoxLayout(wdg);
    layout->setSpacing(0);
    layout->setMargin(0);
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
            for (auto btn: w->findChildren<QPushButton*>()) {
                statusBar()->addPermanentWidget(btn);
            }
            continue;
        }
        statusBar()->addPermanentWidget(w);
    }

    A::AspectsSet* dynAspSet = nullptr;
    for (auto&& as : A::getAspectSets()) {
        if (as.name.startsWith("Dynamic")) {
            dynAspSet = &as; break;
        }
    }

    auto aspectsSelector = astroWidget->getAspectsSelector();
    connect(aspectsSelector,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this, aspectsSelector](int i)
    {
        A::AspectId id = aspectsSelector->itemData(i).toInt();
        const auto& asp = A::getAspectSet(id);
        bool add = asp.name.startsWith("Dynamic");
        auto dactrls = astroWidget->getDynAspectControls();
        if (add) {
            if (dactrls->parent()) dactrls->setVisible(true);
            else statusBar()->insertPermanentWidget(0, dactrls);
        } else {
            if (dactrls->parent()) dactrls->setVisible(false);
        }
    });

    connect(wdg, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(contextMenu(QPoint)));
    connect(filesBar, SIGNAL(currentChanged(int)),
            this, SLOT(currentTabChanged()));
    connect(astroDatabase, SIGNAL(openFile(const QString&)),
            filesBar, SLOT(openFile(const QString&)));
    connect(astroDatabase, SIGNAL(openFileInNewTab(const QString&)),
            filesBar, SLOT(openFileInNewTab(const QString&)));
    connect(astroDatabase, SIGNAL(openFileInNewTabWithTransits(const QString&)),
            filesBar, SLOT(openFileInNewTabWithTransits(const QString&)));
    connect(astroDatabase, SIGNAL(openFileAsSecond(const QString&)),
            filesBar, SLOT(openFileAsSecond(const QString&)));
    connect(astroWidget, SIGNAL(appendFileRequested()),
            filesBar, SLOT(openFileAsSecond()));
    connect(astroWidget, SIGNAL(helpRequested(QString)),
            help, SLOT(searchFor(QString)));
    connect(astroWidget, SIGNAL(swapFilesRequested(int, int)),
            filesBar, SLOT(swapCurrentFiles(int, int)));
    connect(statusBar(), SIGNAL(messageChanged(QString)),
            help, SLOT(searchFor(QString)));
    connect(new QShortcut(QKeySequence("CTRL+TAB"), this),
            SIGNAL(activated()), filesBar, SLOT(nextTab()));
    connect(astroDatabase, SIGNAL(openFileReturn(const QString&, const QString&)),
            filesBar, SLOT(openFileReturn(const QString&, const QString&)));
    connect(astroDatabase, SIGNAL(openFileInNewTabWithReturn(const QString&, const QString&)),
            filesBar, SLOT(openFileInNewTabWithReturn(const QString&, const QString&)));

    loadSettings();
    filesBar->addNewFile();
}

/*static*/
MainWindow*
MainWindow::instance()
{
    static MainWindow* theMainWindow = nullptr;
    if (!theMainWindow) {
        theMainWindow = new MainWindow;
    }
    return theMainWindow;
}

void MainWindow::contextMenu(QPoint p)
{
    QPoint pos = ((QWidget*)sender())->mapToGlobal(p);
    panelsMenu->exec(pos);
}

void MainWindow::addToolBarActions()
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
    toolBar->addAction(QIcon("style/save.png"), tr("Save"), this, SLOT(saveFile()));
    toolBar->addAction(QIcon("style/edit.png"), tr("Edit"), astroWidget, SLOT(openEditor()));
    //toolBar  -> addAction(QIcon("style/print.png"), tr("Экспорт"));

    newAct->setShortcut(QKeySequence("CTRL+N"));
    newEditAct->setShortcut(QKeySequence("Ctrl+Shift+N"));
    toolBar->actions()[1]->setShortcut(QKeySequence("CTRL+S"));
    toolBar->actions()[2]->setShortcut(QKeySequence("F2"));
    //toolBar  -> actions()[3]->setShortcut(QKeySequence("CTRL+P"));

    newAct->setStatusTip(tr("New data") + "\n Ctrl+N");
    newEditAct->setStatusTip(tr("Edit new data") + "\n Ctrl+Shift+N");
    toolBar->actions()[1]->setStatusTip(tr("Save data") + "\n Ctrl+S");
    toolBar->actions()[2]->setStatusTip(tr("Edit data...") + "\n F2");
    //toolBar  -> actions()[3]->setStatusTip(tr("Печать или экспорт \n Ctrl+P"));

    QToolButton* b = new QToolButton;           // panels toggle button
    b->setText(tr("Panels"));
    b->setIcon(QIcon("style/panels.png"));
    b->setToolButtonStyle(toolButtonStyle());
    b->setMenu(panelsMenu);
    b->setPopupMode(QToolButton::InstantPopup);

    toolBar2->addWidget(b);
    toolBar2->addAction(QIcon("style/tools.png"), tr("Options"), this, SLOT(showSettingsEditor()));
    toolBar2->addAction(QIcon("style/info.png"), tr("About"), this, SLOT(showAbout()));
    //toolBar2 -> addAction(QIcon("style/coffee.png"), tr("Справка"));

    QAction* dbToggle = createActionForPanel(databaseDockWidget/*, QIcon("style/database.png")*/);
    dbToggle->setShortcut(QKeySequence("CTRL+O"));
    dbToggle->setStatusTip(tr("Toggle database") + "\n Ctrl+O");

    createActionForPanel(helpToolBar/*, QIcon("style/help.png")*/);
}

QAction* MainWindow::createActionForPanel(QWidget* w)
{
    QAction* a = panelsMenu->addAction(/*icon, */w->windowTitle());
    a->setCheckable(true);
    connect(a, SIGNAL(triggered(bool)), w, SLOT(setVisible(bool)));
    connect(w, SIGNAL(visibilityChanged(bool)), a, SLOT(setChecked(bool)));
    return a;
}

void MainWindow::currentTabChanged()
{
    if (!filesBar->count()) return;
    astroWidget->setFiles(filesBar->currentFiles());
}

AppSettings MainWindow::defaultSettings()
{
    AppSettings s;
    s << astroWidget->defaultSettings();
    s.setValue("Window/Geometry", 0);
    s.setValue("Window/State", 0);
    s.setValue("askToSave", false);
    return s;
}

AppSettings MainWindow::currentSettings()
{
    AppSettings s;
    s << astroWidget->currentSettings();
    s.setValue("Window/Geometry", this->saveGeometry());
    s.setValue("Window/State", this->saveState());
    s.setValue("askToSave", askToSave);
    return s;
}

void MainWindow::applySettings(const AppSettings& s)
{
    astroWidget->applySettings(s);
    this->restoreGeometry(s.value("Window/Geometry").toByteArray());
    this->restoreState(s.value("Window/State").toByteArray());
    askToSave = s.value("askToSave").toBool();
}

void MainWindow::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addControl("askToSave", tr("Ask about unsaved files"));
    astroWidget->setupSettingsEditor(ed);
}

void MainWindow::closeEvent(QCloseEvent* ev)
{
    while (askToSave && filesBar->count()
           && filesBar->currentFiles().count()
           && filesBar->currentFiles()[0]->hasUnsavedChanges()) 
    {
        if (!filesBar->closeTab(filesBar->currentIndex()))
            return ev->ignore();
    }

    saveSettings();

    QMainWindow::closeEvent(ev);
    QApplication::quit();
}

void MainWindow::gotoUrl(QString url)
{
    if (url.isEmpty()) url = ((QWidget*)sender())->toolTip();
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::showAbout()
{
    QDialog* d = new QDialog(this);
    QLabel* l = new QLabel;
    QLabel* l2 = new QLabel;
    SlideWidget* s = new SlideWidget;
    QPushButton* b = new QPushButton(tr("Credits"));
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
    l->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                               Qt::TextSelectableByMouse);
    l2->setWordWrap(true);
    l2->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                Qt::TextSelectableByMouse);
    s->addSlide(l);
    s->addSlide(l2);
    s->setTransitionEffect(SlideWidget::Transition_Overlay);

    QHBoxLayout* h = new QHBoxLayout;
    h->setMargin(10);
    h->addWidget(b);
    h->addStretch();
    h->addWidget(b1);
    h->addWidget(b2);
    h->addWidget(b3);

    QVBoxLayout* v = new QVBoxLayout(d);
    v->setMargin(0);
    v->addWidget(s);
    v->addLayout(h);

    l->setText("<center><b><big>" + QApplication::applicationVersion() + "</big></b>"
               "<p>" + tr("Astrological software for personal use.") + "</p>"
               //"<p><a style='color:yellow' href=\"http://www.syslog.pro/tag/zodiac\">Watch developer's blog</a>"
               //" | <a style='color:yellow' href=\"https://github.com/atten/zodiac\"><img src=\"style/github.png\">Follow on GitHub</a></p>"
               "<p>Copyright (C) 2012-2014 Artem Vasilev<br>"
               "<a style='color:white' href=\"mailto:atten@syslog.pro\">atten@syslog.pro</a></p><br>" +
               tr("This application is provided AS IS and distributed in the hope that it will be useful,"
                  " but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY"
                  " or FITNESS FOR A PARTICULAR PURPOSE.") + "</center>");


    l2->setText("<p><b>Swiss Ephemerides library</b><br>"
                "Copyright (C) 1997 - 2008 Astrodienst AG, Switzerland.  All rights reserved.<br>"
                "<a style='color:white' href=\"ftp://www.astro.ch/pub/swisseph/LICENSE\">ftp://www.astro.ch/pub/swisseph/LICENSE</a></p>"
                "<p><b>Primo Icon Set</b> by Webdesigner Depot<br>"
                "<a style='color:white' href=\"https://www.iconfinder.com/iconsets/Primo_Icons#readme\">www.iconfinder.com/iconsets/Primo_Icons#readme</a></p>"
                "<p><b>Almagest True Type Font</b></p>"
                "<p>Additional thanks to authors of <b>\"SymSolon\"</b> project<br>"
                "<a style='color:white' href=\"http://sf.net/projects/symsolon\">sf.net/projects/symsolon</a></p>");

    connect(l, SIGNAL(linkActivated(QString)), this, SLOT(gotoUrl(QString)));
    connect(l2, SIGNAL(linkActivated(QString)), this, SLOT(gotoUrl(QString)));
    connect(b1, SIGNAL(clicked()), this, SLOT(gotoUrl()));
    connect(b2, SIGNAL(clicked()), this, SLOT(gotoUrl()));
    connect(b3, SIGNAL(clicked()), this, SLOT(gotoUrl()));
    connect(b, SIGNAL(clicked()), s, SLOT(nextSlide()));
    d->exec();
}
