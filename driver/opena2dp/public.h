#pragma once

#include <bthdef.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_GUID(
    GUID_DEVINTERFACE_OPENA2DP,
    0x8E2B145A, 0xCF34, 0x4A74,
    0xA4, 0x11, 0x45, 0x8C, 0xF2, 0x56, 0xF5, 0xA7);

#define OPENA2DP_DEVICE_TYPE 0xA2D0
#define IOCTL_OPENA2DP_GET_STATUS \
    CTL_CODE(OPENA2DP_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef enum _OPENA2DP_CHANNEL_STATE {
    OpenA2dpChannelClosed = 0,
    OpenA2dpChannelOpening = 1,
    OpenA2dpChannelOpen = 2,
    OpenA2dpChannelFailed = 3,
    OpenA2dpChannelClosing = 4,
} OPENA2DP_CHANNEL_STATE;

typedef struct _OPENA2DP_STATUS {
    ULONG State;
    BOOLEAN ChannelOpen;
    UCHAR Reserved[3];
    BTH_ADDR RemoteAddress;
} OPENA2DP_STATUS, *POPENA2DP_STATUS;

#ifdef __cplusplus
}
#endif
