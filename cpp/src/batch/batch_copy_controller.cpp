#include "batch/batch_copy_controller.h"
#include "batch/batch_clipboard.h"
#include "clipboard/core/manager.h"
#include "core/clipboard_utils.h"
#include "core/global_hotkey.h"
#include "core/i18n.h"
#include "core/logger.h"
#include "settings/tool_settings.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace jietuba::batch {
namespace {
QString tr_text(const char *text) { return i18n_tr(text, "BatchCopy"); }

struct HotkeyDef {
    const char *setting;
    const char *label;
    int count; // 0 = default count, -1 = selected
};
const HotkeyDef HOTKEYS[] = {
    {"batch_hotkey", "Batch Copy (default count)", 0}, {"batch_hotkey_5", "Batch Copy Latest 5", 5},
    {"batch_hotkey_10", "Batch Copy Latest 10", 10},   {"batch_hotkey_15", "Batch Copy Latest 15", 15},
    {"batch_hotkey_20", "Batch Copy Latest 20", 20},   {"batch_hotkey_selected", "Batch Copy Selected", -1},
    {"batch_hotkey_history", "Copy Latest 5 History Items", -2},
};

QString normalized(const QString &hotkey) { return hotkey.trimmed().toLower(); }

QString function_label(const QString &function) {
    if (function == "screenshot") return tr_text("Screenshot");
    if (function == "clipboard") return tr_text("Clipboard");
    if (function == "pin_clipboard") return tr_text("Pin Clipboard Image");
    if (function == "translation") return tr_text("Translation");
    if (function == "screen_annotation") return tr_text("Screen Annotation");
    return function;
}
}

BatchCopyController::BatchCopyController(ToolSettingsManager *config, QObject *parent) : QObject(parent), config_(config) {
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(400);
    connect(debounce_, &QTimer::timeout, this, &BatchCopyController::recent_changed);
}

bool BatchCopyController::master_enabled() const { return config_->get_batch_master_enabled(); }

void BatchCopyController::set_master_enabled(bool enabled) {
    config_->set_batch_master_enabled(enabled);
    apply();
}

void BatchCopyController::apply() {
    if (master_enabled() && config_->get_batch_monitor_enabled()) start_watching();
    else stop_watching();
    emit state_changed();
    emit recent_changed();
}

QStringList BatchCopyController::source_folders() const {
    QStringList folders;
    const QString save_path = config_->get_screenshot_save_path();
    if (!save_path.isEmpty()) folders << save_path;
    for (const auto &extra : config_->get_batch_extra_folders())
        if (!extra.isEmpty()) folders << extra;
    return folders;
}

QString BatchCopyController::autosave_problem() const {
    if (!config_->get_screenshot_save_enabled())
        return tr_text("Automatic screenshot saving is turned off. Batch copy uses the saved screenshot files, so turn on "
                       "\"Auto-save screenshots\" in the Screenshot settings. Nothing has been changed for you.");
    return {};
}

QList<RecentShot> BatchCopyController::recent(int limit) const {
    const int cap = limit >= 0 ? limit : config_->get_batch_history_limit();
    return scan_recent(source_folders(), cap);
}

void BatchCopyController::start_watching() {
    stop_watching();
    watcher_ = new QFileSystemWatcher(this);
    for (const auto &folder : source_folders())
        if (QFileInfo(folder).isDir()) watcher_->addPath(folder);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { rescan_soon(); });
    log_info(QString("Batch copy: watching %1 folder(s)").arg(watcher_->directories().size()), "BatchCopy");
}

void BatchCopyController::stop_watching() {
    if (!watcher_) return;
    // An event already queued must not restart the timer once the module is off.
    disconnect(watcher_, nullptr, this, nullptr);
    watcher_->deleteLater();
    watcher_ = nullptr;
    debounce_->stop();
    log_info("Batch copy: folder watching stopped", "BatchCopy");
}

void BatchCopyController::rescan_soon() {
    if (watcher_) debounce_->start();
}

CopyOutcome BatchCopyController::finish(CopyOutcome outcome) {
    emit notice(outcome.ok ? tr_text("Batch Copy") : tr_text("Batch Copy failed"), outcome.message, !outcome.ok);
    return outcome;
}

CopyOutcome BatchCopyController::copy_latest(int count) {
    if (!master_enabled()) {
        // Refused silently apart from the message: the clipboard is not touched.
        return finish({false, 0, tr_text("Batch copy is turned off. Turn on its master switch first.")});
    }
    const auto shots = take_latest(recent(std::max(count, 1)), count);
    if (shots.isEmpty()) {
        const QString problem = autosave_problem();
        return finish({false, 0, problem.isEmpty() ? tr_text("No saved screenshots were found in the screenshot folder.") : problem});
    }
    return copy_paths(paths_of(shots));
}

