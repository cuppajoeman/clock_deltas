#include "common.hpp"
#include <cstring>

time_point get_current_time() { return std::chrono::steady_clock::now(); }

std::chrono::microseconds compute_clock_offset(
    const time_point &local_send, const time_point &remote_receive,
    const time_point &remote_send, const time_point &local_receive,
    std::chrono::microseconds travel_offset) {

  auto offset = ((remote_receive - local_send) - (local_receive - remote_send) -
                 travel_offset) /
                2;

  return std::chrono::duration_cast<std::chrono::microseconds>(offset);
}

std::chrono::microseconds compute_travel_offset(
    const time_point &local_send, const time_point &remote_receive,
    const time_point &remote_send, const time_point &local_receive,
    std::chrono::microseconds clock_offset) {

  auto offset = (remote_receive - local_send) - (local_receive - remote_send) -
                (2 * clock_offset);

  return std::chrono::duration_cast<std::chrono::microseconds>(offset);
}

ENetHost *initialize_enet_host(ENetAddress *address, size_t peer_count,
                               enet_uint16 port) {
  if (enet_initialize() != 0) {
    std::cerr << "An error occurred while initializing ENet.\n";
    exit(EXIT_FAILURE);
  }
  atexit(enet_deinitialize);

  if (address != nullptr) {
    address->host = ENET_HOST_ANY;
    address->port = port;
  }

  ENetHost *host = enet_host_create(address, peer_count, 2, 0, 0);
  if (host == nullptr) {
    std::cerr << "An error occurred while trying to create an ENet host.\n";
    exit(EXIT_FAILURE);
  }

  return host;
}

void send_timestamps(ENetPeer *peer, const RemoteTimestamps &remote_ts) {
  std::cout << "Sending timestamps\n";
  ENetPacket *packet = enet_packet_create(&remote_ts, sizeof(remote_ts),
                                          ENET_PACKET_FLAG_RELIABLE);
  if (enet_peer_send(peer, 0, packet) < 0) {
    std::cout << "sending failed\n";
  };
}

void handle_receive_event(ENetEvent &event, ENetPeer *peer,
                          const time_point &last_local_send, bool is_server) {

  // Extract remote timestamps from received packet
  RemoteTimestamps remote_ts;
  std::memcpy(&remote_ts, event.packet->data, sizeof(remote_ts));

  // Calculate remote send time
  time_point local_receive = get_current_time();

  auto prediction_accuracy =
      local_receive - remote_ts.expected_local_receive_time;

  // Convert duration to microseconds for printing
  auto prediction_accuracy_us =
      std::chrono::duration_cast<std::chrono::microseconds>(
          prediction_accuracy);

  std::cout << "Prediction accuracy: " << prediction_accuracy_us.count()
            << " microseconds" << std::endl;

  // (A) Note: On the very first send out of the client, remote_receive equals
  // remote_send. Refer to process_client_events in client.cpp for details.

  std::chrono::microseconds travel_time_offset;
  if (remote_ts.remote_receive != remote_ts.remote_send) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(1000)); // Simulate computation time

    travel_time_offset = compute_travel_offset(
        last_local_send, remote_ts.remote_receive, remote_ts.remote_send,
        local_receive, remote_ts.clock_offset);

  } else { // iteration 0
    travel_time_offset = std::chrono::microseconds(0);
  }

  std::chrono::microseconds clock_offset = compute_clock_offset(
      last_local_send, remote_ts.remote_receive, remote_ts.remote_send,
      local_receive, travel_time_offset);

  // Send timestamps back to client
  time_point local_send_time = get_current_time();
  time_point expected_receive_time =
      local_send_time +
      ((is_server ? (-1) : 1) *
       clock_offset); // bad need to account for directional travel time
  send_timestamps(peer, {local_receive, local_send_time, expected_receive_time,
                         clock_offset});

  log_timestamps(last_local_send, remote_ts.remote_receive,
                 remote_ts.remote_send, local_receive, is_server);

  // Cleanup packet
  enet_packet_destroy(event.packet);
}

void log_timestamps(const time_point &local_send,
                    const time_point &remote_receive,
                    const time_point &remote_send,
                    const time_point &local_receive, bool is_server) {
  auto duration = [](time_point start, time_point end) {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start)
        .count();
  };

  std::cout << (is_server ? "Server" : "Client") << "\n";
  std::cout << "Local Send Time: " << duration(time_point{}, local_send)
            << " us\n";
  std::cout << "Remote Receive Time: " << duration(time_point{}, remote_receive)
            << " us\n";
  std::cout << "Remote Send Time: " << duration(time_point{}, remote_send)
            << " us\n";
  std::cout << "Local Receive Time: " << duration(time_point{}, local_receive)
            << " us\n";
}
