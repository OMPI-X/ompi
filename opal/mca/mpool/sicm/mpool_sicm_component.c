/*
 * Copyright (c)	2018	UT-Battelle, LLC
 * 				All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADERS$
 */

#include "opal/mca/mpool/mpool.h"
#include "opal/mca/mpool/sicm/mpool_sicm.h"

static int mpool_sicm_priority;
static int mpool_sicm_verbose;

extern mca_mpool_sicm_module_t mca_mpool_sicm_module;

static int mpool_sicm_register (void);
static int mpool_sicm_open (void);
static int mpool_sicm_close (void);
static int mpool_sicm_query (const char *hints, int *priority, mca_mpool_base_module_t **module);

mca_mpool_sicm_component_t mca_mpool_sicm_component = {
	{
		.mpool_version = {
			MCA_MPOOL_BASE_VERSION_3_0_0,
			"sicm",
			MCA_BASE_MAKE_VERSION (
					component,
					OPAL_MAJOR_VERSION,
					OPAL_MINOR_VERSION,
					OPAL_RELEASE_VERSION),
			.mca_open_component = mpool_sicm_open,
			.mca_close_component = mpool_sicm_close,
			.mca_register_component_params = mpool_sicm_register
		},
		.mpool_data = {
			MCA_BASE_METADATA_PARAM_CHECKPOINT
		},
		.mpool_query = mpool_sicm_query,
	},
	.modules = NULL,
	.module_count = 0,
};


static int
mpool_sicm_register (void)
{
	mpool_sicm_priority = 0;
	mpool_sicm_verbose = 0;

#if 0
	mca_base_component_var_register (&mpool_sicm_component.super.mpool_version,
					 "default_type",
					 "Default sicm type to use",
					 MCA_BASE_VAR_TYPE_INT,
					 mpool_sicm_type_enum,
					 0,
					 0,
					 OPAL_INFO_LVL_5,
					 MCA_BASE_VAR_SCOPE_LOCAL
					 &mpool_sicm_component.default_type);
#endif

	mca_base_component_var_register (&mca_mpool_sicm_component.super.mpool_version,
	                                 "priority",
	                                 "Default priority of the SICM mpool component (default: 0)",
	                                 MCA_BASE_VAR_TYPE_INT,
	                                 NULL,
	                                 0,
	                                 0,
	                                 OPAL_INFO_LVL_9,
	                                 MCA_BASE_VAR_SCOPE_LOCAL,
	                                 &mpool_sicm_priority);

	mca_base_component_var_register (&mca_mpool_sicm_component.super.mpool_version,
	                                 "verbose",
	                                 "Default level of verbosity for the SICM mpool component (default: 0)",
	                                 MCA_BASE_VAR_TYPE_INT,
	                                 NULL,
	                                 0,
	                                 0,
	                                 OPAL_INFO_LVL_9,
	                                 MCA_BASE_VAR_SCOPE_LOCAL,
	                                 &mpool_sicm_verbose);

	return OPAL_SUCCESS;
}

static int
_mpool_sicm_extend_module_list (void)
{
	int n_mods = mca_mpool_sicm_component.module_count + 1;
	mca_mpool_sicm_module_t *_new_mod = NULL;

	if (n_mods == 1)
	{
		mca_mpool_sicm_component.modules = (mca_mpool_sicm_module_t**) malloc (sizeof (mca_mpool_sicm_module_t*));
	}
	else
	{
		mca_mpool_sicm_component.modules = (mca_mpool_sicm_module_t**) realloc (mca_mpool_sicm_component.modules, n_mods * sizeof (mca_mpool_sicm_module_t*));
	}

	if (mca_mpool_sicm_component.modules == NULL)
		return OPAL_ERR_OUT_OF_RESOURCE;

	_new_mod = (mca_mpool_sicm_module_t*) malloc (sizeof (mca_mpool_sicm_module_t));
	if (_new_mod == NULL)
		return OPAL_ERR_OUT_OF_RESOURCE;

	mpool_sicm_module_init (_new_mod);
	mca_mpool_sicm_component.modules[mca_mpool_sicm_component.module_count] = _new_mod;

	mca_mpool_sicm_component.module_count = n_mods;

	return OPAL_SUCCESS;
}

