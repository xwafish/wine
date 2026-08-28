/*
 * Wine LSP Support - Core Implementation
 * Compatible with Wine 10.14 (release-10.14 branch)
 *
 * NOTE: Do NOT include "config.h". Wine PE builds (__WINE_PE_BUILD)
 * use msvcrt headers which conflict with config.h.
 *
 * NOTE: Do NOT use wcsncpy/wcscpy/swprintf - Wine's winbase.h
 * redefines them to #error. Use lstrcpynW/lstrcpyW/memcpy/wsprintfW instead.
 *
 * Provider catalog: registry-backed Protocol_Catalog9
 * DLL loading: LoadLibrary + WSPStartup
 * WSP dispatch: function pointer table
 *
 * LGPL v2.1+
 */

#include <stdarg.h>  /* va_list - required by Wine PE headers (msvcrt stdarg.h) */

#define NONAMELESSSTRUCT
#define NONAMELESSUNION
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winnls.h"
#include "wine/debug.h"
#include "wine/list.h"
#include <winsock2.h>

#include "lsp.h"

WINE_DEFAULT_DEBUG_CHANNEL(winsock);

static LSP_CATALOG g_catalog;
static volatile LONG g_init_done = 0;

/* ======================================================================
 * Thread-safe one-time initialization
 *
 * CRITICAL: lsp_catalog_init() was never called anywhere, so the
 * CRITICAL_SECTION was never initialized.  First thread entered
 * EnterCriticalSection on zero-filled memory (appeared to work),
 * second thread spun forever because the internal event object was
 * never created.  Fix: auto-initialize via InterlockedCompareExchange
 * before any lock acquisition.
 * ===================================================================== */
static void lsp_ensure_init(void)
{
    if (InterlockedCompareExchange(&g_init_done, 1, 0) == 0)
    {
        list_init(&g_catalog.providers);
        InitializeCriticalSection(&g_catalog.lock);
        g_catalog.initialized = TRUE;
        g_catalog.lsp_enabled = TRUE;
        g_catalog.next_entry_id = LSP_BASE_CATALOG_ENTRY_ID;
        TRACE("LSP catalog initialized\n");
    }
    else
    {
        /* Another thread is initializing – spin until done.
         * Init is very fast (no I/O), so this loop exits quickly. */
        while (!g_catalog.initialized)
            { } /* busy-wait, safe for microsecond-scale init */
    }
}

/* ======================================================================
 * Helpers
 * ===================================================================== */

static void catalog_id_to_key(DWORD id, WCHAR *buf)
{
    static const WCHAR fmt[] = {'%','0','1','2','l','u',0};
    wsprintfW(buf, fmt, (unsigned long)id);
}

static DWORD pack_info(const WSAPROTOCOL_INFOW *info, BYTE *buf, DWORD len)
{
    DWORD n = sizeof(WSAPROTOCOL_INFOW);
    if (len < n) return n;
    memcpy(buf, info, n);
    return n;
}

static DWORD unpack_info(const BYTE *buf, DWORD len, WSAPROTOCOL_INFOW *info)
{
    DWORD n = sizeof(WSAPROTOCOL_INFOW);
    if (len < n) return 0;
    memcpy(info, buf, n);
    return n;
}

static LSP_PROVIDER_ENTRY *alloc_provider(void)
{
    LSP_PROVIDER_ENTRY *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       sizeof(LSP_PROVIDER_ENTRY));
    if (p) list_init(&p->entry);
    return p;
}

static void free_provider(LSP_PROVIDER_ENTRY *p)
{
    if (!p) return;
    if (p->dll_handle) { FreeLibrary(p->dll_handle); p->dll_handle = NULL; }
    if (p->proc_table) { HeapFree(GetProcessHeap(), 0, p->proc_table); p->proc_table = NULL; }
    list_remove(&p->entry);
    HeapFree(GetProcessHeap(), 0, p);
}

/* ======================================================================
 * Catalog Init / Cleanup
 * ===================================================================== */

void lsp_catalog_init(void)
{
    memset(&g_catalog, 0, sizeof(g_catalog));
    list_init(&g_catalog.providers);
    InitializeCriticalSection(&g_catalog.lock);
    g_catalog.initialized = TRUE;
    g_catalog.lsp_enabled = TRUE;
    g_catalog.next_entry_id = LSP_BASE_CATALOG_ENTRY_ID;
    TRACE("LSP catalog initialized\n");
}

