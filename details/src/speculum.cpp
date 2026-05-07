#include "speculum.h"
#include "../../zodiac/src/thememanager.h"
#include "../../astroprocessor/src/astro-data.h"
#include "../../zodiac/src/mainwindow.h"
#include <Astroprocessor/Calc>
#include <Astroprocessor/Output>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

// Custom delegate implementation
void SpeculumDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    // Check if this cell has highlighting
    int highlightType = index.data(Speculum::CellHighlightRole).toInt();
    
    if (highlightType == Speculum::ClickedHighlight || highlightType == Speculum::MatchedHighlight) {
        painter->save();
        
        QColor bgColor;
        QColor textColor;
        
        if (highlightType == Speculum::ClickedHighlight) {
            // Gold highlight for clicked cell
            bgColor = ThemeManager::instance().getGoldColor(false);
            bgColor.setAlpha(200);
            textColor = QColor(0, 0, 0);
        } else {
            // Theme-aware blue highlight for matched cells
            bgColor = ThemeManager::instance().getTableHighlightColor();
            textColor = ThemeManager::instance().getTableHighlightTextColor();
        }
        
        // Paint the background
        painter->fillRect(option.rect, bgColor);
        
        // Set up text options
        QStyleOptionViewItem opt = option;
        opt.palette.setColor(QPalette::Text, textColor);
        opt.palette.setColor(QPalette::HighlightedText, textColor);
        
        // Paint the text/content on top
        QStyledItemDelegate::paint(painter, opt, index);
        
        painter->restore();
    } else {
        // Normal cell - use default painting
        QStyledItemDelegate::paint(painter, option, index);
    }
}

// Custom role for cell highlighting
const int CellHighlightRole = Qt::UserRole + 2;

Speculum::Speculum(QWidget* parent) :
    AstroFileHandler(parent),
    _selectedPlanet(A::Planet_None),
    _fileIndex(0),
    _selectedChartIndex(0),
    _filterActive(false),
    _filterOrbMinutes(4.0), // Default 4 minutes
    _showFixedStars(true),
    _showParanNatalRows(false),
    m_timezone(0),
    _clickedRow(-1),
    _clickedCol(-1),
    _displayMode(A::DisplayLocalTime) // Default to Local Time
{
    // Create main table
    _table = new QTableWidget();
    _table->setSelectionBehavior(
        QAbstractItemView::SelectItems); // Select individual cells, not rows
    _table->setSelectionMode(
        QAbstractItemView::SingleSelection); // Only one cell at a time
    _table->setAlternatingRowColors(true);
    _table->verticalHeader()->setVisible(false); // Hide row numbers
    _table->setShowGrid(false);                  // No grid lines needed
    
    // Install custom delegate for painting highlighted cells
    _table->setItemDelegate(new SpeculumDelegate(this));

    // Theme will be applied through the global stylesheet - no local CSS needed

    // Create filter controls
    _filterLabel = new QLabel(tr("Filter inactive"));
    _orbSpinBox  = new QDoubleSpinBox();
    _orbSpinBox->setRange(1.0, 60.0); // 1 to 60 minutes (or 0.25 to 15 degrees)
    _orbSpinBox->setValue(4.0);
    _orbSpinBox->setSuffix(tr(" min"));
    _orbSpinBox->setDecimals(1);
    _orbSpinBox->setSingleStep(1.0); // Increment by 1.0 per click
    _orbSpinBox->setKeyboardTracking(
        false); // Only emit valueChanged when editing is finished
    _orbSpinBox->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    _orbSpinBox->setStepType(QAbstractSpinBox::DefaultStepType);
    _orbSpinBox->setToolTip(tr("Time orb for filtering"));

    _clearFilterBtn = new QPushButton(tr("Clear Filter"));
    _clearFilterBtn->setEnabled(false);

    // Create radix button
    _radixBtn = new QPushButton();
    _radixBtn->setCheckable(true);
    _radixBtn->setChecked(false);
    _radixBtn->setToolTip(tr("Filter by birth time"));
    _radixBtn->setProperty("radixButton", true); // For CSS styling

    // Create chart selector buttons
    _chart1Btn = new QPushButton("1");
    _chart1Btn->setCheckable(true);
    _chart1Btn->setChecked(true); // Default to chart 1
    _chart1Btn->setToolTip(tr("Show Chart #1"));
    _chart1Btn->setMaximumWidth(30);
    _chart1Btn->setProperty("chartButton", true);

    _chart2Btn = new QPushButton("2");
    _chart2Btn->setCheckable(true);
    _chart2Btn->setChecked(false);
    _chart2Btn->setToolTip(tr("Show Chart #2"));
    _chart2Btn->setMaximumWidth(30);
    _chart2Btn->setProperty("chartButton", true);
    _chart2Btn->setVisible(false); // Initially hidden until 2nd chart loaded

    // Create labels
    QLabel* radixLabel = new QLabel(tr("Radix:"));
    QLabel* orbLabel   = new QLabel(tr("Orb:"));

    // Layout for filter controls
    QHBoxLayout* filterLayout = new QHBoxLayout();
    // Add chart selector buttons at the beginning
    filterLayout->addWidget(_chart1Btn);
    filterLayout->setAlignment(_chart1Btn, Qt::AlignBaseline);
    filterLayout->addWidget(_chart2Btn);
    filterLayout->setAlignment(_chart2Btn, Qt::AlignBaseline);
    filterLayout->addSpacing(10);// Add some space after chart buttons
    filterLayout->addWidget(radixLabel);
    filterLayout->setAlignment(radixLabel, Qt::AlignBaseline);
    filterLayout->addWidget(_radixBtn);
    filterLayout->setAlignment(_radixBtn, Qt::AlignBaseline);
    filterLayout->addStretch();
    filterLayout->addWidget(orbLabel);
    filterLayout->setAlignment(orbLabel, Qt::AlignBaseline);
    filterLayout->addWidget(_orbSpinBox);
    filterLayout->setAlignment(_orbSpinBox, Qt::AlignBaseline);
    filterLayout->addWidget(_clearFilterBtn);
    filterLayout->setAlignment(_clearFilterBtn, Qt::AlignBaseline);
    filterLayout->addStretch();
    filterLayout->addWidget(_filterLabel);
    filterLayout->setAlignment(_filterLabel, Qt::AlignBaseline);

    // Main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 5, 0, 0);
    mainLayout->setSpacing(5);
    mainLayout->addLayout(filterLayout);
    mainLayout->addWidget(_table);

    // Connect signals
    connect(_table,
            SIGNAL(cellClicked(int, int)),
            this,
            SLOT(onCellClicked(int, int)));

    // Use a single-shot timer to debounce spinbox changes
    // This prevents double-firing on some platforms
    QTimer* orbChangeTimer = new QTimer(this);
    orbChangeTimer->setSingleShot(true);
    orbChangeTimer->setInterval(100); // 100ms debounce
    connect(_orbSpinBox,
            SIGNAL(valueChanged(double)),
            orbChangeTimer,
            SLOT(start()));
    connect(orbChangeTimer,
            SIGNAL(timeout()),
            this,
            SLOT(onFilterOrbChanged()));

    connect(_clearFilterBtn, SIGNAL(clicked()), this, SLOT(onClearFilter()));
    connect(_radixBtn,
            SIGNAL(clicked(bool)),
            this,
            SLOT(onRadixButtonClicked(bool)));
    connect(_chart1Btn, &QPushButton::clicked, this, [this]() {
        onChartButtonClicked(0);
    });
    connect(_chart2Btn, &QPushButton::clicked, this, [this]() {
        onChartButtonClicked(1);
    });

    // Connect to theme changes to refresh colors dynamically
    connect(&ThemeManager::instance(),
            &ThemeManager::themeChanged,
            this,
            &Speculum::onThemeChanged);

    // Setup table headers
    setupTableHeaders();
}

