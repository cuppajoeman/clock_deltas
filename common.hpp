#ifndef COMMON_HPP
#define COMMON_HPP

#include "ring_buffer.hpp"
#include <chrono>
#include <enet/enet.h>
#include <iostream>
#include <thread>

using time_point = std::chrono::system_clock::time_point;

struct RemoteTimestamps {
  time_point remote_receive;
  time_point remote_send;
  time_point expected_local_receive_time;
  std::chrono::microseconds clock_offset;
};

/**
 * @brief
 * @desc Assuming both clocks run at the same speed, then the only difference
between the two could be a phase shift, meaning that there is always a constant
offset to change from one to the other. Therefore to convert from client to
server time we have c2s(t) = t + clock_offset and s2c(t) = t - clock_offset.

Additionally the time it takes for a packet to be sent from the client and
received by the server differs from the time it takes for a packet being sent in
the reverse direction, because different ISP's route packets in different ways.

The same ISP may route the same packet in different ways, but this is a
complexity we will ignore, which is to say that we will assume that sending a
packet of the same size always takes the same amount of time.

With this assumption in place let cts be the travel time from client to server
and stc be the travel time from server to client, then there is some
travel_offset value such that cts = stc + travel_offset. Now consider the
following setup:

        r1   r2
--------*----*----------------- remote
       /      \.
      /         \.
     /            \.
----*--------------*------------ local
   l1               l2

where l1, l2 are measured on the local machine's clock and r1, r2 measured on
the remote machine's clock.

in this setup let's try to compute cts, note that we can't just do r1 - l1 as
they are measured on different clocks, so acutally

cts = r1 - c2s(l1) = s2c(r1) - l1 = r1 - l1 - clock_offset

and

stc = l2 - s2c(r2) = c2s(l2) - r2 = l2 - r2 + clock_offset

Since cts = stc + travel_offset, then

r1 - l1 - clock_offset = l2 - r2 + clock_offset + travel_offset

therefore we can isolate for the clock offset to deduce that

clock_offset = ((r1 - l1) - (l2 - r2) - travel_offset) / 2

or we could isolate for travel_offset which is:

travel_offset = (r1 - l1) - (l2 - r2) - (2 * clock_offset)

if we had the other situation:

       r2             r3
-------*--------------*-- remote
        \.           /
          \.        /
            \.     /
-------------*----*------- local
            l2   l3

stc = l2 - s2c(r2) = l2 - (r2 - clock_offset) = l2 - r2 + clock_offset
cts = r3 - c2s(l3) = r3 - (l3 + clock_offset) = r3 - l3 - clock_offset

notice how the clock offset's sign has flipped

Since cts = stc + travel_offset, then


REDO

r3 - l3 + clock_offset = l2 - r2 - clock_offset + travel_offset

thus clock_offset = ((l2 - r2) - (r3 - l3) + travel_offset) / 2

travel_offset = (r3 - l3) - (l2 - r2) + 2 clock_offset

Note that in order to compute clock_offset or travel_offset we would need to
know the other which leads to a cyclic depedency, so what can be done is to
first assume that there is no travel_offset, in other words travel_offset = 0,
with this we can compute the clock_offset, and then in-turn we can compute the
clock_offset, and use that new clock offset next time, so assuming that the
client to server and server to client travel times are the same allows us to
bootstrap the process. Specifically let's go through some iterations to
understand this in full detail.

The client sends over the initial time to the server, and the server sends back
the receive and send time back to the client, when the client receives this data
we do iteration 0:

after receving the packet (r1, r2, clock_offset_uninitialized)

iteration 0 on client:
travel_offset_0 = 0
clock_offset_0 = ((r1 - l1) - (l2 - r2)) / 2

now if we were try to re-compute the travel_offset using the clock_offset we
just computed, we would simply get 0, which isn't very useful, so we need new
data points. So the next iteration can be done on the server, note that we will
send over our current clock and travel offsets in the packets as well.

                        iter 1
                           |
        r1   r2            r3
--------*----*-------------*---*-------  ... remote
       /      \.          /     \.
      /         \.       /        \.
     /            \.    /           \.
----*--------------*---*-------------*-  ... local
   l1              l2  l3            l4
                   |                 |
                iter 0            iter 2

-----

after receiving the packet: (l2, l3, clock_offset_0)

iteration 1 on server:
travel_offset_1 = (l2 - r2) - (r2 - l3) (2 * clock_offset_0)
clock_offset_1 = ((r1 - l1) - (l2 - r2) - travel_offset_1) / 2

-----

after receiving the packet: (r3, r4, clock_offset_1)
iteration 2 on client:

travel_offset_1 = () - (r2 - l3) (2 * clock_offset_1)
clock_offset_1 = ((r1 - l1) - (l2 - r2) - travel_offset_1) / 2

-----

Notice that clock_offset_i/travel_offset_i represent an approximation of the
clock/travel offset after i iterations, when i is close to 0 then the
clock_offset is closer to the iteration where we assumed that the travel time
offset was 0, and thus has more errors, then when i is higher the travel_offset
is more correct

Also note that the data computed on the client will be:

clock_offset_2k and travel_offset_2k for k in N_0

and on the server

clock_offset_(2k + 1) and travel_offset_(2k + 1) for k in N_0

therefore to improve accuracy you can send travel times and clock times and
average it out.

---

recall that once you know the travel offset we have this equation


cts = stc + travel_offset or stc = cts - travel_offset

stc = (l2 - r2) + clock_offset

cts = (r1 - l1) - clock_offset

therefore

cts = (l2 - r2) + (clock_offset + travel_offset)

stc = (r1 - l1) - clock_offset - travel_offset = (r1 - l1) - (clock_offset +
travel_offset)

notice that the above equations are intuitively saying take the travel time for
the other direction, and then add the offset to it and then apply the clock
offset.

with this information we can compute the expected arrival time of a packet we
are about to send, consider r2, the expected arrival time is s2c(r2) + stc and
l1 and r1 are used to compute it


        r1   r2            r3
--------*----*-------------*---*-------  ... remote
       /      \.          /     \.
      /         \.       /        \.
     /            \.    /           \.
----*--------------*---*-------------*-  ... local
   l1              l2  l3            l4

 */

