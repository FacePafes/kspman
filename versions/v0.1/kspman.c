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

HWND hListView, hMainWnd, hSearch;
char ksp_path[MAX_PATH] = {0};
char target_url[2048] = {0};
char ignore_list[1024] = "|Squad|SquadExpansion|"; 
int col_widths[3] = {300, 150, 100};               
HFONT hGlobalFont = NULL;
COLORREF bg_color = 0xFFFFFF;
COLORREF text_color = 0x000000;
BOOL show_grid = TRUE;
WNDPROC OldEditProc;

void GetModVersion(const char* folderPath, char* outVersion);
unsigned __int64 GetFolderSize(const char* path);

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

void EscapeForPS(const char* s, char* d) {
    while(*s) {
        if(*s=='\'') { *d++='\''; *d++='\''; }
        else if(*s=='\"') { *d++='\\'; *d++='\"'; }
        else { *d++=*s; }
        s++;
    }
    *d='\0';
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
    sprintf(buf, "{\r\n  \"Background\": \"#%02x%02x%02x\",\r\n  \"Text\": \"#%02x%02x%02x\",\r\n  \"Gridlines\": \"%s\",\r\n  \"Path\": \"%s\"\r\n}", 
        GetRValue(bg_color), GetGValue(bg_color), GetBValue(bg_color),
        GetRValue(text_color), GetGValue(text_color), GetBValue(text_color),
        show_grid ? "True" : "False", ksp_path);
    FILE *f = fopen("config.json", "w");
    if (f) { fputs(buf, f); fclose(f); }
}

void LoadConfig() {
    FILE *f = fopen("config.json", "r");
    if (!f) return;

    char buf[4096] = {0};
    fread(buf, 1, 4096, f);
    fclose(f);

    char *p;

    if ((p = strstr(buf, "\"Path\": \""))) {
        char *start = p + 9;
        char *end = strstr(start, "\"");
        if (end) {
            int len = (int)(end - start);
            strncpy(ksp_path, start, len);
            ksp_path[len] = '\0';
        }
    }

    if ((p = strstr(buf, "\"Ignore\": \""))) {
        char *start = p + 11;
        char *end = strstr(start, "\"");
        if (end) {
            int len = (int)(end - start);
            sprintf(ignore_list, "|%.*s|", len, start);
        }
    }

    if ((p = strstr(buf, "\"Width1\": "))) sscanf(p + 10, "%d", &col_widths[0]);
    if ((p = strstr(buf, "\"Width2\": "))) sscanf(p + 10, "%d", &col_widths[1]);
    if ((p = strstr(buf, "\"Width3\": "))) sscanf(p + 10, "%d", &col_widths[2]);

    char font_name[64] = "Segoe UI";
    if ((p = strstr(buf, "\"Font\": \""))) {
        sscanf(p + 9, "%[^\"]", font_name);
    }
    
    if (hGlobalFont) DeleteObject(hGlobalFont);
    hGlobalFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, font_name);

    ApplyTheme(buf);
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

void RunPS(const char* cmd) {
    char fullCmd[32768];
    sprintf(fullCmd, "-NoProfile -ExecutionPolicy Bypass -Command \"%s\"", cmd);
    ShellExecuteA(NULL, "open", "powershell.exe", fullCmd, NULL, SW_HIDE);
}

void ToggleMod(const char* name, BOOL enable) {
    char oldP[MAX_PATH], newP[MAX_PATH];
    if(enable) { 
        sprintf(oldP, "%s\\%s.disabled", ksp_path, name); 
        sprintf(newP, "%s\\%s", ksp_path, name); 
    } else { 
        sprintf(oldP, "%s\\%s", ksp_path, name); 
        sprintf(newP, "%s\\%s.disabled", ksp_path, name); 
    }
    MoveFileA(oldP, newP);
}

