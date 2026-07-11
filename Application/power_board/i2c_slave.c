/*
 * i2c_slave.c
 *
 *  Created on: 5 Tem 2026
 *      Author: fatih
 */
#include "i2c_slave.h"
#include "main.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c3;

#define MEMORY_SIZE 128

static volatile uint8_t i2c_memory_map[MEMORY_SIZE];

static volatile uint8_t rx_data; //stack'de olamaz, HAL alim callbackten sonra calisiyor
static volatile uint8_t current_reg_addr;
static volatile uint8_t is_first_byte = 0;

static volatile i2c_slave_stats_t s_stats;

/**
 * @brief Adres Eslesme Kesmesi - Paket baslangicinda tetiklenir.
 */
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode)
{
    if (hi2c->Instance == I2C3)
    {
        /* Her yeni adres eslesmesinde durum makinesini resetle.
           Gurultu nedeniyle yarim kalan eski paketlerin bayragi bozmasini engeller. */
        is_first_byte = 1;

        if (TransferDirection == I2C_DIRECTION_TRANSMIT) // Master veri YAZIYOR
        {
            s_stats.addr_write++;
            /* Ilk byte'i (register pointer) al. FIRST_AND_NEXT_FRAME RELOAD'i
               acik tutar: pointer sonrasi gelen veri byte'lari NACK'lenmez,
               donanim re-arm olana kadar SCL'i stretch eder. */
            HAL_I2C_Slave_Seq_Receive_IT(hi2c, (uint8_t*)&rx_data, 1, I2C_FIRST_AND_NEXT_FRAME);
        }
        else // Master veri OKUYOR (Kurgumuzda olmasa bile hata onleme amaciyla eklenmeli)
        {
            s_stats.addr_read++;
            /* Master yanlislikla okuma isterse hattin kilitlenmesini engellemek icin
               mevcut adresteki veriyi hatta bas. */
            if (current_reg_addr < MEMORY_SIZE)
            {
                HAL_I2C_Slave_Seq_Transmit_IT(hi2c, (uint8_t*)&i2c_memory_map[current_reg_addr], 1, I2C_FIRST_FRAME);
                current_reg_addr++;
            }
            else
            {
                static uint8_t dummy_zero = 0x00;
                HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &dummy_zero, 1, I2C_FIRST_FRAME);
            }
        }
    }
}

/**
 * @brief Veri Alma Kesmesi - Donanim hatta 1 byte yakaladiginda tetiklenir.
 */
void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C3)
    {
        s_stats.rx_bytes++;
        if (is_first_byte)
        {
            // Alinan ilk veri Master'in hedefledigi register adresidir.
            current_reg_addr = rx_data;
            is_first_byte = 0;

            // Sonraki byte'i (asil veriyi) dinlemeye devam et
            HAL_I2C_Slave_Seq_Receive_IT(hi2c, (uint8_t*)&rx_data, 1, I2C_NEXT_FRAME);
        }
        else
        {
            if (current_reg_addr < MEMORY_SIZE)
            {
                i2c_memory_map[current_reg_addr] = rx_data;
                current_reg_addr++;
            }

            HAL_I2C_Slave_Seq_Receive_IT(hi2c, (uint8_t*)&rx_data, 1, I2C_NEXT_FRAME);
        }
    }
}

/**
 * @brief Veri Gonderme Kesmesi - Master bizden ardisik okuma yaparsa tetiklenir.
 */
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C3)
    {
        s_stats.tx_bytes++;
        /* Master coklu okuma (Sequential Read) yaparsa adresi otomatik artir */
        if (current_reg_addr < MEMORY_SIZE)
        {
            HAL_I2C_Slave_Seq_Transmit_IT(hi2c, (uint8_t*)&i2c_memory_map[current_reg_addr], 1, I2C_NEXT_FRAME);
            current_reg_addr++;
        }
        else
        {
            static uint8_t dummy_zero = 0x00;
            HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &dummy_zero, 1, I2C_NEXT_FRAME);
        }
    }
}

/**
 * @brief Dinleme Bitis Kesmesi - Master STOP sinyali gonderdiginde tetiklenir.
 */
void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C3)
    {
        s_stats.listen_cplt++;
        // Donanimi bir sonraki pakete hazirlamak icin dinleme modunu yeniden baslat
        HAL_I2C_EnableListen_IT(hi2c);
    }
}

