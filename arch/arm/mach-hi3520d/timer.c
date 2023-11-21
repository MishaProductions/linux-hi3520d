#include <asm/mach/time.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/sched_clock.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <mach/io.h>
#include <mach/irqs.h>
#include <mach/time.h>
#include <mach/platform.h>
#include <mach/early-debug.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
extern void early_print(const char *str, ...);

static unsigned long free_timer_overflows;


static unsigned long hi3520d_timer_reload, timer0_clk_hz, timer0_clk_khz;
static unsigned long timer1_clk_hz, timer1_clk_khz;

#define HW_REG(a) readl((void*)a)
#define A9_AXI_SCALE_REG   IO_ADDRESS(CRG_REG_BASE + 0x28)
#define TXIN_OSC_FRE       24000000
#define REG_CRG0_OFFSET    IO_ADDRESS(CRG_REG_BASE + 0x0)
#define REG_CRG1_OFFSET    IO_ADDRESS(CRG_REG_BASE + 0x4)
#define REG_PERI_CRG57     IO_ADDRESS(CRG_REG_BASE + 0xe4)
#define get_bus_clk()({\
	unsigned long fbdiv, pstdiv1, pstdiv2, refdiv;\
	unsigned long tmp_reg, foutvco, busclk;\
	tmp_reg = HW_REG(REG_CRG0_OFFSET);\
	pstdiv1 = (tmp_reg >> 24) & 0x7;\
	pstdiv2 = (tmp_reg >> 27) & 0x7;\
	tmp_reg = HW_REG(REG_CRG1_OFFSET);\
	refdiv = (tmp_reg >> 12) & 0x3f;\
	fbdiv = tmp_reg & 0xfff;\
	foutvco = TXIN_OSC_FRE / refdiv;\
	foutvco *= fbdiv;\
	tmp_reg = HW_REG(A9_AXI_SCALE_REG);\
	if ((tmp_reg & 0xc) == 0xc)\
		busclk = foutvco / 2;\
	else\
		busclk = foutvco / 4;\
	busclk;\
})
void timer_preinit(void)
{
    unsigned long busclk;

	edb_trace();
	busclk = get_bus_clk(); //75000000

	hi3520d_timer_reload = BUSCLK_TO_TIMER_RELOAD(busclk);
	timer0_clk_hz = BUSCLK_TO_TIMER0_CLK_HZ(busclk);
	timer0_clk_khz = BUSCLK_TO_TIMER0_CLK_KHZ(busclk);
	timer1_clk_hz = BUSCLK_TO_TIMER1_CLK_HZ(busclk);
	timer1_clk_khz = BUSCLK_TO_TIMER1_CLK_KHZ(busclk);
}

static u64 notrace hi3520d_read_sched_clock(void)
{
	u32 val = ~readl((void*)(CFG_TIMER_VABASE + REG_TIMER1_VALUE));
	return (u64)val;
}

unsigned long long notrace sched_clock2(void)
{
	return ~readl((void*)(CFG_TIMER_VABASE + REG_TIMER1_VALUE));
}
static struct clocksource hi3520d_clocksource;

unsigned long long hi_sched_clock(void)
{
	return sched_clock2();
}
EXPORT_SYMBOL(hi_sched_clock);


// to disable timer:
//unsigned long ctrl;
//ctrl = readl((void*)(CFG_TIMER_VABASE + REG_TIMER_CONTROL));
//ctrl &= ~CFG_TIMER_ENABLE;
//writel(ctrl, (void*)(CFG_TIMER_VABASE + REG_TIMER_CONTROL));


static int timer_set_oneshot(struct clock_event_device *evt)
{
	writel((CFG_TIMER_32BIT | CFG_TIMER_ONESHOT), (void*)(CFG_TIMER_VABASE + REG_TIMER_CONTROL));
	return 0;
}

static int timer_state_periodic(struct clock_event_device *evt)
{
	writel(0, (void*)(CFG_TIMER_VABASE + REG_TIMER_CONTROL));
		writel(hi3520d_timer_reload,
			(void*)(CFG_TIMER_VABASE + REG_TIMER_RELOAD));
		writel(CFG_TIMER_CONTROL,
			(void*)(CFG_TIMER_VABASE + REG_TIMER_CONTROL));
		edb_trace();
	return 0;
}


static int timer_set_next_event(unsigned long evt,
		struct clock_event_device *unused)
{
	unsigned long ctrl;
	ctrl = readl((void*)(CFG_TIMER_VABASE + REG_TIMER_CONTROL));
	ctrl &= ~(CFG_TIMER_ENABLE | CFG_TIMER_INTMASK);
	writel(ctrl, (void*)(CFG_TIMER_VABASE + REG_TIMER_CONTROL));
	writel(evt, (void*)(CFG_TIMER_VABASE + REG_TIMER_RELOAD));
	writel(CFG_TIMER_ONE_CONTROL, (void*)(CFG_TIMER_VABASE + REG_TIMER_CONTROL));

