#include "ggpo_buffers.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

struct GGPO_Buffer* GGPO_Buffer_create(size_t size)
{
	assert(size > 0);
	struct GGPO_Buffer* buffer = malloc(sizeof(*buffer) + size);
	assert(buffer);
	buffer->size = size;
	buffer->_next = NULL;
	return buffer;
}

void GGPO_Buffer_destroy(struct GGPO_Buffer* buffer)
{
	assert(buffer);
	free(buffer);
}

void GGPO_BufferReader_init(struct GGPO_BufferReader* reader, struct GGPO_Buffer const* buffer)
{
	assert(reader);
	assert(buffer);
	*reader = (struct GGPO_BufferReader){
		.cursor = buffer->buffer,
		._end = buffer->buffer + buffer->size,
	};
}

size_t GGPO_BufferReader_bytes_left(struct GGPO_BufferReader const* reader)
{
	return reader->_end - reader->cursor;
}

void const* GGPO_BufferReader_cursor(struct GGPO_BufferReader const* reader)
{
	return reader->cursor;
}

void GGPO_BufferReader_advance_cursor(struct GGPO_BufferReader* reader, uint32_t size)
{
	assert(size > 0);
	if (GGPO_BufferReader_bytes_left(reader) < size) {
		reader->cursor = reader->_end;
		reader->_overflowed = true;
		return;
	}
	reader->cursor += size;
}

bool GGPO_BufferReader_is_empty(struct GGPO_BufferReader const* reader)
{
	return 0 == GGPO_BufferReader_bytes_left(reader);
}

bool GGPO_BufferReader_is_overflowed(struct GGPO_BufferReader const* reader)
{
	return reader->_overflowed;
}

void GGPO_BufferReader_get(struct GGPO_BufferReader* reader, size_t size, void* dst)
{
	if (size == 0) {
		return;
	}
	if (GGPO_BufferReader_bytes_left(reader) < size) {
		reader->cursor = reader->_end;
		reader->_overflowed = true;
		return;
	}
	memcpy(dst, reader->cursor, size);
	reader->cursor += size;
}

uint32_t GGPO_BufferReader_u32(struct GGPO_BufferReader* reader)
{
	uint32_t value;
	GGPO_BufferReader_get(reader, sizeof(value), &value);
	return ntohl(value);
}

int32_t GGPO_BufferReader_i32(struct GGPO_BufferReader* reader)
{
	uint32_t unsigned_value;
	GGPO_BufferReader_get(reader, sizeof(unsigned_value), &unsigned_value);
	unsigned_value = ntohl(unsigned_value);
	int32_t value;
	memcpy(&value, &unsigned_value, sizeof(unsigned_value));
	return value;
}

char* GGPO_BufferReader_string(struct GGPO_BufferReader* reader)
{
	uint32_t length = GGPO_BufferReader_u32(reader);
	if (reader->_overflowed) {
		return NULL;
	}
	char* string = malloc(length + 1);
	assert(string);
	GGPO_BufferReader_get(reader, length, string);
	if (reader->_overflowed) {
		free(string);
		return NULL;
	}
	string[length] = '\0';
	return string;
}

void GGPO_BufferWriter_init(struct GGPO_BufferWriter* writer, struct GGPO_Buffer* buffer)
{
	assert(writer);
	assert(buffer);
	*writer = (struct GGPO_BufferWriter){
		._cursor = buffer->buffer,
		._end = buffer->buffer + buffer->size,
	};
}

size_t GGPO_BufferWriter_bytes_left(struct GGPO_BufferWriter const* writer)
{
	return writer->_end - writer->_cursor;
}

void* GGPO_BufferWriter_cursor(struct GGPO_BufferWriter const* writer)
{
	return writer->_cursor;
}

void GGPO_BufferWriter_advance_cursor(struct GGPO_BufferWriter* writer, uint32_t size)
{
	assert(size > 0);
	if (GGPO_BufferWriter_bytes_left(writer) < size) {
		writer->_cursor = writer->_end;
		writer->_overflowed = true;
		return;
	}
	writer->_cursor += size;
}

bool GGPO_BufferWriter_is_full(struct GGPO_BufferWriter const* writer)
{
	return 0 == GGPO_BufferWriter_bytes_left(writer);
}

bool GGPO_BufferWriter_is_overflowed(struct GGPO_BufferWriter const* writer)
{
	return writer->_overflowed;
}

void GGPO_BufferWriter_set(struct GGPO_BufferWriter* writer, size_t size, void const* src)
{
	if (size == 0) {
		return;
	}
	if (GGPO_BufferWriter_bytes_left(writer) < size) {
		writer->_cursor = writer->_end;
		writer->_overflowed = true;
		return;
	}
	memcpy(writer->_cursor, src, size);
	writer->_cursor += size;
}

void GGPO_BufferWriter_u32(struct GGPO_BufferWriter* writer, uint32_t value)
{
	value = htonl(value);
	GGPO_BufferWriter_set(writer, sizeof(value), &value);
}

void GGPO_BufferWriter_string(struct GGPO_BufferWriter* writer, char const* string)
{
	assert(string);
	size_t length = strlen(string);
	assert(length < UINT32_MAX);
	GGPO_BufferWriter_u32(writer, (uint32_t)length);
	GGPO_BufferWriter_set(writer, length, string);
}

void GGPO_BufferQueue_init(struct GGPO_BufferQueue* queue)
{
	*queue = (struct GGPO_BufferQueue){};
}

void GGPO_BufferQueue_destroy(struct GGPO_BufferQueue* queue)
{
	assert(queue);
	struct GGPO_Buffer* buffer = queue->_front;
	while (buffer != NULL) {
		struct GGPO_Buffer* next = buffer->_next;
		GGPO_Buffer_destroy(buffer);
		buffer = next;
	}
	*queue = (struct GGPO_BufferQueue){};
}

bool GGPO_BufferQueue_is_empty(struct GGPO_BufferQueue const* queue)
{
	assert(queue);
	return queue->_front == NULL;
}

void GGPO_BufferQueue_push_back(struct GGPO_BufferQueue* queue, struct GGPO_Buffer* buffer)
{
	assert(queue);
	assert(buffer);
	if (queue->_front == NULL) {
		assert(queue->_back == NULL);
		queue->_front = buffer;
		queue->_back = buffer;
	} else {
		assert(queue->_back->_next == NULL);
		assert(buffer->_next == NULL);
		queue->_back->_next = buffer;
		queue->_back = buffer;
	}
}

struct GGPO_Buffer* GGPO_BufferQueue_pop_front(struct GGPO_BufferQueue* queue)
{
	assert(queue);
	struct GGPO_Buffer* buffer = queue->_front;
	if (buffer != NULL) {
		queue->_front = buffer->_next;
		if (queue->_front == NULL) {
			queue->_back = NULL;
		}
		buffer->_next = NULL;
	}
	return buffer;
}