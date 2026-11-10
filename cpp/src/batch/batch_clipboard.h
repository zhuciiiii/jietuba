#pragma once
// Recent Screenshot Batch Copy: puts several existing files on the Windows clipboard as ONE file list
// (CF_HDROP, what Explorer's Ctrl+C produces). The images are never read, merged, stitched or re-encoded;
// a paste target receives the original files. "Preferred DropEffect" is set to COPY so a paste cannot move them.
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace jietuba::batch {

// The DROPFILES structure for the paths (wide characters, native separators).
QByteArray build_dropfiles(const QStringList &paths);
QStringList parse_dropfiles(const QByteArray &data);

struct ClipboardResult {
    bool ok = false;
    int count = 0;
    QString error;
};

// Replaces the clipboard contents with the file list. On failure the previous contents stay unless the failure
// happened after the clipboard was emptied; the clipboard is retried for about a second while another
// program holds it.
ClipboardResult copy_files_to_clipboard(const QStringList &paths);

// What the clipboard currently holds as a file list; empty if none. Mainly for tests.
QStringList read_clipboard_files();
// The "Preferred DropEffect" value, or -1 when absent.
int read_clipboard_drop_effect();
// The "CanIncludeInClipboardHistory" value (0 = not recorded in history), or -1 when absent.
int read_clipboard_history_flag();

}
