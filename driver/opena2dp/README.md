# OpenA2DP KMDF profile driver

This is a development-stage Windows kernel-mode Bluetooth profile driver. It
currently opens and closes the AVDTP signaling channel (PSM `0x0019`) for an
A2DP Sink device selected through the development INF. It also exposes
`IOCTL_OPENA2DP_GET_STATUS` through `GUID_DEVINTERFACE_OPENA2DP`.

It does **not** create a Windows audio endpoint, negotiate codecs, stream media,
or replace the production Alternative A2DP Driver yet. Do not install this INF
on a production machine: it deliberately targets the A2DP Sink service and must
first be validated on a dedicated test machine/VM with a rollback plan.

The driver communicates with `BthPort` through the documented Bluetooth profile
DDIs and BRBs. It does not use WinUSB, change the radio driver, disable HID,
enable test signing, or modify Secure Boot.