std::chrono::microseconds compute_clock_offset(
    const time_point &local_send, const time_point &remote_receive,
    const time_point &remote_send, const time_point &local_receive,
    std::chrono::microseconds travel_offset, bool is_server);

std::chrono::microseconds compute_travel_offset(
    const time_point &local_send, const time_point &remote_receive,
    const time_point &remote_send, const time_point &local_receive,
    std::chrono::microseconds clock_offset, bool is_server);

/**
 * @brief Get the current time.
 *
 * @return time_point Current time.
 */
time_point get_current_time();

/**
 * @brief Initialize an ENet host.
 *
 * @param address Pointer to ENetAddress, or nullptr for any host.
 * @param peer_count Maximum number of peers allowed.
 * @param port Port number.
 * @return ENetHost* Initialized ENet host.
 */
ENetHost *initialize_enet_host(ENetAddress *address, size_t peer_count,
                               enet_uint16 port);

/**
 * @brief Send timestamps to a peer.
 *
 * @param peer ENetPeer to send timestamps to.
 * @param remote_ts Remote timestamps to send.
 */
void send_timestamps(ENetPeer *peer, const RemoteTimestamps &remote_ts);

/**
 * @brief Handle receive event from ENet.
 *
 * This function processes an ENet event and sends back timestamps to the peer.
 * It also adds a delay to control the rate of communication.
 *
 * @param event ENet event to handle.
 * @param peer ENetPeer from which the event was received.
 * @param last_local_send Time of the last local send.
 * @param is_server true if this function is called by the server, false if
 * called by the client.
 */
void handle_receive_event(ENetEvent &event, ENetPeer *peer,
                          time_point &last_local_send,
                          RingBuffer &clock_offset_rb,
                          RingBuffer &travel_offset_rb,
                          RingBuffer &local_to_remote_travel_times,
                          bool is_server);

/**
 * @brief Log timestamps durations to stdout.
 *
 * @param local_send Time of the last local send.
 * @param local_receive Time of the local receive.
 * @param remote_send Time of the remote send.
 * @param remote_receive Time of the remote receive.
 * @param is_server true if this function is called by the server, false if
 * called by the client.
 */
void log(const time_point &last_local_send, const time_point &local_receive,
         const time_point &remote_send, const time_point &remote_receive,
         const time_point &local_send, const time_point &expected_receive_time,
         std::chrono::microseconds clock_offset,
         std::chrono::microseconds travel_offset,
         std::chrono::microseconds average_local_to_remote_travel_time,
         bool is_server);

void print_time(const std::string &label, const time_point &tp);

// Helper function to print microseconds duration
void print_microseconds(const std::string &label, std::chrono::microseconds us);

#endif // COMMON_HPP
