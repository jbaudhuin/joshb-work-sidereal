# Command-Line Options

Zodiac normally runs as a [single instance](../organizing/sessions.md): launching
it again while it is already open just raises the existing window. When you want a
fresh workspace, or a specific session, or **more than one instance running at
once**, start the program with one of these flags.

| Option | What it does |
| --- | --- |
| `--new` | Start a fresh session without restoring your previous state. Also accepted as `-new` and `/new`. |
| `--load-session <file>` | Launch with a specific session file (a `.zos` file). |

Both options allow **multiple concurrent instances** — so you can, for example,
keep your main workspace open and start a second, empty one with `--new`, or open
a saved research session alongside it with `--load-session`.

!!! tip
    You do not need the command line for everyday multi-window work. Double-clicking
    a `.zos` file opens that session, and right-clicking a session in the database
    offers **Open in New Window**. See
    [Sessions &amp; Multi-Window](../organizing/sessions.md) for those routes.
