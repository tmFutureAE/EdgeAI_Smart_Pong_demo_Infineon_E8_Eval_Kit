#include "text5x7.h"

#include <stddef.h>

#include "platform/display_hal.h"
#include "sw_render.h"

/* Each glyph is 7 rows, 5 bits wide, MSB-first in the low 5 bits (bit 4..0). */
static const uint8_t GLYPH_SPACE[7] = {0, 0, 0, 0, 0, 0, 0};
static const uint8_t GLYPH_COLON[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
static const uint8_t GLYPH_DOT[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04};
static const uint8_t GLYPH_COMMA[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08};
static const uint8_t GLYPH_MINUS[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
static const uint8_t GLYPH_PLUS[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
static const uint8_t GLYPH_SLASH[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
static const uint8_t GLYPH_EQUAL[7] = {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00};
static const uint8_t GLYPH_LPAREN[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
static const uint8_t GLYPH_RPAREN[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
static const uint8_t GLYPH_COPYRIGHT[7] = {0x0E, 0x11, 0x16, 0x14, 0x16, 0x11, 0x0E};
static const uint8_t GLYPH_QMARK[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};

static const uint8_t GLYPH_0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
static const uint8_t GLYPH_1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
static const uint8_t GLYPH_2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
static const uint8_t GLYPH_3[7] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
static const uint8_t GLYPH_4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
static const uint8_t GLYPH_5[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
static const uint8_t GLYPH_6[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
static const uint8_t GLYPH_7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
static const uint8_t GLYPH_8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
static const uint8_t GLYPH_9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};

static const uint8_t GLYPH_A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
static const uint8_t GLYPH_B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
static const uint8_t GLYPH_C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
static const uint8_t GLYPH_D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
static const uint8_t GLYPH_E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
static const uint8_t GLYPH_F[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
static const uint8_t GLYPH_G[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
static const uint8_t GLYPH_H[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
static const uint8_t GLYPH_I[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
static const uint8_t GLYPH_J[7] = {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
static const uint8_t GLYPH_K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
static const uint8_t GLYPH_L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
static const uint8_t GLYPH_M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
static const uint8_t GLYPH_N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
static const uint8_t GLYPH_O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
static const uint8_t GLYPH_P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
static const uint8_t GLYPH_Q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
static const uint8_t GLYPH_R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
static const uint8_t GLYPH_S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
static const uint8_t GLYPH_T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
static const uint8_t GLYPH_U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
static const uint8_t GLYPH_V[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
static const uint8_t GLYPH_W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
static const uint8_t GLYPH_X[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
static const uint8_t GLYPH_Y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
static const uint8_t GLYPH_Z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};

static const uint8_t *edgeai_glyph5x7(char c)
{
    char u = c;
    if (u >= 'a' && u <= 'z') u = (char)(u - ('a' - 'A'));

    switch (u)
    {
        case '0': return GLYPH_0;
        case '1': return GLYPH_1;
        case '2': return GLYPH_2;
        case '3': return GLYPH_3;
        case '4': return GLYPH_4;
        case '5': return GLYPH_5;
        case '6': return GLYPH_6;
        case '7': return GLYPH_7;
        case '8': return GLYPH_8;
        case '9': return GLYPH_9;

        case 'A': return GLYPH_A;
        case 'B': return GLYPH_B;
        case 'C': return GLYPH_C;
        case 'D': return GLYPH_D;
        case 'E': return GLYPH_E;
        case 'F': return GLYPH_F;
        case 'G': return GLYPH_G;
        case 'H': return GLYPH_H;
        case 'I': return GLYPH_I;
        case 'J': return GLYPH_J;
        case 'K': return GLYPH_K;
        case 'L': return GLYPH_L;
        case 'M': return GLYPH_M;
        case 'N': return GLYPH_N;
        case 'O': return GLYPH_O;
        case 'P': return GLYPH_P;
        case 'Q': return GLYPH_Q;
        case 'R': return GLYPH_R;
        case 'S': return GLYPH_S;
        case 'T': return GLYPH_T;
        case 'U': return GLYPH_U;
        case 'V': return GLYPH_V;
        case 'W': return GLYPH_W;
        case 'X': return GLYPH_X;
        case 'Y': return GLYPH_Y;
        case 'Z': return GLYPH_Z;

        case '?': return GLYPH_QMARK;
        case ':': return GLYPH_COLON;
        case '.': return GLYPH_DOT;
        case ',': return GLYPH_COMMA;
        case '-': return GLYPH_MINUS;
        case '+': return GLYPH_PLUS;
        case '/': return GLYPH_SLASH;
        case '=': return GLYPH_EQUAL;
        case '(': return GLYPH_LPAREN;
        case ')': return GLYPH_RPAREN;
        case (char)0xA9: return GLYPH_COPYRIGHT;
        case ' ': return GLYPH_SPACE;
        default: return GLYPH_SPACE;
    }
}

static void edgeai_draw_char5x7_scaled(int32_t x, int32_t y, int32_t scale, char c, uint16_t color)
{
    const uint8_t *g = edgeai_glyph5x7(c);
    if (scale < 1) scale = 1;

    for (int32_t row = 0; row < 7; row++)
    {
        uint8_t bits = g[row];
        for (int32_t col = 0; col < 5; col++)
        {
            if (bits & (1u << (4 - col)))
            {
                int32_t x0 = x + col * scale;
                int32_t y0 = y + row * scale;
                display_hal_fill_rect(x0, y0, x0 + scale - 1, y0 + scale - 1, color);
            }
        }
    }
}

static void edgeai_draw_char5x7_scaled_sw(uint16_t *dst, uint32_t w, uint32_t h,
                                          int32_t tile_x0, int32_t tile_y0,
                                          int32_t x, int32_t y, int32_t scale, char c, uint16_t color)
{
    const uint8_t *g = edgeai_glyph5x7(c);
    if (scale < 1) scale = 1;

    for (int32_t row = 0; row < 7; row++)
    {
        uint8_t bits = g[row];
        for (int32_t col = 0; col < 5; col++)
        {
            if (bits & (1u << (4 - col)))
            {
                int32_t x0 = x + col * scale;
                int32_t y0 = y + row * scale;
                sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x0, y0, x0 + scale - 1, y0 + scale - 1, color);
            }
        }
    }
}

int32_t edgeai_text5x7_width(int32_t scale, const char *s)
{
    if (!s) return 0;
    int32_t n = 0;
    while (s[n]) n++;
    if (n == 0) return 0;
    return n * (5 + 1) * scale - 1 * scale;
}

void edgeai_text5x7_draw_scaled_no_present(int32_t x, int32_t y, int32_t scale, const char *s, uint16_t rgb565)
{
    if (!s) return;
    int32_t cx = x;
    while (*s)
    {
        edgeai_draw_char5x7_scaled(cx, y, scale, *s, rgb565);
        cx += (5 + 1) * scale;
        s++;
    }
}

void edgeai_text5x7_draw_scaled(int32_t x, int32_t y, int32_t scale, const char *s, uint16_t rgb565)
{
    edgeai_text5x7_draw_scaled_no_present(x, y, scale, s, rgb565);
    display_hal_present_frame();
}

void edgeai_text5x7_draw_scaled_sw(uint16_t *dst, uint32_t w, uint32_t h,
                                   int32_t tile_x0, int32_t tile_y0,
                                   int32_t x, int32_t y, int32_t scale,
                                   const char *s, uint16_t rgb565)
{
    if (!dst || !s) return;
    int32_t cx = x;
    while (*s)
    {
        edgeai_draw_char5x7_scaled_sw(dst, w, h, tile_x0, tile_y0, cx, y, scale, *s, rgb565);
        cx += (5 + 1) * scale;
        s++;
    }
}
