#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/regs-rtc.h>
#include <mach/pinctrl.h>
#include <../arch/arm/mach-mx28/regs-pinctrl.h>
#include <../arch/arm/mach-mx28/mx28_pins.h>

#define CSIGNAL		0x000000ff
#define CLONE_FS	0x00000200
#define CLONE_SIGHAND	0x00000800

extern void msleep(unsigned int msecs);

static int feed_dog = 0;
static int flag_end = 0;



static int init_wdg_gpio(void)
{
	
	if(gpio_request(MXS_PIN_TO_GPIO(PINID_GPMI_RDY1), NULL))
	{
		printk("wdg gpio error");
		return -1;
	}	
	gpio_direction_output(MXS_PIN_TO_GPIO(PINID_GPMI_RDY1), 0);
	msleep(20);	
	gpio_direction_output(MXS_PIN_TO_GPIO(PINID_GPMI_RDY1), 1);
	feed_dog = 0;
	return 0;	
}


static void watchdog_feed(int timeout)
{
	unsigned long  expire,tick = 1000;
	expire = jiffies + timeout;

	/*
	 * Check whether the transmitter is empty every 'char_time'.
	 * 'timeout' / 'expire' give us the maximum amount of time
	 * we wait.
	 */
	while (tick--) {
		msleep(jiffies_to_msecs(10));
		if (time_after(jiffies, expire))
			break;
	}
	gpio_direction_output(MXS_PIN_TO_GPIO(PINID_GPMI_RDY1), feed_dog);
	feed_dog  = (feed_dog == 0 ? 1 : 0);
}

static int  watchdog_process(void * unused)
{
	for(;;)
	{
		if(flag_end == 1)
			break;
		watchdog_feed(100);
	}
	return 0;
}


static int __init zlg_wdt_init(void)
{
	int ret,pid;
	
	ret = init_wdg_gpio();	
	if(ret)
		return -1;
	pid = kernel_thread(watchdog_process, NULL, CLONE_FS | CLONE_SIGHAND);
	return 0;
}

//static void __exit zlg_wdt_exit(void)
//{
//	;
//}

subsys_initcall(zlg_wdt_init);
//subsys_initcall(zlg_wdt_exit);
