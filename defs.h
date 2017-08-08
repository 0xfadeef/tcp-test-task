#ifndef DEFS_H
#define DEFS_H

#define DEF_PORT	"8888"
#define DEF_ADDR	"127.0.0.1" /* "localhost" */
#define BUFFSZ		BUFSIZ

/* TODO: come up with more elaborate messages */
#define SRV_RSPOK	"SRV_RSPOK"
#define SRV_RETRY	"SRV_RETRY"
#define SYMB_EOT	0x4

#define ERR_HARD	-1
#define ERR_SOFT	0

struct __attribute__((packed)) fdesc {
	unsigned int name_len;
	unsigned int file_size;
	char file_name[0];
};

#include <signal.h>

static inline int set_sig_hand(int signo, void (*hndl)(int)) {
	struct sigaction act = {
		.sa_handler = hndl,
		.sa_flags = SA_RESTART
	};
	sigemptyset(&act.sa_mask);
	return sigaction(signo, &act, NULL);
}

#endif /* DEFS_H */
