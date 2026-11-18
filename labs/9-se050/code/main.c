#include "gpio.h"
#include "se050.h"
#include "i2c.h"
#include "rpi.h"

void notmain()
{
    uart_init();
    i2c_init();

    delay_ms(100);
    if (se050_AppletSelect() < 0)
    {
        printk("Applet select failed\n");
        clean_reboot();
    }
    if (se050_WriteSymmKey((uint8_t *)"totp", (uint8_t *)"12345678901234567890", 20) < 0)
    {
        printk("Write key failed\n");
        clean_reboot();
    }
    int totp = totp_generate((uint8_t *)"totp", 1111111109);
    if (totp != 81804)
        printk("TOTP generation failed, expected 81804, got %d\n", totp);
    else
        printk("TOTP generation succeeded, got %d\n", totp);

    // test vectors
    printk("%d (exp 287082)\n", totp_generate((uint8_t *)"totp", 59));
    printk("%d (exp 81804)\n", totp_generate((uint8_t *)"totp", 1111111109));
    printk("%d (exp 50471)\n", totp_generate((uint8_t *)"totp", 1111111111));
    printk("%d (exp 5924)\n", totp_generate((uint8_t *)"totp", 1234567890));
    printk("%d (exp 279037)\n", totp_generate((uint8_t *)"totp", 2000000000));
    printk("%d (exp 353130)\n", totp_generate((uint8_t *)"totp", 20000000000));

    if (se050_DeleteSecureObject((uint8_t *)"totp") < 0)
    {
        printk("Delete key failed\n");
        clean_reboot();
    }
    totp = totp_generate((uint8_t *)"totp", 1111111109);
    if (totp != 81804)
        printk("TOTP generation failed (should), since key is gone, res: %d\n", totp);
    else
        printk("TOTP generation succeeded, your key was not deleted, got %d\n", totp);

    clean_reboot();
}
