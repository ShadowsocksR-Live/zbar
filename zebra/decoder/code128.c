/*------------------------------------------------------------------------
 *  Copyright 2007-2008 (c) Jeff Brown <spadix@users.sourceforge.net>
 *
 *  This file is part of the Zebra Barcode Library.
 *
 *  The Zebra Barcode Library is free software; you can redistribute it
 *  and/or modify it under the terms of the GNU Lesser Public License as
 *  published by the Free Software Foundation; either version 2.1 of
 *  the License, or (at your option) any later version.
 *
 *  The Zebra Barcode Library is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser Public License
 *  along with the Zebra Barcode Library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 *  Boston, MA  02110-1301  USA
 *
 *  http://sourceforge.net/projects/zebra
 *------------------------------------------------------------------------*/

#include <config.h>
#include <string.h>     /* memmove */
#include <assert.h>

#include <zebra.h>
#include "decoder.h"

#ifdef DEBUG_CODE128
# define DEBUG_LEVEL (DEBUG_CODE128)
#endif
#include "debug.h"

#define NUM_CHARS 108           /* total number of character codes */

typedef enum code128_char_e {
    FNC3        = 0x60,
    FNC2        = 0x61,
    SHIFT       = 0x62,
    CODE_C      = 0x63,
    CODE_B      = 0x64,
    CODE_A      = 0x65,
    FNC1        = 0x66,
    START_A     = 0x67,
    START_B     = 0x68,
    START_C     = 0x69,
    STOP_FWD    = 0x6a,
    STOP_REV    = 0x6b,
    FNC4        = 0x6c,
} code128_char_t;

static const unsigned char characters[NUM_CHARS] = {
    0x5c, 0xbf, 0xa1,                                           /* [00] 00 */
    0x2a, 0xc5, 0x0c, 0xa4,                                     /* [03] 01 */
    0x2d, 0xe3, 0x0f,                                           /* [07] 02 */
    0x5f, 0xe4,                                                 /* [0a] 03 */

    0x6b, 0xe8, 0x69, 0xa7, 0xe7,                               /* [0c] 10 */
    0xc1, 0x51, 0x1e, 0x83, 0xd9, 0x00, 0x84, 0x1f,             /* [11] 11 */
    0xc7, 0x0d, 0x33, 0x86, 0xb5, 0x0e, 0x15, 0x87,             /* [19] 12 */
    0x10, 0xda, 0x11,                                           /* [21] 13 */

    0x36, 0xe5, 0x18, 0x37,                                     /* [24] 20 */
    0xcc, 0x13, 0x39, 0x89, 0x97, 0x14, 0x1b, 0x8a, 0x3a, 0xbd, /* [28] 21 */
    0xa2, 0x5e, 0x01, 0x85, 0xb0, 0x02, 0xa3,                   /* [32] 22 */
    0xa5, 0x2c, 0x16, 0x88, 0xbc, 0x12, 0xa6,                   /* [39] 23 */

    0x61, 0xe6, 0x56, 0x62,                                     /* [40] 30 */
    0x19, 0xdb, 0x1a,                                           /* [44] 31 */
    0xa8, 0x32, 0x1c, 0x8b, 0xcd, 0x1d, 0xa9,                   /* [47] 32 */
    0xc3, 0x20, 0xc4,                                           /* [4e] 33 */

    0x50, 0x5d, 0xc0,       /* [51] 0014 0025 0034 */
    0x2b, 0xc6,             /* [54] 0134 0143 */
    0x2e,                   /* [56] 0243 */
    0x53, 0x60,             /* [57] 0341 0352 */
    0x31,                   /* [59] 1024 */
    0x52, 0xc2,             /* [5a] 1114 1134 */
    0x34, 0xc8,             /* [5c] 1242 1243 */
    0x55,                   /* [5e] 1441 */

    0x57, 0x3e, 0xce,       /* [5f] 4100 5200 4300 */
    0x3b, 0xc9,             /* [62] 4310 3410 */
    0x6a,                   /* [64] 3420 */
    0x54, 0x4f,             /* [65] 1430 2530 */
    0x38,                   /* [67] 4201 */
    0x58, 0xcb,             /* [68] 4111 4311 */
    0x2f, 0xca,             /* [6a] 2421 3421 */
};

static const unsigned char lo_base[8] = {
    0x00, 0x07, 0x0c, 0x19, 0x24, 0x32, 0x40, 0x47
};

