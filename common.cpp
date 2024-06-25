#include "common.hpp"
#include <chrono>
#include <cstring>
#include <iomanip> // for std::setprecision
#include <ratio>

time_point get_current_time() { return std::chrono::steady_clock::now(); }

std::chrono::microseconds compute_clock_offset(
    const time_point &local_send, const time_point &remote_receive,
    const time_point &remote_send, const time_point &local_receive,
    std::chrono::microseconds travel_offset, bool is_server) {

  auto offset = ((is_server ? (-1) : 1) * ((remote_receive - local_send) -
                                           (local_receive - remote_send)) -
                 travel_offset) /
                2;

  return std::chrono::duration_cast<std::chrono::microseconds>(offset);
}

std::chrono::microseconds compute_travel_offset(
    const time_point &local_send, const time_point &remote_receive,
    const time_point &remote_send, const time_point &local_receive,
    std::chrono::microseconds clock_offset, bool is_server) {

  auto offset = (is_server ? (-1) : 1) * ((remote_receive - local_send) -
                                          (local_receive - remote_send)) -
                2 * clock_offset;

  return std::chrono::duration_cast<std::chrono::microseconds>(offset);
}

// std::chrono::microseconds client_to_server_time(const time_point
// &client_time, std::chrono::microseconds clock_offset) {
//   auto server_time = client_time + clock_offset;
//   return std::chrono::duration_cast<std::chrono::microseconds>(server_time);
// }

time_point compute_expected_local_receive_time(
    const time_point &local_send, RingBuffer &local_to_remote_travel_times,
    std::chrono::microseconds clock_offset,
    std::chrono::microseconds travel_offset, bool is_server) {

  auto send_time_from_remote_pov =
      local_send + (is_server ? -1 : 1) * clock_offset;

  auto expected_local_receive_time =
      send_time_from_remote_pov + local_to_remote_travel_times.average();

  return expected_local_receive_time;
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
  ENetPacket *packet = enet_packet_create(&remote_ts, sizeof(remote_ts),
                                          ENET_PACKET_FLAG_RELIABLE);
  if (enet_peer_send(peer, 0, packet) < 0) {
    std::cout << "sending failed\n";
  };
}

void handle_receive_event(ENetEvent &event, ENetPeer *peer,
                          time_point &last_local_send,
                          RingBuffer &clock_offset_rb,
                          RingBuffer &travel_offset_rb,
                          RingBuffer &local_to_remote_travel_times,
                          bool is_server) {

  std::cout << "\n===================\n";

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

  print_time("real receive time    ", local_receive);
  print_time("expected receive time", remote_ts.expected_local_receive_time);
  print_microseconds("prediction accuracy  ", prediction_accuracy_us);

  // (A) Note: On the very first send out of the client, remote_receive equals
  // remote_send. Refer to process_client_events in client.cpp for details.

  std::chrono::microseconds raw_travel_time_offset;
  if (remote_ts.remote_receive != remote_ts.remote_send) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(10)); // Simulate computation time

    raw_travel_time_offset = compute_travel_offset(
        last_local_send, remote_ts.remote_receive, remote_ts.remote_send,
        local_receive, remote_ts.clock_offset, is_server);

  } else { // iteration 0
    raw_travel_time_offset = std::chrono::microseconds(0);
    std::cout << "iteration 0\n";
  }

  print_microseconds("raw travel time offset", raw_travel_time_offset);

  bool use_average = true;

  travel_offset_rb.add(raw_travel_time_offset);
  // travel_offset_rb.print_contents();
  std::cout << "travel offset with average: "
            << travel_offset_rb.average().count()
            << " without average: " << raw_travel_time_offset.count();
  std::chrono::microseconds travel_time_offset =
      use_average ? travel_offset_rb.average() : raw_travel_time_offset;

  std::chrono::microseconds raw_clock_offset = compute_clock_offset(
      last_local_send, remote_ts.remote_receive, remote_ts.remote_send,
      local_receive, travel_time_offset, is_server);

  clock_offset_rb.add(raw_clock_offset);
  // clock_offset_rb.print_contents();

  std::cout << "clock offset with average: "
            << static_cast<long long>(clock_offset_rb.average().count())
            // << clock_offset_rb.average().count()
            << " without average: " << raw_clock_offset.count();
  std::chrono::microseconds clock_offset =
      use_average ? clock_offset_rb.average() : raw_clock_offset;

  auto local_to_remote_travel_time =
      // std::chrono::microseconds remote_to_local_travel_time =
      (remote_ts.remote_receive -
       (last_local_send + (is_server ? -1 : 1) * clock_offset));

  auto local_to_remote_travel_time_fixed =
      std::chrono::duration_cast<std::chrono::microseconds>(
          local_to_remote_travel_time);

  std::cout << "computed local to remote travel time: "
            << local_to_remote_travel_time_fixed.count() << "\n";

  // the above may be negative with a big clock offset therefore we do:
  auto abs_duration = std::chrono::microseconds(
      std::abs(local_to_remote_travel_time_fixed.count()));
  // std::chrono::microseconds(local_to_remote_travel_time.count());
  local_to_remote_travel_times.add(abs_duration);

  // Send timestamps back to client
  time_point local_send_time = get_current_time();

  time_point expected_receive_time = compute_expected_local_receive_time(
      local_send_time, local_to_remote_travel_times, clock_offset,
      travel_time_offset, is_server);

  std::cout << "during this iteration we had the following information vvv";
  log(last_local_send, remote_ts.remote_receive, remote_ts.remote_send,
      local_receive, local_send_time, expected_receive_time, clock_offset,
      travel_time_offset, local_to_remote_travel_times.average(), is_server);

  std::cout << "sending that information now";
  send_timestamps(peer, {local_receive, local_send_time, expected_receive_time,
                         clock_offset});

  last_local_send = local_send_time;

  // Cleanup packet
  enet_packet_destroy(event.packet);
}

