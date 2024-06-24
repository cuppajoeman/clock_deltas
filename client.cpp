#include "common.hpp"
#include "ring_buffer.hpp"

void process_client_events(ENetHost *client, ENetPeer *peer,
                           time_point &last_local_send);

ENetPeer *attempt_connection_and_return_peer(ENetHost *client,
                                             ENetAddress &address) {
  ENetPeer *peer = enet_host_connect(client, &address, 2, 0);
  if (peer == nullptr) {
    std::cerr << "No available peers for initiating an ENet connection.\n";
    // return EXIT_FAILURE;
  }
  /* Wait up to 5 seconds for the connection attempt to succeed. */
  ENetEvent event;
  if (enet_host_service(client, &event, 5000) > 0 &&
      event.type == ENET_EVENT_TYPE_CONNECT) {
    std::cout << "Connection to some.server.net:7777 succeeded.";
  } else {
    /* Either the 5 seconds are up or a disconnect event was */
    /* received. Reset the peer in the event the 5 seconds   */
    /* had run out without any significant event.            */
    enet_peer_reset(peer);

    puts("Connection to some.server.net:7777 failed.");
  }
  return peer;
}

int main(int argc, char **argv) {
  ENetHost *client;
  ENetAddress address;
  ENetPeer *peer;

  bool running_online = true;

  client = initialize_enet_host(nullptr, 1, 0);

  if (running_online) {
    enet_address_set_host(&address, "104.131.10.102");
  } else {
    enet_address_set_host(&address, "localhost");
  }
  address.port = 7777;

  peer = attempt_connection_and_return_peer(client, address);

  time_point last_local_send = get_current_time();
  process_client_events(client, peer, last_local_send);

  enet_host_destroy(client);
  return EXIT_SUCCESS;
}

void process_client_events(ENetHost *client, ENetPeer *peer,
                           time_point &last_local_send) {
  int max_samples_to_average_over = 10;
  RingBuffer clock_offset_rb(max_samples_to_average_over);
  RingBuffer travel_offset_rb(max_samples_to_average_over);
  // (A) Initial send with receive_time and send_time as current time
  auto current_time = get_current_time();
  send_timestamps(peer, {current_time, current_time});

  ENetEvent event;

  while (true) {
    while (enet_host_service(client, &event, 5000) > 0) {
      std::cout << "completed host service call \n";
      switch (event.type) {
      case ENET_EVENT_TYPE_RECEIVE:
        std::cout << "client receive\n";
        handle_receive_event(event, peer, last_local_send, clock_offset_rb,
                             travel_offset_rb, false);
        break;

      case ENET_EVENT_TYPE_DISCONNECT:
        std::cout << "Disconnected from server.\n";
        return;
      }
    }
  }
}
