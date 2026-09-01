/*
 * Wine LSP (Layered Service Provider) Support - Header
 *
 * Compatible with Wine 10.14 (release-10.14 branch)
 * Included by protocol_lsp.c and socket.c after ws2_32_private.h.
 *
 * All struct layouts MUST match Windows SDK ws2spi.h EXACTLY.
 * Reference: https://github.com/tpn/winsdk-10/blob/master/Include/10.0.16299.0/um/WS2spi.h
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
 * WSPDATA - returned by WSPStartup to negotiate version.
 * Reference: ws2spi.h typedef struct WSPData
 * ===================================================================== */
#define WSPDESCRIPTION_LEN 255

typedef struct _LSP_WSPDATA
{
    WORD  wVersion;
    WORD  wHighVersion;
    WCHAR szDescription[WSPDESCRIPTION_LEN + 1];
} LSP_WSPDATA, *LSP_LPWSPDATA;

/* ======================================================================
 * WSPPROC_TABLE - 30 WSP function slots (0..29)
 * Filled by LSP DLL during WSPStartup.
 * Layout must match Windows SDK ws2spi.h EXACTLY.
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
    void *lpWSPDuplicateSocket;     /* 8  */
    void *lpWSPEnumNetworkEvents;   /* 9  */
    void *lpWSPEventSelect;         /* 10 */
    void *lpWSPGetOverlappedResult; /* 11 */
    void *lpWSPGetPeerName;         /* 12 */
    void *lpWSPGetSockName;         /* 13 */
    void *lpWSPGetSockOpt;          /* 14 */
    void *lpWSPGetQOSByName;        /* 15 */
    void *lpWSPIoctl;               /* 16 */
    void *lpWSPJoinLeaf;            /* 17 */
    void *lpWSPListen;              /* 18 */
    void *lpWSPRecv;                /* 19 */
    void *lpWSPRecvDisconnect;      /* 20 */
    void *lpWSPRecvFrom;            /* 21 */
    void *lpWSPSelect;              /* 22 */
    void *lpWSPSend;                /* 23 */
    void *lpWSPSendDisconnect;      /* 24 */
    void *lpWSPSendTo;              /* 25 */
    void *lpWSPSetSockOpt;          /* 26 */
    void *lpWSPShutdown;            /* 27 */
    void *lpWSPSocket;              /* 28 - CRITICAL */
    void *lpWSPStringToAddress;     /* 29 */
} WSPPROC_TABLE, *LPWSPPROC_TABLE;

/* ======================================================================
 * Upcall table - 15 entries passed BY VALUE in WSPStartup.
 *
 * There are TWO different upcall table layouts in Windows SDK history:
 *
 * 1) WPUUPCALLTABLE (old SDK, e.g. Platform SDK / Win7 SDK):
 *      Used by older LSP DLLs compiled with pre-Win10 headers.
 *      Has WPUCreateThread, WPUDisableBlockingHook, WPUModifyFSCloseHandle.
 *
 * 2) WSPUPCALLTABLE (Windows 10 SDK 10.0.16299.0+):
 *      Renamed from WPUUPCALLTABLE, reordered, added WPUCreateSocketHandle,
 *      WPUModifyIFSHandle, WPUCloseThread.  Removed WPUCreateThread,
 *      WPUDisableBlockingHook, WPUModifyFSCloseHandle.
 *
 * We use the OLD WPUUPCALLTABLE order since commercial LSP DLLs
 * (e.g. SSLVPNRedirector.dll) are typically compiled with older SDKs.
 * ===================================================================== */
