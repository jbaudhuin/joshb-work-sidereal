#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QTimeZone>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTableView>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QSettings>

#include "geosearch.h"
#include "fileeditor.h"


/* =========================== ASTRO FILE EDITOR ==================================== */

/*static*/ QStringList AstroFileEditor::planets;
/*static*/ QStringList AstroFileEditor::signs;
/*static*/ QStringList AstroFileEditor::events;

AstroFileEditor::AstroFileEditor(QWidget *parent) :
    AstroFileHandler(parent),
    _inUpdate(false),
    _inApply(false),
    _inDateSelection(false),
    _userEditedTime(false)
{
    if (planets.empty()) {
        planets = QStringList({ "Sun", "Moon", "Mercury", "Venus", "Mars",
                        "Jupiter", "Saturn", "Uranus", "Neptune",
                        "Pluto", "Chiron", "Pallas", "Vesta", "Juno",
                        "Ceres" });
    }

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
    dateTime   = new QDateTimeEdit;
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

    for (unsigned i = TypeEvent, n = TypeCount;
         i < n; ++i)
    {
        type->addItem(tr(AstroFile::typeToString(i).toLatin1().constData()), i);
    }
    type->setCurrentIndex(-1);

    timeZone -> setRange(-12, 12);
    timeZone -> setDecimals(1);

    dateTime -> setCalendarPopup(true);
    QString fmt = dateTime->displayFormat();
    if (fmt.replace("h:mm ", "h:mm:ss ") != dateTime->displayFormat()) {
        // if we don't show seconds, they will be non-zero and
        // the only way to edit it is hand-editing the .dat file.
        dateTime->setDisplayFormat(fmt);
    }
    dateTime->setMinimumDate(QDate(100,1,1));
    qDebug() << "Minimum date:" << dateTime->minimumDate();

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
    // Temporarily hidden:
    // lay3->addWidget(new QLabel(tr("Type:")));
    // lay3->addWidget(type);

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

    //auto lay = new QHBoxLayout;
    startDate = new QDateEdit;
    startDate->setMinimumDate(QDate(100,1,1));
    startDateLbl = new QLabel("Start search:");
    lay1->insertRow(2, startDateLbl, startDate);

    endDate = new QDateEdit;
    endDate->setMinimumDate(QDate(100,1,1));

    //lay->addWidget();
    endDateCB = new QCheckBox(tr("End search:"));
    connect(endDateCB, SIGNAL(toggled(bool)),
            endDate, SLOT(setEnabled(bool)));
    endDateCB->setChecked(true);
    endDateCB->setChecked(false);
    lay1->insertRow(3, endDateCB, endDate);

    int crow;
    QFormLayout::ItemRole role;
    lay1->getWidgetPosition(comment, &crow, &role);
    auto lbll = lay1->itemAt(crow, QFormLayout::LabelRole);
    auto lblw = lbll->widget();
    lblw->setHidden(true);
    comment->setHidden(true);

    hits = new QListWidget();
    hits->setSelectionMode(QAbstractItemView::SingleSelection);
    lay1->addRow(tr("Found"), hits);
    lay1->getWidgetPosition(hits, &crow, &role);
    lbll = lay1->itemAt(crow, QFormLayout::LabelRole);
    auto fndlblw = lbll->widget();

    connect(name, SIGNAL(editingFinished()), this, SLOT(onEditingFinished()));
    connect(startDate,
            &QDateTimeEdit::dateTimeChanged,
            this,
            [this](const QDateTime& dt) {
                if (endDateCB->isChecked())
                    return;
                endDate->blockSignals(true);
                endDate->setDateTime(dt.addYears(1));
                endDate->blockSignals(false);
            });
    connect(startDate, SIGNAL(dateTimeChanged(const QDateTime&)),
            this, SLOT(onEditingFinished()));
    connect(endDate, SIGNAL(dateTimeChanged(const QDateTime&)),
            this, SLOT(onEditingFinished()));
    connect(endDateCB, SIGNAL(toggled(bool)),
            this, SLOT(onEditingFinished()));

    auto emitter = [this]
    { emit hasSelection(!hits->selectedItems().isEmpty()); };

    connect(hits, &QListWidget::itemSelectionChanged, emitter);

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
    connect(dateTime, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime&) {
        if (!_inUpdate && !_inApply) {
            _userEditedTime = true;
            qDebug() << "User manually edited time";
        }
    });

    // Only update timezone when location changes, not when user edits time
    connect(geoSearch, SIGNAL(locationChanged()),
            this, SLOT(updateTimezone()));

    QString plre = "[a-zA-Z]+(-[a-zA-Z])?";  // e.g., planet-r, planet-p
    QString plmpre = QString("(%1(/%1)?)").arg(plre);   // planet/planet
    QString signsre = "(" + signs.join("|") + ")";
    QString zposre = "(?<deg>\\d+\\s+)?(?<sign>"+ signsre +")"
                     "( ?((?<min>\\d+)'? ?((?<sec>\\d+)\"?)?))?";
                                    // sign or deg Sign mins' sec"
    _zposre = QRegularExpression(zposre,QRegularExpression::CaseInsensitiveOption);
    QString zposrea = zposre;
    QString plmpeqre = plmpre + "(=" + plmpre + ")*"
            + "(=(?<posa>" + zposrea.replace(">","a>")
            + "))?";
                                    // e.g., sun=moon=mars
    QString plmpzposre = "(?<body>" + plmpre + ") "
            + "("
            + "(?<ingress>ingress (?<pos>" + zposre + "))"
            + "|(?<ret>return)"
            + ")";       // e.g., sun ingress capricorn, sun return
    QString asprestr = "(?<aspect>" + plmpeqre + ")";
    QString stationre = "((?<station>(" + planets.join("|") + ")) station)";
    QString restr = "(" + stationre + "|"
            + "(H(?<harmonic>\\d+(\\.\\d+)?) )?"
              "(" + plmpzposre + "|" + asprestr + ")"
            + ")";

    qDebug() << restr;
    _re = QRegularExpression(restr,QRegularExpression::CaseInsensitiveOption);
    if (!_re.isValid()) {
        qDebug() << _re.errorString() << "at" << _re.patternErrorOffset();
    } else {
        connect(type,
                qOverload<int>(&QComboBox::currentIndexChanged),
                [this,lblw,fndlblw](int i)
        {
            bool isEventSearch = (i == TypeSearch);
            bool isProgressed = (i == TypeDerivedProg || i == TypeDerivedSA || 
                                i == TypeDerivedPD);
            
            startDateLbl->setVisible(isEventSearch);
            startDate->setVisible(isEventSearch);
            endDate->setVisible(isEventSearch);
            endDateCB->setVisible(isEventSearch);
            comment->setHidden(isEventSearch);
            lblw->setHidden(isEventSearch);
            fndlblw->setVisible(isEventSearch);
            hits->setVisible(isEventSearch);
            basis->setVisible(isProgressed);
            basisLbl->setVisible(isProgressed);
            
            if (isEventSearch) {
                auto rev = new QRegularExpressionValidator(_re, this);
                name->setValidator(rev);
            } else {
                auto rev = name->validator();
                if (rev) {
                    name->setValidator(nullptr);
                    //rev->deleteLater();
                }
            }
            
            // Trigger update to populate basis combo when type changes
            if (isProgressed && !_inUpdate) {
                update(AstroFile::Type);
            }
        });
    }
    type->setCurrentIndex(TypeOther);

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
    if (_inUpdate || _inApply || _inDateSelection
            /*|| type->currentIndex()==TypeEvents*/)
    {
        return;
    }

    // Check if timezone is locked (for relocation charts)
    if (lockTimezone->isChecked()) {
        qDebug() << "Skipping timezone update - timezone is locked";
        return;
    }

    // Now that time display is decoupled from timezone, we can safely
    // update timezone when location changes without affecting displayed time
    QVector3D vec(geoSearch->location());

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
            .arg(dateTime->dateTime().toSecsSinceEpoch());
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
    if (m & AstroFile::Type) {
        // Find the combo box index that has this enum value
        int idx = type->findData(source->getType());
        if (idx >= 0) {
            type->setCurrentIndex(idx);
        }
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
    
    // Set the datetime widget without timezone - just display the local time
    dateTime->blockSignals(true);
    dateTime->setDate(localDt.date());
    dateTime->setTime(localDt.time());
    dateTime->blockSignals(false);
    
    qDebug() << "Loading: GMT" << gmtDt << "TZ offset" << source->getTimezone()
             << "-> Local time" << localDt.date() << localDt.time();
    
    // Populate basis combo box with available natal charts
    basis->clear();
    basis->addItem(tr("(None)"), QVariant());
    
    FileType curType = source->getType();
    bool showBasis = (curType == TypeDerivedProg || curType == TypeDerivedSA || 
                     curType == TypeDerivedPD);
    
    if (showBasis) {
        // Get all chart files from the chart directory
        QString chartDir = AstroFile::fixedChartDir();
        QDir dir(chartDir);
        QStringList filters;
        filters << "*.dat";
        QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
        
        int selectedIndex = 0;
        for (int i = 0; i < files.size(); ++i) {
            QFileInfo fi = files.at(i);
            basis->addItem(fi.baseName(), fi.absoluteFilePath());
            
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
    //if (source->getType()==TypeEvents) {
    // startDate->setDate(source->getStartDate());
    auto r = source->getDateRange();
    if (startDate->date()==QDate() || r.first==r.second) {
        endDateCB->setChecked(false);
        startDate->setDate(source->getLocalTime().date());
    } else {
        const auto& range = source->getDateRange();
        endDateCB->setChecked(true);
        startDate->setDate(range.first);
        endDate->setDate(range.second);
    }
    //}
    comment->setPlainText(source->getComment());

    auto dtfmt = QLocale().dateTimeFormat(QLocale::LongFormat);
    hits->clear();
    for (const auto& dt: source->getEventList()) {
        auto dtwit = new QListWidgetItem(dt.toString(dtfmt));
        dtwit->setData(Qt::UserRole, dt);
        hits->addItem(dtwit);
    }
}

void AstroFileEditor::currentTabChanged(int index)
{
    if (currentFile == index || index==-1) return;
    int oldFile = currentFile;
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
    bool update = true;

    A::modalize<bool> inUpdate(_inApply,true);
    dst->suspendUpdate();

    if ((resume || setNeedsSaveFlag)
            && type->currentIndex()==TypeSearch)
    {
        auto lw = findChild<QListWidget*>();
        auto sel = lw->selectedItems();
        if (!sel.isEmpty()) {
            A::modalize<bool> inReset(_inDateSelection, true);
            auto item = sel.takeFirst();
            auto dt = item->data(Qt::UserRole).toDateTime();
            dateTime->blockSignals(true);
            dateTime->setDateTime(dt);
            dateTime->blockSignals(false);
            dst->setGMT(dt.toUTC());
            update = false;
            //auto off = dt.timeZone().offsetFromUtc(dt);
            //timeZone->setValue();
            setNeedsSaveFlag = false;
        }
    }

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
    dst->setTimezone(timeZone->value());
    dst->setTimezoneLocked(lockTimezone->isChecked());
    
    // Handle base chart selection
    FileType curType = dst->getType();
    bool isProgressed = (curType == TypeDerivedProg || curType == TypeDerivedSA || 
                        curType == TypeDerivedPD);
    
    if (isProgressed && basis->currentIndex() > 0) {
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
    
    if (update) {
        // Manually construct GMT from displayed local time and timezone value
        // Don't trust QDateTimeEdit's internal timezone handling
        QDate localDate = dateTime->date();
        QTime localTime = dateTime->time();
        QTimeZone tz(3600 * timeZone->value());
        QDateTime localDt(localDate, localTime, tz);
        QDateTime gmt = localDt.toUTC();
        dst->setGMT(gmt);
        
        qDebug() << "Saving: Local time" << localDate << localTime 
                 << "TZ offset" << timeZone->value()
                 << "-> GMT" << gmt;
    }
    dst->setComment(comment->document()->toPlainText());

    const auto dtfmt = QLocale().dateTimeFormat(QLocale::LongFormat);
    QList<QDateTime> dtl;
    for (int i = 0, n = hits->count(); i < n; ++i) {
        auto dt = hits->item(i)->data(Qt::UserRole).toDateTime();
        dtl << dt;
    }

    ADateRange range { startDate->date(), endDate->date() };

    dst->setDateRange(range);
    dst->setEventList(dtl);

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


void
AstroFileEditor::onEditingFinished()
{
    if (_inDateSelection || _inUpdate) return;
    if (!name->hasAcceptableInput()) return;

    auto rev = qobject_cast<const QRegularExpressionValidator*>(name->validator());
    if (!rev) return;

    auto re = rev->regularExpression();
    auto match = re.match(name->text());
    if (!match.hasMatch()) return;

    auto capUInt = [&](const QString& cap) -> unsigned {
        auto caps = match.captured(cap);
        if (caps.isEmpty()) return 0;
        return caps.toUInt();
    };
    auto has = [&](const QString& cap) -> bool {
        return !match.captured(cap).isEmpty();
    };

    A::PlanetProfile poses;

    // get any edits to date or whatever
    AstroFileEditor::applyToFile(false/*not needsSave*/,
                                 false/*not recalc*/);
    A::InputData inda = file(currentFile)->data();
    A::InputData indb;
    if (filesCount() > 1) indb = file(1-currentFile)->data();
    else if (has("ret")) return;

    double orb, horb, span;
    A::uintSSet harmonics { 1 };

    if (has("station")) {
        auto planet = match.captured("station");
        auto pid = A::getPlanetId(planet);
        auto pla = A::getPlanet(pid);
        poses.push_back(new A::TransitPosition(pid, inda));
        span = 10;
    } else {
        auto str = match.captured("harmonic");
        if (!str.isNull()) {
            harmonics = { str.toUInt() };
        }

        if (has("ingress")) {
            auto n = match.captured("body");
            auto pid = A::getPlanetId(n);
            auto pla = A::getPlanet(pid);
            auto sgn = match.captured("sign");
            unsigned deg = capUInt("deg");
            unsigned min = capUInt("min");
            unsigned sec = capUInt("sec");
            auto pos = A::getSignPos(inda.zodiac(), sgn, deg, min, sec);
            poses.push_front(new A::Loc(match.captured("ingress"), pos));
            poses.push_back(new A::TransitPosition(pid, inda));
        } else if (has("ret")) {
            auto n = match.captured("body");
            auto pid = A::getPlanetId(n);
            auto pla = A::getPlanet(pid);
            poses.push_back(new A::NatalLoc(pid, indb));
            poses.push_back(new A::TransitPosition(pid, inda));
        } else if (has("aspect")) {
            auto asp = match.captured("aspect");

            typedef std::function<A::Loc*(const A::ChartPlanetId& cpid)> fun;
            QMap<QChar, fun> funs;

            funs.insert('\0', [&](const A::ChartPlanetId& cpid)
            { return new A::TransitPosition(cpid, inda); });

            funs.insert('t',[&](const A::ChartPlanetId& cpid)
            { return new A::TransitPosition(cpid, inda); });

            funs.insert('r', [&](const A::ChartPlanetId& cpid)
            { return new A::NatalLoc(cpid, indb); });

            if (currentFile>0) {
                // preview planets to determine a default pick.
                QMap<QChar, int> pop;
                for (const auto& pln : asp.split("=")) {
                    for (const auto& ppln : pln.split('/')) {
                        auto ps = ppln.split('-');
                        if (ps.size()==1) ++pop['\0'];
                        else if (ps[1].isEmpty()) return;
                        else {
                            QChar t = ps[1].at(0).toLower();
                            if (QString("tpr").indexOf(t)==-1) {
                                // warn
                                return;
                            }
                            ++pop[t];
                        }
                    }
                }
                if (pop.contains('\0') && pop.size()>2) {
                    // ambiguous: TODO warn
                    return;
                }
                if (pop.contains('\0') && pop.contains('r'))
                    funs['\0'] = funs['t'];
                else if (pop.contains('\0') && pop.contains('t'))
                    funs['\0'] = funs['r'];
            }

            for (auto ps : asp.split("=")) {
                auto psp = ps.split('-');
                QChar t = '\0';
                if (psp.size()>1 && !psp.at(1).isEmpty()) t = psp[1][0];
                ps = psp.first();
                if (ps.indexOf('/') == -1) {
                    auto pid = A::getPlanetId(ps);
                    if (pid != A::Planet_None) {
                        auto pla = A::getPlanet(pid);
                        poses.push_back(funs[t](pid));
                        continue;
                    }
                    if (!has("posa")) continue;
                    auto sgn = match.captured("signa");
                    unsigned deg = capUInt("dega");
                    unsigned min = capUInt("mina");
                    unsigned sec = capUInt("seca");
                    auto pos = A::getSignPos(inda.zodiac(), sgn, deg, min, sec);
                    poses.push_front(new A::Loc(match.captured("posa"), pos));
                    continue;
                }
                auto mpls = ps.split('/');
                auto pid1 = A::getPlanetId(mpls.takeFirst());
                auto pid2 = A::getPlanetId(mpls.takeFirst());
                auto cpid = A::ChartPlanetId(pid1,pid2);
                poses.push_back(funs[t](cpid));
            }
            if (poses.size() < 2) return;
        }
        if (poses.empty()) return;
        A::calculateOrbAndSpan(poses, inda, *harmonics.rbegin(), orb, orb, span);
    }

    //auto d0 = A::getJulianDate(startDate->dateTime().toUTC());
    //auto d1 = A::getJulianDate(endDate->dateTime().toUTC());
    inda.setGMT(startDate->dateTime().toUTC());
    auto dl = A::quotidianSearch(poses, inda,
                                 endDate->dateTime().toUTC(),
                                 std::min(span/(*harmonics.rbegin()),30.),
                                 true);

    auto lw = findChild<QListWidget*>();
    lw->clear();
    QTimeZone tz(timeZone->value()*3600);

    auto dtfmt = QLocale().dateTimeFormat(QLocale::LongFormat);
    for (auto dt : dl) {
        dt = dt.addSecs(timeZone->value()*3600);
        dt.setTimeZone(tz); // @todo compute using googleapi
        auto lwit = new QListWidgetItem(dt.toString(dtfmt));
        lwit->setData(Qt::UserRole, dt);
        hits->addItem(lwit);
    }
}
