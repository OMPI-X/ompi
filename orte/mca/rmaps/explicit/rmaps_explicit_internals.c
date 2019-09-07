/*
 * Copyright (c) 2011      Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2011      Los Alamos National Security, LLC.
 *                         All rights reserved.
 * Copyright (c) 2014-2018 Intel, Inc. All rights reserved.
 * Copyright (c) 2015-2017 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orte_config.h"
#include "orte/constants.h"
#include "orte/types.h"

#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif  /* HAVE_UNISTD_H */
#include <string.h>

#include "opal/mca/hwloc/base/base.h"
#include "opal/util/argv.h"

#include "orte/util/show_help.h"
#include "orte/mca/errmgr/errmgr.h"

#include "orte/mca/rmaps/base/rmaps_private.h"
#include "orte/mca/rmaps/base/base.h"
#include "orte/mca/rmaps/explicit/rmaps_explicit.h"

int explicit_mapper(orte_job_t *jdata, rmaps_explicit_layout_t layout);
int assign_locations(orte_job_t *jdata, rmaps_explicit_layout_t layout);

/* RHC: will eventually remove this
 * definition as it is no longer reqd
 * in the rest of OMPI system.
 *
 * Define a hierarchical level value that
 * helps resolve the hwloc behavior of
 * treating caches as a single type of
 * entity - must always be available
 */
typedef enum {
    OPAL_HWLOC_NODE_LEVEL=0,
    OPAL_HWLOC_NUMA_LEVEL,
    OPAL_HWLOC_SOCKET_LEVEL,
    OPAL_HWLOC_L3CACHE_LEVEL,
    OPAL_HWLOC_L2CACHE_LEVEL,
    OPAL_HWLOC_L1CACHE_LEVEL,
    OPAL_HWLOC_CORE_LEVEL,
    OPAL_HWLOC_HWTHREAD_LEVEL
} opal_hwloc_level_t;

#if 0
static void prune(orte_jobid_t jobid,
                  orte_app_idx_t app_idx,
                  orte_node_t *node,
                  opal_hwloc_level_t *level,
                  orte_vpid_t *nmapped);
#endif

static int explicit[OPAL_HWLOC_HWTHREAD_LEVEL+1];