void
Speculum::setupTableHeaders()
{
    QStringList headers;
    headers << tr("Planet") << tr("Rise") << tr("MC") << tr("Set") << tr("IC");
    _table->setColumnCount(headers.size());
    _table->setHorizontalHeaderLabels(headers);

    // Use resize modes instead of fixed widths to prevent ellipses
    QHeaderView* header = _table->horizontalHeader();
    header->setSectionResizeMode(
        0,
        QHeaderView::ResizeToContents); // Planet name auto-sizes
    header->setSectionResizeMode(1,
                                 QHeaderView::Stretch); // Rise gets equal space
    header->setSectionResizeMode(2,
                                 QHeaderView::Stretch); // MC gets equal space
    header->setSectionResizeMode(3,
                                 QHeaderView::Stretch); // Set gets equal space
    header->setSectionResizeMode(4,
                                 QHeaderView::Stretch); // IC gets equal space

    // Set minimum widths to ensure readability
    _table->setColumnWidth(0, 80);     // Minimum for planet names
    header->setMinimumSectionSize(60); // Minimum for time columns
}

void
Speculum::filesUpdated(MembersList m)
{
    if (!file()) {
        clear();
        _chart2Btn->setVisible(false);
        return;
    }

    while (m.size() < filesCount()) m.append(AstroFile::Member());

    // Skip only if every file reports zero changes
    bool anyChanged = false;
    for (const auto& member : m)
        if (member) { anyChanged = true; break; }
    if (!anyChanged) return;

    // Show/hide chart 2 button based on file count
    _chart2Btn->setVisible(filesCount() > 1);

    // If chart 2 button is hidden and was selected, switch to chart 1
    if (!_chart2Btn->isVisible() && _selectedChartIndex == 1) {
        _selectedChartIndex = 0;
        _chart1Btn->setChecked(true);
        _chart2Btn->setChecked(false);
    }

    // Auto-switch to chart 2 when it just became a paran chart, and back
    // to chart 1 when the paran chart is replaced with a non-paran type.
    if (filesCount() > 1 && m.size() > 1 && (m[1] & AstroFile::Type)) {
        if (file(1)->getType() == TypeParan && _selectedChartIndex == 0) {
            _selectedChartIndex = 1;
            _chart1Btn->setChecked(false);
            _chart2Btn->setChecked(true);
        } else if (file(1)->getType() != TypeParan && _selectedChartIndex == 1) {
            _selectedChartIndex = 0;
            _chart1Btn->setChecked(true);
            _chart2Btn->setChecked(false);
        }
    }

    updateSpeculumDisplay();
}

