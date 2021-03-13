#ifndef GeoSearchBox_H
#define GeoSearchBox_H

#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QStackedLayout>
#include <QVector3D>
#include <QTimeZone>

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
extern const QString googAPIKey;
extern const QString googMapURL;
}

class GeoSuggestCompletion : public QObject
{
    Q_OBJECT

    public:
        enum Sources { Google, Yandex };

        GeoSuggestCompletion(GeoSearchBox *parent = nullptr);
        ~GeoSuggestCompletion();
        bool eventFilter(QObject *obj, QEvent *ev);
        void showCompletion(const QStringList &cities, const QStringList &descr, const QStringList &pos);
        void setSource(Sources src);

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

    signals:
        void coordinateUpdated() const;

    protected slots:
        void doSearch();

    public:
        GeoSearchBox(QWidget *parent = nullptr);

        void setSource(GeoSuggestCompletion::Sources src)
        { completer->setSource(src); }

        void setCoordinate(QVector3D coord, QString tag)
        { this->coord = coord; associatedText = tag; setText(tag); }

        void setCoordinate(QVector3D coord) { this->coord = coord; }
        QVector3D coordinate()              { return coord; }

        bool isValid()
        { return associatedText == text() && !text().isEmpty(); }

};


class GeoSearchWidget : public QWidget
{
    Q_OBJECT

    private:
       QAction *googleAct, *yandexAct, *editAct;
       QStackedLayout* modes;
       QToolButton* _tbtn;
       GeoSearchBox* geoSearchBox;
       QDoubleSpinBox* latitude;
       QDoubleSpinBox* longitude;
       QLabel* indicator;

       QVector3D spinBoxesCoord() const;

    private slots:
       void turnGoogleSearch();
       void turnYandexSearch();
       void turnGeoInput();
       void proofCoordinates();

    signals:
       void locationChanged() const;

    public:
       GeoSearchWidget(bool vbox = true, QWidget* parent = nullptr);
       QVector3D location() const;
       QString locationName() const;

       void setLocation(const QVector3D& coord);
       void setLocation(const QVector3D& coord, const QString& name);
       void setLocationName(const QString& name);
};

#endif // GeoSearchBox_H
