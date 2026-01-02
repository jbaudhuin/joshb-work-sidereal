#include "plain.h"
#include "../../zodiac/src/thememanager.h"
#include <Astroprocessor/Output>
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
#include <QToolBar>
#include <QVBoxLayout>
#include "../../zodiac/src/slidewidget.h"

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

    describeParans = toolbar->addAction(tr("Parans"));
    describeParans->setCheckable(true);
    describeParans->setChecked(true);

    describeSpeculum = toolbar->addAction(tr("Mundane"));
    describeSpeculum->setCheckable(true);
    describeSpeculum->setChecked(true);

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

    view = new QTextBrowser();
    view->setAcceptDrops(false); // Disable drops on the view so parent Plain widget handles them

    showAllDiurnalEvents = false;
    includeFixedStars    = true;
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
    connect(chart1Btn, &QPushButton::clicked, this, &Plain::refresh);
    connect(chart2Btn, &QPushButton::clicked, this, &Plain::refresh);
    connect(displayModeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refresh();
    });

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

    // Check if aspects need to be recalculated
    // Invalidate cache if chart data, zodiac, or aspect settings changed
    bool needsAspectUpdate = false;
    for (const auto& members : m) {
        if (members
            & (AstroFile::GMT | AstroFile::Location | AstroFile::HouseSystem
               | AstroFile::Zodiac | AstroFile::AspectSet
               | AstroFile::AspectMode))
        {
            needsAspectUpdate = true;
            break;
        }
    }

    if (chartsCountChanged || needsAspectUpdate) {
        aspectsCached = false;
    }

    // If charts count changed or first member has changes, refresh
    if (chartsCountChanged || (m[0] != 0)) {
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
                   | (A::Article_FixedStars * includeFixedStars);

    // Get theme-appropriate colors
    QString textColor = ThemeManager::instance().getTextColor();
    QString headingColor = ThemeManager::instance().getHeadingColor();

    // Build the HTML content with custom aspect sort order
    QString html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<style>";
    html += "body { font-family: 'Consolas', 'Courier New', courier, 'DejaVu "
            "Sans Mono', 'Lucida Console'; margin: 10px; color: " + textColor + "; "
            "background-color: transparent; }";
    html += "h1, h2, h3 { color: " + headingColor + "; margin-top: 20px; margin-bottom: 10px; }";
    html += "h1 { font-size: 1.4em; }";
    html += "h2 { font-size: 1.2em; }";
    html += "h3 { font-size: 1.1em; }";
    html += "h4 { color: " + headingColor + "; font-size: 1.0em; margin-top: 12px; "
            "margin-bottom: 4px; }";
    html += "table { margin: 10px 0; border-collapse: collapse; "
            "background-color: transparent; }";
    html += "tr { background-color: transparent; }";
    html += "th { background-color: rgba(255,255,255,0.1); font-weight: bold; "
            "color: " + headingColor + "; border: 1px solid #555; }";
    html += "li { margin: 1px 0; line-height: 1.2; }";
    html += "strong { color: " + headingColor + "; }";
    html += ".dignity-list { margin: 4px 0; }";
    html += ".dignity-list p { margin: 1px 0; padding: 0; line-height: 1.1; }";
    html += "</style>";
    html += "</head><body>";

    auto scope = file()->horoscope();
    html += "<h1>" + QObject::tr("%1 sign").arg(scope.zodiac.name) + "</h1>";

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
            html += "<tr style='background-color: rgba(255,255,255,0.1);'>";
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
                        "<tr style='background-color: rgba(255,255,255,0.1);'>";
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
                        "<tr style='background-color: rgba(255,255,255,0.1);'>";
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
            html += A::describeParans(files(),
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode);
        } else if (filesCount() > 1) {
            // Chart #1 Parans
            if (showFirst && file(0) && file(0)->horoscope().planets.count()) {
                html += "<h2>"
                        + QObject::tr("Parans - Chart #1: %1")
                              .arg(file(0)->getName())
                        + "</h2>";
                AstroFileList file1List;
                file1List.append(file(0));
                html +=
                    A::describeParans(file1List,
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode);
            }

            // Chart #2 Parans
            if (showSecond && file(1) && file(1)->horoscope().planets.count()) {
                html += "<h2>"
                        + QObject::tr("Parans - Chart #2: %1")
                              .arg(file(1)->getName())
                        + "</h2>";
                AstroFileList file2List;
                file2List.append(file(1));
                html +=
                    A::describeParans(file2List,
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode);
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
    s.setValue("Plain/chart1Visible", true);  // Default to showing both
    s.setValue("Plain/chart2Visible", true);  // Default to showing both
    s.setValue("Mundane/displayMode", unsigned(A::DisplayLocalTime));
    s.setValue("Mundane/primDirMode", unsigned(A::prdMundane));
    s.setValue("Mundane/showAllDiurnalEvents", false);
    s.setValue("Mundane/paranOrb", 1.0);
    s.setValue("Mundane/includeFixedStars", true);
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
    s.setValue("Plain/chart1Visible", chart1Btn->isChecked());
    s.setValue("Plain/chart2Visible", chart2Btn->isChecked());
    s.setValue("Mundane/displayMode",
               unsigned(displayModeSelector->currentData().toInt()));
    s.setValue("Mundane/primDirMode", unsigned(A::primDirMode));
    s.setValue("Mundane/showAllDiurnalEvents", showAllDiurnalEvents);
    s.setValue("Mundane/paranOrb", paranOrb);
    s.setValue("Mundane/includeFixedStars", includeFixedStars);
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
    includeFixedStars    = s.value("Mundane/includeFixedStars").toBool();
    aspectSortOrder =
        A::AspectSortOrder(s.value("Text/aspectSortOrder").toUInt());

    // If primDirMode changed, recalculate all files to update transit times
    if (primDirModeChanged) {
        for (int i = 0; i < filesCount(); i++) {
            if (file(i)) {
                file(i)->calculate();
            }
        }
        // Invalidate cached aspects since planets were recalculated
        aspectsCached = false;
    }

    refresh();
}

void
Plain::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Mundane"));
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
    ed->addCheckBox("Mundane/includeFixedStars", tr("Include fixed stars"));

    ed->addTab(tr("Aspects Table"));
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
