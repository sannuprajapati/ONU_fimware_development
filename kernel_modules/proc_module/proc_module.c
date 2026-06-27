/*
 * proc_module.c - Kernel Module with procfs Interface
 *
 * Demonstrates:
 *   - Creating /proc/ entries from a kernel module
 *   - seq_file API for procfs output
 *   - Kernel statistics and state reporting
 *   - Simulated driver state machine (like PLOAM O1-O5)
 *   - Module parameters
 *   - Atomic operations for thread-safe counters
 *
 * Relevance to ONU firmware (EN7528HU TCLinux):
 *   /proc/tc3162/ in TCLinux contains vendor proc entries
 *   that expose PON status, HWNAT stats, and driver state.
 *   ponmgr reads /proc/tc3162/pon_status to track PLOAM state.
 *   This module creates a similar interface.
 *
 * Test:
 *   sudo insmod proc_module.ko
 *   cat /proc/mydriver/status
 *   cat /proc/mydriver/stats
 *   echo "reset" | sudo tee /proc/mydriver/control
 *   sudo rmmod proc_module
 *   dmesg | tail -20
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/timekeeping.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

#define DRIVER_NAME     "mydriver"
#define PROC_DIR        "mydriver"

/* Module parameter */
static char *device_name = "ONU-HAL-Device";
module_param(device_name, charp, 0444);
MODULE_PARM_DESC(device_name, "Device name string");

/*
 * Simulated driver state machine
 * Models the PLOAM O1-O5 registration states in ONU firmware
 */
enum driver_state {
    STATE_IDLE       = 0,   /* O1: initial      */
    STATE_INIT       = 1,   /* O2: standby      */
    STATE_DETECTING  = 2,   /* O3: SN detect    */
    STATE_RANGING    = 3,   /* O4: ranging      */
    STATE_OPERATIONAL= 4,   /* O5: operational  */
    STATE_ERROR      = 5,   /* fault            */
};

static const char * const state_names[] = {
    "IDLE",
    "INIT",
    "DETECTING",
    "RANGING",
    "OPERATIONAL",
    "ERROR",
};

/* Driver state and statistics */
static enum driver_state current_state = STATE_IDLE;
static atomic_t          read_count    = ATOMIC_INIT(0);
static atomic_t          write_count   = ATOMIC_INIT(0);
static atomic_t          irq_count     = ATOMIC_INIT(0);
static unsigned long     load_time;
static struct proc_dir_entry *proc_dir;

/*
 * ── /proc/mydriver/status ───────────────────────────────────
 * Shows current driver state and device info
 */
static int status_show(struct seq_file *m, void *v)
{
    unsigned long uptime_sec;
    unsigned long now = jiffies;

    uptime_sec = (now - load_time) / HZ;

    seq_printf(m, "=== %s Driver Status ===\n", DRIVER_NAME);
    seq_printf(m, "Device      : %s\n",   device_name);
    seq_printf(m, "State       : %s (%d)\n",
               state_names[current_state],
               current_state);
    seq_printf(m, "Uptime      : %lu seconds\n", uptime_sec);
    seq_printf(m, "Kernel ver  : %s\n",   UTS_RELEASE);
    seq_printf(m, "\n");
    seq_printf(m, "State meanings (like GPON PLOAM):\n");
    seq_printf(m, "  0=IDLE       (PLOAM O1: powered on)\n");
    seq_printf(m, "  1=INIT       (PLOAM O2: standby)\n");
    seq_printf(m, "  2=DETECTING  (PLOAM O3: SN detect)\n");
    seq_printf(m, "  3=RANGING    (PLOAM O4: ranging)\n");
    seq_printf(m, "  4=OPERATIONAL(PLOAM O5: registered)\n");
    seq_printf(m, "  5=ERROR      (fault state)\n");
    return 0;
}

static int status_open(struct inode *inode, struct file *file)
{
    return single_open(file, status_show, NULL);
}

