#include <QGraphicsView>
#include <QVBoxLayout>
#include <QEvent>
#include <QGraphicsSceneMouseEvent>
#include <QWheelEvent>
#include <QGraphicsDropShadowEffect>
#include <QDebug>
#include <math.h>
#include <Astroprocessor/Output>
#include <Astroprocessor/Calc>
#include "chart.h"
#include <swephexp.h>


RotatingCircleItem::RotatingCircleItem(QRect rect, const QPen& pen) : QAbstractGraphicsShapeItem()
{
    file = 0;
    this->rect = rect;
    setPen(pen);
}

void
RotatingCircleItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{                                            // simply draw a circle with assigned pen
    p->setPen(pen());
    int adjust = pen().width() / 2;
    p->drawEllipse(rect.adjusted(adjust, adjust, -adjust, -adjust));
}

QPainterPath
RotatingCircleItem::shape() const
{                                            // creates a ring shape
    QPainterPath path;
    path.addEllipse(boundingRect());

    QPainterPath innerPath;
    path.addEllipse(boundingRect().adjusted(pen().width(), pen().width(),
                                            -pen().width(), -pen().width()));

    return path.subtracted(innerPath);
}

float
RotatingCircleItem::angle(const QPointF& pos)
{
    QPointF center = boundingRect().center();

    float ret = atan((pos.y() - center.y()) /
                     (pos.x() - center.x())) * 180 / 3.1416;

    if (pos.x() > center.x())       // I, IV quadrant
        ret += 180;
    else if (pos.y() > center.y())  // III quadrant
        ret += 360;

    return ret;
}

Chart*
RotatingCircleItem::chart()
{
    return (Chart*)(scene()->views()[0]->parentWidget());   // ахтунг, быдлокод!
}

bool
RotatingCircleItem::sceneEvent(QEvent *event)
{
    if (!file) return false;

    if (event->type() == QEvent::GraphicsSceneMousePress) {
        dragAngle = angle(((QGraphicsSceneMouseEvent*)event)->scenePos());
        dragDT = file->getGMT();
        return true;
    } else if (event->type() == QEvent::GraphicsSceneMouseMove) {
        QGraphicsSceneMouseEvent* moveEvent = (QGraphicsSceneMouseEvent*)event;
        QPointF lastPos = moveEvent->lastScenePos();
        QPointF newPos  = moveEvent->scenePos();

        float lastAngle = angle(lastPos);
        float newAngle  = angle(newPos);

        // new angle turns 0/360
        if ((lastAngle < 10 && newAngle  > 350) ||
                (newAngle  < 10 && lastAngle > 350))
        {
            dragAngle = newAngle;
            dragDT = file->getGMT();
        }

        // fix rotate in wrong direction
        float k = newAngle - dragAngle;
        if (chart()->isClockwise()
                == (chart()->startPoint() == Start_Ascendent))
        {
            k = -k;
        }

        file->setGMT(dragDT.addSecs(k * 180));
        return true;
    }

    return false;                                           // pass event through
}

bool
RotatingCircleItem::sceneEventFilter(QGraphicsItem *watched,
                                     QEvent *event)
{
    if (event->type() == QEvent::GraphicsSceneHoverEnter) {
        // show help when mouse over item
        QString tag = watched->data(0).toString();
        chart()->help(tag);
        return true;
    } else if (event->type() == QEvent::GraphicsSceneMousePress) {
        // emit signal when item clicked
        if (watched->data(1).isNull()) return false;
        emit chart()->planetSelected(watched->data(1).toInt(),
                                     watched->data(2).toInt());
        return true;
    }

    return false;
}

void
RotatingCircleItem::setHelpTag(QGraphicsItem* item,
                               QString tag)
{
    // assigning help string and installing event handler on item
    item->setAcceptHoverEvents(true);
    item->installSceneEventFilter(this);      // to detect hover event
    item->setData(0, tag);
}


/* =========================== ASTRO MAP SHOW ======================================= */

