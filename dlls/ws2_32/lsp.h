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
 * Standard WSPPROC_TABLE - 35 WSP function slots (0..34)
 * Filled by LSP DLL during WSPStartup.  Layout must match ws2spi.h.
 * ===================================================================== */
typedef struct _WSPPROC_TABLE
{
    void *lpWSPAbsorbRecv;          /* 0  */
    void *lpWSPAbsorbRecvFrom;      /* 1  */
    void *lpWSPAccept;              /* 2  */
    void *lpWSPAddressToString;     /* 3  */
    void *lpWSPAsyncSelect;         /* 4  */
    void *lpWSPBind;                /* 5  */
    void *lpWSPCancelBlockingCall;  /* 6  */
    void *lpWSPCleanup;             /* 7  */
    void *lpWSPCloseSocket;         /* 8  */
    void *lpWSPConnect;             /* 9  - CRITICAL */
    void *lpWSPConnectListen;       /* 10 */
    void *lpWSPDatagramRecv;        /* 11 */
    void *lpWSPDatagramSend;        /* 12 */
    void *lpWSPDuplicateSocket;     /* 13 */
    void *lpWSPEnumNetworkEvents;   /* 14 */
    void *lpWSPEventSelect;         /* 15 */
    void *lpWSPGetOverlappedResult; /* 16 */
    void *lpWSPGetPeerName;         /* 17 */
    void *lpWSPGetQOSByName;        /* 18 */
    void *lpWSPGetSockName;         /* 19 */
    void *lpWSPGetSockOpt;          /* 20 */
    void *lpWSPIoctl;               /* 21 */
    void *lpWSPJoinLeaf;            /* 22 */
    void *lpWSPListen;              /* 23 */
    void *lpWSPRecv;                /* 24 */
    void *lpWSPRecvDisconnect;      /* 25 */
    void *lpWSPRecvFrom;            /* 26 */
    void *lpWSPSelect;              /* 27 */
    void *lpWSPSend;                /* 28 */
    void *lpWSPSendDisconnect;      /* 29 */
    void *lpWSPSendTo;              /* 30 */
    void *lpWSPSetSockOpt;          /* 31 */
    void *lpWSPShutdown;            /* 32 */
    void *lpWSPSocket;              /* 33 - CRITICAL */
    void *lpWSPStringToAddress;     /* 34 */
} WSPPROC_TABLE, *LPWSPPROC_TABLE;

/* ======================================================================
 * WPU Upcall Table - 15 callbacks (0..14)
 * Layout must match Windows SDK ws2spi.h exactly.
 * ===================================================================== */
typedef struct _WPUUPCALLTABLE
{
    void *lpWPUCloseEvent;              /* 0  */
    void *lpWPUCloseSocketHandle;       /* 1  */
    void *lpWPUCreateEvent;             /* 2  */
    void *lpWPUTransmitFile;            /* 3  */
    void *lpWPUFDIsSet;                 /* 4  */
    void *lpWPUGetProviderPath;         /* 5  */
    void *lpWPUModifyFSCloseHandle;     /* 6  */
    void *lpWPUOpenCurrentThread;       /* 7  */
    void *lpWPUPostMessage;             /* 8  */
    void *lpWPUQueryBlockingCallback;   /* 9  */
    void *lpWPUQuerySocketHandleContext;/* 10 */
    void *lpWPUQueueApc;                /* 11 */
    void *lpWPUResetEvent;              /* 12 */
    void *lpWPUSetEvent;                /* 13 */
    void *lpWPUOpenCurrentThread2;      /* 14 (Vista+) */
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
