#ifndef PLAIN_H
#define PLAIN_H

#include <Astroprocessor/Calc>
#include <Astroprocessor/Gui>
#include <Astroprocessor/Output>

class QCheckBox;
class QTextBrowser;
class QToolBar;
class QComboBox;
class QPushButton;

/* ================================== WIDGET
 * ======================================== */

class Plain : public AstroFileHandler {
    Q_OBJECT

  private:
    int chartsCount; // Track number of charts to detect changes

    QToolBar*     toolbar;
    QAction*      describeInput;
    QAction*      describePlanets;
    QAction*      describeHouses;
    QAction*      describeAspects;
    QAction*      describePower;
    QAction*      describeParans;
    QAction*      describeSpeculum;
    QPushButton*  chart1Btn;
    QPushButton*  chart2Btn;
    QComboBox*    displayModeSelector;
    QTextBrowser* view;

    bool               showAllDiurnalEvents;
    bool               includeFixedStars;
    double             paranOrb;
    A::AspectSortOrder aspectSortOrder;

    // Cached aspects to avoid recalculating on every refresh
    A::AspectList cachedChart1Aspects;
    A::AspectList cachedChart2Aspects;
    A::AspectList cachedSynastryAspects;
    bool          aspectsCached = false;

    void updateAspectsCache();

  private slots:
    void refresh();

  public slots:
    void setParanOrb(double orb);

  protected: // AstroFileHandler implementations
    void filesUpdated(MembersList m);
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void        applySettings(const AppSettings&);
    void        setupSettingsEditor(AppSettingsEditor*);

  public:
    Plain(QWidget* parent = 0);
};

#endif // PLAIN_H
