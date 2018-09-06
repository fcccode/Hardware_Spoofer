#include <ntifs.h>
#include <Ntdddisk.h>
#include <windef.h>
#include "main.h"
#include <sys/stat.h>
#include <tchar.h>
#include <stdio.h>
#include <winapifamily.h>
#include <iostream.h>
#include <sys/stat.h>

PDRIVER_DISPATCH RealDiskDeviceControl = NULL;
char NumTable[] = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char SpoofedHWID[] = "XYXYXYYYYYXYXXYXYYYXXYYXXXXYYXYYYXYYX\0";
BOOL HWIDGenerated = 0;

typedef struct _WIN32_FIND_DATA {
	DWORD    dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD    nFileSizeHigh;
	DWORD    nFileSizeLow;
	DWORD    dwReserved0;
	DWORD    dwReserved1;
	TCHAR    cFileName[MAX_PATH];
	TCHAR    cAlternateFileName[14];
} WIN32_FIND_DATA, *PWIN32_FIND_DATA, *LPWIN32_FIND_DATA;

PDRIVER_OBJECT GetDriverObject(PUNICODE_STRING DriverName)
{
	PDRIVER_OBJECT DrvObject;
	if (NT_SUCCESS(ObReferenceObjectByName(DriverName, 0, NULL, 0, *IoDriverObjectType, KernelMode, NULL, &DrvObject)))
	{
		return DrvObject;
	}

	return NULL;
}




NTSTATUS SpoofSerialNumber(char* serialNumber)
{
	if (!HWIDGenerated)
	{
		HWIDGenerated = 1;

		LARGE_INTEGER Seed;
		KeQuerySystemTimePrecise(&Seed);

		for (int i = 0; i < strlen(SpoofedHWID); ++i)
		{

			if (SpoofedHWID[i] == 'Y')
			{
				SpoofedHWID[i] = RtlRandomEx(&Seed.LowPart) % 26 + 65;

			}

			if (SpoofedHWID[i] == 'X')
			{
				SpoofedHWID[i] = NumTable[RtlRandomEx(&Seed.LowPart) % (strlen(NumTable) - 1)];
			}
		}

		RtlCopyMemory((void*)serialNumber, (void*)SpoofedHWID, 21);
	}
	return STATUS_SUCCESS;
}

NTSTATUS StorageQueryCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
	PIO_COMPLETION_ROUTINE OldCompletionRoutine = NULL;
	PVOID OldContext = NULL;
	ULONG OutputBufferLength = 0;
	PSTORAGE_DEVICE_DESCRIPTOR descriptor = NULL;

	if (Context != NULL)
	{
		REQUEST_STRUCT* pRequest = (REQUEST_STRUCT*)Context;
		OldCompletionRoutine = pRequest->OldRoutine;
		OldContext = pRequest->OldContext;
		OutputBufferLength = pRequest->OutputBufferLength;
		descriptor = pRequest->StorageDescriptor;

		ExFreePool(Context);
	}

	if (FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, SerialNumberOffset) < OutputBufferLength && descriptor->SerialNumberOffset > 0 && descriptor->SerialNumberOffset < OutputBufferLength)
	{
		char* SerialNumber = ((char*)descriptor) + descriptor->SerialNumberOffset;

		SpoofSerialNumber(SerialNumber);
	}

	if ((Irp->StackCount >(ULONG)1) && (OldCompletionRoutine != NULL))
		return OldCompletionRoutine(DeviceObject, Irp, OldContext);

	return STATUS_SUCCESS;
}

NTSTATUS SmartCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	PIO_COMPLETION_ROUTINE OldCompletionRoutine = NULL;
	PVOID OldContext = NULL;

	if (Context != NULL)
	{
		REQUEST_STRUCT* pRequest = (REQUEST_STRUCT*)Context;
		OldCompletionRoutine = pRequest->OldRoutine;
		OldContext = pRequest->OldContext;
		ExFreePool(Context);
	}

	Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

	return Irp->IoStatus.Status;
}

