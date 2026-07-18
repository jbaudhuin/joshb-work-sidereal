#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTimeZone>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTableView>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QDirIterator>
#include <QSettings>

#include "geosearch.h"
#include "fileeditor.h"


/* =========================== ASTRO FILE EDITOR ==================================== */

/*static*/ QStringList AstroFileEditor::signs;

AstroFileEditor::AstroFileEditor(QWidget *parent) :
    AstroFileHandler(parent),
    _inUpdate(false),
    _inApply(false),
    _userEditedTime(false)
{
    if (signs.empty()) {
        signs = QStringList({ "Aries", "Taurus", "Gemini", "Cancer",
                      "Leo", "Virgo", "Libra", "Scorpio",
                      "Sagittarius", "Capricorn", "Aquarius", "Pisces" });
    }

    currentFile = 0;

    tabs       = new QTabBar;
    addFileBtn = new QPushButton(tr("+"));
    name       = new QLineEdit;
    type       = new QComboBox;
    basis      = new QComboBox;
    dateTime   = new A::AstroDateTimeEdit(false /*dateOnly*/);
    timeZone   = new QDoubleSpinBox;
    lockTimezone = new QPushButton;
    lockTimezone->setCheckable(true);
    lockTimezone->setMaximumWidth(40);
    lockTimezone->setFlat(true);  // Remove button styling
    QFont lockFont = lockTimezone->font();
    lockFont.setPointSize(lockFont.pointSize() + 4);  // Make emoji bigger
    lockTimezone->setFont(lockFont);
    lockTimezone->setText("🔓");  // Unicode unlocked padlock
    lockTimezone->setToolTip(tr("Click to lock timezone (for relocation charts)"));
    connect(lockTimezone, &QPushButton::toggled, this, [this](bool checked) {
        lockTimezone->setText(checked ? "🔒" : "🔓");  // Locked / Unlocked
        lockTimezone->setToolTip(checked ? 
            tr("Timezone locked - click to unlock") : 
            tr("Click to lock timezone (for relocation charts)"));
        if (!_inUpdate && !_inApply) {
            applyToFile(true, false);
        }
    });
    geoSearch  = new GeoSearchWidget;
    comment    = new QPlainTextEdit;

    tabs     -> setTabsClosable(true);
    tabs     -> setMovable(true);
    addFileBtn -> setMaximumWidth(32);

    // Populate type combo box with all file types (starting from TypeOther = 0)
    for (unsigned i = TypeOther, n = TypeCount;
         i < n; ++i)
    {
        if (i == TypeSearch || i == TypeDerivedSearch) continue;
        QString s = AstroFile::typeToString(i);
        if (s.isEmpty()) continue;
        type->addItem(tr(s.toLatin1().constData()), i);
    }
    type->setCurrentIndex(-1);

    timeZone -> setRange(-12, 12);
    timeZone -> setDecimals(1);

    dateTime->setMinimumDate(QDate(100,1,1));

    comment->setMaximumHeight(70);
    if (!parent) {
        this->setWindowTitle(tr("Edit entry"));
        this->setWindowFlags(Qt::Dialog /*|
                               Qt::MSWindowsFixedSizeDialogHint*/
                             | Qt::WindowStaysOnTopHint);
    }

    QHBoxLayout* lay4 = new QHBoxLayout;
    lay4->addWidget(tabs);
    lay4->addWidget(addFileBtn);

    QHBoxLayout* lay3 = new QHBoxLayout;
    lay3->addWidget(name);
    lay3->addWidget(new QLabel(tr("Type:")));
    lay3->addWidget(type);

    QHBoxLayout* lay2 = new QHBoxLayout;
    lay2->addWidget(dateTime);
    lay2->addWidget(new QLabel(tr("Time zone:")));
    lay2->addWidget(timeZone);
    lay2->addWidget(lockTimezone);

    QFormLayout* lay1 = new QFormLayout;
    lay1->addRow(tr("Name:"),       lay3);
    basisLbl = new QLabel(tr("Based on:"));
    lay1->addRow(basisLbl,   basis);
    lay1->addRow(tr("Local time:"), lay2);
    lay1->addRow(tr("Location:"),   geoSearch);
    lay1->addItem(new QSpacerItem(1,20));
    lay1->addRow(tr("Comment:"),    comment);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(lay4);
    layout->addLayout(lay1);

    if (!parent) {
        QPushButton* ok     = new QPushButton(tr("OK"));
        QPushButton* cancel = new QPushButton(tr("Cancel"));
        QPushButton* apply  = new QPushButton(tr("Apply"));

        QHBoxLayout* buttons = new QHBoxLayout;
        buttons->addSpacerItem(new QSpacerItem(1,1, QSizePolicy::Expanding, QSizePolicy::Preferred));
        buttons->addWidget(ok);
        buttons->addWidget(cancel);
        buttons->addWidget(apply);

        layout->addLayout(buttons);

        connect(ok,     SIGNAL(clicked()), this, SLOT(applyToFile()));
        connect(apply,  SIGNAL(clicked()), this, SLOT(applyToFile()));
        connect(ok,     SIGNAL(clicked()), this, SLOT(close()));
        connect(cancel, SIGNAL(clicked()), this, SLOT(close()));

        //setWindowFlags(Qt::Dialog);
    }

    connect(tabs,       SIGNAL(currentChanged(int)),    this, SLOT(currentTabChanged(int)));
    connect(tabs,       SIGNAL(tabMoved(int,int)),      this, SLOT(swapFilesSlot(int,int)));
    connect(tabs,       SIGNAL(tabCloseRequested(int)), this, SLOT(removeTab(int)));
    connect(addFileBtn, SIGNAL(clicked()), this, SIGNAL(appendFile()));
    connect(timeZone,   SIGNAL(valueChanged(double)),
            this, SLOT(timezoneChanged()));

    // Track when user manually edits time
    connect(dateTime, &A::AstroDateTimeEdit::dateTimeChanged,
            this, [this]() {
        // Disable timezone controls when LMT/LAT is active
        bool usesZoneTime = (dateTime->timeMode() == A::Time_ZoneTime);
        timeZone->setEnabled(usesZoneTime);
        lockTimezone->setEnabled(usesZoneTime);

        if (!_inUpdate && !_inApply) {
            _userEditedTime = true;
            qDebug() << "User manually edited time";
            applyToFile(true, false);
        }
    });

    // Only update timezone when location changes, not when user edits time
    connect(geoSearch, SIGNAL(locationChanged()),
            this, SLOT(updateTimezone()));

    connect(type,
            qOverload<int>(&QComboBox::currentIndexChanged),
            [this](int i)
    {
        int t = type->itemData(i).toInt();
        bool isProgressed = (t == TypeDerivedProg || t == TypeDerivedSA ||
                            t == TypeDerivedPD);
        bool isReturn = (t == TypeReturn);
        bool showBasis = (isProgressed || isReturn);

        basis->setVisible(showBasis);
        basisLbl->setVisible(showBasis);

        // Trigger update to populate basis combo when type changes
        if (showBasis && !_inUpdate) {
            update(AstroFile::Type);
        }
    });
    type->setCurrentIndex(type->findData(TypeOther));

    // Connect basis combo box to update base chart when changed
    connect(basis, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!_inUpdate) {
            applyToFile(true, false);
        }
    });

    if (parent) name->setFocus();
    else dateTime->setFocus();
}

