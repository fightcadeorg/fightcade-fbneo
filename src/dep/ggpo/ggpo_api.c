#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For usleep.

#include "ggpo_input.h"
#include "ggpo_logging.h"
#include "ggpo_peer_to_peer.h"
#include "ggpo_client_to_server.h"

#ifndef _WIN32
#include <time.h>
#endif

#include <ggpoclient.h>
#include <ggponet.h>

// zig cc -target x86-windows-gnu -Iinclude -std=c11 -O2 -shared -Wl,--out-implib,ggponet.lib ggpo_api.c ggpo_buffers.c ggpo_input.c ggpo_network.c ggpo_client_to_server.c ggpo_peer_to_peer.c -o ggponet.dll


#ifndef GGPO_SERVER_HOSTNAME
#define GGPO_SERVER_HOSTNAME "ggpo.fightcade.com"
// #define GGPO_SERVER_HOSTNAME "ggpo_fakefightcade.com"
#endif
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

struct GameState {
	void* buffer;
	int length;
};

#define GGPO_CONFIRMED_INPUT_QUEUE_SIZE 60

struct ConfirmedInputs {
	size_t count;
#ifndef NDEBUG
	size_t input_size;
#endif
	uint8_t bytes[sizeof(struct GGPO_GameInputs) * GGPO_CONFIRMED_INPUT_QUEUE_SIZE];
};

static void ConfirmedInputs_add_(struct ConfirmedInputs* confirmed_inputs, struct GGPO_GameInputs const* inputs, size_t size, int player_count)
{
	assert(confirmed_inputs);
	assert(inputs);
#ifndef NDEBUG
	if (confirmed_inputs->input_size == 0) {
		confirmed_inputs->input_size = size;
	} else {
		assert(confirmed_inputs->input_size == size);
	}
#endif
	size_t const input_byte_count = size * player_count;
	memcpy(confirmed_inputs->bytes + confirmed_inputs->count * input_byte_count, inputs->inputs, input_byte_count);
	confirmed_inputs->count++;
}

static bool ConfirmedInputs_is_full_(struct ConfirmedInputs* confirmed_inputs)
{
	assert(confirmed_inputs);
	return confirmed_inputs->count == GGPO_CONFIRMED_INPUT_QUEUE_SIZE;
}

static void ConfirmedInputs_clear_(struct ConfirmedInputs* confirmed_inputs)
{
	assert(confirmed_inputs);
	confirmed_inputs->count = 0;
}

static void const* ConfirmedInputs_get_bytes_(struct ConfirmedInputs const* confirmed_inputs)
{
	assert(confirmed_inputs);
	return confirmed_inputs->bytes;
}

static int ConfirmedInputs_get_count_(struct ConfirmedInputs const* confirmed_inputs)
{
	assert(confirmed_inputs);
	return confirmed_inputs->count;
}

enum {
	kGGPOSession_Mode_spectate,
	kGGPOSession_Mode_match_server,
	kGGPOSession_Mode_match_direct,
};

struct GGPOSession {
	int mode;
	GGPOSessionCallbacks cb;
	struct GameState game_states[GGPO_INPUT_STATE_QUEUE_SIZE];
	struct GGPO_GameInputsQueue incoming_inputs;
	struct GGPO_PeerToPeer peer_to_peer;
	struct GGPO_ClientToServer client_to_server;
	char* matchid;
	char* game;
	uint16_t peer_to_peer_recv_port;
	int player_index;
	int frame;
	// We will wait until this reaches 0 to send game state to the server.
	int send_state_countdown;
	int next_confirmed_input;
	struct ConfirmedInputs confirmed_inputs;
	bool in_rollback;
	bool match_started;
	bool synchronized;
	int spectator_count;
};

static GGPOSession* GGPOSession_alloc_()
{
	GGPOSession* new_session = calloc(1, sizeof(*new_session));
	return new_session;
}

static int GGPOSession_get_game_state_queue_index_(int frame)
{
	return frame % GGPO_INPUT_STATE_QUEUE_SIZE;
}

// ----------------
// ggpoclient.h
// ----------------

static void ggpo_event_connecting_(GGPOSession* session)
{
	assert(session);
	session->cb.on_event((GGPOEvent*)&(GGPOClientEvent){
		.code = GGPOCLIENT_EVENTCODE_CONNECTING,
	});
}

