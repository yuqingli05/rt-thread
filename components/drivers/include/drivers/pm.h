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

#ifndef __PM_H__
#define __PM_H__

#include <stdint.h>
#include <rtthread.h>
#include <drivers/lptimer.h>

/* All modes used for rt_pm_request() and rt_pm_release() */
enum
{
    /* sleep modes */
    PM_SLEEP_MODE_NONE = 0,
    PM_SLEEP_MODE_IDLE,
    PM_SLEEP_MODE_LIGHT,
    PM_SLEEP_MODE_DEEP,
    PM_SLEEP_MODE_STANDBY,
    PM_SLEEP_MODE_SHUTDOWN,
    PM_SLEEP_MODE_MAX,
};

enum
{
    /* run modes*/
    PM_RUN_MODE_HIGH_SPEED = 0,
    PM_RUN_MODE_NORMAL_SPEED,
    PM_RUN_MODE_MEDIUM_SPEED,
    PM_RUN_MODE_LOW_SPEED,
    PM_RUN_MODE_MAX,
};

enum
{
    RT_PM_FREQUENCY_PENDING = 0x01,
};

/* The name of all modes used in the msh command "pm_dump" */
#define PM_SLEEP_MODE_NAMES    \
    {                          \
        "None Mode",           \
            "Idle Mode",       \
            "LightSleep Mode", \
            "DeepSleep Mode",  \
            "Standby Mode",    \
            "Shutdown Mode",   \
    }

#define PM_RUN_MODE_NAMES   \
    {                       \
        "High Speed",       \
            "Normal Speed", \
            "Medium Speed", \
            "Low Mode",     \
    }

#ifdef PM_USING_CUSTOM_CONFIG
#include <pm_cfg.h>
#endif /* PM_USING_CUSTOM_CONFIG */

#ifndef RT_PM_DEFAULT_SLEEP_MODE
#define RT_PM_DEFAULT_SLEEP_MODE PM_SLEEP_MODE_NONE
#endif

#ifndef RT_PM_DEFAULT_DEEPSLEEP_MODE
#define RT_PM_DEFAULT_DEEPSLEEP_MODE PM_SLEEP_MODE_DEEP
#endif

#ifndef RT_PM_DEFAULT_RUN_MODE
#define RT_PM_DEFAULT_RUN_MODE PM_RUN_MODE_NORMAL_SPEED
#endif

/**
 * device control flag to request or release power
 */
#define RT_PM_DEVICE_CTRL_RELEASE (RT_DEVICE_CTRL_BASE(PM) + 0x00)
#define RT_PM_DEVICE_CTRL_REQUEST (RT_DEVICE_CTRL_BASE(PM) + 0x01)

struct rt_pm;

/**
 * low power mode operations
 */
struct rt_pm_ops
{
    void (*sleep)(struct rt_pm *pm, rt_uint8_t mode);
    void (*run)(struct rt_pm *pm, rt_uint8_t mode);
    void (*timer_start)(struct rt_pm *pm, rt_uint32_t timeout);
    void (*timer_stop)(struct rt_pm *pm);
    rt_tick_t (*timer_get_tick)(struct rt_pm *pm);
};

struct rt_device_pm_ops
{
    int (*suspend)(const struct rt_device *device, rt_uint8_t mode);
    void (*resume)(const struct rt_device *device, rt_uint8_t mode);
    int (*frequency_change)(const struct rt_device *device, rt_uint8_t mode);
};

struct rt_device_pm
{
    const struct rt_device *device;
    const struct rt_device_pm_ops *ops;
    rt_uint8_t sleep_mode;
};

struct rt_pm_module
{
#ifdef PM_ENABLE_DEBUG
    char name[RT_NAME_MAX]; /* debug name */
#endif
    rt_uint8_t sleep_mode; /* mode sleep mode */
    rt_list_t list;
};

typedef struct rt_pm_module *rt_pm_module_t;

/**
 * power management
 */
struct rt_pm
{
    struct rt_device parent;

    /* modes */
    rt_uint8_t modes[PM_SLEEP_MODE_MAX];
    rt_uint8_t sleep_mode; /* current sleep mode */
    rt_uint8_t run_mode;   /* current running mode */

    /* modules list*/
    rt_list_t module_list;

#ifdef PM_ENABLE_DEVICE
    /* the list of device, which has PM feature */
    rt_uint8_t device_pm_number;
    struct rt_device_pm *device_pm;
#endif

    /* if the mode has timer, the corresponding bit is 1*/
    rt_uint8_t timer_mask;
    rt_uint8_t flags;

    const struct rt_pm_ops *ops;
};

enum
{
    RT_PM_ENTER_SLEEP = 0,
    RT_PM_EXIT_SLEEP,
};

struct rt_pm_notify
{
    void (*notify)(rt_uint8_t event, rt_uint8_t mode, void *data);
    void *data;
};

/* 传统pm管理 全局管理*/
void rt_pm_request(rt_uint8_t sleep_mode);
void rt_pm_release(rt_uint8_t sleep_mode);
void rt_pm_release_all(rt_uint8_t sleep_mode);
int rt_pm_run_enter(rt_uint8_t run_mode);

#ifdef PM_ENABLE_DEVICE
void rt_pm_device_register(struct rt_device *device, const struct rt_device_pm_ops *ops);
void rt_pm_device_unregister(struct rt_device *device);
#endif

#ifdef PM_ENABLE_NOTIFY
void rt_pm_notify_set(void (*notify)(rt_uint8_t event, rt_uint8_t mode, void *data), void *data);
#endif

rt_uint8_t rt_pm_get_sleep_mode(void);
rt_uint8_t rt_pm_get_run_mode(void);

void rt_system_pm_init(const struct rt_pm_ops *ops,
                       rt_uint8_t timer_mask,
                       void *user_data);

/* 模块PM管理 只需要 rt_pm_module_set_mode 改变模块支持的最低运行模式。 */
/* 模块采用，低功耗请求。模块设置自身可以正常运行的 最佳功耗模式。pm模块自动匹配 最高优先级  */
void rt_pm_module_set_sleepmode(rt_pm_module_t moudle, rt_uint8_t sleep_mode);
rt_uint8_t rt_pm_module_get_sleepmode(rt_pm_module_t moudle);
rt_err_t rt_pm_module_init(rt_pm_module_t moudle, char *name);
void rt_pm_module_detach(rt_pm_module_t moudle);
rt_pm_module_t rt_pm_module_create(char *name);
void rt_pm_module_delete(rt_pm_module_t moudle);

#endif /* __PM_H__ */
