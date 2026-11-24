# Troubleshooting &amp; Crash Reports

Most problems with Zodiac fall into one of two buckets: a setup step that hasn't
been completed, or the rare unexpected close. This page covers both, and tells you
where your files live so you can find, back up, or move them.

## Location search doesn't work

The single most common setup snag is location lookup. Zodiac uses a Google
geography and time-zone service to turn a place name into coordinates and a time
zone, and that service needs an **API key**. If searching for a city returns
nothing, the key is usually missing or invalid. Without a key, Zodiac starts on its
offline city database instead, which covers towns and cities but may miss very small
places. See
[Locations, time &amp; calendars](../getting-started/location-time.md) for how to
supply the key and confirm it is working.

## Crash reports

If Zodiac closes unexpectedly, a **release build captures a crash report** — a
minidump plus a symbolized stack trace — and saves it under:

```
%LOCALAPPDATA%\Zodiac\crashes
```

The next time you launch the program, you are offered the chance to **bundle the
report into a single zip file** and **reveal it** in the file manager, ready to
send on. Gathering that zip is the most useful first step if you want to report a
crash, because the symbolized stack tells the maintainer where the problem
occurred.

## Where your files live

Knowing these locations helps with backups, cloud sync, and moving to a new
machine:

- **Charts** — in your Documents folder, typically `Documents/zodiac-charts`.
- **Sessions** — `.zos` files in the same user documents area (see
    [Sessions &amp; Multi-Window](../organizing/sessions.md)).
- **Settings** — kept in your user directory so they persist across sessions.
- **Crash reports** — `%LOCALAPPDATA%\Zodiac\crashes`, as above.

!!! tip
    Because charts and sessions both live in your Documents folder, backing them
    up (or syncing them through OneDrive or another cloud service) is just a matter
    of including that folder. See [The Chart Database](../organizing/database.md).
