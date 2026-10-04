/*
 * src/updater.c — optional update check, on the launcher's main page.
 *
 * Off by default: nothing goes online unless the player ticks "Check for
 * updates on startup" or presses "Check for updates now". A check asks GitHub for the
 * latest release of Mr-Shizzy/PacManRecomp (on a background thread) and,
 * when it is newer than this build, asks the player. On Yes the game writes
 * its built-in update script (src/update.ps1) to %TEMP%, starts it and
 * quits; the script downloads that release's Easy Build, builds it from the
 * game folder's ROM (working in update-temp inside the game folder, about
 * 1 GB at most), replaces only the program files (settings, keys, high
 * scores and mods stay) and starts the game again, deleting what it made.
 *
 * Testing: PACMAN_UPDATE_TEST_JSON=<file> reads the "latest release" from a
 * local file, whose zip may then be a local path.
 */
#include "updater.h"
#include "options.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#ifndef PACMAN_VERSION
#define PACMAN_VERSION "0.0.0"
#endif

#define WIDEN_(x)    L##x
#define WIDEN(x)     WIDEN_(x)
#define API_HOST    L"api.github.com"
#define API_PATH    L"/repos/Mr-Shizzy/PacManRecomp/releases/latest"
#define ZIP_PREFIX  "https://github.com/Mr-Shizzy/PacManRecomp/releases/download/"

extern const unsigned char g_update_ps1[];
extern const unsigned g_update_ps1_len;
void nesrecomp_quit_to_desktop(void);

enum { ST_IDLE, ST_CHECKING, ST_LATEST, ST_NEWER, ST_FAILED };

static volatile LONG s_state = ST_IDLE;
static int  s_started;          /* the startup check ran (once per process) */
static int  s_prompted;         /* the player has been asked about this result */
static int  s_loaded;
static char s_tag[64];          /* newer release: "1.0.3" */
static char s_zip[1024];
static char s_notes[1200];
static int  s_test;             /* PACMAN_UPDATE_TEST_JSON in use */

/* ---- tiny JSON readers (GitHub's release object) ------------------------ */

/* The string value after "key": (first match at or after p), unescaped. */
static const char *json_str(const char *p, const char *key, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr(p, pat);
    if (!k) return NULL;
    const char *c = k + strlen(pat);
    while (*c == ' ' || *c == ':' || *c == '\t' || *c == '\r' || *c == '\n') c++;
    if (*c != '"') return NULL;
    c++;
    size_t n = 0;
    while (*c && *c != '"') {
        char ch = *c++;
        if (ch == '\\' && *c) {
            ch = *c++;
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') continue;
            else if (ch == 't') ch = ' ';
            else if (ch == 'u') { c += (strlen(c) >= 4) ? 4 : strlen(c); ch = '?'; }
        }
        if (n + 1 < cap) out[n++] = ch;
    }
    out[n] = '\0';
    return c;
}

/* "v1.2.3" / "1.2" -> comparable number. */
static long version_num(const char *v) {
    int a = 0, b = 0, c = 0;
    if (*v == 'v' || *v == 'V') v++;
    sscanf(v, "%d.%d.%d", &a, &b, &c);
    return (long)a * 1000000L + b * 1000L + c;
}

/* ---- the check (background thread) -------------------------------------- */

static char *http_get_latest(void) {
    HINTERNET ses = NULL, con = NULL, req = NULL;
    char *buf = NULL;
    size_t len = 0;
    ses = WinHttpOpen(L"PacManRecomp-updater/" WIDEN(PACMAN_VERSION),
                      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) goto done;
    WinHttpSetTimeouts(ses, 8000, 8000, 8000, 8000);
    con = WinHttpConnect(ses, API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) goto done;
    req = WinHttpOpenRequest(con, L"GET", API_PATH, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) goto done;
    if (!WinHttpAddRequestHeaders(req, L"Accept: application/vnd.github+json", (DWORD)-1,
                                  WINHTTP_ADDREQ_FLAG_ADD)) goto done;
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) goto done;
    if (!WinHttpReceiveResponse(req, NULL)) goto done;
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) goto done;
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        if (len + avail > 4u * 1024 * 1024) break;          /* far more than a release */
        char *nb = (char *)realloc(buf, len + avail + 1);
        if (!nb) break;
        buf = nb;
        if (!WinHttpReadData(req, buf + len, avail, &got)) break;
        len += got;
    }
    if (buf) buf[len] = '\0';
done:
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return buf;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (n >= 0 && n < 4 * 1024 * 1024) ? (char *)malloc((size_t)n + 1) : NULL;
    if (b) { b[fread(b, 1, (size_t)n, f)] = '\0'; }
    fclose(f);
    return b;
}