Chart::Chart(QWidget *parent) : 
    AstroFileHandler(parent)
{
    chartsCount = 0;
    zoom = 1;

    float scale(0.8), sc2(0.5);
    viewport = QRect(chartRect().x() / scale,
                     chartRect().y() / scale,
                     chartRect().width() / scale,
                     chartRect().height() / scale);
    viewportBig = QRect(chartRect().x() / sc2,
                        chartRect().y() / sc2,
                        chartRect().width() / sc2,
                        chartRect().height() / sc2);

    view = new QGraphicsView(this);

    view->setScene(new QGraphicsScene());
    view->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    //view->installEventFilter(this);
    view->scene()->installEventFilter(this);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(QMargins(0,0,0,0));
    layout->addWidget(view);
}

void
Chart::fitInView()
{
    //QRect rect(chartRect.x() / zoom, chartRect.y() / zoom, chartRect.width() / zoom, chartRect.height() / zoom);
    view->fitInView(viewport, Qt::KeepAspectRatio);
}

void 
Chart::createScene()
{
    qDebug() << "Create scene";
    QGraphicsScene* s = view->scene();

    QBrush background(QColor(8, 103, 192, 50));
    QPen penZodiac(QColor(31, 52, 93), zodiacWidth());
    QPen penBorder(QColor(50, 145, 240));
    QPen penCircle(QColor(227, 214, 202), 1);
    QFont zodiacFont("Almagest", 15 * zoom, QFont::Bold);
    QColor signFillColor = Qt::black;
    QColor signShapeColor = "#6d6d6d";

    if (coloredZodiac) {
        QConicalGradient grad1(chartRect().center(), 180);
        QColor color;

        for (const A::ZodiacSign& sign : file()->horoscope().zodiac.signs)
        {
            color.setNamedColor(sign.userData["bgcolor"].toString());
            float a1 = sign.startAngle / 360;
            float a2 = sign.endAngle / 360 - 0.0001;

            if (clockwise) {
                a1 = 0.5 - a1; if (a1 < 0) a1 += 1;
                a2 = 0.5 - a2; if (a2 < 0) a2 += 1;
            }

            grad1.setColorAt(a1, color);
            grad1.setColorAt(a2, color);
        }

        penZodiac.setBrush(QBrush(grad1));
        penBorder.setColor(Qt::black);

    }

    for (int f = 0; f < filesCount(); f++) {                                                                            // inner circles
        s->addEllipse(-innerRadius(f), -innerRadius(f), 2 * innerRadius(f), 2 * innerRadius(f), penCircle);
        drawCuspides(f);                                                            // cuspides
    }

    s->addEllipse(chartRect().adjusted(2, 2, -2, -2), penBorder, background);        // fill background (with margin)

    circle = new RotatingCircleItem(chartRect(), penZodiac);                      // zodiac circle
    QCursor curs(QPixmap("chart/rotate.png"));
    circle->setCursor(curs);
    s->addItem(circle);
    s->addEllipse(chartRect(), penBorder)->setParentItem(circle);                 // zodiac outer border
    s->addEllipse(chartRect().adjusted(zodiacWidth(), zodiacWidth(),              // zodiac inner border
                                       -zodiacWidth(), -zodiacWidth()),
                  penBorder)->setParentItem(circle);

    if (zodiacDropShadow) {
        QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect;
        effect->setBlurRadius(zodiacWidth());
        effect->setOffset(0);
        effect->setColor(QColor(0, 0, 0, 150));
        circle->setGraphicsEffect(effect);
    }

    for (const A::ZodiacSign& sign : file()->horoscope().zodiac.signs) {
        float endAngle = sign.endAngle;
        if (sign.startAngle > endAngle) endAngle += 360;
        float rad = -sign.startAngle * 3.1416 / 180;
        float rad_mid = -(sign.startAngle + (endAngle - sign.startAngle) / 2)
                * 3.1416 / 180;

        if (clockwise) {
            rad = 3.1416 - rad;
            rad_mid = 3.1416 - rad_mid;
        }

        s->addLine(chartRect().x() * cos(rad),                               // zodiac sign borders
                   chartRect().y() * sin(rad),
                   (chartRect().x() + zodiacWidth()) * cos(rad),
                   (chartRect().y() + zodiacWidth()) * sin(rad), penBorder)->setParentItem(circle);

        QString ch = QChar(sign.userData["fontChar"].toInt());
        QGraphicsSimpleTextItem* text = s->addSimpleText(ch, zodiacFont); // zodiac sign icon
        text->setParentItem(circle);
        text->setBrush(coloredZodiac ? signFillColor : sign.userData["fillColor"].toString());
        text->setPen(coloredZodiac ? signShapeColor : sign.userData["shapeColor"].toString());
        text->setOpacity(0.9);
        text->moveBy((chartRect().x() + zodiacWidth() / 2) * cos(rad_mid) - text->boundingRect().width() / 2,
                     (chartRect().y() + zodiacWidth() / 2) * sin(rad_mid) - text->boundingRect().height() / 2);
        text->setTransformOriginPoint(text->boundingRect().center());
        circle->setHelpTag(text, sign.name);
        signIcons << text;
    }

    for (int i = 0; i < filesCount(); i++) {
        drawPlanets(i);
        drawStars(i);
    }

    /*if (viewport.center() != QPointF(0,0)) */fitInView();
    chartsCount = filesCount();
}