static void ggpo_event_connected_(GGPOSession* session)
{
	assert(session);
	session->cb.on_event((GGPOEvent*)&(GGPOClientEvent){
		.code = GGPOCLIENT_EVENTCODE_CONNECTED,
	});
}

static void ggpo_event_retrieving_matchinfo_(GGPOSession* session)
{
	assert(session);
	session->cb.on_event((GGPOEvent*)&(GGPOClientEvent){
		.code = GGPOCLIENT_EVENTCODE_RETREIVING_MATCHINFO,
	});
}

static void ggpo_event_matchinfo_(GGPOSession* session, char const* player_one, char const* player_two, char const* blurb)
{
	assert(session);
	GGPOClientEvent event = {
		.code = GGPOCLIENT_EVENTCODE_MATCHINFO,
		.u.matchinfo = {
			.p1 = strdup(player_one),
			.p2 = strdup(player_two),
			.blurb = strdup(blurb),
		},
	};
	assert(event.u.matchinfo.p1);
	assert(event.u.matchinfo.p2);
	assert(event.u.matchinfo.blurb);

	session->cb.on_event((GGPOEvent*)&event);

	free(event.u.matchinfo.p1);
	free(event.u.matchinfo.p2);
	free(event.u.matchinfo.blurb);
}

static void ggpo_event_spectator_count_changed_(GGPOSession* session, int spectator_count)
{
	assert(session);
	session->cb.on_event((GGPOEvent*)&(GGPOClientEvent){
		.code = GGPOCLIENT_EVENTCODE_SPECTATOR_COUNT_CHANGED,
		.u.spectator_count_changed = {
			.count = spectator_count,
		},
	});
}

static void ggpo_event_chat_(GGPOSession* session, char* username, char* text)
{
	assert(session);
	assert(username);
	assert(text);

	session->cb.on_event((GGPOEvent*)&(GGPOClientEvent){
		.code = GGPOCLIENT_EVENTCODE_CHAT,
		.u.chat = {
			.username = username,
			.text = text,
		},
	});
}

static void ggpo_event_chat_const_(GGPOSession* session, char const* username, char const* text)
{
	assert(session);
	assert(username);
	assert(text);
	char* username_nonconst = strdup(username);
	char* text_nonconst = strdup(text);

	ggpo_event_chat_(session, username_nonconst, text_nonconst);

	free(username_nonconst);
	free(text_nonconst);
}

static void ggpo_event_disconnected_(GGPOSession* session)
{
	assert(session);
	session->cb.on_event((GGPOEvent*)&(GGPOClientEvent){
		.code = GGPOCLIENT_EVENTCODE_DISCONNECTED,
	});
}

static void ggpo_event_running_(GGPOSession* session)
{
	assert(session);
	session->cb.on_event(&(GGPOEvent){
		.code = GGPO_EVENTCODE_RUNNING,
	});
}

static void ggpo_event_connected_to_peer_(GGPOSession* session)
{
	assert(session);
	session->cb.on_event(&(GGPOEvent){
		.code = GGPO_EVENTCODE_CONNECTED_TO_PEER,
	});
}

static void ggpo_event_synchronizing_with_peer_(GGPOSession* session, int count, int total)
{
	assert(session);
	session->cb.on_event(&(GGPOEvent){
		.code = GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER,
		.u.synchronizing = {
			.count = count,
			.total = total,
		},
	});
}

static void GGPOSession_destroy_shared_(GGPOSession* session)
{
	assert(session);

	free(session->game);
	free(session);
}

static void GGPOSession_init_shared_(GGPOSession* session, GGPOSessionCallbacks *cb, char *game)
{
	assert(cb);
	assert(game);

	*session = (GGPOSession){
		.game = strdup(game),
		.cb = *cb,
		.player_index = -1,
		.send_state_countdown = -1,
	};
	assert(session->game);
}

