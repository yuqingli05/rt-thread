/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-06-02     Bernard      the first version
 * 2018-08-02     Tanek        split run and sleep modes, support custom mode
 * 2019-04-28     Zero-Free    improve PM mode and device ops interface
 * 2020-11-23     zhangsz      update pm mode select
 * 2020-11-27     zhangsz      update pm 2.0
 */

#include <rthw.h>
#include <rtthread.h>
#include <drivers/pm.h>
#include <stdlib.h>

#ifdef RT_USING_PM

#ifdef PM_ENABLE_THRESHOLD_SLEEP_MODE

#ifndef PM_LIGHT_THRESHOLD_TIME
#define PM_LIGHT_THRESHOLD_TIME 5
#endif

#ifndef PM_DEEP_THRESHOLD_TIME
#define PM_DEEP_THRESHOLD_TIME 20
#endif

#ifndef PM_STANDBY_THRESHOLD_TIME
#define PM_STANDBY_THRESHOLD_TIME 100
#endif

#else

#ifndef PM_TICKLESS_THRESHOLD_TIME
#define PM_TICKLESS_THRESHOLD_TIME 5
#endif

#endif

static struct rt_pm _pm;

#ifdef PM_ENABLE_NOTIFY
static struct rt_pm_notify _pm_notify;
#endif

#ifdef PM_ENABLE_DEBUG
rt_slist_t rt_pm_id_list = {.next = NULL};
#endif

static rt_uint8_t _pm_init_flag = 0;

RT_WEAK rt_uint32_t rt_pm_enter_critical(rt_uint8_t sleep_mode)
{
    rt_enter_critical();
    return 0;
}

RT_WEAK void rt_pm_exit_critical(rt_uint32_t ctx, rt_uint8_t sleep_mode)
{
    rt_exit_critical();
}

/* lptimer start */
rt_inline void pm_lptimer_start(struct rt_pm *pm, uint32_t timeout)
{
    if (pm->ops == RT_NULL)
        return;

    if (pm->ops->timer_start != RT_NULL)
        pm->ops->timer_start(pm, timeout);
}

/* lptimer stop */
rt_inline void pm_lptimer_stop(struct rt_pm *pm)
{
    if (pm->ops == RT_NULL)
        return;

    if (pm->ops->timer_stop != RT_NULL)
        pm->ops->timer_stop(pm);
}

/* lptimer get timeout tick */
rt_inline rt_tick_t pm_lptimer_get_timeout(struct rt_pm *pm)
{
    if (pm->ops == RT_NULL)
        return RT_TICK_MAX;

    if (pm->ops->timer_get_tick != RT_NULL)
        return pm->ops->timer_get_tick(pm);

    return RT_TICK_MAX;
}

/* enter sleep mode */
rt_inline void pm_sleep(struct rt_pm *pm, uint8_t sleep_mode)
{
    if (pm->ops == RT_NULL)
        return;

    if (pm->ops->sleep != RT_NULL)
        pm->ops->sleep(pm, sleep_mode);
}

/* enter sleep mode */
rt_inline void pm_run(struct rt_pm *pm, rt_uint8_t run_mode)
{
    if (pm->ops == RT_NULL)
        return;

    if (pm->ops->run != RT_NULL)
        pm->ops->run(pm, run_mode);
}

#ifdef PM_ENABLE_DEVICE
/**
 * This function will suspend all registered devices
 * 返回值是 设备所以允许的 休眠等级
 */
static rt_uint8_t _pm_device_suspend(struct rt_pm *pm, rt_uint8_t mode)
{
    int index, ret = RT_EOK;
    rt_uint8_t sleepmode = mode;

    for (index = 0; index < pm->device_pm_number; index++)
    {
        if (pm->device_pm[index].ops->suspend != RT_NULL)
        {
            ret = pm->device_pm[index].ops->suspend(pm->device_pm[index].device, mode);
            pm->device_pm[index].sleep_mode = ret;
            if (ret < sleepmode)
                sleepmode = ret;
        }
    }

    return sleepmode;
}

/**
 * This function will resume all registered devices
 */