static const unsigned char lo_offset[0x80] = {
    0xff, 0xf0, 0xff, 0x1f, 0xff, 0xf2, 0xff, 0xff,     /* 00 [00] */
    0xff, 0xff, 0xff, 0x3f, 0xf4, 0xf5, 0xff, 0x6f,     /* 01 */
    0xff, 0xff, 0xff, 0xff, 0xf0, 0xf1, 0xff, 0x2f,     /* 02 [07] */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x4f,     /* 03 */
    0xff, 0x0f, 0xf1, 0xf2, 0xff, 0x3f, 0xff, 0xf4,     /* 10 [0c] */
    0xf5, 0xf6, 0xf7, 0x89, 0xff, 0xab, 0xff, 0xfc,     /* 11 */
    0xff, 0xff, 0x0f, 0x1f, 0x23, 0x45, 0xf6, 0x7f,     /* 12 [19] */
    0xff, 0xff, 0xff, 0xff, 0xf8, 0xff, 0xf9, 0xaf,     /* 13 */

    0xf0, 0xf1, 0xff, 0x2f, 0xff, 0xf3, 0xff, 0xff,     /* 20 [24] */
    0x4f, 0x5f, 0x67, 0x89, 0xfa, 0xbf, 0xff, 0xcd,     /* 21 */
    0xf0, 0xf1, 0xf2, 0x3f, 0xf4, 0x56, 0xff, 0xff,     /* 22 [32] */
    0xff, 0xff, 0x7f, 0x8f, 0x9a, 0xff, 0xbc, 0xdf,     /* 23 */
    0x0f, 0x1f, 0xf2, 0xff, 0xff, 0x3f, 0xff, 0xff,     /* 30 [40] */
    0xf4, 0xff, 0xf5, 0x6f, 0xff, 0xff, 0xff, 0xff,     /* 31 */
    0x0f, 0x1f, 0x23, 0xff, 0x45, 0x6f, 0xff, 0xff,     /* 32 [47] */
    0xf7, 0xff, 0xf8, 0x9f, 0xff, 0xff, 0xff, 0xff,     /* 33 */
};

static inline signed char decode_lo (int sig)
{
    unsigned char offset = (((sig >> 1) & 0x01) |
                            ((sig >> 3) & 0x06) |
                            ((sig >> 5) & 0x18) |
                            ((sig >> 7) & 0x60));
    unsigned char idx = lo_offset[offset];
    if(sig & 1)
        idx &= 0xf;
    else
        idx >>= 4;
    if(idx == 0xf)
        return(-1);

    unsigned char base = (sig >> 11) | ((sig >> 9) & 1);
    assert(base < 8);
    idx += lo_base[base];

    assert(idx <= 0x50);
    unsigned char c = characters[idx];
    dprintf(2, " %02x(%x(%02x)/%x(%02x)) => %02x",
            idx, base, lo_base[base], offset, lo_offset[offset],
            (unsigned char)c);
    return(c);
}

static inline signed char decode_hi (int sig)
{
    unsigned char rev = (sig & 0x4400) != 0;
    if(rev)
        sig = (((sig >> 12) & 0x000f) |
               ((sig >>  4) & 0x00f0) |
               ((sig <<  4) & 0x0f00) |
               ((sig << 12) & 0xf000));
    dprintf(2, " rev=%x", rev != 0);

    unsigned char idx;
    switch(sig) {
    case 0x0014: idx = 0x0; break;
    case 0x0025: idx = 0x1; break;
    case 0x0034: idx = 0x2; break;
    case 0x0134: idx = 0x3; break;
    case 0x0143: idx = 0x4; break;
    case 0x0243: idx = 0x5; break;
    case 0x0341: idx = 0x6; break;
    case 0x0352: idx = 0x7; break;
    case 0x1024: idx = 0x8; break;
    case 0x1114: idx = 0x9; break;
    case 0x1134: idx = 0xa; break;
    case 0x1242: idx = 0xb; break;
    case 0x1243: idx = 0xc; break;
    case 0x1441: idx = 0xd; rev = 0; break;
    default: return(-1);
    }
    if(rev)
        idx += 0xe;
    unsigned char c = characters[0x51 + idx];
    dprintf(2, " %02x => %02x", idx, c);
    return(c);
}

static inline unsigned char calc_check (unsigned char c)
{
    if(!(c & 0x80))
        return(0x18);
    c &= 0x7f;
    if(c < 0x3d)
        return((c < 0x30 && c != 0x17) ? 0x10 : 0x20);
    if(c < 0x50)
        return((c == 0x4d) ? 0x20 : 0x10);
    return((c < 0x67) ? 0x20 : 0x10);
}

