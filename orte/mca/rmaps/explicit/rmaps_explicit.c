/*
 * Copyright (c)    2018    UT-Battelle, LLC
 *                          All rights reserved.
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2006 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2013 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2011-2012 Los Alamos National Security, LLC.
 *                         All rights reserved.
 * Copyright (c) 2014-2017 Intel, Inc.  All rights reserved.
 * Copyright (c) 2017      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orte_config.h"
#include "orte/constants.h"
#include "orte/types.h"
#include "orte/util/show_help.h"

#include "orte/mca/rmaps/base/base.h"
#include "orte/mca/rmaps/base/rmaps_private.h"
#include "rmaps_explicit.h"

extern int explicit_mapper(orte_job_t *jdata, rmaps_explicit_layout_t layout);
extern int assign_locations(orte_job_t *jdata, rmaps_explicit_layout_t layout);

char *_pe_layout;
rmaps_explicit_layout_t layout;

/*
 *
 */
static int
_apply_rr_policy (orte_job_t *jdata,
                  orte_app_context_t *app,
                  opal_list_t *node_list,
                  orte_std_cntr_t num_slots,
                  int scope,
                  int num_per_scope,
                  int n_pes)
{
    int i;
    int _mapped_procs = 0;
    int available_slots = 0;
    orte_node_t *_node;
    orte_proc_t *_proc;
    hwloc_obj_t _obj = NULL;
    //orte_vpid_t num_procs = app->num_procs;

    OPAL_LIST_FOREACH(_node, node_list, orte_node_t)
    {
        /* We skip nodes for which we cannot get the topology */
        if (_node->topology == NULL || _node->topology->topo == NULL)
            continue;

        /* We skip nodes that are full */
        if (_node->slots <= _node->slots_inuse)
            continue;

        /* We skip nodes for which we cannot access the topology */
        _obj = hwloc_get_root_obj(_node->topology->topo);
        if (_obj == NULL)
            continue;

        /* We then perform the assignment based on the number of available slots */
        available_slots = _node->slots - _node->slots_inuse;
        for (i = 0; i < available_slots; i++)
        {
            /* add this node to the map - do it only once */
            if (!ORTE_FLAG_TEST(_node, ORTE_NODE_FLAG_MAPPED))
            {
                ORTE_FLAG_SET(_node, ORTE_NODE_FLAG_MAPPED);
                OBJ_RETAIN(_node);
                opal_pointer_array_add(jdata->map->nodes, _node);
                ++(jdata->map->num_nodes);
            }

            /* Create a new proc and assigned it to a node */
            _proc = orte_rmaps_base_setup_proc(jdata, _node, app->idx);
            if (_proc == NULL)
                return ORTE_ERR_OUT_OF_RESOURCE;
            _mapped_procs++;
            orte_set_attribute(&_proc->attributes, ORTE_PROC_HWLOC_LOCALE, ORTE_ATTR_LOCAL, _obj, OPAL_PTR);
        }
    }

    if (_mapped_procs == app->num_procs) {
        /* we are done */
        return ORTE_SUCCESS;
    }

    /*
     * Second pass: if we haven't mapped everyone yet, it is
     * because we are oversubscribed. Figure out how many procs
     * to add
     */
    opal_output_verbose(2, orte_rmaps_base_framework.framework_output,
                        "mca:rmaps:explicit job %s is oversubscribed - performing second pass",
                        ORTE_JOBID_PRINT(jdata->jobid));
    {
        float balance;
        int extra_procs_to_assign = 0;
        int nxtra_nodes = 0;
        int num_procs_to_assign;
        bool add_one = false;

        balance = (float)((int)app->num_procs - _mapped_procs) / (float)opal_list_get_size(node_list);
        extra_procs_to_assign = (int)balance;
        if (0 < (balance - (float)extra_procs_to_assign)) {
            /* compute how many nodes need an extra proc */
            nxtra_nodes = app->num_procs - _mapped_procs - (extra_procs_to_assign * opal_list_get_size(node_list));
            /* add one so that we add an extra proc to the first nodes
             * until all procs are mapped
             */
            extra_procs_to_assign++;
            /* flag that we added one */
            add_one = true;
        }

        OPAL_LIST_FOREACH(_node, node_list, orte_node_t)
        {
            if (_node->topology == NULL || _node->topology->topo == NULL)
                continue;

            _obj = hwloc_get_root_obj(_node->topology->topo);
            if (_obj == NULL)
                continue;

            /* add this node to the map - do it only once */
            if (!ORTE_FLAG_TEST(_node, ORTE_NODE_FLAG_MAPPED))
            {
                ORTE_FLAG_SET(_node, ORTE_NODE_FLAG_MAPPED);
                OBJ_RETAIN(_node);
                opal_pointer_array_add(jdata->map->nodes, _node);
                ++(jdata->map->num_nodes);
            }

            if (add_one)
            {
                if (0 == nxtra_nodes) {
                    --extra_procs_to_assign;
                    add_one = false;
                }
                else
                {
                    --nxtra_nodes;
                }
            }

            num_procs_to_assign = _node->slots - _node->slots_inuse + extra_procs_to_assign;
            for (i = 0; i < num_procs_to_assign && _mapped_procs < app->num_procs; i++)
            {
                _proc = orte_rmaps_base_setup_proc(jdata, _node, app->idx);
                if (_proc == NULL)
                    return ORTE_ERR_OUT_OF_RESOURCE;
                _mapped_procs++;
                orte_set_attribute(&_proc->attributes, ORTE_PROC_HWLOC_LOCALE, ORTE_ATTR_LOCAL, _obj, OPAL_PTR);
            }

            /*
             * Not all nodes are equal, so only set oversubscribed for
             * this node if it is in that state
             */
            if (_node->slots < (int)_node->num_procs)
            {
                /*
                 * Flag the node as oversubscribed so that sched-yield gets
                 * properly set
                 */
                ORTE_FLAG_SET(_node, ORTE_NODE_FLAG_OVERSUBSCRIBED);
                ORTE_FLAG_SET(jdata, ORTE_JOB_FLAG_OVERSUBSCRIBED);
                /* check for permission */
                if (ORTE_FLAG_TEST(_node, ORTE_NODE_FLAG_SLOTS_GIVEN))
                {
                    /* 
                     * If we weren't given a directive either way, then we will error out
                     * as the #slots were specifically given, either by the host RM or
                     * via hostfile/dash-host
                     */
                    if (!(ORTE_MAPPING_SUBSCRIBE_GIVEN & ORTE_GET_MAPPING_DIRECTIVE(jdata->map->mapping)))
                    {
                        orte_show_help("help-orte-rmaps-base.txt", "orte-rmaps-base:alloc-error",
                                       true, app->num_procs, app->app, orte_process_info.nodename);
                        ORTE_UPDATE_EXIT_STATUS(ORTE_ERROR_DEFAULT_EXIT_CODE);
                        return ORTE_ERR_SILENT;
                    }
                    else if (ORTE_MAPPING_NO_OVERSUBSCRIBE & ORTE_GET_MAPPING_DIRECTIVE(jdata->map->mapping))
                    {
                        /* If we were explicitly told not to oversubscribe, then don't */
                        orte_show_help("help-orte-rmaps-base.txt", "orte-rmaps-base:alloc-error",
                                       true, app->num_procs, app->app, orte_process_info.nodename);
                        ORTE_UPDATE_EXIT_STATUS(ORTE_ERROR_DEFAULT_EXIT_CODE);
                        return ORTE_ERR_SILENT;
                    }
                }
            }
            /* if we have mapped everything, then we are done */
            if (_mapped_procs == app->num_procs) {
                break;
            }
        }
    }

    return ORTE_SUCCESS;
}

