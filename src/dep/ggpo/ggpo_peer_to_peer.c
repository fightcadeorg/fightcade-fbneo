#include "ggpo_peer_to_peer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ggpo_logging.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#ifdef _MSC_VER
#define GGPO_STRUCT_PACKED
#define GGPO_STRUCT_PACKED_BEGIN __pragma(pack(push,1))
#define GGPO_STRUCT_PACKED_END __pragma(pack(pop))
#else
#define GGPO_STRUCT_PACKED __attribute__((packed))
#define GGPO_STRUCT_PACKED_BEGIN
#define GGPO_STRUCT_PACKED_END
#endif

enum {
	kUDPMsgKindInvalid = 0,
	kUDPMsgKindSyncRequest = 1,
	kUDPMsgKindSyncReply = 2,
	kUDPMsgKindInput = 3,
	kUDPMsgKindQualityReport = 4,
	kUDPMsgKindQualityReply = 5,
};

GGPO_STRUCT_PACKED_BEGIN
struct GGPO_UDPMsg {
	uint8_t kind;
	union {
		struct {
			uint16_t random;
			uint16_t reserved;
		} sync_request;
		struct {
			uint16_t random;
			uint16_t reserved;
		} sync_reply;
		struct {
			int32_t frame;
			int32_t acked_frame;
			uint16_t num_bits;
			uint8_t num_bytes;
			// Based on input compression scheme, the worst case space required is...
			// 10 bytes per player of input data = 10 * 8 = 80 bits of input data per player.
			// Encoding a single changed bit takes:
			//   - 1 bit for marking the change.
			//   - 1 bit for marking the value.
			//   - 8 bits for marking the position.
			// Takes 10 bits.
			// If every input bit changes in a single frame it would take...
			//   80 bits of input data * 10 bits = 800 bits + 1 bit to mark end of frame data = 801 / 8 = ~100 bytes per frame.
			// 
			uint8_t input_data[1024];
		} input;
		struct {
			int8_t frame_advantage;
			uint32_t time_ms;
		} quality_report;
		struct {
			uint32_t time_ms;
		} quality_reply;
	} data;
} GGPO_STRUCT_PACKED;
GGPO_STRUCT_PACKED_END

bool GGPO_PeerToPeer_init(struct GGPO_PeerToPeer* state, uint16_t recv_port)
{
	*state = (struct GGPO_PeerToPeer) {
		.state = kGGPO_PeerToPeer_State_initialized,
		._send_sock = GGPO_SOCKET_INVALID,
		._recv_sock = GGPO_SOCKET_INVALID,
	};
	state->_recv_sock = GGPO_Socket_udp_create();
	if (state->_recv_sock == GGPO_SOCKET_INVALID) {
		GGPO_PeerToPeer_destroy(state);
		return false;
	}
	bool success = false;
	for (int i = 0; i < 10 && !success; i++, recv_port++) {
		success = GGPO_Socket_bind(state->_recv_sock, recv_port);
	}
	recv_port--;
	state->_listen_port = recv_port;
	if (!GGPO_Socket_make_nonblocking(state->_recv_sock)) {
		GGPO_PeerToPeer_destroy(state);
		return false;
	}

	GGPO_InputState_init(&state->input);

	return true;
}

uint16_t GGPO_PeerToPeer_get_listen_port(struct GGPO_PeerToPeer const* state)
{
	return state->_listen_port;
}

bool GGPO_PeerToPeer_connect_to_peer(struct GGPO_PeerToPeer* state, char const* peer_host, uint16_t peer_port)
{
	state->_peer_address = GGPO_PlatformAddress_init(peer_host, peer_port);
	if (!GGPO_PlatformAddress_is_valid(&state->_peer_address)) {
		return false;
	}
	state->_send_sock = GGPO_Socket_udp_create();
	assert(state->_send_sock != GGPO_SOCKET_INVALID);
	return true;
}

