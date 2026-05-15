#include <windows.h>
#include <commctrl.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <shellapi.h>
#include <shobjidl.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")

#define ID_LISTVIEW      101
#define ID_SEARCH        102
#define ID_MENU_ZIP      401
#define ID_MENU_SCAN     404
#define ID_MENU_URL      406
#define ID_MENU_UNINSTALL 407
#define ID_MENU_CONFIG   201
#define ID_MENU_PATH     205
#define ID_MENU_GEN_MAN  501
#define ID_MENU_VER_MAN  502
#define ID_MENU_REPACK   503
#define ID_MENU_DEV_DEPS  504
#define ID_MENU_DROP     405
#define ID_MENU_TOG_ON   601
#define ID_MENU_TOG_OFF  602
#define ID_MENU_TOG_SPEC 603
#define ID_MENU_EXPLORE  701
#define ID_MENU_LOG      702
#define ID_MENU_STATS    901
#define ID_BTN_SAVE_CFG  202
#define ID_BTN_COPY_LOG  801
#define ID_URL_EDIT      1001
#define ID_URL_DOWNLOAD  1002
#define ID_MENU_RESET_CONFIG 1005

HWND hListView, hMainWnd, hSearch;
char ksp_path[MAX_PATH] = {0};
char target_url[2048] = {0};
char ignore_list[1024] = "|Squad|SquadExpansion|"; 
int col_widths[3] = {300, 150, 100};
int g_FontSize = 18;
int g_Opacity = 255;
BOOL g_ListViewGrid = TRUE;
char g_DefaultRepackPath[MAX_PATH] = "";             
HFONT hGlobalFont = NULL;
COLORREF bg_color = 0xFFFFFF;
COLORREF text_color = 0x000000;
BOOL show_grid = TRUE;
WNDPROC OldEditProc;
BOOL g_IsBusy = FALSE;
WCHAR ksp_path_w[MAX_PATH];
void GetKSPPathFromRegistry(char* outPath);

BOOL EndsWith(const char* str, const char* suffix) {
    if (!str || !suffix) return FALSE;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr) return FALSE;
    return _stricmp(str + lenstr - lensuffix, suffix) == 0;
}

void GetModVersion(const char* folderPath, char* outVersion);
unsigned __int64 GetFolderSize(const char* path);

void CopyRecursiveAndLog(const char* src, const char* dst, const char* manifest);
void CheckAndPromptDeps(const char* stagePath, HWND parent);
void ScanGameData();

typedef struct {
    char links[20][2048];
    char names[20][256];
    int count;
} DEP_DATA;

LRESULT CALLBACK DepCheckProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

typedef struct {
    char modPath[MAX_PATH];
    char manifestPath[MAX_PATH];
    char* fileListBuffer;
} UNINSTALL_DATA;

LRESULT CALLBACK UninstallProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);  

void EnsureConfigExists() {
    if (GetFileAttributesA("config.json") != INVALID_FILE_ATTRIBUTES) return;

    char autoPath[MAX_PATH] = {0};
    GetKSPPathFromRegistry(autoPath);

    char escaped[MAX_PATH * 2] = {0};
    int j = 0;
    for (int i = 0; autoPath[i]; i++) {
        if (autoPath[i] == '\\') {
            escaped[j++] = '\\';
            escaped[j++] = '\\';
        } else {
            escaped[j++] = autoPath[i];
        }
    }
    escaped[j] = '\0';

    FILE* f = fopen("config.json", "w");
    if (f) {
        fprintf(f, "{\n"
                   "  \"Path\": \"%s\",\n"
                   "  \"Ignore\": \"Squad|SquadExpansion\",\n"
                   "  \"Width1\": 220, \"Width2\": 100, \"Width3\": 150,\n"
                   "  \"Font\": \"Segoe UI\", \"font_size\": 18, \"opacity\": 255,\n"
                   "  \"list_view_grid\": true, \"default_repack_path\": \"\"\n"
                   "}", escaped);
        fclose(f);
    }
}

COLORREF HexToCol(const char* hex) {
    if (!hex || hex[0] != '#') return 0xFFFFFF;
    unsigned int r, g, b;
    if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) != 3) return 0xFFFFFF;
    return RGB(r, g, b);
}

void ApplyTheme(const char* json) {
    char* p;
    if (json && strlen(json) > 0) {
        if ((p = strstr(json, "\"Background\": \""))) bg_color = HexToCol(p + 15);
        if ((p = strstr(json, "\"Text\": \""))) text_color = HexToCol(p + 9);
        if (strstr(json, "\"Gridlines\": \"False\"")) show_grid = FALSE;
        else show_grid = TRUE;
    }
    ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | (show_grid ? LVS_EX_GRIDLINES : 0));
    ListView_SetBkColor(hListView, bg_color);
    ListView_SetTextBkColor(hListView, bg_color);
    ListView_SetTextColor(hListView, text_color);
    InvalidateRect(hMainWnd, NULL, TRUE);
}

void SaveConfig() {
    char buf[4096];
    char escapedPath[MAX_PATH * 2] = {0};
    
    int j = 0;
    for (int i = 0; ksp_path[i]; i++) {
        if (ksp_path[i] == '\\') { 
            escapedPath[j++] = '\\'; 
            escapedPath[j++] = '\\'; 
        } else { 
            escapedPath[j++] = ksp_path[i]; 
        }
    }
    escapedPath[j] = '\0';

    _snprintf(buf, sizeof(buf),
        "{\n"
        "  \"Path\": \"%s\",\n"
        "  \"Ignore\": \"Squad|SquadExpansion\",\n"
        "  \"Width1\": %d, \"Width2\": %d, \"Width3\": %d,\n"
        "  \"Font\": \"Segoe UI\", \"font_size\": %d, \"opacity\": %d,\n"
        "  \"list_view_grid\": %s, \"default_repack_path\": \"%s\"\n"
        "}",
        escapedPath, col_widths[0], col_widths[1], col_widths[2],
        g_FontSize, g_Opacity, g_ListViewGrid ? "true" : "false", g_DefaultRepackPath
    );

    FILE *f = fopen("config.json", "w");
    if (f) { fputs(buf, f); fclose(f); }
}

void LoadConfig() {
    FILE *f = fopen("config.json", "r");
    
    if (!f) {
        HKEY hKey;
        const char* subkey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 220200";
        char steamPath[MAX_PATH] = {0};
        
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS ||
            RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) 
        {
            DWORD len = MAX_PATH;
            if (RegQueryValueExA(hKey, "InstallLocation", NULL, NULL, (LPBYTE)steamPath, &len) == ERROR_SUCCESS) {
                _snprintf(ksp_path, sizeof(ksp_path), "%s\\GameData", steamPath);
            }
            RegCloseKey(hKey);
        }

        g_Opacity = 255;
        g_FontSize = 18;
        g_ListViewGrid = TRUE;
        col_widths[0] = 220; col_widths[1] = 100; col_widths[2] = 150;

        SaveConfig(); 
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(fsize + 1);
    if (!buf) { fclose(f); return; }

    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);

    char *p;
    if ((p = strstr(buf, "\"Path\": \""))) {
        char *start = p + 9;
        char *end = strstr(start, "\"");
        if (end) {
            int len = (int)(end - start);
            if (len >= MAX_PATH) len = MAX_PATH - 1;
            memcpy(ksp_path, start, len);
            ksp_path[len] = '\0';
        }
    }
    
    if ((p = strstr(buf, "\"font_size\": "))) sscanf(p + 13, "%d", &g_FontSize);
    if ((p = strstr(buf, "\"opacity\": "))) sscanf(p + 11, "%d", &g_Opacity);
    if ((p = strstr(buf, "\"list_view_grid\": "))) {
        g_ListViewGrid = (strstr(p + 18, "true") != NULL);
    }

    if (g_FontSize <= 0) g_FontSize = 16;
    if (g_Opacity <= 0) g_Opacity = 255;

    ApplyTheme(buf);
    free(buf);
}

