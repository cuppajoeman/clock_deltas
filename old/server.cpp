#include "common.hpp"

/**
 * @brief Process events for the server.
 *
 * @param server ENetHost server instance.
 */
void process_server_events(ENetHost *server);

int main(int argc, char **argv) {
  ENetAddress address;
  ENetHost *server = initialize_enet_host(&address, 32, 7777);

  process_server_events(server);

  enet_host_destroy(server);
  return EXIT_SUCCESS;
}

void process_server_events(ENetHost *server) {

  int max_samples_to_average_over = 1000;
  RingBuffer clock_offset_rb(max_samples_to_average_over);
  RingBuffer travel_offset_rb(max_samples_to_average_over);
  RingBuffer remote_to_local_travel_times(max_samples_to_average_over);

  // Note: Initial value of last_local_send is not the actual time of the last
  // local send on the server
  time_point last_local_send = get_current_time();

  ENetEvent event;

  while (true) {
    while (enet_host_service(server, &event, 1000) > 0) {
      std::cout << "completed host service call \n";
      switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        std::cout << "A new client connected from " << event.peer->address.host
                  << ":" << event.peer->address.port << "\n";
        break;

      case ENET_EVENT_TYPE_RECEIVE:
        std::cout << "got recieve event\n";
        handle_receive_event(event, event.peer, last_local_send,
                             clock_offset_rb, travel_offset_rb,
                             remote_to_local_travel_times, true);
        break;

      case ENET_EVENT_TYPE_DISCONNECT:
        std::cout << "Client disconnected.\n";
        break;
      }
    }
  }
}
