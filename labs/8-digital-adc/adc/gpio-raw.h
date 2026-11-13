enum
{
    GPIO_BASE = 0x20200000, // also GPFSEL0
    gpio_set0 = (GPIO_BASE + 0x1C),
    gpio_clr0 = (GPIO_BASE + 0x28),
    gpio_lev0 = (GPIO_BASE + 0x34),
    GPIO_LEV0 = gpio_lev0,
    gpio_ped0 = (GPIO_BASE + 0x40),
    GPIO_EVENT_DETECT0 = gpio_ped0
};


// duplicate gpio_set_on/gpio_set_off so we can inline to 
// reduce overhead. they have to run in < the delay we are 
// shooting for.
static inline void gpio_set_on_raw(unsigned pin) {
    *((volatile uint32_t *)gpio_set0) = 0x1 << pin;
}
static inline void gpio_set_off_raw(unsigned pin) {
    *((volatile uint32_t *)gpio_clr0) = 0x1 << pin;
}

static inline int gpio_read_raw(unsigned pin) {
    return (*((volatile uint32_t*)gpio_lev0) & (0x1 << pin)) != 0;
}

static inline void gpio_event_clear_raw(unsigned pin) {
    *((volatile uint32_t*)gpio_ped0) = 0x1 << pin;
}

static inline void gpio_set_input_raw(unsigned pin) {
    volatile uint32_t *fsel = (volatile uint32_t *)GPIO_BASE + pin / 10;
    unsigned shift = (pin % 10) * 3;
    *fsel &= ~(0b111u << shift);
}

static inline void gpio_set_output_raw(unsigned pin) {
    volatile uint32_t *fsel = (volatile uint32_t *)GPIO_BASE + pin / 10;
    unsigned shift = (pin % 10) * 3;
    *fsel = (*fsel & ~(0b111u << shift)) | (1u << shift);
}