NTSTATUS DiskDriverDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	PIO_STACK_LOCATION Io = IoGetCurrentIrpStackLocation(Irp);

	switch (Io->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_STORAGE_QUERY_PROPERTY:
	{
		PSTORAGE_PROPERTY_QUERY query = (PSTORAGE_PROPERTY_QUERY)Irp->AssociatedIrp.SystemBuffer;

		if (query->PropertyId == StorageDeviceProperty)
		{
			Io->Control = 0;
			Io->Control |= SL_INVOKE_ON_SUCCESS;

			PVOID OldContext = Io->Context;
			Io->Context = (PVOID)ExAllocatePool(NonPagedPool, sizeof(REQUEST_STRUCT));
			REQUEST_STRUCT *pRequest = (REQUEST_STRUCT*)Io->Context;
			pRequest->OldRoutine = Io->CompletionRoutine;
			pRequest->OldContext = OldContext;
			pRequest->OutputBufferLength = Io->Parameters.DeviceIoControl.OutputBufferLength;
			pRequest->StorageDescriptor = (PSTORAGE_DEVICE_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;

			Io->CompletionRoutine = (PIO_COMPLETION_ROUTINE)StorageQueryCompletionRoutine;
		}

		break;

	}

	case SMART_RCV_DRIVE_DATA:
	{
		Io->Control = 0;
		Io->Control |= SL_INVOKE_ON_SUCCESS;

		PVOID OldContext = Io->Context;
		Io->Context = (PVOID)ExAllocatePool(NonPagedPool, sizeof(REQUEST_STRUCT));
		REQUEST_STRUCT *pRequest = (REQUEST_STRUCT*)Io->Context;
		pRequest->OldRoutine = Io->CompletionRoutine;
		pRequest->OldContext = OldContext;

		Io->CompletionRoutine = (PIO_COMPLETION_ROUTINE)SmartCompletionRoutine;

		break;
	}
	}

	return RealDiskDeviceControl(DeviceObject, Irp);
}

NTSTATUS UnsupportedDispatch(
	_In_ struct _DEVICE_OBJECT *DeviceObject,
	_Inout_ struct _IRP *Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Irp->IoStatus.Status;
}

NTSTATUS CreateDispatch(
	_In_ struct _DEVICE_OBJECT *DeviceObject,
	_Inout_ struct _IRP *Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Irp->IoStatus.Status;
}

NTSTATUS CloseDispatch(_In_ struct _DEVICE_OBJECT *DeviceObject, _Inout_ struct _IRP *Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Irp->IoStatus.Status;
}

NTSTATUS DriverEntry(_In_  struct _DRIVER_OBJECT *DriverObject, _In_  PUNICODE_STRING RegistryPath)
{
	NTSTATUS        status = STATUS_SUCCESS;

	UNICODE_STRING diskDrvName;
	RtlInitUnicodeString(&diskDrvName, L"\\Driver\\disk");

	PDRIVER_OBJECT diskDrvObj = GetDriverObject(&diskDrvName);

	RealDiskDeviceControl = diskDrvObj->MajorFunction[IRP_MJ_DEVICE_CONTROL];

	diskDrvObj->DriverInit = &DriverEntry;
	diskDrvObj->DriverStart = (PVOID)DriverObject;
	diskDrvObj->DriverSize = (ULONG)RegistryPath;
	diskDrvObj->FastIoDispatch = NULL;
	diskDrvObj->DriverStartIo = NULL;
	diskDrvObj->DriverUnload = NULL;

	/*for (ULONG t = 0; t <= IRP_MJ_MAXIMUM_FUNCTION; t++)
	diskDrvObj->MajorFunction[t] = &UnsupportedDispatch;*/

	diskDrvObj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = &DiskDriverDispatch;
	/*diskDrvObj->MajorFunction[IRP_MJ_CREATE] = &CreateDispatch;
	diskDrvObj->MajorFunction[IRP_MJ_CLOSE] = &CloseDispatch;*/

	return status;
}