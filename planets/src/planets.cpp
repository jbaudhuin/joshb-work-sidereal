﻿#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QRegularExpression>
#include <QQmlContext>
#include <QQuickItem>
#include <Astroprocessor/Calc>
#include <QDir>
#include <qqmlfile.h>
#include "planets.h"

/* =========================== ASTRO QML VIEW ======================================= */

AstroQmlView :: AstroQmlView (QWindow* parent) : QQuickView(parent)
 {
  rootContext()->setContextProperty("view", this);       // enables calling setHelpTag() procedure from qml;
  setResizeMode(QQuickView::SizeRootObjectToView); // prevent unnecessary resizing of widget
 }

void AstroQmlView :: fitSceneInView()
{
    //fitInView(QRect(QPoint(0, 0), initialSize()), Qt::KeepAspectRatio);
    QSizeF newSize(initialSize());
    qreal hscale = size().width() / newSize.width();
    qreal vscale = size().height() / newSize.height();
    rootObject()->setTransformOrigin(QQuickItem::TopLeft);
    rootObject()->setSize(newSize);
    rootObject()->setScale(qMin(hscale, vscale));
    //setResizeMode(QQuickView::SizeViewToRootObject);
}

void AstroQmlView :: setSource(const QUrl& url)
 {
  QQuickView::setSource(url);
  fitSceneInView();                             // update content size after loading scene
 }


/* =========================== ASTRO PLANETS SHOW =================================== */

Planets :: Planets(QWidget *parent) : AstroFileHandler(parent)
 {
  QLabel* label1 = new QLabel(tr("Sort by:"));
  sortByPlanets  = new QRadioButton(tr("planet"));
  sortByHouses   = new QRadioButton(tr("house"));
  sortByPower    = new QRadioButton(tr("dignity"));
  sortByElement  = new QRadioButton(tr("element"));
  view           = new AstroQmlView;

  //view          -> setAlignment(Qt::AlignTop);
  sortByPlanets -> setChecked(true);

  QHBoxLayout* l1 = new QHBoxLayout();
    l1->addSpacerItem(new QSpacerItem(1,1,QSizePolicy::Expanding, QSizePolicy::Preferred));
    l1->addWidget(label1);
    l1->addWidget(sortByPlanets);
    l1->addWidget(sortByHouses);
    l1->addWidget(sortByPower);
    l1->addWidget(sortByElement);
    l1->addSpacing(10);

  QVBoxLayout* l3 = new QVBoxLayout(this);
    l3->setContentsMargins(0,5,0,0);
    l3->addLayout(l1);
    l3->addWidget(QWidget::createWindowContainer(view));

  connect(sortByPlanets,  SIGNAL(released()), this, SLOT(orderChanged()));
  connect(sortByHouses,   SIGNAL(released()), this, SLOT(orderChanged()));
  connect(sortByPower,    SIGNAL(released()), this, SLOT(orderChanged()));
  connect(sortByElement,  SIGNAL(released()), this, SLOT(orderChanged()));
  connect(view,           SIGNAL(objectClicked(QString)), this, SLOT(objectClicked(QString)));
  connect(view,           SIGNAL(requestHelp(QString)), this, SIGNAL(requestHelp(QString)));  //  pass signal through
  //connect(planetViewer,   SIGNAL(planetChanged(A::PlanetId)), this, SLOT(highlightPlanet(A::PlanetId)));

  QFile cssfile ( "planets/style.css" );
  cssfile.open  ( QIODevice::ReadOnly | QIODevice::Text );
  setStyleSheet  ( cssfile.readAll() );
 }

