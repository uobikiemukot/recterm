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

enum {
	TERM_WIDTH      = 640,
	TERM_HEIGHT     = 384,
	GIF_DELAY       = 10,
};

enum cmap_bitfield {
	RED_SHIFT   = 5,
	GREEN_SHIFT = 2,
	BLUE_SHIFT  = 0,
	RED_MASK    = 3,
	GREEN_MASK  = 3,
	BLUE_MASK   = 2
};

struct gif_t {
	void *data;
	unsigned char *image;
	int colormap[COLORS * BYTES_PER_PIXEL + 1];
};

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
    }                       // 6x6x6 color cube

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

	animategif(gif->data, /* repetitions */ 1, /* delay */ GIF_DELAY,
		/* transparent background */  -1, /* disposal */ 2);
}

void gif_die(struct gif_t *gif)
{
	int size;

	size = endgif(gif->data);
	if (size > 0) {
		ewrite(STDOUT_FILENO, gif->image, size);
		free(gif->image);
	}
}

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
	term_init(&term, pb.width, pb.height);

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
	gif_die(&gif);
	free(capture);

	term_die(&term);
	pb_die(&pb);

	eclose(input);

	return EXIT_SUCCESS;
}
