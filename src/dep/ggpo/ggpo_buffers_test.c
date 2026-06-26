#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <winsock2.h>
#endif

#include "ggpo_buffers.h"

// zig cc -target x86-windows-gnu -std=c11 -O2 ggpo_buffers.c ggpo_buffers_test.c -o ggpo_buffers_test.exe
// clang -std=c99 -Wall -Wextra -g ggpo_buffers.c ggpo_buffers_test.c -o ggpo_buffers_test

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define TEST(...) do { if (!(__VA_ARGS__)) { fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, #__VA_ARGS__); abort(); } } while(0)

static void fill_buffer(struct GGPO_Buffer* buffer, int v)
{
	memset(buffer->buffer, v, buffer->size);
}

static bool buffers_equal(struct GGPO_Buffer const* lhs, struct GGPO_Buffer const* rhs)
{
	if (lhs->size != rhs->size) {
		return false;
	}
	return 0 == memcmp(lhs->buffer, rhs->buffer, lhs->size);
}

int main(void)
{
	// Test buffer creation.
	{
		struct GGPO_Buffer* buffer = GGPO_Buffer_create(100);
		TEST(buffer != NULL);
		TEST(buffer->buffer != NULL);
		TEST(buffer->size == 100);
		GGPO_Buffer_destroy(buffer);
	}

	// Test BufferReader and BufferWriter.
	{
		uint32_t a = 0xaabbccdd;
		uint32_t b = 0x12345678;
		char const* string = "abcd";

		uint32_t serialized_msg_size = sizeof(a) + sizeof(b) + sizeof(uint32_t) + strlen(string);
		struct GGPO_Buffer* buffer = GGPO_Buffer_create(serialized_msg_size);
		assert(buffer);
		
		struct GGPO_BufferWriter writer;
		GGPO_BufferWriter_init(&writer, buffer);
		TEST(serialized_msg_size == GGPO_BufferWriter_bytes_left(&writer));
		GGPO_BufferWriter_u32(&writer, a);
		GGPO_BufferWriter_u32(&writer, b);
		TEST(!GGPO_BufferWriter_is_full(&writer));
		TEST(8 == GGPO_BufferWriter_bytes_left(&writer));
		GGPO_BufferWriter_string(&writer, string);
		TEST(GGPO_BufferWriter_is_full(&writer));
		TEST(!GGPO_BufferWriter_is_overflowed(&writer));
		GGPO_BufferWriter_u32(&writer, a);
		TEST(GGPO_BufferWriter_is_overflowed(&writer));
		// TEst that the integer is written in network order.
		uint32_t in_a;
		memcpy(&in_a, buffer->buffer, sizeof(in_a));
		TEST(htonl(a) == in_a);

		uint32_t in_b;
		struct GGPO_BufferReader reader;
		GGPO_BufferReader_init(&reader, buffer);
		in_a = GGPO_BufferReader_u32(&reader);
		in_b = GGPO_BufferReader_u32(&reader);
		TEST(!GGPO_BufferReader_is_empty(&reader));
		char* text = GGPO_BufferReader_string(&reader);
		assert(text);
		TEST(a == in_a);
		TEST(b == in_b);
		TEST(0 == strcmp(text, string));
		free(text);
		TEST(!GGPO_BufferReader_is_overflowed(&reader));
		TEST(0 == GGPO_BufferReader_bytes_left(&reader));
		TEST(GGPO_BufferReader_is_empty(&reader));
		GGPO_BufferReader_u32(&reader);
		TEST(GGPO_BufferReader_is_overflowed(&reader));
		GGPO_Buffer_destroy(buffer);
	}

	// Test manual BufferWriter cursor manipulation.
	{
		size_t const buffer_size = 4;
		struct GGPO_Buffer* buffer = GGPO_Buffer_create(4);
		struct GGPO_BufferWriter writer;
		GGPO_BufferWriter_init(&writer, buffer);
		TEST(buffer->buffer == GGPO_BufferWriter_cursor(&writer));
		GGPO_BufferWriter_advance_cursor(&writer, 2);
		TEST(2 == GGPO_BufferWriter_bytes_left(&writer));
		TEST(!GGPO_BufferWriter_is_full(&writer));
		TEST(!GGPO_BufferWriter_is_overflowed(&writer));
		GGPO_BufferWriter_advance_cursor(&writer, 2);
		TEST(0 == GGPO_BufferWriter_bytes_left(&writer));
		TEST(GGPO_BufferWriter_is_full(&writer));
		TEST(!GGPO_BufferWriter_is_overflowed(&writer));
		TEST(GGPO_BufferWriter_cursor(&writer) == buffer->buffer + buffer_size);
		GGPO_BufferWriter_advance_cursor(&writer, 1);
		TEST(GGPO_BufferWriter_is_full(&writer));
		TEST(GGPO_BufferWriter_is_overflowed(&writer));
		GGPO_Buffer_destroy(buffer);
	}

	// Test manual BufferReader cursor manipulation.
	{
		size_t const buffer_size = 4;
		struct GGPO_Buffer* buffer = GGPO_Buffer_create(4);
		struct GGPO_BufferReader reader;
		GGPO_BufferReader_init(&reader, buffer);
		TEST(buffer->buffer == GGPO_BufferReader_cursor(&reader));
		GGPO_BufferReader_advance_cursor(&reader, 2);
		TEST(2 == GGPO_BufferReader_bytes_left(&reader));
		TEST(!GGPO_BufferReader_is_empty(&reader));
		TEST(!GGPO_BufferReader_is_overflowed(&reader));
		GGPO_BufferReader_advance_cursor(&reader, 2);
		TEST(0 == GGPO_BufferReader_bytes_left(&reader));
		TEST(GGPO_BufferReader_is_empty(&reader));
		TEST(!GGPO_BufferReader_is_overflowed(&reader));
		TEST(GGPO_BufferReader_cursor(&reader) == buffer->buffer + buffer_size);
		GGPO_BufferReader_advance_cursor(&reader, 1);
		TEST(GGPO_BufferReader_is_empty(&reader));
		TEST(GGPO_BufferReader_is_overflowed(&reader));
		GGPO_Buffer_destroy(buffer);
	}

	// Test the queue.
	{
		struct GGPO_Buffer* buffer_0 = GGPO_Buffer_create(100);
		struct GGPO_Buffer* buffer_1 = GGPO_Buffer_create(100);
		fill_buffer(buffer_0, 5);
		fill_buffer(buffer_1, 6);
		// Same size but different content.
		TEST(!buffers_equal(buffer_0, buffer_1));
		// Same size, same value.
		fill_buffer(buffer_1, 5);
		TEST(buffers_equal(buffer_0, buffer_1));
		struct GGPO_Buffer* buffer_2 = GGPO_Buffer_create(101);
		// Same value, different size.
		fill_buffer(buffer_2, 5);
		TEST(!buffers_equal(buffer_1, buffer_2));

		struct GGPO_BufferQueue queue = {};
		GGPO_BufferQueue_init(&queue);
		TEST(GGPO_BufferQueue_is_empty(&queue));
		// The queue is empty.
		TEST(NULL == GGPO_BufferQueue_pop_front(&queue));
		GGPO_BufferQueue_push_back(&queue, buffer_0);
		TEST(buffer_0 == GGPO_BufferQueue_pop_front(&queue));
		TEST(NULL == GGPO_BufferQueue_pop_front(&queue));
		TEST(GGPO_BufferQueue_is_empty(&queue));
		GGPO_BufferQueue_push_back(&queue, buffer_1);
		GGPO_BufferQueue_push_back(&queue, buffer_2);
		GGPO_BufferQueue_push_back(&queue, buffer_0);
		TEST(buffer_1 == GGPO_BufferQueue_pop_front(&queue));
		TEST(buffer_2 == GGPO_BufferQueue_pop_front(&queue));
		TEST(!GGPO_BufferQueue_is_empty(&queue));
		TEST(buffer_0 == GGPO_BufferQueue_pop_front(&queue));
		TEST(GGPO_BufferQueue_is_empty(&queue));

		GGPO_Buffer_destroy(buffer_0);
		GGPO_Buffer_destroy(buffer_1);
		GGPO_Buffer_destroy(buffer_2);
		GGPO_BufferQueue_destroy(&queue);
	}

	printf("Tests passed.\n");
	return 0;
}