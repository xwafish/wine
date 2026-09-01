/*
 * Wine LSP (Layered Service Provider) Support - Header
 *
 * Compatible with Wine 10.14 (release-10.14 branch)
 * Included by protocol_lsp.c and socket.c after ws2_32_private.h.
 */
#ifndef __WINE_WS2_32_LSP_H
#define __WINE_WS2_32_LSP_H

#include <winsock2.h>
#include <wine/list.h>

/* If ws2spi.h types are missing */
#ifndef _WINSOCK2SPI_
typedef enum _WSC_PROVIDER_INFO_TYPE
{
    ProviderInfoLspCategories,
    ProviderInfoAudit,
} WSC_PROVIDER_INFO_TYPE;
#endif

#define LSP_MAX_PROVIDERS        256
#define LSP_CATALOG_REGISTRY_PATH \
    L"System\\CurrentControlSet\\Services\\WinSock2\\Parameters\\Protocol_Catalog9"
#define LSP_CATALOG_ENTRIES_PATH  LSP_CATALOG_REGISTRY_PATH L"\\Catalog_Entries"
#define LSP_VAL_NUM_ENTRIES       L"Num_Catalog_Entries"
#define LSP_VAL_SERIAL_NUMBER     L"Serial_Number"
#define LSP_VAL_NEXT_ENTRY_ID     L"Next_Catalog_Entry_ID"
#define LSP_VAL_PACKED_ENTRY      L"PackedCatalogEntry"
#define LSP_BASE_CATALOG_ENTRY_ID 2001

/* ======================================================================
 * Standard WSPPROC_TABLE - 33 WSP function slots (0..32)
 * Filled by LSP DLL during WSPStartup.
 * Layout must match Windows SDK ws2spi.h EXACTLY.
 *
 * Previous version had 2 extra Vista+ entries (lpWSPAbsorbRecv/From)
 * at indices 0-1, shifting all WSP offsets by 2.  The DLL writes
 * lpWSPAccept at offset 0 per SDK, so our struct must start there.
 * ===================================================================== */
typedef struct _WSPPROC_TABLE
{
    void *lpWSPAccept;              /* 0  */
    void *lpWSPAddressToString;     /* 1  */
    void *lpWSPAsyncSelect;         /* 2  */
    void *lpWSPBind;                /* 3  */
    void *lpWSPCancelBlockingCall;  /* 4  */
    void *lpWSPCleanup;             /* 5  */
    void *lpWSPCloseSocket;         /* 6  */
    void *lpWSPConnect;             /* 7  - CRITICAL */
    void *lpWSPConnectListen;       /* 8  */
    void *lpWSPDatagramRecv;        /* 9  */
    void *lpWSPDatagramSend;        /* 10 */
    void *lpWSPDuplicateSocket;     /* 11 */
    void *lpWSPEnumNetworkEvents;   /* 12 */
    void *lpWSPEventSelect;         /* 13 */
    void *lpWSPGetOverlappedResult; /* 14 */
    void *lpWSPGetPeerName;         /* 15 */
    void *lpWSPGetQOSByName;        /* 16 */
    void *lpWSPGetSockName;         /* 17 */
    void *lpWSPGetSockOpt;          /* 18 */
    void *lpWSPIoctl;               /* 19 */
    void *lpWSPJoinLeaf;            /* 20 */
    void *lpWSPListen;              /* 21 */
    void *lpWSPRecv;                /* 22 */
    void *lpWSPRecvDisconnect;      /* 23 */
    void *lpWSPRecvFrom;            /* 24 */
    void *lpWSPSelect;              /* 25 */
    void *lpWSPSend;                /* 26 */
    void *lpWSPSendDisconnect;      /* 27 */
    void *lpWSPSendTo;              /* 28 */
    void *lpWSPSetSockOpt;          /* 29 */
    void *lpWSPShutdown;            /* 30 */
    void *lpWSPSocket;              /* 31 - CRITICAL */
    void *lpWSPStringToAddress;     /* 32 */
} WSPPROC_TABLE, *LPWSPPROC_TABLE;

/* ======================================================================
 * WPU Upcall Table - 15 callbacks (0..14)
 * Layout must match Windows SDK ws2spi.h EXACTLY.
 *
 * Previous version had wrong entries from index 3 onwards:
 *   index 3 was lpWPUTransmitFile (NOT in WPUUPCALLTABLE!)
 *   index 4 was lpWPUFDIsSet (should be index 5)
 * This caused ALL 12 upcall pointers from param6 onwards to be
 * at wrong positions in the 19-param WSPStartup call, resulting
 * in ERROR_INVALID_PARAMETER (87) from SSLVPNRedirector.dll.
 * ===================================================================== */
typedef struct _WPUUPCALLTABLE
{
    void *lpWPUCloseEvent;              /* 0  */
    void *lpWPUCloseSocketHandle;       /* 1  */
    void *lpWPUCreateEvent;             /* 2  */
    void *lpWPUCreateThread;            /* 3  */
    void *lpWPUDisableBlockingHook;     /* 4  */
    void *lpWPUFDIsSet;                 /* 5  */
    void *lpWPUGetProviderPath;         /* 6  */
    void *lpWPUModifyFSCloseHandle;     /* 7  */
    void *lpWPUOpenCurrentThread;       /* 8  */
    void *lpWPUPostMessage;             /* 9  */
    void *lpWPUQueryBlockingCallback;   /* 10 */
    void *lpWPUQuerySocketHandleContext;/* 11 */
    void *lpWPUQueueApc;                /* 12 */
    void *lpWPUResetEvent;              /* 13 */
    void *lpWPUSetEvent;                /* 14 */
} WPUUPCALLTABLE, *LPWPUUPCALLTABLE;