void 
Chart::updateScene()
{
    qDebug() << "Update scene";

    circle->setFile(file());
    float rotate;

    if (circleStart == Start_Ascendent) {
        const auto& houses = file()->horoscope().houses;
        switch (A::aspectMode) {
        case A::amcEquatorial:
            rotate = houses.RAAC;
            break;
        default:
        case A::amcEcliptic:
            rotate = houses.cusp[0];
            break;
        }
    } else {
        rotate = file()->horoscope().zodiac.signs[0].startAngle;
    }
    if (clockwise) rotate = -rotate;

    for (QGraphicsItem* i : signIcons)
        i->setRotation(-rotate);

    circle->setRotation(rotate);
}

void
Chart::updatePlanetsAndCusps(int fileIndex)
{
    qDebug() << "Update planets and cusps" << fileIndex;

    QMap<QGraphicsItem*, const A::Star*> ret;
    QMap<QGraphicsItem*, int> moved;

    auto overlap = [](QGraphicsItem* planet,
                      QGraphicsItem* other,
                      int rung)
    {
        // FIXME: should use some pi-based ratio to increase the
        // available space
        qreal width = planet->boundingRect().width() * (11-rung)/41;
        qreal pbeg = planet->rotation() - width/2;
        qreal pend = pbeg + width;
        width = other->boundingRect().width() * (11-rung)/41;
        qreal obeg = other->rotation() - width/2;
        qreal oend = obeg + width;
        if (pbeg > oend || obeg > pend || pend < obeg || oend < pbeg) {
            return false;
        }
        return true;
    };

    auto moveIfNeeded =
            [&ret, &moved, &overlap](QGraphicsItem* planet,
                                     QGraphicsItem* other)
    {
        if (other->isVisible()
                && moved.value(other, 1) == moved.value(planet, 1)
                && overlap(planet,other,moved.value(other,1)))
        {
            qDebug() << "  collision with" << ret[other]->name
                     << other->pos() << other->rotation()
                     << other->boundingRect().size();
            //planet->moveBy(-other->boundingRect().width(), 0);
            planet->moveBy(-20, 0);
            qDebug() << "    new pos" << planet->pos() << planet->boundingRect().size();
            moved[planet] = moved.value(other, 1) + 1;
            return true;
        }
        return false;
    };

    auto positive = [](qreal angle)
    { if (angle < 0) angle += 360.0; return angle; };

    auto rotate = circle->rotation();
    auto repose = [&](auto b, bool hide)
            -> std::pair<QGraphicsItem*,QGraphicsItem*>
    {
        qreal angle = 0.0;
        switch (A::aspectMode) {
        case A::amcEcliptic: angle = b.eclipticPos.x(); break;
        case A::amcEquatorial: angle = b.equatorialPos.x(); break;
        case A::amcPrimeVertical: angle = b.pvPos; break;
        default: hide = true; break;
        }

        QGraphicsItem* marker = planetMarkers[fileIndex][b.id];
        QGraphicsItem* body = planets[fileIndex][b.id];

        if (hide) {
            marker->setVisible(false);
            body->setVisible(false);
            return std::make_pair(body, marker);
        }

        marker->setVisible(true);
        body->setVisible(true);

        if (clockwise) angle = 180 - angle;

        body->setPos(normalPlanetPosX(body, marker), body->pos().y());
        body->setRotation( positive(angle - rotate) );
        marker->setRotation( positive(rotate - angle) );
        qDebug() << "planet 'name" << b.name << "id" << b.id
                 //<< reinterpret_cast<void*>(planet)
                 << "pos" << body->pos()
                 << "rot" << body->rotation()
                 << body->boundingRect().size();

        // avoid intersection of planets
        bool adjusted = false;
        do {
            for (auto other : ret.keys()) {
                if ((adjusted = moveIfNeeded(body, other))) break;
            }
        } while (adjusted);

        ret.insert(body, &b);

        return std::make_pair(body,marker);
    };

    QGraphicsItem *body, *marker;
    for (const A::Planet& p : file(fileIndex)->horoscope().planets) {
        // update planets
        if (p.id == A::Planet_Asc) continue;

        bool hide = (p.id == A::Planet_MC)
                    && (file(fileIndex)->getHarmonic() ==  1);

        std::tie(body, marker) = repose(p, hide);
        if (hide) continue;

        QString toolTip = QString("%1 %2, %3")
                .arg(p.name)
                .arg(A::zodiacPosition(p, file()->horoscope().zodiac,
                                       A::HighPrecision))
                .arg(A::houseNum(p));
        body->setToolTip(toolTip);
        marker->setToolTip(toolTip);
        if (p.sign) {
            circle->setHelpTag(body, p.name + "+" + p.sign->name);
            circle->setHelpTag(marker, p.name);
        }
    }

    for (const A::Star& s : file(fileIndex)->horoscope().stars) {
        bool hide = !s.isConfiguredWithPlanet();
        std::tie(body, marker) = repose(s, hide);
        if (hide) continue;

        QString toolTip = QString("%1 %2")
                .arg(s.name)
                .arg(A::zodiacPosition(s, file()->horoscope().zodiac,
                                       A::HighPrecision));
        body->setToolTip(toolTip);
        marker->setToolTip(toolTip);
        circle->setHelpTag(body, s.name);
        circle->setHelpTag(marker, s.name);
    }

    auto cuspate = [=](qreal cusp, int i) {
        if (clockwise) cusp = 180 - cusp;

        QGraphicsItem* c = cuspides[fileIndex][i];
        c->setVisible(true);
        QGraphicsItem* l = cuspideLabels[fileIndex][i];
        l->setVisible(true);

        c->setRotation(-cusp + rotate);
        l->setRotation(cusp - rotate);

        QString tag = tr("%1+%2").arg(A::romanNum(i + 1))
                      .arg(A::getSign(cusp, file()->horoscope().zodiac).name);
        circle->setHelpTag(c, tag);
        circle->setHelpTag(l, tag);

        c->setToolTip(tr("House %1<br>%2").arg(A::romanNum(i + 1))
                      .arg(A::zodiacPosition(cusp, file()->horoscope().zodiac)));
        l->setToolTip(c->toolTip());
    };

    switch (A::aspectMode) {
    case A::amcEquatorial:
        {
            const auto& houses = file(fileIndex)->horoscope().houses;
            for (int i = 0; i < 12; ++i) {
                cuspides[fileIndex][i]->setVisible(!(i%3));
            }
            auto cusp = houses.RAAC;
            cuspate(cusp, 0);
            cusp = swe_degnorm(cusp+180);
            cuspate(cusp, 6);
            if (file(fileIndex)->horoscope().inputData.harmonic == 1) {
                cusp = houses.RAMC;
                cuspate(cusp, 9);
                cusp = swe_degnorm(cusp+180);
                cuspate(cusp, 3);
            } else {
                cuspides[fileIndex][3]->setVisible(false);
                cuspides[fileIndex][9]->setVisible(false);
            }
        }
        break;

    case A::amcPrimeVertical:
        //break;

    default:
    case A::amcEcliptic:
        // update cuspides && labels
        for (int i = 0; i < 12; i++) {
            auto cusp = file(fileIndex)->horoscope().houses.cusp[i];
            cuspate(cusp, i);
        }
        break;
    }
}

