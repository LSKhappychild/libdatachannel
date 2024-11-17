#include <iostream>
#include <thread>
#include <memory>
#include <rtc/rtc.hpp>
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

int main() {
    // Generate a random client ID
    std::string clientId = "client_" + std::to_string(rand());

    // Create a WebSocket connection to the signaling server
    auto ws = std::make_shared<rtc::WebSocket>();

    ws->onOpen([]() {
        std::cout << "WebSocket connected to signaling server." << std::endl;
    });

    ws->onError([](const std::string &error) {
        std::cerr << "WebSocket error: " << error << std::endl;
    });

    ws->onClosed([]() {
        std::cout << "WebSocket connection closed." << std::endl;
    });

    // PeerConnection and DataChannel
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;

    //media setup
    // rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
    // media.addH264Codec(96);
    // media.setBitrate(3000); // Request 3Mbps (Browsers do not encode more than 2.5MBps from a webcam)  

    ws->onMessage([&](variant<rtc::binary, std::string> data) {
        if (!holds_alternative<std::string>(data)) {
            return;
        }

        std::cout << "Client: Received message: " << get<std::string>(data) << std::endl;

        json message = json::parse(get<std::string>(data));
        std::string type = message["type"];

        if (message.contains("sdp")) {
            std::string sdp = message["sdp"];

            if (type == "offer") {
                // Create PeerConnection
                rtc::Configuration config;
                config.iceServers.emplace_back("stun:stun.l.google.com:19302");
                pc = std::make_shared<rtc::PeerConnection>(config);

                rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
                media.addH264Codec(96);
                media.setBitrate(3000); // Request 3Mbps (Browsers do not encode more than 2.5MBps from a webcam)  

                pc->addTrack(media);

                // Add state change handler
                pc->onStateChange([](rtc::PeerConnection::State state) {
                    cout << "Client: PeerConnection state changed to: " << state << endl;
                });

                pc->onGatheringStateChange([&ws, pc](rtc::PeerConnection::GatheringState state) {
                    if (state == rtc::PeerConnection::GatheringState::Complete) {
                        auto description = pc->localDescription();
                        std::cout << "Client: ICE gathering complete, sending answer" << std::endl;
                        json answer = {
                            {"id", "server"},
                            {"type", "answer"},
                            {"sdp", std::string(description.value())}
                        };
                        ws->send(answer.dump());
                    }
                });

                pc->onSignalingStateChange([](rtc::PeerConnection::SignalingState state) {
                    cout << "Client: Signaling state changed to: " << state << endl;
                });

                // Set remote description
                std::cout << "Client: Setting remote description: " << sdp << std::endl;    
                pc->setRemoteDescription(rtc::Description(sdp, type));

                // Create answer
                pc->onLocalDescription([&](rtc::Description desc) {
                    std::cout << "Client: Sending answer to server : " << desc.typeString() << std::endl;    
                    json answer = {
                        {"id", clientId},
                        {"type", "answer"},
                        {"sdp", std::string(desc)}
                    };
                    ws->send(answer.dump());
                });

                pc->onDataChannel([&](std::shared_ptr<rtc::DataChannel> dc_in) {
                    std::cout << "Client: Data channel received from server" << std::endl;
                    dc = dc_in;
                    
                    dc->onOpen([&]() {
                        std::cout << "Client: Data channel open" << std::endl;
                        std::string videoName = "my_video.webm";
                        std::cout << "Client: Sending video request: " << videoName << std::endl;
                        dc->send(videoName);
                    });

                    dc->onError([](string error) {
                        std::cout << "Client: Data channel error: " << error << std::endl;
                    });

                    dc->onClosed([]() {
                        std::cout << "Client: Data channel closed" << std::endl;
                    });

                    dc->onMessage([&](variant<rtc::binary, std::string> data) {
                        if (holds_alternative<std::string>(data)) {
                            std::string msg = get<std::string>(data);
                            std::cout << "Client: Received message: " << msg << std::endl;
                            if (msg == "Ping") {
                                std::cout << "Client: Responding with Pong" << std::endl;
                                dc->send("Pong");
                            }
                        }
                    });
                });

            }
        }
    });

    // Open the WebSocket connection
    std::string url = "ws://127.0.0.1:8000/" + clientId;
    ws->open(url);

    // Wait for the WebSocket to be connected
    while (!ws->isOpen()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Send a request to the server to initiate the connection
    json request = {
        {"id", "server"},
        {"type", "request"}
    };
    
    ws->send(request.dump());

    // Keep the application running
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();

    // Clean up
    if (pc) {
        pc->close();
    }
    ws->close();

    return 0;
}
