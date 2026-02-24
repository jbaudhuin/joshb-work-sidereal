#ifndef ASTRO_DATETIMEEDIT_H
#define ASTRO_DATETIMEEDIT_H

#include "astro-data.h"

#include <QCalendarWidget>
#include <QDate>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTime>
#include <QWidget>

namespace A
{

/// Describes the result of parsing a date string.
struct ParsedDate {
    QDate        date;         ///< Resolved date (in the chosen calendar)
    CalendarType calendarType; ///< Cal_Julian / Cal_Gregorian / Cal_Auto
    bool         bcDate;       ///< true when the user entered a BC date
    int          astroYear;    ///< Astronomical year (0 = 1 BC, -1 = 2 BC …)
    bool         valid;
};

/// Describes the result of parsing a time string.
struct ParsedTime {
    QTime    time;
    TimeMode timeMode; ///< Detected from suffix (LMT / LAT / default ZT)
    bool     valid;
};

/// A flexible date/time entry widget that supports:
///   - Julian (Old Style) and Gregorian (New Style) dates
///   - BC / BCE dates (astronomical year numbering)
///   - Zone Time, Local Mean Time (LMT), and Local Apparent Time (LAT)
///   - A calendar popup button for convenience
///   - An indicator showing [OS] / [NS] / [Auto] and the time-mode
///
/// Emits dateTimeChanged() whenever the entered value is modified and valid.
class AstroDateTimeEdit : public QWidget
{
    Q_OBJECT

  public:
    /// If \a dateOnly is true, the time field and time-mode combo are hidden.
    explicit AstroDateTimeEdit(bool dateOnly = false, QWidget* parent = nullptr);

    // ----- Getters -----
    QDate        date() const;
    QTime        time() const;
    CalendarType calendarType() const;
    TimeMode     timeMode() const;
    bool         isBCDate() const;
    int          astroYear() const; ///< 0 = 1 BC, -1 = 2 BC …

    /// Return the currently entered local date/time as a QDateTime
    /// (no timezone applied — caller must use localToUTC() to get UTC).
    QDateTime localDateTime() const;

    // ----- Setters (used when loading a chart) -----
    void setDate(const QDate& date);
    void setTime(const QTime& time);
    void setCalendarType(CalendarType ct);
    void setTimeMode(TimeMode tm);
    void setDateTime(const QDateTime& dt);

    /// Provide the geographic longitude so that the EoT tooltip can
    /// be computed.  Call whenever location changes.
    void setGeoLongitude(double lon);

    /// Minimum date (default: year -4713 = 4714 BC, the Julian Day epoch).
    void setMinimumDate(const QDate& d);

  signals:
    /// Emitted when the user changes any part of the date/time/calendar/mode.
    void dateTimeChanged();

    /// Emitted when only the date portion changes.
    void dateChanged(const QDate& newDate);

    /// Emitted when the user finishes editing (Return pressed or focus lost).
    /// Provided for compatibility with QDateEdit signal expectations.
    void editingFinished();

  public slots:
    void blockSignals(bool block);

  private slots:
    void onDateTextEdited();
    void onTimeTextEdited();
    void onCalendarClicked(const QDate& date);
    void showCalendarPopup();

  private:
    void       updateTooltip();
    ParsedDate parseDate(const QString& text) const;
    ParsedTime parseTime(const QString& text) const;

    QLineEdit*      _dateEdit;
    QLineEdit*      _timeEdit;
    QPushButton*    _calBtn;      ///< calendar popup button

    QCalendarWidget* _calPopup;

    CalendarType _calType;
    TimeMode     _timeMode;
    double       _geoLon;
    bool         _dateOnly;
    bool         _blocked;

    // Cached parsed values
    ParsedDate _parsedDate;
    ParsedTime _parsedTime;
    QDate      _minDate;
};

} // namespace A

#endif // ASTRO_DATETIMEEDIT_H