int explicit_mapper(orte_job_t *jdata, rmaps_explicit_layout_t layout)
{
    int rc = ORTE_SUCCESS;
    mca_base_component_t *c=&mca_rmaps_explicit_component.base_version;
    orte_node_t *node;
    orte_proc_t *proc;
    orte_app_context_t *app;
    orte_vpid_t total_procs, nprocs_mapped;
    opal_hwloc_level_t start=OPAL_HWLOC_NODE_LEVEL;
    hwloc_obj_t obj;
    hwloc_obj_type_t lowest;
    unsigned cache_level=0;
    unsigned int i;
    opal_list_t node_list;
    opal_list_item_t *item;
    orte_std_cntr_t num_slots;
    orte_app_idx_t idx;
    bool initial_map=true;
    uint64_t n_slots_per_node;

    /* only handle initial launch of loadbalanced
     * or NPERxxx jobs - allow restarting of failed apps
     */
    if (ORTE_FLAG_TEST(jdata, ORTE_JOB_FLAG_RESTART)) {
        opal_output_verbose(5, orte_rmaps_base_framework.framework_output,
                            "mca:rmaps:explicit: job %s being restarted - explicit cannot map",
                            ORTE_JOBID_PRINT(jdata->jobid));
        return ORTE_ERR_TAKE_NEXT_OPTION;
    }

    /* flag that I did the mapping */
    if (NULL != jdata->map->last_mapper) {
        free(jdata->map->last_mapper);
    }
    jdata->map->last_mapper = strdup(c->mca_component_name);

    /* initialize */
    memset(explicit, 0, OPAL_HWLOC_HWTHREAD_LEVEL * sizeof(opal_hwloc_level_t));

    if (0 == strcasecmp(layout.target, "node"))
    {
        explicit[OPAL_HWLOC_NODE_LEVEL] = strtol(layout.target, NULL, 10);
        ORTE_SET_MAPPING_POLICY(jdata->map->mapping, ORTE_MAPPING_BYNODE);
        start = OPAL_HWLOC_NODE_LEVEL;
    }
    else if (0 == strcasecmp(layout.target, "PU") ||
             0 == strcasecmp(layout.target, "thread"))
    {
        explicit[OPAL_HWLOC_HWTHREAD_LEVEL] = strtol(layout.target, NULL, 10);
        start = OPAL_HWLOC_HWTHREAD_LEVEL;
        ORTE_SET_MAPPING_POLICY(jdata->map->mapping, ORTE_MAPPING_BYHWTHREAD);
    }
    else if (0 == strcasecmp(layout.target, "core"))
    {
        explicit[OPAL_HWLOC_CORE_LEVEL] = strtol(layout.target, NULL, 10);
        start = OPAL_HWLOC_CORE_LEVEL;
        ORTE_SET_MAPPING_POLICY(jdata->map->mapping, ORTE_MAPPING_BYCORE);
    }
    else if (0 == strcasecmp(layout.target, "package") ||
             0 == strcasecmp(layout.target, "pkg"))
    {
        explicit[OPAL_HWLOC_SOCKET_LEVEL] = strtol(layout.target, NULL, 10);
        start = OPAL_HWLOC_SOCKET_LEVEL;
        ORTE_SET_MAPPING_POLICY(jdata->map->mapping, ORTE_MAPPING_BYSOCKET);
    }
    else if (0 == strcasecmp(layout.target, "l1cache"))
    {
        explicit[OPAL_HWLOC_L1CACHE_LEVEL] = strtol(layout.target, NULL, 10);
        start = OPAL_HWLOC_L1CACHE_LEVEL;
        cache_level = 1;
        ORTE_SET_MAPPING_POLICY(jdata->map->mapping, ORTE_MAPPING_BYL1CACHE);
    }
    else if (0 == strcasecmp(layout.target, "l2cache"))
    {
        explicit[OPAL_HWLOC_L2CACHE_LEVEL] = strtol(layout.target, NULL, 10);
        start = OPAL_HWLOC_L2CACHE_LEVEL;
        cache_level = 2;
        ORTE_SET_MAPPING_POLICY(jdata->map->mapping, ORTE_MAPPING_BYL2CACHE);
    }
    else if (0 == strcasecmp(layout.target, "l3cache"))
    {
        explicit[OPAL_HWLOC_L3CACHE_LEVEL] = strtol(layout.target, NULL, 10);
        start = OPAL_HWLOC_L3CACHE_LEVEL;
        cache_level = 3;
        ORTE_SET_MAPPING_POLICY(jdata->map->mapping, ORTE_MAPPING_BYL3CACHE);
    }
    else if (0 == strcasecmp(layout.target, "numa"))
    {
        explicit[OPAL_HWLOC_NUMA_LEVEL] = strtol(layout.target, NULL, 10);
        start = OPAL_HWLOC_NUMA_LEVEL;
        ORTE_SET_MAPPING_POLICY(jdata->map->mapping, ORTE_MAPPING_BYNUMA);
    }
    else
    {
        /* unknown spec */
        orte_show_help("help-orte-rmaps-explicit.txt", "unrecognized-explicit-option", true, layout.target, jdata->map->ppr);
        return ORTE_ERR_SILENT;
    }

    opal_output_verbose(5, orte_rmaps_base_framework.framework_output,
                        "mca:rmaps:explicit: job %s assigned policy %s",
                        ORTE_JOBID_PRINT(jdata->jobid),
                        orte_rmaps_base_print_mapping(jdata->map->mapping));

    /* convenience */
    lowest = opal_hwloc_levels[start];

    for (idx=0; idx < (orte_app_idx_t)jdata->apps->size; idx++)
    {
        if (NULL == (app = (orte_app_context_t*)opal_pointer_array_get_item(jdata->apps, idx)))
            continue;

        /* if the number of total procs was given, set that
         * limit - otherwise, set to max so we simply fill
         * all the nodes with the pattern
         */
        if (0 < app->num_procs) {
            total_procs = app->num_procs;
        } else {
            total_procs = ORTE_VPID_MAX;
        }

        /* get the available nodes */
        OBJ_CONSTRUCT(&node_list, opal_list_t);
        if(ORTE_SUCCESS != (rc = orte_rmaps_base_get_target_nodes(&node_list, &num_slots, app,
                                                                  jdata->map->mapping, initial_map, false))) {
            ORTE_ERROR_LOG(rc);
            goto error;
        }

        /* flag that all subsequent requests should not reset the node->mapped flag */
        initial_map = false;

        /* if a bookmark exists from some prior mapping, set us to start there */
        jdata->bookmark = orte_rmaps_base_get_starting_point(&node_list, jdata);

        /* Get the number of procs to map on every node */
        n_slots_per_node = layout.n_places;

        /* cycle across the nodes */
        nprocs_mapped = 0;
        for (item = opal_list_get_first(&node_list);
             item != opal_list_get_end(&node_list);
             item = opal_list_get_next(item))
        {
            node = (orte_node_t*)item;

            /* bozo check */
            if (NULL == node->topology || NULL == node->topology->topo) {
                orte_show_help("help-orte-rmaps-explicit.txt", "explicit-topo-missing",
                               true, node->name);
                rc = ORTE_ERR_SILENT;
                goto error;
            }

            /* add the node to the map, if needed */
            if (!ORTE_FLAG_TEST(node, ORTE_NODE_FLAG_MAPPED)) {
                ORTE_FLAG_SET(node, ORTE_NODE_FLAG_MAPPED);
                OBJ_RETAIN(node);
                opal_pointer_array_add(jdata->map->nodes, node);
                jdata->map->num_nodes++;
            }

            /*
             * We know exactly which hardware objects need to be assigned to the job
             * and how to do binding. So we go through the list of node and reserve
             * as many as necessary to deploy all the procs.
             * Remember that we are here in the context of a very specific node.
             */

            /*
             * Based on the requested layout, we must find the correct HW objects
             * Note: We assume that all the lists are in order so we can easily identify
             * the target object.
             */
            for (i = 0; i < n_slots_per_node && nprocs_mapped < total_procs; i++)
            {
                uint64_t obj_idx = layout.locations[i];
                obj = opal_hwloc_base_get_obj_by_type (node->topology->topo,
                                                       lowest, cache_level,
                                                       obj_idx,
                                                       OPAL_HWLOC_AVAILABLE);
                if (NULL == obj) {
                    fprintf(stderr, "[%s:%d] ERROR: %s() topology obj was NULL\n", __FILE__, __LINE__, __func__);
                    rc = ORTE_ERR_NOT_INITIALIZED;
                    goto error;
                }
                if (NULL == (proc = orte_rmaps_base_setup_proc(jdata, node, idx)))
                {
                    rc = ORTE_ERR_OUT_OF_RESOURCE;
                    goto error;
                }
                nprocs_mapped++;
                orte_set_attribute(&proc->attributes, ORTE_PROC_HWLOC_LOCALE, ORTE_ATTR_LOCAL, obj, OPAL_PTR);
            }

            /*
             * We mapped all the procs
             * In this specific mapper, overscribing is NOT possible
             */

            if (!(ORTE_MAPPING_DEBUGGER & ORTE_GET_MAPPING_DIRECTIVE(jdata->map->mapping))) {
                /* set the total slots used */
                if ((int)node->num_procs <= node->slots) {
                    node->slots_inuse = (int)node->num_procs;
                } else {
                    node->slots_inuse = node->slots;
                }
            }

            /* if we haven't mapped all the procs, continue on to the
             * next node
             */
            if (total_procs == nprocs_mapped) {
                break;
            }
        }

        if (0 == app->num_procs) {
            app->num_procs = nprocs_mapped;
        }

        if (ORTE_VPID_MAX != total_procs && nprocs_mapped < total_procs) {
            /* couldn't map them all */
            orte_show_help("help-orte-rmaps-explicit.txt", "explicit-too-many-procs",
                           true, app->app, app->num_procs, jdata->map->ppr);
            rc = ORTE_ERR_SILENT;
            goto error;
        }

        /* track the total number of processes we mapped - must update
         * this AFTER we compute vpids so that computation is done
         * correctly
         */
        jdata->num_procs += app->num_procs;

        while (NULL != (item = opal_list_remove_first(&node_list))) {
            OBJ_RELEASE(item);
        }
        OBJ_DESTRUCT(&node_list);
    }

    return ORTE_SUCCESS;

  error:
    while (NULL != (item = opal_list_remove_first(&node_list))) {
        OBJ_RELEASE(item);
    }
    OBJ_DESTRUCT(&node_list);

    return rc;
}

