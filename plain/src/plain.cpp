#include "plain.h"
#include "../../zodiac/src/thememanager.h"
#include "../../astroprocessor/src/citydb.h"
#include <Astroprocessor/Output>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QFont>
#include <QTextDocumentFragment>
#include <QTextFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QToolBar>
#include <QVBoxLayout>
#include "../../zodiac/src/slidewidget.h"

/* ============================== REPORT BROWSER
 * ======================================== */

// QTextBrowser subclass that produces portable clipboard content when the user
// copies cells out of one of the report tables (Directions, Speculum, etc.).
//
// Qt's default rich-text copy serializes the selection as a fragment of its own
// HTML dialect: every table cell is wrapped in a <p> with margins (which Word
// renders as a blank line after each row) and the monospace font is dropped, and
// the markup is exotic enough that Gmail discards it and falls back to the
// plain-text flavor (one cell per line). We instead emit a clean <table> with an
// explicit monospace font plus a tab-delimited plain-text version, so both Word
// and Gmail paste a proper table.
class ReportBrowser : public QTextBrowser {
  public:
    using QTextBrowser::QTextBrowser;

  protected:
    QMimeData* createMimeDataFromSelection() const override;
};

QMimeData*
ReportBrowser::createMimeDataFromSelection() const
{
    QTextCursor cur = textCursor();
    if (!cur.hasSelection())
        return QTextBrowser::createMimeDataFromSelection();

    const int selStart = cur.selectionStart();
    const int selEnd   = cur.selectionEnd();

    // In a read-only browser a mouse drag yields a plain linear selection rather
    // than a rectangular cell block, so we locate the table from the selection
    // endpoints rather than relying on QTextCursor::selectedTableCells().
    QTextCursor probe(document());
    probe.setPosition(selStart);
    QTextTable* table = probe.currentTable();
    if (!table) {
        probe.setPosition(selEnd);
        table = probe.currentTable();
    }
    if (!table)
        return QTextBrowser::createMimeDataFromSelection();

    QTextTableCell startCell = table->cellAt(selStart);
    QTextTableCell endCell   = table->cellAt(selEnd);

    // Selection confined to a single cell: leave normal text copy alone.
    if (startCell.isValid() && endCell.isValid()
        && startCell.row() == endCell.row()
        && startCell.column() == endCell.column())
        return QTextBrowser::createMimeDataFromSelection();

    int firstRow = startCell.isValid() ? startCell.row() : 0;
    int lastRow  = endCell.isValid() ? endCell.row() : table->rows() - 1;
    if (firstRow > lastRow) std::swap(firstRow, lastRow);

    const int     cols    = table->columns();
    const int     headerRows = table->format().headerRowCount();
    // Generic monospace lets each target (Gmail etc.) use its own configured
    // monospace font; the doubled keyword sidesteps the browser quirk that
    // otherwise shrinks a lone "monospace". Carried on the cell and on a <span>
    // around the run. (Word ignores font here regardless and is fixed up with
    // its "No Spacing" style.)
    const QString fontCss = "font-family:monospace,monospace;font-size:10pt;";

    // Wrap in a full document: Chrome/Gmail's clipboard reader rejects a bare
    // <table> fragment and falls back to plain text, whereas Word is lenient.
    QString html = "<html><head><meta charset=\"utf-8\"></head><body>";
    html += "<table cellspacing=\"0\" style=\"border-collapse:collapse;"
            + fontCss + "\">";
    QString tsv;

    for (int r = firstRow; r <= lastRow; ++r) {
        const bool headerRow = r < headerRows;
        const char* tag = headerRow ? "th" : "td";

        // Cluster separators in the report are drawn as a top border on the
        // first row of each group; carry that through so pasted output keeps the
        // visual grouping (a blank line in the plain-text flavor).
        bool separator = false;
        QTextTableCell c0 = table->cellAt(r, 0);
        if (c0.isValid()) {
            QTextTableCellFormat cf = c0.format().toTableCellFormat();
            separator = cf.hasProperty(QTextFormat::TableCellTopBorder)
                        && cf.topBorder() > 0.0;
        }
        if (separator && r > firstRow)
            tsv += '\n';

        const QString cellBorder =
            separator ? "border-top:1px solid #777;" : QString();

        html += "<tr>";
        QStringList rowCells;
        for (int c = 0; c < cols; ++c) {
            QTextTableCell cell = table->cellAt(r, c);
            QString text;
            // Per-cell run style carries the bold/italic the report applies:
            // bold to planet (vs. fixed-star) names and header cells, italic to
            // natal angle-transit rows. Read from the cell's character format.
            QString runStyle = fontCss;
            if (cell.isValid()) {
                QTextCursor cc = cell.firstCursorPosition();
                cc.setPosition(cell.lastCursorPosition().position(),
                               QTextCursor::KeepAnchor);
                text = cc.selection().toPlainText().trimmed();

                const QTextCharFormat ccf = cc.charFormat();
                if (ccf.fontWeight() > QFont::Normal)
                    runStyle += "font-weight:bold;";
                if (ccf.fontItalic())
                    runStyle += "font-style:italic;";
            }
            rowCells << text;
            // <p margin:0> drops the paragraph spacing Word would otherwise add;
            // the <span> carries the monospace font and bold/italic.
            html += '<';
            html += tag;
            html += " style=\"padding:0px 10px;text-align:left;white-space:pre;";
            html += cellBorder;
            html += fontCss;
            html += "\"><p style=\"margin:0;mso-line-height-rule:exactly;"
                    "line-height:1.0;\"><span style=\"";
            html += runStyle;
            html += "\">";
            html += text.toHtmlEscaped();
            html += "</span></p></";
            html += tag;
            html += '>';
        }
        html += "</tr>";

        tsv += rowCells.join('\t');
        tsv += '\n';
    }
    html += "</table></body></html>";

    QMimeData* md = new QMimeData;
    md->setHtml(html);
    md->setText(tsv);
    return md;
}

/* ================================== WIDGET
 * ======================================== */

