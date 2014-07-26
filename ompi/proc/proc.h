/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2012 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2007-2012 Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * Copyright (c) 2013      Intel, Inc. All rights reserved
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */


/** @file
 * Process identification structure interface
 *
 * Process identification structure interface.  The ompi_proc_t
 * structure contatins basic information about the remote (and local)
 * processes.
 */

#ifndef OMPI_PROC_PROC_H
#define OMPI_PROC_PROC_H

#include "ompi_config.h"
#include "ompi/types.h"

#include "opal/util/proc.h"

#include "ompi/mca/rte/rte.h"


BEGIN_C_DECLS

/* ******************************************************************** */


/**
 * Remote Open MPI process structure
 * 
 * Remote Open MPI process structure.  Each process contains exactly
 * one ompi_proc_t structure for each remote process it knows about.
 *
 * Each proc entry has an array of endpoint data associated with it.
 * The size of this array, and its entries, is unique to a particular
 * build of Open MPI.  As the endpoint list (or index values) are
 * local to a process, this does not negatively impact heterogeneous
 * builds.  If a component or framework requires a tag index, it
 * should call OMPI_REQUIRE_ENDPOINT_TAG(<name>).  Requests which
 * share the same name will have the same value, allowing
 * cross-component sharing of endpoint data.  The tag may be referenced
 * by the pre-processor define OMPI_PROC_ENDPOINT_TAG_<name>.  Adding 
 * a tag increases the memory consumed by Open MPI, so should only be done
 * if unavoidable.
 */
struct ompi_proc_t {
    opal_proc_t                     super;

    /* endpoint data */
    void *proc_endpoints[OMPI_PROC_ENDPOINT_TAG_MAX];
};
typedef struct ompi_proc_t ompi_proc_t;
OBJ_CLASS_DECLARATION(ompi_proc_t);


/**
 * @private
 *
 * Pointer to the ompi_proc_t structure for the local process
 *
 * @note This pointer is declared here to allow inline functions
 * within this header file to access the local process quickly.
 * Please use ompi_proc_local() instead.
 */
OMPI_DECLSPEC extern ompi_proc_t* ompi_proc_local_proc;


/* ******************************************************************** */


/**
 * Initialize the OMPI process subsystem
 *
 * Initialize the Open MPI process subsystem.  This function will
 * query the run-time environment and build a list of the proc
 * instances in the current MPI_COMM_WORLD.  The local information not
 * easily determined by the run-time ahead of time (architecture and
 * hostname) will be published during this call.
 *
 * @note While an ompi_proc_t will exist with mostly valid information
 * for each process in the MPI_COMM_WORLD at the conclusion of this
 * call, some information will not be immediately available.  This
 * includes the architecture and hostname, which will be available by
 * the conclusion of the stage gate.
 *
 * @retval OMPI_SUCESS  System successfully initialized
 * @retval OMPI_ERROR   Initialization failed due to unspecified error
 */
OMPI_DECLSPEC int ompi_proc_init(void);

/**
 * Complete filling up the proc information (arch, name and locality) for all
 * procs related to this job. This function is to be called only after
 * the modex exchange has been completed.
 *
 * @retval OMPI_SUCCESS All information correctly set.
 * @retval OMPI_ERROR   Some info could not be initialized.
 */
OMPI_DECLSPEC int ompi_proc_complete_init(void);

/**
 * Finalize the OMPI Process subsystem
 *
 * Finalize the Open MPI process subsystem.  This function will
 * release all memory created during the life of the application,
 * including all ompi_proc_t structures.
 *
 * @retval OMPI_SUCCESS  System successfully finalized
 */
OMPI_DECLSPEC int ompi_proc_finalize(void);


/**
 * Returns the list of proc instances associated with this job.
 *
 * Returns the list of proc instances associated with this job.  Given
 * the current association between a job and an MPI_COMM_WORLD, this
 * function provides the process instances for the current
 * MPI_COMM_WORLD.
 *
 * @note The reference count of each process in the array is
 * NOT incremented - the caller is responsible for ensuring the
 * correctness of the reference count once they are done with
 * the array.
 *
 * @param[in] size     Number of processes in the ompi_proc_t array
 *
 * @return Array of pointers to proc instances in the current
 * MPI_COMM_WORLD, or NULL if there is an internal failure.
 */
OMPI_DECLSPEC ompi_proc_t** ompi_proc_world(size_t* size);


/**
 * Returns the list of all known proc instances.
 *
 * Returns the list of all known proc instances, including those in
 * other MPI_COMM_WORLDs.  It is possible that we may no longer be
 * connected to some of the procs returned (in the MPI sense of the
 * word connected).  In a strictly MPI-1 application, this function
 * will return the same information as ompi_proc_world().
 *
 * @note The reference count of each process in the array is
 * incremented and the caller is responsible for releasing each
 * process in the array, as well as freeing the array.
 *
 * @param[in] size     Number of processes in the ompi_proc_t array
 *
 * @return Array of pointers to proc instances in the current
 * known universe, or NULL if there is an internal failure.
 */
