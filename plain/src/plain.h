#ifndef PLAIN_H
#define PLAIN_H

#include <Astroprocessor/Calc>
#include <Astroprocessor/Gui>
#include <Astroprocessor/Output>
#include <QWidget>
#include <QToolButton>

class QCheckBox;
class QTextBrowser;
class QToolBar;
class QComboBox;
class QPushButton;
class QAction;
class QActionGroup;
class QLabel;
class QLineEdit;
class QTimer;

/* ============================== SECTION TOGGLE
 * ======================================== */

/// A compact, self-painted section control: one rounded button frame showing a
/// left-justified emoji/abbrev glyph, with two tiny "1"/"2" boxes drawn floating
/// on its right edge. Clicking the body toggles the section; clicking a mini box
/// toggles whether that loaded file's subsection renders. The minis appear only
/// when a second chart is present (setFileCount) and are non-interactive/greyed
/// while the section is off. Painted directly (not via QToolButton) to avoid
/// style-sheet clipping/alignment quirks at these small sizes.
class SectionToggle : public QWidget {
    Q_OBJECT
    // Painted colors, overridable from the theme .qss via `qproperty-*`.
    Q_PROPERTY(QColor offColor        MEMBER _offColor)
    Q_PROPERTY(QColor textColor       MEMBER _textColor)
    Q_PROPERTY(QColor borderColor     MEMBER _borderColor)
    Q_PROPERTY(QColor miniOffColor    MEMBER _miniOffColor)
    Q_PROPERTY(QColor miniOffTextColor MEMBER _miniOffTextColor)
  public:
    SectionToggle(const QString& glyph,
                  const QString& tooltip,
                  QWidget*       parent = nullptr);

    bool sectionOn() const { return _sectionOn; }
    bool fileOn(int i) const { return i == 0 ? _f1On : _f2On; }

    void setSectionOn(bool on);        ///< no signal
    void setFileOn(int i, bool on);    ///< no signal
    void setFileCount(int n);          ///< show "1"/"2" only when n > 1

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

  signals:
    void changed();                    ///< any of section/file states toggled
    /// Request to scroll the report to this section. fileIndex: -1 = section /
    /// whole, 0 = Chart #1, 1 = Chart #2. Emitted when a state is enabled or on
    /// a Ctrl-click of an already-enabled body/mini.
    void navigate(int fileIndex);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

  private:
    QRect miniRect(int i) const;       ///< geometry of the "1"/"2" box (i=0/1)

    QString _glyph;
    QFont   _glyphFont;
    QFont   _miniFont;
    bool    _sectionOn = false;
    bool    _f1On      = true;
    bool    _f2On      = true;
    int     _fileCount = 1;

    bool minisVisible() const { return _fileCount > 1 && _sectionOn; }

    // Theme colors (defaults from palette; overridden by .qss qproperty-*).
    QColor _offColor;
    QColor _textColor;
    QColor _borderColor;
    QColor _miniOffColor;
    QColor _miniOffTextColor;
};

/* ============================ DISPLAY MODE BUTTON
 * ======================================== */

/// Compact, self-painted clock-glyph dropdown for the speculum display-mode
/// selector (Local/Sidereal/RA). A plain QToolButton with a menu sizes itself
/// (and lays out its content) using the style's generic min-width and
/// menu-indicator reservations, which left a lot of dead space between the
/// glyph/abbreviation and the drop-down arrow. This subclass keeps
/// QToolButton's menu-popup behavior but paints its own tightly-fitted frame,
/// glyph, abbreviation and arrow instead — same approach as SectionToggle.
class DisplayModeButton : public QToolButton {
    Q_OBJECT
    // Painted colors, overridable from the theme .qss via `qproperty-*`.
    Q_PROPERTY(QColor offColor          MEMBER _offColor)
    Q_PROPERTY(QColor hoverColor        MEMBER _hoverColor)
    Q_PROPERTY(QColor activeColor       MEMBER _activeColor)
    Q_PROPERTY(QColor textColor         MEMBER _textColor)
    Q_PROPERTY(QColor activeTextColor   MEMBER _activeTextColor)
    Q_PROPERTY(QColor borderColor       MEMBER _borderColor)
    Q_PROPERTY(QColor hoverBorderColor  MEMBER _hoverBorderColor)
    Q_PROPERTY(QColor activeBorderColor MEMBER _activeBorderColor)
  public:
    explicit DisplayModeButton(QWidget* parent = nullptr);

