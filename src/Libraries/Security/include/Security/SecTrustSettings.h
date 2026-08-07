#ifndef _PD_SECTRUSTSETTINGS_H
#define _PD_SECTRUSTSETTINGS_H

#include <Security/SecBase.h>
#include <Security/SecKeychain.h>
#include <CoreFoundation/CoreFoundation.h>

enum {
    errSecNoTrustSettings = -25263,
};

typedef uint32_t SecTrustSettingsDomain;
enum {
    kSecTrustSettingsDomainUser   = 0,
    kSecTrustSettingsDomainAdmin  = 1,
    kSecTrustSettingsDomainSystem = 2,
};

typedef uint32_t SecExternalFormat;
enum {
    kSecFormatUnknown   = 0,
    kSecFormatX509Cert  = 9,
};

typedef uint32_t SecItemImportExportFlags;

OSStatus SecTrustSettingsCopyCertificates(SecTrustSettingsDomain domain,
                                          CFArrayRef *certArray);

OSStatus SecItemExport(CFTypeRef secItemOrArray,
                       SecExternalFormat outputFormat,
                       SecItemImportExportFlags flags,
                       const void *keyParams,
                       CFDataRef *exportedData);

#endif /* _PD_SECTRUSTSETTINGS_H */
