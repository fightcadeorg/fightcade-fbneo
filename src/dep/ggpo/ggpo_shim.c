#include <libloaderapi.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <process.h>

#include <timeapi.h>

#define GGPO_EXPORT __declspec(dllexport)
#include <ggpoclient.h>
#include <ggponet.h>

// zig cc -target x86-windows-gnu -Iinclude -std=c11 -O2 -shared -Wl,--out-implib,ggponet.lib ggpo_shim.c -o ggponet.dll

struct ShimFunctions {
	GGPOSession* (*ggpo_client_connect)(GGPOSessionCallbacks *cb, char *game, char *matchid, int serverport);
	bool (*ggpo_client_chat)(GGPOSession* session, char *text);
	bool (*ggpo_client_set_game_event)(GGPOSession* session, GGPOClientGameEventType type, void *data);
	int (*ggpo_set_frame_delay)(GGPOSession* session, int frame_delay);

	GGPOSession* (*ggpo_start_session)(GGPOSessionCallbacks *cb, char *game, int localport, char *remoteip, int remoteport, int player_num);
	GGPOSession* (*ggpo_start_synctest)(GGPOSessionCallbacks *cb, char *game, int frames);
	GGPOSession* (*ggpo_start_streaming)(GGPOSessionCallbacks *cb, char *game, char *matchid, int port);
	GGPOSession* (*ggpo_start_replay)(GGPOSessionCallbacks *cb, char *file);
	void (*ggpo_close_session)(GGPOSession* session);
	bool (*ggpo_idle)(GGPOSession* session, int timeout);
	bool (*ggpo_synchronize_input)(GGPOSession* session, void* values, int size, int players);
	bool (*ggpo_advance_frame)(GGPOSession* session);
	bool (*ggpo_get_stats)(GGPOSession* session, GGPONetworkStats *stats);
	void (*ggpo_log)(GGPOSession* session, char* fmt, ...);
	void (*ggpo_logv)(GGPOSession* session, char* fmt, va_list args);
};

static struct ShimFunctions g_functions = {};

static int g_indentation = 0;

__attribute__((__format__ (__printf__, 1, 2)))
static void log(char const* format, ...)
{
	static FILE* file = NULL;
	if (!file) {
		char filename[255];
		int chars_printed = snprintf(filename, sizeof(filename), "c:\\users\\ponder\\log_shim_%d.log", _getpid());
		assert(chars_printed < sizeof(filename));
		file = fopen(filename, "wb");
		assert(file);
	}
	for (int i = 0; i < g_indentation; i++) {
		fprintf(file, "    ");
	}
	va_list args;
	va_start(args, format);
	vfprintf(file, format, args);
	va_end(args);

	fflush(file);
}

static void indent()
{
	g_indentation++;
}

static void dedent()
{
	g_indentation--;
}

static float time_in_seconds_from_start_(void)
{
	static int start = 0;
	if (start == 0) {
		start = timeGetTime();
	}
	int now = timeGetTime();
	return (float)(now - start)/1000.0;
}

static struct ShimFunctions const* get_functions(void)
{
	if (g_functions.ggpo_client_connect) {
		return &g_functions;
	}

	HMODULE dll = LoadLibraryA("ggponet_original.dll");
	log("DLL? %s\n", dll ? "YES" : "no");
	assert(dll);

