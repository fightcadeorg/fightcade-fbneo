#include "ggpo_client_to_server.h"
#include "ggpo_input.h"
#include "ggpo_buffers.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif


void GGPO_SockMsgReader_init(struct GGPO_SockMsgReader* msg_reader)
{
	assert(msg_reader);
	*msg_reader = (struct GGPO_SockMsgReader){};
}

void GGPO_SockMsgReader_destroy(struct GGPO_SockMsgReader* msg_reader)
{
	if (msg_reader->_buffer) {
		GGPO_Buffer_destroy(msg_reader->_buffer);
	}
}

struct GGPO_Buffer* GGPO_SockMsgReader_read(struct GGPO_SockMsgReader* msg_reader, GGPO_Socket sock)
{
	GGPO_ReadResult read_result;
	if (msg_reader->_buffer == NULL) {
		uint32_t msg_size_network;
		read_result = GGPO_Socket_read_all(sock, sizeof(msg_size_network), &msg_size_network);
		if (read_result != kGGPO_ReadResult_success) {
			return NULL;
		}
		size_t const msg_size = ntohl(msg_size_network);
		struct GGPO_Buffer* msg_buffer = GGPO_Buffer_create(msg_size);
		assert(msg_buffer);
		msg_reader->_buffer = msg_buffer;
		GGPO_BufferWriter_init(&msg_reader->_writer, msg_buffer);
		if (GGPO_Socket_has_data(sock) != kGGPO_ReadResult_success) {
			return NULL;
		}
	}
	size_t read_size = GGPO_BufferWriter_bytes_left(&msg_reader->_writer);
	read_result = GGPO_Socket_read(sock, &read_size, GGPO_BufferWriter_cursor(&msg_reader->_writer));
	if (read_result != kGGPO_ReadResult_success) {
		return NULL;
	}
	// This buffer size comes from the earlier uint32_t we read from the socket.
	assert(read_size <= UINT32_MAX);
	GGPO_BufferWriter_advance_cursor(&msg_reader->_writer, read_size);
	if (GGPO_BufferWriter_is_full(&msg_reader->_writer)) {
		struct GGPO_Buffer* result = msg_reader->_buffer;
		msg_reader->_buffer = NULL;
		return result;
	}
	return NULL;
}

void GGPO_SockMsgWriter_init(struct GGPO_SockMsgWriter* msg_writer)
{
	assert(msg_writer);
	*msg_writer = (struct GGPO_SockMsgWriter){};
}

void GGPO_SockMsgWriter_destroy(struct GGPO_SockMsgWriter* msg_writer)
{
	assert(msg_writer);
	if (msg_writer->_buffer) {
		GGPO_Buffer_destroy(msg_writer->_buffer);
	}
}

bool GGPO_SockMsgWriter_is_empty(struct GGPO_SockMsgWriter* msg_writer)
{
	assert(msg_writer);
	return msg_writer->_buffer == NULL || GGPO_BufferReader_is_empty(&msg_writer->_reader);
}

GGPO_ReadResult GGPO_SockMsgWriter_write(struct GGPO_SockMsgWriter* msg_writer, struct GGPO_BufferQueue* queue, GGPO_Socket sock)
{
	assert(msg_writer);
	assert(queue);
	assert(sock != GGPO_SOCKET_INVALID);
	GGPO_ReadResult read_result;
	if (msg_writer->_buffer == NULL) {
		msg_writer->_buffer = GGPO_BufferQueue_pop_front(queue);
		if (msg_writer->_buffer == NULL) {
			return kGGPO_ReadResult_wouldblock;
		}
		uint32_t msg_size = msg_writer->_buffer->size;
		uint32_t const msg_size_network = htonl(msg_size);
		bool success = GGPO_Socket_write_all(sock, sizeof(msg_size_network), &msg_size_network);
		if (!success) {
			// TODO: It would be better to put the buffer back but that needs a new BufferQueue function.
			GGPO_Buffer_destroy(msg_writer->_buffer);
			msg_writer->_buffer = NULL;
			return kGGPO_ReadResult_error;
		}
		GGPO_BufferReader_init(&msg_writer->_reader, msg_writer->_buffer);
	}
	size_t write_size = GGPO_BufferReader_bytes_left(&msg_writer->_reader);
	read_result = GGPO_Socket_write(sock, &write_size, GGPO_BufferReader_cursor(&msg_writer->_reader));
	if (read_result != kGGPO_ReadResult_success) {
		return read_result;
	}
	assert(write_size <= UINT32_MAX);
	GGPO_BufferReader_advance_cursor(&msg_writer->_reader, write_size);
	if (GGPO_BufferReader_is_empty(&msg_writer->_reader)) {
		GGPO_Buffer_destroy(msg_writer->_buffer);
		msg_writer->_buffer = NULL;
	}
	return kGGPO_ReadResult_success;
}

