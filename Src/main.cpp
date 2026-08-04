#include <climits>
#include "VirtualUSBDevice.h"
#include "Descriptor.h"
#include "Toastbox/RuntimeError.h"
#include <string>
#include <vector>
#include <memory>
#include <FTEmul.hpp>
#include <JtagAna.hpp>
#include <getopt.h>
#include <time.h>

#if 0
#ifdef printf
#undef printf
#endif
#define printf(a...) do {} while (0)
#endif

using std::string;
using namespace Toastbox;
using namespace ftemul;
using std::vector;

static struct {
    std::mutex lock;
    std::condition_variable signal;
    bool dtePresent = false;
} _State;

static constexpr const uint8_t MODE_MPSSE = 0x02;

class VirtualFTDI : public VirtualUSBDevice {
	vector<uint8_t>                sendData;
	vector<uint8_t>                fragmentedTx;
	size_t                         fragmentedTxTotal {0};
	bool                           fragmentedTxHasRep { false };
        uint8_t                        gMode {0x00};
	std::unique_ptr<FW>            fw;
	std::unique_ptr<JtagAna>       ana;
	uint8_t                        portVal = 0x00;
	
public:
	VirtualFTDI(const VirtualUSBDevice::Info &info, const char *emulDev)
		: VirtualUSBDevice(info)
	{
		fw = std::unique_ptr<FW>(new FW(emulDev));
	}

	using VirtualUSBDevice::_reply;

	virtual void run();
        virtual void handleXfer(VirtualUSBDevice::Xfer&& xfer);
	virtual void handleXferEP0(VirtualUSBDevice::Xfer&& xfer);
	virtual void handleXferEPX(VirtualUSBDevice::Xfer&& xfer);
	virtual void mustbeMPSSE();
	virtual void checkTapState(uint8_t cmd);
        virtual size_t _reply(const _Cmd& cmd, const void *data, size_t len, int32_t status = 0) override;
};

void
VirtualFTDI::checkTapState(uint8_t ftcmd)
{
	JtagTap::State state = ana->getState();
	switch ( state ) {
		case JtagTap::State::ShiftDR:
		case JtagTap::State::ShiftIR:
		case JtagTap::State::PauseIR:
		case JtagTap::State::PauseDR:
		case JtagTap::State::RunTestIdle:
		break;
		default:
			fflush(stdout);
			throw RuntimeError("Unexpected TAP state %s, ftcmd 0x%02x", ana->toString(state).c_str(), ftcmd);
	}
}


void 
VirtualFTDI::handleXferEP0(VirtualUSBDevice::Xfer&& xfer) {
    const USB::SetupRequest req = xfer.setupReq;
    //const uint8_t* payload = xfer.data.get();
    //const size_t payloadLen = xfer.len;
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
			   if ( req.wValue == 0 ) {
			   sendData.push_back(0x02);
			   sendData.push_back(0x60);
			   }
		break;
		case 0x09: // set lat. time; idx: port (1->A), value time
		break;
		case 0x06: // set event char; idx: port (0->A), value '0x00/no trigger'
		break;
		case 0x07: // set error char; idx: port (0->A), value '0x00/no trigger'
		break;
		case 0x0b: // set bit-mode value_lo: bitmask, value_hi: 0x00: normal, 0x02: mpsse
			gMode = req.wValue >> 8;
		break;
	    	case 0x02: // set flow control
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
	    buf[0] = 0x02; //  0x32 FT232H
	    buf[1] = 0x60;
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
            write(USB::Endpoint::DirectionIn|USB::Endpoint::Default, buf, repLen);
    }
    fflush(stdout);
}

void
VirtualFTDI::mustbeMPSSE() {
	if ( gMode != MODE_MPSSE ) {
		throw RuntimeError("Device in mode 0x%02x; not supported -- SKIPPING\n", gMode);
	}
}

