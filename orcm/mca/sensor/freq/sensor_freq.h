/*
 * Copyright (c) 2013-2014 Intel, Inc. All rights reserved.
 *
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */
/**
 * @file
 *
 * FREQ resource manager sensor 
 */
#ifndef ORCM_SENSOR_FREQ_H
#define ORCM_SENSOR_FREQ_H

#include "orcm_config.h"

#include "orcm/mca/sensor/sensor.h"

BEGIN_C_DECLS

typedef struct {
    orcm_sensor_base_component_t super;
    bool test;
    char *policy;
    bool pstate;
    bool use_progress_thread;
    int sample_rate;
} orcm_sensor_freq_component_t;

typedef struct {
    opal_event_base_t *ev_base;
    bool ev_active;
    int sample_rate;
} orcm_sensor_freq_t;

ORCM_MODULE_DECLSPEC extern orcm_sensor_freq_component_t mca_sensor_freq_component;
extern orcm_sensor_base_module_t orcm_sensor_freq_module;


END_C_DECLS

#endif
