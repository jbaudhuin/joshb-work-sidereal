#include "plain.h"
#include <Astroprocessor/Output>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDebug>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QTextBrowser>
#include <QToolBar>
#include <QWidgetAction>

/* ================================== WIDGET
 * ======================================== */

Plain::Plain(QWidget* parent) : AstroFileHandler(parent)
{
    chartsCount = 0;

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

    describeSpeculum = toolbar->addAction(tr("Speculum"));
    describeSpeculum->setCheckable(true);
    describeSpeculum->setChecked(true);

    toolbar->addSeparator();

    // Create chart selector widget (only visible when multiple charts loaded)
    chartSelectorWidget              = new QWidget();
    QHBoxLayout* chartSelectorLayout = new QHBoxLayout(chartSelectorWidget);
    chartSelectorLayout->setContentsMargins(5, 0, 5, 0);
    chartSelectorLayout->addWidget(new QLabel(tr("Show:")));

    chartSelector  = new QButtonGroup(this);
    showChart1     = new QRadioButton(tr("Chart #1"));
    showChart2     = new QRadioButton(tr("Chart #2"));
    showBothCharts = new QRadioButton(tr("Both"));

    chartSelector->addButton(showChart1, 0);
    chartSelector->addButton(showChart2, 1);
    chartSelector->addButton(showBothCharts, 2);
    showBothCharts->setChecked(true);

    chartSelectorLayout->addWidget(showChart1);
    chartSelectorLayout->addWidget(showChart2);
    chartSelectorLayout->addWidget(showBothCharts);

    QWidgetAction* chartSelectorAction = new QWidgetAction(this);
    chartSelectorAction->setDefaultWidget(chartSelectorWidget);
    toolbar->addAction(chartSelectorAction);

    // Initially hide the chart selector
    chartSelectorWidget->setVisible(false);

    view = new QTextBrowser();

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
    connect(chartSelector, &QButtonGroup::idClicked, this, [this](int) {
        refresh();
    });

    QFile cssfile("plain/style.css");
    cssfile.open(QIODevice::ReadOnly | QIODevice::Text);
    setStyleSheet(cssfile.readAll());
}

void
Plain::filesUpdated(MembersList m)
{
    if (!file()) {
        view->clear();
        chartsCount = 0;
        // Hide chart selector when no files
        if (chartSelectorWidget) {
            chartSelectorWidget->setVisible(false);
        }
        return;
    }

    while (m.size() < filesCount()) m.append(AstroFile::Member());

    // Detect if the number of charts changed
    bool chartsCountChanged = (chartsCount != filesCount());
    chartsCount             = filesCount();

    // Show/hide chart selector based on number of files
    if (chartSelectorWidget) {
        chartSelectorWidget->setVisible(filesCount() > 1);
    }

    // If charts count changed or first member has changes, refresh
    if (chartsCountChanged || (m[0] != 0)) {
        refresh();
    }
}

