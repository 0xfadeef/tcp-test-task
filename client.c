#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "defs.h"

char data[BUFFSZ];

/* _Noreturn */
void die(const char* s) {
	if (s) perror(s);
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
	struct addrinfo *res, *rp;
	struct addrinfo hints = {0};
	int sta, cli_sock;

	char* const node = (argc > 1) ? argv[1] : DEF_ADDR;

	setbuf(stdout, NULL); /*-- not portable iirc */

	/* it's probably more safe to bzero this struct */
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	sta = getaddrinfo(node, DEF_PORT, &hints, &res);
	if (sta) {
		fprintf (stderr, "getaddrinfo: %s\n", gai_strerror(sta));
		die(NULL);
	}
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		cli_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (cli_sock == -1) continue;
		if (connect(cli_sock, rp->ai_addr, rp->ai_addrlen) == -1) {
			close(cli_sock);
			continue;
		}
		break; /*-- connected successfully */
	}
	freeaddrinfo(res);
	if (rp == NULL) {
		fprintf(stderr, "client: failed to connect to %s\n", node);
		die(NULL);
	}

	printf("Connected to %s\n", node);
	while (1) {
		printf("\nsend> ");
		if (fgets(data, BUFFSZ, stdin) == NULL)
			break;
		/* trim trailing newline char and quit if filename is empty */
		sta = strlen(data);
		if (sta > 0 && data[sta-1] == '\n')
			data[--sta] = '\0';
		if (sta == 0) break;

		if (sendfileto(data, cli_sock) < 0) /*== ERR_HARD */
			break;
	}
	//sendgoodbye(cli_sock);
	close(cli_sock);
	return EXIT_SUCCESS;
}

int sendgoodbye(int socket) {
	const char ch = SYMB_EOT;
	return send(socket, &ch, 1, MSG_OOB);
}

/* TODO: it's probably better to use struct msghdr for marshalling instead 
         of my structure and then utilize sendmsg, recvmsg functions */

int sendheader(unsigned int filesize, char* filename, int socket) {
	int namelen = strlen(filename) + 1; /* reserve one char for terminating zero */
    int fdsz = sizeof(struct fdesc) + sizeof(char [namelen]);

	struct fdesc* pfd = (struct fdesc*) malloc(fdsz);
	if (!pfd) {
		perror("malloc");
		return ERR_SOFT;
	}
	pfd->name_len  = htonl(namelen);
	pfd->file_size = htonl(filesize);
	strncpy(pfd->file_name, filename, namelen);

	if (send(socket, pfd, fdsz, 0) != fdsz) {
		fprintf(stderr, "client: unable to send header\n");
		fdsz = ERR_HARD;
	}
	free(pfd);
	return fdsz; /* return the size of a header */
}

int sendfileto(char* filename, int socket) {
	struct stat st;
	FILE* fp;
	int rlen, qu = ERR_HARD;
	int s, rv;

	fp = fopen(filename, "rb");
	if (fp == NULL || stat(filename, &st) == -1) {
		perror("fopen");
		return ERR_SOFT;
	}
	
	printf("sending header... ");
	rlen = sendheader(st.st_size, basename(filename), socket);
	if (!(rlen > 0)) {
		printf("failed\n");
		qu = rlen;
		goto err;
	}
	rlen = strlen(SRV_RSPOK); /* all messages have the same length */
	if (recv(socket, data, rlen, 0) == -1) {
		perror("receive_header");
		goto err;
	}
	if (strncmp(data, SRV_RETRY, rlen) == 0) {
		qu = ERR_SOFT;
		fprintf(stderr, "client: retry request\n");
		goto err;
	}
	if (strncmp(data, SRV_RSPOK, rlen)) {
		data[rlen] = '\0';
		fprintf(stderr, "client: invalid server response: %s\n", data);
		goto err;
	}
	printf("OK\n");
	
	/* TODO: man 2 sendfile */
	qu = 0; /*-- what has been sent so far */
	printf("sending file... ");
	while (!feof(fp)) {
		rlen = fread(data, sizeof(char), BUFFSZ, fp);
		if (rlen == 0) continue;

		rlen *= sizeof(char); /* convert to bytes */
		for (s = 0; s < rlen; s += rv) {
			rv = send(socket, data+s, rlen-s, 0);
			if (!(rv > 0)) {
				if (rv == -1) perror("send_file");
				fprintf(stderr, "client: unable to send file\n");
				qu = ERR_HARD;
				goto err;
			}
			qu = qu + rv;
			/* gotta update the number on a screen */
			printf("\033[s"); /* save cursor position */
			printf("%u%%", (unsigned int)(100.*qu/st.st_size));
			printf("\033[u"); /* restore cursor */
		}
	}
err:
	fclose(fp);
	return qu;
}








