#ifndef GGPO_INPUT_H__
#define GGPO_INPUT_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define GGPO_GAMEINPUT_SIZE 20

struct GGPO_GameInputs {
	uint8_t inputs[GGPO_GAMEINPUT_SIZE];
};

#define GGPO_INPUT_STATE_QUEUE_SIZE 32
#define GGPO_INPUT_STATE_FRAME_INVALID -1

// The input state holds a circular queue of inputs for storing local and remote inputs.
// As we progress forward locally we may need to return remote inputs for which we don't yet
// have values from the remote client. For those we will return the closest previous input
// for which we have a value.
//  _ _ _ _ _ _ _ _
// | |x| | | | | | |
// '-'-'-'-'-'-'-'-'
//    ^         ^
enum {
	// No value is stored.
	kInputStateInvalid,
	// A valid and correct value is stored.
	kInputStateValid,
	// A predicted value is stored.
	kInputStatePredicted,
	// A valid value is stored, but it replaced a predicted value that
	// had a different value than is currently stored.
	kInputStateMispredicted,
};
struct GGPO_InputState {
	struct {
		struct GGPO_GameInputs local[GGPO_INPUT_STATE_QUEUE_SIZE];
		struct GGPO_GameInputs remote[GGPO_INPUT_STATE_QUEUE_SIZE];
	} _inputs;

	uint8_t _remote_input_state[GGPO_INPUT_STATE_QUEUE_SIZE];
	int _current_frame;
	int _acked_frame;
	int _oldest_predicted_frame;
	int _input_delay;
	bool _current_local_frame_is_set;
};

void GGPO_InputState_init(struct GGPO_InputState* state);
void GGPO_InputState_ack_local_input(struct GGPO_InputState* state, int frame);
bool GGPO_InputState_add_local_input(struct GGPO_InputState* state, struct GGPO_GameInputs* inputs);
bool GGPO_InputState_get_local_input(struct GGPO_InputState* state, int frame, struct GGPO_GameInputs* inputs);
bool GGPO_InputState_get_all_unacked_local_inputs(struct GGPO_InputState* state, int* start_frame, struct GGPO_GameInputs* inputs, size_t* input_count);
bool GGPO_InputState_add_remote_input(struct GGPO_InputState* state, int frame, struct GGPO_GameInputs* inputs);
void GGPO_InputState_get_input(struct GGPO_InputState* state, int frame, struct GGPO_GameInputs* out_inputs, bool* out_is_predicted);
void GGPO_InputState_get_current_input(struct GGPO_InputState* state, struct GGPO_GameInputs* out_inputs, bool* out_is_predicted);
void GGPO_InputState_set_input_delay(struct GGPO_InputState* state, unsigned int frame_delay);
void GGPO_InputState_frame_end(struct GGPO_InputState* state);

struct GGPO_RollbackState {
	int start;
	int count;
};

bool GGPO_RollbackState_is_valid(struct GGPO_RollbackState const* state);
struct GGPO_RollbackState GGPO_InputState_start_rollback(struct GGPO_InputState* state);
void GGPO_InputState_end_rollback(struct GGPO_InputState* state, struct GGPO_RollbackState const* rollback);

#endif // GGPO_INPUT_H__