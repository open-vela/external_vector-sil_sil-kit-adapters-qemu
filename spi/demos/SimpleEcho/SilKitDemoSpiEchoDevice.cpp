// Copyright (c) Vector Informatik GmbH. All rights reserved.

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include "silkit/SilKit.hpp"
#include "silkit/config/all.hpp"
#include "silkit/services/pubsub/all.hpp"
#include "silkit/util/serdes/Serialization.hpp"

using namespace SilKit::Services::PubSub;

using namespace std::chrono_literals;

/**************************************************************************************************
 * Main Function
 **************************************************************************************************/

int main(int argc, char**)
{
    const std::string participantConfigurationString =
        R"({ "Logging": { "Sinks": [ { "Type": "Stdout", "Level": "Info" } ] } })";

    const std::string participantName = "SPIDevice";
    const std::string registryURI = "silkit://localhost:8501";

    const auto create_pubsubspec = [](const std::string& topic_name, const std::string& instance,
                                SilKit::Services::MatchingLabel::Kind matching_mode) {
        PubSubSpec r(topic_name, SilKit::Util::SerDes::MediaTypeData());
        r.AddLabel("VirtualNetwork", "Default", matching_mode);
        r.AddLabel("Namespace", "Namespace", matching_mode);
        // Uncomment next line if you have a meaningful instance to filter, but this makes it necessary to "know"
        // the instance of the sender, see invocations of "create_pubsubspec", as well as make CANoe a disturbance
        // in this (this is the DO's object name)
        //r.AddLabel("Instance", instance, matching_mode);
        return r;
    };

    const PubSubSpec subDataSpec =
        create_pubsubspec("qemuOutbound", "SPIAdapter", SilKit::Services::MatchingLabel::Kind::Mandatory);

    const PubSubSpec pubDataSpec =
        create_pubsubspec("qemuInbound", participantName, SilKit::Services::MatchingLabel::Kind::Optional);

    try
    {
        auto participantConfiguration =
            SilKit::Config::ParticipantConfigurationFromString(participantConfigurationString);

        std::cout << "Creating participant '" << participantName << "' at " << registryURI << std::endl;
        auto participant = SilKit::CreateParticipant(participantConfiguration, participantName, registryURI);

        auto dataPublisher = participant->CreateDataPublisher(participantName + "_pub", pubDataSpec);
        
        auto dataSubscriber = participant->CreateDataSubscriber(
            participantName + "_sub", subDataSpec,
            [&](SilKit::Services::PubSub::IDataSubscriber* subscriber, const DataMessageEvent& dataMessageEvent) {
                std::cout << "SIL Kit >> SIL Kit: "
                          << std::string_view(reinterpret_cast<const char*>(dataMessageEvent.data.data()+4),
                                              dataMessageEvent.data.size()-4)
                          << std::endl;
                dataPublisher->Publish(dataMessageEvent.data);
            });

        std::cout << "Press enter to stop the process..." << std::endl;
        std::cin.ignore();
    }
    catch (const SilKit::ConfigurationError& error)
    {
        std::cerr << "Invalid configuration: " << error.what() << std::endl;
        std::cout << "Press enter to stop the process..." << std::endl;
        std::cin.ignore();
        return -2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Something went wrong: " << error.what() << std::endl;
        std::cout << "Press enter to stop the process..." << std::endl;
        std::cin.ignore();
        return -3;
    }

    return 0;
}