void lsp_catalog_cleanup(void)
{
    LSP_PROVIDER_ENTRY *p, *next;
    EnterCriticalSection(&g_catalog.lock);
    LIST_FOR_EACH_ENTRY_SAFE(p, next, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
        free_provider(p);
    g_catalog.count = 0;
    LeaveCriticalSection(&g_catalog.lock);
    DeleteCriticalSection(&g_catalog.lock);
    g_catalog.initialized = FALSE;
}

/* ======================================================================
 * Catalog Load from Registry
 * ===================================================================== */
int lsp_catalog_load(void)
{
    HKEY hCatalog, hEntries;
    DWORD num_entries = 0, serial = 0, next_id = 0, i;
    DWORD type, size;
    WCHAR subkey[64];
    FILETIME ft;

    lsp_ensure_init();
    if (g_catalog.initialized && g_catalog.count > 0) return 0;

    EnterCriticalSection(&g_catalog.lock);
    {
        LSP_PROVIDER_ENTRY *p, *n;
        LIST_FOR_EACH_ENTRY_SAFE(p, n, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
            free_provider(p);
        g_catalog.count = 0;
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LSP_CATALOG_REGISTRY_PATH,
                      0, KEY_READ, &hCatalog) != ERROR_SUCCESS)
    {
        LeaveCriticalSection(&g_catalog.lock);
        return 0;
    }

    size = sizeof(DWORD);
    RegQueryValueExW(hCatalog, LSP_VAL_NUM_ENTRIES, NULL, &type,
                     (BYTE*)&num_entries, &size);
    size = sizeof(DWORD);
    RegQueryValueExW(hCatalog, LSP_VAL_SERIAL_NUMBER, NULL, &type,
                     (BYTE*)&serial, &size);
    g_catalog.serial_number = serial;
    size = sizeof(DWORD);
    RegQueryValueExW(hCatalog, LSP_VAL_NEXT_ENTRY_ID, NULL, &type,
                     (BYTE*)&next_id, &size);
    if (next_id > g_catalog.next_entry_id) g_catalog.next_entry_id = next_id;

    if (RegOpenKeyExW(hCatalog, L"Catalog_Entries", 0, KEY_READ,
                      &hEntries) == ERROR_SUCCESS)
    {
        i = 0;
        while (i < num_entries)
        {
            DWORD sklen = 64;
            if (RegEnumKeyExW(hEntries, i, subkey, &sklen,
                              NULL, NULL, NULL, &ft) != ERROR_SUCCESS)
                break;
            {
                HKEY hEntry;
                WCHAR path[256];
                BYTE blob[1024];
                WSAPROTOCOL_INFOW info;

                {
                    static const WCHAR fmt[] = {'%','s','\\','%','s',0};
                    wsprintfW(path, fmt, LSP_CATALOG_ENTRIES_PATH, subkey);
                }
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ,
                                 &hEntry) == ERROR_SUCCESS)
                {
                    size = sizeof(blob);
                    if (RegQueryValueExW(hEntry, LSP_VAL_PACKED_ENTRY, NULL, &type,
                                        blob, &size) == ERROR_SUCCESS &&
                        unpack_info(blob, size, &info) > 0)
                    {
                        LSP_PROVIDER_ENTRY *p = alloc_provider();
                        if (p)
                        {
                            memcpy(&p->info, &info, sizeof(info));
                            p->enabled = TRUE;
                            {
                                WCHAR dp[MAX_PATH];
                                DWORD psz = sizeof(dp);
                                if (RegQueryValueExW(hEntry, L"ProviderDllPath",
                                                    NULL, &type, (BYTE*)dp, &psz) == ERROR_SUCCESS)
                                    lstrcpyW(p->dll_path, dp);
                            }
                            list_add_tail(&g_catalog.providers, &p->entry);
                            g_catalog.count++;
                            TRACE("Loaded: %s (id=%lu, cl=%d)\n",
                                  debugstr_w(info.szProtocol),
                                  info.dwCatalogEntryId, info.ProtocolChain.ChainLen);
                        }
                    }
                    RegCloseKey(hEntry);
                }
            }
            i++;
        }
        RegCloseKey(hEntries);
    }
    RegCloseKey(hCatalog);
    LeaveCriticalSection(&g_catalog.lock);
    TRACE("LSP catalog: %d providers\n", g_catalog.count);
    return 0;
}

