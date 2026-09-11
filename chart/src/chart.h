#ifndef CHART_H
#define CHART_H

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <Astroprocessor/Gui>

class QVariantAnimation;

enum CircleStart { Start_ZeroDegree, Start_Ascendent, Start_Outer_Ascendant };

class Chart;


class RotatingCircleItem : public QAbstractGraphicsShapeItem
{
    private:
        QRectF rect;
        float dragAngle;
        QDateTime dragDT;
        AstroFile* file;

        float angle(const QPointF& pos);                   // converts coordinate into angle
        Chart* chart();

    protected:
        void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;
        bool sceneEventFilter(QGraphicsItem*, QEvent*) override;    // handles events of items
        bool sceneEvent(QEvent *event) override;

    public:
        RotatingCircleItem(QRect rect, const QPen& pen);
        QPainterPath shape() const override;
        QRectF boundingRect() const override { return rect; }

        void setFile(AstroFile* f) { file = f; }
        void setHelpTag(QGraphicsItem* item, QString tag);
};


/* =========================== ASTRO MAP SHOW ======================================= */

class Chart : public AstroFileHandler
{
    Q_OBJECT

private:
    typedef QMap<A::PlanetId, QGraphicsItem*> graphicsItemDict;

    static const int defaultChartRadius = 250;
    static const int wheelDownBiasPx    = 0;   ///< downward bias for input widgets
    int chartsCount;
    QRectF viewport, viewportBig;
    float zoom;
    QGraphicsView* view;       ///< wheel view (its own scene)
    QGraphicsView* declView;   ///< declination strip view (its own scene)
    RotatingCircleItem* circle;
    //A::AspectList synAspects;

    CircleStart circleStart;
    bool clockwise;

    // Transient wheel-rotation freeze, engaged only during continuous Play
    // animation so the zodiac ring does not whirl as the (live) ascendant drifts.
    // Captured once on the first locked updateScene() and held for every frame.
    bool  _rotationLocked     = false;
    bool  _haveLockedRotation = false;
    float _lockedRotation     = 0.0f;  // frozen ascendant-derived angle, pre-clockwise
    float _lastRotate         = 0.0f;  // last LIVE (un-frozen) angle, for capture

    // Discrete-step planet slide ("eye candy"): tween the body/marker glyphs
    // from their pre-step positions to the post-step positions over a short
    // duration instead of snapping. Item pointers are stable across a data
    // change (no clearScene), so they key the start/end snapshots. Aspect lines
    // and figures are hidden during the slide and restored on landing.
    QVariantAnimation*                              _slideAnim = nullptr;
    QHash<QGraphicsItem*, QPair<QPointF, qreal>>    _slideStart; // pos, rotation
    int  _slideDurationMs = 0;
    bool _slidePending    = false;
    // Aspect-line crossfade: clones of the pre-step lines that fade out while
    // the live (reused) aspect lines fade in. Keyed start positions of the
    // declination glyphs (by file*K+planetId) so the rebuilt strip can tween.
    QList<QGraphicsLineItem*> _slideAspectGhosts;
    QHash<int, QPointF>       _slideDeclMarkerStart;
    QHash<int, QPointF>       _slideDeclGlyphStart;
    static constexpr int      declSlideKeyMul = 100000;
    int l_zodiacWidth;
    int l_innerRadius;
    int l_cuspideLength;
    bool coloredZodiac;
    bool zodiacDropShadow;
    bool includeAsteroids;
    bool includeCentaurs;
    bool displayDeclination;

    // Aspect Range Navigator animation tuning (lives in the Chart settings tab).
    int  _animDurationMs = 10000; // continuous playback: traverse a range in this
    int  _slideMs        = 600;   // discrete-step planet slide duration (0 = off)

    // Shadow copy of Mundane/primDirSystem, compared in applySettings() --
    // NOT against the live A::primDirSystem global. Handler construction
    // order is Details, Harmonics, Transits, Speculum, Chart, Planets, Plain
    // (mainwindow.cpp) -- Transits::applySettings() already mutates the live
    // global (for its own PD recompute) before Chart's turn, so by the time
    // Chart runs, comparing against the live global would always see it
    // already equal to the incoming value and never detect a change. Plain
    // is what actually recomputes pvPos (file(i)->calculate(), since
    // mundaneHouseSystem() feeds calculatePlanet()) -- but Plain runs AFTER
    // Chart, so refreshAll() below would otherwise paint with the PREVIOUS
    // Apply's pvPos data, one click behind (same race class already fixed
    // once for Transits vs. Plain -- see plain.h's own _lastPrimDirSystem
    // and the docs file's "Handler-ordering race" writeup). Chart must do
    // its own fresh-value recompute rather than rely on Plain's, exactly
    // like Transits already does for its own purposes.
    A::PrimDirSystem _lastPrimDirSystem = A::pdsPlacidus;

