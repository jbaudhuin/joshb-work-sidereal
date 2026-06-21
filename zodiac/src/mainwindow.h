#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabBar>
#include <QDockWidget>
#include <QTreeView>
#include <QSet>

#include <Astroprocessor/Gui>
#include "help.h"
#include "slidewidget.h"
#include "afileinfo.h"

class QSortFilterProxyModel;
class QFileSystemWatcher;
class QStandardItemModel;
class QStandardItem;
class QLineEdit;
class QActionGroup;
class AstroFileEditor;
class GeoSearchWidget;
class QComboBox;
class QToolButton;
class QLabel;

/* =========================== ASTRO FILE INFO
 * ====================================== */

class AstroFileInfo : public AstroFileHandler {
    Q_OBJECT

  private:
    int          currentIndex;
    QPushButton* edit;
    QLabel*      shadow;
    bool         showAge;

    AstroFile* currentFile() { return file(currentIndex); }
    void       setText(const QString& str);
    void       refresh();

  protected:
    void filesUpdated(MembersList members) override; // AstroFileHandler implementations
    void viewSettingsUpdated(MembersList) override { } // only cares about file data
    void mousePressEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  signals:
    void clicked();
    void middleClicked(int fileIndex);
    void chartDropped(const QString& filePath, int targetIndex);

  public:
    AstroFileInfo(QWidget* parent = nullptr);
    void setCurrentIndex(int i) { currentIndex = i; }

    AppSettings defaultSettings() override;
    AppSettings currentSettings() override;
    void        applySettings(const AppSettings&) override;
    void        setupSettingsEditor(AppSettingsEditor*) override;
};

/* =========================== ASTRO WIDGET
 * ========================================= */

class AstroWidget : public QWidget {
    Q_OBJECT;

  private:
    AstroFileEditor*         editor;
    GeoSearchWidget*         geoWdg;
    QToolBar*                toolBar;
    QActionGroup*            actionGroup;
    SlideWidget*             slides;
    AstroFileInfo *          fileView, *fileView2nd;
    QComboBox*               zodiacSelector;
    QComboBox*               hsystemSelector;
    QComboBox*               aspectsSelector;
    QComboBox*               aspectModeSelector;
    QToolButton*             gcToggle = nullptr;
    QToolButton*             pvToggle = nullptr;
    QComboBox*               harmonicSelector;
    QToolBar*                dynAspectControls;
    QWidgetList              horoscopeControls;
    QList<AstroFileHandler*> handlers;
    QList<AstroFileHandler*> dockHandlers;
    QList<QDockWidget*>      docks;

    QAction* _clickedHarmonic = nullptr;

    bool _dynAspChange = false;

    AstroFileList files() { return fileView->files(); }
    QString       vectorToString(const QVector3D& v);
    QVector3D     vectorFromString(const QString& str);

    void attachHandler(AstroFileHandler* w);
    void addSlide(AstroFileHandler* w, const QIcon& icon, QString title);
    void addDockWidget(AstroFileHandler* w,
                       QString           title,
                       bool              scrollable,
                       QString           objectName = "");
    void addHoroscopeControls();
    void switchToSingleAspectSet();
    void switchToSynastryAspectSet();

    A::AspectSetId& overrideAspectSet()
    {
        static A::AspectSetId s_override = -1;
        return s_override;
    }

    // When true, a focal click draws the clicked aspect PLUS every other
    // aspect involving either focal body (the "bigger picture", via
    // findClusters need=fp).  When false (the default), the focal click draws
    // only the clicked aspect/pattern among the focal bodies ("exactly this",
    // via the override direct path) — the behavior wanted for harmonic-pattern
    // selections (harmonics table, TA/TNA events) and midpoints.
    bool& focalExpand()
    {
        static bool s_focalExpand = false;
        return s_focalExpand;
    }

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
    void handleChartDroppedOnInputWidget(const QString& filePath, int targetIndex);

  public slots:
    void openEditor();
    void setHarmonic(double);
    void setHarmonicQuietly(double h);

  signals:
    void helpRequested(QString tag);
    void appendFileRequested();
    void swapFilesRequested(int, int);
    void closeFileRequested(int fileIndex);
    void chartFileDropped(const QString& filePath);
    void chartDroppedOnInputWidget(const QString& filePath, int targetIndex);

  public:
    AstroWidget(QWidget* parent = nullptr);
    void               setupFile(AstroFile* file, bool suspendUpdate = false);
    QToolBar*          getToolBar() { return toolBar; }
    const QWidgetList& getHoroscopeControls() const
    {
        return horoscopeControls;
    }
    QToolBar*  getDynAspectControls() const { return dynAspectControls; }
    QComboBox* getAspectsSelector() const { return aspectsSelector; }
    const QList<QDockWidget*>& getDockPanels() { return docks; }
    template <class T>
    T* findDockHdlr() const
    {
        for (auto d : dockHandlers) {
            if (auto td = qobject_cast<T*>(d)) {
                return td;
            }
        }
        return nullptr;
    }

