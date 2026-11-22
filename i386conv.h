/* port: helpers reproducing how the original i386/x87 builds converted
 * floating point to integers (arm64 saturates instead; C leaves overflow
 * undefined). Included from the Makefile with -include. */
#ifndef I386CONV_H
#define I386CONV_H
#include <limits.h>
#include <stdint.h>
#include <sys/types.h>	/* uint, u_char... */

/* x87 fistpl (32-bit, truncating): out of range or NaN -> INT_MIN */
static inline int i386_fistl (double d)
{
	if (!(d > -2147483649.0 && d < 2147483648.0)) return INT_MIN;
	return (int) d;
}

/* x87 fistps (16-bit, truncating), used by gcc 3.2 for float->short:
 * out of range or NaN -> -32768 */
static inline short i386_fists (double d)
{
	if (!(d > -32769.0 && d < 32768.0)) return -32768;
	return (short) (int) d;
}

/* gcc on i386 converts to unsigned 32-bit via a 64-bit fistpll and keeps the
 * low word: negative values wrap modulo 2^32 */
static inline unsigned int i386_fistu (double d)
{
	if (!(d > -9223372036854775809.0 && d < 9223372036854775808.0)) return 0;
	return (unsigned int) (long long) d;
}
/* `ulong` (Linux <sys/types.h>) was 32 bits on i386; macOS lacks it */
typedef unsigned int ulong;
#endif
