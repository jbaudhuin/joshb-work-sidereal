#include "thememanager.h"
#include <QApplication>
#include <QWidget>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QDir>
#include <QStyle>
#include <QEvent>
#include <QSettings>

ThemeManager* ThemeManager::s_instance = nullptr;

ThemeManager::ThemeManager()
    : m_currentTheme(Theme::Dark)
{
}

ThemeManager& ThemeManager::instance()
{
    if (!s_instance) {
        s_instance = new ThemeManager();
    }
    return *s_instance;
}

QString ThemeManager::currentThemeName() const
{
    return themeToString(m_currentTheme);
}

void ThemeManager::setTheme(Theme theme, bool propagateToWidgets)
{
    if (m_currentTheme == theme) {
        return; // No change
    }

    m_currentTheme = theme;

    // Save theme preference to settings
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Astroprocessor", "Zodiac");
    settings.setValue("theme", themeToString(theme));
    settings.sync();

    if (propagateToWidgets && qApp) {
        applyToApplication(qApp);
    }

    emit themeChanged(theme);
}

void ThemeManager::setTheme(const QString& themeName, bool propagateToWidgets)
{
    setTheme(stringToTheme(themeName), propagateToWidgets);
}

QString ThemeManager::loadStyleSheet()
{
    QString stylesheet;

    switch (m_currentTheme) {
    case Theme::Dark:
        stylesheet = loadStyleSheetFile("themes/dark.qss");
        break;

    case Theme::Light:
        stylesheet = loadStyleSheetFile("themes/light.qss");
        break;

    case Theme::Printable:
        // Printable inherits from light + overrides
        stylesheet = loadStyleSheetFile("themes/light.qss");
        stylesheet += "\n/* === PRINTABLE THEME OVERRIDES === */\n";
        stylesheet += loadStyleSheetFile("themes/printable-overrides.qss");
        break;
    }

    if (stylesheet.isEmpty()) {
        qWarning() << "ThemeManager: Failed to load stylesheet for theme:" << currentThemeName();
        qWarning() << "ThemeManager: Falling back to legacy style.css";
        // Fallback to existing stylesheet
        stylesheet = loadStyleSheetFile("style/style.css");
    }

    return stylesheet;
}

void ThemeManager::applyToApplication(QApplication* app)
{
    if (!app) {
        return;
    }

    // Load and apply stylesheet
    QString stylesheet = loadStyleSheet();
    app->setStyleSheet(stylesheet);

    // Install global event filter to auto-theme new dialogs
    app->removeEventFilter(this); // Remove first if already installed
    app->installEventFilter(this);

    // Propagate theme property to all top-level widgets
    for (QWidget* widget : app->topLevelWidgets()) {
        propagateThemeProperty(widget);
    }
}

void ThemeManager::propagateThemeProperty(QWidget* widget)
{
    if (!widget) {
        return;
    }

    // Set theme property on this widget
    QString themeName = currentThemeName();
    widget->setProperty("theme", themeName);

    // Recursively set on all children
    for (QWidget* child : widget->findChildren<QWidget*>()) {
        child->setProperty("theme", themeName);
    }

    // Force style refresh
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QColor ThemeManager::getUnderlayColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
    case Theme::Printable:
        // White semi-transparent for light backgrounds
        return QColor(255, 255, 255, 200); // 80% opacity

    case Theme::Dark:
    default:
        // Black semi-transparent for dark backgrounds
        return QColor(0, 0, 0, 180); // 70% opacity
    }
}

QColor ThemeManager::getUnderlayBorderColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
    case Theme::Printable:
        // Very subtle gray border for light themes
        return QColor(200, 200, 200, 150);

    case Theme::Dark:
    default:
        // Very subtle light border for dark themes
        return QColor(80, 80, 80, 150);
    }
}

QColor ThemeManager::getGoldColor(bool forPrint) const
{
    // For printable theme or when specifically requested for print,
    // use darker gold for better contrast on white
    if (m_currentTheme == Theme::Printable || forPrint) {
        return QColor("#B8860B"); // Dark goldenrod - better print contrast
    }

    switch (m_currentTheme) {
    case Theme::Light:
        // Much darker gold for light theme (better contrast)
        return QColor("#B8860B"); // Dark goldenrod

    case Theme::Dark:
    default:
        // Standard gold for dark theme
        return QColor("#FFD700"); // Gold
    }
}

