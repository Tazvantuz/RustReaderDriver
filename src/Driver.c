#include <ntifs.h>
#include <ntddk.h>

#define DEVICE_NAME  L"\\Device\\RustReader"
#define SYMBOLIC_LINK L"\\DosDevices\\RustReader"

#define IOCTL_READ_MEMORY  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_GET_PEB      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

typedef struct _READ_REQUEST {
    ULONG  ProcessId;
    PVOID  Address;
    PVOID  Buffer;
    SIZE_T Size;
} READ_REQUEST, *PREAD_REQUEST;

typedef struct _PEB_REQUEST {
    ULONG ProcessId;
    PVOID PebAddress;
} PEB_REQUEST, *PPEB_REQUEST;

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DriverUnload;
NTSTATUS DispatchCreateClose(PDEVICE_OBJECT, PIRP);
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT, PIRP);
NTSTATUS ReadProcessMemory(ULONG, PVOID, PVOID, SIZE_T);

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    PDEVICE_OBJECT DeviceObject = NULL;
    UNICODE_STRING DeviceName, SymLink;
    NTSTATUS status;

    RtlInitUnicodeString(&DeviceName, DEVICE_NAME);
    status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&SymLink, SYMBOLIC_LINK);
    status = IoCreateSymbolicLink(&SymLink, &DeviceName);
    if (!NT_SUCCESS(status)) { IoDeleteDevice(DeviceObject); return status; }

    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    DriverObject->DriverUnload                         = DriverUnload;

    DbgPrint("[RustReader] loaded.\n");
    return STATUS_SUCCESS;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING SymLink;
    RtlInitUnicodeString(&SymLink, SYMBOLIC_LINK);
    IoDeleteSymbolicLink(&SymLink);
    IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG bytesIO = 0;
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;

    if (code == IOCTL_READ_MEMORY) {
        if (stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(READ_REQUEST)) {
            PREAD_REQUEST req = (PREAD_REQUEST)Irp->AssociatedIrp.SystemBuffer;
            status = ReadProcessMemory(req->ProcessId, req->Address, req->Buffer, req->Size);
            if (NT_SUCCESS(status)) bytesIO = (ULONG)req->Size;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    } else if (code == IOCTL_GET_PEB) {
        if (stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(PEB_REQUEST)) {
            PPEB_REQUEST req = (PPEB_REQUEST)Irp->AssociatedIrp.SystemBuffer;
            PEPROCESS Process;
            status = PsLookupProcessByProcessId(ULongToHandle(req->ProcessId), &Process);
            if (NT_SUCCESS(status)) {
                req->PebAddress = PsGetProcessPeb(Process);
                ObDereferenceObject(Process);
                bytesIO = sizeof(PEB_REQUEST);
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesIO;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS ReadProcessMemory(ULONG ProcessId, PVOID Address, PVOID Buffer, SIZE_T Size) {
    PEPROCESS Process;
    NTSTATUS status = PsLookupProcessByProcessId(ULongToHandle(ProcessId), &Process);
    if (!NT_SUCCESS(status)) return status;

    status = MmCopyVirtualMemory(Process, Address,
                                 PsGetCurrentProcess(), Buffer,
                                 Size, KernelMode, NULL);

    ObDereferenceObject(Process);
    return status;
}