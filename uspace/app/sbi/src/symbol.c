/*
 * Copyright (c) 2010 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @file Symbols. */

#include <stdlib.h>
#include <assert.h>
#include "list.h"
#include "mytypes.h"
#include "strtab.h"
#include "stree.h"

#include "symbol.h"

static stree_symbol_t *symbol_search_global(stree_program_t *prog,
    stree_ident_t *name);
static stree_symbol_t *symbol_find_epoint_rec(stree_program_t *prog,
    stree_ident_t *name, stree_csi_t *csi);
static stree_ident_t *symbol_get_ident(stree_symbol_t *symbol);

/** Lookup symbol in CSI using a type expression.
 *
 * XXX This should be removed in favor of full type expression evaluation
 * (run_texpr). This cannot work properly with generics.
 *
 * @param prog		Program
 * @param scope		CSI used as base for relative references
 * @param texpr		Type expression
 *
 * @return		Symbol referenced by type expression or @c NULL
 *			if not found
 */
stree_symbol_t *symbol_xlookup_in_csi(stree_program_t *prog,
    stree_csi_t *scope, stree_texpr_t *texpr)
{
	stree_symbol_t *a, *b;
	stree_csi_t *a_csi;

	switch (texpr->tc) {
	case tc_tnameref:
		return symbol_lookup_in_csi(prog, scope, texpr->u.tnameref->name);
	case tc_taccess:
		a = symbol_xlookup_in_csi(prog, scope, texpr->u.taccess->arg);
		a_csi = symbol_to_csi(a);
		if (a_csi == NULL) {
			printf("Error: Symbol is not CSI.\n");
			exit(1);
		}
		b = symbol_search_csi(prog, a_csi, texpr->u.taccess->member_name);
		if (b == NULL) {
			printf("Error: CSI '%s' not found\n", strtab_get_str(texpr->u.taccess->member_name->sid));
			exit(1);
		}
		return b;
	case tc_tapply:
		return symbol_xlookup_in_csi(prog, scope,
		    texpr->u.tapply->gtype);
	default:
		assert(b_false);
	}
}

/** Lookup symbol reference in CSI.
 *
 * @param prog	Program to look in
 * @param scope CSI in @a prog which is the base for references
 * @param name	Identifier of the symbol
 *
 * @return	Symbol or @c NULL if symbol not found
 */
stree_symbol_t *symbol_lookup_in_csi(stree_program_t *prog, stree_csi_t *scope,
	stree_ident_t *name)
{
	stree_symbol_t *symbol;

	/* This CSI node should have been processed. */
	assert(scope == NULL || scope->ancr_state == ws_visited);

	symbol = NULL;
	while (scope != NULL && symbol == NULL) {
		symbol = symbol_search_csi(prog, scope, name);
		scope = csi_to_symbol(scope)->outer_csi;
	}

	if (symbol == NULL)
		symbol = symbol_search_global(prog, name);

	return symbol;
}

/** Look for symbol strictly in CSI.
 *
 * Look for symbol in definition of a CSI and its ancestors. (But not
 * in lexically enclosing CSI.)
 *
 * @param prog	Program to look in
 * @param scope CSI in which to look
 * @param name	Identifier of the symbol
 *
 * @return	Symbol or @c NULL if symbol not found.
 */