void Chart::updateAspects()
{
    int i = 0;
    const A::AspectList& list = (filesCount() == 1
                                 ? file()->horoscope().aspects
                                 : calculateSynastryAspects());
    for (const A::Aspect& asp : list) {
        if ((asp.planet1->id == A::Planet_Asc
             || asp.planet2->id == A::Planet_MC)
                && !A::includeAscMC())
        {
            continue;
        }
        QLineF line(getCircleMarker(asp.planet1)->sceneBoundingRect().center(),
                    getCircleMarker(asp.planet2)->sceneBoundingRect().center());

        // add or change geometry
        if (i >= aspects.count()) {
            aspects << view->scene()->addLine(line, aspectPen(asp));
        } else {
            aspects[i]->setLine(line);
            aspects[i]->setPen(aspectPen(asp));
        }

        QString toolTip;
        if (filesCount() > 1)
            toolTip = A::describeAspectFull(asp, "#1", "#2");
        else
            toolTip = A::describeAspectFull(asp);

        // assign messages
        if (aspects[i]->toolTip() != toolTip) {
            aspects[i]->setToolTip(toolTip);
            circle->setHelpTag(aspects[i],
                               QString("%1+%2+%3").arg(asp.d->name)
                               .arg(asp.planet1->name)
                               .arg(asp.planet2->name));
        }

        i++;
    }

    while (i != aspects.count()) {
        // remove unused aspect items
        view->scene()->removeItem(aspects.takeLast());
        //aspectMarkers.removeLast();
    }
}