void GGPO_PeerToPeer_set_frame(struct GGPO_PeerToPeer* state, int frame)
{
	assert(state);
	int remoteFrame = state->_running.last_received_frame + (state->_running.ping_time_ms * 60 / 1000);
	state->_running.local_frame_advantage = remoteFrame - frame;
}

void GGPO_PeerToPeer_get_stats(struct GGPO_PeerToPeer const* state, struct GGPO_PeerToPeer_Stats* out_stats)
{
	assert(state);
	assert(out_stats);

	*out_stats = (struct GGPO_PeerToPeer_Stats){
		.send_queue_len = state->_running.send_queue_len,
		.ping = state->_running.ping_time_ms,
		.local_frames_behind = state->_running.local_frame_advantage,
		.remote_frames_behind = state->_running.remote_frame_advantage,
	};
}

static size_t GGPO_UDPMsg_get_size_(struct GGPO_UDPMsg const* msg)
{
	switch (msg->kind) {
	case kUDPMsgKindSyncRequest: return sizeof(msg->data.sync_request) + 1;
	case kUDPMsgKindSyncReply: return sizeof(msg->data.sync_reply) + 1;
	case kUDPMsgKindInput: return sizeof(msg->data.input) - sizeof(msg->data.input.input_data) + msg->data.input.num_bytes + 1;
	case kUDPMsgKindQualityReport: return sizeof(msg->data.quality_report) + 1;
	case kUDPMsgKindQualityReply: return sizeof(msg->data.quality_reply) + 1;
	default: abort();
	}
}

static void send_msg_(struct GGPO_PeerToPeer* state, struct GGPO_UDPMsg const* msg)
{
	size_t const msg_size = GGPO_UDPMsg_get_size_(msg);
	GGPO_Socket_udp_sendto(state->_send_sock, &state->_peer_address, msg_size, msg);
}

static void send_sync_(struct GGPO_PeerToPeer* state, uint32_t now_ms)
{
	struct GGPO_UDPMsg msg = {
		.kind = kUDPMsgKindSyncRequest,
		.data.sync_request = {
			// It's a bad random number, but it suits our purpose of distinguishing each message.
			.random = state->_sync.count,
		},
	};
	send_msg_(state, &msg);
	uint32_t const resend_interval_ms = 3640;
	state->_sync.time_to_resend_sync_ms = now_ms + resend_interval_ms;
}

static void send_quality_report_(struct GGPO_PeerToPeer* state, uint32_t now_ms)
{
	struct GGPO_UDPMsg msg = {
		.kind = kUDPMsgKindQualityReport,
		.data.quality_report = {
			.frame_advantage = state->state == kGGPO_PeerToPeer_State_running ? state->_running.local_frame_advantage : 0,
			.time_ms = now_ms,
		}
	};
	send_msg_(state, &msg);
	state->_running.next_time_to_send_quality_report_ms = now_ms + 1000;
}

static void process_msg_sync_request_(struct GGPO_PeerToPeer* state, struct GGPO_UDPMsg const* in_msg)
{
	assert(in_msg->kind == kUDPMsgKindSyncRequest);
	struct GGPO_UDPMsg msg = {
		.kind = kUDPMsgKindSyncReply,
		.data.sync_reply = {
			.random = in_msg->data.sync_request.random,
		},
	};
	send_msg_(state, &msg);
}

static void process_msg_sync_reply_(struct GGPO_PeerToPeer* state, uint32_t now_ms, struct GGPO_UDPMsg const* msg)
{
	assert(msg->kind == kUDPMsgKindSyncReply);
	if (msg->data.sync_reply.random != state->_sync.count) {
		// Ignore any sync reply that isn't the one we're waiting for.
		return;
	}
	state->_sync.count++;
	if (state->_sync.count == 5) {
		state->state = kGGPO_PeerToPeer_State_running;
		state->_running.next_time_to_send_quality_report_ms = now_ms + 1000;
		state->_running.last_received_frame = -1;
	} else {
		send_sync_(state, now_ms);
	}
}

