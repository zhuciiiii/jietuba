#pragma once
// Recent Screenshot Batch Copy: the one object that owns the feature's state.
//
// With the master switch OFF it does nothing: no folder watcher, no hotkey bindings, and every copy request is
// refused without touching the clipboard. Turning the switch on or off only affects this module; jietuba's own
// screenshot, clipboard history and hotkeys are neither started nor stopped by it.
#include "batch/recent_screenshots.h"
#include "clipboard/core/models.h"
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <functional>

namespace jietuba {
class ToolSettingsManager;

namespace batch {

struct CopyOutcome {
    bool ok = false;
    int count = 0;
    QString message; // user-facing, already translated
};

struct HotkeyBinding {
    QString setting; // e.g. "batch_hotkey_10"
    QString label;   // shown when registration fails
    QString hotkey;  // "ctrl+shift+v"
    std::function<void()> callback;
};

class BatchCopyController : public QObject {
    Q_OBJECT
public:
    explicit BatchCopyController(ToolSettingsManager *config, QObject *parent = nullptr);

    bool master_enabled() const;
    // Saves the master switch and starts or stops the folder watcher. The application re-registers the hotkeys.
    void set_master_enabled(bool enabled);
    // Starts or stops the folder watcher so it matches the settings; call after any batch setting changed.
    void apply();
    bool watching() const { return watcher_ != nullptr; }

    // Folders searched for screenshots: the screenshot save folder plus the extra folders.
    QStringList source_folders() const;
    // Empty when screenshots are being saved; otherwise why this feature cannot find new screenshots. It never
    // changes a setting: the user has to turn the automatic saving on.
    QString autosave_problem() const;

    QList<RecentShot> recent(int limit = -1) const;
    CopyOutcome copy_latest(int count);
    CopyOutcome copy_default();
    CopyOutcome copy_paths(const QStringList &paths);
    // The window's selection, kept so a hotkey or the tray can copy it.
    void set_selection(const QStringList &paths) { selection_ = paths; }
    QStringList selection() const { return selection_; }
    CopyOutcome copy_selected();
    // The newest `count` image or file items of the clipboard history, copied as separate files.
    CopyOutcome copy_latest_history_files(int count);
    // The files behind one clipboard history item: its stored PNG for an image, the listed paths for a file item;
    // only files that exist. Empty for text and other items.
    static QStringList history_item_files(const clipboard::ClipboardItem &item, const QString &images_dir);

    // Hotkeys to register: empty unless the master switch and the hotkey switch are on.
    std::vector<HotkeyBinding> hotkey_bindings();

    // Names of the other jietuba or batch hotkeys that use `hotkey` (display labels); empty when free.
    // `own_setting` is the batch setting being edited and is skipped. Invalid or system-reserved keys are reported
    // as well, under the label "!".
    static QStringList hotkey_conflicts(ToolSettingsManager *config, const QString &own_setting, const QString &hotkey);
    static QStringList hotkey_settings();

signals:
    void recent_changed();
    void state_changed();
    // Ask the application to show the Recent Screenshots window.
    void window_requested();
    // A result worth telling the user (copy done or failed); the application shows it as a notification.
    void notice(const QString &title, const QString &text, bool error);

private:
    void start_watching();
    void stop_watching();
    void rescan_soon();
    CopyOutcome finish(CopyOutcome outcome);

    ToolSettingsManager *config_;
    QFileSystemWatcher *watcher_ = nullptr;
    QTimer *debounce_ = nullptr;
    QStringList selection_;
};

}
}
