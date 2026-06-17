#include "platform/audio_hal.h"

#include <stddef.h>

#include "cy_pdl.h"
#include "cycfg_peripherals.h"
#include "cy_scb_i2c.h"

typedef struct
{
    bool init_ok;
    uint8_t pending_thunk;
    uint8_t pending_blip;
    uint8_t pending_point;
    uint8_t pending_win_tune;
    bool win_tune_active;
    uint8_t win_tune_step;
    uint32_t win_tune_gap_samples;
    uint32_t phase_q32;
    uint32_t phase_step_q32;
    uint32_t samples_left;
    uint32_t test_samples_until_next_pulse;
    bool test_next_is_high;
    int16_t amp;
    uint8_t volume_percent;
    uint32_t volume_gain_q12;
} audio_state_t;

static audio_state_t s_audio;
extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

/* Conservative rate for simple SFX generation on current TDM config. */
#define AUDIO_SAMPLE_RATE_HZ (16000u)
#define AUDIO_I2C_ADDR_CODEC (0x18u)
#define AUDIO_I2C_TIMEOUT_MS (12u)
#define AUDIO_I2C_RETRIES    (3u)
#define AUDIO_TEST_PATTERN_ENABLED (0u)

static int16_t audio_next_sample(void);
static void audio_fill_tx_fifo(void);
static void audio_tdm_tx_isr(void);
static bool audio_try_start_win_tune_step(void);

static bool audio_codec_reg_write(uint8_t reg, uint8_t val)
{
    for (uint32_t r = 0u; r < AUDIO_I2C_RETRIES; r++)
    {
        cy_en_scb_i2c_status_t rc = Cy_SCB_I2C_MasterSendStart(CYBSP_I2C_CONTROLLER_HW, AUDIO_I2C_ADDR_CODEC,
                                                                CY_SCB_I2C_WRITE_XFER, AUDIO_I2C_TIMEOUT_MS,
                                                                &disp_touch_i2c_controller_context);
        if (rc != CY_SCB_I2C_SUCCESS) continue;

        rc = Cy_SCB_I2C_MasterWriteByte(CYBSP_I2C_CONTROLLER_HW, reg, AUDIO_I2C_TIMEOUT_MS,
                                        &disp_touch_i2c_controller_context);
        if (rc != CY_SCB_I2C_SUCCESS)
        {
            (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, AUDIO_I2C_TIMEOUT_MS,
                                            &disp_touch_i2c_controller_context);
            continue;
        }

        rc = Cy_SCB_I2C_MasterWriteByte(CYBSP_I2C_CONTROLLER_HW, val, AUDIO_I2C_TIMEOUT_MS,
                                        &disp_touch_i2c_controller_context);
        (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, AUDIO_I2C_TIMEOUT_MS,
                                        &disp_touch_i2c_controller_context);
        if (rc == CY_SCB_I2C_SUCCESS) return true;
    }
    return false;
}

