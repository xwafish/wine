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
 * WSP Procedure Table
 * Matches Windows WSPPROC_TABLE layout expected by LSP DLLs.
 * ===================================================================== */
typedef struct _WSPPROC_TABLE
{
    /* Upcall functions (WPU*) - indices 0..17 */
    void *lpWPUCloseEvent;          /* 0 */
    void *lpWPUCloseSocketHandle;   /* 1 */
    void *lpWPUCreateEvent;         /* 2 */
    void *lpWPUCreateThread;        /* 3 */
    void *lpWPUDisableBlockingHook; /* 4 */
    void *lpWPUFDIsSet;             /* 5 */
    void *lpWPUGetProviderPath;     /* 6 */
    void *lpWPUModifyFSCloseHandle; /* 7 */
    void *lpWPUOpenCurrentThread;   /* 8 */
    void *lpWPUPostMessage;         /* 9 */
    void *lpWPUQueryBlockingCallback; /* 10 */
    void *lpWPUQuerySocketHandleContext; /* 11 */
    void *lpWPUQueueApc;            /* 12 */
    void *lpWPUResetEvent;          /* 13 */
    void *lpWPUSetEvent;            /* 14 */
    void *lpWPUOpenCurrentThread2;  /* 15 */
    void *lpWPUCloseThread;         /* 16 */
    /* WSP functions - indices 17..48 - filled by LSP via WSPStartup */
    void *lpWSPAbsorbRecv;          /* 17 */
    void *lpWSPAbsorbRecvFrom;      /* 18 */
    void *lpWSPAccept;              /* 19 */
    void *lpWSPAddressToString;     /* 20 */
    void *lpWSPAsyncSelect;         /* 21 */
    void *lpWSPBind;                /* 22 */
    void *lpWSPCancelBlockingCall;  /* 23 */
    void *lpWSPCleanup;             /* 24 */
    void *lpWSPCloseSocket;         /* 25 */
    void *lpWSPConnect;             /* 26 - CRITICAL */
    void *lpWSPConnectListen;       /* 27 */
    void *lpWSPDatagramRecv;        /* 28 */
    void *lpWSPDatagramSend;        /* 29 */
    void *lpWSPDuplicateSocket;     /* 30 */
    void *lpWSPEnumNetworkEvents;   /* 31 */
    void *lpWSPEventSelect;         /* 32 */
    void *lpWSPGetOverlappedResult; /* 33 */
    void *lpWSPGetPeerName;         /* 34 */
    void *lpWSPGetQOSByName;        /* 35 */
    void *lpWSPGetSockName;         /* 36 */
    void *lpWSPGetSockOpt;          /* 37 */
    void *lpWSPIoctl;               /* 38 */
    void *lpWSPJoinLeaf;            /* 39 */
    void *lpWSPListen;              /* 40 */
    void *lpWSPRecv;                /* 41 */
    void *lpWSPRecvDisconnect;      /* 42 */
    void *lpWSPRecvFrom;            /* 43 */
    void *lpWSPSelect;              /* 44 */
    void *lpWSPSend;                /* 45 */
    void *lpWSPSendDisconnect;      /* 46 */
    void *lpWSPSendTo;              /* 47 */
    void *lpWSPSetSockOpt;          /* 48 */
    void *lpWSPShutdown;            /* 49 */
    void *lpWSPSocket;              /* 50 - CRITICAL */
    void *lpWSPStringToAddress;     /* 51 */
} WSPPROC_TABLE, *LPWSPPROC_TABLE;

/* WSPSocket function signature - for casting void* from WSPPROC_TABLE */
typedef SOCKET (WINAPI *LSP_WSPSOCKET_FUNC)( int, int, int, LPWSAPROTOCOL_INFOW, unsigned int, DWORD );
typedef int (WINAPI *LSP_WSPCONNECT_FUNC)( SOCKET, const struct sockaddr *, int, LPWSABUF, LPWSABUF, LPQOS, LPQOS );

/* WSPStartup entry point type */
typedef int (WINAPI *LSP_WSPSTARTUP_FUNC)(
    WORD wVersionRequested,
    LPWSAPROTOCOL_INFOW lpProtocolInfo,
    LPWSAPROTOCOL_INFOW lpProtocolInfoNext,
    void *lpUpcallTable,
    LPWSPPROC_TABLE lpProcTable
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

#endif /* __WINE_WS2_32_LSP_H */