static void _pm_device_resume(struct rt_pm *pm)
{
    int index;

    for (index = 0; index < pm->device_pm_number; index++)
    {
        if (pm->device_pm[index].ops->resume != RT_NULL)
        {
            pm->device_pm[index].ops->resume(pm->device_pm[index].device, pm->device_pm[index].sleep_mode);
        }
    }
}

/**
 * This function will update the frequency of all registered devices
 */
static void _pm_device_frequency_change(struct rt_pm *pm, rt_uint8_t mode)
{
    rt_uint32_t index;

    /* make the frequency change */
    for (index = 0; index < pm->device_pm_number; index++)
    {
        if (pm->device_pm[index].ops->frequency_change != RT_NULL)
            pm->device_pm[index].ops->frequency_change(pm->device_pm[index].device, mode);
    }
}

/**
 * Register a device with PM feature
 *
 * @param device the device with PM feature
 * @param ops the PM ops for device
 */
void rt_pm_device_register(struct rt_device *device, const struct rt_device_pm_ops *ops)
{
    struct rt_pm *pm;
    register rt_base_t level;
    struct rt_device_pm *device_pm;

    if (_pm_init_flag == 0)
        return;

    RT_DEBUG_NOT_IN_INTERRUPT;

    pm = &_pm;
    level = rt_hw_interrupt_disable();

    device_pm = (struct rt_device_pm *)RT_KERNEL_REALLOC(pm->device_pm,
                                                         (pm->device_pm_number + 1) * sizeof(struct rt_device_pm));
    if (device_pm != RT_NULL)
    {
        pm->device_pm = device_pm;
        pm->device_pm[pm->device_pm_number].device = device;
        pm->device_pm[pm->device_pm_number].ops = ops;
        pm->device_pm_number += 1;
    }

    rt_hw_interrupt_enable(level);
}

/**
 * Unregister device from PM manager.
 *
 * @param device the device with PM feature
 */
void rt_pm_device_unregister(struct rt_device *device)
{
    struct rt_pm *pm;
    register rt_base_t level;
    rt_uint32_t index;

    if (_pm_init_flag == 0)
        return;

    RT_DEBUG_NOT_IN_INTERRUPT;

    pm = &_pm;
    level = rt_hw_interrupt_disable();

    for (index = 0; index < pm->device_pm_number; index++)
    {
        if (pm->device_pm[index].device == device)
        {
            /* remove current entry */
            for (; index < pm->device_pm_number - 1; index++)
            {
                pm->device_pm[index] = pm->device_pm[index + 1];
            }

            pm->device_pm[pm->device_pm_number - 1].device = RT_NULL;
            pm->device_pm[pm->device_pm_number - 1].ops = RT_NULL;

            pm->device_pm_number -= 1;
            /* break out and not touch memory */
            break;
        }
    }

    rt_hw_interrupt_enable(level);
}

#endif

/**
 * 更新休眠模式
 */
rt_inline void _pm_update_sleep_mode(struct rt_pm *pm)
{
    int index;
    for (index = PM_SLEEP_MODE_NONE; index < PM_SLEEP_MODE_MAX; index++)
    {
        if (pm->modes[index])
        {
            pm->sleep_mode = index;
            return;
        }
    }
    pm->sleep_mode = RT_PM_DEFAULT_SLEEP_MODE;
}

RT_WEAK rt_tick_t pm_timer_next_timeout_tick(rt_uint8_t mode)
{
    switch (mode)
    {
    case PM_SLEEP_MODE_IDLE:
    case PM_SLEEP_MODE_LIGHT:
        return rt_timer_next_timeout_tick();
    case PM_SLEEP_MODE_DEEP:
    case PM_SLEEP_MODE_STANDBY:
        return rt_lptimer_next_timeout_tick();
    }

    return RT_TICK_MAX;
}

/**
 * This function will judge sleep mode from threshold timeout.
 *
 * @param cur_mode the current pm sleep mode
 * @param timeout_tick the threshold timeout
 *
 * @return none
 */
