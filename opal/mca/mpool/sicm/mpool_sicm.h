/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c)	2018	UT-Battelle, LLC
 * 				All rights reserved
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADERS$
 */

#ifndef MCA_MPOOL_SICM_H
#define MCA_MPOOL_SICM_H

#include "opal_config.h"
#include "opal/mca/threads/mutex.h"

#include "opal/mca/event/event.h"
#include "opal/mca/mpool/mpool.h"
#include "opal/mca/mpool/base/base.h"

#include "sicm_low.h"

#define SICM_OUT opal_mpool_base_framework.framework_output

BEGIN_C_DECLS

struct mca_mpool_sicm_module_t {
	mca_mpool_base_module_t super;
	bool			sicm_is_initialized;
	sicm_device_tag		target_device_type;
	opal_mutex_t		lock;
};
typedef struct mca_mpool_sicm_module_t mca_mpool_sicm_module_t;

#if 0
struct mca_mpool_sicm_module_le_t {
	opal_list_item_t        super;
	mca_mpool_sicm_module_t module;
};
typedef struct mca_mpool_sicm_module_le_t mca_mpool_sicm_module_le_t;
OBJ_CLASS_DECLARATION(mca_mpool_sicm_module_le_t);
#endif

struct mca_mpool_sicm_component_t {
	mca_mpool_base_component_t      super;
	int				module_count;
	mca_mpool_sicm_module_t         **modules;
	int                             priority;
	int                             output;
	sicm_device_list                devices;
};
typedef struct mca_mpool_sicm_component_t mca_mpool_sicm_component_t;
OPAL_MODULE_DECLSPEC extern mca_mpool_sicm_component_t mca_mpool_sicm_component;

int mpool_sicm_module_init (mca_mpool_sicm_module_t *module);

void mpool_sicm_finalize (mca_mpool_base_module_t *module);

#if 0
static void* mpool_sicm_alloc (mca_mpool_base_module_t *module, size_t size, size_t align, uint32_t flags);

static void* mpool_sicm_realloc (mca_mpool_base_module_t *module, void *addr, size_t size);

static void mpool_sicm_free (mca_mpool_base_module_t *module, void *addr);
#endif

END_C_DECLS

#endif /* MCA_MPOOL_SICM_H */
