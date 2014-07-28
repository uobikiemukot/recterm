/* See LICENSE for licence details. */
#include "../recterm.h"
#include "../conf.h"
#include "../util.h"
#include "../pseudo.h"
#include "../terminal.h"
#include "../function.h"
#include "../osc.h"
#include "../dcs.h"
#include "../parse.h"
#include "../gifsave89.h"
#include "../gif.h"

enum {
	TERM_WIDTH      = 640,
	TERM_HEIGHT     = 384,
	INPUT_WAIT      = 0,
	INPUT_BUF       = 1,
	OUTPUT_BUF      = 1024,
	NO_OUTPUT_LIMIT = 16,
};

void sig_handler(int signo)
{
	if (DEBUG)
		fprintf(stderr, "caught signal(no: %d)! exiting...\n", signo);

	if (signo == SIGCHLD) {
		wait(NULL);
		tty.child_killed = true;
		tty.loop_flag    = false;
	}
	else if (signo == SIGINT) {
		tty.loop_flag = false;
	}
}

void sig_set()
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = sig_handler;
	sigact.sa_flags   = SA_RESTART;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGCHLD, &sigact, NULL);
}

void sig_reset()
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = SIG_DFL;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGCHLD, &sigact, NULL);
}

pid_t fork_and_exec(int *master, int lines, int cols)
{
	pid_t pid;
	struct winsize ws = {.ws_row = lines, .ws_col = cols,
		.ws_xpixel = 0, .ws_ypixel = 0};

	pid = eforkpty(master, NULL, NULL, &ws);

	if (pid == 0) { /* child */
		esetenv("TERM", term_name, 1);
		eexecvp(shell_cmd, (const char *[]){shell_cmd, NULL});
	}
	return pid;
}

void check_fds(fd_set *fds, struct timeval *tv, int fd)
{
	FD_ZERO(fds);
	FD_SET(fd, fds);
	tv->tv_sec  = 0;
	tv->tv_usec = SELECT_TIMEOUT;
	eselect(fd + 1, fds, NULL, NULL, tv);
}

int main(int argc, char *argv[])
{
	uint8_t ibuf[INPUT_BUF], obuf[OUTPUT_BUF];
	int fd, no_output_count = 0;
	ssize_t rsize, osize;
	pid_t child_pid;
	fd_set fds;
	struct timeval tv;
	struct terminal term;
	struct pseudobuffer pb;

	struct gif_t gif;
	unsigned char *capture;

	fd = (argc < 2) ? STDIN_FILENO: eopen(argv[1], O_RDONLY);

	/* init */
	setlocale(LC_ALL, "");
	pb_init(&pb, TERM_WIDTH, TERM_HEIGHT);
	term_init(&term, pb.width, pb.height);
	sig_set();

	/* fork and exec shell */
	child_pid = fork_and_exec(&term.fd, term.lines, term.cols);

	/* init gif */
	gif_init(&gif, pb.width, pb.height);
	capture = (unsigned char *) ecalloc(pb.width * pb.height, 1);

	/* main loop */
	while (no_output_count < NO_OUTPUT_LIMIT && tty.loop_flag) {
		usleep(INPUT_WAIT);
		rsize = read(fd, ibuf, INPUT_BUF);
		if (rsize > 0)
			ewrite(term.fd, ibuf, rsize);

		check_fds(&fds, &tv, term.fd);
		if (FD_ISSET(term.fd, &fds)) {
			osize = read(term.fd, obuf, OUTPUT_BUF);
			if (osize > 0) {
				parse(&term, obuf, osize);
				refresh(&pb, &term);

				/* take screenshot */
				if (osize != OUTPUT_BUF) {
					apply_colormap(&pb, capture);
					putgif(gif.data, capture);
				}
			}
			no_output_count = 0;
		} else {
			no_output_count++;
		}
	}

	if (!tty.child_killed) {
		kill(child_pid, SIGKILL);
		wait(NULL);
	}

	/* output gif */
	gif_die(&gif, stdout);
	free(capture);

	/* normal exit */
	sig_reset();
	term_die(&term);
	eclose(fd);

	return EXIT_SUCCESS;
}