static GGPOSession* GGPOSession_create_match_server_(GGPOSessionCallbacks *cb, char *game, char *matchid, int serverport)
{
	GGPOSession* session = GGPOSession_alloc_();
	GGPOSession_init_shared_(session, cb, game);
	session->mode = kGGPOSession_Mode_match_server;
	session->matchid = strdup(matchid);
	assert(session->matchid);

	ggpo_event_connecting_(session);

	if (!GGPO_ClientToServer_init(&session->client_to_server, GGPO_SERVER_HOSTNAME, serverport)) {
		free(session->matchid);
		GGPOSession_destroy_shared_(session);
		return NULL;
	}

	ggpo_event_connected_(session);

	GGPO_GameInputsQueue_init(&session->incoming_inputs);
	GGPO_ClientToServer_send_msg_version(&session->client_to_server);
	if (!GGPO_ClientToServer_write_messages(&session->client_to_server)) {
		GGPO_GameInputsQueue_destroy(&session->incoming_inputs);
		GGPO_ClientToServer_destroy(&session->client_to_server);
		free(session->matchid);
		GGPOSession_destroy_shared_(session);
		return NULL;
	}

	if (!GGPO_PeerToPeer_init(&session->peer_to_peer, 6000)) {
		GGPO_GameInputsQueue_destroy(&session->incoming_inputs);
		GGPO_ClientToServer_destroy(&session->client_to_server);
		free(session->matchid);
		GGPOSession_destroy_shared_(session);
		return NULL;
	}
	session->peer_to_peer_recv_port = GGPO_PeerToPeer_get_listen_port(&session->peer_to_peer);
	session->cb.begin_game(game);

	return session;
}

static GGPOSession* GGPOSession_create_match_direct_(GGPOSessionCallbacks *cb, char *game, int localport, char *remoteip, int remoteport, int player_num)
{
	assert(cb);
	assert(game);
	assert(remoteip);

	GGPOSession* session = GGPOSession_alloc_();
	GGPOSession_init_shared_(session, cb, game);
	session->mode = kGGPOSession_Mode_match_direct;
	session->match_started = true;

	if (!GGPO_PeerToPeer_init(&session->peer_to_peer, localport)) {
		GGPOSession_destroy_shared_(session);
		return NULL;
	}
	if (!GGPO_PeerToPeer_connect_to_peer(&session->peer_to_peer, remoteip, remoteport)) {
		GGPO_PeerToPeer_destroy(&session->peer_to_peer);
		GGPOSession_destroy_shared_(session);
		return NULL;
	}
	// It's possible we had to pick a different port because our first choice was already taken.
	session->peer_to_peer_recv_port = GGPO_PeerToPeer_get_listen_port(&session->peer_to_peer);
	// The original ggponet.dll has a default input delay of 1.
	GGPO_InputState_set_input_delay(&session->peer_to_peer.input, 1);
	session->player_index = player_num;
	session->cb.begin_game(game);

	return session;
}

static GGPOSession* GGPOSession_create_match_spectate_(GGPOSessionCallbacks *cb, char *game, char *matchid, int port)
{
	assert(cb);
	assert(game);
	assert(matchid);

	GGPOSession* session = GGPOSession_alloc_();
	GGPOSession_init_shared_(session, cb, game);
	session->mode = kGGPOSession_Mode_spectate;
	session->matchid = strdup(matchid);
	assert(session->matchid);

	ggpo_event_connecting_(session);

	if (!GGPO_ClientToServer_init(&session->client_to_server, GGPO_SERVER_HOSTNAME, port)) {
		free(session->matchid);
		GGPOSession_destroy_shared_(session);
		return NULL;
	}

	ggpo_event_connected_(session);

	GGPO_GameInputsQueue_init(&session->incoming_inputs);
	GGPO_ClientToServer_send_msg_version(&session->client_to_server);
	if (!GGPO_ClientToServer_write_messages(&session->client_to_server)) {
		GGPO_GameInputsQueue_destroy(&session->incoming_inputs);
		GGPO_ClientToServer_destroy(&session->client_to_server);
		free(session->matchid);
		GGPOSession_destroy_shared_(session);
		return NULL;
	}
	session->cb.begin_game(game);

	return session;
}

GGPO_EXPORT GGPOSession* ggpo_client_connect(GGPOSessionCallbacks *cb, char *game, char *matchid, int serverport)
{
	// Connect to a server to find our peer.
	assert(cb);
	assert(game);
	assert(matchid);

	return GGPOSession_create_match_server_(cb, game, matchid, serverport);
}

GGPO_EXPORT bool ggpo_client_chat(GGPOSession* session, char *text)
{
	if (!session) {
		return false;
	}

	assert(text);
	if (session->mode != kGGPOSession_Mode_match_server) {
		return false;
	}
	GGPO_ClientToServer_send_msg_chat(&session->client_to_server, session->matchid, text);
	return true;
}

