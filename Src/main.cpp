#include <climits>
#include "VirtualUSBDevice.h"
#include "Descriptor.h"
#include "Toastbox/RuntimeError.h"
#include <string>
#include <vector>
using std::string;
using namespace Toastbox;

static struct {
    std::mutex lock;
    std::condition_variable signal;
    bool dtePresent = false;
} _State;

static void _handleXferEP0(VirtualUSBDevice& dev, VirtualUSBDevice::Xfer&& xfer) {
    const USB::SetupRequest req = xfer.setupReq;
    const uint8_t* payload = xfer.data.get();
    const size_t payloadLen = xfer.len;
    uint8_t buf[64];
    size_t repLen = 0;

   	printf("RequestType 0x%02x\n", req.bmRequestType);
	printf("request     0x%02x\n", req.bRequest);
	printf("index       0x%04x\n", req.wIndex);
	printf("value       0x%04x\n", req.wValue);
	printf("length      0x%04x\n", req.wLength);
 
    // Verify that this request is a `Class` request
    if ((req.bmRequestType&USB::RequestType::TypeMask) != USB::RequestType::TypeVendor) {
        throw RuntimeError((string("invalid request bmRequestType (TypeVendor)") + std::to_string(req.bmRequestType)).c_str());
    }
    
    // Verify that the recipient is the `Interface`
    if ((req.bmRequestType&USB::RequestType::RecipientMask) != USB::RequestType::RecipientDevice)
        throw RuntimeError("invalid request bmRequestType (RecipientDevice)");
    
    switch (req.bmRequestType&USB::RequestType::DirectionMask) {
    case USB::RequestType::DirectionOut:
        switch (req.bRequest) {
		case 0x00: // reset/purge, idx: port; value: 0 - tx and rx, 1 - rx, 2 tx
		break;
		case 0x09: // set lat. time; idx: port (1->A), value time
		break;
		case 0x06: // set event char; idx: port (0->A), value '0x00/no trigger'
		break;
		case 0x07: // set error char; idx: port (0->A), value '0x00/no trigger'
		break;
		case 0x0b: // set bit-mode 0x00: normal, 0x02: mpsse
		break;
        
        default:
            throw RuntimeError("invalid request (DirectionOut): %x", req.bRequest);
        }
	break;
    
    case USB::RequestType::DirectionIn:
        switch (req.bRequest) {
            case 0x05: // get modem stat
	    {
            printf("GET_MODEM_STAT\n");
	    repLen = 2;
	    buf[0] = buf[1] = 0x00;
            break;
	    }
            case 0x0A: // get LatTimer
	    {
            repLen = 1;
	    buf[0] = 0x10;
	    }
	    break;
	    case 0x90: // unknown
            {
       	    repLen = 2;
	    buf[0] = buf[1] = 0x00;
            }
            break;
        
        default:
            throw RuntimeError("invalid request (DirectionIn): %x", req.bRequest);
        }
	break;
    
    default:
        throw RuntimeError("invalid request direction");
    }
    if ( repLen ) {
	    if ( repLen != req.wLength ) {
                throw RuntimeError("payloadLen doesn't match");
	    }
            dev.write(USB::Endpoint::DirectionIn|USB::Endpoint::Default, buf, repLen);
    }
}

static void _handleXferEPX(VirtualUSBDevice& dev, VirtualUSBDevice::Xfer&& xfer) {
    std::vector<uint8_t> rep;
    size_t  cmdsz = 0;
    // apparently, any reply starts with modem + line-status
    rep.push_back( 0x32 );
    rep.push_back( 0x60 );

    switch (xfer.ep) {
    case Endpoint::Out2: {
        printf("Endpoint::Out2: <");
        for (size_t i=0; i<xfer.len; i++) {
            printf(" %02x", xfer.data[i]);
        }
        printf(" >\n\n");
	if ( xfer.len < 1 ) {
            throw RuntimeError("invalid FT command\n");
	}
	while ( cmdsz < xfer.len ) {
	switch ( xfer.data[cmdsz] ) {
		case 0xaa:
		case 0xab: // FT2232c.cpp sends both of these
			{
			// commonly used to sync
			rep.push_back(0xfa); // bad command
			rep.push_back(xfer.data[cmdsz]);
			cmdsz++;
			}
		break;
		case 0x8a: //  use 60MHz master clk
			cmdsz++;
		break;
		case 0x8d: //  disable three-phase clocking
			cmdsz++;
		break;
		case 0x97: //  disable adaptive clocking
			cmdsz++;
		break;
		default:
            		throw RuntimeError("unsupported FT command");
	}
	}
	if ( rep.size() > 0 ) {
		dev.write( Endpoint::In1, &rep[0], rep.size() );
	}
        break;
    }
    
    default:
        throw RuntimeError("invalid endpoint: 0x%02x", xfer.ep);
    }
}

static void _handleXfer(VirtualUSBDevice& dev, VirtualUSBDevice::Xfer&& xfer) {
	printf("Handle XFER; EP %u\n", xfer.ep);
    // Endpoint 0
    if (xfer.ep == 0) _handleXferEP0(dev, std::move(xfer));
    // Other endpoints
    else _handleXferEPX(dev, std::move(xfer));
}

int main(int argc, const char* argv[]) {
    const VirtualUSBDevice::Info deviceInfo = {
        .deviceDesc             = &Descriptor::Device,
        .deviceQualifierDesc    = &Descriptor::DeviceQualifier,
        .configDescs            = Descriptor::Configurations,
        .configDescsCount       = std::size(Descriptor::Configurations),
        .stringDescs            = Descriptor::Strings,
        .stringDescsCount       = std::size(Descriptor::Strings),
        .throwOnErr             = true,
    };
    
    VirtualUSBDevice dev(deviceInfo);
    try {
        try {
            dev.start();
        } catch (std::exception& e) {
            throw RuntimeError(
                "Failed to start VirtualUSBDevice: %s"                                  "\n"
                "Make sure:"                                                            "\n"
                "  - you're root"                                                       "\n"
                "  - the vhci-hcd kernel module is loaded: `sudo modprobe vhci-hcd`"    "\n",
                e.what()
            );
        }
        
        printf("Started\n");
        
//        // Test device teardown (after 5 seconds)
//        std::thread([&] {
//            sleep(5);
//            printf("Stopping device...\n");
//            dev.stop();
//        }).detach();
        
        for (;;) {
            VirtualUSBDevice::Xfer data = *dev.read();
            _handleXfer(dev, std::move(data));
        }
        
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        // Using _exit to avoid calling destructors for static vars, since that hangs
        // in __pthread_cond_destroy, because our thread is sitting in _State.signal.wait()
        _exit(1);
    }
    
    return 0;
}