void CheckAndPromptDeps(const char* stagePath, HWND parent) {
    char depFile[MAX_PATH];
    sprintf(depFile, "%s\\dependencies.txt", stagePath);
    if (!PathFileExistsA(depFile)) {
        char search[MAX_PATH]; sprintf(search, "%s\\*\\dependencies.txt", stagePath);
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(search, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            sprintf(depFile, "%s\\%s", stagePath, fd.cFileName);
            FindClose(h);
        }
    }

    if (!PathFileExistsA(depFile)) return;

    DEP_DATA* data = malloc(sizeof(DEP_DATA));
    memset(data, 0, sizeof(DEP_DATA));
    FILE* f = fopen(depFile, "r");
    if (f) {
        char line[2048];
        while (fgets(line, sizeof(line), f) && data->count < 20) {
            if (line[0] == '/' || strlen(line) < 5) continue;
            char link[2048], ver[64];
            if (sscanf(line, "\"%[^\"]\" \"%[^\"]\"", link, ver) >= 1) {
                strcpy(data->links[data->count], link);
                sprintf(data->names[data->count], "%s (v%s)", link, ver);
                data->count++;
            }
        }
        fclose(f);
    }

    if (data->count > 0) {
        WNDCLASSA dwc = {0, DepCheckProc, 0, 0, GetModuleHandle(NULL), 0, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW+1), 0, "kspman_dep_ui"};
        RegisterClassA(&dwc);
        HWND hDlg = CreateWindowExA(WS_EX_TOPMOST, "kspman_dep_ui", "Dependency Check", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, 400, 400, 400, 300, parent, NULL, GetModuleHandle(NULL), data);
        
        MSG msg;
        while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    free(data);
}