static bool audio_codec_init_tlv320dac3100(void)
{
    /* TI TLV320DAC3100 startup script (I2C addr 0x18 / 7-bit). */
    bool ok = true;
    ok &= audio_codec_reg_write(0x00u, 0x00u); /* Page 0 */
    ok &= audio_codec_reg_write(0x01u, 0x01u); /* SW reset */
    Cy_SysLib_Delay(2u);

    ok &= audio_codec_reg_write(0x04u, 0x03u); /* PLL from MCLK, CODEC_CLKIN=PLL */
    ok &= audio_codec_reg_write(0x06u, 0x08u); /* J = 8 */
    ok &= audio_codec_reg_write(0x07u, 0x00u); /* D MSB */
    ok &= audio_codec_reg_write(0x08u, 0x00u); /* D LSB */
    ok &= audio_codec_reg_write(0x05u, 0x91u); /* PLL on, P=1, R=1 */
    Cy_SysLib_Delay(10u);

    ok &= audio_codec_reg_write(0x0Bu, 0x88u); /* NDAC power + /8 */
    ok &= audio_codec_reg_write(0x0Cu, 0x82u); /* MDAC power + /2 */
    ok &= audio_codec_reg_write(0x0Du, 0x00u); /* DOSR MSB */
    ok &= audio_codec_reg_write(0x0Eu, 0x80u); /* DOSR LSB = 128 */
    ok &= audio_codec_reg_write(0x1Bu, 0x00u); /* I2S, 16-bit, slave */
    ok &= audio_codec_reg_write(0x3Cu, 0x0Bu); /* PRB_P11 */
    ok &= audio_codec_reg_write(0x00u, 0x08u); /* Page 8 */
    ok &= audio_codec_reg_write(0x01u, 0x04u); /* Adaptive filter enable */
    ok &= audio_codec_reg_write(0x00u, 0x00u); /* Page 0 */
    ok &= audio_codec_reg_write(0x74u, 0x00u); /* Disable pin volume control */

    ok &= audio_codec_reg_write(0x00u, 0x01u); /* Page 1 */
    ok &= audio_codec_reg_write(0x1Fu, 0x04u); /* Common-mode voltage */
    ok &= audio_codec_reg_write(0x21u, 0x4Eu); /* De-pop settings */
    ok &= audio_codec_reg_write(0x23u, 0x44u); /* DAC route */
    ok &= audio_codec_reg_write(0x28u, 0x06u); /* HPL unmute, 0 dB */
    ok &= audio_codec_reg_write(0x29u, 0x06u); /* HPR unmute, 0 dB */
    ok &= audio_codec_reg_write(0x2Au, 0x1Cu); /* Class-D unmute */
    ok &= audio_codec_reg_write(0x1Fu, 0xC2u); /* Power up HPL/HPR */
    ok &= audio_codec_reg_write(0x20u, 0x86u); /* Power up Class-D */
    ok &= audio_codec_reg_write(0x24u, 0x92u); /* HPL analog volume */
    ok &= audio_codec_reg_write(0x25u, 0x92u); /* HPR analog volume */
    ok &= audio_codec_reg_write(0x26u, 0x92u); /* Class-D analog volume */
    Cy_SysLib_Delay(60u);

    ok &= audio_codec_reg_write(0x00u, 0x00u); /* Page 0 */
    ok &= audio_codec_reg_write(0x3Fu, 0xD4u); /* DAC channels on */
    ok &= audio_codec_reg_write(0x41u, 0xD4u); /* DAC L digital gain */
    ok &= audio_codec_reg_write(0x42u, 0xD4u); /* DAC R digital gain */
    ok &= audio_codec_reg_write(0x40u, 0x00u); /* DAC unmute */

    return ok;
}

static uint32_t audio_phase_step_q32(uint32_t freq_hz)
{
    uint64_t num = ((uint64_t)freq_hz << 32);
    return (uint32_t)(num / (uint64_t)AUDIO_SAMPLE_RATE_HZ);
}

