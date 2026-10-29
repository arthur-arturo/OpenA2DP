#include "opena2dp.h"

static NTSTATUS
OpenA2dpFormatBrbForRequest(
    _In_ POPENA2DP_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ PBRB Brb,
    _In_ size_t BrbSize)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFMEMORY memory;
    NTSTATUS status;

    if (BrbSize == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Request;
    status = WdfMemoryCreatePreallocated(
        &attributes, Brb, BrbSize, &memory);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return WdfIoTargetFormatRequestForInternalIoctlOthers(
        DeviceContext->IoTarget,
        Request,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        memory,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
OpenA2dpRetrieveRemoteAddress(
    _Inout_ POPENA2DP_DEVICE_CONTEXT DeviceContext)
{
    WDF_MEMORY_DESCRIPTOR output;
    BTH_DEVICE_INFO info;
    NTSTATUS status;

    PAGED_CODE();
    RtlZeroMemory(&info, sizeof(info));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&output, &info, sizeof(info));

    status = WdfIoTargetSendInternalIoctlOthersSynchronously(
        DeviceContext->IoTarget,
        DeviceContext->QueryRequest,
        IOCTL_INTERNAL_BTHENUM_GET_DEVINFO,
        NULL,
        &output,
        NULL,
        NULL);
    if (NT_SUCCESS(status)) {
        DeviceContext->RemoteAddress = info.address;
    }
    return status;
}

static EVT_WDF_REQUEST_COMPLETION_ROUTINE OpenA2dpOpenCompletion;

static VOID
OpenA2dpOpenCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context)
{
    POPENA2DP_DEVICE_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Target);
    context = (POPENA2DP_DEVICE_CONTEXT)Context;
    status = Params->IoStatus.Status;

    WdfSpinLockAcquire(context->StateLock);
    if (NT_SUCCESS(status)) {
        context->ChannelHandle =
            ((PBRB_L2CA_OPEN_CHANNEL)&context->OpenBrb)->ChannelHandle;
        context->State = OpenA2dpChannelOpen;
    } else {
        context->ChannelHandle = NULL;
        context->State = OpenA2dpChannelFailed;
    }
    WdfSpinLockRelease(context->StateLock);

    WdfRequestComplete(Request, status);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
OpenA2dpOpenAvdtpChannel(
    _Inout_ POPENA2DP_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST CreateRequest)
{
    PBRB_L2CA_OPEN_CHANNEL brb;
    NTSTATUS status;

    brb = (PBRB_L2CA_OPEN_CHANNEL)&DeviceContext->OpenBrb;
    DeviceContext->ProfileInterface.BthReuseBrb(
        (PBRB)brb, BRB_L2CA_OPEN_CHANNEL);

    brb->Hdr.ClientContext[0] = DeviceContext;
    brb->BtAddress = DeviceContext->RemoteAddress;
    brb->Psm = OPENA2DP_AVDTP_PSM;
    brb->ChannelFlags = CF_ROLE_EITHER;
    brb->ConfigOut.Flags = CFG_MTU;
    brb->ConfigOut.Mtu.Max = OPENA2DP_SIGNALING_MTU;
    brb->ConfigOut.Mtu.Min = L2CAP_MIN_MTU;
    brb->ConfigOut.Mtu.Preferred = OPENA2DP_SIGNALING_MTU;
    brb->ConfigIn.Flags = CFG_MTU;
    brb->ConfigIn.Mtu = brb->ConfigOut.Mtu;

    brb->CallbackFlags = CALLBACK_DISCONNECT;
    brb->Callback = OpenA2dpIndicationCallback;
    brb->CallbackContext = DeviceContext;
    brb->ReferenceObject =
        WdfDeviceWdmGetDeviceObject(DeviceContext->Device);
    brb->IncomingQueueDepth = OPENA2DP_INCOMING_QUEUE_DEPTH;

    status = OpenA2dpFormatBrbForRequest(
        DeviceContext,
        CreateRequest,
        (PBRB)brb,
        sizeof(*brb));
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WdfRequestSetCompletionRoutine(
        CreateRequest, OpenA2dpOpenCompletion, DeviceContext);

    WdfSpinLockAcquire(DeviceContext->StateLock);
    DeviceContext->State = OpenA2dpChannelOpening;
    WdfSpinLockRelease(DeviceContext->StateLock);

    if (!WdfRequestSend(CreateRequest, DeviceContext->IoTarget, NULL)) {
        return WdfRequestGetStatus(CreateRequest);
    }

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
OpenA2dpCloseAvdtpChannel(
    _Inout_ POPENA2DP_DEVICE_CONTEXT DeviceContext)
{
    PBRB_L2CA_CLOSE_CHANNEL brb;
    WDF_MEMORY_DESCRIPTOR input;
    WDF_REQUEST_REUSE_PARAMS reuse;
    L2CAP_CHANNEL_HANDLE channel;

    PAGED_CODE();

    WdfSpinLockAcquire(DeviceContext->StateLock);
    channel = DeviceContext->ChannelHandle;
    if (channel == NULL) {
        DeviceContext->State = OpenA2dpChannelClosed;
        WdfSpinLockRelease(DeviceContext->StateLock);
        return;
    }
    DeviceContext->State = OpenA2dpChannelClosing;
    WdfSpinLockRelease(DeviceContext->StateLock);

    brb = (PBRB_L2CA_CLOSE_CHANNEL)&DeviceContext->CloseBrb;
    DeviceContext->ProfileInterface.BthReuseBrb(
        (PBRB)brb, BRB_L2CA_CLOSE_CHANNEL);
    brb->BtAddress = DeviceContext->RemoteAddress;
    brb->ChannelHandle = channel;

    WDF_REQUEST_REUSE_PARAMS_INIT(
        &reuse, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_UNSUCCESSFUL);
    (VOID)WdfRequestReuse(DeviceContext->CloseRequest, &reuse);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&input, brb, sizeof(*brb));

    (VOID)WdfIoTargetSendInternalIoctlOthersSynchronously(
        DeviceContext->IoTarget,
        DeviceContext->CloseRequest,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        &input,
        NULL,
        NULL,
        NULL);

    WdfSpinLockAcquire(DeviceContext->StateLock);
    DeviceContext->ChannelHandle = NULL;
    DeviceContext->State = OpenA2dpChannelClosed;
    WdfSpinLockRelease(DeviceContext->StateLock);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
OpenA2dpIndicationCallback(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters)
{
    POPENA2DP_DEVICE_CONTEXT context =
        (POPENA2DP_DEVICE_CONTEXT)Context;

    UNREFERENCED_PARAMETER(Parameters);

    if (Indication == IndicationRemoteDisconnect) {
        WdfSpinLockAcquire(context->StateLock);
        context->ChannelHandle = NULL;
        context->State = OpenA2dpChannelClosed;
        WdfSpinLockRelease(context->StateLock);
    }
}
