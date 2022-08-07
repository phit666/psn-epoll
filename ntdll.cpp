#include "ntdll.h"
#include <Windows.h>
#include <assert.h>

#define AFD_POLL_RECEIVE           0x0001
#define AFD_POLL_RECEIVE_EXPEDITED 0x0002
#define AFD_POLL_SEND              0x0004
#define AFD_POLL_DISCONNECT        0x0008
#define AFD_POLL_ABORT             0x0010
#define AFD_POLL_LOCAL_CLOSE       0x0020
#define AFD_POLL_ACCEPT            0x0080
#define AFD_POLL_CONNECT_FAIL      0x0100

typedef struct _AFD_POLL_HANDLE_INFO {
    HANDLE Handle;
    ULONG Events;
    NTSTATUS Status;
} AFD_POLL_HANDLE_INFO, * PAFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
    LARGE_INTEGER Timeout;
    ULONG NumberOfHandles;
    ULONG Exclusive;
    AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO, * PAFD_POLL_INFO;


typedef LONG NTSTATUS;
typedef NTSTATUS* PNTSTATUS;

typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG_PTR Information;
} IO_STATUS_BLOCK, * PIO_STATUS_BLOCK;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, * POBJECT_ATTRIBUTES;

#define RTL_CONSTANT_STRING(s) \
  { sizeof(s) - sizeof((s)[0]), sizeof(s), s }

#define RTL_CONSTANT_OBJECT_ATTRIBUTES(ObjectName, Attributes) \
  { sizeof(OBJECT_ATTRIBUTES), NULL, ObjectName, Attributes, NULL, NULL }


typedef ULONG(WINAPI* _RtlNtStatusToDosError)(NTSTATUS);
typedef void(NTAPI* PIO_APC_ROUTINE)(PVOID, PIO_STATUS_BLOCK, ULONG);
typedef NTSTATUS(NTAPI* _NtCreateFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER,
    ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
typedef NTSTATUS(NTAPI* _NtCancelIoFileEx)(HANDLE, PIO_STATUS_BLOCK, PIO_STATUS_BLOCK);
typedef NTSTATUS(NTAPI* _NtDeviceIoControlFile)(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG, PVOID, ULONG);
typedef NTSTATUS(NTAPI* _NtCreateKeyedEvent)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, ULONG);
typedef NTSTATUS(NTAPI* _NtReleaseKeyedEvent)(HANDLE, PVOID, BOOLEAN, PLARGE_INTEGER);
typedef NTSTATUS(NTAPI* _NtWaitForKeyedEvent)(HANDLE, PVOID, BOOLEAN, PLARGE_INTEGER);

static _RtlNtStatusToDosError RtlNtStatusToDosError = NULL;
static _NtCreateFile NtCreateFile = NULL;
static _NtCancelIoFileEx NtCancelIoFileEx = NULL;
static _NtDeviceIoControlFile NtDeviceIoControlFile = NULL;
static _NtCreateKeyedEvent NtCreateKeyedEvent = NULL;
static _NtReleaseKeyedEvent NtReleaseKeyedEvent = NULL;
static _NtWaitForKeyedEvent NtWaitForKeyedEvent = NULL;

int ntload() {
    HMODULE nt = LoadLibraryA("ntdll.dll");
    if (nt == NULL)
        return -1;
    RtlNtStatusToDosError = (_RtlNtStatusToDosError)GetProcAddress(nt, "RtlNtStatusToDosError");
    NtCreateFile = (_NtCreateFile)GetProcAddress(nt, "NtCreateFile");
    NtCancelIoFileEx = (_NtCancelIoFileEx)GetProcAddress(nt, "NtCancelIoFileEx");
    NtDeviceIoControlFile = (_NtDeviceIoControlFile)GetProcAddress(nt, "NtDeviceIoControlFile");
    NtCreateKeyedEvent = (_NtCreateKeyedEvent)GetProcAddress(nt, "NtCreateKeyedEvent");
    NtReleaseKeyedEvent = (_NtReleaseKeyedEvent)GetProcAddress(nt, "NtReleaseKeyedEvent");
    NtWaitForKeyedEvent = (_NtWaitForKeyedEvent)GetProcAddress(nt, "NtWaitForKeyedEvent");
}

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS) 0x00000000L)
#endif

