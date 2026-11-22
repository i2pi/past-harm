/* web: macOS's random()/srandom() (FreeBSD's additive feedback generator,
   TYPE_3: x^31 + x^3 + 1, seeded by Park-Miller), so the cube layout drawn
   with srandom (0) is the same as the macOS build's (musl's differs). */
#include <stdint.h>

#define DEG 31
#define SEP 3
static uint32_t state[DEG];
static uint32_t *fptr = &state[SEP], *rptr = &state[0];
static int seeded;

static uint32_t good_rand (int32_t x)
{
	int32_t hi, lo;
	if (x == 0) x = 123459876;
	hi = x / 127773;
	lo = x % 127773;
	x = 16807 * lo - 2836 * hi;
	if (x < 0) x += 0x7fffffff;
	return (uint32_t) x;
}

long bsd_random (void);

void bsd_srandom (unsigned int x)
{
	int i;
	state[0] = x;
	for (i = 1; i < DEG; i++) state[i] = good_rand ((int32_t) state[i - 1]);
	fptr = &state[SEP]; rptr = &state[0];
	seeded = 1;
	for (i = 0; i < 10 * DEG; i++) (void) bsd_random ();
}

long bsd_random (void)
{
	uint32_t i;
	if (!seeded) bsd_srandom (1);
	*fptr += *rptr;
	i = (*fptr >> 1) & 0x7fffffff;
	if (++fptr >= &state[DEG]) { fptr = state; ++rptr; }
	else if (++rptr >= &state[DEG]) rptr = state;
	return (long) i;
}
