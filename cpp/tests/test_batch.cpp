// Recent Screenshot Batch Copy: file selection, CF_HDROP clipboard writing, master switch and hotkeys.
#include "batch/batch_clipboard.h"
#include "batch/batch_copy_controller.h"
#include "batch/recent_screenshots.h"
#include "settings/tool_settings.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace jietuba;
using namespace jietuba::batch;

namespace {
QString make_png(const QDir &dir, const QString &name, const QDateTime &modified, int shade = 0) {
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(QColor(shade % 256, 60, 120));
    const QString path = dir.filePath(name);
    if (!image.save(path)) qFatal("cannot write %s", qPrintable(path));
    QFile file(path);
    file.open(QIODevice::ReadWrite);
    file.setFileTime(modified, QFileDevice::FileModificationTime);
    return QDir::toNativeSeparators(path);
}

QByteArray sha(const QString &path) {
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}
}

class BatchTests : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QVERIFY(dir_.isValid());
        QDir(dir_.path()).removeRecursively();
        QDir().mkpath(dir_.path());
        settings_file_ = dir_.filePath("settings.ini");
    }

    void latestTenAreNewestFirst() {
        const QDateTime base = QDateTime::currentDateTime().addDays(-1);
        QStringList expected;
        for (int i = 0; i < 25; ++i) make_png(QDir(dir_.path()), QString("shot_%1.png").arg(i, 2, 10, QChar('0')), base.addSecs(i * 60), i);
        for (int i = 24; i >= 15; --i) expected << QDir::toNativeSeparators(QDir(dir_.path()).filePath(QString("shot_%1.png").arg(i)));
        const auto shots = scan_recent({dir_.path()}, 50);
        QCOMPARE(shots.size(), 25);
        QCOMPARE(paths_of(take_latest(shots, 10)), expected);
        for (int i = 1; i < shots.size(); ++i) QVERIFY(shots[i - 1].modified >= shots[i].modified);
        QCOMPARE(take_latest(shots, 5).size(), 5);
        QCOMPARE(take_latest(shots, 15).size(), 15);
        QCOMPARE(take_latest(shots, 20).size(), 20);
        QCOMPARE(scan_recent({dir_.path()}, 7).size(), 7); // history limit
    }

    void skipsOriginalsEmptyFilesAndOtherTypes() {
        QDir dir(dir_.path());
        const auto now = QDateTime::currentDateTime();
        make_png(dir, "a.png", now);
        make_png(dir, "a_original.png", now.addSecs(5));
        QFile(dir.filePath("empty.png")).open(QIODevice::WriteOnly);
        QFile note(dir.filePath("notes.txt"));
        note.open(QIODevice::WriteOnly);
        note.write("x");
        note.close();
        make_png(dir, ".hidden.png", now);
        const auto shots = scan_recent({dir_.path(), dir_.filePath("missing-folder")}, 50);
        QCOMPARE(shots.size(), 1);
        QCOMPARE(shots.first().name(), QString("a.png"));
    }

    void dropFilesRoundTripsUnicodePaths() {
        const QStringList paths = {"C:\\shots\\a b.png", "D:\\截图\\图片 1.png"};
        const QByteArray data = build_dropfiles(paths);
        QCOMPARE(data.left(4), QByteArray::fromHex("14000000")); // offset of the list = 20
        QVERIFY(data.endsWith(QByteArray(4, '\0')));
        QCOMPARE(parse_dropfiles(data), paths);
    }

    void tenFilesGoToTheClipboardAsOneFileList() {
        QDir dir(dir_.path());
        QStringList files;
        QMap<QString, QByteArray> hashes;
        for (int i = 0; i < 10; ++i) {
            files << make_png(dir, QString("c%1.png").arg(i), QDateTime::currentDateTime(), i * 20);
            hashes[files.last()] = sha(files.last());
        }
        const auto result = copy_files_to_clipboard(files);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.count, 10);
        QCOMPARE(read_clipboard_files(), files);
        QCOMPARE(read_clipboard_drop_effect(), 1); // copy, never move
        QCOMPARE(read_clipboard_history_flag(), 0); // keeps the batch out of the clipboard history
        for (const auto &file : files) QCOMPARE(sha(file), hashes[file]); // originals untouched

        // A second copy replaces the first one completely.
        const QStringList second = files.mid(0, 3);
        QVERIFY(copy_files_to_clipboard(second).ok);
        QCOMPARE(read_clipboard_files(), second);
    }

    void historyItemsMapToTheirFiles() {
        QDir dir(dir_.path());
        const QString png = make_png(dir, "abc123.png", QDateTime::currentDateTime());
        clipboard::ClipboardItem image;
        image.content_type = "image";
        image.image_id = QString("abc123");
        QCOMPARE(BatchCopyController::history_item_files(image, dir_.path()), QStringList{png});
        image.image_id = QString("missing");
        QVERIFY(BatchCopyController::history_item_files(image, dir_.path()).isEmpty());
        clipboard::ClipboardItem files;
        files.content_type = "file";
        files.content = QString("{\"files\": [\"%1\", \"%2\"]}").arg(QString(png).replace("\\", "\\\\"), "C:\no\such.png");
        QCOMPARE(BatchCopyController::history_item_files(files, dir_.path()), QStringList{png});
        clipboard::ClipboardItem text;
        text.content_type = "text";
        text.content = "hello";
        QVERIFY(BatchCopyController::history_item_files(text, dir_.path()).isEmpty());
    }

    void emptyListIsRefused() {
        QVERIFY(!copy_files_to_clipboard({}).ok);
    }

    void masterSwitchIsOffByDefaultAndBlocksEverything() {
        QSettings stored(settings_file_, QSettings::IniFormat);
        ToolSettingsManager config(stored);
        QDir dir(dir_.path());
        QStringList files;
        for (int i = 0; i < 12; ++i) make_png(dir, QString("m%1.png").arg(i, 2, 10, QChar('0')), QDateTime::currentDateTime().addSecs(i), i);
        config.set_screenshot_save_path(dir_.path());
        BatchCopyController controller(&config);
        QVERIFY(!config.get_batch_master_enabled());
        controller.apply();
        QVERIFY(!controller.watching());
        QVERIFY(controller.hotkey_bindings().empty());

        // Put a known file list on the clipboard; an OFF copy must leave it alone.
        const QString marker = make_png(dir, "marker.png", QDateTime::currentDateTime().addDays(-5));
        QVERIFY(copy_files_to_clipboard({marker}).ok);
        const auto outcome = controller.copy_latest(10);
        QVERIFY(!outcome.ok);
        QCOMPARE(read_clipboard_files(), QStringList{marker});

        controller.set_master_enabled(true);
        QVERIFY(controller.watching());
        QCOMPARE(controller.hotkey_bindings().size(), size_t(1)); // only Ctrl+Shift+V here; Ctrl+0 is a global hotkey of the Shortcuts page
        QCOMPARE(controller.hotkey_bindings().front().hotkey, QString("ctrl+shift+v"));
        const auto on = controller.copy_latest(10);
        QVERIFY2(on.ok, qPrintable(on.message));
        QCOMPARE(on.count, 10);
        QCOMPARE(read_clipboard_files().size(), 10);
        QCOMPARE(read_clipboard_files().first().section('\\', -1), QString("m11.png")); // newest first (marker is oldest)

        config.set_batch_hotkey_enabled(false);
        QVERIFY(controller.hotkey_bindings().empty());
        config.set_batch_hotkey_enabled(true);
        config.set_batch_monitor_enabled(false);
        controller.apply();
        QVERIFY(!controller.watching());
        QVERIFY(controller.master_enabled());

        controller.set_master_enabled(false);
        QVERIFY(!controller.watching());
        QVERIFY(controller.hotkey_bindings().empty());
    }

    void settingsPersistAcrossRestart() {
        {
            QSettings stored(settings_file_, QSettings::IniFormat);
            ToolSettingsManager config(stored);
            config.set_batch_master_enabled(true);
            config.set_batch_hotkey("ctrl+alt+b");
            config.set_batch_default_count(15);
            config.set_batch_notifications(false);
            stored.sync();
        }
        QSettings stored(settings_file_, QSettings::IniFormat);
        ToolSettingsManager config(stored);
        QVERIFY(config.get_batch_master_enabled());
        QCOMPARE(config.get_batch_hotkey(), QString("ctrl+alt+b"));
        QCOMPARE(config.get_batch_default_count(), 15);
        QVERIFY(!config.get_batch_notifications());
    }

    void hotkeyConflictsWithExistingJietubaHotkeys() {
        QSettings stored(settings_file_, QSettings::IniFormat);
        ToolSettingsManager config(stored);
        QVERIFY(BatchCopyController::hotkey_conflicts(&config, "batch_hotkey", "ctrl+shift+v").isEmpty());
        const auto clash = BatchCopyController::hotkey_conflicts(&config, "batch_hotkey", "ctrl+1"); // screenshot default
        QVERIFY(!clash.isEmpty());
        QVERIFY(BatchCopyController::hotkey_conflicts(&config, "batch_hotkey_5", "win+v").size() >= 1); // clipboard slot 3
        config.set_batch_hotkey_10("ctrl+alt+9");
        QVERIFY(!BatchCopyController::hotkey_conflicts(&config, "batch_hotkey_15", "ctrl+alt+9").isEmpty());
        QVERIFY(BatchCopyController::hotkey_conflicts(&config, "batch_hotkey_10", "ctrl+alt+9").isEmpty()); // itself
        QCOMPARE(BatchCopyController::hotkey_conflicts(&config, "batch_hotkey", "v"), QStringList{"!"}); // no modifier
    }

    void historyHotkeyIsAGlobalHotkeyOfTheShortcutsPage() {
        QSettings stored(settings_file_, QSettings::IniFormat);
        ToolSettingsManager config(stored);
        const auto keys = config.get_global_hotkeys("batch_history");
        QCOMPARE(keys.size(), GLOBAL_HOTKEY_SLOTS);
        QCOMPARE(keys.first(), QString("ctrl+0"));
        QVERIFY(keys.mid(1).join("").isEmpty());
        QCOMPARE(config.get_batch_history_count(), 5); // default count
        config.set_global_hotkeys("batch_history", {"ctrl+9", "", "", ""});
        QCOMPARE(config.get_global_hotkeys("batch_history").first(), QString("ctrl+9"));
        // The other batch hotkeys see it as taken.
        QVERIFY(!BatchCopyController::hotkey_conflicts(&config, "batch_hotkey_5", "ctrl+9").isEmpty());
        QVERIFY(BatchCopyController::hotkey_conflicts(&config, "batch_hotkey_5", "ctrl+0").isEmpty());
    }

    void explainsWhenAutoSaveIsOffWithoutChangingIt() {
        QSettings stored(settings_file_, QSettings::IniFormat);
        ToolSettingsManager config(stored);
        BatchCopyController controller(&config);
        config.set_screenshot_save_enabled(false);
        QVERIFY(!controller.autosave_problem().isEmpty());
        QVERIFY(!config.get_screenshot_save_enabled()); // untouched
        config.set_screenshot_save_enabled(true);
        QVERIFY(controller.autosave_problem().isEmpty());
    }

    void watcherReportsNewScreenshots() {
        QSettings stored(settings_file_, QSettings::IniFormat);
        ToolSettingsManager config(stored);
        config.set_screenshot_save_path(dir_.path());
        BatchCopyController controller(&config);
        controller.set_master_enabled(true);
        QSignalSpy spy(&controller, &BatchCopyController::recent_changed);
        make_png(QDir(dir_.path()), "new.png", QDateTime::currentDateTime());
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
        controller.set_master_enabled(false);
        spy.clear();
        make_png(QDir(dir_.path()), "after_off.png", QDateTime::currentDateTime());
        QTest::qWait(900);
        QCOMPARE(spy.count(), 0); // no watcher while off
    }

private:
    QTemporaryDir dir_;
    QString settings_file_;
};

QTEST_MAIN(BatchTests)
#include "test_batch.moc"
