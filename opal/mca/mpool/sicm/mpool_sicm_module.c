/*
 * Copyright (c) 2018-2020  UT-Battelle, LLC
 *                          All rights reserved.
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

static int _mpool_sicm_dump_devices(void);
static int _mpool_sicm_show_device_info(struct sicm_device *device, int devnum);

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

	/*
	 * Only initialize the master list of devices once at
	 * the Component level.  Then have Module(s) for each
	 * of the different device_types.
	 */
	if (NULL == mca_mpool_sicm_component.devices.devices) {

		mca_mpool_sicm_component.devices = sicm_init ();

		opal_output_verbose(5, SICM_OUT,
		     "mpool:sicm sicm_init() with device count: %d\n",
		     mca_mpool_sicm_component.devices.count);

		if (opal_output_check_verbosity(20, SICM_OUT)) {
			_mpool_sicm_dump_devices();
		}

	}

	module->target_device_type = INVALID_TAG;
	module->sicm_is_initialized = true;

	opal_output_verbose(5, SICM_OUT,
	                    "mpool:sicm module init devices.count: %d\n",
	                    mca_mpool_sicm_component.devices.count);

	return OPAL_SUCCESS;
}

static void*
mpool_sicm_alloc (mca_mpool_base_module_t *module, size_t size, size_t align, uint32_t flags)
{
	sicm_arena arena;
	void *mem = NULL;
	sicm_device *dev = NULL;
	sicm_device_tag device_tag = SICM_DRAM;
	mca_mpool_sicm_module_t *_m;
	sicm_device_list devs_list;
	int i;

	_m = (mca_mpool_sicm_module_t*)module;

	if (_m->sicm_is_initialized == false)
		mpool_sicm_module_init (_m);

	if (_m->target_device_type == SICM_DRAM)
		device_tag = SICM_DRAM;

	if (_m->target_device_type == SICM_KNL_HBM)
		device_tag = SICM_KNL_HBM;

	if (_m->target_device_type == SICM_POWERPC_HBM)
		device_tag = SICM_POWERPC_HBM;

#if 0
	if (flags == SICM_DRAM)
		device_tag = SICM_DRAM;

	if (flags == SICM_KNL_HBM)
		device_tag = SICM_KNL_HBM;

	if (flags == SICM_POWERPC_HBM)
		device_tag = SICM_POWERPC_HBM;
#endif

	OPAL_OUTPUT_VERBOSE((20, SICM_OUT,
	          "mpool:sicm DEBUG sicm_find_device 'device_tag' = %d (%s)\n",
	          device_tag,
	          (device_tag == SICM_DRAM)? "SICM_DRAM" :
	            (device_tag == SICM_POWERPC_HBM)? "SICM_POWERPC_HBM" :
	              "other"));

	dev = sicm_find_device (&mca_mpool_sicm_component.devices, device_tag, 0, NULL);
	if (dev == NULL) {
		OPAL_OUTPUT_VERBOSE((0, SICM_OUT,
		   "mpool:sicm ERROR sicm_find_device() failed to find device"
		   "(device_tag=%d)\n", device_tag));
		return NULL;
	}

	/* Create temporary list */
	devs_list.count = 1;
	devs_list.devices = (struct sicm_device**) malloc(devs_list.count * sizeof(struct sicm_device));
	devs_list.devices[0] = dev;

	if (opal_output_check_verbosity(20, SICM_OUT)) {
		opal_output_verbose(1, SICM_OUT, "======DEBUG mpool alloc (begin)========\n");
		for (i = 0; i < devs_list.count; i++) {
			struct sicm_device *d = devs_list.devices[i];
			_mpool_sicm_show_device_info(d, -1);
		}
		opal_output_verbose(1, SICM_OUT, "======DEBUG mpool alloc (end)========\n");
	}

	//arena = sicm_arena_create (0, SICM_ALLOC_RELAXED, dev);
	arena = sicm_arena_create (0, SICM_ALLOC_RELAXED, &devs_list);

	if (NULL != devs_list.devices) {
		free(devs_list.devices);
	}

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

	sicm_arena_destroy(arena);
}

void
mpool_sicm_finalize (mca_mpool_base_module_t *module)
{
	mca_mpool_sicm_module_t *_m = (mca_mpool_sicm_module_t*) module;

	// No function to finalize sicm and/or free devices - Memory leaks

	/*
	 * TODO: (TJN) Free the module from component list,
	 *             Call sicm_fini() to release the devices,
	 *             Free component device list.
	 *             But make sure not happen multiple times,
	 *             and that sicm_fini is at end.
	 */

	OBJ_DESTRUCT (&_m->lock);
}


static int
_mpool_sicm_show_device_info(struct sicm_device *device, int devnum)
{
	if (NULL == device) {
		OPAL_OUTPUT_VERBOSE((1, SICM_OUT,
		                    "mpool:sicm ERROR - bad param %s()\n",
		                    __func__));
		return OPAL_ERROR;
	}

	sicm_pin(device);

	opal_output(0, "mpool:sicm -----------------------------\n");
	opal_output(0, "mpool:sicm SICM    device: %d\n", devnum);
	switch(device->tag) {
	  case SICM_DRAM:
	    opal_output(0, "mpool:sicm SICM      type: SICM_DRAM\n");
	    break;
	  case SICM_KNL_HBM:
	    opal_output(0, "mpool:sicm SICM      type: SICM_KNL_HBM\n");
	    break;
	  case SICM_POWERPC_HBM:
	    opal_output(0, "mpool:sicm SICM      type: SICM_POWERPC_HBM\n");
	    break;
	  case SICM_OPTANE:
	    opal_output(0, "mpool:sicm SICM      type: SICM_OPTANE\n");
	    break;
	  case INVALID_TAG:
	    opal_output(0, "mpool:sicm SICM      type: INVALID_TAG\n");
	    break;
	  default:
	    opal_output(0, "mpool:sicm SICM      type: unknown (bad)\n");
	}
	opal_output(0, "mpool:sicm SICM numa node: %d\n",
	            sicm_numa_id(device));
	opal_output(0, "mpool:sicm SICM page size: %d\n",
	            sicm_device_page_size(device));
	opal_output(0, "mpool:sicm SICM  capacity: %ld\n",
	            sicm_capacity(device));
	opal_output(0, "mpool:sicm SICM available: %ld\n",
	            sicm_avail(device));
	opal_output(0, "mpool:sicm -----------------------------\n");

	return OPAL_SUCCESS;
}

static int
_mpool_sicm_dump_devices(void)
{
	unsigned int i;
	sicm_device *device;

	if (NULL != mca_mpool_sicm_component.devices.devices) {

		for (i=0; i < mca_mpool_sicm_component.devices.count; i++) {
			device = mca_mpool_sicm_component.devices.devices[i];
			_mpool_sicm_show_device_info(device, i);
		}

	} else {
		opal_output(0, "mpool:sicm No SICM devices data.\n");
		return OPAL_ERROR;
	}

	return OPAL_SUCCESS;
}