CopyOutcome BatchCopyController::copy_default() { return copy_latest(config_->get_batch_default_count()); }

CopyOutcome BatchCopyController::copy_selected() {
    if (selection_.isEmpty() && master_enabled())
        return finish({false, 0, tr_text("No screenshots are selected. Select some in the Recent Screenshots window first.")});
    return copy_paths(selection_);
}

CopyOutcome BatchCopyController::copy_paths(const QStringList &paths) {
    if (!master_enabled()) return finish({false, 0, tr_text("Batch copy is turned off. Turn on its master switch first.")});
    QStringList existing;
    for (const auto &path : paths)
        if (QFileInfo::exists(path)) existing << path;
    if (existing.isEmpty()) return finish({false, 0, tr_text("The screenshot files no longer exist.")});
    // A capture's own clipboard write may still be queued; let it land first so it cannot overwrite ours.
    wait_for_clipboard_writes(0.5);
    const auto result = copy_files_to_clipboard(existing);
    if (!result.ok) {
        log_warning("Batch copy failed: " + result.error, "BatchCopy");
        return finish({false, 0, tr_text("Could not copy to the clipboard: ") + result.error});
    }
    log_info(QString("Batch copy: %1 file(s) placed on the clipboard").arg(result.count), "BatchCopy");
    return finish({true, result.count, tr_text("%1 screenshots copied. Press Ctrl+V to paste.").arg(result.count)});
}

QStringList BatchCopyController::history_item_files(const clipboard::ClipboardItem &item, const QString &images_dir) {
    QStringList files;
    if (item.content_type == "image" && item.image_id && !item.image_id->isEmpty() && !images_dir.isEmpty()) {
        files << QDir(images_dir).filePath(*item.image_id + ".png");
    } else if (item.content_type == "file") {
        for (const auto &entry : QJsonDocument::fromJson(item.content.toUtf8()).object().value("files").toArray()) files << entry.toString();
    }
    QStringList existing;
    for (const auto &file : files)
        if (!file.isEmpty() && QFileInfo::exists(file)) existing << QDir::toNativeSeparators(file);
    return existing;
}

CopyOutcome BatchCopyController::copy_latest_history_files(int count) {
    if (!master_enabled()) return finish({false, 0, tr_text("Batch copy is turned off. Turn on its master switch first.")});
    auto &manager = clipboard::ClipboardManager::instance();
    const QString images_dir = manager.get_images_dir().value_or(QString());
    QStringList paths;
    int items = 0;
    for (const auto &item : manager.get_history(0, 200)) {
        if (items >= count) break;
        const auto files = history_item_files(item, images_dir);
        if (files.isEmpty()) continue;
        paths += files;
        ++items;
    }
    if (paths.isEmpty()) return finish({false, 0, tr_text("The clipboard history has no images or files to copy.")});
    return copy_paths(paths);
}

std::vector<HotkeyBinding> BatchCopyController::hotkey_bindings() {
    std::vector<HotkeyBinding> bindings;
    if (!master_enabled() || !config_->get_batch_hotkey_enabled()) return bindings;
    for (const auto &def : HOTKEYS) {
        const QString hotkey = config_->get_app_setting(def.setting).toString().trimmed();
        if (hotkey.isEmpty()) continue;
        std::function<void()> callback;
        if (def.count > 0) callback = [this, n = def.count] { copy_latest(n); };
        else if (def.count == 0) callback = [this] { copy_default(); };
        else if (def.count == -2) callback = [this] { copy_latest_history_files(5); };
        else callback = [this] { copy_selected(); };
        bindings.push_back({def.setting, tr_text(def.label), hotkey, callback});
    }
    return bindings;
}

QStringList BatchCopyController::hotkey_settings() {
    QStringList names;
    for (const auto &def : HOTKEYS) names << def.setting;
    return names;
}

QStringList BatchCopyController::hotkey_conflicts(ToolSettingsManager *config, const QString &own_setting, const QString &hotkey) {
    QStringList conflicts;
    const QString wanted = normalized(hotkey);
    if (wanted.isEmpty() || wanted.endsWith('+')) return conflicts;
    const auto route = global_hotkey::route(wanted);
    if (route == global_hotkey::Route::Invalid || route == global_hotkey::Route::Reserved) return {"!"};
    for (const auto &entry : GLOBAL_HOTKEY_FUNCTIONS()) {
        const auto keys = config->get_global_hotkeys(entry.function);
        for (const auto &key : keys)
            if (normalized(key) == wanted) {
                conflicts << function_label(entry.function);
                break;
            }
    }
    for (const auto &def : HOTKEYS) {
        if (own_setting == QLatin1String(def.setting)) continue;
        if (normalized(config->get_app_setting(def.setting).toString()) == wanted) conflicts << tr_text(def.label);
    }
    return conflicts;
}

}
