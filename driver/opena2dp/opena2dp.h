#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <bthguid.h>
#include <bthioctl.h>
#include <bthddi.h>

#include "public.h"

#define OPENA2DP_POOL_TAG 'p2AO'
#define OPENA2DP_AVDTP_PSM 0x0019
#define OPENA2DP_SIGNALING_MTU 672
#define OPENA2DP_INCOMING_QUEUE_DEPTH 10

typedef struct _OPENA2DP_DEVICE_CONTEXT {
    WDFDEVICE Device;
    WDFIOTARGET IoTarget;
    BTH_PROFILE_DRIVER_INTERFACE ProfileInterface;
    BTH_ADDR RemoteAddress;
    L2CAP_CHANNEL_HANDLE ChannelHandle;
    LONG State;
    WDFSPINLOCK StateLock;
    WDFREQUEST OpenRequest;
    WDFREQUEST CloseRequest;
    WDFREQUEST QueryRequest;
    BRB OpenBrb;
    BRB CloseBrb;
} OPENA2DP_DEVICE_CONTEXT, *POPENA2DP_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    OPENA2DP_DEVICE_CONTEXT, OpenA2dpGetDeviceContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD OpenA2dpEvtDriverDeviceAdd;
EVT_WDF_DEVICE_FILE_CREATE OpenA2dpEvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE OpenA2dpEvtFileClose;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL OpenA2dpEvtIoDeviceControl;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
OpenA2dpRetrieveRemoteAddress(
    _Inout_ POPENA2DP_DEVICE_CONTEXT DeviceContext);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
OpenA2dpOpenAvdtpChannel(
    _Inout_ POPENA2DP_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST CreateRequest);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
OpenA2dpCloseAvdtpChannel(
    _Inout_ POPENA2DP_DEVICE_CONTEXT DeviceContext);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
OpenA2dpIndicationCallback(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters);
