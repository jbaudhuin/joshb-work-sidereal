#include "planettable.h"
#include "../../zodiac/src/mainwindow.h"
#include "geosearch.h"
#include <Astroprocessor/Calc>
#include <Astroprocessor/Output>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTimeZone>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace
{

bool
isAngle(A::PlanetId id)
{
    return id >= A::Angles_Start && id < A::Angles_End;
}

/// Ecliptic longitude of a row's body in a chart, in that chart's zodiac.
bool
positionOf(const AstroFile* f, A::PlanetId id, double& lon, bool& retro)
{
    const A::Horoscope& h = f->horoscope();
    retro                 = false;
    if (isAngle(id)) {
        // A primary-directed chart keeps the natal house grid; its directed
        // angles live in angleEcl[] (see Horoscope).
        if (f->getType() == TypeDerivedPD)
            lon = h.angleEcl[id - A::Angles_Start].x();
        else
            lon = (id == A::Planet_Asc) ? h.houses.Asc : h.houses.MC;
        return true;
    }
    auto it = h.planets.constFind(id);
    if (it == h.planets.constEnd()) return false;
    lon   = it->eclipticPos.x();
    retro = it->isRetro();
    return true;
}

QString
clockString(double hours)
{
    int minutes = qRound(hours * 60.0) % 1440;
    if (minutes < 0) minutes += 1440;
    return QString("%1:%2")
        .arg(minutes / 60, 2, 10, QChar('0'))
        .arg(minutes % 60, 2, 10, QChar('0'));
}

QString
spanString(double days)
{
    const double minutes = days * 1440.0;
    if (minutes < 0.5) return QString();
    if (minutes < 90.0) return QObject::tr("±%1m").arg(qRound(minutes));
    if (minutes < 36 * 60.0) return QObject::tr("±%1h").arg(qRound(minutes / 60.0));
    return QObject::tr("±%1d").arg(qRound(days));
}

QString
dateString(const QDateTime& dt, double jd)
{
    QString s = QString("%1-%2-%3")
                    .arg(dt.date().year())
                    .arg(dt.date().month(), 2, 10, QChar('0'))
                    .arg(dt.date().day(), 2, 10, QChar('0'));
    // dateTimeFromJulian() reports Julian-calendar dates before the 1582
    // cutover; say so, since ten days hang on it.
    if (jd < 2299161.0) s += QObject::tr(" O.S.");
    return s;
}

/// Zone (clock) time offset in hours east of Greenwich at a place and
/// moment, from the time-zone database. Only from 1900 on: before zone time
/// was general, local mean time is the better reading.
bool
zoneOffset(const QString& tzId, const QDateTime& gmt, double& hours)
{
    if (tzId.isEmpty() || !gmt.isValid() || gmt.date().year() < 1900) return false;
    const QTimeZone tz(tzId.toUtf8());
    if (!tz.isValid()) return false;
    hours = tz.offsetFromUtc(gmt) / 3600.0;
    return true;
}

QString
offsetString(double hours)
{
    const int minutes = qRound(std::fabs(hours) * 60.0);
    QString   s = QString("%1%2").arg(hours < 0 ? '-' : '+').arg(minutes / 60);
    if (minutes % 60) s += QString(":%1").arg(minutes % 60, 2, 10, QChar('0'));
    return s;
}

/// Great-circle distance in kilometres.
double
distanceKm(double lat1, double lon1, double lat2, double lon2)
{
    const double p1 = qDegreesToRadians(lat1), p2 = qDegreesToRadians(lat2);
    const double dp = p2 - p1, dl = qDegreesToRadians(lon2 - lon1);
    const double a  = std::sin(dp / 2) * std::sin(dp / 2)
                      + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 6371.0 * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

QString
distanceString(double km)
{
    const double mi = km / 1.609344;
    if (km < 10)
        return QObject::tr("%1 km (%2 mi)").arg(km, 0, 'f', 1).arg(mi, 0, 'f', 1);
    return QObject::tr("%1 km (%2 mi)").arg(qRound(km)).arg(qRound(mi));
}

/// Right ascension, declination and mundane position of a row's body.
bool
equatorialOf(const AstroFile* f, A::PlanetId id, double& ra, double& decl,
             double& mundane)
{
    const A::Horoscope& h = f->horoscope();
    if (isAngle(id)) {
        const int i = id - A::Angles_Start;
        mundane     = h.anglePv[i];
        if (f->getType() == TypeDerivedPD) {
            ra   = h.angleEqu[i].x();
            decl = h.angleEqu[i].y();
        } else {
            // A point on the ecliptic: its declination follows from its RA.
            ra   = (id == A::Planet_Asc) ? h.houses.RAAC : h.houses.RAMC;
            decl = qRadiansToDegrees(std::atan(std::tan(qDegreesToRadians(h.houses.eps))
                                               * std::sin(qDegreesToRadians(ra))));
        }
        return true;
    }
    auto it = h.planets.constFind(id);
    if (it == h.planets.constEnd()) return false;
    ra      = it->equatorialPos.x();
    decl    = it->equatorialPos.y();
    mundane = it->pvPos;
    return true;
}

/// A typed position rewritten the way the table writes one ("9 Can 51" ->
/// "9°51' Can"), to the precision it was typed with and no further: that
/// precision is what the search reads the uncertainty from. Text that cannot
/// be read, or is given in decimals, comes back as it was.
QString
snapPosition(const QString& text, const A::Zodiac& zod)
{
    double lon, unit;
    if (!A::parseZodiacPosition(text, zod, &lon, &unit)) return text;
    const int level = std::fabs(unit - 1.0) < 1e-9          ? 0
                      : std::fabs(unit - 1.0 / 60) < 1e-9   ? 1
                      : std::fabs(unit - 1.0 / 3600) < 1e-9 ? 2
                                                            : -1;
    const A::ZodiacSign& sign = A::getSign(float(lon), zod);
    if (unit > 1.5) return sign.tag; // a sign by itself
    if (level < 0) return text;

    double               in   = lon - sign.startAngle;
    if (in < 0) in += 360;
    const qint64 secs = qRound64(in * 3600.0);
    QString      s    = QString::number(secs / 3600) + QChar(0x00B0);
    if (level >= 1) s += QString("%1'").arg((secs / 60) % 60, 2, 10, QChar('0'));
    if (level >= 2) s += QString("%1\"").arg(secs % 60, 2, 10, QChar('0'));
    return s + ' ' + sign.tag;
}

/// The table, with Tab and Enter made to step through the positions that
/// can be typed into (the ticked rows while searching) instead of through
/// every cell.
class PositionTable : public QTableWidget {
  public:
    PositionTable(int rows, int columns, int inputColumn)
        : QTableWidget(rows, columns), _inputColumn(inputColumn)
    {
    }

  protected:
    QModelIndex moveCursor(CursorAction action, Qt::KeyboardModifiers mods) override
    {
        if (action == MoveNext || action == MovePrevious) {
            const int n    = rowCount();
            const int step = action == MoveNext ? 1 : -1;
            int       r    = currentIndex().isValid() ? currentRow()
                                                      : (step > 0 ? -1 : n);
            for (int i = 0; i < n; ++i) {
                r = (r + step + n) % n;
                if (editable(r)) return model()->index(r, _inputColumn);
            }
        }
        return QTableWidget::moveCursor(action, mods);
    }

    void closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) override
    {
        // Enter: on to the next position below, if there is one.
        if (hint == QAbstractItemDelegate::SubmitModelCache)
            for (int r = currentRow() + 1; r < rowCount(); ++r)
                if (editable(r)) {
                    hint = QAbstractItemDelegate::EditNextItem;
                    break;
                }
        QTableWidget::closeEditor(editor, hint);
    }

  private:
    bool editable(int row) const
    {
        const QTableWidgetItem* it = item(row, _inputColumn);
        return it && (it->flags() & Qt::ItemIsEditable);
    }

    int _inputColumn;
};

const int SignGlyphRole = Qt::UserRole + 1; ///< Almagest codepoint of the sign
const int RetroRole     = Qt::UserRole + 2; ///< draw a retrograde mark

/// Draws a position as degrees followed by the sign's Almagest glyph. The
/// item's text keeps the sign's abbreviation -- that is what gets edited and
/// copied -- and only the painting swaps it for the glyph.
///
/// The degrees are right-aligned against a fixed slot for the glyph and the
/// retrograde mark, so the figures line up down the column.
class PositionDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        // Background, selection and focus; the text too when there is no
        // glyph (a typed position), as initStyleOption() blanks it otherwise.
        QStyledItemDelegate::paint(painter, option, index);
        const int glyph = index.data(SignGlyphRole).toInt();
        if (!glyph) return;

        QStyleOptionViewItem opt = option;
        QStyledItemDelegate::initStyleOption(&opt, index);
        const Slots   sl      = slotsFor(opt);
        // "9°51' Can"; or just "Can" for a sign by itself: the glyph alone.
        const QString degrees =
            opt.text.contains(' ') ? opt.text.section(' ', 0, 0) : QString();
        const QRect   r       = option.rect.adjusted(sl.margin, 0, -sl.margin, 0);
        const int     glyphX  = r.right() - sl.retro - sl.glyph;

        painter->save();
        painter->setClipRect(option.rect);
        painter->setPen(opt.palette.color((opt.state & QStyle::State_Selected)
                                              ? QPalette::HighlightedText
                                              : QPalette::Text));
        painter->setFont(opt.font);
        painter->drawText(QRect(r.left(), r.top(), glyphX - sl.gap - r.left(),
                                r.height()),
                          Qt::AlignRight | Qt::AlignVCenter, degrees);
        if (index.data(RetroRole).toBool())
            painter->drawText(QRect(r.right() - sl.retro, r.top(), sl.retro,
                                    r.height()),
                              Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("R"));
        painter->setFont(glyphFont(opt.font));
        painter->drawText(QRect(glyphX, r.top(), sl.glyph, r.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, QString(QChar(glyph)));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize sz = QStyledItemDelegate::sizeHint(option, index);
        if (index.data(SignGlyphRole).toInt()) {
            QStyleOptionViewItem opt = option;
            QStyledItemDelegate::initStyleOption(&opt, index);
            const Slots sl = slotsFor(opt);
            sz.setWidth(2 * sl.margin
                        + opt.fontMetrics.horizontalAdvance(opt.text.section(' ', 0, 0))
                        + sl.gap + sl.glyph + sl.retro);
        }
        return sz;
    }

  protected:
    void initStyleOption(QStyleOptionViewItem* option,
                         const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        // paint() draws degrees and glyph itself.
        if (index.data(SignGlyphRole).toInt()) option->text.clear();
    }

  private:
    struct Slots {
        int margin, gap, glyph, retro;
    };

    static Slots slotsFor(const QStyleOptionViewItem& opt)
    {
        const QFontMetrics gfm(glyphFont(opt.font));
        Slots              s;
        s.margin = opt.fontMetrics.horizontalAdvance(' ') + 2;
        s.gap    = opt.fontMetrics.horizontalAdvance(' ') / 2;
        // Wide enough for any sign: the widest are about as wide as tall.
        s.glyph = gfm.height();
        s.retro = opt.fontMetrics.horizontalAdvance(QStringLiteral(" R"));
        return s;
    }

    static QFont glyphFont(const QFont& base)
    {
        QFont f("Almagest");
        f.setPointSizeF((base.pointSizeF() > 0 ? base.pointSizeF() : 10) * 1.2);
        return f;
    }
};

} // namespace