static DWORD WINAPI check_thread(LPVOID arg) {
    (void)arg;
    const char *test = getenv("PACMAN_UPDATE_TEST_JSON");
    s_test = test && test[0];
    char *json = s_test ? read_file(test) : http_get_latest();
    LONG result = ST_FAILED;
    char tag[64] = "", url[1024] = "";
    if (json && json_str(json, "tag_name", tag, sizeof(tag))) {
        /* The Easy Build zip among the release's files. */
        const char *p = json;
        while ((p = json_str(p, "browser_download_url", url, sizeof(url))) != NULL) {
            size_t n = strlen(url);
            if (n > 13 && !strcmp(url + n - 13, "EasyBuild.zip")) break;
            url[0] = '\0';
        }
        if (version_num(tag) <= version_num(PACMAN_VERSION)) {
            result = ST_LATEST;
        } else if (url[0] && (s_test || !strncmp(url, ZIP_PREFIX, strlen(ZIP_PREFIX)))) {
            snprintf(s_tag, sizeof(s_tag), "%s", tag[0] == 'v' ? tag + 1 : tag);
            snprintf(s_zip, sizeof(s_zip), "%s", url);
            if (!json_str(json, "body", s_notes, sizeof(s_notes))) s_notes[0] = '\0';
            if (strlen(s_notes) > 600) strcpy(s_notes + 597, "...");
            result = ST_NEWER;
        }
    }
    free(json);
    InterlockedExchange(&s_state, result);
    return 0;
}

static void start_check(void) {
    if (s_state == ST_CHECKING) return;
    s_prompted = 0;
    InterlockedExchange(&s_state, ST_CHECKING);
    HANDLE t = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else InterlockedExchange(&s_state, ST_FAILED);
}

/* ---- installing ----------------------------------------------------------- */

static void utf8_to_w(const char *s, wchar_t *out, int cap) {
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap)) out[0] = 0;
}

/* The folder holding the exe (the game folder), with a trailing backslash. */
static int game_dir(wchar_t out[MAX_PATH]) {
    if (!GetModuleFileNameW(NULL, out, MAX_PATH)) return 0;
    wchar_t *slash = wcsrchr(out, L'\\');
    if (!slash) return 0;
    slash[1] = 0;
    return 1;
}

/* An update that was cut off (power cut, crash) can leave its update-temp
 * folder behind; the game is running, so no update is: delete it. */