GGPO_EXPORT bool ggpo_client_set_game_event(GGPOSession* session, GGPOClientGameEventType type, void *data)
{
	// This function is never called so we can ignore it.
	abort();
	return false;
}

GGPO_EXPORT int ggpo_set_frame_delay(GGPOSession* session, int frame_delay)
{
	if (!session) {
		return 0;
	}

	if (session->mode == kGGPOSession_Mode_match_server || session->mode == kGGPOSession_Mode_match_direct) {
		GGPO_InputState_set_input_delay(&session->peer_to_peer.input, frame_delay);
	}
	return 0;
}

// ----------------
// ggponet.h
// ----------------

GGPO_EXPORT GGPOSession* ggpo_start_session(GGPOSessionCallbacks *cb, char *game, int localport, char *remoteip, int remoteport, int player_num)
{
	// Directly connect to a peer.
	assert(cb);
	assert(game);
	assert(remoteip);

	return GGPOSession_create_match_direct_(cb, game, localport, remoteip, remoteport, player_num);
}

GGPO_EXPORT GGPOSession* ggpo_start_synctest(GGPOSessionCallbacks *cb, char *game, int frames)
{
	// Unused.
	assert(cb);
	assert(game);
	(void)cb;
	(void)game;
	return NULL;
}

GGPO_EXPORT GGPOSession* ggpo_start_streaming(GGPOSessionCallbacks *cb, char *game, char *matchid, int port)
{
	// Replay/Spectate.
	assert(cb);
	assert(game);
	assert(matchid);
	assert(port);

	return GGPOSession_create_match_spectate_(cb, game, matchid, port);
}

GGPO_EXPORT GGPOSession* ggpo_start_replay(GGPOSessionCallbacks *cb, char *file)
{
	// Unused.
	assert(cb);
	assert(file);
	abort();
	return NULL;
}

GGPO_EXPORT void ggpo_close_session(GGPOSession* session)
{
	if (!session) {
		return;
	}

	if (session->mode == kGGPOSession_Mode_match_direct || session->mode == kGGPOSession_Mode_match_server) {
		for (size_t i = 0; i < ARRAY_LEN(session->game_states); i++) {
			free(session->game_states[i].buffer);
		}
		GGPO_PeerToPeer_destroy(&session->peer_to_peer);
	}
	if (session->mode == kGGPOSession_Mode_match_server || session->mode == kGGPOSession_Mode_spectate) {
		GGPO_ClientToServer_destroy(&session->client_to_server);
		GGPO_GameInputsQueue_destroy(&session->incoming_inputs);
	}
	free(session->matchid);
	GGPOSession_destroy_shared_(session);
}

static int get_time_ms_()
{
#ifdef _WIN32
	return timeGetTime();
#else
	struct timespec current;
	clock_gettime(CLOCK_MONOTONIC, &current);

	return (current.tv_sec * 1000) + (current.tv_nsec / 1000000);
#endif
}

