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
#include "mk_wcwidth.h"

enum {
	TERM_WIDTH  = 640,
	TERM_HEIGHT = 384,
	TERM_COLS   = 80,
	TERM_ROWS   = 24,
	INPUT_SIZE  = 1,
	MIN_DELAY   = 5,
};

static const char *default_output = "recterm.gif";

void usage()
{
	printf(
		"usage: recterm [-h] [-m min_delay] [-r rows] [-c cols] [-a width] [output]\n"
		"\t-h          : show this help                                          \n"
		"\t-m min_delay: minimum delay (1/100 sec) of animation gif (default: 5) \n"
		"\t-r rows     : column size of internal termios (default: 80)           \n"
		"\t-c cols     : row size of internal termios (default: 24)              \n"
		"\t-a width    : wide or half for ambiguous width (default: wide)        \n"
		"\toutput      : output filename (default: recterm.gif)                  \n"
	);
}

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

int get_timegap(struct timeval *prev, int min_delay)
{
	struct timeval now;
	double gap; /* 1 unit = 1/100 sec */

	gettimeofday(&now, NULL);
	/* sec */
	gap = (now.tv_sec - prev->tv_sec) + (double) (now.tv_usec - prev->tv_usec) / 1000000;
	/* gif delay 100 = 1 sec */
	gap = 100.0 * gap;

	if (gap < min_delay)
		gap = min_delay;

	if (DEBUG)
		fprintf(stderr, "gap:%lf\n", gap);

	*prev = now;
	return (int) gap;
}

int main(int argc, char *argv[])
{
	uint8_t buf[BUFSIZE], *capture;
	uint16_t rows = TERM_ROWS, cols = TERM_COLS;
	char escseq[BUFSIZE];
	bool screen_refreshed = false, is_first_frame = true, ambiguous_is_wide = true;
	ssize_t size;
	int master = -1, delay = MIN_DELAY, gap, opt;
	fd_set fds;
	FILE *output;
	struct timeval tv, prev;
	struct winsize old_ws;
	struct termios old_termio;
	struct pseudobuffer pb;
	struct terminal term;
	struct gif_t gif;

	/* check args */
	while ((opt = getopt(argc, argv, "hm:r:c:a:")) != -1) {
		switch (opt) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'm':
			delay = dec2num(optarg);
			break;
		case 'r':
			rows  = dec2num(optarg);
			break;
		case 'c':
			cols  = dec2num(optarg);
			break;
		case 'a':
			if (strstr(optarg, "half") != NULL)
				ambiguous_is_wide = false;
			break;
		default:
			break;
		}
	}
	output = (optind < argc) ?
		efopen(argv[optind], "w"): efopen(default_output, "w");

	if (delay <= 0)
		delay = MIN_DELAY;
	if (rows == 0)
		rows = TERM_ROWS;
	if (cols == 0)
		cols = TERM_COLS;

	my_wcwidth = (ambiguous_is_wide) ? mk_wcwidth_cjk: mk_wcwidth;

	/* init */
	pb_init(&pb, CELL_WIDTH * cols, CELL_HEIGHT * rows);
	term_init(&term, pb.width, pb.height, ambiguous_is_wide);
	tty_init(&old_termio, &old_ws);

	/* fork and exec shell */
	fork_and_exec(&master, rows, cols);

	/* set terminal size (dtterm sequence) */
	snprintf(escseq, BUFSIZE, "\033[8;%d;%dt", rows, cols);
	ewrite(STDOUT_FILENO, escseq, strlen(escseq));

	/* reset terminal */
	ewrite(STDOUT_FILENO, "\033c", 2);

	gif_init(&gif, pb.width, pb.height);
	capture = (uint8_t *) ecalloc(pb.width * pb.height, 1);
	get_timegap(&prev, delay);

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
					continue; /* maybe more data arrives soon */
				else
					screen_refreshed = true;
			}
		}
		/* add new frame to gif */
		if (screen_refreshed) {
			gap = get_timegap(&prev, delay);
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