void ScanGameData() {
    char filter[256]; GetWindowTextA(hSearch, filter, 256);
    ListView_DeleteAllItems(hListView);
    if (!ksp_path[0]) return;
    char s[MAX_PATH]; sprintf(s, "%s\\*", ksp_path);
    WIN32_FIND_DATAA f; HANDLE h = FindFirstFileA(s, &f);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && f.cFileName[0] != '.') {
                if (strcasecmp(f.cFileName, "Squad") == 0 || strcasecmp(f.cFileName, "SquadExpansion") == 0) continue;
                if (filter[0] != '\0' && !StrStrIA(f.cFileName, filter)) continue;
                LVITEMA lv = {LVIF_TEXT, ListView_GetItemCount(hListView), 0}; lv.pszText = f.cFileName;
                int idx = ListView_InsertItem(hListView, &lv);
                char m[MAX_PATH]; sprintf(m, "%s\\%s\\kspman_manifest.txt", ksp_path, f.cFileName);
                ListView_SetItemText(hListView, idx, 1, PathFileExistsA(m) ? "Managed" : "Loose");
                char modPath[MAX_PATH];
                sprintf(modPath, "%s\\%s", ksp_path, f.cFileName);
                char vStr[32];
                GetModVersion(modPath, vStr);
                ListView_SetItemText(hListView, idx, 2, vStr);
            }
        } while (FindNextFileA(h, &f)); FindClose(h);
    }
}

void ToggleMod(const char* name, BOOL enable) {
    char oldP[MAX_PATH], newP[MAX_PATH];
    if (enable) {
        sprintf(oldP, "%s\\%s.disabled", ksp_path, name);
        sprintf(newP, "%s\\%s", ksp_path, name);
    } else {
        sprintf(oldP, "%s\\%s", ksp_path, name);
        sprintf(newP, "%s\\%s.disabled", ksp_path, name);
    }

    if (GetFileAttributesA(newP) != INVALID_FILE_ATTRIBUTES) {
        char msg[512];
        sprintf(msg, "Target path already exists:\n%s\n\nDelete the existing folder and proceed?", newP);
        if (MessageBoxA(hMainWnd, msg, "Conflict", MB_YESNO | MB_ICONWARNING) == IDYES) {
            SHFILEOPSTRUCTA s = {0};
            s.wFunc = FO_DELETE;
            s.pFrom = newP;
            s.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
            SHFileOperationA(&s);
        } else {
            return;
        }
    }

retry_move:
    if (!MoveFileA(oldP, newP)) {
        DWORD err = GetLastError();
        char errBuf[512];
        sprintf(errBuf, "Failed to move folder (Error %lu).\nIs the game or a file open?\n\nPath: %s", err, oldP);
        
        int choice = MessageBoxA(hMainWnd, errBuf, "File Lock", MB_ABORTRETRYIGNORE | MB_ICONERROR);
        if (choice == IDRETRY) goto retry_move;
        if (choice == IDABORT) return;
    }
}

void CheckAndPromptDeps(const char* stagePath, HWND parent) {
    WCHAR wStagePath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, stagePath, -1, wStagePath, MAX_PATH);

    WCHAR wDepFile[MAX_PATH];
    swprintf(wDepFile, MAX_PATH, L"%ls\\dependencies.txt", wStagePath);

    if (GetFileAttributesW(wDepFile) == INVALID_FILE_ATTRIBUTES) {
        WCHAR wSearch[MAX_PATH];
        swprintf(wSearch, MAX_PATH, L"%ls\\*\\dependencies.txt", wStagePath);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(wSearch, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            swprintf(wDepFile, MAX_PATH, L"%ls\\%ls\\dependencies.txt", wStagePath, fd.cFileName);
            FindClose(h);
        }
    }

    if (GetFileAttributesW(wDepFile) == INVALID_FILE_ATTRIBUTES) return;

    DEP_DATA* data = (DEP_DATA*)malloc(sizeof(DEP_DATA));
    if (!data) return;
    memset(data, 0, sizeof(DEP_DATA));

    FILE* f = _wfopen(wDepFile, L"r");
    if (f) {
        char line[2048];
        while (fgets(line, sizeof(line), f) && data->count < 20) {
            if (line[0] == '/' || line[0] == '\n' || strlen(line) < 5) continue;
            char link[2048], ver[64];
            if (sscanf(line, "\"%[^\"]\" \"%[^\"]\"", link, ver) >= 1) {
                strncpy(data->links[data->count], link, 2047);
                _snprintf(data->names[data->count], 2110, "%s (v%s)", link, ver);
                data->count++;
            }
        }
        fclose(f);
    }

    if (data->count > 0) {
        WNDCLASSA dwc = {0};
        dwc.lpfnWndProc = DepCheckProc;
        dwc.hInstance = GetModuleHandle(NULL);
        dwc.hCursor = LoadCursor(NULL, IDC_ARROW);
        dwc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); 
        dwc.lpszClassName = "kspman_dep_ui";
        RegisterClassA(&dwc);

        EnableWindow(parent, FALSE);

        HWND hDlg = CreateWindowExA(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, 
            "kspman_dep_ui", "Dependency Check", 
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 
            CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, 
            parent, NULL, GetModuleHandle(NULL), data);

        MSG msg;
        while (IsWindow(hDlg)) {
            if (GetMessage(&msg, NULL, 0, 0)) {
                if (!IsDialogMessage(hDlg, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            } else {
                PostQuitMessage(msg.wParam);
                break;
            }
        }

        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    
    free(data);
}

void InstallFromURL(HWND hwnd) {
    if (strlen(target_url) < 1) return;

    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    hSession = WinHttpOpen(L"kspman/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        MessageBoxA(hwnd, "Failed to initialize WinHTTP session.", "Error", MB_ICONERROR);
        goto cleanup;
    }

    WCHAR wUrl[2048];
    MultiByteToWideChar(CP_UTF8, 0, target_url, -1, wUrl, 2048);

    URL_COMPONENTS uc = { sizeof(uc) };
    uc.dwHostNameLength = (DWORD)-1; 
    uc.dwUrlPathLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wUrl, 0, 0, &uc)) {
        MessageBoxA(hwnd, "Malformed URL provided.", "Error", MB_ICONERROR);
        goto cleanup;
    }

    WCHAR host[256]; 
    DWORD hLen = (uc.dwHostNameLength >= 256) ? 255 : uc.dwHostNameLength;
    wcsncpy(host, uc.lpszHostName, hLen); 
    host[hLen] = 0;

    hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        MessageBoxA(hwnd, "Failed to connect to server.", "Connection Error", MB_ICONERROR);
        goto cleanup;
    }

    hRequest = WinHttpOpenRequest(hConnect, L"GET", uc.lpszUrlPath, NULL, 
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                  (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    
    if (!hRequest || !WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(hRequest, NULL)) {
        MessageBoxA(hwnd, "Failed to send request or receive response.", "HTTP Error", MB_ICONERROR);
        goto cleanup;
    }

    WCHAR tP[MAX_PATH], zF[MAX_PATH], sD[MAX_PATH];
    GetTempPathW(MAX_PATH, tP);
    swprintf(zF, MAX_PATH, L"%ls\\kman_dl.zip", tP);
    swprintf(sD, MAX_PATH, L"%ls\\kman_st", tP);

    hFile = CreateFileW(zF, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char b[8192]; DWORD d = 0, written = 0;
        while (WinHttpReadData(hRequest, b, sizeof(b), &d) && d > 0) {
            WriteFile(hFile, b, d, &written, NULL);
        }
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;

        CoInitialize(NULL);
        IShellDispatch *pISD;
        if (SUCCEEDED(CoCreateInstance(&CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, &IID_IShellDispatch, (void**)&pISD))) {
            SHCreateDirectoryExW(NULL, sD, NULL);
            Folder *pTo = NULL, *pFrom = NULL;
            VARIANT vZ, vS, vO;
            vZ.vt = VT_BSTR; vZ.bstrVal = SysAllocString(zF);
            vS.vt = VT_BSTR; vS.bstrVal = SysAllocString(sD);

            if (SUCCEEDED(pISD->lpVtbl->NameSpace(pISD, vS, &pTo)) && SUCCEEDED(pISD->lpVtbl->NameSpace(pISD, vZ, &pFrom))) {
                FolderItems *pI = NULL;
                pFrom->lpVtbl->Items(pFrom, &pI);
                if (pI) {
                    vO.vt = VT_I4; vO.lVal = 4 | 16 | 1024; 
                    VARIANT vIt; vIt.vt = VT_DISPATCH; vIt.pdispVal = (IDispatch*)pI;
                    pTo->lpVtbl->CopyHere(pTo, vIt, vO);
                    pI->lpVtbl->Release(pI);
                }
            }
            if (pFrom) pFrom->lpVtbl->Release(pFrom);
            if (pTo) pTo->lpVtbl->Release(pTo);
            SysFreeString(vZ.bstrVal); SysFreeString(vS.bstrVal);
            pISD->lpVtbl->Release(pISD);
        }
        CoUninitialize();

        char sD_A[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, sD, -1, sD_A, MAX_PATH, NULL, NULL);
        CheckAndPromptDeps(sD_A, hwnd);

        WCHAR srcP[MAX_PATH], searchP[MAX_PATH];
        swprintf(srcP, MAX_PATH, L"%ls\\GameData", sD);
        if (GetFileAttributesW(srcP) == INVALID_FILE_ATTRIBUTES) wcsncpy(srcP, sD, MAX_PATH);
        
        WIN32_FIND_DATAW ffd;
        swprintf(searchP, MAX_PATH, L"%ls\\*", srcP);
        HANDLE hF = FindFirstFileW(searchP, &ffd);
        if (hF != INVALID_HANDLE_VALUE) {
            do {
                if (wcsstr(ffd.cFileName, L"..")) continue;
                if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && 
                    wcscmp(ffd.cFileName, L".") != 0 && wcscmp(ffd.cFileName, L"..") != 0) {
                    
                    char mS[MAX_PATH], mD[MAX_PATH], mN[MAX_PATH];
                    WCHAR wMS[MAX_PATH], wMD[MAX_PATH], wKSP[MAX_PATH];
                    
                    MultiByteToWideChar(CP_UTF8, 0, ksp_path, -1, wKSP, MAX_PATH);

                    swprintf(wMS, MAX_PATH, L"%ls\\%ls", srcP, ffd.cFileName);
                    swprintf(wMD, MAX_PATH, L"%ls\\%ls", wKSP, ffd.cFileName);

                    WideCharToMultiByte(CP_UTF8, 0, wMS, -1, mS, MAX_PATH, NULL, NULL);
                    WideCharToMultiByte(CP_UTF8, 0, wMD, -1, mD, MAX_PATH, NULL, NULL);
                    _snprintf(mN, MAX_PATH, "%s\\kspman_manifest.txt", mD);

                    SHCreateDirectoryExA(NULL, mD, NULL);
                    CopyRecursiveAndLog(mS, mD, mN);
                }
            } while (FindNextFileW(hF, &ffd));
            FindClose(hF);
        }
        MessageBoxA(hwnd, "Installation Complete", "Status", MB_OK);
    } else {
        MessageBoxA(hwnd, "Could not create local temp file.", "Error", MB_ICONERROR);
    }

cleanup:
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    
    memset(target_url, 0, 2048);
    ScanGameData();
}   