Plain::Plain(QWidget* parent) : AstroFileHandler(parent)
{
    chartsCount   = 0;
    aspectsCached = false;
    
    // Enable drag and drop
    setAcceptDrops(true);

    // Create toolbar with toggle actions
    toolbar = new QToolBar(tr("Display Options"), this);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    describeInput = toolbar->addAction(tr("Input"));
    describeInput->setCheckable(true);
    describeInput->setChecked(false);
    describeInput->setStatusTip(tr("Show input data"));

    describePlanets = toolbar->addAction(tr("Planets"));
    describePlanets->setCheckable(true);
    describePlanets->setChecked(true);
    describePlanets->setStatusTip(tr("Show planets"));

    describeHouses = toolbar->addAction(tr("Houses"));
    describeHouses->setCheckable(true);
    describeHouses->setChecked(true);
    describeHouses->setStatusTip(tr("Show houses"));

    describeAspects = toolbar->addAction(tr("Aspects"));
    describeAspects->setCheckable(true);
    describeAspects->setChecked(true);
    describeAspects->setStatusTip(tr("Show aspects"));

    describePower = toolbar->addAction(tr("Dignities"));
    describePower->setCheckable(true);
    describePower->setChecked(false);
    describePower->setStatusTip(
        tr("Show dignity and deficient points for each planet"));

    describeParans = toolbar->addAction(tr("Directions"));
    describeParans->setCheckable(true);
    describeParans->setChecked(true);
    describeParans->setStatusTip(
        tr("Show natal parans rolled up against primary directions"));

    describeSpeculum = toolbar->addAction(tr("Speculum"));
    describeSpeculum->setCheckable(true);
    describeSpeculum->setChecked(true);
    describeSpeculum->setStatusTip(
        tr("Show rise/set/MC/IC times for each body"));

    describeParanLats = toolbar->addAction(tr("Parans"));
    describeParanLats->setCheckable(true);
    describeParanLats->setChecked(false);
    describeParanLats->setStatusTip(
        tr("Show latitudes (and cities on them) at which each natal-body pair forms a paran"));

    toolbar->addSeparator();

    // Create display mode selector combo box (no label)
    displayModeSelector = new QComboBox();
    displayModeSelector->addItem(tr("Local Time"), A::DisplayLocalTime);
    displayModeSelector->addItem(tr("Sidereal Time"), A::DisplaySiderealTime);
    displayModeSelector->addItem(tr("Right Ascension"), A::DisplayRightAscension);
    displayModeSelector->setCurrentIndex(0); // Default to Local Time
    toolbar->addWidget(displayModeSelector);

    // Insert chart selector buttons at the beginning of toolbar (after Input button)
    chart1Btn = new QPushButton("1");
    chart1Btn->setCheckable(true);
    chart1Btn->setChecked(true); // Default to showing both charts
    chart1Btn->setToolTip(tr("Show Chart #1"));
    chart1Btn->setProperty("chartButton", true);
    toolbar->insertWidget(describeInput, chart1Btn);

    chart2Btn = new QPushButton("2");
    chart2Btn->setCheckable(true);
    chart2Btn->setChecked(true); // Default to showing both charts
    chart2Btn->setToolTip(tr("Show Chart #2"));
    chart2Btn->setProperty("chartButton", true);
    toolbar->insertWidget(describeInput, chart2Btn);
    chart2Btn->setVisible(false); // Initially hidden until 2nd chart loaded

    view = new ReportBrowser();
    view->setAcceptDrops(false); // Disable drops on the view so parent Plain widget handles them

    showAllDiurnalEvents = false;
    includeFixedStars    = true;
    showParanNatalRows   = false;
    paranCityLatTol      = 0.5;
    paranMaxCitiesPerRow = 8;
    paranShowAbsent      = true;
    paranCityPopMask       = A::CityPop_All;
    paranCityContinentMask = A::CityCont_All;
    aspectSortOrder      = A::SortByPlanets;

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,
                               70,
                               0,
                               0); // Top margin for file info widgets *sigh*
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(view);

    connect(describeInput, &QAction::triggered, this, &Plain::refresh);
    connect(describePlanets, &QAction::triggered, this, &Plain::refresh);
    connect(describeHouses, &QAction::triggered, this, &Plain::refresh);
    connect(describeAspects, &QAction::triggered, this, &Plain::refresh);
    connect(describePower, &QAction::triggered, this, &Plain::refresh);
    connect(describeParans, &QAction::triggered, this, &Plain::refresh);
    connect(describeSpeculum, &QAction::triggered, this, &Plain::refresh);
    connect(describeParanLats, &QAction::triggered, this, &Plain::refresh);
    connect(chart1Btn, &QPushButton::clicked, this, &Plain::refresh);
    connect(chart2Btn, &QPushButton::clicked, this, &Plain::refresh);
    connect(displayModeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        A::SpeculumDisplayMode mode = A::SpeculumDisplayMode(displayModeSelector->currentData().toInt());
        emit displayModeChanged(mode);
        refresh();
    });

    // Connect to theme changes to regenerate HTML with theme-appropriate inline colors
    connect(&ThemeManager::instance(),
            &ThemeManager::themeChanged,
            this,
            &Plain::refresh);

    // Component-specific CSS loading disabled - now using global theme system
    // QFile cssfile("plain/style.css");
    // cssfile.open(QIODevice::ReadOnly | QIODevice::Text);
    // setStyleSheet(cssfile.readAll());
}

void
Plain::setParanOrb(double orb)
{
    paranOrb = orb;
    refresh();
}

void
Plain::updateAspectsCache()
{
    if (aspectsCached) {
        return; // Already cached
    }

    cachedChart1Aspects.clear();
    cachedChart2Aspects.clear();
    cachedSynastryAspects.clear();

    if (!file()) {
        aspectsCached = true;
        return;
    }

    // Calculate aspects for chart 1
    if (filesCount() >= 1) {
        cachedChart1Aspects = calculateAspects();
    }

    // Calculate aspects for chart 2 and synastry if we have 2 charts
    if (filesCount() > 1) {
        // Get chart 2 aspects from its horoscope
        auto scope2         = file(1)->horoscope();
        cachedChart2Aspects = scope2.aspects;

        // Calculate synastry aspects
        cachedSynastryAspects = calculateSynastryAspects();
    }

    aspectsCached = true;
}