/// Header with a tick box over the first column that ticks or unticks every
/// row at once (shown only while searching).
class PlanetTable::CheckHeader : public QHeaderView {
  public:
    explicit CheckHeader(QWidget* parent) : QHeaderView(Qt::Horizontal, parent) { }

    std::function<void()> onToggle;

    void setBox(bool shown, Qt::CheckState state)
    {
        if (shown == _shown && state == _state) return;
        _shown = shown;
        _state = state;
        viewport()->update();
    }

  protected:
    void paintSection(QPainter* painter, const QRect& rect, int index) const override
    {
        painter->save();
        QHeaderView::paintSection(painter, rect, index);
        painter->restore();
        if (index != 0 || !_shown) return;

        QStyleOptionButton box;
        const int side = style()->pixelMetric(QStyle::PM_IndicatorWidth);
        box.rect  = QRect(rect.center().x() - side / 2, rect.center().y() - side / 2,
                          side, side);
        box.state = QStyle::State_Enabled
                    | (_state == Qt::Checked            ? QStyle::State_On
                       : _state == Qt::PartiallyChecked ? QStyle::State_NoChange
                                                        : QStyle::State_Off);
        style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &box, painter);
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (_shown && e->button() == Qt::LeftButton
            && logicalIndexAt(e->pos()) == 0 && onToggle)
        {
            onToggle();
            return;
        }
        QHeaderView::mousePressEvent(e);
    }

  private:
    bool           _shown = false;
    Qt::CheckState _state = Qt::Unchecked;
};