static int
_apply_spread_policy (orte_job_t *jdata,
                      orte_app_context_t *app,
                      opal_list_t *node_list,
                      orte_std_cntr_t num_slots,
                      int scope,
                      int num_per_scope,
                      int n_pes)
{
    int i, j, nprocs_mapped, navg;
    orte_node_t *node;
    orte_proc_t *proc;
    int nprocs, nxtra_objs;
    hwloc_obj_t obj = NULL;
    unsigned int nobjs;
    hwloc_obj_type_t target = HWLOC_OBJ_PU; /* FIXME: should be based on what the user requests */
    unsigned cache_level = 0; /* FIXME: should be based on what the user requests */
    unsigned long num_procs = 2; /* FIXME: should be based on what the user requests */

    opal_output_verbose(2, orte_rmaps_base_framework.framework_output,
                        "mca:rmaps:rr: mapping span by %s for job %s slots %d num_procs %lu",
                        hwloc_obj_type_string(target),
                        ORTE_JOBID_PRINT(jdata->jobid),
                        (int)num_slots, (unsigned long)num_procs);

    /* quick check to see if we can map all the procs */
    if (num_slots < (int)app->num_procs) {
        if (ORTE_MAPPING_NO_OVERSUBSCRIBE & ORTE_GET_MAPPING_DIRECTIVE(jdata->map->mapping)) {
            orte_show_help("help-orte-rmaps-base.txt", "orte-rmaps-base:alloc-error",
                           true, app->num_procs, app->app, orte_process_info.nodename);
            ORTE_UPDATE_EXIT_STATUS(ORTE_ERROR_DEFAULT_EXIT_CODE);
            return ORTE_ERR_SILENT;
        }
    }

    /* we know we have enough slots, or that oversubscrption is allowed, so
     * next determine how many total objects we have to work with
     */
    nobjs = 0;
    OPAL_LIST_FOREACH(node, node_list, orte_node_t) {
        if (NULL == node->topology || NULL == node->topology->topo) {
            orte_show_help("help-orte-rmaps-ppr.txt", "ppr-topo-missing",
                           true, node->name);
            return ORTE_ERR_SILENT;
        }
        /* get the number of objects of this type on this node */
        nobjs += opal_hwloc_base_get_nbobjs_by_type(node->topology->topo, target, cache_level, OPAL_HWLOC_AVAILABLE);
    }

    if (0 == nobjs) {
        return ORTE_ERR_NOT_FOUND;
    }

    /* divide the procs evenly across all objects */
    navg = app->num_procs / nobjs;
    if (0 == navg) {
        /* if there are less procs than objects, we have to
         * place at least one/obj
         */
        navg = 1;
    }

    /* compute how many objs need an extra proc */
    if (0 > (nxtra_objs = app->num_procs - (navg * nobjs))) {
        nxtra_objs = 0;
    }

    opal_output_verbose(2, orte_rmaps_base_framework.framework_output,
                        "mca:rmaps:rr: mapping by %s navg %d extra_objs %d",
                        hwloc_obj_type_string(target),
                        navg, nxtra_objs);

    nprocs_mapped = 0;
    OPAL_LIST_FOREACH(node, node_list, orte_node_t) {
        /* add this node to the map, if reqd */
        if (!ORTE_FLAG_TEST(node, ORTE_NODE_FLAG_MAPPED)) {
            ORTE_FLAG_SET(node, ORTE_NODE_FLAG_MAPPED);
            OBJ_RETAIN(node);
            opal_pointer_array_add(jdata->map->nodes, node);
            ++(jdata->map->num_nodes);
        }
        /* get the number of objects of this type on this node */
        nobjs = opal_hwloc_base_get_nbobjs_by_type(node->topology->topo, target, cache_level, OPAL_HWLOC_AVAILABLE);
        opal_output_verbose(2, orte_rmaps_base_framework.framework_output,
                            "mca:rmaps:rr:byobj: found %d objs on node %s", nobjs, node->name);
        /* loop through the number of objects */
        for (i=0; i < (int)nobjs && nprocs_mapped < (int)app->num_procs; i++) {
            /* get the hwloc object */
            if (NULL == (obj = opal_hwloc_base_get_obj_by_type(node->topology->topo, target, cache_level, i, OPAL_HWLOC_AVAILABLE))) {
                //ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
                return ORTE_ERR_NOT_FOUND;
            }
            if (orte_rmaps_base.cpus_per_rank > (int)opal_hwloc_base_get_npus(node->topology->topo, obj)) {
                orte_show_help("help-orte-rmaps-base.txt", "mapping-too-low", true,
                               orte_rmaps_base.cpus_per_rank, opal_hwloc_base_get_npus(node->topology->topo, obj),
                               orte_rmaps_base_print_mapping(orte_rmaps_base.mapping));
                return ORTE_ERR_SILENT;
            }
            /* determine how many to map */
            nprocs = navg;
            if (0 < nxtra_objs) {
                nprocs++;
                nxtra_objs--;
            }
            /* map the reqd number of procs */
            for (j=0; j < nprocs && nprocs_mapped < app->num_procs; j++) {
                if (NULL == (proc = orte_rmaps_base_setup_proc(jdata, node, app->idx))) {
                    return ORTE_ERR_OUT_OF_RESOURCE;
                }
                nprocs_mapped++;
                orte_set_attribute(&proc->attributes, ORTE_PROC_HWLOC_LOCALE, ORTE_ATTR_LOCAL, obj, OPAL_PTR);
            }
            /* keep track of the node we last used */
            jdata->bookmark = node;
        }
        /* not all nodes are equal, so only set oversubscribed for
         * this node if it is in that state
         */
        if (node->slots < (int)node->num_procs) {
            /* flag the node as oversubscribed so that sched-yield gets
             * properly set
             */
            ORTE_FLAG_SET(node, ORTE_NODE_FLAG_OVERSUBSCRIBED);
            ORTE_FLAG_SET(jdata, ORTE_JOB_FLAG_OVERSUBSCRIBED);
        }
        if (nprocs_mapped == app->num_procs) {
            /* we are done */
            break;
        }
    }

    return ORTE_SUCCESS;
}

