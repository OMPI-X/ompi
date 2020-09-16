/*
 * Copyright (c)	2018	UT-Battelle, LLC
 *                              All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADERS$
 */

#include "opal_config.h"

#include "opal/mca/mpool/base/base.h"
#include "opal/mca/mpool/sicm/mpool_sicm.h"

static void* mpool_sicm_alloc (mca_mpool_base_module_t *module, size_t size, size_t align, uint32_t flags);
static void* mpool_sicm_realloc (mca_mpool_base_module_t *module, void *addr, size_t size);
static void mpool_sicm_free (mca_mpool_base_module_t *module, void *addr);

mca_mpool_sicm_module_t mca_mpool_sicm_module = {
	.super = {
		.mpool_component        = &mca_mpool_sicm_component.super,
		.mpool_alloc            = mpool_sicm_alloc,
		.mpool_realloc          = mpool_sicm_realloc,
		.mpool_free             = mpool_sicm_free,
	},
	.sicm_is_initialized = false,
};

int
mpool_sicm_module_init (mca_mpool_sicm_module_t *module)
{
	module->super.mpool_component   = &mca_mpool_sicm_component.super;
	module->super.mpool_base        = NULL;
	module->super.mpool_alloc       = mpool_sicm_alloc;
	module->super.mpool_realloc     = mpool_sicm_realloc;
	module->super.mpool_free        = mpool_sicm_free;
	module->super.mpool_finalize    = mpool_sicm_finalize;
	module->super.mpool_ft_event    = NULL;
	module->super.flags             = MCA_MPOOL_FLAGS_MPI_ALLOC_MEM;

	OBJ_CONSTRUCT (&module->lock, opal_mutex_t);

	mca_mpool_sicm_component.devices = sicm_init ();
	module->target_device_type = INVALID_TAG;
	module->sicm_is_initialized = true;

	return OPAL_SUCCESS;
}

static void*
mpool_sicm_alloc (mca_mpool_base_module_t *module, size_t size, size_t align, uint32_t flags)
{
	sicm_arena arena;
	void *mem = NULL;
	sicm_device *dev;
	sicm_device_tag device_tag = SICM_DRAM;
	mca_mpool_sicm_module_t *_m;

	_m = (mca_mpool_sicm_module_t*)module;

	if (_m->sicm_is_initialized == false)
		mpool_sicm_module_init (_m);

	if (flags == SICM_DRAM)
		device_tag = SICM_DRAM;

	if (flags == SICM_KNL_HBM)
		device_tag = SICM_KNL_HBM;

	if (flags == SICM_POWERPC_HBM)
		device_tag = SICM_POWERPC_HBM;

	dev = sicm_find_device (&mca_mpool_sicm_component.devices, device_tag, 0, NULL);
	if (dev == NULL)
		return NULL;

	arena = sicm_arena_create (0, SICM_ALLOC_RELAXED, dev);
	if (arena == NULL)
		return NULL;

	if (align > 0)
		mem = sicm_arena_alloc_aligned (arena, size, align);
	else
		mem = sicm_arena_alloc (arena, size);

	return mem;
}

static void*
mpool_sicm_realloc (mca_mpool_base_module_t *module, void *addr, size_t size)
{
	sicm_arena arena;

	arena = sicm_arena_lookup (addr);
	if (arena == NULL)
		return NULL;

	return sicm_arena_realloc (arena, addr, size);
}

static void
mpool_sicm_free (mca_mpool_base_module_t *module, void *addr)
{
	sicm_arena arena;

	arena = sicm_arena_lookup (addr);
	if (arena == NULL)
		return;

	sicm_free (addr);

	// No API to destroy an arena - Memory leak
}

void
mpool_sicm_finalize (mca_mpool_base_module_t *module)
{
	mca_mpool_sicm_module_t *_m = (mca_mpool_sicm_module_t*) module;

	// No function to finalize sicm and/or free devices - Memory leaks

	OBJ_DESTRUCT (&_m->lock);
}