void
Planets::drawPlanets()
{
    qDebug() << "Planets::drawPlanets";
    if (view->source().isEmpty())
    {
        QString abs(QDir::current().absoluteFilePath("planets/Planets.qml"));
        QUrl url(QUrl::fromLocalFile(abs));
        bool urlIsRel = url.isRelative();
        auto ulf = QQmlFile::urlToLocalFileOrQrc(url);
        bool qfEmpty = ulf.isEmpty();
        bool qdIsRel = QDir::isRelativePath(ulf);
        view->setSource(url);
        //auto items = view->rootObject()->findChildren<QQuickItem*>();
        cardItems  = view->rootObject()->findChildren<QQuickItem*>(QRegularExpression("card*"));   // select all items where
        labelItems = view->rootObject()->findChildren<QQuickItem*>(QRegularExpression("label*"));  // objectName = "card..." and "label..."
    }

    A::PlanetList planets = file()->horoscope().planets.values();   // create list of planets
    while(planets.count() > cardItems.count()) planets.removeLast();
    A::sortPlanets(planets, order);

    int i = 0;
    for (QQuickItem* item: cardItems) {
        if (i >= planets.count() || planets[i].getPlanetId() < A::Planet_Sun)
        {
            item->setVisible(false);	// ensure item hide
            continue;
        }

        const A::Planet& planet = planets[i];

        item->setVisible(true);	// ensure item visible
        item->setProperty("objectName",   QString("card_%1").arg(planet.id));
        item->setProperty("title",        planet.name.toUpper());
        item->setProperty("house",        A::houseNum(planet));
        item->setProperty("sign",         planet.sign->id);
        item->setProperty("degreeStr",    A::zodiacPosition(planet,file()->horoscope().zodiac).toLower());
        item->setProperty("isRetro",      planet.eclipticSpeed.x() < 0);
        item->setProperty("dignityVal",   planet.power.dignity);
        item->setProperty("deficientVal", planet.power.deficient);
        item->setProperty("imageTip",     planet.name + "+" + planet.sign->name);
        item->setProperty("imageSignTip", planet.sign->name);
        item->setProperty("degreeTip",    QString("%1°").arg((int)planet.eclipticPos.x()));

        if (!planet.houseRuler.isEmpty()) {
            QStringList rs;
            for (auto r: planet.houseRuler) rs << A::romanNum(r);
            auto rlr = rs.join("+");
            item->setProperty("ruler",   tr("ruler of %1").arg(rlr));
            item->setProperty("ruleTip",    planet.name + "+" + A::houseNum(planet) + "+" +
                              "ruler" + rs.join(":"));
        } else {
            item->setProperty("ruler", "");
        }

        // TODO: check through all data and verify a result
        QString imgSrc = "file:///" + QDir::currentPath() + '/' + planet.userData["iconHuge"].toString();

        if (planet.id == A::Planet_Moon)
        {
            int sunAngle = A::angle(planet, file()->horoscope().sun);

            for (int i = 0; i < 360; i+=30)
            {
                int i2 = (i+30) % 360;

                if (i <= sunAngle && i2 > sunAngle)
                {
                    if (qAbs(i-sunAngle) > qAbs(i2-sunAngle)) i = i2;
                    imgSrc = imgSrc.arg(i);
                }
            }
        }

        item->setProperty("imageSrc", imgSrc);


        /*if (planet.id == planetViewer->selectedPlanet())      // highlight current planet
      item->setScale(1.07);
    else
      item->setScale(1);*/


        QStringList labels;
        QString tip;
        if (planet.id == A::almuten(file()->horoscope())->id)
        {
            labels << tr("Almuten");
            tip = planet.name + "+" + tr("Almuten");
        }
        if (planet.id == A::auriga(file()->horoscope())->id)
        {
            labels << tr("Auriga");
            tip = planet.name + "+" + tr("Auriga");
        }
        if (planet.id == A::doryphoros(file()->horoscope())->id)
        {
            labels << tr("Doryphoros");
            tip = planet.name + "+" + tr("Doryphoros");
        }
        labelItems[i]->setProperty("text", labels.join(", "));
        labelItems[i]->setProperty("helpTip", tip);

        i++;
    }
}

void Planets :: filesUpdated(MembersList m)
{
    if (!filesCount())
    {
        view->setSource(QUrl());
        return;
    }

    if (m[0]) drawPlanets();
}

void Planets :: orderChanged()
 {
  if (sortByPlanets->isChecked())
    order = A::Order_NoOrder;
  else if (sortByHouses->isChecked())
    order = A::Order_House;
  else if (sortByPower->isChecked())
    order = A::Order_Power;
  else if (sortByElement->isChecked())
    order = A::Order_Element;

  drawPlanets();
 }

void Planets :: objectClicked(QString name)
 {
  foreach (QQuickItem* item, cardItems)   // highlight selected item
   {
    if (item->objectName() == name)
      item->setScale(1.07);
    else
      item->setScale(1);
   }

  A::PlanetId id = name.remove("card_").toInt();
  emit planetSelected(id, 0);
 }
