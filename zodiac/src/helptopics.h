#pragma once

// Context help: maps widgets (by objectName, else class name) to pages of the
// user help built from help/ (MkDocs) into <exe dir>/help. F1 opens the page
// for the focused widget's nearest mapped ancestor; doc capture can reuse the
// same table to tag screenshots with the page they belong to.

#include <QList>
#include <QString>
#include <QUrl>

class QObject;
class QWidget;

namespace HelpTopics
{

struct Topic
{
    QString key;    // objectName or class name; empty for the home page
    QString page;   // site-relative, as built: "events/index.html"
    QString anchor; // optional heading id, without '#'
};

// Online copy, used when the local help folder is missing.
extern const char* const OnlineBase;

const QList<Topic>& topics();
Topic               topicFor(const QWidget* w); // nearest mapped ancestor, else home
QUrl                urlFor(const Topic& t);     // local file if present, else online
void                open(const Topic& t);
void                openContextHelp();          // focused widget, else the one under the mouse

// Installs the app-wide F1 handler. A QShortcut on the main window would not
// fire while a modal dialog is up, so this is a key filter on qApp instead.
void installF1Handler(QObject* app);

} // namespace HelpTopics