static void log_inputs_(int frame, struct GGPO_GameInputs const* inputs)
{
	int saved_indent = ggpo_logging_indentation;
	ggpo_logging_indentation = 0;
	gglog("[%d]: ", frame);
	for(int i = 0; i < ARRAY_LEN(inputs->inputs); i++) {
		gglog("%02x ", inputs->inputs[i]);
	}
	gglog("\n");
	ggpo_logging_indentation = saved_indent;
}

static bool get_bit_(uint8_t const* bits, int bit_i)
{
	int const byte_i = bit_i / 8;
	int const byte_bit_i = bit_i % 8;
	return bits[byte_i] & (1 << byte_bit_i);
}

static void set_bit_(uint8_t* bits, int bit_i)
{
	int const byte_i = bit_i / 8;
	int const byte_bit_i = bit_i % 8;
	bits[byte_i] |= 1 << byte_bit_i;
}

static void clear_bit_(uint8_t* bits, int bit_i)
{
	int const byte_i = bit_i / 8;
	int const byte_bit_i = bit_i % 8;
	bits[byte_i] &= ~(1 << byte_bit_i);
}

static uint8_t get_byte_(uint8_t const* bits, int bit_i)
{
	uint8_t byte = 0;
	for (size_t i = 0; i < 8; i++) {
		if (get_bit_(bits, bit_i + i)) {
			set_bit_(&byte, i);
		}
	}
	return byte;
}

static void set_byte_(uint8_t* bits, int bit_i, uint8_t byte)
{
	for (size_t i = 0; i < 8; i++) {
		if ((1 << i) & byte) {
			set_bit_(bits, bit_i + i);
		} else {
			clear_bit_(bits, bit_i + i);
		}
	}
}

static void process_msg_input_(struct GGPO_PeerToPeer* state, struct GGPO_UDPMsg const* msg)
{
	assert(msg->kind == kUDPMsgKindInput);
	if (state->_running.last_received_frame == -1) {
		state->_running.last_received_frame = msg->data.input.frame - 1;
	}
	// Decompress input bits and add any inputs we haven't seen yet.
	int bit_i = 0;
	int const num_bits = msg->data.input.num_bits;
	if (num_bits > (int)sizeof(state->_running.last_received_input.inputs) * 8) {
		// If the message is malformed we will ignore it.
		gglog("Too many bits (%d) claimed in input message.\n", num_bits);
		return;
	}
	int current_input_frame = msg->data.input.frame;
	int desired_frame = state->_running.last_received_frame + 1;
	assert(desired_frame >= msg->data.input.frame);
	uint8_t const* input = msg->data.input.input_data;

	gglog("decompress_inputs\n");
	struct GGPO_GameInputs last_received_input = state->_running.last_received_input;
	while (bit_i < num_bits) {
		bool use_frame = current_input_frame == desired_frame;
		bool bit;
		while (bit = get_bit_(input, bit_i++), bit) {
			if (bit_i + 9 >= num_bits) {
				// We need enough bits for the bit value (1 bit), but bit position (8 bits)
				// and then one more bit for the next get_bit_ in the while condition.
				// There isn't enough space in the data to read out the rest
				// so we will ignore this input message.
				gglog("Ignoring bad input (overflowed bits).\n");
				return;
			}
			bool bit_value = get_bit_(input, bit_i++);
			int bit_position = get_byte_(input, bit_i);
			bit_i += 8;
			if (use_frame) {
				if (bit_value) {
					set_bit_(last_received_input.inputs, bit_position);
				} else {
					clear_bit_(last_received_input.inputs, bit_position);
				}
			}
		}
		state->_running.last_received_input = last_received_input;
		log_inputs_(current_input_frame, &state->_running.last_received_input);
		assert(bit_i <= num_bits);
		if (use_frame) {
			state->_running.last_received_frame = current_input_frame;
			desired_frame++;
			GGPO_InputState_add_remote_input(&state->input, current_input_frame, &state->_running.last_received_input);
		}
		current_input_frame++;
	}
	if (msg->data.input.acked_frame >= 0) {
		int success = GGPO_InputState_get_local_input(&state->input, msg->data.input.acked_frame, &state->_running.last_acked_input);
		assert(success);
		GGPO_InputState_ack_local_input(&state->input, msg->data.input.acked_frame);
	}
}