RT_WEAK rt_uint8_t pm_get_sleep_threshold_mode(rt_uint8_t cur_mode, rt_tick_t timeout_tick)
{

    if (cur_mode >= PM_SLEEP_MODE_MAX)
        return cur_mode;

#ifdef PM_ENABLE_THRESHOLD_SLEEP_MODE
    rt_uint8_t tick_sleep_mode;
    if (timeout_tick >= PM_STANDBY_THRESHOLD_TIME)
    {
        tick_sleep_mode = PM_SLEEP_MODE_STANDBY;
    }
    else if (timeout_tick >= PM_DEEP_THRESHOLD_TIME)
    {
        tick_sleep_mode = PM_SLEEP_MODE_DEEP;
    }
    else if (timeout_tick >= PM_LIGHT_THRESHOLD_TIME)
    {
        tick_sleep_mode = PM_SLEEP_MODE_LIGHT;
    }
    else
    {
        tick_sleep_mode = PM_SLEEP_MODE_IDLE;
    }

    if (tick_sleep_mode < cur_mode)
        cur_mode = tick_sleep_mode;
#else
    if (timeout_tick < PM_TICKLESS_THRESHOLD_TIME)
    {
        cur_mode = PM_SLEEP_MODE_IDLE;
    }
#endif

    return cur_mode;
}

/**
 * This function changes the power sleep mode base on the result of selection
 */
rt_inline void _pm_change_sleep_mode(struct rt_pm *pm)
{
    rt_tick_t timeout_tick = 0, delta_tick = 0;
    rt_base_t level_pm;
    uint8_t sleep_mode = pm->sleep_mode;

    if (sleep_mode > PM_SLEEP_MODE_NONE)
    {
        level_pm = rt_pm_enter_critical(sleep_mode);

        /* Tickless*/
        if (pm->timer_mask & (0x01 << sleep_mode))
        {
            timeout_tick = pm_timer_next_timeout_tick(sleep_mode);
            timeout_tick = timeout_tick - rt_tick_get();
            /* Judge sleep_mode from threshold time */
            sleep_mode = pm_get_sleep_threshold_mode(sleep_mode, timeout_tick);
        }

#ifdef PM_ENABLE_DEVICE
        /* Suspend all peripheral device */
        int ret = _pm_device_suspend(pm, sleep_mode);
        if (sleep_mode > ret)
        {
            sleep_mode = ret;
        }
#endif

#ifdef PM_ENABLE_NOTIFY
        /* Notify app will enter sleep mode */
        if (_pm_notify.notify)
            _pm_notify.notify(RT_PM_ENTER_SLEEP, sleep_mode, _pm_notify.data);
#endif

        if (pm->timer_mask & (0x01 << sleep_mode))
        {
            pm_lptimer_start(pm, timeout_tick);
        }
        /* enter lower power state */
        pm_sleep(pm, sleep_mode);

        /* wake up from lower power state*/
        if (pm->timer_mask & (0x01 << sleep_mode))
        {
            delta_tick = pm_lptimer_get_timeout(pm);
            pm_lptimer_stop(pm);
            if (delta_tick)
            {
                rt_base_t level;
                level = rt_hw_interrupt_disable();
                rt_tick_set(rt_tick_get() + delta_tick);
                rt_hw_interrupt_enable(level);
            }
        }

#ifdef PM_ENABLE_DEVICE
        /* resume all device */
        _pm_device_resume(pm);
#endif

#ifdef PM_ENABLE_NOTIFY
        if (_pm_notify.notify)
            _pm_notify.notify(RT_PM_EXIT_SLEEP, sleep_mode, _pm_notify.data);
#endif

        rt_pm_exit_critical(level_pm, sleep_mode);

        if (pm->timer_mask & (0x01 << sleep_mode))
        {
            if (delta_tick)
            {
                rt_timer_check();
            }
        }
    }
}

/**
 * This function will update the system clock frequency when idle
 */
rt_inline void _pm_frequency_scaling(struct rt_pm *pm)
{
    register rt_base_t level;

    if (pm->flags & RT_PM_FREQUENCY_PENDING)
    {
        level = rt_hw_interrupt_disable();
        /* change system runing mode */
        pm->ops->run(pm, pm->run_mode);
#ifdef PM_ENABLE_DEVICE
        /* changer device frequency */
        _pm_device_frequency_change(pm, pm->run_mode);
#endif
        pm->flags &= ~RT_PM_FREQUENCY_PENDING;
        rt_hw_interrupt_enable(level);
    }
}
/**
 * This function will enter corresponding power mode.
 */
