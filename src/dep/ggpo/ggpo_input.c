#include "ggpo_input.h"
#include "ggpo_logging.h"
#include <assert.h>
#include <string.h>
#include <limits.h>

static bool GGPO_GameInputs_equal_(struct GGPO_GameInputs const* lhs, struct GGPO_GameInputs const* rhs)
{
	return 0 == memcmp(lhs->inputs, rhs->inputs, sizeof(lhs->inputs));
}

void GGPO_InputState_init(struct GGPO_InputState* state)
{
	*state = (struct GGPO_InputState){
		._oldest_predicted_frame = GGPO_INPUT_STATE_FRAME_INVALID,
		._acked_frame = GGPO_INPUT_STATE_FRAME_INVALID,
	};
	memset(state->_remote_input_state, kInputStateInvalid, sizeof(state->_remote_input_state));
}

static int GGPO_InputState_get_input_queue_index_(int frame)
{
	return frame % GGPO_INPUT_STATE_QUEUE_SIZE;
}

static int GGPO_InputState_get_oldest_local_frame_(struct GGPO_InputState* state)
{
	int oldest_frame = state->_current_frame - GGPO_INPUT_STATE_QUEUE_SIZE - 1;
	if (oldest_frame < 0) {
		oldest_frame = 0;
	}
	return oldest_frame;
}

void GGPO_InputState_ack_local_input(struct GGPO_InputState* state, int frame)
{
	// The ACK serves as a barrier. We can't add new local inputs that would write
	// over any un-acked inputs.
	state->_acked_frame = frame;
}

bool GGPO_InputState_add_local_input(struct GGPO_InputState* state, struct GGPO_GameInputs* inputs)
{
	// Don't allow adding frames past what the client has acked.
	// We don't need to preserve the actual acked frame, just the ones after it.
	int const current_maximum_allowed_local_frame = state->_acked_frame + GGPO_INPUT_STATE_QUEUE_SIZE;
	if (state->_current_frame > current_maximum_allowed_local_frame) {
		return false;
	}

	int const frame = state->_current_frame + state->_input_delay;
	// Local inputs always go in order and never need to be predicted.
	int const index = GGPO_InputState_get_input_queue_index_(frame);
	state->_inputs.local[index] = *inputs;
	state->_current_local_frame_is_set = true;
	return true;
}

static int GGPO_InputState_get_most_recent_local_frame_(struct GGPO_InputState* state)
{
	int most_recent_frame = state->_current_frame + state->_input_delay;
	if (!state->_current_local_frame_is_set) {
		most_recent_frame--;
	}
	return most_recent_frame;
}

bool GGPO_InputState_get_local_input(struct GGPO_InputState* state, int frame, struct GGPO_GameInputs* inputs)
{
	if (frame < GGPO_InputState_get_oldest_local_frame_(state)) {
		return false;
	}
	if (frame > GGPO_InputState_get_most_recent_local_frame_(state)) {
		return false;
	}
	int const frame_index = GGPO_InputState_get_input_queue_index_(frame);
	*inputs = state->_inputs.local[frame_index];
	return true;
}

bool GGPO_InputState_get_all_unacked_local_inputs(struct GGPO_InputState* state, int* out_start_frame, struct GGPO_GameInputs* inputs, size_t* inout_input_count)
{
	assert(inputs);
	assert(inout_input_count);
	assert(*inout_input_count > 0);
	assert(*inout_input_count <= INT_MAX);
	if (state->_current_frame == 0 && !state->_current_local_frame_is_set) {
		// There are no inputs yet.
		return false;
	}
	int const input_space_available = (int)*inout_input_count;
	int start_frame = state->_acked_frame + 1;
	*out_start_frame = start_frame;
	int most_recent_frame = GGPO_InputState_get_most_recent_local_frame_(state);
	*inout_input_count = most_recent_frame - start_frame + 1;
	if (start_frame + input_space_available <= most_recent_frame) {
		// There's not enough space to return all the inputs from the start frame.
		return false;
	}
	struct GGPO_GameInputs* inputs_cursor = inputs;
	int const end_frame = most_recent_frame + 1;
	if (start_frame >= end_frame) {
		// There are no inputs to copy.
		return false;
	}
	for (int frame = start_frame; frame < end_frame; frame++) {
		int frame_index = GGPO_InputState_get_input_queue_index_(frame);
		*inputs_cursor = state->_inputs.local[frame_index];
		inputs_cursor++;
	}
	return true;
}