void Chart::clearScene()
{
    qDebug() << "Clear scene";
    view->scene()->clear();
    chartsCount = 0;
    cuspides.clear();
    cuspideLabels.clear();
    planets.clear();
    planetMarkers.clear();
    aspects.clear();
    //aspectMarkers.clear();
    signIcons.clear();
}

QRect Chart::chartRect()
{
    return QRect(-defaultChartRadius * zoom,
                 -defaultChartRadius * zoom,
                 defaultChartRadius * 2 * zoom,
                 defaultChartRadius * 2 * zoom);
}

float Chart::innerRadius(int fileIndex)
{
    if (filesCount() == 1) return l_innerRadius * zoom;
    float meanInnerRadius = l_innerRadius * (1 - filesCount() * 0.1);
    float r = (defaultChartRadius - l_zodiacWidth - meanInnerRadius) * fileIndex / (filesCount());
    return (meanInnerRadius + r) * zoom;
}

int Chart::cuspideLength(int fileIndex, int cusp)
{
    int k = 0;
    if (filesCount() > 1) {
        // make bigger cuspides for first file and smaller for second file
        if (fileIndex == 0)
            k = 20;
        else
            k = -3;
    }

    if (cusp == 0)
        return l_cuspideLength * 1.4 + k;
    else if (cusp == 9)
        return l_cuspideLength * 1.2 + k;
    return l_cuspideLength + k;
}

