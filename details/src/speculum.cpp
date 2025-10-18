#include <QApplication>
#include <QClipboard>
#include <QAction>
#include <QFile>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QDateTime>
#include <QDebug>
#include <Astroprocessor/Calc>
#include <Astroprocessor/Output>
#include "../../astroprocessor/src/astro-data.h"
#include "../../zodiac/src/mainwindow.h"
#include "speculum.h"

Speculum::Speculum(QWidget* parent) : 
    AstroFileHandler(parent),
    _selectedPlanet(A::Planet_None),
    _fileIndex(0),
    _filterActive(false),
    _filterOrbMinutes(4.0),  // Default 4 minutes
    _showFixedStars(true),
    m_timezone(0)
{
    // Create main table
    _table = new QTableWidget();
    _table->setSelectionBehavior(QAbstractItemView::SelectItems); // Select individual cells, not rows
    _table->setSelectionMode(QAbstractItemView::SingleSelection);  // Only one cell at a time
    _table->setAlternatingRowColors(true);
    _table->verticalHeader()->setVisible(false); // Hide row numbers
    _table->setShowGrid(false); // No grid lines needed
    
    // Set consistent styling with the app - minimal padding for better space usage
    _table->setStyleSheet(
        "QTableWidget {"
        "    background-color: #202020;"
        "    color: #b5bfdf;"
        "    font: 8pt 'DejaVu Sans', Verdana;"
        "    selection-background-color: rgba(113, 174, 236, 100);"
        "    border: none;"
        "}"
        "QTableWidget::item {"
        "    padding: 0px 2px;"
        "    border: none;"
        "}"
        "QTableWidget::item:selected {"
        "    background-color: rgba(113, 174, 236, 150);"
        "    color: #EEEEEE;"
        "}"
        "QTableWidget::item:hover {"
        "    background-color: rgba(181, 191, 223, 120) !important;" // Stronger hover with !important
        "    color: #EEEEEE !important;"
        "}"
        "QTableWidget::item:alternate {"
        "    background-color: rgba(255,255,255,0.05);"
        "}"
        "QTableWidget::item:alternate:hover {"
        "    background-color: rgba(181, 191, 223, 120) !important;" // Explicit hover for alternating rows
        "    color: #EEEEEE !important;"
        "}"
        "QHeaderView::section {"
        "    background-color: #717b88;"
        "    color: #d8e4f4;"
        "    font-weight: bold;"
        "    padding: 2px 4px;"
        "    border: none;"
        "}"
    );
    
    // Create filter controls
    _filterLabel = new QLabel(tr("Filter inactive"));
    _orbSpinBox = new QSpinBox();
    _orbSpinBox->setRange(1, 60);  // 1 to 60 minutes
    _orbSpinBox->setValue(4);
    _orbSpinBox->setSuffix(tr(" min"));
    _orbSpinBox->setToolTip(tr("Time orb for filtering (minutes)"));
    
    _clearFilterBtn = new QPushButton(tr("Clear Filter"));
    _clearFilterBtn->setEnabled(false);
    
    // Layout for filter controls
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->addWidget(new QLabel(tr("Orb:")));
    filterLayout->addWidget(_orbSpinBox);
    filterLayout->addWidget(_clearFilterBtn);
    filterLayout->addStretch();
    filterLayout->addWidget(_filterLabel);
    
    // Main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 5, 0, 0);
    mainLayout->setSpacing(5);
    mainLayout->addLayout(filterLayout);
    mainLayout->addWidget(_table);
    
    // Connect signals
    connect(_table, SIGNAL(cellClicked(int,int)), this, SLOT(onCellClicked(int,int)));
    connect(_orbSpinBox, SIGNAL(valueChanged(int)), this, SLOT(onFilterOrbChanged()));
    connect(_clearFilterBtn, SIGNAL(clicked()), this, SLOT(onClearFilter()));
    
    // Setup table headers
    setupTableHeaders();
    
    // Note: Using inline stylesheet for table styling instead of external CSS file
}

