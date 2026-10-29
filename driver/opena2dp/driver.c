#include <initguid.h>
#include "opena2dp.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, OpenA2dpEvtDriverDeviceAdd)
#pragma alloc_text(PAGE, OpenA2dpEvtDeviceFileCreate)
#pragma alloc_text(PAGE, OpenA2dpEvtFileClose)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDFDRIVER driver;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&config, OpenA2dpEvtDriverDeviceAdd);
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        &driver);
    return status;
}

NTSTATUS
OpenA2dpEvtDriverDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFDEVICE device;
    POPENA2DP_DEVICE_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        OpenA2dpEvtDeviceFileCreate,
        OpenA2dpEvtFileClose,
        WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(
        DeviceInit,
        &fileConfig,
        WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes, OPENA2DP_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = OpenA2dpGetDeviceContext(device);
    RtlZeroMemory(context, sizeof(*context));
    context->Device = device;
    context->IoTarget = WdfDeviceGetIoTarget(device);
    context->State = OpenA2dpChannelClosed;

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &context->StateLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfRequestCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        context->IoTarget,
        &context->OpenRequest);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfRequestCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        context->IoTarget,
        &context->CloseRequest);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfRequestCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        context->IoTarget,
        &context->QueryRequest);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfFdoQueryForInterface(
        device,
        &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE,
        (PINTERFACE)&context->ProfileInterface,
        sizeof(context->ProfileInterface),
        BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI,
        NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = OpenA2dpRetrieveRemoteAddress(context);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = OpenA2dpEvtIoDeviceControl;
    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_OPENA2DP,
        NULL);
}

VOID
OpenA2dpEvtDeviceFileCreate(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject)
{
    POPENA2DP_DEVICE_CONTEXT context;
    LONG state;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(FileObject);
    PAGED_CODE();

    context = OpenA2dpGetDeviceContext(Device);
    WdfSpinLockAcquire(context->StateLock);
    state = context->State;
    WdfSpinLockRelease(context->StateLock);

    if (state != OpenA2dpChannelClosed) {
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }

    status = OpenA2dpOpenAvdtpChannel(context, Request);
    if (!NT_SUCCESS(status)) {
        WdfSpinLockAcquire(context->StateLock);
        context->State = OpenA2dpChannelFailed;
        WdfSpinLockRelease(context->StateLock);
        WdfRequestComplete(Request, status);
    }
}

VOID
OpenA2dpEvtFileClose(
    _In_ WDFFILEOBJECT FileObject)
{
    PAGED_CODE();
    OpenA2dpCloseAvdtpChannel(
        OpenA2dpGetDeviceContext(WdfFileObjectGetDevice(FileObject)));
}

VOID
OpenA2dpEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    POPENA2DP_DEVICE_CONTEXT context;
    POPENA2DP_STATUS statusBuffer = NULL;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    size_t bytesReturned = 0;

    UNREFERENCED_PARAMETER(InputBufferLength);

    if (IoControlCode != IOCTL_OPENA2DP_GET_STATUS) {
        WdfRequestComplete(Request, status);
        return;
    }

    if (OutputBufferLength < sizeof(OPENA2DP_STATUS)) {
        WdfRequestCompleteWithInformation(
            Request, STATUS_BUFFER_TOO_SMALL, bytesReturned);
        return;
    }

    context = OpenA2dpGetDeviceContext(WdfIoQueueGetDevice(Queue));
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(OPENA2DP_STATUS),
        (PVOID*)&statusBuffer,
        NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    RtlZeroMemory(statusBuffer, sizeof(*statusBuffer));
    WdfSpinLockAcquire(context->StateLock);
    statusBuffer->State = (ULONG)context->State;
    statusBuffer->ChannelOpen =
        (context->ChannelHandle != NULL) ? TRUE : FALSE;
    WdfSpinLockRelease(context->StateLock);
    statusBuffer->RemoteAddress = context->RemoteAddress;

    bytesReturned = sizeof(OPENA2DP_STATUS);
    WdfRequestCompleteWithInformation(
        Request, STATUS_SUCCESS, bytesReturned);
}
