/*
 * sound/dmabuf.c
 *
 * The DMA buffer manager for digitized voice applications
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Thomas Sailer   : moved several static variables into struct audio_operations
 *                   (which is grossly misnamed btw.) because they have the same
 *                   lifetime as the rest in there and dynamic allocation saves
 *                   12k or so
 * Thomas Sailer   : remove {in,out}_sleep_flag. It was used for the sleeper to
 *                   determine if it was woken up by the expiring timeout or by
 *                   an explicit wake_up. The return value from schedule_timeout
 *		     can be used instead; if 0, the wakeup was due to the timeout.
 *
 * Rob Riggs		Added persistent DMA buffers (1998/10/17)
 */
 
#include <linux/config.h>

#define BE_CONSERVATIVE
#define SAMPLE_ROUNDUP 0

#include "sound_config.h"

#if defined(CONFIG_AUDIO) || defined(CONFIG_GUS)

#define DMAP_FREE_ON_CLOSE      0
#define DMAP_KEEP_ON_CLOSE      1
extern int sound_dmap_flag;

static void dma_reset_output(int dev);
static void dma_reset_input(int dev);
static int local_start_dma(struct audio_operations *adev, unsigned long physaddr, int count, int dma_mode);



static int debugmem = 0;	/* switched off by default */
static int dma_buffsize = DSP_BUFFSIZE;

static long dmabuf_timeout(struct dma_buffparms *dmap)
{
	long tmout;

	tmout = (dmap->fragment_size * HZ) / dmap->data_rate;
	tmout += HZ / 5;	/* Some safety distance */
	if (tmout < (HZ / 2))
		tmout = HZ / 2;
	if (tmout > 20 * HZ)
		tmout = 20 * HZ;
	return tmout;
}

static int sound_alloc_dmap(struct dma_buffparms *dmap)
{
	char *start_addr, *end_addr;
	int i, dma_pagesize;
	int sz, size;

	dmap->mapping_flags &= ~DMA_MAP_MAPPED;

	if (dmap->raw_buf != NULL)
		return 0;	/* Already done */
	if (dma_buffsize < 4096)
		dma_buffsize = 4096;
	dma_pagesize = (dmap->dma < 4) ? (64 * 1024) : (128 * 1024);
	dmap->raw_buf = NULL;
	dmap->buffsize = dma_buffsize;
	if (dmap->buffsize > dma_pagesize)
		dmap->buffsize = dma_pagesize;
	start_addr = NULL;
	/*
	 * Now loop until we get a free buffer. Try to get smaller buffer if
	 * it fails. Don't accept smaller than 8k buffer for performance
	 * reasons.
	 */
	while (start_addr == NULL && dmap->buffsize > PAGE_SIZE) {
		for (sz = 0, size = PAGE_SIZE; size < dmap->buffsize; sz++, size <<= 1);
		dmap->buffsize = PAGE_SIZE * (1 << sz);
		start_addr = (char *) __get_free_pages(GFP_ATOMIC|GFP_DMA, sz);
		if (start_addr == NULL)
			dmap->buffsize /= 2;
	}

	if (start_addr == NULL) {
		printk(KERN_WARNING "Sound error: Couldn't allocate DMA buffer\n");
		return -ENOMEM;
	} else {
		/* make some checks */
		end_addr = start_addr + dmap->buffsize - 1;

		if (debugmem)
			printk(KERN_DEBUG "sound: start 0x%lx, end 0x%lx\n", (long) start_addr, (long) end_addr);
		
		/* now check if it fits into the same dma-pagesize */

		if (((long) start_addr & ~(dma_pagesize - 1)) != ((long) end_addr & ~(dma_pagesize - 1))
		    || end_addr >= (char *) (MAX_DMA_ADDRESS)) {
			printk(KERN_ERR "sound: Got invalid address 0x%lx for %db DMA-buffer\n", (long) start_addr, dmap->buffsize);
			return -EFAULT;
		}
	}
	dmap->raw_buf = start_addr;
	dmap->raw_buf_phys = virt_to_bus(start_addr);

	for (i = MAP_NR(start_addr); i <= MAP_NR(end_addr); i++)
		set_bit(PG_reserved, &mem_map[i].flags);;
	return 0;
}

static void sound_free_dmap(struct dma_buffparms *dmap)
{
	int sz, size, i;
	unsigned long start_addr, end_addr;

	if (dmap->raw_buf == NULL)
		return;
	if (dmap->mapping_flags & DMA_MAP_MAPPED)
		return;		/* Don't free mmapped buffer. Will use it next time */
	for (sz = 0, size = PAGE_SIZE; size < dmap->buffsize; sz++, size <<= 1);

	start_addr = (unsigned long) dmap->raw_buf;
	end_addr = start_addr + dmap->buffsize;

	for (i = MAP_NR(start_addr); i <= MAP_NR(end_addr); i++)
		clear_bit(PG_reserved, &mem_map[i].flags);;

	free_pages((unsigned long) dmap->raw_buf, sz);
	dmap->raw_buf = NULL;
}


/* Intel version !!!!!!!!! */

static int sound_start_dma(struct dma_buffparms *dmap, unsigned long physaddr, int count, int dma_mode)
{
	unsigned long flags;
	int chan = dmap->dma;

	/* printk( "Start DMA%d %d, %d\n",  chan,  (int)(physaddr-dmap->raw_buf_phys),  count); */

	flags = claim_dma_lock();
	disable_dma(chan);
	clear_dma_ff(chan);
	set_dma_mode(chan, dma_mode);
	set_dma_addr(chan, physaddr);
	set_dma_count(chan, count);
	enable_dma(chan);
	release_dma_lock(flags);

	return 0;
}

