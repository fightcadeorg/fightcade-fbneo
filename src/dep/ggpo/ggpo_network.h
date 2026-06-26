#ifndef GGPO_NETWORK_H__
#define GGPO_NETWORK_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif


union GGPO_PlatformAddress {
	struct sockaddr addr;
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
};

union GGPO_PlatformAddress GGPO_PlatformAddress_init(char const* host, uint16_t port);
bool GGPO_PlatformAddress_is_valid(union GGPO_PlatformAddress const* address);

#ifdef _WIN32
#define GGPO_Socket SOCKET
#define GGPO_SOCKET_INVALID INVALID_SOCKET
#else
#define GGPO_Socket int
#define GGPO_SOCKET_INVALID -1
#endif

typedef enum {
	kGGPO_ReadResult_success,
	kGGPO_ReadResult_error,
	kGGPO_ReadResult_wouldblock,
} GGPO_ReadResult;

GGPO_Socket GGPO_Socket_udp_create();
bool GGPO_Socket_bind(GGPO_Socket sock, uint16_t port);
GGPO_Socket GGPO_Socket_tcp_connect(char const* host, uint16_t port);
bool GGPO_Socket_make_nonblocking(GGPO_Socket sock);
GGPO_ReadResult GGPO_Socket_has_data(GGPO_Socket sock);
GGPO_ReadResult GGPO_Socket_has_space(GGPO_Socket sock);
bool GGPO_Socket_wait_for_data(GGPO_Socket sock);
bool GGPO_Socket_wait_for_space(GGPO_Socket sock);
GGPO_ReadResult GGPO_Socket_read(GGPO_Socket sock, size_t* size, void* bytes);
GGPO_ReadResult GGPO_Socket_read_all(GGPO_Socket sock, size_t size, void* bytes);
GGPO_ReadResult GGPO_Socket_write(GGPO_Socket sock, size_t* size, void const* bytes);
bool GGPO_Socket_write_all(GGPO_Socket sock, size_t size, void const* bytes);
bool GGPO_Socket_udp_sendto(GGPO_Socket sock, union GGPO_PlatformAddress const* addr, size_t size, void const* bytes);
GGPO_ReadResult GGPO_Socket_udp_recvfrom(GGPO_Socket sock, union GGPO_PlatformAddress* addr, size_t size, void* bytes);
void GGPO_Socket_destroy(GGPO_Socket sock);

#endif // GGPO_NETWORK_H__