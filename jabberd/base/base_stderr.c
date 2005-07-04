/* --------------------------------------------------------------------------
 *
 * License
 *
 * The contents of this file are subject to the Jabber Open Source License
 * Version 1.0 (the "License").  You may not copy or use this file, in either
 * source code or executable form, except in compliance with the License.  You
 * may obtain a copy of the License at http://www.jabber.com/license/ or at
 * http://www.opensource.org/.
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied.  See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * Copyrights
 *
 * Portions created by or assigned to Jabber.com, Inc. are
 * Copyright (c) 1999-2000 Jabber.com, Inc.  All Rights Reserved.  Contact
 * information for Jabber.com, Inc. is available at http://www.jabber.com/.
 *
 * Portions Copyright (c) 1998-1999 Jeremie Miller.
 *
 * Acknowledgements
 *
 * Special thanks to the Jabber Open Source Contributors for their
 * suggestions and support of Jabber.
 *
 * --------------------------------------------------------------------------*/

#include "jabberd.h"

result base_stderr_display(instance i, dpacket p, void *args)
{
	char *message = NULL;

	/* Get the raw data from the packet */
	message = xmlnode_get_data(p->x);

	if (message == NULL) {
		log_debug
		    ("base_stderr_deliver: no message available to print.");
		return r_ERR;
	}

	/* We know message is non-null so fprintf is okay. */
	fprintf(stderr, "%s\n", message);

	pool_free(p->p);
	return r_DONE;
}

result base_stderr_config(instance id, xmlnode x, void *arg)
{
	if (id == NULL)
		return r_PASS;

	if (id->type != p_LOG) {
		log_alert(NULL,
			  "ERROR in instance %s: <stderr/> element only allowed in log sections",
			  id->id);
		return r_ERR;
	}

	log_debug("base_stderr configuring instnace %s", id->id);

	/* Register the handler, for this instance */
	register_phandler(id, o_DELIVER, base_stderr_display, NULL);

	return r_DONE;
}

void base_stderr(void)
{
	log_debug("base_stderr loading...");
	register_config("stderr", base_stderr_config, NULL);
}