static void GGPO_InputState_advance_oldest_predicted_input_(struct GGPO_InputState* state)
{
	int oldest_predicted_frame = state->_oldest_predicted_frame;
	int oldest_predicted_frame_index;
	// Find the next remote input that we would need to do rollback for, if any.
	// This will always be a "predicted" or "mispredicted" input.
	do {
		oldest_predicted_frame++;
		oldest_predicted_frame_index = GGPO_InputState_get_input_queue_index_(oldest_predicted_frame);
		gglog("  input state(%d) = %d\n", oldest_predicted_frame, state->_remote_input_state[oldest_predicted_frame_index]);
		assert(oldest_predicted_frame == state->_current_frame || state->_remote_input_state[oldest_predicted_frame_index] != kInputStateInvalid);
	} while (
		oldest_predicted_frame < state->_current_frame
		&& state->_remote_input_state[oldest_predicted_frame_index] != kInputStatePredicted
		&& state->_remote_input_state[oldest_predicted_frame_index] != kInputStateMispredicted
	);
	if (oldest_predicted_frame == state->_current_frame) {
		state->_oldest_predicted_frame = GGPO_INPUT_STATE_FRAME_INVALID;
	} else {
		state->_oldest_predicted_frame = oldest_predicted_frame;
	}
}

static void GGPO_InputState_invalidate_completed_frames_(struct GGPO_InputState* state, int start_frame)
{
	int end_frame;
	if (state->_oldest_predicted_frame != GGPO_INPUT_STATE_FRAME_INVALID) {
		end_frame = state->_oldest_predicted_frame;
	} else {
		end_frame = state->_current_frame;
	}
	for (int frame = start_frame; frame < end_frame; frame++) {
		int const index = GGPO_InputState_get_input_queue_index_(frame);
		state->_remote_input_state[index] = kInputStateInvalid;
	}
}


#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
static float time_in_seconds_from_start_(void)
{
	static int start = 0;
	if (start == 0) {
		start = timeGetTime();
	}
	int now = timeGetTime();
	return (float)(now - start)/1000.0;
}
#else
static float time_in_seconds_from_start_(void)
{
	return 0.0;
}
#endif

bool GGPO_InputState_add_remote_input(struct GGPO_InputState* state, int frame, struct GGPO_GameInputs* inputs)
{
	gglog("[%.03f] add_remote_input(%d)\n", time_in_seconds_from_start_(), frame);
	// We can get inputs farther ahead than our current frame.
	// We should make use of these so long as we don't run too far ahead.
	int const max_predicted_frame = state->_current_frame + GGPO_INPUT_STATE_QUEUE_SIZE - 1;
	if (frame > max_predicted_frame) {
		gglog("  past max predicted frame\n");
		return false;
	}
	// Ignore any frames far enough ahead to interfere with our oldest predicted frame.
	if (state->_oldest_predicted_frame != GGPO_INPUT_STATE_FRAME_INVALID && frame >= state->_oldest_predicted_frame + GGPO_INPUT_STATE_QUEUE_SIZE) {
		gglog("  too far ahead\n");
		return false;
	}

	int const index = GGPO_InputState_get_input_queue_index_(frame);
	if (state->_current_frame <= frame) {
		gglog("  future input\n");
		// This is a current or future input. Store it.
		state->_inputs.remote[index] = *inputs;
		state->_remote_input_state[index] = kInputStateValid;
		// TODO: It's possible we're setting the input for the current frame but we already added a predicted frame.
		//       It's generally not the way to use the API, but we don't prevent it.
	} else if (state->_remote_input_state[index] == kInputStateInvalid) {
		gglog("  new input\n");
		// This is filling in an input we don't have yet and hasn't been predicted yet.
		state->_inputs.remote[index] = *inputs;
		state->_remote_input_state[index] = kInputStateValid;
	} else if (state->_remote_input_state[index] == kInputStatePredicted) {
		// This input was predicted. Does this new prediction match?
		if (GGPO_GameInputs_equal_(&state->_inputs.remote[index], inputs)) {
			gglog("  correct prediction\n");
			// If this is the oldest predicted input, advance to the next predicted input (if any).
			state->_remote_input_state[index] = kInputStateValid;
			if (state->_oldest_predicted_frame == frame) {
				// Store the oldest_predicted_frame because the next function may modify it.
				int const oldest_predicted_frame = state->_oldest_predicted_frame;
				GGPO_InputState_advance_oldest_predicted_input_(state);
				GGPO_InputState_invalidate_completed_frames_(state, oldest_predicted_frame);
			}
		} else {
			gglog("  misprediction\n");
			// This input was mispredicted. Store the correct value.
			state->_inputs.remote[index] = *inputs;
			state->_remote_input_state[index] = kInputStateMispredicted;
		}
	} else {
		gglog("  else input %d\n", state->_remote_input_state[index]);
	}

	return true;
}

