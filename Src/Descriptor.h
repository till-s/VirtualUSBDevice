#pragma once
#include "Toastbox/Endian.h"
#include "Toastbox/USB.h"


namespace Endpoint {
    constexpr uint8_t In1    = 0x81;
    constexpr uint8_t Out2   = 0x02;
    constexpr uint8_t In3    = 0x83;
    constexpr uint8_t Out4   = 0x04;
} // namespace Endpoint

namespace Descriptor {

using namespace Toastbox;
using namespace Toastbox::Endian;

constexpr USB::DeviceDescriptor Device = {
    .bLength                = LFH_U8(sizeof(Device)),
    .bDescriptorType        = LFH_U8(0x01),
    .bcdUSB                 = LFH_U16(0x0200),
    .bDeviceClass           = LFH_U8(0x00),
    .bDeviceSubClass        = LFH_U8(0x00),
    .bDeviceProtocol        = LFH_U8(0x00),
    .bMaxPacketSize0        = LFH_U8(0x10),
    .idVendor               = LFH_U16(0x0403),
    .idProduct              = LFH_U16(0x6010), // 6010: 2232H, 6014: 232H
    .bcdDevice              = LFH_U16(0x0700), // 0600: 2232H, 0900: 232H -> lattice programmer needs 0700
    .iManufacturer          = LFH_U8(0x01), // String1
    .iProduct               = LFH_U8(0x02), // String2
    .iSerialNumber          = LFH_U8(0x04), // String4
    .bNumConfigurations     = LFH_U8(0x01),
};

constexpr USB::DeviceQualifierDescriptor DeviceQualifier = {
    .bLength                = LFH_U8(sizeof(DeviceQualifier)),
    .bType                  = LFH_U8(0x06),
    .bcdUSB                 = LFH_U16(0x0200),
    .bDeviceClass           = LFH_U8(0x00),
    .bDeviceSubClass        = LFH_U8(0x00),
    .bDeviceProtocol        = LFH_U8(0x00),
    .bMaxPacketSize0        = LFH_U8(0x10),
    .bNumConfigurations     = LFH_U8(0x01),
    .bReserved              = LFH_U8(0x00),
};

struct Configuration {
    USB::ConfigurationDescriptor configDesc;
    USB::InterfaceDescriptor iface0Desc;
        USB::EndpointDescriptor epIn1Desc;
        USB::EndpointDescriptor epOut2Desc;
    USB::InterfaceDescriptor iface1Desc;
        USB::EndpointDescriptor epIn3Desc;
        USB::EndpointDescriptor epOut4Desc;
} __attribute__((packed));

constexpr Configuration Configuration = {
    .configDesc = {
        .bLength                        = LFH_U8(sizeof(USB::ConfigurationDescriptor)),
        .bDescriptorType                = LFH_U8(USB::DescriptorType::Configuration),
        .wTotalLength                   = LFH_U16(sizeof(Configuration)),
        .bNumInterfaces                 = LFH_U8(0x02),
        .bConfigurationValue            = LFH_U8(0x01),
        .iConfiguration                 = LFH_U8(0x00),
        .bmAttributes                   = LFH_U8(0x80),
        .bMaxPower                      = LFH_U8(0x32),
    },
    
        .iface0Desc = {
            .bLength                    = LFH_U8(sizeof(USB::InterfaceDescriptor)),
            .bDescriptorType            = LFH_U8(USB::DescriptorType::Interface),
            .bInterfaceNumber           = LFH_U8(0x00),
            .bAlternateSetting          = LFH_U8(0x00),
            .bNumEndpoints              = LFH_U8(0x02),
            .bInterfaceClass            = LFH_U8(0xff), // Communication Interface Class
            .bInterfaceSubClass         = LFH_U8(0xff), // Abstract Control Model
            .bInterfaceProtocol         = LFH_U8(0xff), // Common AT commands
            .iInterface                 = LFH_U8(0x02),
        },
            .epIn1Desc = {
                .bLength                = LFH_U8(sizeof(USB::EndpointDescriptor)),
                .bDescriptorType        = LFH_U8(USB::DescriptorType::Endpoint),
                .bEndpointAddress       = LFH_U8(Endpoint::In1),
                .bmAttributes           = LFH_U8(0x02),
                .wMaxPacketSize         = LFH_U16(0x0200),
                .bInterval              = LFH_U8(0x00),
            },
        
            .epOut2Desc = {
                .bLength                = LFH_U8(sizeof(USB::EndpointDescriptor)),
                .bDescriptorType        = LFH_U8(USB::DescriptorType::Endpoint),
                .bEndpointAddress       = LFH_U8(Endpoint::Out2),
                .bmAttributes           = LFH_U8(0x02),
                .wMaxPacketSize         = LFH_U16(0x0200),
                .bInterval              = LFH_U8(0x00),
            },
        .iface1Desc = {
            .bLength                    = LFH_U8(sizeof(USB::InterfaceDescriptor)),
            .bDescriptorType            = LFH_U8(USB::DescriptorType::Interface),
            .bInterfaceNumber           = LFH_U8(0x01),
            .bAlternateSetting          = LFH_U8(0x00),
            .bNumEndpoints              = LFH_U8(0x02),
            .bInterfaceClass            = LFH_U8(0xff), // Communication Interface Class
            .bInterfaceSubClass         = LFH_U8(0xff), // Abstract Control Model
            .bInterfaceProtocol         = LFH_U8(0xff), // Common AT commands
            .iInterface                 = LFH_U8(0x02),
        },
            .epIn3Desc = {
                .bLength                = LFH_U8(sizeof(USB::EndpointDescriptor)),
                .bDescriptorType        = LFH_U8(USB::DescriptorType::Endpoint),
                .bEndpointAddress       = LFH_U8(Endpoint::In3),
                .bmAttributes           = LFH_U8(0x02),
                .wMaxPacketSize         = LFH_U16(0x0200),
                .bInterval              = LFH_U8(0x00),
            },
        
            .epOut4Desc = {
                .bLength                = LFH_U8(sizeof(USB::EndpointDescriptor)),
                .bDescriptorType        = LFH_U8(USB::DescriptorType::Endpoint),
                .bEndpointAddress       = LFH_U8(Endpoint::Out4),
                .bmAttributes           = LFH_U8(0x02),
                .wMaxPacketSize         = LFH_U16(0x0200),
                .bInterval              = LFH_U8(0x00),
            },
};

const USB::ConfigurationDescriptor* Configurations[] = {
    (USB::ConfigurationDescriptor*)&Configuration,
};

constexpr auto String0 = USB::SupportedLanguagesDescriptorMake({0x0409});
constexpr auto String1 = USB::StringDescriptorMake("FTDI");
constexpr auto String2 = USB::StringDescriptorMake("USB <-> Serial Converter");
constexpr auto String3 = USB::StringDescriptorMake("InterfaceString");
constexpr auto String4 = USB::StringDescriptorMake("TS9D9HD5");

const USB::StringDescriptor* Strings[] = {
    (USB::StringDescriptor*)&String0,
    (USB::StringDescriptor*)&String1,
    (USB::StringDescriptor*)&String2,
    (USB::StringDescriptor*)&String3,
    (USB::StringDescriptor*)&String4,
};

} // namespace Descriptor
