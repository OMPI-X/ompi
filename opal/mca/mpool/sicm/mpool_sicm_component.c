/*
 * Copyright (c) 2018-2020 UT-Battelle, LLC
 *                         All rights reserved.
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

	opal_output_verbose(5, SICM_OUT, "mpool:sicm module_count = %d\n", n_mods);

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
	//sicm_device_tag device_type = SICM_DRAM;
	sicm_device_tag device_type = -1;

	if (hints)
	{
		char **_hints;
		int _i = 0;

		OPAL_OUTPUT_VERBOSE((20, SICM_OUT,
		                    "mpool:sicm DEBUG parsing hints='%s'\n", hints));

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
				if (strcmp (_val, "SICM_DRAM") == 0) {
					device_type = SICM_DRAM;
					OPAL_OUTPUT_VERBOSE((10, SICM_OUT,
					                    "mpool:sicm DEBUG type %s (device_type=%d)\n",
					                    "SICM_DRAM",
					                    device_type));
				}

				if (strcmp (_val, "SICM_KNL_HBM") == 0) {
					device_type = SICM_KNL_HBM;
                    			OPAL_OUTPUT_VERBOSE((10, SICM_OUT,
					                    "mpool:sicm DEBUG type %s (device_type=%d)\n",
					                    "SICM_KNL_HBM",
					                    device_type));
				}

				if (strcmp (_val, "SICM_POWERPC_HBM") == 0) {
					device_type = SICM_POWERPC_HBM;
					OPAL_OUTPUT_VERBOSE((10, SICM_OUT,
					                    "mpool:sicm DEBUG type %s (device_type=%d)\n",
					                    "SICM_POWERPC_HBM",
					                    device_type));
				}
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

		/* If still have sentinal, we likely got back user input */
		if (device_type == -1) {
			opal_output_verbose(1, SICM_OUT,
			        "\n**********************************************************\n"
			        "mpool:sicm WARN Possibly bad mpool hint 'sicm_device_type'?\n"
			        "**********************************************************\n");
        }

		// We find the SICM module instance that handles the target type of device
		do
		{
			OPAL_OUTPUT_VERBOSE((20, SICM_OUT,
			    "mpool:sicm DEBUG MODULE CHECK module[%d].target_device_type = %d, device_type = %d\n",
			    i,
			    mca_mpool_sicm_component.modules[i]->target_device_type,
			    device_type));

			if (mca_mpool_sicm_component.modules[i]->target_device_type == device_type)
			{
				OPAL_OUTPUT_VERBOSE((20, SICM_OUT,
				    "mpool:sicm DEBUG MODULE FOUND - TARGET_DEVICE_TYPE => %d\n",
				    device_type));
				_m = mca_mpool_sicm_component.modules[i];
				break;
			}

#if 0
			if (mca_mpool_sicm_component.modules[i]->target_device_type == INVALID_TAG)
			{
				fprintf(stderr, "WARN: Ignore requests for INVALID_TAG devices - TARGET_DEVICE_TYPE => %d (INVALID_TAG)\n",
					mca_mpool_sicm_component.modules[i]->target_device_type);
				_m = mca_mpool_sicm_component.modules[i];
				break;
			}
#endif

			i++;
		} while (i < mca_mpool_sicm_component.module_count);

        /*
         * TODO: Check if device_type is in list of available devices
         *      (search component.devices list).  Example: May ask
         *      for KNL_HBM but not on KNL machine so no KNL_HBM device.
         *
         *       if ( _mpool_sicm_check_supported(device_type) ) ...
         */

		if (_m == NULL)
		{
			OPAL_OUTPUT_VERBOSE((20, SICM_OUT,
			     "mpool:sicm DEBUG SICM MPOOL NOT FIND VALID MODULE SO CREATE NEW ONE\n"));

			// We did not find a suitable module so we create a new one
			_mpool_sicm_extend_module_list ();
			assert (mca_mpool_sicm_component.modules[(mca_mpool_sicm_component.module_count - 1)]->target_device_type == INVALID_TAG);
			_m = mca_mpool_sicm_component.modules[(mca_mpool_sicm_component.module_count - 1)];
			_m->target_device_type = device_type;

			OPAL_OUTPUT_VERBOSE((20, SICM_OUT,
			     "mpool:sicm DEBUG ASSIGNED device_type=%d to new module (module_count=%d)\n", device_type, mca_mpool_sicm_component.module_count));
		}

		OPAL_OUTPUT_VERBOSE((20, SICM_OUT,
		     "mpool:sicm DEBUG SICM MPOOL module.target_device_type=%d\n",
		     _m->target_device_type));

		*module = (mca_mpool_base_module_t*)_m;
	}

	opal_output_verbose(5, SICM_OUT,
	     "mpool:sicm SICM priority=%d\n", _priority);

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