static void dma_init_buffers(struct dma_buffparms *dmap)
{
	dmap->qlen = dmap->qhead = dmap->qtail = dmap->user_counter = 0;
	dmap->byte_counter = 0;
	dmap->max_byte_counter = 8000 * 60 * 60;
	dmap->bytes_in_use = dmap->buffsize;

	dmap->dma_mode = DMODE_NONE;
	dmap->mapping_flags = 0;
	dmap->neutral_byte = 0x80;
	dmap->data_rate = 8000;
	dmap->cfrag = -1;
	dmap->closing = 0;
	dmap->nbufs = 1;
	dmap->flags = DMA_BUSY;	/* Other flags off */
}

static int open_dmap(struct audio_operations *adev, int mode, struct dma_buffparms *dmap)
{
	int err;
	
	if (dmap->flags & DMA_BUSY)
		return -EBUSY;
	if ((err = sound_alloc_dmap(dmap)) < 0)
		return err;

	if (dmap->raw_buf == NULL) {
		printk(KERN_WARNING "Sound: DMA buffers not available\n");
		return -ENOSPC;	/* Memory allocation failed during boot */
	}
	if (dmap->dma >= 0 && sound_open_dma(dmap->dma, adev->name)) {
		printk(KERN_WARNING "Unable to grab(2) DMA%d for the audio driver\n", dmap->dma);
		return -EBUSY;
	}
	dma_init_buffers(dmap);
	dmap->open_mode = mode;
	dmap->subdivision = dmap->underrun_count = 0;
	dmap->fragment_size = 0;
	dmap->max_fragments = 65536;	/* Just a large value */
	dmap->byte_counter = 0;
	dmap->max_byte_counter = 8000 * 60 * 60;
	dmap->applic_profile = APF_NORMAL;
	dmap->needs_reorg = 1;
	dmap->audio_callback = NULL;
	dmap->callback_parm = 0;
	return 0;
}

static void close_dmap(struct audio_operations *adev, struct dma_buffparms *dmap)
{
	unsigned long flags;
	
	if (dmap->dma >= 0) {
		sound_close_dma(dmap->dma);
		flags=claim_dma_lock();
		disable_dma(dmap->dma);
		release_dma_lock(flags);
	}
	if (dmap->flags & DMA_BUSY)
		dmap->dma_mode = DMODE_NONE;
	dmap->flags &= ~DMA_BUSY;
	
	if (sound_dmap_flag == DMAP_FREE_ON_CLOSE)
		sound_free_dmap(dmap);
}


static unsigned int default_set_bits(int dev, unsigned int bits)
{
	mm_segment_t fs = get_fs();

	set_fs(get_ds());
	audio_devs[dev]->d->ioctl(dev, SNDCTL_DSP_SETFMT, (caddr_t)&bits);
	set_fs(fs);
	return bits;
}

static int default_set_speed(int dev, int speed)
{
	mm_segment_t fs = get_fs();

	set_fs(get_ds());
	audio_devs[dev]->d->ioctl(dev, SNDCTL_DSP_SPEED, (caddr_t)&speed);
	set_fs(fs);
	return speed;
}

static short default_set_channels(int dev, short channels)
{
	int c = channels;
	mm_segment_t fs = get_fs();

	set_fs(get_ds());
	audio_devs[dev]->d->ioctl(dev, SNDCTL_DSP_CHANNELS, (caddr_t)&c);
	set_fs(fs);
	return c;
}

static void check_driver(struct audio_driver *d)
{
	if (d->set_speed == NULL)
		d->set_speed = default_set_speed;
	if (d->set_bits == NULL)
		d->set_bits = default_set_bits;
	if (d->set_channels == NULL)
		d->set_channels = default_set_channels;
}

int DMAbuf_open(int dev, int mode)
{
	struct audio_operations *adev = audio_devs[dev];
	int retval;
	struct dma_buffparms *dmap_in = NULL;
	struct dma_buffparms *dmap_out = NULL;

	if (!adev)
		  return -ENXIO;
	if (!(adev->flags & DMA_DUPLEX))
		adev->dmap_in = adev->dmap_out;
	check_driver(adev->d);

	if ((retval = adev->d->open(dev, mode)) < 0)
		return retval;
	dmap_out = adev->dmap_out;
	dmap_in = adev->dmap_in;
	if (dmap_in == dmap_out)
		adev->flags &= ~DMA_DUPLEX;

	if (mode & OPEN_WRITE) {
		if ((retval = open_dmap(adev, mode, dmap_out)) < 0) {
			adev->d->close(dev);
			return retval;
		}
	}
	adev->enable_bits = mode;

	if (mode == OPEN_READ || (mode != OPEN_WRITE && (adev->flags & DMA_DUPLEX))) {
		if ((retval = open_dmap(adev, mode, dmap_in)) < 0) {
			adev->d->close(dev);
			if (mode & OPEN_WRITE)
				close_dmap(adev, dmap_out);
			return retval;
		}
	}
	adev->open_mode = mode;
	adev->go = 1;

	adev->d->set_bits(dev, 8);
	adev->d->set_channels(dev, 1);
	adev->d->set_speed(dev, DSP_DEFAULT_SPEED);
	if (adev->dmap_out->dma_mode == DMODE_OUTPUT) 
		memset(adev->dmap_out->raw_buf, adev->dmap_out->neutral_byte,
		       adev->dmap_out->bytes_in_use);
	return 0;
}

void DMAbuf_reset(int dev)
{
	if (audio_devs[dev]->open_mode & OPEN_WRITE)
		dma_reset_output(dev);

	if (audio_devs[dev]->open_mode & OPEN_READ)
		dma_reset_input(dev);
}

