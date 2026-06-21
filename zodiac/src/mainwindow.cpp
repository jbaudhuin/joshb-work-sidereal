#include <QChildEvent>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QMouseEvent>
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
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>

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
#include "thememanager.h"
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QGraphicsBlurEffect>
#include <QHeaderView>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMenu>
#include <QMetaObject>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QWidget>
#include <math.h>

/* =========================== THEMED MESSAGE BOX HELPERS =================== */

namespace {
    // Helper to create a themed message box
    int showThemedMessageBox(QWidget* parent, QMessageBox::Icon icon, 
                            const QString& title, const QString& text,
                            QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                            QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
    {
        QMessageBox msgBox(icon, title, text, buttons, parent);
        if (defaultButton != QMessageBox::NoButton) {
            msgBox.setDefaultButton(defaultButton);
        }
        ThemeManager::instance().propagateThemeProperty(&msgBox);
        return msgBox.exec();
    }

    void showThemedWarning(QWidget* parent, const QString& title, const QString& text) {
        showThemedMessageBox(parent, QMessageBox::Warning, title, text);
    }

    void showThemedInformation(QWidget* parent, const QString& title, const QString& text) {
        showThemedMessageBox(parent, QMessageBox::Information, title, text);
    }

    void showThemedCritical(QWidget* parent, const QString& title, const QString& text) {
        showThemedMessageBox(parent, QMessageBox::Critical, title, text);
    }

    int showThemedQuestion(QWidget* parent, const QString& title, const QString& text,
                          QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
                          QMessageBox::StandardButton defaultButton = QMessageBox::No) {
        return showThemedMessageBox(parent, QMessageBox::Question, title, text, buttons, defaultButton);
    }
}

/* =========================== SESSION MANAGER ============================== */

// Static member initialization
QString SessionManager::s_currentSessionFile;

QString 
SessionManager::sessionDirectory()
{
    // Use the same directory as user charts (Documents/zodiac-charts)
    constexpr auto loc = QStandardPaths::DocumentsLocation;
    QString dir = QStandardPaths::writableLocation(loc) + "/zodiac-charts";
    
    // Ensure directory exists
    QDir d(dir);
    if (!d.exists()) {
        QDir().mkpath(d.absolutePath());
    }
    
    return dir;
}

QString 
SessionManager::settingsFile()
{
    return sessionDirectory() + "/settings.ini";
}

QString 
SessionManager::currentSessionFile()
{
    if (s_currentSessionFile.isEmpty()) {
        // Generate session filename based on current time
        qint64 timestamp = QDateTime::currentDateTime().toSecsSinceEpoch();
        QString filename = QStringLiteral("session-%1.zos").arg(timestamp);
        s_currentSessionFile = sessionDirectory() + "/" + filename;
    }
    return s_currentSessionFile;
}

void 
SessionManager::setCurrentSessionFile(const QString& filename)
{
    s_currentSessionFile = filename;
}

QString 
SessionManager::getMostRecentSession()
{
    QString sessionsIni = sessionDirectory() + "/sessions.ini";
    QSettings sessions(sessionsIni, QSettings::IniFormat);
    QStringList mru = sessions.value(QStringLiteral("MRU/sessions")).toStringList();
    
    if (mru.count() > 0) {
        return mru.first(); // Most recent is first in list
    }
    
    return QString(); // No sessions found
}

bool 
SessionManager::cloneSessionFile(const QString& sourceFile, const QString& destFile, 
                                  const QStringList& excludeSections)
{
    QSettings source(sourceFile, QSettings::IniFormat);
    QSettings dest(destFile, QSettings::IniFormat);
    
    // Clear destination file
    dest.clear();
    
    // Get all groups (including nested ones)
    QStringList allGroups = source.childGroups();
    
    // Also need to discover nested groups by checking each group for children
    QStringList groupsToCheck = allGroups;
    QSet<QString> discoveredGroups;
    
    while (!groupsToCheck.isEmpty()) {
        QString group = groupsToCheck.takeFirst();
        if (discoveredGroups.contains(group)) continue;
        discoveredGroups.insert(group);
        
        source.beginGroup(group);
        QStringList children = source.childGroups();
        for (const QString& child : children) {
            QString fullPath = group + "." + child;
            if (!discoveredGroups.contains(fullPath)) {
                groupsToCheck.append(fullPath);
                discoveredGroups.insert(fullPath);
            }
        }
        source.endGroup();
    }
    
    // Convert set back to list and add empty string for root group
    allGroups = discoveredGroups.values();
    allGroups.prepend(QString()); // Root group
    
    // Copy each group
    for (const QString& group : allGroups) {
        // Check if this group should be excluded (check top-level name only)
        QString topLevel = group.split('.').first();
        if (!topLevel.isEmpty() && excludeSections.contains(topLevel)) {
            continue;
        }
        
        if (!group.isEmpty()) {
            source.beginGroup(group);
            dest.beginGroup(group);
        }
        
        // Copy all keys
        QStringList keys = source.childKeys();
        for (const QString& key : keys) {
            dest.setValue(key, source.value(key));
        }
        
        if (!group.isEmpty()) {
            source.endGroup();
            dest.endGroup();
        }
    }
    
    dest.sync();
    return dest.status() == QSettings::NoError;
}

void 
SessionManager::addToMRU(const QString& sessionFile)
{
    QString sessionsIni = sessionDirectory() + "/sessions.ini";
    QSettings sessions(sessionsIni, QSettings::IniFormat);
    QStringList mru = sessions.value(QStringLiteral("MRU/sessions")).toStringList();
    
    // Remove if already exists (will re-add at front)
    mru.removeOne(sessionFile);
    while (mru.contains(sessionFile)) {
        mru.removeOne(sessionFile);
    }
    
    // Add to front
    mru.prepend(sessionFile);
    
    // Keep only last 20 sessions
    while (mru.count() > 20) {
        mru.removeLast();
    }
    
    sessions.setValue(QStringLiteral("MRU/sessions"), mru);
    sessions.sync();
}

void
SessionManager::removeFromMRU(const QString& sessionFile)
{
    QString sessionsIni = sessionDirectory() + "/sessions.ini";
    QSettings sessions(sessionsIni, QSettings::IniFormat);
    QStringList mru = sessions.value(QStringLiteral("MRU/sessions")).toStringList();
    if (mru.removeAll(sessionFile) > 0) {
        sessions.setValue(QStringLiteral("MRU/sessions"), mru);
        sessions.sync();
    }
}

QList<SessionManager::SessionInfo>
SessionManager::getRecentSessions(int maxCount)
{
    QList<SessionInfo> result;
    QString sessionsIni = sessionDirectory() + "/sessions.ini";
    QSettings sessions(sessionsIni, QSettings::IniFormat);
    QStringList mru = sessions.value(QStringLiteral("MRU/sessions")).toStringList();
    
    for (const QString& filename : mru) {
        if (result.count() >= maxCount) break;
        
        // Check if file exists
        if (!QFile::exists(filename)) continue;
        
        SessionInfo info;
        info.filename = filename;
        
        // Read session metadata
        QSettings sessionSettings(filename, QSettings::IniFormat);
        info.tabCount = sessionSettings.value(QStringLiteral("Session/tabCount"), 0).toInt();
        info.name = sessionSettings.value(QStringLiteral("Session/name"), QString()).toString();
        info.inaugurated = sessionSettings.value(QStringLiteral("Session/inaugurated")).toDateTime();

        // Named sessions identify by filename — fall back to that for the label
        // if the (legacy/optional) Session/name key is absent.
        if (info.name.isEmpty() && isNamedSession(filename))
            info.name = sessionNameFromFile(filename);

        // Prefer the stored save time.  Otherwise: a true timestamped
        // (session-<digits>) file can be parsed from its name, but doing mid(8)
        // on a *named* file yields garbage (epoch 0) — use the file's
        // modification time for those instead.
        if (sessionSettings.contains(QStringLiteral("Session/lastSaved"))) {
            info.timestamp = sessionSettings.value(QStringLiteral("Session/lastSaved")).toDateTime();
        } else if (!isNamedSession(filename)) {
            // Extract timestamp from filename: session-1234567890.zos
            QString base = QFileInfo(filename).baseName(); // "session-1234567890"
            info.timestamp = QDateTime::fromSecsSinceEpoch(base.mid(8).toLongLong());
        } else {
            info.timestamp = QFileInfo(filename).lastModified();
        }

        result.push_back(info);
    }
    
    return result;
}

QString
SessionManager::newAutoSessionFile()
{
    // Mint a fresh timestamped auto-session, make it current, and stamp its
    // inauguration time.  Used on first launch, --new, and "New session".
    qint64 timestamp = QDateTime::currentDateTime().toSecsSinceEpoch();
    QString filename = QStringLiteral("session-%1.zos").arg(timestamp);
    s_currentSessionFile = sessionDirectory() + "/" + filename;

    QSettings session(s_currentSessionFile, QSettings::IniFormat);
    session.setValue(QStringLiteral("Session/inaugurated"), QDateTime::currentDateTime());
    session.sync();

    qDebug() << "Created new auto-session file:" << s_currentSessionFile;
    return s_currentSessionFile;
}

bool
SessionManager::hasExplicitCurrentSession()
{
    return !s_currentSessionFile.isEmpty();
}

QString 
SessionManager::readAPIKey()
{
    QSettings apiSettings("APIKey.ini", QSettings::IniFormat);
    apiSettings.beginGroup(QStringLiteral("General"));
    QString key = apiSettings.value(QStringLiteral("Key"), QString()).toString();
    apiSettings.endGroup();
    return key;
}

void 
SessionManager::writeAPIKey(const QString& apiKey)
{
    QSettings apiSettings("APIKey.ini", QSettings::IniFormat);
    apiSettings.beginGroup(QStringLiteral("General"));
    apiSettings.setValue(QStringLiteral("Key"), apiKey);
    apiSettings.endGroup();
    apiSettings.sync();
}

QString 
SessionManager::getSessionName(const QString& sessionFile)
{
    QSettings settings(sessionFile, QSettings::IniFormat);
    return settings.value(QStringLiteral("Session/name"), QString()).toString();
}

void 
SessionManager::setSessionName(const QString& sessionFile, const QString& name)
{
    QSettings settings(sessionFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Session/name"), name);
    settings.sync();
}

QString 
SessionManager::sessionFileFromName(const QString& name)
{
    // Sanitize name for filename - remove invalid characters
    QString safeName = name;
    safeName.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "-");
    QString filename = QStringLiteral("%1.zos").arg(safeName);
    return sessionDirectory() + "/" + filename;
}

QString 
SessionManager::sessionNameFromFile(const QString& filepath)
{
    QString basename = QFileInfo(filepath).baseName();
    
    // Check if it's a timestamped session (e.g., "session-1234567890")
    if (basename.startsWith("session-")) {
        // It's a timestamped session, no display name
        return QString();
    }
    
    // It's a named session, return the basename as the name
    return basename;
}

QString 
SessionManager::sessionFileFromTimestamp(qint64 timestamp)
{
    if (timestamp == 0) {
        timestamp = QDateTime::currentDateTime().toSecsSinceEpoch();
    }
    QString filename = QStringLiteral("session-%1.zos").arg(QString::number(timestamp));
    return sessionDirectory() + "/" + filename;
}

bool 
SessionManager::isNamedSession(const QString& sessionFile)
{
    // Extract basename without path and extension
    QString baseName = QFileInfo(sessionFile).baseName();
    
    // Check if it matches the pattern "session-<digits>"
    // If it's all digits after "session-", it's a timestamped session
    if (baseName.startsWith("session-")) {
        QString suffix = baseName.mid(8); // Skip "session-"
        bool isNumber;
        suffix.toLongLong(&isNumber);
        return !isNumber; // Named if NOT a number
    }
    
    return true; // If doesn't start with "session-", consider it named
}

QString
SessionManager::friendlyTimestamp(const QDateTime& dt)
{
    if (!dt.isValid()) return QString();

    QDateTime now = QDateTime::currentDateTime();
    qint64 daysAgo = dt.daysTo(now);

    if (daysAgo == 0)
        return QString("Today at %1").arg(dt.toString("h:mm AP"));
    if (daysAgo == 1)
        return QString("Yesterday at %1").arg(dt.toString("h:mm AP"));
    if (daysAgo >= 2 && daysAgo <= 5)
        return QString("%1 at %2").arg(dt.toString("dddd")).arg(dt.toString("h:mm AP"));
    if (dt.date().year() == now.date().year())
        return dt.toString("ddd d MMM h:mm AP");          // "Mon 14 Dec 2:30 PM"
    return dt.toString("ddd d MMM yyyy h:mm AP");          // "Mon 14 Dec 2024 2:30 PM"
}

QString
SessionManager::SessionInfo::displayName() const
{
    QString result;

    // Get base name without extension (e.g., "session-1734124800" or "MyProject")
    QFileInfo fileInfo(filename);
    QString baseName = fileInfo.completeBaseName(); // without .zos extension

    // Is this a true timestamped (auto) session, "session-<digits>"?
    bool isTimestamped = false;
    if (baseName.startsWith("session-")) {
        bool ok;
        baseName.mid(8).toLongLong(&ok);
        isTimestamped = ok;
    }

    if (isTimestamped) {
        // Auto-session: show the SAVED date/time.  Prefer the stored lastSaved
        // (in `timestamp`); fall back to the filename's creation timestamp for
        // legacy files that never recorded one.
        QDateTime dt = timestamp.isValid()
            ? timestamp
            : QDateTime::fromSecsSinceEpoch(baseName.mid(8).toLongLong());
        result = friendlyTimestamp(dt);
    } else {
        // Named session file - decode the filename (handle special characters)
        result = AFileInfo::decodeName(baseName);
    }

    // Append tab count
    if (tabCount > 0) {
        result += QString(" - %1 %2").arg(tabCount).arg(tabCount == 1 ? "tab" : "tabs");
    }

    return result;
}

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
    
    // Enable drag and drop
    setAcceptDrops(true);

    QGridLayout* layout = new QGridLayout(this);
    layout->addWidget(edit, 0, 0, 1, 1);
    layout->addWidget(shadow, 0, 0, 1, 1);

    connect(edit, SIGNAL(clicked()), this, SIGNAL(clicked()));
}

