#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#pragma comment(lib, "winhttp.lib")

struct Config {
    std::wstring teams_url;
    int check_interval_sec = 10;
    int response_timeout_ms = 3000;
    int required_failures = 2;
    int alert_cooldown_sec = 300;
    bool auto_restart = false;
    int restart_delay_sec = 5;
    std::wstring obs_exe = L"C:\\Program Files\\obs-studio\\bin\\64bit\\obs64.exe";
};

static std::wstring trim(const std::wstring &s)
{
    const wchar_t *ws = L" \t\r\n";
    size_t a = s.find_first_not_of(ws);
    if (a == std::wstring::npos)
        return L"";
    size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

static Config load_config(const std::wstring &path)
{
    Config c;
    std::wifstream f(path);
    if (!f)
        return c;

    std::wstring line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';')
            continue;

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos)
            continue;

        std::wstring key = trim(line.substr(0, eq));
        std::wstring value = trim(line.substr(eq + 1));

        if (key == L"TeamsUrl")
            c.teams_url = value;
        else if (key == L"CheckIntervalSeconds")
            c.check_interval_sec = _wtoi(value.c_str());
        else if (key == L"ResponseTimeoutMilliseconds")
            c.response_timeout_ms = _wtoi(value.c_str());
        else if (key == L"RequiredFailedChecks")
            c.required_failures = _wtoi(value.c_str());
        else if (key == L"AlertCooldownSeconds")
            c.alert_cooldown_sec = _wtoi(value.c_str());
        else if (key == L"AutoRestartOBS")
            c.auto_restart = (_wtoi(value.c_str()) != 0);
        else if (key == L"RestartDelaySeconds")
            c.restart_delay_sec = _wtoi(value.c_str());
        else if (key == L"OBSExe")
            c.obs_exe = value;
    }

    if (c.check_interval_sec < 1)
        c.check_interval_sec = 10;
    if (c.response_timeout_ms < 500)
        c.response_timeout_ms = 3000;
    if (c.required_failures < 1)
        c.required_failures = 2;
    if (c.alert_cooldown_sec < 1)
        c.alert_cooldown_sec = 300;
    if (c.restart_delay_sec < 0)
        c.restart_delay_sec = 5;

    return c;
}

static void log_line(const std::wstring &msg, const std::wstring &path)
{
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t ts[64];
    swprintf_s(
        ts,
        L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond);

    std::wofstream f(path, std::ios::app);
    if (f)
        f << ts << msg << L"\n";
}

static bool is_obs_running(DWORD *pid_out = nullptr)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    bool found = false;
    DWORD pid = 0;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"obs64.exe") == 0) {
                found = true;
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);

    if (pid_out)
        *pid_out = pid;

    return found;
}

struct WindowSearch {
    DWORD pid;
    HWND hwnd = nullptr;
};

static BOOL CALLBACK enum_windows_for_pid(HWND hwnd, LPARAM lparam)
{
    auto *s = reinterpret_cast<WindowSearch *>(lparam);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid != s->pid || !IsWindowVisible(hwnd))
        return TRUE;

    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, _countof(title));

    if (title[0] != L'\0') {
        s->hwnd = hwnd;
        return FALSE;
    }

    return TRUE;
}

static HWND find_obs_main_window(DWORD pid)
{
    WindowSearch s{pid, nullptr};
    EnumWindows(enum_windows_for_pid, reinterpret_cast<LPARAM>(&s));
    return s.hwnd;
}

static bool obs_gui_responding(DWORD pid, int timeout_ms)
{
    HWND hwnd = find_obs_main_window(pid);
    if (!hwnd)
        return false;

    DWORD_PTR result = 0;

    LRESULT response = SendMessageTimeoutW(
        hwnd,
        WM_NULL,
        0,
        0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        static_cast<UINT>(timeout_ms),
        &result);

    return response != 0;
}

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
};

static bool parse_url(const std::wstring &url, UrlParts &out)
{
    URL_COMPONENTSW uc{};
    wchar_t host[256]{};
    wchar_t path[4096]{};

    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = _countof(path);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        return false;

    out.host.assign(host, uc.dwHostNameLength);
    out.path.assign(path, uc.dwUrlPathLength);
    out.port = uc.nPort;
    out.secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    if (out.path.empty())
        out.path = L"/";

    return true;
}

static std::string wide_to_utf8(const std::wstring &s)
{
    if (s.empty())
        return {};

    int bytes = WideCharToMultiByte(
        CP_UTF8,
        0,
        s.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);

    if (bytes <= 1)
        return {};

    std::string out(bytes - 1, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        s.c_str(),
        -1,
        out.data(),
        bytes,
        nullptr,
        nullptr);

    return out;
}

static std::string json_escape_utf8(const std::string &input)
{
    std::string out;
    out.reserve(input.size() + 32);

    for (unsigned char ch : input) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", ch);
                out += buf;
            } else {
                out += static_cast<char>(ch);
            }
            break;
        }
    }

    return out;
}

