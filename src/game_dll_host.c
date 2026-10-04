/* game_dll_host.c - the exe side of game.dll (see src/gamedll/game_dll_abi.h).
 *
 * Built only with -DPACMAN_GAME_DLL=ON: the exe then holds no game code. The
 * game code is made on the player's PC from their own ROM, into game.dll
 * next to the exe, the first time the game starts (and again after an
 * update): game_dll_prepare() runs the recompiler (tools\NESRecomp.exe) and
 * TinyCC (tools\tcc\tcc.exe) while a small window shows the progress. */
#include "nes_runtime.h"
#include "crc32.h"
#include "game_extras.h"
#include "game_dll_abi.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define STR_(x) #x
#define STR(x) STR_(x)

/* Game-code data the runtime uses: owned here, shared with the DLL. */
uint16_t g_rti_target = 0;
uint16_t g_rti_source = 0;
int      g_rti_bank = -1;
uint16_t g_rts_target = 0;
int      g_recomp_push_all_jsr = 0;

int call_by_address_tail(uint16_t addr, int caller_bank);

static void (*s_func_RESET)(void);
static void (*s_func_NMI)(void);
static void (*s_func_IRQ)(void);
static int  (*s_call_by_address)(uint16_t);
static int  (*s_call_by_address_cb)(uint16_t, int);

static wchar_t s_dir[MAX_PATH];        /* the exe's folder, with a trailing \ */

static void fail(const char *why)
{
    char msg[1024];
    snprintf(msg, sizeof msg, "Couldn't set up the game: %s", why);
    MessageBoxA(NULL, msg, "Pac-Man Recomp", MB_OK | MB_ICONERROR);
    exit(1);
}

static void find_dir(void)
{
    if (s_dir[0]) return;
    DWORD n = GetModuleFileNameW(NULL, s_dir, MAX_PATH);
    wchar_t *slash = wcsrchr(s_dir, L'\\');
    if (!n || n >= MAX_PATH || !slash || slash - s_dir + 40 >= MAX_PATH)
        fail("the game folder's path is too long.\n\n"
             "Move the folder somewhere shorter, like C:\\Games.");
    slash[1] = 0;
}

static void in_dir(wchar_t *out, const wchar_t *name)
{
    swprintf(out, MAX_PATH, L"%ls%ls", s_dir, name);
}

/* Loads game.dll. Returns 0 (and unloads it) if it's missing or was made
 * for another version of the exe. */
static int try_load(void)
{
    wchar_t path[MAX_PATH];
    in_dir(path, L"game.dll");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return 0;
    HMODULE dll = LoadLibraryW(path);
    if (!dll) return 0;
    int (*init)(int, const char *, void **, void **) =
        (int (*)(int, const char *, void **, void **))(void *)
            GetProcAddress(dll, "game_dll_init");
    void *imports[GAME_DLL_NUM_IMPORTS] = {
#define GD_IMPORT(n) (void *)&n,
        GAME_DLL_IMPORTS(GD_IMPORT)
#undef GD_IMPORT
    };
    void *exports[GAME_DLL_NUM_EXPORTS] = {0};
    if (!init || init(GAME_DLL_ABI_VERSION, PACMAN_VERSION, imports, exports)
                     != GAME_DLL_ABI_VERSION) {
        FreeLibrary(dll);
        return 0;
    }
    int i = 0;
    *(void **)&s_func_RESET = exports[i++];
    *(void **)&s_func_NMI = exports[i++];
    *(void **)&s_func_IRQ = exports[i++];
    *(void **)&s_call_by_address = exports[i++];
    *(void **)&s_call_by_address_cb = exports[i++];
    return 1;
}

/* ---- the "Setting up" window ------------------------------------------- */

static HWND s_win;
static wchar_t s_step[128];
static int s_pct;