PlanetTable::PlanetTable(QWidget* parent) : AstroFileHandler(parent)
{
    _table = new PositionTable(0, numColumns, colChart1);
    _header = new CheckHeader(_table);
    _table->setHorizontalHeader(_header);
    _table->setItemDelegateForColumn(colChart1, new PositionDelegate(_table));
    _table->setItemDelegateForColumn(colChart2, new PositionDelegate(_table));
    _table->setObjectName("planetTable");
    _table->setSelectionBehavior(QAbstractItemView::SelectItems);
    _table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _table->setAlternatingRowColors(true);
    _table->verticalHeader()->setVisible(false);
    _table->setShowGrid(false);
    _table->setIconSize(QSize(18, 18));
    _table->setHorizontalHeaderLabels({ QString(), tr("Body"), tr("Chart 1"),
                                        tr("Chart 2"), tr("Decl"), tr("RA"),
                                        tr("Mundane"), tr("H") });
    _table->horizontalHeaderItem(colHouse)->setToolTip(tr("House (chart 1)"));
    _table->horizontalHeaderItem(colMundane)->setToolTip(
        tr("Position round the mundane circle of the current direction system "
           "(0° at the Ascendant)"));
    _header->setSectionResizeMode(QHeaderView::ResizeToContents);
    // Every column keeps to its contents; the spare width stays empty
    // rather than going to whichever column happens to be last.
    _header->setStretchLastSection(false);
    _header->setToolTip(tr("Right-click to choose the columns"));
    _header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_header, &QWidget::customContextMenuRequested, this,
            &PlanetTable::headerMenu);
    _header->onToggle = [this] {
        // Anything unticked: tick the rest. All ticked: untick them all.
        bool all = true;
        for (A::PlanetId id : rowIds()) all = all && _targets.value(id).use;
        checkAll(!all);
    };

    // Ctrl+C copies the selected cells (QTableWidget has no built-in copy)
    auto* copySc = new QShortcut(QKeySequence::Copy, _table);
    copySc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copySc, &QShortcut::activated, this, &PlanetTable::copySelection);

    // Sits under the table: at the foot of the dock while the search panel
    // is hidden, and heading the panel once it is shown.
    _searchBtn = new QPushButton(tr("Search >>"));
    _searchBtn->setObjectName("planetSearchToggle");
    _searchBtn->setCheckable(true);
    _searchBtn->setToolTip(
        tr("Type in positions and find the charts that have them"));

    auto* top = new QHBoxLayout;
    top->addWidget(_searchBtn);
    top->addStretch();

    /* ---- search panel ---- */

    _searchPanel = new QWidget;
    _searchPanel->setObjectName("planetSearchPanel");

    _zodiac = new QComboBox;
    _zodiac->setObjectName("planetSearchZodiac");
    for (const A::Zodiac& z : A::getZodiacs()) _zodiac->addItem(z.name, z.id);
    _zodiac->setToolTip(tr("Zodiac the typed positions are given in"));

    _orb = new QDoubleSpinBox;
    _orb->setRange(0.01, 15.0);
    _orb->setDecimals(2);
    _orb->setSingleStep(0.25);
    _orb->setValue(1.0);
    _orb->setSuffix(tr("°"));
    _orb->setToolTip(tr("How far a body may be from its typed position"));

    _misses = new QSpinBox;
    _misses->setRange(0, 3);
    _misses->setToolTip(
        tr("Number of bodies allowed to be out of orb; old tables were "
           "often degrees off for Mercury or Mars"));

    _yearFrom = new QSpinBox;
    _yearTo   = new QSpinBox;
    for (QSpinBox* sb : { _yearFrom, _yearTo }) {
        sb->setRange(-5400, 5399);
        sb->setToolTip(tr("Astronomical year numbering: 0 is 1 BC, -1 is 2 BC"));
    }
    _yearFrom->setValue(-1000);
    _yearTo->setValue(QDate::currentDate().year());

    _usePlace = new QCheckBox(tr("Known place"));
    _usePlace->setToolTip(
        tr("With a known place, Asc/MC fix the time of day. Without one, "
           "they are solved for the latitude and longitude instead."));
    _place = new GeoSearchWidget(false /*hbox*/);
    _place->setEnabled(false);
    connect(_usePlace, &QCheckBox::toggled, _place, &QWidget::setEnabled);

    _find  = new QPushButton(tr("Find"));
    _find->setObjectName("planetSearchFind");
    _find->setDefault(true);

    _progress = new QProgressBar;
    _progress->setRange(0, 1000);
    _progress->setTextVisible(false);
    _progress->setVisible(false);
    _status = new QLabel;
    _status->setWordWrap(true);

    _results = new QTreeWidget;
    _results->setObjectName("planetSearchResults");
    _results->setRootIsDecorated(false);
    _results->setAlternatingRowColors(true);
    _results->setUniformRowHeights(true);
    _results->setHeaderLabels({ tr("Date"), tr("UT"), tr("Fit"), tr("LMT"),
                                tr("Zone"), tr("Lat"), tr("Long"), tr("Place") });
    _results->headerItem()->setToolTip(
        4, tr("Clock (zone) time at the place, with its offset from UT; "
              "given for dates from 1900 when the place is known or suggested"));
    _results->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _results->headerItem()->setToolTip(
        2, tr("RMS deviation as a fraction of the orb; 0 is a perfect fit"));
    _results->setToolTip(tr("Select a match to show it; double-click or "
                            "middle-click to open it in a tab of its own"));
    _results->viewport()->installEventFilter(this); // middle click

    auto* orbRow = new QHBoxLayout;
    orbRow->addWidget(_orb);
    orbRow->addSpacing(8);
    orbRow->addWidget(new QLabel(tr("Misses:")));
    orbRow->addWidget(_misses);
    orbRow->addStretch();

    auto* yearRow = new QHBoxLayout;
    yearRow->addWidget(_yearFrom);
    yearRow->addWidget(new QLabel(tr("to")));
    yearRow->addWidget(_yearTo);
    yearRow->addStretch();

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Zodiac:"), _zodiac);
    form->addRow(tr("Orb:"), orbRow);
    form->addRow(tr("Years:"), yearRow);
    form->addRow(_usePlace, _place);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(_find);

    auto* panel = new QVBoxLayout(_searchPanel);
    panel->setContentsMargins(0, 0, 0, 0);
    panel->addLayout(form);
    panel->addLayout(buttons);
    panel->addWidget(_progress);
    panel->addWidget(_status);
    panel->addWidget(_results, 1);
    _searchPanel->setVisible(false);

    auto* main = new QVBoxLayout(this);
    main->setContentsMargins(0, 5, 0, 0);
    main->setSpacing(5);
    main->addWidget(_table, 3);
    main->addLayout(top);
    main->addWidget(_searchPanel, 2);

    connect(_searchBtn, &QPushButton::toggled, this, &PlanetTable::setSearchMode);
    connect(_table, &QTableWidget::cellClicked, this, &PlanetTable::onCellClicked);
    connect(_table, &QTableWidget::itemChanged, this, &PlanetTable::onItemChanged);
    connect(_zodiac, QOverload<int>::of(&QComboBox::activated), this,
            [this] { _zodiacFollows = false; });
    // The position columns are shown in the search zodiac while searching.
    connect(_zodiac, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] {
                if (!_searchMode) return;
                updateColumns();
                rebuild();
            });
    connect(_find, &QPushButton::clicked, this, &PlanetTable::startSearch);
    connect(_results, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* it) { openHit(it); });
    // Arrow keys as well as clicks: browse the matches.
    connect(_results, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* it) { previewHit(it); });

    updateColumns();
    rebuild(true);
}