void Chart::drawPlanets(int fileIndex)
{
    QFont planetFont("Almagest", 15, QFont::Bold);
    QFont planetFontSmall("Almagest", 12, QFont::Bold);

    QGraphicsScene* s = view->scene();

    for (const auto& planet: file(fileIndex)->horoscope().planets) {
        if (planet.id == A::Planet_Asc) continue;

        int radius = 2;
        int charIndex = planet.userData["fontChar"].toInt();

        auto text =
                s->addSimpleText(QChar(charIndex),
                                 planet.isReal
                                 ? planetFont
                                 : planetFontSmall);

        auto marker =
                s->addEllipse(-innerRadius(fileIndex) - radius, -radius,
                              radius * 2, radius * 2,
                              planetMarkerPen(planet, fileIndex));
        qDebug() << "planet" << planet.name
                 << " 'text" << (void*)text << " 'marker" << (void*)marker;

        if (filesCount() > 1) {
            // duplicate on outer circle
            auto e = s->addEllipse(-innerRadius(0) - radius, -radius,
                          radius * 2, radius * 2,
                          planetMarkerPen(planet, fileIndex));
            e->setParentItem(marker);
        }

        text->setPos(normalPlanetPosX(text, marker),
                     -text->boundingRect().height() / 2);
        text->setBrush(planetColor(planet, fileIndex));
        text->setPen(planetShapeColor(planet, fileIndex));
        //text   -> setOpacity(opacity);
        text->setTransformOriginPoint(text->boundingRect().center());
        text->setParentItem(marker);
        text->setData(1, planet.id); // remember PlanetId for clicking on item
        text->setData(2, fileIndex); // remember fileIndex
        marker->setTransformOriginPoint(circle->boundingRect().center());
        marker->setZValue(1);

        if (planets[fileIndex].contains(planet.id)) abort();
        planets[fileIndex][planet.id] = text;
        planetMarkers[fileIndex][planet.id] = marker;
    }
}

void Chart::drawStars(int fileIndex)
{
    QFont planetFont("Almagest", 17, QFont::Bold);
    QFont planetFontSmall("Almagest", 15, QFont::Bold);

    QGraphicsScene* s = view->scene();

    for (const auto& star : file(fileIndex)->horoscope().stars) {
        int radius = 2;

        auto text =
                s->addSimpleText("*", planetFont);
        auto marker =
                s->addEllipse(-innerRadius(fileIndex) - radius, -radius,
                              radius * 2, radius * 2,
                              planetMarkerPen(A::Planet(), fileIndex));

        if (star.isConfiguredWithPlanet())
            qDebug() << "star " << star.name
                     << " 'text" << (void*)text
                     << " 'marker" << (void*)marker;

        if (filesCount() > 1) {
            // duplicate on outer circle
            auto e = s->addEllipse(-innerRadius(0) - radius, -radius,
                          radius * 2, radius * 2,
                          planetMarkerPen(A::Planet(), fileIndex));
            e->setParentItem(marker);
        }

        text->setPos(normalPlanetPosX(text, marker),
                     -text->boundingRect().height() / 2);
        text->setBrush(planetColor(A::Planet(), fileIndex));
        text->setPen(planetShapeColor(A::Planet(), fileIndex));
        //text   -> setOpacity(opacity);
        text->setTransformOriginPoint(text->boundingRect().center());
        text->setParentItem(marker);
        text->setData(1, star.name);    // remember PlanetId for clicking on item
        text->setData(2, fileIndex);    // remember fileIndex
        marker->setTransformOriginPoint(circle->boundingRect().center());
        marker->setZValue(1);

        planets[fileIndex][star.id] = text;
        planetMarkers[fileIndex][star.id] = marker;
    }
}

void
Chart::drawCuspides(int fileIndex)
{
    static QPen penCusp(QColor(227, 214, 202), 2);
    static QPen penCusp1(QColor(250, 90, 58), 3);
    static QPen penCusp10(QColor(210, 195, 150), 3);
    static QFont font("Times New Roman", 13, QFont::Bold);

    QGraphicsScene* s = view->scene();
    QGraphicsLineItem* l;
    QPen pen;
    int endPointX;

    for (int i = 0; i < 12; i++) {
        endPointX = chartRect().x() - cuspideLength(fileIndex, i);

        if (i == 0)
            pen = penCusp1;
        else if (i == 9)
            pen = penCusp10;
        else
            pen = penCusp;

        if (filesCount() > 1 && fileIndex == 1)
            pen.setColor(QColor("#00C0FF"));

        if (filesCount() > 1 && fileIndex == 0) {
            l = s->addLine(-innerRadius(0), 0, -innerRadius(1), 0, pen);
            auto cl = s->addLine(chartRect().x(), 0, endPointX, 0, pen);
            cl->setParentItem(l);
        } else {
            l = s->addLine(-innerRadius(fileIndex), 0, endPointX, 0, pen);
        }

        if (i == 0) {
            auto cl = s->addLine(endPointX, 0, endPointX + 14, 7, pen);
            cl->setParentItem(l);  // arrow for first cuspide
            cl = s->addLine(endPointX, 0, endPointX + 14, -7, pen);
            cl->setParentItem(l);
        } else if (i == 9) {
            int d = 12;
            auto el = s->addEllipse(endPointX - d, -d / 2, d, d, pen);
            el->setParentItem(l);
        }

        cuspides[fileIndex][i] = l;

        QGraphicsSimpleTextItem* t = s->addSimpleText(A::houseTag(i + 1), font);
        t->setBrush(QColor((filesCount() > 1 && fileIndex == 1)
                           ? "#00C0FF" : "#FFFFFF"));
        t->setOpacity(0.6);
        t->setParentItem(l);
        t->moveBy(endPointX + 5, 5);
        t->setTransformOriginPoint(t->boundingRect().center());
        cuspideLabels[fileIndex][i] = t;
    }
}

