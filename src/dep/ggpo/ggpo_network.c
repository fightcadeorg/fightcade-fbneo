#include "ggpo_network.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif


static void GGPO_PlatformAddress_set_port_(union GGPO_PlatformAddress* address, uint16_t port)
{
	assert(address->addr.sa_family == AF_INET || address->addr.sa_family == AF_INET6);
	if (address->addr.sa_family == AF_INET) {
		address->v4.sin_port = htons(port);
	} else if (address->addr.sa_family == AF_INET6) {
		address->v6.sin6_port = htons(port);
	}
}

union GGPO_PlatformAddress GGPO_PlatformAddress_init(char const* host, uint16_t port)
{
	union GGPO_PlatformAddress platform_addr = {};
	struct addrinfo* info;
	struct addrinfo hint = {
		.ai_family = AF_INET,
	};
	int err = getaddrinfo(host, NULL, &hint, &info);
	if (err != 0) {
		return platform_addr;
	}
	for (struct addrinfo* a = info; a != NULL; a = a->ai_next) {
		assert(sizeof(platform_addr) >= a->ai_addrlen);
		memcpy(&platform_addr.addr, a->ai_addr, a->ai_addrlen);
		break;
	}
	freeaddrinfo(info);
	GGPO_PlatformAddress_set_port_(&platform_addr, port);
	return platform_addr;
}

bool GGPO_PlatformAddress_is_valid(union GGPO_PlatformAddress const* address)
{
	return address->addr.sa_family != 0;
}

static socklen_t GGPO_PlatformAddress_get_length_(union GGPO_PlatformAddress const* address)
{
	assert(address->addr.sa_family == AF_INET || address->addr.sa_family == AF_INET6);
	if (address->addr.sa_family == AF_INET) {
		return sizeof(address->v4);
	}
	return sizeof(address->v6);
}

GGPO_Socket GGPO_Socket_udp_create()
{
	int flags = 0;
	GGPO_Socket sock = socket(AF_INET, SOCK_DGRAM, flags);
	assert(sock != GGPO_SOCKET_INVALID);
	return sock;
}

bool GGPO_Socket_bind(GGPO_Socket sock, uint16_t port)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = INADDR_ANY,
	};
	int err = bind(sock, (struct sockaddr const*)&addr, sizeof(addr));
	return err != -1;
}

GGPO_Socket GGPO_Socket_tcp_connect(char const* host, uint16_t port)
{
	int flags = 0;
	GGPO_Socket sock = socket(AF_INET, SOCK_STREAM, flags);
	assert(sock != GGPO_SOCKET_INVALID);

#ifdef __APPLE__
	int set = 1;
    int error = setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
    assert(error == 0);
#endif

	union GGPO_PlatformAddress address = GGPO_PlatformAddress_init(host, port);
	int err = connect(sock, (struct sockaddr const*)&address, GGPO_PlatformAddress_get_length_(&address));
	if (err == -1) {
		GGPO_Socket_destroy(sock);
		return GGPO_SOCKET_INVALID;
	}
	return sock;
}

bool GGPO_Socket_make_nonblocking(GGPO_Socket sock)
{
#ifdef _WIN32
	u_long iMode = 1;
	int err = ioctlsocket(sock, FIONBIO, &iMode);
	assert(err == 0);

	return true;
#else
	int flags = fcntl(sock, F_GETFL, 0);
	assert(flags != -1);
	flags |= O_NONBLOCK;
	int err = fcntl(sock, F_SETFL, flags);
	assert(err != -1);

	return true;
#endif
}

GGPO_ReadResult GGPO_Socket_has_data(GGPO_Socket sock)
{
#ifdef _WIN32
	WSAPOLLFD peer_poll = {
		.fd = sock,
		.events = POLLRDNORM,
	};
	int result = WSAPoll(&peer_poll, 1, 0);
#else
	struct pollfd peer_poll = {
		.fd = sock,
		.events = POLLRDNORM,
	};
	int result = poll(&peer_poll, 1, 0);
#endif
	if (result > 0) {
		return kGGPO_ReadResult_success;
	} else if (result == 0) {
		return kGGPO_ReadResult_wouldblock;
	}
	return kGGPO_ReadResult_error;
}