static void dma_reset_output(int dev)
{
	struct audio_operations *adev = audio_devs[dev];
	unsigned long flags,f ;
	struct dma_buffparms *dmap = adev->dmap_out;

	if (!(dmap->flags & DMA_STARTED))	/* DMA is not active */
		return;

	/*
	 *	First wait until the current fragment has been played completely
	 */
	save_flags(flags);
	cli();
	adev->dmap_out->flags |= DMA_SYNCING;

	adev->dmap_out->underrun_count = 0;
	if (!signal_pending(current) && adev->dmap_out->qlen && 
	    adev->dmap_out->underrun_count == 0)
		interruptible_sleep_on_timeout(&adev->out_sleeper,
					       dmabuf_timeout(dmap));
	adev->dmap_out->flags &= ~(DMA_SYNCING | DMA_ACTIVE);

	/*
	 *	Finally shut the device off
	 */
	if (!(adev->flags & DMA_DUPLEX) || !adev->d->halt_output)
		adev->d->halt_io(dev);
	else
		adev->d->halt_output(dev);
	adev->dmap_out->flags &= ~DMA_STARTED;
	
	f=claim_dma_lock();
	clear_dma_ff(dmap->dma);
	disable_dma(dmap->dma);
	release_dma_lock(f);
	
	restore_flags(flags);
	dmap->byte_counter = 0;
	reorganize_buffers(dev, adev->dmap_out, 0);
	dmap->qlen = dmap->qhead = dmap->qtail = dmap->user_counter = 0;
}

static void dma_reset_input(int dev)
{
        struct audio_operations *adev = audio_devs[dev];
	unsigned long flags;
	struct dma_buffparms *dmap = adev->dmap_in;

	save_flags(flags);
	cli();
	if (!(adev->flags & DMA_DUPLEX) || !adev->d->halt_input)
		adev->d->halt_io(dev);
	else
		adev->d->halt_input(dev);
	adev->dmap_in->flags &= ~DMA_STARTED;
	restore_flags(flags);

	dmap->qlen = dmap->qhead = dmap->qtail = dmap->user_counter = 0;
	dmap->byte_counter = 0;
	reorganize_buffers(dev, adev->dmap_in, 1);
}

void DMAbuf_launch_output(int dev, struct dma_buffparms *dmap)
{
	struct audio_operations *adev = audio_devs[dev];

	if (!((adev->enable_bits * adev->go) & PCM_ENABLE_OUTPUT))
		return;		/* Don't start DMA yet */
	dmap->dma_mode = DMODE_OUTPUT;

	if (!(dmap->flags & DMA_ACTIVE) || !(adev->flags & DMA_AUTOMODE) || (dmap->flags & DMA_NODMA)) {
		if (!(dmap->flags & DMA_STARTED)) {
			reorganize_buffers(dev, dmap, 0);
			if (adev->d->prepare_for_output(dev, dmap->fragment_size, dmap->nbufs))
				return;
			if (!(dmap->flags & DMA_NODMA))
				local_start_dma(adev, dmap->raw_buf_phys, dmap->bytes_in_use,DMA_MODE_WRITE);
			dmap->flags |= DMA_STARTED;
		}
		if (dmap->counts[dmap->qhead] == 0)
			dmap->counts[dmap->qhead] = dmap->fragment_size;
		dmap->dma_mode = DMODE_OUTPUT;
		adev->d->output_block(dev, dmap->raw_buf_phys + dmap->qhead * dmap->fragment_size,
				      dmap->counts[dmap->qhead], 1);
		if (adev->d->trigger)
			adev->d->trigger(dev,adev->enable_bits * adev->go);
	}
	dmap->flags |= DMA_ACTIVE;
}

int DMAbuf_sync(int dev)
{
	struct audio_operations *adev = audio_devs[dev];
	unsigned long flags;
	int n = 0;
	struct dma_buffparms *dmap;

	if (!adev->go && !(adev->enable_bits & PCM_ENABLE_OUTPUT))
		return 0;

	if (adev->dmap_out->dma_mode == DMODE_OUTPUT) {
		dmap = adev->dmap_out;
		save_flags(flags);
		cli();
		if (dmap->qlen > 0 && !(dmap->flags & DMA_ACTIVE))
			DMAbuf_launch_output(dev, dmap);
		adev->dmap_out->flags |= DMA_SYNCING;
		adev->dmap_out->underrun_count = 0;
		while (!signal_pending(current) && n++ <= adev->dmap_out->nbufs && 
		       adev->dmap_out->qlen && adev->dmap_out->underrun_count == 0) {
			long t = dmabuf_timeout(dmap);
			t = interruptible_sleep_on_timeout(&adev->out_sleeper,
							   t);
			if (!t) {
				adev->dmap_out->flags &= ~DMA_SYNCING;
				restore_flags(flags);
				return adev->dmap_out->qlen;
			}
		}
		adev->dmap_out->flags &= ~(DMA_SYNCING | DMA_ACTIVE);
		restore_flags(flags);
		
		/*
		 * Some devices such as GUS have huge amount of on board RAM for the
		 * audio data. We have to wait until the device has finished playing.
		 */

		save_flags(flags);
		cli();
		if (adev->d->local_qlen) {   /* Device has hidden buffers */
			while (!signal_pending(current) &&
			       adev->d->local_qlen(dev))
				interruptible_sleep_on_timeout(&adev->out_sleeper,
							       dmabuf_timeout(dmap));
		}
		restore_flags(flags);
	}
	adev->dmap_out->dma_mode = DMODE_NONE;
	return adev->dmap_out->qlen;
}