static bool send_teams(const Config &c, const std::wstring &message, const std::wstring &log_path)
{
    if (c.teams_url.empty()) {
        log_line(L"Teams URL not configured; alert not sent.", log_path);
        return false;
    }

    UrlParts u;
    if (!parse_url(c.teams_url, u)) {
        log_line(L"Invalid Teams/Power Automate URL.", log_path);
        return false;
    }

    HINTERNET session = WinHttpOpen(
        L"KMBC-OBS-Watchdog/AdaptiveCard",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!session)
        return false;

    HINTERNET conn = WinHttpConnect(session, u.host.c_str(), u.port, 0);
    if (!conn) {
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = u.secure ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET req = WinHttpOpenRequest(
        conn,
        L"POST",
        u.path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    std::string text = json_escape_utf8(wide_to_utf8(message));

    /*
     * Power Automate flow expects an Adaptive Card object directly.
     * This matches the successful PowerShell test:
     *
     * {
     *   "type": "AdaptiveCard",
     *   "$schema": "http://adaptivecards.io/schemas/adaptive-card.json",
     *   "version": "1.5",
     *   "body": [...]
     * }
     */
    std::string body =
        "{\"type\":\"AdaptiveCard\","
        "\"$schema\":\"http://adaptivecards.io/schemas/adaptive-card.json\","
        "\"version\":\"1.5\","
        "\"body\":["
        "{\"type\":\"TextBlock\","
        "\"text\":\"" + text + "\","
        "\"wrap\":true}"
        "]}";

    const wchar_t headers[] = L"Content-Type: application/json\r\n";

    BOOL ok = WinHttpSendRequest(
        req,
        headers,
        static_cast<DWORD>(-1L),
        body.data(),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);

    DWORD status = 0;
    DWORD status_size = sizeof(status);

    if (ok)
        ok = WinHttpReceiveResponse(req, nullptr);

    if (ok) {
        WinHttpQueryHeaders(
            req,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (!ok) {
        log_line(L"Teams HTTP request failed.", log_path);
        return false;
    }

    std::wstringstream ss;
    ss << L"Teams Adaptive Card HTTP status: " << status;
    log_line(ss.str(), log_path);

    return status >= 200 && status < 300;
}

static bool start_obs(const Config &c, const std::wstring &log_path)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi{};

    std::wstring cmd = L"\"" + c.obs_exe + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    std::wstring dir = c.obs_exe;
    size_t slash = dir.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
        dir.resize(slash);

    BOOL ok = CreateProcessW(
        nullptr,
        buf.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        dir.empty() ? nullptr : dir.c_str(),
        &si,
        &pi);

    if (!ok) {
        std::wstringstream ss;
        ss << L"Failed to start OBS. Win32 error " << GetLastError();
        log_line(ss.str(), log_path);
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    log_line(L"Started OBS.", log_path);
    return true;
}

int wmain()
{
    wchar_t exe_path[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(
        nullptr,
        exe_path,
        _countof(exe_path));

    if (!n)
        return 1;

    std::wstring dir(exe_path, n);
    size_t slash = dir.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
        dir.resize(slash);
    else
        dir = L".";

    Config c = load_config(dir + L"\\config.ini");
    std::wstring log_path = dir + L"\\obs_watchdog.log";

    log_line(L"OBS Watchdog (WM_NULL + Adaptive Card) started.", log_path);

    bool unhealthy = false;
    int failures = 0;
    ULONGLONG last_alert = 0;

    for (;;) {
        DWORD pid = 0;

        if (!is_obs_running(&pid)) {
            failures = 0;

            if (!unhealthy) {
                unhealthy = true;
                log_line(L"OBS is NOT RUNNING.", log_path);
            }

            ULONGLONG now = GetTickCount64();

            if (!last_alert ||
                now - last_alert >=
                    static_cast<ULONGLONG>(c.alert_cooldown_sec) * 1000ULL) {

                send_teams(
                    c,
                    L"🚨 KMBC OBS WATCHDOG\n\n"
                    L"OBS Studio is NOT RUNNING.\n\n"
                    L"obs64.exe was not found. It may have crashed or been closed.",
                    log_path);

                last_alert = now;
            }

            if (c.auto_restart) {
                Sleep(static_cast<DWORD>(c.restart_delay_sec) * 1000UL);

                if (!is_obs_running())
                    start_obs(c, log_path);
            }
        } else {
            if (obs_gui_responding(pid, c.response_timeout_ms)) {
                if (unhealthy) {
                    log_line(L"OBS is responding again.", log_path);

                    send_teams(
                        c,
                        L"✅ KMBC OBS WATCHDOG\n\n"
                        L"OBS Studio is responding again.",
                        log_path);
                }

                unhealthy = false;
                failures = 0;
            } else {
                ++failures;

                std::wstringstream ss;
                ss << L"OBS is running but WM_NULL timed out. "
                   << L"Consecutive failures: " << failures;
                log_line(ss.str(), log_path);

                if (failures >= c.required_failures && !unhealthy) {
                    unhealthy = true;

                    send_teams(
                        c,
                        L"🚨 KMBC OBS WATCHDOG\n\n"
                        L"OBS Studio appears to be NOT RESPONDING.\n\n"
                        L"obs64.exe is still running, but its main window did not "
                        L"process WM_NULL within the configured timeout.",
                        log_path);

                    last_alert = GetTickCount64();
                }
            }
        }

        Sleep(static_cast<DWORD>(c.check_interval_sec) * 1000UL);
    }
}