void rt_system_power_manager(void)
{
    if (_pm_init_flag == 0)
        return;
    /* CPU frequency scaling according to the runing mode settings */
    _pm_frequency_scaling(&_pm);

    /* Low Power Mode Processing */
    _pm_change_sleep_mode(&_pm);
}

/**
 * Upper application or device driver requests the system
 * stall in corresponding power mode.
 *
 * @param parameter the parameter of run mode or sleep mode
 */
void rt_pm_request(rt_uint8_t mode)
{
    register rt_base_t level;
    struct rt_pm *pm;

    if (_pm_init_flag == 0)
        return;

    if (mode > (PM_SLEEP_MODE_MAX - 1))
        return;

    level = rt_hw_interrupt_disable();
    pm = &_pm;
    if (pm->modes[mode] < 255)
        pm->modes[mode]++;
    _pm_update_sleep_mode(pm);
    rt_hw_interrupt_enable(level);
}

/**
 * Upper application or device driver releases the stall
 * of corresponding power mode.
 *
 * @param parameter the parameter of run mode or sleep mode
 *
 */
void rt_pm_release(rt_uint8_t mode)
{
    register rt_base_t level;
    struct rt_pm *pm;

    if (_pm_init_flag == 0)
        return;

    if (mode > (PM_SLEEP_MODE_MAX - 1))
        return;

    level = rt_hw_interrupt_disable();
    pm = &_pm;
    if (pm->modes[mode] > 0)
        pm->modes[mode]--;
    _pm_update_sleep_mode(pm);
    rt_hw_interrupt_enable(level);
}

/**
 * Upper application or device driver releases all the stall
 * of corresponding power mode.
 *
 * @param parameter the parameter of run mode or sleep mode
 *
 */
void rt_pm_release_all(rt_uint8_t mode)
{
    register rt_base_t level;
    struct rt_pm *pm;

    if (_pm_init_flag == 0)
        return;

    if (mode > (PM_SLEEP_MODE_MAX - 1))
        return;

    level = rt_hw_interrupt_disable();
    pm = &_pm;
    pm->modes[mode] = 0;
    _pm_update_sleep_mode(pm);
    rt_hw_interrupt_enable(level);
}
int rt_pm_run_enter(rt_uint8_t mode)
{
    register rt_base_t level;
    struct rt_pm *pm;

    if (_pm_init_flag == 0)
        return -RT_EIO;

    if (mode > PM_RUN_MODE_MAX)
        return -RT_EINVAL;

    level = rt_hw_interrupt_disable();
    pm = &_pm;
    if (mode != pm->run_mode)
    {
        pm->run_mode = mode;
        if (mode < pm->run_mode)
        {
            /* change system runing mode */
            pm->ops->run(pm, mode);
#ifdef PM_ENABLE_DEVICE
            /* changer device frequency */
            _pm_device_frequency_change(pm, mode);
#endif
        }
        else
        {
            pm->flags |= RT_PM_FREQUENCY_PENDING;
        }
    }

    rt_hw_interrupt_enable(level);

    return RT_EOK;
}
rt_uint8_t rt_pm_get_sleep_mode(void)
{
    struct rt_pm *pm;

    if (_pm_init_flag == 0)
        return PM_SLEEP_MODE_NONE;

    pm = &_pm;

    return pm->sleep_mode;
}
rt_uint8_t rt_pm_get_run_mode(void)
{
    struct rt_pm *pm;

    if (_pm_init_flag == 0)
        return PM_RUN_MODE_NORMAL_SPEED;

    pm = &_pm;

    return pm->run_mode;
}

void rt_pm_id_set_sleepmode(struct rt_pm_id *id, rt_uint8_t sleep_mode)
{
    register rt_base_t level;
    level = rt_hw_interrupt_disable();

    if (id->sleep_mode != sleep_mode)
    {
        rt_pm_release(id->sleep_mode);
        id->sleep_mode = sleep_mode;
        rt_pm_request(id->sleep_mode);
    }

    rt_hw_interrupt_enable(level);
}
rt_uint8_t rt_pm_id_get_sleepmode(struct rt_pm_id *id)
{
    return id->sleep_mode;
}
void rt_pm_id_init(struct rt_pm_id *id, char *name)
{
    register rt_base_t level;

    level = rt_hw_interrupt_disable();

#ifdef PM_ENABLE_DEBUG
    rt_strncpy(id->name, name, RT_NAME_MAX);
    rt_slist_insert(&rt_pm_id_list, &id->list);
#endif

    id->sleep_mode = RT_PM_DEFAULT_SLEEP_MODE;
    rt_pm_request(id->sleep_mode);

    rt_hw_interrupt_enable(level);
}
void rt_pm_id_detach(struct rt_pm_id *id)
{
    register rt_base_t level;
    level = rt_hw_interrupt_disable();
    rt_pm_release(id->sleep_mode);

#ifdef PM_ENABLE_DEBUG
    rt_slist_remove(&rt_pm_id_list, &id->list);
#endif

    rt_hw_interrupt_enable(level);
}

