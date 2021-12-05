#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabBar>
#include <QDockWidget>

#include <Astroprocessor/Gui>
#include "help.h"
#include "slidewidget.h"
#include "afileinfo.h"

class QSortFilterProxyModel;
class QFileSystemWatcher;
class QTreeView;
class QStandardItemModel;
class QLineEdit;
class QActionGroup;
class AstroFileEditor;
class GeoSearchWidget;
class QComboBox;

/* =========================== ASTRO FILE INFO ====================================== */

class AstroFileInfo : public AstroFileHandler
{
    Q_OBJECT

private:
    int currentIndex;
    QPushButton* edit;
    QLabel* shadow;
    bool showAge;

    AstroFile* currentFile() { return file(currentIndex); }
    void setText(const QString& str);
    void refresh();

protected:
    void filesUpdated(MembersList members);  // AstroFileHandler implementations

signals:
    void clicked();

public:
    AstroFileInfo(QWidget *parent = nullptr);
    void setCurrentIndex(int i) { currentIndex = i; }

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void applySettings(const AppSettings&);
    void setupSettingsEditor(AppSettingsEditor*);
};


/* =========================== ASTRO WIDGET ========================================= */

class AstroWidget : public QWidget
{
    Q_OBJECT;

private:
    AstroFileEditor*  editor;
    GeoSearchWidget*  geoWdg;
    QToolBar*         toolBar;
    QActionGroup*     actionGroup;
    SlideWidget*      slides;
    AstroFileInfo*    fileView, *fileView2nd;
    QComboBox*        zodiacSelector;
    QComboBox*        hsystemSelector;
    QComboBox*        aspectsSelector;
    QComboBox*        aspectModeSelector;
    QComboBox*        harmonicSelector;
    QToolBar*         dynAspectControls;
    QWidgetList       horoscopeControls;
    QList<AstroFileHandler*> handlers;
    QList<AstroFileHandler*> dockHandlers;
    QList<QDockWidget*> docks;

    QAction* _clickedHarmonic = nullptr;

    bool              _dynAspChange = false;

    void setupFile(AstroFile* file, bool suspendUpdate = false);
    AstroFileList files() { return fileView->files(); }
    QString vectorToString(const QVector3D& v);
    QVector3D vectorFromString(const QString& str);

    void attachHandler(AstroFileHandler* w);
    void addSlide(AstroFileHandler* w, const QIcon& icon, QString title);
    void addDockWidget(AstroFileHandler* w, QString title, bool scrollable, QString objectName = "");
    void addHoroscopeControls();
    void switchToSingleAspectSet();
    void switchToSynastryAspectSet();

    A::AspectSetId& overrideAspectSet()
    { static A::AspectSetId s_override = -1; return s_override; }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void applyGeoSettings(AppSettings&);
    void toolBarActionClicked();
    void currentSlideChanged();
    void horoscopeControlChanged();
    void aspectSelectionChanged();
    void destroyingFile();
    void destroyEditor();

public slots:
    void openEditor();
    void setHarmonic(double);

signals:
    void helpRequested(QString tag);
    void appendFileRequested();
    void swapFilesRequested(int, int);

public:
    AstroWidget(QWidget *parent = nullptr);
    QToolBar* getToolBar() { return toolBar; }
    const QWidgetList& getHoroscopeControls() const { return horoscopeControls; }
    QToolBar* getDynAspectControls() const { return dynAspectControls; }
    QComboBox* getAspectsSelector() const { return aspectsSelector; }
    const QList<QDockWidget*>& getDockPanels() { return docks; }
    template <class T> T* findDockHdlr() const
    {
        for (auto d: dockHandlers) {
            if (auto td = qobject_cast<T*>(d)) {
                return td;
            }
        }
        return nullptr;
    }

    void setFiles(const AstroFileList& files);

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void applySettings(const AppSettings&);
    void setupSettingsEditor(AppSettingsEditor*);

    const A::AspectSetId& overrideAspectSet() const
    { return const_cast<AstroWidget*>(this)->overrideAspectSet(); }


    friend class FilesBar;
    friend class Transits;
    friend class AstroFileHandler;
};


/* =========================== ASTRO FILE DATABASE ================================== */