static void GGPOSession_process_one_server_message_(GGPOSession* session, struct GGPO_ClientToServer_Msg* msg)
{
	switch (msg->type) {
	case kGGPO_ClientToServer_Msg_None:
		// Ignore.
		break;
	case kGGPO_ClientToServer_Msg_Connected: {
		gglog("  msg connected\n");
		if (session->mode == kGGPOSession_Mode_spectate) {
			GGPO_ClientToServer_send_msg_spectate_request(&session->client_to_server, session->matchid);
			GGPO_ClientToServer_send_msg_request_match_info(&session->client_to_server, session->matchid);
			ggpo_event_retrieving_matchinfo_(session);
		} else {
			GGPO_ClientToServer_send_msg_connect(&session->client_to_server, session->matchid, session->peer_to_peer_recv_port);
		}
		break;
	}
	case kGGPO_ClientToServer_Msg_MatchInfo: {
		gglog("  match info\n");
		ggpo_event_matchinfo_(session, msg->data.match_info.player_one, msg->data.match_info.player_two, msg->data.match_info.blurb);
		ggpo_event_spectator_count_changed_(session, session->spectator_count);
		if (session->mode == kGGPOSession_Mode_match_server) {
			ggpo_event_chat_const_(session, "System", "Press \'T\' to chat...");
		}
		break;
	}
	case kGGPO_ClientToServer_Msg_MatchStart: {
		gglog("  match start\n");
		session->player_index = msg->data.match_start.player_num;
		if (session->mode == kGGPOSession_Mode_match_server) {
			GGPO_ClientToServer_send_msg_request_match_info(&session->client_to_server, session->matchid);
			ggpo_event_retrieving_matchinfo_(session);
		}
		if (!session->match_started) {
			session->match_started = true;
			gglog("  match started\n");
			GGPO_PeerToPeer_connect_to_peer(&session->peer_to_peer, msg->data.match_start.peer_address, msg->data.match_start.peer_port);
		}
		break;
	}
	case kGGPO_ClientToServer_Msg_Chat: {
		gglog("  chat: '%s'\n", msg->data.chat.text);
		ggpo_event_chat_(session, msg->data.chat.player_id, msg->data.chat.text);
		break;
	}
	case kGGPO_ClientToServer_Msg_DisconnectedPeer: {
		gglog("  disconnected peer\n");
		ggpo_event_disconnected_(session);
		break;
	}
	case kGGPO_ClientToServer_Msg_SpectatorCountChanged: {
		gglog("  spectator count changed\n");
		ggpo_event_spectator_count_changed_(session, msg->data.spectator_count_changed.spectator_count);
		break;
	}
	case kGGPO_ClientToServer_Msg_StateRequest:
		gglog("  state request\n");
		session->send_state_countdown = 3;
		break;
	case kGGPO_ClientToServer_Msg_GameState:
		gglog("  game state\n");
		session->cb.load_game_state((unsigned char*)msg->data.game_state.buffer->buffer, msg->data.game_state.buffer->size);
		break;
	case kGGPO_ClientToServer_Msg_Inputs:
		gglog("  server inputs\n");
		GGPO_GameInputsQueue_give_inputs_msg(&session->incoming_inputs, msg);
		break;
	}
}

static void GGPOSession_process_server_messages_(GGPOSession* session)
{
	struct GGPO_ClientToServer_Msg msg;
	do {
		msg = GGPO_ClientToServer_pop_next_msg(&session->client_to_server);
		GGPOSession_process_one_server_message_(session, &msg);
		GGPO_ClientToServer_Msg_destroy(&msg);
	} while (msg.type != kGGPO_ClientToServer_Msg_None);
}

static void GGPOSession_peer_to_peer_pump_(GGPOSession* session)
{
	if (!session->match_started) {
		return;
	}
	gglog("  p2p pump\n");

	int const transition = GGPO_PeerToPeer_process_network(&session->peer_to_peer, get_time_ms_());
	switch (transition) {
	case kGGPO_PeerToPeer_StateTransition_connected_to_peer:
		ggpo_event_connected_to_peer_(session);
		break;
	case kGGPO_PeerToPeer_StateTransition_syncing_1:
		ggpo_event_synchronizing_with_peer_(session, 1, 5);
		break;
	case kGGPO_PeerToPeer_StateTransition_syncing_2:
		ggpo_event_synchronizing_with_peer_(session, 2, 5);
		break;
	case kGGPO_PeerToPeer_StateTransition_syncing_3:
		ggpo_event_synchronizing_with_peer_(session, 3, 5);
		break;
	case kGGPO_PeerToPeer_StateTransition_syncing_4:
		ggpo_event_synchronizing_with_peer_(session, 4, 5);
		break;
	case kGGPO_PeerToPeer_StateTransition_running:
		ggpo_event_running_(session);
		session->synchronized = true;
		break;
	case kGGPO_PeerToPeer_StateTransition_none: break;
	}
}

static struct GameState* GGPOSession_get_current_game_state_(GGPOSession* session)
{
	int const game_state_index = GGPOSession_get_game_state_queue_index_(session->frame);
	return &session->game_states[game_state_index];
}

static void GGPOSession_save_current_frame_state_(GGPOSession* session)
{
	gglog("save_state(%d)\n", session->frame);
	struct GameState* game_state = GGPOSession_get_current_game_state_(session);
	if (game_state->buffer != NULL) {
		session->cb.free_buffer(game_state->buffer);
		game_state->buffer = NULL;
		game_state->length = 0;
	}

	int game_state_checksum = 0;
	session->cb.save_game_state((unsigned char**)&game_state->buffer, &game_state->length, &game_state_checksum, session->frame);
}

