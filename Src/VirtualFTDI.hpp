#pragma once

#include <VirtualUSBDevice.h>
#include <Toastbox/USB.h>
#include <string>
#include <vector>
#include <memory>

using std::string;
using namespace Toastbox;
using std::vector;

class FTInterface {
public:
	enum class ShiftOp { TDI = -1, TMS_LO = 0, TMS_HI = 1};
	virtual ssize_t mpsse(const uint8_t *tbuf, size_t tsize, int bits, ShiftOp op = ShiftOp::TDI, uint8_t *rbuf = nullptr, size_t rsize = 0) = 0;
	virtual void setPortLevels(uint8_t) = 0;
	virtual ~FTInterface() = default;
};

class JtagAna;

class VirtualFTDI : public VirtualUSBDevice {

	struct Channel {
		static constexpr const uint8_t           MODE_MPSSE = 0x02;
		bool                                     sendModemStatus {false};
		vector<uint8_t>                          fragmentedTx;
		size_t                                   fragmentedTxTotal {0};
		bool                                     fragmentedTxHasRep { false };
		bool                                     loopback { false };
		uint8_t                                  gMode {0x00};
		std::shared_ptr<FTInterface>             ft;
		std::shared_ptr<JtagAna>                 ana;
		uint8_t                                  portVal = 0x00;
		const Toastbox::USB::EndpointDescriptor *epIn;
		const Toastbox::USB::EndpointDescriptor *epOut;
		void mustbeMPSSE();
	};

	std::vector<Channel> _channels;
	
public:
	VirtualFTDI(const VirtualUSBDevice::Info &info)
		: VirtualUSBDevice(info)
	{
	}

	virtual void addChannel(std::shared_ptr<FTInterface> ft, uint8_t epOut, uint8_t epIn, std::shared_ptr<JtagAna> ana = std::shared_ptr<JtagAna>());

	using VirtualUSBDevice::_reply;

	virtual void run();
        virtual void handleXfer(VirtualUSBDevice::Xfer&& xfer);
	virtual void handleXferEP0(VirtualUSBDevice::Xfer&& xfer);
	virtual void handleXferEPX(VirtualUSBDevice::Xfer&& xfer);
        virtual size_t _reply(const _Cmd& cmd, const void *data, size_t len, int32_t status = 0) override;
};
