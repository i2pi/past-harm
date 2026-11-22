/* the title, bottom left, eaten by the picture (see title.c) */
#ifndef TITLE_H
#define TITLE_H

/* set the text for a W x H frame; dir holds fonts/ */
void	title_build (int W, int H, const char *dir);
int		title_built_for (int W, int H);
/* the frame's plain background behind the text (before the cubes etc.) */
void	title_save_background (const unsigned char *img, int W);
/* eat and stamp the text into the frame (before the feedback) */
void	title_draw (unsigned char *img, int W, float dt, double now);
/* the GPU pipeline: the text's box in the frame and its coverage mask */
int		title_info (int *x0, int *y0, int *w, int *h, const unsigned char **coverage);

/* the last layout's text width, credit ascent and descent, cap height (over H) */
void	title_metrics (double m[4]);

#endif