void
Plain::filesUpdated(MembersList m)
{
    if (!file()) {
        view->clear();
        chartsCount   = 0;
        aspectsCached = false;
        // Hide chart 2 button when no files
        chart2Btn->setVisible(false);
        return;
    }

    while (m.size() < filesCount()) m.append(AstroFile::Member());

    // Detect if the number of charts changed
    bool chartsCountChanged = (chartsCount != filesCount());
    chartsCount             = filesCount();

    // Show/hide chart 2 button based on number of files
    chart2Btn->setVisible(filesCount() > 1);

    // File-data changes (GMT, Location) that affect aspect positions
    bool needsAspectUpdate = false;
    for (const auto& members : m) {
        if (members & (AstroFile::GMT | AstroFile::Location)) {
            needsAspectUpdate = true;
            break;
        }
    }

    if (chartsCountChanged || needsAspectUpdate) {
        aspectsCached = false;
    }

    // Refresh on a change to ANY file, not just file(0). The Directions/Parans
    // tables for Chart #2 are rendered from file(1) (e.g. the moving chart of a
    // Par=N biwheel), so paran cycling — which steps file(1)'s GMT — must
    // re-render even when file(0) is unchanged.
    bool anyChanged = false;
    for (const auto& members : m)
        if (members) { anyChanged = true; break; }

    if (chartsCountChanged || anyChanged) {
        refresh();
    }
}

void
Plain::viewSettingsUpdated(MembersList m)
{
    if (!file()) return;

    // View-setting changes always invalidate the aspect cache
    bool needsAspectUpdate = false;
    for (const auto& members : m) {
        if (members) { needsAspectUpdate = true; break; }
    }

    if (needsAspectUpdate) {
        aspectsCached = false;
        refresh();
    }
}

