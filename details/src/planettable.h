#ifndef PLANETTABLE_H
#define PLANETTABLE_H

#include "../../astroprocessor/src/chartsearch.h"
#include <Astroprocessor/Gui>
#include <QMap>
#include <QPointer>
#include <QSet>

class QTableWidget;
class QTableWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QProgressBar;
class QLabel;
class GeoSearchWidget;

/// The "Planets" dock: every body's position for chart 1 and chart 2 side by
/// side, plus a search mode in which positions are typed in and the charts
/// that match them are looked up.
///
/// (Named PlanetTable because `Planets` is the parked QML card-view slide.)
class PlanetTable : public AstroFileHandler {
    Q_OBJECT

  public:
    PlanetTable(QWidget* parent = nullptr);
    ~PlanetTable() override;

  signals:
    void planetSelected(A::PlanetId, int);
    /// A found chart, ready to be opened in a tab of its own.
    void addChart(AstroFile*);
    /// A found chart to show beside the base chart, as the tab's chart 2.
    void updateSecond(AstroFile*);
    /// A found chart to show in place of a chart 1 that is not a base chart.
    void updateFirst(AstroFile*);

  public slots:
    void setCurrentPlanet(A::PlanetId, int);

  protected: // AstroFileHandler implementation
    void filesUpdated(MembersList) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    AppSettings defaultSettings() override;
    AppSettings currentSettings() override;
    void        applySettings(const AppSettings&) override;

  private slots:
    void setSearchMode(bool on);
    void onCellClicked(int row, int column);
    void onItemChanged(QTableWidgetItem* item);
    void checkAll(bool on);
    void previewHit(QTreeWidgetItem* item);
    void headerMenu(const QPoint& pos);
    void startSearch();
    void searchFinished();
    void openHit(QTreeWidgetItem* item);
    void copySelection();

  private:
    enum Column {
        colGlyph, colBody, colChart1, colChart2,
        colDecl, colRA, colMundane, colHouse, // chart 1; each can be hidden
        numColumns
    };

    /// Search state of one body. Kept apart from the table items so it
    /// survives the rebuilds that follow every chart change.
    ///
    /// A ticked row is a search constraint. Until its position is typed
    /// over it stands for chart 1's own position and keeps following it.
    struct TargetRow {
        bool    use    = false;
        bool    edited = false; ///< `text` was typed, not taken from the chart
        QString text;
    };

    QList<A::PlanetId> rowIds() const;
    void               rebuild(bool force = false);
    void               refreshRow(int row);
    void               updateColumns();
    /// Longitude of a body in file `fi`, in the zodiac the column shows
    /// (the search zodiac while searching, the chart's own otherwise).
    bool               shownPosition(int fi, A::PlanetId id, double& lon,
                                     bool& retro) const;
    const A::Zodiac&   shownZodiac(int fi) const;
    int                rowOf(A::PlanetId id) const;
    A::ZodiacId        searchZodiac() const;
    void               stopSearch();
    /// A chart the typed positions can fall back on: chart 1, unless chart 1
    /// is itself a match being previewed.
    bool               hasBase() const;
    /// Chart 1 is a chart in its own right (a person, event, return or
    /// composite) -- one that derived charts and events hang off, and that a
    /// previewed match must therefore sit beside rather than replace.
    bool               chart1IsBaseChart() const;
    void               configureFile(AstroFile* af, const A::ChartSearchHit& h);
    void               setStatus(const QString& text);

    QTableWidget*    _table;
    QPushButton*     _searchBtn;
    QWidget*         _searchPanel;
    QComboBox*       _zodiac;
    QDoubleSpinBox*  _orb;
    QSpinBox*        _misses;
    QSpinBox*        _yearFrom;
    QSpinBox*        _yearTo;
    QCheckBox*       _usePlace;
    GeoSearchWidget* _place;
    class CheckHeader;
    CheckHeader*     _header;
    QPushButton*     _find;
    QProgressBar*    _progress;
    QLabel*          _status;
    QTreeWidget*     _results;

    QMap<A::PlanetId, TargetRow> _targets;
    A::ChartSearcher*            _searcher = nullptr;
    QList<A::ChartSearchHit>     _hits; ///< rows of _results, by index
    QString                      _hitsPlaceName; ///< known place searched with
    QString                      _hitsPlaceTz;   ///< its IANA zone id, if any
    /// The chart that browsing the matches re-points, so stepping through
    /// them does not pile up charts.
    QPointer<AstroFile>          _preview;
    QSet<int>                    _hiddenCols { colMundane };

    bool _searchMode    = false;
    bool _zodiacFollows = true; ///< search zodiac tracks the tab's until chosen
    bool _updating   = false; ///< table being filled: ignore itemChanged
    bool _inSelect   = false;
};

#endif // PLANETTABLE_H
