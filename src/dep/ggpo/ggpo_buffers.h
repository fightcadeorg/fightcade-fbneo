#ifndef GGPO_BUFFERS_H__
#define GGPO_BUFFERS_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct GGPO_Buffer {
	size_t size;
	struct GGPO_Buffer* _next;
	char buffer[];
};

struct GGPO_Buffer* GGPO_Buffer_create(size_t size);
void GGPO_Buffer_destroy(struct GGPO_Buffer* buffer);

struct GGPO_BufferQueue {
	struct GGPO_Buffer* _front;
	struct GGPO_Buffer* _back;
};

void GGPO_BufferQueue_init(struct GGPO_BufferQueue* queue);
void GGPO_BufferQueue_destroy(struct GGPO_BufferQueue* queue);
bool GGPO_BufferQueue_is_empty(struct GGPO_BufferQueue const* queue);
void GGPO_BufferQueue_push_back(struct GGPO_BufferQueue* queue, struct GGPO_Buffer* buffer);
struct GGPO_Buffer* GGPO_BufferQueue_pop_front(struct GGPO_BufferQueue* queue);

struct GGPO_BufferReader {
	char const* cursor;
	char const* _end;
	bool _overflowed;
};

void GGPO_BufferReader_init(struct GGPO_BufferReader* reader, struct GGPO_Buffer const* buffer);
size_t GGPO_BufferReader_bytes_left(struct GGPO_BufferReader const* reader);
void const* GGPO_BufferReader_cursor(struct GGPO_BufferReader const* reader);
void GGPO_BufferReader_advance_cursor(struct GGPO_BufferReader* reader, uint32_t size);
bool GGPO_BufferReader_is_empty(struct GGPO_BufferReader const* reader);
bool GGPO_BufferReader_is_overflowed(struct GGPO_BufferReader const* reader);
void GGPO_BufferReader_get(struct GGPO_BufferReader* reader, size_t size, void* dst);
uint32_t GGPO_BufferReader_u32(struct GGPO_BufferReader* reader);
int32_t GGPO_BufferReader_i32(struct GGPO_BufferReader* reader);
char* GGPO_BufferReader_string(struct GGPO_BufferReader* reader);

struct GGPO_BufferWriter {
	char* _cursor;
	char* _end;
	bool _overflowed;
};

void GGPO_BufferWriter_init(struct GGPO_BufferWriter* writer, struct GGPO_Buffer* buffer);
size_t GGPO_BufferWriter_bytes_left(struct GGPO_BufferWriter const* writer);
void* GGPO_BufferWriter_cursor(struct GGPO_BufferWriter const* writer);
void GGPO_BufferWriter_advance_cursor(struct GGPO_BufferWriter* writer, uint32_t size);
bool GGPO_BufferWriter_is_full(struct GGPO_BufferWriter const* writer);
bool GGPO_BufferWriter_is_overflowed(struct GGPO_BufferWriter const* writer);
void GGPO_BufferWriter_set(struct GGPO_BufferWriter* writer, size_t size, void const* src);
void GGPO_BufferWriter_u32(struct GGPO_BufferWriter* writer, uint32_t value);
void GGPO_BufferWriter_string(struct GGPO_BufferWriter* writer, char const* string);

#endif // GGPO_BUFFERS_H__