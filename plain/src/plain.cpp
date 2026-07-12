#include "plain.h"
#include "../../zodiac/src/thememanager.h"
#include "../../astroprocessor/src/citydb.h"
#include <Astroprocessor/Output>
#include <QAction>
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
};

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
        tr("Directions — natal parans rolled up against primary directions"),
        this);
    togDirections->setSectionOn(true);
    togSpeculum = new SectionToggle(QString::fromUtf8("\U0001F50E"), // 🔎
                                    tr("Speculum — rise/set/MC/IC times"), this);
    togSpeculum->setSectionOn(true);
    togParans = new SectionToggle(
        tr("Par"),
        tr("Parans — latitudes (and cities) where each natal-body pair forms a "
           "paran"),
        this);
    togParans->setSectionOn(false);

    for (SectionToggle* t : { togInput, togPlanets, togHouses, togAspects,
                              togDignities, togDirections, togSpeculum,
                              togParans })
        toolbar->addWidget(t);

    toolbar->addSeparator();

    // Create display mode selector combo box (no label)
    displayModeSelector = new QComboBox();
    displayModeSelector->addItem(tr("Local Time"), A::DisplayLocalTime);
    displayModeSelector->addItem(tr("Sidereal Time"), A::DisplaySiderealTime);
    displayModeSelector->addItem(tr("Right Ascension"), A::DisplayRightAscension);
    displayModeSelector->setCurrentIndex(0); // Default to Local Time
    toolbar->addWidget(displayModeSelector);

    // Match the section toggles' height to the display-mode combo so the whole
    // toolbar row is uniform (and tall enough for the rounded frames).
    const int rowH = displayModeSelector->sizeHint().height();
    for (SectionToggle* t : sectionToggles())
        t->setFixedHeight(rowH);

    view = new ReportBrowser();
    view->setAcceptDrops(false); // Disable drops on the view so parent Plain widget handles them

    showAllDiurnalEvents = false;
    includeFixedStars    = true;
    showParanNatalRows   = false;
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

    connect(displayModeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        A::SpeculumDisplayMode mode = A::SpeculumDisplayMode(displayModeSelector->currentData().toInt());
        emit displayModeChanged(mode);
        refresh();
    });

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

    // File-data changes (GMT, Location) that affect aspect positions
    bool needsAspectUpdate = false;
    for (const auto& members : m) {
        if (members & (AstroFile::GMT | AstroFile::Location)) {
            needsAspectUpdate = true;
            break;
        }
    }

    if (chartsCountChanged || needsAspectUpdate) {
        aspectsCached = false;
    }

    // Refresh on a change to ANY file, not just file(0). The Directions/Parans
    // tables for Chart #2 are rendered from file(1) (e.g. the moving chart of a
    // Par=N biwheel), so paran cycling — which steps file(1)'s GMT — must
    // re-render even when file(0) is unchanged.
    bool anyChanged = false;
    for (const auto& members : m)
        if (members) { anyChanged = true; break; }

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

    // Get display mode from combo box data
    A::SpeculumDisplayMode displayMode =
        A::SpeculumDisplayMode(displayModeSelector->currentData().toInt());

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
            html += A::describeParans(files(),
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      nullptr);
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
                html +=
                    A::describeParans(file1List,
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      nullptr);
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
                html +=
                    A::describeParans(file2List,
                                      bool(articles & A::Article_DiurnalEvents),
                                      bool(articles & A::Article_FixedStars),
                                      paranOrb,
                                      displayMode,
                                      showParanNatalRows,
                                      file(0));
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
                                              displayMode);
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
                                                  displayMode);
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
                                                      &file(1)->horoscope());
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
    s.setValue("Text/describeParanLats", false);
    // Per-section file inclusion (Chart #1 / #2). Default: include both.
    for (const char* base : { "Input", "Planets", "Houses", "Aspects",
                              "Dignities", "Directions", "Speculum", "Parans" }) {
        s.setValue(QString("Plain/%1_f1").arg(base), true);
        s.setValue(QString("Plain/%1_f2").arg(base), true);
    }
    s.setValue("Mundane/displayMode", unsigned(A::DisplayLocalTime));
    s.setValue("Mundane/primDirMode", unsigned(A::prdMundane));
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
    s.setValue("Mundane/displayMode",
               unsigned(displayModeSelector->currentData().toInt()));
    s.setValue("Mundane/primDirMode", unsigned(A::primDirMode));
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

    refresh();
}

void
Plain::setupSettingsEditor(AppSettingsEditor* ed)
{
    ed->addTab(tr("Tables"));
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
    ed->addDoubleSpinBox("Mundane/paranCityLatTol",
                         tr("Paran-latitude city tolerance (degrees)"),
                         0.05 /*~5.5 km*/,
                         5.0 /*~550 km*/);
    ed->addSpinBox("Mundane/paranMaxCitiesPerRow",
                   tr("Max cities listed per paran-latitude row"),
                   1,
                   30);
    ed->addCheckBox("Mundane/paranShowAbsent",
                    tr("Show paran latitudes that are not present in the natal chart"));
    ed->addCheckBox("Mundane/paranCity_PopSmall",
                    tr("Cities: small (15k–100k)"));
    ed->addCheckBox("Mundane/paranCity_PopMedium",
                    tr("Cities: medium (100k–500k)"));
    ed->addCheckBox("Mundane/paranCity_PopLarge",
                    tr("Cities: large (500k–2M)"));
    ed->addCheckBox("Mundane/paranCity_PopHuge",
                    tr("Cities: huge (>2M)"));
    ed->addCheckBox("Mundane/paranCity_ContAfrica",       tr("Cities in Africa"));
    ed->addCheckBox("Mundane/paranCity_ContAsia",         tr("Cities in Asia"));
    ed->addCheckBox("Mundane/paranCity_ContEurope",       tr("Cities in Europe"));
    ed->addCheckBox("Mundane/paranCity_ContNorthAmerica", tr("Cities in North America"));
    ed->addCheckBox("Mundane/paranCity_ContSouthAmerica", tr("Cities in South America"));
    ed->addCheckBox("Mundane/paranCity_ContOceania",      tr("Cities in Oceania"));
    ed->addCheckBox("Mundane/paranCity_ContAntarctica",   tr("Cities in Antarctica"));
    ed->addCheckBox("Mundane/includeFixedStars", tr("Include fixed stars"));
    ed->addCheckBox("Mundane/showParanNatalRows",
                    tr("Show natal ex-precessed positions in paran table"));
    ed->addComboBox("Mundane/dirMethodSolarReturn",
                    tr("Derived directions for Solar Returns"),
                    { { "None (Primary Directions)", unsigned(A::DirNone) },
                      { "PSSR (mean sun)",           unsigned(A::DirPSSR) },
                      { "NeoPSSR (apparent sun)",    unsigned(A::DirNeoPSSR) },
                      { "SQ (mean sun)",             unsigned(A::DirSQ) },
                      { "NeoSQ (apparent sun)",      unsigned(A::DirNeoSQ) } });
    ed->addComboBox("Mundane/dirMethodOther",
                    tr("Derived directions for other charts (lunar returns, ingresses)"),
                    { { "None (Primary Directions)", unsigned(A::DirNone) },
                      { "SQ (mean sun)",             unsigned(A::DirSQ) },
                      { "NeoSQ (apparent sun)",      unsigned(A::DirNeoSQ) } });
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
