// Окно приложения: тот же HTML/CSS/JS интерфейс, что отдаёт HTTP-сервер,
// показывается прямо в окне через WebView2 (без браузера).
#include "ui.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// net.h тянет winsock2.h — он должен идти раньше windows.h.
#include "net.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include "WebView2.h"

#include <cwchar>
#include <string>

#include "app.h"
#include "store.h"
#include "sync.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, \
                "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                "language='*'\"")

namespace ui {

static const wchar_t* kClass = L"ObmenMainWindow";
static const UINT WM_APP_TRAY = WM_APP + 1;
static const UINT WM_APP_WEBFAIL = WM_APP + 2;

enum {
    ID_MENU_OPENSHARE = 1200,
    ID_MENU_HIDE = 1201,
    ID_MENU_EXIT = 1202,
    ID_MENU_SYNC = 1203,
    ID_MENU_WEB = 1204,
    ID_MENU_COPYADDR = 1205,
    ID_MENU_ABOUT = 1206,
    ID_TRAY_SHOW = 1300,
    ID_TRAY_SYNC = 1301,
    ID_TRAY_OPENSHARE = 1302,
    ID_TRAY_COPY = 1303,
    ID_TRAY_EXIT = 1304
};

static HWND g_hwnd = nullptr;
static HICON g_iconBig = nullptr;
static HICON g_iconSmall = nullptr;
static bool g_trayAdded = false;
static bool g_exiting = false;
static bool g_balloonShown = false;
static bool g_fallbackDone = false;
static UINT g_taskbarCreated = 0;

static ICoreWebView2Environment* g_env = nullptr;
static ICoreWebView2Controller* g_controller = nullptr;
static ICoreWebView2* g_webview = nullptr;
static bool g_webReady = false;

// ---------------------------------------------------------------- helpers

static std::wstring web_url() {
    return L"http://127.0.0.1:" + std::to_wstring(g_app.cfg.httpPort) + L"/";
}

static std::string u8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

static std::wstring server_addr() {
    std::string ip = "127.0.0.1";
    auto ips = net::local_ip_list();
    if (!ips.empty()) ip = ips.front();
    std::wstring wip;
    int n = MultiByteToWideChar(CP_UTF8, 0, ip.c_str(), (int)ip.size(), nullptr, 0);
    if (n > 0) {
        wip.resize((size_t)n);
        MultiByteToWideChar(CP_UTF8, 0, ip.c_str(), (int)ip.size(), &wip[0], n);
    }
    return wip + L":" + std::to_wstring(g_app.cfg.httpPort);
}

// Путь первой сетевой папки — для меню «Открыть папку обмена» и подсказки в трее.
static std::filesystem::path first_share_path() {
    auto list = store::shares();
    if (list.empty()) return std::filesystem::path();
    return store::from_utf8(list.front().path);
}

static void open_in_browser() {
    ShellExecuteW(nullptr, L"open", web_url().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void open_share_folder() {
    std::filesystem::path root = first_share_path();
    if (root.empty()) {
        MessageBoxW(g_hwnd, L"Папка обмена не выбрана.", L"Обмен", MB_ICONINFORMATION | MB_OK);
        return;
    }
    ShellExecuteW(g_hwnd, L"open", root.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void copy_to_clipboard(const std::wstring& text) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void* p = GlobalLock(h);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

static void show_main_window() {
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

static void show_balloon(const wchar_t* title, const wchar_t* text) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid.szInfo, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void add_tray_icon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = g_iconSmall ? g_iconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    std::filesystem::path root = first_share_path();
    std::wstring tip = L"Обмен — ";
    tip += root.empty() ? L"папка не выбрана" : root.wstring();
    if (tip.size() > 120) tip = tip.substr(0, 120);
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    log_line(g_trayAdded ? "иконка добавлена в системный трей"
                         : "не удалось добавить иконку в трей");
}

static void hide_to_tray() {
    ShowWindow(g_hwnd, SW_HIDE);
    if (!g_balloonShown) {
        g_balloonShown = true;
        show_balloon(L"Обмен продолжает работать",
                     L"Окно свёрнуто в трей. Двойной щелчок по значку — вернуть окно.");
    }
}

static void tray_menu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, L"Открыть окно");
    AppendMenuW(m, MF_STRING, ID_TRAY_SYNC, L"Синхронизировать сейчас");
    AppendMenuW(m, MF_STRING, ID_TRAY_OPENSHARE, L"Открыть папку обмена");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_COPY, L"Копировать адрес");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Выход");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    if (cmd == 0) return;

    switch (cmd) {
    case ID_TRAY_SHOW: show_main_window(); break;
    case ID_TRAY_SYNC: sync::push_now(); break;
    case ID_TRAY_OPENSHARE: open_share_folder(); break;
    case ID_TRAY_COPY: copy_to_clipboard(server_addr()); break;
    case ID_TRAY_EXIT:
        g_exiting = true;
        DestroyWindow(g_hwnd);
        break;
    default: break;
    }
}

// ------------------------------------------------------------- WebView2

namespace {

class EnvHandler final : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    LONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown ||
            riid == __uuidof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&m_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override;
};

class ControllerHandler final : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    LONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown ||
            riid == __uuidof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&m_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override;
};

class TitleHandler final : public ICoreWebView2DocumentTitleChangedEventHandler {
    LONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2DocumentTitleChangedEventHandler)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&m_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown* args) override {
        (void)args;
        if (!sender) return S_OK;
        LPWSTR title = nullptr;
        if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
            if (title[0]) SetWindowTextW(g_hwnd, title);
            CoTaskMemFree(title);
        }
        return S_OK;
    }
};

}  // namespace

