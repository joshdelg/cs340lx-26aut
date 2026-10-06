enum
{
    GPIO_BASE = 0x20200000, // also GPFSEL0
    gpio_set0 = (GPIO_BASE + 0x1C),
    gpio_clr0 = (GPIO_BASE + 0x28),
    gpio_lev0 = (GPIO_BASE + 0x34),
    gpio_ped0 = (GPIO_BASE + 0x40)
};


// duplicate gpio_set_on/gpio_set_off so we can inline to 
// reduce overhead. they have to run in < the delay we are 
// shooting for.
static inline void gpio_set_on_raw(unsigned pin) {
    *((volatile uint32_t *)gpio_set0) = 0x1 << pin;

    // static const uint32_t pin_masks[32] = {
    //     0x00000001, 0x00000002, 0x00000004, 0x00000008,
    //     0x00000010, 0x00000020, 0x00000040, 0x00000080,
    //     0x00000100, 0x00000200, 0x00000400, 0x00000800,
    //     0x00001000, 0x00002000, 0x00004000, 0x00008000,
    //     0x00010000, 0x00020000, 0x00040000, 0x00080000,
    //     0x00100000, 0x00200000, 0x00400000, 0x00800000,
    //     0x01000000, 0x02000000, 0x04000000, 0x08000000,
    //     0x10000000, 0x20000000, 0x40000000, 0x80000000
    // };
    // *((volatile uint32_t *)gpio_set0) = pin_masks[pin];
}
static inline void gpio_set_off_raw(unsigned pin) {

    *((volatile uint32_t *)gpio_clr0) = 0x1 << pin;
    // static const uint32_t pin_masks[32] = {
    //     0x00000001, 0x00000002, 0x00000004, 0x00000008,
    //     0x00000010, 0x00000020, 0x00000040, 0x00000080,
    //     0x00000100, 0x00000200, 0x00000400, 0x00000800,
    //     0x00001000, 0x00002000, 0x00004000, 0x00008000,
    // };
    // *((volatile uint32_t *)gpio_clr0) = pin_masks[pin];
}

static inline int gpio_read_raw(unsigned pin) {
    return (*((volatile uint32_t*)gpio_lev0) & (0x1 << pin)) != 0;
}

static inline void gpio_event_clear_raw(unsigned pin) {
    *((volatile uint32_t*)gpio_ped0) = 0x1 << pin;
}