int DMAbuf_release(int dev, int mode)
{
	struct audio_operations *adev = audio_devs[dev];
	unsigned long flags;

	if (adev->open_mode & OPEN_WRITE)
		adev->dmap_out->closing = 1;
	if (adev->open_mode & OPEN_READ)
		adev->dmap_in->closing = 1;

	if (adev->open_mode & OPEN_WRITE)
		if (!(adev->dmap_out->mapping_flags & DMA_MAP_MAPPED))
			if (!signal_pending(current) && (adev->dmap_out->dma_mode == DMODE_OUTPUT))
				DMAbuf_sync(dev);
	if (adev->dmap_out->dma_mode == DMODE_OUTPUT)
		memset(adev->dmap_out->raw_buf, adev->dmap_out->neutral_byte, adev->dmap_out->bytes_in_use);
	save_flags(flags);
	cli();

	DMAbuf_reset(dev);
	adev->d->close(dev);

	if (adev->open_mode & OPEN_WRITE)
		close_dmap(adev, adev->dmap_out);

	if (adev->open_mode == OPEN_READ ||
	    (adev->open_mode != OPEN_WRITE &&
	     (adev->flags & DMA_DUPLEX)))
		close_dmap(adev, adev->dmap_in);
	adev->open_mode = 0;
	restore_flags(flags);
	return 0;
}

int DMAbuf_activate_recording(int dev, struct dma_buffparms *dmap)
{
	struct audio_operations *adev = audio_devs[dev];
	int  err;

	if (!(adev->open_mode & OPEN_READ))
		return 0;
	if (!(adev->enable_bits & PCM_ENABLE_INPUT))
		return 0;
	if (dmap->dma_mode == DMODE_OUTPUT) {	/* Direction change */
		DMAbuf_sync(dev);
		DMAbuf_reset(dev);
		dmap->dma_mode = DMODE_NONE;
	}
	if (!dmap->dma_mode) {
		reorganize_buffers(dev, dmap, 1);
		if ((err = adev->d->prepare_for_input(dev,
				dmap->fragment_size, dmap->nbufs)) < 0)
			return err;
		dmap->dma_mode = DMODE_INPUT;
	}
	if (!(dmap->flags & DMA_ACTIVE)) {
		if (dmap->needs_reorg)
			reorganize_buffers(dev, dmap, 0);
		local_start_dma(adev, dmap->raw_buf_phys, dmap->bytes_in_use, DMA_MODE_READ);
		adev->d->start_input(dev, dmap->raw_buf_phys + dmap->qtail * dmap->fragment_size,
				     dmap->fragment_size, 0);
		dmap->flags |= DMA_ACTIVE;
		if (adev->d->trigger)
			adev->d->trigger(dev, adev->enable_bits * adev->go);
	}
	return 0;
}

int DMAbuf_getrdbuffer(int dev, char **buf, int *len, int dontblock)
{
	struct audio_operations *adev = audio_devs[dev];
	unsigned long flags;
	int err = 0, n = 0;
	struct dma_buffparms *dmap = adev->dmap_in;
	int go;

	if (!(adev->open_mode & OPEN_READ))
		return -EIO;
	if (dmap->needs_reorg)
		reorganize_buffers(dev, dmap, 0);
	save_flags(flags);
	cli();
	if (adev->dmap_in->mapping_flags & DMA_MAP_MAPPED) {
/*		  printk(KERN_WARNING "Sound: Can't read from mmapped device (1)\n");*/
		  restore_flags(flags);
		  return -EINVAL;
	} else while (dmap->qlen <= 0 && n++ < 10) {
		long timeout = MAX_SCHEDULE_TIMEOUT;
		if (!(adev->enable_bits & PCM_ENABLE_INPUT) || !adev->go) {
			restore_flags(flags);
			return -EAGAIN;
		}
		if ((err = DMAbuf_activate_recording(dev, dmap)) < 0) {
			restore_flags(flags);
			return err;
		}
		/* Wait for the next block */

		if (dontblock) {
			restore_flags(flags);
			return -EAGAIN;
		}
		if ((go = adev->go))
			timeout = dmabuf_timeout(dmap);
		timeout = interruptible_sleep_on_timeout(&adev->in_sleeper,
							 timeout);
		if (!timeout) {
			/* FIXME: include device name */
			err = -EIO;
			printk(KERN_WARNING "Sound: DMA (input) timed out - IRQ/DRQ config error?\n");
			dma_reset_input(dev);
		} else
			err = -EINTR;
	}
	restore_flags(flags);

	if (dmap->qlen <= 0)
		return err ? err : -EINTR;
	*buf = &dmap->raw_buf[dmap->qhead * dmap->fragment_size + dmap->counts[dmap->qhead]];
	*len = dmap->fragment_size - dmap->counts[dmap->qhead];

	return dmap->qhead;
}

int DMAbuf_rmchars(int dev, int buff_no, int c)
{
	struct audio_operations *adev = audio_devs[dev];
	struct dma_buffparms *dmap = adev->dmap_in;
	int p = dmap->counts[dmap->qhead] + c;

	if (dmap->mapping_flags & DMA_MAP_MAPPED)
	{
/*		  printk("Sound: Can't read from mmapped device (2)\n");*/
		return -EINVAL;
	}
	else if (dmap->qlen <= 0)
		return -EIO;
	else if (p >= dmap->fragment_size) {  /* This buffer is completely empty */
		dmap->counts[dmap->qhead] = 0;
		dmap->qlen--;
		dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
	}
	else dmap->counts[dmap->qhead] = p;

	return 0;
}

