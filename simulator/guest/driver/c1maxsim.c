// SPDX-License-Identifier: GPL-2.0-only
/* Original C1 Max fbdev/input ABI on QEMU Malta; no firmware emulation. */
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <sound/core.h>
#include <sound/control.h>
#include <asm/io.h>

#define WIDTH 340
#define HEIGHT 800
#define STRIDE (WIDTH * 4)
#define MEMORY (STRIDE * HEIGHT * 3)
static u8 *pixels;
static DEFINE_MUTEX(display_lock);
static struct fb_var_screeninfo view;
static struct input_dev *inputs[3];
static struct platform_device *platform;
static struct power_supply *battery, *usb;
static int capacity = 80, plugged = 1;
static struct class *backlight_class;
static struct device *backlight;
static int brightness = 150;
static struct snd_card *sound_card;
static struct snd_kcontrol *volume_control;
static long volumes[2] = {80, 80};

static long display_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
    void __user *pointer = (void __user *)argument;
    struct fb_var_screeninfo next;
    struct fb_fix_screeninfo fixed = { .id = "C1Max QEMU", .smem_len = MEMORY,
        .type = FB_TYPE_PACKED_PIXELS, .visual = FB_VISUAL_TRUECOLOR,
        .line_length = STRIDE, .ypanstep = 1 };
    int result = 0;
    mutex_lock(&display_lock);
    switch (command) {
    case FBIOGET_VSCREENINFO: result = copy_to_user(pointer, &view, sizeof(view)) ? -EFAULT : 0; break;
    case FBIOGET_FSCREENINFO: result = copy_to_user(pointer, &fixed, sizeof(fixed)) ? -EFAULT : 0; break;
    case FBIOPUT_VSCREENINFO:
    case FBIOPAN_DISPLAY:
        if (copy_from_user(&next, pointer, sizeof(next))) { result = -EFAULT; break; }
        if (next.xres != WIDTH || next.yres != HEIGHT || next.bits_per_pixel != 32 ||
            next.xres_virtual != WIDTH || next.yres_virtual < HEIGHT || next.yres_virtual > HEIGHT * 3 ||
            next.xoffset || next.yoffset > next.yres_virtual - HEIGHT) { result = -EINVAL; break; }
        view = next;
        break;
    case FBIOBLANK: break;
    case FBIO_WAITFORVSYNC: break;
    default: result = -ENOTTY;
    }
    mutex_unlock(&display_lock);
    return result;
}
static int display_mmap(struct file *file, struct vm_area_struct *area)
{
    unsigned long offset = area->vm_pgoff << PAGE_SHIFT;
    if (offset >= PAGE_ALIGN(MEMORY) || area->vm_end - area->vm_start > PAGE_ALIGN(MEMORY) - offset) return -EINVAL;
    return remap_vmalloc_range(area, pixels, area->vm_pgoff);
}
static const struct file_operations display_ops = { .owner = THIS_MODULE,
    .unlocked_ioctl = display_ioctl, .mmap = display_mmap, .llseek = no_llseek };
static struct miscdevice display_device = { .minor = MISC_DYNAMIC_MINOR, .name = "fb2", .mode = 0666, .fops = &display_ops };

struct control_packet { u16 source, code; s32 value; };
static ssize_t control_write(struct file *file, const char __user *data, size_t size, loff_t *offset)
{
    struct control_packet packet;
    if (size != sizeof(packet)) return -EINVAL;
    if (copy_from_user(&packet, data, sizeof(packet))) return -EFAULT;
    if (packet.source < 2) {
        if (packet.code > KEY_MAX || packet.value < 0 || packet.value > 2) return -EINVAL;
        input_event(inputs[packet.source], EV_KEY, packet.code, packet.value);
        input_sync(inputs[packet.source]);
    } else if (packet.source == 2) {
        if (packet.code == ABS_X || packet.code == ABS_Y) {
            if (packet.value < 0 || packet.value >= (packet.code == ABS_X ? WIDTH : HEIGHT)) return -EINVAL;
            input_event(inputs[2], EV_ABS, packet.code, packet.value);
        } else if (packet.code == BTN_TOUCH) {
            if (packet.value != 0 && packet.value != 1) return -EINVAL;
            input_event(inputs[2], EV_KEY, BTN_TOUCH, packet.value);
            input_event(inputs[2], EV_ABS, ABS_PRESSURE, packet.value ? 100 : 0);
        } else if (packet.code == 0xffff) input_sync(inputs[2]);
        else return -EINVAL;
    } else if (packet.source == 3) {
        if (packet.value < 0 || packet.value > 100 || packet.code > 1) return -EINVAL;
        capacity = packet.value; plugged = packet.code;
        power_supply_changed(battery); power_supply_changed(usb);
    } else return -EINVAL;
    return size;
}
static const struct file_operations control_ops = { .owner = THIS_MODULE, .write = control_write, .llseek = no_llseek };
static struct miscdevice control_device = { .minor = MISC_DYNAMIC_MINOR, .name = "c1maxsim", .mode = 0600, .fops = &control_ops };