void
Speculum::updateSpeculumDisplay()
{
    if (!file()) return;

    // Get the selected chart (file index)
    AstroFile* selectedFile =
        (_selectedChartIndex == 1 && filesCount() > 1) ? file(1) : file(0);
    if (!selectedFile) return;

    auto scope = selectedFile->horoscope();
    m_timezone = scope.inputData.tz();

    // Store radix (birth) time and update button display
    _radixTime               = scope.inputData.GMT();
    QDateTime localRadixTime = _radixTime.addSecs(m_timezone * 3600);
    QString   dow(tr("MtWTFsS"));
    int dayOfWeek = localRadixTime.date().dayOfWeek();
    if (dayOfWeek < 1 || dayOfWeek > 7) {
        qWarning() << "Invalid day of week:" << dayOfWeek << "for date:" << localRadixTime;
        dayOfWeek = 1; // Default to Monday
    }
    
    // Format radix button text based on display mode
    QString radixTimeStr;
    if (_displayMode == A::DisplaySiderealTime) {
        // Show sidereal time (RAMC)
        double radixRA = scope.houses.RAMC;
        radixTimeStr = A::siderealTimeToString(radixRA, A::HighPrecision);
    } else if (_displayMode == A::DisplayRightAscension) {
        // Show Right Ascension
        double radixRA = scope.houses.RAMC;
        radixTimeStr = A::raToString(radixRA, A::HighPrecision);
    } else {
        // Show local time (default)
        radixTimeStr = QString("%1 %2")
                           .arg(dow[dayOfWeek - 1])
                           .arg(localRadixTime.time().toString("hh:mm"));
    }
    _radixBtn->setText(radixTimeStr);

    populateSpeculumTable();

    if (_filterActive) {
        highlightFilteredRows();
    }
}

void
Speculum::populateSpeculumTable()
{
    if (!file()) return;

    auto scope = file(_selectedChartIndex)->horoscope();

    // Determine if the currently displayed chart is a paran chart.
    // In a biwheel the paran file is file(1) and the natal is file(0);
    // in transitsOnly mode both roles collapse onto file(0).
    AstroFile* paranFile    = file(_selectedChartIndex);
    bool       isParanChart = paranFile && paranFile->getType() == TypeParan;

    // Build the set of transit-body PlanetIds for paran filtering.
    // fileId=1 entries are transit planets (from the transit chart, file index 1).
    QSet<A::PlanetId> paranTransitPlanets;
    if (isParanChart) {
        for (const auto& entry : paranFile->getParanGroupPlanets()) {
            if (entry.first == 1)
                paranTransitPlanets.insert(entry.second);
        }
    }

    // The "natal" file for ex-precessed rows is the other chart in a biwheel
    int        natalIdx  = 1 - _selectedChartIndex;
    AstroFile* natalFile = (filesCount() > 1 && natalIdx >= 0) ? file(natalIdx) : nullptr;

    // Count natal ex-precessed rows for Par=N charts (feature 3)
    int  natalParanCount = 0;
    bool showNatalRows   = isParanChart
        && _showParanNatalRows
        && paranFile->getOriginEventType() == A::etcParanatellontaToNatal
        && natalFile != nullptr;
    if (showNatalRows) {
        for (const auto& entry : paranFile->getParanGroupPlanets()) {
            if (entry.first == 0) natalParanCount++;  // fileId=0 = natal planets
        }
    }

    // Count transit planet rows (filtered for paran charts)
    int planetCount = 0;
    for (const A::Planet& p : scope.planets) {
        if (p.id == A::Planet_MC || p.id == A::Planet_Asc) continue;
        if (isParanChart && !paranTransitPlanets.contains(p.id)) continue;
        planetCount++;
    }

    // Add fixed stars if enabled (suppressed for paran charts — not relevant)
    int totalRows = planetCount + natalParanCount;
    if (_showFixedStars && !isParanChart)
        totalRows += scope.stars.count();

    _table->setRowCount(totalRows);

    // Populate transit planet rows
    int row = 0;
    for (const A::Planet& p : scope.planets) {
        if (p.id == A::Planet_MC || p.id == A::Planet_Asc) continue;
        if (isParanChart && !paranTransitPlanets.contains(p.id)) continue;
        addPlanetRow(p, row++);
    }

    // Populate natal ex-precessed rows for Par=N (right-justified italic)
    if (showNatalRows) {
        double jdNatal = A::getJulianDate(natalFile->getGMT());
        double jdParan = A::getJulianDate(paranFile->getGMT());
        // Search the same midnight-to-midnight window that findParans() uses,
        // so the crossing times are consistent with the event that was found.
        QDateTime paranDayUTC = paranFile->getGMT().toUTC();
        paranDayUTC.setTime(QTime(0, 0, 0));
        double jdMidnight = A::getJulianDate(paranDayUTC);
        double lat     = scope.inputData.location().y();
        double lon     = scope.inputData.location().x();

        for (const auto& entry : paranFile->getParanGroupPlanets()) {
            if (entry.first != 0) continue;  // fileId=0 = natal planets from natalFile
            A::PlanetId pid = entry.second;

            QString planetName;
            for (const A::Planet& p : natalFile->horoscope().planets) {
                if (p.id == pid) { planetName = p.name; break; }
            }
            if (planetName.isEmpty()) continue;

            // Get tropical (non-sidereal) natal RA/Dec — same convention as
            // NatalExprecessedPosition._natalRA so exprecess_equatorial works correctly.
            double tropRA, tropDec;
            if (!A::natalTropicalEquatorialPos(pid, jdNatal, tropRA, tropDec)) continue;

            QDateTime angleTransit[4];
            double    angleTransitRA[4];
            A::computeNatalParanTransits(
                tropRA, tropDec,
                jdNatal, jdMidnight, lat, lon,
                angleTransit, angleTransitRA);

            addNatalParanRow(planetName, angleTransit, angleTransitRA, row++);
        }
    }

    // Fixed star rows (suppressed for paran charts)
    if (_showFixedStars && !isParanChart) {
        for (const A::Star& s : scope.stars) {
            addStarRow(s, row++);
        }
    }
}

