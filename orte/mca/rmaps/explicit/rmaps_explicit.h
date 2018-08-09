/*
 * Copyright (c)    2018    UT-Battelle, LLC
 *                          All rights reserved.
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef ORTE_RMAPS_EXPLICIT_H
#define ORTE_RMAPS_EXPLICIT_H

#include "orte_config.h"
#include "orte/mca/rmaps/base/base.h"

BEGIN_C_DECLS

typedef enum layout_policy
{
    LAYOUT_POLICY_NONE = 0,
    LAYOUT_POLICY_RR,
    LAYOUT_POLICY_SPREAD,
} layout_policy_t;

typedef enum rmaps_explicit_mode
{
    RMAPS_EXPLICIT_MODE_NONE = 0,
    RMAPS_EXPLICIT_MODE_MANUAL,
    RMAPS_EXPLICIT_MODE_AUTO,
} rmaps_explicit_mode_t;

typedef struct rmaps_explicit_layout {
    rmaps_explicit_mode_t mode;

    /* FIXME use a union here once things will get stabilized */

    /* Specific to the auto mode */
    int scope;
    layout_policy_t policy;
    int n_per_scope;
    int n_pes;

    /* Specific to the manual mode */
    char *target;
    char *places;
    uint64_t n_places;
    uint8_t *locations;
} rmaps_explicit_layout_t;


ORTE_MODULE_DECLSPEC extern orte_rmaps_base_component_t mca_rmaps_explicit_component;

extern orte_rmaps_base_module_t orte_rmaps_explicit_module;

END_C_DECLS

#endif /* ORTE_RMAPS_EXPLICIT_H */