HRESULT ControllerHandler::Invoke(HRESULT result, ICoreWebView2Controller* controller) {
    if (FAILED(result) || !controller) {
        PostMessageW(g_hwnd, WM_APP_WEBFAIL, 0, 0);
        return S_OK;
    }
    controller->AddRef();
    g_controller = controller;
    if (FAILED(controller->get_CoreWebView2(&g_webview)) || !g_webview) {
        PostMessageW(g_hwnd, WM_APP_WEBFAIL, 0, 0);
        return S_OK;
    }

    ICoreWebView2Settings* settings = nullptr;
    if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDevToolsEnabled(TRUE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->Release();
    }

    EventRegistrationToken titleToken{};
    TitleHandler* titleHandler = new TitleHandler();
    g_webview->add_DocumentTitleChanged(titleHandler, &titleToken);
    titleHandler->Release();

    ICoreWebView2Controller2* controller2 = nullptr;
    if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2))) && controller2) {
        COREWEBVIEW2_COLOR bg{255, 8, 11, 16};  // A, R, G, B — фон тёмной темы GUI
        controller2->put_DefaultBackgroundColor(bg);
        controller2->Release();
    }

    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    controller->put_Bounds(rc);
    // Окно создаётся скрытым; без явного IsVisible Chromium не рисует кадры.
    controller->put_IsVisible(TRUE);

    std::wstring url = web_url();
    g_webview->Navigate(url.c_str());
    controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    g_webReady = true;
    log_line("встроенный интерфейс открыт: " + u8(url));
    return S_OK;
}

HRESULT EnvHandler::Invoke(HRESULT result, ICoreWebView2Environment* env) {
    if (FAILED(result) || !env) {
        PostMessageW(g_hwnd, WM_APP_WEBFAIL, 0, 0);
        return S_OK;
    }
    env->AddRef();
    g_env = env;

    ControllerHandler* handler = new ControllerHandler();
    HRESULT hr = env->CreateCoreWebView2Controller(g_hwnd, handler);
    handler->Release();
    if (FAILED(hr)) PostMessageW(g_hwnd, WM_APP_WEBFAIL, 0, 0);
    return S_OK;
}

static bool start_webview() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : L".";
    std::wstring parent = base + L"\\Obmen";
    std::wstring dataDir = parent + L"\\WebView2";
    CreateDirectoryW(parent.c_str(), nullptr);
    CreateDirectoryW(dataDir.c_str(), nullptr);

    // Лоадер WebView2 слинкован статически (WebView2LoaderStatic.lib),
    // поэтому рядом с exe не нужен WebView2Loader.dll.
    EnvHandler* handler = new EnvHandler();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr, handler);
    handler->Release();
    return SUCCEEDED(hr);
}

static void resize_webview() {
    if (!g_controller) return;
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    g_controller->put_Bounds(rc);
}

// ------------------------------------------------------------- window

static HMENU make_menu() {
    HMENU m = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_MENU_OPENSHARE, L"Открыть папку обмена");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_MENU_HIDE, L"Свернуть в трей");
    AppendMenuW(file, MF_STRING, ID_MENU_EXIT, L"Выход");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)file, L"Файл");

    HMENU srv = CreatePopupMenu();
    AppendMenuW(srv, MF_STRING, ID_MENU_SYNC, L"Синхронизировать сейчас");
    AppendMenuW(srv, MF_STRING, ID_MENU_COPYADDR, L"Копировать адрес");
    AppendMenuW(srv, MF_STRING, ID_MENU_WEB, L"Открыть веб-интерфейс в браузере");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)srv, L"Сервер");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_MENU_ABOUT, L"О программе");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)help, L"Справка");
    return m;
}

