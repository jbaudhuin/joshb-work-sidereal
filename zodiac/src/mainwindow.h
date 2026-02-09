#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabBar>
#include <QDockWidget>
#include <QTreeView>

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
    void filesUpdated(MembersList members); // AstroFileHandler implementations
    void viewSettingsUpdated(MembersList) override { } // only cares about file data
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  signals:
    void clicked();
    void chartDropped(const QString& filePath, int targetIndex);

  public:
    AstroFileInfo(QWidget* parent = nullptr);
    void setCurrentIndex(int i) { currentIndex = i; }

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void        applySettings(const AppSettings&);
    void        setupSettingsEditor(AppSettingsEditor*);
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

  signals:
    void helpRequested(QString tag);
    void appendFileRequested();
    void swapFilesRequested(int, int);
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
    friend class AstroFileHandler;
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
    void dragEnterEvent(QDragEnterEvent* event);
    void dragMoveEvent(QDragMoveEvent* event);
    void dropEvent(QDropEvent* event);
    void startDrag(Qt::DropActions supportedActions);

  public:
    FileTreeView(AstroDatabase* parent = nullptr);
};

class AstroDatabase : public QFrame {
    Q_OBJECT

  private:
    enum entryType { unknownType, fileType, dirType, dbType, sessionType };
    enum { PathRole = Qt::UserRole + 1, TypeRole };

    FileTreeView*          fileList;
    QStandardItemModel*    dirModel;
    QSortFilterProxyModel* searchProxy;
    QFileSystemWatcher*    fswatch;
    QLineEdit*             search;
    QString                _renamingOldName;
    QString                _renamingDir;

  protected:
    virtual void keyPressEvent(QKeyEvent*);
    virtual bool eventFilter(QObject*, QEvent*);

  public:
    bool validateDropTarget(const QPoint& pos, QString& targetDir);
    void performMove(const QString& targetDir);
    void performCopy(const QString& targetDir);

  private slots:
    void showContextMenu(QPoint);
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

  public:
    FilesBar(QWidget* parent = nullptr);

    void                 setAskToSave(bool b) { askToSave = b; }
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
        QDateTime timestamp;
        int tabCount;
        QString name;          // Optional user-defined name
        
        // Format session info for display
        QString displayName() const;
    };
    
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
    
    // Get list of recent sessions from sessions.ini
    static QList<SessionInfo> getRecentSessions(int maxCount = 10);
    
    // Initialize session file on app launch
    static QString initializeSession(bool isNewSession);
    
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
    bool _isServerInstance; // Track if this instance is running the single-instance server
    bool askToSave;

    FilesBar*      filesBar;
    AstroWidget*   astroWidget;
    AstroDatabase* astroDatabase;
    QDockWidget*   databaseDockWidget;
    QToolBar *     toolBar, *toolBar2, *helpToolBar;
    QMenu*         panelsMenu;

    std::string _APIKey;
    
    // Session management
    QDateTime _sessionStartTime;      // When this instance started
    QString   _currentSessionKey;     // Current session being worked on
    bool      _hadOverlappingInstances; // Track if other instances existed during lifetime

    void     addToolBarActions();
    QAction* createActionForPanel(QWidget* w /*, const QIcon &icon*/);
    
    void saveSession();
    void restoreSession();
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
    AppSettings defaultSettings(); // 'Customizable' class implementations
    AppSettings currentSettings();
    void        applySettings(const AppSettings&);
    void        setupSettingsEditor(AppSettingsEditor*);
    void        saveSettings(const QString& iniFile = "settings.ini") override;

    void closeEvent(QCloseEvent*);
    void paintEvent(QPaintEvent* event) override;

  public:
    MainWindow(bool skipRestore = false, bool isServerInstance = true, QWidget* parent = nullptr);

    const std::string& APIKey() const { return _APIKey; }

    static MainWindow*  instance(bool skipRestore = false, bool isServerInstance = true, const QString& sessionFile = QString());
    static AstroWidget* theAstroWidget() { return instance()->astroWidget; }
};

#endif // MAINWINDOW_H
