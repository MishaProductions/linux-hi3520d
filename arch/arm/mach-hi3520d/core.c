#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/cnt32_to_63.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/sched_clock.h>
#include <asm/smp_twd.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <mach/time.h>
#include <mach/hardware.h>
#include <mach/early-debug.h>
#include <mach/irqs.h>
#include <linux/bootmem.h>
#include <linux/of_platform.h>
#include <asm/setup.h>

// Imports from other files...
extern void hi_sata_init(void __iomem *mmio);
extern void register_hiusb(void);

#define REG_PERI_CRG57     IO_ADDRESS(CRG_REG_BASE + 0xe4)

#define muxctrl_reg0 0x0
#define muxctrl_reg1 0x4
#define muxctrl_reg2 0x8
#define muxctrl_reg3 0xc
#define muxctrl_reg4 0x10


static struct map_desc hi3520d_io_desc[] __initdata = {
	{
		.virtual        = HI3520D_IOCH1_VIRT,
		.pfn            = __phys_to_pfn(HI3520D_IOCH1_PHYS),
		.length         = HI3520D_IOCH1_SIZE,
		.type           = MT_DEVICE
	},
	{
		.virtual        = HI3520D_IOCH2_VIRT,
		.pfn            = __phys_to_pfn(HI3520D_IOCH2_PHYS),
		.length         = HI3520D_IOCH2_SIZE,
		.type           = MT_DEVICE
	}
};

void __init hi3520d_map_io(void)
{
	int i;
	unsigned long reg = 0;

	iotable_init(hi3520d_io_desc, ARRAY_SIZE(hi3520d_io_desc));

	for (i = 0; i < ARRAY_SIZE(hi3520d_io_desc); i++) {
		edb_putstr(" V: ");     edb_puthex(hi3520d_io_desc[i].virtual);
		edb_putstr(" P: ");     edb_puthex(hi3520d_io_desc[i].pfn);
		edb_putstr(" S: ");     edb_puthex(hi3520d_io_desc[i].length);
		edb_putstr(" T: ");     edb_putul(hi3520d_io_desc[i].type);
		edb_putstr("\n");
	}

	/* hi3520d uart use apb bus clk */
	reg = readl((void*)(REG_PERI_CRG57));
	reg &= ~UART_CKSEL_APB;
	writel(reg, (void*)(REG_PERI_CRG57));

	edb_trace();
}

void __init hi3520d_setup_pinmux(void)
{
	// setup multiplexing
	// configure VI_ADC_CLK pin for VI_ADC_CLK
	__raw_writel(1, (void*)(IO_ADDRESS(IOCONFIG_BASE) + muxctrl_reg0));
	// Configure VIU0* pins.
	__raw_writel(0, (void*)(IO_ADDRESS(IOCONFIG_BASE) + muxctrl_reg1));
	// configure VGA_HS pin to use VGA instead of GPIO2_0
	__raw_writel(1, (void*)(IO_ADDRESS(IOCONFIG_BASE) + muxctrl_reg3));
	// configure VGA_VS pin to use VGA instead of GPIO2_0
	__raw_writel(1, (void*)(IO_ADDRESS(IOCONFIG_BASE) + muxctrl_reg3));
}

void __init hi3520d_init(void)
{
	int ret = 0;

	edb_trace();

	// Initialize clocks and stuff first
	#ifdef CONFIG_HI_SATA
	hi_sata_init((void*)IO_ADDRESS(SATA_BASE));
	#endif
	// add USB controllers
	#ifdef CONFIG_HIUSB_HOST
	register_hiusb();
	#endif

	hi3520d_setup_pinmux();

	early_print("creating device tree...\n");
	ret = of_platform_populate(NULL, of_default_bus_match_table, NULL,
				   NULL);
	if (ret) {
		early_print("of_platform_populate failed: %d\n", ret);
	}
	else
	{
		early_print("created device tree\n");
	}
}

static const char __initconst *hi3520d_dt_compat[] = {
	"hisi,hi3520d",
	NULL
};

DT_MACHINE_START(HI3520D, "hi3520d")
	.map_io         = hi3520d_map_io,
	.init_machine   = hi3520d_init,
	.dt_compat	= hi3520d_dt_compat,
MACHINE_END