PlanetTable::~PlanetTable()
{
    stopSearch();
}

bool
PlanetTable::eventFilter(QObject* watched, QEvent* event)
{
    // Middle click on a match: open it in a tab of its own, as chart 1,
    // leaving the selection (and so the previewed chart) alone.
    if (watched == _results->viewport()
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease))
    {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::MiddleButton) {
            if (event->type() == QEvent::MouseButtonRelease)
                if (QTreeWidgetItem* it = _results->itemAt(me->pos())) openHit(it);
            return true;
        }
    }
    return AstroFileHandler::eventFilter(watched, event);
}

void
PlanetTable::stopSearch()
{
    if (!_searcher) return;
    _searcher->disconnect(this);
    _searcher->cancel();
    _searcher->wait();
    delete _searcher;
    _searcher = nullptr;
}

/* ================================ the table =============================== */

QList<A::PlanetId>
PlanetTable::rowIds() const
{
    QList<A::PlanetId> ids { A::Planet_Asc, A::Planet_MC };
    QList<A::PlanetId> bodies;
    for (int i = 0; i < filesCount(); ++i)
        for (A::PlanetId id : file(i)->horoscope().planets.keys())
            if (id >= A::Planets_Start && id < A::Planets_End
                && !bodies.contains(id))
                bodies << id;
    // No chart open: still offer the usual bodies, so a search can be typed.
    if (bodies.isEmpty()) bodies = A::getPlanets(false, true);
    std::sort(bodies.begin(), bodies.end());
    return ids << bodies;
}

int
PlanetTable::rowOf(A::PlanetId id) const
{
    for (int r = 0; r < _table->rowCount(); ++r)
        if (_table->item(r, colBody)->data(Qt::UserRole).toInt() == id) return r;
    return -1;
}

void
PlanetTable::updateColumns()
{
    _table->setColumnHidden(colChart2, filesCount() < 2);
    for (int c : { colDecl, colRA, colMundane, colHouse })
        _table->setColumnHidden(c, _hiddenCols.contains(c));
    // While searching, the columns are read (and typed) in the search zodiac.
    const QString zod = _zodiac->currentText();
    _table->horizontalHeaderItem(colChart1)->setText(
        !_searchMode ? tr("Chart 1") : filesCount() ? tr("Chart 1 (%1)").arg(zod) : zod);
    _table->horizontalHeaderItem(colChart1)->setToolTip(
        _searchMode ? tr("Tick a body to search for its position; type over it, "
                         "e.g. 15 Leo, 15°23' Leo, 15Le23 or 135.4 -- or just "
                         "Leo when only the sign is known")
                    : QString());
}

bool
PlanetTable::hasBase() const
{
    return filesCount() > 0 && file(0) != _preview.data();
}

bool
PlanetTable::chart1IsBaseChart() const
{
    if (!hasBase()) return false;
    // The same set the Events panel treats as a natal reference (see
    // Transits::transitsOnly()).
    const FileType t = file(0)->getType();
    return t == TypeMale || t == TypeFemale || t == TypeEvent || t == TypeReturn
           || t == TypeComposite;
}

void
PlanetTable::headerMenu(const QPoint& pos)
{
    QMenu menu(this);
    for (int c : { colDecl, colRA, colMundane, colHouse }) {
        QAction* a = menu.addAction(
            c == colDecl   ? tr("Declination")
            : c == colRA   ? tr("Right ascension")
            : c == colMundane ? tr("Mundane position")
                              : tr("House"));
        a->setCheckable(true);
        a->setChecked(!_hiddenCols.contains(c));
        connect(a, &QAction::toggled, this, [this, c](bool on) {
            if (on) _hiddenCols.remove(c);
            else    _hiddenCols.insert(c);
            updateColumns();
        });
    }
    menu.exec(_header->viewport()->mapToGlobal(pos));
}

const A::Zodiac&
PlanetTable::shownZodiac(int fi) const
{
    if (_searchMode || fi >= filesCount()) return A::getZodiac(searchZodiac());
    return file(fi)->horoscope().zodiac;
}

bool
PlanetTable::shownPosition(int fi, A::PlanetId id, double& lon, bool& retro) const
{
    if (fi >= filesCount() || !positionOf(file(fi), id, lon, retro)) return false;
    if (_searchMode) {
        const A::InputData& in = file(fi)->horoscope().inputData;
        lon = A::convertZodiacLongitude(
            lon, file(fi)->getZodiac(), searchZodiac(),
            A::getJulianDate(in.GMT(), false, in.calendarType()));
    }
    return true;
}

