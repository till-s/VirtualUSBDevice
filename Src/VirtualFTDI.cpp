#include <VirtualFTDI.hpp>
#ifdef HAVE_ANA
#include <JtagAna.hpp>
#endif

using std::string;
using namespace Toastbox::USB;
using std::vector;

#ifdef HAVE_ANA
namespace {
static void
checkTapState(std::shared_ptr<JtagAna> ana, uint8_t ftcmd)
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
}
#endif

void 
VirtualFTDI::handleXferEP0(VirtualUSBDevice::Xfer&& xfer) {
    const USB::SetupRequest req = xfer.setupReq;
    //const uint8_t* payload = xfer.data.get();
    //const size_t payloadLen = xfer.len;
    uint8_t buf[64];
    size_t repLen = 0;

    dprintf(0, "RequestType 0x%02x\n", req.bmRequestType);
    dprintf(0, "request     0x%02x\n", req.bRequest);
    dprintf(0, "index       0x%04x\n", req.wIndex);
    dprintf(0, "value       0x%04x\n", req.wValue);
    dprintf(0, "length      0x%04x\n", req.wLength);
 
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
		case 0x00: // reset/purge, idx: port; wValue - low byte: 0 - tx and rx, 1 - rx, 2 tx
                           // wIndex low byte: 0 - channel A (?) only used when wValue = 0 => reset device
                           //                  1 -> channel A
                           //                  2 -> channel B
		break;
		case 0x09: // set lat. time; idx: port (1->A, 2->B), value time
		break;
		case 0x06: // set event char; idx: port (0->A), value '0x00/no trigger'
		break;
		case 0x07: // set error char; idx: port (0->A), value '0x00/no trigger'
		break;
		case 0x0b: // set bit-mode value_lo: bitmask, value_hi: 0x00: normal, 0x02: mpsse
		{
			uint8_t i = (req.wIndex & 0xff) - 1;
			if ( i < _channels.size() ) {
				_channels[i].gMode = req.wValue >> 8;
			}
		}
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
            dprintf(2,"GET_MODEM_STAT\n");
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
VirtualFTDI::Channel::mustbeMPSSE() {
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

    for (auto channel = _channels.begin(); channel != _channels.end(); ++channel) {
      if ( xfer.ep == channel->epOut->bEndpointAddress ) {
        dprintf(1, "Endpoint::%02x: <", xfer.ep);
        for (size_t i=0; i<xfer.len; i++) {
            dprintf(1, " %02x", xfer.data[i]);
        }
        dprintf(1, " >\n\n");
	if ( xfer.len < 1 ) {
            throw RuntimeError("invalid FT command\n");
	}
	if ( channel->fragmentedTx.size() > 0 ) {
		channel->mustbeMPSSE();
		size_t missing = channel->fragmentedTxTotal - channel->fragmentedTx.size();
		dprintf(1, "Second part of fragment: tot %zd, mising %zd, len %zd\n", channel->fragmentedTxTotal, missing, xfer.len);
		while ( cmdsz < xfer.len && cmdsz < missing ) {
			channel->fragmentedTx.push_back( xfer.data[cmdsz] );
			cmdsz++;
		}
		if ( cmdsz == missing ) {
			// assembled a transfer
			size_t  pos = rep.size();
			size_t  rsz = 0;
			if ( channel->fragmentedTxHasRep ) {
				rsz = channel->fragmentedTxTotal;
				rep.resize(pos + rsz);
			}
			ssize_t got;
			if ( channel->loopback ) {
				memcpy(&rep[pos], &channel->fragmentedTx[0], rsz);
				got = rsz;
			} else {
				got = channel->ft->mpsse(&channel->fragmentedTx[0], channel->fragmentedTxTotal, 7, FTInterface::ShiftOp::TDI, rsz ? &rep[pos] : nullptr, rsz);
			}
#ifdef HAVE_ANA
			if ( auto ana = channel->ana ) {
				uint8_t bit = 0;
				for ( size_t ii = 0; ii < channel->fragmentedTxTotal; ++ii ) {
					for ( auto jj = 0; jj < 8; ++jj ) {
						bit = !!(channel->fragmentedTx[ii] & (1<<jj));
						ana->nextState(0, bit,  0);
					}
				}
				channel->portVal = (bit<<1); // TMS = 0, TDI
			}
#endif
			if ( channel->fragmentedTxHasRep ) {
				rep.resize(pos + got);
			}
			channel->fragmentedTx.clear();
		}
	}
	while ( cmdsz < xfer.len ) {
	const uint8_t ftcmd = xfer.data[cmdsz];
	switch ( ftcmd )  {
		case 0x00:
			dprintf(0, "OP 0x00 IGNORED (efx)\n");
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
		case 0x84: //  turn-on loopback
			sendStatus = true;
			channel->loopback = true;
			cmdsz++;
		break;
		case 0x85: //  turn-off loopback
			sendStatus = true;
			if ( !! channel->ft ) {
				channel->loopback = false;
			}
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
			if ( !! channel->ft ) {
				channel->ft->setPortLevels(p);
			}
#ifdef HAVE_ANA
			if ( auto ana = channel->ana ) {
				// TMS TDO TDI TCK
				if ( !!(p & 1) ) {
					if ( (p & 0xa) != (channel->portVal & 0xa) ) {
						throw RuntimeError("port glitch: this 0x%02x, last 0x%02x", p, channel->portVal);
					}
					if ( ! (channel->portVal & 1) ) {
						// rising edge
						ana->nextState( !!(p&0x8), !!(p&2), 0 );
					}
				}
			}
#endif
			channel->portVal = p;
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
			channel->mustbeMPSSE();
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
			FTInterface::ShiftOp tms = ( (ftcmd & 0x40) ? !!(xfer.data[cmdsz+2] & 0x80) ? FTInterface::ShiftOp::TMS_HI : FTInterface::ShiftOp::TMS_LO : FTInterface::ShiftOp::TDI );
			if ( channel->loopback ) {
				if ( rsiz ) {
					*rbuf = tbuf ? *tbuf : 0x00;
				}
			} else {
				channel->ft->mpsse(tbuf, tsiz, xfer.data[cmdsz+1], tms, rbuf, rsiz);
			}
#ifdef HAVE_ANA
			if ( auto ana = channel->ana ) {
				uint8_t hiBit = !!(xfer.data[cmdsz+2] & 0x80);
				uint8_t bit;
				for ( auto ii = 0; ii <= xfer.data[cmdsz + 1]; ++ii ) {
					bit = !!(xfer.data[cmdsz + 2] & (1<<ii));
					if ( static_cast<int>(tms) >= 0 ) {
						ana->nextState( bit, hiBit, 0 );
					} else {
						checkTapState(ana, ftcmd);
						ana->nextState( 0, bit, 0 );
					}
				}
				if ( static_cast<int>(tms) >= 0 ) {
					channel->portVal = (bit << 3) | (hiBit << 1); // TMS, TDI
				} else {
					channel->portVal = (bit << 1); // TDI, TMS = 0
				}
			}
#endif
			// no need to resize rep; cannot have head less than 1 byte
			cmdsz += 2 + tsiz;
                }
		break;
		case 0x87: // flush
		{
			channel->mustbeMPSSE();
			// ignore
			cmdsz++;
		}
		break;
		case 0x19: // write-only bytes
		case 0x39: // read-write bytes
		case 0x2c: // read-only; doc says on falling-edge of TCK but that doesn' make sense
                {
			channel->mustbeMPSSE();
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

			dprintf(2, "processing ftcmd 0x%02x, xfer len %zu, cmdsz %zu, xsiz %u\n", ftcmd, xfer.len, cmdsz, xsiz);

			if ( !! (0x20 & ftcmd) ) {
				rep.resize(pos + xsiz);
				rsiz = xsiz;
				rbuf = &rep[pos];
			}

			if ( !! (0x10 & ftcmd) ) {
				tsiz = xsiz;
				if ( cmdsz + 3 + tsiz > xfer.len ) {
					cmdsz += 3;
					dprintf(2, "Buffer fragmented: xfer.len %zd, pos %zd, tsiz %zd", xfer.len, cmdsz, tsiz);
					channel->fragmentedTxTotal  = tsiz;
					channel->fragmentedTxHasRep = !!rsiz;
					while ( cmdsz < xfer.len ) {
					    channel->fragmentedTx.push_back(xfer.data[cmdsz]);
					    ++cmdsz;
					}
					break;
				}
				tbuf = &xfer.data[cmdsz+3];
			}

			ssize_t got;
			if ( channel->loopback ) {
				if ( tbuf ) {
					memcpy(&rep[pos], tbuf, rsiz);
				} else {
					memset(&rep[pos], 0x00, rsiz);
				}
				got = rsiz;
			} else {
				got = channel->ft->mpsse(tbuf, tsiz, 7, FTInterface::ShiftOp::TDI, rbuf, rsiz);
			}
#ifdef HAVE_ANA
			if ( auto ana = channel->ana ) {
				checkTapState(ana, ftcmd);
				if ( !! (0x10 & ftcmd) ) {
					uint8_t bit = 0;
					for ( size_t ii = 0; ii < tsiz; ++ii ) {
						for ( auto jj = 0; jj < 8; ++jj ) {
							bit = !!(tbuf[ii] & (1<<jj));
							ana->nextState(0, bit, 0);
						}
					}
					channel->portVal = (bit<<1); // TMS = 0, TDI
				} else {
					for ( auto ii = 0; ii < 8*xsiz; ++ii ) {
						ana->nextState(0,0,0);
					}
					channel->portVal = 0x00;
				}
			}
#endif
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
	        dprintf(0, "Endpoint::In1: <");
                static constexpr const size_t skip = 2;
		for (size_t i=skip; i<rep.size(); i++) {
	            dprintf(0, " %02x", rep[i]);
	        }
	        dprintf(0, " >\n\n");
		write( channel->epIn->bEndpointAddress, &rep[0] + skip, rep.size() - skip );
	}
    	fflush(stdout);
	return;
      }
    }
    
    throw RuntimeError("invalid endpoint: 0x%02x", xfer.ep);
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

static const EndpointDescriptor *scanForEPDesc(const ConfigurationDescriptor *d, uint8_t epAddr) {
    ssize_t totalLength = d ? d->wTotalLength : 0;
    ssize_t maxOff      = totalLength - sizeof(EndpointDescriptor);
    ssize_t off         = 0;
    EndpointDescriptor *e;
    for ( off = 0; off <= maxOff; off += e->bLength ) {
	    e = reinterpret_cast<EndpointDescriptor*>( reinterpret_cast<uintptr_t>(d) + off );
	    if ( DescriptorType::Endpoint == e->bDescriptorType ) {
		    if ( epAddr == e->bEndpointAddress ) {
			    return e;
		    }
	    }
    }
    return nullptr;
}

void
VirtualFTDI::addChannel(std::shared_ptr<FTInterface> ft, uint8_t epOut, uint8_t epIn, std::shared_ptr<JtagAna> ana)
{
	auto l = getLock();
	if ( _State::Idle != getState() ) {
		throw RuntimeError("VirtualFTDI::addChannel can only be called on an idle device");
	}
	if ( !! (epOut & USB::Endpoint::DirectionIn) ) {
		throw RuntimeError("VirtualFTDI::addChannel OUT-endpoint has wrong direction!?");
	}
	if ( ! (epIn  & USB::Endpoint::DirectionIn) ) {
		throw RuntimeError("VirtualFTDI::addChannel IN-endpoint has wrong direction!?");
	}
	if ( _info.configDescsCount != 1 ) {
		throw RuntimeError("VirtualFTDI::addChannel multiple configurations not supported ATM");
		// would have to wait until a configuration is selected
	}
	Channel ch;
	ch.ft       = ft;
	ch.ana      = ana;
	if ( ! (ch.epIn = scanForEPDesc(_info.configDescs[0], epIn)) ) {
		throw RuntimeError("VirtualFTDI::addChannel EP 0x%02x not found in descriptors\n", epIn);
	}
	if ( ! (ch.epOut = scanForEPDesc(_info.configDescs[0], epOut)) ) {
		throw RuntimeError("VirtualFTDI::addChannel EP 0x%02x not found in descriptors\n", epIn);
	}
	ch.loopback = !ft; 
	_channels.push_back(ch);
}

void
VirtualFTDI::run() {
    for (;;) {
        VirtualUSBDevice::Xfer data = *read();
        handleXfer(std::move(data));
	for ( auto channel = _channels.begin(); channel != _channels.end(); ++channel ) {
		if ( channel->sendModemStatus ) {
			write(channel->epIn->bEndpointAddress, nullptr, 0);
			channel->sendModemStatus = false;
		}
	}
    }
}

size_t
VirtualFTDI::_reply(const _Cmd& cmd, const void *data, size_t len, int32_t status) {
    // The FTDI sends modem status at the beginning of each USB packet; If 'len'
    // spans multiple packets we must insert the modem status because libftd2xx removes
    // it.
    // This is the appropriate place to hack this because at a higher level it is
    // not known how the data will be broken into 'transfers' which correspond
    // to URBs. The transfer size is only known here...
    std::vector<std::pair<const void *, size_t>> sg;
    const uint8_t ep = cmd.header.base.ep | USB::Endpoint::DirectionIn;
    for ( auto channel = _channels.begin(); channel != _channels.end(); ++channel ) {
        if ( channel->epIn->bEndpointAddress == ep ) {
        static constexpr const uint8_t hdr[2] = {0x32, 0x00};
        const size_t pktsz = channel->epIn->wMaxPacketSize;
        const size_t chunksz =pktsz - sizeof(hdr);
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
        }
    }

    sg.push_back({data, len});
    return _reply(cmd, sg);
}
