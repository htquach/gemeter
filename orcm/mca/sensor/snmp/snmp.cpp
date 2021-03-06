/*
 * Copyright (c) 2015-2016  Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

// C++
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <string>

// Plugin Includes...
#include "sensor_snmp.h"
#include "snmp.h"
#include "snmp_collector.h"
#include "snmp_parser.h"

extern "C" {
    #include "opal/runtime/opal_progress_threads.h"
    #include "opal/class/opal_list.h"
    #include "orte/util/proc_info.h"
    #include "orte/util/show_help.h"
    #include "orcm/runtime/orcm_globals.h"
    #include "orcm/mca/analytics/analytics.h"
    #include "orcm/mca/sensor/base/sensor_private.h"
    #include "orcm/mca/db/db.h"
    #include "orcm/mca/evgen/evgen_types.h"

    extern orcm_value_t* orcm_util_load_orcm_value(char *key,
                                                   void *data,
                                                   opal_data_type_t type,
                                                   char* units);

    extern orcm_analytics_value_t*
           orcm_util_load_orcm_analytics_value(opal_list_t *key,
                                               opal_list_t *non_compute,
                                               opal_list_t *compute);

    extern orcm_sensor_base_t orcm_sensor_base;
    extern orte_proc_info_t orte_process_info;
}

// Conveniences...
#define SAFE_DELETE(x) if(NULL != x) delete x; x=NULL
#define SAFE_OBJ_RELEASE(x) if(NULL!=x) OBJ_RELEASE(x); x=NULL
#define ON_NULL_THROW(x) if(NULL==x) {        \
    ORTE_ERROR_LOG(ORCM_ERR_OUT_OF_RESOURCE); \
    throw unableToAllocateObj(); }

// For Testing only illegal as job_id...
#define NOOP_JOBID -999

// Static constants...
const std::string snmp_impl::plugin_name_ = "snmp";

// Class Implementation
using namespace std;

snmp_impl::snmp_impl(): ev_paused_(false), ev_base_(NULL), snmp_sampler_(NULL)
{
}

snmp_impl::~snmp_impl()
{
    finalize();
}

int snmp_impl::init(void)
{
    try {
        (void) load_mca_variables();
        snmpParser sp(config_file_);
        collectorObj_ = sp.parse();
    } catch (exception &e) {
        opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                            "ERROR: %s sensor SNMP : init: '%s'",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), e.what());

        orte_show_help("help-orcm-sensor-snmp.txt", "no-snmp", true,
                       (char*)hostname_.c_str());

        return ORCM_ERROR;
    }

    return ORCM_SUCCESS;
}

void snmp_impl::load_mca_variables(void) 
{
    if (NULL != mca_sensor_snmp_component.config_file) {
        config_file_ = string(mca_sensor_snmp_component.config_file);
    }

    if(0 == mca_sensor_snmp_component.sample_rate) {
        mca_sensor_snmp_component.sample_rate = orcm_sensor_base.sample_rate;
    }
    hostname_ = orte_process_info.nodename;
}

void snmp_impl::finalize(void)
{
    stop(0);
    ev_destroy_thread();
}

void snmp_impl::start(orte_jobid_t job)
{
    if(-999 == (int)job) {
        return; // NOOP ID; Succeed without actually starting...
    }
    // start a separate SNMp progress thread for sampling
    if (true == mca_sensor_snmp_component.use_progress_thread) {
        // setup snmp sampler
        snmp_sampler_ = OBJ_NEW(orcm_sensor_sampler_t);

        // Create event thread
        ev_create_thread();
        if (NULL == ev_base_) {
            OBJ_RELEASE(snmp_sampler_);
        }
    }
    return;
}

void snmp_impl::stop(orte_jobid_t job)
{
    if(-999 == (int)job) {
        return; // NOOP ID; Succeed without actually stopping...
    }
    ev_pause();
    if(NULL != snmp_sampler_) {
        OBJ_RELEASE(snmp_sampler_);
    }
}

void snmp_impl::sample(orcm_sensor_sampler_t* sampler)
{
    if(NULL == sampler) {
        ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return;
    }
    if (false == mca_sensor_snmp_component.use_progress_thread) {
        snmp_sampler_ = sampler;
        collect_sample();
        snmp_sampler_ = NULL;
    }
}

void snmp_impl::log(opal_buffer_t* buf)
{
    if(NULL == buf) {
        ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return;
    }

    opal_list_t* compute = NULL;
    opal_list_t* non_compute = NULL;
    opal_list_t* key = NULL;
    orcm_analytics_value_t* analytics_vals = NULL;

    // Modified in order to have easier to read code
    try {
        key = OBJ_NEW(opal_list_t);
        ON_NULL_THROW(key);

        compute = OBJ_NEW(opal_list_t);
        ON_NULL_THROW(compute);

        non_compute = OBJ_NEW(opal_list_t);
        ON_NULL_THROW(non_compute);

        fromOpalBuffer(buf).appendToOpalList(non_compute); // ctime
        vardata vardataHost = fromOpalBuffer(buf);
        vardataHost.appendToOpalList(key);         // hostname

        opal_output_verbose(10, orcm_sensor_base_framework.framework_output,
                            "%s sensor SNMP : received data from: '%s'",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), vardataHost.getValue<char*>());

        vardata(plugin_name_).setKey("data_group").appendToOpalList(key);
        vardata(string(ORCM_COMPONENT_MON)).setKey("component").appendToOpalList(key);
        vardata(string(ORCM_SUBCOMPONENT_MEM)).setKey("sub_component").appendToOpalList(key);

        vector<vardata> compute_vector = unpackDataFromBuffer(buf);

        for(vector<vardata>::iterator it = compute_vector.begin(); it != compute_vector.end(); ++it) {
            analytics_vals = orcm_util_load_orcm_analytics_value(key, non_compute, compute);
            ON_NULL_THROW(analytics_vals);
            ON_NULL_THROW(analytics_vals->key);
            ON_NULL_THROW(analytics_vals->non_compute_data);
            ON_NULL_THROW(analytics_vals->compute_data);

            it->appendToOpalList(analytics_vals->compute_data);

            orcm_analytics.send_data(analytics_vals);
            SAFE_OBJ_RELEASE(analytics_vals);
        }
    } catch (exception &e) {
        opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                            "ERROR: %s sensor SNMP : init: '%s'",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), e.what());
    }
    SAFE_OBJ_RELEASE(compute);
    SAFE_OBJ_RELEASE(non_compute);
    SAFE_OBJ_RELEASE(analytics_vals);
}


void snmp_impl::set_sample_rate(int sample_rate)
{
    // set the errcounts sample rate if seperate thread is enabled
    if (true == mca_sensor_snmp_component.use_progress_thread) {
        mca_sensor_snmp_component.sample_rate = sample_rate;
    } else {
        opal_output_verbose(5, orcm_sensor_base_framework.framework_output,
                            "%s sensor snmp : set_sample_rate: called but not using"
                             "per-thread sampling", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    }
}

void snmp_impl::get_sample_rate(int* sample_rate)
{
    if(NULL == sample_rate) {
        ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return;
    }
    *sample_rate = mca_sensor_snmp_component.sample_rate;
    if (false == mca_sensor_snmp_component.use_progress_thread) {
        opal_output_verbose(5, orcm_sensor_base_framework.framework_output,
                            "%s sensor snmp : get_sample_rate: called but not using"
                            "per-thread sampling", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    }
}


void snmp_impl::perthread_snmp_sample_relay(int fd, short args, void *user_data)
{
    (void)fd;
    (void)args;
    if(NULL != user_data) {
        ((snmp_impl*)user_data)->perthread_snmp_sample();
    }
}

void snmp_impl::perthread_snmp_sample()
{
    collect_sample(true);

    ORCM_SENSOR_XFER(&snmp_sampler_->bucket);

    // clear the bucket
    OBJ_DESTRUCT(&snmp_sampler_->bucket);
    OBJ_CONSTRUCT(&snmp_sampler_->bucket, opal_buffer_t);

    // check if sample rate is provided for this
    if (mca_sensor_snmp_component.sample_rate != snmp_sampler_->rate.tv_sec) {
        snmp_sampler_->rate.tv_sec = mca_sensor_snmp_component.sample_rate;
    }
    // set ourselves to sample again
    opal_event_evtimer_add(&snmp_sampler_->ev, &snmp_sampler_->rate);
}


// Common data collection...
void snmp_impl::collect_sample(bool perthread /* = false*/)
{
    if(true == perthread) {
        opal_output_verbose(5, orcm_sensor_base_framework.framework_output,
                            "%s sensor snmp : perthread_snmp_sample: called",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    } else {
        opal_output_verbose(5, orcm_sensor_base_framework.framework_output,
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    }

    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    try {
        // opal_buffer_t *buffer = &snmp_sampler_->bucket;

        opal_buffer_t buffer;
        OBJ_CONSTRUCT(&buffer, opal_buffer_t);

        packPluginName(&buffer);

        vardata(current_time).setKey(string("ctime")).packTo(&buffer);
        vardata(hostname_).setKey(string("hostname")).packTo(&buffer);
        collectAndPackDataSamples(&buffer);

        opal_buffer_t* bptr = &buffer;
        int rc;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&snmp_sampler_->bucket, &bptr, 1, OPAL_BUFFER))) {
            ORTE_ERROR_LOG(rc);
        }
    } catch (exception &e) {
        opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                            "ERROR: %s sensor SNMP : collect_data: '%s'",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), e.what());
    }
}