int DMAbuf_get_buffer_pointer(int dev, struct dma_buffparms *dmap, int direction)
{
	/*
	 *	Try to approximate the active byte position of the DMA pointer within the
	 *	buffer area as well as possible.
	 */

	int pos;
	unsigned long flags;
	unsigned long f;

	save_flags(flags);
	cli();
	if (!(dmap->flags & DMA_ACTIVE))
		pos = 0;
	else {
		int chan = dmap->dma;
		
		f=claim_dma_lock();
		clear_dma_ff(chan);
		
		if(!isa_dma_bridge_buggy)
			disable_dma(dmap->dma);
		
		pos = get_dma_residue(chan);
		
		pos = dmap->bytes_in_use - pos;

		if (!(dmap->mapping_flags & DMA_MAP_MAPPED)) {
			if (direction == DMODE_OUTPUT) {
				if (dmap->qhead == 0)
					if (pos > dmap->fragment_size)
						pos = 0;
			} else {
				if (dmap->qtail == 0)
					if (pos > dmap->fragment_size)
						pos = 0;
			}
		}
		if (pos < 0)
			pos = 0;
		if (pos >= dmap->bytes_in_use)
			pos = 0;
		
		if(!isa_dma_bridge_buggy)
			enable_dma(dmap->dma);
			
		release_dma_lock(f);
	}
	restore_flags(flags);
	/* printk( "%04x ",  pos); */

	return pos;
}

/*
 *	DMAbuf_start_devices() is called by the /dev/music driver to start
 *	one or more audio devices at desired moment.
 */

void DMAbuf_start_devices(unsigned int devmask)
{
	struct audio_operations *adev;
	int dev;

	for (dev = 0; dev < num_audiodevs; dev++) {
		if (!(devmask & (1 << dev)))
			continue;
		if (!(adev = audio_devs[dev]))
			continue;
		if (adev->open_mode == 0)
			continue;
		if (adev->go)
			continue;
		/* OK to start the device */
		adev->go = 1;
		if (adev->d->trigger)
			adev->d->trigger(dev,adev->enable_bits * adev->go);
	}
}

int DMAbuf_space_in_queue(int dev)
{
	struct audio_operations *adev = audio_devs[dev];
	int len, max, tmp;
	struct dma_buffparms *dmap = adev->dmap_out;
	int lim = dmap->nbufs;

	if (lim < 2)
		lim = 2;

	if (dmap->qlen >= lim)	/* No space at all */
		return 0;

	/*
	 *	Verify that there are no more pending buffers than the limit
	 *	defined by the process.
	 */

	max = dmap->max_fragments;
	if (max > lim)
		max = lim;
	len = dmap->qlen;

	if (adev->d->local_qlen) {
		tmp = adev->d->local_qlen(dev);
		if (tmp && len)
			tmp--;	/* This buffer has been counted twice */
		len += tmp;
	}
	if (dmap->byte_counter % dmap->fragment_size)	/* There is a partial fragment */
		len = len + 1;

	if (len >= max)
		return 0;
	return max - len;
}

static int output_sleep(int dev, int dontblock)
{
	struct audio_operations *adev = audio_devs[dev];
	int err = 0;
	struct dma_buffparms *dmap = adev->dmap_out;
	long timeout;
	long timeout_value;

	if (dontblock)
		return -EAGAIN;
	if (!(adev->enable_bits & PCM_ENABLE_OUTPUT))
		return -EAGAIN;

	/*
	 * Wait for free space
	 */
	if (signal_pending(current))
		return -EINTR;
	timeout = (adev->go && !(dmap->flags & DMA_NOTIMEOUT));
	if (timeout) 
		timeout_value = dmabuf_timeout(dmap);
	else
		timeout_value = MAX_SCHEDULE_TIMEOUT;
	timeout_value = interruptible_sleep_on_timeout(&adev->out_sleeper,
						       timeout_value);
	if (timeout != MAX_SCHEDULE_TIMEOUT && !timeout_value) {
		printk(KERN_WARNING "Sound: DMA (output) timed out - IRQ/DRQ config error?\n");
		dma_reset_output(dev);
	} else {
		if (signal_pending(current))
			err = -EINTR;
	}
	return err;
}

static int find_output_space(int dev, char **buf, int *size)
{
	struct audio_operations *adev = audio_devs[dev];
	struct dma_buffparms *dmap = adev->dmap_out;
	unsigned long flags;
	unsigned long active_offs;
	long len, offs;
	int maxfrags;
	int occupied_bytes = (dmap->user_counter % dmap->fragment_size);

	*buf = dmap->raw_buf;
	if (!(maxfrags = DMAbuf_space_in_queue(dev)) && !occupied_bytes)
		return 0;
	save_flags(flags);
	cli();

#ifdef BE_CONSERVATIVE
	active_offs = dmap->byte_counter + dmap->qhead * dmap->fragment_size;
#else
	active_offs = DMAbuf_get_buffer_pointer(dev, dmap, DMODE_OUTPUT);
	/* Check for pointer wrapping situation */
	if (active_offs < 0 || active_offs >= dmap->bytes_in_use)
		active_offs = 0;
	active_offs += dmap->byte_counter;
#endif

	offs = (dmap->user_counter % dmap->bytes_in_use) & ~SAMPLE_ROUNDUP;
	if (offs < 0 || offs >= dmap->bytes_in_use) {
		restore_flags(flags);
		printk(KERN_ERR "Sound: Got unexpected offs %ld. Giving up.\n", offs);
		printk("Counter = %ld, bytes=%d\n", dmap->user_counter, dmap->bytes_in_use);
		return 0;
	}
	*buf = dmap->raw_buf + offs;

	len = active_offs + dmap->bytes_in_use - dmap->user_counter;	/* Number of unused bytes in buffer */

	if ((offs + len) > dmap->bytes_in_use)
		len = dmap->bytes_in_use - offs;
	if (len < 0) {
		restore_flags(flags);
		return 0;
	}
	if (len > ((maxfrags * dmap->fragment_size) - occupied_bytes))
		len = (maxfrags * dmap->fragment_size) - occupied_bytes;
	*size = len & ~SAMPLE_ROUNDUP;
	restore_flags(flags);
	return (*size > 0);
}