/**
 * RT-Thread device interface for PM device
 */
static rt_size_t _rt_pm_device_read(rt_device_t dev,
                                    rt_off_t pos,
                                    void *buffer,
                                    rt_size_t size)
{
    struct rt_pm *pm;
    rt_size_t length;

    length = 0;
    pm = (struct rt_pm *)dev;
    RT_ASSERT(pm != RT_NULL);

    if (pos < PM_SLEEP_MODE_MAX)
    {
        int mode;

        mode = pm->modes[pos];
        length = rt_snprintf(buffer, size, "%d", mode);
    }

    return length;
}

static rt_size_t _rt_pm_device_write(rt_device_t dev,
                                     rt_off_t pos,
                                     const void *buffer,
                                     rt_size_t size)
{
    unsigned char request;

    if (size)
    {
        /* get request */
        request = *(unsigned char *)buffer;
        if (request == 0x01)
        {
            rt_pm_request(pos);
        }
        else if (request == 0x00)
        {
            rt_pm_release(pos);
        }
    }

    return 1;
}

static rt_err_t _rt_pm_device_control(rt_device_t dev,
                                      int cmd,
                                      void *args)
{
    rt_uint32_t mode;

    switch (cmd)
    {
    case RT_PM_DEVICE_CTRL_REQUEST:
        mode = (rt_uint32_t)args;
        rt_pm_request(mode);
        break;

    case RT_PM_DEVICE_CTRL_RELEASE:
        mode = (rt_uint32_t)args;
        rt_pm_release(mode);
        break;
    }

    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops pm_ops =
    {
        RT_NULL,
        RT_NULL,
        RT_NULL,
        _rt_pm_device_read,
        _rt_pm_device_write,
        _rt_pm_device_control,
};
#endif

/**
 * This function will initialize power management.
 *
 * @param ops the PM operations.
 * @param timer_mask indicates which mode has timer feature.
 * @param user_data user data
 */
void rt_system_pm_init(const struct rt_pm_ops *ops,
                       rt_uint8_t timer_mask,
                       void *user_data)
{
    struct rt_device *device;
    struct rt_pm *pm;

    pm = &_pm;
    device = &(_pm.parent);

    device->type = RT_Device_Class_PM;
    device->rx_indicate = RT_NULL;
    device->tx_complete = RT_NULL;

#ifdef RT_USING_DEVICE_OPS
    device->ops = &pm_ops;
#else
    device->init = RT_NULL;
    device->open = RT_NULL;
    device->close = RT_NULL;
    device->read = _rt_pm_device_read;
    device->write = _rt_pm_device_write;
    device->control = _rt_pm_device_control;
#endif
    device->user_data = user_data;

    /* register PM device to the system */
    rt_device_register(device, "pm", RT_DEVICE_FLAG_RDWR);

    /* when system power on, set default sleep modes */
    rt_memset(pm->modes, 0, sizeof(pm->modes));
    pm->sleep_mode = RT_PM_DEFAULT_SLEEP_MODE;
    pm->run_mode = RT_PM_DEFAULT_RUN_MODE;
    pm->timer_mask = timer_mask;

    pm->ops = ops;

#ifdef PM_ENABLE_DEVICE
    pm->device_pm = RT_NULL;
    pm->device_pm_number = 0;
#endif

#if IDLE_THREAD_STACK_SIZE <= 256
#error "[pm.c ERR] IDLE Stack Size Too Small!"
#endif

    _pm_init_flag = 1;
}

#endif /* RT_USING_PM */
