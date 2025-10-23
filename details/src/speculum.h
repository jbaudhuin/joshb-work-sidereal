#ifndef SPECULUM_H
#define SPECULUM_H

#include <QDateTime>
#include <QModelIndex>
#include <Astroprocessor/Gui>

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

    // Filter state
    bool      _filterActive;
    QDateTime _filterCenterTime;
    double    _filterOrbMinutes;

    // Settings
    bool        _showFixedStars;
    short       m_timezone;
    A::PlanetId _selectedPlanet;
    int         _fileIndex;
};

#endif // SPECULUM_H