static void GGPOSession_rollback_if_inconsistent_(GGPOSession* session)
{
	assert(session->mode == kGGPOSession_Mode_match_server || session->mode == kGGPOSession_Mode_match_direct);

	struct GGPO_RollbackState rollback = GGPO_InputState_start_rollback(&session->peer_to_peer.input);
	if (GGPO_RollbackState_is_valid(&rollback)) {
		session->in_rollback = true;

		gglog("  do rollback(%d)\n", rollback.start);
		session->frame = rollback.start;
		int game_state_index = GGPOSession_get_game_state_queue_index_(rollback.start);
		struct GameState const* game_state = &session->game_states[game_state_index];
		session->cb.load_game_state(game_state->buffer, game_state->length);
		for (int i = 0; i < rollback.count; i++) {
			gglog("  rollback (%d)\n", session->frame);
			int const flags = 0;
			session->cb.advance_frame(flags);
		}

		session->in_rollback = false;
		GGPO_InputState_end_rollback(&session->peer_to_peer.input, &rollback);
	}
}

static void GGPOSession_idle_client_to_server_(GGPOSession* session)
{
	gglog("idle_client_to_server\n");
	GGPO_ClientToServer_read_messages(&session->client_to_server);
	GGPOSession_process_server_messages_(session);
	GGPO_ClientToServer_write_messages(&session->client_to_server);
}

static void GGPOSession_idle_peer_to_peer_(GGPOSession* session)
{
	if (session->in_rollback) {
		return;
	}

	GGPOSession_peer_to_peer_pump_(session);

	if (!session->synchronized) {
		return;
	}

	if (!session->in_rollback) {
		GGPOSession_rollback_if_inconsistent_(session);
	}
}

static void GGPOSession_idle_spectate_(GGPOSession* session)
{
	GGPOSession_idle_client_to_server_(session);
}

static void GGPOSession_idle_match_server_(GGPOSession* session)
{
	GGPOSession_idle_client_to_server_(session);
	GGPOSession_idle_peer_to_peer_(session);
}

static void GGPOSession_idle_match_direct_(GGPOSession* session)
{
	GGPOSession_idle_peer_to_peer_(session);
}

GGPO_EXPORT bool ggpo_idle(GGPOSession* session, int timeout)
{
	if (!session) {
		return false;
	}

	switch (session->mode) {
	case kGGPOSession_Mode_match_server:
		GGPOSession_idle_match_server_(session);
		break;
	case kGGPOSession_Mode_match_direct:
		GGPOSession_idle_match_direct_(session);
		break;
	case kGGPOSession_Mode_spectate:
		GGPOSession_idle_spectate_(session);
		break;
	}

	// The original ggponet.dll does this.
	// if (!session->in_rollback) {
	// 	usleep(timeout * 1000);
	// }

	return true;
}

static bool GGPOSession_synchronize_input_spectate_(GGPOSession* session, void* values, int size, int players)
{
	struct GGPO_GameInputs inputs = {};
	if (!GGPO_GameInputsQueue_get_input(&session->incoming_inputs, &inputs)) {
		return false;
	}
	memcpy(values, inputs.inputs, size * players);

	return true;
}

static bool GGPOSession_synchronize_input_peer_to_peer_(GGPOSession* session, void* values, int size, int players)
{
	if (!session->match_started || !session->synchronized) {
		return false;
	}

	size_t const input_byte_count = size * players;
	if (session->frame == 0) {
		// The original implementation zeros out the first frame so that we can never predict frame 0 wrong.
		memset(values, 0, input_byte_count);
	}

	struct GGPO_GameInputs inputs = {};
	if (!session->in_rollback) {
		assert(session->player_index != -1);
		memcpy(inputs.inputs + session->player_index * size, values, size);
		if (!GGPO_InputState_add_local_input(&session->peer_to_peer.input, &inputs)) {
			return false;
		}
		int const transition = GGPO_PeerToPeer_process_network(&session->peer_to_peer, get_time_ms_());
		assert(transition == kGGPO_PeerToPeer_StateTransition_none);
	}

	bool is_predicted;
	GGPO_InputState_get_input(&session->peer_to_peer.input, session->frame, &inputs, &is_predicted);

	if (session->mode == kGGPOSession_Mode_match_server && session->player_index == 1 && !is_predicted && session->frame == session->next_confirmed_input) {
		ConfirmedInputs_add_(&session->confirmed_inputs, &inputs, size, players);
		session->next_confirmed_input++;
		if (ConfirmedInputs_is_full_(&session->confirmed_inputs)) {
			if (session->spectator_count > 0) {
				GGPO_ClientToServer_send_msg_inputs(&session->client_to_server, session->matchid, ConfirmedInputs_get_bytes_(&session->confirmed_inputs), ConfirmedInputs_get_count_(&session->confirmed_inputs), input_byte_count);
			}
			ConfirmedInputs_clear_(&session->confirmed_inputs);
			assert(session->send_state_countdown != -1);
			if (session->send_state_countdown > 0) {
				session->send_state_countdown--;
				if (session->send_state_countdown == 0) {
					struct GameState* game_state = GGPOSession_get_current_game_state_(session);
					GGPO_ClientToServer_send_msg_game_state(&session->client_to_server, session->matchid, game_state->buffer, game_state->length);
				}
			}
		}
	}
	memcpy(values, inputs.inputs, input_byte_count);

	return true;
}