	#define LOOKUP_FUNC(func) \
	g_functions.func = (void*)GetProcAddress(dll, #func); \
	assert(g_functions.func)

	LOOKUP_FUNC(ggpo_client_connect);
	LOOKUP_FUNC(ggpo_client_chat);
	LOOKUP_FUNC(ggpo_client_set_game_event);
	LOOKUP_FUNC(ggpo_set_frame_delay);

	LOOKUP_FUNC(ggpo_start_session);
	LOOKUP_FUNC(ggpo_start_synctest);
	LOOKUP_FUNC(ggpo_start_streaming);
	LOOKUP_FUNC(ggpo_start_replay);
	LOOKUP_FUNC(ggpo_close_session);
	LOOKUP_FUNC(ggpo_idle);
	LOOKUP_FUNC(ggpo_synchronize_input);
	LOOKUP_FUNC(ggpo_advance_frame);
	LOOKUP_FUNC(ggpo_get_stats);
	LOOKUP_FUNC(ggpo_log);
	LOOKUP_FUNC(ggpo_logv);

	#undef LOOKUP_FUNC

	return &g_functions;
}

// ----------------
// Callbacks
// ----------------

GGPOSessionCallbacks g_original_callbacks = {};

static bool begin_game(char *game)
{
	log("--[begin_game(game: '%s')\n", game);
	return g_original_callbacks.begin_game(game);
}
struct WrappedGameState {
	int frame;
	int len;
	unsigned char buffer[];
};
static bool save_game_state(unsigned char **buffer, int *len, int *checksum, int frame)
{
	log("--[save_game_state(frame: %d)\n", frame);
	indent();
	bool result = g_original_callbacks.save_game_state(buffer, len, checksum, frame);

	struct WrappedGameState* state = malloc(sizeof(struct WrappedGameState) + *len);
	assert(state);
	memcpy(state->buffer, *buffer, *len);
	state->len = *len;
	state->frame = frame;
	g_original_callbacks.free_buffer(*buffer);
	*buffer = (unsigned char*)state;
	*len = sizeof(struct WrappedGameState) + state->len;
	dedent();
	log("  ]save_game_state(size: %d)\n", *len);

	return result;
}
static bool load_game_state(unsigned char *buffer, int len)
{
	struct WrappedGameState* state = (struct WrappedGameState*)buffer;
	log("--[load_game_state(frame: %d, len: %d)\n", state->frame, state->len);
	return g_original_callbacks.load_game_state(state->buffer, state->len);
}
static bool log_game_state(char *filename, unsigned char *buffer, int len)
{
	log("--[log_game_state(filename: '%s', len: %d)\n", filename, len);
	return g_original_callbacks.log_game_state(filename, buffer, len);
}
static void free_buffer(void *buffer)
{
	free(buffer);
}

static bool g_rolling_back = false;
static int g_frame_count = 0;

static bool advance_frame(int flags)
{
	g_rolling_back = true;
	log("--[advance_frame(flags: %x)\n", flags);
	indent();
	bool result = g_original_callbacks.advance_frame(flags);
	dedent();
	log("  ]advance_frame\n");
	g_rolling_back = false;
	return result;
}

static char const* event_kind_to_string_(int kind)
{
	switch (kind) {
	#define KIND_STRING(e) case e: return #e
	KIND_STRING(GGPO_EVENTCODE_CONNECTED_TO_PEER);
	KIND_STRING(GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER);
	KIND_STRING(GGPO_EVENTCODE_RUNNING);
	KIND_STRING(GGPO_EVENTCODE_DISCONNECTED_FROM_PEER);
	KIND_STRING(GGPO_EVENTCODE_TIMESYNC);

	KIND_STRING(GGPOCLIENT_GAMEEVENT_STARTING);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_PLAYER_1);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_PLAYER_2);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_PLAYER_3);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_PLAYER_4);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_PLAYER_1_SCORE);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_PLAYER_2_SCORE);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_PLAYER_3_SCORE);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_PLAYER_4_SCORE);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_WINNER);
	KIND_STRING(GGPOCLIENT_GAMEEVENT_FINISHED);

	KIND_STRING(GGPOCLIENT_EVENTCODE_CONNECTING);
	KIND_STRING(GGPOCLIENT_EVENTCODE_CONNECTED);
	KIND_STRING(GGPOCLIENT_EVENTCODE_RETREIVING_MATCHINFO);
	KIND_STRING(GGPOCLIENT_EVENTCODE_MATCHINFO);
	KIND_STRING(GGPOCLIENT_EVENTCODE_SPECTATOR_COUNT_CHANGED);
	KIND_STRING(GGPOCLIENT_EVENTCODE_CHAT);
	KIND_STRING(GGPOCLIENT_EVENTCODE_DISCONNECTED);
	#undef KIND_STRING

	default: return "--unknown--";
	}
}

