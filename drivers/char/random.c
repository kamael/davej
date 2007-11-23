/*
 * random.c -- A strong random number generator
 *
 * Version 0.92, last modified 21-Sep-95
 * 
 * Copyright Theodore Ts'o, 1994, 1995.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 * 
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * (now, with legal B.S. out of the way.....) 
 * 
 * This routine gathers environmental noise from device drivers, etc.,
 * and returns good random numbers, suitable for cryptographic use.
 * Besides the obvious cryptographic uses, these numbers are also good
 * for seeding TCP sequence numbers, and other places where it is
 * desireable to have numbers which are not only random, but hard to
 * predict by an attacker.
 *
 * Theory of operation
 * ===================
 * 
 * Computers are very predictable devices.  Hence it is extremely hard
 * to produce truely random numbers on a computer --- as opposed to
 * pseudo-random numbers, which can easily generated by using a
 * algorithm.  Unfortunately, it is very easy for attackers to guess
 * the sequence of pseudo-random number generators, and for some
 * applications this is not acceptable.  So instead, we must try to
 * gather "environmental noise" from the computer's environment, which
 * must be hard for outside attackers to observe, and use that to
 * generate random numbers.  In a Unix environment, this is best done
 * from inside the kernel.
 * 
 * Sources of randomness from the environment include inter-keyboard
 * timings, inter-interrupt timings from some interrupts, and other
 * events which are both (a) non-deterministic and (b) hard for an
 * outside observer to measure.  Randomness from these sources are
 * added to an "entropy pool", which is periodically mixed using the
 * MD5 compression function in CBC mode.  As random bytes are mixed
 * into the entropy pool, the routines keep an *estimate* of how many
 * bits of randomness have been stored into the random number
 * generator's internal state.
 * 
 * When random bytes are desired, they are obtained by taking the MD5
 * hash of a counter plus the contents of the "entropy pool".  The
 * reason for the MD5 hash is so that we can avoid exposing the
 * internal state of random number generator.  Although the MD5 hash
 * does protect the pool, as each random byte which is generated from
 * the pool reveals some information which was derived from the
 * internal state, and thus increasing the amount of information an
 * outside attacker has available to try to make some guesses about
 * the random number generator's internal state.  For this reason,
 * the routine decreases its internal estimate of how many bits of
 * "true randomness" are contained in the entropy pool as it outputs
 * random numbers.
 * 
 * If this estimate goes to zero, the routine can still generate random
 * numbers; however it may now be possible for an attacker to analyze
 * the output of the random number generator, and the MD5 algorithm,
 * and thus have some success in guessing the output of the routine.
 * Phil Karn (who devised this mechanism of using MD5 plus a counter
 * to extract random numbers from an entropy pool) calls this
 * "practical randomness", since in the worse case this is equivalent
 * to hashing MD5 with a counter and an undisclosed secret.  If MD5 is
 * a strong cryptographic hash, this should be fairly resistant to attack.
 * 
 * Exported interfaces ---- output
 * ===============================
 * 
 * There are three exported interfaces; the first is one designed to
 * be used from within the kernel:
 *
 * 	void get_random_bytes(void *buf, int nbytes);
 *
 * This interface will return the requested number of random bytes,
 * and place it in the requested buffer.
 * 
 * The two other interfaces are two character devices /dev/random and
 * /dev/urandom.  /dev/random is suitable for use when very high
 * quality randomness is desired (for example, for key generation.),
 * as it will only return a maximum of the number of bits of
 * randomness (as estimated by the random number generator) contained
 * in the entropy pool.
 * 
 * The /dev/urandom device does not have this limit, and will return
 * as many bytes as are requested.  As more and more random bytes are
 * requested without giving time for the entropy pool to recharge,
 * this will result in lower quality random numbers.  For many
 * applications, however, this is acceptable.
 *
 * Exported interfaces ---- input
 * ==============================
 *
 * The two current exported interfaces for gathering environmental
 * noise from the devices are:
 * 
 * 	void add_keyboard_randomness(unsigned char scancode);
 * 	void add_interrupt_randomness(int irq);
 * 
 * The first function uses the inter-keypress timing, as well as the
 * scancode as random inputs into the "entropy pool".
 *
 * The second function uses the inter-interrupt timing as random
 * inputs to the entropy pool.  Note that not all interrupts are good
 * sources of randomness!  For example, the timer interrupts is not a
 * good choice, because the periodicity of the interrupts is to
 * regular, and hence predictable to an attacker.  Disk interrupts are
 * a better measure, since the timing of the disk interrupts are more
 * unpredictable.  The routines try to estimate how many bits of
 * randomness a particular interrupt channel offers, by keeping track
 * of the first and second order deltas in the interrupt timings.
 *
 * Acknowledgements:
 * =================
 * 
 * Ideas for constructing this random number generator were derived
 * from the Pretty Good Privacy's random number generator, and from
 * private discussions with Phil Karn.  This design has been further
 * modified by myself, so any flaws are solely my responsibility, and
 * should not be attributed to the authors of PGP or to Phil.
 * 
 * The code for MD5 transform was taken from Colin Plumb's
 * implementation, which has been placed in the public domain.  The
 * MD5 cryptographic checksum was devised by Ronald Rivest, and is
 * documented in RFC 1321, "The MD5 Message Digest Algorithm".
 * 
 * Further background information on this topic may be obtained from
 * RFC 1750, "Randomness Recommendations for Security", by Donald
 * Eastlake, Steve Crocker, and Jeff Schiller.
 */