static void remove_leftover_temp(void) {
    wchar_t dir[MAX_PATH + 16];
    if (!game_dir(dir)) return;
    wcscat(dir, L"update-temp");
    if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) return;
    dir[wcslen(dir) + 1] = 0;           /* SHFileOperation: double-NUL list */
    SHFILEOPSTRUCTW op;
    ZeroMemory(&op, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = dir;
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

/* Write the update script to %TEMP%, start it (it shows its own progress
 * window, no console), quit. */
static void start_update(void) {
    wchar_t tmp[MAX_PATH], script[MAX_PATH], exe[MAX_PATH], dir[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmp)) return;
    _snwprintf(script, MAX_PATH, L"%lsPacManRecomp-update.ps1", tmp);
    script[MAX_PATH - 1] = 0;
    FILE *f = _wfopen(script, L"wb");
    if (!f) return;
    fwrite(g_update_ps1, 1, g_update_ps1_len, f);
    fclose(f);

    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;
    wcscpy(dir, exe);
    wchar_t *slash = wcsrchr(dir, L'\\');
    const wchar_t *name = slash ? slash + 1 : exe;
    if (slash) *slash = 0;

    wchar_t zip[1024], ver[64];
    utf8_to_w(s_zip, zip, 1024);
    utf8_to_w(s_tag, ver, 64);
    static wchar_t cmd[4096];
    _snwprintf(cmd, 4096,
        L"powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File \"%ls\" "
        L"-GameDir \"%ls\" -ExeName \"%ls\" -ZipUrl \"%ls\" -Version \"%ls\" -WaitPid %lu",
        script, dir, name, zip, ver, (unsigned long)GetCurrentProcessId());
    cmd[4095] = 0;

    /* Windows PowerShell must not load PowerShell 7's modules (as in the
     * Easy Build's .bat); this only changes our own environment. */
    SetEnvironmentVariableW(L"PSModulePath", NULL);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
        DeleteFileW(script);
        MessageBoxW(GetActiveWindow(), L"Couldn't start the updater.", L"Pac-Man update",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    nesrecomp_quit_to_desktop();
}

/* Free space an update needs while it works: the download, the build tools
 * and the build go in update-temp inside the game folder (measured peak
 * 0.9 GB), deleted afterwards. */
#define UPDATE_NEED_GB 2

static void ask_to_update(void) {
    char msg[2400];
    wchar_t where[MAX_PATH];
    ULARGE_INTEGER avail;
    int need = UPDATE_NEED_GB;
    const char *tneed = getenv("PACMAN_UPDATE_TEST_NEED_GB");   /* tests only */
    if (s_test && tneed && atoi(tneed) > 0) need = atoi(tneed);
    if (game_dir(where) && GetDiskFreeSpaceExW(where, &avail, NULL, NULL) &&
        avail.QuadPart < (ULONGLONG)need * 1024 * 1024 * 1024) {
        char drive[64];
        if (where[0] && where[1] == L':')
            snprintf(drive, sizeof(drive), "drive %c: (where the game is)", (char)where[0]);
        else
            snprintf(drive, sizeof(drive), "the drive where the game is");
        snprintf(msg, sizeof(msg),
            "Pac-Man %s is available, but there isn't enough free space to update.\n\n"
            "Updating needs about %d GB free on %s while it works. It uses that "
            "space for temporary files in the game folder and deletes them when "
            "it's done. You have %.1f GB free.\n\n"
            "Free up some space, then press \"Check for updates now\" in the launcher.",
            s_tag, need, drive,
            (double)avail.QuadPart / (1024.0 * 1024.0 * 1024.0));
        wchar_t w[2400];
        utf8_to_w(msg, w, 2400);
        MessageBoxW(GetActiveWindow(), w, L"Pac-Man update", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        return;
    }
    snprintf(msg, sizeof(msg),
        "Pac-Man %s is available (you have %s).\n\n%s%s"
        "Updating downloads the new version and builds it from your ROM, like the "
        "first time. It takes a few minutes; the game closes and starts again when "
        "it's done.\n\nYour settings, keys, high scores and mods are kept. While it "
        "works it uses up to 1 GB of temporary files in the game folder, and "
        "deletes them when it's done.\n\nUpdate now?",
        s_tag, PACMAN_VERSION, s_notes, s_notes[0] ? "\n\n" : "");
    wchar_t w[2400];
    utf8_to_w(msg, w, 2400);
    if (MessageBoxW(GetActiveWindow(), w, L"Pac-Man update",
                    MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES)
        start_update();
}

/* ---- launcher rows (main page) ------------------------------------------- */

enum { ROW_AUTO, ROW_NOW, ROW_STATUS, ROW_COUNT };

static void poll(void) {
    if (!s_loaded) { options_reload(); s_loaded = 1; }
    if (!s_started) {
        s_started = 1;
        remove_leftover_temp();
        if (g_opt.check_updates) start_check();
    }
    if (s_state == ST_NEWER && !s_prompted) {
        s_prompted = 1;
        ask_to_update();
    }
}

static int rows_count(void *ctx) { (void)ctx; poll(); return ROW_COUNT; }

static int rows_get(void *ctx, int i, RecompLauncherCHostRow *r) {
    (void)ctx;
    memset(r, 0, sizeof(*r));
    switch (i) {
    case ROW_AUTO:
        r->type = RECOMP_HOST_ROW_TOGGLE;
        snprintf(r->label, sizeof(r->label), "Check for updates on startup");
        snprintf(r->help, sizeof(r->help),
                 "Ask GitHub for a newer version each time the launcher opens. "
                 "Off: the game never goes online. With \"Skip launcher on boot\" "
                 "ticked, the launcher doesn't open, so there are no checks.");
        r->value = g_opt.check_updates;
        break;
    case ROW_NOW:
        r->type = RECOMP_HOST_ROW_BUTTON;
        snprintf(r->label, sizeof(r->label), "Check for updates now");
        snprintf(r->help, sizeof(r->help), "Look for a newer version on GitHub now.");
        r->disabled = s_state == ST_CHECKING;
        break;
    default: {
        r->type = RECOMP_HOST_ROW_TEXT;
        const char *t = "";
        switch (s_state) {
        case ST_CHECKING: t = "Checking..."; break;
        case ST_LATEST:   t = "You have the latest version (" PACMAN_VERSION ")."; break;
        case ST_NEWER:    t = "A new version is available."; break;
        case ST_FAILED:   t = "Couldn't check (no internet?)."; break;
        default:          t = "Version " PACMAN_VERSION; break;
        }
        snprintf(r->label, sizeof(r->label), "%s", t);
        break;
    }
    }
    return 1;
}

static int rows_set(void *ctx, int i, int value, const char *rom) {
    (void)ctx; (void)rom;
    if (i == ROW_AUTO) {
        g_opt.check_updates = value ? 1 : 0;
        options_save_now();
    } else if (i == ROW_NOW) {
        start_check();
    }
    return 1;
}

static const char *rows_status(void *ctx) { (void)ctx; return ""; }

const RecompLauncherCHostPage *updater_launcher_page(void) {
    static const RecompLauncherCHostPage page = {
        NULL, "Updates", rows_count, rows_get, NULL, rows_set, rows_status,
        0,                              /* in_settings */
        1                               /* on_dashboard: next to "Skip launcher" */
    };
    return &page;
}

#else  /* not Windows: no updater */
const RecompLauncherCHostPage *updater_launcher_page(void) { return NULL; }
#endif
