#ifndef SPECULUM_H
#define SPECULUM_H

#include <Astroprocessor/Gui>
#include <QDateTime>
#include <QModelIndex>
#include <QStyledItemDelegate>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QDoubleSpinBox;
class QPushButton;
class QCheckBox;

// Custom delegate for painting highlighted cells
class SpeculumDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit SpeculumDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

class Speculum : public AstroFileHandler {
    Q_OBJECT

  public:
    // Custom data roles for cell highlighting
    static const int CellHighlightRole = Qt::UserRole + 2;
    
    enum CellHighlight {
        NoHighlight = 0,
        ClickedHighlight = 1,
        MatchedHighlight = 2
    };

    Speculum(QWidget* parent = nullptr);
    ~Speculum() { }

    QTableWidget* speculumTable() const { return _table; }

  protected: // AstroFileHandler implementation
    void filesUpdated(MembersList) override;

    AppSettings defaultSettings() override;
    AppSettings currentSettings() override;
    void        applySettings(const AppSettings&) override;

    void updateSpeculumDisplay();
    void clear();

  signals:
    void planetSelected(A::PlanetId, int);
    void timeSelected(const QDateTime& time);
    void orbSettingChanged(double orbDegrees);

  public slots:
    void setDisplayMode(A::SpeculumDisplayMode mode);

  protected slots:
    void onCellClicked(int row, int column);
    void onFilterOrbChanged();
    void onClearFilter();
    void onRadixButtonClicked(bool checked);
    void onChartButtonClicked(int chartIndex);
    void refreshSpeculum();
    void onThemeChanged();

  public slots:
    void setCurrentPlanet(A::PlanetId, int);
    void filterByTime(const QDateTime& centerTime, double orbMinutes);

  private:
    void populateSpeculumTable();
    void setupTableHeaders();
    void addPlanetRow(const A::Planet& planet, int row);
    void addNatalParanRow(const QString& name,
                         QDateTime      angleTransit[4],
                         double         angleTransitRA[4],
                         int            row);
    void addStarRow(const A::Star& star, int row);
    bool isTimeWithinOrb(const QDateTime& time1,
                         const QDateTime& time2,
                         double           orbMinutes);
    void highlightFilteredRows();
    void clearClickHighlights();

    QTableWidget*   _table;
    QLabel*         _filterLabel;
    QDoubleSpinBox* _orbSpinBox;
    QPushButton*    _clearFilterBtn;
    QPushButton*    _radixBtn;
    QPushButton*    _chart1Btn;
    QPushButton*    _chart2Btn;

    // Filter state
    bool      _filterActive;
    QDateTime _filterCenterTime;
    QDateTime _radixTime;
    double    _filterOrbMinutes;
    int       _clickedRow;
    int       _clickedCol;

    // Settings
    bool        _showFixedStars;
    bool        _showParanNatalRows;
    double      m_timezone;
    A::PlanetId _selectedPlanet;
    int         _fileIndex;
    int         _selectedChartIndex; // Which chart to display (0 or 1)
    A::SpeculumDisplayMode
        _displayMode; // Display mode: Local Time, Sidereal Time, or RA
};

#endif // SPECULUM_H