static void process_msg_quality_report_(struct GGPO_PeerToPeer* state, struct GGPO_UDPMsg const* in_msg)
{
	assert(in_msg->kind == kUDPMsgKindQualityReport);
	state->_running.remote_frame_advantage = in_msg->data.quality_report.frame_advantage;
	struct GGPO_UDPMsg msg = {
		.kind = kUDPMsgKindQualityReply,
		.data.quality_reply = {
			.time_ms = in_msg->data.quality_report.time_ms,
		},
	};
	send_msg_(state, &msg);
}

static void process_msg_quality_reply_(struct GGPO_PeerToPeer* state, uint32_t now_ms, struct GGPO_UDPMsg const* msg)
{
	assert(msg->kind == kUDPMsgKindQualityReply);
	if (msg->data.quality_reply.time_ms > now_ms) {
		return;
	}
	state->_running.ping_time_ms = now_ms - msg->data.quality_reply.time_ms;
}

static void process_message_(struct GGPO_PeerToPeer* state, uint32_t now_ms, struct GGPO_UDPMsg const* msg)
{
	switch (msg->kind) {
	case kUDPMsgKindSyncRequest: process_msg_sync_request_(state, msg); break;
	case kUDPMsgKindSyncReply: process_msg_sync_reply_(state, now_ms, msg); break;
	case kUDPMsgKindInput: process_msg_input_(state, msg); break;
	case kUDPMsgKindQualityReport: process_msg_quality_report_(state, msg); break;
	case kUDPMsgKindQualityReply: process_msg_quality_reply_(state, now_ms, msg); break;
	default: break; // Ignore any invalid messages.
	}
}

static void process_messages_(struct GGPO_PeerToPeer* state, uint32_t now_ms)
{
	GGPO_ReadResult result;
	struct GGPO_UDPMsg msg;
	union GGPO_PlatformAddress peer_address;
	// Read messages until we get an error or we would block.
	while(true) {
		size_t msg_size = sizeof(msg);
		// TODO: Return the bytes read from recvfrom call as inout param so the caller can validate the message fits.
		// TODO: Reject messages not from our peer. The port doesn't matter but the IP should match.
		result = GGPO_Socket_udp_recvfrom(state->_recv_sock, &peer_address, msg_size, &msg);
		if (result != kGGPO_ReadResult_success) {
			break;
		}
		process_message_(state, now_ms, &msg);
	}
}

static void send_inputs_(struct GGPO_PeerToPeer* state)
{
	struct GGPO_GameInputs queued_inputs[20];
	size_t queued_input_count = ARRAY_LEN(queued_inputs);
	int start_frame;
	bool success = GGPO_InputState_get_all_unacked_local_inputs(&state->input, &start_frame, queued_inputs, &queued_input_count);
	if (!success) {
		return;
	}
	state->_running.send_queue_len = queued_input_count;
	// Compress any un-acked inputs to be sent.
	struct GGPO_UDPMsg msg = {
		.kind = kUDPMsgKindInput,
		.data.input = {
			.acked_frame = state->_running.last_received_frame,
			.frame = start_frame,
		},
	};
	gglog("compress_inputs\n");
	size_t bit_i = 0;
	uint8_t* compressed_input = msg.data.input.input_data;
	struct GGPO_GameInputs const* last_input = &state->_running.last_acked_input;
	for (size_t input_i = 0; input_i < queued_input_count; input_i++) {
		struct GGPO_GameInputs const* current_input = &queued_inputs[input_i];
		log_inputs_(start_frame + input_i, current_input);
		if (0 != memcmp(current_input->inputs, last_input->inputs, sizeof(current_input->inputs))) {
			for (int current_bit_i = 0; current_bit_i < 8 * ARRAY_LEN(current_input->inputs); current_bit_i++) {
				bool last_bit_value = get_bit_(last_input->inputs, current_bit_i);
				bool current_bit_value = get_bit_(current_input->inputs, current_bit_i);
				if (last_bit_value != current_bit_value) {
					set_bit_(compressed_input, bit_i++);
					if (current_bit_value) {
						set_bit_(compressed_input, bit_i++);
					} else {
						clear_bit_(compressed_input, bit_i++);
					}
					set_byte_(compressed_input, bit_i, current_bit_i);
					bit_i += 8;
				}
			}
		}
		clear_bit_(compressed_input, bit_i++);
		last_input = current_input;
	}
	msg.data.input.num_bits = bit_i;
	msg.data.input.num_bytes = (bit_i + 8 - 1) / 8;
	send_msg_(state, &msg);
}

