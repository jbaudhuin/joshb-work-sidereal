#include "mainwindow.h"
#include "thememanager.h"
#include "crashhandler.h"
#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QTextCodec>
#include <QThreadPool>
#include <QTranslator>
#include <QSettings>
#include <memory>

#include <QSslSocket>
#include <qlogging.h>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDataStream>

void
loadTranslations(QApplication* a, QString lang)
{
    QDir dir("i18n");
    foreach (QString s, dir.entryList(QStringList("*" + lang + ".qm"))) {
        QTranslator* t = new QTranslator;
        qDebug() << "load translation file" << s << ":"
                 << (t->load(dir.absolutePath() + '/' + s) ? "success"
                                                           : "failed");
        a->installTranslator(t);
    }
}

#if (QT_VERSION < QT_VERSION_CHECK(5, 2, 0))
void
emptyOutput(QtMsgType type, const char* msg)
#else
void
emptyOutput(QtMsgType                 type,
            const QMessageLogContext& context,
            const QString&            msg)
#endif
{
    Q_UNUSED(type);
    Q_UNUSED(context);
    Q_UNUSED(msg);
}

namespace
{

QFile* g_logFile = nullptr;

void
zodOutputHandler(QtMsgType                 type,
                 const QMessageLogContext& cxt,
                 const QString&            msg)
{
    Q_UNUSED(cxt);
    
    // Write to console
    switch (type) {
    case QtWarningMsg:
    case QtCriticalMsg:
    case QtFatalMsg:    fprintf(stderr, "%s\n", msg.toLatin1().constData()); break;
    case QtInfoMsg:     printf("%s\n", msg.toLatin1().constData()); break;
    case QtDebugMsg:
        //printf("%s %s:%u\n", msg.toLatin1().constData(), cxt.file, cxt.line);
        printf("%s\n", msg.toLatin1().constData());
        break;
    }
    
    // Also write to log file if available
    if (g_logFile && g_logFile->isOpen()) {
        QString logMsg;
        switch (type) {
        case QtDebugMsg:    logMsg = "[DEBUG] "; break;
        case QtInfoMsg:     logMsg = "[INFO] "; break;
        case QtWarningMsg:  logMsg = "[WARN] "; break;
        case QtCriticalMsg: logMsg = "[CRIT] "; break;
        case QtFatalMsg:    logMsg = "[FATAL] "; break;
        }
        logMsg += QString("%1\t%2:%3\n").arg(msg).arg(cxt.file).arg(cxt.line);
        g_logFile->write(logMsg.toUtf8());
        g_logFile->flush();
    }
}

void
my_invalid_parameter(const wchar_t* expression,
                     const wchar_t* function,
                     const wchar_t* file,
                     unsigned int   line,
                     uintptr_t      pReserved)
{
}

// Single instance manager class
class SingleInstanceManager : public QObject
{
    Q_OBJECT
public:
    SingleInstanceManager(const QString& key, QObject* parent = nullptr)
        : QObject(parent)
        , m_key(key)
        , m_server(nullptr)
    {
    }

    ~SingleInstanceManager()
    {
        if (m_server) {
            m_server->close();
            delete m_server;
        }
    }

    bool tryAcquire()
    {
        // Try to connect to existing instance
        QLocalSocket socket;
        socket.connectToServer(m_key);
        
        if (socket.waitForConnected(500)) {
            qDebug() << "Found existing instance, sending raise signal";
            // Send raise command
            QByteArray data;
            QDataStream stream(&data, QIODevice::WriteOnly);
            stream << QString("RAISE");
            socket.write(data);
            socket.flush();
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
            return false; // Another instance is running
        }

        // No existing instance, create server
        m_server = new QLocalServer(this);
        
        // Remove any stale server instances
        QLocalServer::removeServer(m_key);
        
        if (!m_server->listen(m_key)) {
            qDebug() << "Failed to create local server:" << m_server->errorString();
            return true; // Fail-safe: allow app to run anyway
        }

        connect(m_server, &QLocalServer::newConnection, this, &SingleInstanceManager::onNewConnection);
        qDebug() << "Created single instance server";
        return true;
    }

signals:
    void raiseRequested();

private slots:
    void onNewConnection()
    {
        QLocalSocket* socket = m_server->nextPendingConnection();
        if (!socket) return;

        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            QByteArray data = socket->readAll();
            QDataStream stream(&data, QIODevice::ReadOnly);
            QString command;
            stream >> command;

            if (command == "RAISE") {
                qDebug() << "Received raise request from another instance";
                emit raiseRequested();
            }

            socket->disconnectFromServer();
        });

        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
    }

