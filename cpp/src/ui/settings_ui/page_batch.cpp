// Settings page "Batch Copy": the master switch, the independent switches, the hotkeys and the folders of the
// Recent Screenshot Batch Copy feature (src/batch/). Every control applies at once through the controller, so the
// master switch really starts or stops the module's watcher and hotkeys without a restart; none of it is
// registered with the dialog's save tracking, and no other setting is read or written.
#include "batch/batch_copy_controller.h"
#include "core/ui_scale.h"
#include "settings/tool_settings.h"
#include "ui/dialogs.h"
#include "ui/fluent_lite/buttons.h"
#include "ui/fluent_lite/inputs.h"
#include "ui/hotkey_edit.h"
#include "ui/settings_ui/components.h"
#include "ui/settings_ui/dialog.h"
#include "ui/settings_ui/pages.h"
#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

namespace jietuba {
namespace {
using batch::BatchCopyController;

BatchCopyController *batch_controller() {
    return qobject_cast<BatchCopyController *>(qApp->property("_batch_copy_controller").value<QObject *>());
}

struct HotkeyRow {
    const char *setting;
    const char *title;
    const char *desc;
};
const HotkeyRow HOTKEY_ROWS[] = {
    {"batch_hotkey", "Batch Copy Hotkey", "Copies the latest screenshots (the default count above) to the clipboard. Default: Ctrl+Shift+V."},
    {"batch_hotkey_5", "Copy Latest 5", "Optional hotkey, empty by default."},
    {"batch_hotkey_10", "Copy Latest 10", "Optional hotkey, empty by default."},
    {"batch_hotkey_15", "Copy Latest 15", "Optional hotkey, empty by default."},
    {"batch_hotkey_20", "Copy Latest 20", "Optional hotkey, empty by default."},
    {"batch_hotkey_selected", "Copy Selected", "Copies the screenshots selected in the Recent Screenshots window. Optional, empty by default."},
};

QString conflict_text(SettingsDialog *dialog, const QStringList &conflicts) {
    if (conflicts.contains("!")) return dialog->tr_text("This key combination is invalid or reserved by the system.");
    return dialog->tr_text("Already used by: ") + conflicts.join(", ");
}

void apply_now() {
    if (auto *controller = batch_controller()) controller->apply();
}

void add_switch(SettingsDialog *dialog, SettingsCardGroup *group, const IconSource &icon, const char *title, const char *desc, bool value,
                std::function<void(bool)> set) {
    auto *card = new SwitchSettingCard(icon, dialog->tr_text(title), dialog->tr_text(desc), group);
    card->setChecked(value);
    QObject::connect(card, &SwitchSettingCard::checkedChanged, card, [set = std::move(set)](bool checked) {
        set(checked);
        apply_now();
    });
    group->addSettingCard(card);
}

void add_count_combo(SettingsDialog *dialog, SettingsCardGroup *group, const char *title, const char *desc, const QList<int> &values, int current,
                     std::function<void(int)> set) {
    auto *card = new SettingCard(FluentIcon::Setting, dialog->tr_text(title), dialog->tr_text(desc), group);
    auto *combo = new ComboBox(card);
    for (int value : values) combo->addItem(QString::number(value), value);
    int index = combo->findData(current);
    if (index < 0) {
        combo->addItem(QString::number(current), current);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
    QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [combo, set = std::move(set)](int i) {
        set(combo->itemData(i).toInt());
        apply_now();
    });
    card->addControl(combo);
    group->addSettingCard(card);
}

void build_main_section(SettingsDialog *dialog, SettingsCardGroup *group) {
    auto *config = dialog->config_manager;
    add_switch(dialog, group, FluentIcon::PowerButton, "Batch Copy Master Switch",
               "Off by default. While off, batch copy watches no folder, registers no hotkey and never changes the clipboard. jietuba's own features are not affected.",
               config->get_batch_master_enabled(), [config](bool on) { config->set_batch_master_enabled(on); });
    add_switch(dialog, group, FluentIcon::Folder, "Screenshot Monitoring",
               "Watch the screenshot folders so the Recent Screenshots window updates by itself. Needs the master switch.",
               config->get_batch_monitor_enabled(), [config](bool on) { config->set_batch_monitor_enabled(on); });
    add_switch(dialog, group, FluentIcon::Edit, "Batch Copy Hotkeys", "Register the batch copy hotkeys below. Needs the master switch.",
               config->get_batch_hotkey_enabled(), [config](bool on) { config->set_batch_hotkey_enabled(on); });
    add_switch(dialog, group, FluentIcon::Send, "Batch Copy Notifications", "Tell me when screenshots were copied or copying failed.",
               config->get_batch_notifications(), [config](bool on) { config->set_batch_notifications(on); });
    add_count_combo(dialog, group, "Default Batch Size", "How many screenshots the main hotkey and the tray entry copy.", {5, 10, 15, 20},
                    config->get_batch_default_count(), [config](int n) { config->set_batch_default_count(n); });
    add_count_combo(dialog, group, "Recent Screenshot History", "How many recent screenshots the window lists.", {20, 50, 100, 200},
                    config->get_batch_history_limit(), [config](int n) { config->set_batch_history_limit(n); });

    // Windows startup is one setting for the whole application.
    auto *startup = new SettingCard(FluentIcon::PowerButton, dialog->tr_text("Windows Startup"),
                                    dialog->tr_text("Batch copy runs inside jietuba, so it starts with jietuba. Launch on startup is set on the Other page."), group);
    auto *startup_button = new PushButton(dialog->tr_text("Open Other Settings"), startup);
    QObject::connect(startup_button, &QPushButton::clicked, dialog, [dialog] { dialog->on_nav_changed(6, "other"); });
    startup->addControl(startup_button);
    group->addSettingCard(startup);

    auto *window = new SettingCard(FluentIcon::Camera, dialog->tr_text("Recent Screenshots"),
                                   dialog->tr_text("Open the window with the latest screenshots, select some and copy them."), group);
    auto *open_button = new PrimaryPushButton(dialog->tr_text("Open Window"), window);
    QObject::connect(open_button, &QPushButton::clicked, window, [] {
        if (auto *controller = batch_controller()) emit controller->window_requested();
    });
    window->addControl(open_button);
    group->addSettingCard(window);
}

void build_hotkey_section(SettingsDialog *dialog, SettingsCardGroup *group) {
    auto *config = dialog->config_manager;
    for (const auto &row : HOTKEY_ROWS) {
        auto *card = new SettingCard(FluentIcon::Edit, dialog->tr_text(row.title), dialog->tr_text(row.desc), group);
        auto *edit = new HotkeyEdit(card);
        edit->setText(config->get_app_setting(row.setting).toString());
        const QString setting = row.setting;
        QObject::connect(edit, &HotkeyEdit::textChanged, edit, [dialog, config, edit, setting](const QString &text) {
            const QString value = text.trimmed().toLower();
            if (value.endsWith("+")) return; // unfinished prefix
            const auto conflicts = BatchCopyController::hotkey_conflicts(config, setting, value);
            if (!conflicts.isEmpty()) {
                edit->set_validation_error(conflict_text(dialog, conflicts));
                return; // never saved while it clashes
            }
            edit->set_validation_error();
            config->set_app_setting(setting, value);
            apply_now();
        });
        card->addControl(edit);
        group->addSettingCard(card);
    }
}

void build_folder_section(SettingsDialog *dialog, SettingsCardGroup *group) {
    auto *config = dialog->config_manager;
    auto *source = new SettingCard(FluentIcon::Folder, dialog->tr_text("Screenshot Folder"),
                                   dialog->tr_text("Batch copy uses the screenshots jietuba saves automatically to this folder. Change it on the Screenshot page."), group);
    auto *path = new QLabel(QDir::toNativeSeparators(config->get_screenshot_save_path()), source);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    source->addControl(path);
    group->addSettingCard(source);

    auto *warning = new SettingCard(FluentIcon::Info, dialog->tr_text("Automatic Saving"), QString(), group);
    auto refresh = [dialog, config, warning] {
        warning->setContent(config->get_screenshot_save_enabled()
                                ? dialog->tr_text("Automatic screenshot saving is on.")
                                : dialog->tr_text("Automatic screenshot saving is OFF. Batch copy needs the saved screenshot files: turn on auto-save on the Screenshot page. "
                                                  "Nothing was changed for you."));
    };
    refresh();
    group->addSettingCard(warning);

    auto *extra = new SettingCard(FluentIcon::Folder, dialog->tr_text("Extra Folders"), QString(), group);
    auto describe = [dialog, config, extra] {
        const auto folders = config->get_batch_extra_folders();
        extra->setContent(folders.isEmpty() ? dialog->tr_text("None. Screenshots saved elsewhere can be added here.") : folders.join("\n"));
    };
    describe();
    auto *add = new PushButton(dialog->tr_text("Add Folder"), extra);
    auto *clear = new PushButton(dialog->tr_text("Clear"), extra);
    QObject::connect(add, &QPushButton::clicked, dialog, [dialog, config, describe] {
        const auto directory = get_existing_directory(dialog, dialog->tr_text("Choose a screenshot folder"), QDir::homePath());
        if (directory.isEmpty()) return;
        auto folders = config->get_batch_extra_folders();
        const QString native = QDir::toNativeSeparators(directory);
        if (!folders.contains(native, Qt::CaseInsensitive)) folders << native;
        config->set_batch_extra_folders(folders);
        describe();
        apply_now();
    });
    QObject::connect(clear, &QPushButton::clicked, dialog, [config, describe] {
        config->set_batch_extra_folders({});
        describe();
        apply_now();
    });
    extra->addControl(add);
    extra->addControl(clear);
    group->addSettingCard(extra);
}
}

// Recent Screenshot Batch Copy.
QWidget *create_batch_page(SettingsDialog *dialog) {
    auto *scroll = page_scroll_area(dialog);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    auto *view = new QWidget;
    set_own_style(view, "background: transparent;");
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(0, 0, dialog_scaled(10), 0);
    layout->setSpacing(dialog_scaled(14));
    const std::pair<const char *, void (*)(SettingsDialog *, SettingsCardGroup *)> sections[] = {
        {"Batch Copy", build_main_section},
        {"Batch Copy Hotkeys", build_hotkey_section},
        {"Screenshot Source", build_folder_section},
    };
    for (const auto &[title, build] : sections) {
        auto *group = new SettingsCardGroup(dialog->tr_text(title), view, "clipboard");
        build(dialog, group);
        layout->addWidget(group);
    }
    layout->addStretch();
    scroll->setWidget(view);
    return scroll;
}
}
