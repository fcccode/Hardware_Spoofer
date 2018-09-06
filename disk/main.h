#pragma once

#include <ntifs.h>
#include <windef.h>

typedef NTSTATUS(NTAPI OBREFERENCEOBJECTBYNAME) (
	PUNICODE_STRING ObjectPath,
	ULONG Attributes,
	PACCESS_STATE PassedAccessState OPTIONAL,
	ACCESS_MASK DesiredAccess OPTIONAL,
	POBJECT_TYPE ObjectType,
	KPROCESSOR_MODE AccessMode,
	PVOID ParseContext OPTIONAL,
	PVOID *ObjectPtr);

__declspec(dllimport) OBREFERENCEOBJECTBYNAME ObReferenceObjectByName;
__declspec(dllimport) POBJECT_TYPE *IoDriverObjectType;

typedef struct _REQUEST_STRUCT
{
	PIO_COMPLETION_ROUTINE OldRoutine;
	PVOID OldContext;
	ULONG OutputBufferLength;
	PSTORAGE_DEVICE_DESCRIPTOR StorageDescriptor;
} REQUEST_STRUCT, *PREQUEST_STRUCT;