static bool on_event(GGPOEvent *info)
{
	log("on_event(%s)\n", event_kind_to_string_(info->code));
	if ((GGPOClientEventCode)(info->code) == GGPOCLIENT_EVENTCODE_SPECTATOR_COUNT_CHANGED) {
		GGPOClientEvent const* event = (GGPOClientEvent const*)info;
		log("  spectator_count = %d\n", event->u.spectator_count_changed.count);
	}
	return g_original_callbacks.on_event(info);
}

static GGPOSessionCallbacks shim_callbacks(GGPOSessionCallbacks* cb)
{
	g_original_callbacks = *cb;
	return (GGPOSessionCallbacks){
		.begin_game = begin_game,
		.save_game_state = save_game_state,
		.load_game_state = load_game_state,
		.log_game_state = log_game_state,
		.free_buffer = free_buffer,
		.advance_frame = advance_frame,
		.on_event = on_event,
	};
}
// ----------------
// ggpoclient.h
// ----------------

GGPO_EXPORT GGPOSession* ggpo_client_connect(GGPOSessionCallbacks *cb, char *game, char *matchid, int serverport)
{
	g_rolling_back = false;
	log("%.03f: client_connect(game: %s, matchid: %s, serverport: %d)\n", time_in_seconds_from_start_(), game, matchid, serverport);
	indent();
	GGPOSessionCallbacks shimmed_cb = shim_callbacks(cb);
	GGPOSession* session = get_functions()->ggpo_client_connect(&shimmed_cb, game, matchid, serverport);
	dedent();
	log("%.03f:               )client_connect\n", time_in_seconds_from_start_());
	return session;
}

GGPO_EXPORT bool ggpo_client_chat(GGPOSession* session, char *text)
{
	log("%.03f: chat(%s)\n", time_in_seconds_from_start_(), text);
	return get_functions()->ggpo_client_chat(session, text);
}

GGPO_EXPORT bool ggpo_client_set_game_event(GGPOSession* session, GGPOClientGameEventType type, void *data)
{
	log("client_set_game_event(type: %d)\n", type);
	return get_functions()->ggpo_client_set_game_event(session, type, data);
}

GGPO_EXPORT int ggpo_set_frame_delay(GGPOSession* session, int frame_delay)
{
	int result = get_functions()->ggpo_set_frame_delay(session, frame_delay);
	log("%d set_frame_delay(delay: %d)\n", result, frame_delay);
	return result;
}

// ----------------
// ggponet.h
// ----------------

GGPO_EXPORT GGPOSession* ggpo_start_session(GGPOSessionCallbacks *cb, char *game, int localport, char *remoteip, int remoteport, int player_num)
{
	log("%.03f: start_session(game: '%s', localport: %d, remoteip: %s, remoteport: %d, player_num: %d)\n", time_in_seconds_from_start_(), game, localport, remoteip, remoteport, player_num);
	GGPOSessionCallbacks shimmed_cb = shim_callbacks(cb);
	return get_functions()->ggpo_start_session(&shimmed_cb, game, localport, remoteip, remoteport, player_num);
}

GGPO_EXPORT GGPOSession* ggpo_start_synctest(GGPOSessionCallbacks *cb, char *game, int frames)
{
	log("%.03f: start_synctest(game: '%s', frames: %d)\n", time_in_seconds_from_start_(), game, frames);
	GGPOSessionCallbacks shimmed_cb = shim_callbacks(cb);
	return get_functions()->ggpo_start_synctest(&shimmed_cb, game, frames);
}

