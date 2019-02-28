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
void emptyOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
#endif
 {
    Q_UNUSED(type);
    Q_UNUSED(context);
    Q_UNUSED(msg);
 }

int main(int argc, char *argv[])
{
    QCoreApplication::addLibraryPath("C:\\OpenSSL-Win64\\bin");

    QApplication a(argc, argv);
    a.setApplicationName("Zodiac");
    a.setApplicationVersion("v0.8.1 (build 2019-02-08)");
    qDebug() << "SSL version use for build: " << QSslSocket::sslLibraryBuildVersionString();
    qDebug() << "SSL version use for run-time: " << QSslSocket::sslLibraryVersionString();
    qDebug() << QCoreApplication::libraryPaths();

#if (QT_VERSION < QT_VERSION_CHECK(5, 2, 0))
    QTextCodec* codec = QTextCodec::codecForName ( "UTF-8" );
    QTextCodec::setCodecForCStrings ( codec );
    QTextCodec::setCodecForTr ( codec );
    qInstallMsgHandler (emptyOutput);
#else
    //qInstallMessageHandler(emptyOutput);
#endif

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