enum {
	kGGPO_TCPMsgClientKind_Version = 0,
	kGGPO_TCPMsgClientKind_Connect = 11,
	kGGPO_TCPMsgClientKind_RequestMatchInfo = 12,
	kGGPO_TCPMsgClientKind_Chat = 15,
	kGGPO_TCPMsgClientKind_ClientInputs = 17,
	kGGPO_TCPMsgClientKind_GameState = 18,
	kGGPO_TCPMsgClientKind_Recording = 19,
	kGGPO_TCPMsgClientKind_RequestSpectate = 20,
};

struct GGPO_TCPMsg_String {
	uint32_t length;
	char const* unowned_string;
};

static struct GGPO_TCPMsg_String GGPO_TCPMsg_String_create_(char const* string)
{
	assert(string);
	return (struct GGPO_TCPMsg_String){
		.length = strlen(string),
		.unowned_string = string,
	};
}

static uint32_t GGPO_TCPMsg_String_length_(struct GGPO_TCPMsg_String const* string)
{
	assert(string);
	return string->length + sizeof(string->length);
}

static const uint32_t kClientMsgHeaderLength = 2 * sizeof(uint32_t);

static void GGPO_BufferWriter_msg_header_(struct GGPO_BufferWriter* writer, uint32_t sequence, uint32_t kind)
{
	GGPO_BufferWriter_u32(writer, sequence);
	GGPO_BufferWriter_u32(writer, kind);
}

static void GGPO_BufferWriter_msg_string_(struct GGPO_BufferWriter* writer, struct GGPO_TCPMsg_String const* string)
{
	GGPO_BufferWriter_u32(writer, string->length);
	GGPO_BufferWriter_set(writer, string->length, string->unowned_string);
}

void GGPO_ClientToServer_send_msg_version(struct GGPO_ClientToServer* state)
{
	assert(state);
	uint32_t const zero = 0;
	uint32_t const version = 29;
	uint32_t const one = 1;
	uint32_t const length = kClientMsgHeaderLength + sizeof(zero) + sizeof(version) + sizeof(one);
	struct GGPO_Buffer* buffer = GGPO_Buffer_create(length);
	struct GGPO_BufferWriter writer;
	GGPO_BufferWriter_init(&writer, buffer);
	GGPO_BufferWriter_msg_header_(&writer, ++state->_sequence, kGGPO_TCPMsgClientKind_Version);
	GGPO_BufferWriter_u32(&writer, zero);
	GGPO_BufferWriter_u32(&writer, version);
	GGPO_BufferWriter_u32(&writer, one);
	assert(GGPO_BufferWriter_is_full(&writer));
	GGPO_BufferQueue_push_back(&state->outgoing_messages, buffer);
	state->expected_reply_sequence_ids.connect = state->_sequence;
}


void GGPO_ClientToServer_send_msg_connect(struct GGPO_ClientToServer* state, char const* match_id, uint32_t port)
{
	assert(state);
	assert(match_id);

	struct GGPO_TCPMsg_String msg_match_id = GGPO_TCPMsg_String_create_(match_id);
	uint32_t const length = kClientMsgHeaderLength + GGPO_TCPMsg_String_length_(&msg_match_id) + sizeof(port);
	struct GGPO_Buffer* buffer = GGPO_Buffer_create(length);
	struct GGPO_BufferWriter writer;
	GGPO_BufferWriter_init(&writer, buffer);
	GGPO_BufferWriter_msg_header_(&writer, ++state->_sequence, kGGPO_TCPMsgClientKind_Connect);
	GGPO_BufferWriter_msg_string_(&writer, &msg_match_id);
	GGPO_BufferWriter_u32(&writer, port);
	assert(GGPO_BufferWriter_is_full(&writer));
	GGPO_BufferQueue_push_back(&state->outgoing_messages, buffer);
}

