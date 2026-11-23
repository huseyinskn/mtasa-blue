/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        launch/Main.cpp
 *  PURPOSE:     Launcher
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "RebornServer.h"
#include "resource.h"

// C++ STL
#include <array>     // std::array for safe command buffer
#include <bit>       // std::bit_cast for safe function pointer conversion
#include <expected>  // std::expected/std::unexpected for error handling
#include <memory>
#include <format>       // std::format for string formatting
#include <ranges>       // std::ranges::copy/copy_n for safe copying
#include <span>         // std::span for memory view with bounds checking
#include <string_view>  // std::string_view for string parameters and constants
#include <type_traits>  // std::is_trivially_copyable_v for compile-time checks
#include <utility>      // std::forward for perfect forwarding

// C STL
#include <cstring>  // std::strlen for C string length

// Platform-specific headers
#include <gdiplus.h>
#include <psapi.h>  // GetModuleInformation, MODULEINFO

#pragma comment(lib, "Gdiplus.lib")

/*
    IMPORTANT

    If this project changes, a new release build should be copied into
    the Shared\data\launchers directory.

    The .exe in Shared\data\launchers will be used by the installer and updater.

    (set flag.new_client_exe on the build server to generate new exe)
*/

namespace mta::launcher
{
    enum class LoadResult : std::uint32_t
    {
        Success = 0,
        PathError = 1,
        LoadError = 2,
        FunctionError = 3
    };

    enum class DllLoadError : std::uint32_t
    {
        FileNotFound = 1,
        InvalidPath = 2,
        LoadFailed = 3
    };

    // concepts for type safety
    template <std::size_t N>
    concept ValidBufferSize = N > 0 && N <= 65536;

#ifdef MTA_DEBUG
    constexpr std::string_view LOADER_DLL = "loader_d.dll";
#else
    constexpr std::string_view LOADER_DLL = "loader.dll";
#endif

    // RAII wrapper for preserving GetLastError
    class [[nodiscard]] LastErrorPreserver
    {
        DWORD saved_error_;

    public:
        LastErrorPreserver() noexcept : saved_error_(GetLastError()) {}

        ~LastErrorPreserver() noexcept { SetLastError(saved_error_); }

        LastErrorPreserver(const LastErrorPreserver&) = delete;
        LastErrorPreserver& operator=(const LastErrorPreserver&) = delete;
        LastErrorPreserver(LastErrorPreserver&&) = delete;
        LastErrorPreserver& operator=(LastErrorPreserver&&) = delete;
    };

    // RAII scope_exit with perfect forwarding
    template <typename F>
    struct [[nodiscard]] scope_exit
    {
        F f;

        explicit scope_exit(F&& func) noexcept : f(std::forward<F>(func)) {}

        ~scope_exit() noexcept { f(); }

        scope_exit(const scope_exit&) = delete;
        scope_exit& operator=(const scope_exit&) = delete;
        scope_exit(scope_exit&&) = delete;
        scope_exit& operator=(scope_exit&&) = delete;
    };

    template <typename F>
    constexpr auto make_scope_exit(F&& f) noexcept
    {
        return scope_exit<F>{std::forward<F>(f)};
    }

    // DLL directory guard with error preservation and optimized allocation
    class [[nodiscard]] DllDirectoryGuard
    {
        std::wstring original_dir;
        bool         changed = false;

    public:
        DllDirectoryGuard()
        {
            LastErrorPreserver error_guard;
            DWORD              len = GetDllDirectoryW(0, nullptr);
            if (len > 0)
            {
                try
                {
                    original_dir.resize_and_overwrite(len,
                                                      [](wchar_t* buf, std::size_t n) -> std::size_t
                                                      {
                                                          DWORD copied = GetDllDirectoryW(static_cast<DWORD>(n), buf);
                                                          return copied < n ? copied : 0;
                                                      });
                }
                catch (...)
                {
                    original_dir.resize(len);
                    if (GetDllDirectoryW(len, original_dir.data()) == 0)
                        original_dir.clear();
                }
            }
        }