stree_symbol_t *symbol_search_csi(stree_program_t *prog,
    stree_csi_t *scope, stree_ident_t *name)
{
	list_node_t *node;
	stree_csimbr_t *csimbr;
	stree_symbol_t *symbol;
	stree_ident_t *mbr_name;
	stree_symbol_t *base_csi_sym;
	stree_csi_t *base_csi;

	(void) prog;

	/* Look in new members in this class. */

	node = list_first(&scope->members);
	while (node != NULL) {
		csimbr = list_node_data(node, stree_csimbr_t *);

		/* Keep compiler happy. */
		mbr_name = NULL;

		switch (csimbr->cc) {
		case csimbr_csi: mbr_name = csimbr->u.csi->name; break;
		case csimbr_deleg: mbr_name = csimbr->u.deleg->name; break;
		case csimbr_fun: mbr_name = csimbr->u.fun->name; break;
		case csimbr_var: mbr_name = csimbr->u.var->name; break;
		case csimbr_prop: mbr_name = csimbr->u.prop->name; break;
		}

		if (name->sid == mbr_name->sid) {
			/* Match */
			switch (csimbr->cc) {
			case csimbr_csi:
				symbol = csi_to_symbol(csimbr->u.csi);
				break;
			case csimbr_deleg:
				symbol = deleg_to_symbol(csimbr->u.deleg);
				break;
			case csimbr_fun:
				symbol = fun_to_symbol(csimbr->u.fun);
				break;
			case csimbr_var:
				symbol = var_to_symbol(csimbr->u.var);
				break;
			case csimbr_prop:
				symbol = prop_to_symbol(csimbr->u.prop);
				break;
			default:
				assert(b_false);
			}
			return symbol;
		}
		node = list_next(&scope->members, node);
	}

	/* Try inherited members. */
	if (scope->base_csi_ref != NULL) {
		base_csi_sym = symbol_xlookup_in_csi(prog,
		    csi_to_symbol(scope)->outer_csi, scope->base_csi_ref);
		base_csi = symbol_to_csi(base_csi_sym);
		assert(base_csi != NULL);

		return symbol_search_csi(prog, base_csi, name);
	}

	/* No match */
	return NULL;
}

/** Look for symbol in global scope.
 *
 * @param prog	Program to look in
 * @param name	Identifier of the symbol
 *
 * @return	Symbol or @c NULL if symbol not found.
 */
static stree_symbol_t *symbol_search_global(stree_program_t *prog,
    stree_ident_t *name)
{
	list_node_t *node;
	stree_modm_t *modm;
	stree_symbol_t *symbol;

	node = list_first(&prog->module->members);
	while (node != NULL) {
		modm = list_node_data(node, stree_modm_t *);
		if (name->sid == modm->u.csi->name->sid) {
			/* Match */
			switch (modm->mc) {
			case mc_csi:
				symbol = csi_to_symbol(modm->u.csi);
				break;
			default:
				assert(b_false);
			}
			return symbol;
		}
		node = list_next(&prog->module->members, node);
	}

	return NULL;
}

/** Find entry point.
 *
 * Perform a walk of all CSIs and look for a function with the name @a name.
 *
 * @param prog	Program to look in
 * @param name	Name of entry point
 *
 * @return	Symbol or @c NULL if symbol not found.
 */
stree_symbol_t *symbol_find_epoint(stree_program_t *prog, stree_ident_t *name)
{
	list_node_t *node;
	stree_modm_t *modm;
	stree_symbol_t *entry, *etmp;

	entry = NULL;

	node = list_first(&prog->module->members);
	while (node != NULL) {
		modm = list_node_data(node, stree_modm_t *);
		if (modm->mc == mc_csi) {
			etmp = symbol_find_epoint_rec(prog, name, modm->u.csi);
			if (etmp != NULL) {
				if (entry != NULL) {
					printf("Error: Duplicate entry point.\n");
					exit(1);
				}
				entry = etmp;
			}
		}
	    	node = list_next(&prog->module->members, node);
	}

	return entry;
}

/** Find entry point under CSI.
 *
 * Internal part of symbol_find_epoint() that recursively walks CSIs.
 *
 * @param prog	Program to look in
 * @param name	Name of entry point
 *
 * @return	Symbol or @c NULL if symbol not found.
 */
static stree_symbol_t *symbol_find_epoint_rec(stree_program_t *prog,
    stree_ident_t *name, stree_csi_t *csi)
{
	list_node_t *node;
	stree_csimbr_t *csimbr;
	stree_symbol_t *entry, *etmp;

	entry = NULL;

	node = list_first(&csi->members);
	while (node != NULL) {
		csimbr = list_node_data(node, stree_csimbr_t *);

		switch (csimbr->cc) {
		case csimbr_csi:
			etmp = symbol_find_epoint_rec(prog, name, csimbr->u.csi);
			if (etmp != NULL) {
				if (entry != NULL) {
					printf("Error: Duplicate entry point.\n");
					exit(1);
				}
				entry = etmp;
			}
			break;
		case csimbr_fun:
			if (csimbr->u.fun->name->sid == name->sid) {
				if (entry != NULL) {
					printf("Error: Duplicate entry point.\n");
					exit(1);
				}
				entry = fun_to_symbol(csimbr->u.fun);
			}
		default:
			break;
		}

	    	node = list_next(&csi->members, node);
	}

	return entry;
}