void 
VirtualFTDI::handleXferEPX(VirtualUSBDevice::Xfer&& xfer) {
    std::vector<uint8_t> rep;
    size_t  cmdsz = 0;
    // apparently, any reply starts with modem + line-status
    rep.push_back( 0x32 );
    rep.push_back( 0x60 );
    bool sendStatus = false;

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
	if ( fragmentedTx.size() > 0 ) {
		mustbeMPSSE();
		size_t missing = fragmentedTxTotal - fragmentedTx.size();
		printf("Second part of fragment: tot %zd, mising %zd, len %zd\n", fragmentedTxTotal, missing, xfer.len);
		while ( cmdsz < xfer.len && cmdsz < missing ) {
			fragmentedTx.push_back( xfer.data[cmdsz] );
			cmdsz++;
		}
		if ( cmdsz == missing ) {
			// assembled a transfer
			size_t  pos = rep.size();
			size_t  rsz = 0;
			if ( fragmentedTxHasRep ) {
				rsz = fragmentedTxTotal;
				rep.resize(pos + rsz);
			}
			ssize_t got = fw->ft(&fragmentedTx[0], fragmentedTxTotal, rsz ? &rep[pos] : nullptr, rsz);
			if ( ana ) {
				uint8_t bit = 0;
				for ( size_t ii = 0; ii < fragmentedTxTotal; ++ii ) {
					for ( auto jj = 0; jj < 8; ++jj ) {
						bit = !!(fragmentedTx[ii] & (1<<jj));
						ana->nextState(0, bit,  0);
					}
				}
				portVal = (bit<<1); // TMS = 0, TDI
			}
			if ( fragmentedTxHasRep ) {
				rep.resize(pos + got);
			}
			fragmentedTx.clear();
		}
	}
	while ( cmdsz < xfer.len ) {
	const uint8_t ftcmd = xfer.data[cmdsz];
	switch ( ftcmd )  {
		case 0x00:
			printf("OP 0x00 IGNORED (efx)\n");
			cmdsz++;
		break;
		case 0xaa:
		case 0xab: // FT2232c.cpp sends both of these
			{
			sendStatus = true;
			// commonly used to sync
			rep.push_back(0xfa); // bad command
			rep.push_back(xfer.data[cmdsz]);
			cmdsz++;
			}
		break;
		case 0x8a: //  use 60MHz master clk (turn off by-5-dividor)
			sendStatus = true;
			cmdsz++;
		break;
		case 0x8b: //  use 60MHz master clk (turn on  by-5-dividor)
			sendStatus = true;
			cmdsz++;
		break;
		case 0x85: //  turn-off loopback
			sendStatus = true;
			cmdsz++;
		break;
		case 0x86: // set clock divider
			sendStatus = true;
			cmdsz+=3;
		break;
		case 0x8d: //  disable three-phase clocking
			sendStatus = true;
			cmdsz++;
		break;
		case 0x97: //  disable adaptive clocking
			sendStatus = true;
			cmdsz++;
		break;
		case 0x80: //  set port data bits low byte (config IO)
			// status not expected
			{
			uint8_t p = xfer.data[cmdsz+1];
			fw->setPortLevels(p);
			if ( ana ) {
				// TMS TDO TDI TCK
				if ( !!(p & 1) ) {
					if ( (p & 0xa) != (portVal & 0xa) ) {
						throw RuntimeError("port glitch: this 0x%02x, last 0x%02x", p, portVal);
					}
					if ( ! (portVal & 1) ) {
						// rising edge
						ana->nextState( !!(p&0x8), !!(p&2), 0 );
					}
				}
				portVal = p;
			}
			cmdsz+=3;
			}
		break;
		case 0x82: //  set port data bits high byte (config IO)
			// status not expected
			cmdsz+=3;
		break;
		case 0x2e: // read bits
		case 0x1b: // write bits
		case 0x4b: // write TMS
		case 0x3b: // write-read bits
		case 0x6b: // write TMS read bits
                {
			mustbeMPSSE();
			// status not expected
			size_t  pos  = rep.size();
			size_t  rsiz = 0;
			size_t  tsiz = 0;
			uint8_t *tbuf = nullptr;
			uint8_t *rbuf = nullptr;
			if ( !! (ftcmd & 0x20) ) {
				rep.resize(pos + 1);
				rbuf = &rep[pos];
				rsiz = 1;
			}
			if ( !!(0x50 & ftcmd) ) {
				tbuf = &xfer.data[cmdsz + 2];
				tsiz = 1;
			}
			int tms = ( (ftcmd & 0x40) ? !!(xfer.data[cmdsz+2] & 0x80) : -1 );
			fw->ft(tbuf, tsiz, xfer.data[cmdsz+1], tms, rbuf, rsiz);
			if ( ana ) {
				uint8_t hiBit = !!(xfer.data[cmdsz+2] & 0x80);
				uint8_t bit;
				for ( auto ii = 0; ii <= xfer.data[cmdsz + 1]; ++ii ) {
					bit = !!(xfer.data[cmdsz + 2] & (1<<ii));
					if ( tms >= 0 ) {
						ana->nextState( bit, hiBit, 0 );
					} else {
						checkTapState(ftcmd);
						ana->nextState( 0, bit, 0 );
					}
				}
				if ( tms >= 0 ) {
					portVal = (bit << 3) | (hiBit << 1); // TMS, TDI
				} else {
					portVal = (bit << 1); // TDI, TMS = 0
				}
			}
			// no need to resize rep; cannot have head less than 1 byte
			cmdsz += 2 + tsiz;
                }
		break;
		case 0x87: // flush
		{
			mustbeMPSSE();
			// ignore
			cmdsz++;
		}
		break;
		case 0x19: // write-only bytes
		case 0x39: // read-write bytes
		case 0x2c: // read-only; doc says on falling-edge of TCK but that doesn' make sense
                {
			mustbeMPSSE();
			if ( !! (0x20 & ftcmd) ) {
				sendStatus = true;
			}
			// FT gives zero-based count
			uint16_t xsiz = ((xfer.data[cmdsz + 2] << 8) | xfer.data[cmdsz + 1]) + 1;
			size_t pos = rep.size();
			size_t rsiz = 0;
			size_t tsiz = 0;
			uint8_t *rbuf = nullptr;
			uint8_t *tbuf = nullptr;

			printf("processing ftcmd 0x%02x, xfer len %zu, cmdsz %zu, xsiz %u\n", ftcmd, xfer.len, cmdsz, xsiz);

			if ( !! (0x20 & ftcmd) ) {
				rep.resize(pos + xsiz);
				rsiz = xsiz;
				rbuf = &rep[pos];
			}

			if ( !! (0x10 & ftcmd) ) {
				tsiz = xsiz;
				if ( cmdsz + 3 + tsiz > xfer.len ) {
					cmdsz += 3;
					printf("Buffer fragmented: xfer.len %zd, pos %zd, tsiz %zd", xfer.len, cmdsz, tsiz);
					fragmentedTxTotal  = tsiz;
					fragmentedTxHasRep = !!rsiz;
					while ( cmdsz < xfer.len ) {
					    fragmentedTx.push_back(xfer.data[cmdsz]);
					    ++cmdsz;
					}
					break;
				}
				tbuf = &xfer.data[cmdsz+3];
			}

			ssize_t got = fw->ft(tbuf, tsiz, rbuf, rsiz);
			if ( ana ) {
				checkTapState(ftcmd);
				if ( !! (0x10 & ftcmd) ) {
					uint8_t bit = 0;
					for ( size_t ii = 0; ii < tsiz; ++ii ) {
						for ( auto jj = 0; jj < 8; ++jj ) {
							bit = !!(tbuf[ii] & (1<<jj));
							ana->nextState(0, bit, 0);
						}
					}
					portVal = (bit<<1); // TMS = 0, TDI
				} else {
					printf("Read-shift %d\n", 8*xsiz);
					for ( auto ii = 0; ii < 8*xsiz; ++ii ) {
						ana->nextState(0,0,0);
					}
					portVal = 0x00;
				}
			}
			if ( rsiz > 0 ) {
				rep.resize(pos + got);
			}
			cmdsz += 3 + tsiz;
                }
		break;
		default:
            		throw RuntimeError((string("unsupported FT command ") + std::to_string(ftcmd)).c_str());
	}
	if ( cmdsz > xfer.len ) {
            		throw RuntimeError("Cmd buffer overflow");
	}
	}
	if ( rep.size() > 2 || sendStatus ) {
	        printf("Endpoint::In1: <");
                static constexpr const size_t skip = 2;
		for (size_t i=skip; i<rep.size(); i++) {
	            printf(" %02x", rep[i]);
	        }
	        printf(" >\n\n");
		write( Endpoint::In1, &rep[0] + skip, rep.size() - skip );
	}
        break;
    }
    
    default:
        throw RuntimeError("invalid endpoint: 0x%02x", xfer.ep);
    }
    fflush(stdout);
}