void
PlanetTable::rebuild(bool force)
{
    A::modalize<bool> updating(_updating, true);

    const QList<A::PlanetId> ids = rowIds();
    QList<A::PlanetId>       have;
    for (int r = 0; r < _table->rowCount(); ++r)
        have << _table->item(r, colBody)->data(Qt::UserRole).toInt();

    // Only tear the rows down when the set of bodies changes: a chart update
    // must not close the editor of a position being typed.
    if (force || have != ids) {
        QFont       glyphFont("Almagest");
        const qreal pt = _table->font().pointSizeF();
        glyphFont.setPointSizeF((pt > 0 ? pt : 10) * 1.3);

        _table->clearContents();
        _table->setRowCount(ids.size());
        for (int r = 0; r < ids.size(); ++r) {
            const A::PlanetId id = ids[r];
            // Asc and MC are in the catalogue too, with Almagest glyphs.
            const A::Planet& p = A::getPlanet(id);

            auto*          glyph = new QTableWidgetItem;
            const QVariant fc    = p.userData.value("fontChar");
            glyph->setText(fc.toInt() ? QString(QChar(fc.toInt())) : fc.toString());
            glyph->setFont(glyphFont);
            glyph->setTextAlignment(Qt::AlignCenter);
            _table->setItem(r, colGlyph, glyph);

            auto* body = new QTableWidgetItem(p.name);
            body->setData(Qt::UserRole, id);
            body->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            _table->setItem(r, colBody, body);

            for (int c : { colChart1, colChart2, colDecl, colRA, colMundane,
                           colHouse }) {
                auto* it = new QTableWidgetItem;
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                it->setTextAlignment((c == colHouse ? Qt::AlignHCenter : Qt::AlignRight)
                                     | Qt::AlignVCenter);
                _table->setItem(r, c, it);
            }
        }
    }

    for (int r = 0; r < ids.size(); ++r) refreshRow(r);

    int used = 0;
    for (A::PlanetId id : ids) used += _targets.value(id).use;
    _header->setBox(_searchMode, used == 0            ? Qt::Unchecked
                                 : used == ids.size() ? Qt::Checked
                                                      : Qt::PartiallyChecked);
}

void
PlanetTable::refreshRow(int r)
{
    A::modalize<bool> updating(_updating, true);
    const A::PlanetId id = _table->item(r, colBody)->data(Qt::UserRole).toInt();
    const TargetRow   t  = _targets.value(id);

    QTableWidgetItem* glyph = _table->item(r, colGlyph);
    if (_searchMode) {
        glyph->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                        | Qt::ItemIsUserCheckable);
        glyph->setCheckState(t.use ? Qt::Checked : Qt::Unchecked);
    } else {
        glyph->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        glyph->setData(Qt::CheckStateRole, QVariant());
    }

    for (int fi = 0; fi < 2; ++fi) {
        QTableWidgetItem* it = _table->item(r, fi == 0 ? colChart1 : colChart2);
        QString           text;
        double            lon;
        bool              retro = false;
        int               sign  = 0;
        if (shownPosition(fi, id, lon, retro)) {
            const A::Zodiac& zod = shownZodiac(fi);
            text = A::zodiacPosition(float(lon), zod, A::HighPrecision);
            sign = A::getSign(float(lon), zod).userData.value("fontChar").toInt();
        }
        if (fi == 0) {
            const bool input = _searchMode && t.use;
            // Typed positions win; with no chart to fall back on they are
            // all there is. They are shown as typed.
            if (_searchMode && !t.text.isEmpty() && (t.edited || !hasBase())) {
                text = t.text;
                sign = 0;
                // Written the table's way (see snapPosition): show the
                // sign's glyph, as for a position taken from a chart.
                const A::Zodiac& zod = A::getZodiac(searchZodiac());
                double           typed;
                if (A::parseZodiacPosition(text, zod, &typed)) {
                    const A::ZodiacSign& s = A::getSign(float(typed), zod);
                    if (text == s.tag
                        || (text.endsWith(' ' + s.tag) && text.count(' ') == 1))
                        sign = s.userData.value("fontChar").toInt();
                }
            }
            Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
            if (input) f |= Qt::ItemIsEditable;
            it->setFlags(f);
            QFont font = it->font();
            font.setBold(input);
            it->setFont(font);
            if (!_searchMode) it->setToolTip(QString());
        }
        if (it->text() != text) it->setText(text);
        it->setData(SignGlyphRole, sign);
        // No retrograde mark while searching: the cell is an input there.
        it->setData(RetroRole, retro && !_searchMode);
    }

    QString house, decl, ra, mundane;
    if (filesCount()) {
        double a, d, m;
        if (equatorialOf(file(0), id, a, d, m)) {
            ra      = A::degreeToString(float(a), A::HighPrecision);
            decl    = A::formatLatitude(float(d), A::HighPrecision);
            mundane = A::degreeToString(float(m), A::HighPrecision);
        }
        if (!isAngle(id)) {
            const auto& planets = file(0)->horoscope().planets;
            auto        pit     = planets.constFind(id);
            if (pit != planets.constEnd()) house = A::houseNum(*pit);
        }
    }
    _table->item(r, colDecl)->setText(decl);
    _table->item(r, colRA)->setText(ra);
    _table->item(r, colMundane)->setText(mundane);
    _table->item(r, colHouse)->setText(house);
}

void
PlanetTable::filesUpdated(MembersList)
{
    // While scrubbing (wheel drag / animation) skip the refill; the catch-up
    // recompute on scrub-exit refreshes it once.
    if (A::isScrubbing()) return;

    if (_zodiacFollows && filesCount()) {
        const int i = _zodiac->findData(file(0)->getZodiac());
        if (i >= 0) _zodiac->setCurrentIndex(i);
    }
    updateColumns();
    rebuild();
}

void
PlanetTable::setCurrentPlanet(A::PlanetId id, int)
{
    if (_searchMode || _inSelect) return;
    const int r = rowOf(id);
    if (r < 0) return;
    A::modalize<bool> selecting(_inSelect, true);
    _table->clearSelection();
    _table->setCurrentCell(r, colBody);
}

void
PlanetTable::onCellClicked(int row, int column)
{
    if (_searchMode || _inSelect) return;
    const A::PlanetId id = _table->item(row, colBody)->data(Qt::UserRole).toInt();
    // The angles are not planets as far as the other panels are concerned
    // (they are absent from Horoscope::planets).
    if (isAngle(id)) return;
    const int fi = (column == colChart2 && filesCount() > 1) ? 1 : 0;
    A::modalize<bool> selecting(_inSelect, true);
    emit planetSelected(id, fi);
}