    template <class T>
    T* findSlide() const
    {
        for (auto h : handlers) {
            if (auto th = qobject_cast<T*>(h)) {
                return th;
            }
        }
        return nullptr;
    }

    void setFiles(const AstroFileList& files);

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void        applySettings(const AppSettings&);
    void        setupSettingsEditor(AppSettingsEditor*);

    const A::AspectSetId& overrideAspectSet() const
    {
        return const_cast<AstroWidget*>(this)->overrideAspectSet();
    }

    friend class FilesBar;
    friend class Transits;
    friend class Harmonics;
    friend class AstroFileHandler;
    friend class MainWindow;  // Aspect Range Navigator drives focalExpand()
};

/* =========================== ASTRO FILE DATABASE
 * ================================== */

class AstroDatabase;

class FileTreeView : public QTreeView {
    Q_OBJECT

  private:
    AstroDatabase* database;
    QModelIndex dragStartIndex; // Track the index where drag started

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

  public:
    FileTreeView(AstroDatabase* parent = nullptr);
};

class AstroDatabase : public QFrame {
    Q_OBJECT

  private:
    enum entryType { unknownType, fileType, dirType, dbType, sessionType };
    enum { PathRole = Qt::UserRole + 1, TypeRole, SessionPathRole };

    FileTreeView*          fileList;
    QStandardItemModel*    dirModel;
    QSortFilterProxyModel* searchProxy;
    QFileSystemWatcher*    fswatch;
    QLineEdit*             search;
    QString                _renamingOldName;
    QString                _renamingDir;
    bool                   _searchActive = false;
    QSet<QString>          _expandSnapshot; // expansion state captured when a search began

  protected:
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

  public:
    bool validateDropTarget(const QPoint& pos, QString& targetDir);
    void performMove(const QString& targetDir);
    void performCopy(const QString& targetDir);
    void showContextMenu(QPoint);

  private slots:
    void saveCurrent(const QModelIndex& qmi);
    void newDirectory(const QModelIndex& qmi);
    void deleteDirectory(const QModelIndex& qmi);
    bool directoryHasChartFiles(const QString& dirPath);
    void setTypeForSelected();
    void renameSelected();
    void moveToFolder();
    void moveSelected(const QString& targetDir);
    void copySelected(const QString& targetDir);
    void handleItemRenamed(QStandardItem* item);
    void openSelected();
    void openSelectedInNewTab();
    void openSelectedWithTransits();
    void openSelectedWithProgressions();
    void openSelectedAsSecond();
    void openSelectedWithSolarReturn();
    void openSelectedSolarReturnInNewTab();
    void openSelectedComposite();
    void findSelectedDerived();
    void deleteSelected();
    void openSessionInNewWindow();
    void loadSessionsInCurrent();
    void renameSession();
    void deleteSessions();
    void searchFilter(const QString&);

  public slots:
    void updateList();

  public:
    void saveDatabaseState();
    void restoreDatabaseState();

  private:
    QSet<QString> currentExpandedPaths(); // live expansion state, as a set of paths
    void captureExpansionSnapshot();      // snapshot expansion before a search expands the tree
    void applyExpansionSnapshot();        // restore the snapshot when a search is cleared

  signals:
    void fileRemoved(const AFileInfo&);
    void openFile(const AFileInfo&);
    void openFileInNewTab(const AFileInfo&);
    void openFileInNewTabWithTransits(const AFileInfo&);
    void openFileInNewTabWithProgressions(const AFileInfo&);
    void openFileAsSecond(const AFileInfo&);
    void openFilesComposite(const AFileInfoList&);
    void openFileReturn(const AFileInfo&, const QString& = "Sun");
    void openFileInNewTabWithReturn(const AFileInfo&, const QString& = "Sun");
    void findSelectedDerived(const AFileInfo&);
    void saveCurrentToDirectory(const QString& directory);
    void closeSecondaryChartRequested();

  public:
    AstroDatabase(QWidget* parent = nullptr);
};

/* =========================== FILES BAR
 * ============================================ */

class FilesBar : public QTabBar {
    Q_OBJECT
    
    friend class MainWindow;  // Allow MainWindow to access private members for session management

  private:
    bool                 askToSave;
    QList<AstroFileList> files;

    QString _finding, _findingDerived;

    int  getTabIndex(AstroFile* f, bool seekFirstFileOnly = false);
    int  getTabIndex(QString name, bool seekFirstFileOnly = false);