void GGPO_ClientToServer_send_msg_request_match_info(struct GGPO_ClientToServer* state, char const* match_id)
{
	assert(state);
	assert(match_id);

	struct GGPO_TCPMsg_String msg_match_id = GGPO_TCPMsg_String_create_(match_id);
	uint32_t const length = kClientMsgHeaderLength + GGPO_TCPMsg_String_length_(&msg_match_id);
	struct GGPO_Buffer* buffer = GGPO_Buffer_create(length);
	struct GGPO_BufferWriter writer;
	GGPO_BufferWriter_init(&writer, buffer);
	GGPO_BufferWriter_msg_header_(&writer, ++state->_sequence, kGGPO_TCPMsgClientKind_RequestMatchInfo);
	GGPO_BufferWriter_msg_string_(&writer, &msg_match_id);
	assert(GGPO_BufferWriter_is_full(&writer));
	GGPO_BufferQueue_push_back(&state->outgoing_messages, buffer);
	state->expected_reply_sequence_ids.match_info = state->_sequence;
}

void GGPO_ClientToServer_send_msg_chat(struct GGPO_ClientToServer* state, char const* match_id, char const* text)
{
	assert(state);
	assert(match_id);
	assert(text);

	struct GGPO_TCPMsg_String msg_match_id = GGPO_TCPMsg_String_create_(match_id);
	struct GGPO_TCPMsg_String msg_text = GGPO_TCPMsg_String_create_(text);
	uint32_t const length = kClientMsgHeaderLength + GGPO_TCPMsg_String_length_(&msg_match_id) + GGPO_TCPMsg_String_length_(&msg_text);
	struct GGPO_Buffer* buffer = GGPO_Buffer_create(length);
	struct GGPO_BufferWriter writer;
	GGPO_BufferWriter_init(&writer, buffer);
	GGPO_BufferWriter_msg_header_(&writer, ++state->_sequence, kGGPO_TCPMsgClientKind_Chat);
	GGPO_BufferWriter_msg_string_(&writer, &msg_match_id);
	GGPO_BufferWriter_msg_string_(&writer, &msg_text);
	assert(GGPO_BufferWriter_is_full(&writer));
	GGPO_BufferQueue_push_back(&state->outgoing_messages, buffer);
}

void GGPO_ClientToServer_send_msg_inputs(struct GGPO_ClientToServer* state, char const* match_id, uint8_t const* inputs, uint32_t input_count, uint32_t input_size)
{
	assert(state);
	assert(match_id);
	assert(inputs);
	assert(input_count > 0);
	assert(input_size > 0);

	struct GGPO_TCPMsg_String const msg_match_id = GGPO_TCPMsg_String_create_(match_id);
	uint32_t const length = kClientMsgHeaderLength + GGPO_TCPMsg_String_length_(&msg_match_id) + sizeof(input_count) + sizeof(input_size) + input_count * input_size;
	struct GGPO_Buffer* buffer = GGPO_Buffer_create(length);
	struct GGPO_BufferWriter writer;
	GGPO_BufferWriter_init(&writer, buffer);
	GGPO_BufferWriter_msg_header_(&writer, ++state->_sequence, kGGPO_TCPMsgClientKind_ClientInputs);
	GGPO_BufferWriter_msg_string_(&writer, &msg_match_id);
	GGPO_BufferWriter_u32(&writer, input_count);
	GGPO_BufferWriter_u32(&writer, input_size);
	GGPO_BufferWriter_set(&writer, input_count * input_size, inputs);
	assert(GGPO_BufferWriter_is_full(&writer));
	GGPO_BufferQueue_push_back(&state->outgoing_messages, buffer);
}