/*
 * The notion of symbol is designed as a common base class for several
 * types of declarations with global and CSI scope. Here we simulate
 * conversion from this base class (symbol) to derived classes (CSI,
 * fun, ..) and vice versa.
 */

/** Convert symbol to delegate (base to derived).
 *
 * @param symbol	Symbol
 * @return		Delegate or @c NULL if symbol is not a delegate
 */
stree_deleg_t *symbol_to_deleg(stree_symbol_t *symbol)
{
	if (symbol->sc != sc_deleg)
		return NULL;

	return symbol->u.deleg;
}

/** Convert delegate to symbol (derived to base).
 *
 * @param deleg		Delegate
 * @return		Symbol
 */
stree_symbol_t *deleg_to_symbol(stree_deleg_t *deleg)
{
	assert(deleg->symbol);
	return deleg->symbol;
}

/** Convert symbol to CSI (base to derived).
 *
 * @param symbol	Symbol
 * @return		CSI or @c NULL if symbol is not a CSI
 */
stree_csi_t *symbol_to_csi(stree_symbol_t *symbol)
{
	if (symbol->sc != sc_csi)
		return NULL;

	return symbol->u.csi;
}

/** Convert CSI to symbol (derived to base).
 *
 * @param csi		CSI
 * @return		Symbol
 */
stree_symbol_t *csi_to_symbol(stree_csi_t *csi)
{
	assert(csi->symbol);
	return csi->symbol;
}

/** Convert symbol to function (base to derived).
 *
 * @param symbol	Symbol
 * @return		Function or @c NULL if symbol is not a function
 */
stree_fun_t *symbol_to_fun(stree_symbol_t *symbol)
{
	if (symbol->sc != sc_fun)
		return NULL;

	return symbol->u.fun;
}

/** Convert function to symbol (derived to base).
 *
 * @param fun		Function
 * @return		Symbol
 */
stree_symbol_t *fun_to_symbol(stree_fun_t *fun)
{
	assert(fun->symbol);
	return fun->symbol;
}

/** Convert symbol to member variable (base to derived).
 *
 * @param symbol	Symbol
 * @return		Variable or @c NULL if symbol is not a member variable
 */
stree_var_t *symbol_to_var(stree_symbol_t *symbol)
{
	if (symbol->sc != sc_var)
		return NULL;

	return symbol->u.var;
}

/** Convert variable to symbol (derived to base).
 *
 * @param fun		Variable
 * @return		Symbol
 */
stree_symbol_t *var_to_symbol(stree_var_t *var)
{
	assert(var->symbol);
	return var->symbol;
}

/** Convert symbol to property (base to derived).
 *
 * @param symbol	Symbol
 * @return		Property or @c NULL if symbol is not a property
 */
stree_prop_t *symbol_to_prop(stree_symbol_t *symbol)
{
	if (symbol->sc != sc_prop)
		return NULL;

	return symbol->u.prop;
}

/** Convert property to symbol (derived to base).
 *
 * @param fun		Property
 * @return		Symbol
 */
stree_symbol_t *prop_to_symbol(stree_prop_t *prop)
{
	assert(prop->symbol);
	return prop->symbol;
}

/** Print fully qualified name of symbol.
 *
 * @param symbol	Symbol
 */
void symbol_print_fqn(stree_symbol_t *symbol)
{
	stree_ident_t *name;
	stree_symbol_t *outer_sym;

	if (symbol->outer_csi != NULL) {
		outer_sym = csi_to_symbol(symbol->outer_csi);
		symbol_print_fqn(outer_sym);
		printf(".");
	}

	name = symbol_get_ident(symbol);
	printf("%s", strtab_get_str(name->sid));
}

/** Return symbol identifier.
 *
 * @param symbol	Symbol
 * @return 		Symbol identifier
 */
static stree_ident_t *symbol_get_ident(stree_symbol_t *symbol)
{
	stree_ident_t *ident;

	switch (symbol->sc) {
	case sc_csi: ident = symbol->u.csi->name; break;
	case sc_deleg: ident = symbol->u.deleg->name; break;
	case sc_fun: ident = symbol->u.fun->name; break;
	case sc_var: ident = symbol->u.var->name; break;
	case sc_prop: ident = symbol->u.prop->name; break;
	}

	return ident;
}
