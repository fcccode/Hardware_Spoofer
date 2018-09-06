#include <ntifs.h>
#include <Ntdddisk.h>
#include <windef.h>
#include "main.h"
#include <ntddk.h>

#define FILE_DEVICE_SCSI 0x0000001b
#define IOCTL_SCSI_MINIPORT_IDENTIFY ((FILE_DEVICE_SCSI << 16) + 0x0501)
#define  IDE_ATAPI_IDENTIFY  0xA1  //  Returns ID sector for ATAPI.
#define  IDE_ATA_IDENTIFY    0xEC  //  Returns ID sector for ATA.
#define  IOCTL_SCSI_MINIPORT 0x0004D008  //  see NTDDSCSI.H for definition

PDRIVER_DISPATCH RealDiskDeviceControl = NULL;
PDRIVER_DISPATCH RealDiskDeviceControl2 = NULL;

char NumTable[] = "IUHONQXAWEBUIZGERFXIHUONERCIUGOZBECRQUIGHZOBQWUPOI8787990382872348932462367452924324223432524123456789";
char SpoofedHWID[] = "XYXYXYYYY-YXYXXYY-YXXXYXX-XXYYXYYXXYY\0";
BOOL HWIDGenerated = 0;

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

	}

	RtlCopyMemory((void*)serialNumber, (void*)SpoofedHWID, 40);

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

		for (int i = 0; i<30; i++)
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

	//if (input->irDriveRegs.bCommandReg == ATA_IDENTIFY_DEVICE) {

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

	case IOCTL_SCSI_MINIPORT_IDENTIFY:
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
	case IDE_ATAPI_IDENTIFY:
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
	case IDE_ATA_IDENTIFY:
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
	case IOCTL_STORAGE_GET_MEDIA_SERIAL_NUMBER:
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

VOID  Unload(IN  PDRIVER_OBJECT  pDriverObject) {
	//do whatever you like here
	//this deletes the device
	IoDeleteDevice(pDriverObject->DeviceObject);


	return;
}

NTSTATUS DriverEntry(_In_  struct _DRIVER_OBJECT *DriverObject, _In_  PUNICODE_STRING RegistryPath)
{
	NTSTATUS        status = STATUS_SUCCESS;

	UNICODE_STRING diskDrvName;
	RtlInitUnicodeString(&diskDrvName, L"\\Driver\\storahci");

	PDRIVER_OBJECT diskDrvObj = GetDriverObject(&diskDrvName);
	//PDRIVER_OBJECT diskDrvObj2 = GetDriverObject(L"\\Driver\\spaceport");
	//RealDiskDeviceControl2 = diskDrvObj2->MajorFunction[IRP_MJ_DEVICE_CONTROL];


	RealDiskDeviceControl = diskDrvObj->MajorFunction[IRP_MJ_DEVICE_CONTROL];

	diskDrvObj->DriverInit = &DriverEntry;
	diskDrvObj->DriverStart = (PVOID)DriverObject;
	diskDrvObj->DriverSize = (ULONG)RegistryPath;
	diskDrvObj->FastIoDispatch = NULL;
	diskDrvObj->DriverStartIo = NULL;
	diskDrvObj->DriverUnload = NULL;

	//diskDrvObj2->DriverInit = &DriverEntry;
	//diskDrvObj2->DriverStart = (PVOID)DriverObject;
	//diskDrvObj2->DriverSize = (ULONG)RegistryPath;
	//diskDrvObj2->FastIoDispatch = NULL;
	//diskDrvObj2->DriverStartIo = NULL;
	//diskDrvObj2->DriverUnload = NULL;

	/*for (ULONG t = 0; t <= IRP_MJ_MAXIMUM_FUNCTION; t++)
	diskDrvObj->MajorFunction[t] = &UnsupportedDispatch;*/

	diskDrvObj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = &DiskDriverDispatch;
	//diskDrvObj2->MajorFunction[IRP_MJ_DEVICE_CONTROL] = &DiskDriverDispatch2;

	/*diskDrvObj->MajorFunction[IRP_MJ_CREATE] = &CreateDispatch;
	diskDrvObj->MajorFunction[IRP_MJ_CLOSE] = &CloseDispatch;*/
	return status;
}