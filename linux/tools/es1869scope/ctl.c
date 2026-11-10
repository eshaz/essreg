// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ctl.c: the card's controls by name, for es1869scope's engine and viewer
 * (ctl.h): booleans, enumerations and integers alike, the first two values or
 * all of them, and an enumeration's item by its text.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "ctl.h"

#include <string.h>

static int ctl_info(snd_ctl_t *h, const char *name, snd_ctl_elem_id_t *id,
		    snd_ctl_elem_info_t *info)
{
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, name);
	snd_ctl_elem_info_set_id(info, id);
	return snd_ctl_elem_info(h, info);
}

// a control's values, the first two
int ctl_read(snd_ctl_t *h, const char *name, long *v, unsigned int *count)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_type_t type;
	unsigned int i;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	if (ctl_info(h, name, id, info) < 0)
		return -1;
	type = snd_ctl_elem_info_get_type(info);
	*count = snd_ctl_elem_info_get_count(info);
	if (*count > 2)
		*count = 2;
	snd_ctl_elem_value_set_id(val, id);
	if (snd_ctl_elem_read(h, val) < 0)
		return -1;
	for (i = 0; i < *count; i++)
		v[i] = type == SND_CTL_ELEM_TYPE_BOOLEAN ?
			       snd_ctl_elem_value_get_boolean(val, i) :
		       type == SND_CTL_ELEM_TYPE_ENUMERATED ?
			       (long)snd_ctl_elem_value_get_enumerated(val, i) :
			       snd_ctl_elem_value_get_integer(val, i);
	return 0;
}

// a control written, each of its values from v, the last one repeated
int ctl_write(snd_ctl_t *h, const char *name, const long *v, unsigned int count)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_type_t type;
	unsigned int i, all;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	if (!count || ctl_info(h, name, id, info) < 0)
		return -1;
	type = snd_ctl_elem_info_get_type(info);
	all = snd_ctl_elem_info_get_count(info);
	snd_ctl_elem_value_set_id(val, id);
	for (i = 0; i < all; i++) {
		long x = v[i < count ? i : count - 1];

		if (type == SND_CTL_ELEM_TYPE_BOOLEAN)
			snd_ctl_elem_value_set_boolean(val, i, x);
		else if (type == SND_CTL_ELEM_TYPE_ENUMERATED)
			snd_ctl_elem_value_set_enumerated(val, i,
							  (unsigned int)x);
		else
			snd_ctl_elem_value_set_integer(val, i, x);
	}
	return snd_ctl_elem_write(h, val) < 0 ? -1 : 0;
}

// an integer control of count values, all written
int ctl_write_all(snd_ctl_t *h, const char *name, const long *v,
		  unsigned int count)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	unsigned int i;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	if (ctl_info(h, name, id, info) < 0 ||
	    snd_ctl_elem_info_get_count(info) != count)
		return -1;
	snd_ctl_elem_value_set_id(val, id);
	for (i = 0; i < count; i++)
		snd_ctl_elem_value_set_integer(val, i, v[i]);
	return snd_ctl_elem_write(h, val) < 0 ? -1 : 0;
}

// an integer control's count values, all read
int ctl_read_all(snd_ctl_t *h, const char *name, long *v, unsigned int count)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	unsigned int i;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	if (ctl_info(h, name, id, info) < 0 ||
	    snd_ctl_elem_info_get_count(info) != count)
		return -1;
	snd_ctl_elem_value_set_id(val, id);
	if (snd_ctl_elem_read(h, val) < 0)
		return -1;
	for (i = 0; i < count; i++)
		v[i] = snd_ctl_elem_value_get_integer(val, i);
	return 0;
}

// an enumerated control's item, by its name
int ctl_item(snd_ctl_t *h, const char *name, const char *item)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	unsigned int i, n;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	if (ctl_info(h, name, id, info) < 0 ||
	    snd_ctl_elem_info_get_type(info) != SND_CTL_ELEM_TYPE_ENUMERATED)
		return -1;
	n = snd_ctl_elem_info_get_items(info);
	for (i = 0; i < n; i++) {
		snd_ctl_elem_info_set_item(info, i);
		if (snd_ctl_elem_info(h, info) < 0)
			return -1;
		if (!strcmp(snd_ctl_elem_info_get_item_name(info), item))
			return (int)i;
	}
	return -1;
}