	return 0;
}

static struct clock_event_device timer0_clockevent = {
	.name           = "timer0",
	.shift          = 32,
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_state_oneshot	= timer_set_oneshot,
	.set_next_event = timer_set_next_event,
	.set_state_periodic = timer_state_periodic,
};

/*
 * IRQ handler for the timer
 */
static irqreturn_t hi3520d_timer_interrupt(int irq, void *dev_id)
{
	if ((readl((void*)CFG_TIMER_VABASE+REG_TIMER_RIS)) & 0x1) {
		writel(~0, (void*)(CFG_TIMER_VABASE + REG_TIMER_INTCLR));
		timer0_clockevent.event_handler(&timer0_clockevent);
	}

	if ((readl((void*)(CFG_TIMER_VABASE+REG_TIMER1_RIS))) & 0x1) {
		free_timer_overflows++;
		writel(~0, (void*)(CFG_TIMER_VABASE + REG_TIMER1_INTCLR));
	}

	return IRQ_HANDLED;
}


static struct irqaction hi3520d_timer_irq = {
	.name           = "System Timer Tick",
	.flags          = IRQF_SHARED | IRQF_TIMER, // removed IRQF_DISABLED
	.handler        = hi3520d_timer_interrupt,
};

static cycle_t hi3520d_get_cycles(struct clocksource *cs)
{
	return ~readl((void*)(CFG_TIMER_VABASE + REG_TIMER1_VALUE));
}

static struct clocksource hi3520d_clocksource = {
	.name           = "timer1",
	.rating         = 200,
	.read           = hi3520d_get_cycles,
	.mask           = CLOCKSOURCE_MASK(32),
	.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init hi3520d_clocksource_init(void)
{
	unsigned long rate = timer1_clk_khz * 1000;
	writel(0, (void*)(CFG_TIMER_VABASE + REG_TIMER1_CONTROL));
	writel(0xffffffff, (void*)(CFG_TIMER_VABASE + REG_TIMER1_RELOAD));
	writel(0xffffffff, (void*)(CFG_TIMER_VABASE + REG_TIMER1_VALUE));
	writel(CFG_TIMER_CONTROL, (void*)(CFG_TIMER_VABASE + REG_TIMER1_CONTROL));

	/* caculate the mult/shift by clock rate to gain more accratury */
	if (clocksource_register_hz(&hi3520d_clocksource, rate))
		panic("register clocksouce :%s error\n",
				hi3520d_clocksource.name);

	/* force check the mult/shift of clocksource */
	//init_fixed_sched_clock(&cd, hi3520d_update_sched_clock, 32, rate,
	//		hi3520d_clocksource.mult, hi3520d_clocksource.shift);

	//setup_sched_clock(hi3520d_read_sched_clock, 32, rate, hi3520d_clocksource.mult, hi3520d_clocksource.shift);
	sched_clock_register(hi3520d_read_sched_clock, 32, rate);

	return 0;
}


static struct clk *clk[2];
static struct clk_onecell_data clk_data;

void __init hi3520d_timerdev_init(struct device_node *np)
{
    unsigned long reg = 0, busclk = 0, uartclk;
    timer_preinit();
    edb_trace();

	int irq = irq_of_parse_and_map(np, 0);

	if (irq < 0)
	{
		early_print("failed to get timer IRQ: %d\n", irq);
	}

	early_print("timer IRQ: %d\n", irq);
	
    setup_irq(irq, &hi3520d_timer_irq);

	hi3520d_clocksource_init();
	timer0_clockevent.mult =
		div_sc(timer0_clk_hz, NSEC_PER_SEC, timer0_clockevent.shift);
	timer0_clockevent.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &timer0_clockevent);
	timer0_clockevent.min_delta_ns =
		clockevent_delta2ns(0xf, &timer0_clockevent);

	timer0_clockevent.cpumask = cpumask_of(0);
	clockevents_register_device(&timer0_clockevent);


    /* hi3520d uart use apb bus clk */
	reg = readl((void*)(REG_PERI_CRG57));
	reg &= ~UART_CKSEL_APB;
	writel(reg, (void*)(REG_PERI_CRG57));

	busclk = get_bus_clk();
	uartclk = busclk / 4;
    early_print("busclock: %d\n", busclk);
    early_print("uart clock: %d\n", uartclk);
    clk[0] = clk_register_fixed_rate(NULL, "uart:0", NULL, 0, uartclk);
    clk[1] = clk_register_fixed_rate(NULL, "uart:1", NULL, 0, uartclk);
    
    // register clk device
    clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, NULL);
}
CLK_OF_DECLARE(hi3520d_clock, "hisi,hi3520d-clock", hi3520d_timerdev_init);