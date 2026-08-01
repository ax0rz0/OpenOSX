#ifndef _PD_SECKEYCHAIN_H
#define _PD_SECKEYCHAIN_H

#include <Security/SecBase.h>
#include <CoreFoundation/CoreFoundation.h>

typedef struct OpaqueSecKeychainSearchRef *SecKeychainSearchRef;
typedef struct OpaqueSecCertificateRef    *SecCertificateRef;

typedef uint32_t SecItemClass;
typedef uint32_t SecKeychainAttrType;

typedef struct SecKeychainAttribute {
    SecKeychainAttrType tag;
    UInt32              length;
    void               *data;
} SecKeychainAttribute;

typedef struct SecKeychainAttributeList {
    UInt32                count;
    SecKeychainAttribute *attr;
} SecKeychainAttributeList;

typedef struct SecKeychainAttributeInfo {
    UInt32  count;
    UInt32 *tag;
    UInt32 *format;
} SecKeychainAttributeInfo;

OSStatus SecKeychainAddGenericPassword(SecKeychainRef keychain,
                                       UInt32 serviceNameLength,
                                       const char *serviceName,
                                       UInt32 accountNameLength,
                                       const char *accountName,
                                       UInt32 passwordLength,
                                       const void *passwordData,
                                       SecKeychainItemRef *itemRef);

OSStatus SecKeychainFindGenericPassword(CFTypeRef keychainOrArray,
                                        UInt32 serviceNameLength,
                                        const char *serviceName,
                                        UInt32 accountNameLength,
                                        const char *accountName,
                                        UInt32 *passwordLength,
                                        void **passwordData,
                                        SecKeychainItemRef *itemRef);

OSStatus SecKeychainGetUserInteractionAllowed(Boolean *state);
OSStatus SecKeychainSetUserInteractionAllowed(Boolean state);

OSStatus SecKeychainItemCopyAttributesAndData(SecKeychainItemRef itemRef,
                                              SecKeychainAttributeInfo *info,
                                              SecItemClass *itemClass,
                                              SecKeychainAttributeList **attrList,
                                              UInt32 *length,
                                              void **outData);

OSStatus SecKeychainItemDelete(SecKeychainItemRef itemRef);

OSStatus SecKeychainItemFreeAttributesAndData(SecKeychainAttributeList *attrList,
                                              void *data);

OSStatus SecKeychainItemModifyAttributesAndData(SecKeychainItemRef itemRef,
                                                const SecKeychainAttributeList *attrList,
                                                UInt32 length,
                                                const void *data);

OSStatus SecKeychainSearchCopyNext(SecKeychainSearchRef searchRef,
                                   SecKeychainItemRef *itemRef);

OSStatus SecKeychainSearchCreateFromAttributes(CFTypeRef keychainOrArray,
                                               SecItemClass itemClass,
                                               const SecKeychainAttributeList *attrList,
                                               SecKeychainSearchRef *searchRef);

#endif /* _PD_SECKEYCHAIN_H */