    void setGlyph(const QString& glyph);   ///< icon shown left of the abbreviation
    void setAbbrev(const QString& abbrev); ///< short mode label (e.g. "LT")

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

  protected:
    void paintEvent(QPaintEvent*) override;

  private:
    QString _glyph;
    QString _abbrev;
    QFont   _glyphFont;
    QFont   _textFont;

    QColor _offColor, _hoverColor, _activeColor;
    QColor _textColor, _activeTextColor;
    QColor _borderColor, _hoverBorderColor, _activeBorderColor;
};

/* ================================== WIDGET
 * ======================================== */

class Plain : public AstroFileHandler {
    Q_OBJECT

  private:
    int chartsCount; // Track number of charts to detect changes

    QToolBar*      toolbar;
    SectionToggle* togInput;
    SectionToggle* togPlanets;
    SectionToggle* togHouses;
    SectionToggle* togAspects;
    SectionToggle* togDignities;
    SectionToggle* togDirections;
    SectionToggle* togSpeculum;
    SectionToggle* togParans;
    DisplayModeButton* displayModeButton; ///< clock-glyph dropdown of display modes
    QActionGroup*  displayModeGroup;   ///< exclusive Local/Sidereal/RA actions
    A::SpeculumDisplayMode _displayMode;
    QTextBrowser*  view;

    QLineEdit* searchField;    ///< toolbar search box (trailing end of toolbar)
    QTimer*    searchDebounce; ///< restarts on each keystroke; fires applySearch()
    QString    _reportHtml;    ///< last full (unfiltered) report, set by refresh()

    bool               showAllDiurnalEvents;
    bool               includeFixedStars;
    bool               showParanNatalRows;
    bool               includeOutOfOrbNatalRows;
    double             paranOrb;
    double             paranCityLatTol;
    int                paranMaxCitiesPerRow;
    bool               paranShowAbsent;
    unsigned           paranCityPopMask;       ///< CityPopTier bitmask
    unsigned           paranCityContinentMask; ///< CityContinent bitmask
    A::AspectSortOrder aspectSortOrder;

    // Cached aspects to avoid recalculating on every refresh
    A::AspectList cachedChart1Aspects;
    A::AspectList cachedChart2Aspects;
    A::AspectList cachedSynastryAspects;
    bool          aspectsCached = false;

    void updateAspectsCache();

    /// All eight section toggles, in toolbar order.
    QList<SectionToggle*> sectionToggles() const;

    /// Pairs a section toggle with its persistence keys.
    struct SectionKey {
        SectionToggle* t;
        const char*    mainKey; ///< legacy Text/describe* key for main on/off
        const char*    base;    ///< base for Plain/<base>_f1 / _f2 file keys
    };
    QList<SectionKey> sectionKeys() const;

    /// Switch the speculum display mode: sync the toolbutton/actions, and on an
    /// actual change emit displayModeChanged and re-render the report.
    void setDisplayMode(A::SpeculumDisplayMode mode);

    /// HTML anchor name for a section (fileIndex -1) or its per-file subsection.
    QString sectionAnchor(SectionToggle* t, int fileIndex) const;
    /// Scroll the report view to a section/subsection (see SectionToggle::navigate).
    void scrollToSection(SectionToggle* t, int fileIndex);

  signals:
    void displayModeChanged(A::SpeculumDisplayMode mode);

  private slots:
    void refresh();
    /// Re-render _reportHtml, filtered/highlighted for the current search text
    /// (or shown verbatim when the search box is empty). Called after refresh()
    /// rebuilds the report and after the search debounce timer fires.
    void applySearch();

  public slots:
    void setParanOrb(double orb);

  protected: // AstroFileHandler implementations
    void filesUpdated(MembersList m) override;
    void viewSettingsUpdated(MembersList m) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    AppSettings defaultSettings() override;
    AppSettings currentSettings() override;
    void        applySettings(const AppSettings&) override;
    void        setupSettingsEditor(AppSettingsEditor*) override;

  public:
    Plain(QWidget* parent = 0);
};

#endif // PLAIN_H
