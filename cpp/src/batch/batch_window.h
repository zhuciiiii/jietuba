#pragma once
// Recent Screenshot Batch Copy: the "Recent Screenshots" window — newest-first thumbnails, selection by hand or
// "latest N", and the copy buttons.
#include "batch/batch_copy_controller.h"
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPointer>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace jietuba::batch {

class BatchWindow : public QWidget {
    Q_OBJECT
public:
    explicit BatchWindow(BatchCopyController *controller, QWidget *parent = nullptr);
    // Reloads the list from the screenshot folders.
    void reload();
    // Selects the newest `count` entries (clears the selection first).
    void select_latest(int count);
    QStringList selected_paths() const;
    int item_count() const;
    // Called from the thumbnail worker through a queued call.
    Q_INVOKABLE void thumbnail_ready(const QString &path, const QImage &image);

signals:
    void open_settings_requested();

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void apply_style();
    void retranslate();
    void update_state();
    void request_thumbnail(const QString &path, const QDateTime &modified);

    BatchCopyController *controller_;
    QLabel *status_ = nullptr;
    QLabel *banner_ = nullptr;
    QLabel *count_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *master_button_ = nullptr;
    QPushButton *copy_buttons_[4] = {};
    QPushButton *copy_selected_ = nullptr;
    QPushButton *select_button_ = nullptr;
    QPushButton *clear_button_ = nullptr;
    QPushButton *refresh_button_ = nullptr;
    QPushButton *folder_button_ = nullptr;
    QPushButton *settings_button_ = nullptr;
    QHash<QString, QIcon> icons_; // key: path|modified
    bool reloading_ = false;
};

}