void GGPO_InputState_get_input(struct GGPO_InputState* state, int frame, struct GGPO_GameInputs* out_inputs, bool* out_is_predicted)
{
	assert(state);
	assert(out_inputs);
	assert(out_is_predicted);

	gglog("[%.03f] get_input(%d)\n", time_in_seconds_from_start_(), frame);
	int const index = GGPO_InputState_get_input_queue_index_(frame);
	memcpy(out_inputs->inputs, state->_inputs.local[index].inputs, sizeof(out_inputs->inputs));
	struct GGPO_GameInputs* remote_inputs = &state->_inputs.remote[index];
	// Check if we need to predict the current remote input.
	if (state->_remote_input_state[index] == kInputStateInvalid) {
		gglog("get_input predicting frame %d\n", frame);
		int const previous_frame = frame - 1;
		int const previous_index = GGPO_InputState_get_input_queue_index_(previous_frame);
		// Because we only access frames in order, we will have at least predicted the previous frame.
		// Even when doing rollback, we will have previously visited this frame in normal order so
		// it should never be invalid.

		// TODO: Make sure this condition is covered by a test.
		if (previous_frame >= 0) {
			// TODO: I commented out this assert but can it be made more robust?
			//       It was firing after a rollback because the previous frame was corrected and then marked invalid.
			// assert(state->_remote_input_state[previous_index] != kInputStateInvalid);
			*remote_inputs = state->_inputs.remote[previous_index];
		}
		state->_remote_input_state[index] = kInputStatePredicted;
		if (state->_oldest_predicted_frame == GGPO_INPUT_STATE_FRAME_INVALID) {
			state->_oldest_predicted_frame = frame;
		}
	}
	if (state->_remote_input_state[index] == kInputStatePredicted) {
		*out_is_predicted = true;
	} else {
		*out_is_predicted = false;
	}
	for (size_t i = 0; i < sizeof(out_inputs->inputs); i++) {
		out_inputs->inputs[i] |= remote_inputs->inputs[i];
	}
}

void GGPO_InputState_get_current_input(struct GGPO_InputState* state, struct GGPO_GameInputs* out_inputs, bool* out_is_predicted)
{
	GGPO_InputState_get_input(state, state->_current_frame, out_inputs, out_is_predicted);
}

void GGPO_InputState_set_input_delay(struct GGPO_InputState* state, unsigned int frame_delay)
{
	int const frame_delta = (int)frame_delay - state->_input_delay;
	// If we're skipping forward we will need to fill in any blank inputs we would be skipping over.
	if (frame_delta > 0) {
		int const previous_delayed_frame = state->_current_frame + state->_input_delay;
		if (state->_current_local_frame_is_set) {
			int index = GGPO_InputState_get_input_queue_index_(previous_delayed_frame);
			struct GGPO_GameInputs const *current_input = &state->_inputs.local[index];
			for (int i = previous_delayed_frame + 1; i < previous_delayed_frame + frame_delta; i++) {
				index = GGPO_InputState_get_input_queue_index_(i);
				state->_inputs.local[index] = *current_input;
			}
		} else if (previous_delayed_frame > 0) {
			int index = GGPO_InputState_get_input_queue_index_(previous_delayed_frame - 1);
			struct GGPO_GameInputs const *previous_input = &state->_inputs.local[index];
			for (int i = previous_delayed_frame; i < previous_delayed_frame + frame_delta; i++) {
				index = GGPO_InputState_get_input_queue_index_(i);
				state->_inputs.local[index] = *previous_input;
			}
		}
	}
	state->_input_delay = frame_delay;
}