/* ======================================================================
 * Catalog Save to Registry
 * ===================================================================== */
int lsp_catalog_save(void)
{
    HKEY hCatalog, hEntries;
    LSP_PROVIDER_ENTRY *p;
    WCHAR subkey[64];

    TRACE("lsp_catalog_save: enter, count=%d\n", g_catalog.count);
    lsp_ensure_init();
    EnterCriticalSection(&g_catalog.lock);
    g_catalog.serial_number++;
    TRACE("lsp_catalog_save: lock acquired, serial=%lu\n", g_catalog.serial_number);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, LSP_CATALOG_REGISTRY_PATH,
                        0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_ALL_ACCESS, NULL, &hCatalog, NULL) != ERROR_SUCCESS)
    { LeaveCriticalSection(&g_catalog.lock); return -1; }

    if (RegCreateKeyExW(hCatalog, L"Catalog_Entries", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                        NULL, &hEntries, NULL) != ERROR_SUCCESS)
    { RegCloseKey(hCatalog); LeaveCriticalSection(&g_catalog.lock); return -1; }

    { /* Delete old keys */
        DWORD idx = 0, sklen = 64;
        TRACE("lsp_catalog_save: deleting old keys\n");
        while (RegEnumKeyExW(hEntries, idx, subkey, &sklen,
                             NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        {
            RegDeleteTreeW(hEntries, subkey);
            sklen = 64;  /* reset for next iteration */
        }
        TRACE("lsp_catalog_save: old keys deleted\n");
    }

    { DWORD c = g_catalog.count;
      RegSetValueExW(hCatalog, LSP_VAL_NUM_ENTRIES, 0, REG_DWORD, (BYTE*)&c, sizeof(DWORD));
      RegSetValueExW(hCatalog, LSP_VAL_SERIAL_NUMBER, 0, REG_DWORD,
                     (BYTE*)&g_catalog.serial_number, sizeof(DWORD));
      RegSetValueExW(hCatalog, LSP_VAL_NEXT_ENTRY_ID, 0, REG_DWORD,
                     (BYTE*)&g_catalog.next_entry_id, sizeof(DWORD)); }

    LIST_FOR_EACH_ENTRY(p, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
    {
        BYTE blob[sizeof(WSAPROTOCOL_INFOW)];
        DWORD bsz;
        WCHAR key[13], epath[256];
        HKEY hEntry;

        catalog_id_to_key(p->info.dwCatalogEntryId, key);
        {
            static const WCHAR fmt[] = {'%','s','\\','%','s',0};
            wsprintfW(epath, fmt, LSP_CATALOG_ENTRIES_PATH, key);
        }

        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, epath, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                            NULL, &hEntry, NULL) == ERROR_SUCCESS)
        {
            bsz = pack_info(&p->info, blob, sizeof(blob));
            RegSetValueExW(hEntry, LSP_VAL_PACKED_ENTRY, 0, REG_BINARY, blob, bsz);
            if (p->dll_path[0])
            {
                DWORD psz = (wcslen(p->dll_path) + 1) * sizeof(WCHAR);
                RegSetValueExW(hEntry, L"ProviderDllPath", 0, REG_SZ,
                               (BYTE*)p->dll_path, psz);
            }
            RegCloseKey(hEntry);
        }
    }

    RegCloseKey(hEntries);
    RegCloseKey(hCatalog);
    LeaveCriticalSection(&g_catalog.lock);
    TRACE("LSP saved: %d providers, serial=%lu\n", g_catalog.count, g_catalog.serial_number);
    return 0;
}

/* ======================================================================
 * Provider Lookup
 * ===================================================================== */

