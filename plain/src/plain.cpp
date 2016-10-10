#include <QLabel>
#include <QFile>
#include <QCheckBox>
#include <QTextBrowser>
#include <QHBoxLayout>
#include <QDebug>
#include <Astroprocessor/Output>
#include "plain.h"


/* ================================== WIDGET ======================================== */

Plain              :: Plain               ( QWidget* parent ) : AstroFileHandler (parent)
 {
  QLabel* label1  = new QLabel(tr("Show:"));
  describeInput   = new QCheckBox(tr("input;"));
  describePlanets = new QCheckBox(tr("planets;"));
  describeHouses  = new QCheckBox(tr("houses;"));
  describeAspects = new QCheckBox(tr("aspects;"));
  describePower   = new QCheckBox(tr("affetic"));
  describeParans  = new QCheckBox(tr("parans"));
  describeSpeculum= new QCheckBox(tr("spec"));
  view            = new QTextBrowser();

  describeInput   -> setChecked(false);
  describePlanets -> setChecked(true);
  describeHouses  -> setChecked(true);
  describeAspects -> setChecked(true);
  describePower   -> setChecked(false);
  describeParans  -> setChecked(true);
  describeSpeculum-> setChecked(true);
  showAllDiurnalEvents = false;

  describeInput   -> setStatusTip(tr("Show input data"));
  describePlanets -> setStatusTip(tr("Show planets"));
  describeHouses  -> setStatusTip(tr("Show houses"));
  describeAspects -> setStatusTip(tr("Show aspects"));
  describePower   -> setStatusTip(tr("Show dignity and deficient points for each planet"));

  QHBoxLayout* l = new QHBoxLayout();
    l->addSpacerItem(new QSpacerItem(1,1,QSizePolicy::Expanding, QSizePolicy::Preferred));
    l->addWidget(label1);
    l->addWidget(describeInput);
    l->addWidget(describePlanets);
    l->addWidget(describeHouses);
    l->addWidget(describeAspects);
    l->addWidget(describePower);
    l->addWidget(describeParans);
    l->addWidget(describeSpeculum);

  QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,5,0,0);
    layout->setSpacing(0);
    layout->addLayout(l);
    layout->addWidget(view);


  connect(describeInput,   SIGNAL(toggled(bool)), this, SLOT(refresh()));
  connect(describePlanets, SIGNAL(toggled(bool)), this, SLOT(refresh()));
  connect(describeHouses,  SIGNAL(toggled(bool)), this, SLOT(refresh()));
  connect(describeAspects, SIGNAL(toggled(bool)), this, SLOT(refresh()));
  connect(describePower,   SIGNAL(toggled(bool)), this, SLOT(refresh()));
  connect(describeParans,  SIGNAL(toggled(bool)), this, SLOT(refresh()));
  connect(describeSpeculum,SIGNAL(toggled(bool)), this, SLOT(refresh()));

  QFile cssfile ( "plain/style.css" );
  cssfile.open  ( QIODevice::ReadOnly | QIODevice::Text );
  setStyleSheet  ( cssfile.readAll() );
 }

void Plain         :: filesUpdated(MembersList m)
 {
  if (!file())
   {
    view->clear();
    return;
   }

  if (m[0] == 0) return;

  refresh();
 }

void Plain         :: refresh()
 {
  qDebug() << "Plain::refresh";
  if (!file()) {
      return;
  }
  int articles = (A::Article_Input   * describeInput->isChecked())   |
          (A::Article_Planet  * describePlanets->isChecked()) |
          (A::Article_Houses  * describeHouses->isChecked())  |
          (A::Article_Aspects * describeAspects->isChecked()) |
          (A::Article_Power   * describePower->isChecked())   |
          (A::Article_Parans  * describeParans->isChecked())  |
          (A::Article_DiurnalEvents * showAllDiurnalEvents)   |
          (A::Article_Speculum* describeSpeculum->isChecked());

  view->setText(A::describe(file()->horoscope(), (A::Article)articles, paranOrb));
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
    s.setValue("Text/showAllDiurnalEvents", false);
    s.setValue("Text/paranOrb", 1.0);
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
    s.setValue("Text/showAllDiurnalEvents", showAllDiurnalEvents);
    s.setValue("Text/paranOrb", paranOrb);
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
    showAllDiurnalEvents = s.value("Text/showAllDiurnalEvents").toBool();
    paranOrb = s.value("Text/paranOrb").toDouble();
    //refreshAll();
}

void
Plain::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Text"));
    ed->addCheckBox("Text/showAllDiurnalEvents", tr("Show all planetary diurnal events"));
    ed->addDoubleSpinBox("Text/paranOrb", tr("Orb for paranatellontas"),
			 1./60. /*1 minute*/, 3.0 /*3 degrees*/);
}
