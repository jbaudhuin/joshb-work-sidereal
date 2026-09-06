#include "plain.h"
#include "../../zodiac/src/thememanager.h"
#include "../../astroprocessor/src/citydb.h"
#include <Astroprocessor/Output>
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QScrollBar>
#include <QTextBrowser>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QTextDocumentFragment>
#include <QTextFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
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
    void       mouseMoveEvent(QMouseEvent* event) override;
};

// Fixed-star names are wrapped (see astro-output.cpp's formatStarNameHtml) in
// an inert "star:<full constellation name>" anchor purely so we have
// somewhere to hang a hover tooltip; it's never meant to navigate (see
// setOpenLinks(false) in Plain's ctor). anchorAt() returns the href exactly
// as written, so no QUrl percent-encoding round-trip to worry about for
// constellation names containing spaces.
void
ReportBrowser::mouseMoveEvent(QMouseEvent* event)
{
    static const QString prefix = "star:";
    const QPoint          pos   = event->position().toPoint();
    const QString         href  = anchorAt(pos);
    QTextBrowser::mouseMoveEvent(event);
    if (href.startsWith(prefix)) {
        // Offset right/down from the cursor so the pointer doesn't sit on
        // top of the tooltip's left edge.
        QToolTip::showText(mapToGlobal(pos) + QPoint(20, 8),
                            href.mid(prefix.length()), this);
    } else if (!QToolTip::text().isEmpty()) {
        QToolTip::hideText();
    }
}

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

/* ============================== SECTION TOGGLE
 * ======================================== */

// Self-painted geometry: small "1"/"2" boxes floating on the frame's right edge.
static constexpr int kMiniW      = 16; ///< mini box width
static constexpr int kMiniH      = 16; ///< mini box height
static constexpr int kMiniGap    = 3;  ///< gap between the two minis
static constexpr int kMiniMargin = 4;  ///< inset from the frame's right edge
static constexpr int kGlyphPadL  = 6;  ///< padding left of the glyph
static constexpr int kGlyphPadR  = 6;  ///< padding right of the glyph (no minis)

/// Right-side width the glyph must avoid when the minis are shown.
static int miniZoneWidth()
{
    return kMiniGap + 2 * kMiniW + kMiniGap + kMiniMargin;  // lead gap + boxes
}

/// Linear blend of two colors (t=0 → a, t=1 → b).
static QColor blendColor(const QColor& a, const QColor& b, double t)
{
    return QColor(int(a.red()   + (b.red()   - a.red())   * t),
                  int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue()  + (b.blue()  - a.blue())  * t));
}

SectionToggle::SectionToggle(const QString& glyph,
                             const QString& tooltip,
                             QWidget*       parent)
    : QWidget(parent), _glyph(glyph)
{
    setToolTip(tooltip);
    setStatusTip(tooltip);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Emoji glyphs read best a bit larger; the text abbreviations (Dig/Dir/Par)
    // match the Events dock toolbar buttons at 9pt bold. Emoji chars live above
    // the BMP, so their QString starts with a high surrogate.
    const bool isEmoji = !_glyph.isEmpty() && _glyph.at(0).isHighSurrogate();
    _glyphFont = font();
    if (isEmoji) {
        _glyphFont.setPointSize(11);
    } else {
        _glyphFont.setPointSize(9);
        _glyphFont.setBold(true);
    }
    _miniFont = font();
    _miniFont.setPointSize(8);
    _miniFont.setBold(true);

    // Palette-derived defaults; the theme .qss overrides via qproperty-*.
    const QPalette pal = palette();
    _offColor         = pal.color(QPalette::Button);
    _textColor        = pal.color(QPalette::ButtonText);
    _borderColor      = pal.color(QPalette::Mid);
    _miniOffColor     = blendColor(pal.color(QPalette::Button),
                                   pal.color(QPalette::Highlight), 0.30);
    _miniOffTextColor = blendColor(pal.color(QPalette::Text),
                                   _miniOffColor, 0.45);
}

void SectionToggle::setSectionOn(bool on)
{
    if (_sectionOn == on) return;
    _sectionOn = on;
    updateGeometry();   // minis appear/disappear with the section state
    update();
}

void SectionToggle::setFileOn(int i, bool on)
{
    bool& ref = (i == 0 ? _f1On : _f2On);
    if (ref == on) return;
    ref = on;
    update();
}

void SectionToggle::setFileCount(int n)
{
    if (_fileCount == n) return;
    _fileCount = n;
    updateGeometry();   // sizeHint changes (minis appear/disappear)
    update();
}

QSize SectionToggle::sizeHint() const
{
    const QFontMetrics fm(_glyphFont);
    const int glyphW = kGlyphPadL + fm.horizontalAdvance(_glyph);
    const int right  = minisVisible() ? miniZoneWidth() : kGlyphPadR;
    return QSize(glyphW + right, 26);
}

QRect SectionToggle::miniRect(int i) const
{
    const int y  = (height() - kMiniH) / 2;
    const int x2 = width() - kMiniMargin - kMiniW;
    const int x1 = x2 - kMiniGap - kMiniW;
    return QRect(i == 0 ? x1 : x2, y, kMiniW, kMiniH);
}

void SectionToggle::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);   // smooth rounded corners

    const QPalette pal    = palette();
    const QColor   hilite = pal.color(QPalette::Highlight);
    const QColor   hiText = pal.color(QPalette::HighlightedText);
    const QRectF   fr     = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // Frame: rounded, filled; highlighted when the section is on. Off color,
    // border and text come from the theme .qss (qproperty-*) to match the
    // Events dock toolbuttons.
    p.setPen(QPen(_borderColor, 1));
    p.setBrush(_sectionOn ? hilite : _offColor);
    p.drawRoundedRect(fr, 4, 4);

    // Glyph, left-justified.
    p.setFont(_glyphFont);
    p.setPen(_sectionOn ? hiText : _textColor);
    const int rightInset = minisVisible() ? miniZoneWidth() : kGlyphPadR;
    QRect gr = rect().adjusted(kGlyphPadL, 0, -rightInset, 0);
    p.drawText(gr, Qt::AlignLeft | Qt::AlignVCenter, _glyph);

    // Mini "1"/"2" boxes — only while the section is on (they're hidden, not
    // greyed, when off, to avoid a permanently cluttered look).
    if (minisVisible()) {
        p.setFont(_miniFont);
        for (int i = 0; i < 2; ++i) {
            const bool on = (i == 0 ? _f1On : _f2On);
            const QRectF mr = QRectF(miniRect(i)).adjusted(0.5, 0.5, -0.5, -0.5);
            QColor bg, fg, edge;
            if (on) {                   // included ⇒ highlighted
                bg   = hilite;
                fg   = hiText;
                edge = hilite;
            } else {                    // not included ⇒ muted grey-blue box
                bg   = _miniOffColor;
                fg   = _miniOffTextColor;
                edge = _borderColor;
            }
            p.setPen(QPen(edge, 1));
            p.setBrush(bg);
            p.drawRoundedRect(mr, 3, 3);
            p.setPen(fg);
            p.drawText(miniRect(i), Qt::AlignCenter, i == 0 ? "1" : "2");
        }
    }
}