LSP_PROVIDER_ENTRY *lsp_find_provider_by_guid(const GUID *guid)
{
    LSP_PROVIDER_ENTRY *p;
    LIST_FOR_EACH_ENTRY(p, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
        if (IsEqualGUID(&p->info.ProviderId, guid)) return p;
    return NULL;
}

LSP_PROVIDER_ENTRY *lsp_find_provider_by_entry_id(DWORD id)
{
    LSP_PROVIDER_ENTRY *p;
    LIST_FOR_EACH_ENTRY(p, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
        if (p->info.dwCatalogEntryId == id) return p;
    return NULL;
}

LSP_PROVIDER_ENTRY *lsp_find_provider_by_match(int af, int type, int protocol)
{
    LSP_PROVIDER_ENTRY *best_chain = NULL, *best_base = NULL, *p;
    if (!g_catalog.lsp_enabled) { TRACE("LSP match: disabled\n"); return NULL; }
    LIST_FOR_EACH_ENTRY(p, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
    {
        if (!p->enabled) continue;
        TRACE("LSP match: '%s' af=%d type=%d proto=%d cl=%d flags=%#lx\n",
              debugstr_w(p->info.szProtocol), p->info.iAddressFamily,
              p->info.iSocketType, p->info.iProtocol,
              p->info.ProtocolChain.ChainLen, p->info.dwProviderFlags);
        if (p->info.iAddressFamily != af) continue;
        if (p->info.iSocketType != type) continue;
        if (protocol != 0 && p->info.iProtocol != 0 && p->info.iProtocol != protocol) continue;
        if (!(p->info.dwProviderFlags & PFL_MATCHES_PROTOCOL_ZERO) &&
            protocol == 0 && p->info.iProtocol != 0) continue;
        if (p->info.ProtocolChain.ChainLen > 1) { if (!best_chain) best_chain = p; }
        else if (p->info.ProtocolChain.ChainLen == 1) { if (!best_base) best_base = p; }
    }
    TRACE("LSP match result: chain=%p base=%p\n", best_chain, best_base);
    return best_chain ? best_chain : best_base;
}

/* ======================================================================
 * WPU Upcall Functions
 *
 * Callbacks provided by Winsock to the LSP via WSPStartup.
 * The LSP calls these to interact with the Winsock service provider.
 * ===================================================================== */

/* Thread ID structure used by WPUOpenCurrentThread/CloseThread */
typedef struct { HANDLE ThreadHandle; DWORD Reserved; } WSP_THREAD_ID;

static BOOL WINAPI wpu_CreateEvent(HANDLE *lpEvent, int *lpErrno)
{
    HANDLE h = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!h) { if (lpErrno) *lpErrno = (int)GetLastError(); return FALSE; }
    *lpEvent = h;
    TRACE("WPU: CreateEvent -> %p\n", h);
    return TRUE;
}

static int WINAPI wpu_CloseEvent(HANDLE hEvent, int *lpErrno)
{
    TRACE("WPU: CloseEvent %p\n", hEvent);
    if (!CloseHandle(hEvent)) { if (lpErrno) *lpErrno = (int)GetLastError(); return SOCKET_ERROR; }
    return 0;
}

static int WINAPI wpu_SetEvent(HANDLE hEvent, int *lpErrno)
{
    if (!SetEvent(hEvent)) { if (lpErrno) *lpErrno = (int)GetLastError(); return SOCKET_ERROR; }
    return 0;
}

static int WINAPI wpu_ResetEvent(HANDLE hEvent, int *lpErrno)
{
    if (!ResetEvent(hEvent)) { if (lpErrno) *lpErrno = (int)GetLastError(); return SOCKET_ERROR; }
    return 0;
}

static int WINAPI wpu_OpenCurrentThread(WSP_THREAD_ID *lpThreadId, int *lpErrno)
{
    if (!lpThreadId) { if (lpErrno) *lpErrno = WSAEFAULT; return SOCKET_ERROR; }
    lpThreadId->ThreadHandle = GetCurrentThread();
    lpThreadId->Reserved = 0;
    return 0;
}

static int WINAPI wpu_OpenCurrentThread2(WSP_THREAD_ID *lpThreadId, int *lpErrno)
{
    return wpu_OpenCurrentThread(lpThreadId, lpErrno);
}

static int WINAPI wpu_CloseThread(WSP_THREAD_ID ThreadId, int *lpErrno)
{
    /* GetCurrentThread() returns pseudo-handle, no need to close */
    return 0;
}

static int WINAPI wpu_GetProviderPath(GUID *lpProviderId, WCHAR *path,
                                      int *pathLen, int *lpErrno)
{
    LSP_PROVIDER_ENTRY *p;
    DWORD needed;
    if (!lpProviderId || !path || !pathLen) { if (lpErrno) *lpErrno = WSAEFAULT; return SOCKET_ERROR; }
    p = lsp_find_provider_by_guid(lpProviderId);
    if (!p || !p->dll_path[0]) { if (lpErrno) *lpErrno = WSANO_RECOVERY; return SOCKET_ERROR; }
    needed = (wcslen(p->dll_path) + 1) * sizeof(WCHAR);
    if ((DWORD)*pathLen < (int)needed) { *pathLen = (int)needed; if (lpErrno) *lpErrno = WSAEFAULT; return SOCKET_ERROR; }
    memcpy(path, p->dll_path, needed);
    *pathLen = (int)needed;
    return 0;
}

static int WINAPI wpu_FDIsSet(SOCKET fd, fd_set *set)
{
    unsigned int i;
    if (!set) return 0;
    for (i = 0; i < set->fd_count; i++)
        if (set->fd_array[i] == fd) return 1;
    return 0;
}

static BOOL WINAPI wpu_PostMessage(HWND hWnd, UINT Msg, WPARAM wParam,
                                    LPARAM lParam, int *lpErrno)
{
    return PostMessageW(hWnd, Msg, wParam, lParam);
}

static HANDLE WINAPI wpu_CreateThread(WSP_THREAD_ID *lpThreadId,
                                        LPTHREAD_START_ROUTINE lpfn,
                                        void *param, DWORD flags, int *lpErrno)
{
    DWORD tid;
    HANDLE h = CreateThread(NULL, 0, lpfn, param, flags, &tid);
    if (!h) { if (lpErrno) *lpErrno = (int)GetLastError(); return NULL; }
    if (lpThreadId) { lpThreadId->ThreadHandle = h; lpThreadId->Reserved = tid; }
    TRACE("WPU: CreateThread -> %p tid=%lu\n", h, tid);
    return h;
}

static int WINAPI wpu_QueueApc(WSP_THREAD_ID *lpThreadId, void *lpfnApc,
                                DWORD dwContext, int *lpErrno)
{
    if (!lpThreadId || !lpfnApc) { if (lpErrno) *lpErrno = WSAEFAULT; return SOCKET_ERROR; }
    if (!QueueUserAPC((PAPCFUNC)lpfnApc, lpThreadId->ThreadHandle, dwContext))
    { if (lpErrno) *lpErrno = (int)GetLastError(); return SOCKET_ERROR; }
    return 0;
}

static int WINAPI wpu_QueryBlockingCallback(DWORD entryId, void **lplpfn,
                                             DWORD **lpdwCtx, int *lpErrno)
{
    if (lpErrno) *lpErrno = WSAEOPNOTSUPP;
    return SOCKET_ERROR;
}

static int WINAPI wpu_QuerySocketHandleContext(SOCKET s, DWORD *lpCtx,
                                               int *lpErrno)
{
    if (lpErrno) *lpErrno = WSAEINVAL;
    return SOCKET_ERROR;
}

static int WINAPI wpu_CloseSocketHandle(SOCKET s, int *lpErrno)
{
    if (closesocket(s) == SOCKET_ERROR)
    { if (lpErrno) *lpErrno = WSAGetLastError(); return SOCKET_ERROR; }
    return 0;
}

static int WINAPI wpu_ModifyFSCloseHandle(int *lpErrno)
{
    return 0;
}

static int WINAPI wpu_DisableBlockingHook(int *lpErrno)
{
    return 0;
}

/* Global upcall table instance */
static WPUUPCALLTABLE g_upcall_table;
static BOOL g_upcall_inited = FALSE;

static void lsp_init_upcall_table(void)
{
    if (g_upcall_inited) return;
    g_upcall_table.lpWPUCloseEvent            = wpu_CloseEvent;
    g_upcall_table.lpWPUCloseSocketHandle     = wpu_CloseSocketHandle;
    g_upcall_table.lpWPUCreateEvent            = wpu_CreateEvent;
    g_upcall_table.lpWPUCreateThread           = wpu_CreateThread;
    g_upcall_table.lpWPUDisableBlockingHook    = wpu_DisableBlockingHook;
    g_upcall_table.lpWPUFDIsSet                = wpu_FDIsSet;
    g_upcall_table.lpWPUGetProviderPath        = wpu_GetProviderPath;
    g_upcall_table.lpWPUModifyFSCloseHandle    = wpu_ModifyFSCloseHandle;
    g_upcall_table.lpWPUOpenCurrentThread      = wpu_OpenCurrentThread;
    g_upcall_table.lpWPUPostMessage            = wpu_PostMessage;
    g_upcall_table.lpWPUQueryBlockingCallback  = wpu_QueryBlockingCallback;
    g_upcall_table.lpWPUQuerySocketHandleContext = wpu_QuerySocketHandleContext;
    g_upcall_table.lpWPUQueueApc               = wpu_QueueApc;
    g_upcall_table.lpWPUResetEvent             = wpu_ResetEvent;
    g_upcall_table.lpWPUSetEvent               = wpu_SetEvent;
    g_upcall_table.lpWPUOpenCurrentThread2     = wpu_OpenCurrentThread2;
    g_upcall_inited = TRUE;
    TRACE("WPU upcall table initialized\n");
}

/* ======================================================================
 * Provider DLL Loading
 * ===================================================================== */

int lsp_load_provider(LSP_PROVIDER_ENTRY *provider)
{
    LSP_WSPSTARTUP_FUNC wsp_startup;
    WSAPROTOCOL_INFOW next_info;
    LPWSPPROC_TABLE tbl;
    LSP_PROVIDER_ENTRY *next_p;
    int ret;

    if (!provider || provider->dll_handle) return 0;
    if (!provider->dll_path[0])
    {
        ERR("No DLL path for provider id=%lu\n", provider->info.dwCatalogEntryId);
        return -1;
    }

    TRACE("Loading: %s\n", debugstr_w(provider->dll_path));

    /* Expand environment variables (e.g. %SYSTEMROOT%) */
    {
        WCHAR expanded[MAX_PATH];
        DWORD n = ExpandEnvironmentStringsW(provider->dll_path, expanded, MAX_PATH);
        if (n > 0 && n <= MAX_PATH)
        {
            TRACE("Expanded path: %s\n", debugstr_w(expanded));
            provider->dll_handle = LoadLibraryW(expanded);
        }
        else
            provider->dll_handle = LoadLibraryW(provider->dll_path);
    }
    if (!provider->dll_handle)
    {
        ERR("LoadLibrary failed '%s': %lu\n",
            debugstr_w(provider->dll_path), GetLastError());
        return -1;
    }

    wsp_startup = (LSP_WSPSTARTUP_FUNC)GetProcAddress(provider->dll_handle, "WSPStartup");
    if (!wsp_startup)
    {
        ERR("No WSPStartup in '%s'\n", debugstr_w(provider->dll_path));
        FreeLibrary(provider->dll_handle); provider->dll_handle = NULL;
        return -1;
    }

    tbl = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WSPPROC_TABLE));
    if (!tbl) { FreeLibrary(provider->dll_handle); provider->dll_handle = NULL; return -1; }

    memset(&next_info, 0, sizeof(next_info));
    next_p = NULL;
    if (provider->info.ProtocolChain.ChainLen > 1)
    {
        next_p = lsp_find_provider_by_entry_id(
            provider->info.ProtocolChain.ChainEntries[1]);
        if (next_p) memcpy(&next_info, &next_p->info, sizeof(next_info));
    }

    lsp_init_upcall_table();
    TRACE("Calling WSPStartup: %s (with upcall table)\n", debugstr_w(provider->info.szProtocol));
    ret = wsp_startup(MAKEWORD(2, 2), &provider->info, &next_info, &g_upcall_table, tbl);
    if (ret != 0)
    {
        ERR("WSPStartup failed: %d\n", ret);
        HeapFree(GetProcessHeap(), 0, tbl);
        FreeLibrary(provider->dll_handle); provider->dll_handle = NULL;
        return -1;
    }

    provider->proc_table = tbl;
    provider->ref_count = 1;
    TRACE("Loaded '%s': WSPSocket=%p WSPConnect=%p\n",
          debugstr_w(provider->info.szProtocol),
          tbl->lpWSPSocket, tbl->lpWSPConnect);
    return 0;
}