int
Chart::normalPlanetPosX(QGraphicsItem* planet,
                        QGraphicsItem* marker)
{
    int indent = 6;
    return marker->boundingRect().x()
            - planet->boundingRect().width()
            - indent;
}

const QPen& 
Chart::aspectPen(const A::Aspect& asp)
{
    QString tag = asp.d->userData["good"].toString();
    static QMap<QString, QBrush> brushes{
        {"--", QColor(207,41,33)},
        {"-", QColor(230,155,57)},
        {"0", QColor(15, 114, 248)},
        {"+", QColor(14, 162, 98)},
        {"++", QColor(77, 206, 113)},
    };
    static bool s_inited = false;
    if (!s_inited) {
        //QColor rgb;
        for (unsigned i = 1, n = 32; i <= n; ++i) {
            QString is = QString::number(i);
            brushes[is] = A::getHarmonicColor(i);
        }
    }
    auto atOrb = asp.d->orb();
    qreal thick = 3 * (atOrb - asp.orb) / atOrb;
    static QPen p;
    p = QPen(brushes[tag], thick);
    return p;
}

const QPen& 
Chart::planetMarkerPen(const A::Planet& /*p*/,
                       int fileIndex)
{
    static QList<QPen>pens;
    if (pens.isEmpty()) {
        pens << QPen(QColor("#cee1f2"), 2);
        pens << QPen(QColor("#00C0FF"), 2);
    }

    return pens[qMin(fileIndex, pens.count() - 1)];
}

QColor 
Chart::planetColor(const A::Planet& p, 
                   int fileIndex)
{
    QColor color(p.userData["color"].toString());

    if (filesCount() > 1 || !color.isValid()) {
        if (fileIndex == 0)
            return "#cee1f2";
        else
            return "#00C0FF";
    }

    return color;
}

QColor Chart::planetShapeColor(const A::Planet& p, int fileIndex)
{
    QColor shapeColor(p.userData["shapeColor"].toString());

    if (filesCount() > 1 || !shapeColor.isValid()) {
        if (fileIndex == 0)
            return "#48401d";
        else
            return "#412631";
    }

    return shapeColor;
}

QGraphicsItem* Chart::getCircleMarker(const A::Planet* p)
{
    for (int i = 0; i < filesCount(); i++)
        if (*p == file(i)->horoscope().planets.value(p->id)) {
            if (i == 0)
                return planetMarkers[i][p->id];                   // return marker itself
            else
                return planetMarkers[i][p->id]->childItems()[0];  // return child of marker (duplicate on circle)
        }

    return 0;
}

void Chart::refreshAll()
{
    if (!chartsCount) return;
    clearScene();
    createScene();
    updateScene();

    for (int i = 0; i < filesCount(); i++)
        updatePlanetsAndCusps(i);

    updateAspects();
}