int DMAbuf_getwrbuffer(int dev, char **buf, int *size, int dontblock)
{
	struct audio_operations *adev = audio_devs[dev];
	unsigned long flags;
	int err = -EIO;
	struct dma_buffparms *dmap = adev->dmap_out;

	if (dmap->needs_reorg)
		reorganize_buffers(dev, dmap, 0);

	if (dmap->mapping_flags & DMA_MAP_MAPPED) {
/*		printk(KERN_DEBUG "Sound: Can't write to mmapped device (3)\n");*/
		return -EINVAL;
	}
	if (dmap->dma_mode == DMODE_INPUT) {	/* Direction change */
		DMAbuf_reset(dev);
		dmap->dma_mode = DMODE_NONE;
	}
	dmap->dma_mode = DMODE_OUTPUT;

	save_flags(flags);
	cli();
	while (find_output_space(dev, buf, size) <= 0) {
		if ((err = output_sleep(dev, dontblock)) < 0) {
			restore_flags(flags);
			return err;
		}
	}
	restore_flags(flags);

	return 0;
}

int DMAbuf_move_wrpointer(int dev, int l)
{
	struct audio_operations *adev = audio_devs[dev];
	struct dma_buffparms *dmap = adev->dmap_out;
	unsigned long ptr = (dmap->user_counter / dmap->fragment_size) * dmap->fragment_size;
	unsigned long end_ptr, p;
	int post = (dmap->flags & DMA_POST);

	dmap->flags &= ~DMA_POST;
	dmap->cfrag = -1;
	dmap->user_counter += l;
	dmap->flags |= DMA_DIRTY;

	if (dmap->byte_counter >= dmap->max_byte_counter) {
		/* Wrap the byte counters */
		long decr = dmap->byte_counter;
		dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use);
		decr -= dmap->byte_counter;
		dmap->user_counter -= decr;
	}
	end_ptr = (dmap->user_counter / dmap->fragment_size) * dmap->fragment_size;

	p = (dmap->user_counter - 1) % dmap->bytes_in_use;
	dmap->neutral_byte = dmap->raw_buf[p];

	/* Update the fragment based bookkeeping too */
	while (ptr < end_ptr) {
		dmap->counts[dmap->qtail] = dmap->fragment_size;
		dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
		dmap->qlen++;
		ptr += dmap->fragment_size;
	}

	dmap->counts[dmap->qtail] = dmap->user_counter - ptr;

	/*
	 *	Let the low level driver to perform some postprocessing to
	 *	the written data.
	 */
	if (adev->d->postprocess_write)
		adev->d->postprocess_write(dev);

	if (!(dmap->flags & DMA_ACTIVE))
		if (dmap->qlen > 1 || (dmap->qlen > 0 && (post || dmap->qlen >= dmap->nbufs - 1)))
			DMAbuf_launch_output(dev, dmap);
	return 0;
}

int DMAbuf_start_dma(int dev, unsigned long physaddr, int count, int dma_mode)
{
	struct audio_operations *adev = audio_devs[dev];
	struct dma_buffparms *dmap = (dma_mode == DMA_MODE_WRITE) ? adev->dmap_out : adev->dmap_in;

	if (dmap->raw_buf == NULL) {
		printk(KERN_ERR "sound: DMA buffer(1) == NULL\n");
		printk("Device %d, chn=%s\n", dev, (dmap == adev->dmap_out) ? "out" : "in");
		return 0;
	}
	if (dmap->dma < 0)
		return 0;
	sound_start_dma(dmap, physaddr, count, dma_mode);
	return count;
}

static int local_start_dma(struct audio_operations *adev, unsigned long physaddr, int count, int dma_mode)
{
	struct dma_buffparms *dmap = (dma_mode == DMA_MODE_WRITE) ? adev->dmap_out : adev->dmap_in;

	if (dmap->raw_buf == NULL) {
		printk(KERN_ERR "sound: DMA buffer(2) == NULL\n");
		printk(KERN_ERR "Device %s, chn=%s\n", adev->name, (dmap == adev->dmap_out) ? "out" : "in");
		return 0;
	}
	if (dmap->flags & DMA_NODMA)
		return 1;
	if (dmap->dma < 0)
		return 0;
	sound_start_dma(dmap, dmap->raw_buf_phys, dmap->bytes_in_use, dma_mode | DMA_AUTOINIT);
	dmap->flags |= DMA_STARTED;
	return count;
}

static void finish_output_interrupt(int dev, struct dma_buffparms *dmap)
{
	struct audio_operations *adev = audio_devs[dev];

	if (dmap->audio_callback != NULL)
		dmap->audio_callback(dev, dmap->callback_parm);
	wake_up(&adev->out_sleeper);
	wake_up(&adev->poll_sleeper);
}