static void audio_start_tone(uint32_t freq_hz, uint32_t duration_ms, int16_t amp)
{
    uint32_t n = (AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000u;
    if (n == 0u) n = 1u;
    s_audio.phase_step_q32 = audio_phase_step_q32(freq_hz);
    s_audio.samples_left = n;
    {
        /* Global +9 dB output lift requested by user. */
        const uint32_t boost_q12 = 11542u; /* round(10^(9/20) * 4096) */
        int64_t v = (int64_t)amp * (int64_t)s_audio.volume_gain_q12;
        v = (v * (int64_t)boost_q12) / (4096ll * 4096ll);
        if (v > 30000) v = 30000;
        if (v < -30000) v = -30000;
        amp = (int16_t)v;
    }
    s_audio.amp = amp;
}

bool audio_hal_init(void)
{
    if (s_audio.init_ok) return true;

    (void)audio_codec_init_tlv320dac3100();

    cy_en_tdm_status_t st = Cy_AudioTDM_Init(CYBSP_TDM_CONTROLLER_0_HW, &CYBSP_TDM_CONTROLLER_0_config);
    if (st != CY_TDM_SUCCESS)
    {
        s_audio.init_ok = false;
        return false;
    }

    Cy_AudioTDM_EnableTx(CYBSP_TDM_CONTROLLER_0_TX_HW);
    /* Prime before activation to avoid immediate underflow. */
    audio_fill_tx_fifo();

    Cy_AudioTDM_ClearTxInterrupt(CYBSP_TDM_CONTROLLER_0_TX_HW,
                                 CY_TDM_INTR_TX_FIFO_TRIGGER | CY_TDM_INTR_TX_FIFO_OVERFLOW |
                                     CY_TDM_INTR_TX_FIFO_UNDERFLOW | CY_TDM_INTR_TX_IF_UNDERFLOW);
    Cy_AudioTDM_SetTxInterruptMask(CYBSP_TDM_CONTROLLER_0_TX_HW,
                                   CY_TDM_INTR_TX_FIFO_TRIGGER | CY_TDM_INTR_TX_FIFO_UNDERFLOW);
    NVIC_SetVector(CYBSP_TDM_CONTROLLER_0_TX_IRQ, (uint32_t)audio_tdm_tx_isr);
    NVIC_ClearPendingIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);
    NVIC_EnableIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);

    Cy_AudioTDM_ActivateTx(CYBSP_TDM_CONTROLLER_0_TX_HW);

    s_audio.test_samples_until_next_pulse = 1u;
    s_audio.test_next_is_high = false;
    s_audio.volume_percent = 60u;
    s_audio.volume_gain_q12 = 4096u;
    s_audio.init_ok = true;
    return true;
}

void audio_hal_set_volume(uint8_t percent)
{
    if (percent > 100u) percent = 100u;
    s_audio.volume_percent = percent;

    if (percent == 0u)
    {
        s_audio.volume_gain_q12 = 0u;
        return;
    }

    /* 0..100 scale, with 50% as reference (1.0x),
     * and 100% approximately +18 dB (~7.94x) above that reference.
     */
    if (percent <= 50u)
    {
        s_audio.volume_gain_q12 = (uint16_t)(((uint32_t)percent * 4096u) / 50u);
    }
    else
    {
        const uint32_t max_gain_q12 = 32535u; /* round(7.943 * 4096) */
        s_audio.volume_gain_q12 = (uint16_t)(4096u + (((uint32_t)(percent - 50u) * (max_gain_q12 - 4096u)) / 50u));
    }
}

void audio_hal_queue_wall_bounce(uint8_t count)
{
    uint16_t sum = (uint16_t)s_audio.pending_thunk + (uint16_t)count;
    s_audio.pending_thunk = (sum > 255u) ? 255u : (uint8_t)sum;
}

void audio_hal_queue_paddle_hit(uint8_t count)
{
    uint16_t sum = (uint16_t)s_audio.pending_blip + (uint16_t)count;
    s_audio.pending_blip = (sum > 255u) ? 255u : (uint8_t)sum;
}

void audio_hal_queue_point_scored(uint8_t count)
{
    uint16_t sum = (uint16_t)s_audio.pending_point + (uint16_t)count;
    s_audio.pending_point = (sum > 255u) ? 255u : (uint8_t)sum;
}

void audio_hal_queue_win_tune(void)
{
    s_audio.pending_win_tune = 1u;
}

void audio_hal_update(void)
{
    if (!s_audio.init_ok) return;

    audio_fill_tx_fifo();
}

