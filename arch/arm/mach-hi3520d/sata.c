#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <mach/io.h>
#include <linux/platform_device.h>

/* hisilicon sata reg */
#define HI_SATA_PHY0_CTLL       0x54
#define HI_SATA_PHY0_CTLH       0x58
#define HI_SATA_PHY1_CTLL       0x60
#define HI_SATA_PHY1_CTLH       0x64
#define HI_SATA_DIS_CLK     (1 << 12)
#define HI_SATA_OOB_CTL         0x6c
#define HI_SATA_PORT_PHYCTL     0x74

#ifdef CONFIG_ARCH_HI3520D
#include "ahci_sys_hi3520d_defconfig.c"
#endif/*CONFIG_ARCH_HI3520D*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hisilicon osdrv group");


static int phy_config = CONFIG_HI_SATA_PHY_CONFIG;
static int n_ports = CONFIG_HI_SATA_PORTS;
static int mode_3g = CONFIG_HI_SATA_MODE;

#ifdef MODULE
module_param(phy_config, uint, 0600);
MODULE_PARM_DESC(phy_config, "sata phy config (default:0x0e262709)");
module_param(n_ports, uint, 0600);
MODULE_PARM_DESC(n_ports, "sata port number (default:2)");
module_param(mode_3g, uint, 0600);
MODULE_PARM_DESC(mode_3g, "set sata 3G mode (0:1.5G(default);1:3G)");
#endif

static struct resource hisatav100_ahci_resources[] = {
	[0] = {
		.start          = CONFIG_HI_SATA_IOBASE,
		.end            = CONFIG_HI_SATA_IOBASE +
					CONFIG_HI_SATA_IOSIZE - 1,
		.flags          = IORESOURCE_MEM,
	},
	[1] = {
		.start          = CONFIG_HI_SATA_IRQNUM,
		.end            = CONFIG_HI_SATA_IRQNUM,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 ahci_dmamask = ~(u32)0;

static void hisatav100_ahci_platdev_release(struct device *dev)
{
}

static struct platform_device hisatav100_ahci_device = {
	.name           = "ahci",
	.dev = {
		.dma_mask               = &ahci_dmamask,
		.coherent_dma_mask      = 0xffffffff,
		.release                = hisatav100_ahci_platdev_release,
	},
	.num_resources  = ARRAY_SIZE(hisatav100_ahci_resources),
	.resource       = hisatav100_ahci_resources,
};

void hi_sata_init(void __iomem *mmio)
{
	unsigned int tmp;
	int i;
    int ret;

	hi_sata_poweron();
	msleep(20);
	hi_sata_clk_open();
	hi_sata_phy_clk_sel();
	hi_sata_unreset();
	msleep(20);
	hi_sata_phy_unreset();
	msleep(20);
	tmp = readl(mmio + HI_SATA_PHY0_CTLH);
	tmp |= HI_SATA_DIS_CLK;
	writel(tmp, (mmio + HI_SATA_PHY0_CTLH));
	tmp = readl(mmio + HI_SATA_PHY1_CTLH);
	tmp |= HI_SATA_DIS_CLK;
	writel(tmp, (mmio + HI_SATA_PHY1_CTLH));
	if (mode_3g) {
		tmp = CONFIG_HI_SATA_PHY0_CTLL_3G_VAL;
		phy_config = CONFIG_HI_SATA_3G_PHY_CONFIG;
	} else {
		tmp = CONFIG_HI_SATA_PHY0_CTLL_15G_VAL;
	}
	writel(tmp, (mmio + HI_SATA_PHY0_CTLL));
	writel(CONFIG_HI_SATA_PHYX_CTLH_VAL, (mmio + HI_SATA_PHY0_CTLH));
	writel(tmp, (mmio + HI_SATA_PHY1_CTLL));
	writel(CONFIG_HI_SATA_PHYX_CTLH_VAL, (mmio + HI_SATA_PHY1_CTLH));
	writel(0x84060c15, (mmio + HI_SATA_OOB_CTL));
	for (i = 0; i < n_ports; i++)
		writel(phy_config, (mmio + 0x100 + i*0x80
					+ HI_SATA_PORT_PHYCTL));

	hi_sata_phy_reset();
	msleep(20);
	hi_sata_phy_unreset();
	msleep(20);
	hi_sata_clk_unreset();
	msleep(20);

    ret = platform_device_register(&hisatav100_ahci_device);
	if (ret) {
		printk(KERN_ERR "[%s %d] sata platform device register "
				"is failed!!!\n", __func__, __LINE__);
	}
}
EXPORT_SYMBOL(hi_sata_init);

void hi_sata_exit(void)
{
	hi_sata_phy_reset();
	msleep(20);
	hi_sata_reset();
	msleep(20);
	hi_sata_clk_reset();
	msleep(20);
	hi_sata_clk_close();
	hi_sata_poweroff();
	msleep(20);
}
EXPORT_SYMBOL(hi_sata_exit);