void SectionToggle::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        // Right-click (and other buttons) are left alone here so the platform's
        // synthesized QContextMenuEvent can drive contextMenuEvent() instead —
        // otherwise this handler would toggle the section on a right-click too.
        e->ignore();
        return;
    }

    const QPoint pos  = e->pos();
    const bool   ctrl = e->modifiers().testFlag(Qt::ControlModifier);

    // While the section is on, the mini boxes are hot; otherwise there are no
    // minis and the whole control just toggles the section.
    if (minisVisible()) {
        for (int i = 0; i < 2; ++i) {
            if (!miniRect(i).contains(pos)) continue;
            bool& ref   = (i == 0 ? _f1On : _f2On);
            const bool other = (i == 0 ? _f2On : _f1On);
            if (ctrl) {
                // Ctrl-click an enabled box: navigate, don't toggle.
                if (ref) emit navigate(i);
            } else if (ref && !other) {
                // Turning off the last selected file would leave the section on
                // but empty — instead switch the whole section off, preserving
                // the selection so re-enabling the section restores it.
                _sectionOn = false;
                updateGeometry();
                update();
                emit changed();
            } else {
                ref = !ref;
                update();
                emit changed();
                if (ref) emit navigate(i);   // scroll to it when enabling
            }
            return;
        }
    }

    // Body click: toggle the section.
    if (ctrl) {
        if (_sectionOn) emit navigate(-1);   // navigate, don't toggle off
        return;
    }
    _sectionOn = !_sectionOn;
    // Enabling a section that somehow has no file selected (e.g. a legacy saved
    // state) would show nothing — seed both files on.
    if (_sectionOn && _fileCount > 1 && !_f1On && !_f2On)
        _f1On = _f2On = true;
    updateGeometry();                        // minis appear/disappear
    update();
    emit changed();
    if (_sectionOn) emit navigate(-1);       // scroll to it when enabling
}

void SectionToggle::contextMenuEvent(QContextMenuEvent* e)
{
    emit contextMenuRequested(e->globalPos());
    e->accept();
}

/* ================================== WIDGET
 * ======================================== */

