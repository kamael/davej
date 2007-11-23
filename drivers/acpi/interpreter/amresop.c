
/******************************************************************************
 *
 * Module Name: amresop - AML Interpreter operand/object resolution
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
#include "amlcode.h"
#include "parser.h"
#include "dispatch.h"
#include "interp.h"
#include "namesp.h"
#include "tables.h"
#include "events.h"


#define _COMPONENT          INTERPRETER
	 MODULE_NAME         ("amresop");


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_resolve_operands
 *
 * PARAMETERS:  Opcode              Opcode being interpreted
 *              Stack_ptr           Top of operand stack
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert stack entries to required types
 *
 *      Each nibble in Arg_types represents one required operand
 *      and indicates the required Type:
 *
 *      The corresponding stack entry will be converted to the
 *      required type if possible, else return an exception
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_resolve_operands (
	u16                     opcode,
	ACPI_OBJECT_INTERNAL    **stack_ptr)
{
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_STATUS             status = AE_OK;
	u8                      object_type;
	ACPI_HANDLE             temp_handle;
	u32                     arg_types;
	ACPI_OP_INFO            *op_info;
	u32                     this_arg_type;


	op_info = acpi_ps_get_opcode_info (opcode);
	if (!op_info) {
		return (AE_AML_BAD_OPCODE);
	}


	arg_types = op_info->runtime_args;
	if (arg_types == ARGI_INVALID_OPCODE) {
		status = AE_AML_INTERNAL;
		goto cleanup;
	}


   /*
	 * Normal exit is with *Types == '\0' at end of string.
	 * Function will return an exception from within the loop upon
	 * finding an entry which is not, and cannot be converted
	 * to, the required type; if stack underflows; or upon
	 * finding a NULL stack entry (which "should never happen").
	 */

	while (GET_CURRENT_ARG_TYPE (arg_types)) {
		if (!stack_ptr || !*stack_ptr) {
			status = AE_AML_INTERNAL;
			goto cleanup;
		}

		/* Extract useful items */

		obj_desc = *stack_ptr;

		/* Decode the descriptor type */

		if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_NAMED)) {
			/* NTE */

			object_type = ((ACPI_NAMED_OBJECT*) obj_desc)->type;
		}

		else if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_INTERNAL)) {
			/* ACPI internal object */

			object_type = obj_desc->common.type;

			/* Check for bad ACPI_OBJECT_TYPE */

			if (!acpi_aml_validate_object_type (object_type)) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}

			if (object_type == (u8) INTERNAL_TYPE_REFERENCE) {
				/*
				 * Decode the Reference
				 */

				op_info = acpi_ps_get_opcode_info (opcode);
				if (!op_info) {
					return (AE_AML_BAD_OPCODE);
				}


				switch (obj_desc->reference.op_code)
				{
				case AML_ZERO_OP:
				case AML_ONE_OP:
				case AML_ONES_OP:
				case AML_DEBUG_OP:
				case AML_NAME_OP:
				case AML_INDEX_OP:
				case AML_ARG_OP:
				case AML_LOCAL_OP:

					break;

				default:
					status = AE_AML_OPERAND_TYPE;
					goto cleanup;
					break;
				}
			}

		}

		else {
			/* Invalid descriptor */

			status = AE_AML_OPERAND_TYPE;
			goto cleanup;
		}


		/*
		 * Decode a character from the type string
		 */

		this_arg_type = GET_CURRENT_ARG_TYPE (arg_types);
		INCREMENT_ARG_LIST (arg_types);


		switch (this_arg_type)
		{

		case ARGI_REFERENCE:   /* Reference */
		case ARGI_TARGETREF:

			/* Need an operand of type INTERNAL_TYPE_REFERENCE */

			if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_NAMED))            /* direct name ptr OK as-is */ {
				break;
			}

			if (INTERNAL_TYPE_REFERENCE != object_type) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}

			if (AML_NAME_OP == obj_desc->reference.op_code) {
				/*
				 * Convert an indirect name ptr to direct name ptr and put
				 * it on the stack
				 */

				temp_handle = obj_desc->reference.object;
				acpi_cm_remove_reference (obj_desc);
				(*stack_ptr) = temp_handle;
			}
			break;


		case ARGI_NUMBER:   /* Number */

			/* Need an operand of type ACPI_TYPE_NUMBER */

			if ((status = acpi_aml_resolve_to_value (stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			if (ACPI_TYPE_NUMBER != (*stack_ptr)->common.type) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		case ARGI_STRING:

			/* Need an operand of type ACPI_TYPE_STRING or ACPI_TYPE_BUFFER */

			if ((status = acpi_aml_resolve_to_value (stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			if ((ACPI_TYPE_STRING != (*stack_ptr)->common.type) &&
				(ACPI_TYPE_BUFFER != (*stack_ptr)->common.type))
			{
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		case ARGI_BUFFER:

			/* Need an operand of type ACPI_TYPE_BUFFER */

			if ((status = acpi_aml_resolve_to_value(stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			if (ACPI_TYPE_BUFFER != (*stack_ptr)->common.type) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		case ARGI_MUTEX:

			/* Need an operand of type ACPI_TYPE_MUTEX */

			if ((status = acpi_aml_resolve_to_value(stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			if (ACPI_TYPE_MUTEX != (*stack_ptr)->common.type) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		case ARGI_EVENT:

			/* Need an operand of type ACPI_TYPE_EVENT */

			if ((status = acpi_aml_resolve_to_value(stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			if (ACPI_TYPE_EVENT != (*stack_ptr)->common.type) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		case ARGI_REGION:

			/* Need an operand of type ACPI_TYPE_REGION */

			if ((status = acpi_aml_resolve_to_value(stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			if (ACPI_TYPE_REGION != (*stack_ptr)->common.type) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		 case ARGI_IF:   /* If */

			/* Need an operand of type INTERNAL_TYPE_IF */

			if (INTERNAL_TYPE_IF != (*stack_ptr)->common.type) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		case ARGI_PACKAGE:   /* Package */

			/* Need an operand of type ACPI_TYPE_PACKAGE */

			if ((status = acpi_aml_resolve_to_value (stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			if (ACPI_TYPE_PACKAGE != (*stack_ptr)->common.type) {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		case ARGI_ANYTYPE:


			/*
			 * We don't want to resolve Index_op reference objects during
			 * a store because this would be an implicit De_ref_of operation.
			 * Instead, we just want to store the reference object.
			 */

			if ((opcode == AML_STORE_OP) &&
				((*stack_ptr)->common.type == INTERNAL_TYPE_REFERENCE) &&
				((*stack_ptr)->reference.op_code == AML_INDEX_OP))
			{
				break;
			}

			/* All others must be resolved */

			if ((status = acpi_aml_resolve_to_value (stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			/* All types OK, so we don't perform any typechecks */

			break;


		case ARGI_DATAOBJECT:
			/*
			 * ARGI_DATAOBJECT is only used by the Size_of operator.
			 *
			 * The ACPI specification allows Size_of to return the size of
			 *  a Buffer, String or Package.  However, the MS ACPI.SYS AML
			 *  Interpreter also allows an NTE reference to return without
			 *  error with a size of 4.
			 */

			if ((status = acpi_aml_resolve_to_value (stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			/* Need a buffer, string, package or NTE reference */

			if (((*stack_ptr)->common.type != ACPI_TYPE_BUFFER) &&
				((*stack_ptr)->common.type != ACPI_TYPE_STRING) &&
				((*stack_ptr)->common.type != ACPI_TYPE_PACKAGE) &&
				((*stack_ptr)->common.type != INTERNAL_TYPE_REFERENCE))
			{
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}

			/*
			 * If this is a reference, only allow a reference to an NTE.
			 */
			if ((*stack_ptr)->common.type == INTERNAL_TYPE_REFERENCE) {
				if (!(*stack_ptr)->reference.nte) {
					status = AE_AML_OPERAND_TYPE;
					goto cleanup;
				}
			}

			break;


		case ARGI_COMPLEXOBJ:

			if ((status = acpi_aml_resolve_to_value (stack_ptr)) != AE_OK) {
				goto cleanup;
			}

			/* Need a buffer or package */

			if (((*stack_ptr)->common.type != ACPI_TYPE_BUFFER) &&
				((*stack_ptr)->common.type != ACPI_TYPE_PACKAGE))
			{
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
			break;


		/* Unknown abbreviation passed in */

		default:
			status = AE_BAD_PARAMETER;
			goto cleanup;

		}   /* switch (*Types++) */


		/*
		 * If more operands needed, decrement Stack_ptr to point
		 * to next operand on stack (after checking for underflow).
		 */
		if (GET_CURRENT_ARG_TYPE (arg_types)) {
			stack_ptr--;
		}

	}   /* while (*Types) */


cleanup:

  return (status);
}

