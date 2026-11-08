// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * snd-flyer: MOTU's PC-MIDI Flyer on a parallel port: two MIDI inputs and two
 * outputs as one rawmidi device, with subdevice 0 for A and 1 for B.
 *
 * It takes the port's I/O and IRQ itself, as snd-mtpav does, so parport_pc has
 * to stay out of it. irq=-1 polls instead of using the port's IRQ.
 *
 * The port's interrupt says that a byte came in, and the port passes it on only
 * while an input is open. Nothing says that the unit has room to send, and it
 * takes only four bytes each time, so a timer faster than the kernel's tick
 * looks for room while an output has bytes. The timer runs as a softirq and the
 * interrupt only starts it: a byte takes some thirty port accesses of about a
 * microsecond each, and other interrupts get in between bytes.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>

#define fl_out(io, reg, v) outb(v, (io)->base + (reg))
#define fl_in(io, reg) inb((io)->base + (reg))
#define fl_us(n) udelay(n)
#define fl_ms(n) msleep(n)
#include "proto.h"

#define FL_PORTS 2
#define FL_OUT_US 250 // room, when it comes back an output may have a byte left
#define FL_IN_US 2000 // polled input, whose ring lasts 4.8 ms at full speed

static int index = SNDRV_DEFAULT_IDX1;
static char *id = "Flyer";
static long port = 0x378;
static int irq = 7;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for the Flyer.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for the Flyer.");
module_param_hw(port, long, ioport, 0444);
MODULE_PARM_DESC(port, "Parallel port of the Flyer.");
module_param_hw(irq, int, irq, 0444);
MODULE_PARM_DESC(irq, "The parallel port's IRQ, -1 to poll.");

struct flyer {
	struct snd_card *card;
	struct fl_io io;
	spinlock_t lock; // the port's lines, and all below
	struct hrtimer tick;
	struct snd_rawmidi_substream *in[FL_PORTS], *out[FL_PORTS];
	unsigned int in_on, out_on; // bits by port: triggered
	unsigned int credit; // bytes that the last sight of room still covers
	unsigned int turn; // the output that goes next
	bool polled;
	bool ticking; // the timer is queued, or runs and goes on
	bool dead; // removed: the port is no longer touched
};

// what the unit holds, each byte under the lock on its own; bytes that no open
// input takes go too, since each one left waiting raises the interrupt again
// every 256 us
static void fl_input(struct flyer *m)
{
	unsigned long flags;
	unsigned int in;
	int n;

	for (in = 0; in < FL_PORTS; in++) {
		for (n = 0; n < FL_RING; n++) {
			bool got;
			u8 b;

			spin_lock_irqsave(&m->lock, flags);
			got = !m->dead && fl_waiting(&m->io, in);
			if (got) {
				b = fl_get(&m->io, in);
				if (m->in_on & BIT(in))
					snd_rawmidi_receive(m->in[in], &b, 1);
			}
			spin_unlock_irqrestore(&m->lock, flags);
			if (!got)
				break;
		}
	}
}

// as much as the unit has room for, taking the outputs in turn
static void fl_output(struct flyer *m)
{
	unsigned long flags;
	bool more;

	do {
		unsigned int out;
		u8 b;

		spin_lock_irqsave(&m->lock, flags);
		more = !m->dead && m->out_on;
		if (more) {
			m->credit = fl_credit(&m->io, m->credit);
			more = m->credit;
		}
		if (more) {
			out = m->out_on & BIT(m->turn) ? m->turn : !m->turn;
			if (snd_rawmidi_transmit(m->out[out], &b, 1) == 1) {
				fl_put(&m->io, out, b);
				m->credit--;
			} else {
				m->out_on &= ~BIT(out);
			}
			m->turn = !out;
		}
		spin_unlock_irqrestore(&m->lock, flags);
	} while (more);
}

