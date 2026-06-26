#include "ggpo_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#ifdef _WIN32
#include <winsock2.h>
#endif

// zig cc -target x86-windows-gnu -std=c11 -lWs2_32 -O2 ggpo_network.c ggpo_network_test.c -o ggpo_network_test
// clang -std=c99 -Wall -Wextra -g ggpo_network.c ggpo_network_test.c -o ggpo_network_test

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define TEST(...) do { if (!(__VA_ARGS__)) { fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, #__VA_ARGS__); exit(1); } } while(0)

int main(void)
{
#ifdef _WIN32
	{
		WSADATA data = {};
		int result = WSAStartup(MAKEWORD(2, 2), &data);
		assert(result == 0);
	}
#endif

	// Test that we can send and recv data over UDP.
	{
		bool success;
		GGPO_Socket sock_recv = GGPO_Socket_udp_create();
		TEST(sock_recv != GGPO_SOCKET_INVALID);
		success = GGPO_Socket_bind(sock_recv, 8888);
		TEST(success);
		GGPO_Socket sock_send = GGPO_Socket_udp_create();
		TEST(sock_send != GGPO_SOCKET_INVALID);

		union GGPO_PlatformAddress platform_address = GGPO_PlatformAddress_init("localhost", 8888);
		TEST(platform_address.addr.sa_family != 0);

		int msg_send = -555;
		success = GGPO_Socket_udp_sendto(sock_send, &platform_address, sizeof(msg_send), &msg_send);
		TEST(success);
		int msg_recv;
		union GGPO_PlatformAddress recv_platform_address = {};
		GGPO_ReadResult read_result;
		read_result = GGPO_Socket_udp_recvfrom(sock_recv, &recv_platform_address, sizeof(msg_recv), &msg_recv);
		TEST(read_result == kGGPO_ReadResult_success);
		TEST(msg_recv == msg_send);

		// Make recv socket non-blocking and test that we return immediately when reading without a message to receive.
		success = GGPO_Socket_make_nonblocking(sock_recv);
		TEST(success);
		msg_recv = -1;
		read_result = GGPO_Socket_udp_recvfrom(sock_recv, &recv_platform_address, sizeof(msg_recv), &msg_recv);
		TEST(read_result == kGGPO_ReadResult_wouldblock);
		TEST(msg_recv = -1);

		GGPO_Socket_destroy(sock_recv);
		GGPO_Socket_destroy(sock_send);
	}


	// Test that we can send/recv on TCP.
	{
		// Because our GGPO library is purely a client library there is no code for receiving connections.
		// Instead we have to do this ourselves in the test.
		GGPO_Socket listener = socket(AF_INET, SOCK_STREAM, 0);
		assert(listener != GGPO_SOCKET_INVALID);
		struct sockaddr_in listen_addr = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = INADDR_ANY,
		};
		int err;
		err = bind(listener, (struct sockaddr*)&listen_addr, sizeof(listen_addr));
		assert(err == 0);
		union GGPO_PlatformAddress listen_address;
		socklen_t listen_address_len = sizeof(listen_address);
		err = getsockname(listener, &listen_address.addr, &listen_address_len);
		assert(err == 0);
		uint16_t listen_port = ntohs(listen_address.v4.sin_port);
		err = listen(listener, 10);
		assert(err == 0);

		GGPO_Socket client_sock = GGPO_Socket_tcp_connect("localhost", listen_port);
		TEST(client_sock != GGPO_SOCKET_INVALID);

		union GGPO_PlatformAddress platform_address;
		socklen_t platform_address_len = sizeof(platform_address);
		GGPO_Socket server_sock = accept(listener, &platform_address.addr, &platform_address_len);
		TEST(server_sock != GGPO_SOCKET_INVALID);

		// Test sending to the server.
		int client_data = -12345;
		bool success;
		GGPO_ReadResult read_result;
		success = GGPO_Socket_write_all(client_sock, sizeof(client_data), &client_data);
		TEST(success);
		int server_data;
		read_result = GGPO_Socket_read_all(server_sock, sizeof(server_data), &server_data);
		TEST(read_result == kGGPO_ReadResult_success);
		TEST(client_data == server_data);

		// Test reading from the server.
		server_data = 987654;
		success = GGPO_Socket_write_all(server_sock, sizeof(server_data), &server_data);
		TEST(success);
		read_result = GGPO_Socket_read_all(client_sock, sizeof(client_data), &client_data);
		TEST(read_result == kGGPO_ReadResult_success);
		TEST(client_data == server_data);

		// Test that we can detect when there is data available.
		// We'll send some data and read part of it, just so we know in the test that there
		// is data available, then check.
		server_data = 0xaaaabbbb;
		success = GGPO_Socket_write_all(server_sock, sizeof(server_data), &server_data);
		TEST(success);
		uint16_t half_client_data;
		read_result = GGPO_Socket_read_all(client_sock, sizeof(half_client_data), &half_client_data);
		TEST(read_result == kGGPO_ReadResult_success);
		// Check that we can see the data still left.
		read_result = GGPO_Socket_has_data(client_sock);
		TEST(read_result == kGGPO_ReadResult_success);
		// Read the rest to clear it out.
		read_result = GGPO_Socket_read_all(client_sock, sizeof(half_client_data), &half_client_data);
		TEST(read_result == kGGPO_ReadResult_success);
		// Check that we see there is no data.
		read_result = GGPO_Socket_has_data(client_sock);
		TEST(read_result == kGGPO_ReadResult_wouldblock);

		// Test GGPO_Socket_read with non-blocking sockets returning with partial reads.
		// Set the client socket to non-blocking and test that we return immediately
		// when there is not enough.
		success = GGPO_Socket_make_nonblocking(client_sock);
		TEST(success);
		size_t read_size = 1;
		read_result = GGPO_Socket_read(client_sock, &read_size, &client_data);
		TEST(read_result == kGGPO_ReadResult_wouldblock);
		TEST(read_size == 0);
		success = GGPO_Socket_write_all(server_sock, 1, &server_data);
		TEST(success);
		success = GGPO_Socket_wait_for_data(client_sock);
		TEST(success);
		read_size = sizeof(client_data);
		read_result = GGPO_Socket_read(client_sock, &read_size, &client_data);
		TEST(read_result == kGGPO_ReadResult_success);
		TEST(1 == read_size);
		// Read the rest.
		success = GGPO_Socket_write_all(server_sock, sizeof(server_data) - 1, (char*)&server_data + 1);
		TEST(success);
		success = GGPO_Socket_wait_for_data(client_sock);
		TEST(success);
		read_size = sizeof(read_size) - 1;
		read_result = GGPO_Socket_read(client_sock, &read_size, (char*)&client_data + 1);
		TEST(read_result == kGGPO_ReadResult_success);
		TEST(3 == read_size);
		TEST(client_data == server_data);

		// Test GGPO_Socket_write with non-blocking sockets returning with partial writes.
		// GGPO_ReadResult write_result;
		// First make the socket non-blocking.
		success = GGPO_Socket_make_nonblocking(server_sock);
		TEST(success);
		// Allocate a buffer that would not fit in network buffers then try to write it.
		// We should only see some of it written.
		size_t const massive_size = 1024 * 1024;
		void* massive_buffer = malloc(massive_size);
		size_t write_size = massive_size;
		read_result = kGGPO_ReadResult_success;
		// Write until we can't anymore, then validate that it would block.
		while (read_result == kGGPO_ReadResult_success) {
			write_size = massive_size;
			read_result = GGPO_Socket_write(server_sock, &write_size, massive_buffer);
			printf("read result: %d\n", read_result);
			if (read_result == kGGPO_ReadResult_success) {
				TEST(write_size > 0);
				TEST(write_size < massive_size);
			}
		}
		TEST(read_result == kGGPO_ReadResult_wouldblock);
		// Writing again should block.
		read_result = GGPO_Socket_has_space(server_sock);
		TEST(read_result == kGGPO_ReadResult_wouldblock);

		// TODO; This part of the test needs improvement.
		// Since we haven't read anything out I would expect to not be able to write more.
		write_size = massive_size;
		read_result = GGPO_Socket_write(server_sock, &write_size, massive_buffer);
		printf("read result: %d\n", read_result);
		TEST(read_result == kGGPO_ReadResult_wouldblock);
		TEST(write_size == 0);
		read_size = massive_size;
		GGPO_Socket_wait_for_data(client_sock);
		// Clear out the data we wrote.
		GGPO_Socket_read(client_sock, &read_size, massive_buffer);
		free(massive_buffer);

		// TODO: Test detecting disconnect.
		// Test we don't get SIGPIPE when writing to a closed socket.
		GGPO_Socket_destroy(listener);
		GGPO_Socket_destroy(server_sock);
		GGPO_Socket_wait_for_data(client_sock);
		GGPO_Socket_wait_for_space(client_sock);

		write_size = sizeof(client_data);
		read_result = GGPO_Socket_write(client_sock, &write_size, &client_data);
		TEST(read_result == kGGPO_ReadResult_success);
		printf("write size = %zu\n", write_size);
		TEST(write_size == 0);

		GGPO_Socket_destroy(client_sock);
	}
#ifdef _WIN32
	WSACleanup();
#endif

	printf("Test passed.\n");
	return 0;
}