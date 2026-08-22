/*
 * serial_win.c
 *
 *  Created on: Aug 22, 2026
 *      Author: fatih
 *
 * Win32 seri port (COM) uygulamasi. Ozellikle RTU USART3 hattina
 * baglanan USB-uart cevirici ile 230400 8N1 konusur.
 */

#include "serial_win.h"
#include <windows.h>
#include <stdio.h>

static HANDLE com_handle = INVALID_HANDLE_VALUE;

int serial_open(const char *port, uint32_t baud)
{
    DCB dcb;
    COMMTIMEOUTS timeouts;
    char path[32];

    (void)snprintf(path, sizeof(path), "\\\\.\\%s", port);

    com_handle = CreateFileA(path,
                             GENERIC_READ | GENERIC_WRITE,
                             0, NULL, OPEN_EXISTING, 0, NULL);
    if (com_handle == INVALID_HANDLE_VALUE)
    {
        (void)fprintf(stderr, "COM acilamadi: %s (hata=%lu)\n",
                      path, (unsigned long)GetLastError());
        return -1;
    }

    (void)memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(com_handle, &dcb) == 0)
    {
        (void)fprintf(stderr, "GetCommState hatasi=%lu\n",
                      (unsigned long)GetLastError());
        CloseHandle(com_handle);
        com_handle = INVALID_HANDLE_VALUE;
        return -1;
    }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = 1;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;   /* karsi taraf RX beslemesi icin */
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = 0;
    dcb.fOutxDsrFlow = 0;
    dcb.fInX = 0;
    dcb.fOutX = 0;

    if (SetCommState(com_handle, &dcb) == 0)
    {
        (void)fprintf(stderr, "SetCommState hatasi=%lu\n",
                      (unsigned long)GetLastError());
        CloseHandle(com_handle);
        com_handle = INVALID_HANDLE_VALUE;
        return -1;
    }

    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutMultiplier = 2;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 2;
    timeouts.WriteTotalTimeoutConstant = 50;
    (void)SetCommTimeouts(com_handle, &timeouts);

    (void)PurgeComm(com_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 0;
}

void serial_close(void)
{
    if (com_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(com_handle);
        com_handle = INVALID_HANDLE_VALUE;
    }
}

int serial_read(uint8_t *buf, size_t max_len)
{
    DWORD got = 0;

    if (com_handle == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
    if (ReadFile(com_handle, buf, (DWORD)max_len, &got, NULL) == 0)
    {
        return -1;
    }
    return (int)got;
}

int serial_write(const uint8_t *buf, size_t len)
{
    DWORD put = 0;

    if (com_handle == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
    if (WriteFile(com_handle, buf, (DWORD)len, &put, NULL) == 0)
    {
        return -1;
    }
    return (int)put;
}

/*** end of file ***/
