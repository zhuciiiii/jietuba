#include "batch/batch_clipboard.h"
#include <QDir>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace jietuba::batch {
namespace {
#pragma pack(push, 1)
struct DropFilesHeader {
    quint32 files_offset; // offset of the file list from the start of the structure
    qint32 x, y;
    quint32 non_client;
    quint32 wide;
};
#pragma pack(pop)
static_assert(sizeof(DropFilesHeader) == 20);
}

QByteArray build_dropfiles(const QStringList &paths) {
    DropFilesHeader header{sizeof(DropFilesHeader), 0, 0, 0, 1};
    QByteArray data(reinterpret_cast<const char *>(&header), sizeof(header));
    for (const auto &path : paths) {
        const QString native = QDir::toNativeSeparators(path);
        data.append(reinterpret_cast<const char *>(native.utf16()), native.size() * 2);
        data.append(QByteArray(2, '\0'));
    }
    data.append(QByteArray(2, '\0'));
    return data;
}

QStringList parse_dropfiles(const QByteArray &data) {
    if (data.size() < static_cast<qsizetype>(sizeof(DropFilesHeader))) return {};
    DropFilesHeader header;
    std::memcpy(&header, data.constData(), sizeof(header));
    if (header.files_offset >= static_cast<quint32>(data.size())) return {};
    QStringList paths;
    if (header.wide) {
        const auto *chars = reinterpret_cast<const char16_t *>(data.constData() + header.files_offset);
        const qsizetype total = (data.size() - header.files_offset) / 2;
        qsizetype start = 0;
        for (qsizetype i = 0; i < total; ++i) {
            if (chars[i] != 0) continue;
            if (i == start) break;
            paths.append(QString::fromUtf16(chars + start, i - start));
            start = i + 1;
        }
    } else {
        for (const auto &part : data.mid(header.files_offset).split('\0')) {
            if (part.isEmpty()) break;
            paths.append(QString::fromLocal8Bit(part));
        }
    }
    return paths;
}

#ifdef Q_OS_WIN
namespace {
UINT drop_effect_format() {
    static const UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
    return format;
}

UINT history_flag_format() {
    static const UINT format = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    return format;
}

HGLOBAL make_global(const void *bytes, size_t size) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    if (!handle) return nullptr;
    void *target = GlobalLock(handle);
    if (!target) {
        GlobalFree(handle);
        return nullptr;
    }
    std::memcpy(target, bytes, size);
    GlobalUnlock(handle);
    return handle;
}

bool open_clipboard_with_retry() {
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (OpenClipboard(nullptr)) return true;
        Sleep(50);
    }
    return false;
}
}

ClipboardResult copy_files_to_clipboard(const QStringList &paths) {
    ClipboardResult result;
    if (paths.isEmpty()) {
        result.error = "No files to copy.";
        return result;
    }
    const QByteArray dropfiles = build_dropfiles(paths);
    HGLOBAL drop = make_global(dropfiles.constData(), static_cast<size_t>(dropfiles.size()));
    const DWORD copy_effect = 1; // DROPEFFECT_COPY
    HGLOBAL effect = make_global(&copy_effect, sizeof(copy_effect));
    // 0 = do not record this in clipboard history. The batch is made from history and saved files already; recording it
    // would put the batch itself at the top of the history, and the next batch copy would count it again.
    const DWORD no_history = 0;
    HGLOBAL history_flag = make_global(&no_history, sizeof(no_history));
    if (!drop || !effect || !history_flag) {
        if (drop) GlobalFree(drop);
        if (effect) GlobalFree(effect);
        if (history_flag) GlobalFree(history_flag);
        result.error = "Out of memory.";
        return result;
    }
    if (!open_clipboard_with_retry()) {
        GlobalFree(drop);
        GlobalFree(effect);
        GlobalFree(history_flag);
        result.error = QString("The clipboard is in use by another program (error %1).").arg(GetLastError());
        return result;
    }
    if (!EmptyClipboard()) {
        result.error = QString("EmptyClipboard failed (error %1).").arg(GetLastError());
    } else if (!SetClipboardData(CF_HDROP, drop)) {
        result.error = QString("SetClipboardData failed (error %1).").arg(GetLastError());
    } else {
        drop = nullptr; // owned by the system now
        if (SetClipboardData(drop_effect_format(), effect)) effect = nullptr; // non-fatal: paste defaults to copy
        if (SetClipboardData(history_flag_format(), history_flag)) history_flag = nullptr; // non-fatal: history may record it
        result.ok = true;
        result.count = static_cast<int>(paths.size());
    }
    CloseClipboard();
    if (drop) GlobalFree(drop);
    if (effect) GlobalFree(effect);
    if (history_flag) GlobalFree(history_flag);
    return result;
}

QStringList read_clipboard_files() {
    if (!open_clipboard_with_retry()) return {};
    QStringList files;
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        if (HANDLE handle = GetClipboardData(CF_HDROP)) {
            const SIZE_T size = GlobalSize(handle);
            if (const void *data = GlobalLock(handle)) {
                files = parse_dropfiles(QByteArray(static_cast<const char *>(data), static_cast<qsizetype>(size)));
                GlobalUnlock(handle);
            }
        }
    }
    CloseClipboard();
    return files;
}

int read_clipboard_history_flag() {
    if (!open_clipboard_with_retry()) return -1;
    int value = -1;
    if (IsClipboardFormatAvailable(history_flag_format())) {
        if (HANDLE handle = GetClipboardData(history_flag_format())) {
            if (const void *data = GlobalLock(handle)) {
                DWORD flag = 0;
                std::memcpy(&flag, data, sizeof(flag));
                value = static_cast<int>(flag);
                GlobalUnlock(handle);
            }
        }
    }
    CloseClipboard();
    return value;
}

int read_clipboard_drop_effect() {
    if (!open_clipboard_with_retry()) return -1;
    int value = -1;
    if (IsClipboardFormatAvailable(drop_effect_format())) {
        if (HANDLE handle = GetClipboardData(drop_effect_format())) {
            if (const void *data = GlobalLock(handle)) {
                DWORD effect = 0;
                std::memcpy(&effect, data, sizeof(effect));
                value = static_cast<int>(effect);
                GlobalUnlock(handle);
            }
        }
    }
    CloseClipboard();
    return value;
}
#else
ClipboardResult copy_files_to_clipboard(const QStringList &) { return {false, 0, "Windows only."}; }
QStringList read_clipboard_files() { return {}; }
int read_clipboard_drop_effect() { return -1; }
int read_clipboard_history_flag() { return -1; }
#endif

}
