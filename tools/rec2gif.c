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
#include "../mk_wcwidth.h"

enum {
	TERM_WIDTH      = 640,
	TERM_HEIGHT     = 384,
};

int main(int argc, char *argv[])
{
	uint8_t buf[BUFSIZE];
	ssize_t size;
	int input;
	struct pseudobuffer pb;
	struct terminal term;
	struct gif_t gif;
	unsigned char *capture;

	/* check args */
	input = (argc < 2) ? STDIN_FILENO: eopen(argv[1], O_RDONLY);

	/* init */
	setlocale(LC_ALL, "");
	pb_init(&pb, TERM_WIDTH, TERM_HEIGHT);
	/* TODO: add ambiguous width option */
	my_wcwidth = mk_wcwidth_cjk;
	term_init(&term, pb.width, pb.height, true);

	gif_init(&gif, pb.width, pb.height);
	capture = (unsigned char *) ecalloc(pb.width * pb.height, 1);

	/* main loop */
	while ((size = read(input, buf, BUFSIZE)) > 0) {
		parse(&term, buf, size);
		refresh(&pb, &term);

		/* take screenshot */
		apply_colormap(&pb, capture);
		putgif(gif.data, capture);
	}

	/* normal exit */
	gif_die(&gif, stdout);
	free(capture);

	term_die(&term);
	pb_die(&pb);

	eclose(input);

	return EXIT_SUCCESS;
}
