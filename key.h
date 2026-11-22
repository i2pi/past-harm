/* The key of the moment in past: a pitch-class histogram of the three scales
 * and the sounding notes, matched against Krumhansl-Kessler key profiles,
 * with hysteresis so it only changes when another key clearly wins.
 * Pitch classes are past's: 0 = A (past's note 0 = 880 Hz). */
#ifndef KEY_H
#define KEY_H

#include "past_engine.h"

typedef struct
{
	int		pc, minor;		/* current key */
	float	score;
	int		hold;
} key_tracker;

void	key_init (key_tracker *k);
/* update from the engine state (call every few hundred samples) */
void	key_update (key_tracker *k, const past_state *s, const float *env);

#endif
