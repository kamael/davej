/* Coda filesystem -- Linux Minicache
 *
 * Copyright (C) 1989 - 1997 Carnegie Mellon University
 *
 * Carnegie Mellon University encourages users of this software to
 * contribute improvements to the Coda project. Contact Peter Braam
 * <coda@cs.cmu.edu>
 */

#ifndef _CFSNC_HEADER_
#define _CFSNC_HEADER_

/*
 * Structure for an element in the Coda Credential Cache.
 */

struct coda_cache {
	struct list_head   cc_cclist;  /* list of all cache entries */
	struct list_head   cc_cnlist;  /* list of cache entries/cnode */
	int                cc_mask;
	struct coda_cred   cc_cred;
};

/* credential cache */
void coda_cache_enter(struct inode *inode, int mask);
void coda_cache_clear_inode(struct inode *);
void coda_cache_clear_all(struct super_block *sb);
void coda_cache_clear_cred(struct super_block *sb, struct coda_cred *cred);
int coda_cache_check(struct inode *inode, int mask);

/* for downcalls and attributes and lookups */
void coda_flag_inode_children(struct inode *inode, int flag);

#endif _CFSNC_HEADER_