void GGPO_ClientToServer_send_msg_game_state(struct GGPO_ClientToServer* state, char const* match_id, void const* game_state, uint32_t game_state_size)
{
	assert(state);
	assert(match_id);
	assert(game_state);
	assert(game_state_size > 0);

	uLongf compressed_state_size_z = compressBound(game_state_size);
	void* compressed_state = malloc(compressed_state_size_z);
	int compress_result = compress(compressed_state, &compressed_state_size_z, game_state, game_state_size);
	if (compress_result != Z_OK) {
		free(compressed_state);
		return;
	}
	assert(compressed_state_size_z <= UINT32_MAX);
	uint32_t const compressed_state_size = compressed_state_size_z;

	struct GGPO_TCPMsg_String msg_match_id = GGPO_TCPMsg_String_create_(match_id);
	uint32_t const length = kClientMsgHeaderLength + GGPO_TCPMsg_String_length_(&msg_match_id) + sizeof(compressed_state_size) + sizeof(game_state_size) + compressed_state_size;
	struct GGPO_Buffer* buffer = GGPO_Buffer_create(length);
	struct GGPO_BufferWriter writer;
	GGPO_BufferWriter_init(&writer, buffer);
	GGPO_BufferWriter_msg_header_(&writer, ++state->_sequence, kGGPO_TCPMsgClientKind_GameState);
	GGPO_BufferWriter_msg_string_(&writer, &msg_match_id);
	GGPO_BufferWriter_u32(&writer, compressed_state_size);
	GGPO_BufferWriter_u32(&writer, game_state_size);
	GGPO_BufferWriter_set(&writer, compressed_state_size, compressed_state);
	assert(GGPO_BufferWriter_is_full(&writer));
	GGPO_BufferQueue_push_back(&state->outgoing_messages, buffer);
	free(compressed_state);
}

void GGPO_ClientToServer_send_msg_spectate_request(struct GGPO_ClientToServer* state, char const* match_id)
{
	assert(state);
	assert(match_id);

	struct GGPO_TCPMsg_String msg_match_id = GGPO_TCPMsg_String_create_(match_id);
	uint32_t const length = kClientMsgHeaderLength + GGPO_TCPMsg_String_length_(&msg_match_id);
	struct GGPO_Buffer* buffer = GGPO_Buffer_create(length);
	struct GGPO_BufferWriter writer;
	GGPO_BufferWriter_init(&writer, buffer);
	GGPO_BufferWriter_msg_header_(&writer, ++state->_sequence, kGGPO_TCPMsgClientKind_RequestSpectate);
	GGPO_BufferWriter_msg_string_(&writer, &msg_match_id);
	assert(GGPO_BufferWriter_is_full(&writer));
	GGPO_BufferQueue_push_back(&state->outgoing_messages, buffer);
}

bool GGPO_ClientToServer_init(struct GGPO_ClientToServer* state, char const* host, uint16_t port)
{
	assert(state);
	state->_sock = GGPO_Socket_tcp_connect(host, port);
	if (state->_sock == GGPO_SOCKET_INVALID) {
		return false;
	}
	GGPO_SockMsgWriter_init(&state->outgoing_writer);
	GGPO_SockMsgReader_init(&state->incoming_reader);
	GGPO_BufferQueue_init(&state->outgoing_messages);
	GGPO_BufferQueue_init(&state->incoming_messages);
	return true;
}

bool GGPO_ClientToServer_destroy(struct GGPO_ClientToServer* state)
{
	assert(state);
	GGPO_Socket_destroy(state->_sock);
	GGPO_SockMsgWriter_destroy(&state->outgoing_writer);
	GGPO_SockMsgReader_destroy(&state->incoming_reader);
	GGPO_BufferQueue_destroy(&state->outgoing_messages);
	GGPO_BufferQueue_destroy(&state->incoming_messages);
	return true;
}

static void GGPO_ClientToServer_process_match_start_(struct GGPO_BufferReader* reader, struct GGPO_ClientToServer_Msg* result_msg)
{
	*result_msg = (struct GGPO_ClientToServer_Msg) {
		.type = kGGPO_ClientToServer_Msg_MatchStart,
		.data.match_start = {
			.peer_address = GGPO_BufferReader_string(reader),
			.peer_port = GGPO_BufferReader_u32(reader),
			.player_num = GGPO_BufferReader_u32(reader),
		},
	};
}

