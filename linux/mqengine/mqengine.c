// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * snd-mqengine: an Opcode MIDIEngine 2Port/SE, a Music Quest design, on a
 * parallel port: two MIDI inputs and two outputs as one rawmidi device, with
 * subdevice 0 for A and 1 for B.
 *
 * It takes the port's I/O and IRQ itself, as snd-mtpav does, so parport_pc has
 * to stay out of it. irq=-1 polls instead of using the port's IRQ.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>

#define mq_out(io, reg, v) outb(v, (io)->base + (reg))
#define mq_in(io, reg) inb((io)->base + (reg))
#define mq_us(n) udelay(n)
#define mq_ms(n) msleep(n)
#include "proto.h"

#define MQ_PORTS 2
#define MQ_NOW_BYTES 4 // sent from a write itself, for its timing
#define MQ_TICK_BYTES 32 // each way, more than a tick of two full cables

static int index = SNDRV_DEFAULT_IDX1;
static char *id = "MIDIEngine";
static long port = 0x378;
static int irq = 7;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for the MIDIEngine.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for the MIDIEngine.");
module_param_hw(port, long, ioport, 0444);
MODULE_PARM_DESC(port, "Parallel port of the MIDIEngine.");
module_param_hw(irq, int, irq, 0444);
MODULE_PARM_DESC(irq, "The parallel port's IRQ, -1 to poll.");

struct mqengine {
	struct snd_card *card;
	struct mq_io io;
	spinlock_t lock; // the port's lines, and all below
	struct timer_list timer;
	struct snd_rawmidi_substream *in[MQ_PORTS], *out[MQ_PORTS];
	unsigned int in_on, out_on; // bits by port: triggered
	unsigned int turn; // the output that goes next
};

// up to n bytes from the unit, each under the lock on its own so that other
// interrupts get in; whether more may wait
static bool mq_input(struct mqengine *m, unsigned int n)
{
	unsigned long flags;
	unsigned int in;
	u8 b;

	while (n--) {
		bool got;

		spin_lock_irqsave(&m->lock, flags);
		got = mq_get(&m->io, &b, &in);
		if (got && (m->in_on & BIT(in)))
			snd_rawmidi_receive(m->in[in], &b, 1);
		spin_unlock_irqrestore(&m->lock, flags);
		if (!got)
			return false;
		if (n)
			udelay(MQ_GAP_US);
	}
	return true;
}

// up to n bytes to the unit, taking the outputs in turn
static void mq_output(struct mqengine *m, unsigned int n)
{
	unsigned long flags;
	u8 b;

	while (n--) {
		bool stop, sent = false;
		unsigned int out;

		spin_lock_irqsave(&m->lock, flags);
		stop = !m->out_on || !mq_room(&m->io);
		if (!stop) {
			out = m->out_on & BIT(m->turn) ? m->turn : !m->turn;
			sent = snd_rawmidi_transmit(m->out[out], &b, 1) == 1;
			if (sent)
				mq_put(&m->io, out, b);
			else
				m->out_on &= ~BIT(out);
			m->turn = !out;
		}
		spin_unlock_irqrestore(&m->lock, flags);
		if (stop)
			return;
		if (sent)
			udelay(MQ_GAP_US);
	}
}

static void mq_tick_on(struct mqengine *m)
{
	mod_timer(&m->timer, jiffies + 1);
}

// the outputs, and the input as a net under the interrupt, or in place of it
static void mq_tick(struct timer_list *t)
{
	struct mqengine *m = timer_container_of(m, t, timer);
	unsigned long flags;
	bool more;

	more = mq_input(m, MQ_TICK_BYTES);
	mq_output(m, MQ_TICK_BYTES);
	spin_lock_irqsave(&m->lock, flags);
	if (more || m->in_on || m->out_on)
		mq_tick_on(m);
	spin_unlock_irqrestore(&m->lock, flags);
}

static irqreturn_t mq_irq(int irq, void *dev)
{
	struct mqengine *m = dev;

	// the unit raises one for each byte, and takes a while over the next
	if (mq_input(m, 1))
		mq_tick_on(m);
	return IRQ_HANDLED;
}

static int mq_input_open(struct snd_rawmidi_substream *s)
{
	struct mqengine *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	m->in[s->number] = s;
	return 0;
}

static int mq_input_close(struct snd_rawmidi_substream *s)
{
	struct mqengine *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	m->in_on &= ~BIT(s->number);
	m->in[s->number] = NULL;
	return 0;
}

static void mq_input_trigger(struct snd_rawmidi_substream *s, int up)
{
	struct mqengine *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	if (up) {
		m->in_on |= BIT(s->number);
		mq_tick_on(m);
	} else {
		m->in_on &= ~BIT(s->number);
	}
}

static int mq_output_open(struct snd_rawmidi_substream *s)
{
	struct mqengine *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	m->out[s->number] = s;
	return 0;
}

static int mq_output_close(struct snd_rawmidi_substream *s)
{
	struct mqengine *m = s->rmidi->private_data;

	guard(spinlock_irqsave)(&m->lock);
	m->out_on &= ~BIT(s->number);
	m->out[s->number] = NULL;
	return 0;
}