static int
mpool_sicm_open (void)
{
	if (mpool_sicm_verbose != 0)
	{
		mca_mpool_sicm_component.output = opal_output_open (NULL);
	}
	else
	{
		mca_mpool_sicm_component.output = -1;
	}

	// In case we can allocate memory based on various parameters,
	// we will instantiate a SICM module for each configuration
	// For now, we create one default module.
	_mpool_sicm_extend_module_list ();

	return OPAL_SUCCESS;
}

static int
mpool_sicm_close (void)
{
	for (int _i = 0; _i < mca_mpool_sicm_component.module_count; _i++)
	{
		mca_mpool_sicm_module_t *_m = mca_mpool_sicm_component.modules[_i];
		_m->super.mpool_finalize (&_m->super);
	}

	free (mca_mpool_sicm_component.modules);
	mca_mpool_sicm_component.modules = NULL;

	return OPAL_SUCCESS;
}

static void
_parse_hints (const char *hints, int *priority, mca_mpool_base_module_t **module)
{
	int _priority = 0;
	sicm_device_tag device_type = SICM_DRAM;

	if (hints)
	{
		char **_hints;
		int _i = 0;

		// _hints is NULL terminated
		_hints = opal_argv_split (hints, ',');
		if (_hints == NULL)
			return;

		while (_hints[_i] != NULL)
		{
			char *_key = _hints[_i];
			char *_val = NULL;
			char *_str = NULL;

			// The hints we are looking for are in the form of a key/value pair.
			// Separate the key from the value without copy.
			_str = strchr (_key, '=');
			if (_str != NULL)
			{
				_val = _str + 1;
				*_str = '\0';
			}

			if (strcasecmp("mpool", _key) == 0)
			{
				if (_val != NULL && strcasecmp ("sicm", _val) == 0)
				{
					// This module is the target of the request
					_priority = 100;
				}
			}

			if (strcasecmp("sicm_device_type", _key) == 0)
			{
				// Because SICM does not define enums in a precise way, we need to explicitely
				// figure out what the tag is
				if (strcmp (_val, "SICM_KNL_HBM") == 0)
					device_type = SICM_KNL_HBM;

				if (strcmp (_val, "SICM_POWERPC_HBM") == 0)
					device_type = SICM_POWERPC_HBM;

			}

			// TODO: make sure we get all the hints to drive the memory operation

			_i++;
		}

		opal_argv_free (_hints);
	}

	if (_priority > 0)
	{
		int i = 0;
		mca_mpool_sicm_module_t *_m = NULL;

		// We find the SICM module instance that handles the target type of device
		do
		{
			if (mca_mpool_sicm_component.modules[i]->target_device_type == device_type)
			{
				_m = mca_mpool_sicm_component.modules[i];
				break;
			}

			if (mca_mpool_sicm_component.modules[i]->target_device_type == INVALID_TAG)
			{
				mca_mpool_sicm_component.modules[i]->target_device_type = device_type;
				_m = mca_mpool_sicm_component.modules[i];
				break;
			}

			i++;
		} while (i < mca_mpool_sicm_component.module_count);

		if (_m == NULL)
		{
			// We did not find a suitable module so we create a new one
			_mpool_sicm_extend_module_list ();
			assert (mca_mpool_sicm_component.modules[mca_mpool_sicm_component.module_count]->target_device_type == INVALID_TAG);
			_m = mca_mpool_sicm_component.modules[mca_mpool_sicm_component.module_count];
			_m->target_device_type = device_type;
		}

		*module = (mca_mpool_base_module_t*)_m;
	}

	*priority = _priority;
}

static int
mpool_sicm_query (const char *hints, int *priority, mca_mpool_base_module_t **module)
{

	// hints is a string of comma-sperated keys that are used to pass parameters in
	if (hints)
	{
		// Parse the list of hints
		_parse_hints (hints, priority, module);
	}
	else
	{
		*priority = 0;
	}

	return OPAL_SUCCESS;
}

