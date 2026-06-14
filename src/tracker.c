#include "tracker.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "units/unit_registry.h"

const ScaleDef SCALES[NUM_SCALES] = {
    // --- Chromatic ---
    {"CHROM", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    // --- Western diatonic modes ---
    {"MAJOR", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"MINOR", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"DORIAN", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"PHRYG", 7, {0, 1, 3, 5, 7, 8, 10}},
    {"LYDIAN", 7, {0, 2, 4, 6, 7, 9, 11}},
    {"MIXOLY", 7, {0, 2, 4, 5, 7, 9, 10}},
    {"LOCRIAN", 7, {0, 1, 3, 5, 6, 8, 10}},
    // --- Harmonic / melodic variants ---
    {"HM.MIN", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"ML.MIN", 7, {0, 2, 3, 5, 7, 9, 11}},
    {"HM.MAJ", 7, {0, 2, 4, 5, 7, 8, 11}},
    // --- Jazz / altered modes ---
    {"LYD.DOM", 7, {0, 2, 4, 6, 7, 9, 10}},
    {"PHR.DOM", 7, {0, 1, 4, 5, 7, 8, 10}},
    {"LYD.AUG", 7, {0, 2, 4, 6, 8, 9, 11}},
    {"ALTERED", 7, {0, 1, 3, 4, 6, 8, 10}},
    {"UK.DORI", 7, {0, 2, 3, 6, 7, 9, 10}},
    // --- Pentatonic / blues ---
    {"PENT.MJ", 5, {0, 2, 4, 7, 9}},
    {"PENT.MN", 5, {0, 3, 5, 7, 10}},
    {"BLUES", 6, {0, 3, 5, 6, 7, 10}},
    {"MJ.BLUE", 6, {0, 2, 3, 4, 7, 9}},
    {"EGYPT", 5, {0, 2, 5, 7, 10}},
    // --- Symmetric ---
    {"W.TONE", 6, {0, 2, 4, 6, 8, 10}},
    {"DIM.H-W", 8, {0, 1, 3, 4, 6, 7, 9, 10}},
    {"DIM.W-H", 8, {0, 2, 3, 5, 6, 8, 9, 11}},
    {"AUGMENT", 6, {0, 3, 4, 7, 8, 11}},
    {"TRITONE", 6, {0, 1, 4, 6, 7, 10}},
    {"PROMETH", 6, {0, 2, 4, 6, 9, 10}},
    // --- Middle Eastern ---
    {"PERSIAN", 7, {0, 1, 4, 5, 6, 8, 11}},
    {"BYZANT", 7, {0, 1, 4, 5, 7, 8, 11}},
    {"ENIGMA", 7, {0, 1, 4, 6, 8, 10, 11}},
    // --- Japanese ---
    {"HIRAJSH", 5, {0, 2, 3, 7, 8}},
    {"IN", 5, {0, 1, 5, 7, 8}},
    {"INSEN", 5, {0, 1, 5, 7, 10}},
    {"IWATO", 5, {0, 1, 5, 6, 10}},
    {"YO", 5, {0, 2, 5, 7, 9}},
    {"MAN.GON", 5, {0, 3, 5, 8, 10}},
    // --- Hungarian ---
    {"HUNG.MN", 7, {0, 2, 3, 6, 7, 8, 11}},
    {"HUNG.MJ", 7, {0, 3, 4, 6, 7, 9, 10}},
    // --- Neapolitan ---
    {"NAPL.MJ", 7, {0, 1, 3, 5, 7, 9, 11}},
    {"NAPL.MN", 7, {0, 1, 3, 5, 7, 8, 11}},
    // --- Indian ragas ---
    {"TODI", 7, {0, 1, 3, 6, 7, 8, 11}},
    {"MARWA", 7, {0, 1, 4, 6, 7, 9, 11}},
    {"PURVI", 7, {0, 1, 4, 6, 7, 8, 11}},
    // --- 8-tone / bebop ---
    {"SPANISH", 8, {0, 1, 3, 4, 5, 6, 8, 10}},
    {"BEBOP.D", 8, {0, 2, 4, 5, 7, 9, 10, 11}},
};

const char *const SCALE_ROOT_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

uint8_t scale_next_note(uint8_t note, int dir, uint8_t scale_idx, uint8_t root) {
  if (scale_idx == 0) {
    // Chromatic: straight semitone step
    int n = (int)note + dir;
    if (n < 1)
      return 1;
    if (n > 127)
      return 127;
    return (uint8_t)n;
  }
  const ScaleDef *sc = &SCALES[scale_idx];
  int n = (int)note;
  for (int i = 0; i < 128; i++) {
    n += dir;
    if (n < 1)
      return 1;
    if (n > 127)
      return 127;
    int pc = ((n - (int)root) % 12 + 12) % 12;
    for (int j = 0; j < sc->len; j++)
      if (sc->intervals[j] == pc)
        return (uint8_t)n;
  }
  return note;
}

void tracker_inst_set_slot(TrackerInstrument *inst, int slot, const char *unit_id, int inst_idx) {
  if (slot < 0 || slot >= CHAIN_MAX)
    return;
  ChainSlot *s = &inst->chain[slot];
  // Clear file data when switching to a different unit type — old paths are meaningless
  if (strncmp(s->unit_id, unit_id, UNIT_ID_LEN) != 0)
    s->data[0] = '\0';
  strncpy(s->unit_id, unit_id, UNIT_ID_LEN - 1);
  s->unit_id[UNIT_ID_LEN - 1] = '\0';
  s->enabled = true;
  const UnitDef *def = unit_find(unit_id);
  if (def) {
    for (int i = 0; i < UNIT_MAX_PARAMS; i++)
      s->params[i] = def->param_defaults[i];
    if (def->init_params)
      def->init_params(s->params, inst_idx);
  }
}

void tracker_init(TrackerSong *song) {
  memset(song, 0, sizeof(TrackerSong));
  song->song_len = DEFAULT_SONG_LEN;
  memset(song->patterns, TRACKER_EMPTY, sizeof(song->patterns));
  for (int pi = 0; pi < NUM_PATTERNS; pi++) {
    song->pattern_data[pi].len = DEFAULT_PATTERN_STEPS;
    for (int si = 0; si < MAX_PATTERN_STEPS; si++) {
      song->pattern_data[pi].steps[si].fx[0] = TRACKER_EMPTY;
      song->pattern_data[pi].steps[si].fx[1] = TRACKER_EMPTY;
    }
  }
  for (int i = 0; i < NUM_INSTRUMENTS; i++) {
    snprintf(song->instruments[i].name, 16, "INST%02X", i);
    for (int s = 0; s < CHAIN_MAX; s++)
      memset(song->instruments[i].chain[s].cc_map, 0xFF, UNIT_MAX_PARAMS);
  }
  song->bpm = 120;
  song->swing = 0;
  song->loop = true;
  song->name[0] = '\0';
}

void tracker_clear(TrackerSong *song) {
  uint8_t bpm = song->bpm;
  char name[32];
  strncpy(name, song->name, 32);
  tracker_init(song);
  song->bpm = bpm;
  strncpy(song->name, name, 32);
}

#include <stdlib.h>

// ---- path utilities --------------------------------------------------------

static void song_dir(const char *file_path, char *dir, int sz) {
  strncpy(dir, file_path, sz - 1);
  dir[sz - 1] = '\0';
  char *sep = strrchr(dir, '/');
  if (!sep) sep = strrchr(dir, '\\');
  if (sep) *(sep + 1) = '\0';
  else { dir[0] = '.'; dir[1] = '/'; dir[2] = '\0'; }
}

static void path_make_relative(const char *base_dir, const char *abs_path,
                                char *out, int out_sz) {
  int last_sep = 0;
  for (int i = 0; base_dir[i] && abs_path[i]; i++) {
    if (base_dir[i] != abs_path[i]) break;
    if (base_dir[i] == '/') last_sep = i + 1;
  }
  if (last_sep == 0) { strncpy(out, abs_path, out_sz - 1); out[out_sz-1] = '\0'; return; }
  int ups = 0;
  for (int i = last_sep; base_dir[i]; i++) if (base_dir[i] == '/') ups++;
  out[0] = '\0';
  for (int i = 0; i < ups; i++) strncat(out, "../", out_sz - (int)strlen(out) - 1);
  strncat(out, abs_path + last_sep, out_sz - (int)strlen(out) - 1);
}

static void path_resolve(const char *base_dir, const char *rel, char *out, int out_sz) {
  if (rel[0] == '/' || rel[0] == '\\' || (rel[0] && rel[1] == ':'))
    strncpy(out, rel, out_sz - 1);
  else
    snprintf(out, out_sz, "%s%s", base_dir, rel);
  out[out_sz - 1] = '\0';
}

// Rewrite the path portion of a ChainSlot data field (handles CLAP "path\tplugin_id").
static void rewrite_path(char *d, int d_sz, const char *dir,
                         void (*fn)(const char *, const char *, char *, int)) {
  if (!d[0]) return;
  char fpath[512];
  strncpy(fpath, d, sizeof(fpath) - 1); fpath[511] = '\0';
  char *tab = strchr(fpath, '\t');
  if (tab) *tab = '\0';
  char result[512];
  fn(dir, fpath, result, sizeof(result));
  if (tab)
    snprintf(d, d_sz, "%s\t%s", result, tab + 1);
  else
    strncpy(d, result, d_sz - 1);
  d[d_sz - 1] = '\0';
}

// ---- write buffer ----------------------------------------------------------

typedef struct { uint8_t *data; size_t len, cap; } WBuf;

static bool wbuf_ensure(WBuf *b, size_t n) {
  if (b->len + n <= b->cap) return true;
  size_t nc = b->cap ? b->cap * 2 : 65536;
  while (nc < b->len + n) nc *= 2;
  uint8_t *nd = realloc(b->data, nc);
  if (!nd) return false;
  b->data = nd; b->cap = nc; return true;
}
static void wb_u8(WBuf *b, uint8_t v)   { if (wbuf_ensure(b,1)) b->data[b->len++]=v; }
static void wb_u16(WBuf *b, uint16_t v) { wb_u8(b,v&0xFF); wb_u8(b,(v>>8)&0xFF); }
static void wb_u32(WBuf *b, uint32_t v) { wb_u8(b,v&0xFF); wb_u8(b,(v>>8)&0xFF);
                                           wb_u8(b,(v>>16)&0xFF); wb_u8(b,(v>>24)&0xFF); }
static void wb_raw(WBuf *b, const void *src, size_t n) {
  if (wbuf_ensure(b,n)) { memcpy(b->data+b->len,src,n); b->len+=n; }
}
static void wb_strn(WBuf *b, const char *s, int n) {
  int sl = (int)strlen(s); if (sl > n) sl = n;
  if (wbuf_ensure(b,n)) { memcpy(b->data+b->len,s,sl); memset(b->data+b->len+sl,0,n-sl); b->len+=n; }
}
// Start a chunk: write 4-byte tag + placeholder size. Returns offset of size field.
static size_t wb_chunk_start(WBuf *b, const char *tag) {
  wb_raw(b, tag, 4);
  size_t off = b->len;
  wb_u32(b, 0);
  return off;
}
// Patch size field written by wb_chunk_start.
static void wb_chunk_end(WBuf *b, size_t off) {
  uint32_t sz = (uint32_t)(b->len - off - 4);
  b->data[off]   = sz & 0xFF;       b->data[off+1] = (sz>>8)  & 0xFF;
  b->data[off+2] = (sz>>16) & 0xFF; b->data[off+3] = (sz>>24) & 0xFF;
}

// ---- read buffer -----------------------------------------------------------

typedef struct { const uint8_t *p, *end; } RBuf;
static uint8_t  rb_u8 (RBuf *r) { return r->p<r->end ? *r->p++ : 0; }
static uint16_t rb_u16(RBuf *r) {
  if (r->p+2>r->end){r->p=r->end;return 0;}
  uint16_t v=r->p[0]|((uint16_t)r->p[1]<<8); r->p+=2; return v;
}
static uint32_t rb_u32(RBuf *r) {
  if (r->p+4>r->end){r->p=r->end;return 0;}
  uint32_t v=r->p[0]|((uint32_t)r->p[1]<<8)|((uint32_t)r->p[2]<<16)|((uint32_t)r->p[3]<<24);
  r->p+=4; return v;
}
static void rb_raw(RBuf *r, void *dst, int n) {
  int av=(int)(r->end-r->p);
  if (av<n){memcpy(dst,r->p,av<0?0:av);memset((uint8_t*)dst+(av<0?0:av),0,n-(av<0?0:av));r->p=r->end;return;}
  memcpy(dst,r->p,n); r->p+=n;
}
static void rb_strn(RBuf *r, char *dst, int n) { rb_raw(r,dst,n); dst[n-1]='\0'; }

// ---- pattern helpers -------------------------------------------------------

static bool pattern_has_data(const Pattern *p) {
  for (int si = 0; si < p->len; si++) {
    const PatternStep *s = &p->steps[si];
    if (s->note != NOTE_EMPTY) return true;
    for (int fi = 0; fi < FX_PER_STEP; fi++)
      if (s->fx[fi] != TRACKER_EMPTY) return true;
  }
  return false;
}

static bool inst_has_data(const TrackerInstrument *inst, int idx) {
  for (int s = 0; s < CHAIN_MAX; s++)
    if (inst->chain[s].unit_id[0]) return true;
  if (inst->midi_in_device[0]) return true;
  char def[16]; snprintf(def, 16, "INST%02X", idx);
  return strcmp(inst->name, def) != 0;
}

// ---- public API ------------------------------------------------------------

bool tracker_save(const TrackerSong *song, const char *path) {
  char dir[512];
  song_dir(path, dir, sizeof(dir));

  WBuf b = {0};
  // Header: magic + version + num_sections placeholder
  wb_raw(&b, "RPT2", 4);
  wb_u16(&b, 1);              // version
  size_t ns_off = b.len;
  wb_u16(&b, 0);              // num_sections — filled in at end

  uint16_t ns = 0;

  // META
  { size_t co = wb_chunk_start(&b, "META");
    wb_strn(&b, song->name, 32);
    wb_u8(&b, song->bpm); wb_u8(&b, song->swing);
    wb_u8(&b, song->scale_root); wb_u8(&b, song->scale_idx);
    wb_u8(&b, song->loop ? 1 : 0);
    for (int i = 0; i < 3; i++) wb_u8(&b, 0); // padding
    wb_chunk_end(&b, co); ns++; }

  // SONG
  { size_t co = wb_chunk_start(&b, "SONG");
    wb_u16(&b, song->song_len);
    for (int r = 0; r < song->song_len; r++)
      for (int ch = 0; ch < SONG_CHANNELS; ch++)
        wb_u8(&b, song->patterns[ch][r]);
    wb_chunk_end(&b, co); ns++; }

  // PATN — only non-trivial patterns
  { size_t co = wb_chunk_start(&b, "PATN");
    size_t cnt_off = b.len; wb_u16(&b, 0);
    uint16_t cnt = 0;
    for (int pi = 0; pi < NUM_PATTERNS; pi++) {
      const Pattern *p = &song->pattern_data[pi];
      if (!pattern_has_data(p) && p->len == DEFAULT_PATTERN_STEPS) continue;
      wb_u8(&b, (uint8_t)pi);
      wb_u16(&b, p->len);
      for (int si = 0; si < p->len; si++) {
        const PatternStep *s = &p->steps[si];
        wb_u8(&b, s->note); wb_u8(&b, s->velocity); wb_u8(&b, s->instrument);
        wb_u8(&b, s->fx[0]); wb_u8(&b, s->fx[1]);
        wb_u8(&b, s->fxv[0]); wb_u8(&b, s->fxv[1]);
      }
      cnt++;
    }
    b.data[cnt_off]   = cnt & 0xFF;
    b.data[cnt_off+1] = (cnt >> 8) & 0xFF;
    wb_chunk_end(&b, co); ns++; }

  // INST — only non-default instruments
  { size_t co = wb_chunk_start(&b, "INST");
    size_t cnt_off = b.len; wb_u16(&b, 0);
    uint16_t cnt = 0;
    for (int ii = 0; ii < NUM_INSTRUMENTS; ii++) {
      const TrackerInstrument *inst = &song->instruments[ii];
      if (!inst_has_data(inst, ii)) continue;
      wb_u8(&b, (uint8_t)ii);
      wb_strn(&b, inst->name, 16);
      wb_strn(&b, inst->midi_in_device, 128);
      wb_u8(&b, inst->midi_in_channel);
      for (int s = 0; s < CHAIN_MAX; s++) {
        const ChainSlot *sl = &inst->chain[s];
        wb_strn(&b, sl->unit_id, UNIT_ID_LEN);
        wb_u8(&b, sl->enabled ? 1 : 0);
        wb_raw(&b, sl->params, UNIT_MAX_PARAMS);
        wb_raw(&b, sl->cc_map, UNIT_MAX_PARAMS);
        // Relativise data path before writing
        char tmp[240]; strncpy(tmp, sl->data, 239); tmp[239] = '\0';
        if (tmp[0] == '/' || tmp[0] == '\\' || (tmp[0] && tmp[1] == ':'))
          rewrite_path(tmp, 240, dir, path_make_relative);
        uint16_t dl = (uint16_t)strlen(tmp);
        wb_u16(&b, dl);
        if (dl) wb_raw(&b, tmp, dl);
      }
      cnt++;
    }
    b.data[cnt_off]   = cnt & 0xFF;
    b.data[cnt_off+1] = (cnt >> 8) & 0xFF;
    wb_chunk_end(&b, co); ns++; }

  // Patch num_sections
  b.data[ns_off]   = ns & 0xFF;
  b.data[ns_off+1] = (ns >> 8) & 0xFF;

  bool ok = b.data && SaveFileData(path, b.data, (int)b.len);
  free(b.data);
  return ok;
}

bool tracker_load(TrackerSong *song, const char *path) {
  int sz = 0;
  unsigned char *raw = LoadFileData(path, &sz);
  if (!raw || sz < 8) { if (raw) UnloadFileData(raw); return false; }

  char dir[512];
  song_dir(path, dir, sizeof(dir));

  RBuf hdr = { raw, raw + sz };
  uint8_t magic[4]; rb_raw(&hdr, magic, 4);
  if (memcmp(magic, "RPT2", 4) != 0) { UnloadFileData(raw); return false; }

  uint16_t version   = rb_u16(&hdr);  (void)version;
  uint16_t num_sects = rb_u16(&hdr);

  tracker_init(song);

  const uint8_t *cur = hdr.p;
  for (int si = 0; si < num_sects; si++) {
    if (cur + 8 > raw + sz) break;
    char tag[5]; memcpy(tag, cur, 4); tag[4] = '\0';
    uint32_t csz = cur[4]|((uint32_t)cur[5]<<8)|((uint32_t)cur[6]<<16)|((uint32_t)cur[7]<<24);
    const uint8_t *cdata = cur + 8;
    const uint8_t *cnext = cdata + csz;
    if (cnext > raw + sz) cnext = raw + sz;
    RBuf c = { cdata, cnext };

    if (strcmp(tag, "META") == 0) {
      rb_strn(&c, song->name, 32);
      song->bpm = rb_u8(&c); song->swing = rb_u8(&c);
      song->scale_root = rb_u8(&c); song->scale_idx = rb_u8(&c);
      song->loop = rb_u8(&c) & 1;

    } else if (strcmp(tag, "SONG") == 0) {
      uint16_t slen = rb_u16(&c);
      if (slen < 1) slen = 1;
      if (slen > MAX_SONG_LEN) slen = MAX_SONG_LEN;
      song->song_len = slen;
      for (int r = 0; r < slen; r++)
        for (int ch = 0; ch < SONG_CHANNELS; ch++)
          song->patterns[ch][r] = rb_u8(&c);

    } else if (strcmp(tag, "PATN") == 0) {
      uint16_t cnt = rb_u16(&c);
      for (int i = 0; i < cnt; i++) {
        uint8_t pidx = rb_u8(&c);
        uint16_t plen = rb_u16(&c);
        if (plen < 1) plen = 1;
        if (plen > MAX_PATTERN_STEPS) plen = MAX_PATTERN_STEPS;
        Pattern *p = &song->pattern_data[pidx];
        p->len = plen;
        for (int s = 0; s < plen; s++) {
          PatternStep *st = &p->steps[s];
          st->note       = rb_u8(&c);
          st->velocity   = rb_u8(&c);
          st->instrument = rb_u8(&c);
          st->fx[0]      = rb_u8(&c); st->fx[1]  = rb_u8(&c);
          st->fxv[0]     = rb_u8(&c); st->fxv[1] = rb_u8(&c);
        }
      }

    } else if (strcmp(tag, "INST") == 0) {
      uint16_t cnt = rb_u16(&c);
      for (int i = 0; i < cnt; i++) {
        uint8_t iidx = rb_u8(&c);
        TrackerInstrument *inst = &song->instruments[iidx];
        rb_strn(&c, inst->name, 16);
        rb_strn(&c, inst->midi_in_device, 128);
        inst->midi_in_channel = rb_u8(&c);
        for (int s = 0; s < CHAIN_MAX; s++) {
          ChainSlot *sl = &inst->chain[s];
          rb_strn(&c, sl->unit_id, UNIT_ID_LEN);
          sl->enabled = rb_u8(&c) != 0;
          rb_raw(&c, sl->params, UNIT_MAX_PARAMS);
          rb_raw(&c, sl->cc_map, UNIT_MAX_PARAMS);
          uint16_t dl = rb_u16(&c);
          if (dl >= sizeof(sl->data)) dl = (uint16_t)(sizeof(sl->data) - 1);
          rb_raw(&c, sl->data, dl);
          sl->data[dl] = '\0';
          // Resolve relative path
          if (sl->data[0] && sl->data[0] != '/' && sl->data[0] != '\\' &&
              !(sl->data[0] && sl->data[1] == ':'))
            rewrite_path(sl->data, (int)sizeof(sl->data), dir, path_resolve);
        }
      }
    }
    cur = cnext;
  }

  UnloadFileData(raw);
  return true;
}
