#include "chart.h"
#include "../../zodiac/src/thememanager.h"
#include <Astroprocessor/Calc>
#include <Astroprocessor/Output>
#include <QDebug>
#include <QEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QCoreApplication>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <math.h>
#include <QtMath>
#include <swephexp.h>
#include "../../zodiac/src/slidewidget.h"


RotatingCircleItem::RotatingCircleItem(QRect rect, const QPen& pen) :
    QAbstractGraphicsShapeItem()
{
    file       = 0;
    this->rect = rect;
    setPen(pen);
}

void
RotatingCircleItem::paint(QPainter* p,
                          const QStyleOptionGraphicsItem*,
                          QWidget*)
{ // simply draw a circle with assigned pen
    p->setPen(pen());
    int adjust = pen().width() / 2;
    p->drawEllipse(rect.adjusted(adjust, adjust, -adjust, -adjust));
}

QPainterPath
RotatingCircleItem::shape() const
{ // creates a ring shape
    QPainterPath path;
    path.addEllipse(boundingRect());

    QPainterPath innerPath;
    path.addEllipse(boundingRect().adjusted(pen().width(),
                                            pen().width(),
                                            -pen().width(),
                                            -pen().width()));

    return path.subtracted(innerPath);
}

float
RotatingCircleItem::angle(const QPointF& pos)
{
    QPointF center = boundingRect().center();

    float ret =
        atan((pos.y() - center.y()) / (pos.x() - center.x())) * 180 / 3.1416;

    if (pos.x() > center.x()) // I, IV quadrant
        ret += 180;
    else if (pos.y() > center.y()) // III quadrant
        ret += 360;

    return ret;
}

Chart*
RotatingCircleItem::chart()
{
    return (Chart*) (scene()->views()[0]->parentWidget()); // ахтунг, быдлокод!
}