static LRESULT CALLBACK win_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
        HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        RECT t = { 20, 16, r.right - 20, 76 };
        DrawTextW(dc, L"Setting up Pac-Man from your ROM. This happens the first "
                      L"time you play and after an update, and takes a few seconds.",
                  -1, &t, DT_WORDBREAK);
        RECT s = { 20, 80, r.right - 20, 100 };
        DrawTextW(dc, s_step, -1, &s, DT_SINGLELINE);
        RECT bar = { 20, 108, r.right - 20, 128 };
        FrameRect(dc, &bar, (HBRUSH)GetStockObject(GRAY_BRUSH));
        RECT fill = { bar.left + 2, bar.top + 2,
                      bar.left + 2 + (bar.right - bar.left - 4) * s_pct / 100,
                      bar.bottom - 2 };
        HBRUSH yellow = CreateSolidBrush(RGB(255, 204, 0));
        FillRect(dc, &fill, yellow);
        DeleteObject(yellow);
        SelectObject(dc, old);
        DeleteObject(font);
        EndPaint(w, &ps);
        return 0;
    }
    if (m == WM_CLOSE) return 0;           /* it closes itself when done */
    return DefWindowProcW(w, m, wp, lp);
}

static void win_open(void)
{
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = win_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_WAIT);
    wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"PacManRecompSetup";
    RegisterClassW(&wc);
    RECT r = { 0, 0, 460, 148 };
    DWORD style = WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    s_win = CreateWindowW(wc.lpszClassName, L"Pac-Man Recomp", style,
                          (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                          (GetSystemMetrics(SM_CYSCREEN) - h) / 2,
                          w, h, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(s_win, SW_SHOW);
    UpdateWindow(s_win);
}

static void win_show(const wchar_t *step, int pct)
{
    if (step) wcsncpy(s_step, step, 127);
    s_pct = pct;
    InvalidateRect(s_win, NULL, FALSE);
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* ---- building game.dll ------------------------------------------------- */

/* Runs a tool hidden, in `cwd`, appending its output to `log`; the bar
 * creeps from `from` toward `to` meanwhile. Returns the exit code. */
static DWORD run(wchar_t *cmd, const wchar_t *cwd, HANDLE log, int from, int to)
{
    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd,
                        &si, &pi))
        return (DWORD)-1;
    DWORD start = GetTickCount();
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, 100, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        double secs = (GetTickCount() - start) / 1000.0;
        int pct = from + (int)((to - from) * (1.0 - 1.0 / (1.0 + secs / 2.0)));
        win_show(NULL, pct);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

static void remove_tree(const wchar_t *dir)
{
    wchar_t pat[MAX_PATH], p[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            swprintf(p, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(p);
            else DeleteFileW(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

/* Copies the ROM into the work folder as pacman.nes, after checking it's the
 * right one. Read with fopen, the way the runner itself opens it. */
static void copy_rom(const char *rom, const wchar_t *dest)
{
    FILE *f = rom ? fopen(rom, "rb") : NULL;
    if (!f) fail("couldn't read your Pac-Man ROM.");
    static uint8_t data[1 << 20];
    size_t n = fread(data, 1, sizeof data, f);
    fclose(f);
    if (n <= 16 || n == sizeof data ||
        crc32_compute(data + 16, n - 16) != game_get_expected_crc32())
        fail("this isn't the right ROM. It must be Pac-Man (USA) (Namco), "
             "the Namco release (the Tengen version won't work).");
    FILE *o = _wfopen(dest, L"wb");
    if (!o || fwrite(data, 1, n, o) != n) fail("couldn't write to the game folder.");
    fclose(o);
}

static void build(const char *rom)
{
    wchar_t work[MAX_PATH], path[MAX_PATH], tools[MAX_PATH], cmd[4 * MAX_PATH];
    in_dir(work, L"setup-temp");
    in_dir(tools, L"tools");
    remove_tree(work);
    if (!CreateDirectoryW(work, NULL))
        fail("couldn't write to the game folder.\n\n"
             "Move the folder somewhere you can save files, like Documents.");

    win_open();
    win_show(L"Step 1 of 2: reading your ROM...", 2);
    swprintf(path, MAX_PATH, L"%ls\\pacman.nes", work);
    copy_rom(rom, path);

    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    wchar_t logpath[MAX_PATH];
    swprintf(logpath, MAX_PATH, L"%ls\\setup-log.txt", work);
    HANDLE log = CreateFileW(logpath, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                             CREATE_ALWAYS, 0, NULL);

    /* 1. The recompiler translates the ROM's program into C (generated\). */
    win_show(L"Step 1 of 2: translating the game's program into C...", 5);
    swprintf(cmd, 4 * MAX_PATH,
             L"\"%ls\\NESRecomp.exe\" pacman.nes --game \"%ls\\game.toml\"", tools, tools);
    DWORD rc = run(cmd, work, log, 5, 45);

    /* 2. TinyCC compiles it into game.dll. Relative paths with forward
     * slashes: TinyCC's #pragma once misses a header reached by a path with
     * backslashes, and includes it twice. */
    if (rc == 0) {
        win_show(L"Step 2 of 2: compiling the game...", 45);
        swprintf(cmd, 4 * MAX_PATH,
                 L"\"%ls\\tcc\\tcc.exe\" -shared -w -DGAME_DLL_STAMP=" STR(PACMAN_VERSION_BARE)
                 L" -I generated -I ../tools/include ../tools/gamedll/game_dll.c"
                 L" -o game.dll", tools);
        rc = run(cmd, work, log, 45, 95);
    }
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);

    wchar_t built[MAX_PATH], dll[MAX_PATH], keep[MAX_PATH];
    swprintf(built, MAX_PATH, L"%ls\\game.dll", work);
    in_dir(dll, L"game.dll");
    in_dir(keep, L"setup-log.txt");
    if (rc != 0 || !MoveFileExW(built, dll, MOVEFILE_REPLACE_EXISTING)) {
        if (rc != (DWORD)-1) CopyFileW(logpath, keep, FALSE);
        remove_tree(work);
        DestroyWindow(s_win);
        fail(rc == (DWORD)-1
             ? "a file of the game is missing (the tools folder).\n\n"
               "Unzip the whole download again."
             : "a setup step failed. setup-log.txt in the game folder says why.\n\n"
               "Your antivirus may have blocked it: allow the game folder and try again.");
    }
    DeleteFileW(keep);
    win_show(L"Done.", 100);
    remove_tree(work);                     /* the ROM copy and generated C */
    DestroyWindow(s_win);
}

void game_dll_prepare(const char *rom)
{
    if (s_func_RESET) return;
    find_dir();
    if (try_load()) return;
    /* One setup at a time, if the game is started twice. */
    HANDLE m = CreateMutexW(NULL, FALSE, L"Local\\PacManRecomp-setup");
    if (m) WaitForSingleObject(m, INFINITE);
    if (!try_load()) {
        build(rom);
        if (!try_load())
            fail("the game didn't load after setting it up. "
                 "Your antivirus may have blocked game.dll: allow the game folder "
                 "and try again.");
    }
    if (m) { ReleaseMutex(m); CloseHandle(m); }
}

static void need(void)
{
    extern const char *g_rom_path_for_extras;
    if (!s_func_RESET) game_dll_prepare(g_rom_path_for_extras);
}

void func_RESET(void) { need(); s_func_RESET(); }
void func_NMI(void)   { need(); s_func_NMI(); }
void func_IRQ(void)   { need(); s_func_IRQ(); }
int call_by_address(uint16_t addr) { need(); return s_call_by_address(addr); }
int call_by_address_cb(uint16_t addr, int caller_bank)
{
    need();
    return s_call_by_address_cb(addr, caller_bank);
}
