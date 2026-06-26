#ifndef GGPO_PEER_TO_PEER_H__
#define GGPO_PEER_TO_PEER_H__

#include <stdint.h>
#include <stdbool.h>
#include "ggpo_input.h"
#include "ggpo_network.h"

enum {
	kGGPO_PeerToPeer_State_initialized,
	kGGPO_PeerToPeer_State_syncing,
	kGGPO_PeerToPeer_State_running,
};

enum {
	kGGPO_PeerToPeer_StateTransition_none,
	kGGPO_PeerToPeer_StateTransition_connected_to_peer,
	kGGPO_PeerToPeer_StateTransition_syncing_1,
	kGGPO_PeerToPeer_StateTransition_syncing_2,
	kGGPO_PeerToPeer_StateTransition_syncing_3,
	kGGPO_PeerToPeer_StateTransition_syncing_4,
	kGGPO_PeerToPeer_StateTransition_running,
};

struct GGPO_PeerToPeer {
	int state;
	struct {
		// Five rounds of request/reply.
		int count;
		uint32_t time_to_resend_sync_ms;
	} _sync;
	struct {
		uint32_t ping_time_ms;
		uint32_t next_time_to_send_quality_report_ms;
		int8_t remote_frame_advantage;
		int8_t local_frame_advantage;
		int send_queue_len;
		int last_received_frame;
		struct GGPO_GameInputs last_received_input;
		struct GGPO_GameInputs last_acked_input;
	} _running;
	union GGPO_PlatformAddress _peer_address;
	uint16_t _listen_port;
	struct GGPO_InputState input;
	GGPO_Socket _recv_sock;
	GGPO_Socket _send_sock;
};

bool GGPO_PeerToPeer_init(struct GGPO_PeerToPeer* state, uint16_t recv_port);
uint16_t GGPO_PeerToPeer_get_listen_port(struct GGPO_PeerToPeer const* state);
bool GGPO_PeerToPeer_connect_to_peer(struct GGPO_PeerToPeer* state, char const* peer_host, uint16_t peer_port);
int GGPO_PeerToPeer_process_network(struct GGPO_PeerToPeer* state, uint32_t now_ms);
void GGPO_PeerToPeer_set_frame(struct GGPO_PeerToPeer* state, int frame);

struct GGPO_PeerToPeer_Stats {
	int send_queue_len;
	int ping;
	int local_frames_behind;
	int remote_frames_behind;
};

void GGPO_PeerToPeer_get_stats(struct GGPO_PeerToPeer const* state, struct GGPO_PeerToPeer_Stats* out_stats);
void GGPO_PeerToPeer_frame_end(struct GGPO_PeerToPeer* state);
void GGPO_PeerToPeer_destroy(struct GGPO_PeerToPeer* state);

#endif // GGPO_PEER_TO_PEER_H__