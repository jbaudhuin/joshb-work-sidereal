#include <QGridLayout>
#include <QDesktopServices>
#include <QTreeWidget>
#include <QHeaderView>
#include <QToolBar>
#include <QToolButton>
#include <QAction>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QFile>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QXmlStreamReader>
#include <QStringView>
#include <QActionGroup>
#include <QLocale>
#include <QDebug>
#include "geosearch.h"

namespace A {
const QString googMapURL("https://maps.googleapis.com/maps/api");
const char* _googAPIKey = getenv("ZOD_GOOG_API_KEY");
const QString googAPIKey(_googAPIKey? _googAPIKey : "");
}

GeoSuggestCompletion::GeoSuggestCompletion(GeoSearchBox *parent) :
    QObject(parent)
{
    editor = parent;
    source = Google;

    popup = new QTreeWidget;
    popup->setWindowFlags(Qt::Popup);
    popup->setFocusPolicy(Qt::NoFocus);
    popup->setFocusProxy(parent);
    popup->setMouseTracking(true);

    popup->setColumnCount(3);
    popup->setUniformRowHeights(true);
    popup->setRootIsDecorated(false);
    popup->setEditTriggers(QTreeWidget::NoEditTriggers);
    popup->setSelectionBehavior(QTreeWidget::SelectRows);
    popup->setFrameStyle(QFrame::Box | QFrame::Plain);
    popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popup->header()->hide();

    popup->installEventFilter(this);

    connect(popup, SIGNAL(itemClicked(QTreeWidgetItem*,int)),
            SLOT(doneCompletion()));

    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(500);
    connect(timer, SIGNAL(timeout()), SLOT(autoSuggest()));
    connect(editor, SIGNAL(textEdited(QString)), timer, SLOT(start()));

    connect(&networkManager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(handleNetworkData(QNetworkReply*)));
}

bool
GeoSuggestCompletion::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj != popup)
        return false;

    if (ev->type() == QEvent::MouseButtonPress) {
        popup->hide();
        editor->setFocus();
        return true;
    }

    if (ev->type() == QEvent::KeyPress)
    {
        bool consumed = false;
        int key = static_cast<QKeyEvent*>(ev)->key();
        switch (key)
        {
        case Qt::Key_Enter:
        case Qt::Key_Return:
            doneCompletion();
            consumed = true;
            [[clang::fallthrough]];

        case Qt::Key_Escape:
            editor->setFocus();
            popup->hide();
            consumed = true;
            [[clang::fallthrough]];

        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            break;

        default:
            editor->setFocus();
            editor->event(ev);
            popup->hide();
            break;
        }

        return consumed;
    }

    return false;
}

void
GeoSuggestCompletion::showCompletion(const QStringList &cities,
                                     const QStringList &descr,
                                     const QStringList &pos)
{
    int count = qMin(qMin(cities.count(), descr.count()), pos.count());

    if (!count) return;

    const QPalette &pal = editor->palette();
    QColor color = pal.color(QPalette::Disabled, QPalette::WindowText);

    popup->setUpdatesEnabled(false);
    popup->clear();

    for (int i = 0; i < count; ++i)
    {
        QTreeWidgetItem * item;
        item = new QTreeWidgetItem(popup);
        item->setText(0, cities[i]);
        item->setText(1, descr[i]);
        item->setText(2, pos[i]);

        item->setForeground(1, color);

        item->setToolTip(0, descr[i]);
        item->setToolTip(1, descr[i]);
    }

    popup->setCurrentItem(popup->topLevelItem(0));
    popup->resizeColumnToContents(0);
    popup->resizeColumnToContents(1);
    popup->adjustSize();
    popup->setUpdatesEnabled(true);

    int h = popup->sizeHintForRow(0) * qMin(7, cities.count()) + 3;
    popup->resize(editor->width(), h);

    popup->move(editor->mapToGlobal(QPoint(0, editor->height())));
    popup->setFocus();
    popup->show();
}

void GeoSuggestCompletion::doneCompletion()
{
    timer->stop();
    popup->hide();
    editor->setFocus();
    QTreeWidgetItem *item = popup->currentItem();

    if (item) {
        QVector3D vec;
        QString text = item->text(2);
        vec.setX(text.mid(0, text.indexOf(' ')).toFloat());
        vec.setY(text.mid(text.indexOf(' ') + 1).toFloat());

        editor->setCoordinate(vec, item->text(1));
        QMetaObject::invokeMethod(editor, "returnPressed");
    }
}