void Chart::filesUpdated(MembersList m)
{
    while (m.size() < filesCount()) m << 0;
    if (chartsCount && (chartsCount != filesCount() ||     // clear if charts count or zodiac has changed
                        (filesCount() && (m[0] & AstroFile::Zodiac))))
        clearScene();

    bool justCreated = false;
    bool updAspects = false;
    if (!chartsCount && filesCount()) {
        createScene();
        justCreated = true;
    }

    AstroFile::Members updateFlags = AstroFile::GMT
            | AstroFile::Timezone | AstroFile::Location
            | AstroFile::HouseSystem | AstroFile::AspectSet
            | AstroFile::Zodiac | AstroFile::Harmonic;

    if (filesCount() && (justCreated || (m[0] & updateFlags))) {
        updateScene();
        updatePlanetsAndCusps(0);
        updAspects = true;
    } else if (!updAspects && filesCount()
               && (m[0] & AstroFile::AspectMode))
    {
        // aspect mode changed
        updateScene();
        updatePlanetsAndCusps(0);
        updAspects = true;
    }

    if (filesCount() > 1
            && (justCreated
                || (m[1] & updateFlags)
                || ((m[0] & updateFlags) && startPoint() == Start_Ascendent))) {
        updatePlanetsAndCusps(1);
        updAspects = true;
    }

    if (updAspects)
        updateAspects();
}

bool
Chart::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::GraphicsSceneWheel) {
        ev->accept();
        auto e = static_cast<QGraphicsSceneWheelEvent*>(ev);
        float z = 1;
        if (e->delta() < 0) {
            viewport.moveCenter(QPoint(0, 0));
            if (zoom == 1) {
                fitInView();
                return true;
            }
            zoom = 1;
        } else {
            zoom += z;
            viewport.moveCenter(e->scenePos());
        }

        refreshAll();
        return true;
    }

    return QObject::eventFilter(obj, ev);
}

void Chart::resizeEvent(QResizeEvent*)
{
    fitInView();
}

AppSettings Chart::defaultSettings()
{
    AppSettings s;
    s.setValue("Circle/circleStart", Start_Ascendent);
    s.setValue("Circle/clockwise", false);
    s.setValue("Circle/zodiacWidth", 36);
    s.setValue("Circle/cuspideLength", 36);
    s.setValue("Circle/innerRadius", 130);
    s.setValue("Circle/coloredZodiac", true);
    s.setValue("Circle/zodiacDropShadow", true);
    return s;
}

AppSettings Chart::currentSettings()
{
    AppSettings s;
    s.setValue("Circle/circleStart", circleStart);
    s.setValue("Circle/clockwise", clockwise);
    s.setValue("Circle/zodiacWidth", l_zodiacWidth);
    s.setValue("Circle/cuspideLength", l_cuspideLength);
    s.setValue("Circle/innerRadius", l_innerRadius);
    s.setValue("Circle/coloredZodiac", coloredZodiac);
    s.setValue("Circle/zodiacDropShadow", zodiacDropShadow);
    return s;
}

void Chart::applySettings(const AppSettings& s)
{
    circleStart = (CircleStart)s.value("Circle/circleStart").toInt();
    clockwise = s.value("Circle/clockwise").toBool();
    l_zodiacWidth = s.value("Circle/zodiacWidth").toInt();
    l_cuspideLength = s.value("Circle/cuspideLength").toInt();
    l_innerRadius = s.value("Circle/innerRadius").toInt();
    coloredZodiac = s.value("Circle/coloredZodiac").toBool();
    zodiacDropShadow = s.value("Circle/zodiacDropShadow").toBool();

    refreshAll();
}

void Chart::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Chart"));

    QMap<QString, QVariant> values;
    values[tr("Ascendent")] = Start_Ascendent;
    values[tr("0 Aries")] = Start_ZeroDegree;
    ed->addComboBox("Circle/circleStart", tr("Circle start:"), values);

    ed->addCheckBox("Circle/clockwise", tr("Clockwise circle"));
    ed->addSpinBox("Circle/zodiacWidth", tr("Zodiac circle width:"), 5, 1000);
    ed->addSpinBox("Circle/cuspideLength", tr("Cusp line length"), 0, 1000);
    ed->addSpinBox("Circle/innerRadius", tr("Inner circle:"), 10, 1000);
    ed->addSpacing(10);
    ed->addControl("Circle/coloredZodiac", tr("Colored circle:"));
    ed->addControl("Circle/zodiacDropShadow", tr("Drop shadow:"));
}
