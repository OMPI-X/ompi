/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c)    2018    UT-Battelle, LLC
 *                          All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orte_config.h"
#include "orte/constants.h"
#include "orte/mca/rmaps/base/base.h"
#include "rmaps_explicit.h"

static int orte_rmaps_explicit_register (void);
static int orte_rmaps_explicit_open (void);
static int orte_rmaps_explicit_close (void);
static int orte_rmaps_explicit_query (mca_base_module_t **module, int *priority);

orte_rmaps_base_component_t mca_rmaps_explicit_component = {
    .base_version = {   ORTE_RMAPS_BASE_VERSION_2_0_0,
                        .mca_component_name = "explicit",
                        MCA_BASE_MAKE_VERSION (component,
                                               ORTE_MAJOR_VERSION,
                                               ORTE_MINOR_VERSION,
                                               ORTE_RELEASE_VERSION),
                        .mca_register_component_params = orte_rmaps_explicit_register,
                        .mca_open_component = orte_rmaps_explicit_open,
                        .mca_close_component = orte_rmaps_explicit_close,
                        .mca_query_component = orte_rmaps_explicit_query,
                    },
    .base_data = {  MCA_BASE_METADATA_PARAM_CHECKPOINT  },
};

static int _priority;
extern rmaps_explicit_layout_t layout;
extern char *_pe_layout;

/*
 * 
 */
static layout_policy_t
_token_to_policy (char *policy_name)
{   
    if (strcasecmp (policy_name, "rr") == 0)
        return LAYOUT_POLICY_RR;
    
    if (strcasecmp (policy_name, "spread") == 0)
        return LAYOUT_POLICY_SPREAD;
    
    return LAYOUT_POLICY_NONE;
}

#define MAX_LOCATIONS   (1024)
static int
_parse_places (char *places, uint64_t *num, uint8_t **locations)
{
    char *s;
    char *token;
    uint64_t idx = 0;
    uint64_t cnt = 0;
    const char delimiters[] = ",";
    uint8_t *_l;

    if (locations == NULL || *locations == NULL)
    {
        uint8_t *locs = malloc (MAX_LOCATIONS * sizeof (uint8_t));
        if (locs == NULL)
            return ORTE_ERR_OUT_OF_RESOURCE;

        *locations = locs;
    }
    _l = *locations;

    /*
     * The places string looks like X:-:-:X, where "X" is a place where rank X should be
     * placed and "-" a place that should be left alone
     */

    s = strdupa (places);
    token = strtok (s, delimiters);

    do
    {
        if (strcmp (token, "-") != 0)
        {
            int target_rank = atoi (token);
            fprintf (stderr, "[%s:%s:%d] Assigning rank # %d to place # %d\n", __FILE__, __func__, __LINE__, target_rank, idx);
            _l[target_rank] = idx;
            cnt++;
        }
        idx++;
        token = strtok (NULL, delimiters);
    } while (token != NULL);

    *num = cnt;
    fprintf (stderr, "[%s:%s:%d] Done\n", __FILE__, __func__, __LINE__);

    return ORTE_SUCCESS;
}


/*
 *
 */
static int
_parse_layout_desc (char *layout_desc, rmaps_explicit_layout_t *layout)
{
    int rc;
    char *s;
    char *token;
    const char delimiters[] = "[,]";
    const char block_delimiters[] = "[]";

    s = strdupa (layout_desc);
    token = strtok (s, delimiters);

    while (token != NULL)
    {
        if (strcasecmp (token, "MPI") == 0 || strcasecmp (token, "mpi") == 0)
        {
            fprintf (stderr, "Found MPI block\n");
            token = strtok (NULL, delimiters);
            token = strtok (NULL, delimiters);
            (*layout).target = strdup (token);

            token = strtok (NULL, block_delimiters);
            (*layout).places = strdup (token);
            fprintf (stderr, "[%s:%s:%d] Layout places: %s\n", __FILE__, __func__, __LINE__, (*layout).places);

            (*layout).locations = NULL; /* We need to make sure the ptr is set to NULL to have the expected behavior */
            rc = _parse_places (token, &((*layout).n_places), &((*layout).locations));
            if (rc != ORTE_SUCCESS)
                return ORTE_ERROR;
        }
        token = strtok (NULL, delimiters);
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
        if (obj->type != NULL)
        {
            if (target != NULL && strcasecmp (hwloc_obj_type_string (obj->type), target) == 0)
            {   
                fprintf (stderr, "LEVEL: %d - %s.   <--\n", level, hwloc_obj_type_string (obj->type));
            }
            else
            {   
                fprintf (stderr, "LEVEL: %d - %s.\n", level, hwloc_obj_type_string (obj->type));
            }
        }

        if (obj->children == NULL)
            obj = NULL;
        else
            obj = obj->children[0];

        level++;
    }
}


static int
orte_rmaps_explicit_register (void)
{
    int rc;

    _priority = 1;
    (void) mca_base_component_var_register (&mca_rmaps_explicit_component.base_version,
                                            "priority",
                                            "explicit rmaps component's priotiry",
                                            MCA_BASE_VAR_TYPE_INT,
                                            NULL,
                                            0,
                                            0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_READONLY,
                                            &_priority);

    (void) mca_base_component_var_register (&mca_rmaps_explicit_component.base_version,
                                            "layout",
                                            "PE layout",
                                            MCA_BASE_VAR_TYPE_STRING,
                                            NULL,
                                            0,
                                            0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_READONLY,
                                            &_pe_layout);

    fprintf (stderr, "Priority: %d\n", _priority);

    if (NULL == _pe_layout) {
        fprintf (stderr, "ERROR: _pe_layout is not defined (NULL)\n");
        fprintf (stderr, "INFO: Maybe Missing MCA 'rmaps_explicit_layout'\n");
        return ORTE_ERROR;
    } else {
        fprintf (stderr, "Layout: %s\n", _pe_layout);
    }

    /* Parse the layout */
    rc = _parse_layout_desc (_pe_layout, &layout);
    if (rc != ORTE_SUCCESS)
    {
        fprintf (stderr, "ERROR: _parse_layout_desc() failed\n");
        return ORTE_ERROR;
    }

    {
        uint64_t i;

        fprintf (stderr, "TARGET: %s\n", layout.target);
        fprintf (stderr, "PLACES: %s\n", layout.places);
        fprintf (stderr, "NUM PLACES: %"PRIu64"\n", layout.n_places);
        if (layout.n_places > 0 && layout.locations != NULL)
        {
            fprintf (stderr, "PLACES DETAILS: ");
            for (i = 0; i < layout.n_places; i++)
            {
                fprintf (stderr, "%d ", (int)layout.locations[i]);
            }
            fprintf (stderr, "\n");
        }
        _parse_topo (opal_hwloc_topology, layout.target);
    }

    return ORTE_SUCCESS;
}

static int
orte_rmaps_explicit_query (mca_base_module_t **module, int *priority)
{
    *priority = _priority;
    *module = (mca_base_module_t*)&orte_rmaps_explicit_module;

    return ORTE_SUCCESS;
}

static int
orte_rmaps_explicit_open (void)
{
    return ORTE_SUCCESS;
}

static int
orte_rmaps_explicit_close (void)
{
    return ORTE_SUCCESS;
}