static void on_command(int id) {
    switch (id) {
    case ID_MENU_OPENSHARE:
    case ID_TRAY_OPENSHARE:
        open_share_folder();
        break;
    case ID_MENU_SYNC:
    case ID_TRAY_SYNC:
        sync::push_now();
        break;
    case ID_MENU_COPYADDR:
    case ID_TRAY_COPY:
        copy_to_clipboard(server_addr());
        break;
    case ID_MENU_WEB:
        open_in_browser();
        break;
    case ID_MENU_HIDE:
        hide_to_tray();
        break;
    case ID_MENU_EXIT:
    case ID_TRAY_EXIT:
        g_exiting = true;
        DestroyWindow(g_hwnd);
        break;
    case ID_TRAY_SHOW:
        show_main_window();
        break;
    case ID_MENU_ABOUT: {
        std::wstring text = L"Обмен 1.0\n\nОбщая папка в локальной сети.\n\n";
        text += L"Папка: " + first_share_path().wstring() + L"\n";
        text += L"Адрес: " + server_addr() + L"\n";
        text += L"Веб-интерфейс: " + web_url();
        MessageBoxW(g_hwnd, text.c_str(), L"О программе", MB_ICONINFORMATION | MB_OK);
        break;
    }
    default:
        break;
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        SetTimer(hwnd, 1, 3000, nullptr);
        return 0;
    case WM_SIZE:
        resize_webview();
        return 0;
    case WM_TIMER:
        // Обновляем подсказку трея, если папку обмена сменили из веб-интерфейса.
        if (g_trayAdded) {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            nid.uFlags = NIF_TIP;
            std::filesystem::path root = first_share_path();
            std::wstring tip = L"Обмен — ";
            tip += root.empty() ? L"папка не выбрана" : root.wstring();
            if (tip.size() > 120) tip = tip.substr(0, 120);
            wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
        return 0;
    case WM_COMMAND:
        on_command(LOWORD(wp));
        return 0;
    case WM_APP_TRAY: {
        UINT ev = (UINT)lp;
        if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) tray_menu();
        else if (ev == WM_LBUTTONDBLCLK || ev == WM_LBUTTONUP) show_main_window();
        return 0;
    }
    case WM_APP_WEBFAIL:
        if (!g_fallbackDone) {
            g_fallbackDone = true;
            log_line("WebView2 недоступен — открываю интерфейс в браузере");
            MessageBoxW(hwnd,
                        L"Не удалось запустить встроенный просмотр (WebView2).\n\n"
                        L"Интерфейс открыт в браузере. Установите компонент "
                        L"«Microsoft Edge WebView2 Runtime», чтобы окно работало без браузера.",
                        L"Обмен", MB_ICONWARNING | MB_OK);
            open_in_browser();
        }
        return 0;
    case WM_CLOSE:
        if (g_exiting) {
            DestroyWindow(hwnd);
        } else {
            hide_to_tray();
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_trayAdded) {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            g_trayAdded = false;
        }
        if (g_controller) {
            g_controller->Close();
            g_controller->Release();
            g_controller = nullptr;
        }
        if (g_webview) {
            g_webview->Release();
            g_webview = nullptr;
        }
        if (g_env) {
            g_env->Release();
            g_env = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        if (g_taskbarCreated != 0 && msg == g_taskbarCreated) {
            g_trayAdded = false;
            add_tray_icon();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool available() { return true; }

int run(bool hidden, bool keepConsole) {
    SetProcessDPIAware();
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    (void)hrCo;

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    g_iconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    g_iconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!g_iconBig) g_iconBig = LoadIconW(nullptr, IDI_APPLICATION);
    if (!g_iconSmall) g_iconSmall = g_iconBig;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hIcon = g_iconBig;
    wc.hIconSm = g_iconSmall;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(8, 11, 16));
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) return -1;

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    RECT rc{0, 0, 1180, 820};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, TRUE, 0);
    HWND hwnd = CreateWindowExW(0, kClass, L"Обмен — общая папка", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, make_menu(), hInst, nullptr);
    if (!hwnd) {
        if (g_iconBig) DestroyIcon(g_iconBig);
        if (g_iconSmall && g_iconSmall != g_iconBig) DestroyIcon(g_iconSmall);
        g_iconBig = g_iconSmall = nullptr;
        return -1;
    }
    g_hwnd = hwnd;
    log_line("окно программы создано");

    if (!keepConsole) {
        HWND con = GetConsoleWindow();
        if (con) ShowWindow(con, SW_HIDE);
    }

    add_tray_icon();
    if (!start_webview()) {
        PostMessageW(hwnd, WM_APP_WEBFAIL, 0, 0);
    }

    ShowWindow(hwnd, hidden ? SW_HIDE : SW_SHOW);
    if (!hidden) UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_iconBig) DestroyIcon(g_iconBig);
    if (g_iconSmall && g_iconSmall != g_iconBig) DestroyIcon(g_iconSmall);
    g_iconBig = g_iconSmall = nullptr;
    CoUninitialize();
    return 0;
}

}  // namespace ui

#else  // не Windows

namespace ui {

bool available() { return false; }

int run(bool, bool) { return -1; }

}  // namespace ui

#endif