static void GGPO_ClientToServer_process_chat_(struct GGPO_BufferReader* reader, struct GGPO_ClientToServer_Msg* result_msg)
{
	*result_msg = (struct GGPO_ClientToServer_Msg) {
		.type = kGGPO_ClientToServer_Msg_Chat,
		.data.chat = {
			.match_id = GGPO_BufferReader_string(reader),
			.player_id = GGPO_BufferReader_string(reader),
			.text = GGPO_BufferReader_string(reader),
		},
	};
}

static void GGPO_ClientToServer_process_spectator_count_changed_(struct GGPO_BufferReader* reader, struct GGPO_ClientToServer_Msg* result_msg)
{
	*result_msg = (struct GGPO_ClientToServer_Msg) {
		.type = kGGPO_ClientToServer_Msg_SpectatorCountChanged,
		.data.spectator_count_changed = {
			.spectator_count = GGPO_BufferReader_u32(reader),
		},
	};
}

static void GGPO_ClientToServer_process_game_state_(struct GGPO_BufferReader* reader, struct GGPO_ClientToServer_Msg* result_msg)
{
	uint32_t const uncompressed_size = GGPO_BufferReader_u32(reader);
	struct GGPO_Buffer* uncompressed_buffer = GGPO_Buffer_create(uncompressed_size);
	uLongf uncompressed_size_z = uncompressed_size;
	int const decompress_result = uncompress((unsigned char*)uncompressed_buffer->buffer, &uncompressed_size_z, (unsigned char*)reader->cursor, GGPO_BufferReader_bytes_left(reader));
	if (decompress_result != Z_OK) {
		GGPO_Buffer_destroy(uncompressed_buffer);
		*result_msg = (struct GGPO_ClientToServer_Msg) {
			.type = kGGPO_ClientToServer_Msg_None,
		};
	} else {
		assert(uncompressed_size == uncompressed_size_z);
		*result_msg = (struct GGPO_ClientToServer_Msg) {
			.type = kGGPO_ClientToServer_Msg_GameState,
			.data.game_state = {
				.buffer = uncompressed_buffer,
			},
		};
	}
}

void GGPO_GameInputsQueue_init(struct GGPO_GameInputsQueue* queue)
{
	assert(queue);
	*queue = (struct GGPO_GameInputsQueue){};
	GGPO_BufferQueue_init(&queue->_buffer_queue);
}

void GGPO_GameInputsQueue_destroy(struct GGPO_GameInputsQueue* queue)
{
	assert(queue);
	if (queue->_current_buffer) {
		GGPO_Buffer_destroy(queue->_current_buffer);
	}
	GGPO_BufferQueue_destroy(&queue->_buffer_queue);
}

static void GGPO_GameInputsQueue_start_buffer_(struct GGPO_GameInputsQueue* queue, struct GGPO_Buffer* buffer)
{
	assert(queue);
	assert(buffer);
	assert(queue->_current_buffer == NULL);
	queue->_current_buffer = buffer;
	GGPO_BufferReader_init(&queue->_reader, buffer);
	// Skip the 'Kind' field.
	(void)GGPO_BufferReader_u32(&queue->_reader);
	queue->_current.size = GGPO_BufferReader_u32(&queue->_reader);
	queue->_current.count = GGPO_BufferReader_u32(&queue->_reader);
	queue->_current.index = 0;
}

void GGPO_GameInputsQueue_give_inputs_msg(struct GGPO_GameInputsQueue* queue, struct GGPO_ClientToServer_Msg* msg)
{
	assert(msg->type == kGGPO_ClientToServer_Msg_Inputs);
	if (queue->_current_buffer == NULL) {
		GGPO_GameInputsQueue_start_buffer_(queue, msg->data.inputs.buffer);
	} else {
		GGPO_BufferQueue_push_back(&queue->_buffer_queue, msg->data.inputs.buffer);
	}
	msg->data.inputs.buffer = NULL;
}

