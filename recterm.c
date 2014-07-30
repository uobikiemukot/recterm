/* See LICENSE for licence details. */
#include "recterm.h"
#include "conf.h"
#include "util.h"
#include "pseudo.h"
#include "terminal.h"
#include "function.h"
#include "osc.h"
#include "dcs.h"
#include "parse.h"
#include "gifsave89.h"
#include "gif.h"

enum {
	TERM_WIDTH  = 640,
	TERM_HEIGHT = 384,
	TERM_COLS   = 80,
	TERM_ROWS   = 24,
	INPUT_SIZE  = 1,
	MINIMUM_DELAY = 10,
};

static const char *default_output = "recterm.gif";

void sig_handler(int signo)
{
	if (DEBUG)
		fprintf(stderr, "caught signal(num:%d)\n", signo);

	if (signo == SIGCHLD) {
		wait(NULL);
		tty.loop_flag = false;
	}
}

void set_rawmode(int fd, struct termios *old_termio)
{
	struct termios termio;

	termio = *old_termio;
	termio.c_iflag = termio.c_oflag = 0;
	termio.c_cflag &= ~CSIZE;
	termio.c_cflag |= CS8;
	termio.c_lflag &= ~(ECHO | ISIG | ICANON);
	termio.c_cc[VMIN]  = 1; /* min data size (byte) */
	termio.c_cc[VTIME] = 0; /* time out */
	etcsetattr(fd, TCSAFLUSH, &termio);
}

void tty_init(struct termios *old_termio, struct winsize *old_ws)
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = sig_handler;
	sigact.sa_flags   = SA_RESTART;
	esigaction(SIGCHLD, &sigact, NULL);

	/* save current termio mode and disable linebuffering/echo */
	etcgetattr(STDIN_FILENO, old_termio);
	set_rawmode(STDIN_FILENO, old_termio);

	/* save current terminal size */
	ioctl(STDIN_FILENO, TIOCGWINSZ, old_ws);

	//ewrite(STDIN_FILENO, "\033[?25l", 6); /* make cusor invisible */
}

void tty_die(struct termios *old_termio, struct winsize *old_ws)
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sigact, NULL);

	/* restore termio mode */
	tcsetattr(STDIN_FILENO, TCSAFLUSH, old_termio);

	/* restore terminal size */
	ioctl(STDIN_FILENO, TIOCSWINSZ, old_ws);

	//ewrite(STDIN_FILENO, "\033[?25h", 6); /* make cursor visible */
}

void fork_and_exec(int *master, int lines, int cols)
{
	char *shell_env;
	pid_t pid;
	struct winsize ws = {.ws_row = lines, .ws_col = cols,
		.ws_xpixel = 0, .ws_ypixel = 0};

	pid = eforkpty(master, NULL, NULL, &ws);

	if (pid == 0) { /* child */
		esetenv("TERM", term_name, 1);
		if ((shell_env = getenv("SHELL")) != NULL)
			eexecvp(shell_env, (const char *[]){shell_env, NULL});
		else
			eexecvp(shell_cmd, (const char *[]){shell_cmd, NULL});
	}
}

void check_fds(fd_set *fds, struct timeval *tv, int input, int master)
{
	FD_ZERO(fds);
	FD_SET(input, fds);
	FD_SET(master, fds);
	tv->tv_sec  = 0;
	tv->tv_usec = SELECT_TIMEOUT;
	eselect(master + 1, fds, NULL, NULL, tv);
}

int get_timegap(struct timeval *prev)
{
	struct timeval now;
	double gap; /* 1 unit = 1/100 sec */

	gettimeofday(&now, NULL);
	/* sec */
	gap = (now.tv_sec - prev->tv_sec) + (double) (now.tv_usec - prev->tv_usec) / 1000000;
	/* gif delay 100 = 1 sec */
	gap = 100.0 * gap;

	if (gap < MINIMUM_DELAY)
		gap = MINIMUM_DELAY;

	if (DEBUG)
		fprintf(stderr, "gap:%lf\n", gap);

	*prev = now;
	return (int) gap;
}

int main(int argc, char *argv[])
{
	uint8_t buf[BUFSIZE];
	char escseq[BUFSIZE];
	ssize_t size;
	int master = -1, gap;
	fd_set fds;
	FILE *output;
	struct timeval tv, prev;
	struct winsize old_ws;
	struct termios old_termio;
	struct pseudobuffer pb;
	struct terminal term;
	struct gif_t gif;
	unsigned char *capture;
	bool screen_refreshed = false, is_first_frame = true;

	/* check args */
	output = (argc < 2) ? efopen(default_output, "w"): efopen(argv[1], "w");

	/* init */
	setlocale(LC_ALL, "");
	pb_init(&pb, TERM_WIDTH, TERM_HEIGHT);
	term_init(&term, pb.width, pb.height);
	tty_init(&old_termio, &old_ws);

	/* fork and exec shell */
	/* use current termio size */
	//ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
	//fork_and_exec(&master, ws.ws_row, ws.ws_col);

	/* set termio size 80x24 */
	fork_and_exec(&master, TERM_ROWS, TERM_COLS);

	/* set terminal size 80x24 (dtterm sequence) */
	snprintf(escseq, BUFSIZE, "\033[8;%d;%dt", TERM_ROWS, TERM_COLS);
	ewrite(STDOUT_FILENO, escseq, strlen(escseq));

	/* reset terminal */
	ewrite(STDOUT_FILENO, "\033c", 2);

	gif_init(&gif, pb.width, pb.height);
	capture = (unsigned char *) ecalloc(pb.width * pb.height, 1);
	get_timegap(&prev);

	/* main loop */
	while (tty.loop_flag) {
		check_fds(&fds, &tv, STDIN_FILENO, master);
		if (FD_ISSET(STDIN_FILENO, &fds)) {
			if ((size = read(STDIN_FILENO, buf, INPUT_SIZE)) > 0)
				ewrite(master, buf, size);
		}
		if (FD_ISSET(master, &fds)) {
			if ((size = read(master, buf, BUFSIZE)) > 0) {
				parse(&term, buf, size);
				refresh(&pb, &term);

				/* pass through */
				ewrite(STDOUT_FILENO, buf, size);

				if (size == BUFSIZE)
					;/* maybe more data arrives soon */
				else
					screen_refreshed = true;
			}
		}
		/* add new frame to gif */
		if (screen_refreshed) {
			gap = get_timegap(&prev);
			if (is_first_frame) {
				is_first_frame = false; /* not capture */
			}
			else {
				controlgif(gif.data, /* transparency color index */ -1,
					/* delay */ gap, /* userinput */ 0, /* disposal */ 2);
				putgif(gif.data, capture);
			}
			apply_colormap(&pb, capture);
			screen_refreshed = false;
		}
	}

	/* normal exit */
	gif_die(&gif, output);
	free(capture);

	tty_die(&old_termio, &old_ws);
	term_die(&term);
	pb_die(&pb);

	efclose(output);

	return EXIT_SUCCESS;
}
