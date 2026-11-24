#include "widgetpicker.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QWidget>

namespace WidgetPicker
{

const char* const OverlayProperty = "docCaptureOverlay";

namespace
{

// Qt's internal children ("qt_scrollarea_viewport" etc.) carry names that say
// nothing about the app, so they count as unnamed.
bool
hasUsefulName(const QWidget* w)
{
    const QString n = w->objectName();
    return !n.isEmpty() && !n.startsWith(QLatin1String("qt_"));
}

QString
slug(QString s, int maxLen = 40)
{
    s = s.toLower();
    s.remove(QLatin1Char('&')); // mnemonic markers
    s.replace(QRegularExpression("[^a-z0-9]+"), "-");
    s = s.left(maxLen);
    while (s.startsWith('-')) s.remove(0, 1);
    while (s.endsWith('-')) s.chop(1);
    return s;
}

QString
fileSafe(QString s)
{
    s.replace(QRegularExpression("[^A-Za-z0-9_.-]+"), "-");
    return s;
}

// The page of a QTabWidget sits under its private QStackedWidget.
const QTabWidget*
owningTabWidget(const QWidget* w, int* index)
{
    auto* stack = qobject_cast<const QStackedWidget*>(w->parentWidget());
    if (!stack) return nullptr;
    auto* tabs = qobject_cast<const QTabWidget*>(stack->parentWidget());
    if (!tabs) return nullptr;
    *index = tabs->indexOf(const_cast<QWidget*>(w));
    return *index >= 0 ? tabs : nullptr;
}

QString
titleOf(const QWidget* w)
{
    int tabIndex = -1;
    if (auto* tabs = owningTabWidget(w, &tabIndex))
        return tabs->tabText(tabIndex);
    if (auto* tb = qobject_cast<const QToolButton*>(w)) {
        if (QAction* a = tb->defaultAction()) {
            if (!a->text().isEmpty()) return a->text();
            if (!a->toolTip().isEmpty()) return a->toolTip();
        }
    }
    if (auto* b = qobject_cast<const QAbstractButton*>(w)) {
        if (!b->text().isEmpty()) return b->text();
        return b->toolTip();
    }
    if (qobject_cast<const QDockWidget*>(w) || w->isWindow())
        return w->windowTitle();
    return QString();
}

} // namespace

bool
isOverlay(const QWidget* w)
{
    for (; w; w = w->parentWidget())
        if (w->property(OverlayProperty).toBool()) return true;
    return false;
}

QWidget*
widgetAtGlobal(const QPoint& global)
{
    QWidget* w = QApplication::widgetAt(global);
    return isOverlay(w) ? nullptr : w;
}

QList<QWidget*>
ancestorChain(QWidget* leaf)
{
    // parentWidget() already crosses window boundaries, so a combo's popup
    // container leads back to the combo and on up through the main window.
    QList<QWidget*> chain;
    for (QWidget* w = leaf; w; w = w->parentWidget()) chain << w;
    return chain;
}

QString
stableId(const QWidget* w)
{
    QStringList parts;
    for (; w; w = w->parentWidget())
        parts.prepend(hasUsefulName(w) ? w->objectName()
                                       : QString(w->metaObject()->className()));
    return parts.join('/');
}

QWidget*
resolve(const QString& id)
{
    const QStringList parts = id.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return nullptr;
    auto matches = [](const QWidget* w, const QString& seg) {
        return hasUsefulName(w) ? w->objectName() == seg
                                : seg == QLatin1String(w->metaObject()->className());
    };
    // Same-class siblings are common (stacked pages, slides); a visible one wins.
    auto pick = [&](const QList<QWidget*>& cands, const QString& seg) -> QWidget* {
        QWidget* hidden = nullptr;
        for (QWidget* c : cands) {
            if (isOverlay(c) || !matches(c, seg)) continue;
            if (c->isVisible()) return c;
            if (!hidden) hidden = c;
        }
        return hidden;
    };

    QWidget* w = pick(QApplication::topLevelWidgets(), parts.first());
    for (int i = 1; w && i < parts.size(); ++i)
        w = pick(w->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly), parts[i]);
    if (w || parts.size() > 1) return w;

    // A bare objectName ("Transits") or, failing that, an unnamed widget's
    // class ("Plain"): search every window, visible ones first.
    QList<QWidget*> all;
    for (QWidget* tl : QApplication::topLevelWidgets()) {
        if (isOverlay(tl)) continue;
        all << tl;
        all += tl->findChildren<QWidget*>();
    }
    if (QWidget* named = pick(all, parts.first()); named && named->objectName() == parts.first())
        return named;
    QWidget* hidden = nullptr;
    for (QWidget* c : all) {
        if (!hasUsefulName(c) && parts.first() == QLatin1String(c->metaObject()->className())) {
            if (c->isVisible()) return c;
            if (!hidden) hidden = c;
        }
    }
    return hidden;
}

QString
fileStem(const QWidget* w)
{
    if (!w) return QStringLiteral("capture");
    if (hasUsefulName(w)) return fileSafe(w->objectName());

    // A popup (combo dropdown, menu) is named after the widget that owns it.
    if (w->windowType() == Qt::Popup && w->parentWidget())
        return fileStem(w->parentWidget()) + "-popup";

    QStringList parts;
    for (const QWidget* a = w->parentWidget(); a; a = a->parentWidget()) {
        if (hasUsefulName(a)) {
            parts << fileSafe(a->objectName());
            break;
        }
    }
    const QString hint = slug(titleOf(w));
    QString cls        = QString(w->metaObject()->className());
    if (cls.startsWith('Q') && cls.size() > 1 && cls[1].isUpper()) cls.remove(0, 1);
    // A hint (button text, tab title) says more than the class name does.
    parts << (hint.isEmpty() ? slug(cls) : hint);
    return parts.join('-');
}

QString
label(const QWidget* w)
{
    if (!w) return QString();
    QString s = w->metaObject()->className();
    if (!w->objectName().isEmpty()) s += '#' + w->objectName();
    return s;
}

Descriptor
describe(const QWidget* w)
{
    Descriptor d;
    if (!w) return d;
    d.id         = stableId(w);
    d.objectName = w->objectName();
    d.className  = w->metaObject()->className();
    d.title      = titleOf(w);
    d.globalGeom = QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
    return d;
}

} // namespace WidgetPicker
