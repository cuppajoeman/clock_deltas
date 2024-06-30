#include <chrono>
#include <cstring>
#include <enet/enet.h>
#include <getopt.h>
#include <iostream>
#include <map>
#include <string>
#include <thread>

// Helper function to get the current time in milliseconds
uint64_t get_time_in_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void run_server() {
  if (enet_initialize() != 0) {
    std::cerr << "An error occurred while initializing ENet." << std::endl;
    exit(EXIT_FAILURE);
  }

  ENetAddress address;
  ENetHost *server;
  ENetEvent event;

  address.host = ENET_HOST_ANY;
  address.port = 1234;

  server = enet_host_create(&address, 32, 2, 0, 0);
  if (!server) {
    std::cerr << "An error occurred while trying to create an ENet server host."
              << std::endl;
    enet_deinitialize();
    exit(EXIT_FAILURE);
  }

  while (true) {
    while (enet_host_service(server, &event, 1000) > 0) {
      switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        std::cout << "A new client connected from " << event.peer->address.host
                  << ":" << event.peer->address.port << std::endl;
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
        ENetPacket *response_packet = enet_packet_create(
            response_times, sizeof(response_times), ENET_PACKET_FLAG_RELIABLE);
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

void run_client(const std::string &server_ip) {
  if (enet_initialize() != 0) {
    std::cerr << "An error occurred while initializing ENet." << std::endl;
    exit(EXIT_FAILURE);
  }

  ENetHost *client = enet_host_create(nullptr, 1, 2, 0, 0);
  if (!client) {
    std::cerr << "An error occurred while trying to create an ENet client host."
              << std::endl;
    enet_deinitialize();
    exit(EXIT_FAILURE);
  }

  ENetAddress address;
  ENetPeer *peer;
  ENetEvent event;

  enet_address_set_host(&address, server_ip.c_str());
  address.port = 1234;

  peer = enet_host_connect(client, &address, 2, 0);
  if (!peer) {
    std::cerr << "No available peers for initiating an ENet connection."
              << std::endl;
    enet_host_destroy(client);
    enet_deinitialize();
    exit(EXIT_FAILURE);
  }

  std::map<uint64_t, int64_t> rtt_to_clock_delta;

  // Wait for the connection to succeed
  if (enet_host_service(client, &event, 5000) > 0 &&
      event.type == ENET_EVENT_TYPE_CONNECT) {
    std::cout << "Connection to server succeeded." << std::endl;

    while (true) {
      // Send a packet with the current time (T1)
      uint64_t t1 = get_time_in_ms();
      ENetPacket *packet =
          enet_packet_create(&t1, sizeof(t1), ENET_PACKET_FLAG_RELIABLE);
      enet_peer_send(peer, 0, packet);
      enet_host_flush(client);

      // Wait for the response
      if (enet_host_service(client, &event, 5000) > 0 &&
          event.type == ENET_EVENT_TYPE_RECEIVE) {
        uint64_t t4 = get_time_in_ms();
        uint64_t response_times[2];
        std::memcpy(response_times, event.packet->data, sizeof(response_times));
        uint64_t t2 = response_times[0];
        uint64_t t3 = response_times[1];

        std::cout << "Received packet with T2: " << t2 << " ms, T3: " << t3
                  << " ms" << std::endl;
        std::cout << "Client receive time (T4): " << t4 << " ms" << std::endl;

        // Calculate round-trip delay (δ) and clock offset (θ)
        uint64_t delta = (t4 - t1) - (t3 - t2);
        int64_t theta = ((int64_t)(t2 - t1) + (int64_t)(t3 - t4)) / 2;

        std::cout << "Round-trip delay (δ): " << delta << " ms" << std::endl;
        std::cout << "Clock offset (θ): " << theta << " ms" << std::endl;

        // Store round-trip time and clock delta
        rtt_to_clock_delta[delta] = theta;

        // Extract the clock delta corresponding to the smallest round-trip time
        // std::map is an ordered associative container that stores elements in
        // key order. The function rtt_to_clock_delta.begin() returns an
        // iterator to the first element in the map, which is the element with
        // the smallest key. Since we store round-trip times as keys,
        // rtt_to_clock_delta.begin() gives us the entry with the smallest
        // round-trip time.
        auto min_element = rtt_to_clock_delta.begin();
        int64_t current_clock_delta = min_element->second;

        std::cout << "Current computed clock delta: " << current_clock_delta
                  << " ms" << std::endl;

        enet_packet_destroy(event.packet);
      } else {
        std::cerr << "Failed to receive response from server." << std::endl;
      }

      std::this_thread::sleep_for(
          std::chrono::seconds(1)); // Wait before sending the next packet
    }
  } else {
    std::cerr << "Connection to server failed." << std::endl;
  }

  enet_peer_disconnect(peer, 0);
  while (enet_host_service(client, &event, 3000) > 0) {
    switch (event.type) {
    case ENET_EVENT_TYPE_RECEIVE:
      enet_packet_destroy(event.packet);
      break;
    case ENET_EVENT_TYPE_DISCONNECT:
      std::cout << "Disconnection succeeded." << std::endl;
      break;
    default:
      break;
    }
  }

  enet_host_destroy(client);
  enet_deinitialize();
}

void print_usage(const char *program_name) {
  std::cout << "Usage: " << program_name << " [-s | -c <server_ip>]"
            << std::endl;
  std::cout << "Description: A program which can compute the clock deltas "
               "between two computers."
            << std::endl;
  std::cout << "The delta is given such that the local clock time + the delta "
               "yields the server clock time."
            << std::endl;
  std::cout << "This is only an approximation and uses the assumption that "
               "travel time to and from the server is identical."
            << std::endl;
  std::cout
      << "For more accurate results, explore the Network Time Protocol (NTP)."
      << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -s         Run as server" << std::endl;
  std::cout
      << "  -c <ip>    Run as client and connect to the specified server IP"
      << std::endl;
}

int main(int argc, char **argv) {
  int opt;
  bool is_server = false;
  std::string server_ip;

  while ((opt = getopt(argc, argv, "sc:")) != -1) {
    switch (opt) {
    case 's':
      is_server = true;
      break;
    case 'c':
      server_ip = optarg;
      break;
    default:
      print_usage(argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  if (is_server) {
    run_server();
  } else if (!server_ip.empty()) {
    run_client(server_ip);
  } else {
    print_usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  return 0;
}
