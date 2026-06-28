#ifndef GeoSearchBox_H
#define GeoSearchBox_H

#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QStackedLayout>
#include <QVector3D>
#include <QTimeZone>
#include "mainwindow.h"

class QLineEdit;
class QNetworkReply;
class QTimer;
class QToolButton;
class QTreeWidget;
class QDoubleSpinBox;
class GeoSearchBox;
class QLabel;
class QDateTime;

namespace A {
extern const QString googMapURL;
}

class GeoSuggestCompletion : public QObject
{
    Q_OBJECT

    public:
        enum Sources { Google, Local };

        GeoSuggestCompletion(GeoSearchBox *parent = nullptr);
        ~GeoSuggestCompletion();
        bool eventFilter(QObject *obj, QEvent *ev) override;
        void showCompletion(const QStringList &cities,
                            const QStringList &descr,
                            const QStringList &pos,
                            const QStringList &timezoneIds = {});
        void setSource(Sources src);
        Sources currentSource() const { return source; }

    public slots:
        void doneCompletion();
        void preventSuggest();
        void autoSuggest();
        void handleNetworkData(QNetworkReply *networkReply);

    private:
        GeoSearchBox *editor;
        QTreeWidget *popup;
        QTimer *timer;
        QNetworkAccessManager networkManager;
        Sources source;
};


class GeoSearchBox: public QLineEdit
{
    Q_OBJECT    

    private:
        GeoSuggestCompletion *completer;
        QVector3D coord;
        QString associatedText;
        QString timezoneId;

    signals:
        void coordinateUpdated();

    protected slots:
        void doSearch();

    public:
        GeoSearchBox(QWidget *parent = nullptr);

        void setSource(GeoSuggestCompletion::Sources src)
        { completer->setSource(src); }
        GeoSuggestCompletion::Sources source() const
        { return completer->currentSource(); }

        void setCoordinate(QVector3D coord, QString tag, const QString& tzId = QString())
        { this->coord = coord; associatedText = tag; timezoneId = tzId; setText(tag); }

        void setCoordinate(QVector3D coord) { this->coord = coord; associatedText.clear(); timezoneId.clear(); }
        QVector3D coordinate() const        { return coord; }
        QString selectedTimezoneId() const  { return timezoneId; }

        bool isValid()
        { return associatedText == text() && !text().isEmpty(); }

};


class GeoSearchWidget : public QWidget
{
    Q_OBJECT

    private:
         QAction *googleAct, *localAct, *editAct;
       QStackedLayout* modes;
       QToolButton* _tbtn;
       GeoSearchBox* geoSearchBox;
       QDoubleSpinBox* latitude;
       QDoubleSpinBox* longitude;
       QLabel* indicator;

       QVector3D spinBoxesCoord() const;
    void showSearchMode();

    private slots:
       void turnGoogleSearch();
         void turnLocalSearch();
       void turnGeoInput();
       void proofCoordinates();

    signals:
       void locationChanged() const;

    public:
       GeoSearchWidget(bool vbox = true, QWidget* parent = nullptr);
       QVector3D location() const;
       QString locationName() const;
    QString selectedTimezoneId() const;

       void setLocation(const QVector3D& coord);
       void setLocation(const QVector3D& coord, const QString& name);
       void setLocationName(const QString& name);
};

#endif // GeoSearchBox_H