static void GGPO_InputState_log_remote_input_state_(struct GGPO_InputState* state)
{
#if GGPO_LOG_ENABLE
	size_t const input_length = sizeof(state->_remote_input_state) / sizeof(state->_remote_input_state[0]);
	int current_frame_index = GGPO_InputState_get_input_queue_index_(state->_current_frame);
	for (size_t i = 0; i < input_length; i++) {
		int s = state->_remote_input_state[i];
		if (s == kInputStateInvalid) {
			gglog("_");
		} else if (s == kInputStateValid) {
			gglog("o");
		} else if (s == kInputStatePredicted) {
			gglog("?");
		} else if (s == kInputStateMispredicted) {
			gglog("!");
		}
	}
	gglog("\n");
	for (size_t i = 0; i < input_length; i++) {
		if (i == current_frame_index) {
			gglog("^");
		} else {
			gglog(" ");
		}
	}
	gglog("\n");
#else
	(void)state;
#endif
}

void GGPO_InputState_frame_end(struct GGPO_InputState* state)
{
	gglog("input_frame_end(%d -> %d) [%d]\n", state->_current_frame, state->_current_frame + 1, state->_oldest_predicted_frame);
	GGPO_InputState_log_remote_input_state_(state);

	if (state->_oldest_predicted_frame == GGPO_INPUT_STATE_FRAME_INVALID) {
		gglog("invalidating input frame %d.\n", state->_current_frame);
		// We're not rolling back to this frame ever so let's invalidate it.
		int index = GGPO_InputState_get_input_queue_index_(state->_current_frame);
		state->_remote_input_state[index] = kInputStateInvalid;
	}

	state->_current_frame++;
	state->_current_local_frame_is_set = false;
}

bool GGPO_RollbackState_is_valid(struct GGPO_RollbackState const* state)
{
	return state->start != GGPO_INPUT_STATE_FRAME_INVALID;
}

struct GGPO_RollbackState GGPO_InputState_start_rollback(struct GGPO_InputState* state)
{
	gglog("->rollback start\n");
	int const oldest_predicted_frame = state->_oldest_predicted_frame;
	// Don't run rollback unless we have predicted frames.
	if (oldest_predicted_frame == GGPO_INPUT_STATE_FRAME_INVALID) {
		gglog("no rollback. oldest_predicted: %d\n", oldest_predicted_frame);
		return (struct GGPO_RollbackState){ .start = GGPO_INPUT_STATE_FRAME_INVALID };
	}

	int const oldest_predicted_frame_index = GGPO_InputState_get_input_queue_index_(oldest_predicted_frame);
	int const oldest_predicted_frame_state = state->_remote_input_state[oldest_predicted_frame_index];
	// Even if we haven't resolved all the predicted frames we still want to run rollback.
	// BUT! We want to be able to advance our oldest predicted frame by at least one so make sure we a misprediction.
	if (oldest_predicted_frame_state == kInputStatePredicted) {
		gglog("<- oldest frame predicted\n");
		return (struct GGPO_RollbackState){ .start = GGPO_INPUT_STATE_FRAME_INVALID };
	}

	// We should never be starting rollback from a correctly predicted frame because
	// we should have already advanced the oldest_predicted_frame past this frame.
	assert(oldest_predicted_frame_state != kInputStateValid);
	state->_oldest_predicted_frame = GGPO_INPUT_STATE_FRAME_INVALID;
	gglog("finding oldest predicted from %d -> %d\n", oldest_predicted_frame, state->_current_frame);
	for (int frame = oldest_predicted_frame + 1; frame < state->_current_frame; frame++) {
		int const index = GGPO_InputState_get_input_queue_index_(frame);
		int const input_state = state->_remote_input_state[index];
		if (input_state == kInputStatePredicted) {
			state->_oldest_predicted_frame = frame;
			break;
		}
	}
	return (struct GGPO_RollbackState){
		.start = oldest_predicted_frame,
		.count = state->_current_frame - oldest_predicted_frame
	};
}

void GGPO_InputState_end_rollback(struct GGPO_InputState* state, struct GGPO_RollbackState const* rollback)
{
	gglog("end rollback %d -> %d\n", rollback->start, rollback->start + rollback->count - 1);
	for (int i = 0; i < rollback->count; i++) {
		int const index = GGPO_InputState_get_input_queue_index_(rollback->start + i);
		int const input_state = state->_remote_input_state[index];
		if (input_state == kInputStatePredicted) {
			break;
		}
		gglog("  invalidate %d\n", rollback->start + i);
		state->_remote_input_state[index] = kInputStateInvalid;
	}
}
