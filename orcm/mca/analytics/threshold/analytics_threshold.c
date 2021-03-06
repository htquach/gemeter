/*
* Copyright (c) 2014      Intel, Inc. All rights reserved.
* $COPYRIGHT$
*
* Additional copyrights may follow
*
* $HEADER$
*/

#include "orcm_config.h"
#include "orcm/constants.h"

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include "opal/util/output.h"

#include "orte/util/name_fns.h"
#include "orte/runtime/orte_globals.h"

#include "orcm/mca/analytics/base/base.h"

#include "orcm/mca/analytics/analytics_types.h"
#include "orcm/mca/analytics/base/analytics_private.h"
#include "orcm/mca/analytics/threshold/analytics_threshold.h"

#include "orcm/runtime/orcm_globals.h"
#include "orte/mca/notifier/notifier.h"
#include "opal/class/opal_list.h"

#include <stdarg.h>
#include <limits.h>
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include "orcm/util/utils.h"


static int init(orcm_analytics_base_module_t *imod);
static void finalize(orcm_analytics_base_module_t *imod);
static int analyze(int sd, short args, void *cbdata);


static void threshold_policy_t_con(orcm_mca_analytics_threshold_policy_t *policy)
{
    policy->hi = 0.0;
    policy->hi_action = NULL;
    policy->hi_sev = ORTE_NOTIFIER_INFO;
    policy->low = 0.0;
    policy->low_action = NULL;
    policy->low_sev = ORTE_NOTIFIER_INFO;
}

static void threshold_policy_t_des(orcm_mca_analytics_threshold_policy_t *policy)
{
    SAFEFREE(policy->hi_action);
    SAFEFREE(policy->low_action);
}

OBJ_CLASS_INSTANCE(orcm_mca_analytics_threshold_policy_t,
                   opal_object_t,
                   threshold_policy_t_con, threshold_policy_t_des);

mca_analytics_threshold_module_t orcm_analytics_threshold_module = {
    {
        init,
        finalize,
        analyze,
        NULL,
    }
};

static int init(orcm_analytics_base_module_t *imod)
{
    if(NULL == imod) {
        return ORCM_ERROR;
    }
    return ORCM_SUCCESS;
}