static enum power_supply_property battery_properties[] = { POWER_SUPPLY_PROP_CAPACITY, POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_VOLTAGE_NOW, POWER_SUPPLY_PROP_CURRENT_NOW, POWER_SUPPLY_PROP_CHARGE_FULL, POWER_SUPPLY_PROP_CHARGE_NOW };
static enum power_supply_property usb_properties[] = { POWER_SUPPLY_PROP_ONLINE };
static int supply_get(struct power_supply *supply, enum power_supply_property property, union power_supply_propval *value)
{
    switch (property) {
    case POWER_SUPPLY_PROP_CAPACITY: value->intval = capacity; break;
    case POWER_SUPPLY_PROP_STATUS: value->intval = plugged ? POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_DISCHARGING; break;
    case POWER_SUPPLY_PROP_ONLINE: value->intval = plugged; break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW: value->intval = 3900000; break;
    case POWER_SUPPLY_PROP_CURRENT_NOW: value->intval = plugged ? 100000 : -60000; break;
    case POWER_SUPPLY_PROP_CHARGE_FULL: value->intval = 2000000; break;
    case POWER_SUPPLY_PROP_CHARGE_NOW: value->intval = capacity * 20000; break;
    default: return -EINVAL;
    }
    return 0;
}
static const struct power_supply_desc battery_desc = { .name = "battery", .type = POWER_SUPPLY_TYPE_BATTERY,
    .properties = battery_properties, .num_properties = ARRAY_SIZE(battery_properties), .get_property = supply_get };
static const struct power_supply_desc usb_desc = { .name = "usb", .type = POWER_SUPPLY_TYPE_USB,
    .properties = usb_properties, .num_properties = ARRAY_SIZE(usb_properties), .get_property = supply_get };
static ssize_t brightness_show(struct device *device, struct device_attribute *attr, char *buffer) { return sysfs_emit(buffer, "%d\n", brightness); }
static ssize_t brightness_store(struct device *device, struct device_attribute *attr, const char *buffer, size_t size)
{ int value; if (kstrtoint(buffer, 0, &value) || value < 0 || value > 255) return -EINVAL; brightness = value; return size; }
static ssize_t actual_brightness_show(struct device *device, struct device_attribute *attr, char *buffer) { return sysfs_emit(buffer, "%d\n", brightness); }
static ssize_t max_brightness_show(struct device *device, struct device_attribute *attr, char *buffer) { return sysfs_emit(buffer, "255\n"); }
static DEVICE_ATTR_RW(brightness);
static DEVICE_ATTR_RO(actual_brightness);
static DEVICE_ATTR_RO(max_brightness);
static struct attribute *light_attrs[] = { &dev_attr_brightness.attr, &dev_attr_actual_brightness.attr, &dev_attr_max_brightness.attr, NULL };
ATTRIBUTE_GROUPS(light);

static int volume_info(struct snd_kcontrol *control, struct snd_ctl_elem_info *info)
{ info->type = SNDRV_CTL_ELEM_TYPE_INTEGER; info->count = 2; info->value.integer.min = 0; info->value.integer.max = 100; return 0; }
static int volume_get(struct snd_kcontrol *control, struct snd_ctl_elem_value *value)
{ value->value.integer.value[0] = volumes[0]; value->value.integer.value[1] = volumes[1]; return 0; }
static int volume_put(struct snd_kcontrol *control, struct snd_ctl_elem_value *value)
{
    struct snd_ctl_elem_id id = { .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = "Master Playback Volume" };
    struct snd_kcontrol *master = snd_ctl_find_id(sound_card, &id);
    struct { struct snd_ctl_elem_info info; struct snd_ctl_elem_value gain; } *data;
    data = kzalloc(sizeof(*data), GFP_KERNEL);
    if (!data) return -ENOMEM;
    int changed = 0;
    for (int i = 0; i < 2; ++i) {
        long next = clamp(value->value.integer.value[i], 0L, 100L);
        changed |= next != volumes[i]; volumes[i] = next;
    }
    if (master && master->info(master, &data->info) == 0) {
        for (int i = 0; i < 2; ++i) data->gain.value.integer.value[i] = data->info.value.integer.min +
            volumes[i] * (data->info.value.integer.max - data->info.value.integer.min) / 100;
        master->put(master, &data->gain);
    }
    kfree(data);
    return changed;
}
static const struct snd_kcontrol_new volume_template = { .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = "softvolume",
    .info = volume_info, .get = volume_get, .put = volume_put };

