#pragma once

// Widget-under-cursor helpers shared by DocCapture, and meant to be reused by a
// future objectName -> {label, help anchor} registry (F1 context help, callout
// mode). Pure inspection: nothing here installs filters or shows UI.

#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>

class QWidget;

namespace WidgetPicker
{

// Dynamic property set on DocCapture's own overlay windows so pickers skip them.
extern const char* const OverlayProperty;

struct Descriptor
{
    QString id;         // stableId(): objectName path from the window down
    QString objectName;
    QString className;
    QString title;      // window/dock title, button text or tab text, if any
    QRect   globalGeom; // logical pixels, screen coordinates
};

bool            isOverlay(const QWidget* w);
QWidget*        widgetAtGlobal(const QPoint& global);
QList<QWidget*> ancestorChain(QWidget* leaf); // leaf first; popups continue into their owner
QString         stableId(const QWidget* w);
QWidget*        resolve(const QString& id);   // stableId() path or bare objectName -> widget
QString         fileStem(const QWidget* w);   // objectName, or a derived fallback
QString         label(const QWidget* w);      // "QToolButton#hasp5btn"
Descriptor      describe(const QWidget* w);

} // namespace WidgetPicker
