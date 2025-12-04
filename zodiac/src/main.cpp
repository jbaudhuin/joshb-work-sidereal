#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QTextCodec>
#include <QThreadPool>
#include <QTranslator>
#include <memory>

#include <QSslSocket>
#include <qlogging.h>

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

void
zodOutputHandler(QtMsgType                 type,
                 const QMessageLogContext& cxt,
                 const QString&            msg)
{
    Q_UNUSED(cxt);
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
}

void
my_invalid_parameter(const wchar_t* expression,
                     const wchar_t* function,
                     const wchar_t* file,
                     unsigned int   line,
                     uintptr_t      pReserved)
{
}

} // namespace

int
main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("Zodiac");
    a.setApplicationVersion("v0.9.1 (build 2025-12-03)");

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
#elif defined(_ZOD_DEBUG)
    qInstallMessageHandler(zodOutputHandler);
    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);
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

    // Check for command-line flags to skip session restore
    bool skipRestore = false;
    QStringList args = a.arguments();
    for (const QString& arg : args) {
        QString lower = arg.toLower();
        if (lower == "--new" || lower == "-new" || lower == "/new" || 
            lower == "/n" || lower == "--norestore") {
            skipRestore = true;
            break;
        }
    }
    
    std::unique_ptr<MainWindow> mw(MainWindow::instance(skipRestore));
    MainWindow&                 w = *mw;

    QFile cssfile("style/style.css");
    if (cssfile.open(QIODeviceBase::ReadOnly | QIODeviceBase::Text)) {
        w.setStyleSheet(cssfile.readAll());
    } else {
        // Show message box and exit with failure
        QMessageBox::critical(nullptr, "Error", "Could not open style file: " + cssfile.fileName());
        qDebug() << "Could not open style file:" << cssfile.fileName();
        return 1;
    }

    w.show();
    return a.exec();
}
