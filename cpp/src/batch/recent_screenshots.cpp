#include "batch/recent_screenshots.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace jietuba::batch {

QString RecentShot::name() const { return QFileInfo(path).fileName(); }

QStringList default_suffixes() { return {"png", "jpg", "jpeg", "bmp", "webp"}; }

bool is_screenshot_file(const QString &file_name, qint64 size, const QStringList &suffixes) {
    if (size <= 0 || file_name.startsWith('.') || file_name.startsWith('~')) return false;
    const QFileInfo info(file_name);
    if (!suffixes.contains(info.suffix().toLower())) return false;
    return !info.completeBaseName().endsWith("_original", Qt::CaseInsensitive);
}

QList<RecentShot> scan_recent(const QStringList &folders, int limit, const QStringList &suffixes) {
    QList<RecentShot> shots;
    QSet<QString> seen;
    for (const auto &folder : folders) {
        const QDir dir(folder);
        if (folder.isEmpty() || !dir.exists()) continue;
        for (const auto &info : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
            if (!is_screenshot_file(info.fileName(), info.size(), suffixes)) continue;
            const QString path = QDir::toNativeSeparators(info.absoluteFilePath());
            if (!seen.insert(path.toLower()).second) continue;
            shots.append({path, info.lastModified(), info.size()});
        }
    }
    std::sort(shots.begin(), shots.end(), [](const RecentShot &a, const RecentShot &b) {
        if (a.modified != b.modified) return a.modified > b.modified;
        return a.path.compare(b.path, Qt::CaseInsensitive) > 0;
    });
    if (limit >= 0 && shots.size() > limit) shots.erase(shots.begin() + limit, shots.end());
    return shots;
}

QList<RecentShot> take_latest(const QList<RecentShot> &shots, int count) { return shots.mid(0, std::max(0, count)); }

QStringList paths_of(const QList<RecentShot> &shots) {
    QStringList paths;
    for (const auto &shot : shots) paths.append(shot.path);
    return paths;
}

}
