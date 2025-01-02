// Copyright (c) Vector Informatik GmbH. All rights reserved.

#include "ChardevSocketToLinAdapter.hpp"

#include <type_traits>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "adapter/Exceptions.hpp"
#include "adapter/Parsing.hpp"

#include "asio/ts/buffer.hpp"
#include "asio/ts/io_context.hpp"

#include "silkit/config/all.hpp"
#include "silkit/services/logging/all.hpp"
#include "silkit/util/serdes/Serialization.hpp"

using namespace SilKit::Services::Lin;
using namespace std::chrono_literals;
using namespace adapters;

namespace adapters {
namespace lin {

void ChardevSocketToLinAdapter::DoReceiveFrameFromSocket()
{
    _socket.async_receive(
        asio::buffer(_dataBufferFromChardev.data() + _bufferPosFromChardev,
                     _dataBufferFromChardev.size() - _bufferPosFromChardev),
        [this](const std::error_code ec, const std::size_t bytes_received) {
            if (ec)
                throw IncompleteReadError{};
            SyncFrameFromChardev(bytes_received);

            if (_bufferPosFromChardev == _dataBufferFromChardev.size())
            {
                _frameFromChardev =
                    *static_cast<struct CharLinFrame*>(static_cast<void*>(_dataBufferFromChardev.data()));
                if (_frameFromChardev.can_id == 0x3E)
                {
                    _logger->Debug("Send wake up signale to silkit");
                    _linController->Wakeup();
                }
                else
                {
                    LinFrame frame;
                    frame.id = _frameFromChardev.can_id & LIN_ID_MASK;
                    frame.checksumModel = _frameFromChardev.can_id & LIN_CHECKSUM_ENHANCED ? LinChecksumModel::Enhanced
                                                                                           : LinChecksumModel::Classic;
                    frame.dataLength = _frameFromChardev.can_dlc;
                    memcpy(frame.data.data(), _frameFromChardev.data, frame.dataLength);
                    std::stringstream msg;
                    msg << _controllerName << " to Silkit: Sending frame 0x" << std::hex << (int)frame.id
                        << " to SIL KIT.";
                    _logger->Debug(msg.str());
                    LinFrameResponseType responseType = _frameFromChardev.can_id & LIN_RTR_FLAG
                                                            ? LinFrameResponseType::SlaveResponse
                                                            : LinFrameResponseType::MasterResponse;
                    _linController->SendFrame(frame, responseType);
                    CheckIfGotoSleep();
                }
                _bufferPosFromChardev = 0;
            }
            else if (_bufferPosFromChardev > _dataBufferFromChardev.size())
            {
                _logger->Error("buffer exceed max size: current buffer size is: "
                               + std::to_string(_bufferPosFromChardev)
                               + " byte size is: " + std::to_string(bytes_received) + ".");
                _bufferPosFromChardev = 0;
            }
            setsockopt(_socket.native_handle(), IPPROTO_TCP, TCP_QUICKACK, &_quickAck, sizeof(_quickAck));
            DoReceiveFrameFromSocket();
        });
}

void ChardevSocketToLinAdapter::SyncFrameFromChardev(const std::size_t bytes_received)
{
    size_t start = 0;
    size_t length = _bufferPosFromChardev;

    // check sync header
    while ((length < (size_t)16) && ((start + length) < (_bufferPosFromChardev + bytes_received)))
    {
        if (_dataBufferFromChardev[start + length] == kCharLinFrameSyncHead[length])
        {
            length++;
        }
        else
        {
            start += length + 1;
            length = 0;
        }
    }

    // move rx_buf_pos
    _bufferPosFromChardev = _bufferPosFromChardev + bytes_received - start;
    if (start && _bufferPosFromChardev)
    {
        uint8_t tmp[_bufferPosFromChardev];
        memcpy(tmp, _dataBufferFromChardev.data() + start, _bufferPosFromChardev);
        memcpy(_dataBufferFromChardev.data(), tmp, _bufferPosFromChardev);
    }
}

bool ChardevSocketToLinAdapter::CheckIfGotoSleep()
{
    if (_frameFromChardev.can_id != 0x3C || _frameFromChardev.can_dlc != 8)
    {
        return false;
    }
    uint8_t sleep_data[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    for (size_t i = 0; i < _frameFromChardev.can_dlc; ++i)
    {
        if (_frameFromChardev.data[i] != sleep_data[i])
        {
            return false;
        }
    }
    _linController->GoToSleep();
    _logger->Info("Received sleep frame, going to sleep");
    return true;
}

ChardevSocketToLinAdapter::ChardevSocketToLinAdapter(asio::io_context& io_context, const std::string& host,
                                                     const std::string& service, const string& controller_name,
                                                     const string& network_name, SilKit::IParticipant* participant)
    : _socket{io_context}
    , _logger{participant->GetLogger()}
    , _controllerName{controller_name}
    , _networkName{network_name}
{
    try
    {
        asio::connect(_socket, asio::ip::tcp::resolver{io_context}.resolve(host, service));
        _socket.set_option(asio::ip::tcp::no_delay(true));
    }
    catch (std::exception& e)
    {
        std::ostringstream error_message;
        error_message << e.what() << std::endl;
        error_message << "Error encountered while trying to connect to QEMU with \"" << linArg << "\" at \"" << host
                      << ':' << service << '"';
        throw std::runtime_error(error_message.str());
    }
    _logger->Info("Socket connect success");
    _linController = participant->CreateLinController(_controllerName, _networkName);
    _logger->Debug("Created LinController " + _controllerName + " at Network: " + _networkName);
    LinControllerConfig masterConfig;
    // currently only master mode is supported
    masterConfig.controllerMode = LinControllerMode::Master;
    masterConfig.baudRate = 20000;
    _linController->Init(masterConfig);

    // Register FrameStatusHandler to receive confirmation of the successful transmission
    auto master_FrameStatusHandler = [this](ILinController*, const LinFrameStatusEvent& frameStatusEvent) {
        _frameToChardev.can_id = frameStatusEvent.frame.id;
        if (frameStatusEvent.status == LinFrameStatus::LIN_RX_OK)
        {
            std::stringstream info_message;
            info_message << "From Silkit to " << _controllerName << ": Frame id: " << std::hex
                         << (int)frameStatusEvent.frame.id << " RX is OK.";
            _logger->Debug(info_message.str());
            _frameToChardev.can_dlc = frameStatusEvent.frame.dataLength;
            std::copy(frameStatusEvent.frame.data.begin(), frameStatusEvent.frame.data.end(), _frameToChardev.data);
        }
        else if (frameStatusEvent.status == LinFrameStatus::LIN_TX_OK)
        {
            std::stringstream info_message;
            info_message << "From Silkit to " << _controllerName << ": Frame id: " << std::hex
                         << (int)frameStatusEvent.frame.id << " TX is OK.";
            _logger->Debug(info_message.str());
            _frameToChardev.can_id |= LIN_ID_TCF_FLAG;
            _frameToChardev.can_dlc = frameStatusEvent.frame.dataLength;
            std::copy(frameStatusEvent.frame.data.begin(), frameStatusEvent.frame.data.end(), _frameToChardev.data);
        }
        else if (frameStatusEvent.status == LinFrameStatus::LIN_RX_ERROR)
        {
            std::stringstream info_message;
            info_message << "From Silkit to " << _controllerName << ": Frame id: " << std::hex
                         << (int)frameStatusEvent.frame.id << " RX is ERROR.";
            _logger->Warn(info_message.str());
            _frameToChardev.can_id |= LIN_ERR_FLAG;
            _frameToChardev.data[LIN_ERR_RX_INDEX] = LIN_ERR_RX_CKSUM;
        }
        else if (frameStatusEvent.status == LinFrameStatus::LIN_TX_ERROR)
        {
            std::stringstream info_message;
            info_message << "From Silkit to " << _controllerName << ": Frame id: " << std::hex
                         << (int)frameStatusEvent.frame.id << " TX is ERROR.";
            _logger->Warn(info_message.str());
            _frameToChardev.can_id |= LIN_ERR_FLAG;
            _frameToChardev.data[LIN_ERR_TX_INDEX] = LIN_ERR_TX_UNSPEC;
        }
        else if (frameStatusEvent.status == LinFrameStatus::LIN_RX_NO_RESPONSE)
        {
            std::stringstream info_message;
            info_message << "From Silkit to " << _controllerName << ": Frame id: " << std::hex
                         << (int)frameStatusEvent.frame.id << " RX is no response.";
            _logger->Warn(info_message.str());
            _frameToChardev.can_id |= LIN_ERR_FLAG;
            _frameToChardev.data[LIN_ERR_RX_INDEX] = LIN_ERR_RX_NO_RESPONSE;
        }
        asio::write(_socket, asio::buffer(&_frameToChardev, sizeof(_frameToChardev)));
        setsockopt(_socket.native_handle(), IPPROTO_TCP, TCP_QUICKACK, &_quickAck, sizeof(_quickAck));
    };
    _linController->AddFrameStatusHandler(master_FrameStatusHandler);

    auto wakeupHandler = [this](ILinController*, LinWakeupEvent wakeupEvent) {
        _linController->WakeupInternal();
        _logger->Debug("Lin network " + _networkName + " Wakeup event received");
    };

    _linController->AddWakeupHandler(wakeupHandler);

    for (int i = 0; i < kCharLinFrameSyncHeadSize; i++)
    {
        _frameToChardev.sync_head[i] = kCharLinFrameSyncHead[i];
    }
    // start receiving frames from socket
    DoReceiveFrameFromSocket();
}

ChardevSocketToLinAdapter* parseChardevSocketToLinArgument(char* chardevSocketTransmitterArg,
                                                           std::set<std::string>& alreadyProvidedSockets,
                                                           const std::string& participantName,
                                                           asio::io_context& ioContext,
                                                           SilKit::IParticipant* participant,
                                                           SilKit::Services::Logging::ILogger* logger)
{
    ChardevSocketToLinAdapter* newAdapter;
    auto args = Utils::split(chardevSocketTransmitterArg, ",");
    auto arg_iter = args.begin();

    // handle <address>:<port>
    assertAdditionalIterator(arg_iter, args);
    throwInvalidCliIf(alreadyProvidedSockets.insert(*arg_iter).second == false);
    auto portAddress = Utils::split(*arg_iter++, ":");
    throwInvalidCliIf(portAddress.size() != 2);
    const auto& address = portAddress[0];
    const auto& port = portAddress[1];

    // handle controller name
    assertAdditionalIterator(arg_iter, args);
    auto controller = Utils::split(*arg_iter++, ":");
    throwInvalidCliIf(controller.size() != 2 && controller[0] == controllerArg);
    std::string controllerName = controller[1];

    // handle network name
    assertAdditionalIterator(arg_iter, args);
    auto network = Utils::split(*arg_iter++, ":");
    throwInvalidCliIf(network.size() != 2 && network[0] == networkArg);
    std::string networkName = network[1];
    newAdapter = new ChardevSocketToLinAdapter(ioContext, address, port, controllerName, networkName, participant);

    logger->Debug("Created lin transmitter " + address + ':' + port);

    return newAdapter;
}
} // namespace lin
} // namespace adapters