class AstroDatabase : public QFrame
{
    Q_OBJECT

private:
    QTreeView* fileList;
    QStandardItemModel* dirModel;
    QSortFilterProxyModel* searchProxy;
    QFileSystemWatcher* fswatch;
    QLineEdit* search;

protected:
    virtual void keyPressEvent(QKeyEvent*);
    virtual bool eventFilter(QObject *, QEvent *);

private slots:
    void showContextMenu(QPoint);
    void openSelected();
    void openSelectedInNewTab();
    void openSelectedWithTransits();
    void openSelectedAsSecond();
    void openSelectedWithSolarReturn();
    void openSelectedSolarReturnInNewTab();
    void openSelectedComposite();
    void findSelectedDerived();
    void deleteSelected();
    void searchFilter(const QString&);

public slots:
    void updateList();

signals:
    void fileRemoved(const AFileInfo&);
    void openFile(const AFileInfo&);
    void openFileInNewTab(const AFileInfo&);
    void openFileInNewTabWithTransits(const AFileInfo&);
    void openFileAsSecond(const AFileInfo&);
    void openFilesComposite(const AFileInfoList&);
    void openFileReturn(const AFileInfo&, const QString& = "Sun");
    void openFileInNewTabWithReturn(const AFileInfo&, const QString& = "Sun");
    void findSelectedDerived(const AFileInfo&);

public:
    AstroDatabase(QWidget *parent = nullptr);
};


/* =========================== FILES BAR ============================================ */

class FilesBar : public QTabBar
{
    Q_OBJECT

private:
    bool askToSave;
    QList<AstroFileList> files;

    QString _finding, _findingDerived;

    void updateTab(int index);
    int getTabIndex(AstroFile* f, bool seekFirstFileOnly = false);
    int getTabIndex(QString name, bool seekFirstFileOnly = false);

private slots:
    void swapTabs(int, int);
    void fileUpdated(AstroFile::Members);
    void fileDestroyed();

public slots:
    void addFile(AstroFile* file);
    void addNewFile() { addFile(new AstroFile); }
    void editNewChart();
    void findChart();
    void swapCurrentFiles(int, int);
    void openFile(const AFileInfo& name);
    void openFile(AstroFile* af);
    void openFileInNewTab(const AFileInfo& name);
    void openFileInNewTabWithTransits(const AFileInfo& name);
    void openFileInNewTabWithTransits(const AFileInfo& name,
                                      AstroFile* af);
    void openTransits(int);
    void openFileAsSecond(const AFileInfo& name = AFileInfo());
    void openTransitsAsSecond(AstroFile* af);
    void openFileComposite(const AFileInfoList& names);
    void openFileReturn(const AFileInfo& name, const QString& body);
    void findDerivedChart(const AFileInfo& name);
    void openFileInNewTabWithReturn(const AFileInfo& name, const QString& body);
    void nextTab() { setCurrentIndex((currentIndex() + 1) % count()); }
    bool closeTab(int);

public:
    FilesBar(QWidget *parent = nullptr);

    void setAskToSave(bool b) { askToSave = b; }
    const AstroFileList& currentFiles()
    {
        if (count() && currentIndex() < count()) return files[currentIndex()];
#if 1
        static AstroFileList dummy;
        return dummy;
#endif
    }
};


/* =========================== MAIN WINDOW ========================================== */

class MainWindow : public QMainWindow, public Customizable
{
    Q_OBJECT

private:
    bool askToSave;

    FilesBar*      filesBar;
    AstroWidget*   astroWidget;
    AstroDatabase* astroDatabase;
    QDockWidget*   databaseDockWidget;
    QToolBar       *toolBar, *toolBar2, *helpToolBar;
    QMenu*         panelsMenu;

    void addToolBarActions();
    QAction* createActionForPanel(QWidget* w/*, const QIcon &icon*/);

private slots:
    void saveFile() { filesBar->currentFiles()[0]->save(); astroDatabase->updateList(); }
    void currentTabChanged();
    void showSettingsEditor() { openSettingsEditor(); }
    void showAbout();
    void gotoUrl(QString url = "");
    void contextMenu(QPoint);

protected:
    AppSettings defaultSettings();      // 'Customizable' class implementations
    AppSettings currentSettings();
    void applySettings(const AppSettings&);
    void setupSettingsEditor(AppSettingsEditor*);

    void closeEvent(QCloseEvent*);

public:
    MainWindow(QWidget *parent = nullptr);

    static MainWindow* instance();
    static AstroWidget* theAstroWidget() { return instance()->astroWidget; }
};

#endif // MAINWINDOW_H
