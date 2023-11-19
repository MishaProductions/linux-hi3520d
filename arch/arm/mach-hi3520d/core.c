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
#include <linux/leds.h>
#include <asm/hardware/arm_timer.h>
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
#include "clock.h"
#include <linux/bootmem.h>
#include <linux/of_platform.h>

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


/*#define HW_REG(a) (*(volatile unsigned int *)(a))*/
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
extern void timer_preinit(void);
static void early_init(void)
{
	timer_preinit();
}

void __init hi3520d_map_io(void)
{
	int i;

	iotable_init(hi3520d_io_desc, ARRAY_SIZE(hi3520d_io_desc));

	for (i = 0; i < ARRAY_SIZE(hi3520d_io_desc); i++) {
		edb_putstr(" V: ");     edb_puthex(hi3520d_io_desc[i].virtual);
		edb_putstr(" P: ");     edb_puthex(hi3520d_io_desc[i].pfn);
		edb_putstr(" S: ");     edb_puthex(hi3520d_io_desc[i].length);
		edb_putstr(" T: ");     edb_putul(hi3520d_io_desc[i].type);
		edb_putstr("\n");
	}

	early_init();

	edb_trace();
}
void __iomem *hi3520d_gic_cpu_base_addr;

void __init hi3520d_gic_init_irq(void)
{
	edb_trace();

	hi3520d_gic_cpu_base_addr = (void __iomem *)CFG_GIC_CPU_BASE;

	gic_init(0, HI3520D_IRQ_START, (void __iomem *)CFG_GIC_DIST_BASE,
		(void __iomem *)CFG_GIC_CPU_BASE);
}

#define HIL_AMBADEV_NAME(name) hil_ambadevice_##name

#define HIL_AMBA_DEVICE(name, busid, base, platdata)               \
	static struct amba_device HIL_AMBADEV_NAME(name) =              \
	{	.dev            = {                                     \
			.coherent_dma_mask = ~0,                        \
			.init_name = busid,                              \
			.platform_data = platdata,                      \
		},                                                      \
		.res            = {                                     \
			.start  = base##_BASE,                      \
			.end    = base##_BASE + 0x10000 - 1,  \
			.flags  = IORESOURCE_IO,                        \
		},                                                      \
		.irq            = { base##_IRQ, base##_IRQ }              \
	}


HIL_AMBA_DEVICE(uart0, "uart:0",  UART0,    NULL);
HIL_AMBA_DEVICE(uart1, "uart:1",  UART1,    NULL);

static struct amba_device *amba_devs[] __initdata = {
	&HIL_AMBADEV_NAME(uart0),
	//&HIL_AMBADEV_NAME(uart1),
};


static struct clk uart_clk;
static struct clk_lookup lookups[2];
unsigned long the_uart_rate;
static void  uart_clk_init(unsigned long clk)
{
	uart_clk.rate = clk;

	lookups[0].dev_id = "uart:0";
	lookups[0].clk = &uart_clk;

	lookups[1].dev_id = "uart:1";
	lookups[1].clk = &uart_clk;
}

extern void hi_sata_init(void __iomem *mmio);
extern void register_hiusb(void);
extern void early_print(const char *str, ...);

void __init hi3520d_init(void)
{
	unsigned long i = 0;
	int ret=0;

	hi3520d_gic_cpu_base_addr = (void __iomem *)CFG_GIC_CPU_BASE;

	edb_trace();


	//clkdev_add_table(lookups, ARRAY_SIZE(lookups));
	

	ret = of_platform_populate(NULL, of_default_bus_match_table, NULL,
				   NULL);
	if (ret) {
		early_print("of_platform_populate failed: %d\n", ret);
	}


	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		edb_trace();
		//ret = amba_device_register(amba_devs[i], &iomem_resource);
		if(ret<0){
			early_print("failed to register uart with code %d...\n",ret);
		}
	}

	early_print("sata...\n");
	// add SATA controller device and write the correct registers
	#ifdef CONFIG_HI_SATA
	hi_sata_init((void*)IO_ADDRESS(SATA_BASE));
	#endif
	early_print("usb...\n");
	// add USB controllers
	#ifdef CONFIG_HIUSB_HOST
	register_hiusb();
	#endif
	edb_trace();
}

void hi3520d_restart(enum reboot_mode mode, const char *cmd)
{
	__raw_writel(~0, (void*)(IO_ADDRESS(SYS_CTRL_BASE) + REG_SC_SYSRES));
}

static const char __initconst *hi3520d_dt_compat[] = {
	"hisi,hi3520d",
	NULL,
};
extern void __init hi3520d_timerdev_initl(void);
DT_MACHINE_START(HI3520D, "hi3520d")
	//.boot_params    = PHYS_OFFSET+0x100,
	.map_io         = hi3520d_map_io,
	.init_irq       = hi3520d_gic_init_irq,
	//.init_time          = hi3520d_timerdev_initl,
	.init_machine   = hi3520d_init,
	.restart = hi3520d_restart,
	.dt_compat	= hi3520d_dt_compat,
MACHINE_END