static void do_outputintr(int dev, int dummy)
{
	struct audio_operations *adev = audio_devs[dev];
	unsigned long flags;
	struct dma_buffparms *dmap = adev->dmap_out;
	int this_fragment;

	if (dmap->raw_buf == NULL) {
		printk(KERN_ERR "Sound: Error. Audio interrupt (%d) after freeing buffers.\n", dev);
		return;
	}
	if (dmap->mapping_flags & DMA_MAP_MAPPED) {	/* Virtual memory mapped access */
		/* mmapped access */
		dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
		if (dmap->qhead == 0) {	    /* Wrapped */
			dmap->byte_counter += dmap->bytes_in_use;
			if (dmap->byte_counter >= dmap->max_byte_counter) {	/* Overflow */
				long decr = dmap->byte_counter;
				dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use);
				decr -= dmap->byte_counter;
				dmap->user_counter -= decr;
			}
		}
		dmap->qlen++;	/* Yes increment it (don't decrement) */
		if (!(adev->flags & DMA_AUTOMODE))
			dmap->flags &= ~DMA_ACTIVE;
		dmap->counts[dmap->qhead] = dmap->fragment_size;
		DMAbuf_launch_output(dev, dmap);
		finish_output_interrupt(dev, dmap);
		return;
	}
	save_flags(flags);
	cli();

	dmap->qlen--;
	this_fragment = dmap->qhead;
	dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;

	if (dmap->qhead == 0) {	/* Wrapped */
		dmap->byte_counter += dmap->bytes_in_use;
		if (dmap->byte_counter >= dmap->max_byte_counter) {	/* Overflow */
			long decr = dmap->byte_counter;
			dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use);
			decr -= dmap->byte_counter;
			dmap->user_counter -= decr;
		}
	}
	if (!(adev->flags & DMA_AUTOMODE))
		dmap->flags &= ~DMA_ACTIVE;
		
	/*
	 *	This is  dmap->qlen <= 0 except when closing when
	 *	dmap->qlen < 0
	 */
	 
	while (dmap->qlen <= -dmap->closing) {
		dmap->underrun_count++;
		dmap->qlen++;
		if ((dmap->flags & DMA_DIRTY) && dmap->applic_profile != APF_CPUINTENS) {
			dmap->flags &= ~DMA_DIRTY;
			memset(adev->dmap_out->raw_buf, adev->dmap_out->neutral_byte,
			       adev->dmap_out->buffsize);
		}
		dmap->user_counter += dmap->fragment_size;
		dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
	}
	if (dmap->qlen > 0)
		DMAbuf_launch_output(dev, dmap);
	restore_flags(flags);
	finish_output_interrupt(dev, dmap);
}

void DMAbuf_outputintr(int dev, int notify_only)
{
	struct audio_operations *adev = audio_devs[dev];
	unsigned long flags;
	struct dma_buffparms *dmap = adev->dmap_out;

	save_flags(flags);
	cli();
	if (!(dmap->flags & DMA_NODMA)) {
		int chan = dmap->dma, pos, n;
		unsigned long f;
		
		f=claim_dma_lock();
		
		if(!isa_dma_bridge_buggy)
			disable_dma(dmap->dma);
		clear_dma_ff(chan);
		pos = dmap->bytes_in_use - get_dma_residue(chan);
		if(!isa_dma_bridge_buggy)
			enable_dma(dmap->dma);
		release_dma_lock(f);
		
		pos = pos / dmap->fragment_size;	/* Actual qhead */
		if (pos < 0 || pos >= dmap->nbufs)
			pos = 0;
		n = 0;
		while (dmap->qhead != pos && n++ < dmap->nbufs)
			do_outputintr(dev, notify_only);
	}
	else
		do_outputintr(dev, notify_only);
	restore_flags(flags);
}

static void do_inputintr(int dev)
{
	struct audio_operations *adev = audio_devs[dev];
	struct dma_buffparms *dmap = adev->dmap_in;

	if (dmap->raw_buf == NULL) {
		printk(KERN_ERR "Sound: Fatal error. Audio interrupt after freeing buffers.\n");
		return;
	}
	if (dmap->mapping_flags & DMA_MAP_MAPPED) {
		dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
		if (dmap->qtail == 0) {		/* Wrapped */
			dmap->byte_counter += dmap->bytes_in_use;
			if (dmap->byte_counter >= dmap->max_byte_counter) {	/* Overflow */
				long decr = dmap->byte_counter;
				dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use) + dmap->bytes_in_use;
				decr -= dmap->byte_counter;
				dmap->user_counter -= decr;
			}
		}
		dmap->qlen++;

		if (!(adev->flags & DMA_AUTOMODE)) {
			if (dmap->needs_reorg)
				reorganize_buffers(dev, dmap, 0);
			local_start_dma(adev, dmap->raw_buf_phys, dmap->bytes_in_use,DMA_MODE_READ);
			adev->d->start_input(dev, dmap->raw_buf_phys + dmap->qtail * dmap->fragment_size,
					     dmap->fragment_size, 1);
			if (adev->d->trigger)
				adev->d->trigger(dev, adev->enable_bits * adev->go);
		}
		dmap->flags |= DMA_ACTIVE;
	} else if (dmap->qlen >= (dmap->nbufs - 1)) {
		printk(KERN_WARNING "Sound: Recording overrun\n");
		dmap->underrun_count++;

		/* Just throw away the oldest fragment but keep the engine running */
		dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
		dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
	} else if (dmap->qlen >= 0 && dmap->qlen < dmap->nbufs) {
		dmap->qlen++;
		dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
		if (dmap->qtail == 0) {		/* Wrapped */
			dmap->byte_counter += dmap->bytes_in_use;
			if (dmap->byte_counter >= dmap->max_byte_counter) {	/* Overflow */
				long decr = dmap->byte_counter;
				dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use) + dmap->bytes_in_use;
				decr -= dmap->byte_counter;
				dmap->user_counter -= decr;
			}
		}
	}
	if (!(adev->flags & DMA_AUTOMODE) || (dmap->flags & DMA_NODMA)) {
		local_start_dma(adev, dmap->raw_buf_phys, dmap->bytes_in_use, DMA_MODE_READ);
		adev->d->start_input(dev, dmap->raw_buf_phys + dmap->qtail * dmap->fragment_size, dmap->fragment_size, 1);
		if (adev->d->trigger)
			adev->d->trigger(dev,adev->enable_bits * adev->go);
	}
	dmap->flags |= DMA_ACTIVE;
	if (dmap->qlen > 0)
	{
		wake_up(&adev->in_sleeper);
		wake_up(&adev->poll_sleeper);
	}
}

