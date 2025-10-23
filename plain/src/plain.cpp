#include <QLabel>
#include <QFile>
#include <QCheckBox>
#include <QTextBrowser>
#include <QHBoxLayout>
#include <QDebug>
#include <Astroprocessor/Output>
#include "plain.h"


/* ================================== WIDGET ======================================== */

Plain::Plain(QWidget* parent) : AstroFileHandler(parent)
{
    QLabel* label1   = new QLabel(tr("Show:"));
    describeInput    = new QCheckBox(tr("input;"));
    describePlanets  = new QCheckBox(tr("planets;"));
    describeHouses   = new QCheckBox(tr("houses;"));
    describeAspects  = new QCheckBox(tr("aspects;"));
    describePower    = new QCheckBox(tr("aphetic"));
    describeParans   = new QCheckBox(tr("parans"));
    describeSpeculum = new QCheckBox(tr("spec"));
    view             = new QTextBrowser();

    describeInput->setChecked(false);
    describePlanets->setChecked(true);
    describeHouses->setChecked(true);
    describeAspects->setChecked(true);
    describePower->setChecked(false);
    describeParans->setChecked(true);
    describeSpeculum->setChecked(true);
    showAllDiurnalEvents = false;
    includeFixedStars    = true;
    aspectSortOrder      = A::SortByPlanets;

    describeInput->setStatusTip(tr("Show input data"));
    describePlanets->setStatusTip(tr("Show planets"));
    describeHouses->setStatusTip(tr("Show houses"));
    describeAspects->setStatusTip(tr("Show aspects"));
    describePower->setStatusTip(
        tr("Show dignity and deficient points for each planet"));

    QHBoxLayout* l = new QHBoxLayout();
    l->addSpacerItem(
        new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Preferred));
    l->addWidget(label1);
    l->addWidget(describeInput);
    l->addWidget(describePlanets);
    l->addWidget(describeHouses);
    l->addWidget(describeAspects);
    l->addWidget(describePower);
    l->addWidget(describeParans);
    l->addWidget(describeSpeculum);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 5, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(l);
    layout->addWidget(view);

    connect(describeInput, SIGNAL(toggled(bool)), this, SLOT(refresh()));
    connect(describePlanets, SIGNAL(toggled(bool)), this, SLOT(refresh()));
    connect(describeHouses, SIGNAL(toggled(bool)), this, SLOT(refresh()));
    connect(describeAspects, SIGNAL(toggled(bool)), this, SLOT(refresh()));
    connect(describePower, SIGNAL(toggled(bool)), this, SLOT(refresh()));
    connect(describeParans, SIGNAL(toggled(bool)), this, SLOT(refresh()));
    connect(describeSpeculum, SIGNAL(toggled(bool)), this, SLOT(refresh()));

    QFile cssfile("plain/style.css");
    cssfile.open(QIODevice::ReadOnly | QIODevice::Text);
    setStyleSheet(cssfile.readAll());
}

void
Plain::filesUpdated(MembersList m)
{
    if (!file()) {
        view->clear();
        return;
    }

    while (m.size() < filesCount()) m.append(AstroFile::Member());
    if (m[0] == 0) return;

    refresh();
}

void
Plain::refresh()
{
    qDebug() << "Plain::refresh";
    if (!file()) {
        return;
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
    html += "body { font-family: 'Consolas', 'Courier New', courier, 'DejaVu Sans Mono', 'Lucida Console'; margin: 10px; color: #b5bfdf; background-color: transparent; }";
    html += "h1, h2, h3 { color: #e9e9e4; margin-top: 20px; margin-bottom: 10px; }";
    html += "h1 { font-size: 1.4em; }";
    html += "h2 { font-size: 1.2em; }";
    html += "h3 { font-size: 1.1em; }";
    html += "h4 { color: #e9e9e4; font-size: 1.0em; margin-top: 12px; margin-bottom: 4px; }";
    html += "table { margin: 10px 0; border-collapse: collapse; background-color: transparent; }";
    html += "th { background-color: rgba(255,255,255,0.1); font-weight: bold; color: #e9e9e4; border: 1px solid #555; }";
    html += "td { border: 1px solid #555; color: #b5bfdf; }";
    html += "tr:nth-child(even) { background-color: rgba(255,255,255,0.05); }";
    html += ".planets-table td:first-child { font-weight: bold; color: #e9e9e4; }";
    html += "p { color: #b5bfdf; margin: 2px 0; line-height: 1.2; }";
    html += "ul { color: #b5bfdf; margin: 4px 0; }";
    html += "li { margin: 1px 0; line-height: 1.2; }";
    html += "strong { color: #e9e9e4; }";
    html += ".dignity-list { margin: 4px 0; }";
    html += ".dignity-list p { margin: 1px 0; padding: 0; line-height: 1.1; }";
    html += "</style>";
    html += "</head><body>";

    auto scope = file()->horoscope();
    html += "<h1>" + QObject::tr("%1 sign").arg(scope.zodiac.name) + "</h1>";

    if (articles & A::Article_Input)
        html += A::describeInput(scope.inputData);

    if ((articles & A::Article_Planet) && scope.planets.count()) {
        html += "<h2>" + QObject::tr("Planets") + "</h2>";
        html += "<table class='planets-table' style='border-collapse: collapse; width: 100%;'>";
        html += "<tr style='background-color: rgba(255,255,255,0.1);'>";
        html += "<th style='padding: 4px 8px; text-align: left;'>" + QObject::tr("Planet") + "</th>";
        html += "<th style='padding: 4px 8px; text-align: right;'>" + QObject::tr("Position") + "</th>";
        html += "<th style='padding: 4px 8px; text-align: center;'>" + QObject::tr("House") + "</th>";
        html += "<th style='padding: 4px 8px; text-align: center;'>" + QObject::tr("Speed") + "</th>";
        html += "<th style='padding: 4px 8px; text-align: center;'>" + QObject::tr("Power") + "</th>";
        html += "<th style='padding: 4px 8px;'>" + QObject::tr("Ruler") + "</th>";
        html += "<th style='padding: 4px 8px;'>" + QObject::tr("Status") + "</th>";
        html += "</tr>";
        
        foreach(const A::Planet& p, scope.planets)
            html += A::describePlanet(p, scope.zodiac);
        
        html += "</table>";
    }

    if ((articles & A::Article_Houses) && scope.houses.system)
        html += A::describeHouses(scope.houses, scope.zodiac, scope.planets);

    if ((articles & A::Article_Aspects) && scope.aspects.count()) {
        html += A::describeAspectsTable(scope.aspects, aspectSortOrder);
    }

    if ((articles & A::Article_Power) && scope.planets.count()) {
        html += "<h2>" + QObject::tr("Planetary Dignities") + "</h2>";
        foreach(const A::Planet& p, scope.planets) {
            if (p.isReal) {
                html += "<h4>" + p.name + "</h4>";
                html += "<div class='dignity-list'>" + A::describePowerInHtml(p, scope) + "</div>";
            }
        }
    }

    if ((articles & A::Article_Parans) && scope.planets.count()) {
        html += A::describeParans(files(),
                              bool(articles & A::Article_DiurnalEvents),
                              bool(articles & A::Article_FixedStars),
                              paranOrb);
    }

    if ((articles & A::Article_Speculum) && scope.planets.count()) {
        html += A::describeSpeculum(scope, bool(articles & A::Article_FixedStars));
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
    aspectSortOrder      = A::AspectSortOrder(s.value("Text/aspectSortOrder").toUInt());

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