QColor ThemeManager::getTableHighlightColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
    case Theme::Printable:
        // Lighter, more visible blue for light theme
        return QColor(173, 216, 230, 180); // Light blue with good opacity
        
    case Theme::Dark:
    default:
        // Light blue for dark theme - increase opacity for better visibility
        return QColor(113, 174, 236, 200);
    }
}

QColor ThemeManager::getTableHighlightTextColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
    case Theme::Printable:
        // Black text on medium-blue background for light theme
        return QColor(0, 0, 0);
        
    case Theme::Dark:
    default:
        // Dark text on light blue background
        return QColor(0, 0, 0);
    }
}

QString ThemeManager::getTextColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return "#000000"; // Pure black for maximum readability on white
        
    case Theme::Printable:
        return "#000000"; // Pure black for printing
        
    case Theme::Dark:
    default:
        return "#E0E0E0"; // Light gray for dark backgrounds
    }
}

QString ThemeManager::getHeadingColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
    case Theme::Printable:
        return "#000000"; // Pure black for light backgrounds
        
    case Theme::Dark:
    default:
        return "#FFFFFF"; // Pure white for dark backgrounds
    }
}

// Chart-specific colors
QColor ThemeManager::getChartBackgroundColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(240, 245, 255, 80); // Very light blue, semi-transparent
        
    case Theme::Printable:
        return QColor(255, 255, 255, 30); // Nearly transparent white
        
    case Theme::Dark:
    default:
        return QColor(8, 103, 192, 50); // Dark blue, semi-transparent
    }
}

QColor ThemeManager::getChartZodiacColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(70, 130, 180); // Steel blue for light theme
        
    case Theme::Printable:
        return QColor(40, 40, 40); // Dark gray for printing
        
    case Theme::Dark:
    default:
        return QColor(31, 52, 93); // Dark blue for dark theme
    }
}

QColor ThemeManager::getChartBorderColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(100, 149, 237); // Cornflower blue
        
    case Theme::Printable:
        return QColor(0, 0, 0); // Black for printing
        
    case Theme::Dark:
    default:
        return QColor(50, 145, 240); // Light blue
    }
}

QColor ThemeManager::getChartCircleColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(160, 160, 160); // Medium gray
        
    case Theme::Printable:
        return QColor(128, 128, 128); // Gray for printing
        
    case Theme::Dark:
    default:
        return QColor(227, 214, 202); // Beige/tan
    }
}

QColor ThemeManager::getChartSignFillColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(255, 255, 255); // White fill
        
    case Theme::Printable:
        return QColor(255, 255, 255); // White for printing
        
    case Theme::Dark:
    default:
        return Qt::black; // Black fill
    }
}

QColor ThemeManager::getChartSignShapeColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(60, 60, 60); // Dark gray outline
        
    case Theme::Printable:
        return QColor(0, 0, 0); // Black outline for printing
        
    case Theme::Dark:
    default:
        return QColor(109, 109, 109); // Medium gray
    }
}

QColor ThemeManager::getChartCuspColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(120, 120, 120); // Medium gray
        
    case Theme::Printable:
        return QColor(80, 80, 80); // Dark gray for printing
        
    case Theme::Dark:
    default:
        return QColor(227, 214, 202); // Beige
    }
}

QColor ThemeManager::getChartAscendantColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(220, 60, 40); // Bright red-orange
        
    case Theme::Printable:
        return QColor(0, 0, 0); // Black for printing
        
    case Theme::Dark:
    default:
        return QColor(250, 90, 58); // Orange-red
    }
}

QColor ThemeManager::getChartMCColor() const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return QColor(180, 150, 100); // Muted gold
        
    case Theme::Printable:
        return QColor(100, 100, 100); // Gray for printing
        
    case Theme::Dark:
    default:
        return QColor(210, 195, 150); // Light beige/gold
    }
}