#ifdef linux
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/random.h>

#include <asm/segment.h>
#include <asm/irq.h>
#include <asm/io.h>
#endif

#ifdef CONFIG_RANDOM

#define RANDPOOL 512

struct random_bucket {
	int add_ptr;
	int entropy_count;
	int length;
	int bit_length;
	int delay_mix:1;
	__u8 *pool;
};

struct timer_rand_state {
	unsigned long	last_time;
	int 		last_delta;
	int 		nbits;
};

static struct random_bucket random_state;
static __u32 rand_pool_key[16];
static __u8 random_pool[RANDPOOL];
static __u32 random_counter[16];
static struct timer_rand_state keyboard_timer_state;
static struct timer_rand_state irq_timer_state[NR_IRQS];

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
	
static void flush_random(struct random_bucket *random_state)
{
	random_state->add_ptr = 0;
	random_state->bit_length = random_state->length * 8;
	random_state->entropy_count = 0;
	random_state->delay_mix = 0;
}

void rand_initialize(void)
{
	random_state.length = RANDPOOL;
	random_state.pool = random_pool;
	flush_random(&random_state);
}

/*
 * MD5 transform algorithm, taken from code written by Colin Plumb,
 * and put into the public domain
 */

/* The four core functions - F1 is optimized somewhat */

/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f, w, x, y, z, data, s) \
	( w += f(x, y, z) + data,  w = w<<s | w>>(32-s),  w += x )

/*
 * The core of the MD5 algorithm, this alters an existing MD5 hash to
 * reflect the addition of 16 longwords of new data.  MD5Update blocks
 * the data and converts bytes into longwords for this routine.
 */