static inline signed char decode6 (zebra_decoder_t *dcode)
{
    /* build edge signature of character */
    unsigned s = dcode->code128.s6;
    dprintf(2, " s=%d", s);
    if(s < 5)
        return(-1);
    /* calculate similar edge measurements */
    int sig = (get_color(dcode) == ZEBRA_BAR)
        ? ((decode_e(get_width(dcode, 0) + get_width(dcode, 1), s, 11) << 12) |
           (decode_e(get_width(dcode, 1) + get_width(dcode, 2), s, 11) << 8) |
           (decode_e(get_width(dcode, 2) + get_width(dcode, 3), s, 11) << 4) |
           (decode_e(get_width(dcode, 3) + get_width(dcode, 4), s, 11)))
        : ((decode_e(get_width(dcode, 5) + get_width(dcode, 4), s, 11) << 12) |
           (decode_e(get_width(dcode, 4) + get_width(dcode, 3), s, 11) << 8) |
           (decode_e(get_width(dcode, 3) + get_width(dcode, 2), s, 11) << 4) |
           (decode_e(get_width(dcode, 2) + get_width(dcode, 1), s, 11)));
    if(sig < 0)
        return(-1);
    dprintf(2, " sig=%04x", sig);
    /* lookup edge signature */
    signed char c = (sig & 0x4444) ? decode_hi(sig) : decode_lo(sig);
    if(c == -1)
        return(-1);

    /* character validation */
    unsigned bars = (get_color(dcode) == ZEBRA_BAR)
        ? (get_width(dcode, 0) + get_width(dcode, 2) + get_width(dcode, 4))
        : (get_width(dcode, 1) + get_width(dcode, 3) + get_width(dcode, 5));
    bars = bars * 11 * 4 / s;
    unsigned char chk = calc_check(c);
    dprintf(2, " bars=%d chk=%d", bars, chk);
    if(chk - 7 > bars || bars > chk + 7)
        return(-1);

    return(c & 0x7f);
}

static inline unsigned char validate_checksum (zebra_decoder_t *dcode)
{
    code128_decoder_t *dcode128 = &dcode->code128;
    if(dcode128->character < 3)
        return(1);

    /* add in irregularly weighted start character */
    unsigned idx = (dcode128->direction) ? dcode128->character - 1 : 0;
    unsigned sum = dcode->buf[idx];
    if(sum >= 103)
        sum -= 103;

    /* calculate sum in reverse to avoid multiply operations */
    unsigned i, acc = 0;
    for(i = dcode128->character - 3; i; i--) {
        assert(sum < 103);
        idx = (dcode128->direction) ? dcode128->character - 1 - i : i;
        acc += dcode->buf[idx];
        if(acc >= 103)
            acc -= 103;
        assert(acc < 103);
        sum += acc;
        if(sum >= 103)
            sum -= 103;
    }

    /* and compare to check character */
    idx = (dcode128->direction) ? 1 : dcode128->character - 2;
    unsigned char check = dcode->buf[idx];
    dprintf(2, " chk=%02x(%02x)", sum, check);
    unsigned char err = (sum != check);
    if(err)
        dprintf(1, " [checksum error]\n");
    return(err);
}

/* expand and decode character set C */
static inline unsigned postprocess_c (zebra_decoder_t *dcode,
                                      unsigned start,
                                      unsigned end,
                                      unsigned dst)
{
    /* expand buffer to accomodate 2x set C characters (2 digits per-char) */
    unsigned delta = end - start;
    unsigned newlen = dcode->code128.character + delta;
    size_buf(dcode, newlen);

    /* relocate unprocessed data to end of buffer */
    memmove(dcode->buf + start + delta, dcode->buf + start,
            dcode->code128.character - start);
    dcode->code128.character = newlen;

    unsigned i, j;
    for(i = 0, j = dst; i < delta; i++, j += 2) {
        /* convert each set C character into two ASCII digits */
        unsigned char code = dcode->buf[start + delta + i];
        dcode->buf[j] = '0';
        if(code >= 50) {
            code -= 50;
            dcode->buf[j] += 5;
        }
        if(code >= 30) {
            code -= 30;
            dcode->buf[j] += 3;
        }
        if(code >= 20) {
            code -= 20;
            dcode->buf[j] += 2;
        }
        if(code >= 10) {
            code -= 10;
            dcode->buf[j] += 1;
        }
        assert(dcode->buf[j] <= '9');
        assert(code <= 9);
        dcode->buf[j + 1] = '0' + code;
    }
    return(delta);
}