void
AstroFileEditor::timezoneChanged()
{
    // Just update the display prefix - don't touch the datetime widget
    // The timezone value will be used when converting to GMT in applyToFile()
    if (timeZone->value() < 0)
        timeZone->setPrefix("");
    else
        timeZone->setPrefix("+");
}

void
AstroFileEditor::updateTimezone()
{
    if (_inUpdate || _inApply) {
        return;
    }

    // Check if timezone is locked (for relocation charts)
    if (lockTimezone->isChecked()) {
        qDebug() << "Skipping timezone update - timezone is locked";
        return;
    }

    // Skip timezone lookup when LMT or LAT is active — the timezone
    // spinbox is irrelevant for those modes.
    if (dateTime->timeMode() != A::Time_ZoneTime) {
        qDebug() << "Skipping timezone lookup - time mode is"
                 << (dateTime->timeMode() == A::Time_LMT ? "LMT" : "LAT");
        // Still keep geo-longitude in sync for EoT tooltip
        dateTime->setGeoLongitude(geoSearch->location().x());
        return;
    }

    // Now that time display is decoupled from timezone, we can safely
    // update timezone when location changes without affecting displayed time
    QVector3D vec(geoSearch->location());

    // Keep the datetime widget's longitude in sync for EoT tooltips
    dateTime->setGeoLongitude(vec.x());

    const QString timezoneId = geoSearch->selectedTimezoneId();
    if (!timezoneId.isEmpty()) {
        const QTimeZone tz(timezoneId.toUtf8());
        if (tz.isValid()) {
            const QDateTime localInput = dateTime->localDateTime();
            const QDateTime zonedLocal(localInput.date(), localInput.time(), tz);
            const double offsetHours = zonedLocal.offsetFromUtc() / 3600.0;
            timeZone->setValue(offsetHours);
            qDebug() << "Resolved timezone locally from city DB:" << timezoneId
                     << "offset" << offsetHours;
            return;
        }
    }

    auto nm = new QNetworkAccessManager(this);
    connect(nm, &QNetworkAccessManager::finished,
            [this](QNetworkReply* reply)
    {
        reply->deleteLater();
        if (auto nm = sender()) {
            nm->deleteLater();
        }
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument response =
                QJsonDocument::fromJson(reply->readAll());
        if (response["status"].toString()!="OK") {
            qDebug() << "Timezone request failed:"
                     << response["status"].toString()
                     << response["errorMessage"].toString();
            return;
        }
        qreal tz = (response["rawOffset"].toInt()
                + response["dstOffset"].toInt())/3600;
        timeZone->setValue(tz);

        qDebug() << "Timezone for location is"
                 << response["rawOffset"].toInt()/60 /*minsPerSec*/
                 << "with dstOffset"
                 << response["dstOffset"].toInt()/60
                 << "in" << response["timeZoneName"].toString();
    });

    QString url =
            QString(A::googMapURL + "/timezone/json?"
                                "location=%1,%2"
                                "&timestamp=%4"
                                "&key=%3"
                                "&language=en")
            .arg(vec.y()).arg(vec.x())
                      .arg(MainWindow::instance()->APIKey().c_str())
            .arg(dateTime->localDateTime().toSecsSinceEpoch());
    qDebug() << "Issuing TZ URL:" << url;
    nm->get(QNetworkRequest(url));
}

