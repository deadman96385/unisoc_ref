/*
 * SPRD multipule usb manage defines.
 *
 * These APIs may be used by usb controller or phy drivers.
 */

#ifndef __LINUX_SPRD_USBM_H
#define __LINUX_SPRD_USBM_H

#include <linux/notifier.h>
#include <linux/usb.h>
#include <linux/usb/phy.h>

/* associate a type with PAM */
enum sprd_usbm_event_type {
	SPRD_USBM_EVENT_TYPE_UNDEFINED,
	SPRD_USBM_EVENT_TYPE_USB2,
	SPRD_USBM_EVENT_TYPE_USB3,
};

enum sprd_usbm_event_mode {
	SPRD_USBM_EVENT_MUSB = 0,
	SPRD_USBM_EVENT_HOST_MUSB,
	SPRD_USBM_EVENT_DWC3,
	SPRD_USBM_EVENT_HOST_DWC3,
	SPRD_USBM_EVENT_MAX,
};

#if IS_ENABLED(CONFIG_SPRD_USBM)
extern int call_sprd_usbm_event_notifiers(unsigned int id, unsigned long val, void *v);
extern int register_sprd_usbm_notifier(struct notifier_block *nb, unsigned int id);
extern int sprd_usbm_event_is_active(void);
extern int sprd_usbm_event_set_deactive(void);
extern int sprd_usbm_hsphy_get_onoff(void);
extern int sprd_usbm_hsphy_set_onoff(int onoff);
extern int sprd_usbm_ssphy_get_onoff(void);
extern int sprd_usbm_ssphy_set_onoff(int onoff);
#else
static inline int call_sprd_usbm_event_notifiers(unsigned int id, unsigned long val, void *v)
{
	return 0;
}
static inline int register_sprd_usbm_notifier(struct notifier_block *nb, unsigned int id)
{
	return 0;
}
static inline int sprd_usbm_event_is_active(void)
{
	return 0;
}
static inline int sprd_usbm_event_set_deactive(void)
{
	return 0;
}
static inline int sprd_usbm_hsphy_get_onoff(void)
{
	return 0;
}
static inline int sprd_usbm_hsphy_set_onoff(int onoff)
{
	return 0;
}
static inline int sprd_usbm_ssphy_get_onoff(void)
{
	return 0;
}
static inline int sprd_usbm_ssphy_set_onoff(int onoff)
{
	return 0;
}
#endif /* IS_ENABLED(CONFIG_SPRD_USBM) */

#endif /* __LINUX_SPRD_USBM_H */