void
Plain::refresh()
{
    if (!file()) {
        return;
    }

    // Save scroll position
    QScrollBar* vScrollBar = view->verticalScrollBar();
    int         scrollPos  = vScrollBar->value();

    // Determine which charts to display
    bool showFirst  = true;
    bool showSecond = true;

    if (filesCount() > 1) {
        showFirst  = chart1Btn->isChecked();
        showSecond = chart2Btn->isChecked();
    } else {
        showSecond = false; // Only one chart available
    }

    // Get display mode from combo box data
    A::SpeculumDisplayMode displayMode =
        A::SpeculumDisplayMode(displayModeSelector->currentData().toInt());

    // Update aspects cache if needed (only when aspects will be displayed)
    if (describeAspects->isChecked()) {
        updateAspectsCache();
    }

    int articles = (A::Article_Input * describeInput->isChecked())
                   | (A::Article_Planet * describePlanets->isChecked())
                   | (A::Article_Houses * describeHouses->isChecked())
                   | (A::Article_Aspects * describeAspects->isChecked())
                   | (A::Article_Power * describePower->isChecked())
                   | (A::Article_Parans * describeParans->isChecked())
                   | (A::Article_DiurnalEvents * showAllDiurnalEvents)
                   | (A::Article_Speculum * describeSpeculum->isChecked())
                   | (A::Article_FixedStars * includeFixedStars)
                   | (A::Article_ParanLatitudes * describeParanLats->isChecked());

    // Get theme-appropriate row highlight color (subtle overlay for alternating rows/headers)
    QString rowHighlightColor;
    if (ThemeManager::instance().currentTheme() == ThemeManager::Theme::Dark) {
        rowHighlightColor = "rgba(255,255,255,0.08)"; // Subtle white overlay for dark theme
    } else {
        rowHighlightColor = "rgba(0,0,0,0.08)"; // Subtle black overlay for light theme
    }

    // Build the HTML content - colors inherit from QTextBrowser stylesheet
    QString html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<style>";
    html += "body { font-family: 'Consolas', 'Courier New', courier, 'DejaVu "
            "Sans Mono', 'Lucida Console'; margin: 10px; background-color: transparent; }";
    html += "h1, h2, h3 { font-weight: bold; margin-top: 20px; margin-bottom: 10px; }";
    html += "h1 { font-size: 1.4em; }";
    html += "h2 { font-size: 1.2em; }";
    html += "h3 { font-size: 1.1em; }";
    html += "h4 { font-weight: bold; font-size: 1.0em; margin-top: 12px; "
            "margin-bottom: 4px; }";
    html += "table { margin: 10px 0; border-collapse: collapse; "
            "background-color: transparent; }";
    html += "tr { background-color: transparent; }";
    html += "th { background-color: " + rowHighlightColor + "; font-weight: bold; "
            "border: 1px solid #555; }";
    html += "li { margin: 1px 0; line-height: 1.2; }";
    html += "strong { font-weight: bold; }";
    html += ".dignity-list { margin: 4px 0; }";
    html += ".dignity-list p { margin: 1px 0; padding: 0; line-height: 1.1; }";
    html += "</style>";
    html += "</head><body>";

    auto scope = file()->horoscope();

    // Chart calculation summary (replaces the old loud "<zodiac> sign" banner).
    {
        html += "<h3>" + QObject::tr("Chart Calculation") + "</h3>";
        html += "<p>";
        if (scope.zodiac.id == A::Zodiac_Tropical) {
            html += "<strong>" + QObject::tr("Zodiac:") + "</strong> "
                    + QObject::tr("Tropical");
        } else {
            html += "<strong>" + QObject::tr("Ayanamsha:") + "</strong> "
                    + scope.zodiac.name;
        }
        html += "<br><strong>" + QObject::tr("Aspect set:") + "</strong> "
                + file()->getAspectSet().name;
        QString aspMode = A::usePrimeVerticalDisplay
                              ? QObject::tr("Prime Vertical")
                              : A::aspectMode.asUserString();
        html += "<br><strong>" + QObject::tr("Aspect mode:") + "</strong> "
                + aspMode;
        html += "</p>";
    }

    // Display input data for selected charts
    if (articles & A::Article_Input) {
        if (filesCount() == 1) {
            html += A::describeInput(scope.inputData);
        } else if (filesCount() > 1) {
            // Display Chart #1
            if (showFirst) {
                html += "<h2>"
                        + QObject::tr("Chart #1: %1").arg(file(0)->getName())
                        + "</h2>";
                html += A::describeInput(file(0)->horoscope().inputData);
            }

            // Display Chart #2
            if (showSecond) {
                html += "<h2>"
                        + QObject::tr("Chart #2: %1").arg(file(1)->getName())
                        + "</h2>";
                html += A::describeInput(file(1)->horoscope().inputData);
            }
        }
    }

    // Display planets for selected charts
    if (articles & A::Article_Planet) {
        if (filesCount() == 1 && scope.planets.count()) {
            html += "<h2>" + QObject::tr("Planets") + "</h2>";
            html +=
                "<table class='planets-table' style='border-collapse: " "collap" "se; " "width:" " 100%;" "'>";
            html += "<tr style='background-color: " + rowHighlightColor + ";'>";
            html += "<th style='padding: 4px 8px; text-align: left;'>"
                    + QObject::tr("Planet") + "</th>";
            html += "<th style='padding: 4px 8px; text-align: right;'>"
                    + QObject::tr("Position") + "</th>";
            html += "<th style='padding: 4px 8px; text-align: center;'>"
                    + QObject::tr("House") + "</th>";
            html += "<th style='padding: 4px 8px; text-align: center;'>"
                    + QObject::tr("Speed") + "</th>";
            html += "<th style='padding: 4px 8px; text-align: center;'>"
                    + QObject::tr("Power") + "</th>";
            html += "<th style='padding: 4px 8px;'>" + QObject::tr("Ruler of")
                    + "</th>";
            html += "<th style='padding: 4px 8px;'>" + QObject::tr("Status")
                    + "</th>";
            html += "</tr>";

            foreach (const A::Planet& p, scope.planets)
                html += A::describePlanet(p, scope.zodiac);

            html += "</table>";
        } else if (filesCount() > 1) {
            // Chart #1 Planets
            if (showFirst) {
                auto scope1 = file(0)->horoscope();
                if (scope1.planets.count()) {
                    html += "<h2>"
                            + QObject::tr("Planets - Chart #1: %1")
                                  .arg(file(0)->getName())
                            + "</h2>";
                    html +=
                        "<table class='planets-table' " "style='border-" "colla"
                                                                         "pse: "
                                                                         "colla"
                                                                         "pse; " "width: 100%;'>";
                    html +=
                        "<tr style='background-color: " + rowHighlightColor + ";'>";
                    html += "<th style='padding: 4px 8px; text-align: left;'>"
                            + QObject::tr("Planet") + "</th>";
                    html += "<th style='padding: 4px 8px; text-align: right;'>"
                            + QObject::tr("Position") + "</th>";
                    html += "<th style='padding: 4px 8px; text-align: center;'>"
                            + QObject::tr("House") + "</th>";
                    html += "<th style='padding: 4px 8px; text-align: center;'>"
                            + QObject::tr("Speed") + "</th>";
                    html += "<th style='padding: 4px 8px; text-align: center;'>"
                            + QObject::tr("Power") + "</th>";
                    html += "<th style='padding: 4px 8px;'>"
                            + QObject::tr("Ruler of") + "</th>";
                    html += "<th style='padding: 4px 8px;'>"
                            + QObject::tr("Status") + "</th>";
                    html += "</tr>";

                    foreach (const A::Planet& p, scope1.planets)
                        html += A::describePlanet(p, scope1.zodiac);

                    html += "</table>";
                }
            }

            // Chart #2 Planets
            if (showSecond) {
                auto scope2 = file(1)->horoscope();
                if (scope2.planets.count()) {
                    html += "<h2>"
                            + QObject::tr("Planets - Chart #2: %1")
                                  .arg(file(1)->getName())
                            + "</h2>";
                    html +=
                        "<table class='planets-table' " "style='border-" "colla"
                                                                         "pse: "
                                                                         "colla"
                                                                         "pse; " "width: 100%;'>";
                    html +=
                        "<tr style='background-color: " + rowHighlightColor + ";'>";
                    html += "<th style='padding: 4px 8px; text-align: left;'>"
                            + QObject::tr("Planet") + "</th>";
                    html += "<th style='padding: 4px 8px; text-align: right;'>"
                            + QObject::tr("Position") + "</th>";
                    html += "<th style='padding: 4px 8px; text-align: center;'>"
                            + QObject::tr("House") + "</th>";
                    html += "<th style='padding: 4px 8px; text-align: center;'>"
                            + QObject::tr("Speed") + "</th>";
                    html += "<th style='padding: 4px 8px; text-align: center;'>"
                            + QObject::tr("Power") + "</th>";
                    html += "<th style='padding: 4px 8px;'>"
                            + QObject::tr("Ruler of") + "</th>";
                    html += "<th style='padding: 4px 8px;'>"
                            + QObject::tr("Status") + "</th>";
                    html += "</tr>";

                    foreach (const A::Planet& p, scope2.planets)
                        html += A::describePlanet(p, scope2.zodiac);

                    html += "</table>";
                }
            }
        }
    }

    // Display houses for selected charts
    if (articles & A::Article_Houses) {
        if (filesCount() == 1 && scope.houses.system) {
            html +=
                A::describeHouses(scope.houses, scope.zodiac, scope.planets);
        } else if (filesCount() > 1) {
            // Chart #1 Houses
            if (showFirst) {
                auto scope1 = file(0)->horoscope();
                if (scope1.houses.system) {
                    html +=
                        "<h3>"
                        + QObject::tr("Chart #1: %1").arg(file(0)->getName())
                        + "</h3>";
                    html += A::describeHouses(scope1.houses,
                                              scope1.zodiac,
                                              scope1.planets);
                }
            }

            // Chart #2 Houses
            if (showSecond) {
                auto scope2 = file(1)->horoscope();
                if (scope2.houses.system) {
                    html +=
                        "<h3>"
                        + QObject::tr("Chart #2: %1").arg(file(1)->getName())
                        + "</h3>";
                    html += A::describeHouses(scope2.houses,
                                              scope2.zodiac,
                                              scope2.planets);
                }
            }
        }
    }

    // Display aspects
    if (articles & A::Article_Aspects) {
        if (filesCount() == 1 && cachedChart1Aspects.count()) {
            html +=
                A::describeAspectsTable(cachedChart1Aspects, aspectSortOrder);
        } else if (filesCount() > 1 && showFirst && showSecond) {
            // Display synastry aspects only when both charts are shown
            if (cachedSynastryAspects.count()) {
                html += "<h2>" + QObject::tr("Synastry Aspects") + "</h2>";
                html += "<p>"
                        + QObject::tr("Between Chart #1 (%1) and Chart #2 (%2)")
                              .arg(file(0)->getName())
                              .arg(file(1)->getName())
                        + "</p>";
                html += A::describeAspectsTable(cachedSynastryAspects,
                                                aspectSortOrder);
            }
        } else if (filesCount() > 1) {
            // Show individual chart aspects when only one chart is selected
            if (showFirst) {
                if (cachedChart1Aspects.count()) {
                    html += "<h2>"
                            + QObject::tr("Aspects - Chart #1: %1")
                                  .arg(file(0)->getName())
                            + "</h2>";
                    html += A::describeAspectsTable(cachedChart1Aspects,
                                                    aspectSortOrder);
                }
            }
            if (showSecond) {
                if (cachedChart2Aspects.count()) {
                    html += "<h2>"
                            + QObject::tr("Aspects - Chart #2: %1")
                                  .arg(file(1)->getName())
                            + "</h2>";
                    html += A::describeAspectsTable(cachedChart2Aspects,
                                                    aspectSortOrder);
                }
            }
        }
    }

    // Display planetary dignities/power for selected charts
    if (articles & A::Article_Power) {
        if (filesCount() == 1 && scope.planets.count()) {
            html += "<h2>" + QObject::tr("Planetary Dignities") + "</h2>";
            foreach (const A::Planet& p, scope.planets) {
                if (p.isReal) {
                    html += "<h4>" + p.name + "</h4>";
                    html += "<div class='dignity-list'>"
                            + A::describePowerInHtml(p, scope) + "</div>";
                }
            }
        } else if (filesCount() > 1) {
            // Chart #1 Dignities
            if (showFirst) {
                auto scope1 = file(0)->horoscope();
                if (scope1.planets.count()) {
                    html += "<h2>"
                            + QObject::tr("Planetary Dignities - Chart #1: %1")
                                  .arg(file(0)->getName())
                            + "</h2>";
                    foreach (const A::Planet& p, scope1.planets) {
                        if (p.isReal) {
                            html += "<h4>" + p.name + "</h4>";
                            html += "<div class='dignity-list'>"
                                    + A::describePowerInHtml(p, scope1)
                                    + "</div>";
                        }
                    }
                }
            }

            // Chart #2 Dignities
            if (showSecond) {
                auto scope2 = file(1)->horoscope();
                if (scope2.planets.count()) {
                    html += "<h2>"
                            + QObject::tr("Planetary Dignities - Chart #2: %1")
                                  .arg(file(1)->getName())
                            + "</h2>";
                    foreach (const A::Planet& p, scope2.planets) {
                        if (p.isReal) {
                            html += "<h4>" + p.name + "</h4>";
                            html += "<div class='dignity-list'>"
                                    + A::describePowerInHtml(p, scope2)
                                    + "</div>";
                        }
                    }
                }
            }
        }
    }

    // Display parans for selected charts
    if (articles & A::Article_Parans) {
        if (filesCount() == 1 && scope.planets.count()) {
            html += "<h2>" + QObject::tr("Directions") + "</h2>";
            html += A::describeParans(files(),
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      nullptr);
        } else if (filesCount() > 1) {
            // Chart #1 Parans
            if (showFirst && file(0) && file(0)->horoscope().planets.count()) {
                html += "<h2>"
                        + QObject::tr("Directions - Chart #1: %1")
                              .arg(file(0)->getName())
                        + "</h2>";
                AstroFileList file1List;
                file1List.append(file(0));
                html +=
                    A::describeParans(file1List,
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      nullptr);
            }

            // Chart #2 Parans — pass file(0) as natal context for Par=N filtering
            if (showSecond && file(1) && file(1)->horoscope().planets.count()) {
                html += "<h2>"
                        + QObject::tr("Directions - Chart #2: %1")
                              .arg(file(1)->getName())
                        + "</h2>";
                AstroFileList file2List;
                file2List.append(file(1));
                html +=
                    A::describeParans(file2List,
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      file(0));
            }
        }
    }

    // Display paran-latitudes table for selected charts
    if (articles & A::Article_ParanLatitudes) {
        if (filesCount() == 1 && scope.planets.count()) {
            html += "<h2>" + QObject::tr("Parans") + "</h2>";
            html += A::describeParanLatitudes(scope,
                                              paranOrb,
                                              paranCityLatTol,
                                              paranMaxCitiesPerRow,
                                              paranShowAbsent,
                                              paranCityPopMask,
                                              paranCityContinentMask,
                                              displayMode);
        } else if (filesCount() > 1) {
            if (showFirst && file(0) && file(0)->horoscope().planets.count()) {
                html += "<h2>"
                        + QObject::tr("Parans - Chart #1: %1")
                              .arg(file(0)->getName())
                        + "</h2>";
                html += A::describeParanLatitudes(file(0)->horoscope(),
                                                  paranOrb,
                                                  paranCityLatTol,
                                                  paranMaxCitiesPerRow,
                                                  paranShowAbsent,
                                                  paranCityPopMask,
                                                  paranCityContinentMask,
                                                  displayMode);
            }
            if (showSecond && file(1) && file(1)->horoscope().planets.count()) {
                html += "<h2>"
                        + QObject::tr("Parans - Chart #2: %1")
                              .arg(file(1)->getName())
                        + "</h2>";
                const A::Horoscope* natalCtx =
                    (showParanNatalRows && file(0)
                     && file(0)->horoscope().planets.count())
                    ? &file(0)->horoscope()
                    : nullptr;
                // For Chart #2 we treat file(1) as the *transit* context and
                // file(0) as the natal source.  Pass natal as the first arg
                // (its planets supply the ex-precessed natal participant)
                // and the transit horoscope as the optional transitCtx.
                if (natalCtx) {
                    html += A::describeParanLatitudes(*natalCtx,
                                                      paranOrb,
                                                      paranCityLatTol,
                                                      paranMaxCitiesPerRow,
                                                      paranShowAbsent,
                                                      paranCityPopMask,
                                                      paranCityContinentMask,
                                                      displayMode,
                                                      &file(1)->horoscope());
                } else {
                    html += A::describeParanLatitudes(file(1)->horoscope(),
                                                      paranOrb,
                                                      paranCityLatTol,
                                                      paranMaxCitiesPerRow,
                                                      paranShowAbsent,
                                                      paranCityPopMask,
                                                      paranCityContinentMask,
                                                      displayMode);
                }
            }
        }
    }

    // Display speculum for selected charts
    if (articles & A::Article_Speculum) {
        if (filesCount() == 1 && scope.planets.count()) {
            html += A::describeSpeculum(scope,
                                        bool(articles & A::Article_FixedStars),
                                        displayMode);
        } else if (filesCount() > 1) {
            // Chart #1 Speculum
            if (showFirst) {
                auto scope1 = file(0)->horoscope();
                if (scope1.planets.count()) {
                    html +=
                        "<h3>"
                        + QObject::tr("Chart #1: %1").arg(file(0)->getName())
                        + "</h3>";
                    html += A::describeSpeculum(
                        scope1,
                        bool(articles & A::Article_FixedStars),
                        displayMode);
                }
            }

            // Chart #2 Speculum
            if (showSecond) {
                auto scope2 = file(1)->horoscope();
                if (scope2.planets.count()) {
                    html +=
                        "<h3>"
                        + QObject::tr("Chart #2: %1").arg(file(1)->getName())
                        + "</h3>";
                    html += A::describeSpeculum(
                        scope2,
                        bool(articles & A::Article_FixedStars),
                        displayMode);
                }
            }
        }
    }

    html += "</body></html>";
    view->setHtml(html);

    // Restore scroll position
    vScrollBar->setValue(scrollPos);
}