#ifndef STATUS_PENDING
#define STATUS_PENDING ((NTSTATUS) 0x00000103L)
#endif

#ifndef STATUS_CANCELLED
#define STATUS_CANCELLED ((NTSTATUS) 0xC0000120L)
#endif

#ifndef STATUS_NOT_FOUND
#define STATUS_NOT_FOUND ((NTSTATUS) 0xC0000225L)
#endif

#define IOCTL_AFD_POLL 0x00012024
#ifndef FILE_OPEN
#define FILE_OPEN 0x00000001UL
#endif
#define KEYEDEVENT_WAIT 0x00000001UL
#define KEYEDEVENT_WAKE 0x00000002UL
#define KEYEDEVENT_ALL_ACCESS \
  (STANDARD_RIGHTS_REQUIRED | KEYEDEVENT_WAIT | KEYEDEVENT_WAKE)

static UNICODE_STRING afd__device_name =
RTL_CONSTANT_STRING(L"\\Device\\Afd\\Epoll");

static OBJECT_ATTRIBUTES afd__device_attributes =
RTL_CONSTANT_OBJECT_ATTRIBUTES(&afd__device_name, 0);

int afd_create_device_handle(HANDLE iocp_handle,

    HANDLE* afd_device_handle_out) {
    HANDLE afd_device_handle;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;

    status = NtCreateFile(&afd_device_handle,
        SYNCHRONIZE,
        &afd__device_attributes,
        &iosb,
        NULL,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN,
        0,
        NULL,
        0);

    if (status != STATUS_SUCCESS) {
        return -1;
    }

    if (CreateIoCompletionPort(afd_device_handle, iocp_handle, 0, 0) == NULL)
        goto error;

    if (!SetFileCompletionNotificationModes(afd_device_handle,
        FILE_SKIP_SET_EVENT_ON_HANDLE))
        goto error;

    *afd_device_handle_out = afd_device_handle;
    return 0;

error:
    CloseHandle(afd_device_handle);
    return -1;
}

int afd_poll(HANDLE afd_device_handle,
    AFD_POLL_INFO* poll_info,
    IO_STATUS_BLOCK* io_status_block) {
    NTSTATUS status;

    /* Blocking operation is not supported. */
    assert(io_status_block != NULL);

    io_status_block->Status = STATUS_PENDING;
    status = NtDeviceIoControlFile(afd_device_handle,
        NULL,
        NULL,
        io_status_block,
        io_status_block,
        IOCTL_AFD_POLL,
        poll_info,
        sizeof * poll_info,
        poll_info,
        sizeof * poll_info);

    if (status == STATUS_SUCCESS)
        return 0;
    else if (status == STATUS_PENDING)
        return -1;// return_set_error(-1, ERROR_IO_PENDING);
    else
        return -1;// return_set_error(-1, RtlNtStatusToDosError(status));
}

int afd_cancel_poll(HANDLE afd_device_handle,
    IO_STATUS_BLOCK* io_status_block) {
    NTSTATUS cancel_status;
    IO_STATUS_BLOCK cancel_iosb;

    /* If the poll operation has already completed or has been cancelled earlier,
     * there's nothing left for us to do. */
    if (io_status_block->Status != STATUS_PENDING)
        return 0;

    cancel_status =
        NtCancelIoFileEx(afd_device_handle, io_status_block, &cancel_iosb);

    /* NtCancelIoFileEx() may return STATUS_NOT_FOUND if the operation completed
     * just before calling NtCancelIoFileEx(). This is not an error. */
    if (cancel_status == STATUS_SUCCESS || cancel_status == STATUS_NOT_FOUND)
        return 0;
    else
        return -1;// return_set_error(-1, RtlNtStatusToDosError(cancel_status));
}