static bool GGPOSession_synchronize_input_match_server_(GGPOSession* session, void* values, int size, int players)
{
	return GGPOSession_synchronize_input_peer_to_peer_(session, values, size, players);
}

static bool GGPOSession_synchronize_input_match_direct_(GGPOSession* session, void* values, int size, int players)
{
	return GGPOSession_synchronize_input_peer_to_peer_(session, values, size, players);
}

GGPO_EXPORT bool ggpo_synchronize_input(GGPOSession* session, void* values, int size, int players)
{
	if (!session) {
		return false;
	}
	assert(values);
	assert(size > 0);
	if (players > 2) {
		players = 2;
	}

	switch (session->mode) {
	case kGGPOSession_Mode_match_server:
		return GGPOSession_synchronize_input_match_server_(session, values, size, players);
	case kGGPOSession_Mode_match_direct:
		return GGPOSession_synchronize_input_match_direct_(session, values, size, players);
	case kGGPOSession_Mode_spectate:
		return GGPOSession_synchronize_input_spectate_(session, values, size, players);
	}

	return false;
}

static bool GGPOSession_advance_frame_spectate_(GGPOSession* session)
{
	session->frame++;
	return true;
}

static bool GGPOSession_advance_frame_peer_to_peer_(GGPOSession* session)
{
	session->frame++;
	GGPOSession_save_current_frame_state_(session);
	if (!session->in_rollback) {
		GGPO_PeerToPeer_frame_end(&session->peer_to_peer);
		ggpo_idle(session, 0);
	}
	return true;
}

static bool GGPOSession_advance_frame_match_server_(GGPOSession* session)
{
	return GGPOSession_advance_frame_peer_to_peer_(session);
}

static bool GGPOSession_advance_frame_match_direct_(GGPOSession* session)
{
	return GGPOSession_advance_frame_peer_to_peer_(session);
}

GGPO_EXPORT bool ggpo_advance_frame(GGPOSession* session)
{
	if (!session) {
		return false;
	}
	gglog("advance_frame %d -> %d\n", session->frame, session->frame + 1);

	switch (session->mode) {
	case kGGPOSession_Mode_match_server:
		return GGPOSession_advance_frame_match_server_(session);
	case kGGPOSession_Mode_match_direct:
		return GGPOSession_advance_frame_match_direct_(session);
	case kGGPOSession_Mode_spectate:
		return GGPOSession_advance_frame_spectate_(session);
	}

	return false;
}

GGPO_EXPORT bool ggpo_get_stats(GGPOSession* session, GGPONetworkStats *out_stats)
{
	if (!session) {
		return false;
	}

	if (session->mode == kGGPOSession_Mode_spectate) {
		*out_stats = (GGPONetworkStats){};
		return true;
	}

	struct GGPO_PeerToPeer_Stats peer_to_peer_stats;
	GGPO_PeerToPeer_get_stats(&session->peer_to_peer, &peer_to_peer_stats);

	*out_stats = (GGPONetworkStats){
		.network = {
			.send_queue_len = peer_to_peer_stats.send_queue_len,
			.ping = peer_to_peer_stats.ping,
		},
		.timesync = {
			.local_frames_behind = peer_to_peer_stats.local_frames_behind,
			.remote_frames_behind = peer_to_peer_stats.remote_frames_behind,
		},
	};
	return true;
}

GGPO_EXPORT void ggpo_log(GGPOSession* session, char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	ggpo_logv(session, fmt, args);
	va_end(args);
}

GGPO_EXPORT void ggpo_logv(GGPOSession* session, char* fmt, va_list args)
{
	(void)session;
	(void)fmt;
	(void)args;
}