AppSettings
Plain::defaultSettings()
{
    AppSettings s;
    s.setValue("Text/describeInput", false);
    s.setValue("Text/describePlanets", true);
    s.setValue("Text/describeHouses", true);
    s.setValue("Text/describeAspects", true);
    s.setValue("Text/describePower", false);
    s.setValue("Text/describeParans", true);
    s.setValue("Text/describeSpeculum", false);
    s.setValue("Text/describeParanLats", false);
    s.setValue("Plain/chart1Visible", true);  // Default to showing both
    s.setValue("Plain/chart2Visible", true);  // Default to showing both
    s.setValue("Mundane/displayMode", unsigned(A::DisplayLocalTime));
    s.setValue("Mundane/primDirMode", unsigned(A::prdMundane));
    s.setValue("Mundane/showAllDiurnalEvents", false);
    s.setValue("Mundane/paranOrb", 1.0);
    s.setValue("Mundane/paranCityLatTol", 0.5);
    s.setValue("Mundane/paranMaxCitiesPerRow", 8);
    s.setValue("Mundane/paranShowAbsent", true);
    s.setValue("Mundane/paranCity_PopSmall",  true);
    s.setValue("Mundane/paranCity_PopMedium", true);
    s.setValue("Mundane/paranCity_PopLarge",  true);
    s.setValue("Mundane/paranCity_PopHuge",   true);
    s.setValue("Mundane/paranCity_ContAfrica",       true);
    s.setValue("Mundane/paranCity_ContAsia",         true);
    s.setValue("Mundane/paranCity_ContEurope",       true);
    s.setValue("Mundane/paranCity_ContNorthAmerica", true);
    s.setValue("Mundane/paranCity_ContSouthAmerica", true);
    s.setValue("Mundane/paranCity_ContOceania",      true);
    s.setValue("Mundane/paranCity_ContAntarctica",   true);
    s.setValue("Mundane/includeFixedStars", true);
    s.setValue("Mundane/showParanNatalRows", false);
    s.setValue("Mundane/dirMethodSolarReturn", unsigned(A::DirNeoPSSR));
    s.setValue("Mundane/dirMethodOther",       unsigned(A::DirNeoSQ));
    s.setValue("Text/aspectSortOrder", unsigned(A::SortByPlanets));
    return s;
}

