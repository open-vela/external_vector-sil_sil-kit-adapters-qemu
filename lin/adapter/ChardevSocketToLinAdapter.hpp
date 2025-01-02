// Copyright (c) Vector Informatik GmbH. All rights reserved.

#pragma once

#include <string>
#include <set>

#include "chardev/Utility/StringUtils.hpp"

#include "asio/ts/net.hpp"

#include "silkit/SilKit.hpp"
#include "silkit/services/pubsub/all.hpp"
#include "silkit/util/serdes/Deserializer.hpp"
#include "silkit/util/serdes/Serializer.hpp"

#define LIN_ID_BITS 6
#define LIN_ID_MASK ((1 << LIN_ID_BITS) - 1)
#define LIN_ID_MAX LIN_ID_MASK
#define LIN_CHECKSUM_ENHANCED (1 << (LIN_ID_BITS + 1))
#define LIN_ID_TCF_FLAG (1 << (LIN_ID_BITS + 3))
#define LIN_ID_NOT_RX_OK_MASK 0xffffffc0U

#define LIN_CTRL_FLAG 0x80000000 /* Describe control information(such as wirte et.) */
#define LIN_RTR_FLAG 0x40000000 /* Describe the direction of sending and receiving */
#define LIN_ERR_FLAG 0x20000000 /* The flag indicate  this is LIN err_frame */
#define LIN_EVT_FLAG 0x10000000 /* Lower_half use this flags to report state switch event */

/* ERR_CLASS in  data[0] */
#define LIN_ERR_UNSPEC 0x00
#define LIN_ERR_TX (1 << 0)
#define LIN_ERR_RX (1 << 1)
#define LIN_ERR_BUS (1 << 2)
#define LIN_ERR_CRTL (1 << 3)

#define LIN_ERR_TX_INDEX 1
#define LIN_ERR_RX_INDEX 2
#define LIN_ERR_BUS_INDEX 3
#define LIN_ERR_CRTL_INDEX 4

/* Data[1] tx error */
#define LIN_ERR_TX_UNSPEC 0x00 /* Unspecified error */
#define LIN_ERR_TX_BREAK_TMO (1 << 0) /* Bit 0: Master send break field, but detect break event timeout */
#define LIN_ERR_TX_SYNC_TMO (1 << 1) /* Bit 1: Master send sync timeout (receive back timeout) */
#define LIN_ERR_TX_PID_TMO (1 << 2) /* Bit 2: Master send pid timeout (receive back timeout) */
#define LIN_ERR_TX_DATA_TMO (1 << 3) /* Bit 3: Master/slave send data timeout (receive back timeout) */
#define LIN_ERR_TX_CHECKSUM_TMO (1 << 4) /* Bit 4: Master/slave send checksum timeout(receive back timeout) */

/* Data[2] rx error */

#define LIN_ERR_RX_UNSPEC 0x00 /* Unspecified error */
#define LIN_ERR_RX_NO_RESPONSE (1 << 0) /* Bit 0: Prepared to receive response, but no response received */
#define LIN_ERR_RX_RESPONSE (1 << 1) /* Bit 1: Receive incomplete response */
#define LIN_ERR_RX_CKSUM_TMO (1 << 2) /* Bit 2: Response data received, receive checksum timeout */
#define LIN_ERR_RX_CKSUM (1 << 3) /* Bit 3: Received error checksum */
#define LIN_ERR_RX_SYNC_TMO (1 << 4) /* Bit 4: Slave receive sync timeout */
#define LIN_ERR_RX_SYNC (1 << 5) /* Bit 5: Received error sync byte */
#define LIN_ERR_RX_PID_TMO (1 << 6) /* Bit 6: Receive pid timeout after a sync field */
#define LIN_ERR_RX_PID_PARITY (1 << 7) /* Bit 7: Pid parity error */

/* Data[3] bus reasons make frame error */

#define LIN_ERR_BUS_UNSPEC 0x00 /* Unspecified error */
#define LIN_ERR_BUS_PID (1 << 0) /* Bit 0: Pid received back is not equal to the pid sent */
#define LIN_ERR_BUS_TXCKSUM (1 << 1) /* Bit 1: Checksum received back is not equal to checksum sent */
#define LIN_ERR_BUS_SYNC (1 << 2) /* Bit 2: Master send sync, but receive back sync is not 0x55 */
#define LIN_ERR_BUS_DATA (1 << 3) /* Bit 3: Data received back is not equal to the data sent */

/* Data[4] error status of LIN-controller */

#define LIN_ERR_CRTL_UNSPEC 0x00 /* Unspecified error */
#define LIN_ERR_CRTL_RXOVERFLOW (1 << 0) /* Hardware controller receive overflow */
#define LIN_ERR_CTRL_FRAMEERROR (1 << 1) /* Hardware controller frame error */
#define LIN_ERR_CTRL_NOISE (1 << 2) /* Hardware controller noise error */

namespace asio {
class io_context;
}

namespace adapters {
namespace lin {

const std::string controllerArg = "ControllerName";

const std::string networkArg = "Network";

const int kCharLinFrameSyncHeadSize = 16;

const uint8_t kCharLinFrameSyncHead[kCharLinFrameSyncHeadSize] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                                                  0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

struct CharLinFrame
{
    uint8_t sync_head[kCharLinFrameSyncHeadSize];
    uint32_t can_id; /* 32 bit CAN_ID + EFF/RTR/ERR flags */
    uint8_t can_dlc; /* frame payload length in byte (0 .. CAN_MAX_DLEN) */
    uint8_t __pad; /* padding */
    uint8_t __res0; /* reserved / padding */
    uint8_t __res1; /* reserved / padding */
    uint8_t data[8];
    uint32_t checksum;
};

class ChardevSocketToLinAdapter
{
    typedef std::string string;
    typedef SilKit::Services::PubSub::PubSubSpec PubSubSpec;

public:
    ChardevSocketToLinAdapter(asio::io_context& io_context, const string& host, const string& service,
                              const string& controller_name, const string& network_name,
                              SilKit::IParticipant* participant);

private:
    //internal callback
    void DoReceiveFrameFromSocket();

    void SyncFrameFromChardev(const std::size_t bytes_received);

    bool CheckIfGotoSleep();

private:
    asio::ip::tcp::socket _socket;
    SilKit::Services::Logging::ILogger* _logger;
    std::array<uint8_t, sizeof(struct CharLinFrame)> _dataBufferFromChardev = {};
    size_t _bufferPosFromChardev = 0;
    SilKit::Services::Lin::ILinController* _linController;
    std::string _controllerName;
    std::string _networkName;
    struct CharLinFrame _frameFromChardev;
    struct CharLinFrame _frameToChardev;
    const int _quickAck = 1;
};

ChardevSocketToLinAdapter* parseChardevSocketToLinArgument(char* chardevSocketTransmitterArg,
                                                           std::set<std::string>& alreadyProvidedSockets,
                                                           const std::string& participantName,
                                                           asio::io_context& ioContext,
                                                           SilKit::IParticipant* participant,
                                                           SilKit::Services::Logging::ILogger* logger);

} // namespace lin
} // namespace adapters