static void MD5Transform(__u32 buf[4],
			 __u32 const in[16])
{
	__u32 a, b, c, d;

	a = buf[0];
	b = buf[1];
	c = buf[2];
	d = buf[3];

	MD5STEP(F1, a, b, c, d, in[ 0]+0xd76aa478,  7);
	MD5STEP(F1, d, a, b, c, in[ 1]+0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, in[ 2]+0x242070db, 17);
	MD5STEP(F1, b, c, d, a, in[ 3]+0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, in[ 4]+0xf57c0faf,  7);
	MD5STEP(F1, d, a, b, c, in[ 5]+0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, in[ 6]+0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, in[ 7]+0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, in[ 8]+0x698098d8,  7);
	MD5STEP(F1, d, a, b, c, in[ 9]+0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, in[10]+0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, in[11]+0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, in[12]+0x6b901122,  7);
	MD5STEP(F1, d, a, b, c, in[13]+0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, in[14]+0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, in[15]+0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, in[ 1]+0xf61e2562,  5);
	MD5STEP(F2, d, a, b, c, in[ 6]+0xc040b340,  9);
	MD5STEP(F2, c, d, a, b, in[11]+0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, in[ 0]+0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, in[ 5]+0xd62f105d,  5);
	MD5STEP(F2, d, a, b, c, in[10]+0x02441453,  9);
	MD5STEP(F2, c, d, a, b, in[15]+0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, in[ 4]+0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, in[ 9]+0x21e1cde6,  5);
	MD5STEP(F2, d, a, b, c, in[14]+0xc33707d6,  9);
	MD5STEP(F2, c, d, a, b, in[ 3]+0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, in[ 8]+0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, in[13]+0xa9e3e905,  5);
	MD5STEP(F2, d, a, b, c, in[ 2]+0xfcefa3f8,  9);
	MD5STEP(F2, c, d, a, b, in[ 7]+0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, in[12]+0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, in[ 5]+0xfffa3942,  4);
	MD5STEP(F3, d, a, b, c, in[ 8]+0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, in[11]+0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, in[14]+0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, in[ 1]+0xa4beea44,  4);
	MD5STEP(F3, d, a, b, c, in[ 4]+0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, in[ 7]+0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, in[10]+0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, in[13]+0x289b7ec6,  4);
	MD5STEP(F3, d, a, b, c, in[ 0]+0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, in[ 3]+0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, in[ 6]+0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, in[ 9]+0xd9d4d039,  4);
	MD5STEP(F3, d, a, b, c, in[12]+0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, in[15]+0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, in[ 2]+0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, in[ 0]+0xf4292244,  6);
	MD5STEP(F4, d, a, b, c, in[ 7]+0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, in[14]+0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, in[ 5]+0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, in[12]+0x655b59c3,  6);
	MD5STEP(F4, d, a, b, c, in[ 3]+0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, in[10]+0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, in[ 1]+0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, in[ 8]+0x6fa87e4f,  6);
	MD5STEP(F4, d, a, b, c, in[15]+0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, in[ 6]+0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, in[13]+0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, in[ 4]+0xf7537e82,  6);
	MD5STEP(F4, d, a, b, c, in[11]+0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, in[ 2]+0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, in[ 9]+0xeb86d391, 21);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;
}

#undef F1
#undef F2
#undef F3
#undef F4
#undef MD5STEP

/*
 * The function signature should be take a struct random_bucket * as
 * input, but this makes tqueue unhappy.
 */
static void mix_bucket(void *v)
{
	struct random_bucket *r = (struct random_bucket *) v;
	int	i, num_passes;
	__u32 *p;
	__u32 iv[4];

	r->delay_mix = 0;
	
	/* Start IV from last block of the random pool */
	memcpy(iv, r->pool + r->length - sizeof(iv), sizeof(iv));

	num_passes = r->length / 16;
	for (i = 0, p = (__u32 *) r->pool; i < num_passes; i++) {
		MD5Transform(iv, rand_pool_key);
		iv[0] = (*p++ ^= iv[0]);
		iv[1] = (*p++ ^= iv[1]);
		iv[2] = (*p++ ^= iv[2]);
		iv[3] = (*p++ ^= iv[3]);
	}
	memcpy(rand_pool_key, r->pool, sizeof(rand_pool_key));
	
	/* Wipe iv from memory */
	memset(iv, 0, sizeof(iv));

	r->add_ptr = 0;
}

/*
 * This function adds a byte into the entropy "pool".  It does not
 * update the entropy estimate.  The caller must do this if appropriate.
 */
static inline void add_entropy_byte(struct random_bucket *r,
				    const __u8 ch,
				    int delay)
{
	if (!delay && r->delay_mix)
		mix_bucket(r);
	r->pool[r->add_ptr++] ^= ch;
	if (r->add_ptr >= r->length) {
		if (delay) {
			r->delay_mix = 1;
			r->add_ptr = 0;
		} else
			mix_bucket(r);
	}
}

/*
 * This function adds some number of bytes into the entropy pool and
 * updates the entropy count as appropriate.
 */
void add_entropy(struct random_bucket *r, const __u8 *ptr,
		 int length, int entropy_level, int delay)
{
	while (length-- > 0)
		add_entropy_byte(r, *ptr++, delay);
		
	r->entropy_count += entropy_level;
	if (r->entropy_count > r->length*8)
		r->entropy_count = r->length * 8;
}

/*
 * This function adds entropy to the entropy "pool" by using timing
 * delays.  It uses the timer_rand_state structure to make an estimate
 * of how many bits of entropy this call has added to the pool.
 */
