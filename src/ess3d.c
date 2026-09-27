/*
 * ess3d's command line and 3-D register changes, see ess3d.h.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ess3d.h"
#include "esscat.h"
#include "essio.h"

int ess3d_level_max(void) { return cat_max(&ess_fields[F_3D_LEVEL]); }

// --- command line ---------------------------------------------------------

static int is_space(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int is_letter(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

// next word of the command line (quoted text is one word), 0 at the end
static const char *next_word(const char *p, char *out, unsigned size) {
  unsigned n = 0;
  int quoted = 0;

  while (is_space(*p))
    p++;
  if (!*p)
    return 0;
  while (*p && (quoted || !is_space(*p))) {
    if (*p == '"')
      quoted = !quoted;
    else if (n + 1 < size)
      out[n++] = *p;
    p++;
  }
  out[n] = 0;
  return p;
}

static void lower(char *w) {
  for (; *w; w++)
    if (*w >= 'A' && *w <= 'Z')
      *w = (char)(*w - 'A' + 'a');
}

// keep the first problem, the rest of the line is still read so /q and
// /log= count wherever they are
static void fail(struct ess3d_cmd *c, const char *fmt, const char *word) {
  if (!c->err[0])
    sprintf(c->err, fmt, word);
}

// a level: "40", "+4", "-4", "50%" or "+10%", with the sign character in
// *sign (0, '+' or '-')
// returns the number of steps (percentages of 63, rounded), or -1
static int level_word(const char *w, char *sign) {
  unsigned long n = 0;
  int digits = 0;

  *sign = 0;
  if (*w == '+' || *w == '-')
    *sign = *w++;
  while (*w >= '0' && *w <= '9' && digits < 4) {
    n = n * 10 + (unsigned)(*w++ - '0');
    digits++;
  }
  if (!digits)
    return -1;
  if (*w == '%') {
    w++;
    if (n > 100)
      return -1;
    n = (n * (unsigned)ess3d_level_max() + 50) / 100;
  } else if (n > (unsigned)ess3d_level_max()) {
    return -1;
  }
  return *w ? -1 : (int)n;
}

static void bad_level(struct ess3d_cmd *c, const char *cmd, const char *arg) {
  char text[32];

  if (*arg)
    sprintf(text, "%s %.20s", cmd, arg);
  else
    strcpy(text, cmd);
  fail(c, "%s: the level is 0 to 63, or 0%% to 100%%", text);
}

// a switch, without its / or -
static void switch_word(char *w, struct ess3d_cmd *c) {
  char *arg = strchr(w, '=');
  char *end;
  unsigned long v;

  if (arg)
    *arg++ = 0;
  lower(w);
  if (!arg && !strcmp(w, "q")) {
    c->quiet = 1;
  } else if (!arg && !strcmp(w, "sim")) {
    c->sim = 1;
  } else if (!arg && !strcmp(w, "novxd")) {
    c->novxd = 1;
  } else if (arg && (!strcmp(w, "base") || !strcmp(w, "cfg"))) {
    v = strtoul(arg, &end, 16);
    if (!*arg || *end || v > 0xFFFF)
      fail(c, "/%s= needs a port in hex", w);
    else if (w[0] == 'b')
      c->audio_base = (u16)v;
    else
      c->config_base = (u16)v;
  } else if (arg && !strcmp(w, "log")) {
    if (!*arg)
      fail(c, "/log= needs a file name", "");
    strncpy(c->log, arg, sizeof(c->log) - 1);
    c->log[sizeof(c->log) - 1] = 0;
  } else if (arg && !strcmp(w, "t")) {
    v = strtoul(arg, &end, 10);
    if (!*arg || *end || v < 100 || v > 60000)
      fail(c, "/t= is the display time, 100 to 60000 ms", "");
    else
      c->time_ms = (u16)v;
  } else {
    fail(c, "unknown or bad option: /%.40s", w);
  }
}

int ess3d_parse(const char *line, struct ess3d_cmd *c) {
  char word[128], arg[128];
  const char *p = line;
  const char *after;
  struct ess3d_action spare;
  struct ess3d_action *a;
  char sign;
  int n;

  memset(c, 0, sizeof(*c));
  c->time_ms = ESS3D_TIME;
  while ((p = next_word(p, word, sizeof(word))) != 0) {
    // "-4" is a level, "-q" a switch
    if (word[0] == '/' || (word[0] == '-' && is_letter(word[1]))) {
      switch_word(word + 1, c);
      continue;
    }
    lower(word);
    a = c->nact < ESS3D_MAX_ACTIONS ? &c->act[c->nact] : &spare;
    if (!strcmp(word, "on")) {
      a->op = ESS3D_ON;
    } else if (!strcmp(word, "off")) {
      a->op = ESS3D_OFF;
    } else if (!strcmp(word, "toggle")) {
      a->op = ESS3D_TOGGLE;
    } else if (!strcmp(word, "hold")) {
      a->op = ESS3D_HOLD;
    } else if (!strcmp(word, "reset")) {
      a->op = ESS3D_RESET;
    } else if (!strcmp(word, "show")) {
      a->op = ESS3D_SHOW;
    } else if (!strcmp(word, "level")) {
      // absolute, or relative with a sign
      after = next_word(p, arg, sizeof(arg));
      if (!after) {
        bad_level(c, word, "");
        continue;
      }
      p = after;
      if ((n = level_word(arg, &sign)) < 0) {
        bad_level(c, word, arg);
        continue;
      }
      a->op = sign ? ESS3D_ADD : ESS3D_LEVEL;
      a->arg = (s8)(sign == '-' ? -n : n);
    } else if (!strcmp(word, "up") || !strcmp(word, "down")) {
      // the number of steps is optional, a word starting with a digit is it
      n = ESS3D_STEP;
      after = next_word(p, arg, sizeof(arg));
      if (after && arg[0] >= '0' && arg[0] <= '9') {
        p = after;
        if ((n = level_word(arg, &sign)) < 0) {
          bad_level(c, word, arg);
          continue;
        }
      }
      a->op = ESS3D_ADD;
      a->arg = (s8)(word[0] == 'd' ? -n : n);
    } else {
      fail(c, "unknown command: %.40s", word);
      continue;
    }
    if (a == &spare)
      fail(c, "too many commands", "");
    else
      c->nact++;
  }
  if (!c->nact)
    fail(c, "no command", "");
  return c->err[0] ? -1 : 0;
}

// --- registers ------------------------------------------------------------

int ess3d_read(struct ess3d_state *s) {
  u8 raw;
  int err = ess_field_read(F_3D_EN, &s->enable, &raw);

  if (err < 0)
    return err;
  s->run = cat_get(&ess_fields[F_3D_RUN], raw);
  return ess_field_read(F_3D_LEVEL, &s->level, 0);
}

// write a field of the effect if it changes, *have is what the chip has
static int set(int field, u8 *have, u8 value) {
  int err;

  if (*have == value)
    return 0;
  err = ess_field_write(field, value, 0);
  if (err < 0)
    return err;
  *have = value;
  return 0;
}

// release from reset first, as the driver does (50h 04h, then 0Ch)
static int turn_on(struct ess3d_state *s) {
  int err = set(F_3D_RUN, &s->run, 1);

  return err < 0 ? err : set(F_3D_EN, &s->enable, 1);
}

// 0 then 1 on the reset bit, the enable bit stays
static int reset(struct ess3d_state *s) {
  u8 level;
  int err = ess_field_write(F_3D_RUN, 0, 0);

  if (err == 0)
    err = ess_field_write(F_3D_RUN, 1, 0);
  if (err < 0)
    return err;
  s->run = 1;
  // the data sheet only clears 52h at a hardware reset, put the level back
  // if the effect's reset cleared it anyway
  err = ess_field_read(F_3D_LEVEL, &level, 0);
  if (err < 0)
    return err;
  return level == s->level ? 0 : ess_field_write(F_3D_LEVEL, s->level, 0);
}

static int apply(const struct ess3d_action *a, struct ess3d_state *s) {
  int level;

  switch (a->op) {
  case ESS3D_ON:
    return turn_on(s);
  case ESS3D_OFF:
    return set(F_3D_EN, &s->enable, 0);
  case ESS3D_TOGGLE:
    // held in reset counts as off
    if (s->enable && s->run)
      return set(F_3D_EN, &s->enable, 0);
    return turn_on(s);
  case ESS3D_HOLD:
    return set(F_3D_RUN, &s->run, 0);
  case ESS3D_RESET:
    return reset(s);
  case ESS3D_LEVEL:
    return set(F_3D_LEVEL, &s->level, (u8)a->arg);
  case ESS3D_ADD:
    level = s->level + a->arg;
    if (level < 0)
      level = 0;
    if (level > ess3d_level_max())
      level = ess3d_level_max();
    return set(F_3D_LEVEL, &s->level, (u8)level);
  }
  return 0;
}

int ess3d_run(const struct ess3d_cmd *c, struct ess3d_state *s) {
  struct ess3d_state want;
  int i, err;

  memset(&want, 0, sizeof(want));
  err = ess3d_read(&want);
  for (i = 0; err == 0 && i < c->nact; i++)
    err = apply(&c->act[i], &want);
  *s = want;
  if (err < 0)
    return err;
  err = ess3d_read(s);
  if (err < 0)
    return err;
  if (s->enable != want.enable || s->run != want.run || s->level != want.level)
    return ESS3D_MISMATCH;
  return 0;
}

void ess3d_text(const struct ess3d_state *s, char *buf, unsigned size) {
  char tmp[64];

  sprintf(tmp, "3-D %s%s, level %u of %d", s->enable ? "on" : "off",
          s->enable && !s->run ? ", held in reset" : "", s->level,
          ess3d_level_max());
  strncpy(buf, tmp, size - 1);
  buf[size - 1] = 0;
}