/* WSP function signatures - for casting void* from WSPPROC_TABLE */

typedef SOCKET (WINAPI *LSP_WSPSOCKET_FUNC)( int, int, int, LPWSAPROTOCOL_INFOW, unsigned int, DWORD );
typedef int (WINAPI *LSP_WSPCONNECT_FUNC)( SOCKET, const struct sockaddr *, int, LPWSABUF, LPWSABUF, LPQOS, LPQOS );

/* ======================================================================
 * WSPStartup entry point - TWO signatures supported
 *
 * 1) Standard 4-param (ret $0x10): most LSPs use this.
 * 2) Extended 19-param (ret $0x4c): some LSP SDKs pass the entire
 *    WPUUPCALLTABLE by value (15 function pointers expanded as
 *    individual DWORD parameters), plus an extra function pointer
 *    at param2.  Detected by disassembling ret instruction.
 * ===================================================================== */

/* Standard WSPStartup: 4 params, stdcall ret $0x10 */
typedef int (WINAPI *LSP_WSPSTARTUP_FUNC)(
    WORD wVersionRequested,
    LPWSAPROTOCOL_INFOW lpProtocolInfo,
    LPWPUUPCALLTABLE lpUpcallTable,
    LPWSPPROC_TABLE lpProcTable
);

/* Extended WSPStartup: 19 params, stdcall ret $0x4c
 *
 * Standard SDK expansion of WPUUPCALLTABLE by value:
 *   Param1: wVersionRequested
 *   Param2: lpProtocolInfo (NOT pfnNext!)
 *   Param3-17: 15 upcall table entries (expanded)
 *   Param18: lpProcTable
 *   Param19: lpErrno
 *
 * The previous assumption that param2 was pfnNext caused
 * ERROR_INVALID_PARAMETER (87) because the DLL received
 * NULL as lpProtocolInfo. */
typedef int (WINAPI *LSP_WSPSTARTUP_FUNC_EX)(
    WORD wVersionRequested,
    LPWSAPROTOCOL_INFOW lpProtocolInfo,
    void *uc0,  void *uc1,  void *uc2,  void *uc3,  void *uc4,
    void *uc5,  void *uc6,  void *uc7,  void *uc8,  void *uc9,
    void *uc10, void *uc11, void *uc12, void *uc13, void *uc14,
    LPWSPPROC_TABLE lpProcTable,
    int *lpErrno
);

/* Provider catalog entry */
typedef struct _LSP_PROVIDER_ENTRY
{
    struct list           entry;
    WSAPROTOCOL_INFOW     info;
    WCHAR                 dll_path[MAX_PATH];
    HMODULE               dll_handle;
    LPWSPPROC_TABLE       proc_table;
    BOOL                  enabled;
    DWORD                 ref_count;
} LSP_PROVIDER_ENTRY;

/* Global catalog */
typedef struct _LSP_CATALOG
{
    struct list           providers;
    int                   count;
    DWORD                 serial_number;
    DWORD                 next_entry_id;
    CRITICAL_SECTION      lock;
    BOOL                  initialized;
    BOOL                  lsp_enabled;
} LSP_CATALOG;

/* Functions from lsp.c */
void     lsp_catalog_init(void);
void     lsp_catalog_cleanup(void);
int      lsp_catalog_load(void);
int      lsp_catalog_save(void);
LSP_PROVIDER_ENTRY *lsp_find_provider_by_guid(const GUID *guid);
LSP_PROVIDER_ENTRY *lsp_find_provider_by_entry_id(DWORD catalog_entry_id);
LSP_PROVIDER_ENTRY *lsp_find_provider_by_match(int af, int type, int protocol);
int      lsp_add_provider(const GUID *guid, const WCHAR *path,
                          const WSAPROTOCOL_INFOW *info, DWORD count);
int      lsp_remove_provider(const GUID *guid);
int      lsp_enable_provider(const GUID *guid, BOOL enable);
int      lsp_enum_protocols(int *protocols, WSAPROTOCOL_INFOW *buffer,
                            DWORD *buffer_len, BOOL include_builtin);
int      lsp_load_provider(LSP_PROVIDER_ENTRY *provider);
void     lsp_unload_provider(LSP_PROVIDER_ENTRY *provider);
LPWSPPROC_TABLE lsp_get_provider_dispatch(LSP_PROVIDER_ENTRY *provider);
LSP_PROVIDER_ENTRY *lsp_get_chain_next(LSP_PROVIDER_ENTRY *provider);
BOOL     lsp_is_lsp_loaded(void);
void     lsp_set_lsp_enabled(BOOL enabled);
int      lsp_write_provider_order(DWORD *entry, DWORD number);
BOOL     lsp_stack_low(void);  /* check if current thread has < 32KB stack left */

#endif /* __WINE_WS2_32_LSP_H */