/**
 * @brief Gelismis Donanimsal Hata Yonetimi Kesmesi
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C3) { 
        return; 
    }

    uint32_t err = HAL_I2C_GetError(hi2c);

    s_stats.last_error = err;
    if ((err & HAL_I2C_ERROR_AF) != 0U)   { s_stats.err_af++; }
    if ((err & HAL_I2C_ERROR_BERR) != 0U) { s_stats.err_berr++; }
    if ((err & HAL_I2C_ERROR_ARLO) != 0U) { s_stats.err_arlo++; }
    if ((err & HAL_I2C_ERROR_OVR) != 0U)  { s_stats.err_ovr++; }
    if ((err & ~(uint32_t)(HAL_I2C_ERROR_AF | HAL_I2C_ERROR_BERR
                           | HAL_I2C_ERROR_ARLO | HAL_I2C_ERROR_OVR)) != 0U)
    {
        s_stats.err_other++;
    }

    /* AF normaldir (master read sonu NACK) - dokunma, sadece dinlemeye don. */
    if ((err & (HAL_I2C_ERROR_BERR | HAL_I2C_ERROR_ARLO | HAL_I2C_ERROR_OVR)) != 0U)
    {
        s_stats.recover++;
        /* Tam re-init: HAL state makinesini temizler, boylece EnableListen
           gercekten yeniden silahlanabilir (bare PE toggle State'i READY
           yapmaz -> slave kalici sagir kalir). */
        (void)HAL_I2C_DeInit(hi2c);
        (void)HAL_I2C_Init(hi2c);
        (void)HAL_I2CEx_ConfigAnalogFilter(hi2c, I2C_ANALOGFILTER_ENABLE);
        (void)HAL_I2CEx_ConfigDigitalFilter(hi2c, 0);
    }

    is_first_byte = 1;
    (void)HAL_I2C_EnableListen_IT(hi2c);
}

void i2c_slave_init(void)
{
	HAL_I2C_EnableListen_IT(&hi2c3);
	__HAL_I2C_ENABLE(&hi2c3);
}

uint8_t i2c_slave_read_reg(uint8_t reg_addr)
{
    if (reg_addr >= (uint8_t)MEMORY_SIZE)
    {
        return 0U;
    }

    /* Single-byte load is atomic on Cortex-M; volatile prevents caching. */
    return i2c_memory_map[reg_addr];
}

void i2c_slave_snapshot(uint8_t *p_dst, uint8_t start, uint8_t len)
{
    if (p_dst == NULL)
    {
        return;
    }

    uint8_t copy_len = 0U;
    if (start < (uint8_t)MEMORY_SIZE)
    {
        uint8_t avail = (uint8_t)((uint8_t)MEMORY_SIZE - start);
        copy_len = (len < avail) ? len : avail;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (copy_len > 0U)
    {
        (void)memcpy(p_dst, (const uint8_t *)&i2c_memory_map[start], copy_len);
    }

    __set_PRIMASK(primask);

    if (copy_len < len)
    {
        (void)memset(&p_dst[copy_len], 0, (size_t)(len - copy_len));
    }
}

void i2c_slave_get_stats(i2c_slave_stats_t *p_out)
{
    if (p_out == NULL)
    {
        return;
    }

    /* Copy the counters with interrupts masked so the ISR cannot update
     * them mid-copy; casting away volatile is safe inside the mask. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    (void)memcpy(p_out, (const void *)&s_stats, sizeof(*p_out));
    __set_PRIMASK(primask);
}

void i2c_slave_clear_stats(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    (void)memset((void *)&s_stats, 0, sizeof(s_stats));
    __set_PRIMASK(primask);
}

void i2c_slave_write_block(uint8_t start, const uint8_t *src, uint8_t len)
{
    if (src == NULL)
    {
        return;
    }

    /* Clamp to the register file (out-of-range bytes are dropped). */
    uint8_t copy_len = 0U;
    if (start < (uint8_t)MEMORY_SIZE)
    {
        uint8_t avail = (uint8_t)((uint8_t)MEMORY_SIZE - start);
        copy_len = (len < avail) ? len : avail;
    }

    /* Mask interrupts so the master (via the ISR) cannot read a partially
     * updated block mid-write; casting away volatile is safe here. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (copy_len > 0U)
    {
        (void)memcpy((void *)&i2c_memory_map[start], src, copy_len);
    }
    __set_PRIMASK(primask);
}