    QMap<int, graphicsItemDict> cuspides;
    QMap<int, graphicsItemDict> cuspideLabels;
    QMap<int, graphicsItemDict> planetMarkers;
    QMap<int, graphicsItemDict> planets;
    //QList<QGraphicsSimpleTextItem*> aspectMarkers;
    QList<QGraphicsLineItem*>         aspects;
    QList<QGraphicsItem*>             signIcons;
    /// Radial divider lines between zodiac sign sectors, one per sign,
    /// index-parallel to signIcons. Tracked (like signIcons) because the ring
    /// is re-laid-out whenever the mundane/equatorial projection changes --
    /// see layoutZodiacRing().
    QList<QGraphicsLineItem*>         signBorders;

    /// Midpoint visualization items: chord between B,C and line to A
    struct MidpointFigure {
        QGraphicsLineItem* chordLine = nullptr;    ///< dashed line between B and C
        QGraphicsLineItem* toALine  = nullptr;     ///< orb-weighted line from chord center to A
    };
    QList<MidpointFigure>             midpointFigures;

    /// Paran focal visualization: a neutral hub at the wheel center with a
    /// spoke to each involved body's marker (radix and/or transit wheel).
    struct ParanFigure {
        QGraphicsEllipseItem*       hub = nullptr;  ///< central node
        QList<QGraphicsLineItem*>   spokes;         ///< hub -> each body marker
        QList<QGraphicsItem*>       spokeMarkers;   ///< marker each spoke tracks
    };
    QList<ParanFigure>               paranFigures;

    /// Primary Direction event marker: for each directed promissor (2 for a
    /// rapt parallel -- X and Y individually), a "travel" line from its natal
    /// position to where the arc carries it, and a ghost "phantom" marker at
    /// that arrival point. Driven by
    /// AstroFile::getDirectionFocus{Promissors,Significator,Arc}() (see
    /// astro-gui.h) -- only meaningful in Mundane/PV display mode, since a
    /// directed mundane position has no ecliptic-mode analog.
    ///
    /// The travel line, not a phantom->significator spoke, is what carries
    /// the information: for a CONJUNCTION direction the promissor by
    /// definition arrives exactly ON the significator, so a spoke between
    /// them is always zero-length (and the phantom alone is invisible inside
    /// a crowded ring). `spoke` is therefore only drawn for aspectual rays,
    /// where the arrival point sits a ray-offset away from the significator
    /// -- it stays null when the two coincide.
    ///
    /// Positions are recomputed fresh each draw rather than tracking marker
    /// items: the significator is never directed (only promissors move), and
    /// Asc/IC/Desc have no QGraphicsItem of their own anyway (they're drawn
    /// as cusp lines, not planet markers) -- nor do angles appear in
    /// horoscope().planets for a normal chart, so their 0/90/180/270 pvPos
    /// values are special-cased the same way astro-calc.cpp's getPos() does.
    struct DirectionFigure {
        QGraphicsEllipseItem* phantom = nullptr; ///< ghost marker, directed position
        QGraphicsLineItem*    travel  = nullptr; ///< natal promissor -> directed position
        QGraphicsLineItem*    spoke   = nullptr; ///< directed -> significator (null if coincident)
    };
    QList<DirectionFigure>           directionFigures;

    /// Declination strip (horizontal axis below the wheel).
    /// X = |declination|; southern bodies above the axis line, northern below.
    static constexpr float declMaxDeg          = 28.0f;
    static constexpr int   declMaxRungs        = 3;
    static constexpr int   declGlyphSpacing    = 16;   ///< default px between rungs
    static constexpr int   declMinGlyphSpacing = 10;   ///< floor when compressing rungs
    static constexpr int   declGlyphHeightApx  = 18;   ///< approx glyph bbox height
    static constexpr int   declGlyphHPad       = 3;    ///< px pad between adjacent glyph bboxes
    static constexpr int   declEdgeMargin      = 4;    ///< px slack at the strip's outer edge
    static constexpr int   declTickClearance   = 8;    ///< px from baseline to nearest rung
                                                        ///< when there's no number label to clear
    static constexpr int   declLabelClearance  = 24;   ///< px from baseline to north rung 0
                                                        ///< when a number label must be cleared
    static constexpr int   declStripAbove      = 4 + declMaxRungs * declGlyphSpacing
                                                + declGlyphHeightApx;
    static constexpr int   declStripBelow      = declLabelClearance
                                                + (declMaxRungs - 1) * declGlyphSpacing
                                                + declGlyphHeightApx + 4;
    static constexpr int   declViewHeight      = declStripAbove + declStripBelow;
    static constexpr int   declStripMargin     = 20;   ///< px L/R inset for axis line
    QList<QGraphicsItem*>          declStripItems;  ///< axis line, ticks, labels
    QMap<int, graphicsItemDict>    declMarkers;     ///< [fileIndex][PlanetId] -> ellipse
    QMap<int, graphicsItemDict>    declGlyphs;      ///< [fileIndex][PlanetId] -> text
    QList<QPair<float,float>>      declLabelXRanges; ///< [left,right] px span of each number label