void
Plain::refresh()
{
    qDebug() << "Plain::refresh";
    if (!file()) {
        return;
    }

    // Determine which charts to display
    bool showFirst  = true;
    bool showSecond = true;

    if (filesCount() > 1) {
        int selectedChart = chartSelector->checkedId();
        showFirst =
            (selectedChart == 0 || selectedChart == 2); // Chart #1 or Both
        showSecond =
            (selectedChart == 1 || selectedChart == 2); // Chart #2 or Both
    } else {
        showSecond = false; // Only one chart available
    }

    // Always calculate aspects to ensure they're available
    calculateAspects();

    int articles = (A::Article_Input * describeInput->isChecked())
                   | (A::Article_Planet * describePlanets->isChecked())
                   | (A::Article_Houses * describeHouses->isChecked())
                   | (A::Article_Aspects * describeAspects->isChecked())
                   | (A::Article_Power * describePower->isChecked())
                   | (A::Article_Parans * describeParans->isChecked())
                   | (A::Article_DiurnalEvents * showAllDiurnalEvents)
                   | (A::Article_Speculum * describeSpeculum->isChecked())
                   | (A::Article_FixedStars * includeFixedStars);

    // Build the HTML content with custom aspect sort order
    QString html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<style>";
    html +=
        "body { font-family: 'Consolas', 'Courier New', courier, 'DejaVu " "San"
                                                                           "s "
                                                                           "Mon"
                                                                           "o',"
                                                                           " '"
                                                                           "Luc"
                                                                           "ida"
                                                                           " Co"
                                                                           "nso"
                                                                           "le'"
                                                                           "; "
                                                                           "mar"
                                                                           "gin"
                                                                           ": "
                                                                           "10p"
                                                                           "x; "
                                                                           "col"
                                                                           "or:"
                                                                           " #"
                                                                           "b5b"
                                                                           "fdf"
                                                                           "; " "background-color: transparent; }";
    html +=
        "h1, h2, h3 { color: #e9e9e4; margin-top: 20px; margin-bottom: 10px; }";
    html += "h1 { font-size: 1.4em; }";
    html += "h2 { font-size: 1.2em; }";
    html += "h3 { font-size: 1.1em; }";
    html +=
        "h4 { color: #e9e9e4; font-size: 1.0em; margin-top: 12px; " "margin-"
                                                                    "bottom: "
                                                                    "4px; }";
    html +=
        "table { margin: 10px 0; border-collapse: collapse; " "background-"
                                                              "color: "
                                                              "transparent; }";
    html += "tr { background-color: transparent; }";
    html +=
        "th { background-color: rgba(255,255,255,0.1); font-weight: bold; " "co"
                                                                            "lo"
                                                                            "r:"
                                                                            " #"
                                                                            "e9"
                                                                            "e9"
                                                                            "e4"
                                                                            "; "
                                                                            "bo"
                                                                            "rd"
                                                                            "er"
                                                                            ": "
                                                                            "1p"
                                                                            "x "
                                                                            "so"
                                                                            "li"
                                                                            "d "
                                                                            "#5"
                                                                            "55"
                                                                            "; "
                                                                            "}";
    html += "li { margin: 1px 0; line-height: 1.2; }";
    html += "strong { color: #e9e9e4; }";
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
                "<table class='planets-table' style='border-collapse: " "collap"
                                                                        "se; "
                                                                        "width:"
                                                                        " 100%;"
                                                                        "'>";
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
                        "<table class='planets-table' " "style='border-"
                                                        "collapse: collapse; "
                                                        "width: 100%;'>";
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
                        "<table class='planets-table' " "style='border-"
                                                        "collapse: collapse; "
                                                        "width: 100%;'>";
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
        if (filesCount() == 1 && scope.aspects.count()) {
            html += A::describeAspectsTable(scope.aspects, aspectSortOrder);
        } else if (filesCount() > 1 && showFirst && showSecond) {
            // Display synastry aspects only when both charts are shown
            auto synastryAspects = calculateSynastryAspects();
            if (synastryAspects.count()) {
                html += "<h2>" + QObject::tr("Synastry Aspects") + "</h2>";
                html += "<p>"
                        + QObject::tr("Between Chart #1 (%1) and Chart #2 (%2)")
                              .arg(file(0)->getName())
                              .arg(file(1)->getName())
                        + "</p>";
                html +=
                    A::describeAspectsTable(synastryAspects, aspectSortOrder);
            }
        } else if (filesCount() > 1) {
            // Show individual chart aspects when only one chart is selected
            if (showFirst) {
                auto scope1 = file(0)->horoscope();
                if (scope1.aspects.count()) {
                    html += "<h2>"
                            + QObject::tr("Aspects - Chart #1: %1")
                                  .arg(file(0)->getName())
                            + "</h2>";
                    html += A::describeAspectsTable(scope1.aspects,
                                                    aspectSortOrder);
                }
            }
            if (showSecond) {
                auto scope2 = file(1)->horoscope();
                if (scope2.aspects.count()) {
                    html += "<h2>"
                            + QObject::tr("Aspects - Chart #2: %1")
                                  .arg(file(1)->getName())
                            + "</h2>";
                    html += A::describeAspectsTable(scope2.aspects,
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
                                      paranOrb);
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
                                      paranOrb);
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
                                      paranOrb);
            }
        }
    }

    // Display speculum for selected charts
    if (articles & A::Article_Speculum) {
        if (filesCount() == 1 && scope.planets.count()) {
            html += A::describeSpeculum(scope,
                                        bool(articles & A::Article_FixedStars));
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
                        bool(articles & A::Article_FixedStars));
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
                        bool(articles & A::Article_FixedStars));
                }
            }
        }
    }

    html += "</body></html>";
    view->setHtml(html);
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
    s.setValue("Text/primDirMode", unsigned(A::prdMundane));
    s.setValue("Text/showAllDiurnalEvents", false);
    s.setValue("Text/paranOrb", 1.0);
    s.setValue("Text/includeFixedStars", true);
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
    s.setValue("Text/primDirMode", unsigned(A::primDirMode));
    s.setValue("Text/showAllDiurnalEvents", showAllDiurnalEvents);
    s.setValue("Text/paranOrb", paranOrb);
    s.setValue("Text/includeFixedStars", includeFixedStars);
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
    A::primDirMode       = A::PrimDirMode(s.value("Text/primDirMode").toUInt());
    showAllDiurnalEvents = s.value("Text/showAllDiurnalEvents").toBool();
    paranOrb             = s.value("Text/paranOrb").toDouble();
    includeFixedStars    = s.value("Text/includeFixedStars").toBool();
    aspectSortOrder =
        A::AspectSortOrder(s.value("Text/aspectSortOrder").toUInt());

    refresh();
}

void
Plain::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Parans and Speculum"));
    ed->addComboBox("Text/primDirMode",
                    tr("Speculum type"),
                    { { "Mundane", A::prdMundane },
                      { "Zodiacal", A::prdZodiacal },
                      { "Active", A::prdActive } });
    ed->addCheckBox("Text/showAllDiurnalEvents",
                    tr("Show all planetary diurnal events"));
    ed->addDoubleSpinBox("Text/paranOrb",
                         tr("Orb for paranatellontas"),
                         1. / 60. /*1 minute*/,
                         5.0 /*5 degrees*/);
    ed->addCheckBox("Text/includeFixedStars", tr("Include fixed stars"));

    ed->addTab(tr("Aspects Table"));
    ed->addComboBox("Text/aspectSortOrder",
                    tr("Sort aspects by"),
                    { { "Planet pairs", A::SortByPlanets },
                      { "Orb strength", A::SortByOrbStrength },
                      { "Aspect type", A::SortByAspectType } });
}
