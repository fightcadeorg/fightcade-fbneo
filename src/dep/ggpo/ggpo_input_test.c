#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "ggpo_input.h"

// zig cc -target x86-windows-gnu -std=c11 -O2 ggpo_input.c ggpo_input_test.c -o ggpo_input_test.exe
// clang -std=c99 -Wall -Wextra -g ggpo_input.c ggpo_input_test.c -o ggpo_input_test

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define TEST(...) do { if (!(__VA_ARGS__)) { fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, #__VA_ARGS__); abort(); } } while(0)


static void GGPO_GameInputs_print_(struct GGPO_GameInputs const* inputs, char const* label)
{
	printf("%s: ", label);
	for (size_t i = 0; i < ARRAY_LEN(inputs->inputs); i ++) {
		printf("%02x ", inputs->inputs[i]);
	}
	printf("\n");
}

static bool GGPO_GameInputs_equal_(struct GGPO_GameInputs const* lhs, struct GGPO_GameInputs const* rhs)
{
	if(0 != memcmp(lhs->inputs, rhs->inputs, sizeof(lhs->inputs))) {
		GGPO_GameInputs_print_(lhs, "lhs");
		GGPO_GameInputs_print_(rhs, "rhs");
		return false;
	}
	return true;
}

static struct GGPO_GameInputs GGPO_GameInputs_create_(uint8_t pattern, int player)
{
	struct GGPO_GameInputs game_inputs;
	size_t const half_size = sizeof(game_inputs.inputs) / 2;
	if (player == 0) {
		memset(game_inputs.inputs, pattern, half_size);
		memset(game_inputs.inputs + half_size, 0, half_size);
	} else {
		memset(game_inputs.inputs, 0, half_size);
		memset(game_inputs.inputs + half_size, pattern, half_size);
	}
	return game_inputs;
}

static struct GGPO_GameInputs GGPO_GameInputs_combine_(struct GGPO_GameInputs const* lhs, struct GGPO_GameInputs const* rhs)
{
	struct GGPO_GameInputs result;
	for (size_t i = 0; i < ARRAY_LEN(lhs->inputs); i ++) {
		result.inputs[i] = lhs->inputs[i] | rhs->inputs[i];
	}
	return result;
}

static void GGPO_InputState_print_remote_state_(struct GGPO_InputState const* state)
{
	fprintf(stderr, "' ' = invalid/unset input\n");
	fprintf(stderr, "'?' = predicted input\n");
	fprintf(stderr, "'o' = valid/confirmed input\n");
	fprintf(stderr, "'x' = mispredicted input\n");
	for (int i = 0; i < ARRAY_LEN(state->_remote_input_state); i++) {
		fprintf(stderr, ",--");
	}
	fprintf(stderr, ",\n");

	int current_frame_index = state->_current_frame % ARRAY_LEN(state->_remote_input_state);

	for (int i = 0; i < ARRAY_LEN(state->_remote_input_state); i++) {
		int s = state->_remote_input_state[i];
		fprintf(stderr, "|");
		if (s == kInputStateInvalid) {
			fprintf(stderr, "  ");
		} else if (s == kInputStatePredicted) {
			fprintf(stderr, "? ");
		} else if (s == kInputStateValid) {
			fprintf(stderr, "o ");
		} else if (s == kInputStateMispredicted) {
			fprintf(stderr, "x ");
		}
	}
	fprintf(stderr, "|\n");
	for (int i = 0; i < ARRAY_LEN(state->_remote_input_state); i++) {
		fprintf(stderr, "|%02x", state->_inputs.remote[i].inputs[0]);
	}
	fprintf(stderr, "|\n");

	for (int i = 0; i < ARRAY_LEN(state->_remote_input_state); i++) {
		fprintf(stderr, "'--");
	}
	fprintf(stderr, "'\n");

	for (int i = 0; i < ARRAY_LEN(state->_remote_input_state); i++) {
		if (i == current_frame_index) {
			fprintf(stderr, " ^^");
		} else {
			fprintf(stderr, "   ");
		}
	}
	fprintf(stderr, "\n");

	for (int i = 0; i < ARRAY_LEN(state->_remote_input_state); i++) {
		if (i == current_frame_index) {
			fprintf(stderr, " %-2d", state->_current_frame);
		} else {
			fprintf(stderr, "   ");
		}
	}
	fprintf(stderr, "\n");
}

struct TestState {
	struct GGPO_GameInputs inputs_local[10];
	struct GGPO_GameInputs inputs_remote[10];
};

static void TestState_init_(struct TestState* state)
{
	for (uint8_t i = 0; i < ARRAY_LEN(state->inputs_local); i ++) {
		if (i == 0) {
			state->inputs_local[i] = GGPO_GameInputs_create_(0xff, 0);	
		} else {
			state->inputs_local[i] = GGPO_GameInputs_create_(i, 0);
		}
		
	}
	for (uint8_t i = 0; i < ARRAY_LEN(state->inputs_remote); i ++) {
		if (i == 0) {
			state->inputs_remote[i] = GGPO_GameInputs_create_(0xff, 1);
		} else {
			state->inputs_remote[i] = GGPO_GameInputs_create_(i, 1);
		}
	}
}

#define REMOTE_INPUT(_frame, _input) \
	do { \
		struct GGPO_GameInputs remote_inputs = { .inputs = { (_input) }}; \
		bool success = GGPO_InputState_add_remote_input(&input_state, _frame, &remote_inputs); \
		if (!success) { \
			fprintf(stderr, "%s(%d): Failed to add remote input for frame %d of value %#02x.\n", __FILE__, __LINE__, _frame, (_input)); \
			GGPO_InputState_print_remote_state_(&input_state); \
			abort(); \
		} \
	} while(0)
#define LOCAL_INPUT(_input) \
	do { \
		struct GGPO_GameInputs remote_inputs = { .inputs = { (_input) }}; \
		bool success = GGPO_InputState_add_local_input(&input_state, &remote_inputs); \
		if (!success) { \
			fprintf(stderr, "%s(%d): Failed to add local input for frame %d of value %#02x.\n", __FILE__, __LINE__, input_state._current_frame, (_input)); \
			GGPO_InputState_print_remote_state_(&input_state); \
			abort(); \
		} \
	} while(0)
#define EXPECT_INPUT_(_input, _is_predicted) \
	do { \
		int _frame = input_state._current_frame; \
		struct GGPO_GameInputs remote_inputs = {}; \
		bool is_predicted; \
		GGPO_InputState_get_input(&input_state, _frame, &remote_inputs, &is_predicted); \
		if (is_predicted != _is_predicted) { \
			fprintf(stderr, "%s(%d): Frame %d inputs should have been %s.\n", __FILE__, __LINE__, _frame, _is_predicted ? "predicted but was not" : "not predicted but was"); \
			GGPO_InputState_print_remote_state_(&input_state); \
			abort(); \
		} \
		if (remote_inputs.inputs[0] != (_input)) { \
			fprintf(stderr, "%s(%d): Frame %d input expected %#02x but got %#02x.\n", __FILE__, __LINE__, _frame, (_input), remote_inputs.inputs[0]); \
			GGPO_InputState_print_remote_state_(&input_state); \
			abort(); \
		} \
	} while(0)
#define EXPECT_INPUT_PREDICTED(_input) EXPECT_INPUT_((_input), true)
#define EXPECT_INPUT(_input) EXPECT_INPUT_((_input), false)
#define NEXT_FRAME GGPO_InputState_frame_end(&input_state);
#define EXPECT_ROLLBACK_FROM(_frame, ...) \
	do { \
		struct GGPO_RollbackState rollback = GGPO_InputState_start_rollback(&input_state); \
		if (rollback.start != (_frame)) { \
			if ((_frame) == GGPO_INPUT_STATE_FRAME_INVALID) { \
				fprintf(stderr, "%s(%d): On frame %d, expected no rollback but got rollback from frame %d.\n", __FILE__, __LINE__, input_state._current_frame, rollback.start); \
			} else if (rollback.start == GGPO_INPUT_STATE_FRAME_INVALID) { \
				fprintf(stderr, "%s(%d): On frame %d, expected rollback from frame %d but got no rollback.\n", __FILE__, __LINE__, input_state._current_frame, (_frame)); \
			} else { \
				fprintf(stderr, "%s(%d): On frame %d, expected rollback from frame %d but got rollback from frame %d.\n", __FILE__, __LINE__, input_state._current_frame, (_frame), rollback.start); \
			} \
			GGPO_InputState_print_remote_state_(&input_state); \
			abort(); \
		} else if (rollback.start != GGPO_INPUT_STATE_FRAME_INVALID) { \
			int expected_inputs[] = __VA_ARGS__; \
			int expected_count = ARRAY_LEN(expected_inputs); \
			if (rollback.count != expected_count) { \
				fprintf(stderr, "%s(%d): On frame %d, expected %d frames of rollback but got %d.\n", __FILE__, __LINE__, input_state._current_frame, expected_count, rollback.count); \
				GGPO_InputState_print_remote_state_(&input_state); \
				abort(); \
			} \
			for (int i = 0; i < rollback.count; i++) { \
				struct GGPO_GameInputs remote_inputs = {}; \
				bool _is_predicted; \
				GGPO_InputState_get_input(&input_state, rollback.start + i, &remote_inputs, &_is_predicted); \
				if (remote_inputs.inputs[0] != expected_inputs[i]) { \
					fprintf(stderr, "%s(%d): While rolling back, on frame %d expected input value %#02x, but got %#02x.\n", __FILE__, __LINE__, rollback.start + i, expected_inputs[i], remote_inputs.inputs[0]); \
					GGPO_InputState_print_remote_state_(&input_state); \
					abort(); \
				} \
			} \
			GGPO_InputState_end_rollback(&input_state, &rollback); \
		} \
	} while(0)
#define EXPECT_NO_ROLLBACK EXPECT_ROLLBACK_FROM(GGPO_INPUT_STATE_FRAME_INVALID, {0})

int main(void)
{
	struct GGPO_GameInputs inputs_zero = {};

	struct TestState test_state;
	TestState_init_(&test_state);

	struct GGPO_InputState input_state;
	struct GGPO_GameInputs inputs;
	bool is_predicted;

	// Test normal adding inputs. Add enough to fill the whole ring buffer.
	{
		GGPO_InputState_init(&input_state);

		for (int i = 0; i < ARRAY_LEN(input_state._inputs.local); i++) {
			LOCAL_INPUT(i);
			REMOTE_INPUT(i, i);
			EXPECT_INPUT(i);
			NEXT_FRAME;
		}

		inputs = (struct GGPO_GameInputs){ .inputs = { 0x55 } };
		// We shouldn't be able to add new local inputs because none have been acked yet.
		TEST(!GGPO_InputState_add_local_input(&input_state, &inputs));
		// Ack the first input and we should be able add a new local input, but only one.
		GGPO_InputState_ack_local_input(&input_state, 0);
		TEST(GGPO_InputState_add_local_input(&input_state, &inputs));
		GGPO_InputState_frame_end(&input_state);
		// It's full again so adding another will fail.
		TEST(!GGPO_InputState_add_local_input(&input_state, &inputs));
	}


	// Test that we can't overwrite un-acked inputs.
	{
		GGPO_InputState_init(&input_state);

		for (int i = 0; i < GGPO_INPUT_STATE_QUEUE_SIZE; i++) {
			int const input_i = i % ARRAY_LEN(test_state.inputs_local);
			TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[input_i]));
			TEST(GGPO_InputState_add_remote_input(&input_state, i, &test_state.inputs_remote[input_i]));
		}
	}

	// Test with a delay on the local inputs.
	{
		GGPO_InputState_init(&input_state);
		
		int delay = 4;
		GGPO_InputState_set_input_delay(&input_state, delay);
		for (int i = 0; i < GGPO_INPUT_STATE_QUEUE_SIZE; i++) {
			int const input_i = i % ARRAY_LEN(test_state.inputs_local);
			TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[input_i]));
			TEST(GGPO_InputState_add_remote_input(&input_state, i, &test_state.inputs_remote[input_i]));
			GGPO_InputState_get_current_input(&input_state, &inputs, &is_predicted);
			struct GGPO_GameInputs const* expected_local_inputs;
			if (i < delay) {
				expected_local_inputs = &inputs_zero;
			} else {
				int const delayed_input_i = (i - delay) % ARRAY_LEN(test_state.inputs_local);
				expected_local_inputs = &test_state.inputs_local[delayed_input_i];
			}
			struct GGPO_GameInputs expected_inputs = GGPO_GameInputs_combine_(expected_local_inputs, &test_state.inputs_remote[input_i]);
			TEST(GGPO_GameInputs_equal_(&expected_inputs, &inputs));
			GGPO_InputState_frame_end(&input_state);
		}
	}

	// Test delay adjustments.
	{
		GGPO_InputState_init(&input_state);
		struct GGPO_GameInputs expected_inputs;

		struct {
			int delay;
			struct {
				int local;
				int remote;
			} expected;
		} steps[] = {
			// Pictured is the state of local inputs after adjusting delay and adding inputs.
			//   v
			// [ p p 0 x x x x x x ]
			{ .delay =  2, .expected = { .local = -1, .remote = 0 } },
			//     v
			// [ p p 0 0 0 1 x x x ]
			{ .delay =  4, .expected = { .local = -1, .remote = 1 } },
			//       v
			// [ p p 0 0 0 1 2 x x ]
			{ .delay = -1, .expected = { .local =  0, .remote = 2 } },
			//         v
			// [ p p 0 0 0 1 2 3 x ]
			{ .delay = -1, .expected = { .local =  0, .remote = 3 } },
			//           v
			// [ p p 0 0 0 1 2 3 4 ]
			{ .delay = -1, .expected = { .local =  0, .remote = 4 } },
			//             v
			// [ p p 0 0 0 1 5 3 4 ]
			{ .delay =  1, .expected = { .local =  1, .remote = 5 } },
			//               v
			// [ p p 0 0 0 1 5 6 4 ]
			{ .delay =  1, .expected = { .local =  5, .remote = 6 } },
		};
		for (size_t i = 0; i < ARRAY_LEN(steps); i ++) {
			if (steps[i].delay != -1) {
				GGPO_InputState_set_input_delay(&input_state, steps[i].delay);
			}
			TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[i]));
			TEST(GGPO_InputState_add_remote_input(&input_state, i, &test_state.inputs_remote[i]));
			GGPO_InputState_get_current_input(&input_state, &inputs, &is_predicted);
			if (steps[i].expected.local == -1) {
				expected_inputs = GGPO_GameInputs_combine_(&inputs_zero, &test_state.inputs_remote[steps[i].expected.remote]);
			} else {
				expected_inputs = GGPO_GameInputs_combine_(&test_state.inputs_local[steps[i].expected.local], &test_state.inputs_remote[steps[i].expected.remote]);
			}
			TEST(GGPO_GameInputs_equal_(&expected_inputs, &inputs));
			GGPO_InputState_frame_end(&input_state);
		}
	}

	// Test delay changed after setting inputs.
	// It should copy our most recent input forward.
	{
		GGPO_InputState_init(&input_state);
		struct GGPO_GameInputs expected_inputs;

		// Test that changing the delay after we've already added inputs doesn't retroactively apply the delay.
		TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[0]));
		TEST(GGPO_InputState_add_remote_input(&input_state, 0, &test_state.inputs_remote[0]));
		GGPO_InputState_set_input_delay(&input_state, 4);
		GGPO_InputState_get_current_input(&input_state, &inputs, &is_predicted);
		expected_inputs = GGPO_GameInputs_combine_(&test_state.inputs_local[0], &test_state.inputs_remote[0]);
		TEST(GGPO_GameInputs_equal_(&expected_inputs, &inputs));
		GGPO_InputState_frame_end(&input_state);

		// Adding new inputs on the next frame should now apply the delay.
		TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[1]));
		TEST(GGPO_InputState_add_remote_input(&input_state, 1, &test_state.inputs_remote[1]));
		GGPO_InputState_get_current_input(&input_state, &inputs, &is_predicted);
		expected_inputs = GGPO_GameInputs_combine_(&test_state.inputs_local[0], &test_state.inputs_remote[1]);
		TEST(GGPO_GameInputs_equal_(&expected_inputs, &inputs));
	}

	// Test getting out a single local input.
	{
		GGPO_InputState_init(&input_state);

		// No inputs added yet so getting anything will fail.
		TEST(!GGPO_InputState_get_local_input(&input_state, 0, &inputs));
		TEST(!GGPO_InputState_get_local_input(&input_state, 1, &inputs));
		TEST(!GGPO_InputState_get_local_input(&input_state, -1, &inputs));
		TEST(!GGPO_InputState_get_local_input(&input_state, 100, &inputs));

		// Add one input and we should be able to get frame 0 out.
		TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[0]));
		TEST(GGPO_InputState_get_local_input(&input_state, 0, &inputs));
		TEST(GGPO_GameInputs_equal_(&inputs, &test_state.inputs_local[0]));
		// Getting anything else still fails.
		TEST(!GGPO_InputState_get_local_input(&input_state, 1, &inputs));
		TEST(!GGPO_InputState_get_local_input(&input_state, -1, &inputs));
		TEST(!GGPO_InputState_get_local_input(&input_state, 100, &inputs));

		// End frame 0 and getting frame 1 still fails.
		GGPO_InputState_frame_end(&input_state);
		TEST(!GGPO_InputState_get_local_input(&input_state, 1, &inputs));

		// Fill the ring buffer.
		int end_frame = 40;
		for (int frame = 1; frame < end_frame; frame++) {
			TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[frame % ARRAY_LEN(test_state.inputs_local)]));
			GGPO_InputState_ack_local_input(&input_state, frame);
			GGPO_InputState_frame_end(&input_state);
		}

		// Can't get frames outside the ringbuffer.
		TEST(!GGPO_InputState_get_local_input(&input_state, 0, &inputs));
		TEST(!GGPO_InputState_get_local_input(&input_state, end_frame, &inputs));
		// Check that we can get inputs from the ends of the ring buffer boundaries.
		int frame = end_frame - 1;
		TEST(GGPO_InputState_get_local_input(&input_state, frame, &inputs));
		TEST(GGPO_GameInputs_equal_(&inputs, &test_state.inputs_local[frame % ARRAY_LEN(test_state.inputs_local)]));
		frame = end_frame - ARRAY_LEN(input_state._inputs.local);
		TEST(GGPO_InputState_get_local_input(&input_state, frame, &inputs));
		TEST(GGPO_GameInputs_equal_(&inputs, &test_state.inputs_local[frame % ARRAY_LEN(test_state.inputs_local)]));
	}

	// Test extracting local inputs.
	{
		GGPO_InputState_init(&input_state);

		struct GGPO_GameInputs input_array[4];
		size_t input_count = ARRAY_LEN(input_array);
		int frame = 0;

		int start_frame = 0;
		// Test that we get an error when trying to copy inputs out when none have been added yet.
		TEST(!GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));
		TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[frame]));
		// There's one frame of input (frame 0). Test that we can copy out that input.
		input_count = ARRAY_LEN(input_array);
		TEST(GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));
		TEST(input_count == 1);
		TEST(start_frame == 0);
		TEST(GGPO_GameInputs_equal_(&input_array[0], &test_state.inputs_local[0]));

		GGPO_InputState_frame_end(&input_state);
		frame++;

		// We've advanced to frame 1. Test that we still only get a single input.
		input_count = ARRAY_LEN(input_array);
		TEST(GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));
		TEST(input_count == 1);
		TEST(start_frame == 0);
		TEST(GGPO_GameInputs_equal_(&input_array[0], &test_state.inputs_local[0]));
		// Test that we still only get one frame.
		input_count = ARRAY_LEN(input_array);
		TEST(GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));
		TEST(input_count == 1);
		TEST(start_frame == 0);
		TEST(GGPO_GameInputs_equal_(&input_array[0], &test_state.inputs_local[0]));
		// Set an input for this frame and check that we get two inputs out.
		TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[frame]));
		input_count = ARRAY_LEN(input_array);
		TEST(GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));
		TEST(input_count == 2);
		TEST(start_frame == 0);
		TEST(GGPO_GameInputs_equal_(&input_array[0], &test_state.inputs_local[0]));
		TEST(GGPO_GameInputs_equal_(&input_array[1], &test_state.inputs_local[1]));

		GGPO_InputState_frame_end(&input_state);
		frame++;

		// Write enough inputs to fill the ring buffer.
		while (GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[frame % ARRAY_LEN(test_state.inputs_local)])) {
			GGPO_InputState_frame_end(&input_state);
			frame++;
			if (frame <= (int)ARRAY_LEN(input_array)) {
				input_count = ARRAY_LEN(input_array);
				TEST(GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));
				TEST(start_frame == 0);
				TEST((int)input_count == frame);
			} else {
				TEST(!GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));
			}
		}

		// Test that we get an error if there's not enough space in the output buffer.
		input_count = ARRAY_LEN(input_array);
		TEST(!GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));

		// Ack inputs then we should be able to copy out the inputs again.
		// There are no inputs yet for the current frame so we discount that one with the "- 1".
		GGPO_InputState_ack_local_input(&input_state, frame - ARRAY_LEN(input_array) - 1);
		input_count = ARRAY_LEN(input_array);
		TEST(GGPO_InputState_get_all_unacked_local_inputs(&input_state, &start_frame, input_array, &input_count));
		TEST(input_count == ARRAY_LEN(input_array));
		TEST(start_frame == frame - ARRAY_LEN(input_array));
	}

	// Test that after adding multiple frames of remote input in one frame we can read them all out.
	{
		GGPO_InputState_init(&input_state);

		TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[0]));
		for (int i = 0; i < 4; i++) {
			TEST(GGPO_InputState_add_remote_input(&input_state, i, &test_state.inputs_remote[i]));
		}
		GGPO_InputState_frame_end(&input_state);
		for (int i = 1; i < 4; i++) {
			TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[i]));
			struct GGPO_GameInputs expected_inputs = GGPO_GameInputs_combine_(&test_state.inputs_local[i], &test_state.inputs_remote[i]);
			GGPO_InputState_get_current_input(&input_state, &inputs, &is_predicted);
			TEST(GGPO_GameInputs_equal_(&expected_inputs, &inputs));
			GGPO_InputState_frame_end(&input_state);
		}
	}

	// Test that we can store previous remote inputs that haven't been read yet.
	{
		GGPO_InputState_init(&input_state);

		// Go one frame without adding any remote inputs and don't read any.
		TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[0]));
		GGPO_InputState_frame_end(&input_state);
		TEST(GGPO_InputState_add_local_input(&input_state, &test_state.inputs_local[1]));
		TEST(GGPO_InputState_add_remote_input(&input_state, 0, &test_state.inputs_remote[0]));
		TEST(GGPO_InputState_add_remote_input(&input_state, 1, &test_state.inputs_remote[1]));
		struct GGPO_GameInputs expected_inputs = GGPO_GameInputs_combine_(&test_state.inputs_local[0], &test_state.inputs_remote[0]);
		GGPO_InputState_get_input(&input_state, 0, &inputs, &is_predicted);
		TEST(!is_predicted);
		TEST(GGPO_GameInputs_equal_(&expected_inputs, &inputs));
		expected_inputs = GGPO_GameInputs_combine_(&test_state.inputs_local[1], &test_state.inputs_remote[1]);
		GGPO_InputState_get_input(&input_state, 1, &inputs, &is_predicted);
		TEST(!is_predicted);
		GGPO_InputState_frame_end(&input_state);
	}

	// Test rollback.
	{
		// Generate the following remote input state.
		// - = unset / invalid
		// o = set / valid
		// p = predicted
		// x = mispredicted
		//
		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |o|p|p|o|-|-|
		// '-'-'-'-'-'-'
		//
		// struct GGPO_GameInputs expected_inputs;

		GGPO_InputState_init(&input_state);

		// 0
		REMOTE_INPUT(0, 0x0);
		REMOTE_INPUT(3, 0x3);
		REMOTE_INPUT(4, 0x4);
		EXPECT_INPUT(0x0);
		EXPECT_NO_ROLLBACK;
		NEXT_FRAME;

		// 1
		// Remote frame 1 will be predicted from frame 0.
		// We still can't do rollback because we still have predicted frames.
		EXPECT_INPUT_PREDICTED(0x0);
		EXPECT_NO_ROLLBACK;
		NEXT_FRAME;

		// 2
		// Remote frame 2 will be predicted from frame 1 which is from frame 0.
		// We still can't do rollback because we still have predicted frames.
		EXPECT_INPUT_PREDICTED(0x0);
		EXPECT_NO_ROLLBACK;
		NEXT_FRAME;

		// 3
		// Add real inputs for frame 2.
		REMOTE_INPUT(2, 0x2);
		// Remote frame 3 already has valid inputs.
		// We still can't rollback because we're still missing frame 1.
		EXPECT_INPUT(0x3);
		EXPECT_NO_ROLLBACK;
		NEXT_FRAME;

		// 4
		// Add real inputs for frame 1.
		REMOTE_INPUT(1, 0x1);
		// Because we also already had valid inputs for frame 4, we should now do rollback.
		EXPECT_ROLLBACK_FROM(1, {0x1, 0x2, 0x3});
		NEXT_FRAME;

		// 5
		// We leave frames 5 and 6 to be predicted.
		// The predicted input will be from frame 4.
		EXPECT_INPUT_PREDICTED(0x4);
		EXPECT_NO_ROLLBACK;
		NEXT_FRAME;

		// 6
		EXPECT_INPUT_PREDICTED(0x4);
		EXPECT_NO_ROLLBACK;
		NEXT_FRAME;

		// 7
		// Supply remote frames 5 and 7.
		// We do rollback since we got our oldest predicted frame even though we're still waiting for frame 6.
		REMOTE_INPUT(5, 0x5);
		REMOTE_INPUT(7, 0x7);
		EXPECT_INPUT(0x7);
		EXPECT_ROLLBACK_FROM(0x5, {0x5, 0x4});
		NEXT_FRAME;

		// 8
		// We supply the predicted frame 6 and now we can do rollback.
		REMOTE_INPUT(6, 0x6);
		EXPECT_ROLLBACK_FROM(6, {0x6, 0x7});
		REMOTE_INPUT(8, 0x8);
		EXPECT_INPUT(0x8);
		NEXT_FRAME;
	}

	// Test when predicted inputs match the actual inputs.
	{
		// Generate the following remote input state.
		// - = unset / invalid
		// o = set / valid
		// p = predicted
		// x = mispredicted
		//
		GGPO_InputState_init(&input_state);

		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |p|-|-|-|-|-|
		// '-'-'-'-'-'-'
		//  ^
		LOCAL_INPUT(0x0);
		EXPECT_INPUT_PREDICTED(0x0);
		NEXT_FRAME;
		EXPECT_NO_ROLLBACK;

		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |x|p|-|-|-|-|
		// '-'-'-'-'-'-'
		//    ^
		LOCAL_INPUT(0x1);
		REMOTE_INPUT(0, 0xff);
		EXPECT_ROLLBACK_FROM(0, {0xff});
		EXPECT_INPUT_PREDICTED(0xff);
		NEXT_FRAME;

		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |-|o|-|-|-|-|
		// '-'-'-'-'-'-'
		//      ^
		LOCAL_INPUT(0x2);
		REMOTE_INPUT(1, 0xff);
		EXPECT_NO_ROLLBACK;
		EXPECT_INPUT_PREDICTED(0xff);
	}

	// Test that rollback will run as long as we've got a mispredicted oldest frame
	// and _not_ when it's been correctly predicted or hasn't had a real input yet.
	{
		// Input state diagram key.
		// - = unset / invalid
		// o = set / valid
		// p = predicted
		// x = mispredicted
		//

		// struct GGPO_GameInputs expected_inputs;
		GGPO_InputState_init(&input_state);

		// Start with a valid input for the first frame.
		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |o|-|-|-|-|-|
		// '-'-'-'-'-'-'
		//  ^
		REMOTE_INPUT(0, 0x0);
		EXPECT_INPUT(0x0);
		NEXT_FRAME;
		EXPECT_NO_ROLLBACK;

		// Predict the second remote input.
		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |o|p|-|-|-|-|
		// '-'-'-'-'-'-'
		//    ^
		EXPECT_INPUT_PREDICTED(0x0);
		NEXT_FRAME;
		EXPECT_NO_ROLLBACK;

		// Predict the third remote input.
		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |o|p|p|-|-|-|
		// '-'-'-'-'-'-'
		//      ^
		EXPECT_INPUT_PREDICTED(0x0);
		NEXT_FRAME;
		EXPECT_NO_ROLLBACK;

		// Predict the third remote input.
		// We also get the input for frame 1 and it was a correct prediction.
		// Because the prediction was correct there is no rollback.
		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |o|o|p|p|-|-|
		// '-'-'-'-'-'-'
		//        ^
		REMOTE_INPUT(1, 0x0);
		EXPECT_INPUT_PREDICTED(0x0);
		NEXT_FRAME;
		EXPECT_NO_ROLLBACK;

		// Predict the fourth remote input.
		// We also get the input for frame 2 and it was a misprediction.
		// Because we mispredicted we do rollback.
		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |o|o|m|p|p|-|
		// '-'-'-'-'-'-'
		//          ^
		REMOTE_INPUT(2, 2);
		EXPECT_INPUT_PREDICTED(0x0);
		NEXT_FRAME;
		EXPECT_ROLLBACK_FROM(2, {0x2, 0x0, 0x0});

		// Predict the fifth remote input.
		// We also get the input for frame 4 and it was a misprediction.
		// Because is isn't the oldest predicted frame we do no rollback.
		//  0 1 2 3 4 5
		// .-.-.-.-.-.-.
		// |o|o|o|p|m|p|
		// '-'-'-'-'-'-'
		//            ^
		REMOTE_INPUT(4, 0x4);
		EXPECT_INPUT_PREDICTED(0x4);
		NEXT_FRAME;
		EXPECT_NO_ROLLBACK;
	}

	{
		struct GGPO_GameInputs expected_inputs;
		GGPO_InputState_init(&input_state);

		// 0
		EXPECT_NO_ROLLBACK;
		EXPECT_INPUT_PREDICTED(0x0);
		NEXT_FRAME;

		// 1
		// Add input for frame 0 that matches the prediction.
		REMOTE_INPUT(0, 0x0);
		EXPECT_NO_ROLLBACK;
		// Still no inputs for this frame yet so we predict again.
		EXPECT_INPUT_PREDICTED(0x0);
		NEXT_FRAME;

		// 2
		REMOTE_INPUT(1, 0x1);
		REMOTE_INPUT(2, 0x2);
		// Though we have a valid input for this frame as well as the last one
		// we still only rollback one frame because we haven't finished the current frame yet.
		EXPECT_ROLLBACK_FROM(1, {0x1});
		EXPECT_INPUT(0x2);
		NEXT_FRAME;

		// 3
		EXPECT_NO_ROLLBACK;
		EXPECT_INPUT_PREDICTED(0x2);
		NEXT_FRAME;

		// 4
		EXPECT_NO_ROLLBACK;
		EXPECT_INPUT_PREDICTED(0x2);
		NEXT_FRAME;

		// 5
		REMOTE_INPUT(3, 0x3);
		// We do a rollback because we got an input for our oldest predicted input.
		EXPECT_ROLLBACK_FROM(3, {0x3, 0x2});
		EXPECT_INPUT_PREDICTED(0x2);
		NEXT_FRAME;

		// 6
		// We get the input for frame 5 and it is a misprediction but we can't do rollback
		// because we don't have our oldest predicted frame (4) yet.
		REMOTE_INPUT(5, 0x5);
		EXPECT_NO_ROLLBACK;
		EXPECT_INPUT_PREDICTED(0x5);
		NEXT_FRAME;

		// 7
		// Get all inputs up to current.
		// Our oldest predicted input turns out to be a correct prediction so it turns valid
		// and we run rollback skipping that frame.
		REMOTE_INPUT(4, 0x2);
		REMOTE_INPUT(6, 0x6);
		REMOTE_INPUT(7, 0x7);
		EXPECT_ROLLBACK_FROM(5, {0x5, 0x6});
		EXPECT_INPUT(0x7);
		NEXT_FRAME;

		// 8+
		// Fill the whole ring buffer with correct inputs.
		for (int i = 0; i < ARRAY_LEN(input_state._inputs.remote); i++) {
			REMOTE_INPUT(8 + i, 0x8 + i);
			EXPECT_NO_ROLLBACK;
			EXPECT_INPUT(0x8 + i);
			NEXT_FRAME;
		}

		// The next frame will be predicted.
		EXPECT_NO_ROLLBACK;
		int frame = 0x8 + ARRAY_LEN(input_state._inputs.remote);
		EXPECT_INPUT_PREDICTED(frame - 1);
		NEXT_FRAME;

		// Then we will get all inputs the next frame and the prediction will be correct.
		REMOTE_INPUT(frame, frame - 1);
		REMOTE_INPUT(frame + 1, frame + 1);
		EXPECT_NO_ROLLBACK;
		EXPECT_INPUT(frame + 1);
		NEXT_FRAME;
	}

#undef EXPECT_NO_ROLLBACK
#undef EXPECT_ROLLBACK_FROM
#undef NEXT_FRAME
#undef EXPECT_FRAME
#undef EXPECT_FRAME_PREDICTED
#undef EXPECT_FRAME_
#undef REMOTE_INPUT

	printf("tests passed\n");
	return 0;
}