static void mq_output_trigger(struct snd_rawmidi_substream *s, int up)
{
	struct mqengine *m = s->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	if (up) {
		m->out_on |= BIT(s->number);
		mq_tick_on(m);
	} else {
		m->out_on &= ~BIT(s->number);
	}
	spin_unlock_irqrestore(&m->lock, flags);
	if (up)
		mq_output(m, MQ_NOW_BYTES);
}

static const struct snd_rawmidi_ops mq_input_ops = {
	.open = mq_input_open,
	.close = mq_input_close,
	.trigger = mq_input_trigger,
};

static const struct snd_rawmidi_ops mq_output_ops = {
	.open = mq_output_open,
	.close = mq_output_close,
	.trigger = mq_output_trigger,
};

static void mq_name(struct snd_rawmidi *rmidi, int stream)
{
	struct snd_rawmidi_substream *s;

	list_for_each_entry(s, &rmidi->streams[stream].substreams, list)
		snprintf(s->name, sizeof(s->name), "MIDIEngine %c",
			 'A' + s->number);
}

static int mq_rawmidi(struct mqengine *m)
{
	struct snd_rawmidi *rmidi;
	int err;

	err = snd_rawmidi_new(m->card, "MIDIEngine", 0, MQ_PORTS, MQ_PORTS,
			      &rmidi);
	if (err < 0)
		return err;
	rmidi->private_data = m;
	rmidi->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT |
			    SNDRV_RAWMIDI_INFO_INPUT |
			    SNDRV_RAWMIDI_INFO_DUPLEX;
	strscpy(rmidi->name, "MIDIEngine 2Port/SE");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &mq_output_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &mq_input_ops);
	mq_name(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT);
	mq_name(rmidi, SNDRV_RAWMIDI_STREAM_INPUT);
	return 0;
}

static int mq_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct mqengine *m;
	u8 found[3];
	int err;

	err = snd_devm_card_new(&pdev->dev, index, id, THIS_MODULE, sizeof(*m),
				&card);
	if (err < 0)
		return err;
	m = card->private_data;
	m->card = card;
	m->io.base = port;
	spin_lock_init(&m->lock);
	timer_setup(&m->timer, mq_tick, 0);

	if (!devm_request_region(&pdev->dev, port, 3, "MIDIEngine")) {
		dev_err(&pdev->dev, "port 0x%lx is in use\n", port);
		return -EBUSY;
	}
	if (mq_reset(&m->io, found)) {
		dev_err(&pdev->dev,
			"no 2Port/SE at 0x%lx: status %02x %02x %02x. Is the port on, and the unit powered?\n",
			port, found[0], found[1], found[2]);
		return -ENODEV;
	}
	if (irq >= 0) {
		err = devm_request_irq(&pdev->dev, irq, mq_irq, 0, "MIDIEngine",
				       m);
		if (err < 0) {
			dev_err(&pdev->dev, "IRQ %d is in use\n", irq);
			return err;
		}
	}
	err = mq_rawmidi(m);
	if (err < 0)
		return err;

	strscpy(card->driver, "MQEngine");
	strscpy(card->shortname, "MIDIEngine 2Port/SE");
	snprintf(card->longname, sizeof(card->longname),
		 "Opcode MIDIEngine 2Port/SE at 0x%lx, %s %d", port,
		 irq >= 0 ? "irq" : "polled, irq", irq);
	err = snd_card_register(card);
	if (err < 0)
		return err;
	if (irq >= 0) {
		m->io.control |= MQ_C_IRQ;
		mq_out(&m->io, MQ_CONTROL, m->io.control);
	}
	platform_set_drvdata(pdev, card);
	return 0;
}

// the lines quiet while the port is still this driver's
static void mq_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct mqengine *m = card->private_data;

	m->io.control = MQ_C_IDLE;
	mq_out(&m->io, MQ_CONTROL, m->io.control);
	mq_out(&m->io, MQ_DATA, 0xFF);
	timer_shutdown_sync(&m->timer);
}

#define MQ_DRIVER "snd_mqengine"

static struct platform_driver mq_driver = {
	.probe = mq_probe,
	.remove = mq_remove,
	.driver = {
		.name = MQ_DRIVER,
	},
};

static struct platform_device *mq_device;

static int __init mq_init(void)
{
	int err = platform_driver_register(&mq_driver);

	if (err < 0)
		return err;
	mq_device = platform_device_register_simple(MQ_DRIVER, -1, NULL, 0);
	if (!IS_ERR(mq_device) && platform_get_drvdata(mq_device))
		return 0;
	if (IS_ERR(mq_device)) {
		err = PTR_ERR(mq_device);
	} else {
		platform_device_unregister(mq_device);
		err = -ENODEV;
	}
	platform_driver_unregister(&mq_driver);
	return err;
}

static void __exit mq_exit(void)
{
	platform_device_unregister(mq_device);
	platform_driver_unregister(&mq_driver);
}

module_init(mq_init);
module_exit(mq_exit);

MODULE_AUTHOR("Ethan Halsall <ethan.s.halsall@gmail.com>");
MODULE_DESCRIPTION("Opcode MIDIEngine 2Port/SE on a parallel port");
MODULE_LICENSE("GPL");
