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
#include <QShortcut>
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
    _paranOrb(1.0),
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
        QAbstractItemView::ExtendedSelection); // Drag/Ctrl to select a range
    _table->setAlternatingRowColors(true);
    _table->verticalHeader()->setVisible(false); // Hide row numbers
    _table->setShowGrid(false);                  // No grid lines needed

    // Ctrl+C copies the selected cells (QTableWidget has no built-in copy)
    auto* copySc = new QShortcut(QKeySequence::Copy, _table);
    copySc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copySc, &QShortcut::activated, this, &Speculum::copySelection);
    
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

    // While scrubbing (wheel drag / animation), skip the speculum rebuild;
    // the catch-up recompute on scrub-exit refreshes it once.
    if (A::isScrubbing()) return;

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

    // The "natal" file for ex-precessed rows is the other chart in a biwheel
    int        natalIdx  = 1 - _selectedChartIndex;
    AstroFile* natalFile = (filesCount() > 1 && natalIdx >= 0) ? file(natalIdx) : nullptr;

    // Natal ex-precessed rows follow the same rule as describeParans(): shown
    // for a Par=N (ToNatal) paran chart always, and for a plain paran chart
    // only when the natal chart is a radix and the user enabled natal rows.
    const bool natalCtxIsRadix = natalFile
        && (natalFile->getType() == TypeMale
            || natalFile->getType() == TypeFemale
            || natalFile->getType() == TypeEvent
            || natalFile->getType() == TypeComposite);
    const bool runNatalRows = isParanChart && natalFile
        && (paranFile->getOriginEventType() == A::etcParanatellontaToNatal
            || (paranFile->getOriginEventType() == A::etcParanatellonta
                && _showParanNatalRows && natalCtxIsRadix));

    // --- Non-paran charts: the original full planet + star listing ---------
    if (!isParanChart) {
        int planetCount = 0;
        for (const A::Planet& p : scope.planets) {
            if (p.id == A::Planet_MC || p.id == A::Planet_Asc) continue;
            planetCount++;
        }
        int totalRows = planetCount + (_showFixedStars ? scope.stars.count() : 0);
        _table->setRowCount(totalRows);

        int row = 0;
        for (const A::Planet& p : scope.planets) {
            if (p.id == A::Planet_MC || p.id == A::Planet_Asc) continue;
            addPlanetRow(p, row++);
        }
        if (_showFixedStars) {
            for (const A::Star& s : scope.stars) addStarRow(s, row++);
        }
        return;
    }

    // --- Paran charts: model the Directions focused cluster ----------------
    // Compute the natal ex-precessed crossings up front (needed both as
    // cluster candidates and as rendered rows), then gather every candidate
    // crossing (radix + planets + stars + natal), prune to the radix-anchored
    // cluster with the shared anchor-chained walk, and show every body that
    // has at least one crossing in the cluster (planets also always include
    // the event's own focal set). Participating cells are highlighted.
    struct NatalRowData {
        A::PlanetId pid;
        QString     name;
        QDateTime   angleTransit[4];
        double      angleTransitRA[4];
    };
    QVector<NatalRowData> natalRows;

    if (runNatalRows) {
        double jdNatal = A::getJulianDate(natalFile->getGMT());
        double jdParan = A::getJulianDate(paranFile->getGMT());
        // Search the same midnight-to-midnight window that findParans() uses,
        // so the crossing times are consistent with the event that was found.
        QDateTime paranDayUTC = paranFile->getGMT().toUTC();
        paranDayUTC.setTime(QTime(0, 0, 0));
        double jdMidnight = A::getJulianDate(paranDayUTC);
        double lat     = scope.inputData.location().y();
        double lon     = scope.inputData.location().x();

        const bool natalIsComposite = natalFile->getType() == TypeComposite;

        // Every natal planet is a candidate (matching describeParans); the
        // cluster prune below keeps only those falling in the paran group.
        for (const A::Planet& np : natalFile->horoscope().planets) {
            A::PlanetId pid = np.id;

            // Get tropical (non-sidereal) natal RA/Dec — same convention as
            // NatalExprecessedPosition._natalRA so exprecess_equatorial works correctly.
            // Composite: use the synthesized horoscope positions (no real natal sky).
            double tropRA, tropDec;
            if (natalIsComposite) {
                if (pid <= A::Planet_None || pid >= A::Angles_Start) continue;
                if (!A::horoscopeTropicalEquatorialPos(
                        np, natalFile->horoscope(), tropRA, tropDec))
                    continue;
            } else if (!A::natalTropicalEquatorialPos(pid, jdNatal, tropRA, tropDec))
                continue;

            NatalRowData nr;
            nr.pid  = pid;
            nr.name = np.name;
            A::computeNatalParanTransits(
                tropRA, tropDec,
                jdNatal, jdMidnight, lat, lon,
                nr.angleTransit, nr.angleTransitRA,
                /*jdAnchor=*/jdParan);
            natalRows.append(nr);
        }
    }

    // Gather cluster candidates. kind: 0=radix, 1=planet, 2=star, 3=natal.
    // Radix, transit planets, AND natal ex-precessed bodies anchor the chain;
    // only fixed stars ride along without extending it — matching describeParans().
    struct Cand {
        QDateTime   dt;
        bool        isAnchor;
        int         kind;
        A::PlanetId key;    // planet/star/natal body id (Planet_None for radix)
        int         angle;  // 0..3, -1 for radix
    };
    QVector<Cand> cands;
    cands.append({ paranFile->getGMT(), true, 0, A::Planet_None, -1 });

    for (const A::Planet& p : scope.planets) {
        if (p.id == A::Planet_MC || p.id == A::Planet_Asc) continue;
        for (int m = 0; m < 4; ++m) {
            const QDateTime& dt = p.angleTransit.at(m);
            if (dt.isValid()) cands.append({ dt, true, 1, p.id, m });
        }
    }
    if (_showFixedStars) {
        for (const A::Star& s : scope.stars) {
            for (int m = 0; m < 4; ++m) {
                const QDateTime& dt = s.angleTransit.at(m);
                if (dt.isValid())
                    cands.append({ dt, false, 2, s.id, m });
            }
        }
    }
    for (const NatalRowData& nr : natalRows) {
        for (int m = 0; m < 4; ++m) {
            if (nr.angleTransit[m].isValid())
                cands.append({ nr.angleTransit[m], true, 3, nr.pid, m });
        }
    }

    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.dt < b.dt; });

    // Membership by body → participating angle indices (for highlighting), plus
    // a time-ordered, deduplicated row list: each body is emitted once, at its
    // first in-cluster crossing. Because cands is sorted by time, this reproduces
    // the Directions table's focal-paran ordering (which lists crossings by time).
    QMap<A::PlanetId, QSet<int>> planetAngles, starAngles, natalAngles;
    struct RowRef { int kind; A::PlanetId key; };
    QVector<RowRef> orderedRows;
    int radixIdx = -1;
    for (int i = 0; i < cands.size(); ++i)
        if (cands[i].kind == 0) { radixIdx = i; break; }

    if (radixIdx >= 0) {
        QVector<A::ParanClusterCandidate> pcc;
        pcc.reserve(cands.size());
        for (const Cand& c : std::as_const(cands))
            pcc.append({ c.dt, c.isAnchor });

        const qint64 orbSecs = qint64(_paranOrb * 240);
        const auto range = A::radixParanClusterRange(pcc, radixIdx, orbSecs);
        for (int i = range.first; i <= range.second; ++i) {
            const Cand& c = cands[i];
            // First appearance of a body (map doesn't yet contain its key) marks
            // its row position in time order.
            switch (c.kind) {
            case 1:
                if (!planetAngles.contains(c.key)) orderedRows.append({ 1, c.key });
                planetAngles[c.key].insert(c.angle);
                break;
            case 2:
                if (!starAngles.contains(c.key)) orderedRows.append({ 2, c.key });
                starAngles[c.key].insert(c.angle);
                break;
            case 3:
                if (!natalAngles.contains(c.key)) orderedRows.append({ 3, c.key });
                natalAngles[c.key].insert(c.angle);
                break;
            default: break;
            }
        }
    }

    // Body lookups for rendering by id (planet/natal keyed by PlanetId; star by
    // its Stars_Start+i id). Kept in separate maps so a transit body and its
    // natal ex-precessed twin — same PlanetId — don't collide.
    QMap<A::PlanetId, const A::Planet*> planetById;
    for (const A::Planet& p : scope.planets) planetById[p.id] = &p;
    QMap<A::PlanetId, const A::Star*> starById;
    for (const A::Star& s : scope.stars) starById[s.id] = &s;
    QMap<A::PlanetId, const NatalRowData*> natalByPid;
    for (const NatalRowData& nr : natalRows) natalByPid[nr.pid] = &nr;

    _table->setRowCount(orderedRows.size());

    // Column order in the table is {Rise, MC, Set, IC} = angle indices
    // {0, 2, 1, 3}; reverse map angle → column (1-based, col 0 is the name).
    static const int angleToCol[4] = { 1, 3, 2, 4 };
    auto highlight = [&](int r, const QSet<int>& angles) {
        for (int m : angles)
            if (QTableWidgetItem* it = _table->item(r, angleToCol[m]))
                it->setData(CellHighlightRole, MatchedHighlight);
    };

    int row = 0;
    for (const RowRef& rr : std::as_const(orderedRows)) {
        switch (rr.kind) {
        case 1:
            if (const A::Planet* p = planetById.value(rr.key)) {
                addPlanetRow(*p, row);
                highlight(row, planetAngles.value(rr.key));
            }
            break;
        case 2:
            if (const A::Star* s = starById.value(rr.key)) {
                addStarRow(*s, row);
                highlight(row, starAngles.value(rr.key));
            }
            break;
        case 3:
            if (const NatalRowData* nr = natalByPid.value(rr.key)) {
                addNatalParanRow(nr->name,
                                 const_cast<QDateTime*>(nr->angleTransit),
                                 const_cast<double*>(nr->angleTransitRA), row);
                highlight(row, natalAngles.value(rr.key));
            }
            break;
        default: break;
        }
        ++row;
    }
}

