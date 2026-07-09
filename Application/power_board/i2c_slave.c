/*
 * i2c_slave.c
 *
 *  Created on: 5 Tem 2026
 *      Author: fatih
 */
#include "i2c_slave.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c3;

#define MEMORY_SIZE 128

static volatile uint8_t i2c_memory_map[MEMORY_SIZE];

static volatile uint8_t rx_data; //stack'de olamaz, HAL alim callbackten sonra calisiyor
static volatile uint8_t current_reg_addr;
static volatile uint8_t is_first_byte = 0;

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
            // Ilk byte'i (Register adresini) almak uzere hatti ac
            HAL_I2C_Slave_Seq_Receive_IT(hi2c, (uint8_t*)&rx_data, 1, I2C_FIRST_FRAME);
        }
        else // Master veri OKUYOR (Kurgumuzda olmasa bile hata onleme amaciyla eklenmeli)
        {
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
        // Donanimi bir sonraki pakete hazirlamak icin dinleme modunu yeniden baslat
        HAL_I2C_EnableListen_IT(hi2c);
    }
}

/**
 * @brief Gelismis Donanimsal Hata Yonetimi Kesmesi
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C3)
    {
        uint32_t error_code = HAL_I2C_GetError(hi2c);

        /* AF (Acknowledge Failure) hatasi Master okumayi bitirirken NACK gonderdiginde
           dogal olarak olusur. Kritik olan BERR (Bus Error), ARLO (Arbitration Lost) veya OVR (Overrun)
           gibi hat tikanmalarina neden olabilecek donanimsal hatalardir. */
        if (error_code != HAL_I2C_ERROR_AF)
        {
            // Kritik hatada donanimin durum makinesini (State Machine) zorla sifirla
            __HAL_I2C_DISABLE(hi2c);
            is_first_byte = 1;
            __HAL_I2C_ENABLE(hi2c);
        }

        HAL_I2C_EnableListen_IT(hi2c);
    }
}

void i2c_slave_init(void)
{
	HAL_I2C_EnableListen_IT(&hi2c3);
}
