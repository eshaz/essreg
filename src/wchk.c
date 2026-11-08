/*
 * esswchk's measures and verdicts, see wchk.h.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wchk.h"

#define PI 3.14159265358979

const char *const wchk_rname[5] = {"PASS", "FAIL", "KNOWN", "INFO", "SKIP"};

// --- measures ----------------------------------------------------------------

void wchk_fit_add(struct wchk_fit *f, double t, double m) {
  f->n += 1;
  f->t += t;
  f->m += m;
  f->tt += t * t;
  f->tm += t * m;
}

double wchk_fit_slope(const struct wchk_fit *f) {
  double d = f->n * f->tt - f->t * f->t;

  return f->n < 3 || d <= 0 ? 0 : (f->n * f->tm - f->t * f->m) / d;
}

void wchk_sides_add(struct wchk_sides *w, const s16 *pcm, u16 frames) {
  u16 i;
  int k;
  double x;

  for (i = 0; i < frames; i++)
    for (k = 0; k < 2; k++) {
      x = pcm[2 * i + k];
      w->s[k] += x;
      w->ss[k] += x * x;
    }
  w->frames += frames;
}

double wchk_side_db(const struct wchk_sides *w, int side) {
  double m, p;

  if (!w->frames)
    return -120;
  m = w->s[side] / w->frames;
  p = (w->ss[side] / w->frames - m * m) / (32768.0 * 32768.0 / 2);
  return p > 1e-12 ? 10 * log10(p) : -120;
}

void wchk_pos_init(struct wchk_pos *p) { memset(p, 0, sizeof(*p)); }

void wchk_pos_add(struct wchk_pos *p, u32 pos, u32 ms, double bytes_per_ms) {
  u32 d, dt;

  if (p->have) {
    if (pos < p->last) {
      d = p->last - pos;
      p->back++;
      if (d > p->worst_back)
        p->worst_back = d;
    } else {
      d = pos - p->last;
      dt = ms - p->last_ms;
      if (d > bytes_per_ms * (dt + 2.0) + 1024) {
        p->over++;
        if (d > p->worst_over) {
          p->worst_over = d;
          p->worst_ms = dt;
        }
      }
    }
  }
  p->have = 1;
  p->last = pos;
  p->last_ms = ms;
  p->n++;
}

u32 wchk_ring(u16 most) {
  u32 r = 1;

  while (r <= most)
    r <<= 1;
  return r;
}

u16 wchk_count_pos(u16 count, u32 ring) {
  long p = (long)ring - 1 - (long)count;

  p %= (long)ring;
  return (u16)(p < 0 ? p + (long)ring : p);
}

void wchk_sbt_init(struct wchk_sbt *s) { memset(s, 0, sizeof(*s)); }

void wchk_sbt_line(struct wchk_sbt *s, const char *line) {
  unsigned v[5];
  size_t n;

  if (sscanf(line, "pass=%u fail=%u known=%u info=%u skip=%u", &v[0], &v[1],
             &v[2], &v[3], &v[4]) == 5) {
    s->found = 1;
    s->pass = v[0];
    s->fail = v[1];
    s->known = v[2];
    s->info = v[3];
    s->skip = v[4];
  } else if (!strncmp(line, "ctl.poll16 ", 11) && !s->poll16[0]) {
    strncpy(s->poll16, line, sizeof(s->poll16) - 1);
    n = strcspn(s->poll16, "\r\n");
    s->poll16[n] = 0;
  }
}

// --- verdicts ----------------------------------------------------------------

int wchk_count(const struct tears *t, int fixed, char *text) {
  u32 torn = tears_torn(t);
  int n = sprintf(text,
                  "%lu readings, %lu torn (%lu near 128, %lu near 256, %lu "
                  "more), the worst %ld off, %lu steps back",
                  (unsigned long)t->readings, (unsigned long)torn,
                  (unsigned long)t->torn128, (unsigned long)t->torn256,
                  (unsigned long)t->torn_other, (long)t->worst,
                  (unsigned long)t->back);

  if (!fixed) {
    if (torn)
      sprintf(text + n, "; one in %lu, as ESS's VxD reads it",
              (unsigned long)(t->readings / torn));
    else
      strcpy(text + n, "; as ESS's VxD reads it");
    return WCHK_INFO;
  }
  if (!t->readings) {
    strcpy(text + n, "; nothing read");
    return WCHK_FAIL;
  }
  return torn || t->back ? WCHK_FAIL : WCHK_PASS;
}

int wchk_position(const struct wchk_pos *p, u32 ms, char *text) {
  sprintf(text,
          "%lu positions over %lu ms: %lu steps back (the worst %lu bytes), "
          "%lu steps past the rate (the worst %lu bytes in %lu ms)",
          (unsigned long)p->n, (unsigned long)ms, (unsigned long)p->back,
          (unsigned long)p->worst_back, (unsigned long)p->over,
          (unsigned long)p->worst_over, (unsigned long)p->worst_ms);
  if (p->n < 2)
    return WCHK_FAIL;
  return p->back || p->over ? WCHK_FAIL : WCHK_PASS;
}

int wchk_drain(const u32 *last_ms, int n, char *text) {
  u32 first = 0;
  int i, k;

  k = sprintf(text, "the last header back");
  for (i = 0; i < n; i++) {
    k += sprintf(text + k, i ? ", %lu" : " %lu", (unsigned long)last_ms[i]);
    if (last_ms[i] && (!first || last_ms[i] < first))
      first = last_ms[i];
  }
  k += sprintf(text + k,
               " ms after the first write; the DAC plays its end at %u ms",
               WCHK_DRAIN_MS);
  if (!first) {
    strcpy(text + k, "; it never came back");
    return WCHK_FAIL;
  }
  if (first < WCHK_DRAIN_MS) {
    strcpy(text + k, "; back before the DAC played it, at the DMA's end");
    return WCHK_FAIL;
  }
  if (first > WCHK_DRAIN_SLOW) {
    sprintf(text + k, "; later than %u ms", WCHK_DRAIN_SLOW);
    return WCHK_INFO;
  }
  return WCHK_PASS;
}

int wchk_fm_side(double left_db, double right_db, char want, int true_sides,
                 char *text) {
  double on = want == 'l' ? left_db : right_db;
  double off = want == 'l' ? right_db : left_db;
  int n = sprintf(text, "left %.1f, right %.1f dBFS, panned %s; TrueSides %s",
                  left_db, right_db, want == 'l' ? "left" : "right",
                  true_sides ? "on" : "off");

  if (on < -60 && off < -60) {
    strcpy(text + n, "; no FM in the recording");
    return WCHK_FAIL;
  }
  if (on - off >= 20)
    return WCHK_PASS;
  if (off - on >= 20) {
    strcpy(text + n, "; on the other side");
    return true_sides ? WCHK_FAIL : WCHK_KNOWN;
  }
  strcpy(text + n, "; on both sides");
  return WCHK_FAIL;
}

int wchk_fm_rate(double hz, double secs, char *text) {
  double ppm = (hz / WCHK_FM_HZ - 1) * 1e6;

  sprintf(text,
          "%.2f Hz over %.0f s, %+.0f ppm from %.1f Hz (the data sheet's "
          "49716 Hz is %+.0f ppm)",
          hz, secs, hz ? ppm : 0, WCHK_FM_HZ, (49716.0 / WCHK_FM_HZ - 1) * 1e6);
  if (!hz)
    return WCHK_FAIL;
  return fabs(ppm) <= 500 ? WCHK_PASS : WCHK_FAIL;
}

int wchk_sbt_verdict(const struct wchk_sbt *s, char *text) {
  int n;

  if (!s->found) {
    strcpy(text, "sb16test's report has no summary");
    return WCHK_FAIL;
  }
  n = sprintf(text, "sb16test in a DOS box: pass=%u fail=%u known=%u info=%u",
              s->pass, s->fail, s->known, s->info);
  if (s->poll16[0])
    sprintf(text + n, "; %.*s", (int)(WCHK_TEXT - n - 3), s->poll16);
  return s->fail ? WCHK_FAIL : WCHK_PASS;
}

// --- /sim --------------------------------------------------------------------

u16 wchk_sim_count(u32 us, double rate, u16 ring, u32 i, int old) {
  u32 moved = (u32)(us / 1e6 * rate) % ring;
  u16 count = (u16)(ring - 1 - moved);

  if (old && i % 300 == 150 && count >= 0x110)
    count = (u16)(count - 0x100);
  return count;
}

u32 wchk_sim_pos(u32 us, double bytes_per_ms, u32 i, int old) {
  u32 pos = (u32)(us / 1000.0 * bytes_per_ms);

  return old && i % 50 == 25 ? pos + 256 : pos;
}

u32 wchk_sim_drain(int run, int old) { return (old ? 468 : 531) + run; }

void wchk_sim_fm(s16 *pcm, u16 frames, u32 at, char side, int old) {
  u16 i;
  int k = (side == 'l') != (old != 0) ? 0 : 1;

  for (i = 0; i < frames; i++) {
    pcm[2 * i + k] =
        (s16)(8000 * sin(2 * PI * 440.0 * (double)(at + i) / WCHK_FM_HZ));
    pcm[2 * i + 1 - k] = 0;
  }
}
