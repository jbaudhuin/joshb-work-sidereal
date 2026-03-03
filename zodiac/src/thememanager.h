#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QColor>

class QWidget;
class QApplication;

/**
 * @brief Singleton class for managing application themes
 * 
 * ThemeManager handles loading and switching between different visual themes
 * (Dark, Light, Printable). It manages QSS stylesheet loading, property-based
 * theme propagation to widgets, and color palette access.
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Available theme types
     */
    enum class Theme {
        Dark,       ///< Dark theme (default)
        Light,      ///< Light theme
        Printable   ///< Printable theme (light base with overrides for ink optimization)
    };
    Q_ENUM(Theme)

    /**
     * @brief Get the singleton instance
     */
    static ThemeManager& instance();

    /**
     * @brief Get the current active theme
     */
    Theme currentTheme() const { return m_currentTheme; }

    /**
     * @brief Get the current theme as a string
     */
    QString currentThemeName() const;

    /**
     * @brief Set the active theme
     * @param theme The theme to apply
     * @param propagateToWidgets If true, immediately propagate theme property to all widgets
     */
    void setTheme(Theme theme, bool propagateToWidgets = true);

    /**
     * @brief Set the active theme by name
     * @param themeName Theme name: "dark", "light", or "printable"
     */
    void setTheme(const QString& themeName, bool propagateToWidgets = true);

    /**
     * @brief Load and return the stylesheet for the current theme
     */
    QString loadStyleSheet();

    /**
     * @brief Apply the current theme to the application
     * This loads the stylesheet and propagates theme properties to all widgets
     */
    void applyToApplication(QApplication* app);

    /**
     * @brief Propagate theme property to a widget and all its children
     * Sets the "theme" property to enable property-based QSS selectors
     */
    void propagateThemeProperty(QWidget* widget);

    /**
     * @brief Get a theme-appropriate color for chart underlays
     * @return Semi-transparent underlay color based on current theme
     */
    QColor getUnderlayColor() const;

    /**
     * @brief Get a theme-appropriate border color for chart underlays
     * @return Very subtle border color for underlays (0.5px recommended)
     */
    QColor getUnderlayBorderColor() const;

    /**
     * @brief Get theme-adjusted gold color for better contrast
     * @param forPrint If true, returns print-optimized gold color
     * @return Gold color appropriate for current theme
     */
    QColor getGoldColor(bool forPrint = false) const;

    /**
     * @brief Get table cell highlight color for matching/filtered cells
     * @return Background color for highlighted table cells
     */
    QColor getTableHighlightColor() const;

    /**
     * @brief Get text color for highlighted table cells
     * @return Foreground/text color for highlighted cells
     */
    QColor getTableHighlightTextColor() const;

    /**
     * @brief Get theme-appropriate text color for HTML output
     * @return Text color hex string (e.g., "#b5bfdf" for dark, "#3A4A5A" for light)
     */
    QString getTextColor() const;

    /**
     * @brief Get theme-appropriate heading color for HTML output
     * @return Heading color hex string (e.g., "#e9e9e4" for dark, "#1A1A1A" for light)
     */
    QString getHeadingColor() const;

    // Chart-specific colors for QGraphicsScene rendering
    /**
     * @brief Get chart background color
     * @return Semi-transparent background color for chart
     */
    QColor getChartBackgroundColor() const;

    /**
     * @brief Get zodiac circle pen color
     * @return Color for the main zodiac circle
     */
    QColor getChartZodiacColor() const;

    /**
     * @brief Get chart border color
     * @return Color for chart borders and outlines
     */
    QColor getChartBorderColor() const;

    /**
     * @brief Get inner circle color
     * @return Color for inner concentric circles
     */
    QColor getChartCircleColor() const;

    /**
     * @brief Get zodiac sign fill color
     * @return Fill color for zodiac sign glyphs
     */
    QColor getChartSignFillColor() const;

    /**
     * @brief Get zodiac sign shape/outline color
     * @return Outline color for zodiac sign glyphs
     */
    QColor getChartSignShapeColor() const;

    /**
     * @brief Get house cusp line color
     * @return Color for standard house cusp lines
     */
    QColor getChartCuspColor() const;

    /**
     * @brief Get Ascendant cusp color
     * @return Accent color for Ascendant (1st house cusp)
     */
    QColor getChartAscendantColor() const;

    /**
     * @brief Get MC cusp color
     * @return Accent color for MC (10th house cusp)
     */
    QColor getChartMCColor() const;

    /**
     * @brief Get planet marker color for specific file/chart
     * @param fileIndex Chart index (0 for primary, 1 for secondary)
     * @return Color for planet position markers
     */
    QColor getChartPlanetMarkerColor(int fileIndex = 0) const;

    /**
     * @brief Get planet glyph color for specific file/chart
     * @param fileIndex Chart index (0 for primary, 1 for secondary)
     * @return Color for planet glyphs
     */
    QColor getChartPlanetColor(int fileIndex = 0) const;

    /**
     * @brief Get planet glyph outline color for specific file/chart
     * @param fileIndex Chart index (0 for primary, 1 for secondary)
     * @return Outline color for planet glyphs
     */
    QColor getChartPlanetShapeColor(int fileIndex = 0) const;

    /**
     * @brief Get cusp label text color for specific file/chart
     * @param fileIndex Chart index (0 for primary, 1 for secondary)
     * @return Color for house cusp labels
     */
    QColor getChartCuspLabelColor(int fileIndex = 0) const;

    /**
     * @brief Get midpoint visualization color (chord + connecting lines)
     * @return White for dark theme, black for light/printable
     */
    QColor getChartMidpointColor() const;

    /**
     * @brief Get the HTML export CSS file path for a specific theme
     * @param theme Theme to get export CSS for (defaults to current theme)
     * @return Path to the CSS file for HTML export
     */
    QString getHtmlExportCssPath(Theme theme) const;
    QString getHtmlExportCssPath() const { return getHtmlExportCssPath(m_currentTheme); }

    /**
     * @brief Load HTML export CSS content for a specific theme
     */
    QString loadHtmlExportCss(Theme theme) const;
    QString loadHtmlExportCss() const { return loadHtmlExportCss(m_currentTheme); }

    /**
     * @brief Convert theme enum to string
     */
    static QString themeToString(Theme theme);

    /**
     * @brief Convert string to theme enum
     * @return Theme enum, or Dark if string is invalid
     */
    static Theme stringToTheme(const QString& themeName);

protected:
    /**
     * @brief Event filter to auto-apply theme to newly created dialogs
     */
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    /**
     * @brief Emitted when the theme changes
     * @param newTheme The new active theme
     */
    void themeChanged(Theme newTheme);

private:
    ThemeManager();
    ~ThemeManager() = default;
    
    // Prevent copying
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    /**
     * @brief Load a stylesheet file from the themes directory
     */
    QString loadStyleSheetFile(const QString& filename) const;

    Theme m_currentTheme;
    static ThemeManager* s_instance;
};

#endif // THEMEMANAGER_H
