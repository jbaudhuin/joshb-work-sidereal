#include "speculum.h"
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
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

Speculum::Speculum(QWidget* parent) :
    AstroFileHandler(parent),
    _selectedPlanet(A::Planet_None),
    _fileIndex(0),
    _selectedChartIndex(0),
    _filterActive(false),
    _filterOrbMinutes(4.0), // Default 4 minutes
    _showFixedStars(true),
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

    // Load stylesheet from CSS file
    QFile cssfile("details/style.css");
    if (cssfile.open(QIODevice::ReadOnly)) {
        setStyleSheet(cssfile.readAll());
        cssfile.close();
    }

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
    filterLayout->addSpacing(10); // Add some space after chart buttons
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
    if (m[0] == 0) return;

    // Show/hide chart 2 button based on file count
    _chart2Btn->setVisible(filesCount() > 1);

    // If chart 2 button is hidden and was selected, switch to chart 1
    if (!_chart2Btn->isVisible() && _selectedChartIndex == 1) {
        _selectedChartIndex = 0;
        _chart1Btn->setChecked(true);
        _chart2Btn->setChecked(false);
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
    QString   radixTimeStr = QString("%1 %2")
                               .arg(dow[dayOfWeek - 1])
                               .arg(localRadixTime.time().toString("hh:mm"));
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

    // Count planets (excluding MC and Asc)
    int planetCount = 0;
    for (const A::Planet& p : scope.planets) {
        if (p.id != A::Planet_MC && p.id != A::Planet_Asc) {
            planetCount++;
        }
    }

    // Add fixed stars if enabled
    int totalRows = planetCount;
    if (_showFixedStars) {
        totalRows += scope.stars.count();
    }

    _table->setRowCount(totalRows);

    // Populate planet rows
    int row = 0;
    for (const A::Planet& p : scope.planets) {
        if (p.id == A::Planet_MC || p.id == A::Planet_Asc) continue;
        addPlanetRow(p, row++);
    }

    // Populate fixed star rows
    if (_showFixedStars) {
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

    // Get PSSR context if this is chart 2 and it's a return chart
    const A::PSSRContext* pssrCtx = nullptr;
    AstroFile* currentFile = file(_selectedChartIndex);
    if (_selectedChartIndex == 1 && currentFile && filesCount() > 1) {
        // Check if chart 2 is a return type or has "Sun-r=Sun" pattern
        bool isReturn = currentFile->getType() == TypeReturn;
        
        if (!isReturn) {
            // Check for return pattern in filename (H# Sun-r=Sun)
            QString name = currentFile->getName();
            QRegularExpression returnPattern(R"(H\d+\s+Sun-r=Sun)");
            isReturn = returnPattern.match(name).hasMatch();
        }
        
        if (isReturn) {
            // Try to get cached PSSR context, or calculate if needed
            if (!currentFile->hasPSSRContext()) {
                auto ctx = A::calculatePSSRContext(currentFile->horoscope());
                currentFile->setPSSRContext(ctx);
            }
            if (currentFile->hasPSSRContext()) {
                pssrCtx = &currentFile->pssrContext();
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
            
            QDateTime angularDateGMT = A::calculateAngularDate(_radixTime, transitTime, 
                                                                 planetRA, angleRA, pssrCtx);
            // Convert to local time
            int offsetSeconds = m_timezone * 3600;
            QTimeZone timeZone = QTimeZone::fromSecondsAheadOfUtc(offsetSeconds);
            QDateTime angularDate = angularDateGMT.toTimeZone(timeZone);
            
            QString direction = (transitTime < _radixTime) ? "Con" : "Dir";
            QString method = pssrCtx ? "PSSR" : "PD";
            QString dateFormat = pssrCtx ? "ddd yyyy-MM-dd hh:mm" : "yyyy/MM/dd";
            QString tooltip = QString("%1: %2 %3")
                                  .arg(method)
                                  .arg(angularDate.toString(dateFormat))
                                  .arg(direction);
            timeItem->setToolTip(tooltip);
        }

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

    // Get PSSR context if this is chart 2 and it's a return chart
    const A::PSSRContext* pssrCtx = nullptr;
    AstroFile* currentFile = file(_selectedChartIndex);
    if (_selectedChartIndex == 1 && currentFile && filesCount() > 1) {
        // Check if chart 2 is a return type or has "Sun-r=Sun" pattern
        bool isReturn = currentFile->getType() == TypeReturn;
        
        if (!isReturn) {
            // Check for return pattern in filename (H# Sun-r=Sun)
            QString name = currentFile->getName();
            QRegularExpression returnPattern(R"(H\d+\s+Sun-r=Sun)");
            isReturn = returnPattern.match(name).hasMatch();
        }
        
        if (isReturn) {
            // Try to get cached PSSR context, or calculate if needed
            if (!currentFile->hasPSSRContext()) {
                auto ctx = A::calculatePSSRContext(currentFile->horoscope());
                currentFile->setPSSRContext(ctx);
            }
            if (currentFile->hasPSSRContext()) {
                pssrCtx = &currentFile->pssrContext();
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
            
            QDateTime angularDateGMT = A::calculateAngularDate(_radixTime, transitTime,
                                                                 starRA, angleRA, pssrCtx);
            // Convert to local time
            int offsetSeconds = m_timezone * 3600;
            QTimeZone timeZone = QTimeZone::fromSecondsAheadOfUtc(offsetSeconds);
            QDateTime angularDate = angularDateGMT.toTimeZone(timeZone);
            
            QString direction = (transitTime < _radixTime) ? "Con" : "Dir";
            QString method = pssrCtx ? "PSSR" : "PD";
            QString dateFormat = pssrCtx ? "ddd yyyy-MM-dd hh:mm" : "yyyy/MM/dd";
            QString tooltip = QString("%1: %2 %3")
                                  .arg(method)
                                  .arg(angularDate.toString(dateFormat))
                                  .arg(direction);
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

                    // Highlight the clicked cell with a distinct color
                    item->setBackground(
                        QColor(255,
                               215,
                               0,
                               180)); // Gold color for clicked cell
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
                    // Use different colors for clicked cell vs matching cells
                    if (isClickedCell) {
                        item->setBackground(
                            QColor(255,
                                   215,
                                   0,
                                   200)); // Stronger gold for clicked cell
                        item->setForeground(
                            QColor(0, 0, 0)); // Dark text for contrast
                    } else {
                        item->setBackground(
                            QColor(113,
                                   174,
                                   236,
                                   160)); // Stronger blue for matching cells
                        item->setForeground(
                            QColor(255, 255, 255)); // White text for contrast
                    }
                } else if (!isClickedCell) {
                    // Clear highlight only if not the clicked cell
                    item->setBackground(QBrush());
                    item->setForeground(QBrush()); // Reset text color too
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
                item->setBackground(QBrush());
                item->setForeground(QBrush()); // Also reset text color
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
    return s;
}

AppSettings
Speculum::currentSettings()
{
    AppSettings s;
    s.setValue("Mundane/includeFixedStars", _showFixedStars);
    s.setValue("Mundane/paranOrb",
               _filterOrbMinutes / 4.0); // Convert minutes to degrees
    return s;
}

void
Speculum::applySettings(const AppSettings& s)
{
    _showFixedStars = s.value("Mundane/includeFixedStars", true).toBool();

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