void
Speculum::addPlanetRow(const A::Planet& planet, int row)
{
    // Planet name with bold font
    QTableWidgetItem* nameItem = new QTableWidgetItem(planet.name);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole, static_cast<int>(planet.id));
    QFont nameFont;
    nameFont.setBold(true);
    nameItem->setFont(nameFont);
    _table->setItem(row, 0, nameItem);

    // Get PSSR/NeoSQ context if this is a solar-based chart (solar return or solar ingress)
    const A::PSSRContext* pssrCtx = nullptr;
    bool isSolarReturn = false;
    bool isSolarIngress = false;
    AstroFile* currentFile = file(_selectedChartIndex);
    if (currentFile) {
        // Check if this is a solar-based chart (solar return or solar ingress)
        if (A::isSolarBasedChart(currentFile->getName())) {
            // Rebuild context if missing or if Sun mode changed
            if (!currentFile->hasPSSRContext()
                || currentFile->pssrContext().useApparentSun != A::useApparentSun) {
                auto ctx = A::calculatePSSRContext(currentFile->horoscope(), A::useApparentSun);
                currentFile->setPSSRContext(ctx);
            }
            if (currentFile->hasPSSRContext()) {
                pssrCtx = &currentFile->pssrContext();
                isSolarReturn = A::isSolarReturn(currentFile->getName());
                isSolarIngress = A::isSolarIngress(currentFile->getName());
            }
        }
    }

    // Add time columns in order: Rise, MC, Set, IC (indices 0, 2, 1, 3)
    QList<int> timeIndices = { 0, 2, 1, 3 };
    for (int col = 1; col <= 4; col++) {
        int       timeIndex   = timeIndices[col - 1];
        QDateTime transitTime = planet.angleTransit.at(timeIndex);
        QString   timeStr;

        // Check if transit time is valid (some stars never rise/set)
        if (!transitTime.isValid()) {
            timeStr = "    --    ";
        } else if (_displayMode == A::DisplayLocalTime) {
            timeStr = A::_formatTime(transitTime, m_timezone);
        } else {
            // Use RA values for Sidereal Time or Right Ascension modes
            double raValue = planet.angleTransitRA[timeIndex];
            if (_displayMode == A::DisplaySiderealTime) {
                timeStr = A::siderealTimeToString(raValue, A::HighPrecision);
            } else { // DisplayRightAscension
                timeStr = A::raToString(raValue, A::HighPrecision);
            }
        }

        QTableWidgetItem* timeItem = new QTableWidgetItem(timeStr);
        timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
        timeItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        // Set monospace font for data columns
        QFont monoFont;
        monoFont.setStyleHint(QFont::Monospace);
        timeItem->setFont(monoFont);

        // Store the actual QDateTime for filtering
        timeItem->setData(Qt::UserRole, transitTime);

        // Calculate and set angular date tooltip (PD or PSSR)
        if (transitTime.isValid() && _radixTime.isValid()) {
            // Get planet's RA and the angle's RA
            double planetRA = planet.equatorialPos.x();
            double angleRA = planet.angleTransitRA[timeIndex];
            
            QString label = planet.name + " @ " + A::angleTransitName(timeIndex);
            QDateTime angularDateGMT = A::calculateAngularDate(_radixTime, transitTime, 
                                                                 planetRA, angleRA, pssrCtx, label);
            // Convert to local time
            int offsetSeconds = m_timezone * 3600;
            QTimeZone timeZone = QTimeZone::fromSecondsAheadOfUtc(offsetSeconds);
            QDateTime angularDate = angularDateGMT.toTimeZone(timeZone);
            
            QString direction = (transitTime < _radixTime) ? "Con" : "Dir";
            QString method    = "PD";
            QString dateFormat = "yyyy/MM/dd";
            QString tooltip;
            if (pssrCtx) {
                method     = isSolarReturn ? "PSSR" : "NeoSQ";
                dateFormat = "ddd yyyy-MM-dd hh:mm";

                    // Intermediate steps (Bowser/Fagan linear formula):
                    //   RAMC arc    = angleRA − returnRAMC  (sidereal arc, degrees)
                    //   elapsedRAMS = RAMC arc / anniversarySecond
                    //   elapsedDays = elapsedRAMS / 360° × 365.25
                    double ramcDiff = angleRA - pssrCtx->returnRAMC;
                    if (ramcDiff > 180.0) ramcDiff -= 360.0;
                    else if (ramcDiff <= -180.0) ramcDiff += 360.0;
                    double elapsedRAMS = ramcDiff / pssrCtx->anniversarySecond;
                    double elapsedDays = (elapsedRAMS / 360.0) * 365.25;

                    // Format a signed decimal degree as ±D°MM'SS"
                    auto dms = [](double d) -> QString {
                        QString sign = (d < 0) ? "-" : "";
                        double  a    = qAbs(d);
                        int     deg  = static_cast<int>(a);
                        double  rem  = (a - deg) * 60.0;
                        int     arcm = static_cast<int>(rem);
                        int     arcs = static_cast<int>((rem - arcm) * 60.0);
                        return QString("%1%2° %3' %4\"")
                            .arg(sign)
                            .arg(deg)
                            .arg(arcm, 2, 10, QChar('0'))
                            .arg(arcs, 2, 10, QChar('0'));
                    };
                    auto st = [](double d) -> QString {
                        QString sign  = (d < 0) ? "-" : "";
                        double  a     = qAbs(d) / 15.0;
                        int     h     = static_cast<int>(a);
                        double  rem   = (a - h) * 60.0;
                        int     m     = static_cast<int>(rem);
                        int     s     = static_cast<int>((rem - m) * 60.0);
                        return QString("%1%2h %3m %4s")
                            .arg(sign)
                            .arg(h)
                            .arg(m, 2, 10, QChar('0'))
                            .arg(s, 2, 10, QChar('0'));
                    };

                    // Format absolute elapsed days as Nd HHh MMm
                    double absDays = qAbs(elapsedDays);
                    int    dWhole  = static_cast<int>(absDays);
                    double fracDay = (absDays - dWhole) * 24.0;
                    int    hours   = static_cast<int>(fracDay);
                    int    mins    = static_cast<int>((fracDay - hours) * 60.0);
                    QString daysStr = QString("%1d %2h %3m")
                                          .arg(dWhole)
                                          .arg(hours, 2, 10, QChar('0'))
                                          .arg(mins, 2, 10, QChar('0'));

                    tooltip = QString(
                        "%1 %2: %3\n"
                        "─────────────────────────\n"
                        "Ann. Sec. mode: %4\n"
                        "Next RAMC    : %5\n"
                        "Return RAMC  : %6\n"
                        "Anniv. Second: %7\n"
                        "RAMC arc     : %8\n"
                        "Elapsed RA   : %9\n"
                        "Elapsed days : %10\n"
                        "─────────────────────────\n"
                        "Return time  : %11")
                        .arg(method, direction, angularDate.toString(dateFormat))
                        .arg(pssrCtx->useApparentSun ? "RAAS (apparent Sun)" : "RAMS (mean Sun)")
                        .arg(A::siderealTimeToString(pssrCtx->nextReturnRAMC, A::HighPrecision))
                        .arg(A::siderealTimeToString(pssrCtx->returnRAMC, A::HighPrecision))
                        .arg(QString::number(pssrCtx->anniversarySecond, 'f', 6))
                        .arg(st(ramcDiff))
                        .arg(st(elapsedRAMS))
                        .arg(daysStr)
                        .arg(pssrCtx->returnTime.toString("yyyy-MM-dd hh:mm"));
            } else {
                tooltip = QString("%1: %2 %3")
                              .arg(method)
                              .arg(angularDate.toString(dateFormat))
                              .arg(direction);
            }
            timeItem->setToolTip(tooltip);
        }

        _table->setItem(row, col, timeItem);
    }
}