void lsp_unload_provider(LSP_PROVIDER_ENTRY *provider)
{
    if (!provider) return;
    provider->ref_count--;
    if (provider->ref_count > 0) return;
    if (provider->dll_handle)
    {
        FARPROC c = GetProcAddress(provider->dll_handle, "WSPCleanup");
        if (c) ((int(WINAPI*)(void))c)();
        FreeLibrary(provider->dll_handle); provider->dll_handle = NULL;
    }
    if (provider->proc_table) { HeapFree(GetProcessHeap(), 0, provider->proc_table); provider->proc_table = NULL; }
}

LPWSPPROC_TABLE lsp_get_provider_dispatch(LSP_PROVIDER_ENTRY *provider)
{
    if (!provider) return NULL;
    if (!provider->dll_handle && lsp_load_provider(provider) != 0) return NULL;
    return provider->proc_table;
}

LSP_PROVIDER_ENTRY *lsp_get_chain_next(LSP_PROVIDER_ENTRY *provider)
{
    if (!provider || provider->info.ProtocolChain.ChainLen <= 1) return NULL;
    return lsp_find_provider_by_entry_id(provider->info.ProtocolChain.ChainEntries[1]);
}

/* ======================================================================
 * Add/Remove/Enable Provider
 * ===================================================================== */