void Speculum::setupTableHeaders()
{
    QStringList headers;
    headers << tr("Planet") << tr("Rise") << tr("MC") << tr("Set") << tr("IC");
    _table->setColumnCount(headers.size());
    _table->setHorizontalHeaderLabels(headers);
    
    // Use resize modes instead of fixed widths to prevent ellipses
    QHeaderView* header = _table->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // Planet name auto-sizes
    header->setSectionResizeMode(1, QHeaderView::Stretch);          // Rise gets equal space
    header->setSectionResizeMode(2, QHeaderView::Stretch);          // MC gets equal space  
    header->setSectionResizeMode(3, QHeaderView::Stretch);          // Set gets equal space
    header->setSectionResizeMode(4, QHeaderView::Stretch);          // IC gets equal space
    
    // Set minimum widths to ensure readability
    _table->setColumnWidth(0, 80);   // Minimum for planet names
    header->setMinimumSectionSize(60); // Minimum for time columns
}

void Speculum::filesUpdated(MembersList m)
{
    if (!file()) {
        clear();
        return;
    }
    
    while (m.size() < filesCount()) m.append(AstroFile::Member());
    if (m[0] == 0) return;
    
    updateSpeculumDisplay();
}

void Speculum::updateSpeculumDisplay()
{
    if (!file()) return;
    
    auto scope = file()->horoscope();
    m_timezone = scope.inputData.tz();
    
    populateSpeculumTable();
    
    if (_filterActive) {
        highlightFilteredRows();
    }
}

void Speculum::populateSpeculumTable()
{
    if (!file()) return;
    
    auto scope = file()->horoscope();
    
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

void Speculum::addPlanetRow(const A::Planet& planet, int row)
{
    // Planet name
    QTableWidgetItem* nameItem = new QTableWidgetItem(planet.name);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole, static_cast<int>(planet.id));
    _table->setItem(row, 0, nameItem);
    
    // Add time columns in order: Rise, MC, Set, IC (indices 0, 2, 1, 3)
    QList<int> timeIndices = {0, 2, 1, 3};
    for (int col = 1; col <= 4; col++) {
        int timeIndex = timeIndices[col - 1];
        QString timeStr = A::_formatTime(planet.angleTransit.at(timeIndex), m_timezone);
        
        QTableWidgetItem* timeItem = new QTableWidgetItem(timeStr);
        timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
        timeItem->setTextAlignment(Qt::AlignCenter);
        
        // Store the actual QDateTime for filtering
        timeItem->setData(Qt::UserRole, planet.angleTransit.at(timeIndex));
        
        _table->setItem(row, col, timeItem);
    }
}

void Speculum::addStarRow(const A::Star& star, int row)
{
    // Star name
    QTableWidgetItem* nameItem = new QTableWidgetItem(star.name);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole, -1);  // Use -1 for stars
    _table->setItem(row, 0, nameItem);
    
    // Add time columns for star
    QList<int> timeIndices = {0, 2, 1, 3};
    for (int col = 1; col <= 4; col++) {
        int timeIndex = timeIndices[col - 1];
        QString timeStr = A::_formatTime(star.angleTransit.at(timeIndex), m_timezone);
        
        QTableWidgetItem* timeItem = new QTableWidgetItem(timeStr);
        timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
        timeItem->setTextAlignment(Qt::AlignCenter);
        
        // Store the actual QDateTime for filtering
        timeItem->setData(Qt::UserRole, star.angleTransit.at(timeIndex));
        
        _table->setItem(row, col, timeItem);
    }
}

void Speculum::onCellClicked(int row, int column)
{
    if (column == 0) {
        // Planet name clicked - emit planet selection
        QTableWidgetItem* item = _table->item(row, 0);
        if (item) {
            int planetId = item->data(Qt::UserRole).toInt();
            if (planetId >= 0) {
                emit planetSelected(static_cast<A::PlanetId>(planetId), _fileIndex);
            }
        }
    } else if (column >= 1 && column <= 4) {
        // Time cell clicked - apply filter
        QTableWidgetItem* item = _table->item(row, column);
        if (item) {
            QDateTime timeData = item->data(Qt::UserRole).toDateTime();
            if (timeData.isValid()) {
                // Clear previous click highlights
                clearClickHighlights();
                
                // Highlight the clicked cell with a distinct color
                item->setBackground(QColor(255, 215, 0, 180)); // Gold color for clicked cell
                item->setData(Qt::UserRole + 1, true); // Mark as clicked cell
                
                filterByTime(timeData, _orbSpinBox->value());
                emit timeSelected(timeData);
            }
        }
    }
}