    float zodiacWidth() { return l_zodiacWidth * zoom; }
    float innerRadius(int fileIndex = 0);
    int cuspideLength(int fileIndex, int cusp);
    QRect chartRect();
    int   declBaselineY();    ///< Y of the axis in declView scene coords
    float declXForDeg(float absDec);

    int normalPlanetPosX(QGraphicsItem* planet, QGraphicsItem* marker);
    /// Wheel angle for a body in PV display mode: relocalized into the
    /// reference file's frame for biwheels (per circleStart), else raw pvPos.
    qreal displayPvPos(const A::Star& b, int fileIndex);
    const QPen& aspectPen(const A::Aspect& asp);
    const QPen& planetMarkerPen(const A::Planet& p, int fileIndex);
    QColor planetColor(const A::Planet& p, int fileIndex);
    QColor planetShapeColor(const A::Planet& p, int fileIndex);
    QGraphicsItem* getCircleMarker(const A::Planet* p);

    /// Position the zodiac ring's sector dividers, sign glyphs and colored
    /// band for the CURRENT draw frame. Sign spans are only equal 30 degree
    /// sectors in the ecliptic frame; in equatorial/mundane they must be
    /// projected (A::eclipticPointDisplayAngle). Called from createScene()
    /// once the items exist, and again from updateScene() because the
    /// projection depends on RAMC/latitude/obliquity, which change on time
    /// and location edits that never rebuild the scene.
    /// File whose horoscope anchors the wheel: rotation, zodiac-ring
    /// projection, and the drawn angle/house grid. file(1) only for
    /// "prefer outer" bi-wheels. Deliberately NOT the same as the
    /// body-relocalization anchor passed to setPvFrameFile() in
    /// updateAspects(), which returns -1 for Start_ZeroDegree to disable
    /// relocalization entirely -- the ring still has to be laid out against
    /// SOME file, and that is file 0.
    int ringFileIndex() const
    {
        return (circleStart == Start_Outer_Ascendant && filesCount() > 1) ? 1
                                                                         : 0;
    }

    void layoutZodiacRing();

    void drawPlanets(int fileIndex);
    void drawStars(int fileIndex);
    void drawCuspides(int fileIndex);
    void updatePlanetsAndCusps(int fileIndex);
    void updateAspects();
    void drawMidpointFigures();
    void clearMidpointFigures();
    void drawParanFigures();
    void clearParanFigures();
    void drawDirectionFigure();
    void clearDirectionFigure();
    void drawDeclinationAxis();
    void drawDeclinationBodies(int fileIndex);
    void layoutDeclinationGlyphs();
    void clearDeclinationStrip();
    void rebuildDeclinationStrip();

    void snapshotPlanetState(QHash<QGraphicsItem*, QPair<QPointF, qreal>>& into);
    void startPlanetSlide();   // capture end state, reset to start, run the tween
    void finishPlanetSlide();  // snap to end, restore aspect lines/figures
    void clearAspectGhosts();  // remove the crossfade ghost lines
    void abortPlanetSlide();   // hard teardown when the scene is wiped under us

    void fitInView();
    void createScene();
    void updateScene();
    void clearScene();

    void refreshAll();

protected:                            // AstroFileHandler && other implementations
    void filesUpdated(MembersList) override;
    void viewSettingsUpdated(MembersList) override;

    AppSettings defaultSettings() override;
    AppSettings currentSettings() override;
    void applySettings(const AppSettings&) override;
    void setupSettingsEditor(AppSettingsEditor*) override;

    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *ev) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

public slots:
    void onPlanetsSelected(const A::PlanetSet&) { }

signals:
    void planetSelected(A::PlanetId, int fileIndex);
    /// Emitted when a wheel time-drag finishes (mouse release after dragging the
    /// zodiac ring to change the chart's moment). Carries the dragged file so
    /// listeners (e.g. the events panel) can react to the deliberate time change.
    void timeDragFinished(AstroFile* draggedFile);
    void planetsSelected(const A::PlanetSet&);

public:
    Chart(QWidget *parent = nullptr);

    void help(QString tag) { requestHelp(tag); }    // called by circle item (because requestHelp() is protected)
    bool isClockwise() { return clockwise; }
    CircleStart startPoint() { return circleStart; }

    // Navigator animation settings (configured in the Chart settings tab).
    int animationDurationMs() const { return _animDurationMs; }
    int planetSlideMs() const { return _slideMs; }

    // Freeze (on==true) / release (on==false) the wheel rotation. Used by the
    // Aspect Range Navigator around continuous Play so the ring stays fixed.
    void lockRotation(bool on);

    // Arm a planet slide for the NEXT moment change (a discrete navigator step).
    // Snapshots the current glyph positions; the slide is kicked off from
    // filesUpdated once the post-step positions are computed. durationMs is the
    // tween length (the caller may shorten it to match a fast click cadence).
    void beginPlanetSlide(int durationMs);

    friend class RotatingCircleItem;
};

#endif // CHART_H