// the timer, us from now unless it already runs; under the lock
static void fl_tick_on(struct flyer *m, unsigned int us)
{
	if (m->ticking || m->dead)
		return;
	m->ticking = true;
	hrtimer_start(&m->tick, us_to_ktime(us), HRTIMER_MODE_REL_SOFT);
}

// the port's interrupt while an input is open, unless polling, which needs the
// timer instead; under the lock
static void fl_listen(struct flyer *m)
{
	u8 want = m->in_on && !m->polled && !m->dead ? FL_C_IRQ : 0;

	if (m->io.irq != want) {
		m->io.irq = want;
		fl_ctl(&m->io, FL_C_IDLE);
	}
	if (m->in_on && m->polled)
		fl_tick_on(m, 0);
}

// room to send, and the bytes that came in
static enum hrtimer_restart fl_tick(struct hrtimer *t)
{
	struct flyer *m = container_of(t, struct flyer, tick);
	unsigned long flags;
	unsigned int us = 0;

	fl_input(m);
	fl_output(m);
	spin_lock_irqsave(&m->lock, flags);
	if (!m->dead && m->out_on)
		us = FL_OUT_US;
	else if (!m->dead && m->in_on && m->polled)
		us = FL_IN_US;
	m->ticking = us != 0;
	if (us)
		hrtimer_forward_now(t, us_to_ktime(us));
	spin_unlock_irqrestore(&m->lock, flags);
	return us ? HRTIMER_RESTART : HRTIMER_NORESTART;
}

static irqreturn_t fl_irq(int irq_no, void *dev)
{
	struct flyer *m = dev;

	spin_lock(&m->lock);
	fl_tick_on(m, 0);
	spin_unlock(&m->lock);
	return IRQ_HANDLED;
}

// what came in before the input was open goes first
static int fl_input_open(struct snd_rawmidi_substream *s)
{
	struct flyer *m = s->rmidi->private_data;

	fl_input(m);
	guard(spinlock_irqsave)(&m->lock);
	m->in[s->number] = s;
	return 0;
}

static int fl_input_close(struct snd_rawmidi_substream *s)
{
	struct flyer *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	m->in_on &= ~BIT(s->number);
	m->in[s->number] = NULL;
	fl_listen(m);
	return 0;
}

static void fl_input_trigger(struct snd_rawmidi_substream *s, int up)
{
	struct flyer *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	if (up)
		m->in_on |= BIT(s->number);
	else
		m->in_on &= ~BIT(s->number);
	fl_listen(m);
}

static int fl_output_open(struct snd_rawmidi_substream *s)
{
	struct flyer *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	m->out[s->number] = s;
	return 0;
}

static int fl_output_close(struct snd_rawmidi_substream *s)
{
	struct flyer *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	m->out_on &= ~BIT(s->number);
	m->out[s->number] = NULL;
	return 0;
}

static void fl_output_trigger(struct snd_rawmidi_substream *s, int up)
{
	struct flyer *m = s->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	if (up) {
		m->out_on |= BIT(s->number);
		fl_tick_on(m, FL_OUT_US);
	} else {
		m->out_on &= ~BIT(s->number);
	}
	spin_unlock_irqrestore(&m->lock, flags);
	if (up)
		fl_output(m);
}

static const struct snd_rawmidi_ops fl_input_ops = {
	.open = fl_input_open,
	.close = fl_input_close,
	.trigger = fl_input_trigger,
};

static const struct snd_rawmidi_ops fl_output_ops = {
	.open = fl_output_open,
	.close = fl_output_close,
	.trigger = fl_output_trigger,
};

static void fl_name(struct snd_rawmidi *rmidi, int stream)
{
	struct snd_rawmidi_substream *s;

	list_for_each_entry(s, &rmidi->streams[stream].substreams, list)
		snprintf(s->name, sizeof(s->name), "Flyer %c", 'A' + s->number);
}