void GeoSuggestCompletion::autoSuggest()
{
    QString str = editor->text();
    if (str.isEmpty()) return;

    QString url;
    QString lang = QLocale::system().name().mid(0,2);

    switch (source)
    {
    case Google:
        url = QString(A::googMapURL + "/geocode/xml?"
                      "address=%1"
                      "&key=%2"
                      "&sensor=false"
                      "&language=%3")
                .arg(str)
                .arg(A::googAPIKey)
                .arg(lang);
        break;

    case Yandex:
        url = QString("https://geocode-maps.yandex.ru/1.x/?geocode=%1"
                      "&key=ANpUFEkBAAAAf7jmJwMAHGZHrcKNDsbEqEVjEUtCmufx"
                      "QMwAAAAAAAAAAAAvVrubVT4btztbduoIgTLAeFILaQ==").arg(str);
        break;
    }
    networkManager.get(QNetworkRequest(url));
}

void GeoSuggestCompletion::preventSuggest()
{
    timer->stop();
}

void GeoSuggestCompletion::setSource(Sources src)
{
    source = src;
}

void
GeoSuggestCompletion::handleNetworkData(QNetworkReply *networkReply)
{
    QUrl url = networkReply->url();
    if (!networkReply->error()) {
        QStringList shortNames;
        QStringList fullText;
        QStringList pos;      // "longitude latitude"

        QByteArray response(networkReply->readAll());
        QXmlStreamReader xml(response);

        while (!xml.atEnd())
        {
            xml.readNext();

            if (source == Google) {
                static const QString shortName("short_name");
                static const QString formattedAddress("formatted_address");
                static const QString lat("lat");
                static const QString lng("lng");
                if (xml.tokenType() == QXmlStreamReader::StartElement) {
                    if (xml.name() == shortName)
                        shortNames << xml.readElementText();

                    if (xml.name() == formattedAddress)
                        fullText << xml.readElementText();

                    if (xml.name() == lat)
                        pos << xml.readElementText();

                    if (xml.name() == lng)
                        pos.last().prepend(xml.readElementText() + " ");
                }
            }
            else if (source == Yandex)
            {
                static const QString name("name");
                static const QString text("text");
                static const QString spos("pos");
                if (xml.tokenType() == QXmlStreamReader::StartElement)
                {
                    if (xml.name() == name)
                        shortNames << xml.readElementText();

                    if (xml.name() == text)
                        fullText << xml.readElementText();

                    if (xml.name() == spos)
                        pos << xml.readElementText();
                }
            }

        }

        showCompletion(shortNames, fullText, pos);
    }

    networkReply->deleteLater();
}

GeoSuggestCompletion::~GeoSuggestCompletion()
{
    delete popup;
}


GeoSearchBox::GeoSearchBox(QWidget *parent) : QLineEdit(parent)
{
    completer = new GeoSuggestCompletion(this);
    setFocus();
    connect(this, SIGNAL(returnPressed()),this, SLOT(doSearch()));
}

void GeoSearchBox::doSearch()
{
    if (!isValid()) coord = QVector3D();
}



GeoSearchWidget::GeoSearchWidget(bool vbox /*=true*/,
                                 QWidget *parent /*=nullptr*/) :
    QWidget(parent)
{
    geoSearchBox = new GeoSearchBox;
    longitude    = new QDoubleSpinBox;
    latitude     = new QDoubleSpinBox;
    indicator    = new QLabel;
    googleAct    = new QAction(QIcon("fileeditor/google.png"),
                               tr("Search using Google Maps"), this);
    yandexAct    = new QAction(QIcon("fileeditor/yandex.png"),
                               tr("Search using Yandex.Maps"), this);
    editAct      = new QAction(QIcon("fileeditor/edit.png"),
                               tr("Input coordinates"), this);

    QToolBar* toolBar = nullptr;
    _tbtn = nullptr;
    if (vbox) toolBar = new QToolBar(this);
    else _tbtn = new QToolButton(this);

    QActionGroup* acts = new QActionGroup(this);
    QWidget* backSite = new QWidget;

    backSite     -> setObjectName("backSite");
    acts         -> addAction(googleAct);
    acts         -> addAction(yandexAct);
    acts         -> addAction(editAct);
    acts         -> setExclusive(true);
    if (toolBar) {
        toolBar      -> setIconSize(QSize(16,16));
        toolBar      -> addSeparator();
        toolBar      -> addActions(acts->actions());
    } else {
        _tbtn->setIconSize(QSize(16,16));
        _tbtn->addActions(acts->actions());
        _tbtn->setPopupMode(QToolButton::MenuButtonPopup);
    }
    geoSearchBox -> setPlaceholderText(tr("Input place here"));
    longitude    -> setSingleStep(1);
    latitude     -> setSingleStep(1);
    longitude    -> setDecimals(5);
    latitude     -> setDecimals(5);
    longitude    -> setRange(-180, 180);
    latitude     -> setRange(-180, 180);
    longitude    -> setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    latitude     -> setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QWidget* w1 = new QWidget;
    QHBoxLayout* l1 = new QHBoxLayout(w1);
    l1->setContentsMargins(QMargins(5,5,5,5));
    l1->addWidget(geoSearchBox);
    l1->addWidget(indicator);

    QWidget* w2 = new QWidget;
    QHBoxLayout* l2 = new QHBoxLayout(w2);
    l2->setContentsMargins(QMargins(5,5,5,5));
    l2->addWidget(new QLabel(tr("lon:")));
    l2->addWidget(longitude);
    l2->addWidget(new QLabel(tr("lat:")));
    l2->addWidget(latitude);

    modes = new QStackedLayout(backSite);
    modes->addWidget(w1);
    modes->addWidget(w2);

    QBoxLayout* layout = nullptr;
    if (vbox) layout = new QVBoxLayout(this);
    else layout = new QHBoxLayout(this);
    layout->setContentsMargins(QMargins(0,0,0,0));
    layout->setSpacing(0);
    layout->addWidget(backSite);
    if (toolBar) layout->addWidget(toolBar);
    else {
        layout->addWidget(_tbtn);
        layout->setStretch(0,1);
    }

    QFile cssfile ( "fileeditor/style.css" );
    cssfile.open  ( QIODevice::ReadOnly | QIODevice::Text );
    setStyleSheet  ( cssfile.readAll() );

    foreach (QAction* act, acts->actions())
        act->setCheckable(true);

    turnGoogleSearch();

    connect(googleAct,    SIGNAL(triggered()),          this, SLOT(turnGoogleSearch()));
    connect(yandexAct,    SIGNAL(triggered()),          this, SLOT(turnYandexSearch()));
    connect(editAct,      SIGNAL(triggered()),          this, SLOT(turnGeoInput()));
    connect(geoSearchBox, SIGNAL(returnPressed()),      this, SLOT(proofCoordinates()));
    connect(latitude,     SIGNAL(valueChanged(double)), this, SIGNAL(locationChanged()));
    connect(longitude,    SIGNAL(valueChanged(double)), this, SIGNAL(locationChanged()));
    //connect(geoSearchBox, SIGNAL(editingFinished()),    this, SIGNAL(locationChanged()));
}

