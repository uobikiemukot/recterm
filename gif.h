/* See LICENSE for licence details. */
enum gif_opetion {
	GIF_REPEAT      = 1,
	GIF_DELAY       = 0,
	GIF_TRANSPARENT = -1,
	GIF_DISPOSAL    = 2,
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

	animategif(gif->data, /* repetitions */ GIF_REPEAT, /* delay */ GIF_DELAY,
		/* transparent background */ GIF_TRANSPARENT, /* disposal */ GIF_DISPOSAL);
}

void gif_die(struct gif_t *gif, FILE *output)
{
	int size = endgif(gif->data);

	if (size > 0) {
		//ewrite(STDOUT_FILENO, gif->image, size);
		if (output)
			fwrite(gif->image, sizeof(unsigned char), size, output);
		free(gif->image);
	}
}
