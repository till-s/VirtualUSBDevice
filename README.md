## Overview

`VirtualUSBDevice` is a C++ class that creates a virtual USB device on Linux. The virtual device is visible to the rest of the system as any real USB device is.

The implementation relies on the `usbip` subsystem, but is self-contained and doesn't require the `usbip` utility to be manually invoked.

## Features Introduced by This Fork

 - fix speed guessing (PR submitted); must use bcdUSB, not bcdDevice
 - use multiple locks (releaves contention)
 - automatically reply to IN transactions with an empty packet after a (configurable) timeout.
   Otherwise, if the virtual device has nothing to send there will simply be no reply and
   the host will time out.  I found that some software (libftd2xx) does not handle such timeouts
   gracefully.
 - made some members protected in order to encourage subclassing.
 - separated USBIPLib.h into .h and .cpp files. Otherwise, inclusion of the VirtualUSBDevice.h
   from multiple .cpp files is not possible.
 - added simple 'debug level' feature for enabling/disabling diagnostic messages.

## Creation

To create a virtual USB device, instantiate a VirtualUSBDevice (supplying standard USB descriptors to the constructor), and call `start()`.

## Handling Transfers

To handle USB transfers, call `read()`. Before `read()` returns, VirtualUSBDevice will automatically handle standard USB requests (such as `GET_STATUS`, `GET_DESCRIPTOR`, `SET_CONFIGURATION` requests, and all IN transfers), and will only return from `read()` when there's an OUT transfer that it can't handle itself. The returned `Xfer` object represents the USB OUT transfer to be performed, and contains these fields:

- `ep`: the transfer's endpoint
- `setupReq`: if ep==0, the Setup packet
- `data`: the payload data
- `len`: the length of `data`

## Sending Data

To write data to an IN endpoint, call `write()` with the endpoint, data, and length.

## Stopping

To tear down the virtual USB device, call `stop()`.

## Error Handling

Using the `throwOnErr` member of `VirtualUSBDevice::Info`, VirtualUSBDevice can be configured to throw an exception when encountering an error, or alternatively store the error to be queried by the caller. If you choose to disable exceptions, each call to VirtualUSBDevice must be followed by checking `err()` to see whether an error occurred.

## Example

The included example (`main.cpp`) implements a simple ACM/CDC USB serial device that can be accessed at `/dev/ttyACM0` (or `ttyACM1`, etc). The example outputs periodic messages while consuming input sent to the device.

## Thanks

Thanks to the [USB-Emulation](https://github.com/smulikHakipod/USB-Emulation) and [USBIP-Virtual-USB-Device](https://github.com/lcgamboa/USBIP-Virtual-USB-Device) projects for trailblazing and illustrating the techniques used by this project.
