#pragma once
// Recent Screenshot Batch Copy: finds the newest saved screenshot files. Pure file-system logic with no
// dependency on the rest of the application, so it can be tested alone.
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace jietuba::batch {

struct RecentShot {
    QString path;       // absolute path, native separators
    QDateTime modified; // sort key: newest first
    qint64 size = 0;
    QString name() const;
};

// Image suffixes that count as screenshots (lower case, no dot).
QStringList default_suffixes();

// Whether a file in a screenshot folder is a screenshot of its own. Skips empty files, hidden files and the
// "<name>_original" copies jietuba writes beside an annotated screenshot, which would otherwise count twice.
bool is_screenshot_file(const QString &file_name, qint64 size, const QStringList &suffixes);

// The newest `limit` screenshots of the folders (not recursive; missing folders are ignored), newest first by
// modification time, ties broken by file name (jietuba names carry the capture time), no path twice.
QList<RecentShot> scan_recent(const QStringList &folders, int limit, const QStringList &suffixes = default_suffixes());

// The first `count` entries of a newest-first list.
QList<RecentShot> take_latest(const QList<RecentShot> &shots, int count);
QStringList paths_of(const QList<RecentShot> &shots);

}