Plain::Plain(QWidget* parent) : AstroFileHandler(parent)
{
    chartsCount   = 0;
    aspectsCached = false;
    
    // Enable drag and drop
    setAcceptDrops(true);

    // Create toolbar of compact, grouped section toggles. Each SectionToggle is
    // a pictorial main button plus its own [1]/[2] file toggles, so file
    // inclusion is chosen per-section (e.g. Directions for #2 only while Parans
    // shows both).
    toolbar = new QToolBar(tr("Display Options"), this);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setStyleSheet("QToolBar { border: none; spacing: 2px; padding: 0; }");

    // glyph, tooltip, default-on
    togInput = new SectionToggle(QString::fromUtf8("\U0001F4C4"),  // 📄
                                 tr("Input data"), this);
    togInput->setSectionOn(false);
    togPlanets = new SectionToggle(QString::fromUtf8("\U0001FA90"), // 🪐
                                   tr("Planets"), this);
    togPlanets->setSectionOn(true);
    togHouses = new SectionToggle(QString::fromUtf8("\U0001F3E0"),  // 🏠
                                  tr("Houses"), this);
    togHouses->setSectionOn(true);
    togAspects = new SectionToggle(QString::fromUtf8("\U0001F4D0"), // 📐
                                   tr("Aspects"), this);
    togAspects->setSectionOn(true);
    togDignities = new SectionToggle(
        tr("Dig"), tr("Dignities — dignity and deficient points"), this);
    togDignities->setSectionOn(false);
    togDirections = new SectionToggle(
        tr("Dir"),
        tr("Directions — natal parans rolled up against primary directions\n"
           "(right-click for quick options)"),
        this);
    togDirections->setSectionOn(true);
    togSpeculum = new SectionToggle(QString::fromUtf8("\U0001F50E"), // 🔎
                                    tr("Speculum — rise/set/MC/IC times\n"
                                       "(right-click for quick options)"), this);
    togSpeculum->setSectionOn(true);
    togParans = new SectionToggle(
        tr("Par"),
        tr("Parans — latitudes (and cities) where each natal-body pair forms a "
           "paran\n(right-click for quick options)"),
        this);
    togParans->setSectionOn(false);

    for (SectionToggle* t : { togInput, togPlanets, togHouses, togAspects,
                              togDignities, togDirections, togSpeculum,
                              togParans })
        toolbar->addWidget(t);

    _displayMode = A::DisplayLocalTime;

    connect(togDirections, &SectionToggle::contextMenuRequested, this,
            [this](const QPoint& p) { showDirectionsContextMenu(p); });
    connect(togSpeculum, &SectionToggle::contextMenuRequested, this,
            [this](const QPoint& p) { showSpeculumContextMenu(p); });
    connect(togParans, &SectionToggle::contextMenuRequested, this,
            [this](const QPoint& p) { showParansContextMenu(p); });

    // Match the section toggles' height across the toolbar row (tall enough
    // for the rounded frames).
    const int rowH = 26;
    for (SectionToggle* t : sectionToggles())
        t->setFixedHeight(rowH);

    // Report search: filters/highlights the already-rendered report (see
    // applySearch/filterReportHtml) rather than triggering any recalculation.
    // Debounced so fast typing doesn't re-scan on every keystroke.
    toolbar->addSeparator();
    searchField = new QLineEdit();
    searchField->setObjectName("plainSearchField");
    searchField->setPlaceholderText(tr("Search"));
    searchField->setClearButtonEnabled(true);
    searchField->setFixedHeight(rowH);
    searchField->setToolTip(
        tr("Search the report. Terms are OR'd together (Jupiter Venus shows "
           "both); quote a phrase to match it together (\"Jupiter Dsc\" finds "
           "only Jupiter on the Descendant). Matching rows/blocks are kept "
           "and highlighted; whole paran groupings stay together."));
    toolbar->addWidget(searchField);

    searchDebounce = new QTimer(this);
    searchDebounce->setSingleShot(true);
    searchDebounce->setInterval(300);
    connect(searchField, &QLineEdit::textChanged, searchDebounce,
            qOverload<>(&QTimer::start));
    connect(searchDebounce, &QTimer::timeout, this, &Plain::applySearch);

    view = new ReportBrowser();
    view->setAcceptDrops(false); // Disable drops on the view so parent Plain widget handles them
    // Fixed-star "star:" anchors (see astro-output.cpp's formatStarNameHtml)
    // are hover-tooltip hooks only, not real links; don't let QTextBrowser
    // try to navigate to them on click.
    view->setOpenLinks(false);

    showAllDiurnalEvents = false;
    includeFixedStars    = true;
    showParanNatalRows   = false;
    includeOutOfOrbNatalRows = false;
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

    // Any change to a section (its master or a file box) re-renders the report;
    // a navigate request scrolls the (freshly rebuilt) report to that section.
    for (SectionToggle* t : { togInput, togPlanets, togHouses, togAspects,
                              togDignities, togDirections, togSpeculum,
                              togParans }) {
        connect(t, &SectionToggle::changed, this, &Plain::refresh);
        connect(t, &SectionToggle::navigate, this,
                [this, t](int fileIndex) { scrollToSection(t, fileIndex); });
    }

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
Plain::setDisplayMode(A::SpeculumDisplayMode mode)
{
    if (_displayMode == mode) return;
    _displayMode = mode;
    emit displayModeChanged(mode);
    refresh();
}

void
Plain::setPrimDirMode(A::PrimDirMode mode)
{
    if (A::primDirMode == mode) return;
    A::primDirMode = mode;
    // Speculum type feeds primary-direction calc; every eligible chart needs
    // to recompute (mirrors the primDirModeChanged branch in applySettings()).
    for (int i = 0; i < filesCount(); i++) {
        if (file(i)) file(i)->calculate();
    }
    aspectsCached = false;
    refresh();
}

void
Plain::setParanOrb(double orb)
{
    paranOrb = orb;
    refresh();
}

/* ============================ QUICK OPTIONS MENUS
 * ======================================== */

/// One line in a quick-options menu: a checkable action bound directly to a
/// bool member (no round-trip through AppSettings — the member is the state,
/// and it's picked up automatically next time settings are persisted). The
/// action (and its connection) only lives as long as the transient menu, so
/// binding straight to the member reference is safe.
void
Plain::addBoolAction(QMenu* menu, const QString& label, bool& member)
{
    QAction* a = menu->addAction(label);
    a->setCheckable(true);
    a->setChecked(member);
    connect(a, &QAction::toggled, this, [this, &member](bool on) {
        member = on;
        refresh();
    });
}

void
Plain::addSpeculumTypeSubmenu(QMenu* menu)
{
    QMenu* sub = menu->addMenu(tr("Speculum type"));
    QActionGroup* grp = new QActionGroup(sub);
    grp->setExclusive(true);
    const QList<QPair<QString, A::PrimDirMode>> modes = {
        { tr("Mundane"),  A::prdMundane },
        { tr("Zodiacal"), A::prdZodiacal },
        { tr("Active"),   A::prdActive },
    };
    for (const auto& m : modes) {
        QAction* a = sub->addAction(m.first);
        a->setCheckable(true);
        a->setChecked(A::primDirMode == m.second);
        grp->addAction(a);
        A::PrimDirMode mode = m.second;
        connect(a, &QAction::triggered, this, [this, mode]() { setPrimDirMode(mode); });
    }
}

void
Plain::addDisplayModeSubmenu(QMenu* menu)
{
    QMenu* sub = menu->addMenu(tr("Display mode"));
    QActionGroup* grp = new QActionGroup(sub);
    grp->setExclusive(true);
    const QList<QPair<QString, A::SpeculumDisplayMode>> modes = {
        { tr("Local Time"),      A::DisplayLocalTime },
        { tr("Sidereal Time"),   A::DisplaySiderealTime },
        { tr("Right Ascension"), A::DisplayRightAscension },
    };
    for (const auto& m : modes) {
        QAction* a = sub->addAction(m.first);
        a->setCheckable(true);
        a->setChecked(_displayMode == m.second);
        grp->addAction(a);
        A::SpeculumDisplayMode mode = m.second;
        connect(a, &QAction::triggered, this, [this, mode]() { setDisplayMode(mode); });
    }
}

void
Plain::addMoreOptionsAction(QMenu* menu)
{
    menu->addSeparator();
    connect(menu->addAction(tr("More options…")), &QAction::triggered, this,
            [this]() { openSettingsEditor(); });
}

void
Plain::showDirectionsContextMenu(const QPoint& globalPos)
{
    QMenu menu(this);
    addBoolAction(&menu, tr("Include fixed stars"), includeFixedStars);
    addBoolAction(&menu, tr("Include out-of-orb natal ex-precessed rows"),
                 includeOutOfOrbNatalRows);
    menu.addSeparator();
    addSpeculumTypeSubmenu(&menu);
    addDisplayModeSubmenu(&menu);
    addMoreOptionsAction(&menu);
    menu.exec(globalPos);
}

void
Plain::showSpeculumContextMenu(const QPoint& globalPos)
{
    QMenu menu(this);
    addBoolAction(&menu, tr("Include fixed stars"), includeFixedStars);
    addBoolAction(&menu, tr("Show all planetary diurnal events"),
                 showAllDiurnalEvents);
    menu.addSeparator();
    addSpeculumTypeSubmenu(&menu);
    addDisplayModeSubmenu(&menu);
    addMoreOptionsAction(&menu);
    menu.exec(globalPos);
}

void
Plain::showParansContextMenu(const QPoint& globalPos)
{
    QMenu menu(this);
    addBoolAction(&menu, tr("Include fixed stars"), includeFixedStars);
    addBoolAction(&menu, tr("Show natal ex-precessed positions"),
                 showParanNatalRows);
    addBoolAction(&menu, tr("Include all latitudes"), paranShowAbsent);
    menu.addSeparator();
    addDisplayModeSubmenu(&menu);
    addMoreOptionsAction(&menu);
    menu.exec(globalPos);
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

QList<SectionToggle*>
Plain::sectionToggles() const
{
    return { togInput,      togPlanets,  togHouses,   togAspects,
             togDignities,  togDirections, togSpeculum, togParans };
}

QList<Plain::SectionKey>
Plain::sectionKeys() const
{
    return {
        { togInput,      "Text/describeInput",     "Input" },
        { togPlanets,    "Text/describePlanets",   "Planets" },
        { togHouses,     "Text/describeHouses",    "Houses" },
        { togAspects,    "Text/describeAspects",   "Aspects" },
        { togDignities,  "Text/describePower",     "Dignities" },
        { togDirections, "Text/describeParans",    "Directions" },
        { togSpeculum,   "Text/describeSpeculum",  "Speculum" },
        { togParans,     "Text/describeParanLats", "Parans" },
    };
}

QString
Plain::sectionAnchor(SectionToggle* t, int fileIndex) const
{
    QString base;
    for (const auto& sk : sectionKeys())
        if (sk.t == t) { base = sk.base; break; }
    QString a = "sec_" + base;
    if (fileIndex >= 0) a += "_" + QString::number(fileIndex + 1);
    return a;
}

void
Plain::scrollToSection(SectionToggle* t, int fileIndex)
{
    if (!view) return;
    // The report was just rebuilt by refresh() (which also restored the old
    // scroll position); jump to the requested anchor. A missing anchor (section
    // empty / not rendered) is a harmless no-op.
    view->scrollToAnchor(sectionAnchor(t, fileIndex));
}

void
Plain::filesUpdated(MembersList m)
{
    if (!file()) {
        view->clear();
        chartsCount   = 0;
        aspectsCached = false;
        // No files: hide the per-section [1]/[2] file toggles.
        for (SectionToggle* t : sectionToggles()) t->setFileCount(0);
        return;
    }

    while (m.size() < filesCount()) m.append(AstroFile::Member());

    // Detect if the number of charts changed
    bool chartsCountChanged = (chartsCount != filesCount());
    chartsCount             = filesCount();

    // Show/hide each section's [1]/[2] file toggles based on the file count.
    for (SectionToggle* t : sectionToggles()) t->setFileCount(filesCount());

    // Refresh on a change to ANY file, not just file(0). The Directions/Parans
    // tables for Chart #2 are rendered from file(1) (e.g. the moving chart of a
    // Par=N biwheel), so paran cycling — which steps file(1)'s GMT — must
    // re-render even when file(0) is unchanged.
    bool anyChanged = false;
    for (const auto& members : m)
        if (members) { anyChanged = true; break; }

    // Any file change also invalidates the aspect cache: the cached AspectLists
    // hold Planet* into the files' horoscopes, and any change that triggers a
    // recalculation (GMT, harmonic, zodiac, aspect set, …) reallocates that
    // storage, leaving the cached pointers dangling (crash in
    // describeAspectsTable). Recomputing is cheap next to rendering.
    if (chartsCountChanged || anyChanged) {
        aspectsCached = false;
    }

    // While scrubbing (wheel drag / animation), skip the HTML rebuild; the
    // catch-up recompute on scrub-exit refreshes it once.
    if (A::isScrubbing()) return;

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

/* ================================== SEARCH
 * ======================================== */
// The report is one big HTML string rebuilt from scratch by refresh(); there's
// no live DOM to query. Searching therefore re-scans that string as a second
// pass: split the search box text into terms, walk the report's tables (and
// the Dignities section's per-planet blocks) keeping only rows/blocks that
// contain a term, highlight the matches, and drop a whole section's header
// when nothing in it survived. Free-text sections with no table/dignity
// structure (Input, the Chart Calculation summary) aren't row data to filter,
// so they're always shown untouched.

/// Splits the search box text into terms; each term matches independently (OR
/// across terms — "Jupiter Venus" shows both bodies' rows), case-insensitive.
/// A "quoted phrase" is kept as one term instead of being split on its
/// internal whitespace, so "Jupiter Dsc" (quoted) matches only rows where
/// that exact phrase appears together — e.g. Jupiter specifically on the
/// Descendant — rather than the OR of the two bare words.
static QStringList
searchTerms(const QString& text)
{
    QStringList terms;
    static const QRegularExpression tokenRe(QStringLiteral("\"([^\"]*)\"|(\\S+)"));
    for (auto it = tokenRe.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString term = m.captured(1).isNull() ? m.captured(2) : m.captured(1);
        if (!term.isEmpty()) terms << term;
    }
    return terms;
}

/// Splits html into (isTag, text) tokens so matching/highlighting only ever
/// touches text content, never tag markup or attribute values.
static QVector<QPair<bool, QString>>
tokenizeHtml(const QString& html)
{
    static const QRegularExpression tagRe("<[^>]*>");
    QVector<QPair<bool, QString>> tokens;
    int  pos = 0;
    auto it  = tagRe.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedStart() > pos)
            tokens.append({ false, html.mid(pos, m.capturedStart() - pos) });
        tokens.append({ true, m.captured() });
        pos = m.capturedEnd();
    }
    if (pos < html.length())
        tokens.append({ false, html.mid(pos) });
    return tokens;
}

/// One text token's contribution to the flattened (whitespace-joined) text of
/// a row: which token it came from, where its trimmed content starts within
/// that token's raw text, and where that content landed in the flattened
/// string. Needed because the Directions table puts a planet's name and its
/// angle abbreviation ("Jupiter", "Set") in adjacent but separate <td> cells,
/// so a quoted phrase like "Jupiter Set" only ever matches across that cell
/// boundary in the flattened text -- and this map lets the match still be
/// highlighted back in each contributing cell.
struct FlattenedSegment {
    int tokenIndex;
    int startInToken;
    int startInFlattened;
    int length;
};

/// Tag names that represent a real visual break between text runs (a new
/// table cell/row, an explicit line break, a block element) -- crossing one
/// of these is what should turn into a joining space in the flattened text.
/// Inline styling tags (<b>, <span>, ...) do NOT: e.g. the Parans table's
/// bolded latitude hemisphere letter ("44<b>N</b>03") must still flatten to
/// the contiguous "44N03", not "44 N 03", or a plain search for "44N03"
/// would silently stop matching it.
static bool
isBreakingTag(const QString& tag)
{
    static const QStringList breakers = {
        QStringLiteral("<td"), QStringLiteral("<th"), QStringLiteral("<tr"),
        QStringLiteral("<br"), QStringLiteral("<div"), QStringLiteral("<p"),
        QStringLiteral("<li")
    };
    for (const QString& b : breakers)
        if (tag.startsWith(b, Qt::CaseInsensitive)) return true;
    return false;
}

static QString
flattenForMatching(const QVector<QPair<bool, QString>>& tokens,
                    QVector<FlattenedSegment>&           segments)
{
    QString flattened;
    bool    pendingBreak = false;
    for (int ti = 0; ti < tokens.size(); ++ti) {
        if (tokens[ti].first) {
            if (isBreakingTag(tokens[ti].second)) pendingBreak = true;
            continue;
        }
        const QString& raw = tokens[ti].second;
        int lead = 0;
        while (lead < raw.size() && raw.at(lead).isSpace()) ++lead;
        const int len = raw.trimmed().length();
        if (len == 0) continue;
        if (!flattened.isEmpty() && pendingBreak) flattened += QLatin1Char(' ');
        segments.append({ ti, lead, int(flattened.length()), len });
        flattened += raw.mid(lead, len);
        pendingBreak = false;
    }
    return flattened;
}

/// Wraps case-insensitive term matches within an HTML fragment's text nodes in
/// a themed highlight span, leaving tags/attributes untouched. Sets *matched
/// when any wrapping happened. A phrase term (one containing whitespace) is
/// also tried against the flattened cross-cell text via flattenForMatching(),
/// so e.g. "Jupiter Set" matches and highlights even when "Jupiter" and "Set"
/// live in separate <td> cells, not just when they share one text node.
static QString
highlightMatches(const QString& fragment, const QStringList& terms, bool* matched)
{
    const QVector<QPair<bool, QString>> tokens = tokenizeHtml(fragment);

    QVector<FlattenedSegment> segments;
    QString flattened;
    bool    flattenedBuilt = false;
    auto    ensureFlattened = [&] {
        if (!flattenedBuilt) {
            flattened      = flattenForMatching(tokens, segments);
            flattenedBuilt = true;
        }
    };

    bool any = false;
    QVector<QVector<QPair<int, int>>> tokenSpans(tokens.size());

    for (const QString& term : terms) {
        if (term.isEmpty()) continue;

        // Per-token search: plain single-word terms, and phrase terms that
        // happen to fit inside one cell (e.g. the Parans table's "Jupiter
        // Dsc", combined into a single <td>).
        for (int ti = 0; ti < tokens.size(); ++ti) {
            if (tokens[ti].first) continue;
            const QString& text = tokens[ti].second;
            int from = 0;
            for (;;) {
                const int idx = text.indexOf(term, from, Qt::CaseInsensitive);
                if (idx < 0) break;
                tokenSpans[ti].append({ idx, term.length() });
                any  = true;
                from = idx + term.length();
            }
        }

        // Cross-cell search: only phrase terms need this (a single word is
        // always caught by the per-token pass, since a lone token IS the
        // flattened text when there's nothing else to join it to).
        if (!term.contains(QLatin1Char(' '))) continue;
        ensureFlattened();
        int from = 0;
        for (;;) {
            const int idx = flattened.indexOf(term, from, Qt::CaseInsensitive);
            if (idx < 0) break;
            const int end = idx + term.length();
            for (const FlattenedSegment& seg : std::as_const(segments)) {
                const int segEnd = seg.startInFlattened + seg.length;
                const int os     = qMax(idx, seg.startInFlattened);
                const int oe     = qMin(end, segEnd);
                if (os < oe) {
                    tokenSpans[seg.tokenIndex].append(
                        { seg.startInToken + (os - seg.startInFlattened),
                          oe - os });
                    any = true;
                }
            }
            from = end;
        }
    }

    QString out;
    for (int ti = 0; ti < tokens.size(); ++ti) {
        const auto& tok = tokens[ti];
        if (tok.first || tok.second.isEmpty()) {
            out += tok.second;
            continue;
        }
        const QString& text  = tok.second;
        auto&          spans = tokenSpans[ti];
        if (spans.isEmpty()) {
            out += text;
            continue;
        }
        std::sort(spans.begin(), spans.end());
        QVector<QPair<int, int>> merged;
        for (const auto& s : spans) {
            if (!merged.isEmpty()
                && s.first <= merged.last().first + merged.last().second) {
                const int endA = merged.last().first + merged.last().second;
                const int endB = s.first + s.second;
                merged.last().second = qMax(endA, endB) - merged.last().first;
            } else {
                merged.append(s);
            }
        }
        int cur = 0;
        for (const auto& s : merged) {
            out += text.mid(cur, s.first - cur);
            out += "<span class='search-hit'>" + text.mid(s.first, s.second)
                   + "</span>";
            cur = s.first + s.second;
        }
        out += text.mid(cur);
    }
    if (matched) *matched = any;
    return out;
}

/// Row-level filter for one <table ...>...</table> fragment: header rows
/// (containing <th>) always survive; data rows survive if they match a term,
/// OR — for Directions rows carrying a data-pgroup="N" marker (see
/// describeParans/event::fmt) — if any other row in the same paran cluster
/// matched, so a hit anywhere in a cluster keeps the whole grouping intact.
static QString
filterTable(const QString& tableHtml, const QStringList& terms, int* keptRows)
{
    static const QRegularExpression rowRe(
        "<tr\\b[^>]*>.*?</tr>", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression pgroupRe("data-pgroup=\"(\\d+)\"");

    const int tableOpenEnd = tableHtml.indexOf('>') + 1;
    const QString openTag  = tableHtml.left(tableOpenEnd);
    QString       inner    = tableHtml.mid(tableOpenEnd);
    inner.chop(QStringLiteral("</table>").length());

    struct Row {
        bool    isHeader;
        int     pgroup;
        bool    matched;
        QString html; // raw for headers, highlighted for data rows
    };
    QVector<Row> rows;
    for (auto it = rowRe.globalMatch(inner); it.hasNext();) {
        const QRegularExpressionMatch m   = it.next();
        const QString                 raw = m.captured();
        Row                            r;
        r.isHeader = raw.contains(QLatin1String("<th"));
        const auto gm = pgroupRe.match(raw);
        r.pgroup = gm.hasMatch() ? gm.captured(1).toInt() : -1;
        if (r.isHeader) {
            r.matched = false;
            r.html    = raw;
        } else {
            r.html = highlightMatches(raw, terms, &r.matched);
        }
        rows.append(r);
    }

    QSet<int> matchedGroups;
    for (const Row& r : std::as_const(rows))
        if (r.matched && r.pgroup >= 0) matchedGroups.insert(r.pgroup);

    QString out = openTag;
    int     kept = 0;
    for (const Row& r : std::as_const(rows)) {
        if (r.isHeader) {
            out += r.html;
            continue;
        }
        const bool keep =
            r.matched || (r.pgroup >= 0 && matchedGroups.contains(r.pgroup));
        if (!keep) continue;
        out += r.html;
        ++kept;
    }
    out += "</table>";
    if (keptRows) *keptRows = kept;
    return out;
}

/// Block-level filter for a Dignities chunk: each <h4>Planet</h4><div
/// class='dignity-list'>...</div> pair is kept as a unit when the planet name
/// or its dignity text matches; everything else in the chunk (anchor, section
/// header, whitespace) passes through untouched.
static QString
filterDignityChunk(const QString& chunk, const QStringList& terms, int* keptBlocks)
{
    static const QRegularExpression blockRe(
        "<h4>.*?</h4>\\s*<div class='dignity-list'>.*?</div>",
        QRegularExpression::DotMatchesEverythingOption);

    QString out;
    int     kept = 0;
    int     pos  = 0;
    for (auto it = blockRe.globalMatch(chunk); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += chunk.mid(pos, m.capturedStart() - pos);
        bool          matched     = false;
        const QString highlighted = highlightMatches(m.captured(), terms, &matched);
        if (matched) {
            out += highlighted;
            ++kept;
        }
        pos = m.capturedEnd();
    }
    out += chunk.mid(pos);
    if (keptBlocks) *keptBlocks = kept;
    return out;
}

/// One "sec_X" anchor-delimited chunk (a whole section, or one chart's
/// sub-section of it): tables get row-filtered, Dignities blocks get
/// block-filtered, and the chunk (anchor + headers included) disappears
/// entirely if nothing in it survived. Anything else (Input, free text) has
/// no row structure to filter and is always kept as-is.
static QString
processSectionChunk(const QString& chunk, const QStringList& terms)
{
    if (chunk.contains(QLatin1String("<table"))) {
        const int tStart = chunk.indexOf(QLatin1String("<table"));
        int       tEnd   = chunk.indexOf(QLatin1String("</table>"), tStart);
        if (tStart < 0 || tEnd < 0) return chunk; // malformed guard
        tEnd += QStringLiteral("</table>").length();

        int           kept = 0;
        const QString filtered =
            filterTable(chunk.mid(tStart, tEnd - tStart), terms, &kept);
        if (kept == 0) return QString();
        return chunk.left(tStart) + filtered + chunk.mid(tEnd);
    }
    if (chunk.contains(QLatin1String("dignity-list"))) {
        int           kept     = 0;
        const QString filtered = filterDignityChunk(chunk, terms, &kept);
        return kept == 0 ? QString() : filtered;
    }
    return chunk;
}

/// Filters/highlights the full report for the given search terms. The
/// preamble (Chart Calculation, before the first gated section) is always
/// kept; each "sec_X" chunk after it is processed independently.
static QString
filterReportHtml(const QString& fullHtml, const QStringList& terms)
{
    if (terms.isEmpty()) return fullHtml;

    const int bodyTagEnd = fullHtml.indexOf("<body>");
    const int bodyEnd    = fullHtml.indexOf("</body>");
    if (bodyTagEnd < 0 || bodyEnd < 0) return fullHtml;

    const int     bodyStart = bodyTagEnd + QStringLiteral("<body>").length();
    const QString head      = fullHtml.left(bodyStart);
    const QString tail      = fullHtml.mid(bodyEnd);
    const QString body      = fullHtml.mid(bodyStart, bodyEnd - bodyStart);

    static const QRegularExpression anchorRe("<a name=\"sec_[^\"]*\"></a>");
    QVector<QRegularExpressionMatch> anchors;
    for (auto it = anchorRe.globalMatch(body); it.hasNext();)
        anchors.append(it.next());

    if (anchors.isEmpty()) return fullHtml; // nothing gated to filter

    QString out = body.left(anchors.first().capturedStart()); // preamble

    for (int i = 0; i < anchors.size(); ++i) {
        const int start = anchors[i].capturedStart();
        const int end =
            (i + 1 < anchors.size()) ? anchors[i + 1].capturedStart() : body.length();
        out += processSectionChunk(body.mid(start, end - start), terms);
    }

    return head + out + tail;
}

void
Plain::applySearch()
{
    if (!file()) return;
    const QStringList terms = searchTerms(searchField->text());
    view->setHtml(terms.isEmpty() ? _reportHtml
                                   : filterReportHtml(_reportHtml, terms));
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

    // Per-section file selection. With a single chart only #1 is meaningful.
    // With two charts, each section decides which files it includes via its own
    // [1]/[2] toggles. showFirst/showSecond helpers below are evaluated
    // per-section rather than globally.
    const bool twoCharts = filesCount() > 1;
    auto sec1 = [twoCharts](SectionToggle* t) {
        return t->sectionOn() && (!twoCharts || t->fileOn(0));
    };
    auto sec2 = [twoCharts](SectionToggle* t) {
        return t->sectionOn() && twoCharts && t->fileOn(1);
    };

    const A::SpeculumDisplayMode displayMode = _displayMode;

    // Update aspects cache if needed (only when aspects will be displayed)
    if (togAspects->sectionOn()) {
        updateAspectsCache();
    }

    int articles = (A::Article_Input * togInput->sectionOn())
                   | (A::Article_Planet * togPlanets->sectionOn())
                   | (A::Article_Houses * togHouses->sectionOn())
                   | (A::Article_Aspects * togAspects->sectionOn())
                   | (A::Article_Power * togDignities->sectionOn())
                   | (A::Article_Parans * togDirections->sectionOn())
                   | (A::Article_DiurnalEvents * showAllDiurnalEvents)
                   | (A::Article_Speculum * togSpeculum->sectionOn())
                   | (A::Article_FixedStars * includeFixedStars)
                   | (A::Article_ParanLatitudes * togParans->sectionOn());

    // Get theme-appropriate row highlight color (subtle overlay for alternating rows/headers)
    QString rowHighlightColor;
    if (ThemeManager::instance().currentTheme() == ThemeManager::Theme::Dark) {
        rowHighlightColor = "rgba(255,255,255,0.08)"; // Subtle white overlay for dark theme
    } else {
        rowHighlightColor = "rgba(0,0,0,0.08)"; // Subtle black overlay for light theme
    }

    // Search-match highlight: distinguishable from the bold planet-name
    // styling and readable for un-bold text (fixed stars) alike, so a solid
    // themed yellow with forced dark text rather than just a font weight.
    const bool   isDarkTheme = ThemeManager::instance().currentTheme()
                              == ThemeManager::Theme::Dark;
    const QString searchHighlightBg = isDarkTheme ? "#D4AC0D" : "#FFF176";
    const QString searchHighlightFg = "#1a1a1a";

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
    html += ".search-hit { background-color: " + searchHighlightBg
            + "; color: " + searchHighlightFg + "; border-radius: 2px; }";
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
        html += "<a name=\"sec_Input\"></a>";
        if (filesCount() == 1) {
            html += A::describeInput(scope.inputData);
        } else if (filesCount() > 1) {
            // Display Chart #1
            if (sec1(togInput)) {
                html += "<a name=\"sec_Input_1\"></a>";
                html += "<h2>"
                        + QObject::tr("Chart #1: %1").arg(file(0)->getName())
                        + "</h2>";
                html += A::describeInput(file(0)->horoscope().inputData);
            }

            // Display Chart #2
            if (sec2(togInput)) {
                html += "<a name=\"sec_Input_2\"></a>";
                html += "<h2>"
                        + QObject::tr("Chart #2: %1").arg(file(1)->getName())
                        + "</h2>";
                html += A::describeInput(file(1)->horoscope().inputData);
            }
        }
    }

    // Display planets for selected charts
    if (articles & A::Article_Planet) {
        html += "<a name=\"sec_Planets\"></a>";
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
            if (sec1(togPlanets)) {
                auto scope1 = file(0)->horoscope();
                if (scope1.planets.count()) {
                    html += "<a name=\"sec_Planets_1\"></a>";
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
            if (sec2(togPlanets)) {
                auto scope2 = file(1)->horoscope();
                if (scope2.planets.count()) {
                    html += "<a name=\"sec_Planets_2\"></a>";
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
        html += "<a name=\"sec_Houses\"></a>";
        if (filesCount() == 1 && scope.houses.system) {
            html +=
                A::describeHouses(scope.houses, scope.zodiac, scope.planets);
        } else if (filesCount() > 1) {
            // Chart #1 Houses
            if (sec1(togHouses)) {
                auto scope1 = file(0)->horoscope();
                if (scope1.houses.system) {
                    html += "<a name=\"sec_Houses_1\"></a>";
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
            if (sec2(togHouses)) {
                auto scope2 = file(1)->horoscope();
                if (scope2.houses.system) {
                    html += "<a name=\"sec_Houses_2\"></a>";
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
        html += "<a name=\"sec_Aspects\"></a>";
        if (filesCount() == 1 && cachedChart1Aspects.count()) {
            html +=
                A::describeAspectsTable(cachedChart1Aspects, aspectSortOrder);
        } else if (filesCount() > 1 && sec1(togAspects) && sec2(togAspects)) {
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
            if (sec1(togAspects)) {
                if (cachedChart1Aspects.count()) {
                    html += "<a name=\"sec_Aspects_1\"></a>";
                    html += "<h2>"
                            + QObject::tr("Aspects - Chart #1: %1")
                                  .arg(file(0)->getName())
                            + "</h2>";
                    html += A::describeAspectsTable(cachedChart1Aspects,
                                                    aspectSortOrder);
                }
            }
            if (sec2(togAspects)) {
                if (cachedChart2Aspects.count()) {
                    html += "<a name=\"sec_Aspects_2\"></a>";
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
        html += "<a name=\"sec_Dignities\"></a>";
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
            if (sec1(togDignities)) {
                auto scope1 = file(0)->horoscope();
                if (scope1.planets.count()) {
                    html += "<a name=\"sec_Dignities_1\"></a>";
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
            if (sec2(togDignities)) {
                auto scope2 = file(1)->horoscope();
                if (scope2.planets.count()) {
                    html += "<a name=\"sec_Dignities_2\"></a>";
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
        html += "<a name=\"sec_Directions\"></a>";
        if (filesCount() == 1 && scope.planets.count()) {
            html += "<h2>" + QObject::tr("Directions") + "</h2>";
            // Primary Direction focal preview: a click on a PD event's T/P/S
            // or T/P/N cell sets this file's direction-focus range without
            // touching its GMT or Type (see Transits::clickedCell()) —
            // passed in explicitly here since this mode is orthogonal to
            // paran-ness (isParanChart is derived from TypeParan inside
            // describeParans() itself; this one isn't tied to any FileType).
            const bool focusOnDirection0 =
                file(0) && file(0)->hasDirectionFocus();
            html += A::describeParans(files(),
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      includeOutOfOrbNatalRows,
                                      nullptr,
                                      focusOnDirection0,
                                      focusOnDirection0
                                          ? file(0)->getDirectionFocusRange()
                                          : A::ADateTimeRange(),
                                      focusOnDirection0
                                          ? file(0)->getDirectionFocusDate()
                                          : QDateTime(),
                                      focusOnDirection0
                                          ? file(0)->getDirectionFocusLabel()
                                          : QString());
        } else if (filesCount() > 1) {
            // Chart #1 Parans
            if (sec1(togDirections) && file(0)
                && file(0)->horoscope().planets.count()) {
                html += "<a name=\"sec_Directions_1\"></a>";
                html += "<h2>"
                        + QObject::tr("Directions - Chart #1: %1")
                              .arg(file(0)->getName())
                        + "</h2>";
                AstroFileList file1List;
                file1List.append(file(0));
                const bool focusOnDirection1 = file(0)->hasDirectionFocus();
                html +=
                    A::describeParans(file1List,
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      includeOutOfOrbNatalRows,
                                      nullptr,
                                      focusOnDirection1,
                                      focusOnDirection1
                                          ? file(0)->getDirectionFocusRange()
                                          : A::ADateTimeRange(),
                                      focusOnDirection1
                                          ? file(0)->getDirectionFocusDate()
                                          : QDateTime(),
                                      focusOnDirection1
                                          ? file(0)->getDirectionFocusLabel()
                                          : QString());
            }

            // Chart #2 Parans — pass file(0) as natal context for Par=N filtering
            if (sec2(togDirections) && file(1)
                && file(1)->horoscope().planets.count()) {
                html += "<a name=\"sec_Directions_2\"></a>";
                html += "<h2>"
                        + QObject::tr("Directions - Chart #2: %1")
                              .arg(file(1)->getName())
                        + "</h2>";
                AstroFileList file2List;
                file2List.append(file(1));
                const bool focusOnDirection2 = file(1)->hasDirectionFocus();
                html +=
                    A::describeParans(file2List,
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      includeOutOfOrbNatalRows,
                                      file(0),
                                      focusOnDirection2,
                                      focusOnDirection2
                                          ? file(1)->getDirectionFocusRange()
                                          : A::ADateTimeRange(),
                                      focusOnDirection2
                                          ? file(1)->getDirectionFocusDate()
                                          : QDateTime(),
                                      focusOnDirection2
                                          ? file(1)->getDirectionFocusLabel()
                                          : QString());
            }
        }
    }

    // Display paran-latitudes table for selected charts
    if (articles & A::Article_ParanLatitudes) {
        html += "<a name=\"sec_Parans\"></a>";
        if (filesCount() == 1 && scope.planets.count()) {
            html += "<h2>" + QObject::tr("Parans") + "</h2>";
            html += A::describeParanLatitudes(scope,
                                              paranOrb,
                                              paranCityLatTol,
                                              paranMaxCitiesPerRow,
                                              paranShowAbsent,
                                              paranCityPopMask,
                                              paranCityContinentMask,
                                              displayMode,
                                              nullptr,
                                              file(0)->getType() == TypeComposite);
        } else if (filesCount() > 1) {
            if (sec1(togParans) && file(0)
                && file(0)->horoscope().planets.count()) {
                html += "<a name=\"sec_Parans_1\"></a>";
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
                                                  displayMode,
                                                  nullptr,
                                                  file(0)->getType() == TypeComposite);
            }
            if (sec2(togParans) && file(1)
                && file(1)->horoscope().planets.count()) {
                html += "<a name=\"sec_Parans_2\"></a>";
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
                                                      &file(1)->horoscope(),
                                                      file(0)->getType() == TypeComposite);
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
        html += "<a name=\"sec_Speculum\"></a>";
        if (filesCount() == 1 && scope.planets.count()) {
            html += A::describeSpeculum(scope,
                                        bool(articles & A::Article_FixedStars),
                                        displayMode);
        } else if (filesCount() > 1) {
            // Chart #1 Speculum
            if (sec1(togSpeculum)) {
                auto scope1 = file(0)->horoscope();
                if (scope1.planets.count()) {
                    html += "<a name=\"sec_Speculum_1\"></a>";
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
            if (sec2(togSpeculum)) {
                auto scope2 = file(1)->horoscope();
                if (scope2.planets.count()) {
                    html += "<a name=\"sec_Speculum_2\"></a>";
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
    _reportHtml = html;
    applySearch(); // shows _reportHtml as-is, or re-scans it if search is active

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
    // Per-section file inclusion (Chart #1 / #2). Default: include both.
    for (const char* base : { "Input", "Planets", "Houses", "Aspects",
                              "Dignities", "Directions", "Speculum", "Parans" }) {
        s.setValue(QString("Plain/%1_f1").arg(base), true);
        s.setValue(QString("Plain/%1_f2").arg(base), true);
    }
    s.setValue("Mundane/displayMode", unsigned(A::DisplayLocalTime));
    s.setValue("Mundane/primDirMode", unsigned(A::prdMundane));
    s.setValue("Mundane/pdTimingKey", unsigned(A::PDPtolemy));
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
    s.setValue("Mundane/includeOutOfOrbNatalRows", false);
    s.setValue("Mundane/dirMethodSolarReturn", unsigned(A::DirNeoPSSR));
    s.setValue("Mundane/dirMethodOther",       unsigned(A::DirNeoSQ));
    s.setValue("Text/aspectSortOrder", unsigned(A::SortByPlanets));
    return s;
}

AppSettings
Plain::currentSettings()
{
    AppSettings s;
    // Section main on/off (legacy Text/describe* keys) plus per-section [1]/[2].
    for (const auto& sk : sectionKeys()) {
        s.setValue(sk.mainKey, sk.t->sectionOn());
        s.setValue(QString("Plain/%1_f1").arg(sk.base), sk.t->fileOn(0));
        s.setValue(QString("Plain/%1_f2").arg(sk.base), sk.t->fileOn(1));
    }
    s.setValue("Mundane/displayMode", unsigned(_displayMode));
    s.setValue("Mundane/primDirMode", unsigned(A::primDirMode));
    s.setValue("Mundane/pdTimingKey", unsigned(A::pdTimingKey));
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
    s.setValue("Mundane/includeOutOfOrbNatalRows", includeOutOfOrbNatalRows);
    s.setValue("Mundane/dirMethodSolarReturn", unsigned(A::dirMethodSolarReturn));
    s.setValue("Mundane/dirMethodOther",       unsigned(A::dirMethodOther));
    s.setValue("Text/aspectSortOrder", unsigned(aspectSortOrder));
    return s;
}

void
Plain::applySettings(const AppSettings& s)
{
    // Restore each section's main on/off and its per-file [1]/[2] toggles.
    // Legacy sessions lack the Plain/<section>_fN keys → default both files on.
    for (const auto& sk : sectionKeys()) {
        sk.t->setSectionOn(s.value(sk.mainKey).toBool());
        sk.t->setFileOn(
            0, s.value(QString("Plain/%1_f1").arg(sk.base), true).toBool());
        sk.t->setFileOn(
            1, s.value(QString("Plain/%1_f2").arg(sk.base), true).toBool());
    }

    setDisplayMode(
        A::SpeculumDisplayMode(s.value("Mundane/displayMode").toUInt()));

    // Check if primDirMode changed - if so, need to recalculate charts
    A::PrimDirMode newPrimDirMode =
        A::PrimDirMode(s.value("Mundane/primDirMode").toUInt());
    bool primDirModeChanged = (A::primDirMode != newPrimDirMode);
    A::primDirMode          = newPrimDirMode;

    // pdTimingKey only affects calculateAngularDate's date-conversion step,
    // read fresh on every Directions-table render — no recalculate() needed,
    // just a redraw (see the primDirModeChanged block below).
    A::PDTimingKey newPdTimingKey = A::PDTimingKey(
        s.value("Mundane/pdTimingKey", unsigned(A::PDPtolemy)).toUInt());
    bool pdTimingKeyChanged = (A::pdTimingKey != newPdTimingKey);
    A::pdTimingKey          = newPdTimingKey;

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
    includeOutOfOrbNatalRows = s.value("Mundane/includeOutOfOrbNatalRows").toBool();
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

    // pdTimingKey doesn't feed the ephemeris (no calculate() needed) but the
    // Directions-table HTML is cached, so it still needs invalidating.
    if (pdTimingKeyChanged) {
        aspectsCached = false;
    }

    refresh();
}

void
Plain::setupSettingsEditor(AppSettingsEditor* ed)
{
    // Organized by report section: general rows first, then a group per table.
    // (Display mode is set from the toolbar's clock button, not here.)
    ed->addTab(tr("Tables"));
    ed->addComboBox("Text/aspectSortOrder",
                    tr("Sort aspects by"),
                    { { "Planet pairs", A::SortByPlanets },
                      { "Orb strength", A::SortByOrbStrength },
                      { "Aspect type", A::SortByAspectType } });

    ed->beginGroup(tr("Directions && Speculum"));
    ed->addComboBox("Mundane/primDirMode",
                    tr("Speculum type"),
                    { { "Mundane", A::prdMundane },
                      { "Zodiacal", A::prdZodiacal },
                      { "Active", A::prdActive } });
    ed->addComboBox("Mundane/pdTimingKey",
                    tr("Primary Direction timing key\n(Directions table, and the Events tab's Primary Directions)"),
                    { { "Ptolemy (1 deg/year)", unsigned(A::PDPtolemy) },
                      { "Naibod (0.98565 deg/year)", unsigned(A::PDNaibod) } });
    ed->addCheckBox("Mundane/showAllDiurnalEvents",
                    tr("Show all planetary diurnal events"));
    ed->addCheckBox("Mundane/includeFixedStars", tr("Include fixed stars"));
    ed->addCheckBox("Mundane/includeOutOfOrbNatalRows",
                    tr("Always include natal ex-precessed positions\n(when shown, ignores the Parans orb filter)"));
    ed->addComboBox("Mundane/dirMethodSolarReturn",
                    tr("Derived directions for Solar Returns"),
                    { { "None (Primary Directions)", unsigned(A::DirNone) },
                      { "PSSR (mean sun)",           unsigned(A::DirPSSR) },
                      { "NeoPSSR (apparent sun)",    unsigned(A::DirNeoPSSR) },
                      { "SQ (mean sun)",             unsigned(A::DirSQ) },
                      { "NeoSQ (apparent sun)",      unsigned(A::DirNeoSQ) } });
    ed->addComboBox("Mundane/dirMethodOther",
                    tr("Derived directions for other charts\n(lunar returns, ingresses)"),
                    { { "None (Primary Directions)", unsigned(A::DirNone) },
                      { "SQ (mean sun)",             unsigned(A::DirSQ) },
                      { "NeoSQ (apparent sun)",      unsigned(A::DirNeoSQ) } });
    ed->endGroup();

    ed->beginGroup(tr("Parans"));
    ed->addDoubleSpinBox("Mundane/paranOrb",
                         tr("Orb for paranatellontas"),  // also Directions table
                         1. / 60. /*1 minute*/,
                         5.0 /*5 degrees*/);
    ed->addCheckBox("Mundane/showParanNatalRows",
                    tr("Show natal ex-precessed positions"));
    ed->addCheckBox("Mundane/paranShowAbsent",
                    tr("Include all latitudes"));
    ed->addDoubleSpinBox("Mundane/paranCityLatTol",
                         tr("City latitude tolerance"),
                         0.05 /*~5.5 km*/,
                         5.0 /*~550 km*/);
    ed->addSpinBox("Mundane/paranMaxCitiesPerRow",
                   tr("Max cities listed per row"),
                   1,
                   30);
    ed->addCheckBoxRow(tr("City sizes"),
                       { { "Mundane/paranCity_PopSmall",  tr("15k–100k") },
                         { "Mundane/paranCity_PopMedium", tr("100k–500k") },
                         { "Mundane/paranCity_PopLarge",  tr("500k–2M") },
                         { "Mundane/paranCity_PopHuge",   tr(">2M") } });
    ed->addCheckBoxRow(tr("Continents"),
                       { { "Mundane/paranCity_ContAfrica",       tr("Africa") },
                         { "Mundane/paranCity_ContAsia",         tr("Asia") },
                         { "Mundane/paranCity_ContEurope",       tr("Europe") },
                         { "Mundane/paranCity_ContNorthAmerica", tr("N. America") },
                         { "Mundane/paranCity_ContSouthAmerica", tr("S. America") },
                         { "Mundane/paranCity_ContOceania",      tr("Oceania") },
                         { "Mundane/paranCity_ContAntarctica",   tr("Antarctica") } });
    ed->endGroup();
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
