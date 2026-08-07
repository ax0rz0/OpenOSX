/*
 * Shim for AppleKeyStore/AppleKeyStoreFSServices.h
 *
 * The real header ships with AppleKeyStore and provides the AKS key-service
 * types used by HFS content protection.  Content protection is disabled in
 * this build (CONFIG_PROTECT is off for the OS X configuration), so we only
 * need enough type/decl surface for hfs_iokit.{h,cpp} to compile and link.
 */
#ifndef _AKS_FS_SERVICES_SHIM_H
#define _AKS_FS_SERVICES_SHIM_H

#include <sys/cdefs.h>
#include <sys/cprotect.h>

__BEGIN_DECLS

typedef cp_cred_t          aks_cred_t;
typedef cp_wrapped_key_t   aks_wrapped_key_t;
typedef cp_raw_key_t       aks_raw_key_t;

#define kAKSFileSystemKeyServices "AKSFileSystemKeyServices"

typedef struct {
	int (*unwrap_key)(aks_cred_t access, const aks_wrapped_key_t wrapped_key_in,
	                  aks_raw_key_t key_out);
	int (*rewrap_key)(aks_cred_t access, cp_key_class_t dp_class,
	                  const aks_wrapped_key_t wrapped_key_in,
	                  aks_wrapped_key_t wrapped_key_out);
	int (*new_key)(aks_cred_t access, cp_key_class_t dp_class,
	               aks_raw_key_t key_out, aks_wrapped_key_t wrapped_key_out);
	int (*backup_key)(aks_cred_t access, const aks_wrapped_key_t wrapped_key_in,
	                  aks_wrapped_key_t wrapped_key_out);
} aks_file_system_key_services_t;

__END_DECLS

#endif /* _AKS_FS_SERVICES_SHIM_H */