GGPO_ReadResult GGPO_Socket_has_space(GGPO_Socket sock)
{
#ifdef _WIN32
	WSAPOLLFD peer_poll = {
		.fd = sock,
		.events = POLLWRNORM,
	};
	int result = WSAPoll(&peer_poll, 1, 0);
#else
	struct pollfd peer_poll = {
		.fd = sock,
		.events = POLLWRNORM,
	};
	int result = poll(&peer_poll, 1, 0);
#endif
	if (result > 0) {
		return kGGPO_ReadResult_success;
	} else if (result == 0) {
		return kGGPO_ReadResult_wouldblock;
	}
	return kGGPO_ReadResult_error;
}

bool GGPO_Socket_wait_for_data(GGPO_Socket sock)
{
#ifdef _WIN32
	WSAPOLLFD peer_poll = {
		.fd = sock,
		.events = POLLRDNORM,
	};
	int result = WSAPoll(&peer_poll, 1, -1);
#else
	struct pollfd peer_poll = {
		.fd = sock,
		.events = POLLRDNORM,
	};
	int result = poll(&peer_poll, 1, -1);
#endif
	if (result > 0) {
		return true;
	}
	return false;
}

bool GGPO_Socket_wait_for_space(GGPO_Socket sock)
{
#ifdef _WIN32
	WSAPOLLFD peer_poll = {
		.fd = sock,
		.events = POLLWRNORM,
	};
	int result = WSAPoll(&peer_poll, 1, -1);
#else
	struct pollfd peer_poll = {
		.fd = sock,
		.events = POLLWRNORM,
	};
	int result = poll(&peer_poll, 1, -1);
#endif
	if (result > 0) {
		return true;
	}
	return false;
}

GGPO_ReadResult GGPO_Socket_read(GGPO_Socket sock, size_t* inout_size, void* data)
{
	uint8_t* bytes = data;
	int const flags = 0;
#ifdef _WIN32
	int bytes_read = recv(sock, (char*)bytes, *inout_size, flags);
	if (bytes_read == SOCKET_ERROR) {
		*inout_size = 0;
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK) {
			return kGGPO_ReadResult_wouldblock;
		}
		return kGGPO_ReadResult_error;
	}
	*inout_size = bytes_read;
#else
	int bytes_read = recv(sock, bytes, *inout_size, flags);
	if (bytes_read == -1) {
		*inout_size = 0;
		if (errno == EAGAIN) {
			return kGGPO_ReadResult_wouldblock;
		}
		return kGGPO_ReadResult_error;
	}
	*inout_size = bytes_read;
#endif
	return kGGPO_ReadResult_success;
}

// TODO: Harmonize this result with write_all.
GGPO_ReadResult GGPO_Socket_read_all(GGPO_Socket sock, size_t size, void* data)
{
	uint8_t* bytes = data;
	uint8_t const* const end = bytes + size;
	int const flags = 0;
#ifdef _WIN32
	while (bytes < end) {
		int bytes_read = recv(sock, (char*)bytes, end - bytes, flags);
		if (bytes_read == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				continue;
			}
			return kGGPO_ReadResult_error;
		}
		bytes += bytes_read;
		assert(bytes <= end);
	}
#else
	while (bytes < end) {
		int bytes_read = recv(sock, bytes, end - bytes, flags);
		if (bytes_read == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			return kGGPO_ReadResult_error;
		}
		bytes += bytes_read;
		assert(bytes <= end);
	}
#endif
	return kGGPO_ReadResult_success;
}