void
AstroFileInfo::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && filesCount() > 1) {
        emit middleClicked(currentIndex);
        event->accept();
        return;
    }
    AstroFileHandler::mousePressEvent(event);
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
    double tzVal = currentFile()->getTimezone();
    if (tzVal > 0)
        timezone = QString("GMT +%1").arg(tzVal);
    else if (tzVal < 0)
        timezone = QString("GMT %1").arg(tzVal);
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

void
AstroFileInfo::dragEnterEvent(QDragEnterEvent* event)
{
    qDebug() << "AstroFileInfo::dragEnterEvent - index:" << currentIndex;
    // Accept drops from the chart list
    if (event->mimeData()->hasUrls() || event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        qDebug() << "AstroFileInfo accepting drag for widget" << currentIndex;
        event->acceptProposedAction();
    }
}

void
AstroFileInfo::dragMoveEvent(QDragMoveEvent* event)
{
    // Accept drag move events
    if (event->mimeData()->hasUrls() || event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        event->acceptProposedAction();
    }
}

void
AstroFileInfo::dropEvent(QDropEvent* event)
{
    qDebug() << "AstroFileInfo::dropEvent - index:" << currentIndex;
    
    // Get the file path from the mime data
    QString filePath;
    
    // Check for file path in text format
    if (event->mimeData()->hasText()) {
        filePath = event->mimeData()->text();
        qDebug() << "Drop text:" << filePath;
    }
    
    // Check for URL format
    if (filePath.isEmpty() && event->mimeData()->hasUrls()) {
        QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            filePath = urls.first().toLocalFile();
            qDebug() << "Drop URL:" << filePath;
        }
    }
    
    if (!filePath.isEmpty()) {
        // Emit signal with the file path and target index
        // Index 0 = primary chart (left), Index 1 = secondary chart (right)
        emit chartDropped(filePath, currentIndex);
        event->acceptProposedAction();
    }
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
    addSlide(new Plain, QIcon("style/plain.png"), tr("Tables"));
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
            // Connect Plain display mode changes to Speculum widget
            connect(plain,
                    &Plain::displayModeChanged,
                    speculum,
                    &Speculum::setDisplayMode);
        }
    }

    connect(fileView, SIGNAL(clicked()), this, SLOT(openEditor()));
    connect(fileView2nd, SIGNAL(clicked()), this, SLOT(openEditor()));
    connect(fileView, SIGNAL(middleClicked(int)), this, SIGNAL(closeFileRequested(int)));
    connect(fileView2nd, SIGNAL(middleClicked(int)), this, SIGNAL(closeFileRequested(int)));
    connect(fileView, SIGNAL(chartDropped(QString,int)), this, SLOT(handleChartDroppedOnInputWidget(QString,int)));
    connect(fileView2nd, SIGNAL(chartDropped(QString,int)), this, SLOT(handleChartDroppedOnInputWidget(QString,int)));
    connect(slides,
            SIGNAL(currentSlideChanged()),
            this,
            SLOT(currentSlideChanged()));
    connect(slides,
            SIGNAL(chartDropped(QString)),
            this,
            SIGNAL(chartFileDropped(QString)));
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

    // Stamp global display settings into InputData (no signals)
    file->stampDisplaySettings();

    // Harmonic is per-file, set from toolbar for new files
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
AstroWidget::setHarmonicQuietly(double h)
{
    // Update the harmonic combo box without triggering horoscopeControlChanged.
    // This avoids premature chart redraws when the caller will trigger a
    // controlled update later (e.g., after setting focal planets).
    QString ns = QString::number(h);
    int     i  = harmonicSelector->findText(ns);
    if (i == -1) {
        harmonicSelector->addItem(ns);
        i = harmonicSelector->findText(ns);
    }
    if (i != -1) {
        harmonicSelector->blockSignals(true);
        harmonicSelector->setCurrentIndex(i);
        harmonicSelector->blockSignals(false);
    }
    // Apply harmonic directly to current files (recalculates positions
    // without triggering ds.apply -> viewSettingsUpdated chart redraw)
    for (AstroFile* f : files())
        f->setHarmonic(h);
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
AstroWidget::handleChartDroppedOnInputWidget(const QString& filePath, int targetIndex)
{
    qDebug() << "AstroWidget::handleChartDroppedOnInputWidget:" << filePath << "targetIndex:" << targetIndex;
    
    // Emit signal to be handled by MainWindow/FilesBar
    emit chartDroppedOnInputWidget(filePath, targetIndex);
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

    // Primary frame combo: only the two mutually-exclusive frames.  The
    // legacy Great-Circle and Prime-Vertical entries are demoted to
    // orthogonal toggle buttons (see gcToggle / pvToggle below).
    aspectModeSelector->addItem(A::aspectModeType::toUserString(A::amcEcliptic),
                                int(A::amcEcliptic));
    aspectModeSelector->addItem(A::aspectModeType::toUserString(A::amcEquatorial),
                                int(A::amcEquatorial));

    // GC and PV toggles -- visual styling lives in themes/*.qss; selectors
    // match the objectName values "gcToggle" / "pvToggle".
    gcToggle = new QToolButton();
    gcToggle->setObjectName("gcToggle");
    gcToggle->setText(tr("GC"));
    gcToggle->setCheckable(true);
    gcToggle->setAutoRaise(false);
    gcToggle->setToolTip(tr("Great Circle aspects (3D angular separation)"));

    pvToggle = new QToolButton();
    pvToggle->setObjectName("pvToggle");
    pvToggle->setText(tr("PV"));
    pvToggle->setCheckable(true);
    pvToggle->setAutoRaise(false);
    pvToggle->setToolTip(tr("Prime Vertical chart display (does not affect events)"));

    for (int i = 1; i <= 16; ++i) {
        harmonicSelector->addItem(QString::number(i));
    }

    horoscopeControls << zodiacSelector << hsystemSelector << aspectsSelector
                      << aspectModeSelector << gcToggle << pvToggle
                      << harmonicSelector;

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

    connect(gcToggle, &QToolButton::toggled, this,
            &AstroWidget::horoscopeControlChanged);
    connect(pvToggle, &QToolButton::toggled, this,
            &AstroWidget::horoscopeControlChanged);
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
    // Update global display settings (HouseSystem, Zodiac, AspectSet, AspectMode)
    auto& ds = DisplaySettings::instance();
    auto aset = aspectsSelector->itemData(aspectsSelector->currentIndex()).toInt();
    // When _dynAspChange is set, the aspect set content changed (not its ID),
    // so force the AspectSet notification even if the ID is unchanged
    const A::aspectModeEnum frame = A::aspectModeEnum(
        aspectModeSelector->itemData(aspectModeSelector->currentIndex()).toInt());
    ds.apply(
        hsystemSelector->itemData(hsystemSelector->currentIndex()).toInt(),
        zodiacSelector->itemData(zodiacSelector->currentIndex()).toInt(),
        aset,
        frame,
        gcToggle->isChecked(),
        pvToggle->isChecked(),
        _dynAspChange);

    // Harmonic is per-file — parse from combo and apply to all current files
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
                else break;
            }
            if (ok) h = v;
        } else {
            ops = val.split(QRegularExpression("\\s*/\\s*"));
            if (ops.size() >= 2) {
                double v = ops.takeFirst().toDouble(&ok);
                for (auto m : ops) {
                    auto mv = m.toDouble(&ok);
                    if (std::abs(mv) <= std::numeric_limits<double>::epsilon()) {
                        ok = false; break;
                    }
                    if (ok) v /= mv;
                    else break;
                }
                if (ok) h = v;
            }
        }
    }
    if (ok) {
        for (AstroFile* f : files())
            f->setHarmonic(h);
    }
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

    s.setValue("Scope/defaultLocation", "-122.6784 45.5152 82");
    s.setValue("Scope/defaultLocationName", "Portland, OR, USA");
    s.setValue("Scope/zodiac",
               2); // Fagan-Bradley (Sidereal)
    s.setValue("Scope/houseSystem", 2); // Campanus
    s.setValue("Scope/aspectSet", 5); // Dynamic
    s.setValue("Scope/dynamic", "1, 2, 3, 4, 6, 8, 12"); // Ptolemaic
    s.setValue("Scope/aspectMode", 1); // ecliptic (legacy key, kept for migration)
    s.setValue("Scope/primaryFrame", int(A::amcEcliptic));
    s.setValue("Scope/useGreatCircle", false);
    s.setValue("Scope/usePrimeVertical", false);
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
    s.setValue("Scope/primaryFrame",
               aspectModeSelector->itemData(aspectModeSelector->currentIndex()).toInt());
    s.setValue("Scope/useGreatCircle", gcToggle->isChecked());
    s.setValue("Scope/usePrimeVertical", pvToggle->isChecked());

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

    // Decoupled aspect-mode controls.  Honor the three new keys if present;
    // otherwise migrate from the legacy 4-value Scope/aspectMode.
    A::aspectModeEnum loadedFrame = A::amcEcliptic;
    bool              loadedGC    = false;
    bool              loadedPV    = false;
    if (s.contains("Scope/primaryFrame")) {
        const int f = s.value("Scope/primaryFrame").toInt();
        loadedFrame = (f == int(A::amcEquatorial))
                          ? A::amcEquatorial : A::amcEcliptic;
        loadedGC = s.value("Scope/useGreatCircle", false).toBool();
        loadedPV = s.value("Scope/usePrimeVertical", false).toBool();
    } else if (s.contains("Scope/aspectMode")) {
        const int legacy = s.value("Scope/aspectMode").toInt();
        switch (legacy) {
        case int(A::amcGreatCircle):
            loadedFrame = A::amcEcliptic;   loadedGC = true;  loadedPV = false; break;
        case int(A::amcEcliptic):
            loadedFrame = A::amcEcliptic;   loadedGC = false; loadedPV = false; break;
        case int(A::amcEquatorial):
            loadedFrame = A::amcEquatorial; loadedGC = false; loadedPV = false; break;
        case int(A::amcPrimeVertical):
            loadedFrame = A::amcEcliptic;   loadedGC = false; loadedPV = true;  break;
        default: break;
        }
    }
    {
        const int idx = aspectModeSelector->findData(int(loadedFrame));
        aspectModeSelector->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    gcToggle->setChecked(loadedGC);
    pvToggle->setChecked(loadedPV);

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

    // Call setupSettingsEditor in desired tab order: Chart, Tables, Events, Harmonics.
    // A plain handlers loop would produce construction order (Harmonics, Events, Chart, Tables).
    // Uses a type-tag lambda so each pass exits as soon as the first match is found.
    QSet<AstroFileHandler*> calledHandlers;
    auto callSetupFor = [&](auto* t) {
        using T = std::remove_pointer_t<decltype(t)>;
        for (AstroFileHandler* h : handlers) {
            if (!qobject_cast<T*>(h)) continue;

            h->setupSettingsEditor(ed);
            calledHandlers.insert(h);
            break;
        }
    };
    callSetupFor((Chart*)    nullptr);
    callSetupFor((Plain*)    nullptr);
    callSetupFor((Transits*) nullptr);
    callSetupFor((Harmonics*)nullptr);
    for (AstroFileHandler* h : handlers)
        if (!calledHandlers.contains(h)) h->setupSettingsEditor(ed);

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
FileTreeView::mousePressEvent(QMouseEvent* event)
{
    // Capture the index where the mouse was pressed for later use in startDrag
    dragStartIndex = indexAt(event->pos());
    QTreeView::mousePressEvent(event);
}

void
FileTreeView::contextMenuEvent(QContextMenuEvent* event)
{
    qDebug() << ">>> FileTreeView::contextMenuEvent fired at" << event->pos()
             << "global:" << event->globalPos();
    database->showContextMenu(event->globalPos());
    event->accept();
}

void
FileTreeView::startDrag(Qt::DropActions supportedActions)
{
    // Start the drag but DON'T call the base implementation
    // This prevents Qt from trying to move items in the model
    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData();
    
    // Use the index captured in mousePressEvent (where drag started)
    QModelIndex dragIndex = dragStartIndex;
    
    // Get selected indexes
    QModelIndexList selection = selectionModel()->selectedIndexes();
    
    // Determine which items to drag:
    // If the drag started from a selected item, drag all selected items
    // Otherwise, just drag the single item under the mouse
    QStringList files;
    
    if (selection.contains(dragIndex) && !selection.isEmpty()) {
        // Drag all selected items
        qDebug() << "Dragging" << selection.count() << "selected items";
        for (const auto& index : selection) {
            qDebug() << "  Index row:" << index.row() << "column:" << index.column();
            qDebug() << "  Display data:" << index.data(Qt::DisplayRole).toString();
            
            QString path;
            QString pathData = index.data(Qt::UserRole + 1).toString(); // PathRole
            
            if (!pathData.isEmpty()) {
                // Directory item - has full path
                path = pathData;
            } else {
                // File item - need to build path from parent directory + filename
                QString filename = index.data(Qt::DisplayRole).toString();
                QModelIndex parentIndex = index.parent();
                if (parentIndex.isValid()) {
                    QString parentPath = parentIndex.data(Qt::UserRole + 1).toString();
                    if (!parentPath.isEmpty()) {
                        path = parentPath + "/" + AFileInfo::encodeName(filename) + AFileInfo::suff();
                        qDebug() << "  Built path from parent:" << path;
                    }
                }
            }
            
            if (!path.isEmpty()) {
                files << path;
            } else {
                qDebug() << "  WARNING: Could not determine path!";
            }
        }
    } else if (dragIndex.isValid()) {
        // Drag just the single item under the mouse
        qDebug() << "Dragging single item from drag start index";
        qDebug() << "  Index row:" << dragIndex.row() << "column:" << dragIndex.column();
        qDebug() << "  Display data:" << dragIndex.data(Qt::DisplayRole).toString();
        
        QString path;
        QString pathData = dragIndex.data(Qt::UserRole + 1).toString(); // PathRole
        
        if (!pathData.isEmpty()) {
            // Directory item - has full path
            path = pathData;
        } else {
            // File item - need to build path from parent directory + filename
            QString filename = dragIndex.data(Qt::DisplayRole).toString();
            QModelIndex parentIndex = dragIndex.parent();
            if (parentIndex.isValid()) {
                QString parentPath = parentIndex.data(Qt::UserRole + 1).toString();
                if (!parentPath.isEmpty()) {
                    path = parentPath + "/" + AFileInfo::encodeName(filename) + AFileInfo::suff();
                    qDebug() << "  Built path from parent:" << path;
                }
            }
        }
        
        if (!path.isEmpty()) {
            files << path;
        } else {
            qDebug() << "  WARNING: Could not determine path!";
        }
    }
    
    // Set both text and URL formats for compatibility
    if (!files.isEmpty()) {
        mimeData->setText(files.first()); // Use first file path
        
        // Also provide as URL list
        QList<QUrl> urls;
        for (const QString& file : files) {
            urls << QUrl::fromLocalFile(file);
        }
        mimeData->setUrls(urls);
    }
    
    drag->setMimeData(mimeData);
    
    qDebug() << "FileTreeView::startDrag - files:" << files;
    
    // Support both move (default) and copy (with Ctrl)
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
}

void
FileTreeView::dragEnterEvent(QDragEnterEvent* event)
{
    // Allow drags from this tree view - accept them here for internal reordering,
    // but they will also propagate to other widgets outside this tree
    if (event->source() == this) {
        event->acceptProposedAction();
    } else {
        // Let external drag sources work too
        QTreeView::dragEnterEvent(event);
    }
}

void
FileTreeView::dragMoveEvent(QDragMoveEvent* event)
{
    // Only handle drag move if it's within the tree view area
    // This allows drags to continue outside the widget
    if (event->source() == this) {
        QString targetDir;
        if (database->validateDropTarget(event->position().toPoint(), targetDir)) {
            event->acceptProposedAction();
        } else {
            // Don't ignore - let it propagate
            event->setAccepted(false);
        }
    } else {
        QTreeView::dragMoveEvent(event);
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

    // Add Sessions folder at the top
    auto sessionsItem = new QStandardItem(tr("Sessions"));
    sessionsItem->setData(SessionManager::sessionDirectory(), PathRole); // Sessions in zodiac-charts directory
    sessionsItem->setData(dbType, TypeRole); // Use dbType to mark as special
    sessionsItem->setData(tr("Session files"), Qt::ToolTipRole);
    
    QFont boldFont = sessionsItem->data(Qt::FontRole).value<QFont>();
    boldFont.setBold(true);
    sessionsItem->setData(boldFont, Qt::FontRole);
    
    sessionsItem->setFlags(Qt::ItemIsEnabled); // No drag-drop for sessions
    
    fswatch->addPath(SessionManager::sessionDirectory()); // Watch for new session files
    dirModel->appendRow(sessionsItem);

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
    setWindowTitle(tr("Database"));
    setWindowFlags(Qt::WindowStaysOnTopHint);

    QHBoxLayout* l = new QHBoxLayout;
    l->addWidget(search);
    l->addWidget(refresh);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(l);
    layout->addWidget(fileList);

    connect(refresh, SIGNAL(clicked()), this, SLOT(updateList()));
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

QSet<QString>
AstroDatabase::currentExpandedPaths()
{
    QSet<QString> expanded;

    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& index) {
        if (!index.isValid()) return;

        QStandardItem* item = dirModel->itemFromIndex(index);
        auto itemType = item ? item->data(TypeRole).toUInt() : 0;
        if (item && (itemType == dirType || itemType == dbType)) {
            QString path = item->data(PathRole).toString();
            QModelIndex proxyIndex = searchProxy->mapFromSource(index);
            if (!path.isEmpty() && fileList->isExpanded(proxyIndex)) {
                expanded.insert(path);
            }

            // Recursively collect children
            for (int i = 0; i < item->rowCount(); ++i) {
                QStandardItem* child = item->child(i);
                if (child && child->data(TypeRole).toUInt() == dirType) {
                    walk(dirModel->indexFromItem(child));
                }
            }
        }
    };

    for (int i = 0; i < dirModel->rowCount(); ++i) {
        walk(dirModel->index(i, 0));
    }
    return expanded;
}

void
AstroDatabase::captureExpansionSnapshot()
{
    _expandSnapshot = currentExpandedPaths();
}

void
AstroDatabase::applyExpansionSnapshot()
{
    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& index) {
        if (!index.isValid()) return;

        QStandardItem* item = dirModel->itemFromIndex(index);
        auto itemType = item ? item->data(TypeRole).toUInt() : 0;
        if (item && (itemType == dirType || itemType == dbType)) {
            QString path = item->data(PathRole).toString();
            QModelIndex proxyIndex = searchProxy->mapFromSource(index);
            fileList->setExpanded(proxyIndex,
                                  !path.isEmpty() && _expandSnapshot.contains(path));

            // Recursively restore children
            for (int i = 0; i < item->rowCount(); ++i) {
                QStandardItem* child = item->child(i);
                if (child && child->data(TypeRole).toUInt() == dirType) {
                    walk(dirModel->indexFromItem(child));
                }
            }
        }
    };

    for (int i = 0; i < dirModel->rowCount(); ++i) {
        walk(dirModel->index(i, 0));
    }
}

void
AstroDatabase::saveDatabaseState()
{
    QSettings settings(SessionManager::settingsFile(), QSettings::IniFormat);
    settings.beginGroup("Database");

    // While a search is active the tree is force-expanded to reveal matches, so
    // the live expansion/scroll state doesn't reflect the user's real layout.
    // Persist the pre-search snapshot instead, and leave scrollPosition as-is.
    if (!_searchActive) {
        QScrollBar* vbar = fileList->verticalScrollBar();
        if (vbar) {
            settings.setValue("scrollPosition", vbar->value());
        }
    }

    // Clear old expansion state
    settings.remove("expanded");

    const QSet<QString> expanded =
        _searchActive ? _expandSnapshot : currentExpandedPaths();
    for (const QString& path : expanded) {
        settings.setValue(QString("expanded/%1").arg(path), true);
    }

    settings.endGroup();
}

void
AstroDatabase::restoreDatabaseState()
{
    QSettings settings(SessionManager::settingsFile(), QSettings::IniFormat);
    settings.beginGroup("Database");
    
    // Recursively restore expanded state for all directories
    std::function<void(const QModelIndex&)> restoreExpansion = [&](const QModelIndex& index) {
        if (!index.isValid()) return;
        
        QStandardItem* item = dirModel->itemFromIndex(index);
        auto itemType = item ? item->data(TypeRole).toUInt() : 0;
        if (item && (itemType == dirType || itemType == dbType)) {
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
    const bool wasActive = _searchActive;
    const bool nowActive = !nf.isEmpty();

    // Snapshot the user's real expansion state the moment a search begins, so we
    // can restore it when the search is cleared.
    if (nowActive && !wasActive) {
        captureExpansionSnapshot();
    }

    searchProxy->setFilterRegularExpression(nf);

    if (nowActive) {
        // Recursive filtering keeps only matches plus their ancestor folders, so
        // expanding everything reveals exactly the matches and nothing else.
        fileList->expandAll();
    } else if (wasActive) {
        applyExpansionSnapshot();
    }

    _searchActive = nowActive;
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
                auto childType = child ? child->data(TypeRole).toUInt() : 0;
                if (child && (childType == dirType || childType == dbType)) {
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
        auto itemType = diritem->data(TypeRole).toUInt();
        QDir dir(diritem->data().toString());
        diritem->removeRows(0, diritem->rowCount());

        // Special handling for Sessions folder - scan for .zos files instead of .dat
        if (itemType == dbType && diritem->text() == tr("Sessions")) {
            QStringList sessionFiles = dir.entryList(QStringList() << "*.zos",
                                                     QDir::Files,
                                                     QDir::Name | QDir::IgnoreCase);
            
            const QStringList& presel = sel[diritem];
            int j = 0;
            
            for (const QString& sessionFile : sessionFiles) {
                // Get session info for display name
                QString absolutePath = dir.absoluteFilePath(sessionFile);
                SessionManager::SessionInfo sessionInfo;
                sessionInfo.filename = sessionFile;
                
                // Read session metadata
                QSettings sessionSettings(absolutePath, QSettings::IniFormat);
                sessionInfo.name = sessionSettings.value("Session/name", "").toString();
                sessionInfo.timestamp = sessionSettings.value("Session/lastSaved").toDateTime();
                sessionInfo.inaugurated = sessionSettings.value("Session/inaugurated").toDateTime();

                // Read tab count
                sessionInfo.tabCount = sessionSettings.value("Session/tabCount", 0).toInt();

                auto child = new QStandardItem(sessionInfo.displayName());
                child->setData(sessionType, TypeRole);
                child->setData(sessionFile, PathRole); // Store filename for loading
                child->setData(absolutePath, SessionPathRole); // path the actions read
                // Tooltip: creation date (more interesting than the path) + path.
                QString tip = sessionInfo.inaugurated.isValid()
                    ? tr("Created %1\n%2")
                          .arg(SessionManager::friendlyTimestamp(sessionInfo.inaugurated))
                          .arg(absolutePath)
                    : absolutePath;
                child->setData(tip, Qt::ToolTipRole);
                child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                
                // Italicize the current session
                if (sessionFile == QFileInfo(SessionManager::currentSessionFile()).fileName()) {
                    QFont font = child->font();
                    font.setItalic(true);
                    child->setFont(font);
                }
                
                diritem->appendRow(child);
                
                while (j < presel.count() && presel.at(j) < sessionFile) ++j;
                if (j < presel.count() && presel.at(j) == sessionFile) {
                    QModelIndex qmi =
                        dirModel->index(diritem->rowCount() - 1, 0, mi);
                    sl.select(qmi, qmi);
                }
            }
            return; // Don't process as a normal directory
        }

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
                    auto childType = child ? child->data(TypeRole).toUInt() : 0;
                    if (child && (childType == dirType || childType == dbType)) {
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

    // Check if any selected items are sessions
    auto sfpModel = qobject_cast<QSortFilterProxyModel*>(fileList->model());
    auto dirModel = qobject_cast<QStandardItemModel*>(
        sfpModel ? sfpModel->sourceModel() : fileList->model());
    
    bool hasSessions = false;
    if (dirModel) {
        for (const auto& mi : sil) {
            auto sourceIndex = sfpModel->mapToSource(mi);
            auto item = dirModel->itemFromIndex(sourceIndex);
            if (item && item->data(TypeRole).toUInt() == sessionType) {
                hasSessions = true;
                break;
            }
        }
    }
    
    // If any sessions are selected, route to deleteSessions() which handles them properly
    if (hasSessions) {
        deleteSessions();
        return;
    }

    // Original file deletion logic for non-session items
    QMessageBox msgBox;
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    ThemeManager::instance().propagateThemeProperty(&msgBox);

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
AstroDatabase::openSessionInNewWindow()
{
    QItemSelectionModel* sm = fileList->selectionModel();
    if (!sm || !sm->hasSelection()) return;

    auto sfpModel = qobject_cast<QSortFilterProxyModel*>(fileList->model());
    auto dirModel = qobject_cast<QStandardItemModel*>(
        sfpModel ? sfpModel->sourceModel() : fileList->model());
    if (!dirModel) return;

    auto selectedIndexes = sm->selectedIndexes();
    if (selectedIndexes.isEmpty()) return;

    auto firstIndex = sfpModel->mapToSource(selectedIndexes.first());
    auto firstItem = dirModel->itemFromIndex(firstIndex);
    if (!firstItem || firstItem->data(TypeRole).toUInt() != sessionType) return;

    QString sessionFile = firstItem->data(SessionPathRole).toString();
    
    QStringList args;
    args << "--load-session" << sessionFile;  // Pass full path
    
    qDebug() << "=== openSessionInNewWindow ===";
    qDebug() << "Session file (full path):" << sessionFile;
    qDebug() << "Executable:" << QCoreApplication::applicationFilePath();
    qDebug() << "Arguments:" << args;
    qDebug() << "Session file exists:" << QFile::exists(sessionFile);
    
    // Use QProcess in detached mode to avoid any potential blocking
    // IMPORTANT: Redirect stdout/stderr to prevent handle inheritance issues
    // When a parent process closes, inherited handles become invalid and can cause
    // child processes to freeze when writing debug output (printf/qDebug).
    // This is especially problematic when launching chains of instances (e.g., 1→2→3)
    QProcess* process = new QProcess();
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setArguments(args);
    process->setWorkingDirectory(QDir::currentPath());
    
#ifdef _ZOD_DEBUG
    // In debug builds, redirect to null to prevent I/O deadlocks
    // Child processes will still log to their own zodiac-<PID>.log files
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
#endif
    
    // Start in detached mode - process will continue even if parent dies
    qint64 pid = 0;
    bool success = process->startDetached(&pid);
    
    qDebug() << "startDetached returned:" << success << "PID:" << pid;
    
    // Delete the QProcess object - we don't need it anymore
    delete process;
    
    if (success) {
        qDebug() << "Successfully launched new instance with PID:" << pid;
    } else {
        qDebug() << "Failed to launch new instance";
        QString errorMsg = tr("Failed to launch new window with session.\n\n"
                            "Executable: %1\n"
                            "Session: %2\n"
                            "Arguments: %3")
                            .arg(QCoreApplication::applicationFilePath())
                            .arg(sessionFile)
                            .arg(args.join(" "));
        showThemedWarning(const_cast<AstroDatabase*>(this),
            tr("Launch Failed"),
            errorMsg);
    }
}

void
AstroDatabase::loadSessionsInCurrent()
{
    QItemSelectionModel* sm = fileList->selectionModel();
    if (!sm || !sm->hasSelection()) return;

    auto sfpModel = qobject_cast<QSortFilterProxyModel*>(fileList->model());
    auto dirModel = qobject_cast<QStandardItemModel*>(
        sfpModel ? sfpModel->sourceModel() : fileList->model());
    if (!dirModel) return;

    QStringList sessionFiles;
    for (const auto& mi : sm->selectedIndexes()) {
        auto sourceIndex = sfpModel->mapToSource(mi);
        auto item = dirModel->itemFromIndex(sourceIndex);
        if (!item || item->data(TypeRole).toUInt() != sessionType) continue;
        
        sessionFiles << item->data(SessionPathRole).toString();
    }
    
    if (sessionFiles.isEmpty()) return;
    
    qDebug() << "Loading" << sessionFiles.count() << "session(s) into current";
    
    // Load charts from each session file into current tabs
    for (const QString& sessionFile : sessionFiles) {
        QSettings sessionSettings(sessionFile, QSettings::IniFormat);
        int tabCount = sessionSettings.value("Session/tabCount", 0).toInt();
        
        for (int i = 0; i < tabCount; ++i) {
            QString tabGroup = QString("Tab%1").arg(i);
            int fileCount = sessionSettings.value(tabGroup + "/fileCount", 0).toInt();
            
            for (int j = 0; j < fileCount; ++j) {
                QString fileGroup = QString("%1.File%2/").arg(tabGroup).arg(j);
                
                QString directory = sessionSettings.value(fileGroup + "directory").toString();
                QString filename = sessionSettings.value(fileGroup + "filename").toString();
                
                if (!directory.isEmpty() && !filename.isEmpty()) {
                    // Load this chart file
                    AFileInfo fileInfo(QDir(directory), filename);
                    emit openFileInNewTab(fileInfo);
                }
            }
        }
    }
}

void
AstroDatabase::renameSession()
{
    QItemSelectionModel* sm = fileList->selectionModel();
    if (!sm || !sm->hasSelection()) return;

    auto sfpModel = qobject_cast<QSortFilterProxyModel*>(fileList->model());
    auto dirModel = qobject_cast<QStandardItemModel*>(
        sfpModel ? sfpModel->sourceModel() : fileList->model());
    if (!dirModel) return;

    auto selectedIndexes = sm->selectedIndexes();
    if (selectedIndexes.isEmpty()) return;

    auto firstIndex = sfpModel->mapToSource(selectedIndexes.first());
    auto firstItem = dirModel->itemFromIndex(firstIndex);
    if (!firstItem || firstItem->data(TypeRole).toUInt() != sessionType) return;

    QString oldSessionFile = firstItem->data(SessionPathRole).toString();
    QFileInfo oldFileInfo(oldSessionFile);
    QString oldBaseName = oldFileInfo.completeBaseName();
    
    // Determine current name for dialog
    QString currentName;
    if (oldBaseName.startsWith("session-")) {
        QString suffix = oldBaseName.mid(8); // Remove "session-" prefix
        bool isTimestamp;
        suffix.toLongLong(&isTimestamp);
        if (isTimestamp) {
            currentName = ""; // Timestamped session, suggest empty for user to name
        } else {
            currentName = AFileInfo::decodeName(suffix); // Named session
        }
    } else {
        currentName = AFileInfo::decodeName(oldBaseName);
    }
    
    bool ok;
    QString newName = QInputDialog::getText(
        this,
        tr("Rename Session"),
        tr("Session name:"),
        QLineEdit::Normal,
        currentName,
        &ok);
    
    if (!ok || newName.isEmpty()) return;
    
    // Generate new filename (already includes full path from sessionFileFromName)
    QString newSessionFile = SessionManager::sessionFileFromName(newName);
    
    // Check if target already exists
    if (QFile::exists(newSessionFile) && newSessionFile != oldSessionFile) {
        showThemedWarning(this, 
            tr("Rename Failed"), 
            tr("A session with the name '%1' already exists.").arg(newName));
        return;
    }
    
    // Rename the file
    if (QFile::rename(oldSessionFile, newSessionFile)) {
        // Update the session name in the file
        SessionManager::setSessionName(newSessionFile, newName);
        
        // Update current session reference if this is the current session
        if (oldSessionFile == SessionManager::currentSessionFile()) {
            SessionManager::setCurrentSessionFile(newSessionFile);
        }
        
        updateList();
    } else {
        showThemedWarning(this, 
            tr("Rename Failed"), 
            tr("Failed to rename session file."));
    }
}

void
AstroDatabase::deleteSessions()
{
    QItemSelectionModel* sm = fileList->selectionModel();
    if (!sm || !sm->hasSelection()) return;

    auto sfpModel = qobject_cast<QSortFilterProxyModel*>(fileList->model());
    auto dirModel = qobject_cast<QStandardItemModel*>(
        sfpModel ? sfpModel->sourceModel() : fileList->model());
    if (!dirModel) return;

    QStringList sessionFilesToDelete;
    for (const auto& mi : sm->selectedIndexes()) {
        auto sourceIndex = sfpModel->mapToSource(mi);
        auto item = dirModel->itemFromIndex(sourceIndex);
        if (!item || item->data(TypeRole).toUInt() != sessionType) continue;
        
        sessionFilesToDelete << item->data(SessionPathRole).toString();
    }
    
    if (sessionFilesToDelete.isEmpty()) return;
    
    QMessageBox msgBox;
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    ThemeManager::instance().propagateThemeProperty(&msgBox);
    
    if (sessionFilesToDelete.count() == 1) {
        msgBox.setText(tr("Delete session '%1'?").arg(QFileInfo(sessionFilesToDelete.first()).completeBaseName()));
    } else {
        msgBox.setText(tr("Delete %1 sessions?").arg(sessionFilesToDelete.count()));
    }
    
    if (msgBox.exec() != QMessageBox::Yes) return;
    
    QString currentSession = SessionManager::currentSessionFile();
    bool deletingCurrentSession = false;
    
    for (const QString& sessionFile : sessionFilesToDelete) {
        if (sessionFile == currentSession) {
            deletingCurrentSession = true;
            continue; // Don't delete current session
        }
        QFile::remove(sessionFile);
    }
    
    if (deletingCurrentSession) {
        showThemedWarning(this,
            tr("Cannot Delete Current Session"),
            tr("The current session cannot be deleted. Please switch to a different session first."));
    }
    
    updateList();
}

void
AstroDatabase::openSelected()
{
    QItemSelectionModel* sm = fileList->selectionModel();
    if (!sm || !sm->hasSelection()) return;

    auto sfpModel = qobject_cast<QSortFilterProxyModel*>(fileList->model());
    auto dirModel = qobject_cast<QStandardItemModel*>(
        sfpModel ? sfpModel->sourceModel() : fileList->model());
    if (!dirModel) return;

    auto selectedIndexes = sm->selectedIndexes();
    if (selectedIndexes.isEmpty()) return;

    // Check if we're selecting a session item
    auto firstIndex = sfpModel->mapToSource(selectedIndexes.first());
    auto firstItem = dirModel->itemFromIndex(firstIndex);
    if (firstItem && firstItem->data(TypeRole).toUInt() == sessionType) {
        // Load session file and restart application
        QString sessionFile = firstItem->data(SessionPathRole).toString();
        
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Load Session"));
        msgBox.setText(tr("Loading session will restart the application with the selected session."));
        msgBox.setInformativeText(tr("Continue?"));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        
        if (msgBox.exec() == QMessageBox::Yes) {
            // Get the application executable path
            QString program = QCoreApplication::applicationFilePath();
            QStringList arguments;
            arguments << "--load-session" << sessionFile;
            
            // Start the new instance
            if (QProcess::startDetached(program, arguments)) {
                // Close this instance
                QTimer::singleShot(100, qApp, &QApplication::quit);
            } else {
                showThemedCritical(nullptr, tr("Error"), 
                    tr("Failed to start new instance with session: %1").arg(sessionFile));
            }
        }
        return;
    }

    // Normal chart file handling
    auto sfi = getSelectedItems(fileList);
    if (sfi.empty()) return;

    auto count = sfi.count();
    if (count == 1) {
        // Close secondary chart before opening (if user confirms)
        emit closeSecondaryChartRequested();
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
AstroDatabase::showContextMenu(QPoint pos)
{
    qDebug() << ">>> showContextMenu called, global pos:" << pos;

    // pos is in global coordinates; convert to viewport for indexAt()
    QPoint vp       = fileList->viewport()->mapFromGlobal(pos);
    qDebug() << "    viewport pos:" << vp;
    auto proxyIndex = fileList->indexAt(vp);
    if (!proxyIndex.isValid()) {
        qDebug() << "    BAIL: proxyIndex not valid at viewport pos" << vp;
        return;
    }

    qDebug() << "    proxyIndex:" << proxyIndex << proxyIndex.data() << "TypeRole:" << proxyIndex.data(TypeRole);
    auto qmi = searchProxy->mapToSource(proxyIndex);
    qDebug() << "    sourceIndex:" << qmi;

    auto item = dirModel->itemFromIndex(qmi);
    if (!item) {
        qDebug() << "    BAIL: item is null";
        return;
    }

    auto type = entryType(item->data(TypeRole).toUInt());

    // For multi-select operations (sessions, files), check the selection model.
    // For single-item operations (directories), we just use the clicked item.
    QItemSelectionModel* sm = fileList->selectionModel();
    bool hasSelection       = sm && sm->hasSelection();
    
    if (hasSelection) {
        int sessionCount = 0;
        int fileCount    = 0;
        int dirCount     = 0;
    
        for (const auto& mi : sm->selectedIndexes()) {
            auto sourceIndex  = searchProxy->mapToSource(mi);
            auto selectedItem = dirModel->itemFromIndex(sourceIndex);
            if (!selectedItem) continue;
        
            auto selectedType = entryType(selectedItem->data(TypeRole).toUInt());
            if (selectedType == sessionType) {
                sessionCount++;
            } else if (selectedType == fileType) {
                fileCount++;
            } else if (selectedType == dirType) {
                dirCount++;
            }
        }
    
        // Don't show menu for mixed selection types
        if ((sessionCount > 0 && fileCount > 0) || 
            (sessionCount > 0 && dirCount > 0) || 
            (fileCount > 0 && dirCount > 0)) {
            qDebug() << "Mixed selection detected, no context menu";
            return;
        }
    
        // Handle session-specific menu
        if (sessionCount > 0) {
            QMenu* mnu = new QMenu(this);
        
            // "Open in new window" - only for single session
            QAction* openNewWindowAction = mnu->addAction(tr("Open in new window"), this, SLOT(openSessionInNewWindow()));
            openNewWindowAction->setEnabled(sessionCount == 1);
        
            // "Load in current" - works with multiple sessions
            mnu->addAction(tr("Load in current"), this, SLOT(loadSessionsInCurrent()));
        
            mnu->addSeparator();
        
            // "Rename..." - only for single session
            QAction* renameAction = mnu->addAction(tr("Rename..."), this, SLOT(renameSession()));
            renameAction->setEnabled(sessionCount == 1);
        
            // "Delete" - works with multiple sessions
            mnu->addAction(QIcon("style/delete.png"), tr("Delete"), this, SLOT(deleteSessions()));
        
            mnu->exec(pos);
            mnu->deleteLater();
            return;
        }
    }

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
        showThemedInformation(
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
        showThemedInformation(
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
        showThemedWarning(
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
        showThemedInformation(
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
            showThemedWarning(
                this,
                tr("Cannot Delete Directory"),
                tr("The folder contains subdirectories and cannot be deleted."));
            return;
        }
    }
    
    // Check for chart files
    QStringList chartFiles = dir.entryList(AFileInfo::wildcard(), QDir::Files);
    if (!chartFiles.isEmpty()) {
        showThemedWarning(
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
    
    auto reply = showThemedQuestion(
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
        showThemedWarning(
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
        showThemedInformation(this, tr("Set Type"), 
            tr("No chart files selected."));
        return;
    }

    // Create dialog to select new type
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Set Chart Type"));
    dialog.setModal(true);
    
    // Apply theme to dialog
    ThemeManager::instance().propagateThemeProperty(&dialog);
    
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
    
    showThemedInformation(this, tr("Set Type"), 
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
    
    // Apply theme to dialog
    ThemeManager::instance().propagateThemeProperty(&dialog);
    
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
        showThemedInformation(this, tr("Move to Folder"), tr("No folder selected."));
        return;
    }
    
    auto selectedIndex = selected.first();
    auto selectedItem = folderModel->itemFromIndex(selectedIndex);
    if (!selectedItem) return;
    
    QString targetDir = selectedItem->data(PathRole).toString();
    
    // Check if target is Sample Charts
    if (targetDir.contains("user/") || targetDir.endsWith("user")) {
        showThemedWarning(
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
        showThemedWarning(
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
        showThemedWarning(
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
        showThemedInformation(
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
        showThemedInformation(
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
            showThemedWarning(
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
            showThemedWarning(
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
            showThemedWarning(
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
            showThemedWarning(
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
            showThemedWarning(
                this,
                tr("Copy Failed"),
                tr("A file named '%1' already exists in the target directory.").arg(targetFileName));
            continue;
        }

        // Copy the file
        if (!QFile::copy(sourceFileInfo.filePath(), targetFileInfo.filePath())) {
            showThemedWarning(
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

    // Qt creates the scroller QToolButtons during QTabBar's constructor,
    // so they are already children by the time we get here.
    labelScrollButtons();
}

void
FilesBar::labelScrollButtons()
{
    // Qt names its internal tab scroller buttons "qt_tabbar_scrollbutton".
    const auto buttons = findChildren<QToolButton*>("qt_tabbar_scrollbutton");
    if (buttons.size() >= 1) {
        buttons[0]->setArrowType(Qt::NoArrow);
        buttons[0]->setToolButtonStyle(Qt::ToolButtonTextOnly);
        buttons[0]->setText(u8"\u25C0"); // ◀
    }
    if (buttons.size() >= 2) {
        buttons[1]->setArrowType(Qt::NoArrow);
        buttons[1]->setToolButtonStyle(Qt::ToolButtonTextOnly);
        buttons[1]->setText(u8"\u25B6"); // ▶
    }
}

void
FilesBar::childEvent(QChildEvent* event)
{
    QTabBar::childEvent(event);
    // Fallback: catch any scroller buttons added after construction.
    if (event->added() && qobject_cast<QToolButton*>(event->child())) {
        QTimer::singleShot(0, this, [this]() { labelScrollButtons(); });
    }
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

    // When a secondary file is removed from the current tab, notify
    // AstroWidget so it can call setFiles() — this hides fileView2nd
    // and forces Chart / other handlers to fully rebuild.
    if (tab == currentIndex() && index > 0 && !files[tab].isEmpty()) {
        emit currentChanged(currentIndex());
    }
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
FilesBar::closeSecondaryChart()
{
    // Check if there's a secondary chart to close
    if (currentIndex() < 0 || currentIndex() >= files.count()) return true;
    if (files[currentIndex()].count() < 2) return true;
    
    AstroFile* secondFile = files[currentIndex()][1];
    if (!secondFile) return true;
    
    // Prompt to save if needed
    if (askToSave && secondFile->hasUnsavedChanges()) {
        QMessageBox msgBox;
        msgBox.setText(
            tr("Save changes in '%1' before closing?").arg(secondFile->getName()));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No
                                  | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Yes);
        int ret = msgBox.exec();

        switch (ret) {
        case QMessageBox::Yes:    secondFile->save(); break;
        case QMessageBox::Cancel: return false;
        default:                  break;
        }
    }
    
    // Stop any active transit finder threads before closing the secondary chart
    auto transits = MainWindow::theAstroWidget()->findDockHdlr<Transits>();
    if (transits) {
        transits->stopThreads();
    }

    // Preserve file(1)'s location on file(0) so the Events panel keeps
    // it after the secondary chart disappears.
    AstroFile* primaryFile = files[currentIndex()][0];
    if (primaryFile && secondFile) {
        primaryFile->setTransitLocation(secondFile->getLocation());
        primaryFile->setTransitLocationName(secondFile->getLocationName());
        primaryFile->setTransitTimezone(secondFile->getTimezone());
    }

    // Inhibit Transits recomputation while the file is being destroyed —
    // file(0) and its cached events remain valid.
    if (transits) transits->setInhibitUpdate(true);

    // Remove the secondary file — fileDestroyed() handles list removal,
    // tab update, and emitting currentChanged to refresh all widgets.
    secondFile->destroy();

    // Re-enable Transits updates and refresh the location widget from
    // file(0)'s stored transit location (no event recompute needed).
    if (transits) {
        transits->setInhibitUpdate(false);
        transits->refreshLocationUI();
    }
    
    return true;
}

bool
FilesBar::closeFileByIndex(int fileIndex)
{
    // Close a specific file (by index within the current tab's file list)
    // via middle-click on the file info widget.
    if (currentIndex() < 0 || currentIndex() >= files.count()) return true;

    auto& tabFiles = files[currentIndex()];
    if (tabFiles.count() < 2) return true;    // can't close the only file
    if (fileIndex < 0 || fileIndex >= tabFiles.count()) return true;

    if (fileIndex == 1) {
        // Closing file 2 (secondary) — same as closeSecondaryChart
        return closeSecondaryChart();
    }

    // Closing file 1 (primary, index 0): swap so file 2 becomes primary,
    // then close the new secondary (which is the old file 1).
    swapCurrentFiles(0, 1);
    return closeSecondaryChart();
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

    // Stop any active transit finder threads before closing the tab
    if (auto transits = MainWindow::theAstroWidget()->findDockHdlr<Transits>()) {
        transits->stopThreads();
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
        // Stop any active transit finder threads before replacing the chart
        if (auto transits = MainWindow::theAstroWidget()->findDockHdlr<Transits>()) {
            transits->stopThreads();
        }
        
        // Reset transit date range to defaults for the new chart
        auto today = QDate::currentDate();
        auto startOfMonth = QDate(today.year(), today.month(), 1);
        currentFiles()[0]->setTransitStartDate(startOfMonth);
        currentFiles()[0]->setTransitDuration("1 mo");
        
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
        bool sameFile = (files[currentIndex()].count() > 0
                         && files[currentIndex()][0] == af);
        if (!sameFile) {
            // Stop finder threads and reset date range only when actually
            // replacing the file — not when the same file is being
            // "re-opened" (e.g. after a focal-planet click in transitsOnly mode).
            if (auto transits = MainWindow::theAstroWidget()->findDockHdlr<Transits>()) {
                transits->stopThreads();
            }
            auto today = QDate::currentDate();
            auto startOfMonth = QDate(today.year(), today.month(), 1);
            af->setTransitStartDate(startOfMonth);
            af->setTransitDuration("1 mo");
        }

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
    file2->clearUnsavedState(); // Transit charts don't need to be saved
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

    // Transfer transit preset settings from af (return/transit chart) to
    // file1 (natal chart) because Transits::filesUpdated() reads and
    // writes transit state on file(0).
    if (!af->getTransitEventOptions().empty())
        file1->setTransitEventOptions(af->getTransitEventOptions());
    file1->setTransitSkipByDuration(af->getTransitSkipByDuration());
    if (!af->getTransitDuration().isEmpty())
        file1->setTransitDuration(af->getTransitDuration());
    if (!af->getTransitStartDate().isNull())
        file1->setTransitStartDate(af->getTransitStartDate());
    if (!af->getTransitHarmonicRestrictions().isEmpty())
        file1->setTransitHarmonicRestrictions(af->getTransitHarmonicRestrictions());
    if (!af->getTransitPattern().isEmpty())
        file1->setTransitPattern(af->getTransitPattern());
    if (af->getOriginEventType() != A::etcUnknownEvent)
        file1->setOriginEventType(af->getOriginEventType());

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
    
    // Set base chart to the natal chart for return calculations
    QDateTime natalGMT = native->getGMT();
    delete native;

    planetReturn->setGMT(dt);
    planetReturn->setBaseChart(natalGMT);

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
    
    // Set base chart to the natal chart for return calculations
    planetReturn->setBaseChart(native->getGMT());

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

MainWindow::MainWindow(bool skipRestore, bool isServerInstance, bool autoRestore, QWidget* parent)
    : QMainWindow(parent), Customizable(), _skipRestore(skipRestore), _launchedWithNew(skipRestore), _autoRestore(autoRestore), _isServerInstance(isServerInstance)
{
    // Resolve which session to open (reuse-in-place; may prompt for a named
    // session).  Deferred restore happens further below via restoreSession().
    QString sessionFile = chooseStartupSession();
    qDebug() << "Using session file:" << sessionFile;
    
    // Initialize session tracking (legacy - may remove later)
    _sessionStartTime = QDateTime::currentDateTime();
    _currentSessionKey = "Session"; // Default session key
    _hadOverlappingInstances = false; // Will be set to true if we detect other instances during lifetime
    
    HelpWidget* help = new HelpWidget("text/" + A::usedLanguage(), this);
    qDebug() << "HelpWidget created";

    filesBar           = new FilesBar(this);
    qDebug() << "FilesBar created";
    astroWidget        = new AstroWidget(this);
    qDebug() << "AstroWidget created";
    databaseDockWidget = new QDockWidget(this);
    astroDatabase      = new AstroDatabase();
    toolBar            = new QToolBar(tr("File"), this);
    toolBar2           = new QToolBar(tr("Settings"), this);
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
    buildParanToolBar();
    addToolBar(Qt::TopToolBarArea, paranToolBar);
    addDockWidget(Qt::LeftDockWidgetArea, databaseDockWidget);

    for (QDockWidget* w : astroWidget->getDockPanels()) {
        addDockWidget(Qt::RightDockWidgetArea, w);
        w->hide();
        createActionForPanel(w);
    }

    for (QWidget* w : astroWidget->getHoroscopeControls()) {
        auto name = w->objectName();
        qDebug() << "Permanent widget added:" << w;
        if (qobject_cast<QComboBox*>(w) || qobject_cast<QToolButton*>(w)) {
            statusBar()->addPermanentWidget(w);
            continue;
        }
        for (auto btn : w->findChildren<QPushButton*>()) {
            statusBar()->addPermanentWidget(btn);
        }
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
    connect(astroDatabase,
            SIGNAL(closeSecondaryChartRequested()),
            filesBar,
            SLOT(closeSecondaryChart()));
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
    connect(astroWidget,
            SIGNAL(closeFileRequested(int)),
            filesBar,
            SLOT(closeFileByIndex(int)));
    connect(astroWidget,
            SIGNAL(chartFileDropped(QString)),
            this,
            SLOT(handleChartDroppedOnSlides(QString)));
    connect(astroWidget,
            SIGNAL(chartDroppedOnInputWidget(QString,int)),
            this,
            SLOT(handleChartDroppedOnInputWidget(QString,int)));
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

    // Load settings from current session file.
    //
    // A fresh "New Session" mints a brand-new auto-session whose file already
    // exists on disk (newAutoSessionFile() pre-stamps it with an "inaugurated"
    // key) but carries none of the curated preferences. Loading from it would
    // leave every value at the raw library defaults — a weird window size (empty
    // Window/Geometry), no panels (empty Window/State), and an uncurated
    // zodiac/aspect setup. So when we're not restoring this session's tabs
    // (_skipRestore), seed those preferences from the most-recent session
    // instead, so the user inherits their last-curated layout. Only the settings
    // are carried over — tabs are still skipped (see the _skipRestore branch
    // below).
    QString settingsSource = SessionManager::currentSessionFile();
    if (_skipRestore) {
        QString recent = SessionManager::getMostRecentSession();
        if (!recent.isEmpty() && recent != settingsSource
            && QFile::exists(recent)) {
            qDebug() << "New session: seeding settings from most-recent session"
                     << recent;
            settingsSource = recent;
        }
    }
    loadSettings(settingsSource);
    qDebug() << "_skipRestore =" << _skipRestore;
    if (!_skipRestore) {
        qDebug() << "Calling restoreSession()...";
        restoreSession();
        astroDatabase->restoreDatabaseState();
    } else {
        qDebug() << "Skipping session restore, adding new file";
        filesBar->addNewFile();
        // A pristine startup session is scratch until the user does something;
        // clear the "unsaved" flag the default chart setup sets so an untouched
        // session is recognized as empty (and discarded, not saved as a dummy).
        if (filesBar->currentFiles().count())
            filesBar->currentFiles()[0]->clearUnsavedState();
    }

    // Update window title with session name if available
    updateWindowTitle();

    // Force-refresh the active tab once the event loop starts.
    // restoreSession() runs before the window is shown, so all handler
    // updates are deferred (isVisible() == false).  This zero-timer fires
    // after w.show() / a.exec(), guaranteeing every widget is visible and
    // the deferred updates are properly dispatched.
    QTimer::singleShot(0, this, SLOT(currentTabChanged()));
}

/*static*/
MainWindow*
MainWindow::instance(bool skipRestore, bool isServerInstance, bool autoRestore, const QString& sessionFile)
{
    static MainWindow* theMainWindow = nullptr;
    if (!theMainWindow) {
        // If session file specified, set it before creating MainWindow
        if (!sessionFile.isEmpty()) {
            SessionManager::setCurrentSessionFile(sessionFile);
        }
        theMainWindow = new MainWindow(skipRestore, isServerInstance, autoRestore);
    }
    return theMainWindow;
}

QString
MainWindow::chooseStartupSession()
{
    // --load-session <file>: an explicit file was set before construction.
    // Open it in place if it exists.
    if (SessionManager::hasExplicitCurrentSession()) {
        QString f = SessionManager::currentSessionFile();
        if (QFile::exists(f)) {
            qDebug() << "Startup: opening explicitly requested session" << f;
            return f;
        }
        qDebug() << "Startup: requested session missing, falling through:" << f;
    }

    // --new: always start a fresh auto-session, no restore.
    if (_launchedWithNew) {
        qDebug() << "Startup: --new, creating fresh auto-session";
        return SessionManager::newAutoSessionFile();
    }

    QString mostRecent = SessionManager::getMostRecentSession();

    // First launch ever, or the MRU points only at deleted files.
    if (mostRecent.isEmpty() || !QFile::exists(mostRecent)) {
        qDebug() << "Startup: no usable recent session, creating fresh auto-session";
        return SessionManager::newAutoSessionFile();
    }

    const bool named = SessionManager::isNamedSession(mostRecent);

    // Named session + interactive launch: prompt before reopening curated work,
    // since edits will save back to it on exit.  --auto-restore bypasses this.
    if (named && !_autoRestore) {
        QString name = SessionManager::sessionNameFromFile(mostRecent);

        QMessageBox box(this);
        box.setWindowTitle(tr("Restore Session"));
        box.setText(tr("Your last session was the saved session '%1'.").arg(name));
        box.setInformativeText(
            tr("Reopening it will load its charts and save changes back to it "
               "when you exit."));
        // Keep button labels short: the themed QMessageBox stylesheet (padding +
        // min-width) makes Qt mis-size buttons and truncate long ones, so the
        // session name lives in the message text above rather than on the button.
        QPushButton* openBtn =
            box.addButton(tr("Open"), QMessageBox::AcceptRole);
        QPushButton* newBtn =
            box.addButton(tr("New Session"), QMessageBox::DestructiveRole);
        QPushButton* chooseBtn =
            box.addButton(tr("Choose…"), QMessageBox::ActionRole);
        box.setDefaultButton(openBtn);

        // Countdown on the Open button so the user can just wait instead of
        // clicking.  10 seconds feels right — enough time to intercept, not so
        // long that it feels like staring at a spinner.
        int remaining = 10;
        QTimer countdown;
        countdown.setInterval(1000);
        connect(&countdown, &QTimer::timeout, [&]() {
            --remaining;
            if (remaining <= 0) {
                countdown.stop();
                // Re-apply the original label so the clickedButton check below
                // matches, then simulate the open path by clearing the dialog's
                // result so we fall through to the in-place restore.
                box.done(QMessageBox::NoButton);
            } else {
                openBtn->setText(tr("Open in %1s").arg(remaining));
            }
        });
        countdown.start();

        // If the user presses Enter on the default button while the countdown is
        // still running, cancel the timer and handle the click normally.
        connect(&box, &QMessageBox::buttonClicked, [&]() {
            countdown.stop();
        });

        box.exec();

        // If the timer ran out (done(NoButton)), fall through to open.
        QAbstractButton* clicked = box.clickedButton();
        if (clicked == newBtn) {
            qDebug() << "Startup prompt: New Session";
            _skipRestore = true;
            return SessionManager::newAutoSessionFile();
        }
        if (clicked == chooseBtn) {
            // Show the picker right now (before the main window appears) and
            // open the choice in place — no throwaway session, no relaunch, no
            // abandoned window.  Cancelling falls back to a fresh New Session.
            qDebug() << "Startup prompt: Choose…";
            QString chosen = pickSessionFile();
            if (!chosen.isEmpty()) {
                SessionManager::setCurrentSessionFile(chosen);
                return chosen;   // restoreSession() opens it in place
            }
            _skipRestore = true;
            return SessionManager::newAutoSessionFile();
        }
        // openBtn clicked, Enter pressed, countdown expired, or dialog dismissed:
        // fall through to open in place.
        qDebug() << "Startup prompt: Open" << mostRecent;
    }

    // Auto-session, named-with-Open, or named-with-auto-restore: open in place.
    SessionManager::setCurrentSessionFile(mostRecent);
    return mostRecent;
}

void
MainWindow::contextMenu(QPoint p)
{
    QPoint pos = ((QWidget*) sender())->mapToGlobal(p);
    panelsMenu->exec(pos);
}

void
MainWindow::updateWindowTitle()
{
    QString currentSession = SessionManager::currentSessionFile();
    QString sessionName = SessionManager::sessionNameFromFile(currentSession);
    
    if (!sessionName.isEmpty()) {
        setWindowTitle(QString("%1 - %2").arg(QApplication::applicationName()).arg(sessionName));
    } else {
        setWindowTitle(QApplication::applicationName());
    }
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

    auto editAct = toolBar->addAction(QIcon("style/edit.png"),
                                       tr("Edit..."),
                                       astroWidget,
                                       SLOT(openEditor()));

    auto saveAct = toolBar->addAction(QIcon("style/save.png"),
                                       tr("Save Chart"),
                                       this,
                                       SLOT(saveFile()));

    auto saveSessionAct = toolBar->addAction(QIcon("style/file.png"),
                                              tr("Save Session..."),
                                              this,
                                              SLOT(saveSessionAs()));

    // Use standard icon for Restore Session - SP_DialogOpenButton shows
    // folder/open icon
    auto restoreSessionAct = toolBar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                                 tr("Restore Session..."),
                                                 this,
                                                 SLOT(showRestoreSessionDialog()));

    // toolBar  -> addAction(QIcon("style/print.png"), tr("Экспорт"));

    newAct->setShortcut(QKeySequence("CTRL+N"));
    newEditAct->setShortcut(QKeySequence("Ctrl+Shift+N"));
    editAct->setShortcut(QKeySequence("F2"));
    saveAct->setShortcut(QKeySequence("CTRL+S"));
    // toolBar  -> actions()[4]->setShortcut(QKeySequence("CTRL+P"));

    newAct->setStatusTip(tr("New data") + "\n Ctrl+N");
    newEditAct->setStatusTip(tr("Edit new data") + "\n Ctrl+Shift+N");
    editAct->setStatusTip(tr("Edit data...") + "\n F2");
    saveAct->setStatusTip(tr("Save data") + "\n Ctrl+S");
    saveSessionAct->setStatusTip(tr("Save session with name"));
    restoreSessionAct->setStatusTip(tr("Restore session"));
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
                        tr("Settings"),
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
MainWindow::buildParanToolBar()
{
    paranToolBar = new QToolBar(tr("Aspect Range Navigator"), this);
    paranToolBar->setObjectName("aspectRangeNavigator");
    // Text glyphs rather than QStyle icons: the standard pixmaps don't follow
    // the ThemeManager dark theme (they render dark on the dark UI), whereas
    // text picks up the theme foreground. A scoped stylesheet (see
    // styleParanToolBar() below) sizes the glyphs and trims the window-wide
    // 48px text-under-icon padding.
    paranToolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    _paranFirst = paranToolBar->addAction(QStringLiteral("|◀"));
    _paranFirst->setToolTip(tr("First in-orb moment"));
    _paranPrev  = paranToolBar->addAction(QStringLiteral("◀"));
    _paranPrev->setToolTip(tr("Previous in-orb moment"));
    _paranPeak  = paranToolBar->addAction(QStringLiteral("◉"));
    _paranPeak->setToolTip(tr("Snap to tightest orb (focal paran)"));
    _paranNext  = paranToolBar->addAction(QStringLiteral("▶"));
    _paranNext->setToolTip(tr("Next in-orb moment"));
    _paranLast  = paranToolBar->addAction(QStringLiteral("▶|"));
    _paranLast->setToolTip(tr("Last in-orb moment"));

    _paranLabel = new QLabel(tr("(no paran)"));
    _paranLabel->setContentsMargins(6, 0, 6, 0);
    paranToolBar->addWidget(_paranLabel);

    connect(_paranFirst, &QAction::triggered, this, [this] { paranStep(-2); });
    connect(_paranPrev,  &QAction::triggered, this, [this] { paranStep(-1); });
    connect(_paranPeak,  &QAction::triggered, this, [this] { paranStep(0);  });
    connect(_paranNext,  &QAction::triggered, this, [this] { paranStep(+1); });
    connect(_paranLast,  &QAction::triggered, this, [this] { paranStep(+2); });

    // Theme-aware glyph/label color + compact padding. Re-applied on theme
    // change so the colors track a live theme switch.
    auto styleBar = [this] {
        const QString fg = ThemeManager::instance().getTextColor();
        paranToolBar->setStyleSheet(QStringLiteral(
            "QToolBar#aspectRangeNavigator { spacing: 0px; }"
            "QToolBar#aspectRangeNavigator QToolButton {"
            "  color: %1; font-size: 15px; font-weight: bold;"
            "  padding: 1px 5px; margin: 0px; min-width: 0px; }"
            "QToolBar#aspectRangeNavigator QToolButton:disabled { color: #808080; }"
            "QToolBar#aspectRangeNavigator QLabel { color: %1; padding-left: 8px; }")
            .arg(fg));
    };
    styleBar();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [styleBar] { styleBar(); });

    updateParanTransport(); // start in the disabled (no-paran) state
}

AstroFile*
MainWindow::paranMovingFile() const
{
    for (AstroFile* f : filesBar->currentFiles())
        if (f && f->getType() == TypeParan
            && !f->getParanOccurrences().isEmpty())
            return f;
    return nullptr;
}

void
MainWindow::rewireParanTransport()
{
    // Re-subscribe to the current tab's files so the transport reflects paran
    // creation, stepping, and any other GMT change without polling.
    for (const auto& c : _paranConns) disconnect(c);
    _paranConns.clear();
    for (AstroFile* f : filesBar->currentFiles()) {
        if (!f) continue;
        _paranConns << connect(f, &AstroFile::changed, this,
                               [this](AstroFile::Members) { updateParanTransport(); });
    }
    updateParanTransport();
}

void
MainWindow::updateParanTransport()
{
    if (!paranToolBar) return;
    AstroFile* mv = paranMovingFile();

    if (!mv) {
        _paranOccIndex = -1;
        for (QAction* a : { _paranFirst, _paranPrev, _paranPeak,
                            _paranNext, _paranLast })
            a->setEnabled(false);
        _paranLabel->setText(tr("(no paran)"));
        return;
    }

    const auto& occ = mv->getParanOccurrences();
    const int   n   = occ.size();

    // Index = occurrence nearest current GMT; peak = tightest orb (anchor).
    const QDateTime gmt = mv->getGMT();
    int    best = 0, peak = 0;
    qint64 bestAbs = qAbs(occ[0].first.secsTo(gmt));
    for (int i = 1; i < n; ++i) {
        const qint64 d = qAbs(occ[i].first.secsTo(gmt));
        if (d < bestAbs) { bestAbs = d; best = i; }
        if (occ[i].second < occ[peak].second) peak = i;
    }
    _paranOccIndex = best;

    _paranFirst->setEnabled(best > 0);
    _paranPrev->setEnabled(best > 0);
    _paranNext->setEnabled(best < n - 1);
    _paranLast->setEnabled(best < n - 1);
    _paranPeak->setEnabled(best != peak);

    const QDateTime when = occ[best].first.toLocalTime();
    _paranLabel->setText(QStringLiteral("%1   %2/%3   (orb %4°)")
                             .arg(when.toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                             .arg(best + 1)
                             .arg(n)
                             .arg(occ[best].second, 0, 'f', 2));
}

void
MainWindow::paranStep(int mode)
{
    AstroFile* mv = paranMovingFile();
    if (!mv) return;
    const auto& occ = mv->getParanOccurrences();
    const int   n   = occ.size();
    if (n == 0) return;

    int idx = (_paranOccIndex < 0) ? 0 : _paranOccIndex;
    switch (mode) {
    case -2: idx = 0;                    break;
    case -1: idx = qMax(0, idx - 1);     break;
    case +1: idx = qMin(n - 1, idx + 1); break;
    case +2: idx = n - 1;                break;
    case 0: { // snap to tightest orb
        int peak = 0;
        for (int i = 1; i < n; ++i)
            if (occ[i].second < occ[peak].second) peak = i;
        idx = peak;
        break;
    }
    }

    _paranOccIndex = idx;

    // Reproduce the focal-aspect purview the paran click established. focalExpand
    // is a transient process-global that clickedCell sets only for its own
    // (synchronous) redraw and then reverts; without restoring it here the step
    // redraw would draw the paran lines but drop the attendant aspect lines
    // (the aspects involving the paran bodies). We set it across setGMT — whose
    // redraw is synchronous — then restore. Alt is still read live in
    // calculateAspects, so Alt+step keeps giving the whole-chart clusters.
    const bool savedExpand = astroWidget ? astroWidget->focalExpand() : false;
    if (astroWidget) astroWidget->focalExpand() = true;
    // setGMT fans out (Plain re-renders Directions, Chart redraws) and fires
    // changed() → updateParanTransport() via the rewired connection.
    mv->setGMT(occ[idx].first);
    if (astroWidget) astroWidget->focalExpand() = savedExpand;
}

void
MainWindow::currentTabChanged()
{
    if (!filesBar->count()) return;
    astroWidget->setFiles(filesBar->currentFiles());
    rewireParanTransport();
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
    s.setValue("theme", "dark");
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
    // Populate API key from APIKey.ini for display in settings dialog only
    // This value is never saved to settings.ini
    s.setValue("Key", SessionManager::readAPIKey());
    s.setValue("theme", ThemeManager::instance().currentThemeName());
    return s;
}

void
MainWindow::applySettings(const AppSettings& s)
{
    astroWidget->applySettings(s);
    this->restoreGeometry(s.value("Window/Geometry").toByteArray());
    this->restoreState(s.value("Window/State").toByteArray());
    askToSave = s.value("askToSave").toBool();
    
    // Apply theme
    if (s.contains("theme")) {
        QString themeName = s.value("theme").toString();
        ThemeManager& tm = ThemeManager::instance();
        tm.setTheme(themeName, true);  // Apply to all widgets
    }
    
    // Handle API key - only save to APIKey.ini if changed via settings dialog
    // Note: Key should only be processed when coming from the settings dialog with a non-empty value
    // Session files may have empty Key= entries which should be ignored
    if (s.contains("Key")) {
        QString newKey = s.value("Key").toString();
        
        // Only process non-empty keys (empty means it came from an old session file)
        if (!newKey.isEmpty()) {
            QString currentKey = SessionManager::readAPIKey();
            
            // If key changed in settings dialog, save it to APIKey.ini
            if (newKey != currentKey) {
                qDebug() << "API key changed in settings dialog, saving to APIKey.ini";
                SessionManager::writeAPIKey(newKey);
                _APIKey = newKey.toStdString();
            }
        }
    }
    
    // Always ensure API key is loaded from APIKey.ini at startup
    if (_APIKey.empty()) {
        _APIKey = SessionManager::readAPIKey().toStdString();
    }
}

void
MainWindow::saveSettings(const QString& iniFile)
{
    // Get current settings but exclude the API key
    AppSettings s = currentSettings();
    s.values().remove("Key");  // Never save API key to settings.ini or session files
    s.save(iniFile);
}

void
MainWindow::setupSettingsEditor(AppSettingsEditor* ed)
{
    // Add theme selector
    QKeyValueList themes {
        { tr("Dark"), "dark" },
        { tr("Light"), "light" },
        { tr("Printable"), "printable" }
    };
    ed->addComboBox("theme", tr("Theme"), themes);
    
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

    QString currentSession = SessionManager::currentSessionFile();
    bool isNamed = SessionManager::isNamedSession(currentSession);

    if (!isNamed && !sessionWorthKeeping()) {
        // A pristine/empty auto session is throwaway — discard it instead of
        // persisting a dummy entry in the session list.  (Named sessions are
        // always kept; the user deliberately named them.)
        qDebug() << "Discarding empty auto session on close:" << currentSession;
        SessionManager::removeFromMRU(currentSession);
        QFile::remove(currentSession);
    } else {
        // Update the current session file in-place.
        saveSession();
        saveSettings(currentSession);
        SessionManager::addToMRU(currentSession);
        qDebug() << (isNamed ? "Named session updated:" : "Auto session updated:")
                 << currentSession;
    }

    ev->accept();
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
    
    // Theme-aware colors
    ThemeManager& tm = ThemeManager::instance();
    if (tm.currentTheme() == ThemeManager::Theme::Light) {
        gradient.setColorAt(0, QColor(250, 250, 250));  // Very light center
        gradient.setColorAt(1, QColor(220, 220, 220));  // Slightly darker edges
    } else {
        gradient.setColorAt(0, QColor(32, 32, 32));     // Lighter center
        gradient.setColorAt(1, QColor(80, 80, 80));     // Darker edges
    }
    
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
        showThemedWarning(this,
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
MainWindow::handleChartDroppedOnSlides(const QString& filePath)
{
    qDebug() << "MainWindow::handleChartDroppedOnSlides:" << filePath;
    
    // Close secondary chart first (like double-click behavior)
    if (!filesBar->closeSecondaryChart()) {
        // User canceled the save prompt
        return;
    }
    
    // Decode URL encoding (e.g., %3d -> =)
    QString decodedPath = QUrl::fromPercentEncoding(filePath.toUtf8());
    qDebug() << "Decoded path:" << decodedPath;
    
    // Use the file path directly - AFileInfo has a constructor that takes a full path
    AFileInfo fi(decodedPath);
    if (fi.exists()) {
        filesBar->openFile(fi);
        // Force tab update to show new chart name
        filesBar->updateTab(filesBar->currentIndex());
    } else {
        showThemedWarning(this,
                             tr("File Not Found"),
                             tr("Could not find chart file: %1").arg(decodedPath));
    }
}

void
MainWindow::handleChartDroppedOnInputWidget(const QString& filePath, int targetIndex)
{
    qDebug() << "MainWindow::handleChartDroppedOnInputWidget:" << filePath << "targetIndex:" << targetIndex;
    
    // Decode URL encoding (e.g., %3d -> =)
    QString decodedPath = QUrl::fromPercentEncoding(filePath.toUtf8());
    qDebug() << "Decoded path:" << decodedPath;
    
    // AFileInfo constructor intelligently handles paths that already end with .dat
    AFileInfo fi(decodedPath);
    if (!fi.exists()) {
        showThemedWarning(this,
                             tr("File Not Found"),
                             tr("Could not find chart file: %1").arg(decodedPath));
        return;
    }
    
    if (targetIndex == 0) {
        // Dropped on left (primary chart) widget - open as primary without closing secondary
        // Check if there's an unsaved primary chart
        auto files = filesBar->currentFiles();
        if (!files.isEmpty() && files[0] && files[0]->hasUnsavedChanges()) {
            QMessageBox msgBox;
            msgBox.setText(
                tr("Save changes in '%1' before opening another chart?").arg(files[0]->getName()));
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No
                                      | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Yes);
            int ret = msgBox.exec();

            switch (ret) {
            case QMessageBox::Yes:    files[0]->save(); break;
            case QMessageBox::Cancel: return;
            default:                  break;
            }
        }
        
        // Open as primary chart (without creating new tab if current tab has no changes)
        if (files.isEmpty() || !files[0] || !files[0]->hasUnsavedChanges()) {
            if (files.isEmpty() || !files[0]) {
                filesBar->openFileInNewTab(fi);
            } else {
                files[0]->load(fi);
            }
        } else {
            filesBar->openFileInNewTab(fi);
        }
        // Force tab update to show new chart name
        filesBar->updateTab(filesBar->currentIndex());
    } else if (targetIndex == 1) {
        // Dropped on right (secondary chart) widget - open as secondary
        // This is like the synastry/openFileAsSecond functionality
        auto files = filesBar->currentFiles();
        if (files.count() >= 2 && files[1] && files[1]->hasUnsavedChanges()) {
            QMessageBox msgBox;
            msgBox.setText(
                tr("Save changes in '%1' before replacing it?").arg(files[1]->getName()));
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No
                                      | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Yes);
            int ret = msgBox.exec();

            switch (ret) {
            case QMessageBox::Yes:    files[1]->save(); break;
            case QMessageBox::Cancel: return;
            default:                  break;
            }
        }
        
        // Open as secondary chart
        filesBar->openFileAsSecond(fi);
        // Force tab update to show new chart name
        filesBar->updateTab(filesBar->currentIndex());
    }
}

QString
MainWindow::pickSessionFile()
{
    QList<SessionManager::SessionInfo> sessions = SessionManager::getRecentSessions();

    if (sessions.isEmpty()) {
        showThemedInformation(this,
                                 tr("No Saved Sessions"),
                                 tr("There are no saved sessions to restore."));
        return QString();
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Restore Session"));
    dialog.setMinimumWidth(500);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* label = new QLabel(tr("Select a session to restore:"));
    layout->addWidget(label);

    QListWidget* listWidget = new QListWidget;
    // Override transparent background with dark background for readability
    listWidget->setStyleSheet("QListWidget { background: #2b2b2b; }");

    for (const SessionManager::SessionInfo& info : sessions) {
        QString displayText = info.timestamp.toString("MMM dd, yyyy hh:mm AP");
        displayText += QString(" - %1 tab%2").arg(info.tabCount).arg(info.tabCount == 1 ? "" : "s");

        if (!info.name.isEmpty())
            displayText += QString(" (%1)").arg(info.name);

        QListWidgetItem* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, info.filename); // absolute session path
        if (info.inaugurated.isValid())
            item->setToolTip(tr("Created %1")
                                 .arg(SessionManager::friendlyTimestamp(info.inaugurated)));
        listWidget->addItem(item);
    }

    listWidget->setCurrentRow(0); // Select first (most recent)
    layout->addWidget(listWidget);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) return QString();
    QListWidgetItem* selectedItem = listWidget->currentItem();
    return selectedItem ? selectedItem->data(Qt::UserRole).toString() : QString();
}

void
MainWindow::showRestoreSessionDialog()
{
    QString selectedSessionFile = pickSessionFile();
    if (selectedSessionFile.isEmpty()) return;

    // Warn if there are unsaved changes
    bool hasUnsaved = false;
    for (int i = 0; i < filesBar->count() && !hasUnsaved; ++i)
        for (AstroFile* af : filesBar->files[i])
            if (af && af->hasUnsavedChanges()) { hasUnsaved = true; break; }

    if (hasUnsaved) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Unsaved Changes"));
        msgBox.setText(tr("You have unsaved changes in the current session."));
        msgBox.setInformativeText(tr("The current session has been automatically saved. Click OK to restore the selected session."));
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);
        if (msgBox.exec() == QMessageBox::Cancel)
            return; // Don't restore
    }

    // Persist (or discard) the session we're leaving so its edits survive the
    // relaunch and the MRU stays accurate.  An empty auto session the user
    // never put real content into is discarded rather than left as a dummy.
    QString leaving = SessionManager::currentSessionFile();
    if (!SessionManager::isNamedSession(leaving) && !sessionWorthKeeping()) {
        qDebug() << "Discarding empty session on switch:" << leaving;
        SessionManager::removeFromMRU(leaving);
        QFile::remove(leaving);
    } else {
        saveSession();
        saveSettings(leaving);
        SessionManager::addToMRU(leaving);
    }

    // Switch sessions by relaunching with --load-session: a fresh process opens
    // the selected session in place (adopt-identity, saves back on exit) and
    // becomes the most-recent.  This mirrors the Database "open session" path
    // and avoids tearing down the live UI in-process (clearing all tabs left it
    // in a fragile zero-tab state that crashed downstream handlers).
    QStringList arguments;
    arguments << "--load-session" << selectedSessionFile;
    qDebug() << "Relaunching for session:" << selectedSessionFile;

    if (QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments)) {
        // Close this instance once the new one has had a moment to start.
        QTimer::singleShot(100, qApp, &QApplication::quit);
    } else {
        showThemedCritical(this, tr("Error"),
            tr("Failed to start new instance with session: %1").arg(selectedSessionFile));
    }
}

void
MainWindow::saveSessionAs()
{
    QString currentSessionFile = SessionManager::currentSessionFile();
    QFileInfo currentFileInfo(currentSessionFile);
    QString currentBaseName = currentFileInfo.completeBaseName();
    
    // Determine current name for dialog
    QString currentName;
    if (currentBaseName.startsWith("session-")) {
        QString suffix = currentBaseName.mid(8); // Remove "session-" prefix
        bool isTimestamp;
        suffix.toLongLong(&isTimestamp);
        if (isTimestamp) {
            currentName = ""; // Timestamped session, suggest empty for user to name
        } else {
            currentName = AFileInfo::decodeName(suffix); // Named session
        }
    } else {
        currentName = AFileInfo::decodeName(currentBaseName);
    }
    
    bool ok;
    QString newName = QInputDialog::getText(
        this,
        tr("Save Session As"),
        tr("Session name:"),
        QLineEdit::Normal,
        currentName,
        &ok);
    
    if (!ok || newName.isEmpty()) return;
    
    // Generate new filename (already includes full path from sessionFileFromName)
    QString newSessionFile = SessionManager::sessionFileFromName(newName);
    
    // Check if target already exists
    if (QFile::exists(newSessionFile) && newSessionFile != currentSessionFile) {
        int reply = showThemedQuestion(
            this,
            tr("Overwrite Session"),
            tr("A session with this name already exists. Overwrite it?"),
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply != QMessageBox::Yes) {
            return;
        }
        
        // Remove existing file
        QFile::remove(newSessionFile);
    }
    
    // Save current session state to the current file
    saveSession();
    saveSettings(currentSessionFile);
    
    // If renaming to a different file, copy/rename
    if (newSessionFile != currentSessionFile) {
        if (QFile::copy(currentSessionFile, newSessionFile)) {
            qDebug() << "Session saved as:" << newSessionFile;
            
            // Update the current session file reference to the new name
            SessionManager::setCurrentSessionFile(newSessionFile);
            
            // Update database to reflect the new session file
            astroDatabase->updateList();
            
            // Update window title with new session name
            updateWindowTitle();
            
            showThemedInformation(this,
                                   tr("Session Saved"),
                                   tr("Session saved as: %1").arg(newName));
        } else {
            showThemedWarning(this,
                               tr("Save Failed"),
                               tr("Failed to save session as: %1").arg(newName));
        }
    } else {
        qDebug() << "Session already has this name";
        showThemedInformation(this,
                               tr("Session Saved"),
                               tr("Session saved: %1").arg(newName));
    }
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
    
    // Background image is set via QSS (border-image on QDialog#about)
    
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
        "Copyright © 2016-2026 Josh Baudhuin/Turtle Crescent Graphics<br>"
        "<a style='color:yellow' href='https://github.com/jbaudhuin/joshb-work-sidereal'>github.com/jbaudhuin/joshb-work-sidereal</a></p>"
        "<p><b>Key Features Added:</b><br>"
        "• Sidereal zodiac support (Fagan-Bradley & Lahiri ayanamshas)<br>"
        "• Harmonic charts and patterns (H1-H32)<br>"
        "• Fixed star conjunctions and parans<br>"
        "• Paranatellonta event finding<br>"
        "• Primary directions and speculum<br>"
        "• Declination strip view<br>"
        "• Bi-wheel prime vertical display<br>"
        "• Comprehensive event search and timing<br>"
        "• Transit and progression tracking<br>"
        "• Equatorial and prime vertical aspects<br>"
        "• Complete session management with auto-save/restore<br>"
        "• Named sessions with multi-window support</p>"
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
    QString currentSession = SessionManager::currentSessionFile();

    // Read the original inauguration time BEFORE saveFilesToSession() runs.
    // saveFilesToSession() does remove("") on the [Session] group, and because
    // two QSettings on the same file share Qt's underlying store, any metadata
    // written *before* that call is wiped.  (This is why lastSaved/inaugurated
    // historically never persisted.)  So: capture inaugurated first, write the
    // tab contents, then write ALL [Session] metadata afterward.
    QDateTime inaugurated;
    {
        QSettings probe(currentSession, QSettings::IniFormat);
        inaugurated = probe.value("Session/inaugurated").toDateTime();
    }

    int totalCharts = 0;
    if (filesBar->count() > 0) {
        filesBar->saveFilesToSession(); // clears [Session] + writes Tab groups
        for (int i = 0; i < filesBar->count(); ++i)
            totalCharts += filesBar->files[i].count();
    }

    QSettings settings(currentSession, QSettings::IniFormat);
    settings.beginGroup("Session");

    settings.setValue("lastSaved", QDateTime::currentDateTime());
    settings.setValue("inaugurated",
                      inaugurated.isValid() ? inaugurated : QDateTime::currentDateTime());

    // Save the session name for named sessions (non-"session-<digits>" file).
    QString baseName = QFileInfo(currentSession).baseName();
    bool named = true;
    if (baseName.startsWith("session-")) {
        bool isTimestamp;
        baseName.mid(8).toLongLong(&isTimestamp);
        named = !isTimestamp;
    }
    if (named)
        settings.setValue("name", AFileInfo::decodeName(baseName));

    if (filesBar->count() == 0) {
        settings.setValue("tabCount", 0);
        settings.setValue("currentTab", 0);
        settings.setValue("chartCount", 0);
        qDebug() << "Session saved: empty (no tabs)";
    } else {
        settings.setValue("chartCount", totalCharts);
        settings.setValue("currentTab", filesBar->currentIndex());
        qDebug() << "Session saved:" << filesBar->count() << "tabs," << totalCharts << "charts";
    }

    settings.endGroup();

    // Save database tree state
    astroDatabase->saveDatabaseState();
}

bool
MainWindow::sessionWorthKeeping() const
{
    // Worth persisting if any open chart is saved on disk or has unsaved edits.
    // A pristine new session (an untouched "Untitled" — its unsaved flag is
    // cleared at creation) is throwaway scratch space and gets discarded rather
    // than cluttering the session list.
    for (int i = 0; i < filesBar->count(); ++i) {
        for (AstroFile* af : filesBar->files[i]) {
            if (!af) continue;
            if (af->fileInfo().exists() || af->hasUnsavedChanges())
                return true;
        }
    }
    return false;
}

bool
MainWindow::hasOtherInstances()
{
    // Try to connect to the single instance server
    // If we can connect, another instance exists
    QLocalSocket socket;
    socket.connectToServer("ZodiacSiderealInstance");
    
    if (socket.waitForConnected(500)) {
        // Another instance is running
        socket.disconnectFromServer();
        socket.waitForDisconnected(500);
        return true;
    }
    
    return false;
}

void
MainWindow::restoreSession()
{
    qDebug() << "=== restoreSession() called ===";
    QSettings settings(SessionManager::currentSessionFile(), QSettings::IniFormat);
    qDebug() << "Reading session from:" << SessionManager::currentSessionFile();
    
    int tabCount = settings.value("Session/tabCount", 0).toInt();
    qDebug() << "tabCount from session file:" << tabCount;
    
    if (tabCount == 0) {
        // No saved session, create default new file
        filesBar->addNewFile();
        // Pristine startup session — clear the setup-induced unsaved flag so an
        // untouched session is recognized as empty (see ctor's --new path).
        if (filesBar->currentFiles().count())
            filesBar->currentFiles()[0]->clearUnsavedState();
        return;
    }
    
    // Restore each tab
    for (int i = 0; i < tabCount; ++i) {
        QString tabGroup = QString("Tab%1").arg(i);
        
        int fileCount = settings.value(tabGroup + "/fileCount", 0).toInt();
        qDebug() << "Tab" << i << "fileCount:" << fileCount;
        
        if (fileCount == 0) {
            qDebug() << "Tab" << i << "has no files, skipping";
            continue;
        }
        
        for (int j = 0; j < fileCount; ++j) {
            // Use compound group name to match save format
            QString fileGroup = QString("%1.File%2/").arg(tabGroup).arg(j);
            
            // Read directory and filename
            QString directory = settings.value(fileGroup + "directory").toString();
            QString filename = settings.value(fileGroup + "filename").toString();
            bool hasUnsavedChanges = settings.value(fileGroup + "hasUnsavedChanges", false).toBool();
            
            AstroFile* af = nullptr;
            bool hasCurrentData = settings.contains(fileGroup + "name") && settings.contains(fileGroup + "gmt");
            
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
                    af->setName(settings.value(fileGroup + "name").toString());
                    af->setGMT(settings.value(fileGroup + "gmt").toDateTime());
                    af->setType((FileType)settings.value(fileGroup + "type", TypeEvent).toInt());
                    af->setLocation(settings.value(fileGroup + "location").value<QVector3D>());
                    af->setLocationName(settings.value(fileGroup + "locationName").toString());
                    af->setTimezone(settings.value(fileGroup + "timezone").toDouble());
                    af->setHarmonic(settings.value(fileGroup + "harmonic", 1.0).toDouble());
                    af->setComment(settings.value(fileGroup + "comment").toString());
                    
                    if (settings.value(fileGroup + "hasBaseChart", false).toBool()) {
                        af->setBaseChart(settings.value(fileGroup + "baseChart").toDateTime());
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
                    continue;
                }
                
                // Restore transit date range (per-tab UI state) for all files
                if (af) {
                    if (settings.contains(fileGroup + "transitStartDate")) {
                        af->setTransitStartDate(settings.value(fileGroup + "transitStartDate").toDate());
                    }
                    if (settings.contains(fileGroup + "transitDuration")) {
                        af->setTransitDuration(settings.value(fileGroup + "transitDuration").toString());
                    }
                    if (settings.contains(fileGroup + "transitLocation")) {
                        af->setTransitLocation(settings.value(fileGroup + "transitLocation").value<QVector3D>());
                        af->setTransitLocationName(settings.value(fileGroup + "transitLocationName").toString());
                        af->setTransitTimezone(settings.value(fileGroup + "transitTimezone").value<short>());
                    }
                    
                    // Restore per-tab pattern input field
                    if (settings.contains(fileGroup + "transitPattern")) {
                        af->setTransitPattern(settings.value(fileGroup + "transitPattern").toString());
                    }

                    // Restore per-file transit event options (toolbar state)
                    if (settings.contains(fileGroup + "transitEventOptions")) {
                        A::EventTypeSet eventOpts;
                        QVariant vopt = settings.value(fileGroup + "transitEventOptions");
                        
                        // Handle both old format (QVariantList of ints) and new format (QStringList)
                        if (vopt.canConvert<QStringList>()) {
                            // New format: strings using event brief names
                            QStringList sl = vopt.toStringList();
                            for (const auto& s : sl) {
                                A::EventType et = A::EventTypeManager::briefToEventType(s);
                                if (et != A::etcUnknownEvent) {
                                    eventOpts.insert(et);
                                }
                            }
                        } else {
                            // Old format: integers (for backward compatibility)
                            auto vl = vopt.toList();
                            for (const auto& v : vl) {
                                eventOpts.insert(static_cast<A::EventType>(v.toInt()));
                            }
                        }
                        af->setTransitEventOptions(eventOpts);
                    }

                    // Restore per-tab skip-by-duration level
                    if (settings.contains(fileGroup + "transitSkipByDuration")) {
                        af->setTransitSkipByDuration(
                            static_cast<A::EventOptions::skipper>(
                                settings.value(fileGroup + "transitSkipByDuration").toUInt()));
                    }

                    // Restore per-tab auto-reconcile preference
                    if (settings.contains(fileGroup + "transitAutoReconcile")) {
                        af->setTransitAutoReconcile(
                            settings.value(fileGroup + "transitAutoReconcile").toBool());
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
        }
    }
    
    // Restore current tab
    int currentTab = settings.value("Session/currentTab", 0).toInt();
    if (currentTab < filesBar->count()) {
        filesBar->setCurrentIndex(currentTab);
    }
    
    // If no tabs were successfully restored, create a new file
    if (filesBar->count() == 0) {
        qDebug() << "No tabs restored, creating new file";
        filesBar->addNewFile();
    } else {
        qDebug() << "Session restored:" << filesBar->count() << "tabs";
    }
}

QString
MainWindow::generateSessionKey(const QDateTime& dt)
{
    return QString("Session_%1").arg(dt.toSecsSinceEpoch());
}

QList<MainWindow::SessionInfo>
MainWindow::listRecentSessions()
{
    QSettings settings(SessionManager::settingsFile(), QSettings::IniFormat);
    QList<SessionInfo> sessions;
    
    // Get all groups that start with "Session_"
    QStringList allGroups = settings.childGroups();
    
    for (const QString& group : allGroups) {
        if (group.startsWith("Session_")) {
            settings.beginGroup(group);
            
            SessionInfo info;
            info.key = group;
            
            // Extract timestamp from key (Session_1234567890)
            QString timestampStr = group.mid(8); // Skip "Session_"
            qint64 timestamp = timestampStr.toLongLong();
            info.timestamp = QDateTime::fromSecsSinceEpoch(timestamp);
            
            // Get tab count
            info.tabCount = settings.value("tabCount", 0).toInt();
            
            // Get optional name (future feature)
            info.name = settings.value("sessionName", "").toString();
            
            sessions.append(info);
            settings.endGroup();
        }
    }
    
    // Sort by timestamp, newest first
    std::sort(sessions.begin(), sessions.end(), 
              [](const SessionInfo& a, const SessionInfo& b) {
                  return a.timestamp > b.timestamp;
              });
    
    return sessions;
}

void
MainWindow::saveSessionWithTimestamp(const QString& sessionKey)
{
    QSettings settings(SessionManager::settingsFile(), QSettings::IniFormat);
    settings.beginGroup(sessionKey);
    
    if (filesBar->count() == 0) {
        // No tabs - save empty session
        settings.setValue("tabCount", 0);
        settings.setValue("currentTab", 0);
        qDebug() << "Session saved to" << sessionKey << ": empty (no tabs)";
    } else {
        // Clear old data in this session group
        settings.remove("");
        
        // Save tab count and current tab
        settings.setValue("tabCount", filesBar->count());
        settings.setValue("currentTab", filesBar->currentIndex());
        
        // Save each tab
        for (int i = 0; i < filesBar->count(); ++i) {
            settings.beginGroup(QString("Tab%1").arg(i));
            
            const AstroFileList& fileList = filesBar->files[i];
            settings.setValue("fileCount", fileList.count());
            
            for (int j = 0; j < fileList.count(); ++j) {
                settings.beginGroup(QString("File%1").arg(j));
                
                AstroFile* af = fileList[j];
                AFileInfo fileInfo = af->fileInfo();
                
                bool hasSavedFile = fileInfo.exists() 
                                 && !af->getName().startsWith("Untitled")
                                 && !fileInfo.absoluteFilePath().isEmpty();
                
                bool hasModifications = af->hasUnsavedChanges();
                
                if (hasSavedFile) {
                    QString dir = fileInfo.absolutePath();
                    QString name = fileInfo.baseName();
                    settings.setValue("directory", dir);
                    settings.setValue("filename", name);
                }
                
                settings.setValue("hasUnsavedChanges", hasModifications);
                
                if (!hasSavedFile || hasModifications) {
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
                
                if (!af->getTransitStartDate().isNull()) {
                    settings.setValue("transitStartDate", af->getTransitStartDate());
                }
                if (!af->getTransitDuration().isEmpty()) {
                    settings.setValue("transitDuration", af->getTransitDuration());
                }
                if (af->hasTransitLocation()) {
                    settings.setValue("transitLocation", af->getTransitLocation());
                    settings.setValue("transitLocationName", af->getTransitLocationName());
                    settings.setValue("transitTimezone", af->getTransitTimezone());
                }
                if (!af->getTransitPattern().isEmpty()) {
                    settings.setValue("transitPattern", af->getTransitPattern());
                }
                
                settings.endGroup(); // File%1
            }
            
            settings.endGroup(); // Tab%1
        }
        
        qDebug() << "Session saved to" << sessionKey << ":" << filesBar->count() << "tabs";
    }
    
    settings.endGroup();
    
    // Save database tree state (shared across all sessions)
    astroDatabase->saveDatabaseState();
    
    // Update current session key
    _currentSessionKey = sessionKey;
}

void
MainWindow::restoreSessionByKey(const QString& sessionKey)
{
    QSettings settings(SessionManager::settingsFile(), QSettings::IniFormat);
    settings.beginGroup(sessionKey);
    
    int tabCount = settings.value("tabCount", 0).toInt();
    
    // Close all existing tabs first
    while (filesBar->count() > 0) {
        filesBar->closeTab(0);
    }
    
    if (tabCount == 0) {
        filesBar->addNewFile();
        settings.endGroup();
        _currentSessionKey = sessionKey;
        return;
    }
    
    // Restore each tab (same logic as restoreSession())
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
            
            QString directory = settings.value("directory").toString();
            QString filename = settings.value("filename").toString();
            bool hasUnsavedChanges = settings.value("hasUnsavedChanges", false).toBool();
            
            AstroFile* af = nullptr;
            bool hasCurrentData = settings.contains("name") && settings.contains("gmt");
            
            try {
                if (!directory.isEmpty() && !filename.isEmpty() && !hasCurrentData) {
                    af = new AstroFile;
                    astroWidget->setupFile(af);
                    AFileInfo fileInfo(QDir(directory), filename);
                    af->load(fileInfo);
                } else if (hasCurrentData) {
                    af = new AstroFile;
                    astroWidget->setupFile(af);
                    
                    af->suspendUpdate();
                    af->setName(settings.value("name").toString());
                    af->setGMT(settings.value("gmt").toDateTime());
                    af->setType((FileType)settings.value("type", TypeEvent).toInt());
                    af->setLocation(settings.value("location").value<QVector3D>());
                    af->setLocationName(settings.value("locationName").toString());
                    af->setTimezone(settings.value("timezone").toDouble());
                    af->setHarmonic(settings.value("harmonic", 1.0).toDouble());
                    af->setComment(settings.value("comment").toString());
                    
                    if (settings.value("hasBaseChart", false).toBool()) {
                        af->setBaseChart(settings.value("baseChart").toDateTime());
                    }
                    
                    af->resumeUpdate();
                    
                    if (!hasUnsavedChanges) {
                        af->clearUnsavedState();
                    }
                }
                
                if (af) {
                    if (settings.contains("transitStartDate")) {
                        af->setTransitStartDate(settings.value("transitStartDate").toDate());
                    }
                    if (settings.contains("transitDuration")) {
                        af->setTransitDuration(settings.value("transitDuration").toString());
                    }
                    if (settings.contains("transitLocation")) {
                        af->setTransitLocation(settings.value("transitLocation").value<QVector3D>());
                        af->setTransitLocationName(settings.value("transitLocationName").toString());
                        af->setTransitTimezone(settings.value("transitTimezone").value<short>());
                    }
                    if (settings.contains("transitPattern")) {
                        af->setTransitPattern(settings.value("transitPattern").toString());
                    }
                    
                    if (j == 0) {
                        filesBar->addFile(af);
                    } else {
                        filesBar->openTransitsAsSecond(af);
                    }
                }
            } catch (const std::exception& e) {
                qDebug() << "Error restoring file from" << sessionKey << ":" << e.what();
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
    
    if (filesBar->count() == 0) {
        filesBar->addNewFile();
    }
    
    _currentSessionKey = sessionKey;
    qDebug() << "Restored session from" << sessionKey << ":" << filesBar->count() << "tabs";
}

void
MainWindow::pruneOldSessions(int maxSessions)
{
    QList<SessionInfo> sessions = listRecentSessions();
    
    if (sessions.size() <= maxSessions) {
        return; // Nothing to prune
    }
    
    QSettings settings(SessionManager::settingsFile(), QSettings::IniFormat);
    
    // Remove sessions beyond the limit (oldest ones)
    for (int i = maxSessions; i < sessions.size(); ++i) {
        settings.remove(sessions[i].key);
        qDebug() << "Pruned old session:" << sessions[i].key;
    }
}

void
FilesBar::saveFilesToSession()
{
    QSettings settings(SessionManager::currentSessionFile(), QSettings::IniFormat);
    
    // Clear old session data
    settings.beginGroup("Session");
    settings.remove(""); // Removes all keys in current group
    settings.endGroup();
    
    // Remove old Tab groups
    QStringList allGroups = settings.childGroups();
    for (const QString& group : allGroups) {
        if (group.startsWith("Tab")) {
            settings.remove(group);
        }
    }
    
    // Save session metadata
    settings.beginGroup("Session");
    settings.setValue("tabCount", count());
    settings.setValue("currentTab", -1); // Will be set by MainWindow::saveSession()
    settings.endGroup();
    
    // Save each tab as a separate group at root level
    for (int i = 0; i < count(); ++i) {
        QString tabGroup = QString("Tab%1").arg(i);
        settings.beginGroup(tabGroup);
        
        const AstroFileList& fileList = files[i];
        settings.setValue("fileCount", fileList.count());
        settings.endGroup(); // End Tab group before creating File groups
        
        // Save each file in this tab
        for (int j = 0; j < fileList.count(); ++j) {
            // Use compound group name: Tab0.File0, Tab0.File1, etc.
            QString fileGroup = QString("%1.File%2").arg(tabGroup).arg(j);
            settings.beginGroup(fileGroup);
            
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
            if (af->hasTransitLocation()) {
                settings.setValue("transitLocation", af->getTransitLocation());
                settings.setValue("transitLocationName", af->getTransitLocationName());
                settings.setValue("transitTimezone", af->getTransitTimezone());
            }
            
            // Save per-tab pattern input field
            if (!af->getTransitPattern().isEmpty()) {
                settings.setValue("transitPattern", af->getTransitPattern());
            }

            // Save per-file transit event options (toolbar state) using brief strings
            const A::EventTypeSet& eventOpts = af->getTransitEventOptions();
            if (!eventOpts.empty()) {
                QStringList sl;
                for (const auto& et : eventOpts) {
                    sl << A::EventTypeManager::eventTypeToBrief(et);
                }
                settings.setValue("transitEventOptions", sl);
            } else {
                settings.setValue("transitEventOptions", QVariant());
            }

            // Save per-tab skip-by-duration level
            settings.setValue("transitSkipByDuration",
                              static_cast<unsigned>(af->getTransitSkipByDuration()));

            // Save per-tab auto-reconcile preference
            settings.setValue("transitAutoReconcile", af->getTransitAutoReconcile());

            settings.endGroup(); // End File group
        }
    }
}
