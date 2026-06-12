/**
 * @file cobs.c
 * @brief COBS encode / decode implementation.
 * @version 1.0.0
 */

#include "cobs.h"

#define COBS_MAX_CODE 0xFFU

bool cobs_encode(const uint8_t *p_input,
                 size_t input_len,
                 uint8_t *p_output,
                 size_t output_size,
                 size_t *p_output_len)
{
    if ((NULL == p_output) || (NULL == p_output_len))
    {
        return false;
    }

    if ((input_len > 0U) && (NULL == p_input))
    {
        return false;
    }

    if (output_size < 1U)
    {
        return false;
    }

    size_t  read_idx  = 0U;
    size_t  write_idx = 1U;
    size_t  code_idx  = 0U;
    uint8_t code      = 1U;

    while (read_idx < input_len)
    {
        if (p_input[read_idx] == 0x00U)
        {
            p_output[code_idx] = code;
            code_idx  = write_idx;
            write_idx++;
            code = 1U;

            if (write_idx > output_size)
            {
                return false;
            }
        }
        else
        {
            if (write_idx >= output_size)
            {
                return false;
            }

            p_output[write_idx] = p_input[read_idx];
            write_idx++;
            code++;

            if (code == COBS_MAX_CODE)
            {
                p_output[code_idx] = code;
                code_idx  = write_idx;
                write_idx++;
                code = 1U;
            }
        }

        read_idx++;
    }

    if (code_idx >= output_size)
    {
        return false;
    }
    p_output[code_idx] = code;

    *p_output_len = write_idx;
    return true;
}

bool cobs_decode(const uint8_t *p_input,
                 size_t input_len,
                 uint8_t *p_output,
                 size_t output_size,
                 size_t *p_output_len)
{
    if ((NULL == p_output) || (NULL == p_output_len))
    {
        return false;
    }

    if ((input_len > 0U) && (NULL == p_input))
    {
        return false;
    }

    size_t read_idx  = 0U;
    size_t write_idx = 0U;

    while (read_idx < input_len)
    {
        uint8_t code = p_input[read_idx];
        read_idx++;

        if (code == 0x00U)
        {
            return false;
        }

        for (uint8_t i = 1U; i < code; i++)
        {
            if (read_idx >= input_len)
            {
                return false;
            }
            if (p_input[read_idx] == 0x00U)
            {
                return false;
            }
            if (write_idx >= output_size)
            {
                return false;
            }
            p_output[write_idx] = p_input[read_idx];
            write_idx++;
            read_idx++;
        }

        if ((code != COBS_MAX_CODE) && (read_idx < input_len))
        {
            if (write_idx >= output_size)
            {
                return false;
            }
            p_output[write_idx] = 0x00U;
            write_idx++;
        }
    }

    *p_output_len = write_idx;
    return true;
}

bool cobs_decode_inplace(uint8_t *p_data,
                         size_t input_len,
                         size_t *p_output_len)
{
    if ((NULL == p_data) || (NULL == p_output_len))
    {
        return false;
    }

    size_t read_idx  = 0U;
    size_t write_idx = 0U;

    while (read_idx < input_len)
    {
        uint8_t code = p_data[read_idx];
        read_idx++;

        if (code == 0x00U)
        {
            return false;
        }

        for (uint8_t i = 1U; i < code; i++)
        {
            if (read_idx >= input_len)
            {
                return false;
            }
            if (p_data[read_idx] == 0x00U)
            {
                return false;
            }
            /* Safe: write_idx < read_idx invariant holds throughout */
            p_data[write_idx] = p_data[read_idx];
            write_idx++;
            read_idx++;
        }

        if ((code != COBS_MAX_CODE) && (read_idx < input_len))
        {
            p_data[write_idx] = 0x00U;
            write_idx++;
        }
    }

    *p_output_len = write_idx;
    return true;
}