GGPO_ReadResult GGPO_Socket_write(GGPO_Socket sock, size_t* inout_size, void const* bytes)
{
#ifdef _WIN32
	int const flags = 0;
	int bytes_sent = send(sock, (char*)bytes, *inout_size, flags);
	if (bytes_sent == SOCKET_ERROR) {
		*inout_size = 0;
		int err = WSAGetLastError();
		if (err == WSAENOBUFS || err == WSAEWOULDBLOCK) {
			return kGGPO_ReadResult_wouldblock;
		}
		assert(err != WSAEHOSTUNREACH && err != WSAECONNRESET && err != WSAENETUNREACH && err != WSAEHOSTUNREACH);
		return kGGPO_ReadResult_error;
	}
	*inout_size = bytes_sent;
#else
#ifdef __linux__
	int const flags = MSG_NOSIGNAL;
#else
	int const flags = 0;
#endif
	int bytes_sent = send(sock, bytes, *inout_size, flags);
	if (bytes_sent == -1) {
		*inout_size = 0;
		if (errno == EAGAIN) {
			return kGGPO_ReadResult_wouldblock;
		}
		return kGGPO_ReadResult_error;
	}
	*inout_size = bytes_sent;
#endif
	return kGGPO_ReadResult_success;
}

bool GGPO_Socket_write_all(GGPO_Socket sock, size_t size, void const* data)
{
	uint8_t const* bytes = data;
	uint8_t const* const end = bytes + size;
#ifdef _WIN32
	int const flags = 0;
	while (bytes < end) {
		int bytes_sent = send(sock, (char*)bytes, end - bytes, flags);
		if (bytes_sent == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAENOBUFS || err == WSAEWOULDBLOCK) {
				continue;
			}
			assert(err != WSAEHOSTUNREACH && err != WSAECONNRESET && err != WSAENETUNREACH && err != WSAEHOSTUNREACH);
			return false;
		}
		bytes += bytes_sent;
		assert(bytes <= end);
	}
#else
#ifdef __linux__
	int const flags = MSG_NOSIGNAL;
#else
	int const flags = 0;
#endif
	while (bytes < end) {
		int bytes_sent = send(sock, bytes, end - bytes, flags);
		if (bytes_sent == -1) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			}
			return false;
		}
		bytes += bytes_sent;
		assert(bytes <= end);
	}
#endif
	return true;
}

bool GGPO_Socket_udp_sendto(GGPO_Socket sock, union GGPO_PlatformAddress const* addr, size_t size, void const* bytes)
{
#ifdef _WIN32
	int flags = 0;
	int bytes_sent = sendto(sock, bytes, size, flags, (struct sockaddr const*)addr, GGPO_PlatformAddress_get_length_(addr));
	if (bytes_sent == SOCKET_ERROR) {
		assert(WSAGetLastError() != WSAEHOSTUNREACH && WSAGetLastError() != WSAECONNRESET && WSAGetLastError() != WSAENETUNREACH);
		return false;
	}
	return true;
#else
	int flags = 0;
	int bytes_sent = sendto(sock, bytes, size, flags, &addr->addr, GGPO_PlatformAddress_get_length_(addr));
	if (bytes_sent == -1) {
		assert(errno != ENETDOWN && errno != ENETUNREACH && errno != EHOSTUNREACH);
		return false;
	}
	return true;
#endif
}

GGPO_ReadResult GGPO_Socket_udp_recvfrom(GGPO_Socket sock, union GGPO_PlatformAddress* addr, size_t size, void* bytes)
{
#ifdef _WIN32
	int flags = 0;
	int addrlen = sizeof(*addr);
	int bytes_sent = recvfrom(sock, bytes, size, flags, &addr->addr, &addrlen);
	if (bytes_sent == SOCKET_ERROR) {
		if (WSAGetLastError() == WSAEWOULDBLOCK) {
			return kGGPO_ReadResult_wouldblock;
		}
		return kGGPO_ReadResult_error;
	}
	return kGGPO_ReadResult_success;
#else
	int flags = 0;
	socklen_t addrlen = sizeof(*addr);
	int bytes_sent = recvfrom(sock, bytes, size, flags, &addr->addr, &addrlen);
	if (bytes_sent == -1) {
		if (errno == EWOULDBLOCK) {
			return kGGPO_ReadResult_wouldblock;
		}
		return kGGPO_ReadResult_error;
	}
	return kGGPO_ReadResult_success;
#endif
}

void GGPO_Socket_destroy(GGPO_Socket sock)
{
	if (sock == GGPO_SOCKET_INVALID) {
		return;
	}
#ifdef _WIN32
	closesocket(sock);
#else
	close(sock);
#endif
}