int lsp_add_provider(const GUID *guid, const WCHAR *path,
                     const WSAPROTOCOL_INFOW *arr, DWORD count)
{
    DWORD i;
    LSP_PROVIDER_ENTRY *p;
    if (!guid || !path || !arr || !count) return -1;

    lsp_ensure_init();
    EnterCriticalSection(&g_catalog.lock);
    /* Remove old entries for same GUID */
    { LSP_PROVIDER_ENTRY *o, *n;
      LIST_FOR_EACH_ENTRY_SAFE(o, n, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
          if (IsEqualGUID(&o->info.ProviderId, guid)) { free_provider(o); g_catalog.count--; } }

    for (i = 0; i < count; i++)
    {
        p = alloc_provider();
        if (!p) { LeaveCriticalSection(&g_catalog.lock); return -1; }
        memcpy(&p->info, &arr[i], sizeof(WSAPROTOCOL_INFOW));
        memcpy(&p->info.ProviderId, guid, sizeof(GUID));
        if (p->info.dwCatalogEntryId == 0)
            p->info.dwCatalogEntryId = g_catalog.next_entry_id++;
        else if (p->info.dwCatalogEntryId >= g_catalog.next_entry_id)
            g_catalog.next_entry_id = p->info.dwCatalogEntryId + 1;
        lstrcpynW(p->dll_path, path, MAX_PATH);
        p->enabled = TRUE;
        list_add_tail(&g_catalog.providers, &p->entry);
        g_catalog.count++;
        TRACE("Added: %s (id=%lu, cl=%d)\n", debugstr_w(p->info.szProtocol),
              p->info.dwCatalogEntryId, p->info.ProtocolChain.ChainLen);
    }
    LeaveCriticalSection(&g_catalog.lock);
    return 0;
}

int lsp_remove_provider(const GUID *guid)
{
    LSP_PROVIDER_ENTRY *p, *next;
    BOOL found = FALSE;
    if (!guid) return -1;
    lsp_ensure_init();
    EnterCriticalSection(&g_catalog.lock);
    LIST_FOR_EACH_ENTRY_SAFE(p, next, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
        if (IsEqualGUID(&p->info.ProviderId, guid)) { free_provider(p); g_catalog.count--; found = TRUE; }
    LeaveCriticalSection(&g_catalog.lock);
    return found ? 0 : -1;
}

int lsp_enable_provider(const GUID *guid, BOOL enable)
{
    LSP_PROVIDER_ENTRY *p;
    LIST_FOR_EACH_ENTRY(p, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
    {
        if (IsEqualGUID(&p->info.ProviderId, guid))
        { p->enabled = enable; TRACE("Provider %s\n", enable ? "enabled" : "disabled"); return 0; }
    }
    return -1;
}

/* ======================================================================
 * Enum Protocols
 * ===================================================================== */
int lsp_enum_protocols(int *protocols, WSAPROTOCOL_INFOW *buffer,
                       DWORD *buffer_len, BOOL include_builtin)
{
    DWORD needed = 0, count = 0, avail;
    LSP_PROVIDER_ENTRY *p;
    if (!buffer_len) return -1;
    avail = *buffer_len;
    lsp_ensure_init();
    EnterCriticalSection(&g_catalog.lock);
    LIST_FOR_EACH_ENTRY(p, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
    {
        if (!p->enabled) continue;
        if (protocols)
        {
            BOOL m = FALSE; int j;
            for (j = 0; protocols[j]; j++) if (p->info.iProtocol == protocols[j]) { m = TRUE; break; }
            if (!m) continue;
        }
        needed += sizeof(WSAPROTOCOL_INFOW);
        count++;
    }
    *buffer_len = needed;
    if (!buffer || avail < needed)
    {
        LeaveCriticalSection(&g_catalog.lock);
        return count;  /* return count even when buffer is NULL/too small */
    }
    {
        DWORD off = 0;
        LIST_FOR_EACH_ENTRY(p, &g_catalog.providers, LSP_PROVIDER_ENTRY, entry)
        {
            if (!p->enabled) continue;
            if (protocols)
            {
                BOOL m = FALSE; int j;
                for (j = 0; protocols[j]; j++) if (p->info.iProtocol == protocols[j]) { m = TRUE; break; }
                if (!m) continue;
            }
            memcpy(&buffer[off], &p->info, sizeof(WSAPROTOCOL_INFOW));
            off++;
        }
    }
    LeaveCriticalSection(&g_catalog.lock);
    return count;
}

BOOL lsp_is_lsp_loaded(void) { return g_catalog.count > 0; }
void lsp_set_lsp_enabled(BOOL enabled) { g_catalog.lsp_enabled = enabled; }

/* ======================================================================
 * Write Provider Order
 *
 * Reorders the catalog list so that providers listed in 'entry' appear
 * first, in the given order. Remaining providers follow in their
 * current relative order.
 * ===================================================================== */
int lsp_write_provider_order(DWORD *entry, DWORD number)
{
    DWORD i;
    struct list temp;
    LSP_PROVIDER_ENTRY *p, *next_p;

    if (!entry || !number) return -1;

    TRACE("lsp_write_provider_order: load catalog\n");
    lsp_catalog_load();
    TRACE("lsp_write_provider_order: catalog loaded, count=%d\n", g_catalog.count);
    list_init(&temp);
    EnterCriticalSection(&g_catalog.lock);
    TRACE("lsp_write_provider_order: lock acquired\n");

    /* Detach all nodes into temp list */
    if (!list_empty(&g_catalog.providers))
    {
        temp.next = g_catalog.providers.next;
        temp.prev = g_catalog.providers.prev;
        temp.next->prev = &temp;
        temp.prev->next = &temp;
    }
    list_init(&g_catalog.providers);

    /* Move specified entries (in order) to g_catalog */
    for (i = 0; i < number; i++)
    {
        LIST_FOR_EACH_ENTRY(p, &temp, LSP_PROVIDER_ENTRY, entry)
        {
            if (p->info.dwCatalogEntryId == entry[i] && p->enabled)
            {
                list_remove(&p->entry);
                list_add_tail(&g_catalog.providers, &p->entry);
                break;
            }
        }
    }

    /* Append remaining entries */
    TRACE("lsp_write_provider_order: appending remaining\n");
    LIST_FOR_EACH_ENTRY_SAFE(p, next_p, &temp, LSP_PROVIDER_ENTRY, entry)
    {
        list_remove(&p->entry);
        list_add_tail(&g_catalog.providers, &p->entry);
    }

    LeaveCriticalSection(&g_catalog.lock);
    TRACE("lsp_write_provider_order: lock released, calling save\n");
    lsp_catalog_save();
    TRACE("Provider order updated: %lu entries requested\n", number);
    return 0;
}