void CopyRecursiveAndLog_Internal(const char* src, const char* dst, const char* manifestPath) {
    char sPath[MAX_PATH], dPath[MAX_PATH];
    WIN32_FIND_DATAA f;
    sprintf(sPath, "%s\\*", src);
    HANDLE h = FindFirstFileA(sPath, &f);

    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(f.cFileName, ".") == 0 || strcmp(f.cFileName, "..") == 0) continue;

        sprintf(sPath, "%s\\%s", src, f.cFileName);
        sprintf(dPath, "%s\\%s", dst, f.cFileName);

        if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryA(dPath, NULL);
            CopyRecursiveAndLog_Internal(sPath, dPath, manifestPath);
        } else {
            if (CopyFileA(sPath, dPath, FALSE)) {
                FILE* m = fopen(manifestPath, "a");
                if (m) {
                    fprintf(m, "%s\n", dPath);
                    fclose(m);
                }
            }
        }
    } while (FindNextFileA(h, &f));
    FindClose(h);
}

void CopyRecursiveAndLog(const char* src, const char* dst, const char* modName) {
    char manifestPath[MAX_PATH];
    sprintf(manifestPath, "%s\\%s\\kspman_manifest.txt", ksp_path, modName);

    FILE* f = fopen(manifestPath, "w");
    if (f) fclose(f);

    CopyRecursiveAndLog_Internal(src, dst, manifestPath);
}

void GetModVersion(const char* folderPath, char* outVersion) {
    strcpy(outVersion, "Unknown");
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s\\*.version", folderPath);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        char fullPath[MAX_PATH];
        sprintf(fullPath, "%s\\%s", folderPath, fd.cFileName);
        FILE* f = fopen(fullPath, "r");
        if (f) {
            char line[256];
            int maj = 0, min = 0, pat = 0;
            BOOL found = FALSE;
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, "\"MAJOR\"")) sscanf(strstr(line, ":"), ": %d", &maj);
                if (strstr(line, "\"MINOR\"")) sscanf(strstr(line, ":"), ": %d", &min);
                if (strstr(line, "\"PATCH\"")) { sscanf(strstr(line, ":"), ": %d", &pat); found = TRUE; }
            }
            if (found) sprintf(outVersion, "%d.%d.%d", maj, min, pat);
            fclose(f);
        }
        FindClose(hFind);
    }
}

void GetKSPPathFromRegistry(char* outPath) {
    HKEY hKey;
    const char* subkey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 220200";
    outPath[0] = '\0';

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
        DWORD len = MAX_PATH;
        RegQueryValueExA(hKey, "InstallLocation", NULL, NULL, (LPBYTE)outPath, &len);
        RegCloseKey(hKey);
    }
}

void WriteManifestRecursive(const char* path, FILE* f) {
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (strcmp(fd.cFileName, "kspman_manifest.txt") == 0) continue;

        char fullPath[MAX_PATH];
        sprintf(fullPath, "%s\\%s", path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            WriteManifestRecursive(fullPath, f);
        } else {
            fprintf(f, "%s\n", fullPath);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

long GetLocalFileSize(const char* filename) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(filename, GetFileExInfoStandard, &fad)) return 0;
    return (long)fad.nFileSizeLow;
}

