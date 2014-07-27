/*
	Copyright (C) 2014 haru <uobikiemukot at gmail dot com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
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

enum {
	TERM_WIDTH  = 640,
	TERM_HEIGHT = 384,
	TERM_COLS   = 80,
	TERM_ROWS   = 24,
	INPUT_SIZE  = 1,
};

enum cmap_bitfield {
	RED_SHIFT   = 5,
	GREEN_SHIFT = 2,
	BLUE_SHIFT  = 0,
	RED_MASK	= 3,
	GREEN_MASK  = 3,
	BLUE_MASK   = 2
};

struct gif_t {
	void *data;
	unsigned char *image;
	int colormap[COLORS * BYTES_PER_PIXEL + 1];
};

static const char *default_output = "irec.gif";

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
	pid_t pid;
	struct winsize ws = {.ws_row = lines, .ws_col = cols,
		.ws_xpixel = 0, .ws_ypixel = 0};

	pid = eforkpty(master, NULL, NULL, &ws);

	if (pid == 0) { /* child */
		esetenv("TERM", term_name, 1);
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

void set_colormap(int colormap[COLORS * BYTES_PER_PIXEL + 1])
{
	int i, ci, r, g, b;
	uint8_t index;

	/* colormap: terminal 256color
	for (i = 0; i < COLORS; i++) {
		ci = i * BYTES_PER_PIXEL;

		r = (color_list[i] >> 16) & bit_mask[8];
		g = (color_list[i] >> 8)  & bit_mask[8];
		b = (color_list[i] >> 0)  & bit_mask[8];

		colormap[ci + 0] = r;
		colormap[ci + 1] = g;
		colormap[ci + 2] = b;
	}
	*/

	/* colormap: red/green: 3bit blue: 2bit
	*/
	for (i = 0; i < COLORS; i++) {
		index = (uint8_t) i;
		ci = i * BYTES_PER_PIXEL;

		r = (index >> RED_SHIFT)   & bit_mask[RED_MASK];
		g = (index >> GREEN_SHIFT) & bit_mask[GREEN_MASK];
		b = (index >> BLUE_SHIFT)  & bit_mask[BLUE_MASK];

		colormap[ci + 0] = r * bit_mask[BITS_PER_BYTE] / bit_mask[RED_MASK];
		colormap[ci + 1] = g * bit_mask[BITS_PER_BYTE] / bit_mask[GREEN_MASK];
		colormap[ci + 2] = b * bit_mask[BITS_PER_BYTE] / bit_mask[BLUE_MASK];
	}
	colormap[COLORS * BYTES_PER_PIXEL] = -1;
}

uint32_t pixel2index(uint32_t pixel)
{
	/* pixel is always 24bpp */
	uint32_t r, g, b;

	/* split r, g, b bits */
	r = (pixel >> 16) & bit_mask[8];
	g = (pixel >> 8)  & bit_mask[8];
	b = (pixel >> 0)  & bit_mask[8];

	/* colormap: terminal 256color
	if (r == g && r == b) { // 24 gray scale
		r = 24 * r / COLORS;
		return 232 + r;
	}					   // 6x6x6 color cube

	r = 6 * r / COLORS;
	g = 6 * g / COLORS;
	b = 6 * b / COLORS;

	return 16 + (r * 36) + (g * 6) + b;
	*/

	/* colormap: red/green: 3bit blue: 2bit
	*/
	// get MSB ..._MASK bits
	r = (r >> (8 - RED_MASK))   & bit_mask[RED_MASK];
	g = (g >> (8 - GREEN_MASK)) & bit_mask[GREEN_MASK];
	b = (b >> (8 - BLUE_MASK))  & bit_mask[BLUE_MASK];

	return (r << RED_SHIFT) | (g << GREEN_SHIFT) | (b << BLUE_SHIFT);
}

void apply_colormap(struct pseudobuffer *pb, unsigned char *capture)
{
	int w, h;
	uint32_t pixel = 0;

	for (h = 0; h < pb->height; h++) {
		for (w = 0; w < pb->width; w++) {
			memcpy(&pixel, pb->buf + h * pb->line_length
				+ w * pb->bytes_per_pixel, pb->bytes_per_pixel);
			*(capture + h * pb->width + w) = pixel2index(pixel) & bit_mask[BITS_PER_BYTE];
		}
	}
}

void gif_init(struct gif_t *gif, int width, int height)
{
	set_colormap(gif->colormap);

	if (!(gif->data = newgif((void **) &gif->image, width, height, gif->colormap, 0)))
		exit(EXIT_FAILURE);

	animategif(gif->data, /* repetitions */ 1, /* delay */ 0,
		/* transparent background */  -1, /* disposal */ 2);
}

void gif_die(struct gif_t *gif, FILE *output)
{
	int size;

	size = endgif(gif->data);
	if (size > 0) {
		//ewrite(STDOUT_FILENO, gif->image, size);
		fwrite(gif->image, sizeof(unsigned char), size, output);
		free(gif->image);
	}
}

int get_timegap(time_t *prev)
//int get_timegap(clock_t *prev)
{
	time_t now = time(NULL);
	//clock_t now = clock();
	int gap; /* 1 unit = 1/100 sec */

	gap = (now - *prev);
	gap = (gap <= 0) ? 10: 100 * gap;
	//gap = (double) 100.0 * (now - *prev) / CLOCKS_PER_SEC;

	if (DEBUG)
		fprintf(stderr, "prev:%ld now:%ld gap:%u\n", *prev, now, gap);

	*prev = now;
	return gap;
}

int main(int argc, char *argv[])
{
	uint8_t buf[BUFSIZE];
	//char escseq[BUFSIZE];
	ssize_t size;
	int master, gap;
	fd_set fds;
	FILE *output;
	struct timeval tv;
	struct winsize old_ws;
	struct termios old_termio;
	struct pseudobuffer pb;
	struct terminal term;
	struct gif_t gif;
	unsigned char *capture;
	time_t prev;
	//clock_t prev;

	/* check args */
	output = (argc < 2) ? fopen(default_output, "w"): fopen(argv[1], "w");

	/* init */
	setlocale(LC_ALL, "");
	pb_init(&pb, TERM_WIDTH, TERM_HEIGHT);
	term_init(&term, pb.width, pb.height);
	tty_init(&old_termio, &old_ws);

	/* fork and exec shell */
	//ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
	//fork_and_exec(&master, ws.ws_row, ws.ws_col);

	/* set termio size 80x24 */
	fork_and_exec(&master, TERM_ROWS, TERM_COLS);

	/* set terminal size 80x24 (dtterm sequence) */
	//snprintf(escseq, BUFSIZE, "\033[8;%d;%dt", TERM_ROWS, TERM_COLS);
	//ewrite(STDOUT_FILENO, escseq, strlen(escseq));

	/* reset terminal */
	ewrite(STDOUT_FILENO, "\033c", 2);

	gif_init(&gif, pb.width, pb.height);
	capture = (unsigned char *) ecalloc(pb.width * pb.height, 1);
	prev = time(NULL);
	//prev = clock();

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
				ewrite(STDOUT_FILENO, buf, size);
				if (size != BUFSIZE) /* maybe more data arrives soon */
					tty.redraw_flag = true;
			}
		}
		/* output shell output to file (add gap info) */
		if (tty.redraw_flag) {
			gap = get_timegap(&prev);
			apply_colormap(&pb, capture);
			controlgif(gif.data, /* transparency color index */ -1,
				/* delay */ gap, /* userinput */ 0, /* disposal */ 2);
			putgif(gif.data, capture);
			tty.redraw_flag = false;
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