void GeoSearchWidget::turnGoogleSearch()
{
    modes->setCurrentIndex(0);
    googleAct->setChecked(true);
    geoSearchBox->setSource(GeoSuggestCompletion::Google);
    if (_tbtn) _tbtn->setIcon(googleAct->icon());
}

void GeoSearchWidget::turnYandexSearch()
{
    modes->setCurrentIndex(0);
    yandexAct->setChecked(true);
    geoSearchBox->setSource(GeoSuggestCompletion::Yandex);
    if (_tbtn) _tbtn->setIcon(yandexAct->icon());
}

void GeoSearchWidget::turnGeoInput()
{
    modes->setCurrentIndex(1);
    editAct->setChecked(true);
    if (_tbtn) _tbtn->setIcon(editAct->icon());
}

void GeoSearchWidget::proofCoordinates()
{
    //if (geoSearchBox->isValid())
    // {
    indicator->setPixmap(QPixmap(":res/ok.png"));
    indicator->setToolTip(tr("Current position is:\n%1 %2").arg(geoSearchBox->coordinate().x())
                          .arg(geoSearchBox->coordinate().y()));

    longitude->blockSignals(true);           // prevent calling 'locationChanged()' twice

    longitude->setValue(geoSearchBox->coordinate().x());
    latitude->setValue(geoSearchBox->coordinate().y());

    longitude->blockSignals(false);
    /*}
  else
   {
    indicator->setPixmap(QPixmap(":res/error.png"));
    indicator->setToolTip(tr("Invalid place tag"));
   }*/
}

QVector3D GeoSearchWidget::spinBoxesCoord() const
{
    return QVector3D(longitude->value(), latitude->value(), 0);
}

QVector3D GeoSearchWidget::location() const
{
    if (modes->currentIndex() == 0)
        return geoSearchBox->coordinate();

    return spinBoxesCoord();
}

QString GeoSearchWidget::locationName() const
{
    if (modes->currentIndex() == 0 ||
            spinBoxesCoord() == geoSearchBox->coordinate())
        return geoSearchBox->text();

    return "";
}

void GeoSearchWidget::setLocation(const QVector3D& coord)
{
    geoSearchBox->setCoordinate(coord);

    longitude->blockSignals(true);           // prevent calling 'locationChanged()' twice
    longitude->setValue(coord.x());
    latitude->setValue(coord.y());
    longitude->blockSignals(false);

    if (!coord.isNull())
        turnGeoInput();
}

void GeoSearchWidget::setLocation(const QVector3D& coord, const QString& name)
{
    setLocation(coord);
    geoSearchBox->setCoordinate(coord, name);
    //setLocationName(name);

    if (!name.isEmpty())
        turnGoogleSearch();
}

void GeoSearchWidget::setLocationName(const QString& name)
{
    geoSearchBox->setText(name);

    if (!name.isEmpty())
        turnGoogleSearch();
}
