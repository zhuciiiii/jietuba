#include "batch/batch_window.h"
#include "core/i18n.h"
#include "core/ui_theme.h"
#include "ui/fluent_lite/buttons.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QPixmap>
#include <QEvent>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

namespace jietuba::batch {
namespace {
QString tr_text(const char *text) { return i18n_tr(text, "BatchCopy"); }
constexpr int THUMB_W = 176, THUMB_H = 110;
const int COUNTS[4] = {5, 10, 15, 20};

// Decodes one thumbnail off the UI thread; the original file is only read.
class ThumbTask : public QRunnable {
public:
    ThumbTask(QPointer<BatchWindow> window, QString path) : window_(std::move(window)), path_(std::move(path)) {}
    void run() override {
        QImageReader reader(path_);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid()) reader.setScaledSize(size.scaled(THUMB_W * 2, THUMB_H * 2, Qt::KeepAspectRatio));
        const QImage image = reader.read();
        if (!window_) return;
        QMetaObject::invokeMethod(window_.data(), "thumbnail_ready", Qt::QueuedConnection, Q_ARG(QString, path_), Q_ARG(QImage, image));
    }
private:
    QPointer<BatchWindow> window_;
    QString path_;
};
}

BatchWindow::BatchWindow(BatchCopyController *controller, QWidget *parent) : QWidget(parent), controller_(controller) {
    setWindowFlag(Qt::Window);
    resize(980, 640);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto *top = new QHBoxLayout;
    status_ = new QLabel(this);
    master_button_ = new PushButton({}, this);
    top->addWidget(status_, 1);
    top->addWidget(master_button_);
    layout->addLayout(top);

    banner_ = new QLabel(this);
    banner_->setWordWrap(true);
    layout->addWidget(banner_);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    for (int i = 0; i < 4; ++i) {
        copy_buttons_[i] = new PrimaryPushButton({}, this);
        const int count = COUNTS[i];
        connect(copy_buttons_[i], &QPushButton::clicked, this, [this, count] { controller_->copy_latest(count); });
        buttons->addWidget(copy_buttons_[i]);
    }
    copy_selected_ = new PrimaryPushButton({}, this);
    connect(copy_selected_, &QPushButton::clicked, this, [this] {
        controller_->set_selection(selected_paths());
        controller_->copy_selected();
    });
    buttons->addWidget(copy_selected_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto *tools = new QHBoxLayout;
    tools->setSpacing(8);
    select_button_ = new PushButton({}, this);
    clear_button_ = new PushButton({}, this);
    refresh_button_ = new PushButton({}, this);
    folder_button_ = new PushButton({}, this);
    settings_button_ = new PushButton({}, this);
    count_ = new QLabel(this);
    for (auto *b : {select_button_, clear_button_, refresh_button_, folder_button_, settings_button_}) tools->addWidget(b);
    tools->addStretch(1);
    tools->addWidget(count_);
    layout->addLayout(tools);

    list_ = new QListWidget(this);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize({THUMB_W, THUMB_H});
    list_->setGridSize({THUMB_W + 28, THUMB_H + 54});
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setSelectionMode(QAbstractItemView::MultiSelection);
    list_->setWordWrap(true);
    list_->setUniformItemSizes(true);
    list_->setSpacing(4);
    layout->addWidget(list_, 1);

    auto *menu = new QMenu(select_button_);
    for (int count : COUNTS) {
        auto *action = menu->addAction(QString());
        action->setData(count);
        connect(action, &QAction::triggered, this, [this, count] { select_latest(count); });
    }
    menu->addSeparator();
    connect(menu->addAction(QString()), &QAction::triggered, list_, &QListWidget::selectAll);
    select_button_->setMenu(menu);

    connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
        if (reloading_) return;
        controller_->set_selection(selected_paths());
        update_state();
    });
    connect(clear_button_, &QPushButton::clicked, list_, &QListWidget::clearSelection);
    connect(refresh_button_, &QPushButton::clicked, this, [this] { reload(); });
    connect(folder_button_, &QPushButton::clicked, this, [this] {
        const auto folders = controller_->source_folders();
        if (!folders.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(folders.first()));
    });
    connect(settings_button_, &QPushButton::clicked, this, &BatchWindow::open_settings_requested);
    connect(master_button_, &QPushButton::clicked, this, [this] { controller_->set_master_enabled(!controller_->master_enabled()); });
    connect(controller_, &BatchCopyController::state_changed, this, [this] { update_state(); });
    connect(controller_, &BatchCopyController::recent_changed, this, [this] {
        if (isVisible()) reload();
    });
    connect(get_ui_theme(), &UIThemeManager::theme_changed, this, [this] { apply_style(); });
    apply_style();
    retranslate();
    update_state();
}

