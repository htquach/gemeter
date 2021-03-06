/*
 * Copyright (c) 2013-2016 Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/** @file:
 */

#ifndef MCA_ANALYTICS_PRIVATE_H
#define MCA_ANALYTICS_PRIVATE_H

#include "orcm/mca/analytics/base/base.h"
#include "orcm/mca/evgen/evgen.h"
#include "orcm/mca/evgen/base/base.h"

BEGIN_C_DECLS

ORCM_DECLSPEC int orcm_analytics_base_workflow_create(opal_buffer_t* buffer, int *wfid);
ORCM_DECLSPEC int orcm_analytics_base_workflow_delete(int workflow_id);
ORCM_DECLSPEC int orcm_analytics_base_workflow_list(opal_buffer_t *buffer);
ORCM_DECLSPEC int orcm_analytics_base_comm_start(void);
ORCM_DECLSPEC int orcm_analytics_base_comm_stop(void);
ORCM_DECLSPEC int orcm_analytics_base_recv_pack_int(opal_buffer_t *buffer, int *value, int count);
ORCM_DECLSPEC int orcm_analytics_base_select_workflow_step(orcm_workflow_step_t *workflow);
ORCM_DECLSPEC void orcm_analytics_stop_wokflow(orcm_workflow_t *wf);
ORCM_DECLSPEC void orcm_analytics_base_activate_analytics_workflow_step(orcm_workflow_t *wf,
                                                                        orcm_workflow_step_t *wf_step,
                                                                        uint64_t hash_key,
                                                                        orcm_analytics_value_t *data);

ORCM_DECLSPEC void orcm_analytics_base_send_data(orcm_analytics_value_t *data);

/*function to verify whether workflow step has attribute to store in db or not*/
bool orcm_analytics_base_db_check(orcm_workflow_step_t *wf_step);

/* function to store data of type orcm_analytics_value_t*/
ORCM_DECLSPEC int orcm_analytics_base_store_analytics(orcm_analytics_value_t *analytics_data);

/* orcm_analytics_base_event_create
 * Arguments: orcm_analytics_value_t *analytics_data, int type, int severity
 * Description: function to create eventgen structure for analytics data of type orcm_analytics_value_t
 * Returns: If failed function returns NULL and upon success it returns struct of type orcm_ras_event_t*
 * */
ORCM_DECLSPEC orcm_ras_event_t* orcm_analytics_base_event_create(orcm_analytics_value_t *analytics_data, int type, int severity);

/* orcm_analytics_base_event_set_category
 * Arguments: orcm_ras_event_t *analytics_event_data, unsigned int event_category
 * Description: function to append event category into the event description list
 * Returns: If success function returns ORCM_SUCCESS and if failed function returns Error(ORCM_ERR*)
 * */
ORCM_DECLSPEC int orcm_analytics_base_event_set_category(orcm_ras_event_t *analytics_event_data, unsigned int event_category);

/* orcm_analytics_base_event_set_storage
 * Arguments: orcm_ras_event_t *analytics_event_data, unsigned int storage_type
 * Description: function to set storage type into the event description list
 * Returns: If success function returns ORCM_SUCCESS and if failed function returns Error(ORCM_ERR*)
 * */
ORCM_DECLSPEC int orcm_analytics_base_event_set_storage(orcm_ras_event_t *analytics_event_data, unsigned int storage_type);

/* orcm_analytics_base_event_set_description
 * Arguments: orcm_ras_event_t *analytics_event_data, char *key, void *data, opal_data_type_t type, char *units
 * Description: function to set data into the event description list
 * Returns: If success function returns ORCM_SUCCESS and if failed function returns Error(ORCM_ERR*)
 * */
ORCM_DECLSPEC int orcm_analytics_base_event_set_description(orcm_ras_event_t *analytics_event_data, char *key,
                                                            void *data, opal_data_type_t type, char *units);

/* orcm_analytics_base_event_set_reporter
 * Arguments: orcm_ras_event_t *analytics_event_data, char *key, void *data, opal_data_type_t type, char *units
 * Description: function to set data into the event reporter list
 * Returns: If success function returns ORCM_SUCCESS and if failed function returns Error(ORCM_ERR*)
 * */
ORCM_DECLSPEC int orcm_analytics_base_event_set_reporter(orcm_ras_event_t *analytics_event_data, char *key,
                                                         void *data, opal_data_type_t type, char *units);

/* function to assert that the input caddy data is valid */
int orcm_analytics_base_assert_caddy_data(void *cbdata);

/*function to log analytics value to database*/
int orcm_analytics_base_log_to_database_event(orcm_analytics_value_t* value);

/* function to convert the timeval to uint64_t with rounded microsecond */
uint64_t orcm_analytics_base_timeval_to_uint64(struct timeval time);

/* function to get the time in uint64_t for a list of coming data samples */
int orcm_analytics_base_get_sample_time(opal_list_t *list, uint64_t *sample_time);

#define ANALYTICS_COUNT_DEFAULT 1
#define MAX_ALLOWED_ATTRIBUTES_PER_WORKFLOW_STEP 2

typedef struct {
    /* list of active workflows */
    opal_list_t workflows;
    bool store_raw_data;
    bool store_event_data;
} orcm_analytics_base_t;
ORCM_DECLSPEC extern orcm_analytics_base_t orcm_analytics_base;

/* executes the next workflow step in a workflow */
#define ORCM_ACTIVATE_NEXT_WORKFLOW_STEP(wf, prev_wf_step, hash_key, data)         \
    do {                                                                           \
        opal_list_item_t *list_item = opal_list_get_next(prev_wf_step);            \
        if (NULL == list_item){                                                    \
            opal_output_verbose(1, orcm_analytics_base_framework.framework_output, \
                                "%s TRIED TO ACTIVATE EMPTY WORKFLOW %d AT %s:%d", \
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),                \
                                (wf)->workflow_id,                                 \
                                __FILE__, __LINE__);                               \
            OBJ_RELEASE(data);                                                     \
            break;                                                                 \
        }                                                                          \
        orcm_workflow_step_t *wf_step_item = (orcm_workflow_step_t *)list_item;    \
        if (list_item == opal_list_get_end(&(wf->steps))) {                        \
            opal_output_verbose(1, orcm_analytics_base_framework.framework_output, \
                                "%s END OF WORKFLOW %d AT %s:%d",                  \
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),                \
                                (wf)->workflow_id,                                 \
                                __FILE__, __LINE__);                               \
            OBJ_RELEASE(data);                                                     \
        } else {                                                                   \
            opal_output_verbose(1, orcm_analytics_base_framework.framework_output, \
                                "%s ACTIVATE NEXT WORKFLOW %d MODULE %s AT %s:%d", \
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),                \
                                (wf)->workflow_id, wf_step_item->analytic,         \
                                __FILE__, __LINE__);                               \
            orcm_analytics_base_activate_analytics_workflow_step((wf),             \
                                                                 wf_step_item,     \
                                                                 hash_key,         \
                                                                 (data));          \
        }                                                                          \
    }while(0);
END_C_DECLS
#endif