static const struct file_operations status_fops = {
    .owner   = THIS_MODULE,
    .open    = status_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/*
 * ── /proc/mydriver/stats ────────────────────────────────────
 * Shows driver statistics — like /proc/tc3162/hwnat on EN7528HU
 */
static int stats_show(struct seq_file *m, void *v)
{
    seq_printf(m, "=== %s Driver Statistics ===\n",
               DRIVER_NAME);
    seq_printf(m, "Read  ops : %d\n",
               atomic_read(&read_count));
    seq_printf(m, "Write ops : %d\n",
               atomic_read(&write_count));
    seq_printf(m, "IRQ count : %d\n",
               atomic_read(&irq_count));
    return 0;
}

static int stats_open(struct inode *inode, struct file *file)
{
    atomic_inc(&read_count);
    return single_open(file, stats_show, NULL);
}

static const struct file_operations stats_fops = {
    .owner   = THIS_MODULE,
    .open    = stats_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/*
 * ── /proc/mydriver/control ──────────────────────────────────
 * Writable proc entry — write commands to control driver state
 * Models how ponmgr writes to /proc/tc3162/ to trigger
 * PLOAM state transitions on EN7528HU.
 *
 * Commands:
 *   echo "init"   > /proc/mydriver/control
 *   echo "start"  > /proc/mydriver/control
 *   echo "reset"  > /proc/mydriver/control
 *   echo "next"   > /proc/mydriver/control
 */
static ssize_t control_write(struct file *file,
                              const char __user *ubuf,
                              size_t len, loff_t *off)
{
    char cmd[32];
    size_t copy_len = min(len, sizeof(cmd) - 1);

    if (copy_from_user(cmd, ubuf, copy_len))
        return -EFAULT;

    cmd[copy_len] = '\0';

    /* strip trailing newline */
    if (copy_len > 0 && cmd[copy_len-1] == '\n')
        cmd[copy_len-1] = '\0';

    atomic_inc(&write_count);

    if (strcmp(cmd, "init") == 0) {
        current_state = STATE_INIT;
        pr_info("%s: state -> INIT\n", DRIVER_NAME);
    } else if (strcmp(cmd, "start") == 0) {
        current_state = STATE_DETECTING;
        pr_info("%s: state -> DETECTING\n", DRIVER_NAME);
    } else if (strcmp(cmd, "next") == 0) {
        if (current_state < STATE_OPERATIONAL)
            current_state++;
        pr_info("%s: state -> %s\n",
                DRIVER_NAME, state_names[current_state]);
    } else if (strcmp(cmd, "reset") == 0) {
        current_state = STATE_IDLE;
        atomic_set(&read_count, 0);
        atomic_set(&write_count, 0);
        atomic_set(&irq_count, 0);
        pr_info("%s: reset to IDLE\n", DRIVER_NAME);
    } else if (strcmp(cmd, "error") == 0) {
        current_state = STATE_ERROR;
        pr_warn("%s: state -> ERROR\n", DRIVER_NAME);
    } else {
        pr_warn("%s: unknown command [%s]\n",
                DRIVER_NAME, cmd);
        pr_info("%s: commands: init|start|next|reset|error\n",
                DRIVER_NAME);
        return -EINVAL;
    }

    return len;
}

static int control_show(struct seq_file *m, void *v)
{
    seq_printf(m, "Current state: %s\n",
               state_names[current_state]);
    seq_printf(m, "Commands: init | start | next | reset | error\n");
    seq_printf(m, "Usage: echo <cmd> > /proc/%s/control\n",
               PROC_DIR);
    return 0;
}

static int control_open(struct inode *inode, struct file *file)
{
    return single_open(file, control_show, NULL);
}

static const struct file_operations control_fops = {
    .owner   = THIS_MODULE,
    .open    = control_open,
    .read    = seq_read,
    .write   = control_write,
    .llseek  = seq_lseek,
    .release = single_release,
};

static int __init proc_mod_init(void)
{
    load_time = jiffies;

    /* Create /proc/mydriver/ directory */
    proc_dir = proc_mkdir(PROC_DIR, NULL);
    if (!proc_dir) {
        pr_err("%s: failed to create /proc/%s\n",
               DRIVER_NAME, PROC_DIR);
        return -ENOMEM;
    }

    /* Create /proc/mydriver/status */
    if (!proc_create("status", 0444, proc_dir, &status_fops)) {
        pr_err("%s: failed to create status entry\n",
               DRIVER_NAME);
        proc_remove(proc_dir);
        return -ENOMEM;
    }

    /* Create /proc/mydriver/stats */
    if (!proc_create("stats", 0444, proc_dir, &stats_fops)) {
        pr_err("%s: failed to create stats entry\n",
               DRIVER_NAME);
        proc_remove(proc_dir);
        return -ENOMEM;
    }

    /* Create /proc/mydriver/control (read-write) */
    if (!proc_create("control", 0644, proc_dir, &control_fops)) {
        pr_err("%s: failed to create control entry\n",
               DRIVER_NAME);
        proc_remove(proc_dir);
        return -ENOMEM;
    }

    pr_info("%s: loaded\n", DRIVER_NAME);
    pr_info("%s: /proc/%s/{status,stats,control} created\n",
            DRIVER_NAME, PROC_DIR);
    return 0;
}

static void __exit proc_mod_exit(void)
{
    proc_remove(proc_dir);
    pr_info("%s: unloaded\n", DRIVER_NAME);
}

module_init(proc_mod_init);
module_exit(proc_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ONU Firmware Engineer");
MODULE_DESCRIPTION("Procfs Driver — models /proc/tc3162/ on EN7528HU");
MODULE_VERSION("1.0");