OMPI_DECLSPEC ompi_proc_t** ompi_proc_all(size_t* size);


/**
 * Returns a list of the local process
 *
 * Returns a list containing the local process (and only the local
 * process).  Has calling semantics similar to ompi_proc_world() and
 * ompi_proc_all().
 *
 * @note The reference count of each process in the array is
 * incremented and the caller is responsible for releasing each
 * process in the array, as well as freeing the array.
 *
 * @param[in] size     Number of processes in the ompi_proc_t array
 *
 * @return Array of pointers to proc instances in the current
 * known universe, or NULL if there is an internal failure.
 */
OMPI_DECLSPEC ompi_proc_t** ompi_proc_self(size_t* size);


/**
 * Returns a pointer to the local process
 *
 * Returns a pointer to the local process.  Unlike ompi_proc_self(),
 * the reference count on the local proc instance is not modified by
 * this function.
 *
 * @return Pointer to the local process structure
 */
static inline ompi_proc_t* ompi_proc_local(void) 
{   
    return ompi_proc_local_proc;
}


/**
 * Returns the proc instance for a given name 
 *
 * Returns the proc instance for the specified process name.  The
 * reference count for the proc instance is not incremented by this
 * function.
 *
 * @param[in] name     The process name to look for
 *
 * @return Pointer to the process instance for \c name
*/
OMPI_DECLSPEC ompi_proc_t * ompi_proc_find ( const ompi_process_name_t* name );

/**
 * Pack proc list into portable buffer
 *
 * This function takes a list of ompi_proc_t pointers (e.g. as given
 * in groups) and returns a orte buffer containing all information
 * needed to add the proc to a remote list.  This includes the ORTE
 * process name, the architecture, and the hostname.  Ordering is
 * maintained.  The buffer is packed to be sent to a remote node with
 * different architecture (endian or word size).
 * 
 * @param[in] proclist     List of process pointers
 * @param[in] proclistsize Length of the proclist array
 * @param[in,out] buf      An opal_buffer containing the packed names.  
 *                         The buffer must be constructed but empty when
 *                         passed to this function
 * @retval OMPI_SUCCESS    Success
 * @retval OMPI_ERROR      Unspecified error
 */
OMPI_DECLSPEC int ompi_proc_pack(ompi_proc_t **proclist, int proclistsize,
                                 bool full_info,
                                 opal_buffer_t *buf);


/**
 * Unpack a portable buffer of procs
 *
 * This function unpacks a packed list of ompi_proc_t structures and
 * returns the ordered list of proc structures.  If the given proc is
 * already "known", the architecture and hostname information in the
 * buffer is ignored.  If the proc is "new" to this process, it will
 * be added to the global list of known procs, with information
 * provided in the buffer.  The lookup actions are always entirely
 * local.  The proclist returned is a list of pointers to all procs in
 * the buffer, whether they were previously known or are new to this
 * process.
 *
 * @note In previous versions of this function, The PML's add_procs()
 * function was called for any new processes discovered as a result of
 * this operation.  That is no longer the case -- the caller must use
 * the newproclist information to call add_procs() if necessary.
 *
 * @note The reference count for procs created as a result of this
 * operation will be set to 1.  Existing procs will not have their
 * reference count changed.  The reference count of a proc at the
 * return of this function is the same regardless of whether NULL is
 * provided for newproclist.  The user is responsible for freeing the
 * newproclist array.
 *
 * @param[in] buf          opal_buffer containing the packed names
 * @param[in] proclistsize number of expected proc-pointres
 * @param[out] proclist    list of process pointers
 * @param[out] newproclistsize Number of new procs added as a result
 *                         of the unpack operation.  NULL may be
 *                         provided if information is not needed.
 * @param[out] newproclist List of new procs added as a result of
 *                         the unpack operation.  NULL may be
 *                         provided if informationis not needed.
 *
 * Return value:
 *   OMPI_SUCCESS               on success
 *   OMPI_ERROR                 else
 */
OMPI_DECLSPEC int ompi_proc_unpack(opal_buffer_t *buf, 
                                   int proclistsize, ompi_proc_t ***proclist,
                                   bool full_info,
                                   int *newproclistsize, ompi_proc_t ***newproclist);

/**
 * Refresh the OMPI process subsystem
 *
 * Refresh the Open MPI process subsystem. This function will update
 * the list of proc instances in the current MPI_COMM_WORLD with
 * data from the run-time environemnt.
 *
 * @note This is primarily used when restarting a process and thus
 * need to update the jobid and node name.
 *
 * @retval OMPI_SUCESS  System successfully refreshed
 * @retval OMPI_ERROR   Refresh failed due to unspecified error
 */
OMPI_DECLSPEC int ompi_proc_refresh(void);


/**
 * Set the locality of a proc relative to me
 */
OMPI_DECLSPEC int ompi_proc_set_locality(ompi_proc_t *proc);

END_C_DECLS

#endif /* OMPI_PROC_PROC_H */
