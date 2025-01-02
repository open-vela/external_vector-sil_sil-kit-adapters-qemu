// Copyright (c) Vector Informatik GmbH. All rights reserved.

#include <iostream>
#include <string>
#include <cstring>
#include <vector>

#include "silkit/SilKit.hpp"
#include "silkit/config/all.hpp"
#include "silkit/services/can/all.hpp"
#include "silkit/services/can/string_utils.hpp"
#include "silkit/util/Span.hpp"
#include "adapter/Parsing.hpp"
#include "adapter/SignalHandler.hpp"

using namespace adapters;
using namespace std::chrono_literals;
using namespace SilKit::Services::Orchestration;
using namespace SilKit::Services::Lin;

const std::string linControlName = "--lin-name";

const std::string network = "--network";

void promptForExit()
{
    std::promise<int> signalPromise;
    auto signalValue = signalPromise.get_future();
    RegisterSignalHandler([&signalPromise](auto sigNum) {
        signalPromise.set_value(sigNum);
    });

    std::cout << "Press CTRL + C to stop the process..." << std::endl;

    signalValue.wait();

    std::cout << "\nSignal " << signalValue.get() << " received!" << std::endl;
    std::cout << "Exiting..." << std::endl;
}

/**************************************************************************************************
 * Main Function
 **************************************************************************************************/

int main(int argc, char** argv)
{
    const std::string loglevel = getArgDefault(argc, argv, logLevelArg, "Info");
    const std::string participantName = getArgDefault(argc, argv, participantNameArg, "EchoDevice");
    const std::string registryURI = getArgDefault(argc, argv, regUriArg, "silkit://localhost:8501");
    const std::string controlName = getArgDefault(argc, argv, linControlName, "LinSlave1");
    const std::string networkName = getArgDefault(argc, argv, network, "Lin1");

    const std::string participantConfigurationString =
        R"({ "Logging": { "Sinks": [ { "Type": "Stdout", "Level": ")" + loglevel + R"("} ] } })";

    try
    {
        auto participantConfiguration =
            SilKit::Config::ParticipantConfigurationFromString(participantConfigurationString);

        std::cout << "Creating participant '" << participantName << "' at " << registryURI << std::endl;
        auto participant = SilKit::CreateParticipant(participantConfiguration, participantName, registryURI);

        auto logger = participant->GetLogger();

        std::ostringstream SILKitInfoMessage;
        SILKitInfoMessage << "Creating LIN controller '" << controlName << "'";
        logger->Info(SILKitInfoMessage.str());
        auto* linController = participant->CreateLinController(controlName, networkName);

        LinControllerConfig slaveConfig;
        slaveConfig.controllerMode = LinControllerMode::Slave;
        slaveConfig.baudRate = 20000;
        for (int i = 0; i < 62; i++)
        {
            LinFrame slaveFrame;
            slaveFrame.id = i;
            slaveFrame.dataLength = 8;
            slaveFrame.checksumModel = LinChecksumModel::Enhanced;
            if (i % 2)
            {
                slaveFrame.data = {'S', 'L', 'A', 'V', 'E', 'T', 'X', 0};
                slaveConfig.frameResponses.push_back(
                    LinFrameResponse{slaveFrame, LinFrameResponseMode::TxUnconditional});
            }
            else
            {
                slaveFrame.data = {'S', 'L', 'A', 'V', 'E', 'R', 'X', 0};
                slaveConfig.frameResponses.push_back(LinFrameResponse{slaveFrame, LinFrameResponseMode::Rx});
            }
        }

        linController->Init(slaveConfig);

        auto onReceiveLinMessageFromSilKit = [&logger](ILinController* /*controller*/,
                                                       const LinFrameStatusEvent& frameStatusEvent) {
            if (frameStatusEvent.status == LinFrameStatus::LIN_RX_OK)
            {
                std::ostringstream SILKitDebugMessage;
                std::string frameContent;
                for (int i = 0; i < frameStatusEvent.frame.dataLength; i++)
                {
                    frameContent += frameStatusEvent.frame.data[i];
                }
                SILKitDebugMessage << "SIL Kit >> LinDemo: Resive from master Lin frame (id=" << std::hex
                                   << (int)frameStatusEvent.frame.id << ")"
                                   << "context: " << frameContent;
                logger->Debug(SILKitDebugMessage.str());
            }
        };
        linController->AddFrameStatusHandler(onReceiveLinMessageFromSilKit);
        auto wakeupHandler = [&logger, &linController](ILinController*, LinWakeupEvent wakeupEvent) {
            linController->WakeupInternal();
            logger->Debug("Lin network Wakeup event received, event transmitted direction is: "
                          + std::to_string((int)wakeupEvent.direction));
            logger->Debug("Current state: " + std::to_string((int)linController->Status()));
        };
        linController->AddWakeupHandler(wakeupHandler);

        auto sleepHandler = [&logger, &linController](ILinController*, const LinGoToSleepEvent& goToSleepEvent) {
            linController->GoToSleepInternal();
            logger->Debug("Lin network sleep event received.");
            logger->Debug("Current state: " + std::to_string((int)linController->Status()));
        };
        linController->AddGoToSleepHandler(sleepHandler);
        promptForExit();
    }
    catch (const SilKit::ConfigurationError& error)
    {
        std::cerr << "Invalid configuration: " << error.what() << std::endl;
        return -2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Something went wrong: " << error.what() << std::endl;
        return -3;
    }

    return 0;
}