void
Speculum::addNatalParanRow(const QString& name,
                           QDateTime      angleTransit[4],
                           double         angleTransitRA[4],
                           int            row)
{
    // Right-justified italic label to visually distinguish natal ex-precessed rows
    QTableWidgetItem* nameItem = new QTableWidgetItem(name);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    QFont nameFont;
    nameFont.setBold(true);
    nameFont.setItalic(true);
    nameItem->setFont(nameFont);
    nameItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _table->setItem(row, 0, nameItem);

    // Column order: Rise(0), MC(2), Set(1), IC(3) — matches addPlanetRow
    static const int timeIndices[4] = { 0, 2, 1, 3 };
    for (int col = 1; col <= 4; col++) {
        int       timeIndex   = timeIndices[col - 1];
        QDateTime transitTime = angleTransit[timeIndex];
        QString   timeStr;

        if (!transitTime.isValid()) {
            timeStr = "    --    ";
        } else if (_displayMode == A::DisplayLocalTime) {
            timeStr = A::_formatTime(transitTime, m_timezone);
        } else {
            double raValue = angleTransitRA[timeIndex];
            if (_displayMode == A::DisplaySiderealTime) {
                timeStr = A::siderealTimeToString(raValue, A::HighPrecision);
            } else {
                timeStr = A::raToString(raValue, A::HighPrecision);
            }
        }

        QTableWidgetItem* timeItem = new QTableWidgetItem(timeStr);
        timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
        timeItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        QFont monoFont;
        monoFont.setStyleHint(QFont::Monospace);
        monoFont.setItalic(true);
        timeItem->setFont(monoFont);
        timeItem->setData(Qt::UserRole, transitTime);
        _table->setItem(row, col, timeItem);
    }
}