static void add_timer_randomness(struct random_bucket *r,
				 struct timer_rand_state *state, int delay)
{
	int	delta, delta2;
	int	nbits;

	/*
	 * Calculate number of bits of randomness we probably
	 * added.  We take into account the first and second order
	 * delta's in order to make our estimate.
	 */
	delta = jiffies - state->last_time;
	delta2 = delta - state->last_delta;
	state->last_time = jiffies;
	state->last_delta = delta;
	if (delta < 0) delta = -delta;
	if (delta2 < 0) delta2 = -delta2;
	delta = MIN(delta, delta2) >> 1;
	for (nbits = 0; delta; nbits++)
		delta >>= 1;
	
	add_entropy(r, (__u8 *) &jiffies, sizeof(jiffies),
		    nbits, delay);

#if defined (__i386__)
	/*
	 * On a Pentium, read the cycle counter. We assume that
	 * this gives us 8 bits of randomness.  XXX This needs
	 * investigation.
	 */
	if (x86_capability & 16) {
		unsigned long low, high;
		__asm__(".byte 0x0f,0x31"
			:"=a" (low), "=d" (high));
		add_entropy_byte(r, low, 1);
		r->entropy_count += 8;
		if (r->entropy_count > r->bit_length)
			r->entropy_count = r->bit_length;
	}
#endif
}

void add_keyboard_randomness(unsigned char scancode)
{
	struct random_bucket *r = &random_state;

	add_timer_randomness(r, &keyboard_timer_state, 0);
	add_entropy_byte(r, scancode, 0);
	r->entropy_count += 6;
	if (r->entropy_count > r->bit_length)
		r->entropy_count = r->bit_length;
}

void add_interrupt_randomness(int irq)
{
	struct random_bucket *r = &random_state;

	if (irq >= NR_IRQS)
		return;

	add_timer_randomness(r, &irq_timer_state[irq], 1);
}

/*
 * This function extracts randomness from the "entropy pool", and
 * returns it in a buffer.  This function computes how many remaining
 * bits of entropy are left in the pool, but it does not restrict the
 * number of bytes that are actually obtained.
 */
static inline int extract_entropy(struct random_bucket *r, char * buf,
				  int nbytes, int to_user)
{
	int length, ret, passes, i;
	__u32 tmp[4];
	u8 *cp;
	
	add_entropy(r, (u8 *) &jiffies, sizeof(jiffies), 0, 0);
	
	if (r->entropy_count > r->bit_length) 
		r->entropy_count = r->bit_length;
	if (nbytes > 32768)
		nbytes = 32768;
	ret = nbytes;
	r->entropy_count -= ret * 8;
	if (r->entropy_count < 0)
		r->entropy_count = 0;
	passes = r->length / 64;
	while (nbytes) {
		length = MIN(nbytes, 16);
		for (i=0; i < 16; i++) {
			if (++random_counter[i] != 0)
				break;
		}
		tmp[0] = 0x67452301;
		tmp[1] = 0xefcdab89;
		tmp[2] = 0x98badcfe;
		tmp[3] = 0x10325476;
		MD5Transform(tmp, random_counter);
		for (i = 0, cp = r->pool; i < passes; i++, cp+=64)
			MD5Transform(tmp, (__u32 *) cp);
		if (to_user)
			memcpy_tofs(buf, tmp, length);
		else
			memcpy(buf, tmp, length);
		nbytes -= length;
		buf += length;
	}
	return ret;
}

/*
 * This function is the exported kernel interface.  It returns some
 * number of good random numbers, suitable for seeding TCP sequence
 * numbers, etc.
 */
void get_random_bytes(void *buf, int nbytes)
{
	extract_entropy(&random_state, (char *) buf, nbytes, 0);
}

#ifdef linux
int read_random(struct inode * inode,struct file * file,char * buf,int nbytes)
{
	if ((nbytes * 8) > random_state.entropy_count)
		nbytes = random_state.entropy_count / 8;
	
	return extract_entropy(&random_state, buf, nbytes, 1);
}

int read_random_unlimited(struct inode * inode,struct file * file,
			  char * buf,int nbytes)
{
	return extract_entropy(&random_state, buf, nbytes, 1);
}
#endif

#endif /* CONFIG_RANDOM */