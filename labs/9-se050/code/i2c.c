#include <stdint.h>
#include "i2c.h"
#include "gpio.h"
#include "rpi.h"

// NOTE: this I2C driver roughly the same as the course one, but have to handle error retries

#define DEV_ADDR 0x48

// bcm ch 3
enum
{
    BSC_BASE = 0x20804000,
    BSC_C    = BSC_BASE + 0x00,
    BSC_S    = BSC_BASE + 0x04,
    BSC_DLEN = BSC_BASE + 0x08,
    BSC_A    = BSC_BASE + 0x0C,
    BSC_FIFO = BSC_BASE + 0x10,
    BSC_DIV  = BSC_BASE + 0x14,
};

#define C_I2CEN (1 << 15)
#define C_ST    (1 << 7)
#define C_CLEAR (1 << 4)
#define C_READ  (1 << 0)

#define S_CLKT (1 << 9)
#define S_ERR  (1 << 8)
#define S_RXD  (1 << 5)
#define S_TXD  (1 << 4)
#define S_DONE (1 << 1)

void i2c_init()
{
    gpio_set_function(2, GPIO_FUNC_ALT0);
    gpio_set_function(3, GPIO_FUNC_ALT0);
    dev_barrier();

    // Set clock, addr, and enable
    PUT32(BSC_DIV, 2500); // 250MHz / 2500 = 100kHz
    PUT32(BSC_A, DEV_ADDR);
    PUT32(BSC_C, C_I2CEN);
    dev_barrier();
}

int i2c_xfer(int is_read, uint8_t *buf, uint32_t len)
{
    // clear status, set len, start
    dev_barrier();
    PUT32(BSC_S, S_CLKT | S_ERR | S_DONE);
    PUT32(BSC_DLEN, len);
    PUT32(BSC_C, C_I2CEN | C_CLEAR | C_ST | (is_read ? C_READ : 0));

    // move bytes until done or error
    uint32_t i = 0;
    uint32_t start = timer_get_usec();
    uint32_t s;
    while (!((s = GET32(BSC_S)) & S_DONE) || i < len)
    {
        // NAK
        if (s & (S_ERR | S_CLKT))
            break;
        
        // rx: drain fifo, tx: fill fifo
        if (is_read && (s & S_RXD))
            buf[i++] = GET32(BSC_FIFO);
        else if (!is_read && i < len && (s & S_TXD))
            PUT32(BSC_FIFO, buf[i++]);
        
        // 50ms timeout
        if (timer_get_usec() - start > 50000)
            return -1;
    }

    dev_barrier();
    return (s & (S_ERR | S_CLKT)) ? -1 : (int)len;
}

int i2c_xfer_poll(int is_read, uint8_t *buf, uint32_t len)
{
    // retry up to 1000x, 1ms apart
    for (int i = 0; i < 1000; i++)
    {
        if (i2c_xfer(is_read, buf, len) >= 0)
            return len;
        delay_ms(1);
    }
    return -1;
}