void
Speculum::addStarRow(const A::Star& star, int row)
{
    // Star name
    QTableWidgetItem* nameItem = new QTableWidgetItem(star.name);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole, -1); // Use -1 for stars
    _table->setItem(row, 0, nameItem);

    // Get PSSR/NeoSQ context if this is a solar-based chart (solar return or solar ingress)
    const A::PSSRContext* pssrCtx = nullptr;
    bool isSolarReturn = false;
    bool isSolarIngress = false;
    AstroFile* currentFile = file(_selectedChartIndex);
    if (currentFile) {
        // Check if this is a solar-based chart (solar return or solar ingress)
        if (A::isSolarBasedChart(currentFile->getName())) {
            // Rebuild context if missing or if Sun mode changed
            if (!currentFile->hasPSSRContext()
                || currentFile->pssrContext().useApparentSun != A::useApparentSun) {
                auto ctx = A::calculatePSSRContext(currentFile->horoscope(), A::useApparentSun);
                currentFile->setPSSRContext(ctx);
            }
            if (currentFile->hasPSSRContext()) {
                pssrCtx = &currentFile->pssrContext();
                isSolarReturn = A::isSolarReturn(currentFile->getName());
                isSolarIngress = A::isSolarIngress(currentFile->getName());
            }
        }
    }

    // Add time columns for star
    QList<int> timeIndices = { 0, 2, 1, 3 };
    for (int col = 1; col <= 4; col++) {
        int       timeIndex   = timeIndices[col - 1];
        QDateTime transitTime = star.angleTransit.at(timeIndex);
        QString   timeStr;

        // Check if transit time is valid (some stars never rise/set)
        if (!transitTime.isValid()) {
            timeStr = "    --    ";
        } else if (_displayMode == A::DisplayLocalTime) {
            timeStr = A::_formatTime(transitTime, m_timezone);
        } else {
            // Use RA values for Sidereal Time or Right Ascension modes
            double raValue = star.angleTransitRA[timeIndex];
            if (_displayMode == A::DisplaySiderealTime) {
                timeStr = A::siderealTimeToString(raValue, A::HighPrecision);
            } else { // DisplayRightAscension
                timeStr = A::raToString(raValue, A::HighPrecision);
            }
        }

        QTableWidgetItem* timeItem = new QTableWidgetItem(timeStr);
        timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
        timeItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        // Set monospace font for data columns
        QFont monoFont;
        monoFont.setStyleHint(QFont::Monospace);
        timeItem->setFont(monoFont);

        // Store the actual QDateTime for filtering
        timeItem->setData(Qt::UserRole, transitTime);

        // Calculate and set angular date tooltip (PD or PSSR)
        if (transitTime.isValid() && _radixTime.isValid()) {
            // Get star's RA and the angle's RA
            double starRA = star.equatorialPos.x();
            double angleRA = star.angleTransitRA[timeIndex];
            
            QString label = star.name + " @ " + A::angleTransitName(timeIndex);
            QDateTime angularDateGMT = A::calculateAngularDate(_radixTime, transitTime,
                                                                 starRA, angleRA, pssrCtx, label);
            // Convert to local time
            int offsetSeconds = m_timezone * 3600;
            QTimeZone timeZone = QTimeZone::fromSecondsAheadOfUtc(offsetSeconds);
            QDateTime angularDate = angularDateGMT.toTimeZone(timeZone);
            
            QString direction = (transitTime < _radixTime) ? "Con" : "Dir";
            QString method    = "PD";
            QString dateFormat = "yyyy/MM/dd";
            QString tooltip;
            if (pssrCtx) {
                method     = isSolarReturn ? "PSSR" : "NeoSQ";
                dateFormat = "ddd yyyy-MM-dd hh:mm";

                    double ramcDiff = angleRA - pssrCtx->returnRAMC;
                    if (ramcDiff > 180.0) ramcDiff -= 360.0;
                    else if (ramcDiff <= -180.0) ramcDiff += 360.0;
                    double elapsedRAMS = ramcDiff / pssrCtx->anniversarySecond;
                    double elapsedDays = (elapsedRAMS / 360.0) * 365.25;

                    auto dms = [](double d) -> QString {
                        QString sign = (d < 0) ? "-" : "";
                        double  a    = qAbs(d);
                        int     deg  = static_cast<int>(a);
                        double  rem  = (a - deg) * 60.0;
                        int     arcm = static_cast<int>(rem);
                        int     arcs = static_cast<int>((rem - arcm) * 60.0);
                        return QString("%1%2° %3' %4\"")
                            .arg(sign)
                            .arg(deg)
                            .arg(arcm, 2, 10, QChar('0'))
                            .arg(arcs, 2, 10, QChar('0'));
                    };
                    auto st = [](double d) -> QString {
                        QString sign  = (d < 0) ? "-" : "";
                        double  a     = qAbs(d) / 15.0;
                        int     h     = static_cast<int>(a);
                        double  rem   = (a - h) * 60.0;
                        int     m     = static_cast<int>(rem);
                        int     s     = static_cast<int>((rem - m) * 60.0);
                        return QString("%1%2h %3m %4s")
                            .arg(sign)
                            .arg(h)
                            .arg(m, 2, 10, QChar('0'))
                            .arg(s, 2, 10, QChar('0'));
                    };

                    double absDays = qAbs(elapsedDays);
                    int    dWhole  = static_cast<int>(absDays);
                    double fracDay = (absDays - dWhole) * 24.0;
                    int    hours   = static_cast<int>(fracDay);
                    int    mins    = static_cast<int>((fracDay - hours) * 60.0);
                    QString daysStr = QString("%1d %2h %3m")
                                          .arg(dWhole)
                                          .arg(hours, 2, 10, QChar('0'))
                                          .arg(mins, 2, 10, QChar('0'));

                    tooltip = QString(
                        "%1 %2: %3\n"
                        "─────────────────────────\n"
                        "Ann. Sec. mode: %4\n"
                        "Next RAMC    : %5\n"
                        "Return RAMC  : %6\n"
                        "Anniv. Second: %7\n"
                        "RAMC arc     : %8\n"
                        "Elapsed RA   : %9\n"
                        "Elapsed days : %10\n"
                        "─────────────────────────\n"
                        "Return time  : %11")
                        .arg(method, direction, angularDate.toString(dateFormat))
                        .arg(pssrCtx->useApparentSun ? "RAAS (apparent Sun)" : "RAMS (mean Sun)")
                        .arg(A::siderealTimeToString(pssrCtx->nextReturnRAMC, A::HighPrecision))
                        .arg(A::siderealTimeToString(pssrCtx->returnRAMC, A::HighPrecision))
                        .arg(QString::number(pssrCtx->anniversarySecond, 'f', 6))
                        .arg(st(ramcDiff))
                        .arg(st(elapsedRAMS))
                        .arg(daysStr)
                        .arg(pssrCtx->returnTime.toString("yyyy-MM-dd hh:mm"));
            } else {
                tooltip = QString("%1: %2 %3")
                              .arg(method)
                              .arg(angularDate.toString(dateFormat))
                              .arg(direction);
            }
            timeItem->setToolTip(tooltip);
        }

        _table->setItem(row, col, timeItem);
    }
}

