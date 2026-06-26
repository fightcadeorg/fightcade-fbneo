#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#endif

#include "ggpo_peer_to_peer.h"

// zig cc -target x86-windows-gnu -std=c11 -lWs2_32 -O2 ggpo_input.c ggpo_network.c ggpo_peer_to_peer.c ggpo_peer_to_peer_test.c -o ggpo_peer_to_peer_test
// clang -std=c99 -Wall -Wextra -g ggpo_input.c ggpo_network.c ggpo_peer_to_peer.c ggpo_peer_to_peer_test.c -o ggpo_peer_to_peer_test

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define TEST(...) do { if (!(__VA_ARGS__)) { fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, #__VA_ARGS__); exit(1); } } while(0)

void wait_for_data(struct GGPO_PeerToPeer const* peer)
{
#ifdef _WIN32
	WSAPOLLFD peer_poll = {
		.fd = peer->_recv_sock,
		.events = POLLRDNORM,
	};
	int num = WSAPoll(&peer_poll, 1, -1);
	assert(num == 1);
#else
	struct pollfd peer_poll = {
		.fd = peer->_recv_sock,
		.events = POLLRDNORM,
	};
	int num = poll(&peer_poll, 1, -1);
	assert(num == 1);
#endif
}

int main(void)
{
#ifdef _WIN32
	WSADATA data = {};
	int result = WSAStartup(MAKEWORD(2, 2), &data);
	assert(result == 0);
#endif

	struct GGPO_PeerToPeer peer_a;
	struct GGPO_PeerToPeer peer_b;

	uint16_t const peer_a_port = 16220;
	uint16_t const peer_b_port = 16221;
	GGPO_PeerToPeer_init(&peer_a, peer_a_port);
	GGPO_PeerToPeer_init(&peer_b, peer_b_port);
	TEST(peer_a.state == kGGPO_PeerToPeer_State_initialized);
	TEST(peer_b.state == kGGPO_PeerToPeer_State_initialized);
	TEST(peer_a_port == GGPO_PeerToPeer_get_listen_port(&peer_a));
	TEST(peer_b_port == GGPO_PeerToPeer_get_listen_port(&peer_b));

	TEST(GGPO_PeerToPeer_connect_to_peer(&peer_a, "localhost", peer_b_port));
	TEST(GGPO_PeerToPeer_connect_to_peer(&peer_b, "localhost", peer_a_port));

	uint32_t time_now_ms = 0;
	// We start out synchronizing.
	// Peer A will send a sync request.
	GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
	wait_for_data(&peer_b);
	// Peer B will send a sync request and send a sync reply back to Peer A.
	GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
	wait_for_data(&peer_a);
	TEST(peer_a.state == kGGPO_PeerToPeer_State_syncing);
	TEST(peer_b.state == kGGPO_PeerToPeer_State_syncing);
	GGPO_PeerToPeer_frame_end(&peer_a);
	GGPO_PeerToPeer_frame_end(&peer_b);
	time_now_ms++;
	// Peer A will
	// - Send a sync reply to Peer B's request.
	// - Process the sync reply from Peer B and send a new sync request.
	GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
	TEST(peer_a._sync.count == 1);
	// Remove both the new sync request and sync reply from Peer B's receive socket to simulate dropped packets.
	{
		char dummy[2048];
		wait_for_data(&peer_b);
		int result = recv(peer_b._recv_sock, dummy, sizeof(dummy), 0);
		assert(result > 0);
		wait_for_data(&peer_b);
		result = recv(peer_b._recv_sock, dummy, sizeof(dummy), 0);
		assert(result > 0);
	}
	// Let's jump way forward, to force a resend of the sync requests.
	time_now_ms += 5000; // 5 seconds.
	// Run Peer B and it should resend it's sync request.
	GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
	TEST(peer_b._sync.count == 0);
	GGPO_PeerToPeer_frame_end(&peer_a);
	GGPO_PeerToPeer_frame_end(&peer_b);

	// Peer A will resend a Sync request and send the sync reply to Peer B.
	wait_for_data(&peer_a);
	GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
	TEST(peer_a._sync.count == 1);
	wait_for_data(&peer_b);
	// Peer B will process Peer A's sync request and send a sync reply and resend it's own sync request.
	GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
	TEST(peer_b._sync.count == 1);
	GGPO_PeerToPeer_frame_end(&peer_a);
	GGPO_PeerToPeer_frame_end(&peer_b);
	time_now_ms++;

	// Do this for four more rounds so sync completes.
	for(int i = 2; i <= 5; i++) {
		printf("sync round %d\n", i);
		wait_for_data(&peer_a);
		GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
		TEST(peer_a._sync.count == i);
		wait_for_data(&peer_b);
		GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
		TEST(peer_b._sync.count == i);

		time_now_ms++;
	}
	// Both should now have finished synchronizing and be in the running state.
	TEST(peer_a.state == kGGPO_PeerToPeer_State_running);
	TEST(peer_b.state == kGGPO_PeerToPeer_State_running);

	struct GGPO_GameInputs peer_a_input = {};
	struct GGPO_GameInputs peer_b_input = {};
	peer_a_input.inputs[0] = 1;
	peer_b_input.inputs[1] = 1;

	// Frame 0.
	// Both peers will send their first frame of input to each other.
	TEST(GGPO_InputState_add_local_input(&peer_a.input, &peer_a_input));
	TEST(GGPO_InputState_add_local_input(&peer_b.input, &peer_b_input));
	GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
	wait_for_data(&peer_b);
	GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
	// Both peers have only sent their first input with nothing acked yet.
	TEST(peer_a.input._acked_frame == -1);
	TEST(peer_b.input._acked_frame == -1);
	struct GGPO_GameInputs total_inputs;
	int frame = 0;
	bool is_predicted;
	// Since Peer A ran first it will not have processed remote inputs yet.
	GGPO_InputState_get_input(&peer_a.input, frame, &total_inputs, &is_predicted);
	TEST(is_predicted);
	TEST(total_inputs.inputs[0] == 1);
	TEST(total_inputs.inputs[1] == 0);

	// Peer B will have both remote and local inputs.
	GGPO_InputState_get_input(&peer_b.input, frame, &total_inputs, &is_predicted);
	TEST(!is_predicted);
	TEST(total_inputs.inputs[0] == 1);
	TEST(total_inputs.inputs[1] == 1);

	GGPO_PeerToPeer_frame_end(&peer_a);
	GGPO_PeerToPeer_frame_end(&peer_b);

	// Frame 1.
	// Send another frame of inputs.
	time_now_ms++;
	peer_a_input.inputs[0] = 2;
	peer_b_input.inputs[1] = 2;
	TEST(GGPO_InputState_add_local_input(&peer_a.input, &peer_a_input));
	TEST(GGPO_InputState_add_local_input(&peer_b.input, &peer_b_input));
	wait_for_data(&peer_a);
	GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
	wait_for_data(&peer_b);
	GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
	// Because new inputs are sent before we process received messages we will not have received
	// an ack for our first inputs yet when we send these new inputs, hence last_acked_frame
	// is still -1.
	TEST(peer_a.input._acked_frame == -1);
	TEST(peer_b.input._acked_frame == -1);
	// Peer A should have frame 0 now.
	frame = 0;
	GGPO_InputState_get_input(&peer_a.input, frame, &total_inputs, &is_predicted);
	TEST(!is_predicted);
	TEST(total_inputs.inputs[0] == 1);
	TEST(total_inputs.inputs[1] == 1);
	frame++;
	// ...but Peer A needs to predict frame 1.
	GGPO_InputState_get_input(&peer_a.input, frame, &total_inputs, &is_predicted);
	TEST(is_predicted);
	TEST(total_inputs.inputs[0] == 2);
	TEST(total_inputs.inputs[1] == 1);
	// Peer B should have frame 1 from Peer A.
	GGPO_InputState_get_input(&peer_b.input, frame, &total_inputs, &is_predicted);
	TEST(!is_predicted);
	TEST(total_inputs.inputs[0] == 2);
	TEST(total_inputs.inputs[1] == 2);

	GGPO_PeerToPeer_frame_end(&peer_a);
	GGPO_PeerToPeer_frame_end(&peer_b);

	// Frame 2.
	// Skip forward a second to force both peers to send a quality report.
	time_now_ms += 1000; // 1 second.
	peer_a_input.inputs[0] = 3;
	peer_b_input.inputs[1] = 3;
	TEST(GGPO_InputState_add_local_input(&peer_a.input, &peer_a_input));
	TEST(GGPO_InputState_add_local_input(&peer_b.input, &peer_b_input));
	wait_for_data(&peer_a);
	GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
	wait_for_data(&peer_b);
	GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
	// We now finally receiving acks.
	TEST(peer_a.input._acked_frame == 0);
	TEST(peer_b.input._acked_frame == 0);
	// Peer B should have frame 3.
	GGPO_InputState_get_input(&peer_b.input, 2, &total_inputs, &is_predicted);
	TEST(!is_predicted);
	TEST(total_inputs.inputs[0] == 3);
	TEST(total_inputs.inputs[1] == 3);
	// They both should have sent their quality report and Peer B will have sent a quality reply.

	GGPO_PeerToPeer_frame_end(&peer_a);
	GGPO_PeerToPeer_frame_end(&peer_b);

	// Frame 3.
	// Step forward one last frame and we should get our quality replies.
	time_now_ms++;
	peer_a_input.inputs[0] = 4;
	peer_b_input.inputs[1] = 4;
	TEST(GGPO_InputState_add_local_input(&peer_a.input, &peer_a_input));
	TEST(GGPO_InputState_add_local_input(&peer_b.input, &peer_b_input));
	wait_for_data(&peer_a);
	GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
	wait_for_data(&peer_b);
	GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
	TEST(peer_a.input._acked_frame == 1);
	TEST(peer_b.input._acked_frame == 1);
	// Peer B should have frame 4.
	GGPO_InputState_get_input(&peer_b.input, 3, &total_inputs, &is_predicted);
	TEST(!is_predicted);
	TEST(total_inputs.inputs[0] == 4);
	TEST(total_inputs.inputs[1] == 4);
	// Ping time should only be 1 because we only advanced time 1ms since the last step.
	TEST(peer_b._running.ping_time_ms == 1);
	TEST(peer_a._running.ping_time_ms == 1);

	GGPO_PeerToPeer_frame_end(&peer_a);
	GGPO_PeerToPeer_frame_end(&peer_b);

	// Run Peer A a few times without running Peer B to generate a buildup of inputs sent to Peer B.
	wait_for_data(&peer_a);
	for (int i = 4; i < 10; i++) {
		time_now_ms++;
		peer_a_input.inputs[0] = i + 1;
		TEST(GGPO_InputState_add_local_input(&peer_a.input, &peer_a_input));
		GGPO_PeerToPeer_process_network(&peer_a, time_now_ms);
		// The API is expecting us to get inputs every frame.
		GGPO_InputState_get_input(&peer_b.input, i, &total_inputs, &is_predicted);
		GGPO_PeerToPeer_frame_end(&peer_a);
	}

	// Now do the same for Peer B and we should see the inputs from Peer A.
	wait_for_data(&peer_b);
	for (int i = 4; i < 10; i++) {
		printf("frame %d\n", i);
		time_now_ms++;
		peer_b_input.inputs[1] = i + 1;
		TEST(GGPO_InputState_add_local_input(&peer_b.input, &peer_b_input));
		GGPO_PeerToPeer_process_network(&peer_b, time_now_ms);
		GGPO_InputState_get_input(&peer_b.input, i, &total_inputs, &is_predicted);
		TEST(total_inputs.inputs[0] == i + 1);
		GGPO_PeerToPeer_frame_end(&peer_b);
	}

	GGPO_PeerToPeer_destroy(&peer_b);
	GGPO_PeerToPeer_destroy(&peer_a);

	printf("tests passed\n");
	return 0;
}