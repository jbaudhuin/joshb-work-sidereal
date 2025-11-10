#ifndef PLAIN_H
#define PLAIN_H

#include <Astroprocessor/Calc>
#include <Astroprocessor/Gui>
#include <Astroprocessor/Output>

class QCheckBox;
class QTextBrowser;
class QToolBar;
class QButtonGroup;
class QRadioButton;

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
    QWidget*      chartSelectorWidget; // Store reference to show/hide
    QButtonGroup* chartSelector;
    QRadioButton* showChart1;
    QRadioButton* showChart2;
    QRadioButton* showBothCharts;
    QWidget*      displayModeWidget; // Container for display mode controls
    QButtonGroup* displayModeSelector;
    QRadioButton* showLocalTime;
    QRadioButton* showSiderealTime;
    QRadioButton* showRightAscension;
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

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void        applySettings(const AppSettings&);
    void        setupSettingsEditor(AppSettingsEditor*);

  public:
    Plain(QWidget* parent = 0);
};

#endif // PLAIN_H