void AstroFileEditor::updateTabs()
{
    for (int i = 0; i < filesCount(); i++) {
        QString txt = file(i)->getName();
        if (tabs->count() <= i)
            tabs->addTab(txt);
        else
            tabs->setTabText(i, txt);
    }

    // remove unused tabs
    for (int i = filesCount(); i < tabs->count(); i++)
        tabs->removeTab(i);

    addFileBtn->setVisible(filesCount() < 2);
}

void AstroFileEditor::update(AstroFile::Members m)
{
    qDebug() << "AstroFileEditor: show file" << currentFile;
    if (_inApply) return;

    AstroFile* source =  file(currentFile);

    // Reset user-edited flag when loading a chart
    _userEditedTime = false;

    A::modalize<bool> inUpdate(_inUpdate,true);
    
    // Always update the type combo to match the current file type
    // (it may have been changed programmatically even if Type member flag isn't set)
    int idx = type->findData(source->getType());
    if (idx >= 0) {
        type->setCurrentIndex(idx);
    }

    // Show only the base name (without ", loc: City" suffix) in the name field
    name->setText(source->getBaseName());
    geoSearch->blockSignals(true);
    timeZone->blockSignals(true);
    lockTimezone->blockSignals(true);
    dateTime->blockSignals(true);
    basis->blockSignals(true);
    
    geoSearch->setLocation(source->getLocation(),
                           source->getLocationName());
    timeZone->setValue(source->getTimezone());
    lockTimezone->setChecked(source->isTimezoneLocked());
    
    // Update lock button appearance manually since signals are blocked
    bool isLocked = source->isTimezoneLocked();
    lockTimezone->setText(isLocked ? "🔒" : "🔓");
    lockTimezone->setToolTip(isLocked ? 
        tr("Timezone locked - click to unlock") : 
        tr("Click to lock timezone (for relocation charts)"));
    
    // Convert GMT to local time in the chart's timezone
    auto gmtDt = source->getGMT();
    QTimeZone tz(3600 * source->getTimezone());
    QDateTime localDt = gmtDt.toTimeZone(tz);
    
    // Set the datetime widget — calendar type, time mode, then date/time
    dateTime->blockSignals(true);
    dateTime->setCalendarType(source->getCalendarType());
    dateTime->setTimeMode(source->getTimeMode());
    dateTime->setGeoLongitude(source->getLocation().x());
    dateTime->setDate(localDt.date());
    dateTime->setTime(localDt.time());
    dateTime->blockSignals(false);
    
    // Disable timezone controls when LMT/LAT is active
    bool usesZoneTime = (source->getTimeMode() == A::Time_ZoneTime);
    timeZone->setEnabled(usesZoneTime);
    lockTimezone->setEnabled(usesZoneTime);
    
    qDebug() << "Loading: GMT" << gmtDt << "TZ offset" << source->getTimezone()
             << "-> Local time" << localDt.date() << localDt.time();
    
    // Populate basis combo box with available natal charts
    basis->clear();
    basis->addItem(tr("(None)"), QVariant());
    
    FileType curType = source->getType();
    bool showBasis = (curType == TypeDerivedProg || curType == TypeDerivedSA || 
                     curType == TypeDerivedPD || curType == TypeReturn);
    
    // Always populate basis combo for types that need it, even during _inUpdate
    // (because user might be changing the type manually)
    if (showBasis) {
        // Get all chart files from the chart directory (including subdirectories)
        QString chartDir = AstroFile::fixedChartDir();
        QDir dir(chartDir);
        QStringList filters;
        filters << "*.dat";
        
        // Recursively find all .dat files in subdirectories
        QFileInfoList files;
        QDirIterator it(chartDir, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            files.append(it.fileInfo());
        }
        
        int selectedIndex = 0;
        for (int i = 0; i < files.size(); ++i) {
            QFileInfo fi = files.at(i);
            // Show relative path from chart directory for files in subdirectories
            QString displayName = dir.relativeFilePath(fi.absoluteFilePath());
            if (displayName.contains('/') || displayName.contains('\\')) {
                // File is in a subdirectory, show the path
                basis->addItem(displayName.replace('\\', '/'), fi.absoluteFilePath());
            } else {
                // File is in root directory, just show basename
                basis->addItem(fi.baseName(), fi.absoluteFilePath());
            }
            
            // Check if this is the current base chart
            if (source->hasBaseChart()) {
                // Load this file to check its GMT
                QSettings testFile(fi.absoluteFilePath(), QSettings::IniFormat);
                auto testGmt = testFile.value("GMT").toString();
                if (!testGmt.endsWith('Z')) testGmt += 'Z';
                auto testDT = QDateTime::fromString(testGmt, Qt::ISODate);
                
                if (testDT == source->getBaseChartGMT()) {
                    selectedIndex = i + 1; // +1 because "(None)" is at index 0
                }
            }
        }
        basis->setCurrentIndex(selectedIndex);
    }
    
    basis->setVisible(showBasis);
    basisLbl->setVisible(showBasis);
    
    geoSearch->blockSignals(false);
    timeZone->blockSignals(false);
    lockTimezone->blockSignals(false);
    dateTime->blockSignals(false);
    basis->blockSignals(false);
    comment->setPlainText(source->getComment());
}