static void GGPO_GameInputsQueue_next_buffer_(struct GGPO_GameInputsQueue* queue)
{
	assert(queue);
	assert(queue->_current_buffer == NULL);
	struct GGPO_Buffer* buffer = GGPO_BufferQueue_pop_front(&queue->_buffer_queue);
	if (buffer) {
		GGPO_GameInputsQueue_start_buffer_(queue, buffer);
	}
}

bool GGPO_GameInputsQueue_get_input(struct GGPO_GameInputsQueue* queue, struct GGPO_GameInputs* inputs)
{
	assert(queue);
	assert(inputs);
	if (queue->_current_buffer == NULL) {
		return false;
	}
	GGPO_BufferReader_get(&queue->_reader, queue->_current.size, inputs->inputs);
	queue->_current.index++;
	if (0 == GGPO_BufferReader_bytes_left(&queue->_reader)) {
		assert(queue->_current.index == queue->_current.count);
		GGPO_Buffer_destroy(queue->_current_buffer);
		queue->_current_buffer = NULL;
		GGPO_GameInputsQueue_next_buffer_(queue);
	}
	return true;
}

enum {
	kGGPO_TCPMsgServerKind_MatchStart = -7,
	kGGPO_TCPMsgServerKind_Chat = -8,
	kGGPO_TCPMsgServerKind_DisconnectedPeer = -9,
	kGGPO_TCPMsgServerKind_SpectatorCountChanged = -10,
	kGGPO_TCPMsgServerKind_StateRequest = -11,
	kGGPO_TCPMsgServerKind_GameState = -12,
	kGGPO_TCPMsgServerKind_InputsState = -13,
};

static void GGPO_ClientToServer_process_and_consume_msg_buffer_(struct GGPO_ClientToServer* state, struct GGPO_Buffer* buffer, struct GGPO_ClientToServer_Msg* result_msg)
{
	struct GGPO_BufferReader reader;
	GGPO_BufferReader_init(&reader, buffer);
	int32_t const kind = GGPO_BufferReader_i32(&reader);
	if (kind < 0) {
		switch (kind) {
		case kGGPO_TCPMsgServerKind_MatchStart:
			GGPO_ClientToServer_process_match_start_(&reader, result_msg);
			GGPO_Buffer_destroy(buffer);
			return;
		case kGGPO_TCPMsgServerKind_Chat:
			GGPO_ClientToServer_process_chat_(&reader, result_msg);
			GGPO_Buffer_destroy(buffer);
			return;
		case kGGPO_TCPMsgServerKind_DisconnectedPeer:
			*result_msg = (struct GGPO_ClientToServer_Msg) {
				.type = kGGPO_ClientToServer_Msg_DisconnectedPeer,
			};
			GGPO_Buffer_destroy(buffer);
			return;
		case kGGPO_TCPMsgServerKind_SpectatorCountChanged:
			GGPO_ClientToServer_process_spectator_count_changed_(&reader, result_msg);
			GGPO_Buffer_destroy(buffer);
			return;
		case kGGPO_TCPMsgServerKind_StateRequest:
			*result_msg = (struct GGPO_ClientToServer_Msg) {
				.type = kGGPO_ClientToServer_Msg_StateRequest,
			};
			GGPO_Buffer_destroy(buffer);
			return;
		case kGGPO_TCPMsgServerKind_GameState:
			GGPO_ClientToServer_process_game_state_(&reader, result_msg);
			GGPO_Buffer_destroy(buffer);
			return;
		case kGGPO_TCPMsgServerKind_InputsState:
			*result_msg = (struct GGPO_ClientToServer_Msg) {
				.type = kGGPO_ClientToServer_Msg_Inputs,
				.data.inputs = {
					.buffer = buffer,
				},
			};
			return;
		}
	} else {
		// These are replies to messages we sent earlier.
		uint32_t const sequence_reply = kind;
		// Since we only care about the value being zero or not we don't bother byteswapping.
		uint32_t error = GGPO_BufferReader_u32(&reader);
		if (error == 0) {
			if (sequence_reply == state->expected_reply_sequence_ids.connect) {
				state->expected_reply_sequence_ids.connect = UINT32_MAX;
				*result_msg = (struct GGPO_ClientToServer_Msg){
					.type = kGGPO_ClientToServer_Msg_Connected,
				};
				GGPO_Buffer_destroy(buffer);
				return;
			} else if (sequence_reply == state->expected_reply_sequence_ids.match_info) {
				state->expected_reply_sequence_ids.match_info = UINT32_MAX;
				*result_msg = (struct GGPO_ClientToServer_Msg){
					.type = kGGPO_ClientToServer_Msg_MatchInfo,
					.data.match_info = {
						.player_one = GGPO_BufferReader_string(&reader),
						.player_two = GGPO_BufferReader_string(&reader),
						.blurb = GGPO_BufferReader_string(&reader),
						.spectator_count = GGPO_BufferReader_u32(&reader),
					},
				};
				GGPO_Buffer_destroy(buffer);
				return;
			}
		}
	}
	*result_msg = (struct GGPO_ClientToServer_Msg) {
		.type = kGGPO_ClientToServer_Msg_None,
	};
	GGPO_Buffer_destroy(buffer);
}

