#include "se050.h"
#include "i2c.h"
#include "rpi.h"

static void print_hex(const char *name, uint8_t *buf, int len)
{
    printk("%s: ", name);
    for (int i = 0; i < len; i++)
        printk("%x%x", buf[i] >> 4, buf[i] & 0xf);
    printk("\n");
}

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

    uint8_t *client = (uint8_t *)"clnt";
    uint8_t *server = (uint8_t *)"srvr";

    // clear leftovers from a previous run (errors ok)
    se050_DeleteSecureObject(client);
    se050_DeleteSecureObject(server);

    // 1. ephemeral key pairs, generated on chip
    if (se050_GenECKey(client) < 0 || se050_GenECKey(server) < 0)
    {
        printk("Gen EC key failed\n");
        clean_reboot();
    }

    // only public halves leave the chip
    uint8_t client_pub[EC_PUB_LEN], server_pub[EC_PUB_LEN];
    if (se050_ReadPubKey(client, client_pub) < 0 || se050_ReadPubKey(server, server_pub) < 0)
    {
        printk("Read pub key failed\n");
        clean_reboot();
    }
    print_hex("client pub", client_pub, EC_PUB_LEN);
    print_hex("server pub", server_pub, EC_PUB_LEN);

    // 4. ECDH: each side uses own private key + peer's public key
    uint8_t client_secret[EC_SECRET_LEN], server_secret[EC_SECRET_LEN];
    if (se050_ECDH(client, server_pub, client_secret) < 0 || se050_ECDH(server, client_pub, server_secret) < 0)
    {
        printk("ECDH failed\n");
        clean_reboot();
    }
    print_hex("client secret", client_secret, EC_SECRET_LEN);
    print_hex("server secret", server_secret, EC_SECRET_LEN);

    if (memcmp(client_secret, server_secret, EC_SECRET_LEN) == 0)
        printk("ECDH secrets match\n");
    else
        printk("ECDH secrets DIFFER\n");

    se050_DeleteSecureObject(client);
    se050_DeleteSecureObject(server);
    clean_reboot();
}