static void finalize(orcm_analytics_base_module_t *imod)
{
OPAL_OUTPUT_VERBOSE((5, orcm_analytics_base_framework.framework_output,
                     "%s analytics:threshold:finalize",
                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
}

static orte_notifier_severity_t get_severity(char* severity)
{
    orte_notifier_severity_t sev;
    if ( 0 == strcmp(severity, "emerg") ) {
        sev = ORTE_NOTIFIER_EMERG;
    } else if ( 0 == strcmp(severity, "alert") ) {
        sev = ORTE_NOTIFIER_ALERT;
    } else if ( 0 == strcmp(severity, "crit") ) {
        sev = ORTE_NOTIFIER_CRIT;
    } else if ( 0 == strcmp(severity, "error") ) {
        sev = ORTE_NOTIFIER_ERROR;
    } else if ( 0 == strcmp(severity, "warn") ) {
        sev = ORTE_NOTIFIER_WARN;
    } else if ( 0 == strcmp(severity, "notice") ) {
        sev = ORTE_NOTIFIER_NOTICE;
    } else if ( 0 == strcmp(severity, "info") ) {
        sev = ORTE_NOTIFIER_INFO;
    } else if ( 0 == strcmp(severity, "debug") ) {
        sev = ORTE_NOTIFIER_DEBUG;
    } else {
        sev = ORTE_NOTIFIER_INFO;
    }
    return sev;
}

static int generate_notification_event(orcm_analytics_value_t* analytics_value,orte_notifier_severity_t sev, char *msg, char* action)
{
    int rc = ORCM_SUCCESS;
    char* event_action = NULL;
    orcm_ras_event_t *threshold_event_data = NULL;

    if (NULL != msg) {
        OPAL_OUTPUT_VERBOSE((5, orcm_analytics_base_framework.framework_output,
                            "%s analytics:threshold:%s",ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), msg));
    }
    event_action = strdup(action);
    if(NULL == event_action) {
        return ORCM_ERROR;
    }
    threshold_event_data = orcm_analytics_base_event_create(analytics_value,
                             ORCM_RAS_EVENT_EXCEPTION, sev);
    if(NULL == threshold_event_data){
        rc = ORCM_ERROR;
        goto done;
    }
    if(0 != strcmp(event_action, "none")){
        rc = orcm_analytics_base_event_set_storage(threshold_event_data, ORCM_STORAGE_TYPE_NOTIFICATION);
        if(ORCM_SUCCESS != rc){
            goto done;
        }
        rc = orcm_analytics_base_event_set_description(threshold_event_data, "notifier_msg", msg, OPAL_STRING,NULL);
        if(ORCM_SUCCESS != rc){
           goto done;
        }
        rc = orcm_analytics_base_event_set_description(threshold_event_data, "notifier_action", event_action, OPAL_STRING, NULL);
        if(ORCM_SUCCESS != rc){
           goto done;
        }
        ORCM_RAS_EVENT(threshold_event_data);
    }
done:
    SAFEFREE(event_action);
    return rc;
}

static int monitor_threshold(orcm_workflow_caddy_t *current_caddy,
                             orcm_mca_analytics_threshold_policy_t *threshold_policy,
                             opal_list_t* threshold_list)
{
    char* msg1 = NULL;
    char* msg2 = NULL;
    orcm_value_t *current_value = NULL;
    orcm_value_t *analytics_orcm_value = NULL;
    bool copy = false;
    int rc = ORCM_SUCCESS;
    double val = 0.0;

    if(NULL == current_caddy || NULL == current_caddy->analytics_value ||
       NULL == current_caddy->analytics_value->compute_data || NULL == threshold_policy || NULL == threshold_list ) {
        return ORCM_ERR_BAD_PARAM;
    }
    OPAL_LIST_FOREACH(current_value, current_caddy->analytics_value->compute_data, orcm_value_t) {
        if(NULL == current_value){
            rc = ORCM_ERROR;
            goto cleanup;
        }
        val = orcm_util_get_number_orcm_value(current_value);
        if(val >= threshold_policy->hi && NULL != threshold_policy->hi_action){
            copy=true;
            if(0 < asprintf(&msg1, "%s value %.02f %s,greater than threshold %.02f %s",
                            current_value->value.key,val,current_value->units,threshold_policy->hi,current_value->units)){
                rc = generate_notification_event(current_caddy->analytics_value, threshold_policy->hi_sev, msg1, threshold_policy->hi_action);
                if(ORCM_SUCCESS != rc) {
                    goto cleanup;
                }
            }
        }
        else if(val <= threshold_policy->low && NULL != threshold_policy->low_action && threshold_policy->low != threshold_policy->hi) {
            copy=true;
            if(0 < asprintf(&msg2, "%s value %.02f %s, lower than threshold %.02f %s",
                            current_value->value.key,val,current_value->units,threshold_policy->low,current_value->units)) {
                rc = generate_notification_event(current_caddy->analytics_value, threshold_policy->low_sev, msg2, threshold_policy->low_action);
                if(ORCM_SUCCESS != rc) {
                    goto cleanup;
                }
            }
        }
        if(true == copy) {
            analytics_orcm_value = orcm_util_copy_orcm_value(current_value);
            if(NULL != analytics_orcm_value) {
                opal_list_append(threshold_list, (opal_list_item_t *)analytics_orcm_value);
            }
        }

cleanup:
        SAFEFREE(msg1);
        SAFEFREE(msg2);
        if (ORCM_SUCCESS != rc) {
            break;
        }
    }

    return rc;
}

static int get_threshold_value(char *tval, double* val)
{
    int j=0;
    if(NULL == tval || NULL == val) {
        return ORCM_ERR_BAD_PARAM;
    }
    for (j=0; j < (int)strlen(tval); j++) {
        if (!isdigit(tval[j]) && '-' != tval[j] && '+' != tval[j] && '.' != tval[j]) {
            return ORCM_ERR_BAD_PARAM;
        }
    }
    errno = 0;
    *val = strtod(tval, NULL);
    if(0 == *val && 0 != errno) {
        return ORCM_ERR_BAD_PARAM;
    }
    return ORCM_SUCCESS;
}

static int get_threshold_policy(void *cbdata,orcm_mca_analytics_threshold_policy_t* threshold_policy )
{
    opal_value_t *temp = NULL;
    char **policy = NULL;
    char **token = NULL;
    char* action_hi = NULL;
    char* severity = NULL;
    char* action_low = NULL;
    orte_notifier_severity_t sev = ORTE_NOTIFIER_ERROR;
    orcm_workflow_caddy_t *current_caddy = NULL;
    char* label = strdup("policy");
    int rc = ORCM_SUCCESS;
    double val = 0.0;
    int count=0, i=0;
    char* tval = NULL;

    if(NULL == cbdata || NULL == threshold_policy) {
        rc = ORCM_ERR_BAD_PARAM;
        goto done;
    }
    current_caddy = (orcm_workflow_caddy_t *)cbdata;
    temp = (opal_value_t*)opal_list_get_first(&current_caddy->wf_step->attributes);
    if(NULL == temp) {
        rc = ORCM_ERR_NOT_FOUND;
        goto done;
    }
    if (0 != strcmp(temp->key,label)) {
        rc = ORCM_ERR_BAD_PARAM;
        goto done;
    }
    policy = opal_argv_split(temp->data.string,',');
    count = opal_argv_count(policy);
    if(0 == count || 2 < count) {
        rc = ORCM_ERR_BAD_PARAM;
        goto done;
    }
    for(i=0; i<count; i++) {
        token = opal_argv_split(policy[i], '|');
        if(NULL == token || opal_argv_count(token) != 4) {
            rc = ORCM_ERR_BAD_PARAM;
            goto done;
        }
        rc = get_threshold_value(token[1], &val);
        if(ORCM_SUCCESS != rc) {
            goto done;
        }
        severity = strdup(token[2]);
        if(NULL != severity) {
        sev = get_severity(severity);
        }
        if(0 == strcmp(token[0],"hi")) {
            threshold_policy->hi = val;
            threshold_policy->hi_sev = sev;
            action_hi = strdup(token[3]);
            threshold_policy->hi_action = action_hi;
        }
        else if(0 == strcmp(token[0],"low")) {
            threshold_policy->low = val;
            threshold_policy->low_sev = sev;
            action_low = strdup(token[3]);
            threshold_policy->low_action = action_low;
        }
        else {
            rc = ORCM_ERR_BAD_PARAM;
            goto done;
        }
        SAFEFREE(tval);
        SAFEFREE(severity);
        opal_argv_free(token);
        token = NULL;
    }
done:
    SAFEFREE(label);
    SAFEFREE(tval);
    SAFEFREE(severity);
    opal_argv_free(policy);
    policy = NULL;
    opal_argv_free(token);
    token = NULL;
    return rc;
}

static int analyze(int sd, short args, void *cbdata)
{
    int rc = ORCM_SUCCESS;
    opal_list_t* threshold_list = NULL;
    orcm_mca_analytics_threshold_policy_t *threshold_policy = NULL;
    orcm_analytics_value_t *analytics_value_to_next = NULL;
    orcm_workflow_caddy_t *current_caddy = NULL;

    if (ORCM_SUCCESS != orcm_analytics_base_assert_caddy_data(cbdata)) {
        return ORCM_ERROR;
    }

    current_caddy = (orcm_workflow_caddy_t *)cbdata;

    threshold_policy = OBJ_NEW(orcm_mca_analytics_threshold_policy_t);
    if(NULL == threshold_policy){
        rc = ORCM_ERR_OUT_OF_RESOURCE;
        goto done;
    }
    rc = get_threshold_policy(cbdata,threshold_policy);
    if(ORCM_SUCCESS != rc) {
        OPAL_OUTPUT_VERBOSE((5, orcm_analytics_base_framework.framework_output,
                        "%s analytics:threshold:Invalid argument/s to workflow step",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        goto done;
    }

    threshold_list = OBJ_NEW(opal_list_t);
    if(NULL == threshold_list || NULL == threshold_policy){
        rc = ORCM_ERR_OUT_OF_RESOURCE;
        goto done;
    }
    rc = monitor_threshold(current_caddy, threshold_policy, threshold_list);
    if(ORCM_SUCCESS != rc){
        goto done;
    }
    if(NULL != threshold_list && 0 != threshold_list->opal_list_length)
    {
        analytics_value_to_next = orcm_util_load_orcm_analytics_value_compute(current_caddy->analytics_value->key,
                                          current_caddy->analytics_value->non_compute_data, threshold_list);
        if(NULL != analytics_value_to_next) {
            if(true == orcm_analytics_base_db_check(current_caddy->wf_step)){
                rc = orcm_analytics_base_log_to_database_event(analytics_value_to_next);
                if(ORCM_SUCCESS != rc){
                    goto done;
                }
            }
            ORCM_ACTIVATE_NEXT_WORKFLOW_STEP(current_caddy->wf,current_caddy->wf_step,
                                             current_caddy->hash_key, analytics_value_to_next);
        }
    }
done:
    if(NULL != current_caddy) {
        OBJ_RELEASE(current_caddy);
    }
    if(NULL != threshold_policy){
        OBJ_RELEASE(threshold_policy);
    }
    return rc;
}
