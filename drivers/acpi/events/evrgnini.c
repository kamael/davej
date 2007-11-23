/******************************************************************************
 *
 * Module Name: evrgnini- ACPI Address_space / Op_region init
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "events.h"
#include "namesp.h"
#include "interp.h"
#include "amlcode.h"

#define _COMPONENT          EVENT_HANDLING
	 MODULE_NAME         ("evrgnini");


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ev_system_memory_region_setup
 *
 * PARAMETERS:  Region_obj          - region we are interested in
 *              Function            - start or stop
 *              Handler_context     - Address space handler context
 *              Returned context    - context to be used with each call to the
 *                                    handler for this region
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling, a nop for now
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ev_system_memory_region_setup (
	ACPI_HANDLE             handle,
	u32                     function,
	void                    *handler_context,
	void                    **return_context)
{
	MEM_HANDLER_CONTEXT     *mem_context;
	ACPI_OBJECT_INTERNAL    *region_obj = (ACPI_OBJECT_INTERNAL *) handle;


	if (function == ACPI_REGION_DEACTIVATE) {
		region_obj->region.region_flags &= ~(REGION_INITIALIZED);

		*return_context = NULL;
		if (handler_context) {
			mem_context = handler_context;
			*return_context = mem_context->handler_context;

			acpi_cm_free (mem_context);
		}
		return (AE_OK);
	}


	/* Activate.  Create a new context */

	mem_context = acpi_cm_callocate (sizeof (MEM_HANDLER_CONTEXT));
	if (!mem_context) {
		return (AE_NO_MEMORY);
	}

	/* Init.  (Mapping fields are all set to zeros above) */

	mem_context->handler_context = handler_context;
	region_obj->region.region_flags |= REGION_INITIALIZED;

	*return_context = mem_context;
	return (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ev_io_space_region_setup
 *
 * PARAMETERS:  Region_obj          - region we are interested in
 *              Function            - start or stop
 *              Handler_context     - Address space handler context
 *              Returned context    - context to be used with each call to the
 *                                    handler for this region
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ev_io_space_region_setup (
	ACPI_HANDLE             handle,
	u32                     function,
	void                    *handler_context,
	void                    **return_context)
{
	ACPI_OBJECT_INTERNAL    *region_obj = (ACPI_OBJECT_INTERNAL *) handle;


	if (function == ACPI_REGION_DEACTIVATE) {
		region_obj->region.region_flags &= ~(REGION_INITIALIZED);
		*return_context = handler_context;
		return (AE_OK);
	}

	/* Activate the region */

	region_obj->region.region_flags |= REGION_INITIALIZED;
	*return_context = handler_context;

	return (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ev_pci_config_region_setup
 *
 * PARAMETERS:  Region_obj          - region we are interested in
 *              Function            - start or stop
 *              Handler_context     - Address space handler context
 *              Returned context    - context to be used with each call to the
 *                                    handler for this region
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 * MUTEX:       Assumes namespace is locked
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ev_pci_config_region_setup (
	ACPI_HANDLE             handle,
	u32                     function,
	void                    *handler_context,
	void                    **return_context)
{
	ACPI_STATUS             status = AE_OK;
	u32                     temp;
	PCI_HANDLER_CONTEXT     *pci_context;
	ACPI_OBJECT_INTERNAL    *handler_obj;
	ACPI_NAMED_OBJECT       *search_scope;
	ACPI_OBJECT_INTERNAL    *region_obj = (ACPI_OBJECT_INTERNAL *) handle;


	handler_obj = region_obj->region.addr_handler;

	if (!handler_obj) {
		/*
		 *  No installed handler. This shouldn't happen because the dispatch
		 *  routine checks before we get here, but we check again just in case.
		 */
		return(AE_EXIST);
	}

	if (function == ACPI_REGION_DEACTIVATE) {
		region_obj->region.region_flags &= ~(REGION_INITIALIZED);

		*return_context = NULL;
		if (handler_context) {
			pci_context = handler_context;
			*return_context = pci_context->handler_context;

			acpi_cm_free (pci_context);
		}

		return (status);
	}


	/* Create a new context */

	pci_context = acpi_cm_allocate (sizeof(PCI_HANDLER_CONTEXT));
	if (!pci_context) {
		return (AE_NO_MEMORY);
	}

	/*
	 *  For PCI Config space access, we have to pass the segment, bus,
	 *  device and function numbers.  This routine must acquire those.
	 */

	/*
	 *  First get device and function numbers from the _ADR object
	 *  in the parent's scope.
	 */
	ACPI_ASSERT(region_obj->region.nte);

	search_scope = acpi_ns_get_parent_entry (region_obj->region.nte);


	acpi_cm_release_mutex (ACPI_MTX_NAMESPACE);

	/* Acpi_evaluate the _ADR object */

	status = acpi_cm_evaluate_numeric_object (METHOD_NAME__ADR, search_scope, &temp);
	/*
	 *  The default is zero, since the allocation above zeroed the data, just
	 *  do nothing on failures.
	 */
	if (ACPI_SUCCESS (status)) {
		/*
		 *  Got it..
		 */
		pci_context->dev_func = temp;
	}

	/*
	 *  Get the _SEG and _BBN values from the device upon which the handler
	 *  is installed.
	 *
	 *  We need to get the _SEG and _BBN objects relative to the PCI BUS device.
	 *  This is the device the handler has been registered to handle.
	 */

	search_scope = handler_obj->addr_handler.nte;

	status = acpi_cm_evaluate_numeric_object (METHOD_NAME__SEG, search_scope, &temp);
	if (ACPI_SUCCESS (status)) {
		/*
		 *  Got it..
		 */
		pci_context->seg = temp;
	}

	status = acpi_cm_evaluate_numeric_object (METHOD_NAME__BBN, search_scope, &temp);
	if (ACPI_SUCCESS (status)) {
		/*
		 *  Got it..
		 */
		pci_context->bus = temp;
	}

	acpi_cm_acquire_mutex (ACPI_MTX_NAMESPACE);

	*return_context = pci_context;

	region_obj->region.region_flags |= REGION_INITIALIZED;
	return (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ev_default_region_setup
 *
 * PARAMETERS:  Region_obj          - region we are interested in
 *              Function            - start or stop
 *              Handler_context     - Address space handler context
 *              Returned context    - context to be used with each call to the
 *                                    handler for this region
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ev_default_region_setup (
	ACPI_HANDLE             handle,
	u32                     function,
	void                    *handler_context,
	void                    **return_context)
{
	ACPI_OBJECT_INTERNAL    *region_obj = (ACPI_OBJECT_INTERNAL *) handle;


	if (function == ACPI_REGION_DEACTIVATE) {
		region_obj->region.region_flags &= ~(REGION_INITIALIZED);
		*return_context = NULL;
	}
	else {
		region_obj->region.region_flags |= REGION_INITIALIZED;
		*return_context = handler_context;
	}

	return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ev_initialize_region
 *
 * PARAMETERS:  Region_obj - Region we are initializing
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes the region, finds any _REG methods and saves them
 *              for execution at a later time
 *
 *              Get the appropriate address space handler for a newly
 *              created region.
 *
 *              This also performs address space specific intialization.  For
 *              example, PCI regions must have an _ADR object that contains
 *              a PCI address in the scope of the defintion.  This address is
 *              required to perform an access to PCI config space.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ev_initialize_region (
	ACPI_OBJECT_INTERNAL    *region_obj,
	u8                      acpi_ns_locked)
{
	ACPI_OBJECT_INTERNAL   *handler_obj;
	ACPI_OBJECT_INTERNAL   *obj_desc;
	u32                     space_id;
	ACPI_NAMED_OBJECT      *entry;        /* Namespace Object */
	ACPI_STATUS             status;
	ACPI_NAMED_OBJECT      *reg_entry;
	ACPI_NAME              *reg_name_ptr = (ACPI_NAME *) METHOD_NAME__REG;


	if (!region_obj) {
		return (AE_BAD_PARAMETER);
	}

	ACPI_ASSERT(region_obj->region.nte);

	entry = acpi_ns_get_parent_entry (region_obj->region.nte);
	space_id = region_obj->region.space_id;

	region_obj->region.addr_handler = NULL;
	region_obj->region.REGmethod = NULL;
	region_obj->region.region_flags = INITIAL_REGION_FLAGS;

	/*
	 *  Find any "_REG" associated with this region definition
	 */
	status = acpi_ns_search_one_scope (*reg_name_ptr, entry->child_table,
			  ACPI_TYPE_METHOD, &reg_entry, NULL);
	if (status == AE_OK) {
		/*
		 *  The _REG method is optional and there can be only one per region
		 *  definition.  This will be executed when the handler is attached
		 *  or removed
		 */
		region_obj->region.REGmethod = reg_entry;
	}

	/*
	 *  The following loop depends upon the root nte having no parent
	 *  ie: Acpi_gbl_Root_object->Parent_entry being set to NULL
	 */
	while (entry) {
		/*
		 *  Check to see if a handler exists
		 */
		handler_obj = NULL;
		obj_desc = acpi_ns_get_attached_object ((ACPI_HANDLE) entry);
		if (obj_desc) {
			/*
			 *  can only be a handler if the object exists
			 */
			switch (entry->type)
			{
			case ACPI_TYPE_DEVICE:

				handler_obj = obj_desc->device.addr_handler;
				break;

			case ACPI_TYPE_PROCESSOR:

				handler_obj = obj_desc->processor.addr_handler;
				break;

			case ACPI_TYPE_THERMAL:

				handler_obj = obj_desc->thermal_zone.addr_handler;
				break;
			}

			while (handler_obj) {
				/*
				 *  This guy has at least one address handler
				 *  see if it has the type we want
				 */
				if (handler_obj->addr_handler.space_id == space_id) {
					/*
					 *  Found it! Now update the region and the handler
					 */
					acpi_ev_associate_region_and_handler(handler_obj, region_obj);
					return (AE_OK);
				}

				handler_obj = handler_obj->addr_handler.link;

			} /* while handlerobj */
		}

		/*
		 *  This one does not have the handler we need
		 *  Pop up one level
		 */
		entry = acpi_ns_get_parent_entry (entry);

	} /* while Entry != ROOT */

	/*
	 *  If we get here, there is no handler for this region
	 */
	return (AE_NOT_EXIST);
}