private:
    QString m_key;
    QLocalServer* m_server;
};

} // namespace

int
main(int argc, char* argv[])
{
    // Save original arguments before QApplication modifies them
    QStringList originalArgs;
    for (int i = 0; i < argc; ++i) {
        originalArgs.append(QString::fromLocal8Bit(argv[i]));
    }
    
    // Create QApplication FIRST so we can use qDebug
    QApplication a(argc, argv);
    
    // Immediately create log file before anything else (moved from below)
#if defined(_ZOD_DEBUG)
    QString logTimestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QString logFileName = QString("zodiac-%1.log").arg(logTimestamp);
    g_logFile = new QFile(logFileName);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        // Don't use qDebug yet, write directly
        QTextStream stream(g_logFile);
        stream << "[DEBUG] === NEW INSTANCE START ===" << Qt::endl;
        stream << "[DEBUG] PID: " << QCoreApplication::applicationPid() << Qt::endl;
        stream << "[DEBUG] argc: " << argc << Qt::endl;
        for (int i = 0; i < argc; ++i) {
            stream << "[DEBUG] argv[" << i << "]: " << argv[i] << Qt::endl;
        }
        stream.flush();
    }
    qInstallMessageHandler(zodOutputHandler);
    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);
#endif
    a.setApplicationName("Zodiac");
    a.setApplicationVersion("v0.9.9 (build 2026-08-14)");

    // Install post-mortem crash capture before any real work. On Windows this
    // writes a minidump + symbolized stack trace to %LOCALAPPDATA%\Zodiac\crashes
    // when the process dies unexpectedly; on next launch the user is offered the
    // report to send on. No-op on other platforms.
    CrashHandler::install(a.applicationVersion());

    // Self-test: `zodiac --crashtest` deliberately faults so the crash-capture
    // pipeline (minidump + symbolized trace) can be validated on demand. Does
    // nothing unless the flag is explicitly passed.
    if (originalArgs.contains("--crashtest", Qt::CaseInsensitive)) {
        volatile int* p = nullptr;
        *p = 42;
    }

    // Debug: Show current working directory and application path
    auto cwd        = QDir::currentPath();
    qDebug() << "current path:" << cwd;

    auto appDirPath = a.applicationDirPath();
    qDebug() << "appDirPath:" << appDirPath;

#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_WNDW);
#endif

    // auto foo = _get_invalid_parameter_handler();
    // auto fum = _set_invalid_parameter_handler((_invalid_parameter_handler)
    // my_invalid_parameter); auto fie =
    // _set_thread_local_invalid_parameter_handler((_invalid_parameter_handler)
    // my_invalid_parameter);

#if (QT_VERSION < QT_VERSION_CHECK(5, 2, 0))
    QTextCodec* codec = QTextCodec::codecForName("UTF-8");
    QTextCodec::setCodecForCStrings(codec);
    QTextCodec::setCodecForTr(codec);
    qInstallMsgHandler(emptyOutput);
#elif defined(NDEBUG)
    //qInstallMessageHandler(emptyOutput);