void DMAbuf_inputintr(int dev)
{
	struct audio_operations *adev = audio_devs[dev];
	struct dma_buffparms *dmap = adev->dmap_in;
	unsigned long flags;

	save_flags(flags);
	cli();

	if (!(dmap->flags & DMA_NODMA)) {
		int chan = dmap->dma, pos, n;
		unsigned long f;
		
		f=claim_dma_lock();
		if(!isa_dma_bridge_buggy)
			disable_dma(dmap->dma);
		clear_dma_ff(chan);
		pos = dmap->bytes_in_use - get_dma_residue(chan);
		if(!isa_dma_bridge_buggy)
			enable_dma(dmap->dma);
		release_dma_lock(f);

		pos = pos / dmap->fragment_size;	/* Actual qhead */
		if (pos < 0 || pos >= dmap->nbufs)
			pos = 0;

		n = 0;
		while (dmap->qtail != pos && ++n < dmap->nbufs)
			do_inputintr(dev);
	} else
		do_inputintr(dev);
	restore_flags(flags);
}

int DMAbuf_open_dma(int dev)
{
	/*
	 *    NOTE!  This routine opens only the primary DMA channel (output).
	 */
	struct audio_operations *adev = audio_devs[dev];
	int err;

	if ((err = open_dmap(adev, OPEN_READWRITE, adev->dmap_out)) < 0)
		return -EBUSY;
	dma_init_buffers(adev->dmap_out);
	adev->dmap_out->flags |= DMA_ALLOC_DONE;
	adev->dmap_out->fragment_size = adev->dmap_out->buffsize;

	if (adev->dmap_out->dma >= 0) {
		unsigned long flags;

		flags=claim_dma_lock();
		clear_dma_ff(adev->dmap_out->dma);
		disable_dma(adev->dmap_out->dma);
		release_dma_lock(flags);
	}
	return 0;
}

void DMAbuf_close_dma(int dev)
{
	close_dmap(audio_devs[dev], audio_devs[dev]->dmap_out);
}

void DMAbuf_init(int dev, int dma1, int dma2)
{
	struct audio_operations *adev = audio_devs[dev];
	/*
	 * NOTE! This routine could be called several times.
	 */

	/* drag in audio_syms.o */
	{
		extern char audio_syms_symbol;
		audio_syms_symbol = 0;
	}

	if (adev && adev->dmap_out == NULL) {
		if (adev->d == NULL)
			panic("OSS: audio_devs[%d]->d == NULL\n", dev);

		if (adev->parent_dev) {	 /* Use DMA map of the parent dev */
			int parent = adev->parent_dev - 1;
			adev->dmap_out = audio_devs[parent]->dmap_out;
			adev->dmap_in = audio_devs[parent]->dmap_in;
		} else {
			adev->dmap_out = adev->dmap_in = &adev->dmaps[0];
			adev->dmap_out->dma = dma1;
			if (adev->flags & DMA_DUPLEX) {
				adev->dmap_in = &adev->dmaps[1];
				adev->dmap_in->dma = dma2;
			}
		}
		/* Persistent DMA buffers allocated here */
		if (sound_dmap_flag == DMAP_KEEP_ON_CLOSE) {
			if (adev->dmap_in->raw_buf == NULL)
				sound_alloc_dmap(adev->dmap_in);
			if (adev->dmap_out->raw_buf == NULL)
				sound_alloc_dmap(adev->dmap_out);
		}
	}
}

static unsigned int poll_input(struct file * file, int dev, poll_table *wait)
{
	struct audio_operations *adev = audio_devs[dev];
	struct dma_buffparms *dmap = adev->dmap_in;

	if (!(adev->open_mode & OPEN_READ))
		return 0;
	if (dmap->mapping_flags & DMA_MAP_MAPPED) {
		if (dmap->qlen)
			return POLLIN | POLLRDNORM;
		return 0;
	}
	if (dmap->dma_mode != DMODE_INPUT) {
		if (dmap->dma_mode == DMODE_NONE &&
		    adev->enable_bits & PCM_ENABLE_INPUT &&
		    !dmap->qlen && adev->go) {
			unsigned long flags;
			
			save_flags(flags);
			cli();
			DMAbuf_activate_recording(dev, dmap);
			restore_flags(flags);
		}
		return 0;
	}
	if (!dmap->qlen)
		return 0;
	return POLLIN | POLLRDNORM;
}

static unsigned int poll_output(struct file * file, int dev, poll_table *wait)
{
	struct audio_operations *adev = audio_devs[dev];
	struct dma_buffparms *dmap = adev->dmap_out;
	
	if (!(adev->open_mode & OPEN_WRITE))
		return 0;
	if (dmap->mapping_flags & DMA_MAP_MAPPED) {
		if (dmap->qlen)
			return POLLOUT | POLLWRNORM;
		return 0;
	}
	if (dmap->dma_mode == DMODE_INPUT)
		return 0;
	if (dmap->dma_mode == DMODE_NONE)
		return POLLOUT | POLLWRNORM;
	if (!DMAbuf_space_in_queue(dev))
		return 0;
	return POLLOUT | POLLWRNORM;
}

unsigned int DMAbuf_poll(struct file * file, int dev, poll_table *wait)
{
	struct audio_operations *adev = audio_devs[dev];
	poll_wait(file, &adev->poll_sleeper, wait);
	return poll_input(file, dev, wait) | poll_output(file, dev, wait);
}

void DMAbuf_deinit(int dev)
{
	struct audio_operations *adev = audio_devs[dev];
	/* This routine is called when driver is being unloaded */
	if (!adev)
		return;

	/* Persistent DMA buffers deallocated here */
	if (sound_dmap_flag == DMAP_KEEP_ON_CLOSE) {
		sound_free_dmap(adev->dmap_out);
		if (adev->flags & DMA_DUPLEX)
			sound_free_dmap(adev->dmap_in);
	}
}

#endif