static int fl_rawmidi(struct flyer *m)
{
	struct snd_rawmidi *rmidi;
	int err;

	err = snd_rawmidi_new(m->card, "Flyer", 0, FL_PORTS, FL_PORTS, &rmidi);
	if (err < 0)
		return err;
	rmidi->private_data = m;
	rmidi->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT |
			    SNDRV_RAWMIDI_INFO_INPUT |
			    SNDRV_RAWMIDI_INFO_DUPLEX;
	strscpy(rmidi->name, "PC-MIDI Flyer");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &fl_output_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &fl_input_ops);
	fl_name(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT);
	fl_name(rmidi, SNDRV_RAWMIDI_STREAM_INPUT);
	return 0;
}

static int fl_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct flyer *m;
	int err;

	err = snd_devm_card_new(&pdev->dev, index, id, THIS_MODULE, sizeof(*m),
				&card);
	if (err < 0)
		return err;
	m = card->private_data;
	m->card = card;
	m->io.base = port;
	m->polled = irq < 0;
	spin_lock_init(&m->lock);
	hrtimer_setup(&m->tick, fl_tick, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_SOFT);

	if (!devm_request_region(&pdev->dev, port, 3, "Flyer")) {
		dev_err(&pdev->dev, "port 0x%lx is in use\n", port);
		return -EBUSY;
	}
	fl_start(&m->io);
	if (!fl_alive(&m->io)) {
		dev_err(&pdev->dev,
			"no PC-MIDI Flyer at 0x%lx: is the port on?\n", port);
		return -ENODEV;
	}
	if (!m->polled) {
		err = devm_request_irq(&pdev->dev, irq, fl_irq, 0, "Flyer", m);
		if (err < 0) {
			dev_err(&pdev->dev, "IRQ %d is in use\n", irq);
			return err;
		}
	}
	err = fl_rawmidi(m);
	if (err < 0)
		return err;

	strscpy(card->driver, "Flyer");
	strscpy(card->shortname, "PC-MIDI Flyer");
	snprintf(card->longname, sizeof(card->longname),
		 "MOTU PC-MIDI Flyer at 0x%lx, %s %d", port,
		 m->polled ? "polled, irq" : "irq", irq);
	err = snd_card_register(card);
	if (err < 0)
		return err;
	platform_set_drvdata(pdev, card);
	return 0;
}

// the port's interrupt off while the port is still this driver's, and nothing
// after; the data lines stay up for the next load
static void fl_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct flyer *m = card->private_data;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	m->dead = true;
	m->io.irq = 0;
	fl_ctl(&m->io, FL_C_IDLE);
	spin_unlock_irqrestore(&m->lock, flags);
	hrtimer_cancel(&m->tick);
}

#define FL_DRIVER "snd_flyer"

static struct platform_driver fl_driver = {
	.probe = fl_probe,
	.remove = fl_remove,
	.driver = {
		.name = FL_DRIVER,
	},
};

static struct platform_device *fl_device;

static int __init fl_init(void)
{
	int err = platform_driver_register(&fl_driver);

	if (err < 0)
		return err;
	fl_device = platform_device_register_simple(FL_DRIVER, -1, NULL, 0);
	if (!IS_ERR(fl_device) && platform_get_drvdata(fl_device))
		return 0;
	if (IS_ERR(fl_device)) {
		err = PTR_ERR(fl_device);
	} else {
		platform_device_unregister(fl_device);
		err = -ENODEV;
	}
	platform_driver_unregister(&fl_driver);
	return err;
}

static void __exit fl_exit(void)
{
	platform_device_unregister(fl_device);
	platform_driver_unregister(&fl_driver);
}

module_init(fl_init);
module_exit(fl_exit);

MODULE_AUTHOR("Ethan Halsall <ethan.s.halsall@gmail.com>");
MODULE_DESCRIPTION("MOTU PC-MIDI Flyer on a parallel port");
MODULE_LICENSE("GPL");