AppSettings
Plain::currentSettings()
{
    AppSettings s;
    s.setValue("Text/describeInput", describeInput->isChecked());
    s.setValue("Text/describePlanets", describePlanets->isChecked());
    s.setValue("Text/describeHouses", describeHouses->isChecked());
    s.setValue("Text/describeAspects", describeAspects->isChecked());
    s.setValue("Text/describePower", describePower->isChecked());
    s.setValue("Text/describeParans", describeParans->isChecked());
    s.setValue("Text/describeSpeculum", describeSpeculum->isChecked());
    s.setValue("Text/describeParanLats", describeParanLats->isChecked());
    s.setValue("Plain/chart1Visible", chart1Btn->isChecked());
    s.setValue("Plain/chart2Visible", chart2Btn->isChecked());
    s.setValue("Mundane/displayMode",
               unsigned(displayModeSelector->currentData().toInt()));
    s.setValue("Mundane/primDirMode", unsigned(A::primDirMode));
    s.setValue("Mundane/showAllDiurnalEvents", showAllDiurnalEvents);
    s.setValue("Mundane/paranOrb", paranOrb);
    s.setValue("Mundane/paranCityLatTol", paranCityLatTol);
    s.setValue("Mundane/paranMaxCitiesPerRow", paranMaxCitiesPerRow);
    s.setValue("Mundane/paranShowAbsent", paranShowAbsent);
    s.setValue("Mundane/paranCity_PopSmall",  bool(paranCityPopMask & A::CityPop_Small));
    s.setValue("Mundane/paranCity_PopMedium", bool(paranCityPopMask & A::CityPop_Medium));
    s.setValue("Mundane/paranCity_PopLarge",  bool(paranCityPopMask & A::CityPop_Large));
    s.setValue("Mundane/paranCity_PopHuge",   bool(paranCityPopMask & A::CityPop_Huge));
    s.setValue("Mundane/paranCity_ContAfrica",       bool(paranCityContinentMask & A::CityCont_Africa));
    s.setValue("Mundane/paranCity_ContAsia",         bool(paranCityContinentMask & A::CityCont_Asia));
    s.setValue("Mundane/paranCity_ContEurope",       bool(paranCityContinentMask & A::CityCont_Europe));
    s.setValue("Mundane/paranCity_ContNorthAmerica", bool(paranCityContinentMask & A::CityCont_NorthAmerica));
    s.setValue("Mundane/paranCity_ContSouthAmerica", bool(paranCityContinentMask & A::CityCont_SouthAmerica));
    s.setValue("Mundane/paranCity_ContOceania",      bool(paranCityContinentMask & A::CityCont_Oceania));
    s.setValue("Mundane/paranCity_ContAntarctica",   bool(paranCityContinentMask & A::CityCont_Antarctica));
    s.setValue("Mundane/includeFixedStars", includeFixedStars);
    s.setValue("Mundane/showParanNatalRows", showParanNatalRows);
    s.setValue("Mundane/dirMethodSolarReturn", unsigned(A::dirMethodSolarReturn));
    s.setValue("Mundane/dirMethodOther",       unsigned(A::dirMethodOther));
    s.setValue("Text/aspectSortOrder", unsigned(aspectSortOrder));
    return s;
}