bool
RotatingCircleItem::sceneEvent(QEvent* event)
{
    if (!file) return false;

    if (event->type() == QEvent::GraphicsSceneMousePress) {
        dragAngle = angle(((QGraphicsSceneMouseEvent*) event)->scenePos());
        dragDT    = file->getGMT();
        return true;
    } else if (event->type() == QEvent::GraphicsSceneMouseMove) {
        QGraphicsSceneMouseEvent* moveEvent = (QGraphicsSceneMouseEvent*) event;
        QPointF                   lastPos   = moveEvent->lastScenePos();
        QPointF                   newPos    = moveEvent->scenePos();

        float lastAngle = angle(lastPos);
        float newAngle  = angle(newPos);

        // new angle turns 0/360
        if ((lastAngle < 10 && newAngle > 350)
            || (newAngle < 10 && lastAngle > 350))
        {
            dragAngle = newAngle;
            dragDT    = file->getGMT();
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

    return false; // pass event through
}

bool
RotatingCircleItem::sceneEventFilter(QGraphicsItem* watched, QEvent* event)
{
    if (event->type() == QEvent::GraphicsSceneHoverEnter) {
        // show help when mouse over item
        QString tag = watched->data(0).toString();
        chart()->help(tag);
        return true;
    } else if (event->type() == QEvent::GraphicsSceneMousePress) {
        // emit signal when item clicked
        if (watched->data(1).isNull()) return false;
        emit chart() -> planetSelected(watched->data(1).toInt(),
                                       watched->data(2).toInt());
        return true;
    }

    return false;
}

void
RotatingCircleItem::setHelpTag(QGraphicsItem* item, QString tag)
{
    // assigning help string and installing event handler on item
    item->setAcceptHoverEvents(true);
    item->installSceneEventFilter(this); // to detect hover event
    item->setData(0, tag);
}

/* =========================== ASTRO MAP SHOW
 * ======================================= */

Chart::Chart(QWidget* parent) : AstroFileHandler(parent)
{
    chartsCount = 0;
    zoom        = 1;
    
    // Enable drag and drop
    setAcceptDrops(true);

    float scale(0.8), sc2(0.5);
    viewport    = QRect(chartRect().x() / scale,
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
    view->scene()->installEventFilter(this);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setAcceptDrops(false);

    declView = new QGraphicsView(this);
    declView->setScene(new QGraphicsScene());
    declView->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    declView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    declView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    declView->setFixedHeight(declViewHeight);
    declView->setFrameShape(QFrame::NoFrame);
    declView->setAcceptDrops(false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(QMargins(0, 0, 0, 0));
    layout->setSpacing(0);
    // Top spacer to clear the input-data widgets that overlay the chart pane.
    layout->addSpacing(35);
    layout->addWidget(view);       // stretch=1 implicit
    layout->addWidget(declView);   // fixed height
    
    // Connect to theme changes to refresh chart colors
    connect(&ThemeManager::instance(),
            &ThemeManager::themeChanged,
            this,
            &Chart::refreshAll);
}

void
Chart::fitInView()
{
    view->fitInView(viewport, Qt::KeepAspectRatio);
}

void
Chart::createScene()
{
    qDebug() << "Create scene";
    QGraphicsScene* s = view->scene();

    ThemeManager& theme = ThemeManager::instance();
    QBrush background(theme.getChartBackgroundColor());
    QPen   penZodiac(theme.getChartZodiacColor(), zodiacWidth());
    QPen   penBorder(theme.getChartBorderColor());
    QPen   penCircle(theme.getChartCircleColor(), 1);
    QFont  zodiacFont("Almagest", 15 * zoom, QFont::Bold);
    QColor signFillColor  = Qt::black;          // Restored: original hardcoded value
    QColor signShapeColor = QColor(109, 109, 109); // Restored: original hardcoded value

    if (coloredZodiac) {
        QConicalGradient grad1(chartRect().center(), 180);
        QColor           color;

        for (const A::ZodiacSign& sign : file()->horoscope().zodiac.signs) {
            color = QColor::fromString(sign.userData["bgcolor"].toString());
            float a1 = sign.startAngle / 360;
            float a2 = sign.endAngle / 360 - 0.0001;

            if (clockwise) {
                a1 = 0.5 - a1;
                if (a1 < 0) a1 += 1;
                a2 = 0.5 - a2;
                if (a2 < 0) a2 += 1;
            }

            grad1.setColorAt(a1, color);
            grad1.setColorAt(a2, color);
        }

        penZodiac.setBrush(QBrush(grad1));
        penBorder.setColor(Qt::black);
    }

    for (int f = 0; f < filesCount(); f++) { // inner circles
        s->addEllipse(-innerRadius(f),
                      -innerRadius(f),
                      2 * innerRadius(f),
                      2 * innerRadius(f),
                      penCircle);
        drawCuspides(f); // cuspides
    }

    s->addEllipse(chartRect().adjusted(2, 2, -2, -2),
                  penBorder,
                  background); // fill background (with margin)

    circle = new RotatingCircleItem(chartRect(), penZodiac); // zodiac circle
    QCursor curs(QPixmap("chart/rotate.png"));
    circle->setCursor(curs);
    s->addItem(circle);
    s->addEllipse(chartRect(), penBorder)
        ->setParentItem(circle); // zodiac outer border
    s->addEllipse(chartRect().adjusted(zodiacWidth(),
                                       zodiacWidth(), // zodiac inner border
                                       -zodiacWidth(),
                                       -zodiacWidth()),
                  penBorder)
        ->setParentItem(circle);

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
        float rad     = -sign.startAngle * 3.1416 / 180;
        float rad_mid = -(sign.startAngle + (endAngle - sign.startAngle) / 2)
                        * 3.1416 / 180;

        if (clockwise) {
            rad     = 3.1416 - rad;
            rad_mid = 3.1416 - rad_mid;
        }

        s->addLine(chartRect().x() * cos(rad), // zodiac sign borders
                   chartRect().y() * sin(rad),
                   (chartRect().x() + zodiacWidth()) * cos(rad),
                   (chartRect().y() + zodiacWidth()) * sin(rad),
                   penBorder)
            ->setParentItem(circle);

        QString                  ch = QChar(sign.userData["fontChar"].toInt());
        QGraphicsSimpleTextItem* text =
            s->addSimpleText(ch, zodiacFont); // zodiac sign icon
        text->setParentItem(circle);
        text->setBrush(coloredZodiac ? signFillColor
                                     : sign.userData["fillColor"].toString());
        text->setPen(coloredZodiac ? signShapeColor
                                   : sign.userData["shapeColor"].toString());
        text->setOpacity(0.9);
        text->moveBy((chartRect().x() + zodiacWidth() / 2) * cos(rad_mid)
                         - text->boundingRect().width() / 2,
                     (chartRect().y() + zodiacWidth() / 2) * sin(rad_mid)
                         - text->boundingRect().height() / 2);
        text->setTransformOriginPoint(text->boundingRect().center());
        circle->setHelpTag(text, sign.name);
        signIcons << text;
    }

    for (int i = 0; i < filesCount(); i++) {
        drawPlanets(i);
        drawStars(i);
    }

    /*if (viewport.center() != QPointF(0,0)) */ fitInView();
    chartsCount = filesCount();
    rebuildDeclinationStrip();
}

void
Chart::updateScene()
{
    qDebug() << "Update scene";

    circle->setFile(file());
    float rotate;

    // Use the outer chart's ascendant whenever two charts are present.
    bool useReturnAsc = (circleStart == Start_Outer_Ascendant && files().size() > 1);

    switch (circleStart) {
    case Start_Outer_Ascendant:
    case Start_Ascendent:       {
        const auto& houses = file(useReturnAsc ? 1 : 0)->horoscope().houses;
        switch (A::aspectMode) {
        case A::amcEquatorial:    rotate = houses.RAAC; break;
        case A::amcPrimeVertical: rotate = 0; break;
        default:
        case A::amcEcliptic:      rotate = houses.cusp[0]; break;
        }
        break;
    }
    default: rotate = file()->horoscope().zodiac.signs[0].startAngle; break;
    }
    if (clockwise) {
        rotate = -rotate;
    }

    for (QGraphicsItem* i : std::as_const(signIcons)) i->setRotation(-rotate);

    circle->setRotation(rotate);
}

void
Chart::updatePlanetsAndCusps(int fileIndex)
{
    qDebug() << "Update planets and cusps" << fileIndex;

    QMap<QGraphicsItem*, const A::Star*> ret;
    QMap<QGraphicsItem*, int>            moved;

    auto overlap = [](QGraphicsItem* planet, QGraphicsItem* other, int rung) {
        // FIXME: should use some pi-based ratio to increase the
        // available space
        qreal width   = planet->boundingRect().width() * (11 - rung) / 41;
        qreal pcenter = planet->rotation();
        qreal pbeg    = pcenter - width / 2;
        qreal pend    = pbeg + width;
        width         = other->boundingRect().width() * (11 - rung) / 41;
        // Normalize other's center to within [-180, 180] of planet's center
        // so that planets straddling the 0°/360° boundary are detected correctly.
        qreal diff    = other->rotation() - pcenter;
        while (diff >  180.0) diff -= 360.0;
        while (diff < -180.0) diff += 360.0;
        qreal ocenter = pcenter + diff;
        qreal obeg    = ocenter - width / 2;
        qreal oend    = obeg + width;
        if (pbeg > oend || obeg > pend || pend < obeg || oend < pbeg) {
            return false;
        }
        return true;
    };

    auto moveIfNeeded = [&ret, &moved, &overlap](QGraphicsItem* planet,
                                                 QGraphicsItem* other) {
        if (other->isVisible()
            && moved.value(other, 1) == moved.value(planet, 1)
            && overlap(planet, other, moved.value(other, 1)))
        {
#if 0
            qDebug() << "  collision with" << ret[other]->name
                     << other->pos() << other->rotation()
                     << other->boundingRect().size();
#endif
            // planet->moveBy(-other->boundingRect().width(), 0);
            planet->moveBy(-20, 0);
#if 0
            qDebug() << "    new pos" << planet->pos() << planet->boundingRect().size();
#endif
            moved[planet] = moved.value(other, 1) + 1;
            return true;
        }
        return false;
    };

    auto positive = [](qreal angle) {
        if (angle < 0) angle += 360.0;
        return angle;
    };

    auto rotate = circle->rotation();
    auto repose = [&](auto b,
                      bool hide) -> std::pair<QGraphicsItem*, QGraphicsItem*> {
        qreal angle = 0.0;
        switch (A::aspectMode) {
        case A::amcEcliptic:      angle = b.eclipticPos.x(); break;
        case A::amcEquatorial:    angle = b.equatorialPos.x(); break;
        case A::amcPrimeVertical: {
            angle = b.pvPos;
            if (filesCount() > 1 && b.tropicalEclipticPos.x() >= 0.0) {
                int refIdx = -1;
                if (fileIndex == 0 && circleStart == Start_Outer_Ascendant)
                    refIdx = 1;
                else if (fileIndex == 1 && circleStart == Start_Ascendent)
                    refIdx = 0;
                if (refIdx >= 0) {
                    const auto& refHouses = file(refIdx)->horoscope().houses;
                    double xpin[2] = { b.tropicalEclipticPos.x(),
                                       b.tropicalEclipticPos.y() };
                    char pvErr[256] = "";
                    double hp = swe_house_pos(refHouses.RAMC,
                                              file(refIdx)->getLocation().y(),
                                              refHouses.eps,
                                              'C', xpin, pvErr);
                    if (hp >= 1.0 && hp <= 13.0) {
                        angle = (hp - 1.0) / 12.0 * 360.0;
                        if (b.id == A::Planet_SouthNode)
                            angle = swe_degnorm(angle + 180.0);
                    }
                }
            }
            break;
        }
        default:                  hide = true; break;
        }

        QGraphicsItem* marker = planetMarkers[fileIndex][b.id];
        QGraphicsItem* body   = planets[fileIndex][b.id];

        if (hide) {
            marker->setVisible(false);
            body->setVisible(false);
            return std::make_pair(body, marker);
        }

        marker->setVisible(true);
        body->setVisible(true);

        if (clockwise) angle = 180 - angle;

        body->setPos(normalPlanetPosX(body, marker), body->pos().y());
        body->setRotation(positive(angle - rotate));
        marker->setRotation(positive(rotate - angle));
#if 0
        qDebug() << "planet 'name" << b.name << "id" << b.id
                 //<< reinterpret_cast<void*>(planet)
                 << "pos" << body->pos()
                 << "rot" << body->rotation()
                 << body->boundingRect().size();
#endif

        // avoid intersection of planets
        bool adjusted = false;
        do {
            for (auto other : ret.keys()) {
                if ((adjusted = moveIfNeeded(body, other))) break;
            }
        } while (adjusted);

        ret.insert(body, &b);

        return std::make_pair(body, marker);
    };

    QGraphicsItem *body, *marker;
    for (const A::Planet& p : file(fileIndex)->horoscope().planets) {
        // update planets
        if (p.id >= A::Planet_Asc && p.id <= A::House_12
            && p.id != A::Planet_MC)
            continue;

        bool hide =
            (p.id == A::Planet_MC) && (file(fileIndex)->getHarmonic() == 1);

        std::tie(body, marker) = repose(p, hide);
        if (hide) continue;

        QString toolTip = QString("%1 %2, %3")
                              .arg(p.name)
                              .arg(A::zodiacPosition(p,
                                                     file()->horoscope().zodiac,
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
        bool hide              = !s.isConfiguredWithPlanet();
        std::tie(body, marker) = repose(s, hide);
        if (hide) continue;

        QString toolTip = QString("%1 %2").arg(s.name).arg(
            A::zodiacPosition(s, file()->horoscope().zodiac, A::HighPrecision));
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

        QString tag =
            tr("%1+%2")
                .arg(A::romanNum(i + 1))
                .arg(A::getSign(cusp, file()->horoscope().zodiac).name);
        circle->setHelpTag(c, tag);
        circle->setHelpTag(l, tag);

        c->setToolTip(
            tr("House %1<br>%2")
                .arg(A::romanNum(i + 1))
                .arg(A::zodiacPosition(cusp, file()->horoscope().zodiac)));
    };

    // Par=N inner wheel: items exist (avoiding null-deref) but stay hidden.
    if (filesCount() > 1 && fileIndex == 0
        && file(1)->getOriginEventType() == A::etcParanatellontaToNatal)
    {
        for (int i = 0; i < 12; ++i) {
            cuspides[fileIndex][i]->setVisible(false);
            cuspideLabels[fileIndex][i]->setVisible(false);
        }
        return;
    }

    switch (A::aspectMode) {
    case A::amcEquatorial: {
        const auto& houses = file(fileIndex)->horoscope().houses;
        qDebug() << "[ANGLE_PRECESSION] chart cusps fileIndex=" << fileIndex
                 << "RAAC=" << houses.RAAC << "RAMC=" << houses.RAMC
                 << "exprApplied=" << file(fileIndex)->horoscope().exprecessApplied();
        for (int i = 0; i < 12; ++i) {
            cuspides[fileIndex][i]->setVisible(!(i % 3));
        }
        auto cusp = houses.RAAC;
        cuspate(cusp, 0);
        cusp = swe_degnorm(cusp + 180);
        cuspate(cusp, 6);
        if (file(fileIndex)->getHarmonic() == 1) {
            cusp = houses.RAMC;
            cuspate(cusp, 9);
            cusp = swe_degnorm(cusp + 180);
            cuspate(cusp, 3);
        } else {
            cuspides[fileIndex][3]->setVisible(false);
            cuspides[fileIndex][9]->setVisible(false);
        }
    } break;

    case A::amcPrimeVertical: {
        for (int i = 0; i < 12; ++i) {
            cuspides[fileIndex][i]->setVisible(!(i % 3));
        }
        cuspate(0, 0);               // ASC = 0°
        cuspate(180, 6);             // DESC = 180°
        if (file(fileIndex)->getHarmonic() == 1) {
            cuspate(270, 9);         // MC = 270°
            cuspate(90, 3);          // IC = 90°
        } else {
            cuspides[fileIndex][3]->setVisible(false);
            cuspides[fileIndex][9]->setVisible(false);
        }
    } break;

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

void
Chart::updateAspects()
{
    int  i = 0;
    auto list =
        (filesCount() == 1 ? calculateAspects() : calculateSynastryAspects());
    for (const A::Aspect& asp : std::as_const(list)) {
        if ((asp.planet1->id == A::Planet_Asc
             || asp.planet2->id == A::Planet_MC)
            && !A::includeAscMC())
        {
            continue;
        }
        auto m1 = getCircleMarker(asp.planet1);
        auto m2 = getCircleMarker(asp.planet2);
        if (!m1 || !m2) continue;

        QLineF line(m1->sceneBoundingRect().center(),
                    m2->sceneBoundingRect().center());

        // add or change geometry
        if (i >= aspects.count()) {
            aspects << view->scene()->addLine(line, aspectPen(asp));
        } else {
            aspects[i]->setLine(line);
            aspects[i]->setPen(aspectPen(asp));
        }

        QString toolTip;
        if (filesCount() > 1)
            // @todo fix #1/#2 -- should come from cpid
            toolTip = A::describeAspectFull(asp, "#1", "#2");
        else
            toolTip = A::describeAspectFull(asp);

        // assign messages
        if (aspects[i]->toolTip() != toolTip) {
            aspects[i]->setToolTip(toolTip);
            circle->setHelpTag(
                aspects[i],
                QString("%1+%2+%3")
                    .arg(asp.d->name, asp.planet1->name, asp.planet2->name));
            // qDebug() << toolTip;
        }

        i++;
    }

    while (i != aspects.count()) {
        // remove unused aspect items
        view->scene()->removeItem(aspects.takeLast());
        // aspectMarkers.removeLast();
    }
}

void
Chart::clearMidpointFigures()
{
    for (auto& mf : midpointFigures) {
        if (mf.chordLine) view->scene()->removeItem(mf.chordLine);
        if (mf.toALine)   view->scene()->removeItem(mf.toALine);
        delete mf.chordLine;
        delete mf.toALine;
    }
    midpointFigures.clear();
}

void
Chart::drawMidpointFigures()
{
    clearMidpointFigures();

    {
        QStringList fileTags;
        for (int i = 0; i < filesCount(); ++i) {
            fileTags << QString("[%1 type=%2 focal=%3]")
                            .arg(i)
                            .arg(file(i)->getType())
                            .arg(file(i)->focalPlanets().size());
        }
        qDebug().noquote() << "[MPFIG-ENTRY]"
            << "focalMidpoints=" << focalMidpoints().size()
            << "filesCount=" << filesCount()
            << "files=" << fileTags.join(" ");
    }

    // Build the working set of focal midpoints. For non-paran charts this is
    // simply _focalMidpoints (populated by calculateAspects[Synastry]).  For
    // paran charts the focal-aspect pipeline does NOT classify paran focal
    // bodies as midpoint participants, so _focalMidpoints is empty even when
    // the paran event named a midpoint. Read midpoint cpids straight from
    // each paran file's focalPlanets() in that case.
    QList<A::ChartPlanetId> focalMPs(focalMidpoints());
    bool isParanChart = false;
    for (int i = 0; i < filesCount(); ++i) {
        if (file(i)->getType() != TypeParan) continue;
        isParanChart = true;
        for (const auto& cpid : file(i)->focalPlanets()) {
            if (cpid.isMidpt() && !focalMPs.contains(cpid))
                focalMPs.append(cpid);
        }
    }
    if (focalMPs.isEmpty()) return;

    QGraphicsScene* s = view->scene();
    QColor midpointColor = ThemeManager::instance().getChartMidpointColor();
    QPen chordPen(midpointColor, 1.5, Qt::DashLine);

    // Helper: for bi-wheels (fid > 0), return the inner-circle child
    // marker so lines draw to the inner wheel, matching aspect lines.
    auto innerMarker = [&](int fi, A::PlanetId pid) -> QGraphicsItem* {
        auto* m = planetMarkers.value(fi).value(pid);
        if (!m) return nullptr;
        if (fi > 0 && !m->childItems().isEmpty())
            return m->childItems().first();
        return m;
    };

    qreal maxOrb = A::EventOptions::current().patternsSpreadOrb;
    if (maxOrb <= 0) maxOrb = 2.0;

    // Track chord info for midpoint-to-midpoint connections (A/B=C/D)
    struct ChordInfo {
        QPointF chordCenter;
        double  midAngle;
        int     figureIndex;   // index into midpointFigures
        QString name;          // "B/C" for tooltip
    };
    QVector<ChordInfo> chords;

    for (const auto& mpid : focalMPs) {
        int fid = mpid.fileId();
        if (fid < 0) fid = 0;
        if (fid >= filesCount()) continue;

        A::PlanetId pid1 = mpid.planetId();  // B
        A::PlanetId pid2 = mpid.planetId2(); // C

        // Locate markers for B and C on the inner wheel
        QGraphicsItem* markerB = innerMarker(fid, pid1);
        QGraphicsItem* markerC = innerMarker(fid, pid2);
        if (!markerB || !markerC) continue;

        QPointF posB = markerB->sceneBoundingRect().center();
        QPointF posC = markerC->sceneBoundingRect().center();
        QPointF chordCenter = (posB + posC) / 2.0;

        {
            const auto& dbgB = file(fid)->horoscope().planets.value(pid1);
            const auto& dbgC = file(fid)->horoscope().planets.value(pid2);
            qDebug() << "[MPFIG-CONST] cpid name=" << mpid.name()
                     << "fid(mpid)=" << mpid.fileId()
                     << "fid(used)=" << fid
                     << "pid1=" << int(pid1) << "name=" << dbgB.name
                     << "eclLon=" << dbgB.eclipticPos.x()
                     << "eclLat=" << dbgB.eclipticPos.y()
                     << "posB=" << posB
                     << "; pid2=" << int(pid2) << "name=" << dbgC.name
                     << "eclLon=" << dbgC.eclipticPos.x()
                     << "eclLat=" << dbgC.eclipticPos.y()
                     << "posC=" << posC;
        }

        // Draw chord line between B and C
        MidpointFigure mf;
        mf.chordLine = s->addLine(QLineF(posB, posC), chordPen);
        mf.chordLine->setZValue(0.5);

        // Find planet A geometrically: compute the midpoint angle of B/C,
        // then look for a solo focal planet near that angle.
        const auto& p1Data = file(fid)->horoscope().planets.value(pid1);
        const auto& p2Data = file(fid)->horoscope().planets.value(pid2);

        // Read the same coordinate `repose` used to place the constituent
        // markers — otherwise in equatorial / prime-vertical modes the
        // wheel rotates per equatorialPos/pvPos but midAngle stays in
        // ecliptic, so the solid line drawn from the chord-center to
        // mid-angle-on-ring lands at the wrong place.
        auto aspectModeAngle = [](const A::Planet& b) -> double {
            switch (A::aspectMode) {
            case A::amcEquatorial:    return b.equatorialPos.x();
            case A::amcPrimeVertical: return b.pvPos;
            default:                  return b.eclipticPos.x();
            }
        };
        double posB_ang = aspectModeAngle(p1Data);
        double posC_ang = aspectModeAngle(p2Data);
        double diff = swe_difdeg2n(posC_ang, posB_ang);
        double midAngle = swe_degnorm(posB_ang + diff / 2.0);
        double farAngle = swe_degnorm(midAngle + 180.0);

        // Record for midpoint-to-midpoint pass
        int figIdx = midpointFigures.size();
        chords.append({ chordCenter, midAngle, figIdx,
                        QString("%1/%2").arg(p1Data.name, p2Data.name) });

        // For paran charts the midpoint is a positional body, not part of an
        // A=B/C ecliptic-aspect structure.  Draw a solid line from the chord
        // centre to the actual midpoint position on the wheel — i.e. the
        // point where a glyph at midAngle would render on file(fid)'s ring.
        // Mirror the rotation math in updatePlanetsAndCusps (chart.cpp ~463):
        //   marker is centred at scene origin with a local-frame point at
        //   (-innerRadius(fid), 0), rotated by (circle->rotation() - eff)
        //   where eff = clockwise ? (180 - angle) : angle.
        if (isParanChart) {
            // Project chordCenter outward to the inner ring. Because B and C
            // sit on the same ring centered at scene origin, the direction
            // from origin through their midpoint is the angular bisector —
            // i.e. exactly where a glyph at midAngle would render, regardless
            // of aspect mode (ecliptic / equatorial / PV / bi-wheel PV).
            const qreal r = innerRadius(fid);
            const qreal cLen = std::hypot(chordCenter.x(), chordCenter.y());
            QPointF mpScene = (cLen > 1e-6)
                ? QPointF(chordCenter.x() / cLen * r,
                          chordCenter.y() / cLen * r)
                : QPointF(-r, 0);

            QPen solidPen(midpointColor, 1.5, Qt::SolidLine);
            mf.toALine = s->addLine(QLineF(chordCenter, mpScene), solidPen);
            mf.toALine->setZValue(0.5);

            QString tip = tr("Midpoint paran: %1/%2").arg(p1Data.name, p2Data.name);
            mf.chordLine->setToolTip(tip);
            mf.toALine->setToolTip(tip);
            midpointFigures.append(mf);
            continue;
        }

        // Search focal planets for the solo "A" planet closest to the
        // midpoint axis (check BOTH near and far midpoint = 180° opposite)
        // N.B. Store ID+fileId rather than a pointer: QMap::value()
        // returns a temporary, so &aPlanet would dangle after the loop.
        A::PlanetId bestPid = A::Planet_None;
        int   bestFid = -1;
        qreal bestOrb = 999;

        for (int fi = 0; fi < filesCount(); ++fi) {
            const auto& fp = file(fi)->focalPlanets();
            for (const auto& cpid : fp) {
                if (cpid.isMidpt()) continue;           // skip midpoint entries
                A::PlanetId aPid = cpid.planetId();
                if (aPid == pid1 || aPid == pid2) continue; // skip B and C
                int cfid = cpid.fileId();
                if (cfid < 0) cfid = fi;
                const auto aPlanet = file(cfid)->horoscope().planets.value(aPid);
                double aPos  = aspectModeAngle(aPlanet);
                double dNear = fabs(swe_difdeg2n(aPos, midAngle));
                double dFar  = fabs(swe_difdeg2n(aPos, farAngle));
                double d = qMin(dNear, dFar);
                if (d < bestOrb) {
                    bestOrb = d;
                    bestPid = aPid;
                    bestFid = cfid;
                }
            }
        }

        // Also check the other file's focal planets if none were found above
        // (the focal set is stored on file(1) for synastry)
        if (bestFid < 0 && filesCount() > 1) {
            const auto& fp1 = file(1)->focalPlanets();
            for (const auto& cpid : fp1) {
                if (cpid.isMidpt()) continue;
                A::PlanetId aPid = cpid.planetId();
                if (aPid == pid1 || aPid == pid2) continue;
                int cfid = cpid.fileId();
                if (cfid < 0) cfid = 1;
                const auto aPlanet = file(cfid)->horoscope().planets.value(aPid);
                double aPos  = aspectModeAngle(aPlanet);
                double dNear = fabs(swe_difdeg2n(aPos, midAngle));
                double dFar  = fabs(swe_difdeg2n(aPos, farAngle));
                double d = qMin(dNear, dFar);
                if (d < bestOrb) {
                    bestOrb = d;
                    bestPid = aPid;
                    bestFid = cfid;
                }
            }
        }

        if (bestFid >= 0 && bestOrb <= maxOrb) {
            const auto bestPlanet = file(bestFid)->horoscope().planets.value(bestPid);
            QGraphicsItem* markerA = innerMarker(bestFid, bestPid);
            if (markerA) {
                QPointF posA = markerA->sceneBoundingRect().center();

                // Orb-based thickness: 3px when exact, tapering to 0.5 at limit
                qreal thick = (maxOrb > 0) ? 3.0 * (maxOrb - bestOrb) / maxOrb : 1.5;
                if (thick < 0.5) thick = 0.5;

                QPen toAPen(midpointColor, thick, Qt::SolidLine);
                mf.toALine = s->addLine(QLineF(chordCenter, posA), toAPen);
                mf.toALine->setZValue(0.5);

                // Tooltip
                QString tip = tr("%1 = %2/%3  orb %4")
                    .arg(bestPlanet.name, p1Data.name, p2Data.name,
                         A::degreeToString(bestOrb));
                mf.chordLine->setToolTip(tip);
                mf.toALine->setToolTip(tip);
            }
        }

        // Tooltip fallback if no A-planet line drawn
        if (!mf.toALine) {
            mf.chordLine->setToolTip(
                tr("Midpoint: %1/%2").arg(p1Data.name, p2Data.name));
        }

        midpointFigures.append(mf);
    }

    // Second pass: connect chord centers for midpoint-to-midpoint patterns
    // (A/B=C/D).  Check all pairs – both near and far midpoint axes.
    for (int i = 0; i < chords.size(); ++i) {
        for (int j = i + 1; j < chords.size(); ++j) {
            double dNear = fabs(swe_difdeg2n(chords[i].midAngle,
                                              chords[j].midAngle));
            double dFar  = fabs(swe_difdeg2n(chords[i].midAngle,
                                              swe_degnorm(chords[j].midAngle + 180)));
            double d = qMin(dNear, dFar);
            if (d > maxOrb) continue;

            qreal thick = (maxOrb > 0) ? 3.0 * (maxOrb - d) / maxOrb : 1.5;
            if (thick < 0.5) thick = 0.5;

            QPen mpPen(midpointColor, thick, Qt::SolidLine);
            auto* line = s->addLine(
                QLineF(chords[i].chordCenter, chords[j].chordCenter), mpPen);
            line->setZValue(0.5);

            QString tip = tr("%1 = %2  orb %3")
                .arg(chords[i].name, chords[j].name,
                     A::degreeToString(d));
            line->setToolTip(tip);

            // Also update the chord tooltips to reflect the full equation
            auto& mfI = midpointFigures[chords[i].figureIndex];
            auto& mfJ = midpointFigures[chords[j].figureIndex];
            mfI.chordLine->setToolTip(tip);
            mfJ.chordLine->setToolTip(tip);

            // Store as additional MidpointFigure for proper cleanup
            MidpointFigure mfLink;
            mfLink.toALine = line;   // chordLine stays nullptr
            midpointFigures.append(mfLink);
        }
    }
}

void
Chart::clearDeclinationStrip()
{
    declView->scene()->clear();
    declStripItems.clear();
    declMarkers.clear();
    declGlyphs.clear();
}

int
Chart::declBaselineY()
{
    return declStripAbove;   // scene Y of the axis line
}

float
Chart::declXForDeg(float absDec)
{
    int viewW  = declView->viewport()->width();
    int leftX  = declStripMargin;
    int rightX = viewW - declStripMargin;
    float t = qBound(0.0f, absDec / declMaxDeg, 1.0f);
    return leftX + t * (rightX - leftX);
}

void
Chart::rebuildDeclinationStrip()
{
    if (!chartsCount || !filesCount()) return;
    clearDeclinationStrip();

    int viewW = declView->viewport()->width();
    if (viewW <= 0) return;
    declView->scene()->setSceneRect(0, 0, viewW, declViewHeight);

    drawDeclinationAxis();
    for (int i = 0; i < filesCount(); ++i) drawDeclinationBodies(i);
    layoutDeclinationGlyphs();
}

void
Chart::drawDeclinationAxis()
{
    QGraphicsScene* s = declView->scene();
    ThemeManager& theme = ThemeManager::instance();
    QColor axisColor  = theme.getChartCircleColor();
    QColor labelColor = theme.getChartCuspLabelColor(0);
    QPen   penAxis(axisColor, 1.5);
    QPen   penMajor(axisColor, 1.5);
    QPen   penMedium(axisColor, 1.0);
    QPen   penMinor(axisColor, 0.75);
    QFont  labelFont("Times New Roman", 9, QFont::Normal);

    int viewW    = declView->viewport()->width();
    int leftX    = declStripMargin;
    int rightX   = viewW - declStripMargin;
    int baselineY = declBaselineY();

    declStripItems << s->addLine(leftX, baselineY, rightX, baselineY, penAxis);

    auto isMajor   = [](int t){ return t == 0 || t == 10 || t == 20; };
    auto isMedium  = [](int t){ return t == 5 || t == 15 || t == 25; };
    auto isLabeled = [](int t){ return t == 0 || t == 10 || t == 20 || t == 28; };

    for (int t = 0; t <= int(declMaxDeg); ++t) {
        int x = leftX + (float(t) / declMaxDeg) * (rightX - leftX);
        QPen pen;
        int len;
        if      (isMajor(t))  { pen = penMajor;  len = 6; }
        else if (isMedium(t)) { pen = penMedium; len = 4; }
        else                  { pen = penMinor;  len = 2; }
        declStripItems << s->addLine(x, baselineY - len, x, baselineY + len, pen);
        if (isLabeled(t)) {
            auto* lbl = s->addSimpleText(QString("%1°").arg(t), labelFont);
            lbl->setBrush(labelColor);
            lbl->setPos(x - lbl->boundingRect().width() / 2,
                        baselineY + 7);
            declStripItems << lbl;
        }
    }
}

void
Chart::drawDeclinationBodies(int fileIndex)
{
    QFont planetFont     ("Almagest", 13, QFont::Bold);
    QFont planetFontSmall("Almagest", 11, QFont::Bold);

    QGraphicsScene* s = declView->scene();
    int baselineY = declBaselineY();
    int radius = 2;
    QColor bgFill = ThemeManager::instance().getChartBackgroundColor();

    // For biwheel/synastry, ex-precess file(0) declinations to file(1)'s
    // epoch so both charts share a coordinate frame. The processor only
    // auto-applies this in equatorial aspect mode (see
    // AstroFileHandler::dispatchUpdate); here we cover all modes by
    // computing on the fly when the horoscope hasn't already been mutated.
    bool exprecessHere = false;
    double jdNatal = 0, jdTarget = 0;
    if (fileIndex == 0 && filesCount() > 1
        && !file(0)->horoscope().exprecessApplied())
    {
        jdNatal = A::getJulianDate(
            file(0)->horoscope().inputData.GMT(), false,
            file(0)->horoscope().inputData.calendarType());
        jdTarget = A::getJulianDate(
            file(1)->horoscope().inputData.GMT(), false,
            file(1)->horoscope().inputData.calendarType());
        exprecessHere = true;
    }

    for (const auto& planet : file(fileIndex)->horoscope().planets) {
        if (-1 == planet.id) continue;
        if (planet.id >= A::Planet_Asc && planet.id <= A::House_12
            && planet.id != A::Planet_MC)
            continue;

        float dec;
        if (exprecessHere) {
            auto ep = A::exprecess_equatorial(
                planet.equatorialPos.x(), planet.equatorialPos.y(),
                jdNatal, jdTarget);
            dec = ep.dec;
        } else {
            dec = planet.equatorialPos.y();
        }
        float absDec = qMin(qAbs(dec), declMaxDeg);
        float x      = declXForDeg(absDec);

        auto* marker = s->addEllipse(-radius, -radius, 2 * radius, 2 * radius,
                                     planetMarkerPen(planet, fileIndex));
        marker->setBrush(bgFill);
        marker->setPos(x, baselineY);
        marker->setZValue(2);

        int charIndex = planet.userData["fontChar"].toInt();
        auto* text = s->addSimpleText(QChar(charIndex),
                        planet.isReal ? planetFont : planetFontSmall);
        text->setBrush(planetColor(planet, fileIndex));
        text->setPen(planetShapeColor(planet, fileIndex));
        text->setData(1, planet.id);
        text->setData(2, fileIndex);
        text->setData(3, dec);   // displayed declination (after any exprecession)
        text->setZValue(3);

        QString tip = QString("%1 decl %2%3°")
                          .arg(planet.name)
                          .arg(dec >= 0 ? "+" : "")
                          .arg(QString::number(dec, 'f', 2));
        marker->setToolTip(tip);
        text  ->setToolTip(tip);

        declMarkers[fileIndex][planet.id] = marker;
        declGlyphs [fileIndex][planet.id] = text;
    }
}

void
Chart::layoutDeclinationGlyphs()
{
    int baselineY = declBaselineY();
    int spacing = declGlyphSpacing;

    struct Entry {
        QGraphicsItem* glyph;
        float          absDec;
    };

    // Collision avoidance is hemisphere-wide across ALL files so that file 0
    // and file 1 glyphs push each other rather than overlap.
    for (int hem = 0; hem < 2; ++hem) {
        bool south = (hem == 0);
        QList<Entry> bucket;
        for (int fi = 0; fi < filesCount(); ++fi) {
            const graphicsItemDict& glyphMap = declGlyphs[fi];
            for (auto it = glyphMap.begin(); it != glyphMap.end(); ++it) {
                float dec = it.value()->data(3).toFloat();
                if ((dec < 0) != south) continue;
                bucket.append({ it.value(), qMin(qAbs(dec), declMaxDeg) });
            }
        }
        std::sort(bucket.begin(), bucket.end(),
                  [](const Entry& a, const Entry& b){
                      return a.absDec < b.absDec;
                  });

        // Per-rung last-X tracker; rung 0 = closest to axis.
        QVector<float> rungLastX;
        for (const Entry& e : bucket) {
            float x = declXForDeg(e.absDec);
            int rung = 0;
            while (rung < rungLastX.size() && x - rungLastX[rung] < spacing)
                rung++;
            if (rung == rungLastX.size()) rungLastX.append(-1e9f);
            rungLastX[rung] = x;

            float gw = e.glyph->boundingRect().width();
            float gh = e.glyph->boundingRect().height();
            float gx = x - gw / 2;
            float gy = south
                ? baselineY - spacing * (rung + 1) - gh
                : baselineY + declLabelClearance + spacing * rung;
            e.glyph->setPos(gx, gy);
        }
    }
}

void
Chart::clearScene()
{
    qDebug() << "Clear scene";
    view->scene()->clear();
    chartsCount = 0;
    cuspides.clear();
    cuspideLabels.clear();
    planets.clear();
    planetMarkers.clear();
    aspects.clear();
    // aspectMarkers.clear();
    signIcons.clear();
    // scene()->clear() already deleted the items, just clear tracking lists
    midpointFigures.clear();
    clearDeclinationStrip();
}

QRect
Chart::chartRect()
{
    return QRect(-defaultChartRadius * zoom,
                 -defaultChartRadius * zoom,
                 defaultChartRadius * 2 * zoom,
                 defaultChartRadius * 2 * zoom);
}

float
Chart::innerRadius(int fileIndex)
{
    if (filesCount() == 1) return l_innerRadius * zoom;
    float meanInnerRadius = l_innerRadius * (1 - filesCount() * 0.1);
    float r = (defaultChartRadius - l_zodiacWidth - meanInnerRadius) * fileIndex
              / (filesCount());
    return (meanInnerRadius + r) * zoom;
}

int
Chart::cuspideLength(int fileIndex, int cusp)
{
    int k = 0;
    if (filesCount() > 1) {
        // make bigger cuspides for first file and smaller for second file
        if (fileIndex == 0) k = 20;
        else
            k = -3;
    }

    if (cusp == 0) return l_cuspideLength * 1.4 + k;
    else if (cusp == 9)
        return l_cuspideLength * 1.2 + k;
    return l_cuspideLength + k;
}

void
Chart::drawPlanets(int fileIndex)
{
    QFont planetFont("Almagest", 15, QFont::Bold);
    QFont planetFontSmall("Almagest", 12, QFont::Bold);

    QGraphicsScene* s = view->scene();

    for (const auto& planet : file(fileIndex)->horoscope().planets) {
        if (-1 == planet.id) continue;
        if (planet.id >= A::Planet_Asc && planet.id <= A::House_12
            && planet.id != A::Planet_MC)
            continue;

        int radius = 2;

        QGraphicsSimpleTextItem* text = nullptr;
#if 0
        if (planet.userData["fontChar"].typeId() == QMetaType::QString) {
            text = s->addSimpleText(planet.userData["fontChar"].toString(),
                    planet.isReal? planetFont : planetFontSmall);
        } else if (planet.userData["fontChar"].typeId() == QMetaType::Int) {
#endif
        int charIndex = planet.userData["fontChar"].toInt();
        text          = s->addSimpleText(QChar(charIndex),
                                planet.isReal ? planetFont : planetFontSmall);
#if 0
        }
#endif

        auto marker = s->addEllipse(-innerRadius(fileIndex) - radius,
                                    -radius,
                                    radius * 2,
                                    radius * 2,
                                    planetMarkerPen(planet, fileIndex));
#if 0
        qDebug() << "planet" << planet.name << planet.name.length()
                 << "id" << planet.id;
#endif

        if (filesCount() > 1) {
            // duplicate on outer circle
            auto e = s->addEllipse(-innerRadius(0) - radius,
                                   -radius,
                                   radius * 2,
                                   radius * 2,
                                   planetMarkerPen(planet, fileIndex));
            e->setParentItem(marker);
        }

        text->setPos(normalPlanetPosX(text, marker),
                     -text->boundingRect().height() / 2);
        text->setBrush(planetColor(planet, fileIndex));
        text->setPen(planetShapeColor(planet, fileIndex));
        // text   -> setOpacity(opacity);
        text->setTransformOriginPoint(text->boundingRect().center());
        text->setParentItem(marker);
        text->setData(1, planet.id); // remember PlanetId for clicking on item
        text->setData(2, fileIndex); // remember fileIndex
        marker->setTransformOriginPoint(circle->boundingRect().center());
        marker->setZValue(1);

        if (planets[fileIndex].contains(planet.id)) abort();
        planets[fileIndex][planet.id]       = text;
        planetMarkers[fileIndex][planet.id] = marker;
    }
}

void
Chart::drawStars(int fileIndex)
{
    QFont planetFont("Almagest", 17, QFont::Bold);
    QFont planetFontSmall("Almagest", 15, QFont::Bold);

    QGraphicsScene* s = view->scene();

    for (const auto& star : file(fileIndex)->horoscope().stars) {
        int radius = 2;

        auto text   = s->addSimpleText("*", planetFont);
        auto marker = s->addEllipse(-innerRadius(fileIndex) - radius,
                                    -radius,
                                    radius * 2,
                                    radius * 2,
                                    planetMarkerPen(A::Planet(), fileIndex));
#if 0
        if (star.isConfiguredWithPlanet())
            qDebug() << "star " << star.name
                     << " 'text" << (void*)text
                     << " 'marker" << (void*)marker;
#endif
        if (filesCount() > 1) {
            // duplicate on outer circle
            auto e = s->addEllipse(-innerRadius(0) - radius,
                                   -radius,
                                   radius * 2,
                                   radius * 2,
                                   planetMarkerPen(A::Planet(), fileIndex));
            e->setParentItem(marker);
        }

        text->setPos(normalPlanetPosX(text, marker),
                     -text->boundingRect().height() / 2);
        text->setBrush(planetColor(A::Planet(), fileIndex));
        text->setPen(planetShapeColor(A::Planet(), fileIndex));
        // text   -> setOpacity(opacity);
        text->setTransformOriginPoint(text->boundingRect().center());
        text->setParentItem(marker);
        text->setData(1, star.name); // remember PlanetId for clicking on item
        text->setData(2, fileIndex); // remember fileIndex
        marker->setTransformOriginPoint(circle->boundingRect().center());
        marker->setZValue(1);

        planets[fileIndex][star.id]       = text;
        planetMarkers[fileIndex][star.id] = marker;
    }
}

void
Chart::drawCuspides(int fileIndex)
{
    ThemeManager& theme = ThemeManager::instance();
    QPen  penCusp(theme.getChartCuspColor(), 2);
    QPen  penCusp1(theme.getChartAscendantColor(), 3);
    QPen  penCusp10(theme.getChartMCColor(), 3);
    QFont font("Times New Roman", 13, QFont::Bold);

    QGraphicsScene*    s = view->scene();
    QGraphicsLineItem* l;
    QPen               pen;
    int                endPointX;

    for (int i = 0; i < 12; i++) {
        endPointX = chartRect().x() - cuspideLength(fileIndex, i);

        if (i == 0) pen = penCusp1;
        else if (i == 9)
            pen = penCusp10;
        else
            pen = penCusp;

        if (filesCount() > 1 && fileIndex == 1) pen.setColor(theme.getChartPlanetMarkerColor(1));

        if (filesCount() > 1 && fileIndex == 0) {
            l       = s->addLine(-innerRadius(0), 0, -innerRadius(1), 0, pen);
            auto cl = s->addLine(chartRect().x(), 0, endPointX, 0, pen);
            cl->setParentItem(l);
        } else {
            l = s->addLine(-innerRadius(fileIndex), 0, endPointX, 0, pen);
        }

        if (i == 0) {
            auto cl = s->addLine(endPointX, 0, endPointX + 14, 7, pen);
            cl->setParentItem(l); // arrow for first cuspide
            cl = s->addLine(endPointX, 0, endPointX + 14, -7, pen);
            cl->setParentItem(l);
        } else if (i == 9) {
            int  d  = 12;
            auto el = s->addEllipse(endPointX - d, -d / 2, d, d, pen);
            el->setParentItem(l);
        }

        cuspides[fileIndex][i] = l;

        QGraphicsSimpleTextItem* t = s->addSimpleText(A::houseTag(i + 1), font);
        t->setBrush(theme.getChartCuspLabelColor(fileIndex));
        t->setOpacity(0.6);
        t->setParentItem(l);
        t->moveBy(endPointX + 5, 5);
        t->setTransformOriginPoint(t->boundingRect().center());
        cuspideLabels[fileIndex][i] = t;
    }

    // Par=N biwheel: suppress inner-wheel (natal) angles and house lines.
    // The chart is computed at the current/locus location; the natal angles
    // belong to the birth location and are meaningless here.
    if (filesCount() > 1 && fileIndex == 0
        && file(0)->getOriginEventType() == A::etcParanatellontaToNatal)
    {
        for (int i = 0; i < 12; ++i) {
            cuspides[fileIndex][i]->setVisible(false);
            cuspideLabels[fileIndex][i]->setVisible(false);
        }
    }
}

int
Chart::normalPlanetPosX(QGraphicsItem* planet, QGraphicsItem* marker)
{
    int indent = 6;
    return marker->boundingRect().x() - planet->boundingRect().width() - indent;
}

const QPen&
Chart::aspectPen(const A::Aspect& asp)
{
    QString                      tag = asp.d->userData["good"].toString();
    static QMap<QString, QBrush> brushes {
        { "--", QColor(207, 41, 33) },  { "-", QColor(230, 155, 57) },
        { "0", QColor(15, 114, 248) },  { "+", QColor(14, 162, 98) },
        { "++", QColor(77, 206, 113) },
    };
    static bool s_inited = false;
    if (!s_inited) {
        for (unsigned i = 1, n = 32; i <= n; ++i) {
            brushes[QString::number(i)] = A::getHarmonicColor(i);
        }
        s_inited = true;
    }

    // For h > 32: use theme-appropriate color on first encounter
    if (!brushes.contains(tag)) {
        bool ok;
        if (tag.toInt(&ok), ok) {
            brushes[tag] = ThemeManager::instance().getChartMidpointColor();
        }
    }

    auto        atOrb = asp.d->orb();
    qreal       thick = 3 * (atOrb - asp.orb) / atOrb;
    static QPen p;
    bool        ok;
    int         h     = tag.toInt(&ok);
    Qt::PenStyle style = (ok && h > 32) ? Qt::DashLine : Qt::SolidLine;
    p = QPen(brushes[tag], thick, style);
    return p;
}

const QPen&
Chart::planetMarkerPen(const A::Planet& /*p*/, int fileIndex)
{
    static QList<QPen> pens;
    ThemeManager& theme = ThemeManager::instance();
    
    // Rebuild pens based on current theme
    pens.clear();
    pens << QPen(theme.getChartPlanetMarkerColor(0), 2);
    pens << QPen(theme.getChartPlanetMarkerColor(1), 2);

    return pens[qMin(fileIndex, pens.count() - 1)];
}

QColor
Chart::planetColor(const A::Planet& p, int fileIndex)
{
    QColor color(p.userData["color"].toString());

    if (filesCount() > 1 || !color.isValid()) {
        return ThemeManager::instance().getChartPlanetColor(fileIndex);
    }

    return color;
}

QColor
Chart::planetShapeColor(const A::Planet& p, int fileIndex)
{
    QColor shapeColor(p.userData["shapeColor"].toString());

    if (filesCount() > 1 || !shapeColor.isValid()) {
        return ThemeManager::instance().getChartPlanetShapeColor(fileIndex);
    }

    return shapeColor;
}

QGraphicsItem*
Chart::getCircleMarker(const A::Planet* p)
{
    for (int i = 0; i < filesCount(); i++)
        if (*p == file(i)->horoscope().planets.value(p->id)) {
            if (i == 0) return planetMarkers[i][p->id]; // return marker itself
            else
                return planetMarkers[i][p->id]
                    ->childItems()[0]; // return child of marker (duplicate on
                                       // circle)
        }

    return 0;
}

void
Chart::refreshAll()
{
    if (!chartsCount) return;
    clearScene();
    createScene();
    updateScene();

    for (int i = 0; i < filesCount(); i++) updatePlanetsAndCusps(i);

    updateAspects();
    drawMidpointFigures();
}

void
Chart::filesUpdated(MembersList m)
{
    while (m.size() < filesCount()) {
        m.append(AstroFile::Member());
    }

    // File-data changes that require scene rebuild
    AstroFile::Members rebuildFlags =
        AstroFile::GMT | AstroFile::Location
        | AstroFile::Type | AstroFile::Name;

    if (chartsCount
        && (chartsCount != filesCount()
            || (filesCount()
                && ((m[0] | (filesCount() > 1 ? m[1]
                                              : AstroFile::Member()))
                    & rebuildFlags))))
    {
        clearScene();
    }

    bool justCreated = false;
    if (!chartsCount && filesCount()) {
        createScene();
        justCreated = true;
    }

    AstroFile::Members dataUpdateFlags =
        AstroFile::GMT | AstroFile::Timezone | AstroFile::Location;

    bool updAspects = false;
    if (filesCount() && (justCreated || (m[0] & dataUpdateFlags))) {
        updateScene();
        for (int i = 0; i < filesCount(); ++i) updatePlanetsAndCusps(i);
        updAspects = true;
    }

    if (filesCount() > 1
        && (justCreated || (m[1] & dataUpdateFlags)
            || ((m[0] & dataUpdateFlags) && startPoint() == Start_Ascendent)))
    {
        updateScene();
        for (int i = 0; i < filesCount(); ++i) updatePlanetsAndCusps(i);
        updAspects = true;
    }

    if (updAspects) {
        updateAspects();
        drawMidpointFigures();
    }
}

void
Chart::viewSettingsUpdated(MembersList m)
{
    while (m.size() < filesCount()) {
        m.append(AstroFile::Member());
    }

    // Zodiac change requires scene rebuild
    if (chartsCount && filesCount() && (m[0] & AstroFile::Zodiac)) {
        clearScene();
    }

    if (!chartsCount && filesCount()) {
        createScene();
    }

    // Any view-setting change triggers full scene update
    bool hasViewChange = false;
    for (const auto& ml : m) {
        if (ml) { hasViewChange = true; break; }
    }

    if (hasViewChange && filesCount()) {
        updateScene();
        for (int i = 0; i < filesCount(); ++i) updatePlanetsAndCusps(i);
        updateAspects();
        drawMidpointFigures();
    }
}

bool
Chart::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::GraphicsSceneWheel) {
        ev->accept();
        auto  e = static_cast<QGraphicsSceneWheelEvent*>(ev);
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

void
Chart::resizeEvent(QResizeEvent*)
{
    fitInView();
    rebuildDeclinationStrip();
}

AppSettings
Chart::defaultSettings()
{
    AppSettings s;
    s.setValue("Circle/circleStart", Start_Ascendent);
    s.setValue("Circle/clockwise", false);
    s.setValue("Circle/zodiacWidth", 36);
    s.setValue("Circle/cuspideLength", 36);
    s.setValue("Circle/innerRadius", 130);
    s.setValue("Circle/coloredZodiac", true);
    s.setValue("Circle/zodiacDropShadow", true);
    s.setValue("Circle/includeAsteroids", true);
    s.setValue("Circle/includeCentaurs", true);
    s.setValue("Circle/displayDeclination", true);
    return s;
}

AppSettings
Chart::currentSettings()
{
    AppSettings s;
    s.setValue("Circle/circleStart", circleStart);
    s.setValue("Circle/clockwise", clockwise);
    s.setValue("Circle/zodiacWidth", l_zodiacWidth);
    s.setValue("Circle/cuspideLength", l_cuspideLength);
    s.setValue("Circle/innerRadius", l_innerRadius);
    s.setValue("Circle/coloredZodiac", coloredZodiac);
    s.setValue("Circle/zodiacDropShadow", zodiacDropShadow);
    s.setValue("Circle/includeAsteroids", includeAsteroids);
    s.setValue("Circle/includeCentaurs", includeCentaurs);
    s.setValue("Circle/displayDeclination", displayDeclination);
    return s;
}

void
Chart::applySettings(const AppSettings& s)
{
    circleStart      = (CircleStart) s.value("Circle/circleStart").toInt();
    clockwise        = s.value("Circle/clockwise").toBool();
    l_zodiacWidth    = s.value("Circle/zodiacWidth").toInt();
    l_cuspideLength  = s.value("Circle/cuspideLength").toInt();
    l_innerRadius    = s.value("Circle/innerRadius").toInt();
    coloredZodiac    = s.value("Circle/coloredZodiac").toBool();
    zodiacDropShadow = s.value("Circle/zodiacDropShadow").toBool();
    includeAsteroids = s.value("Circle/includeAsteroids").toBool();
    includeCentaurs  = s.value("Circle/includeCentaurs").toBool();
    displayDeclination = s.value("Circle/displayDeclination").toBool();
    declView->setVisible(displayDeclination);

    refreshAll();
}

void
Chart::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Chart"));

    QKeyValueList values {
        { tr("Ascendant"), Start_Ascendent },
        { tr("Ascendant (prefer outer)"), Start_Outer_Ascendant },
        { tr("0 Aries"), Start_ZeroDegree }
    };
    ed->addComboBox("Circle/circleStart", tr("Circle start:"), values);

    ed->addCheckBox("Circle/clockwise", tr("Clockwise circle"));
    ed->addSpinBox("Circle/zodiacWidth", tr("Zodiac circle width:"), 5, 1000);
    ed->addSpinBox("Circle/cuspideLength", tr("Cusp line length"), 0, 1000);
    ed->addSpinBox("Circle/innerRadius", tr("Inner circle:"), 10, 1000);
    ed->addSpacing(10);
    ed->addControl("Circle/coloredZodiac", tr("Colored circle:"));
    ed->addControl("Circle/zodiacDropShadow", tr("Drop shadow:"));
    ed->addSpacing(10);
    ed->addControl("Circle/includeAsteroids", tr("Display Juno etc.:"));
    ed->addControl("Circle/includeCentaurs", tr("Display Chiron:"));
    ed->addSpacing(10);
    ed->addControl("Circle/displayDeclination", tr("Display declination graph:"));
}

void
Chart::dragEnterEvent(QDragEnterEvent* event)
{
    qDebug() << "Chart::dragEnterEvent";
    // Accept drops from the chart list - check for text or URLs
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        qDebug() << "Chart accepting drag";
        event->acceptProposedAction();
    }
}

void
Chart::dragMoveEvent(QDragMoveEvent* event)
{
    // Accept drag move events - check for text or URLs
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void
Chart::dropEvent(QDropEvent* event)
{
    qDebug() << "Chart::dropEvent";
    
    // Extract file path from mime data
    QString filePath;
    
    if (event->mimeData()->hasText()) {
        filePath = event->mimeData()->text();
        qDebug() << "Chart drop text:" << filePath;
    }
    
    if (filePath.isEmpty() && event->mimeData()->hasUrls()) {
        QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            filePath = urls.first().toLocalFile();
            qDebug() << "Chart drop URL:" << filePath;
        }
    }
    
    if (!filePath.isEmpty()) {
        // Get parent SlideWidget and emit its chartDropped signal
        SlideWidget* slideWidget = qobject_cast<SlideWidget*>(parentWidget());
        if (slideWidget) {
            qDebug() << "Chart emitting chartDropped signal";
            emit slideWidget->chartDropped(filePath);
            event->acceptProposedAction();
        }
    }
}
