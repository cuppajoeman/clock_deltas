#include <chrono>
#include <cstring>
#include <enet/enet.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>

// Default port
#define DEFAULT_PORT 7777

// Helper function to get the current time in milliseconds
uint64_t get_time_in_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

struct TimeData {
  TimeData() : time(0), id(0) {}
  TimeData(uint64_t time, uint64_t id) : time(time), id(id) {}
  uint64_t time;
  uint64_t id;
};

void run_server(int port, int receive_rate) {
  if (enet_initialize() != 0) {
    std::cerr << "An error occurred while initializing ENet." << std::endl;
    exit(EXIT_FAILURE);
  }

  ENetAddress address;
  ENetHost *server;
  ENetEvent event;

  address.host = ENET_HOST_ANY;
  address.port = port;

  server = enet_host_create(&address, 32, 2, 0, 0);
  if (!server) {
    std::cerr << "An error occurred while trying to create an ENet server host."
              << std::endl;
    enet_deinitialize();
    exit(EXIT_FAILURE);
  }

  std::cout << "Server running on port " << port << std::endl;

  int receive_interval_ms = 1000 / receive_rate;

  while (true) {
    while (enet_host_service(server, &event, receive_interval_ms) > 0) {
      switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        std::cout << "A new client connected from " << event.peer->address.host
                  << ":" << event.peer->address.port << std::endl;
        break;

      case ENET_EVENT_TYPE_RECEIVE: {
        uint64_t cts_receive_tmos = get_time_in_ms();

        TimeData td_cst;
        std::memcpy(&td_cst, event.packet->data, sizeof(TimeData));

        uint64_t cts_send_tmoc = td_cst.time;
        uint64_t send_id = td_cst.id;

        uint64_t stc_send_tmos = get_time_in_ms();

        std::cout << "Received packet with cts_send_tmoc: " << cts_send_tmoc << " ms" << std::endl;
        std::cout << "Server receive time cts_receive_tmos: " << cts_receive_tmos << " ms" << std::endl;
        std::cout << "Server send time stc_send_tmos: " << stc_send_tmos << " ms" << std::endl;

        TimeData td_crt(cts_receive_tmos, send_id);
        TimeData td_sst(stc_send_tmos, send_id);
        TimeData response_times[2] = {td_crt, td_sst};
        ENetPacket *response_packet = enet_packet_create(
            response_times, sizeof(response_times), ENET_PACKET_FLAG_RELIABLE);

        if (enet_peer_send(event.peer, 0, response_packet) < 0) {
          std::cout << "sending failed\n";
        };

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

void run_client(const std::string &server_ip, int port, int send_rate) {
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

  // Validate and set the server IP address
  enet_address_set_host(&address, server_ip.c_str());
  address.port = port;

  peer = enet_host_connect(client, &address, 2, 0);
  if (!peer) {
    std::cerr << "No available peers for initiating an ENet connection."
              << std::endl;
    enet_host_destroy(client);
    enet_deinitialize();
    exit(EXIT_FAILURE);
  }

  /* Wait up to 5 seconds for the connection attempt to succeed. */
  if (enet_host_service(client, &event, 5000) > 0 &&
      event.type == ENET_EVENT_TYPE_CONNECT) {
    std::cout << "Connection to " << server_ip << " succeeded." << std::endl;
  } else {
    /* Either the 5 seconds are up or a disconnect event was */
    /* received. Reset the peer in the event the 5 seconds   */
    /* had run out without any significant event.            */
    std::cout << "Connection to " << server_ip << " failed, exiting."
              << std::endl;
    enet_host_destroy(client);
    enet_deinitialize();
    exit(EXIT_FAILURE);
  }

  // round trip travel time
  std::map<uint64_t, int64_t> rtt_to_clock_delta;
  int send_interval_ms = 1000 / send_rate;

  std::unordered_map<uint64_t, uint64_t> send_id_to_cts_send_tmoc;


  // TODO: this is bad, instead we should use a limited vector here 
  // so that the average time is used for the last couple samples, fine for now
  uint64_t total_rtt = 0;
  uint64_t num_measurements = 0;

  uint64_t send_id = 0;
  while (true) {
    // client to server send time measured on client
    uint64_t cts_send_tmoc = get_time_in_ms();

    TimeData td(cts_send_tmoc, send_id);

    ENetPacket *packet =
        enet_packet_create(&td, sizeof(TimeData), ENET_PACKET_FLAG_RELIABLE);
    send_id_to_cts_send_tmoc[send_id] = cts_send_tmoc;

    enet_peer_send(peer, 0, packet);
    enet_host_flush(client);

    std::cout << "just sent packet to server at time: " << cts_send_tmoc << "ms"<<  std::endl;
    send_id++;

    bool packet_received = false;
    while (enet_host_service(client, &event, send_interval_ms) > 0) {
      switch (event.type) {
      case ENET_EVENT_TYPE_RECEIVE: {
        if (event.packet->dataLength == sizeof(TimeData) * 2) {
          uint64_t stc_receive_tmoc = get_time_in_ms();
          TimeData response_times[2];
          std::memcpy(response_times, event.packet->data,
                      sizeof(response_times));


          TimeData td_crt = response_times[0];
          TimeData td_sst = response_times[1];
          uint64_t cts_receive_tmos = td_crt.time;
          uint64_t stc_send_tmos = td_sst.time;


          // note that we could have indexed with either of td_crt or td_sst's id, as they are the same
          // also note this is safe because if you get a message back from the server it's only
          // because the client already created and sent the packet and at the same time 
          // we also store into this map:
          uint64_t corresponding_cts_send_tmoc = send_id_to_cts_send_tmoc[td_crt.id];

          std::cout << "just received packet at time: " << stc_receive_tmoc << " ms" << std::endl;

          std::cout << "packet contained cts_receive_tmos: " << cts_receive_tmos << " ms and stc_send_tmos: " << stc_send_tmos
                    << " ms" << std::endl;

          // in the following setup we assume the following things which are not the reality of the situation:
          // * client to server travel time is constant is a constant tt
          // * server to client travel time is also tt
          // * given "real time" which is what we perceive, if we freeze time (at any time) 
          // and check the server clock (sc) and compare with the client clock (cc) 
          // there is a constant cd such that sc = cc + cd
          //
          // Note that the fact that sc = cc + cd gives us an easy way to convert a time that was measured
          // on the server to a time that was measured on the client if you have the server time, simply 
          // subtract cd to obtain cc, and if you have cc add cd to obtain sc
          // 
          //
          //             <--tt-->      <--tt-->
          //                    t2     t3
          // server ------------x------x---------------------
          //                   /        \
          //                  /          \
          //                 /            \
          //                /              \
          //               /                \
          //              /                  \
          // client -----x---------------------x-------------
          //             t1                    t4
          // 
          // If at time t1 on the client we sent out the packet, then on the clients clock
          // we expect that packet to arrive at t1 + tt, since t1 is measured on the client then
          // that time on the server would be given by t1 + dc + tt, which represents the time 
          // the server should theoretically receive the packet, therefore we have
          //  
          // t2 - t1 = (t1 + dc + tt) - t1 = dc + tt = tt + dc 
          //
          // tt = t2 - t1 - dc (A)
          //
          // similarly for the packet travelling back we have:
          //
          // t4 - t3 = (t3 - dc + tt) - t3 = tt - dc 
          //
          // tt = t4 - t3 + dc (B)
          //
          // therefore 
          //
          // t4 - t3 + dc = t2 - t1 - dc
          //
          // so 
          //
          // dc = ((t2 - t1) + (t3 - t4)) / 2
          //
          // now in the code we have these identifications:
          //
          // * t1 = cts_send_tmoc
          // * t2 = cts_receive_tmos
          // * t3 = stc_send_tmos
          // * t4 = stc_receive_tmoc
          //
          // Calculate round-trip delay (δ) and clock offset (θ)
          uint64_t time_between_client_send_and_receive = (stc_receive_tmoc - corresponding_cts_send_tmoc);
          uint64_t time_spent_on_server = (stc_send_tmos - cts_receive_tmos);
          uint64_t delta = time_between_client_send_and_receive - time_spent_on_server;

          int64_t theta = ((int64_t)(cts_receive_tmos - corresponding_cts_send_tmoc) + (int64_t)(stc_send_tmos - stc_receive_tmoc)) / 2;

          std::cout << "Round-trip delay (δ): " << delta << " ms" << std::endl;
          std::cout << "Clock offset (θ): " << theta << " ms" << std::endl;

          // Store round-trip time and clock delta
          rtt_to_clock_delta[delta] = theta;

          // Calculate and display the average round trip time
          total_rtt += delta;
          num_measurements++;
          uint64_t average_rtt = total_rtt / num_measurements;
          std::cout << "Average round-trip time (RTT): " << average_rtt << " ms" << std::endl;

          // Extract the clock delta corresponding to the smallest round-trip
          // time std::map is an ordered associative container that stores
          // elements in key order. The function rtt_to_clock_delta.begin()
          // returns an iterator to the first element in the map, which is the
          // element with the smallest key. Since we store round-trip times as
          // keys, rtt_to_clock_delta.begin() gives us the entry with the
          // smallest round-trip time.
          //
          // as mentioned earlier our assumptions are incorrect, thus we cannot expect our equations to actually work out
          // in order for our equation to work out "more", and give us accurate results
          // we should prioritize those data samples collected whose circumstances are closer 
          // to the assumptions, one assumption is that the travel time to the server and back from the server is the same
          // and additionally implicitly assuming that the network path is the same, under this assumption
          // if a round trip time takes longer than usually this is because there are delays.
          //
          // delays come from "bare metal" delays and any delays spent waiting in router queues, thus if a 
          // round trip takes longer assuming the same network path it is because there were more delays, 
          // since these delays may occur on the way there and potentially not on the way back (or vise versa)
          // data collected in these types of samples have a higher probability of not adhering to the assumption
          // that travel time to the server and back from the server are the same, therefore we should cast 
          // away those samples, therefore we we use those samples collected which have minimal round trip travel time
          // and only use the minimum one.
          // 
          auto min_element = rtt_to_clock_delta.begin();
          int64_t current_clock_delta = min_element->second;

          std::cout << "Current computed clock delta: " << current_clock_delta
                    << " ms" << std::endl;
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

    // not sure why this was here, but I commented out because on the first iteration 
    // you might not have an enet message which would trigger this.
    /*if (!packet_received) {*/
    /*  std::cerr << "Error: Timeout or unexpected disconnection from server."*/
    /*            << std::endl;*/
    /*  enet_peer_disconnect(peer, 0);*/
    /*  // enet_host_flush(client);*/
    /*}*/
  }

  // Cleanup in case of exit
  enet_peer_disconnect(peer, 0);
  enet_host_flush(client);
  enet_host_destroy(client);
  enet_deinitialize();
}

void print_usage(const char *program_name) {
  std::cout << "Usage: " << program_name
            << " [-s [-p <port>] | -c <server_ip> [-p <port>] [-r <rate>]]"
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
  std::cout << "  -s         Run as server (default port: " << DEFAULT_PORT
            << ")" << std::endl;
  std::cout << "  -c <ip>    Run as client and connect to the specified server "
               "IP (default port: "
            << DEFAULT_PORT << ")" << std::endl;
  std::cout << "  -p <port>  Specify port number (optional)" << std::endl;
  std::cout << "  -r <rate>  Specify (client/server) (send/poll) rate in Hz "
               "(optional, default: 20) if you change this value, be sure that "
               "it matches on the client and server"
            << std::endl;
}

int main(int argc, char **argv) {
  int opt;
  bool is_server = false;
  bool is_client = false;
  std::string server_ip;
  int port = DEFAULT_PORT;
  int send_rate = 20; // Default send rate 20 Hz

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
    run_server(port, send_rate);
  } else if (is_client && !server_ip.empty()) {
    run_client(server_ip, port, send_rate);
  } else {
    print_usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  return 0;
}