void
Plain::applySettings(const AppSettings& s)
{
    describeInput->setChecked(s.value("Text/describeInput").toBool());
    describePlanets->setChecked(s.value("Text/describePlanets").toBool());
    describeHouses->setChecked(s.value("Text/describeHouses").toBool());
    describeAspects->setChecked(s.value("Text/describeAspects").toBool());
    describePower->setChecked(s.value("Text/describePower").toBool());
    describeParans->setChecked(s.value("Text/describeParans").toBool());
    describeSpeculum->setChecked(s.value("Text/describeSpeculum").toBool());
    describeParanLats->setChecked(s.value("Text/describeParanLats").toBool());

    // Restore chart button states
    chart1Btn->setChecked(s.value("Plain/chart1Visible").toBool());
    chart2Btn->setChecked(s.value("Plain/chart2Visible").toBool());

    A::SpeculumDisplayMode loadedMode =
        A::SpeculumDisplayMode(s.value("Mundane/displayMode").toUInt());

    // Set combo box to the loaded display mode
    int index = displayModeSelector->findData(loadedMode);
    if (index >= 0) {
        displayModeSelector->setCurrentIndex(index);
    }

    // Check if primDirMode changed - if so, need to recalculate charts
    A::PrimDirMode newPrimDirMode =
        A::PrimDirMode(s.value("Mundane/primDirMode").toUInt());
    bool primDirModeChanged = (A::primDirMode != newPrimDirMode);
    A::primDirMode          = newPrimDirMode;

    showAllDiurnalEvents = s.value("Mundane/showAllDiurnalEvents").toBool();
    paranOrb             = s.value("Mundane/paranOrb").toDouble();
    paranCityLatTol      = s.value("Mundane/paranCityLatTol", 0.5).toDouble();
    paranMaxCitiesPerRow = s.value("Mundane/paranMaxCitiesPerRow", 8).toInt();
    paranShowAbsent      = s.value("Mundane/paranShowAbsent", true).toBool();
    paranCityPopMask = 0u;
    if (s.value("Mundane/paranCity_PopSmall",  true).toBool()) paranCityPopMask |= A::CityPop_Small;
    if (s.value("Mundane/paranCity_PopMedium", true).toBool()) paranCityPopMask |= A::CityPop_Medium;
    if (s.value("Mundane/paranCity_PopLarge",  true).toBool()) paranCityPopMask |= A::CityPop_Large;
    if (s.value("Mundane/paranCity_PopHuge",   true).toBool()) paranCityPopMask |= A::CityPop_Huge;
    paranCityContinentMask = 0u;
    if (s.value("Mundane/paranCity_ContAfrica",       true).toBool()) paranCityContinentMask |= A::CityCont_Africa;
    if (s.value("Mundane/paranCity_ContAsia",         true).toBool()) paranCityContinentMask |= A::CityCont_Asia;
    if (s.value("Mundane/paranCity_ContEurope",       true).toBool()) paranCityContinentMask |= A::CityCont_Europe;
    if (s.value("Mundane/paranCity_ContNorthAmerica", true).toBool()) paranCityContinentMask |= A::CityCont_NorthAmerica;
    if (s.value("Mundane/paranCity_ContSouthAmerica", true).toBool()) paranCityContinentMask |= A::CityCont_SouthAmerica;
    if (s.value("Mundane/paranCity_ContOceania",      true).toBool()) paranCityContinentMask |= A::CityCont_Oceania;
    if (s.value("Mundane/paranCity_ContAntarctica",   true).toBool()) paranCityContinentMask |= A::CityCont_Antarctica;
    includeFixedStars    = s.value("Mundane/includeFixedStars").toBool();
    showParanNatalRows   = s.value("Mundane/showParanNatalRows").toBool();
    aspectSortOrder =
        A::AspectSortOrder(s.value("Text/aspectSortOrder").toUInt());

    // Check if either derived-direction method changed
    A::DirMethod newDirSR =
        A::DirMethod(s.value("Mundane/dirMethodSolarReturn",
                             unsigned(A::DirNeoPSSR)).toUInt());
    A::DirMethod newDirOther =
        A::DirMethod(s.value("Mundane/dirMethodOther",
                             unsigned(A::DirNeoSQ)).toUInt());
    bool dirMethodChanged = (A::dirMethodSolarReturn != newDirSR)
                            || (A::dirMethodOther != newDirOther);
    A::dirMethodSolarReturn = newDirSR;
    A::dirMethodOther       = newDirOther;

    // If a method changed, clear cached direction contexts and recalculate every
    // eligible return/ingress chart (the display rebuilds the context lazily, but
    // recalc keeps dependent panels in sync).
    if (dirMethodChanged) {
        for (int i = 0; i < filesCount(); i++) {
            if (file(i) && A::classifyDirChart(file(i)) != A::DirChartNotEligible) {
                file(i)->clearPSSRContext();
                file(i)->calculate();
            }
        }
        aspectsCached = false;
    }

    // If primDirMode changed, recalculate all files to update transit times
    if (primDirModeChanged) {
        for (int i = 0; i < filesCount(); i++) {
            if (file(i)) {
                file(i)->calculate();
            }
        }
        aspectsCached = false;
    }

    refresh();
}