QColor ThemeManager::getChartPlanetMarkerColor(int fileIndex) const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return (fileIndex == 0) ? QColor("#4682B4") : QColor("#1E90FF"); // Steel blue / Dodger blue
        
    case Theme::Printable:
        return (fileIndex == 0) ? QColor("#000000") : QColor("#404040"); // Black / Dark gray
        
    case Theme::Dark:
    default:
        return (fileIndex == 0) ? QColor("#cee1f2") : QColor("#00C0FF"); // Light blue / Cyan
    }
}

QColor ThemeManager::getChartPlanetColor(int fileIndex) const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return (fileIndex == 0) ? QColor("#2F4F7F") : QColor("#1E90FF"); // Dark slate blue / Dodger blue
        
    case Theme::Printable:
        return (fileIndex == 0) ? QColor("#000000") : QColor("#404040"); // Black / Dark gray
        
    case Theme::Dark:
    default:
        return (fileIndex == 0) ? QColor("#cee1f2") : QColor("#00C0FF"); // Light blue / Cyan
    }
}

QColor ThemeManager::getChartPlanetShapeColor(int fileIndex) const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return (fileIndex == 0) ? QColor("#D8E4F0") : QColor("#C8E0FF"); // Light blue shades for light theme
        
    case Theme::Printable:
        return QColor("#000000"); // Black for all in print
        
    case Theme::Dark:
    default:
        return (fileIndex == 0) ? QColor("#48401d") : QColor("#412631"); // Brown / Purple-brown
    }
}

QColor ThemeManager::getChartCuspLabelColor(int fileIndex) const
{
    switch (m_currentTheme) {
    case Theme::Light:
        return (fileIndex == 0) ? QColor("#1A1A1A") : QColor("#0066CC"); // Very dark / Blue
        
    case Theme::Printable:
        return QColor("#000000"); // Black for all in print
        
    case Theme::Dark:
    default:
        return (fileIndex == 0) ? QColor("#FFFFFF") : QColor("#00C0FF"); // White / Cyan
    }
}

QString ThemeManager::getHtmlExportCssPath(Theme theme) const
{
    QString filename;
    switch (theme) {
    case Theme::Dark:
        filename = "themes/html-export/dark.css";
        break;
    case Theme::Light:
        filename = "themes/html-export/light.css";
        break;
    case Theme::Printable:
        filename = "themes/html-export/printable.css";
        break;
    }

    return QDir::currentPath() + "/" + filename;
}

QString ThemeManager::loadHtmlExportCss(Theme theme) const
{
    QString cssPath = getHtmlExportCssPath(theme);
    QFile file(cssPath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ThemeManager: Failed to load HTML export CSS:" << cssPath;
        return QString();
    }

    QTextStream in(&file);
    return in.readAll();
}

QString ThemeManager::loadStyleSheetFile(const QString& filename) const
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "ThemeManager: Could not open stylesheet file:" << filename;
        return QString();
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    return content;
}

QString ThemeManager::themeToString(Theme theme)
{
    switch (theme) {
    case Theme::Dark:
        return "dark";
    case Theme::Light:
        return "light";
    case Theme::Printable:
        return "printable";
    default:
        return "dark";
    }
}

ThemeManager::Theme ThemeManager::stringToTheme(const QString& themeName)
{
    QString lower = themeName.toLower();

    if (lower == "light") {
        return Theme::Light;
    } else if (lower == "printable" || lower == "print") {
        return Theme::Printable;ch
    } else {
        return Theme::Dark; // Default
    }
}

bool ThemeManager::eventFilter(QObject* obj, QEvent* event)
{
    // Auto-apply theme to newly shown dialogs and windows
    if (event->type() == QEvent::Show) {
        if (QWidget* widget = qobject_cast<QWidget*>(obj)) {
            // Check if this is a top-level window (dialog, popup, etc.)
            if (widget->isWindow() && !widget->property("theme").isValid()) {
                //qDebug() << "ThemeManager: Auto-applying theme to" << widget->metaObject()->className();
                propagateThemeProperty(widget);
            }
        }
    }
    
    return QObject::eventFilter(obj, event);
}
