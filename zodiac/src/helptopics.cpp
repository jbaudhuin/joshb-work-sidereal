#include "helptopics.h"

#include <QApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QWidget>

namespace HelpTopics
{

const char* const OnlineBase = "https://jbaudhuin.github.io/joshb-work-sidereal/";

const QList<Topic>&
topics()
{
    // Keys are objectNames, or class names for one-of-a-kind widgets. Docks
    // made by AstroWidget::addDockWidget() are named after their class
    // (the Events dock is "Transits"). Pages follow help/mkdocs.yml's nav.
    static const QList<Topic> t = {
        { "Transits",             "events/index.html", "" },
        { "eventsToolbar",        "events/toolbar.html", "" },
        { "Speculum",             "parans/speculum.html", "" },
        { "Details",              "views/details.html", "" },
        { "Harmonics",            "chart-wheel/harmonics-tab.html", "" },
        { "dbDockWidget",         "organizing/database.html", "" },
        { "restoreSessionDialog", "organizing/sessions.html", "" },
        { "chartView",            "views/chart-wheel.html", "" },
        { "Chart",                "views/chart-wheel.html", "" },
        { "declView",             "chart-wheel/declination-strip.html", "" },
        { "planetsView",          "views/planets.html", "" },
        { "Planets",              "views/planets.html", "" },
        { "PlanetTable",          "views/planets.html", "" },
        { "Plain",                "views/tables.html", "" },
        { "rangeNavigator", "chart-wheel/animation.html", "" },
        { "zodiacSelector",       "chart-wheel/zodiacs.html", "" },
        { "hsystemSelector",      "chart-wheel/houses-aspects.html", "" },
        { "aspectsSelector",      "chart-wheel/houses-aspects.html", "" },
        { "aspectModeSelector",   "chart-wheel/houses-aspects.html", "" },
        { "namedAspectControls",  "chart-wheel/houses-aspects.html", "" },
        { "harmonicSelector",     "chart-wheel/harmonics.html", "" },
        { "dynAspectControls",    "chart-wheel/harmonics.html", "" },
        { "appSettingsEditor",    "reference/settings.html", "" },
        { "AstroFileEditor",      "getting-started/charts.html", "" },
        { "about",                "reference/credits.html", "" },
    };
    return t;
}

namespace
{

const QHash<QString, Topic>&
index()
{
    static const QHash<QString, Topic> h = [] {
        QHash<QString, Topic> m;
        for (const Topic& t : topics()) m.insert(t.key, t);
        return m;
    }();
    return h;
}

class F1Filter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override
    {
        if (ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->matches(QKeySequence::HelpContents)) {
                if (!ke->isAutoRepeat()) openContextHelp();
                return true;
            }
        }
        return QObject::eventFilter(obj, ev);
    }
};

} // namespace

Topic
topicFor(const QWidget* w)
{
    const auto& idx = index();
    for (; w; w = w->parentWidget()) {
        auto it = idx.constFind(w->objectName());
        if (it == idx.constEnd()) it = idx.constFind(w->metaObject()->className());
        if (it != idx.constEnd()) return *it;
    }
    return { QString(), "index.html", QString() };
}

QUrl
urlFor(const Topic& t)
{
    const QString local = QCoreApplication::applicationDirPath() + "/help/" + t.page;
    QUrl          url   = QFileInfo::exists(local) ? QUrl::fromLocalFile(local)
                                                   : QUrl(QString(OnlineBase) + t.page);
    if (!t.anchor.isEmpty()) url.setFragment(t.anchor);
    return url;
}

void
open(const Topic& t)
{
    QDesktopServices::openUrl(urlFor(t));
}

void
openContextHelp()
{
    Topic t = topicFor(QApplication::focusWidget());
    if (t.key.isEmpty()) {
        // Clicking the chart wheel or a table doesn't always move focus, so
        // fall back to what the mouse is over -- in the active window only.
        QWidget* hover = QApplication::widgetAt(QCursor::pos());
        if (hover && hover->window() == QApplication::activeWindow())
            t = topicFor(hover);
    }
    open(t);
}

void
installF1Handler(QObject* app)
{
    static F1Filter* filter = nullptr;
    if (filter) return;
    filter = new F1Filter(app);
    app->installEventFilter(filter);
}

} // namespace HelpTopics