bool GGPO_ClientToServer_read_messages(struct GGPO_ClientToServer* state)
{
	assert(state);
	GGPO_ReadResult read_result = GGPO_Socket_has_data(state->_sock);
	while (read_result == kGGPO_ReadResult_success) {
		struct GGPO_Buffer* msg_buffer = GGPO_SockMsgReader_read(&state->incoming_reader, state->_sock);
		if (msg_buffer == NULL) {
			break;
		}
		GGPO_BufferQueue_push_back(&state->incoming_messages, msg_buffer);
		read_result = GGPO_Socket_has_data(state->_sock);
	}
	return read_result != kGGPO_ReadResult_error;
}

bool GGPO_ClientToServer_write_messages(struct GGPO_ClientToServer* state)
{
	assert(state);
	GGPO_ReadResult read_result = GGPO_Socket_has_space(state->_sock);
	while (read_result == kGGPO_ReadResult_success) {
		read_result = GGPO_SockMsgWriter_write(&state->outgoing_writer, &state->outgoing_messages, state->_sock);
		if (read_result == kGGPO_ReadResult_error) {
			return false;
		}
	}
	return true;
}

struct GGPO_ClientToServer_Msg GGPO_ClientToServer_pop_next_msg(struct GGPO_ClientToServer* state)
{
	struct GGPO_ClientToServer_Msg result_msg;
	do {
		struct GGPO_Buffer* buffer = GGPO_BufferQueue_pop_front(&state->incoming_messages);
		if (buffer == NULL) {
			return (struct GGPO_ClientToServer_Msg) {
				.type = kGGPO_ClientToServer_Msg_None,
			};
		}
		GGPO_ClientToServer_process_and_consume_msg_buffer_(state, buffer, &result_msg);
		// Skip over any messages that failed to decode.
	} while (result_msg.type == kGGPO_ClientToServer_Msg_None);
	return result_msg;
}

bool GGPO_ClientToServer_Msg_is_valid(struct GGPO_ClientToServer_Msg* msg)
{
	return msg->type != kGGPO_ClientToServer_Msg_None;
}

void GGPO_ClientToServer_Msg_destroy(struct GGPO_ClientToServer_Msg* msg)
{
	assert(msg);
	switch (msg->type) {
	case kGGPO_ClientToServer_Msg_MatchInfo:
		free(msg->data.match_info.player_one);
		free(msg->data.match_info.player_two);
		free(msg->data.match_info.blurb);
		break;
	case kGGPO_ClientToServer_Msg_MatchStart:
		free(msg->data.match_start.peer_address);
		break;
	case kGGPO_ClientToServer_Msg_Chat:
		free(msg->data.chat.match_id);
		free(msg->data.chat.player_id);
		free(msg->data.chat.text);
		break;
	case kGGPO_ClientToServer_Msg_GameState:
		if (msg->data.game_state.buffer) {
			GGPO_Buffer_destroy(msg->data.game_state.buffer);
		}
		break;
	case kGGPO_ClientToServer_Msg_Inputs:
		if (msg->data.inputs.buffer) {
			GGPO_Buffer_destroy(msg->data.inputs.buffer);
		}
		break;
	default:
		break;
	};
}