GGPO_EXPORT GGPOSession* ggpo_start_streaming(GGPOSessionCallbacks *cb, char *game, char *matchid, int port)
{
	log("%.03f: start_streaming(game: '%s', matchid: '%s', port: %d)\n", time_in_seconds_from_start_(), game, matchid, port);
	GGPOSessionCallbacks shimmed_cb = shim_callbacks(cb);
	return get_functions()->ggpo_start_streaming(&shimmed_cb, game, matchid, port);
}

GGPO_EXPORT GGPOSession* ggpo_start_replay(GGPOSessionCallbacks *cb, char *file)
{
	log("start_replay(file: '%s')\n", file);
	{
		// Copy the replay aside so we can examine it later.
		FILE* f = fopen(file, "rb");
		assert(f);
		fseek(f, 0, SEEK_END);
		size_t size = ftell(f);
		rewind(f);
		void* file_data = malloc(size);
		assert(file_data);
		size_t bytes_read = fread(file_data, 1, size, f);
		assert(bytes_read == size);
		fclose(f);
		f = fopen("c:\\users\\ponder\\replay.dat", "wb");
		assert(f);
		size_t bytes_written = fwrite(file_data, 1, size, f);
		assert(bytes_written == size);
		fclose(f);
	}
	GGPOSessionCallbacks shimmed_cb = shim_callbacks(cb);
	return get_functions()->ggpo_start_replay(&shimmed_cb, file);
}

GGPO_EXPORT void ggpo_close_session(GGPOSession* session)
{
	log("close_session()\n");
	get_functions()->ggpo_close_session(session);
}

GGPO_EXPORT bool ggpo_idle(GGPOSession* session, int timeout)
{
	log("%.03f: idle(%d)\n", time_in_seconds_from_start_(), timeout);
	indent();
	bool result = get_functions()->ggpo_idle(session, timeout);
	dedent();
	log("%.03f:    )idle\n", time_in_seconds_from_start_());
	return result;
}

GGPO_EXPORT bool ggpo_synchronize_input(GGPOSession* session, void* values, int size, int players)
{
	log("%.03f: synchronize_input[%d](size: %d, players: %d, inputs: ", time_in_seconds_from_start_(), g_frame_count, size, players);
	int saved_indentation = g_indentation;
	g_indentation = 0;
	for(int i = 0; i < size; i++) {
		if (i == size) {
			log("| ");
		}
		log("%02x ", ((uint8_t*)values)[i]);
	}
	log(")\n");
	g_indentation = saved_indentation;

	bool result = get_functions()->ggpo_synchronize_input(session, values, size, players);
	log("%.03f:    ) (%s)synchronize_input < ", time_in_seconds_from_start_(), result ? "true" : "false");
	g_indentation = 0;
	for(int i = 0; i < size * players; i++) {
		if (i == size) {
			log("| ");
		}
		log("%02x ", ((uint8_t*)values)[i]);
	}
	log(">\n");
	g_indentation = saved_indentation;
	return result;
}

GGPO_EXPORT bool ggpo_advance_frame(GGPOSession* session)
{
	log("%.03f: advance_frame(\n", time_in_seconds_from_start_());
	indent();
	bool result = get_functions()->ggpo_advance_frame(session);
	dedent();
	log("%.03f:  )advance_frame\n", time_in_seconds_from_start_());
	if (!g_rolling_back) {
		g_frame_count++;
	}
	return result;
}

GGPO_EXPORT bool ggpo_get_stats(GGPOSession* session, GGPONetworkStats *stats)
{
	bool result = get_functions()->ggpo_get_stats(session, stats);
	log("get_stats(predict: %d, send: %d, recv: %d, ping: %d, kbps: %d, local_frames_behind: %d, remote_frames_behind: %d)\n",
		stats->network.predict_queue_len,
		stats->network.send_queue_len,
		stats->network.recv_queue_len,
		stats->network.ping,
		stats->network.kbps_sent,
		stats->timesync.local_frames_behind,
		stats->timesync.remote_frames_behind);
	return result;
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
	get_functions()->ggpo_logv(session, fmt, args);
}