LRESULT CALLBACK StatsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hLabel;
    if (msg == WM_CREATE) {
        hLabel = CreateWindowExA(0, "STATIC", "calculating", WS_CHILD|WS_VISIBLE, 10, 10, 300, 220, hwnd, NULL, NULL, NULL);
        if (hGlobalFont) SendMessage(hLabel, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
        SetTimer(hwnd, 1, 1000, NULL);
    } else if (msg == WM_TIMER) {
        PROCESS_MEMORY_COUNTERS pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        
        int managed = 0, loose = 0;
        int total = ListView_GetItemCount(hListView);
        for(int i=0; i<total; i++) {
            char status[20]; 
            ListView_GetItemText(hListView, i, 1, status, 20);
            if(strcmp(status, "Managed") == 0) managed++; else loose++;
        }

        unsigned __int64 totalBytes = GetFolderSize(ksp_path);
        double sizeInGB = (double)totalBytes / (1024.0 * 1024.0 * 1024.0);
        
        long cfgSize = GetLocalFileSize("config.json");

        char stats[1024];
        sprintf(stats, 
                "Live RAM: %.2f MB\n\n"
                "Managed Mods: %d\n"
                "Loose Mods: %d\n"
                "Total in View: %d\n\n"
                "GameData Size: %.2f GB\n"
                "Config size: %ld bytes",
                (double)pmc.WorkingSetSize / 1024 / 1024, 
                managed, loose, total, 
                sizeInGB, cfgSize);

        SetWindowTextA(hLabel, stats);
    } else if (msg == WM_CLOSE) { 
        KillTimer(hwnd, 1); 
        DestroyWindow(hwnd); 
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LRESULT CALLBACK LogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit;
    if (msg == WM_CREATE) {
        CREATESTRUCT *cs = (CREATESTRUCT*)lp;
        hEdit = CreateWindowExA(0, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|WS_BORDER, 10, 10, 560, 300, hwnd, NULL, NULL, NULL);
        CreateWindowA("BUTTON", "copy to clipboard", WS_CHILD|WS_VISIBLE, 10, 320, 150, 30, hwnd, (HMENU)ID_BTN_COPY_LOG, NULL, NULL);
        if (cs->lpCreateParams) {
            FILE *f = fopen((char*)cs->lpCreateParams, "rb");
            if (f) {
                fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
                char *b = malloc(sz + 1); fread(b, 1, sz, f); b[sz] = 0; fclose(f);
                SetWindowTextA(hEdit, b); free(b);
            } else { SetWindowTextA(hEdit, "Error manifest file missing or unreadable."); }
        }
    } else if (msg == WM_COMMAND && LOWORD(wp) == ID_BTN_COPY_LOG) {
        int len = GetWindowTextLengthA(hEdit);
        char *b = malloc(len + 1); GetWindowTextA(hEdit, b, len + 1);
        OpenClipboard(hwnd); EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, len + 1);
        memcpy(GlobalLock(hg), b, len + 1); GlobalUnlock(hg);
        SetClipboardData(CF_TEXT, hg); CloseClipboard(); free(b);
    } else if (msg == WM_CLOSE) { DestroyWindow(hwnd); }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

void ShowModMenu(HWND hwnd, int x, int y) {
    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (i == -1) return;
    
    HMENU hPopup = CreatePopupMenu();
    char n[256];
    ListView_GetItemText(hListView, i, 0, n, 256);
    BOOL isDisabled = strstr(n, ".disabled") != NULL;

    AppendMenuA(hPopup, MF_STRING, ID_MENU_TOG_SPEC, isDisabled ? "enable Mod" : "disable Mod");
    AppendMenuA(hPopup, MF_STRING, ID_MENU_EXPLORE, "open in explorer");
    AppendMenuA(hPopup, MF_STRING, ID_MENU_LOG, "view manifest/log");
    
    AppendMenuA(hPopup, MF_SEPARATOR, 0, 0);
    
    AppendMenuA(hPopup, MF_STRING, ID_MENU_GEN_MAN, "force generate manifest");
    AppendMenuA(hPopup, MF_STRING, ID_MENU_VER_MAN, "verify files");
    AppendMenuA(hPopup, MF_STRING, ID_MENU_REPACK, "repack to ZIP");
    AppendMenuA(hPopup, MF_STRING, ID_MENU_UNINSTALL, "uninstall mod");
    
    AppendMenuA(hPopup, MF_SEPARATOR, 0, 0);

    HMENU hDevSub = CreatePopupMenu();
    AppendMenuA(hDevSub, MF_STRING, ID_MENU_DEV_DEPS, "create dependencies.txt");
    AppendMenuA(hPopup, MF_POPUP, (UINT_PTR)hDevSub, "Developer Tools");

    TrackPopupMenu(hPopup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL);
    DestroyMenu(hDevSub);
    DestroyMenu(hPopup);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit;
    if (msg == WM_CREATE) {
        hEdit = CreateWindowExA(0, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|WS_BORDER, 10, 10, 360, 200, hwnd, NULL, NULL, NULL);
        CreateWindowA("BUTTON", "Save & Apply", WS_CHILD|WS_VISIBLE, 10, 220, 120, 30, hwnd, (HMENU)ID_BTN_SAVE_CFG, NULL, NULL);
        FILE *f = fopen("config.json", "r");
        if (f) { char buf[4096] = {0}; fread(buf, 1, 4096, f); fclose(f); SetWindowTextA(hEdit, buf); }
    } else if (msg == WM_COMMAND && LOWORD(wp) == ID_BTN_SAVE_CFG) {
        char buf[4096] = {0}; GetWindowTextA(hEdit, buf, 4096);
        FILE *f = fopen("config.json", "w"); fputs(buf, f); fclose(f);
        ApplyTheme(buf); DestroyWindow(hwnd);
    } else if (msg == WM_CLOSE) { DestroyWindow(hwnd); }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LRESULT CALLBACK DropProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND hText = CreateWindowA("STATIC", "DRAG ZIP HERE", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 40, 300, 20, hwnd, NULL, NULL, NULL);
            SendMessage(hText, WM_SETFONT, (WPARAM)hFont, TRUE);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wp;
            WCHAR szZip[MAX_PATH]; 
            
            if (DragQueryFileW(hDrop, 0, szZip, MAX_PATH)) {
                WCHAR szTemp[MAX_PATH], szStage[MAX_PATH];
                GetTempPathW(MAX_PATH, szTemp);
                swprintf(szStage, MAX_PATH, L"%ls\\kman_drop_%u", szTemp, GetTickCount());

                CoInitialize(NULL);
                IShellDispatch *pISD;
                if (SUCCEEDED(CoCreateInstance(&CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, &IID_IShellDispatch, (void**)&pISD))) {
                    SHCreateDirectoryExW(NULL, szStage, NULL);

                    Folder *pTo = NULL, *pFrom = NULL;
                    VARIANT vZip, vStage, vOpt;
                    vZip.vt = VT_BSTR; vZip.bstrVal = SysAllocString(szZip);
                    vStage.vt = VT_BSTR; vStage.bstrVal = SysAllocString(szStage);

                    if (SUCCEEDED(pISD->lpVtbl->NameSpace(pISD, vStage, &pTo)) && 
                        SUCCEEDED(pISD->lpVtbl->NameSpace(pISD, vZip, &pFrom))) {
                        
                        FolderItems *pItems = NULL;
                        pFrom->lpVtbl->Items(pFrom, &pItems);
                        if (pItems) {
                            long expectedCount = 0;
                            pItems->lpVtbl->get_Count(pItems, &expectedCount);

                            vOpt.vt = VT_I4; vOpt.lVal = 4 | 16 | 1024;
                            VARIANT vIt; vIt.vt = VT_DISPATCH; vIt.pdispVal = (IDispatch*)pItems;
                            pTo->lpVtbl->CopyHere(pTo, vIt, vOpt);

                            long currentCount = 0;
                            int timeout = 0;
                            while (timeout < 100) {
                                FolderItems *pToItems = NULL;
                                if (SUCCEEDED(pTo->lpVtbl->Items(pTo, &pToItems)) && pToItems) {
                                    pToItems->lpVtbl->get_Count(pToItems, &currentCount);
                                    pToItems->lpVtbl->Release(pToItems);
                                }
                                if (currentCount >= expectedCount) break;
                                Sleep(100);
                                timeout++;
                            }
                            pItems->lpVtbl->Release(pItems);
                        }
                    }
                    if (pFrom) pFrom->lpVtbl->Release(pFrom);
                    if (pTo) pTo->lpVtbl->Release(pTo);
                    SysFreeString(vZip.bstrVal); SysFreeString(vStage.bstrVal);
                    pISD->lpVtbl->Release(pISD);
                }
                CoUninitialize();

                WCHAR szSrc[MAX_PATH];
                swprintf(szSrc, MAX_PATH, L"%ls\\GameData", szStage);
                if (GetFileAttributesW(szSrc) == INVALID_FILE_ATTRIBUTES) {
                    wcsncpy(szSrc, szStage, MAX_PATH);
                }

                WIN32_FIND_DATAW ffd;
                WCHAR szSearch[MAX_PATH];
                swprintf(szSearch, MAX_PATH, L"%ls\\*", szSrc);
                HANDLE hFind = FindFirstFileW(szSearch, &ffd);
                
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (ffd.cFileName[0] == L'.') continue;
                        
                        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                            char mS[MAX_PATH], mD[MAX_PATH], mModName[MAX_PATH];
                            
                            WCHAR wFullSrc[MAX_PATH];
                            swprintf(wFullSrc, MAX_PATH, L"%ls\\%ls", szSrc, ffd.cFileName);
                            WideCharToMultiByte(CP_UTF8, 0, wFullSrc, -1, mS, MAX_PATH, NULL, NULL);

                            sprintf(mD, "%s\\%ls", ksp_path, ffd.cFileName);

                            WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, mModName, MAX_PATH, NULL, NULL);

                            SHCreateDirectoryExA(NULL, mD, NULL);

                            CopyRecursiveAndLog(mS, mD, mModName);
                        }
                    } while (FindNextFileW(hFind, &ffd));
                    FindClose(hFind);
                }
                MessageBoxA(hwnd, "Mod Installed Successfully", "kspman", MB_OK);
            }
            DragFinish(hDrop);
            DestroyWindow(hwnd);
            ScanGameData();
            return 0;
        }
        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT res = CallWindowProc(OldEditProc, hwnd, msg, wp, lp);
    if(msg == WM_KEYUP) ScanGameData();
    return res;
}

