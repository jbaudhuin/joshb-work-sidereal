#ifndef SPECULUM_H
#define SPECULUM_H

#include <Astroprocessor/Gui>
#include <QDateTime>
#include <QModelIndex>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QSpinBox;
class QPushButton;

class Speculum : public AstroFileHandler {
    Q_OBJECT

  public:
    Speculum(QWidget* parent = nullptr);
    ~Speculum() { }

    QTableWidget* speculumTable() const { return _table; }

  protected: // AstroFileHandler implementation
    void filesUpdated(MembersList);

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void        applySettings(const AppSettings&);
    void        setupSettingsEditor(AppSettingsEditor*);

    void updateSpeculumDisplay();
    void clear();

  signals:
    void planetSelected(A::PlanetId, int);
    void timeSelected(const QDateTime& time);

  protected slots:
    void onCellClicked(int row, int column);
    void onFilterOrbChanged();
    void onClearFilter();
    void onRadixButtonClicked(bool checked);
    void onChartButtonClicked(int chartIndex);
    void refreshSpeculum();

  public slots:
    void setCurrentPlanet(A::PlanetId, int);
    void filterByTime(const QDateTime& centerTime, double orbMinutes);

  private:
    void populateSpeculumTable();
    void setupTableHeaders();
    void addPlanetRow(const A::Planet& planet, int row);
    void addStarRow(const A::Star& star, int row);
    bool isTimeWithinOrb(const QDateTime& time1,
                         const QDateTime& time2,
                         double           orbMinutes);
    void highlightFilteredRows();
    void clearClickHighlights();

    QTableWidget* _table;
    QLabel*       _filterLabel;
    QSpinBox*     _orbSpinBox;
    QPushButton*  _clearFilterBtn;
    QPushButton*  _radixBtn;
    QPushButton*  _chart1Btn;
    QPushButton*  _chart2Btn;

    // Filter state
    bool      _filterActive;
    QDateTime _filterCenterTime;
    QDateTime _radixTime;
    double    _filterOrbMinutes;
    int       _clickedRow;
    int       _clickedCol;

    // Settings
    bool        _showFixedStars;
    short       m_timezone;
    A::PlanetId _selectedPlanet;
    int         _fileIndex;
    int         _selectedChartIndex; // Which chart to display (0 or 1)
    A::SpeculumDisplayMode
        _displayMode; // Display mode: Local Time, Sidereal Time, or RA
};

#endif // SPECULUM_H