void BatchWindow::apply_style() {
    const auto &t = get_ui_theme()->tokens();
    setStyleSheet(QString(R"(
        BatchWindow { background-color: %1; }
        QLabel { color: %2; background: transparent; }
        QListWidget { background-color: %3; border: 1px solid %4; border-radius: 6px; color: %2; outline: none; padding: 6px; }
        QListWidget::item { border-radius: 6px; padding: 4px; color: %2; }
        QListWidget::item:selected { background-color: %5; color: %2; border: 1px solid %6; }
        QListWidget::item:hover { background-color: %7; }
    )")
                      .arg(t.window, t.text, t.surface, t.border, t.accent_soft, t.accent, t.surface_hover));
}

void BatchWindow::retranslate() {
    setWindowTitle(tr_text("Recent Screenshots"));
    for (int i = 0; i < 4; ++i) copy_buttons_[i]->setText(tr_text("Copy Latest %1").arg(COUNTS[i]));
    copy_selected_->setText(tr_text("Copy Selected"));
    select_button_->setText(tr_text("Select Latest..."));
    clear_button_->setText(tr_text("Clear Selection"));
    refresh_button_->setText(tr_text("Refresh"));
    folder_button_->setText(tr_text("Open Folder"));
    settings_button_->setText(tr_text("Batch Copy Settings"));
    const auto actions = select_button_->menu()->actions();
    for (auto *action : actions) {
        if (action->isSeparator()) continue;
        if (action->data().isValid()) action->setText(tr_text("Latest %1").arg(action->data().toInt()));
        else action->setText(tr_text("Select All"));
    }
    update_state();
}

void BatchWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) retranslate();
    QWidget::changeEvent(event);
}

void BatchWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    retranslate();
    reload();
}

void BatchWindow::update_state() {
    const bool on = controller_->master_enabled();
    status_->setText(on ? tr_text("Batch copy is ON") : tr_text("Batch copy is OFF - no folder is watched, the hotkey is off and the clipboard is not touched."));
    master_button_->setText(on ? tr_text("Turn Off") : tr_text("Turn On"));
    const QString problem = controller_->autosave_problem();
    banner_->setText(problem);
    banner_->setVisible(!problem.isEmpty());
    banner_->setStyleSheet("QLabel { color: #b45309; font-weight: 600; }");
    for (auto *b : copy_buttons_) b->setEnabled(on);
    const int selected = list_ ? list_->selectedItems().size() : 0;
    copy_selected_->setEnabled(on && selected > 0);
    count_->setText(tr_text("%1 selected, %2 shown").arg(selected).arg(list_ ? list_->count() : 0));
}

int BatchWindow::item_count() const { return list_->count(); }

QStringList BatchWindow::selected_paths() const {
    QStringList paths;
    for (int row = 0; row < list_->count(); ++row) // list order = newest first
        if (list_->item(row)->isSelected()) paths << list_->item(row)->data(Qt::UserRole).toString();
    return paths;
}

void BatchWindow::select_latest(int count) {
    list_->clearSelection();
    for (int row = 0; row < std::min(count, list_->count()); ++row) list_->item(row)->setSelected(true);
}

void BatchWindow::reload() {
    reloading_ = true;
    const QStringList previous = selected_paths();
    list_->clear();
    const auto shots = controller_->recent();
    QIcon placeholder;
    for (const auto &shot : shots) {
        auto *item = new QListWidgetItem(list_);
        item->setText(shot.name() + "\n" + shot.modified.toString("yyyy-MM-dd HH:mm:ss"));
        item->setData(Qt::UserRole, shot.path);
        item->setToolTip(shot.path);
        item->setSizeHint(list_->gridSize());
        const QString key = shot.path + "|" + QString::number(shot.modified.toMSecsSinceEpoch());
        if (icons_.contains(key)) item->setIcon(icons_.value(key));
        else request_thumbnail(shot.path, shot.modified);
        if (previous.contains(shot.path)) item->setSelected(true);
    }
    reloading_ = false;
    controller_->set_selection(selected_paths());
    update_state();
}

void BatchWindow::request_thumbnail(const QString &path, const QDateTime &modified) {
    (void)modified;
    QThreadPool::globalInstance()->start(new ThumbTask(QPointer<BatchWindow>(this), path));
}

void BatchWindow::thumbnail_ready(const QString &path, const QImage &image) {
    if (image.isNull()) return;
    const QIcon icon(QPixmap::fromImage(image));
    for (int row = 0; row < list_->count(); ++row) {
        auto *item = list_->item(row);
        if (item->data(Qt::UserRole).toString() != path) continue;
        item->setIcon(icon);
        icons_.insert(path + "|" + QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch()), icon);
    }
}

}