void
Speculum::onCellClicked(int row, int column)
{
    if (column == 0) {
        // Planet name clicked - emit planet selection
        QTableWidgetItem* item = _table->item(row, 0);
        if (item) {
            int planetId = item->data(Qt::UserRole).toInt();
            if (planetId >= 0) {
                emit planetSelected(static_cast<A::PlanetId>(planetId),
                                    _fileIndex);
            }
        }
    } else if (column >= 1 && column <= 4) {
        // Time cell clicked - check if clicking the same cell again
        QTableWidgetItem* item = _table->item(row, column);
        if (item) {
            QDateTime timeData = item->data(Qt::UserRole).toDateTime();
            if (timeData.isValid()) {
                // Check if this is the same cell that was already clicked
                if (_filterActive && _clickedRow == row
                    && _clickedCol == column)
                {
                    // Toggle off - clear filter and restore full view
                    onClearFilter();
                } else {
                    // New cell clicked - apply filter
                    // Clear previous click highlights
                    clearClickHighlights();

                    // Uncheck radix button if it was checked
                    _radixBtn->setChecked(false);

                    // Mark the clicked cell with highlight
                    item->setData(CellHighlightRole, ClickedHighlight);
                    item->setData(Qt::UserRole + 1,
                                  true); // Mark as clicked cell

                    // Store which cell is clicked
                    _clickedRow = row;
                    _clickedCol = column;

                    filterByTime(timeData, _orbSpinBox->value());
                    emit timeSelected(timeData);
                }
            }
        }
    }
}

void
Speculum::filterByTime(const QDateTime& centerTime, double orbMinutes)
{
    _filterActive     = true;
    _filterCenterTime = centerTime;
    _filterOrbMinutes = orbMinutes;

    // Convert UTC time to chart's local time zone
    QDateTime localTime = centerTime.addSecs(m_timezone * 3600);

    _filterLabel->setText(tr("Filter: %1 ± %2min")
                              .arg(localTime.time().toString("hh:mm"))
                              .arg(orbMinutes, 0, 'f', 0));
    _clearFilterBtn->setEnabled(true);

    highlightFilteredRows();
}

void
Speculum::highlightFilteredRows()
{
    if (!_filterActive) return;

    for (int row = 0; row < _table->rowCount(); row++) {
        bool rowMatches = false;

        // Check each time column for this row
        for (int col = 1; col <= 4; col++) {
            QTableWidgetItem* item = _table->item(row, col);
            if (item) {
                QDateTime cellTime      = item->data(Qt::UserRole).toDateTime();
                bool      isClickedCell = item->data(Qt::UserRole + 1).toBool();

                if (cellTime.isValid()
                    && isTimeWithinOrb(cellTime,
                                       _filterCenterTime,
                                       _filterOrbMinutes))
                {
                    rowMatches = true;
                    // Mark cells with appropriate highlight type
                    if (isClickedCell) {
                        item->setData(CellHighlightRole, ClickedHighlight);
                    } else {
                        item->setData(CellHighlightRole, MatchedHighlight);
                    }
                } else if (!isClickedCell) {
                    // Clear highlight only if not the clicked cell
                    item->setData(CellHighlightRole, NoHighlight);
                }
            }
        }

        // Hide/show row based on whether it matches
        _table->setRowHidden(row, !rowMatches);
    }
}

void
Speculum::clearClickHighlights()
{
    for (int row = 0; row < _table->rowCount(); row++) {
        for (int col = 1; col <= 4; col++) {
            QTableWidgetItem* item = _table->item(row, col);
            if (item) {
                item->setData(Qt::UserRole + 1, false); // Clear clicked flag
            }
        }
    }
}

bool
Speculum::isTimeWithinOrb(const QDateTime& time1,
                          const QDateTime& time2,
                          double           orbMinutes)
{
    if (!time1.isValid() || !time2.isValid()) return false;

    qint64 diffSeconds = qAbs(time1.secsTo(time2));
    double diffMinutes = diffSeconds / 60.0;

    return diffMinutes <= orbMinutes;
}