void
VirtualFTDI::handleXfer(VirtualUSBDevice::Xfer&& xfer) {
    // Endpoint 0
    if (xfer.ep == 0) handleXferEP0(std::move(xfer));
    // Other endpoints
    else {
        handleXferEPX(std::move(xfer));
    }
}

void
VirtualFTDI::run() {
    for (;;) {
        VirtualUSBDevice::Xfer data = *read();
        handleXfer(std::move(data));
	if ( sendData.size() > 0 ) {
		write(Endpoint::In1, &sendData[0], sendData.size());
		sendData.clear();
	}
    }
}

size_t
VirtualFTDI::_reply(const _Cmd& cmd, const void *data, size_t len, int32_t status) {
    std::vector<std::pair<const void *, size_t>> sg;
    if ( 1 && (Endpoint::In1 & USB::Endpoint::IndexMask) == cmd.header.base.ep ) {
        static constexpr const uint8_t hdr[2] = {0x32, 0x00};
        static constexpr const size_t pktsz = 512;
        static constexpr const size_t chunksz = 512 - sizeof(hdr);
        const auto q = std::ldiv((size_t)cmd.header.cmd_submit.transfer_buffer_length, pktsz);
        // max. payload length
        const size_t maxlen = q.quot * chunksz + ((size_t)q.rem > sizeof(hdr) ? q.rem - sizeof(hdr) : 0);
        const size_t consumed_len = std::min(maxlen, len);
        if ( 0 == consumed_len ) {
            sg.push_back({hdr, sizeof(hdr)});
        } else {
            const uint8_t *p = static_cast<const uint8_t*>(data);
            len = consumed_len;
            while ( len > 0 ) {
                size_t sz = len > chunksz ? chunksz : len;
                sg.push_back({hdr, sizeof(hdr)});
                sg.push_back({p, sz});
                p   += sz;
                len -= sz;
            }
        }
        _reply(cmd, sg);
        // caller doesn't 'know' about the added modem status bytes;
        // tell them how much payload data we consumed
        return consumed_len;
    } else {
        sg.push_back({data, len});
    	return _reply(cmd, sg);
    }
}

int main(int argc, char * const argv[]) {
    const char *emulDev = "/dev/ttyACM0";
//    const char *serialNo1 = "TS9D9HD5B";
//    const char *serialNo2 = "FT6WW2FJA";

    int opt;

    while ( (opt = getopt(argc, argv, "d:")) > 0 ) {
	    switch ( opt ) {
		case 'd': emulDev = optarg; break;
		default:
			  throw RuntimeError("Unsupported option -%c", opt);
	    }
    }

    const VirtualUSBDevice::Info deviceInfo = {
        .deviceDesc             = &Descriptor::Device,
        .deviceQualifierDesc    = &Descriptor::DeviceQualifier,
        .configDescs            = Descriptor::Configurations,
        .configDescsCount       = std::size(Descriptor::Configurations),
        .stringDescs            = Descriptor::Strings,
        .stringDescsCount       = std::size(Descriptor::Strings),
        .throwOnErr             = true,
    };
    
    VirtualFTDI dev(deviceInfo, emulDev);
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
        
	dev.run();
        
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        // Using _exit to avoid calling destructors for static vars, since that hangs
        // in __pthread_cond_destroy, because our thread is sitting in _State.signal.wait()
        _exit(1);
    }
    
    return 0;
}
