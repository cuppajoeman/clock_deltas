#include <enet/enet.h>
#include <iostream>
#include <chrono>
#include <cstring>
#include <map>
#include <thread>
#include <string>
#include <getopt.h>

// Default port
#define DEFAULT_PORT 7777

// Helper function to get the current time in milliseconds
uint64_t get_time_in_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void run_server(int port) {
    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing ENet." << std::endl;
        exit(EXIT_FAILURE);
    }

    ENetAddress address;
    ENetHost* server;
    ENetEvent event;

    address.host = ENET_HOST_ANY;
    address.port = port;

    server = enet_host_create(&address, 32, 2, 0, 0);
    if (!server) {
        std::cerr << "An error occurred while trying to create an ENet server host." << std::endl;
        enet_deinitialize();
        exit(EXIT_FAILURE);
    }

    std::cout << "Server running on port " << port << std::endl;

    while (true) {
        while (enet_host_service(server, &event, 1000) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    std::cout << "A new client connected from "
                              << event.peer->address.host << ":" << event.peer->address.port << std::endl;
                    break;

                case ENET_EVENT_TYPE_RECEIVE: {
                    uint64_t t2 = get_time_in_ms();
                    uint64_t t1;
                    std::memcpy(&t1, event.packet->data, sizeof(t1));
                    uint64_t t3 = get_time_in_ms();

                    std::cout << "Received packet with T1: " << t1 << " ms" << std::endl;
                    std::cout << "Server receive time (T2): " << t2 << " ms" << std::endl;
                    std::cout << "Server send time (T3): " << t3 << " ms" << std::endl;

                    uint64_t response_times[2] = {t2, t3};
                    ENetPacket* response_packet = enet_packet_create(response_times, sizeof(response_times), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, response_packet);
                    enet_host_flush(server);

                    enet_packet_destroy(event.packet);
                    break;
                }

                case ENET_EVENT_TYPE_DISCONNECT:
                    std::cout << "Client disconnected." << std::endl;
                    break;

                default:
                    break;
            }
        }
    }

    enet_host_destroy(server);
    enet_deinitialize();
}

void run_client(const std::string& server_ip, int port, int send_rate) {
    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing ENet." << std::endl;
        exit(EXIT_FAILURE);
    }

    ENetHost* client = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!client) {
        std::cerr << "An error occurred while trying to create an ENet client host." << std::endl;
        enet_deinitialize();
        exit(EXIT_FAILURE);
    }

    ENetAddress address;
    ENetPeer* peer;
    ENetEvent event;

    // Validate and set the server IP address
    enet_address_set_host(&address, server_ip.c_str());
    address.port = port;

    peer = enet_host_connect(client, &address, 2, 0);
    if (!peer) {
        std::cerr << "No available peers for initiating an ENet connection." << std::endl;
        enet_host_destroy(client);
        enet_deinitialize();
        exit(EXIT_FAILURE);
    }

    /* Wait up to 5 seconds for the connection attempt to succeed. */
    if (enet_host_service(client, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
        std::cout << "Connection to " << server_ip << " succeeded." << std::endl;
    } else {
        /* Either the 5 seconds are up or a disconnect event was */
        /* received. Reset the peer in the event the 5 seconds   */
        /* had run out without any significant event.            */
        std::cout << "Connection to " << server_ip << " failed, exiting." << std::endl;
        enet_host_destroy(client);
        enet_deinitialize();
        exit(EXIT_FAILURE);
    }

    std::map<uint64_t, int64_t> rtt_to_clock_delta;
    int send_interval_ms = 1000 / send_rate;

    while (true) {
        // Send initial timestamp T1 to server
        uint64_t t1 = get_time_in_ms();
        ENetPacket* packet = enet_packet_create(&t1, sizeof(t1), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, 0, packet);
        enet_host_flush(client);

        // Receive T2 and T3 from server
        bool packet_received = false;
        while (enet_host_service(client, &event, 3000) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    if (event.packet->dataLength == sizeof(uint64_t) * 2) {
                        uint64_t response_times[2];
                        std::memcpy(response_times, event.packet->data, sizeof(response_times));
                        uint64_t t2 = response_times[0];
                        uint64_t t3 = response_times[1];

                        uint64_t local_time = get_time_in_ms();
                        int64_t delta = ((t2 - t1) + (t3 - local_time)) / 2;
                        rtt_to_clock_delta[event.peer->incomingPeerID] = delta;

                        std::cout << "Client receive time (T4): " << local_time << " ms" << std::endl;
                        std::cout << "Computed clock delta: " << delta << " ms" << std::endl;

                        packet_received = true;
                    }
                    enet_packet_destroy(event.packet);
                    break;
                }

                case ENET_EVENT_TYPE_DISCONNECT:
                    std::cout << "Disconnected from server." << std::endl;
                    break;

                default:
                    break;
            }
        }

        if (!packet_received) {
            std::cerr << "Error: Timeout or unexpected disconnection from server." << std::endl;
            enet_peer_disconnect(peer, 0);
            enet_host_flush(client);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(send_interval_ms));
    }

    // Cleanup in case of exit
    enet_peer_disconnect(peer, 0);
    enet_host_flush(client);
    enet_host_destroy(client);
    enet_deinitialize();
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [-s [-p <port>] | -c <server_ip> [-p <port>] [-r <rate>]]" << std::endl;
    std::cout << "Description: A program which can compute the clock deltas between two computers." << std::endl;
    std::cout << "The delta is given such that the local clock time + the delta yields the server clock time." << std::endl;
    std::cout << "This is only an approximation and uses the assumption that travel time to and from the server is identical." << std::endl;
    std::cout << "For more accurate results, explore the Network Time Protocol (NTP)." << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -s         Run as server (default port: " << DEFAULT_PORT << ")" << std::endl;
    std::cout << "  -c <ip>    Run as client and connect to the specified server IP (default port: " << DEFAULT_PORT << ")" << std::endl;
    std::cout << "  -p <port>  Specify port number (optional)" << std::endl;
    std::cout << "  -r <rate>  Specify client send rate in Hz (optional, default: 20)" << std::endl;
}

int main(int argc, char** argv) {
    int opt;
    bool is_server = false;
    bool is_client = false;
    std::string server_ip;
    int port = DEFAULT_PORT;
    int send_rate = 20;  // Default send rate 20 Hz

    while ((opt = getopt(argc, argv, "sc:p:r:")) != -1) {
        switch (opt) {
            case 's':
                is_server = true;
                break;
            case 'c':
                is_client = true;
                server_ip = std::string(optarg);
                break;
            case 'p':
                port = std::stoi(optarg);
                break;
            case 'r':
                send_rate = std::stoi(optarg);
                break;
            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (is_server) {
        run_server(port);
    } else if (is_client && !server_ip.empty()) {
        run_client(server_ip, port, send_rate);
    } else {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    return 0;
}
