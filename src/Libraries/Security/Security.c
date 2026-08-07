#include <Security/Security.h>
#include <Security/SecTask.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

extern void arc4random_buf(void * buf, size_t nbytes);

int SecRandomCopyBytes(SecRandomRef rnd, size_t count, void * bytes)
{
    (void) rnd;
    arc4random_buf(bytes, count);
    return errSecSuccess;
}

struct __SecRandom { int unused; };
static const struct __SecRandom _kSecRandomDefaultStorage;
const SecRandomRef kSecRandomDefault = &_kSecRandomDefaultStorage;

const CFStringRef kSecClass                 = CFSTR("class");
const CFStringRef kSecClassGenericPassword   = CFSTR("genp");
const CFStringRef kSecClassInternetPassword  = CFSTR("inet");
const CFStringRef kSecAttrAccount            = CFSTR("acct");
const CFStringRef kSecAttrService            = CFSTR("svce");
const CFStringRef kSecAttrServer             = CFSTR("srvr");
const CFStringRef kSecAttrLabel              = CFSTR("labl");
const CFStringRef kSecValueData              = CFSTR("v_Data");
const CFStringRef kSecReturnData             = CFSTR("r_Data");
const CFStringRef kSecReturnAttributes       = CFSTR("r_Attributes");
const CFStringRef kSecMatchLimit             = CFSTR("m_Limit");
const CFStringRef kSecMatchLimitOne          = CFSTR("m_LimitOne");
const CFStringRef kSecMatchLimitAll          = CFSTR("m_LimitAll");

OSStatus SecItemCopyMatching(CFDictionaryRef query, CFTypeRef * result)
{
    (void) query;
    if ( result )  *result = NULL;
    return errSecItemNotFound;
}

OSStatus SecItemAdd(CFDictionaryRef attributes, CFTypeRef * result)
{
    (void) attributes;
    if ( result )  *result = NULL;
    return errSecUnimplemented;
}

OSStatus SecItemUpdate(CFDictionaryRef query, CFDictionaryRef attributesToUpdate)
{
    (void) query;
    (void) attributesToUpdate;
    return errSecItemNotFound;
}

OSStatus SecItemDelete(CFDictionaryRef query)
{
    (void) query;
    return errSecItemNotFound;
}

CFTypeID
SecTaskGetTypeID(void)
{
    return CFDataGetTypeID();
}

SecTaskRef
SecTaskCreateWithAuditToken(CFAllocatorRef allocator, audit_token_t token)
{
    return (SecTaskRef)CFDataCreate(allocator,
                                    (const UInt8 *)&token,
                                    (CFIndex)sizeof(token));
}

SecTaskRef
SecTaskCreateFromSelf(CFAllocatorRef allocator)
{
    audit_token_t token;

    memset(&token, 0, sizeof(token));
    return SecTaskCreateWithAuditToken(allocator, token);
}

CFTypeRef
SecTaskCopyValueForEntitlement(SecTaskRef task, CFStringRef entitlement,
                               CFErrorRef *error)
{
    (void) task;
    (void) entitlement;
    if ( error )  *error = NULL;
    return NULL;
}

CFDictionaryRef
SecTaskCopyValuesForEntitlements(SecTaskRef task, CFArrayRef entitlements,
                                 CFErrorRef *error)
{
    (void) task;
    (void) entitlements;
    if ( error )  *error = NULL;
    return CFDictionaryCreate(NULL, NULL, NULL, 0,
                              &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);
}

CFStringRef
SecTaskCopySigningIdentifier(SecTaskRef task, CFErrorRef *error)
{
    (void) task;
    if ( error )  *error = NULL;
    return NULL;
}

struct AuthorizationOpaqueRef { int unused; };
static const struct AuthorizationOpaqueRef _pd_authorization_token;

OSStatus
AuthorizationCreate(const AuthorizationRights *rights,
                    const AuthorizationEnvironment *environment,
                    AuthorizationFlags flags, AuthorizationRef *authorization)
{
    (void) rights;
    (void) environment;
    (void) flags;
    if ( authorization )  *authorization = &_pd_authorization_token;
    return errAuthorizationSuccess;
}

OSStatus
AuthorizationFree(AuthorizationRef authorization, AuthorizationFlags flags)
{
    (void) authorization;
    (void) flags;
    return errAuthorizationSuccess;
}

OSStatus
AuthorizationCopyRights(AuthorizationRef authorization,
                        const AuthorizationRights *rights,
                        const AuthorizationEnvironment *environment,
                        AuthorizationFlags flags,
                        AuthorizationRights **authorizedRights)
{
    (void) authorization;
    (void) rights;
    (void) environment;
    (void) flags;
    if ( authorizedRights )  *authorizedRights = NULL;
    return ( geteuid() == 0 ) ? errAuthorizationSuccess : errAuthorizationDenied;
}