/*
 * Create a map for the job, based on the explicit description that was passed in.
 */
static int
orte_rmaps_explicit_map (orte_job_t *jdata)
{
    explicit_mapper (jdata, layout);

    return ORTE_SUCCESS;
}

/*
 * Mapping by hwloc object looks a lot like mapping by node,
 * but has the added complication of possibly having different
 * numbers of objects on each node
 */
static int
orte_rmaps_explicit_assign_byobj(orte_job_t *jdata,
                                 hwloc_obj_type_t target,
                                 unsigned cache_level)
{
    int start, j, m, n;
    orte_app_context_t *app;
    orte_node_t *node;
    orte_proc_t *proc;
    hwloc_obj_t obj=NULL;
    unsigned int nobjs;

    opal_output_verbose(2, orte_rmaps_base_framework.framework_output,
                        "mca:rmaps:rr: assigning locations by %s for job %s",
                        hwloc_obj_type_string(target),
                        ORTE_JOBID_PRINT(jdata->jobid));


    /* start mapping procs onto objects, filling each object as we go until
     * all procs are mapped. If one pass doesn't catch all the required procs,
     * then loop thru the list again to handle the oversubscription
     */
    for (n=0; n < jdata->apps->size; n++) {
        if (NULL == (app = (orte_app_context_t*)opal_pointer_array_get_item(jdata->apps, n))) {
            continue;
        }
        for (m=0; m < jdata->map->nodes->size; m++) {
            if (NULL == (node = (orte_node_t*)opal_pointer_array_get_item(jdata->map->nodes, m))) {
                continue;
            }
            if (NULL == node->topology || NULL == node->topology->topo) {
                orte_show_help("help-orte-rmaps-ppr.txt", "ppr-topo-missing",
                               true, node->name);
                return ORTE_ERR_SILENT;
            }
            /* get the number of objects of this type on this node */
            nobjs = opal_hwloc_base_get_nbobjs_by_type(node->topology->topo, target, cache_level, OPAL_HWLOC_AVAILABLE);
            if (0 == nobjs) {
                continue;
            }
            opal_output_verbose(2, orte_rmaps_base_framework.framework_output,
                                "mca:rmaps:rr: found %u %s objects on node %s",
                                nobjs, hwloc_obj_type_string(target), node->name);

            /* if this is a comm_spawn situation, start with the object
             * where the parent left off and increment */
            if (ORTE_JOBID_INVALID != jdata->originator.jobid) {
                start = (jdata->bkmark_obj + 1) % nobjs;
            } else {
                start = 0;
            }
            /* loop over the procs on this node */
            for (j=0; j < node->procs->size; j++) {
                if (NULL == (proc = (orte_proc_t*)opal_pointer_array_get_item(node->procs, j))) {
                    continue;
                }
                /* ignore procs from other jobs */
                if (proc->name.jobid != jdata->jobid) {
                    opal_output_verbose(5, orte_rmaps_base_framework.framework_output,
                                        "mca:rmaps:rr:assign skipping proc %s - from another job",
                                        ORTE_NAME_PRINT(&proc->name));
                    continue;
                }
                /* ignore procs from other apps */
                if (proc->app_idx != app->idx) {
                    continue;
                }
                opal_output_verbose(20, orte_rmaps_base_framework.framework_output,
                                    "mca:rmaps:rr: assigning proc to object %d", (j + start) % nobjs);
                /* get the hwloc object */
                if (NULL == (obj = opal_hwloc_base_get_obj_by_type(node->topology->topo, target, cache_level, (j + start) % nobjs, OPAL_HWLOC_AVAILABLE))) {
                    //ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
                    return ORTE_ERR_NOT_FOUND;
                }
                if (orte_rmaps_base.cpus_per_rank > (int)opal_hwloc_base_get_npus(node->topology->topo, obj)) {
                    orte_show_help("help-orte-rmaps-base.txt", "mapping-too-low", true,
                                   orte_rmaps_base.cpus_per_rank, opal_hwloc_base_get_npus(node->topology->topo, obj),
                                   orte_rmaps_base_print_mapping(orte_rmaps_base.mapping));
                    return ORTE_ERR_SILENT;
                }
                orte_set_attribute(&proc->attributes, ORTE_PROC_HWLOC_LOCALE, ORTE_ATTR_LOCAL, obj, OPAL_PTR);
            }
        }
    }

    return ORTE_SUCCESS;
}

static void
_parse_topo (hwloc_topology_t topo, char *target)
{
    hwloc_obj_t root, obj;
    int level = 0;

    obj = root = hwloc_get_root_obj (topo);
    while (obj != NULL)
    {
        if (target != NULL && strcasecmp (hwloc_obj_type_string (obj->type), target) == 0)
        {
            fprintf (stderr, "LEVEL: %d - %s.   <--\n", level, hwloc_obj_type_string (obj->type));
        }
        else
        {
            fprintf (stderr, "LEVEL: %d - %s.\n", level, hwloc_obj_type_string (obj->type)); 
        }
        obj = obj->children[0];
        level++;
    }
}

static int
orte_rmaps_explicit_assign_locations (orte_job_t *jdata)
{
    int rc;

    rc = assign_locations (jdata, layout);
    if (rc != ORTE_SUCCESS)
        return ORTE_ERROR;

    return ORTE_SUCCESS;
}

orte_rmaps_base_module_t orte_rmaps_explicit_module = {
    .map_job            = orte_rmaps_explicit_map,
    .assign_locations   = orte_rmaps_explicit_assign_locations
};