void Speculum::filterByTime(const QDateTime& centerTime, double orbMinutes)
{
    _filterActive = true;
    _filterCenterTime = centerTime;
    _filterOrbMinutes = orbMinutes;
    
    _filterLabel->setText(tr("Filter: %1 ± %2min")
                         .arg(centerTime.time().toString("hh:mm"))
                         .arg(orbMinutes, 0, 'f', 0));
    _clearFilterBtn->setEnabled(true);
    
    highlightFilteredRows();
}

void Speculum::highlightFilteredRows()
{
    if (!_filterActive) return;
    
    for (int row = 0; row < _table->rowCount(); row++) {
        bool rowMatches = false;
        
        // Check each time column for this row
        for (int col = 1; col <= 4; col++) {
            QTableWidgetItem* item = _table->item(row, col);
            if (item) {
                QDateTime cellTime = item->data(Qt::UserRole).toDateTime();
                bool isClickedCell = item->data(Qt::UserRole + 1).toBool();
                
                if (cellTime.isValid() && isTimeWithinOrb(cellTime, _filterCenterTime, _filterOrbMinutes)) {
                    rowMatches = true;
                    // Use different colors for clicked cell vs matching cells
                    if (isClickedCell) {
                        item->setBackground(QColor(255, 215, 0, 200)); // Stronger gold for clicked cell
                        item->setForeground(QColor(0, 0, 0)); // Dark text for contrast
                    } else {
                        item->setBackground(QColor(113, 174, 236, 160)); // Stronger blue for matching cells
                        item->setForeground(QColor(255, 255, 255)); // White text for contrast
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

void Speculum::clearClickHighlights()
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

bool Speculum::isTimeWithinOrb(const QDateTime& time1, const QDateTime& time2, double orbMinutes)
{
    if (!time1.isValid() || !time2.isValid()) return false;
    
    qint64 diffSeconds = qAbs(time1.secsTo(time2));
    double diffMinutes = diffSeconds / 60.0;
    
    return diffMinutes <= orbMinutes;
}

void Speculum::onFilterOrbChanged()
{
    _filterOrbMinutes = _orbSpinBox->value();
    if (_filterActive) {
        filterByTime(_filterCenterTime, _filterOrbMinutes);
    }
}

void Speculum::onClearFilter()
{
    _filterActive = false;
    _clearFilterBtn->setEnabled(false);
    _filterLabel->setText(tr("Filter inactive"));
    
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

void Speculum::refreshSpeculum()
{
    updateSpeculumDisplay();
}

void Speculum::clear()
{
    _table->setRowCount(0);
    onClearFilter();
}

void Speculum::setCurrentPlanet(A::PlanetId planet, int fileIndex)
{
    _selectedPlanet = planet;
    _fileIndex = fileIndex;
}

AppSettings Speculum::defaultSettings()
{
    AppSettings s;
    s.setValue("Speculum/showFixedStars", true);
    s.setValue("Speculum/filterOrb", 4.0);
    return s;
}

AppSettings Speculum::currentSettings()
{
    AppSettings s;
    s.setValue("Speculum/showFixedStars", _showFixedStars);
    s.setValue("Speculum/filterOrb", _filterOrbMinutes);
    return s;
}

void Speculum::applySettings(const AppSettings& s)
{
    _showFixedStars = s.value("Speculum/showFixedStars", true).toBool();
    _filterOrbMinutes = s.value("Speculum/filterOrb", 4.0).toDouble();
    
    _orbSpinBox->setValue(static_cast<int>(_filterOrbMinutes));
    
    updateSpeculumDisplay();
}

void Speculum::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Speculum"));
    ed->addCheckBox("Speculum/showFixedStars", tr("Include fixed stars"));
    QDoubleSpinBox* orbSpinBox = ed->addDoubleSpinBox("Speculum/filterOrb",
                         tr("Default filter orb (minutes)"),
                         1.0,
                         60.0);
    // Override the default "deg" suffix with "min" for time filtering
    if (orbSpinBox) {
        orbSpinBox->setSuffix(tr(" min"));
    }
}