OSStatus
AuthorizationMakeExternalForm(AuthorizationRef authorization,
                              AuthorizationExternalForm *extForm)
{
    (void) authorization;
    if ( extForm == NULL )  return errAuthorizationInvalidPointer;
    memset(extForm, 0, sizeof(*extForm));
    return errAuthorizationSuccess;
}

OSStatus
AuthorizationCreateFromExternalForm(const AuthorizationExternalForm *extForm,
                                    AuthorizationRef *authorization)
{
    (void) extForm;
    if ( authorization )  *authorization = &_pd_authorization_token;
    return errAuthorizationSuccess;
}

OSStatus
SecKeychainAddGenericPassword(SecKeychainRef keychain,
                              UInt32 serviceNameLength, const char *serviceName,
                              UInt32 accountNameLength, const char *accountName,
                              UInt32 passwordLength, const void *passwordData,
                              SecKeychainItemRef *itemRef)
{
    (void) keychain;
    (void) serviceNameLength; (void) serviceName;
    (void) accountNameLength; (void) accountName;
    (void) passwordLength; (void) passwordData;
    if ( itemRef )  *itemRef = NULL;
    return errSecNotAvailable;
}

OSStatus
SecKeychainFindGenericPassword(CFTypeRef keychainOrArray,
                               UInt32 serviceNameLength, const char *serviceName,
                               UInt32 accountNameLength, const char *accountName,
                               UInt32 *passwordLength, void **passwordData,
                               SecKeychainItemRef *itemRef)
{
    (void) keychainOrArray;
    (void) serviceNameLength; (void) serviceName;
    (void) accountNameLength; (void) accountName;
    if ( passwordLength )  *passwordLength = 0;
    if ( passwordData )    *passwordData = NULL;
    if ( itemRef )         *itemRef = NULL;
    return errSecItemNotFound;
}

OSStatus
SecKeychainGetUserInteractionAllowed(Boolean *state)
{
    if ( state )  *state = false;
    return errSecSuccess;
}

OSStatus
SecKeychainSetUserInteractionAllowed(Boolean state)
{
    (void) state;
    return errSecSuccess;
}

OSStatus
SecKeychainItemCopyAttributesAndData(SecKeychainItemRef itemRef,
                                     SecKeychainAttributeInfo *info,
                                     SecItemClass *itemClass,
                                     SecKeychainAttributeList **attrList,
                                     UInt32 *length, void **outData)
{
    (void) itemRef;
    (void) info;
    if ( itemClass )  *itemClass = 0;
    if ( attrList )   *attrList = NULL;
    if ( length )     *length = 0;
    if ( outData )    *outData = NULL;
    return errSecItemNotFound;
}

OSStatus
SecKeychainItemDelete(SecKeychainItemRef itemRef)
{
    (void) itemRef;
    return errSecItemNotFound;
}

OSStatus
SecKeychainItemFreeAttributesAndData(SecKeychainAttributeList *attrList,
                                     void *data)
{
    (void) attrList;
    (void) data;
    return errSecSuccess;
}

OSStatus
SecKeychainItemModifyAttributesAndData(SecKeychainItemRef itemRef,
                                       const SecKeychainAttributeList *attrList,
                                       UInt32 length, const void *data)
{
    (void) itemRef;
    (void) attrList;
    (void) length;
    (void) data;
    return errSecItemNotFound;
}

OSStatus
SecKeychainSearchCopyNext(SecKeychainSearchRef searchRef,
                          SecKeychainItemRef *itemRef)
{
    (void) searchRef;
    if ( itemRef )  *itemRef = NULL;
    return errSecItemNotFound;
}

OSStatus
SecKeychainSearchCreateFromAttributes(CFTypeRef keychainOrArray,
                                      SecItemClass itemClass,
                                      const SecKeychainAttributeList *attrList,
                                      SecKeychainSearchRef *searchRef)
{
    (void) keychainOrArray;
    (void) itemClass;
    (void) attrList;
    if ( searchRef )  *searchRef = NULL;
    return errSecItemNotFound;
}

OSStatus
SecTrustSettingsCopyCertificates(SecTrustSettingsDomain domain,
                                 CFArrayRef *certArray)
{
    (void) domain;
    if ( certArray )  *certArray = NULL;
    return errSecNoTrustSettings;
}

OSStatus
SecItemExport(CFTypeRef secItemOrArray, SecExternalFormat outputFormat,
              SecItemImportExportFlags flags, const void *keyParams,
              CFDataRef *exportedData)
{
    (void) secItemOrArray;
    (void) outputFormat;
    (void) flags;
    (void) keyParams;
    if ( exportedData )  *exportedData = NULL;
    return errSecUnimplemented;
}