/* resolve scan direction and convert to ASCII */
static inline unsigned char postprocess (zebra_decoder_t *dcode)
{
    code128_decoder_t *dcode128 = &dcode->code128;
    dprintf(2, "\n    postproc len=%d", dcode128->character);
    unsigned i, j;
    unsigned char code = 0;
    if(dcode128->direction) {
        /* reverse buffer */
        dprintf(2, " (rev)");
        for(i = 0; i < dcode128->character / 2; i++) {
            unsigned j = dcode128->character - 1 - i;
            code = dcode->buf[i];
            dcode->buf[i] = dcode->buf[j];
            dcode->buf[j] = code;
        }
        assert(dcode->buf[dcode128->character - 1] == STOP_REV);
    }
    else
        assert(dcode->buf[dcode128->character - 1] == STOP_FWD);

    code = dcode->buf[0];
    assert(code >= START_A && code <= START_C);
    unsigned char charset = code - START_A;
    unsigned cexp = (code == START_C) ? 1 : 0;
    dprintf(2, " start=%c", 'A' + charset);

    for(i = 1, j = 0; i < dcode128->character - 2; i++) {
        unsigned char code = dcode->buf[i];
        assert(!(code & 0x80));

        if((charset & 0x2) && (code < 100))
            /* defer character set C for expansion */
            continue;
        else if(code < 0x60) {
            /* convert character set B to ASCII */
            code = code + 0x20;
            if((!charset || (charset == 0x81)) && (code >= 0x60))
                /* convert character set A to ASCII */
                code -= 0x60;
            dcode->buf[j++] = code;
            if(charset & 0x80)
                charset &= 0x7f;
        }
        else {
            dprintf(2, " %02x", code);
            if(charset & 0x2) {
                /* expand character set C to ASCII */
                assert(cexp);
                unsigned delta = postprocess_c(dcode, cexp, i, j);
                i += delta;
                j += delta * 2;
                cexp = 0;
            }
            if(code < CODE_C) {
                if(code == SHIFT)
                    charset |= 0x80;
                else if(code == FNC2)
                    /* FIXME FNC2 - message append */;
                else if(code == FNC3)
                    /* FIXME FNC3 - initialize */;
            }
            else if(code == FNC1)
                /* FIXME FNC1 - Code 128 subsets or ASCII 0x1d */;
            else if(code >= START_A) {
                dprintf(1, " [truncated]\n");
                return(1);
            }
            else {
                assert(code >= CODE_C && code <= CODE_A);
                unsigned char newset = CODE_A - code;
                assert(newset <= 2);
                if(newset != charset)
                    charset = newset;
                else
                    /* FIXME FNC4 - extended ASCII */;
            }
            if(charset & 0x2)
                cexp = i + 1;
        }
    }
    if(charset & 0x2) {
        assert(cexp);
        j += postprocess_c(dcode, cexp, i, j) * 2;
    }
    dcode->buf[j] = '\0';
    return(0);
}

zebra_symbol_type_t zebra_decode_code128 (zebra_decoder_t *dcode)
{
    code128_decoder_t *dcode128 = &dcode->code128;

    /* update latest character width */
    dcode128->s6 -= get_width(dcode, 6);
    dcode128->s6 += get_width(dcode, 0);

    if(/* process every 6th element of active symbol */
       (dcode128->character >= 0 &&
        (++dcode128->element) != 6) ||
       /* decode color based on direction */
       (get_color(dcode) != dcode128->direction))
        return(0);
    dcode128->element = 0;

    dprintf(2, "      code128[%c%02d+%x]:",
            (dcode128->direction) ? '<' : '>',
            dcode128->character, dcode128->element);

    signed char c = decode6(dcode);
    if(dcode128->character < 0) {
        dprintf(2, " c=%02x", c);
        if(c < START_A || c > STOP_REV || c == STOP_FWD) {
            dprintf(2, " [invalid]\n");
            return(0);
        }
        /* lock shared resources */
        if(get_lock(dcode)) {
            dprintf(2, " [locked]\n");
            dcode128->character = -1;
            return(0);
        }
        /* decoded valid start/stop */
        /* initialize state */
        dcode128->character = 0;
        if(c == STOP_REV) {
            dcode128->direction = ZEBRA_BAR;
            dcode128->element = 7;
        }
        else
            dcode128->direction = ZEBRA_SPACE;
        dprintf(2, " dir=%x [valid start]", dcode128->direction);
    }
    else if((c < 0) ||
            ((dcode128->character >= BUFFER_MIN) &&
             size_buf(dcode, dcode128->character + 1))) {
        dprintf(1, (c < 0) ? " [aborted]\n" : " [overflow]\n");
        dcode->lock = 0;
        dcode128->character = -1;
        return(0);
    }

    assert(dcode->buflen > dcode128->character);
    dcode->buf[dcode128->character++] = c;

    if(dcode128->character > 2 &&
       ((dcode128->direction)
        ? c >= START_A && c <= START_C
        : c == STOP_FWD)) {
        /* FIXME STOP_FWD should check extra bar (and QZ!) */
        zebra_symbol_type_t sym = ZEBRA_CODE128;
        if(validate_checksum(dcode) || postprocess(dcode)) {
            dcode->lock = 0;
            sym = ZEBRA_NONE;
        }
        else
            dprintf(2, " [valid end]\n");
        dcode128->character = -1;
        return(sym);
    }

    dprintf(2, "\n");
    return(0);
}