namespace {

// Resolve the quotidian direction context for the chart shown in the speculum.
// Returns nullptr (and DirNone) when the chart isn't an eligible return/ingress;
// otherwise (re)builds and caches the context for the resolved method.
const A::PSSRContext*
resolveSpeculumDirCtx(AstroFile* f)
{
    A::DirMethod method = A::resolveDirMethod(f);
    if (!f || method == A::DirNone) return nullptr;
    if (!f->hasPSSRContext() || f->pssrContext().method != method)
        f->setPSSRContext(A::buildDirContext(f->horoscope(), method));
    return f->hasPSSRContext() ? &f->pssrContext() : nullptr;
}

// Build the angular-date tooltip for a single Rise/MC/Set/IC cell.  Handles PD
// (ctx == nullptr), the SQ/NeoSQ family (simple), and the PSSR/NeoPSSR family
// (full anniversary-second breakdown).
QString
buildAngularTooltip(const A::PSSRContext* ctx,
                    double                angleRA,
                    const QDateTime&      angularDate,
                    const QString&        direction)
{
    const QString method     = ctx ? A::dirMethodLabel(ctx->method) : QStringLiteral("PD");
    const QString dateFormat  = ctx ? QStringLiteral("ddd yyyy-MM-dd hh:mm")
                                    : QStringLiteral("yyyy/MM/dd");

    if (!ctx) {
        return QString("%1: %2 %3")
            .arg(method, angularDate.toString(dateFormat), direction);
    }

    auto st = [](double d) -> QString {
        QString sign = (d < 0) ? "-" : "";
        double  a    = qAbs(d) / 15.0;
        int     h    = static_cast<int>(a);
        double  rem  = (a - h) * 60.0;
        int     m    = static_cast<int>(rem);
        int     s    = static_cast<int>((rem - m) * 60.0);
        return QString("%1%2h %3m %4s")
            .arg(sign).arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    };

    // RAMC arc (both families use it as the starting quantity).
    double ramcDiff = angleRA - ctx->returnRAMC;
    if (ramcDiff > 180.0) ramcDiff -= 360.0;
    else if (ramcDiff <= -180.0) ramcDiff += 360.0;

    if (A::dirMethodIsPSSR(ctx->method)) {
        // PSSR / NeoPSSR — anniversary-second breakdown.
        double elapsedRAMS = ramcDiff / ctx->anniversarySecond;
        double elapsedDays = (elapsedRAMS / 360.0) * 365.25;

        double absDays = qAbs(elapsedDays);
        int    dWhole  = static_cast<int>(absDays);
        double fracDay = (absDays - dWhole) * 24.0;
        int    hours   = static_cast<int>(fracDay);
        int    mins    = static_cast<int>((fracDay - hours) * 60.0);
        QString daysStr = QString("%1d %2h %3m")
                              .arg(dWhole).arg(hours, 2, 10, QChar('0')).arg(mins, 2, 10, QChar('0'));

        return QString(
            "%1 %2: %3\n"
            "─────────────────────────\n"
            "Sun mode     : %4\n"
            "Next RAMC    : %5\n"
            "Return RAMC  : %6\n"
            "Anniv. Second: %7\n"
            "RAMC arc     : %8\n"
            "Elapsed RA   : %9\n"
            "Elapsed days : %10\n"
            "─────────────────────────\n"
            "Return time  : %11")
            .arg(method, direction, angularDate.toString(dateFormat))
            .arg(ctx->useApparentSun ? "RAAS (apparent Sun)" : "RAMS (mean Sun)")
            .arg(A::siderealTimeToString(ctx->nextReturnRAMC, A::HighPrecision))
            .arg(A::siderealTimeToString(ctx->returnRAMC, A::HighPrecision))
            .arg(QString::number(ctx->anniversarySecond, 'f', 6))
            .arg(st(ramcDiff))
            .arg(st(elapsedRAMS))
            .arg(daysStr)
            .arg(ctx->returnTime.toString("yyyy-MM-dd hh:mm"));
    }

    // SQ / NeoSQ — simple solar quotidian (no anniversary second).
    //   SV0 = anchor Sun RA;  SV1 = SV0 + elapsed = Sun RA at the perfected direction.
    double sv1 = ctx->returnRAMS + ramcDiff;
    while (sv1 >= 360.0) sv1 -= 360.0;
    while (sv1 < 0.0)    sv1 += 360.0;

    return QString(
        "%1 %2: %3\n"
        "─────────────────────────\n"
        "Sun mode      : %4\n"
        "Anchor RAMC   : %5\n"
        "SV0 (anchor)  : %6\n"
        "SV1 (event)   : %7\n"
        "Elapsed RA    : %8\n"
        "─────────────────────────\n"
        "Anchor time   : %9")
        .arg(method, direction, angularDate.toString(dateFormat))
        .arg(ctx->useApparentSun ? "RAAS (apparent Sun)" : "RAMS (mean Sun)")
        .arg(A::siderealTimeToString(ctx->returnRAMC, A::HighPrecision))
        .arg(A::siderealTimeToString(ctx->returnRAMS, A::HighPrecision))
        .arg(A::siderealTimeToString(sv1, A::HighPrecision))
        .arg(st(ramcDiff))
        .arg(ctx->returnTime.toString("yyyy-MM-dd hh:mm"));
}

} // namespace

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

    // Resolve the quotidian direction context for this chart (SQ/NeoSQ/PSSR/NeoPSSR).
    const A::PSSRContext* pssrCtx = resolveSpeculumDirCtx(file(_selectedChartIndex));

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
            double radixRAMC = file(_selectedChartIndex)->horoscope().houses.RAMC;
            QDateTime angularDateGMT = A::calculateAngularDate(_radixTime, transitTime,
                                                                 planetRA, angleRA, pssrCtx, label, radixRAMC);
            // Convert to local time
            int offsetSeconds = m_timezone * 3600;
            QTimeZone timeZone = QTimeZone::fromSecondsAheadOfUtc(offsetSeconds);
            QDateTime angularDate = angularDateGMT.toTimeZone(timeZone);
            
            QString direction = (transitTime < _radixTime) ? "Con" : "Dir";
            timeItem->setToolTip(buildAngularTooltip(pssrCtx, angleRA, angularDate, direction));
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

    // Resolve the quotidian direction context for this chart (SQ/NeoSQ/PSSR/NeoPSSR).
    const A::PSSRContext* pssrCtx = resolveSpeculumDirCtx(file(_selectedChartIndex));

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
            double radixRAMC = file(_selectedChartIndex)->horoscope().houses.RAMC;
            QDateTime angularDateGMT = A::calculateAngularDate(_radixTime, transitTime,
                                                                 starRA, angleRA, pssrCtx, label, radixRAMC);
            // Convert to local time
            int offsetSeconds = m_timezone * 3600;
            QTimeZone timeZone = QTimeZone::fromSecondsAheadOfUtc(offsetSeconds);
            QDateTime angularDate = angularDateGMT.toTimeZone(timeZone);
            
            QString direction = (transitTime < _radixTime) ? "Con" : "Dir";
            timeItem->setToolTip(buildAngularTooltip(pssrCtx, angleRA, angularDate, direction));
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
    _paranOrb = orbDegrees;  // paran-cluster orb (shared with Directions)

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
Speculum::copySelection()
{
    // Copy the selected cells to the clipboard as a tab-delimited grid
    // spanning the selection's bounding box (unselected cells left empty).
    const auto items = _table->selectedItems();
    if (items.isEmpty()) return;

    QMap<int, QMap<int, QString>> cells;  // row → col → text
    for (const QTableWidgetItem* it : items)
        cells[it->row()][it->column()] = it->text().trimmed();

    QStringList lines;
    for (auto rit = cells.constBegin(); rit != cells.constEnd(); ++rit) {
        QStringList fields;
        const int firstCol = rit->firstKey();
        const int lastCol  = rit->lastKey();
        for (int c = firstCol; c <= lastCol; ++c)
            fields << rit->value(c);
        lines << fields.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
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