void AstroFileEditor::currentTabChanged(int index)
{
    if (currentFile == index || index==-1) return;
    
    // Save current file's data before switching tabs to preserve user edits.
    // Use resume=false so that this quiet save doesn't trigger recalculations
    // (we're just preserving the UI state, not asking for a recompute).
    // Then explicitly resume the file so it doesn't stay permanently suspended
    // and block other handlers from receiving updates.
    int oldFile = currentFile;
    currentFile = oldFile;  // Temporarily keep currentFile pointing to old file
    applyToFile(false, false);  // Save without needsSave flag or triggering recalc
    file(oldFile)->resumeUpdate();  // Unblock — don't leave file(0) suspended
    
    // Now switch to the new file
    currentFile = index;
    update(file(index)->diff(file(oldFile)));
}

void AstroFileEditor::removeTab(int index)
{
    if (filesCount() < 2) return;
    //currentFile = 0;
    tabs->removeTab(index);
    file(index)->destroy();
}

void AstroFileEditor::swapFilesSlot(int i, int j)
{
    currentFile = i;
    emit swapFiles(i, j);
}

void AstroFileEditor::setCurrentFile(int index)
{
    tabs->setCurrentIndex(index);
}

void AstroFileEditor::filesUpdated(MembersList members)
{
    while (members.size() < filesCount()) members.append(AstroFile::Member());
    updateTabs();
    if (!filesCount())
        close();
    else if (currentFile >= filesCount())
        setCurrentFile(0);
    else if(members.count() > currentFile && members[currentFile])
        update(members[currentFile]);
}