typedef struct _WPUUPCALLTABLE
{
    void *lpWPUCloseEvent;              /* 0  */
    void *lpWPUCloseSocketHandle;       /* 1  */
    void *lpWPUCreateEvent;             /* 2  */
    void *lpWPUCreateThread;            /* 3  - old SDK */
    void *lpWPUDisableBlockingHook;    /* 4  - old SDK */
    void *lpWPUFDIsSet;                 /* 5  */
    void *lpWPUGetProviderPath;         /* 6  */
    void *lpWPUModifyFSCloseHandle;     /* 7  - old SDK */
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
 * WSPStartup entry point signatures
 *
 * The DLL has ret $0x4c (76 bytes = 19 DWORDs), confirming 19 params.
 *
 * TWO possible 19-param layouts exist depending on SDK version:
 *
 * Layout A (Win10 SDK WSPUPCALLTABLE, 19 params):
 *   wVersion, lpWSPData, lpProtocolInfo, 15 upcalls, lpProcTable
 *
 * Layout B (old SDK WPUUPCALLTABLE, 19 params):
 *   wVersion, lpProtocolInfo, 15 upcalls, lpProcTable, lpErrno
 *
 * Layout A was tried and returned 87.  Layout B was tried before the
 * 1MB stack fix (unreliable).  We now try Layout A with the OLD
 * WPUUPCALLTABLE upcall order (different member positions 3-14).
 * ===================================================================== */

/* Standard WSPStartup: 5 logical params (for reference) */
typedef int (WINAPI *LSP_WSPSTARTUP_FUNC)(
    WORD wVersionRequested,
    LSP_LPWSPDATA lpWSPData,
    LPWSAPROTOCOL_INFOW lpProtocolInfo,
    LPWPUUPCALLTABLE lpUpcallTable,
    LPWSPPROC_TABLE lpProcTable
);

/* Extended WSPStartup: 19 params (upcall table expanded by value)
 *
 * Layout A (Win10 SDK, with lpWSPData):
 *   Param1:  wVersionRequested
 *   Param2:  lpWSPData
 *   Param3:  lpProtocolInfo
 *   Param4-18: 15 upcall entries (WPUUPCALLTABLE order)
 *   Param19: lpProcTable
 */
typedef int (WINAPI *LSP_WSPSTARTUP_FUNC_EX)(
    WORD wVersionRequested,
    LSP_LPWSPDATA lpWSPData,
    LPWSAPROTOCOL_INFOW lpProtocolInfo,
    void *uc0,  void *uc1,  void *uc2,  void *uc3,  void *uc4,
    void *uc5,  void *uc6,  void *uc7,  void *uc8,  void *uc9,
    void *uc10, void *uc11, void *uc12, void *uc13, void *uc14,
    LPWSPPROC_TABLE lpProcTable
);

/* =====================================================================
 * SDK-compatible WSAPROTOCOL_INFOW (ChainEntries[5])
 *
 * CRITICAL: Wine defines MAX_PROTOCOL_CHAIN=7 (ChainEntries[7]),
 * but the Windows SDK defines MAX_PROTOCOL_CHAIN=5 (ChainEntries[5]).
 * This makes Wine's WSAPROTOCOL_INFOW 8 bytes larger than the SDK version,
 * shifting all fields after ProtocolChain by 8 bytes.
 *
 * LSP DLLs compiled with the Windows SDK access fields at SDK offsets.
 * If we pass Wine's larger struct, the DLL reads wrong field values
 * and returns ERROR_INVALID_PARAMETER (87).
 *
 * Fix: define an SDK-layout struct and convert before calling WSPStartup.
 * ===================================================================== */

#define LSP_SDK_MAX_PROTOCOL_CHAIN 5

/* Inline copy of WSAPROTOCOLCHAIN with 5 entries (matching Windows SDK) */
typedef struct _LSP_SDK_WSAPROTOCOLCHAIN
{
    int   ChainLen;
    DWORD ChainEntries[LSP_SDK_MAX_PROTOCOL_CHAIN];
} LSP_SDK_WSAPROTOCOLCHAIN;

/* WSAPROTOCOL_INFOW with SDK-compatible layout (ChainEntries[5]) */
typedef struct _LSP_SDK_WSAPROTOCOL_INFOW
{
    DWORD                    dwServiceFlags1;
    DWORD                    dwServiceFlags2;
    DWORD                    dwServiceFlags3;
    DWORD                    dwServiceFlags4;
    DWORD                    dwProviderFlags;
    GUID                     ProviderId;
    DWORD                    dwCatalogEntryId;
    LSP_SDK_WSAPROTOCOLCHAIN ProtocolChain;
    int                      iVersion;
    int                      iAddressFamily;
    int                      iMaxSockAddr;
    int                      iMinSockAddr;
    int                      iSocketType;
    int                      iProtocol;
    int                      iProtocolMaxOffset;
    int                      iNetworkByteOrder;
    int                      iSecurityScheme;
    DWORD                    dwMessageSize;
    DWORD                    dwProviderReserved;
    WCHAR                    szProtocol[256 + 1];
} LSP_SDK_WSAPROTOCOL_INFOW;

/* Convert Wine WSAPROTOCOL_INFOW (ChainEntries[7]) to SDK layout (ChainEntries[5]).
 * 'dst' must be >= sizeof(LSP_SDK_WSAPROTOCOL_INFOW) bytes. */
static inline void lsp_convert_info_to_sdk(const WSAPROTOCOL_INFOW *src,
                                           LSP_SDK_WSAPROTOCOL_INFOW *dst)
{
    int i;
    dst->dwServiceFlags1 = src->dwServiceFlags1;
    dst->dwServiceFlags2 = src->dwServiceFlags2;
    dst->dwServiceFlags3 = src->dwServiceFlags3;
    dst->dwServiceFlags4 = src->dwServiceFlags4;
    dst->dwProviderFlags = src->dwProviderFlags;
    dst->ProviderId = src->ProviderId;
    dst->dwCatalogEntryId = src->dwCatalogEntryId;
    dst->ProtocolChain.ChainLen = src->ProtocolChain.ChainLen;
    for (i = 0; i < LSP_SDK_MAX_PROTOCOL_CHAIN && i < src->ProtocolChain.ChainLen; i++)
        dst->ProtocolChain.ChainEntries[i] = src->ProtocolChain.ChainEntries[i];
    for (; i < LSP_SDK_MAX_PROTOCOL_CHAIN; i++)
        dst->ProtocolChain.ChainEntries[i] = 0;
    dst->iVersion = src->iVersion;
    dst->iAddressFamily = src->iAddressFamily;
    dst->iMaxSockAddr = src->iMaxSockAddr;
    dst->iMinSockAddr = src->iMinSockAddr;
    dst->iSocketType = src->iSocketType;
    dst->iProtocol = src->iProtocol;
    dst->iProtocolMaxOffset = src->iProtocolMaxOffset;
    dst->iNetworkByteOrder = src->iNetworkByteOrder;
    dst->iSecurityScheme = src->iSecurityScheme;
    dst->dwMessageSize = src->dwMessageSize;
    dst->dwProviderReserved = src->dwProviderReserved;
    memcpy(dst->szProtocol, src->szProtocol, sizeof(dst->szProtocol));
}

/* Provider catalog entry */
typedef struct _LSP_PROVIDER_ENTRY
{
    struct list           entry;
    WSAPROTOCOL_INFOW     info;           /* Wine layout (ChainEntries[7]) */
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