static int16_t audio_next_sample(void)
{
    if (s_audio.samples_left == 0u)
    {
        if (s_audio.win_tune_gap_samples == 0u)
        {
            if (audio_try_start_win_tune_step())
            {
                /* Started next win-tune note. */
            }
            else if (s_audio.pending_win_tune > 0u)
            {
                s_audio.pending_win_tune = 0u;
                s_audio.win_tune_active = true;
                s_audio.win_tune_step = 0u;
                (void)audio_try_start_win_tune_step();
            }
        }

#if AUDIO_TEST_PATTERN_ENABLED
        if (s_audio.test_samples_until_next_pulse == 0u)
        {
            if (s_audio.test_next_is_high)
            {
                audio_start_tone(800u, 120u, 2500);
            }
            else
            {
                audio_start_tone(400u, 120u, 2500);
            }
            s_audio.test_next_is_high = !s_audio.test_next_is_high;
            s_audio.test_samples_until_next_pulse = AUDIO_SAMPLE_RATE_HZ;
        }
#else
        if (!s_audio.win_tune_active && (s_audio.pending_point > 0u))
        {
            s_audio.pending_point--;
            /* Point scored: highest pitch, longest duration. */
            audio_start_tone(490u, 140u, 1800);
        }
        else if (!s_audio.win_tune_active && (s_audio.pending_blip > 0u))
        {
            s_audio.pending_blip--;
            /* Paddle hit: medium pitch, medium duration. */
            audio_start_tone(459u, 90u, 1700);
        }
        else if (!s_audio.win_tune_active && (s_audio.pending_thunk > 0u))
        {
            s_audio.pending_thunk--;
            /* Wall bounce: low pitch, short duration. */
            audio_start_tone(226u, 40u, 1600);
        }
#endif
    }

    int16_t s = 0;
    if (s_audio.samples_left > 0u)
    {
        s = (s_audio.phase_q32 & 0x80000000u) ? s_audio.amp : (int16_t)(-s_audio.amp);
        s_audio.phase_q32 += s_audio.phase_step_q32;
        s_audio.samples_left--;
    }
    if (s_audio.test_samples_until_next_pulse > 0u)
    {
        s_audio.test_samples_until_next_pulse--;
    }
    if (s_audio.win_tune_gap_samples > 0u)
    {
        s_audio.win_tune_gap_samples--;
    }
    return s;
}

static bool audio_try_start_win_tune_step(void)
{
    static const uint16_t k_win_freq_hz[] = { 523u, 659u, 784u, 1047u };
    static const uint16_t k_win_dur_ms[] = { 70u, 70u, 90u, 170u };
    static const uint16_t k_win_gap_ms[] = { 20u, 20u, 24u, 0u };

    if (!s_audio.win_tune_active) return false;
    if (s_audio.samples_left > 0u) return false;
    if (s_audio.win_tune_gap_samples > 0u) return false;

    if (s_audio.win_tune_step >= (uint8_t)(sizeof(k_win_freq_hz) / sizeof(k_win_freq_hz[0])))
    {
        s_audio.win_tune_active = false;
        s_audio.win_tune_step = 0u;
        return false;
    }

    audio_start_tone((uint32_t)k_win_freq_hz[s_audio.win_tune_step],
                     (uint32_t)k_win_dur_ms[s_audio.win_tune_step],
                     1900);
    s_audio.win_tune_gap_samples = (AUDIO_SAMPLE_RATE_HZ * (uint32_t)k_win_gap_ms[s_audio.win_tune_step]) / 1000u;
    s_audio.win_tune_step++;
    return true;
}

static void audio_fill_tx_fifo(void)
{
    /* Keep FIFO fed; two writes per stereo sample (L,R). */
    while (Cy_AudioTDM_GetNumInTxFifo(CYBSP_TDM_CONTROLLER_0_TX_HW) < 120u)
    {
        int16_t s = audio_next_sample();

        uint16_t u = (uint16_t)s;
        Cy_AudioTDM_WriteTxData(CYBSP_TDM_CONTROLLER_0_TX_HW, (uint32_t)u);
        Cy_AudioTDM_WriteTxData(CYBSP_TDM_CONTROLLER_0_TX_HW, (uint32_t)u);
    }
}

static void audio_tdm_tx_isr(void)
{
    uint32_t st = Cy_AudioTDM_GetTxInterruptStatusMasked(CYBSP_TDM_CONTROLLER_0_TX_HW);
    if (st != 0u)
    {
        Cy_AudioTDM_ClearTxInterrupt(CYBSP_TDM_CONTROLLER_0_TX_HW, st);
        audio_fill_tx_fifo();
    }
}