// Helper function to print time in microseconds and seconds
void print_time(const std::string &label, const time_point &tp) {
  auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
      tp.time_since_epoch());
  auto duration_sec =
      std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
  auto seconds = std::chrono::duration<double>(tp.time_since_epoch());

  std::cout << label << " - microseconds: " << duration_us.count() << " us, ";
  if (duration_sec.count() > 0) {
    std::cout << "seconds: " << std::fixed << std::setprecision(6)
              << seconds.count() << " s\n";
  } else {
    std::cout << "seconds: " << duration_sec.count() << " s\n";
  }
}

// Helper function to print microseconds duration with decimal seconds if less
// than 1 second
void print_microseconds(const std::string &label,
                        std::chrono::microseconds us) {
  auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(us);

  std::cout << label << " - microseconds: " << us.count() << " us, ";
  if (us < std::chrono::seconds(1)) {
    std::cout << "seconds: " << std::fixed << std::setprecision(6)
              << seconds.count() << " s\n";
  } else {
    std::cout << "seconds: " << seconds.count() << " s\n";
  }
}

// Function to log various timestamps
void log(const time_point &last_local_send, const time_point &remote_receive,
         const time_point &remote_send, const time_point &local_receive,
         const time_point &local_send, const time_point &expected_receive_time,
         std::chrono::microseconds clock_offset,
         std::chrono::microseconds travel_offset,
         std::chrono::microseconds average_local_to_remote_travel_time,
         bool is_server) {

  std::cout << "From " << (is_server ? "Server" : "Client") << " POV:\n";
  std::cout << "Local send time:\n";
  print_time("  ", last_local_send);
  std::cout << "Remote receive time:\n";
  print_time("  ", remote_receive);
  std::cout << "Remote send time:\n";
  print_time("  ", remote_send);
  std::cout << "Local receive time:\n";
  print_time("  ", local_receive);
  std::cout << "Expected receive time:\n";
  print_time("  ", expected_receive_time);

  std::cout << "Computed clock offset:\n";
  print_microseconds("  ", clock_offset);
  std::cout << "Computed travel offset:\n";
  print_microseconds("  ", travel_offset);
  std::cout << "Average local to remote travel time:\n";
  print_microseconds("  ", average_local_to_remote_travel_time);
}