static void cleanup(void)
{
    if (volume_control) snd_ctl_remove(sound_card, volume_control);
    if (sound_card) snd_card_unref(sound_card);
    if (!IS_ERR_OR_NULL(backlight)) device_unregister(backlight);
    if (!IS_ERR_OR_NULL(backlight_class)) class_destroy(backlight_class);
    if (!IS_ERR_OR_NULL(usb)) power_supply_unregister(usb);
    if (!IS_ERR_OR_NULL(battery)) power_supply_unregister(battery);
    for (int i = 0; i < 3; ++i) if (inputs[i]) input_unregister_device(inputs[i]);
    if (!IS_ERR_OR_NULL(platform)) platform_device_unregister(platform);
    vfree(pixels);
}
static int __init simulator_init(void)
{
    int error;
    /* Match Malta PCI level-triggered AC97/disk interrupt routing. */
    outb(inb(0x4d1) | 0x0c, 0x4d1);
    pixels = vmalloc_user(PAGE_ALIGN(MEMORY));
    if (!pixels) return -ENOMEM;
    view = (struct fb_var_screeninfo){ .xres = WIDTH, .yres = HEIGHT, .xres_virtual = WIDTH, .yres_virtual = HEIGHT * 3,
        .bits_per_pixel = 32, .red = {16, 8, 0}, .green = {8, 8, 0}, .blue = {0, 8, 0}, .transp = {24, 8, 0}, .width = 86, .height = 202 };
    platform = platform_device_register_simple("c1maxsim", -1, NULL, 0);
    if (IS_ERR(platform)) { error = PTR_ERR(platform); goto fail; }
    for (int i = 0; i < 3; ++i) {
        int code;
        inputs[i] = input_allocate_device();
        if (!inputs[i]) { error = -ENOMEM; goto fail; }
        inputs[i]->name = i == 2 ? "C1 Max cst3xx touch" : i ? "C1 Max gpio keys" : "C1 Max matrix keys";
        inputs[i]->id.bustype = BUS_VIRTUAL;
        if (i < 2) for (code = 1; code <= KEY_MAX; ++code) input_set_capability(inputs[i], EV_KEY, code);
        else {
            input_set_capability(inputs[i], EV_KEY, BTN_TOUCH);
            input_set_abs_params(inputs[i], ABS_X, 0, WIDTH - 1, 0, 0);
            input_set_abs_params(inputs[i], ABS_Y, 0, HEIGHT - 1, 0, 0);
            input_set_abs_params(inputs[i], ABS_PRESSURE, 0, 255, 0, 0);
            __set_bit(INPUT_PROP_DIRECT, inputs[i]->propbit);
        }
        error = input_register_device(inputs[i]);
        if (error) { input_free_device(inputs[i]); inputs[i] = NULL; goto fail; }
    }
    battery = power_supply_register(&platform->dev, &battery_desc, NULL);
    if (IS_ERR(battery)) { error = PTR_ERR(battery); goto fail; }
    usb = power_supply_register(&platform->dev, &usb_desc, NULL);
    if (IS_ERR(usb)) { error = PTR_ERR(usb); goto fail; }
    backlight_class = class_create("backlight");
    if (IS_ERR(backlight_class)) { error = PTR_ERR(backlight_class); goto fail; }
    backlight = device_create_with_groups(backlight_class, NULL, 0, NULL, light_groups, "backlight");
    if (IS_ERR(backlight)) { error = PTR_ERR(backlight); goto fail; }
    sound_card = snd_card_ref(0);
    if (!sound_card) { error = -ENODEV; goto fail; }
    volume_control = snd_ctl_new1(&volume_template, NULL);
    if (!volume_control) { error = -ENOMEM; goto fail; }
    error = snd_ctl_add(sound_card, volume_control);
    if (error) { volume_control = NULL; goto fail; }
    error = misc_register(&display_device);
    if (error) goto fail;
    error = misc_register(&control_device);
    if (error) { misc_deregister(&display_device); goto fail; }
    pr_info("c1maxsim: fb2 340x800 BGRA, triple buffer, evdev touch, softvolume ready\n");
    return 0;
fail: cleanup(); return error;
}
static void __exit simulator_exit(void) { misc_deregister(&control_device); misc_deregister(&display_device); cleanup(); }
module_init(simulator_init);
module_exit(simulator_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("C1 Max application ABI on QEMU MIPS Malta");