void InstallFromURL(HWND hwnd) {
    if (strlen(target_url) < 1) return;
    HINTERNET hSession = WinHttpOpen(L"kspman/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        WCHAR wUrl[2048];
        MultiByteToWideChar(CP_ACP, 0, target_url, -1, wUrl, 2048);
        HINTERNET hConnect = WinHttpOpenRequest(hSession, L"GET", wUrl, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_REFRESH);
        if (hConnect && WinHttpSendRequest(hConnect, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hConnect, NULL)) {
            char tempPath[MAX_PATH], zipFile[MAX_PATH], stageDir[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            sprintf(zipFile, "%s\\kspman_dl.zip", tempPath);
            sprintf(stageDir, "%s\\kspman_stage", tempPath);

            FILE* f = fopen(zipFile, "wb");
            if (f) {
                DWORD dwSize = 0, dwDownloaded = 0;
                while (WinHttpQueryDataAvailable(hConnect, &dwSize) && dwSize > 0) {
                    char* buf = malloc(dwSize);
                    if (WinHttpReadData(hConnect, buf, dwSize, &dwDownloaded)) fwrite(buf, 1, dwDownloaded, f);
                    free(buf);
                }
                fclose(f);

                char szZip[MAX_PATH*2], szStage[MAX_PATH*2], cmd[16384];
                EscapeForPS(zipFile, szZip); EscapeForPS(stageDir, szStage);
                sprintf(cmd, "Remove-Item '%s' -Recurse -Force -ErrorAction SilentlyContinue; Expand-Archive '%s' '%s' -Force", szStage, szZip, szStage);
                RunPS(cmd);

                Sleep(500);

                CheckAndPromptDeps(stageDir, hwnd);

                char ck[MAX_PATH*2]; EscapeForPS(ksp_path, ck);
                sprintf(cmd, "Add-Type -AssemblyName PresentationFramework; $t='%s'; $k='%s'; $g=if(Test-Path \\\"$t\\GameData\\\"){\\\"$t\\GameData\\*\\\"}else{\\\"$t\\*\\\"}; Get-ChildItem $g -Recurse|ForEach-Object{$r=$_.FullName.Substring($t.Length).TrimStart('\\\\'); if($r.StartsWith('GameData')){$r=$r.Substring(9)} $d=Join-Path $k $r; if($_.PSIsContainer){New-Item $d -ItemType Directory -Force|Out-Null}else{Copy-Item $_.FullName $d -Force} $mD=$r.Split('\\\\')[0]; if($mD){$mp=Join-Path $k (Join-Path $mD 'kspman_manifest.txt'); Add-Content $mp $d}}; [System.Windows.MessageBox]::Show('Installation Complete','Status')", szStage, ck);
                RunPS(cmd);
            }
        }
        if (hConnect) WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
    }
    memset(target_url, 0, 2048);
    ScanGameData();
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
    if(msg == WM_CREATE) { DragAcceptFiles(hwnd, TRUE); CreateWindowA("STATIC", "DRAG ZIP HERE", WS_CHILD|WS_VISIBLE|SS_CENTER, 0, 40, 300, 20, hwnd, NULL, NULL, NULL); }
    else if(msg == WM_DROPFILES) { HDROP h = (HDROP)wp; char f[MAX_PATH]; if(DragQueryFileA(h,0,f,MAX_PATH)) { char ck[MAX_PATH*2], cz[MAX_PATH*2], cmd[16384]; EscapeForPS(ksp_path, ck); EscapeForPS(f, cz); sprintf(cmd, "Add-Type -AssemblyName PresentationFramework; $z='%s';$k='%s';$t=Join-Path $env:TEMP ('kspt_'+(Get-Random)); Expand-Archive $z $t -Force; $g=if(Test-Path \\\"$t\\GameData\\\"){\\\"$t\\GameData\\*\\\"}else{\\\"$t\\*\\\"}; Get-ChildItem $g -Recurse|ForEach-Object{$r=$_.FullName.Substring($t.Length).TrimStart('\\\\'); if($r.StartsWith('GameData')){$r=$r.Substring(9)} $d=Join-Path $k $r; if($_.PSIsContainer){New-Item $d -ItemType Directory -Force|Out-Null}else{Copy-Item $_.FullName $d -Force} $mD=$r.Split('\\\\')[0]; if($mD){$mp=Join-Path $k (Join-Path $mD 'kspman_manifest.txt'); Add-Content $mp $d}}; [System.Windows.MessageBox]::Show('Installation Complete','Status')", cz, ck); RunPS(cmd); } DragFinish(h); DestroyWindow(hwnd); ScanGameData(); }
    else if(msg == WM_CLOSE) DestroyWindow(hwnd);
    return DefWindowProcA(hwnd, msg, wp, lp);
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
    static UNINSTALL_DATA* ud;
    if (msg == WM_CREATE) {
        ud = (UNINSTALL_DATA*)((CREATESTRUCT*)lp)->lpCreateParams;
        
        CreateWindowA("STATIC", "The following files/folders will be permanently deleted:", WS_CHILD|WS_VISIBLE, 10, 10, 400, 20, hwnd, NULL, NULL, NULL);
        
        HWND hEdit = CreateWindowExA(0, "EDIT", ud->fileListBuffer, WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|WS_BORDER, 10, 40, 415, 250, hwnd, NULL, NULL, NULL);
        
        CreateWindowA("BUTTON", "confirm delete", WS_CHILD|WS_VISIBLE, 10, 310, 150, 40, hwnd, (HMENU)IDOK, NULL, NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 275, 310, 150, 40, hwnd, (HMENU)IDCANCEL, NULL, NULL);
        
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDOK) {
            char cmd[16384], eM[MAX_PATH*2], eP[MAX_PATH*2];
            EscapeForPS(ud->manifestPath, eM);
            EscapeForPS(ud->modPath, eP);

            sprintf(cmd, "Add-Type -AssemblyName PresentationFramework; "
                         "if(Test-Path '%s'){$files=Get-Content '%s'; foreach($f in $files){if(Test-Path $f.Trim()){Remove-Item $f.Trim() -Force}}}; "
                         "Remove-Item '%s' -Recurse -Force; "
                         "[System.Windows.MessageBox]::Show('Mod Uninstalled','Status')", eM, eM, eP);
            
            RunPS(cmd);
            DestroyWindow(hwnd);
            ScanGameData();
        } 
        else if (LOWORD(wp) == IDCANCEL) DestroyWindow(hwnd);
    } else if (msg == WM_CLOSE) {
        free(ud->fileListBuffer);
        free(ud);
        DestroyWindow(hwnd);
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

            ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);

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
            AppendMenuA(hSet, MF_STRING, ID_MENU_PATH, "set GameData path...");
            AppendMenuA(hSet, MF_STRING, ID_MENU_STATS, "application stats...");
            
            HMENU hDev = CreatePopupMenu();
            AppendMenuA(hDev, MF_STRING, ID_MENU_DEV_DEPS, "generate dependencies.txt");
            AppendMenuA(hSet, MF_POPUP, (UINT_PTR)hDev, "Developer Tools");
            
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSet, "Options");
            SetMenu(hwnd, hMenu); 

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
                case ID_MENU_TOG_ON: case ID_MENU_TOG_OFF: {
                    BOOL en = (LOWORD(wParam)==ID_MENU_TOG_ON); char s[MAX_PATH]; sprintf(s, "%s\\*", ksp_path);
                    WIN32_FIND_DATAA f; HANDLE h = FindFirstFileA(s, &f);
                    if(h!=INVALID_HANDLE_VALUE){ do { if((f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && f.cFileName[0] != '.'){
                        if(en && strstr(f.cFileName, ".disabled")) { char n[MAX_PATH]; strcpy(n, f.cFileName); n[strlen(n)-9]='\0'; ToggleMod(n, TRUE); }
                        else if(!en && !strstr(f.cFileName, ".disabled") && strcmpi(f.cFileName, "Squad")!=0) ToggleMod(f.cFileName, FALSE);
                    } } while(FindNextFileA(h, &f)); FindClose(h); ScanGameData(); }
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
                    if (i != -1) { char n[256], p[MAX_PATH], mP[MAX_PATH], eP[MAX_PATH*2], eM[MAX_PATH*2], cmd[8192];
                        ListView_GetItemText(hListView, i, 0, n, 256); sprintf(p, "%s\\%s", ksp_path, n); sprintf(mP, "%s\\%s\\kspman_manifest.txt", ksp_path, n);
                        EscapeForPS(p, eP); EscapeForPS(mP, eM);
                        sprintf(cmd, "Add-Type -AssemblyName PresentationFramework; Get-ChildItem '%s' -Recurse | Where-Object {!$_.PSIsContainer -and $_.Name -ne 'kspman_manifest.txt'} | ForEach-Object {$_.FullName} | Out-File -FilePath '%s' -Encoding utf8; [System.Windows.MessageBox]::Show('Manifest Generated','Status')", eP, eM);
                        RunPS(cmd); ScanGameData(); } break;
                        Sleep(1500);
                        ScanGameData();
                }
                case ID_MENU_VER_MAN: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) { char n[256], mP[MAX_PATH], eM[MAX_PATH*2], cmd[8192];
                        ListView_GetItemText(hListView, i, 0, n, 256); sprintf(mP, "%s\\%s\\kspman_manifest.txt", ksp_path, n);
                        EscapeForPS(mP, eM);
                        sprintf(cmd, "Add-Type -AssemblyName PresentationFramework; if(!(Test-Path '%s')){[System.Windows.MessageBox]::Show('error manifest does not exist. cannot verify.','Verify');exit} $m=Get-Content '%s'; $miss=@(); foreach($f in $m){if($f.Trim() -and !(Test-Path $f.Trim())){$miss+=$f}}; if($miss.Count -eq 0){[System.Windows.MessageBox]::Show('manifest is valid: all files are present.','Verify')}else{[System.Windows.MessageBox]::Show('manifest invalid: ' + $miss.Count + ' files missing.','Verify')}", eM, eM);
                        RunPS(cmd); } break;
                }
                case ID_MENU_REPACK: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) { char n[256], mP[MAX_PATH], dZ[MAX_PATH]={0}, eM[MAX_PATH*2], eZ[MAX_PATH*2], cmd[16384];
                        ListView_GetItemText(hListView, i, 0, n, 256); sprintf(mP, "%s\\%s\\kspman_manifest.txt", ksp_path, n);
                        OPENFILENAMEA sfn = {sizeof(sfn), hwnd}; sfn.lpstrFilter = "ZIP\0*.zip\0"; sfn.lpstrFile = dZ; sfn.nMaxFile = MAX_PATH; sfn.Flags = OFN_OVERWRITEPROMPT;
                        if (GetSaveFileNameA(&sfn)) { EscapeForPS(mP, eM); EscapeForPS(dZ, eZ);
                            sprintf(cmd, "Add-Type -AssemblyName PresentationFramework; if(!(Test-Path '%s')){[System.Windows.MessageBox]::Show('error no manifest. create one first.','Repack');exit} $f=Get-Content '%s'|Where-Object{Test-Path $_}; Compress-Archive -Path $f -DestinationPath '%s' -Force; [System.Windows.MessageBox]::Show('repack complete','Status')", eM, eM, eZ);
                            RunPS(cmd); } } break;
                }
                case ID_MENU_PATH: {
                    BROWSEINFOA bi = {hwnd, 0, 0, "Select GameData", BIF_RETURNONLYFSDIRS|BIF_USENEWUI};
                    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi); if(pidl){ SHGetPathFromIDListA(pidl, ksp_path); SaveConfig(); ScanGameData(); }
                    break;
                }
                case ID_MENU_ZIP: {
                    OPENFILENAMEA ofn = {sizeof(ofn), hwnd}; char f[MAX_PATH] = {0}; ofn.lpstrFilter = "ZIP\0*.zip\0"; ofn.lpstrFile = f; ofn.nMaxFile = MAX_PATH;
                    if(GetOpenFileNameA(&ofn)) { char ck[MAX_PATH*2], cz[MAX_PATH*2], cmd[16384]; EscapeForPS(ksp_path, ck); EscapeForPS(f, cz); sprintf(cmd, "Add-Type -AssemblyName PresentationFramework; $z='%s';$k='%s';$t=Join-Path $env:TEMP ('kspt_'+(Get-Random)); Expand-Archive $z $t -Force; $g=if(Test-Path \\\"$t\\GameData\\\"){\\\"$t\\GameData\\*\\\"}else{\\\"$t\\*\\\"}; Get-ChildItem $g -Recurse|ForEach-Object{$r=$_.FullName.Substring($t.Length).TrimStart('\\\\'); if($r.StartsWith('GameData')){$r=$r.Substring(9)} $d=Join-Path $k $r; if($_.PSIsContainer){New-Item $d -ItemType Directory -Force|Out-Null}else{Copy-Item $_.FullName $d -Force} $mD=$r.Split('\\\\')[0]; if($mD){$mp=Join-Path $k (Join-Path $mD 'kspman_manifest.txt'); Add-Content $mp $d}}; [System.Windows.MessageBox]::Show('Installation Complete','Status')", cz, ck); RunPS(cmd); ScanGameData(); }
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
                case ID_MENU_UNINSTALL: {
                    int i = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    if (i != -1) {
                        char n[256];
                        UNINSTALL_DATA* ud = malloc(sizeof(UNINSTALL_DATA));
                        ListView_GetItemText(hListView, i, 0, n, 256);
                        
                        sprintf(ud->modPath, "%s\\%s", ksp_path, n);
                        sprintf(ud->manifestPath, "%s\\kspman_manifest.txt", ud->modPath);

                        FILE* f = fopen(ud->manifestPath, "r");
                        if (f) {
                            fseek(f, 0, SEEK_END);
                            long sz = ftell(f);
                            rewind(f);
                            ud->fileListBuffer = malloc(sz + 1);
                            fread(ud->fileListBuffer, 1, sz, f);
                            ud->fileListBuffer[sz] = 0;
                            fclose(f);
                        } else {
                            ud->fileListBuffer = strdup("no manifest found. only the folder will be removed.");
                        }

                        static char className[] = "kspman_uninstall";
                        WNDCLASSA uwc = {0, UninstallProc, 0, 0, GetModuleHandle(NULL), 0, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW+1), 0, className};
                        RegisterClassA(&uwc);
                        CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, className, "Confirm Uninstall", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 400, 300, 450, 400, hwnd, NULL, NULL, ud);
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
    WNDCLASSA wc = {0, WindowProc, 0, 0, h, 0, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW+1), 0, "kspman"};
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "kspman", "kspman", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, 600, 420, NULL, NULL, h, NULL);
    ShowWindow(hwnd, n);
    MSG m; while(GetMessage(&m, 0, 0, 0)){ TranslateMessage(&m); DispatchMessage(&m); }
    return 0;
}