#endif

    qDebug() << "SSL version use for build: "
             << QSslSocket::sslLibraryBuildVersionString();
    qDebug() << "SSL version use for run-time: "
             << QSslSocket::sslLibraryVersionString();
    qDebug() << QCoreApplication::libraryPaths();

    // QDir::setCurrent(a.applicationDirPath());
    QString lang = "";
    if (!a.arguments().contains("nolocale")) {
        if (QLocale::system().name().contains("RU", Qt::CaseInsensitive))
            lang = "ru";
        else
            lang = "en";

        loadTranslations(&a, lang);
    }

    qDebug() << "Ideal thread count" << QThread::idealThreadCount();

    QFontDatabase::addApplicationFont("fonts/Almagest.ttf");
    A::load(lang);
    A::ChartPreset::loadAll();

    // Check for command-line flags to skip session restore and allow multiple instances
    qDebug() << "Parsing command-line arguments...";
    bool skipRestore = false;
    bool autoRestore = false; // Restore most-recent session without prompting
    bool allowMultipleInstances = false;
    QString sessionFile; // Optional session file to restore
    QStringList args = originalArgs; // Use saved args, not QApplication's modified version
    qDebug() << "originalArgs:" << originalArgs;
    for (int i = 1; i < args.size(); ++i) {
        QString lower = args[i].toLower();
        if (lower == "--new" || lower == "-new" || lower == "/new" ||
            lower == "/n" || lower == "--norestore") {
            qDebug() << "Found --new flag";
            skipRestore = true;
            allowMultipleInstances = true;
        } else if (lower == "--auto-restore" || lower == "-auto-restore" || lower == "/auto-restore") {
            qDebug() << "Found --auto-restore flag";
            autoRestore = true;
        } else if (lower == "--load-session" || lower == "-load-session" || lower == "/load-session" ||
                   lower == "--restore" || lower == "-restore" || lower == "/restore") {
            // Next argument should be the session file
            if (i + 1 < args.size()) {
                sessionFile = args[i + 1];
                qDebug() << "Found --load-session flag with file:" << sessionFile;
                i++; // Skip next argument
                allowMultipleInstances = true; // Allow loading specific session in new instance
            }
        }
    }
    
    qDebug() << "skipRestore:" << skipRestore;
    qDebug() << "autoRestore:" << autoRestore;
    qDebug() << "allowMultipleInstances:" << allowMultipleInstances;
    qDebug() << "sessionFile:" << sessionFile;
    
    // Single instance management (unless --new flag is specified)
    SingleInstanceManager* singleInstance = nullptr;
    bool isServerInstance = false; // Track if this instance owns the single-instance server
    if (!allowMultipleInstances) {
        qDebug() << "Single instance mode - trying to acquire server...";
        singleInstance = new SingleInstanceManager("ZodiacSiderealInstance");
        if (!singleInstance->tryAcquire()) {
            // Another instance is running and we've told it to raise itself
            qDebug() << "Another instance is already running. Exiting.";
            delete singleInstance;
            return 0;
        }
        qDebug() << "Successfully acquired single instance server";
        isServerInstance = true; // We successfully created the server
    } else {
        qDebug() << "Multiple instances allowed - skipping single instance check";
    }
    
    // Initialize and apply the theme to the application BEFORE constructing the
    // window, so startup dialogs shown during construction (the session prompt
    // and the "Choose…" picker) are themed.
    ThemeManager& themeManager = ThemeManager::instance();
    QSettings themeSettings(QSettings::IniFormat, QSettings::UserScope, "Astroprocessor", "Zodiac");
    QString savedTheme = themeSettings.value("theme", "dark").toString();
    themeManager.setTheme(savedTheme, false);
    themeManager.applyToApplication(&a);

    qDebug() << "Creating MainWindow instance...";
    std::unique_ptr<MainWindow> mw(MainWindow::instance(skipRestore, isServerInstance, autoRestore, sessionFile));
    MainWindow&                 w = *mw;
    
    // Connect raise signal if single instance mode
    if (singleInstance) {
        QObject::connect(singleInstance, &SingleInstanceManager::raiseRequested, [&w]() {
            // Restore from minimized state if needed
            if (w.isMinimized()) {
                w.showNormal();
            }
            
            // Multiple strategies to ensure window comes to front on Windows
            w.show();
            w.raise();
            w.activateWindow();
            
#ifdef Q_OS_WIN
            // Windows-specific: Set window to be on top temporarily, then restore
            w.setWindowState((w.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            w.setWindowFlags(w.windowFlags() | Qt::WindowStaysOnTopHint);
            w.show();
            w.setWindowFlags(w.windowFlags() & ~Qt::WindowStaysOnTopHint);
            w.show();
#endif
        });
    }

    // Propagate theme to main window (theme already applied to the app above)
    themeManager.propagateThemeProperty(&w);

    w.show();

    // If a previous run left a crash report, offer to bundle and reveal it now
    // that we're in a healthy process with the UI up.
    CrashHandler::checkForPendingReports();

    int result = a.exec();
    
    // Cleanup
    if (singleInstance) {
        delete singleInstance;
    }
    
    // Close log file
    if (g_logFile) {
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
    
    return result;
}

// Include MOC file for SingleInstanceManager Q_OBJECT macro
#include "main.moc"