void AstroFileEditor::applyToFile(bool setNeedsSaveFlag /*=true*/,
                                  bool resume /*=true*/)
{
    AstroFile* dst = file(currentFile);

    A::modalize<bool> inUpdate(_inApply,true);
    dst->suspendUpdate();

    // Build full name with location suffix if timezone is locked
    QString fullName = name->text();
    if (lockTimezone->isChecked() && !geoSearch->locationName().isEmpty()) {
        // Extract just the city name (first part before comma)
        QString cityName = geoSearch->locationName().split(',').first().trimmed();
        fullName = fullName + " in " + cityName;
    }
    
    // Preserve the original file's directory when setting the name
    // Fall back to default chart directory if original is read-only
    QString dir = dst->fileInfo().path();
    if (dir == ".") {
        dir = AstroFile::fixedChartDir();
    } else {
        // Check if directory is writable
        QFileInfo dirInfo(dir);
        if (!dirInfo.isWritable()) {
            qDebug() << "Directory" << dir << "is read-only, using default chart directory";
            dir = AstroFile::fixedChartDir();
        }
    }
    AFileInfo newFileInfo(dir, fullName);
    dst->setFileInfo(newFileInfo);
    
    dst->setType(FileType(type->currentData().toInt()));
    dst->setLocationName(geoSearch->locationName());
    dst->setLocation(geoSearch->location());
    dst->setTimezoneLocked(lockTimezone->isChecked());
    
    // Handle base chart selection
    FileType curType = dst->getType();
    bool isProgressed = (curType == TypeDerivedProg || curType == TypeDerivedSA || 
                        curType == TypeDerivedPD);
    bool isReturn = (curType == TypeReturn);
    bool needsBaseChart = (isProgressed || isReturn);
    
    if (needsBaseChart && basis->currentIndex() > 0) {
        // A base chart is selected (not "(None)")
        QString baseFilePath = basis->currentData().toString();
        if (!baseFilePath.isEmpty()) {
            // Load the base chart file to get its GMT
            QSettings baseFile(baseFilePath, QSettings::IniFormat);
            auto baseGmtStr = baseFile.value("GMT").toString();
            if (!baseGmtStr.endsWith('Z')) baseGmtStr += 'Z';
            auto baseGmt = QDateTime::fromString(baseGmtStr, Qt::ISODate);
            dst->setBaseChart(baseGmt);
        } else {
            dst->clearBaseChart();
        }
    } else {
        dst->clearBaseChart();
    }
    
    // Convert displayed local date/time to UTC using the new widget
    double tzOff   = timeZone->value();
    double geoLon  = geoSearch->location().x();
    A::CalendarType calType = dateTime->calendarType();
    A::TimeMode     tMode   = dateTime->timeMode();
    QDateTime localDt       = dateTime->localDateTime();
    QDateTime gmt = A::localToUTC(localDt, tzOff, geoLon,
                                   tMode, calType);
    dst->setGMT(gmt);
    dst->setCalendarType(calType);
    dst->setTimeMode(tMode);

    // Store the effective timezone offset so the round-trip
    // (GMT → displayed local time) reproduces what the user typed.
    if (tMode == A::Time_LMT || tMode == A::Time_LAT) {
        // Compute the effective offset = (local_entered - gmt) in hours.
        // This makes update() display the same local time the user typed.
        qint64 localEpoch = QDateTime(localDt.date(), localDt.time(),
                                      QTimeZone::utc()).toSecsSinceEpoch();
        qint64 utcEpoch   = gmt.toSecsSinceEpoch();
        double effectiveTz = (localEpoch - utcEpoch) / 3600.0;
        dst->setTimezone(effectiveTz);
        qDebug() << "Saving: Local time" << localDt
                 << "mode" << (tMode == A::Time_LMT ? "LMT" : "LAT")
                 << "effective TZ" << effectiveTz
                 << "-> GMT" << gmt;
    } else {
        dst->setTimezone(timeZone->value());
        qDebug() << "Saving: Local time" << localDt
                 << "TZ offset" << timeZone->value()
                 << "-> GMT" << gmt;
    }
    dst->setComment(comment->document()->toPlainText());

    if (resume) dst->resumeUpdate();
    if (!setNeedsSaveFlag) dst->clearUnsavedState();
    
    // Reset user-edited flag after saving
    if (setNeedsSaveFlag) {
        _userEditedTime = false;
    }
}

/*void AstroFileEditor::showEvent(QShowEvent* e)
 {
  AstroFileHandler::showEvent(e);
  if (!secondFile()) reset2ndFile();
 }*/