void snmp_impl::packPluginName(opal_buffer_t* buffer) {
    int rc;
    char* str_ptr = strdup(plugin_name_.c_str());
    if(OPAL_SUCCESS != (rc = opal_dss.pack(buffer, &str_ptr, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        if (NULL != str_ptr) {
            free(str_ptr);
        }
        return;
    }
    if (NULL != str_ptr) {
        free(str_ptr);
    }
    return;
}


void snmp_impl::collectAndPackDataSamples(opal_buffer_t *buffer) {
    for(vector<snmpCollector>::iterator it = collectorObj_.begin(); it != collectorObj_.end(); ++it) {
        try {
            vector<vardata> dataSamples = it->collectData();
            packDataToBuffer(dataSamples, buffer);
        } catch (exception &e) {
            opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                                "WARNING: %s sensor SNMP : unable to collect sample: '%s'",
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), e.what());
        }
    }
}

void snmp_impl::ev_pause()
{
    if(NULL != ev_base_ && false == ev_paused_) {
        if(OPAL_SUCCESS == opal_progress_thread_pause("snmp")) {
            ev_paused_ = true;
        }
    }
}

void snmp_impl::ev_resume()
{
    if(NULL != ev_base_ && true == ev_paused_) {
        if(OPAL_SUCCESS == opal_progress_thread_resume("snmp")) {
            ev_paused_ = false;
        }
    }
}

void snmp_impl::ev_create_thread()
{
    if(NULL == ev_base_ && NULL != snmp_sampler_) {
        if (NULL == (ev_base_ = opal_progress_thread_init("snmp"))) {
            ORTE_ERROR_LOG(ORCM_ERROR);
            return;
        }
        snmp_sampler_->rate.tv_sec = mca_sensor_snmp_component.sample_rate;
        snmp_sampler_->log_data = orcm_sensor_base.log_samples;
        opal_event_evtimer_set(ev_base_, &snmp_sampler_->ev,
                               perthread_snmp_sample_relay, this);
        opal_event_evtimer_add(&snmp_sampler_->ev, &snmp_sampler_->rate);
        ev_paused_ = false;
    }
}

void snmp_impl::ev_destroy_thread()
{
    if(NULL != ev_base_) {
        opal_progress_thread_finalize("snmp");
        ev_base_ = NULL;
        ev_paused_ = false;
    }
}
