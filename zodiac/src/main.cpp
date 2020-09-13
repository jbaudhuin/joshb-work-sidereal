#include <QApplication>
#include <QFile>
#include <QDir>
#include <QTextCodec>
#include <QTranslator>
#include <QFontDatabase>
#include <QDebug>
#include <memory>
#include "mainwindow.h"

#include <QSslSocket>

void loadTranslations(QApplication* a, QString lang)
{
    QDir dir("i18n");
    foreach (QString s, dir.entryList(QStringList("*" + lang + ".qm")))
    {
        QTranslator* t = new QTranslator;
        qDebug() << "load translation file" << s << ":"
                 << (t->load(dir.absolutePath() + '/' + s) ? "success" : "failed");
        a->installTranslator(t);
    }
}

#if (QT_VERSION < QT_VERSION_CHECK(5, 2, 0))
void emptyOutput ( QtMsgType type, const char *msg )
#else
void emptyOutput(QtMsgType type,
                 const QMessageLogContext &context,
                 const QString &msg)
#endif
{
    Q_UNUSED(type);
    Q_UNUSED(context);
    Q_UNUSED(msg);
}

namespace {
void
zodOutputHandler(QtMsgType type,
                 const QMessageLogContext& cxt,
                 const QString& msg)
{
    Q_UNUSED(type);
    Q_UNUSED(cxt);
    if (!msg.startsWith("Invalid paameter")) {
    fprintf(stderr, "%s", msg.toLatin1().constData());
    }
}

void my_invalid_parameter(const wchar_t * expression,
   const wchar_t * function,
   const wchar_t * file,
   unsigned int line,
   uintptr_t pReserved)
{

}
}

int main(int argc, char *argv[])
{
    QCoreApplication::addLibraryPath("C:\\OpenSSL-Win64\\bin");

    QApplication a(argc, argv);
    a.setApplicationName("Zodiac");
    a.setApplicationVersion("v0.8.1 (build 2019-02-08)");

#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_WNDW);
#endif

    //auto foo = _get_invalid_parameter_handler();
    //auto fum = _set_invalid_parameter_handler((_invalid_parameter_handler) my_invalid_parameter);
    //auto fie = _set_thread_local_invalid_parameter_handler((_invalid_parameter_handler) my_invalid_parameter);

#if (QT_VERSION < QT_VERSION_CHECK(5, 2, 0))
    QTextCodec* codec = QTextCodec::codecForName ( "UTF-8" );
    QTextCodec::setCodecForCStrings ( codec );
    QTextCodec::setCodecForTr ( codec );
    qInstallMsgHandler (emptyOutput);
#elif defined(_ZOD_DEBUG)
    //qInstallMessageHandler(zodOutputHandler);
#elif defined(NDEBUG)
    qInstallMessageHandler(emptyOutput);
#endif

    qDebug() << "SSL version use for build: " << QSslSocket::sslLibraryBuildVersionString();
    qDebug() << "SSL version use for run-time: " << QSslSocket::sslLibraryVersionString();
    qDebug() << QCoreApplication::libraryPaths();

    //QDir::setCurrent(a.applicationDirPath());
    QString lang = "";
    if (!a.arguments().contains("nolocale")) {
        if (QLocale::system().name().contains("RU", Qt::CaseInsensitive))
            lang = "ru";
        else
            lang = "en";

        loadTranslations(&a, lang);
    }

    QFontDatabase::addApplicationFont("fonts/Almagest.ttf");
    A::load(lang);

    std::unique_ptr<MainWindow> mw(MainWindow::instance());
    MainWindow& w = *mw;

    QFile cssfile ( "style/style.css" );
    cssfile.open  ( QIODevice::ReadOnly | QIODevice::Text );
    w.setStyleSheet  ( cssfile.readAll() );

    w.show();
    return a.exec();
}