static void GGPO_PeerToPeer_process_network_(struct GGPO_PeerToPeer* state, uint32_t now_ms)
{
	if (state->state == kGGPO_PeerToPeer_State_initialized) {
		send_sync_(state, now_ms);
		state->state = kGGPO_PeerToPeer_State_syncing;
	} else if (state->state == kGGPO_PeerToPeer_State_syncing) {
		if (now_ms >= state->_sync.time_to_resend_sync_ms) {
			// Our last symc message probably got dropped, so send another.
			send_sync_(state, now_ms);
		}
	} else if (state->state == kGGPO_PeerToPeer_State_running) {
		// We're in the running state. Just send inputs and quality reports.
		//If we're past syncing and it's been more than a second since the last one, send a quality report.
		if (now_ms >= state->_running.next_time_to_send_quality_report_ms) {
			send_quality_report_(state, now_ms);
		}
		send_inputs_(state);
	}
	process_messages_(state, now_ms);
}

int GGPO_PeerToPeer_process_network(struct GGPO_PeerToPeer* state, uint32_t now_ms)
{
	if (state->_send_sock == GGPO_SOCKET_INVALID) {
		return kGGPO_PeerToPeer_StateTransition_none;
	}

	int const old_state	= state->state;
	int const old_sync_count = state->_sync.count;
	GGPO_PeerToPeer_process_network_(state, now_ms);
	int const new_state = state->state;

	int const new_sync_count = state->_sync.count;
	if (old_state == new_state && old_sync_count == new_sync_count) {
		return kGGPO_PeerToPeer_StateTransition_none;
	} else if (new_state == kGGPO_PeerToPeer_State_syncing) {
		
		if (old_sync_count == new_sync_count) {
			return kGGPO_PeerToPeer_StateTransition_none;
		}
		switch (new_sync_count) {
		case 0:
			return kGGPO_PeerToPeer_StateTransition_connected_to_peer;
		case 1:
			return kGGPO_PeerToPeer_StateTransition_syncing_1;
		case 2:
			return kGGPO_PeerToPeer_StateTransition_syncing_2;
		case 3:
			return kGGPO_PeerToPeer_StateTransition_syncing_3;
		case 4:
			return kGGPO_PeerToPeer_StateTransition_syncing_4;
		default:
			return kGGPO_PeerToPeer_StateTransition_none;
		}
	} else if (new_state == kGGPO_PeerToPeer_State_running) {
		return kGGPO_PeerToPeer_StateTransition_running;
	}

	return kGGPO_PeerToPeer_StateTransition_none;
}

void GGPO_PeerToPeer_frame_end(struct GGPO_PeerToPeer* state)
{
	if (state->state == kGGPO_PeerToPeer_State_running) {
		GGPO_InputState_frame_end(&state->input);
	}
}

void GGPO_PeerToPeer_destroy(struct GGPO_PeerToPeer* state)
{
	assert(state);
	GGPO_Socket_destroy(state->_recv_sock);
	GGPO_Socket_destroy(state->_send_sock);
	*state = (struct GGPO_PeerToPeer) {
	};
}