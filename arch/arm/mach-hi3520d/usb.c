#include <linux/init.h>
#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <asm/unaligned.h>
#include <asm/io.h>
#include <mach/io.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#define PERI_CRG46		(IO_ADDRESS(0x20030000 + 0xb8))
#define USB_CKEN		(1 << 7)
#define USB_CTRL_UTMI1_REG	(1 << 6)
#define USB_CTRL_UTMI0_REG	(1 << 5)
#define USB_CTRL_HUB_REG	(1 << 4)
#define USBPHY_PORT1_TREQ	(1 << 3)
#define USBPHY_PORT0_TREQ	(1 << 2)
#define USBPHY_REQ		(1 << 1)
#define USB_AHB_SRST_REQ	(1 << 0)
#define PERI_USB		(IO_ADDRESS(0x20050000 + 0x80))
#define WORDINTERFACE		(1 << 0)
#define ULPI_BYPASS_EN		(1 << 3)
#define SS_BURST4_EN		(1 << 7)
#define SS_BURST8_EN		(1 << 8)
#define SS_BURST16_EN		(1 << 9)
#define USBOVR_P_CTRL		(1 << 17)

static atomic_t dev_open_cnt = {
	.counter = 0,
};

void hiusb_start_hcd(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (atomic_add_return(1, &dev_open_cnt) == 1) {

		int reg;

		/* enable clock to EHCI block and HS PHY PLL*/
		reg = readl((void*)PERI_CRG46);
		reg |= USB_CKEN;
		reg &= ~(USB_CTRL_UTMI1_REG);
		reg &= ~(USB_CTRL_UTMI0_REG);
		reg &= ~(USB_CTRL_HUB_REG);
		reg &= ~(USBPHY_PORT1_TREQ);
		reg &= ~(USBPHY_PORT0_TREQ);
		reg &= ~(USBPHY_REQ);
		reg &= ~(USB_AHB_SRST_REQ);
		writel(reg, (void*)PERI_CRG46);
		udelay(100);

		/* enable phy */
		reg = readl((void*)PERI_USB);
		reg |= ULPI_BYPASS_EN;
		reg &= ~(WORDINTERFACE);
		/* disable ehci burst16 mode*/
		reg &= ~(SS_BURST16_EN);
		reg &= ~(USBOVR_P_CTRL);
		writel(reg, (void*)PERI_USB);
		udelay(100);
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(hiusb_start_hcd);

void hiusb_stop_hcd(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (atomic_sub_return(1, &dev_open_cnt) == 0) {

		int reg;

		/* Disable EHCI clock.
		If the HS PHY is unused disable it too. */

		reg = readl((void*)PERI_CRG46);
		reg &= ~(USB_CKEN);
		reg |= (USB_CTRL_UTMI1_REG);
		reg |= (USB_CTRL_UTMI0_REG);
		reg |= (USB_CTRL_HUB_REG);
		reg |= (USBPHY_PORT1_TREQ);
		reg |= (USBPHY_PORT0_TREQ);
		reg |= (USBPHY_REQ);
		reg |= (USB_AHB_SRST_REQ);
		writel(reg, (void*)PERI_CRG46);
		udelay(100);

		/* enable phy */
		reg = readl((void*)PERI_USB);
		reg &= ~ULPI_BYPASS_EN;
		reg |= (WORDINTERFACE);
		reg |= (SS_BURST16_EN);
		reg |= (USBOVR_P_CTRL);
		writel(reg, (void*)PERI_USB);
		udelay(100);
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(hiusb_stop_hcd);


static struct resource hiusb_ehci_res[] = {
	[0] = {
		.start = CONFIG_HIUSB_EHCI_IOBASE,
		.end = CONFIG_HIUSB_EHCI_IOBASE
				 + CONFIG_HIUSB_EHCI_IOSIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = CONFIG_HIUSB_EHCI_IRQNUM,
		.end   = CONFIG_HIUSB_EHCI_IRQNUM,
		.flags = IORESOURCE_IRQ,
	},
};

static void usb_ehci_platdev_release(struct device *dev)
{
		/* These don't need to do anything because the
		 pdev structures are statically allocated. */
}

static u64 usb_dmamask = DMA_BIT_MASK(32);

static struct platform_device hiusb_ehci_platdev = {
	.name = "hiusb-ehci",
	.id = 0,
	.dev = {
		.platform_data = NULL,
		.dma_mask = &usb_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.release = usb_ehci_platdev_release,
	},
	.num_resources = ARRAY_SIZE(hiusb_ehci_res),
	.resource = hiusb_ehci_res,
};


static struct resource hiusb_ohci_res[] = {
	[0] = {
		.start = CONFIG_HIUSB_OHCI_IOBASE,
		.end   = CONFIG_HIUSB_OHCI_IOBASE +
				 CONFIG_HIUSB_OHCI_IOSIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = CONFIG_HIUSB_OHCI_IRQNUM,
		.end   = CONFIG_HIUSB_OHCI_IRQNUM,
		.flags = IORESOURCE_IRQ,
	},
};

static void usb_ohci_platdev_release(struct device *dev)
{
		/* These don't need to do anything because the pdev
		 structures are statically allocated. */
}

static struct platform_device hiusb_ohci_platdev = {
	.name   = "hiusb-ohci",
	.id     = 0,
	.dev    = {
		.platform_data  = NULL,
		.dma_mask = &usb_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.release = usb_ohci_platdev_release,
	},
	.num_resources = ARRAY_SIZE(hiusb_ohci_res),
	.resource       = hiusb_ohci_res,
};


void register_hiusb(void)
{
	hiusb_start_hcd();
}