LRESULT CALLBACK URLPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit;
    if (msg == WM_CREATE) {
        CreateWindowA("STATIC", "Enter Zip URL:", WS_CHILD|WS_VISIBLE, 10, 15, 260, 20, hwnd, NULL, NULL, NULL);
        hEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 10, 40, 260, 25, hwnd, (HMENU)ID_URL_EDIT, NULL, NULL);
        CreateWindowA("BUTTON", "Download & Install", WS_CHILD|WS_VISIBLE, 75, 75, 130, 30, hwnd, (HMENU)ID_URL_DOWNLOAD, NULL, NULL);
    } 
    else if (msg == WM_COMMAND && LOWORD(wp) == ID_URL_DOWNLOAD) {
        GetWindowTextA(hEdit, target_url, 2048);
        if (strlen(target_url) > 5) {
            DestroyWindow(hwnd);
            InstallFromURL(hMainWnd);
        } else {
            MessageBoxA(hwnd, "Please enter a valid URL...", "Error", MB_OK | MB_ICONERROR);
        }
    } 
    else if (msg == WM_CLOSE) { DestroyWindow(hwnd); }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LRESULT CALLBACK DepCheckProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static DEP_DATA* data;
    static HWND hList;
    if (msg == WM_CREATE) {
        data = (DEP_DATA*)((CREATESTRUCT*)lp)->lpCreateParams;
        hList = CreateWindowExA(0, "LISTBOX", "", WS_CHILD|WS_VISIBLE|LBS_NOTIFY|LBS_MULTIPLESEL|WS_VSCROLL|WS_BORDER, 10, 50, 360, 150, hwnd, NULL, NULL, NULL);
        for(int i=0; i < data->count; i++) SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)data->names[i]);
        CreateWindowA("BUTTON", "Open Selected & Continue", WS_CHILD|WS_VISIBLE, 100, 210, 180, 30, hwnd, (HMENU)IDOK, NULL, NULL);
    } else if (msg == WM_COMMAND && LOWORD(wp) == IDOK) {
        int selCount = SendMessage(hList, LB_GETSELCOUNT, 0, 0);
        if (selCount > 0) {
            int indices[20];
            SendMessage(hList, LB_GETSELITEMS, 20, (LPARAM)indices);
            for(int i=0; i<selCount; i++) {
                ShellExecuteA(NULL, "open", data->links[indices[i]], NULL, NULL, SW_SHOWNORMAL);
            }
        }
        DestroyWindow(hwnd);
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LRESULT CALLBACK UninstallProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UNINSTALL_DATA* ud = (UNINSTALL_DATA*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lp;
            ud = (UNINSTALL_DATA*)cs->lpCreateParams;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)ud);
            
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND hEdit = CreateWindowExA(0, "EDIT", ud->fileListBuffer, 
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_BORDER, 
                10, 10, 415, 300, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtn = CreateWindowExA(0, "BUTTON", "Confirm Uninstall", 
                WS_CHILD | WS_VISIBLE, 
                150, 320, 130, 30, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);
            SendMessage(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wp) == 1 && ud) {
                char bP[MAX_PATH];
                sprintf(bP, "%s\\uninstall_tmp.bat", ksp_path);
                FILE* f = fopen(bP, "w");
                if (f) {
                    fprintf(f, "@echo off\ntimeout /t 1 /nobreak > nul\n");
                    char *scan = ud->fileListBuffer;
                    char line[MAX_PATH];
                    while (scan && *scan) {
                        char *next = strpbrk(scan, "\r\n");
                        size_t len = next ? (size_t)(next - scan) : strlen(scan);
                        if (len > 0 && len < MAX_PATH) {
                            memcpy(line, scan, len);
                            line[len] = '\0';
                            fprintf(f, "del /q /f \"%s\" >nul 2>&1\n", line);
                        }
                        scan = next ? (next + strspn(next, "\r\n")) : NULL;
                    }
                    fprintf(f, "rd /s /q \"%s\" >nul 2>&1\n", ud->modPath);
                    fprintf(f, "del /q /f \"%s\"\n", bP);
                    fclose(f);

                    ShellExecuteA(NULL, "open", bP, NULL, NULL, SW_HIDE);
                    DestroyWindow(hwnd);
                }
            }
            return 0;
        }
        case WM_NCDESTROY: {
            if (ud) {
                if (ud->fileListBuffer) free(ud->fileListBuffer);
                free(ud);
            }
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

unsigned __int64 GetFolderSize(const char* path) {
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s\\*", path);
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(searchPath, &f);
    unsigned __int64 total = 0;

    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        if (strcmp(f.cFileName, ".") == 0 || strcmp(f.cFileName, "..") == 0) continue;

        char fullPath[MAX_PATH];
        sprintf(fullPath, "%s\\%s", path, f.cFileName);

        if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            total += GetFolderSize(fullPath);
        } else {
            total += ((unsigned __int64)f.nFileSizeHigh << 32) | f.nFileSizeLow;
        }
    } while (FindNextFileA(h, &f));

    FindClose(h);
    return total;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            hMainWnd = hwnd;
            
            hSearch = CreateWindowExA(0, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 10, 5, 565, 20, hwnd, (HMENU)ID_SEARCH, NULL, NULL);
            OldEditProc = (WNDPROC)SetWindowLongPtr(hSearch, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
            
            hListView = CreateWindowExA(0, WC_LISTVIEWA, "", WS_CHILD|WS_VISIBLE|LVS_REPORT|WS_BORDER, 10, 30, 565, 325, hwnd, (HMENU)ID_LISTVIEW, NULL, NULL);
            
            LVCOLUMNA lvc = {LVCF_TEXT|LVCF_WIDTH, 0, col_widths[0], "Mod Folder"}; 
            ListView_InsertColumn(hListView, 0, &lvc);
            
            lvc.pszText = "Status"; 
            lvc.cx = col_widths[1]; 
            ListView_InsertColumn(hListView, 1, &lvc);
            
            lvc.pszText = "Version"; 
            lvc.cx = col_widths[2]; 
            ListView_InsertColumn(hListView, 2, &lvc);

            DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
            if (g_ListViewGrid) exStyle |= LVS_EX_GRIDLINES;
            ListView_SetExtendedListViewStyle(hListView, exStyle);

            HMENU hMenu = CreateMenu();
            
            HMENU hFile = CreatePopupMenu();
            AppendMenuA(hFile, MF_STRING, ID_MENU_ZIP, "install ZIP");
            AppendMenuA(hFile, MF_STRING, ID_MENU_DROP, "drop in UI");
            AppendMenuA(hFile, MF_STRING, ID_MENU_URL, "install from URL");
            AppendMenuA(hFile, MF_SEPARATOR, 0, 0);
            AppendMenuA(hFile, MF_STRING, ID_MENU_SCAN, "refresh List");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "file");
            
            HMENU hTogGlobal = CreatePopupMenu();
            AppendMenuA(hTogGlobal, MF_STRING, ID_MENU_TOG_ON, "enable All");
            AppendMenuA(hTogGlobal, MF_STRING, ID_MENU_TOG_OFF, "disable All");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hTogGlobal, "Batch");
            
            HMENU hSet = CreatePopupMenu();
            AppendMenuA(hSet, MF_STRING, ID_MENU_CONFIG, "JSON config...");
            AppendMenuA(hSet, MF_STRING, ID_MENU_RESET_CONFIG, "Reset to defaults");
            AppendMenuA(hSet, MF_STRING, ID_MENU_PATH, "set GameData path...");
            AppendMenuA(hSet, MF_STRING, ID_MENU_STATS, "application stats...");
            
            HMENU hDev = CreatePopupMenu();
            AppendMenuA(hDev, MF_STRING, ID_MENU_DEV_DEPS, "generate dependencies.txt");
            AppendMenuA(hSet, MF_POPUP, (UINT_PTR)hDev, "Developer Tools");
            
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSet, "Options");
            SetMenu(hwnd, hMenu); 

            SendMessage(hSearch, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
            SendMessage(hListView, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);

            LoadConfig(); 
            ScanGameData(); 
            break;
        case WM_NOTIFY: {
            LPNMHDR nm = (LPNMHDR)lParam;
            if (nm->idFrom == ID_LISTVIEW && nm->code == NM_RCLICK) {
                POINT pt; GetCursorPos(&pt); ShowModMenu(hwnd, pt.x, pt.y);
            }
        } break;
        case WM_COMMAND:
            switch(LOWORD(wParam)) {
                case ID_MENU_STATS: {
                    static char className[] = "kspman_stats";
                    WNDCLASSA stc = {0};
                    stc.lpfnWndProc = StatsProc;
                    stc.hInstance = GetModuleHandle(NULL);
                    stc.hCursor = LoadCursor(NULL, IDC_ARROW);
                    stc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                    stc.lpszClassName = className;

                    if (!GetClassInfoA(stc.hInstance, className, &stc)) {
                        RegisterClassA(&stc);
                    }

                    int modCount = ListView_GetItemCount(hListView);
                    unsigned __int64 totalBytes = GetFolderSize(ksp_path);
                    double sizeInMB = (double)totalBytes / (1024 * 1024);
                    
                    char statsMsg[512];
                    sprintf(statsMsg, "Total Mods: %d\nGameData Size: %.2f MB\nPath: %s", modCount, sizeInMB, ksp_path);

                    HWND hStatsWnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, className, "Application Statistics", 
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
                                    300, 300, 350, 200, hwnd, NULL, stc.hInstance, (LPVOID)statsMsg);
                    
                    break;
                }
                case ID_MENU_EXPLORE: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) { char n[256], p[MAX_PATH]; ListView_GetItemText(hListView, i, 0, n, 256); sprintf(p, "%s\\%s", ksp_path, n); ShellExecuteA(NULL, "explore", p, NULL, NULL, SW_SHOWNORMAL); }
                    break;
                }
                case ID_MENU_LOG: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) { 
                        static char mP[MAX_PATH]; char n[256]; 
                        ListView_GetItemText(hListView, i, 0, n, 256); 
                        sprintf(mP, "%s\\%s\\kspman_manifest.txt", ksp_path, n);
                        static char className[] = "kspman_log";
                        WNDCLASSA lwc = {0, LogProc, 0, 0, GetModuleHandle(NULL), 0, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW+1), 0, className};
                        RegisterClassA(&lwc); CreateWindowExA(WS_EX_TOOLWINDOW, className, "Mod Manifest", WS_OVERLAPPEDWINDOW|WS_VISIBLE, 200, 200, 600, 400, hwnd, NULL, NULL, mP);
                    } break;
                }
                case ID_MENU_TOG_ON: 
                case ID_MENU_TOG_OFF: {
                    BOOL en = (LOWORD(wParam) == ID_MENU_TOG_ON);
                    char s[MAX_PATH]; 
                    sprintf(s, "%s\\*", ksp_path);
                    WIN32_FIND_DATAA f; 
                    HANDLE h = FindFirstFileA(s, &f);
                    
                    int successCount = 0;
                    int failCount = 0;

                    if (h != INVALID_HANDLE_VALUE) {
                        do {
                            if (strcmp(f.cFileName, ".") == 0 || strcmp(f.cFileName, "..") == 0) continue;
                            
                            if (_stricmp(f.cFileName, "Squad") == 0 || _stricmp(f.cFileName, "SquadExpansion") == 0) continue;

                            char oldFullPath[MAX_PATH];
                            char newFullPath[MAX_PATH];
                            sprintf(oldFullPath, "%s\\%s", ksp_path, f.cFileName);

                            if (en) {
                                if (EndsWith(f.cFileName, ".disabled")) {
                                    char cleanName[MAX_PATH];
                                    size_t len = strlen(f.cFileName) - 9;
                                    strncpy(cleanName, f.cFileName, len);
                                    cleanName[len] = '\0';
                                    
                                    sprintf(newFullPath, "%s\\%s", ksp_path, cleanName);
                                    if (MoveFileA(oldFullPath, newFullPath)) successCount++; else failCount++;
                                }
                            } else {
                                if (!EndsWith(f.cFileName, ".disabled")) {
                                    sprintf(newFullPath, "%s\\%s.disabled", ksp_path, f.cFileName);
                                    if (MoveFileA(oldFullPath, newFullPath)) successCount++; else failCount++;
                                }
                            }
                        } while (FindNextFileA(h, &f));
                        FindClose(h);
                        
                        ScanGameData();

                        char report[256];
                        sprintf(report, "Done.\nModified: %d\nErrors: %d", successCount, failCount);
                        MessageBoxA(hwnd, report, "Bulk Toggle", MB_OK);
                    }
                    break;
                }
                case ID_MENU_TOG_SPEC: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if(i != -1) { char n[256], name[256]; ListView_GetItemText(hListView, i, 0, n, 256); strcpy(name, n);
                        if(strstr(name, ".disabled")) { name[strlen(name)-9]='\0'; ToggleMod(name, TRUE); } else ToggleMod(name, FALSE);
                        ScanGameData(); }
                    break;
                }
                case ID_MENU_GEN_MAN: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) {
                        char n[256];
                        ListView_GetItemText(hListView, i, 0, n, 256);

                        WCHAR wKSP[MAX_PATH], wP[MAX_PATH], wMP[MAX_PATH];
                        MultiByteToWideChar(CP_UTF8, 0, ksp_path, -1, wKSP, MAX_PATH);

                        swprintf(wP, MAX_PATH, L"%ls\\%hs", wKSP, n);
                        swprintf(wMP, MAX_PATH, L"%ls\\kspman_manifest.txt", wP);

                        FILE* f = _wfopen(wMP, L"w");
                        
                        if (f) {
                            char p_utf8[MAX_PATH];
                            WideCharToMultiByte(CP_UTF8, 0, wP, -1, p_utf8, MAX_PATH, NULL, NULL);
                            
                            WriteManifestRecursive(p_utf8, f);
                            fclose(f);
                            
                            MessageBoxA(hwnd, "Manifest generated successfully.", "kspman", MB_OK | MB_ICONINFORMATION);
                        } else {
                            DWORD err = GetLastError();
                            char errMsg[512];
                            if (err == ERROR_SHARING_VIOLATION) {
                                _snprintf(errMsg, 512, "Could not generate manifest.\n\nThe file is currently in use. Please close the Manifest Viewer and try again.");
                            } else {
                                _snprintf(errMsg, 512, "Failed to open manifest for writing.\nError Code: %lu", err);
                            }
                            MessageBoxA(hwnd, errMsg, "File Error", MB_OK | MB_ICONERROR);
                        }
                        
                        ScanGameData();
                    }
                    break;
                }
                case ID_MENU_VER_MAN: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) {
                        char n[256], mP[MAX_PATH];
                        ListView_GetItemText(hListView, i, 0, n, 256);
                        sprintf(mP, "%s\\%s\\kspman_manifest.txt", ksp_path, n);

                        FILE* f = fopen(mP, "r");
                        if (!f) {
                            MessageBoxA(hwnd, "Error: Manifest does not exist.", "Verify", MB_ICONERROR);
                            break;
                        }

                        char line[MAX_PATH];
                        int missingCount = 0;
                        while (fgets(line, sizeof(line), f)) {
                            line[strcspn(line, "\r\n")] = 0;
                            if (strlen(line) > 0 && !PathFileExistsA(line)) {
                                missingCount++;
                            }
                        }
                        fclose(f);

                        char msg[256];
                        if (missingCount == 0) {
                            sprintf(msg, "Manifest is valid: All files are present.");
                        } else {
                            sprintf(msg, "Manifest invalid: %d files are missing.", missingCount);
                        }
                        MessageBoxA(hwnd, msg, "Verify", missingCount == 0 ? MB_OK : MB_ICONWARNING);
                    }
                    break;
                }
                case ID_MENU_REPACK: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) {
                        char n[256], mP[MAX_PATH];
                        ListView_GetItemText(hListView, i, 0, n, 256);
                        _snprintf(mP, MAX_PATH, "%s\\%s\\kspman_manifest.txt", ksp_path, n);

                        if (GetFileAttributesA(mP) == INVALID_FILE_ATTRIBUTES) {
                            MessageBoxA(hwnd, "Error: No manifest found.", "Repack", MB_ICONERROR);
                            break;
                        }

                        OPENFILENAMEW sfn = {0};
                        WCHAR f[MAX_PATH] = {0};
                        sfn.lStructSize = sizeof(sfn);
                        sfn.hwndOwner = hwnd;
                        sfn.lpstrFilter = L"ZIP Files (*.zip)\0*.zip\0";
                        sfn.lpstrFile = f;
                        sfn.nMaxFile = MAX_PATH;
                        sfn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

                        if (GetSaveFileNameW(&sfn)) {
                            g_IsBusy = TRUE;
                            unsigned char emptyZip[] = { 80,75,5,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
                            HANDLE hFile = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                            if (hFile != INVALID_HANDLE_VALUE) {
                                DWORD written;
                                WriteFile(hFile, emptyZip, sizeof(emptyZip), &written, NULL);
                                CloseHandle(hFile);

                                CoInitialize(NULL);
                                IShellDispatch *pISD;
                                if (SUCCEEDED(CoCreateInstance(&CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, &IID_IShellDispatch, (void**)&pISD))) {
                                    Folder *pZipFolder = NULL;
                                    VARIANT vZip;
                                    vZip.vt = VT_BSTR; vZip.bstrVal = SysAllocString(f);
                                    
                                    if (SUCCEEDED(pISD->lpVtbl->NameSpace(pISD, vZip, &pZipFolder))) {
                                        FILE* mf = fopen(mP, "r");
                                        if (mf) {
                                            char line[MAX_PATH];
                                            long expectedCount = 0;
                                            while (fgets(line, MAX_PATH, mf)) {
                                                line[strcspn(line, "\r\n")] = 0;
                                                if (GetFileAttributesA(line) != INVALID_FILE_ATTRIBUTES) {
                                                    WCHAR wFile[MAX_PATH];
                                                    MultiByteToWideChar(CP_UTF8, 0, line, -1, wFile, MAX_PATH);
                                                    
                                                    VARIANT vItem, vOpt;
                                                    vItem.vt = VT_BSTR; vItem.bstrVal = SysAllocString(wFile);
                                                    vOpt.vt = VT_I4; vOpt.lVal = 4 | 16 | 1024;

                                                    if (SUCCEEDED(pZipFolder->lpVtbl->CopyHere(pZipFolder, vItem, vOpt))) {
                                                        expectedCount++;
                                                    }
                                                    SysFreeString(vItem.bstrVal);
                                                }
                                            }
                                            fclose(mf);

                                            int timeout = 0;
                                            FolderItems *pItems = NULL;
                                            long currentCount = 0;
                                            while (timeout < 100) {
                                                if (SUCCEEDED(pZipFolder->lpVtbl->Items(pZipFolder, &pItems)) && pItems) {
                                                    pItems->lpVtbl->get_Count(pItems, &currentCount);
                                                    pItems->lpVtbl->Release(pItems);
                                                }
                                                if (currentCount >= expectedCount) break;
                                                Sleep(100);
                                                timeout++;
                                            }
                                        }
                                        pZipFolder->lpVtbl->Release(pZipFolder);
                                    }
                                    SysFreeString(vZip.bstrVal);
                                    pISD->lpVtbl->Release(pISD);
                                }
                                CoUninitialize();
                                g_IsBusy = FALSE;
                                MessageBoxA(hwnd, "Repack Complete", "Status", MB_OK);
                            } else {
                                g_IsBusy = FALSE;
                            }
                        }
                    }
                    break;
                }
                case ID_MENU_PATH: {
                    BROWSEINFOA bi = {hwnd, 0, 0, "Select GameData", BIF_RETURNONLYFSDIRS|BIF_USENEWUI};
                    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi); if(pidl){ SHGetPathFromIDListA(pidl, ksp_path); SaveConfig(); ScanGameData(); }
                    break;
                }
                case ID_MENU_ZIP: {
                    OPENFILENAMEW ofn = {0};
                    WCHAR f[MAX_PATH] = {0};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"ZIP Files (*.zip)\0*.zip\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = f;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

                    if (GetOpenFileNameW(&ofn)) {
                        WCHAR szTemp[MAX_PATH], szStage[MAX_PATH];
                        GetTempPathW(MAX_PATH, szTemp);
                        swprintf(szStage, MAX_PATH, L"%ls\\kman_zip_%u", szTemp, GetTickCount());

                        CoInitialize(NULL);
                        IShellDispatch *pISD;
                        if (SUCCEEDED(CoCreateInstance(&CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, &IID_IShellDispatch, (void**)&pISD))) {
                            SHCreateDirectoryExW(NULL, szStage, NULL);

                            Folder *pTo = NULL, *pFrom = NULL;
                            VARIANT vZip, vStage, vOpt;
                            vZip.vt = VT_BSTR; vZip.bstrVal = SysAllocString(f);
                            vStage.vt = VT_BSTR; vStage.bstrVal = SysAllocString(szStage);

                            if (SUCCEEDED(pISD->lpVtbl->NameSpace(pISD, vStage, &pTo)) && 
                                SUCCEEDED(pISD->lpVtbl->NameSpace(pISD, vZip, &pFrom))) {
                                
                                FolderItems *pItems = NULL;
                                pFrom->lpVtbl->Items(pFrom, &pItems);
                                if (pItems) {
                                    vOpt.vt = VT_I4; vOpt.lVal = 4 | 16 | 1024; 
                                    VARIANT vIt; vIt.vt = VT_DISPATCH; vIt.pdispVal = (IDispatch*)pItems;
                                    pTo->lpVtbl->CopyHere(pTo, vIt, vOpt);
                                    pItems->lpVtbl->Release(pItems);
                                }
                            }
                            if (pFrom) pFrom->lpVtbl->Release(pFrom);
                            if (pTo) pTo->lpVtbl->Release(pTo);
                            SysFreeString(vZip.bstrVal); SysFreeString(vStage.bstrVal);
                            pISD->lpVtbl->Release(pISD);
                        }
                        CoUninitialize();

                        WCHAR szSrc[MAX_PATH], szSearch[MAX_PATH];
                        swprintf(szSrc, MAX_PATH, L"%ls\\GameData", szStage);
                        
                        if (GetFileAttributesW(szSrc) == INVALID_FILE_ATTRIBUTES) {
                            wcsncpy(szSrc, szStage, MAX_PATH);
                        }

                        WIN32_FIND_DATAW ffd;
                        swprintf(szSearch, MAX_PATH, L"%ls\\*", szSrc);
                        HANDLE hFind = FindFirstFileW(szSearch, &ffd);
                        
                        if (hFind != INVALID_HANDLE_VALUE) {
                            do {
                                if (wcsstr(ffd.cFileName, L"..")) continue;

                                if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && 
                                    wcscmp(ffd.cFileName, L".") != 0 && wcscmp(ffd.cFileName, L"..") != 0) {
                                    
                                    char mS[MAX_PATH], mD[MAX_PATH], mN[MAX_PATH];
                                    WCHAR wMS[MAX_PATH], wMD[MAX_PATH], wKsp[MAX_PATH];

                                    swprintf(wMS, MAX_PATH, L"%ls\\%ls", szSrc, ffd.cFileName);
                                    
                                    MultiByteToWideChar(CP_UTF8, 0, ksp_path, -1, wKsp, MAX_PATH);
                                    swprintf(wMD, MAX_PATH, L"%ls\\%ls", wKsp, ffd.cFileName);

                                    WideCharToMultiByte(CP_UTF8, 0, wMS, -1, mS, MAX_PATH, NULL, NULL);
                                    WideCharToMultiByte(CP_UTF8, 0, wMD, -1, mD, MAX_PATH, NULL, NULL);
                                    _snprintf(mN, MAX_PATH, "%s\\kspman_manifest.txt", mD);

                                    SHCreateDirectoryExA(NULL, mD, NULL);
                                    CopyRecursiveAndLog(mS, mD, mN);
                                }
                            } while (FindNextFileW(hFind, &ffd));
                            FindClose(hFind);
                        }
                        MessageBoxA(hwnd, "Mod Installed Successfully", "kspman", MB_OK);
                        ScanGameData();
                    }
                    break;
                }
                case ID_MENU_SCAN: ScanGameData(); break;
                case ID_MENU_CONFIG: {
                    static char className[] = "kspman_cfg";
                    WNDCLASSA swc = {0, SettingsProc, 0, 0, GetModuleHandle(NULL), 0, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW+1), 0, className};
                    RegisterClassA(&swc); CreateWindowExA(WS_EX_TOOLWINDOW, className, "Settings", WS_OVERLAPPEDWINDOW|WS_VISIBLE, 200, 200, 400, 300, hwnd, NULL, NULL, NULL);
                    break;
                }
                case ID_MENU_DROP: {
                    static char className[] = "kspman_drop";
                    WNDCLASSA dwc = {0, DropProc, 0, 0, GetModuleHandle(NULL), 0, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW+1), 0, className};
                    RegisterClassA(&dwc); CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_TOPMOST, className, "Drop ZIP", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE, 300, 300, 300, 150, hwnd, NULL, NULL, NULL);
                    break;
                }
                case ID_MENU_URL: {
                    static char className[] = "kspman_url_ui";
                    static BOOL registered = FALSE;
                    if (!registered) {
                        WNDCLASSA uwc = {0, URLPromptProc, 0, 0, GetModuleHandle(NULL), 0, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW+1), 0, className};
                        RegisterClassA(&uwc);
                        registered = TRUE;
                    }
                    CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_TOPMOST, className, "Install from URL", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE, 400, 400, 300, 160, hwnd, NULL, NULL, NULL);
                    break;
                }
                case ID_MENU_DEV_DEPS: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i == -1) {
                        MessageBoxA(hwnd, "please select a mod folder first", "Error", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    char modFolderName[256];
                    ListView_GetItemText(hListView, i, 0, modFolderName, 256);

                    char depPath[MAX_PATH];
                    sprintf(depPath, "%s\\%s\\dependencies.txt", ksp_path, modFolderName);

                    FILE *f = fopen(depPath, "w");
                    if (f) {
                        fprintf(f, "// Dependencies for %s\n", modFolderName);
                        fprintf(f, "// Format: \"Link to dependency\" \"Version\"\n\n");
                        
                        fprintf(f, "\"https://example.com/mod\" \"1.0.0\"\n");
                        
                        fclose(f);

                        char msg[512];
                        sprintf(msg, "dependencies.txt created in %s!", modFolderName);
                        MessageBoxA(hwnd, msg, "Success", MB_OK);
                        
                        ShellExecuteA(NULL, "open", depPath, NULL, NULL, SW_SHOWNORMAL);
                    } else {
                        MessageBoxA(hwnd, "could not create file. check permissions.", "Error", MB_OK | MB_ICONERROR);
                    }
                    break;
                }
                case ID_MENU_RESET_CONFIG: {
                    if (MessageBoxA(hwnd, "Reset all settings to default?\n\nThis will clear your paths and reset the UI.", "kspman", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        strcpy(ksp_path, "");
                        strcpy(g_DefaultRepackPath, "");
                        g_FontSize = 18;
                        g_Opacity = 230;
                        g_ListViewGrid = TRUE;
                        col_widths[0] = 220; col_widths[1] = 100; col_widths[2] = 150;

                        SaveConfig();
                        LoadConfig();
                        
                        SetLayeredWindowAttributes(hwnd, 0, (BYTE)g_Opacity, LWA_ALPHA);
                        
                        if (hListView) {
                            DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
                            if (g_ListViewGrid) exStyle |= LVS_EX_GRIDLINES;
                            ListView_SetExtendedListViewStyle(hListView, exStyle);
                            SendMessage(hListView, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
                        }

                        InvalidateRect(hwnd, NULL, TRUE); 
                        MessageBoxA(hwnd, "Settings have been reset to factory defaults :)", "Success", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }
                case ID_MENU_UNINSTALL: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) {
                        static BOOL uninstRegistered = FALSE;
                        if (!uninstRegistered) {
                            WNDCLASSA uwc = {0};
                            uwc.lpfnWndProc = UninstallProc;
                            uwc.hInstance = GetModuleHandle(NULL);
                            uwc.hCursor = LoadCursor(NULL, IDC_ARROW);
                            uwc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                            uwc.lpszClassName = "kspman_uninstall";
                            RegisterClassA(&uwc);
                            uninstRegistered = TRUE;
                        }

                        char n[256];
                        ListView_GetItemText(hListView, i, 0, n, 256);
                        UNINSTALL_DATA* ud = calloc(1, sizeof(UNINSTALL_DATA));
                        
                        sprintf(ud->modPath, "%s\\%s", ksp_path, n);
                        sprintf(ud->manifestPath, "%s\\kspman_manifest.txt", ud->modPath);

                        FILE* f = fopen(ud->manifestPath, "rb");
                        if (f) {
                            fseek(f, 0, SEEK_END);
                            long sz = ftell(f);
                            rewind(f);
                            ud->fileListBuffer = malloc(sz + 1);
                            if(ud->fileListBuffer) {
                                fread(ud->fileListBuffer, 1, sz, f);
                                ud->fileListBuffer[sz] = '\0';
                            }
                            fclose(f);
                        } else {
                            ud->fileListBuffer = _strdup("No manifest found. Only the folder will be removed.");
                        }

                        CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "kspman_uninstall", 
                                        "Confirm Uninstall", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
                                        400, 300, 450, 400, hwnd, NULL, GetModuleHandle(NULL), ud);
                    }
                    break;
                }
            } break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR l, int n) {
    InitCommonControls();
    
    g_Opacity = 255; 
    g_FontSize = 18;
    ksp_path[0] = '\0';

    LoadConfig(); 

    if (g_Opacity <= 0) g_Opacity = 255;

    WNDCLASSA wc = {0, WindowProc, 0, 0, h, 0, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), 0, "kspman"};
    RegisterClassA(&wc);
    
    DWORD dwExStyle = (g_Opacity < 255) ? WS_EX_LAYERED : 0;

    HWND hwnd = CreateWindowExA(dwExStyle, "kspman", "kspman", 
                                WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, 
                                CW_USEDEFAULT, CW_USEDEFAULT, 600, 420, 
                                NULL, NULL, h, NULL);
    
    if (!hwnd) return 0;

    if (g_Opacity < 255) {
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)g_Opacity, LWA_ALPHA);
    }

    ShowWindow(hwnd, n);
    UpdateWindow(hwnd);

    MSG m; 
    while(GetMessage(&m, 0, 0, 0)) { 
        TranslateMessage(&m); 
        DispatchMessage(&m); 
    }
    return (int)m.wParam;
}