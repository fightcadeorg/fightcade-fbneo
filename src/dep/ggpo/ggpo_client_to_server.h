#ifndef GGPO_CLIENT_TO_SERVER_H__
#define GGPO_CLIENT_TO_SERVER_H__

#include <stdint.h>
#include "ggpo_network.h"
#include "ggpo_buffers.h"
#include "ggpo_input.h"

struct GGPO_SockMsgReader {
	struct GGPO_Buffer* _buffer;
	struct GGPO_BufferWriter _writer;
};

void GGPO_SockMsgReader_init(struct GGPO_SockMsgReader* msg_reader);
void GGPO_SockMsgReader_destroy(struct GGPO_SockMsgReader* msg_reader);
struct GGPO_Buffer* GGPO_SockMsgReader_read(struct GGPO_SockMsgReader* msg_reader, GGPO_Socket sock);

struct GGPO_SockMsgWriter {
	struct GGPO_Buffer* _buffer;
	struct GGPO_BufferReader _reader;
};

void GGPO_SockMsgWriter_init(struct GGPO_SockMsgWriter* msg_writer);
void GGPO_SockMsgWriter_destroy(struct GGPO_SockMsgWriter* msg_writer);
bool GGPO_SockMsgWriter_is_empty(struct GGPO_SockMsgWriter* msg_writer);
GGPO_ReadResult GGPO_SockMsgWriter_write(struct GGPO_SockMsgWriter* msg_writer, struct GGPO_BufferQueue* queue, GGPO_Socket sock);

struct GGPO_ClientToServer {
	GGPO_Socket _sock;
	uint32_t _sequence;
	struct {
		uint32_t connect;
		uint32_t match_info;
	} expected_reply_sequence_ids;
	struct GGPO_SockMsgWriter outgoing_writer;
	struct GGPO_SockMsgReader incoming_reader;
	struct GGPO_BufferQueue outgoing_messages;
	struct GGPO_BufferQueue incoming_messages;
};

bool GGPO_ClientToServer_init(struct GGPO_ClientToServer* state, char const* host, uint16_t port);
bool GGPO_ClientToServer_destroy(struct GGPO_ClientToServer* state);
bool GGPO_ClientToServer_read_messages(struct GGPO_ClientToServer* state);
bool GGPO_ClientToServer_write_messages(struct GGPO_ClientToServer* state);
void GGPO_ClientToServer_send_msg_version(struct GGPO_ClientToServer* state);
void GGPO_ClientToServer_send_msg_connect(struct GGPO_ClientToServer* state, char const* match_id, uint32_t port);
void GGPO_ClientToServer_send_msg_request_match_info(struct GGPO_ClientToServer* state, char const* match_id);
void GGPO_ClientToServer_send_msg_chat(struct GGPO_ClientToServer* state, char const* match_id, char const* text);
void GGPO_ClientToServer_send_msg_inputs(struct GGPO_ClientToServer* state, char const* match_id, uint8_t const* inputs, uint32_t input_count, uint32_t input_size);
void GGPO_ClientToServer_send_msg_game_state(struct GGPO_ClientToServer* state, char const* match_id, void const* game_state, uint32_t game_state_size);
void GGPO_ClientToServer_send_msg_spectate_request(struct GGPO_ClientToServer* state, char const* match_id);

enum {
	kGGPO_ClientToServer_Msg_None,
	kGGPO_ClientToServer_Msg_Connected,
	kGGPO_ClientToServer_Msg_MatchInfo,
	kGGPO_ClientToServer_Msg_MatchStart,
	kGGPO_ClientToServer_Msg_Chat,
	kGGPO_ClientToServer_Msg_DisconnectedPeer,
	kGGPO_ClientToServer_Msg_SpectatorCountChanged,
	kGGPO_ClientToServer_Msg_StateRequest,
	kGGPO_ClientToServer_Msg_GameState,
	kGGPO_ClientToServer_Msg_Inputs,
};

struct GGPO_ClientToServer_Msg {
	int type;
	union {
		struct {
			char* player_one;
			char* player_two;
			char* blurb;
			uint32_t spectator_count;
		} match_info;
		struct {
			char* peer_address;
			uint32_t peer_port;
			uint32_t player_num;
		} match_start;
		struct {
			char* match_id;
			char* player_id;
			char* text;
		} chat;
		struct {
			uint32_t spectator_count;
		} spectator_count_changed;
		struct {
			struct GGPO_Buffer* buffer;
		} game_state;
		struct {
			struct GGPO_Buffer* buffer;
		} inputs;
	} data;
};

struct GGPO_ClientToServer_Msg GGPO_ClientToServer_pop_next_msg(struct GGPO_ClientToServer* state);
bool GGPO_ClientToServer_Msg_is_valid(struct GGPO_ClientToServer_Msg* msg);
void GGPO_ClientToServer_Msg_destroy(struct GGPO_ClientToServer_Msg* msg);

struct GGPO_GameInputsQueue {
	struct GGPO_BufferQueue _buffer_queue;
	struct GGPO_Buffer* _current_buffer;
	struct GGPO_BufferReader _reader;
	struct {
		uint32_t count;
		uint32_t size;
		uint32_t index;
	} _current;
};

void GGPO_GameInputsQueue_init(struct GGPO_GameInputsQueue* queue);
void GGPO_GameInputsQueue_destroy(struct GGPO_GameInputsQueue* queue);
void GGPO_GameInputsQueue_give_inputs_msg(struct GGPO_GameInputsQueue* queue, struct GGPO_ClientToServer_Msg* msg);
bool GGPO_GameInputsQueue_get_input(struct GGPO_GameInputsQueue* queue, struct GGPO_GameInputs* inputs);

#endif // GGPO_CLIENT_TO_SERVER_H__