void
Speculum::onFilterOrbChanged()
{
    _filterOrbMinutes = _orbSpinBox->value();

    // Convert to degrees and emit signal to notify other widgets
    double orbDegrees = (_displayMode == A::DisplayRightAscension)
                            ? _filterOrbMinutes
                            : _filterOrbMinutes / 4.0;
    emit   orbSettingChanged(orbDegrees);

    if (_filterActive) {
        filterByTime(_filterCenterTime, _filterOrbMinutes);
    }
}

void
Speculum::onClearFilter()
{
    _filterActive = false;
    _clearFilterBtn->setEnabled(false);
    _filterLabel->setText(tr("Filter inactive"));

    // Uncheck radix button if it was checked
    _radixBtn->setChecked(false);

    // Reset clicked cell tracking
    _clickedRow = -1;
    _clickedCol = -1;

    // Clear all highlights and show all rows
    clearClickHighlights();
    for (int row = 0; row < _table->rowCount(); row++) {
        _table->setRowHidden(row, false);
        for (int col = 1; col <= 4; col++) {
            QTableWidgetItem* item = _table->item(row, col);
            if (item) {
                item->setData(CellHighlightRole, NoHighlight);
            }
        }
    }
}

void
Speculum::onChartButtonClicked(int chartIndex)
{
    // Update button states - mutually exclusive
    _chart1Btn->setChecked(chartIndex == 0);
    _chart2Btn->setChecked(chartIndex == 1);

    // Store selected chart and refresh display
    _selectedChartIndex = chartIndex;
    updateSpeculumDisplay();
}

void
Speculum::onRadixButtonClicked(bool checked)
{
    if (checked && _radixTime.isValid()) {
        // Apply filter using radix time
        clearClickHighlights(); // Clear any cell highlights

        // Reset clicked cell tracking since radix button is now active
        _clickedRow = -1;
        _clickedCol = -1;

        filterByTime(_radixTime, _orbSpinBox->value());
    } else {
        // Uncheck and clear filter
        onClearFilter();
    }
}

void
Speculum::refreshSpeculum()
{
    updateSpeculumDisplay();
}

void
Speculum::clear()
{
    _table->setRowCount(0);
    onClearFilter();
}

void
Speculum::setCurrentPlanet(A::PlanetId planet, int fileIndex)
{
    _selectedPlanet = planet;
    _fileIndex      = fileIndex;
}

AppSettings
Speculum::defaultSettings()
{
    AppSettings s;
    s.setValue("Mundane/includeFixedStars", true);
    s.setValue("Mundane/paranOrb", 1.0);
    s.setValue("Mundane/showParanNatalRows", false);
    return s;
}

AppSettings
Speculum::currentSettings()
{
    AppSettings s;
    s.setValue("Mundane/includeFixedStars", _showFixedStars);
    s.setValue("Mundane/paranOrb",
               _filterOrbMinutes / 4.0); // Convert minutes to degrees
    s.setValue("Mundane/showParanNatalRows", _showParanNatalRows);
    return s;
}

void
Speculum::applySettings(const AppSettings& s)
{
    _showFixedStars     = s.value("Mundane/includeFixedStars", true).toBool();
    _showParanNatalRows = s.value("Mundane/showParanNatalRows", false).toBool();

    // Read orb in degrees and convert based on display mode
    double orbDegrees = s.value("Mundane/paranOrb", 1.0).toDouble();

    // Read display mode from shared Plain widget setting
    _displayMode = A::SpeculumDisplayMode(
        s.value("Mundane/displayMode", unsigned(A::DisplayLocalTime)).toUInt());

    // Convert degrees to appropriate units for current display mode
    if (_displayMode == A::DisplayRightAscension) {
        _filterOrbMinutes = orbDegrees; // Display as degrees
        _orbSpinBox->setSuffix(tr(" deg"));
        _orbSpinBox->setRange(0.25, 15.0); // 0.25 to 15 degrees
    } else {
        _filterOrbMinutes =
            orbDegrees * 4.0; // Convert to minutes (1° = 4 minutes)
        _orbSpinBox->setSuffix(tr(" min"));
        _orbSpinBox->setRange(1.0, 60.0); // 1 to 60 minutes
    }

    _orbSpinBox->setValue(_filterOrbMinutes);

    updateSpeculumDisplay();
}

void
Speculum::onThemeChanged()
{
    // When theme changes, refresh highlights to use new theme colors
    if (_filterActive) {
        highlightFilteredRows();
    }
}

void
Speculum::setDisplayMode(A::SpeculumDisplayMode mode)
{
    if (_displayMode == mode) {
        return; // No change needed
    }
    
    _displayMode = mode;
    
    // Update orb spinbox units based on display mode
    if (_displayMode == A::DisplayRightAscension) {
        // RA mode uses degrees
        double orbDegrees = _filterOrbMinutes; // Already in degrees in RA mode
        _orbSpinBox->setSuffix(tr(" deg"));
        _orbSpinBox->setRange(0.25, 15.0);
        _orbSpinBox->blockSignals(true);
        _orbSpinBox->setValue(orbDegrees);
        _orbSpinBox->blockSignals(false);
    } else {
        // Local/Sidereal time modes use minutes
        double orbMinutes = _filterOrbMinutes; // Already in minutes in time modes
        _orbSpinBox->setSuffix(tr(" min"));
        _orbSpinBox->setRange(1.0, 60.0);
        _orbSpinBox->blockSignals(true);
        _orbSpinBox->setValue(orbMinutes);
        _orbSpinBox->blockSignals(false);
    }
    
    // Refresh the table to show times in the new mode
    updateSpeculumDisplay();
}
