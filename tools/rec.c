/* See LICENSE for licence details. */
#include "../recterm.h"
#include "../conf.h"
#include "../util.h"
#include "../pseudo.h"

enum {
	TERM_COLS = 80,
	TERM_ROWS = 24,
};

static const char *default_output = "recfile";

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

void tty_init(struct termios *old_termio, struct winsize *ws)
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
	ioctl(STDIN_FILENO, TIOCGWINSZ, ws);

	//ewrite(STDIN_FILENO, "\033[?25l", 6); /* make cusor invisible */
}

void tty_die(struct termios *old_termio, struct winsize *ws)
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sigact, NULL);

	/* restore termio mode */
	tcsetattr(STDIN_FILENO, TCSAFLUSH, old_termio);

	/* restore terminal size */
	ioctl(STDIN_FILENO, TIOCSWINSZ, ws);

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

void write_timegap(int output, struct timeval *prev)
{
	char escseq[BUFSIZE];
	struct timeval now;
	double gap; /* 1 unit = 100ms */

	gettimeofday(&now, NULL);
	/* sec */
	gap = (now.tv_sec - prev->tv_sec) + (double) (now.tv_usec - prev->tv_usec) / 1000000;
	/* gif delay 100 = 1 sec */
	gap = 100.0 * gap;

	if (DEBUG)
		fprintf(stderr, "gap:%lf\n", gap);

	/* define new escape sequence: (TIMEGAP)
		CSI > Ps ~
			> : private parameter
			Ps: time gap (100 == 1sec: the same value of gif delay)
			~ : final character
	*/
	snprintf(escseq, BUFSIZE, "\033[<%d~", (int) gap);
	ewrite(output, escseq, strlen(escseq));
	*prev = now;
}

int main(int argc, char *argv[])
{
	uint8_t buf[BUFSIZE];
	int output;
	char escseq[BUFSIZE];
	ssize_t size;
	int master = -1;
	fd_set fds;
	struct timeval tv, prev;
	struct winsize ws;
	struct termios old_termio;

	/* check args */
	if (argc < 2) 
		output = open(default_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	else
		output = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);

	/* init */
	tty_init(&old_termio, &ws);

	/* fork and exec shell */
	//ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
	//fork_and_exec(&master, ws.ws_row, ws.ws_col);

	/* set termio size 80x24 */
	fork_and_exec(&master, TERM_ROWS, TERM_COLS);

	/* set terminal size 80x24 (dtterm sequence) */
	snprintf(escseq, BUFSIZE, "\033[8;%d;%dt", TERM_ROWS, TERM_COLS);
	ewrite(master, escseq, strlen(escseq));
	ewrite(output, escseq, strlen(escseq));

	gettimeofday(&prev, NULL);

	/* main loop */
	while (tty.loop_flag) {
		check_fds(&fds, &tv, STDIN_FILENO, master);
		if (FD_ISSET(STDIN_FILENO, &fds)) {
			if ((size = read(STDIN_FILENO, buf, BUFSIZE)) > 0)
				ewrite(master, buf, size);
		}
		if (FD_ISSET(master, &fds)) {
			if ((size = read(master, buf, BUFSIZE)) > 0) {
				ewrite(STDOUT_FILENO, buf, size);
				/* output shell output to file (add gap info) */
				write_timegap(output, &prev);
				ewrite(output, buf, size);
			}
		}
	}

	/* normal exit */
	tty_die(&old_termio, &ws);
	eclose(output);

	return EXIT_SUCCESS;
}
