#define _GNU_SOURCE

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>

#include "logging.h"

#define PREENY_MAX_FD 8192
#define PREENY_SOCKET_OFFSET 500
#define READ_BUF_SIZE 65536

// #define PREENY_SIN_PORT 9000
#define PREENY_SIN_PORT 35150
// #define PREENY_SIN_PORT 17900 //17902

#define PREENY_SOCKET(x) (x+PREENY_SOCKET_OFFSET)

int preeny_desock_shutdown_flag = 0;
int preeny_desock_accepted_sock = -1;

pthread_t *preeny_socket_threads_to_front[PREENY_MAX_FD] = { 0 };
pthread_t *preeny_socket_threads_to_back[PREENY_MAX_FD] = { 0 };

int preeny_socket_sync(int from, int to, int timeout)
{
	struct pollfd poll_in = { from, POLLIN, 0 };
	char read_buf[READ_BUF_SIZE];
	int total_n;
	char error_buf[1024];
	int n;
	int r;

	r = poll(&poll_in, 1, timeout);
	if (r < 0)
	{
		strerror_r(errno, error_buf, 1024);
		preeny_debug("read poll() received error '%s' on fd %d\n", error_buf, from);
		return 0;
	}
	else if (poll_in.revents == 0)
	{
		preeny_debug("read poll() timed out on fd %d\n", from);
		return 0;
	}

	total_n = read(from, read_buf, READ_BUF_SIZE);
	if (total_n < 0)
	{
		strerror_r(errno, error_buf, 1024);
		preeny_info("synchronization of fd %d to %d shutting down due to read error '%s'\n", from, to, error_buf);
		return -1;
	}
	else if (total_n == 0 && from == 0)
	{
		preeny_info("synchronization of fd %d to %d shutting down due to EOF\n", from, to);
		return -1;
	}
	preeny_debug("read %d bytes from %d (will write to %d)\n", total_n, from, to);

	n = 0;
	while (n != total_n)
	{
		r = write(to, read_buf, total_n - n);
		if (r < 0)
		{
			strerror_r(errno, error_buf, 1024);
			preeny_info("synchronization of fd %d to %d shutting down due to read error '%s'\n", from, to, error_buf);
			return -1;
		}
		n += r;
	}

	preeny_debug("wrote %d bytes to %d (had read from %d)\n", total_n, to, from);
	return total_n;
}

__attribute__((destructor)) void preeny_desock_shutdown()
{
	int i;
	int to_sync[PREENY_MAX_FD] = { };

	preeny_debug("shutting down desock...\n");
	preeny_desock_shutdown_flag = 1;


	for (i = 0; i < PREENY_MAX_FD; i++)
	{
		if (preeny_socket_threads_to_front[i])
		{
			preeny_debug("sending SIGINT to thread %d...\n", i);
			pthread_join(*preeny_socket_threads_to_front[i], NULL);
			pthread_join(*preeny_socket_threads_to_back[i], NULL);
			preeny_debug("... sent!\n");
			to_sync[i] = 1;
		}
	}

	for (i = 0; i < PREENY_MAX_FD; i++)
	{
		if (to_sync[i])
		{
			//while (preeny_socket_sync(0, PREENY_SOCKET(i), 10) > 0);
			while (preeny_socket_sync(PREENY_SOCKET(i), 1, 0) > 0);
		}
	}

	preeny_debug("... shutdown complete!\n");
}

void preeny_socket_sync_loop(int from, int to)
{
	int r;

	preeny_debug("starting forwarding from %d to %d!\n", from, to);

	while (!preeny_desock_shutdown_flag)
	{
		r = preeny_socket_sync(from, to, 15);
		if (r < 0) return;
	}
}

#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"

void *preeny_socket_sync_to_back(void *fd)
{
	int front_fd = (int)fd;
	int back_fd = PREENY_SOCKET(front_fd);
	preeny_socket_sync_loop(back_fd, 1);
	return NULL;
}

void *preeny_socket_sync_to_front(void *fd)
{
	int front_fd = (int)fd;
	int back_fd = PREENY_SOCKET(front_fd);
	preeny_socket_sync_loop(0, back_fd);
	shutdown(back_fd, SHUT_WR);
	return NULL;
}

//
// originals
//
int (*original_socket)(int, int, int);
int (*original_bind)(int, const struct sockaddr *, socklen_t);
int (*original_listen)(int, int);
int (*original_accept)(int, struct sockaddr *, socklen_t *);
int (*original_connect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int (*original_close)(int fd);
int (*original_shutdown)(int sockfd, int how);
int (*original_getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int (*original_setsockopt)(int sockfd, int level, int optname, const void* optval, socklen_t optlen);
__attribute__((constructor)) void preeny_desock_orig()
{
	original_socket = dlsym(RTLD_NEXT, "socket");
	original_listen = dlsym(RTLD_NEXT, "listen");
	original_accept = dlsym(RTLD_NEXT, "accept");
	original_bind = dlsym(RTLD_NEXT, "bind");
	original_connect = dlsym(RTLD_NEXT, "connect");
	original_close = dlsym(RTLD_NEXT, "close");
	original_shutdown = dlsym(RTLD_NEXT, "shutdown");
	original_getsockname = dlsym(RTLD_NEXT, "getsockname");
	original_setsockopt = dlsym(RTLD_NEXT, "setsockopt");
}

int socket(int domain, int type, int protocol)
{
	preeny_debug("--- Emulating original Socket()\n");
	return original_socket(domain, type, protocol);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	
	preeny_debug("--- Emulating original Accept\n");
	return original_accept(sockfd, addr, addrlen);
	
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	preeny_debug("--- Emulating original Accept\n");
	return accept(sockfd, addr, addrlen);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	preeny_debug("--- Emulating original Bind\n");
	return original_bind(sockfd, addr, addrlen);
}

int listen(int sockfd, int backlog)
{
	preeny_debug("--- Emulating original listen\n");
	return original_listen(sockfd, backlog);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	preeny_debug("--- Emulating original connect\n");
	return original_connect(sockfd, addr, addrlen);
}

int close(int fd) {
	preeny_debug("--- Emulating original close\n");
	return original_close(fd);
}

int shutdown(int sockfd, int how) {
	preeny_debug("--- Emulating original shutdown\n");
	return original_shutdown(sockfd, how);
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	preeny_debug("--- Emulating original getsockname\n");
	return original_getsockname(sockfd, addr, addrlen);
}

// Bool indicator
int preeny_socket_hooked[PREENY_MAX_FD] = { 0 }; 


int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
	preeny_debug("--- Emulating original listen\n");
	return original_setsockopt(sockfd, level, optname, optval, optlen);
}