int assign_locations(orte_job_t *jdata, rmaps_explicit_layout_t layout)
{
    int rc;
    uint64_t i;
    int m, n;
    mca_base_component_t *c = &mca_rmaps_explicit_component.base_version;
    orte_node_t *node;
    orte_proc_t *proc;
    orte_app_context_t *app;
    hwloc_obj_type_t level;
    hwloc_obj_t obj;
    unsigned int cache_level=0;
    int nprocs_mapped;
    uint64_t n_slots_per_node = layout.n_places;

    /* The exact placement/binding of ranks for the node was already done during the previous phase so we have nothing to do here */
    return ORTE_SUCCESS;

    if (NULL == jdata->map->last_mapper ||
        0 != strcasecmp(jdata->map->last_mapper, c->mca_component_name)) {
        /* a mapper has been specified, and it isn't me */
        opal_output_verbose(5, orte_rmaps_base_framework.framework_output,
                            "mca:rmaps:explicit: job %s not using explicit assign: %s",
                            ORTE_JOBID_PRINT(jdata->jobid),
                            (NULL == jdata->map->last_mapper) ? "NULL" : jdata->map->last_mapper);
        return ORTE_ERR_TAKE_NEXT_OPTION;
    }

    opal_output_verbose(5, orte_rmaps_base_framework.framework_output,
                        "mca:rmaps:explicit: assigning locations for job %s with explicit %s policy %s",
                        ORTE_JOBID_PRINT(jdata->jobid), jdata->map->ppr,
                        orte_rmaps_base_print_mapping(jdata->map->mapping));

    /* pickup the object level */
    if (ORTE_MAPPING_BYNODE == ORTE_GET_MAPPING_POLICY(jdata->map->mapping)) {
        level = HWLOC_OBJ_MACHINE;
    } else if (ORTE_MAPPING_BYHWTHREAD == ORTE_GET_MAPPING_POLICY(jdata->map->mapping)) {
        level = HWLOC_OBJ_PU;
    } else if (ORTE_MAPPING_BYCORE == ORTE_GET_MAPPING_POLICY(jdata->map->mapping)) {
        level = HWLOC_OBJ_CORE;
    } else if (ORTE_MAPPING_BYSOCKET == ORTE_GET_MAPPING_POLICY(jdata->map->mapping)) {
        level = HWLOC_OBJ_SOCKET;
    } else if (ORTE_MAPPING_BYL1CACHE == ORTE_GET_MAPPING_POLICY(jdata->map->mapping)) {
        level = HWLOC_OBJ_L1CACHE;
        cache_level = 1;
    } else if (ORTE_MAPPING_BYL2CACHE == ORTE_GET_MAPPING_POLICY(jdata->map->mapping)) {
        level = HWLOC_OBJ_L2CACHE;
        cache_level = 2;
    } else if (ORTE_MAPPING_BYL3CACHE == ORTE_GET_MAPPING_POLICY(jdata->map->mapping)) {
        level = HWLOC_OBJ_L3CACHE;
        cache_level = 3;
    } else if (ORTE_MAPPING_BYNUMA == ORTE_GET_MAPPING_POLICY(jdata->map->mapping)) {
        level = HWLOC_OBJ_NUMANODE;
    } else {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        return ORTE_ERR_TAKE_NEXT_OPTION;
    }

    for (n = 0; n < jdata->apps->size; n++)
    {
        app = (orte_app_context_t*)opal_pointer_array_get_item(jdata->apps, n);
        if (app == NULL)
            continue;

	/* The exact placement/binding of ranks for the node was already done during the previous phase so we have nothing to do here */
    }

    return ORTE_SUCCESS;

 error:
    return rc;
}