        [[nodiscard]] bool SetDirectory(const wchar_t* path) noexcept
        {
            if (SetDllDirectoryW(path))
            {
                changed = true;
                return true;
            }
            return false;
        }

        ~DllDirectoryGuard() noexcept
        {
            if (changed)
                SetDllDirectoryW(original_dir.empty() ? nullptr : original_dir.c_str());
        }

        DllDirectoryGuard(const DllDirectoryGuard&) = delete;
        DllDirectoryGuard& operator=(const DllDirectoryGuard&) = delete;
        DllDirectoryGuard(DllDirectoryGuard&&) = delete;
        DllDirectoryGuard& operator=(DllDirectoryGuard&&) = delete;
    };

    // COM initialization with error handling
    [[nodiscard]] std::expected<void, HRESULT> InitializeCOM() noexcept
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(hr))
            return std::unexpected(hr);
        return {};
    }

    // Library loading with optimized string conversion
    [[nodiscard]] std::expected<HMODULE, DllLoadError> LoadLibrarySafe(const SString& dll_path, std::string_view dll_name, const SString& mta_path)
    {
        WString dll_path_w = FromUTF8(dll_path);
        if (dll_path_w.empty()) [[unlikely]]
            return std::unexpected(DllLoadError::InvalidPath);

        if (HMODULE module = LoadLibraryW(dll_path_w.c_str()))
            return module;

        DllDirectoryGuard dll_guard;
        WString           mta_path_w = FromUTF8(mta_path);
        std::wstring      dll_name_w;
        try
        {
            dll_name_w.resize_and_overwrite(dll_name.size() * 2,
                                            [&dll_name](wchar_t* buf, std::size_t n) -> std::size_t
                                            {
                                                SString temp{dll_name.data(), dll_name.size()};
                                                WString wide = FromUTF8(temp);
                                                if (wide.empty())
                                                    return 0;
                                                std::size_t len = std::min(n, wide.length());
                                                std::ranges::copy_n(wide.c_str(), len, buf);
                                                return len;
                                            });
        }
        catch (...)
        {
            SString temp{dll_name.data(), dll_name.size()};
            WString wide = FromUTF8(temp);
            dll_name_w.assign(wide.begin(), wide.end());
        }

        if (mta_path_w.empty() || dll_name_w.empty()) [[unlikely]]
            return std::unexpected(DllLoadError::InvalidPath);

        if (!dll_guard.SetDirectory(mta_path_w.c_str()))
            return std::unexpected(DllLoadError::LoadFailed);

        std::wstring full_path_w = std::wstring(mta_path_w.c_str()) + L"\\" + dll_name_w;
        if (HMODULE module = LoadLibraryW(full_path_w.c_str()))
            return module;

        return std::unexpected(DllLoadError::LoadFailed);
    }

    // Function execution with bounds checking and secure conversion
    [[nodiscard]] std::expected<int, LoadResult> ExecuteDoWinMain(HMODULE module, HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
    {
        using DoWinMainFunc = int (*)(HINSTANCE, HINSTANCE, LPSTR, int);

        if (!module) [[unlikely]]
            return std::unexpected(LoadResult::FunctionError);

        FARPROC proc = GetProcAddress(module, "DoWinMain");
        if (!proc) [[unlikely]]
        {
            AddReportLog(5713, "Launcher Main: DoWinMain function not found");
            return std::unexpected(LoadResult::FunctionError);
        }

        // Validate function pointer within module bounds
        MODULEINFO info{};
        if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
            return std::unexpected(LoadResult::FunctionError);

        std::span<const std::byte> module_span{static_cast<const std::byte*>(info.lpBaseOfDll), info.SizeOfImage};
        auto                       func_ptr = reinterpret_cast<const std::byte*>(proc);

        if (func_ptr < module_span.data() || func_ptr >= (module_span.data() + module_span.size()))
        {
            AddReportLog(5713, "Launcher Main: Function pointer outside module bounds");
            return std::unexpected(LoadResult::FunctionError);
        }

        // Command line handling
        constexpr std::size_t MAX_CMD = 8192;
        static_assert(ValidBufferSize<MAX_CMD>);

        std::array<char, MAX_CMD> safe_cmd{};
        if (cmd)
        {
            auto src_view = std::string_view{cmd, std::min(std::strlen(cmd), MAX_CMD - 1)};
            std::ranges::copy(src_view, safe_cmd.begin());
        }

        // Function pointer conversion with safety checks
        static_assert(sizeof(FARPROC) == sizeof(DoWinMainFunc), "Function pointer size mismatch");
        static_assert(alignof(FARPROC) == alignof(DoWinMainFunc), "Function pointer alignment mismatch");
        static_assert(std::is_trivially_copyable_v<FARPROC>, "FARPROC must be trivially copyable for bit_cast");
        static_assert(std::is_trivially_copyable_v<DoWinMainFunc>, "DoWinMainFunc must be trivially copyable for bit_cast");

        auto do_win_main = std::bit_cast<DoWinMainFunc>(proc);
        if (!do_win_main) [[unlikely]]
            return std::unexpected(LoadResult::FunctionError);

        try
        {
            return do_win_main(inst, prev, safe_cmd.data(), show);
        }
        catch (...)
        {
            AddReportLog(5722, "Launcher Main: Exception in DoWinMain");
            return std::unexpected(LoadResult::FunctionError);
        }
    }

    struct RebornDialog
    {
        HBRUSH                           background = nullptr;
        HFONT                            titleFont = nullptr;
        ULONG_PTR                        imageRuntime = 0;
        IStream*                         logoStream = nullptr;
        std::unique_ptr<Gdiplus::Bitmap> logo;

        ~RebornDialog()
        {
            logo.reset();
            if (logoStream)
                logoStream->Release();
            if (imageRuntime)
                Gdiplus::GdiplusShutdown(imageRuntime);
            if (titleFont)
                DeleteObject(titleFont);
            if (background)
                DeleteObject(background);
        }

        void LoadLogo(HWND dialog)
        {
            const auto  module = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
            const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_REBORN_LOGO), MAKEINTRESOURCEW(10));
            if (!resource)
                return;
            const HGLOBAL data = LoadResource(module, resource);
            if (!data)
                return;

            // The layout embeds the original PNG from the Reborn auth resource.
            logoStream = SHCreateMemStream(static_cast<const BYTE*>(LockResource(data)), SizeofResource(module, resource));
            if (!logoStream)
                return;

            Gdiplus::GdiplusStartupInput startup;
            if (Gdiplus::GdiplusStartup(&imageRuntime, &startup, nullptr) == Gdiplus::Ok)
                logo = std::make_unique<Gdiplus::Bitmap>(logoStream);
        }

        static bool IsNicknameValid(const char* input)
        {
            const std::string_view nickname = input;
            if (nickname.size() < 3 || nickname.size() > 22 || _stricmp(input, "admin") == 0 || _stricmp(input, "console") == 0 ||
                _stricmp(input, "server") == 0)
                return false;

            return std::ranges::all_of(nickname, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
        }

        static bool Launch(std::string_view nickname)
        {
            // Use the executable path directly so Windows never routes the URI to a stock MTA installation.
            std::wstring executable(32768, L'\0');
            const DWORD  length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            if (!length || length >= executable.size())
                return false;
            executable.resize(length);

            const auto   uri = FromUTF8(std::format("mtasa://{}@{}:{}", nickname, Reborn::ServerHost, Reborn::ServerPort));
            std::wstring command = L"\"" + executable + L"\" " + uri.c_str();
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION child{};
            if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &child))
                return false;

            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
            return true;
        }

        static INT_PTR CALLBACK HandleMessage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto* state = reinterpret_cast<RebornDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
            if (!state && message != WM_INITDIALOG)
                return FALSE;

            switch (message)
            {
                case WM_INITDIALOG:
                {
                    state = reinterpret_cast<RebornDialog*>(lParam);
                    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
                    state->background = CreateSolidBrush(RGB(18, 26, 40));
                    state->LoadLogo(dialog);
                    RECT titleSize{0, 0, 0, 16};
                    MapDialogRect(dialog, &titleSize);
                    state->titleFont = CreateFontW(-titleSize.bottom, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                    if (state->titleFont)
                        SendDlgItemMessageW(dialog, IDC_REBORN_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);

                    const auto endpoint = FromUTF8(std::format("{}:{}", Reborn::ServerHost, Reborn::ServerPort));
                    SetDlgItemTextW(dialog, IDC_REBORN_ENDPOINT, endpoint.c_str());
                    SendDlgItemMessageW(dialog, IDC_REBORN_NICKNAME, EM_SETLIMITTEXT, 22, 0);
                    const auto nickname = GetApplicationSetting("reborn-launcher-nickname");
                    if (IsNicknameValid(nickname.c_str()))
                        SetDlgItemTextW(dialog, IDC_REBORN_NICKNAME, FromUTF8(nickname).c_str());
                    return TRUE;
                }
                case WM_DRAWITEM:
                {
                    const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                    if (item.CtlID != IDC_REBORN_LOGO)
                        return FALSE;

                    FillRect(item.hDC, &item.rcItem, state->background);
                    if (state->logo && state->logo->GetLastStatus() == Gdiplus::Ok)
                    {
                        const int         size = std::min(item.rcItem.right - item.rcItem.left, item.rcItem.bottom - item.rcItem.top);
                        Gdiplus::Graphics graphics(item.hDC);
                        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                        graphics.DrawImage(state->logo.get(), static_cast<int>(item.rcItem.left), static_cast<int>(item.rcItem.top), size, size);
                    }
                    return TRUE;
                }
                case WM_CTLCOLORDLG:
                    return reinterpret_cast<INT_PTR>(state->background);
                case WM_CTLCOLORSTATIC:
                    SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
                    SetTextColor(reinterpret_cast<HDC>(wParam),
                                 GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == IDC_REBORN_TITLE ? RGB(89, 234, 183) : RGB(212, 224, 239));
                    return reinterpret_cast<INT_PTR>(state->background);
                case WM_COMMAND:
                    if (LOWORD(wParam) == IDOK)
                    {
                        wchar_t input[23]{};
                        GetDlgItemTextW(dialog, IDC_REBORN_NICKNAME, input, static_cast<int>(std::size(input)));
                        const auto nickname = ToUTF8(input);
                        if (!IsNicknameValid(nickname.c_str()))
                        {
                            SetDlgItemTextW(dialog, IDC_REBORN_STATUS, L"3\u201322 karakter kullan: harf, rakam veya alt cizgi.");
                            SetFocus(GetDlgItem(dialog, IDC_REBORN_NICKNAME));
                            return TRUE;
                        }
                        const auto root = GetLaunchPath();
                        // Report partial extractions before starting the underlying game loader.
                        if (!FileExists(PathJoin(root, "mta", LOADER_DLL.data())) || !FileExists(PathJoin(root, "mods", "deathmatch", "client.dll")))
                        {
                            SetDlgItemTextW(dialog, IDC_REBORN_STATUS, L"Oyun dosyalar\u0131 eksik. ZIP paketinin tamam\u0131n\u0131 \u00e7\u0131kar.");
                            return TRUE;
                        }
                        if (!Launch(nickname.c_str()))
                        {
                            SetDlgItemTextW(dialog, IDC_REBORN_STATUS, L"Oyun baslatilamadi. Paketi yeniden cikarmayi dene.");
                            return TRUE;
                        }
                        SetApplicationSetting("reborn-launcher-nickname", nickname);
                        EndDialog(dialog, IDOK);
                        return TRUE;
                    }
                    if (LOWORD(wParam) == IDCANCEL)
                    {
                        EndDialog(dialog, IDCANCEL);
                        return TRUE;
                    }
                    break;
            }
            return FALSE;
        }
    };

    // Path validation
    [[nodiscard]] constexpr bool ValidatePathSafety(std::string_view path) noexcept
    {
        return path.find("..") == std::string_view::npos && path.find("//") == std::string_view::npos;
    }

    // Path discovery with validation
    [[nodiscard]] std::expected<SString, LoadResult> FindMtaPath(const SString& launch_path)
    {
        if (!ValidatePathSafety(launch_path)) [[unlikely]]
        {
            AddReportLog(5731, std::format("Launcher Main: Invalid launch path: '{}'", std::string_view{launch_path}));
            return std::unexpected(LoadResult::PathError);
        }

        SString mta_path = PathJoin(launch_path, "mta");
        if (!ValidatePathSafety(mta_path)) [[unlikely]]
            return std::unexpected(LoadResult::PathError);

        if (DirectoryExists(mta_path))
            return mta_path;

        SString parent = PathJoin(launch_path, "..");
        if (!ValidatePathSafety(parent)) [[unlikely]]
            return std::unexpected(LoadResult::PathError);

        SString alt = PathJoin(parent, "mta");

        if (ValidatePathSafety(alt) && DirectoryExists(alt))
            return alt;

        AddReportLog(5730, std::format("Launcher Main: MTA directory not found from: '{}'", std::string_view{launch_path}));
        return std::unexpected(LoadResult::PathError);
    }
}  // namespace mta::launcher

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    using namespace mta::launcher;

    // Configure silent error handling
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    if (!lpCmdLine || !*lpCmdLine)
    {
        RebornDialog launcher;
        if (DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_REBORN_LAUNCHER), nullptr, RebornDialog::HandleMessage, reinterpret_cast<LPARAM>(&launcher)) == -1)
        {
            MessageBoxW(nullptr, L"Reborn Roleplay baslaticisi acilamadi.", L"Reborn Roleplay", MB_OK | MB_ICONERROR);
            return 1;
        }
        return 0;
    }

    // COM initialization
    auto com_result = InitializeCOM();
    if (!com_result) [[unlikely]]
    {
        AddReportLog(5737, std::format("Launcher Main: COM initialization failed (hr=0x{:08X})", static_cast<DWORD>(com_result.error())));
    }

    auto com_cleanup = make_scope_exit([]() noexcept { CoUninitialize(); });

    // Set taskbar grouping
    [[maybe_unused]] HRESULT hr = SetCurrentProcessExplicitAppUserModelID(L"Multi Theft Auto " MTA_STR(MTASA_VERSION_MAJOR) L"." MTA_STR(MTASA_VERSION_MINOR));

    // Path discovery
    SString launch_path = GetLaunchPath();
    auto    mta_path_result = FindMtaPath(launch_path);

    if (!mta_path_result) [[unlikely]]
    {
        return std::to_underlying(mta_path_result.error());
    }

    SString mta_path = mta_path_result.value();
    SString dll_path = PathJoin(mta_path, SString{LOADER_DLL.data(), LOADER_DLL.size()});

    AddReportLog(5725, SString("Launcher Main: Launch: '%s', MTA: '%s'", launch_path.c_str(), mta_path.c_str()));

    // DLL loading
    auto module_result = LoadLibrarySafe(dll_path, LOADER_DLL, mta_path);

    if (!module_result) [[unlikely]]
    {
        DWORD   error = GetLastError();
        SString msg = std::format("Launcher Main: Failed to load: '{}'\n\n{}", dll_path.data(), GetSystemErrorMessage(error).data());
        AddReportLog(5711, msg);

        // Pattern matching
        switch (module_result.error())
        {
            case DllLoadError::FileNotFound:
                AddReportLog(5734, "Launcher Main: Build the 'Loader' project to create loader_d.dll");
                BrowseToSolution("loader-dll-missing", ASK_GO_ONLINE, msg);
                break;
            case DllLoadError::InvalidPath:
            case DllLoadError::LoadFailed:
                BrowseToSolution("loader-dll-not-loadable", ASK_GO_ONLINE, msg);
                break;
        }

        return std::to_underlying(LoadResult::LoadError);
    }

    HMODULE module = module_result.value();

    // RAII module cleanup
    auto module_cleanup = make_scope_exit(
        [module]() noexcept
        {
            if (module)
                FreeLibrary(module);
        });

    // Execution with concepts
    auto exec_result = ExecuteDoWinMain(module, hInstance, hPrevInstance, lpCmdLine, nCmdShow);

    return exec_result.value_or(std::to_underlying(LoadResult::FunctionError));
}