  public:
    void updateTab(int index);
    void saveFilesToSession();

  private slots:
    void swapTabs(int, int);
    void fileUpdated(AstroFile::Members);
    void fileDestroyed();

  public slots:
    void addFile(AstroFile* file);
    void addNewFile() { addFile(new AstroFile); }
    void editNewChart();
    void findChart();
    void saveAsCurrentFile();
    void swapCurrentFiles(int, int);
    void openFile(const AFileInfo& name);
    void openFile(AstroFile* af);
    void openFileInNewTab(const AFileInfo& name);
    void openFileInNewTabWithTransits(const AFileInfo& name);
    void openFileInNewTabWithTransits(const AFileInfo& name, AstroFile* af);
    void openFileInNewTabWithProgressions(const AFileInfo& name);
    void openTransits(int);
    void openFileAsSecond(const AFileInfo& name = AFileInfo());
    void openTransitsAsSecond(AstroFile* af);
    void openFileComposite(const AFileInfoList& names);
    void openFileReturn(const AFileInfo& name, const QString& body);
    void findDerivedChart(const AFileInfo& name);
    void openFileInNewTabWithReturn(const AFileInfo& name, const QString& body);
    void nextTab() { setCurrentIndex((currentIndex() + 1) % count()); }
    bool closeTab(int);
    bool closeSecondaryChart();
    bool closeFileByIndex(int fileIndex);

  protected:
    void childEvent(QChildEvent* event) override;
    void labelScrollButtons();

  public:
    FilesBar(QWidget* parent = nullptr);

    void                 setAskToSave(bool b) { askToSave = b; }
    bool                 getAskToSave() const { return askToSave; }
    AstroFile*           findOpenFile(const QString& dir, const QString& name);
    void                 refreshTabForFile(AstroFile* file);
    const AstroFileList& currentFiles()
    {
        if (count() && currentIndex() < count()) return files[currentIndex()];
#if 1
        static AstroFileList dummy;
        return dummy;
#endif
    }
};

/* =========================== SESSION MANAGER
 * ========================================== */

class SessionManager {
public:
    // Session info structure
    struct SessionInfo {
        QString filename;      // e.g., "session-1734124800.ini"
        QDateTime timestamp;   // last-saved time (preferred display date)
        QDateTime inaugurated; // creation time (for tooltips)
        int tabCount;
        QString name;          // Optional user-defined name

        // Format session info for display
        QString displayName() const;
    };

    // Format a timestamp as a friendly relative/absolute string
    // (e.g. "Today at 2:30 PM", "Mon 14 Dec 2:30 PM").
    static QString friendlyTimestamp(const QDateTime& dt);
    
    // Get the directory where session files are stored (same as user charts)
    static QString sessionDirectory();
    
    // Get the settings file path (in user's chart directory)
    static QString settingsFile();
    
    // Get current session filename based on timestamp
    static QString currentSessionFile();
    
    // Set current session file (for session restore)
    static void setCurrentSessionFile(const QString& filename);
    
    // Get most recent session from sessions.ini MRU
    static QString getMostRecentSession();
    
    // Clone a settings file to a new session file, optionally excluding sections
    static bool cloneSessionFile(const QString& sourceFile, const QString& destFile, 
                                  const QStringList& excludeSections = QStringList());
    
    // Add session to MRU in sessions.ini
    static void addToMRU(const QString& sessionFile);

    // Remove a session from the MRU (e.g. when a throwaway session is discarded)
    static void removeFromMRU(const QString& sessionFile);
    
    // Get list of recent sessions from sessions.ini
    static QList<SessionInfo> getRecentSessions(int maxCount = 10);
    
    // Create a fresh timestamped auto-session file, set it current, and stamp
    // its inauguration time.  Returns the new file path.
    static QString newAutoSessionFile();

    // True if a session file has already been explicitly set (e.g. via
    // --load-session) — distinguishes that case from a cold normal launch.
    static bool hasExplicitCurrentSession();

    // Read API key from APIKey.ini
    static QString readAPIKey();
    
    // Write API key to APIKey.ini
    static void writeAPIKey(const QString& apiKey);
    
    // Get/set session name
    static QString getSessionName(const QString& sessionFile);
    static void setSessionName(const QString& sessionFile, const QString& name);
    
    // Generate session filename from name or timestamp
    static QString sessionFileFromName(const QString& name);
    static QString sessionNameFromFile(const QString& filepath);
    static QString sessionFileFromTimestamp(qint64 timestamp = 0); // 0 = current time
    
    // Check if session file is named (not timestamped)
    static bool isNamedSession(const QString& sessionFile);
    
private:
    static QString s_currentSessionFile;
};