void
PlanetTable::onItemChanged(QTableWidgetItem* item)
{
    if (_updating || !_searchMode) return;
    const int         row = item->row();
    const A::PlanetId id  = _table->item(row, colBody)->data(Qt::UserRole).toInt();
    TargetRow&        t   = _targets[id];

    if (item->column() == colGlyph) {
        t.use = item->checkState() == Qt::Checked;
        // Unticking hands the row back to the chart; with no chart to fall
        // back on, what was typed stays.
        if (!t.use && hasBase()) {
            t.edited = false;
            t.text.clear();
        }
    } else if (item->column() == colChart1) {
        t.text   = snapPosition(item->text().trimmed(),
                                A::getZodiac(searchZodiac()));
        t.edited = !t.text.isEmpty(); // emptied: back to the chart's value
        double     deg;
        const bool bad =
            t.edited
            && !A::parseZodiacPosition(t.text, A::getZodiac(searchZodiac()), &deg);
        A::modalize<bool> updating(_updating, true);
        item->setToolTip(bad ? tr("Can't read this position") : QString());
        setStatus(bad ? tr("Can't read the position \"%1\".").arg(t.text)
                      : QString());
    } else {
        return;
    }
    rebuild(); // the row, and the header's tick box
}

void
PlanetTable::copySelection()
{
    // Tab-delimited grid spanning the selection (as Speculum::copySelection)
    const auto items = _table->selectedItems();
    if (items.isEmpty()) return;

    QMap<int, QMap<int, QString>> cells; // row -> col -> text
    for (const QTableWidgetItem* it : items)
        cells[it->row()][it->column()] = it->text().trimmed();

    QStringList lines;
    for (auto rit = cells.constBegin(); rit != cells.constEnd(); ++rit) {
        QStringList fields;
        for (int c = rit->firstKey(); c <= rit->lastKey(); ++c)
            if (!_table->isColumnHidden(c)) fields << rit->value(c);
        lines << fields.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
}

/* ================================= search ================================= */

A::ZodiacId
PlanetTable::searchZodiac() const
{
    return _zodiac->currentData().toInt();
}

void
PlanetTable::setStatus(const QString& text)
{
    _status->setText(text);
    _status->setVisible(!text.isEmpty());
}

void
PlanetTable::setSearchMode(bool on)
{
    _searchMode = on;
    _searchPanel->setVisible(on);
    _searchBtn->setText(on ? tr("Search <<") : tr("Search >>"));
    if (on) {
        // Start from "this chart": everything ticked, unless some rows were
        // already chosen. The header's tick box clears them in one click.
        bool any = false;
        for (const TargetRow& t : std::as_const(_targets)) any = any || t.use;
        if (!any)
            for (A::PlanetId id : rowIds()) _targets[id].use = true;
    }
    _table->clearSelection();
    updateColumns();
    rebuild(true);
}

void
PlanetTable::checkAll(bool on)
{
    for (A::PlanetId id : rowIds()) {
        TargetRow& t = _targets[id];
        t.use        = on;
        // As when a single row is unticked: back to the chart's value.
        if (!on && hasBase()) {
            t.edited = false;
            t.text.clear();
        }
    }
    setStatus(QString());
    rebuild();
}

void
PlanetTable::startSearch()
{
    if (_searcher) { // the button reads "Cancel" while a search runs
        _searcher->cancel();
        return;
    }

    // Commit a cell still being edited.
    _table->setCurrentItem(nullptr);

    // Chart 1 is not a base chart, so showing a match will replace it: fix
    // the positions taken from it now, as if they had been typed, so that
    // browsing the matches leaves the search as it was asked.
    if (hasBase() && !chart1IsBaseChart()) {
        for (A::PlanetId id : rowIds()) {
            TargetRow& t = _targets[id];
            double     lon;
            bool       retro;
            if (!t.use || t.edited || !shownPosition(0, id, lon, retro)) continue;
            t.text   = A::zodiacPosition(float(lon), shownZodiac(0), A::HighPrecision);
            t.edited = true;
        }
        rebuild();
    }

    A::ChartSearchSpec spec;
    spec.zodiac            = searchZodiac();
    const A::Zodiac& zod   = A::getZodiac(spec.zodiac);
    const double     base  = _orb->value();

    for (A::PlanetId id : rowIds()) {
        const TargetRow t = _targets.value(id);
        if (!t.use) continue;

        const QString name = _table->item(rowOf(id), colBody)->text();
        double        lon, orb = base, flat = 0;
        // Taken from the chart: good to the arcsecond it is displayed to.
        double        precision = 0.5 / 3600.0;
        bool          retro;
        if (!t.text.isEmpty() && (t.edited || !hasBase())) {
            double unit;
            if (!A::parseZodiacPosition(t.text, zod, &lon, &unit)) {
                setStatus(tr("Can't read the position for %1: \"%2\".")
                              .arg(name, t.text));
                return;
            }
            // A position stated to the degree cannot be held to a tighter
            // orb than half a degree, whatever the Orb box says.
            orb       = qMax(base, unit / 2);
            precision = unit / 2;
            if (unit > 1.5) {
                // A sign by itself: anywhere in the sign fits, and the Orb
                // is how far past its edges still counts.
                flat = unit / 2;
                orb  = base;
            }
        } else if (!hasBase() || !shownPosition(0, id, lon, retro)) {
            continue; // ticked, but nothing typed and no chart to take it from
        }

        if (id == A::Planet_Asc) {
            spec.hasAsc = true;
            spec.asc    = lon;
            spec.ascOrb = orb;
            spec.ascPrecision = precision;
            spec.ascFlat      = flat;
        } else if (id == A::Planet_MC) {
            spec.hasMC = true;
            spec.mc    = lon;
            spec.mcOrb = orb;
            spec.mcPrecision = precision;
            spec.mcFlat      = flat;
        } else {
            A::ChartSearchTarget target;
            target.id  = id;
            target.lon = lon;
            target.orb = orb;
            target.precision = precision;
            target.flat      = flat;
            spec.targets << target;
        }
    }

    if (spec.targets.isEmpty()) {
        setStatus(tr("Tick at least one planet and give its position. Asc "
                     "and MC alone cannot date a chart."));
        return;
    }
    if (_yearFrom->value() > _yearTo->value()) {
        setStatus(tr("The first year is after the last."));
        return;
    }
    spec.jdFrom        = A::julianDayForYear(_yearFrom->value());
    spec.jdTo          = A::julianDayForYear(_yearTo->value() + 1);
    spec.allowedMisses = qMin(_misses->value(), int(spec.targets.size()) - 1);

    _hitsPlaceName.clear();
    _hitsPlaceTz.clear();
    if (_usePlace->isChecked()) {
        spec.hasLocation = true;
        spec.location    = _place->location();
        _hitsPlaceName   = _place->locationName();
        _hitsPlaceTz     = _place->selectedTimezoneId();
    }
    spec.prepare();

    _results->clear();
    _hits.clear();
    setStatus(tr("Searching…"));
    _progress->setValue(0);
    _progress->setVisible(true);
    _find->setText(tr("Cancel"));

    _searcher = new A::ChartSearcher(spec, this);
    connect(_searcher, &A::ChartSearcher::progress, this,
            [this](double f) { _progress->setValue(int(f * 1000)); });
    connect(_searcher, &QThread::finished, this, &PlanetTable::searchFinished);
    _searcher->start();
}

void
PlanetTable::searchFinished()
{
    if (!_searcher) return;
    const bool cancelled = _searcher->cancelled();
    const bool truncated = _searcher->truncated();
    _hits                = _searcher->hits();
    _searcher->deleteLater();
    _searcher = nullptr;

    _progress->setVisible(false);
    _find->setText(tr("Find"));

    for (const A::ChartSearchHit& h : std::as_const(_hits)) {
        const QDateTime dt   = A::dateTimeFromJulian(h.jdUt);
        auto*           it   = new QTreeWidgetItem;

        it->setText(0, dateString(dt, h.jdUt));
        if (h.hasTime) {
            it->setText(1, QString("%1 %2")
                               .arg(dt.time().toString("HH:mm"), spanString(h.timeErr))
                               .trimmed());
        } else {
            it->setText(1, spanString(h.timeErr));
            it->setToolTip(1, tr("Nothing given moves fast enough to fix the "
                                 "time of day; add the Moon"));
        }
        QString fit = QString::number(h.score, 'f', 2);
        if (h.misses) fit += tr(" (%1 out)").arg(h.misses);
        it->setText(2, fit);
        if (h.hasLmt) it->setText(3, clockString(h.lmtHours));

        const QString tzId = _hitsPlaceTz.isEmpty() ? h.placeTimezoneId : _hitsPlaceTz;
        double        zone;
        if (h.hasTime && zoneOffset(tzId, dt, zone)) {
            const double utHours = dt.time().msecsSinceStartOfDay() / 3600000.0;
            it->setText(4, QString("%1 (%2)")
                               .arg(clockString(utHours + zone + 24.0),
                                    offsetString(zone)));
            it->setToolTip(4, tzId);
        }

        // Stated to the precision they are known to: a tenth of a degree
        // of uncertainty and upwards is spelled out.
        auto withErr = [](QString s, double err) {
            if (err >= 0.05) s += QString(" ±%1°").arg(err, 0, 'f', err < 1 ? 1 : 0);
            return s;
        };
        if (h.hasLat)
            it->setText(5, withErr(A::formatLatitude(float(h.lat), A::NormalPrecision),
                                   h.latErr));
        if (h.hasLon) {
            it->setText(6, withErr(A::formatLongitude(float(h.lon), A::NormalPrecision),
                                   h.lonErr));
        } else if (h.hasLmt) {
            it->setToolTip(6, tr("The longitude follows from the UT, which the "
                                 "positions given do not fix closely enough"));
        }
        it->setText(7, !h.place.isEmpty()   ? h.place
                       : !h.note.isEmpty()  ? h.note
                                            : _hitsPlaceName);
        QStringList tip;
        if (!h.place.isEmpty())
            tip << tr("The solved position is %1 from %2.")
                       .arg(distanceString(distanceKm(h.lat, h.lon, h.placeLat,
                                                      h.placeLon)),
                            h.place);
        if (!h.note.isEmpty()) tip << h.note;
        it->setToolTip(7, tip.join('\n'));
        _results->addTopLevelItem(it);
    }

    if (cancelled)
        setStatus(tr("Search cancelled."));
    else if (_hits.isEmpty())
        setStatus(tr("No chart in that range matches. Try a wider orb, or "
                     "allow a miss."));
    else if (truncated)
        setStatus(tr("Too many matches; the best %1 are shown. Add a body "
                     "or narrow the orb.").arg(_hits.size()));
    else
        setStatus(_hits.size() == 1
                      ? tr("1 match. Select it to show it; double-click or "
                           "middle-click to open it in its own tab.")
                      : tr("%1 matches. Select one to show it; double-click or "
                           "middle-click to open it in its own tab.")
                            .arg(_hits.size()));
}

void
PlanetTable::configureFile(AstroFile* af, const A::ChartSearchHit& h)
{
    const QDateTime gmt = A::dateTimeFromJulian(h.jdUt);

    QString name = tr("Found %1").arg(dateString(gmt, h.jdUt));
    if (h.hasTime) name += gmt.time().toString(" HHmm");
    af->setName(name);

    QStringList comment { tr("Found by position search (fit %1).")
                              .arg(h.score, 0, 'f', 2) };

    if (h.hasLat && h.hasLon) {
        // The solved coordinates, not the nearby city's: they are what
        // reproduce the typed angles.
        af->setLocation(QVector3D(float(h.lon), float(h.lat), 0));
        if (!_hitsPlaceName.isEmpty())
            af->setLocationName(_hitsPlaceName);
        else if (!h.place.isEmpty())
            af->setLocationName(tr("near %1").arg(h.place));
        else
            af->setLocationName(tr("Solved from the angles"));
        if (!h.place.isEmpty())
            comment << tr("The solved position (%1, %2) is %3 from %4.")
                           .arg(A::formatLatitude(float(h.lat), A::NormalPrecision),
                                A::formatLongitude(float(h.lon), A::NormalPrecision),
                                distanceString(distanceKm(h.lat, h.lon, h.placeLat,
                                                          h.placeLon)),
                                h.place);
        if (h.lonErr >= 0.05)
            comment << tr("Longitude uncertain by about ±%1°.")
                           .arg(h.lonErr, 0, 'f', h.lonErr < 1 ? 1 : 0);

        // A modern chart reads in the zone time its place kept; otherwise in
        // local mean time, the only clock a place had before zone time.
        const QString tzId = _hitsPlaceTz.isEmpty() ? h.placeTimezoneId : _hitsPlaceTz;
        double        zone;
        if (h.hasTime && zoneOffset(tzId, gmt, zone)) {
            af->setTimeMode(A::Time_ZoneTime);
            af->setTimezone(zone);
            comment << tr("Clock time is %1 zone time (UT%2); local mean time "
                          "was %3. Check any daylight-saving time against a "
                          "historical source.")
                           .arg(tzId, offsetString(zone), clockString(h.lmtHours));
        } else {
            af->setTimezone(h.lon / 15.0);
            af->setTimeMode(A::Time_LMT);
        }
    } else if (chart1IsBaseChart() && af != file(0)) {
        // Place unknown: borrow the base chart's, so the two can be compared.
        af->setLocation(file(0)->getLocation());
        af->setLocationName(file(0)->getLocationName());
        af->setTimezone(file(0)->getTimezone());
        af->setTimeMode(file(0)->getTimeMode());
        comment << tr("The place could not be determined; the base chart's "
                      "is used.");
    } else {
        comment << tr("The place could not be determined.");
    }
    if (!h.hasTime)
        comment << tr("Only the date is determined, not the time.");
    else if (h.timeErr * 1440.0 >= 5.0)
        comment << tr("The time shown is the middle of the span that fits (%1).")
                       .arg(spanString(h.timeErr));
    if (!h.note.isEmpty()) comment << h.note;

    af->setGMT(gmt);
    af->setComment(comment.join(' '));
}

void
PlanetTable::openHit(QTreeWidgetItem* item)
{
    const int i = _results->indexOfTopLevelItem(item);
    if (i < 0 || i >= _hits.size()) return;

    auto* aw = MainWindow::theAstroWidget();
    if (!aw) return;
    if (!A::dateTimeFromJulian(_hits[i].jdUt).isValid()) return;

    AstroFile* af = new AstroFile;
    af->suspendUpdate();
    configureFile(af, _hits[i]);
    // Fills in whatever is still unset (default location, display settings).
    aw->setupFile(af);
    af->resumeUpdate();

    emit addChart(af);
}

void
PlanetTable::previewHit(QTreeWidgetItem* item)
{
    const int i = item ? _results->indexOfTopLevelItem(item) : -1;
    if (i < 0 || i >= _hits.size()) return;

    auto* aw = MainWindow::theAstroWidget();
    if (!aw) return;
    if (!A::dateTimeFromJulian(_hits[i].jdUt).isValid()) return;

    // Still on show from the last match looked at: re-point it.
    if (_preview && files().contains(_preview.data())) {
        _preview->suspendUpdate();
        configureFile(_preview, _hits[i]);
        _preview->resumeUpdate();
        _preview->clearUnsavedState();
        return;
    }

    AstroFile* af = new AstroFile;
    af->suspendUpdate();
    configureFile(af, _hits[i]);
    aw->setupFile(af);
    // Its place is part of the match: the Events panel must not move it to
    // the tab's observer location as it does an ordinary transit chart.
    af->setLocationPinned(true);
    af->resumeUpdate();
    af->clearUnsavedState();
    _preview = af;

    // Beside a base chart, whose positions the search may still be following;
    // in place of a chart 1 that is not one; in a new tab when nothing is open.
    if (!filesCount())            emit addChart(af);
    else if (chart1IsBaseChart()) emit updateSecond(af);
    else                          emit updateFirst(af);
}

/* ================================ settings ================================ */

AppSettings
PlanetTable::defaultSettings()
{
    AppSettings s;
    s.setValue("PlanetTable/orb", 1.0);
    s.setValue("PlanetTable/misses", 0);
    s.setValue("PlanetTable/yearFrom", -1000);
    s.setValue("PlanetTable/yearTo", QDate::currentDate().year());
    s.setValue("PlanetTable/zodiac", -1); // follow the tab's zodiac
    s.setValue("PlanetTable/usePlace", false);
    s.setValue("PlanetTable/hiddenColumns", QString::number(colMundane));
    return s;
}

AppSettings
PlanetTable::currentSettings()
{
    AppSettings s;
    s.setValue("PlanetTable/orb", _orb->value());
    s.setValue("PlanetTable/misses", _misses->value());
    s.setValue("PlanetTable/yearFrom", _yearFrom->value());
    s.setValue("PlanetTable/yearTo", _yearTo->value());
    // -1 while the search zodiac still follows the tab's: only a zodiac that
    // was actually chosen is worth bringing back.
    s.setValue("PlanetTable/zodiac",
               _zodiacFollows ? -1 : _zodiac->currentData().toInt());
    s.setValue("PlanetTable/usePlace", _usePlace->isChecked());
    const QVector3D loc = _place->location();
    s.setValue("PlanetTable/placeLon", double(loc.x()));
    s.setValue("PlanetTable/placeLat", double(loc.y()));
    s.setValue("PlanetTable/placeName", _place->locationName());
    s.setValue("PlanetTable/placeTz", _place->selectedTimezoneId());
    QStringList hidden;
    for (int c : { colDecl, colRA, colMundane, colHouse })
        if (_hiddenCols.contains(c)) hidden << QString::number(c);
    s.setValue("PlanetTable/hiddenColumns", hidden.join(','));
    return s;
}

void
PlanetTable::applySettings(const AppSettings& s)
{
    // contains() guards: these keys are absent from sessions saved before
    // this panel existed, and value() would then reset the controls.
    if (s.contains("PlanetTable/orb"))
        _orb->setValue(s.value("PlanetTable/orb").toDouble());
    if (s.contains("PlanetTable/misses"))
        _misses->setValue(s.value("PlanetTable/misses").toInt());
    if (s.contains("PlanetTable/yearFrom"))
        _yearFrom->setValue(s.value("PlanetTable/yearFrom").toInt());
    if (s.contains("PlanetTable/yearTo"))
        _yearTo->setValue(s.value("PlanetTable/yearTo").toInt());
    if (s.contains("PlanetTable/zodiac")) {
        const int z = s.value("PlanetTable/zodiac").toInt();
        const int i = z < 0 ? -1 : _zodiac->findData(z);
        _zodiacFollows = i < 0;
        if (i >= 0) _zodiac->setCurrentIndex(i);
    }
    if (s.contains("PlanetTable/placeLon")) {
        const QVector3D loc(s.value("PlanetTable/placeLon").toFloat(),
                            s.value("PlanetTable/placeLat").toFloat(), 0);
        _place->setLocation(loc, s.value("PlanetTable/placeName").toString(),
                            s.value("PlanetTable/placeTz").toString());
    }
    if (s.contains("PlanetTable/usePlace"))
        _usePlace->setChecked(s.value("PlanetTable/usePlace").toBool());
    if (s.contains("PlanetTable/hiddenColumns")) {
        _hiddenCols.clear();
        const QStringList hidden = s.value("PlanetTable/hiddenColumns")
                                       .toString()
                                       .split(',', Qt::SkipEmptyParts);
        for (const QString& c : hidden) _hiddenCols.insert(c.toInt());
        updateColumns();
    }
}
