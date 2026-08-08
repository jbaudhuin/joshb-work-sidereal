#include "astrodatetimeedit.h"
#include "astro-calc.h"

#include <QHBoxLayout>
#include <QRegularExpression>
#include <QToolTip>
#include <QVBoxLayout>

namespace A
{

// -------------------------------------------------------------------------
// Helper: construct QDate for any year including 0 and negative
// -------------------------------------------------------------------------

/// Create a QDate from astronomical year, month, day.
/// If QDate(y,m,d) is invalid (Qt versions that reject year 0 or negative
/// years), fall back to computing the Julian Day Number via the standard
/// astronomical formula and using QDate::fromJulianDay().
static QDate
makeQDate(int y, int m, int d)
{
    QDate qd(y, m, d);
    if (qd.isValid()) return qd;

    // Standard Julian Day Number formula (Meeus, Astronomical Algorithms)
    // Valid for all dates including negative years.
    int a = (14 - m) / 12;
    int yr = y + 4800 - a;
    int mo = m + 12 * a - 3;
    // Proleptic Gregorian
    qint64 jdn = d + (153 * mo + 2) / 5 + 365 * yr
               + yr / 4 - yr / 100 + yr / 400 - 32045;
    qd = QDate::fromJulianDay(jdn);
    return qd;
}

/// Round a QTime to the nearest second. Computed times (e.g. from
/// dateTimeFromJulian()) often carry floating-point noise in the
/// milliseconds (59.99999996s instead of 60.0s); QTime::toString("HH:mm:ss")
/// truncates rather than rounds, so without this the display would show
/// ":59:59" instead of rolling over to the intended second/minute.
/// Note: wraps within the day (23:59:59.6 -> 00:00:00) without carrying
/// into the date; callers that have a full QDateTime should round that
/// instead (see setDateTime) so the day carries over correctly.
static QTime
roundToNearestSecond(const QTime& t)
{
    int msec = t.msec();
    if (msec == 0) return t;
    return (msec >= 500) ? t.addMSecs(1000 - msec) : t.addMSecs(-msec);
}

// -------------------------------------------------------------------------
// Construction
// -------------------------------------------------------------------------

AstroDateTimeEdit::AstroDateTimeEdit(bool dateOnly, QWidget* parent) :
    QWidget(parent),
    _calType(Cal_Auto),
    _timeMode(Time_ZoneTime),
    _geoLon(0.0),
    _dateOnly(dateOnly),
    _blocked(false),
    _minDate(QDate(-4713, 1, 1))
{
    // --- Date field ---
    _dateEdit = new QLineEdit;
    _dateEdit->setPlaceholderText(tr("YYYY-MM-DD  (or  44 BC-03-15)"));
    _dateEdit->setToolTip(
        tr("Enter a date.  Suffixes: OS or J = Julian,  NS or G = Gregorian.\n"
           "BC dates: \"44 BC-03-15\" or \"-43-03-15\" (astronomical year).\n"
           "No suffix → auto-detect (Julian before 1582-10-15, Gregorian after)."));

    // --- Calendar popup button ---
    _calBtn = new QPushButton(QStringLiteral("\u2026")); // ellipsis
    _calBtn->setObjectName(QStringLiteral("calPopupBtn"));
    _calBtn->setFixedWidth(24);
    _calBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _calBtn->setToolTip(tr("Open calendar popup"));
    _calBtn->setFlat(true);

    _calPopup = new QCalendarWidget;
    _calPopup->setWindowFlags(Qt::Popup);
    _calPopup->hide();

    // --- Time field ---
    _timeEdit = new QLineEdit;
    _timeEdit->setPlaceholderText(QStringLiteral("HH:MM:SS  (LMT / LAT)"));
    _timeEdit->setMaximumWidth(160);

    // Layout
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(_dateEdit, 3);
    layout->addWidget(_calBtn);

    if (!dateOnly) {
        layout->addWidget(_timeEdit, 2);
    }
    setLayout(layout);

    // Hide time widgets if date-only
    if (dateOnly) {
        _timeEdit->hide();
    }

    // Connections
    connect(_dateEdit, &QLineEdit::editingFinished, this,
            &AstroDateTimeEdit::onDateTextEdited);
    connect(_dateEdit, &QLineEdit::editingFinished, this,
            &AstroDateTimeEdit::editingFinished);
    connect(_calBtn, &QPushButton::clicked, this,
            &AstroDateTimeEdit::showCalendarPopup);
    connect(_calPopup, &QCalendarWidget::clicked, this,
            &AstroDateTimeEdit::onCalendarClicked);

    if (!dateOnly) {
        connect(_timeEdit, &QLineEdit::editingFinished, this,
                &AstroDateTimeEdit::onTimeTextEdited);
        connect(_timeEdit, &QLineEdit::editingFinished, this,
                &AstroDateTimeEdit::editingFinished);
    }

    // Initial state
    _parsedDate.valid = false;
    _parsedTime.valid = false;
    _parsedTime.time     = QTime(12, 0, 0);
    _parsedTime.timeMode  = Time_ZoneTime;
    updateTooltip();
}

// -------------------------------------------------------------------------
// Getters
// -------------------------------------------------------------------------

QDate
AstroDateTimeEdit::date() const
{
    return _parsedDate.valid ? _parsedDate.date : QDate();
}

QTime
AstroDateTimeEdit::time() const
{
    return _parsedTime.valid ? _parsedTime.time : QTime(12, 0, 0);
}

CalendarType
AstroDateTimeEdit::calendarType() const
{
    return _calType;
}

TimeMode
AstroDateTimeEdit::timeMode() const
{
    return _timeMode;
}

bool
AstroDateTimeEdit::isBCDate() const
{
    return _parsedDate.bcDate;
}

int
AstroDateTimeEdit::astroYear() const
{
    return _parsedDate.valid ? _parsedDate.astroYear : 0;
}

QDateTime
AstroDateTimeEdit::localDateTime() const
{
    if (!_parsedDate.valid) return {};
    QDate d = _parsedDate.date;
    QTime t = _parsedTime.valid ? _parsedTime.time : QTime(12, 0, 0);
    return QDateTime(d, t);
}

// -------------------------------------------------------------------------
// Setters
// -------------------------------------------------------------------------

void
AstroDateTimeEdit::setDate(const QDate& date)
{
    if (!date.isValid()) return;
    _parsedDate.date         = date;
    _parsedDate.valid        = true;
    _parsedDate.calendarType = _calType;
    _parsedDate.astroYear    = date.year();
    _parsedDate.bcDate       = (date.year() <= 0);

    // Format display string
    QString txt;
    if (_parsedDate.bcDate) {
        int bcYear = 1 - date.year(); // astro 0 = 1 BC, -1 = 2 BC
        txt        = QString("%1 BC-%2-%3")
                  .arg(bcYear)
                  .arg(date.month(), 2, 10, QLatin1Char('0'))
                  .arg(date.day(), 2, 10, QLatin1Char('0'));
    } else {
        txt = date.toString(QStringLiteral("yyyy-MM-dd"));
    }
    // Append calendar suffix when overriding. (Cal_Auto's resolved
    // calendar is surfaced via the tooltip instead of the text itself —
    // any suffix typed here gets re-parsed on editingFinished, so an
    // "auto-detected" marker would risk being read back as an explicit
    // override and permanently pinning the calendar type. See
    // onDateTextEdited()/parseDate().)
    if (_calType == Cal_Julian) txt += QStringLiteral(" OS");
    else if (_calType == Cal_Gregorian && _parsedDate.astroYear < 1582)
        txt += QStringLiteral(" NS");

    _dateEdit->setText(txt);
    updateTooltip();
}

void
AstroDateTimeEdit::setTime(const QTime& time)
{
    if (!time.isValid()) return;
    QTime rounded         = roundToNearestSecond(time);
    _parsedTime.time     = rounded;
    _parsedTime.valid    = true;
    _parsedTime.timeMode = _timeMode;

    // Format with time-mode suffix
    QString txt = rounded.toString(QStringLiteral("HH:mm:ss"));
    if (_timeMode == Time_LMT) txt += QStringLiteral(" LMT");
    else if (_timeMode == Time_LAT) txt += QStringLiteral(" LAT");
    _timeEdit->setText(txt);
    updateTooltip();
}

void
AstroDateTimeEdit::setCalendarType(CalendarType ct)
{
    _calType = ct;
    if (_parsedDate.valid) setDate(_parsedDate.date); // re-format
}

void
AstroDateTimeEdit::setTimeMode(TimeMode tm)
{
    _timeMode = tm;
    if (_parsedTime.valid) setTime(_parsedTime.time); // re-format with suffix
}

void
AstroDateTimeEdit::setDateTime(const QDateTime& dt)
{
    if (!dt.isValid()) return;
    // Round to the nearest second across the full date/time so a computed
    // instant like 23:59:59.999... (floating-point noise) rolls over into
    // the next day rather than displaying as ":59:59" on the wrong day.
    int       msec    = dt.time().msec();
    QDateTime rounded = (msec >= 500) ? dt.addMSecs(1000 - msec) : dt.addMSecs(-msec);
    setDate(rounded.date());
    setTime(rounded.time());
}

void
AstroDateTimeEdit::setGeoLongitude(double lon)
{
    _geoLon = lon;
    updateTooltip();
}

void
AstroDateTimeEdit::setMinimumDate(const QDate& d)
{
    _minDate = d;
}

// -------------------------------------------------------------------------
// Slots
// -------------------------------------------------------------------------

void
AstroDateTimeEdit::blockSignals(bool block)
{
    _blocked = block;
    QWidget::blockSignals(block);
}

void
AstroDateTimeEdit::onDateTextEdited()
{
    auto parsed = parseDate(_dateEdit->text());
    if (!parsed.valid) return;
    _parsedDate = parsed;
    // If the text had an explicit calendar suffix, adopt it
    if (parsed.calendarType != Cal_Auto) _calType = parsed.calendarType;
    updateTooltip();
    if (!_blocked) {
        emit dateChanged(_parsedDate.date);
        emit dateTimeChanged();
    }
}

void
AstroDateTimeEdit::onTimeTextEdited()
{
    auto parsed = parseTime(_timeEdit->text());
    if (!parsed.valid) return;
    _parsedTime = parsed;
    _timeMode   = parsed.timeMode;
    updateTooltip();
    if (!_blocked) emit dateTimeChanged();
}

void
AstroDateTimeEdit::onCalendarClicked(const QDate& date)
{
    _calPopup->hide();
    setDate(date);
    if (!_blocked) {
        emit dateChanged(date);
        emit dateTimeChanged();
    }
}

void
AstroDateTimeEdit::showCalendarPopup()
{
    if (_parsedDate.valid) _calPopup->setSelectedDate(_parsedDate.date);
    QPoint pos = _calBtn->mapToGlobal(QPoint(0, _calBtn->height()));
    _calPopup->move(pos);
    _calPopup->show();
}

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

void
AstroDateTimeEdit::updateTooltip()
{
    if (!_parsedDate.valid) return;

    QStringList tips;

    // Calendar note
    if (_calType == Cal_Auto) {
        bool isJulian = (_parsedDate.astroYear < 1582
                         || (_parsedDate.astroYear == 1582
                             && (_parsedDate.date.month() < 10
                                 || (_parsedDate.date.month() == 10
                                     && _parsedDate.date.day() < 15))));
        tips << (isJulian ? tr("Calendar: Julian (Old Style) [auto-detected]")
                          : tr("Calendar: Gregorian (New Style) [auto-detected]"));
    } else if (_calType == Cal_Julian) {
        tips << tr("Calendar: Julian (Old Style) [forced]");
    } else {
        tips << tr("Calendar: Gregorian (New Style) [forced]");
    }

    if (_parsedDate.bcDate) {
        tips << tr("BC date — astronomical year %1").arg(_parsedDate.astroYear);
    }

    // Equation-of-time info when we have a valid time
    if (!_dateOnly && _parsedTime.valid && _geoLon != 0.0) {
        // Build a rough UTC from the local date/time + geolon for EoT
        QDateTime localDt(_parsedDate.date, _parsedTime.time);
        QDateTime utc = localToUTC(localDt, 0.0, _geoLon, Time_LMT, _calType);
        auto      eot = computeEoT(utc, _geoLon, _calType);
        if (eot.valid) {
            int    absSec = static_cast<int>(qAbs(eot.eotSeconds));
            int    mm     = absSec / 60;
            int    ss     = absSec % 60;
            QChar  sign   = (eot.eotSeconds >= 0) ? '+' : '-';
            tips << tr("Equation of Time: %1%2m %3s").arg(sign).arg(mm).arg(ss);

            // Show LAT equivalent
            QDateTime latDt = dateTimeFromJulian(eot.latJD, _calType);
            tips << tr("LAT (sundial): %1").arg(latDt.time().toString("HH:mm:ss"));

            // Show LMT equivalent
            QDateTime lmtDt = dateTimeFromJulian(eot.lmtJD, _calType);
            tips << tr("LMT: %1").arg(lmtDt.time().toString("HH:mm:ss"));
        }
    }

    setToolTip(tips.join('\n'));
}

// -------------------------------------------------------------------------
// Date parser
// -------------------------------------------------------------------------

ParsedDate
AstroDateTimeEdit::parseDate(const QString& text) const
{
    ParsedDate result;
    result.valid        = false;
    result.bcDate       = false;
    result.astroYear    = 0;
    result.calendarType = _calType; // inherit current unless overridden

    QString s = text.trimmed();
    if (s.isEmpty()) return result;

    // Detect and strip calendar suffixes
    // "OS" or "J" → Julian, "NS" or "G" → Gregorian
    static const QRegularExpression reSuffix(
        QStringLiteral("\\s+(OS|NS|J|G)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = reSuffix.match(s);
    if (m.hasMatch()) {
        QString suf = m.captured(1).toUpper();
        if (suf == "OS" || suf == "J")
            result.calendarType = Cal_Julian;
        else
            result.calendarType = Cal_Gregorian;
        s = s.left(m.capturedStart());
    }

    // Detect BC / BCE / AD / CE
    static const QRegularExpression reBCPrefix(
        QStringLiteral("^(\\d+)\\s*(?:BC|BCE)\\s*-"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reBCSuffix(
        QStringLiteral("\\s*(?:BC|BCE)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    int  yearSign = 1;
    bool hasBCTag = false;

    // Check "44 BC-03-15" style
    auto mBC = reBCPrefix.match(s);
    if (mBC.hasMatch()) {
        int bcYear       = mBC.captured(1).toInt();
        result.astroYear = 1 - bcYear; // 1 BC = year 0, 44 BC = year -43
        result.bcDate    = true;
        hasBCTag         = true;
        s = QString::number(result.astroYear) + s.mid(mBC.capturedEnd() - 1);
    }

    // Check trailing "BC" / "BCE"
    if (!hasBCTag) {
        auto mBCS = reBCSuffix.match(s);
        if (mBCS.hasMatch()) {
            // Extract year from the beginning
            s = s.left(mBCS.capturedStart()).trimmed();
            hasBCTag = true;
        }
    }

    // Check leading negative year (astronomical convention)
    if (!hasBCTag && s.startsWith('-')) {
        yearSign = -1;
        // Don't strip the sign yet — QDate::fromString handles it
    }

    // Parse YYYY-MM-DD (or Y-MM-DD for negative/low years)
    static const QRegularExpression reISO(
        QStringLiteral("^(-?\\d+)-(\\d{1,2})-(\\d{1,2})"));
    auto mISO = reISO.match(s);
    if (mISO.hasMatch()) {
        int y = mISO.captured(1).toInt();
        int mo = mISO.captured(2).toInt();
        int d = mISO.captured(3).toInt();
        if (hasBCTag && !s.startsWith('-')) {
            // "44 BC" was converted above
        } else if (yearSign == -1) {
            result.astroYear = y; // already negative
            result.bcDate    = true;
        } else {
            result.astroYear = y;
            result.bcDate    = (y <= 0);
        }
        // QDate in Qt uses proleptic Gregorian internally, but we just need
        // a year/month/day container.  For negative years, QDate may fail
        // on older Qt.  Use JD-based construction as fallback.
        QDate d2 = makeQDate(y, mo, d);
        if (d2.isValid()) {
            result.date  = d2;
            result.valid = true;
        }
        return result;
    }

    // Fallback: try locale-aware parse
    QDate d2 = QDate::fromString(s, Qt::ISODate);
    if (!d2.isValid()) d2 = QDate::fromString(s, Qt::TextDate);
    if (!d2.isValid()) {
        // Try slash-separated date: MM/DD/YYYY or DD/MM/YYYY
        static const QRegularExpression reSlash(
            QStringLiteral("^(\\d{1,2})/(\\d{1,2})/(\\d+)"));
        auto mSlash = reSlash.match(s);
        if (mSlash.hasMatch()) {
            int a    = mSlash.captured(1).toInt();
            int b    = mSlash.captured(2).toInt();
            int year = mSlash.captured(3).toInt();
            // Try MM/DD/YYYY first (if a ≤ 12)
            if (a >= 1 && a <= 12)
                d2 = makeQDate(year, a, b);
            // If that failed, try DD/MM/YYYY
            if (!d2.isValid() && b >= 1 && b <= 12)
                d2 = makeQDate(year, b, a);
        }
    }
    if (d2.isValid()) {
        result.date      = d2;
        result.astroYear = d2.year();
        result.bcDate    = (d2.year() <= 0);
        result.valid     = true;
    }

    return result;
}

// -------------------------------------------------------------------------
// Time parser — supports:
//   HH:MM:SS, HH:MM, H:MM, H:MM:SS
//   12-hour: "2:55 PM", "2:55:35 PM", "10am", "2pm"
//   Bare hour: "10", "22"
//   Trailing tokens: LMT, LAT (case-insensitive)
//   e.g. "14:15 LAT", "2:55:35 PM LMT", "08:30", "10am LAT"
//   Leading non-numeric characters are stripped as typos.
// -------------------------------------------------------------------------

ParsedTime
AstroDateTimeEdit::parseTime(const QString& text) const
{
    ParsedTime result;
    result.valid    = false;
    result.timeMode = _timeMode; // inherit current unless overridden

    QString s = text.trimmed();
    if (s.isEmpty()) return result;

    // --- Detect and strip trailing time-mode token (LMT / LAT) ---
    static const QRegularExpression reTimeMode(
        QStringLiteral("\\s+(LMT|LAT)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    auto mMode = reTimeMode.match(s);
    if (mMode.hasMatch()) {
        QString tok = mMode.captured(1).toUpper();
        if (tok == QStringLiteral("LMT"))
            result.timeMode = Time_LMT;
        else
            result.timeMode = Time_LAT;
        s = s.left(mMode.capturedStart()).trimmed();
    } else {
        // No explicit token → zone time
        result.timeMode = Time_ZoneTime;
    }

    // --- Detect and strip AM / PM (with or without space/glued) ---
    bool hasAmPm = false;
    bool isPM    = false;
    static const QRegularExpression reAmPm(
        QStringLiteral("\\s*(AM|PM)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    auto mAP = reAmPm.match(s);
    if (mAP.hasMatch()) {
        hasAmPm = true;
        isPM    = (mAP.captured(1).toUpper() == QStringLiteral("PM"));
        s       = s.left(mAP.capturedStart()).trimmed();
    }

    // --- Strip leading non-numeric junk (typos like "j10") ---
    static const QRegularExpression reLeadingJunk(
        QStringLiteral("^[^\\d]+"));
    s.remove(reLeadingJunk);

    if (s.isEmpty()) return result;

    // --- Parse the numeric time portion ---
    QTime t = QTime::fromString(s, QStringLiteral("H:mm:ss"));
    if (!t.isValid()) t = QTime::fromString(s, QStringLiteral("HH:mm:ss"));
    if (!t.isValid()) t = QTime::fromString(s, QStringLiteral("H:mm"));
    if (!t.isValid()) t = QTime::fromString(s, QStringLiteral("HH:mm"));
    if (!t.isValid()) t = QTime::fromString(s, QStringLiteral("Hmm"));

    // Bare hour: "10", "2", "22"
    if (!t.isValid()) {
        bool ok  = false;
        int  h   = s.toInt(&ok);
        if (ok && h >= 0 && h <= 23) {
            t = QTime(h, 0, 0);
        }
    }

    if (!t.isValid()) return result;

    // Apply AM/PM conversion
    if (hasAmPm) {
        int h = t.hour();
        if (isPM && h < 12) h += 12;
        else if (!isPM && h == 12) h = 0;
        t = QTime(h, t.minute(), t.second());
    }

    result.time  = t;
    result.valid = true;
    return result;
}

} // namespace A