/* =========================== MAIN WINDOW
 * ========================================== */

class MainWindow : public QMainWindow, public Customizable {
    Q_OBJECT

  private:
    bool _skipRestore;
    bool _launchedWithNew;  // Track if launched with --new flag
    bool _autoRestore;      // Track if launched with --auto-restore flag
    bool _isServerInstance; // Track if this instance is running the single-instance server
    bool askToSave;

    FilesBar*      filesBar;
    AstroWidget*   astroWidget;
    AstroDatabase* astroDatabase;
    QDockWidget*   databaseDockWidget;
    QToolBar *     toolBar, *toolBar2, *helpToolBar;
    QMenu*         panelsMenu;

    // Paran cycling transport — a draggable/floatable toolbar that steps the
    // current paran chart's moving file through its in-orb occurrences. Always
    // present; disabled when the current tab is not a paran chart.
    QToolBar* paranToolBar   = nullptr;
    QAction*  _paranFirst    = nullptr;
    QAction*  _paranPrev     = nullptr;
    QAction*  _paranPeak     = nullptr;
    QAction*  _paranNext     = nullptr;
    QAction*  _paranLast     = nullptr;
    QLabel*   _paranLabel    = nullptr;
    int       _paranOccIndex = -1;
    QList<QMetaObject::Connection> _paranConns; // to current files' changed()

    void       buildParanToolBar();
    AstroFile* paranMovingFile() const;
    void       rewireParanTransport();  // re-subscribe to current files' changed()
    void       updateParanTransport();  // reconcile enable/label with current chart
    void       paranStep(int mode);     // -2 first, -1 prev, 0 peak, +1 next, +2 last

    std::string _APIKey;
    
    // Session management
    QDateTime _sessionStartTime;      // When this instance started
    QString   _currentSessionKey;     // Current session being worked on
    bool      _hadOverlappingInstances; // Track if other instances existed during lifetime

    void     addToolBarActions();
    QAction* createActionForPanel(QWidget* w /*, const QIcon &icon*/);
    
    void saveSession();
    void restoreSession();
    // True if the current session holds anything worth persisting (a saved
    // chart or unsaved edits) — false for a pristine "Untitled"-only session.
    bool sessionWorthKeeping() const;
    // Resolve which session to open at launch (handles --new / --auto-restore /
    // --load-session and the named-session restore prompt).  Sets the current
    // session file and may set _skipRestore / _pendingRestoreDialog.  Returns
    // the chosen session file path.
    QString chooseStartupSession();
    // Show the session-picker list dialog; returns the chosen session file path
    // (absolute), or an empty string if cancelled / none available.
    QString pickSessionFile();
    bool hasOtherInstances();  // Check if other instances are running
    
    // Session management methods
    struct SessionInfo {
        QString key;           // Settings key (e.g., "Session_1234567890")
        QDateTime timestamp;
        int tabCount;
        QString name;          // Optional user-defined name (future feature)
    };
    QList<SessionInfo> listRecentSessions();
    void saveSessionWithTimestamp(const QString& sessionKey);
    void restoreSessionByKey(const QString& sessionKey);
    void pruneOldSessions(int maxSessions = 10);
    QString generateSessionKey(const QDateTime& dt);

  private slots:
    void saveFile()
    {
        filesBar->currentFiles()[0]->save();
        astroDatabase->updateList();
    }
    void handleSaveToDirectory(const QString& directory);
    void handleChartDroppedOnSlides(const QString& filePath);
    void handleChartDroppedOnInputWidget(const QString& filePath, int targetIndex);
    void currentTabChanged();
    void showSettingsEditor() { openSettingsEditor(); }
    void showAbout();
    void showRestoreSessionDialog();
    void saveSessionAs();
    void updateWindowTitle();
    void gotoUrl(QString url = "");
    void contextMenu(QPoint);

  protected:
    AppSettings defaultSettings() override; // 'Customizable' class implementations
    AppSettings currentSettings() override;
    void        applySettings(const AppSettings&) override;
    void        setupSettingsEditor(AppSettingsEditor*) override;
    void        saveSettings(const QString& iniFile = "settings.ini") override;

    void closeEvent(QCloseEvent*) override;
    void paintEvent(QPaintEvent* event) override;

  public:
    MainWindow(bool skipRestore = false, bool isServerInstance = true, bool autoRestore = false, QWidget* parent = nullptr);

    const std::string& APIKey() const { return _APIKey; }

    static MainWindow*  instance(bool skipRestore = false, bool isServerInstance = true, bool autoRestore = false, const QString& sessionFile = QString());
    static AstroWidget* theAstroWidget() { return instance()->astroWidget; }
};

#endif // MAINWINDOW_H