void
Plain::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Tables"));
    ed->addComboBox("Mundane/primDirMode",
                    tr("Speculum type"),
                    { { "Mundane", A::prdMundane },
                      { "Zodiacal", A::prdZodiacal },
                      { "Active", A::prdActive } });
    ed->addComboBox("Mundane/displayMode",
                    tr("Display mode"),
                    { { "Local Time", A::DisplayLocalTime },
                      { "Sidereal Time", A::DisplaySiderealTime },
                      { "Right Ascension", A::DisplayRightAscension } });
    ed->addCheckBox("Mundane/showAllDiurnalEvents",

                    tr("Show all planetary diurnal events"));
    ed->addDoubleSpinBox("Mundane/paranOrb",
                         tr("Orb for paranatellontas"),
                         1. / 60. /*1 minute*/,
                         5.0 /*5 degrees*/);
    ed->addDoubleSpinBox("Mundane/paranCityLatTol",
                         tr("Paran-latitude city tolerance (degrees)"),
                         0.05 /*~5.5 km*/,
                         5.0 /*~550 km*/);
    ed->addSpinBox("Mundane/paranMaxCitiesPerRow",
                   tr("Max cities listed per paran-latitude row"),
                   1,
                   30);
    ed->addCheckBox("Mundane/paranShowAbsent",
                    tr("Show paran latitudes that are not present in the natal chart"));
    ed->addCheckBox("Mundane/paranCity_PopSmall",
                    tr("Cities: small (15k–100k)"));
    ed->addCheckBox("Mundane/paranCity_PopMedium",
                    tr("Cities: medium (100k–500k)"));
    ed->addCheckBox("Mundane/paranCity_PopLarge",
                    tr("Cities: large (500k–2M)"));
    ed->addCheckBox("Mundane/paranCity_PopHuge",
                    tr("Cities: huge (>2M)"));
    ed->addCheckBox("Mundane/paranCity_ContAfrica",       tr("Cities in Africa"));
    ed->addCheckBox("Mundane/paranCity_ContAsia",         tr("Cities in Asia"));
    ed->addCheckBox("Mundane/paranCity_ContEurope",       tr("Cities in Europe"));
    ed->addCheckBox("Mundane/paranCity_ContNorthAmerica", tr("Cities in North America"));
    ed->addCheckBox("Mundane/paranCity_ContSouthAmerica", tr("Cities in South America"));
    ed->addCheckBox("Mundane/paranCity_ContOceania",      tr("Cities in Oceania"));
    ed->addCheckBox("Mundane/paranCity_ContAntarctica",   tr("Cities in Antarctica"));
    ed->addCheckBox("Mundane/includeFixedStars", tr("Include fixed stars"));
    ed->addCheckBox("Mundane/showParanNatalRows",
                    tr("Show natal ex-precessed positions in paran table"));
    ed->addComboBox("Mundane/dirMethodSolarReturn",
                    tr("Derived directions for Solar Returns"),
                    { { "None (Primary Directions)", unsigned(A::DirNone) },
                      { "PSSR (mean sun)",           unsigned(A::DirPSSR) },
                      { "NeoPSSR (apparent sun)",    unsigned(A::DirNeoPSSR) },
                      { "SQ (mean sun)",             unsigned(A::DirSQ) },
                      { "NeoSQ (apparent sun)",      unsigned(A::DirNeoSQ) } });
    ed->addComboBox("Mundane/dirMethodOther",
                    tr("Derived directions for other charts (lunar returns, ingresses)"),
                    { { "None (Primary Directions)", unsigned(A::DirNone) },
                      { "SQ (mean sun)",             unsigned(A::DirSQ) },
                      { "NeoSQ (apparent sun)",      unsigned(A::DirNeoSQ) } });
    ed->addComboBox("Text/aspectSortOrder",
                    tr("Sort aspects by"),
                    { { "Planet pairs", A::SortByPlanets },
                      { "Orb strength", A::SortByOrbStrength },
                      { "Aspect type", A::SortByAspectType } });
}

void
Plain::dragEnterEvent(QDragEnterEvent* event)
{
    qDebug() << "Plain::dragEnterEvent";
    // Accept drops from the chart list
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        qDebug() << "Plain accepting drag";
        event->acceptProposedAction();
    }
}

void
Plain::dragMoveEvent(QDragMoveEvent* event)
{
    // Accept drag move events
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void
Plain::dropEvent(QDropEvent* event)
{
    qDebug() << "Plain::dropEvent";
    
    // Extract file path from mime data
    QString filePath;
    
    if (event->mimeData()->hasText()) {
        filePath = event->mimeData()->text();
        qDebug() << "Plain drop text:" << filePath;
    }
    
    if (filePath.isEmpty() && event->mimeData()->hasUrls()) {
        QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            filePath = urls.first().toLocalFile();
            qDebug() << "Plain drop URL:" << filePath;
        }
    }
    
    if (!filePath.isEmpty()) {
        // Get parent SlideWidget and emit its chartDropped signal
        SlideWidget* slideWidget = qobject_cast<SlideWidget*>(parentWidget());
        if (slideWidget) {
            qDebug() << "Plain emitting chartDropped signal";
            emit slideWidget->chartDropped(filePath);
            event->acceptProposedAction();
        }
    }
}
