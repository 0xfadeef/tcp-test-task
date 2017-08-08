#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "defs.h"
#define MAX_CLIENTS		20
#define HDR_RETRY_N		4

static pthread_mutex_t sout_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int prev; /* = 0 */

/* TODO: is there a way to validate thread id? */
static struct {
	pthread_t th_id;
	char data[BUFFSZ];
	bool alive;
} cli[MAX_CLIENTS] = {
	{ .alive = false }
};

/* _Noreturn */
void die(const char* s) {
	if (s) perror(s);
	exit(EXIT_FAILURE);
}

void* get_addr(struct sockaddr_storage* sa) {
	return (sa->ss_family == AF_INET) ? 
		&(((struct sockaddr_in*)  sa)->sin_addr) : /* IPv4 */
		&(((struct sockaddr_in6*) sa)->sin6_addr); /* IPv6 */
}

static void move_cursor_to(int pos) {
	int r = pos - prev;
	if (r != 0) {
		printf("\033[%d%c", (r > 0 ? r : -r), (r > 0 ? 'A' : 'B'));
	}
}

void* process(void* arg);

int main(int argc, char** argv) {
	struct addrinfo *res, *rp;
	struct addrinfo hints = {0};
	int sta, serv_sock;

	struct sockaddr_storage cli_addr;
	socklen_t addr_len;
	int cli_sock;

	FILE* flog = freopen("server.log", "wt", stderr);
	setbuf(stdout, NULL); /*-- not portable iirc */

	/* TODO: consider using MSG_NOSIGNAL flag instead */
	if(set_sig_hand(SIGPIPE, SIG_IGN) == -1) {
		fprintf(stderr, "sigaction: unable to set disposition for SIGPIPE\n");
		die(NULL);
	}

	/* it's probably more safe to bzero this struct */
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	sta = getaddrinfo(NULL, DEF_PORT, &hints, &res);
	if (sta) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(sta));
		die(NULL);
	}
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		serv_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (serv_sock == -1) continue;

		sta = 1;
		if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &sta, sizeof(int)) == -1)
			die("setsocketopt");
		if (bind(serv_sock, rp->ai_addr, rp->ai_addrlen) == -1) {
			close(serv_sock);
			continue;
		}
		break; /* all good, bail out */
	}
	freeaddrinfo(res);
	if (rp == NULL) {
		fprintf(stderr, "server: failed to bind to port %s\n", DEF_PORT);
		die(NULL);
	}

	if (listen(serv_sock, 5) == -1) die("listen");
	printf("Listening on port %s ...\n\n", DEF_PORT);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	for (;;) {
		addr_len = sizeof(cli_addr);
		cli_sock = accept(serv_sock, (struct sockaddr*) &cli_addr, &addr_len);
		if (cli_sock == -1) {
			perror("accept");
			continue;
		}
		for (sta = 0; sta < MAX_CLIENTS; ++sta) {
			if (!cli[sta].alive) break;
		}
		if (sta == MAX_CLIENTS) {
			close(cli_sock);
			continue;
		}

		inet_ntop(cli_addr.ss_family, get_addr(&cli_addr), cli[sta].data, 
			INET6_ADDRSTRLEN);

		pthread_mutex_lock(&sout_mutex);
		move_cursor_to(sta);
		printf("\033[100D");
		printf("Client connected %s\t\t", cli[sta].data);
		prev = sta;
		pthread_mutex_unlock(&sout_mutex);

		if (pthread_create(&cli[sta].th_id, &attr, process, 
				(int []){cli_sock, sta}) == 0) {
			cli[sta].alive = true;
		}
	}

	pthread_attr_destroy(&attr);
	close(serv_sock);
	if (flog) fclose(flog);
	return EXIT_SUCCESS;
}

void* process(void* arg) {
	int* argi = (int*) arg;

	while (recvfilefrom(argi[0], argi[1]) >= 0);
	cli[argi[1]].alive = false;

	return (void*)0;
}

struct fdesc* recvheader(int socket) {
	unsigned int nl, fs;
	const size_t uisz = sizeof(unsigned int);
	struct fdesc* pfd = NULL;

	if (recv(socket, &nl, uisz, 0) != uisz) {
		goto fail;
	}
	nl = ntohl(nl);
	fs = sizeof(struct fdesc) + sizeof(char [nl]);
	pfd = (struct fdesc*) malloc(fs);
	if (!pfd) {
		perror("malloc");
		return NULL;
	}
	pfd->name_len = nl;

	if (recv(socket, &fs, uisz, 0) != uisz) {
		goto fail;
	}
	pfd->file_size = ntohl(fs);
	
	if (recv(socket, pfd->file_name, nl, 0) != nl) {
		goto fail;
	}
	return pfd;
fail:
	fprintf(stderr, "server: unable to receive header\n");
	free(pfd);
	return NULL;
}

int recvfilefrom(int socket, const int num) {
	struct fdesc* pfd;
	int rlen = 1, qu = ERR_HARD;
	FILE* fp = NULL;

	while (1) {
		pfd = recvheader(socket);
		if (pfd) {
			fp = fopen(pfd->file_name, "wb");
			if (fp) break; /*-- OK */
			perror("fopen");
			free(pfd);
		}
		fprintf(stderr, "server: retry(%d) to receive header\n", rlen);
		if (send(socket, SRV_RETRY, strlen(SRV_RETRY), 0) == -1) {
			perror("send_retry");
			goto err;
		}
		if (++rlen == HDR_RETRY_N) goto err;
	}
	if (send(socket, SRV_RSPOK, strlen(SRV_RSPOK), 0) == -1) {
		/* not a big deal, but it can cause a block on recv later,
		   so it seems like a better choice to just fall back */
		perror("send_ok");
		goto err;
	}

	qu = 0;
	while (qu < pfd->file_size) {
		rlen = recv(socket, cli[num].data, BUFFSZ, 0);
		if (!(rlen > 0)) {
			if (rlen == -1) perror("receive_file");
			fprintf(stderr, "server: unable to receive data\n");
			qu = ERR_HARD;
			goto err;
		}

		fwrite(cli[num].data, sizeof(char), rlen/sizeof(char), fp);
		qu = qu + rlen;
		/* update the numbers */
		pthread_mutex_lock(&sout_mutex);
		move_cursor_to(num);
		printf("\033[s"); /* save cursor position */
		printf("%u%%", (unsigned int)(100.*qu/pfd->file_size));
		printf("\033[u"); /* restore cursor */
		prev = num;
		pthread_mutex_unlock(&sout_mutex);
	}
err:
	free(pfd);
	if (fp) fclose(fp);
	return qu;
}










