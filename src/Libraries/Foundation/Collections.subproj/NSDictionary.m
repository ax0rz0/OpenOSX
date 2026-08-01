/*
 * Copyright (C) 2026, Samuel Zormeister.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#import <Foundation/NSDictionary.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFPropertyList.h>
#include <CoreFoundation/CFURL.h>
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFStream.h>
#include <CoreFoundation/ForFoundationOnly.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>

/* Read a whole file into a CFData. CFReadStream would do, but plists are small
 * and stdio keeps this independent of the stream machinery. */
static CFDataRef pd_read_file(CFStringRef path) {
    char buf[1024];
    if (!CFStringGetCString(path, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        return NULL;
    }

    FILE *f = fopen(buf, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    UInt8 *bytes = malloc((size_t)size);
    if (bytes == NULL) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(bytes, 1, (size_t)size, f);
    fclose(f);

    CFDataRef data = CFDataCreate(kCFAllocatorDefault, bytes, (CFIndex)got);
    free(bytes);
    return data;
}

static CFPropertyListRef pd_plist_from_path(CFStringRef path) {
    CFDataRef data = pd_read_file(path);
    if (data == NULL) {
        return NULL;
    }
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        kCFAllocatorDefault, data, kCFPropertyListImmutable, NULL, NULL);
    CFRelease(data);

    if (plist != NULL && CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
        CFRelease(plist);
        return NULL;
    }
    return plist;
}

@implementation NSDictionary

+ (instancetype)dictionary {
    return (id)CFDictionaryCreate(kCFAllocatorDefault, NULL, NULL, 0,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
}

+ (nullable instancetype)dictionaryWithContentsOfURL:(NSURL *)url
                                               error:(NSError **)error {
    if (error != NULL) {
        *error = nil;
    }
    if (url == nil) {
        return nil;
    }

    CFStringRef path = CFURLCopyFileSystemPath((CFURLRef)url, kCFURLPOSIXPathStyle);
    if (path == NULL) {
        return nil;
    }
    CFPropertyListRef plist = pd_plist_from_path(path);
    CFRelease(path);
    return (id)plist;
}

+ (nullable instancetype)dictionaryWithContentsOfFile:(NSString *)path {
    if (path == nil) {
        return nil;
    }
    return (id)pd_plist_from_path((CFStringRef)path);
}

- (NSUInteger)count {
    return (NSUInteger)CFDictionaryGetCount((CFDictionaryRef)self);
}

- (nullable id)objectForKey:(id)key {
    return (id)CFDictionaryGetValue((CFDictionaryRef)self, (const void *)key);
}

- (nullable id)objectForKeyedSubscript:(id)key {
    return [self objectForKey:key];
}

- (NSArray *)allKeys {
    CFIndex n = CFDictionaryGetCount((CFDictionaryRef)self);
    const void **keys = malloc(sizeof(void *) * (size_t)(n > 0 ? n : 1));
    if (keys == NULL) {
        return nil;
    }
    CFDictionaryGetKeysAndValues((CFDictionaryRef)self, keys, NULL);
    CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, keys, n,
                                     &kCFTypeArrayCallBacks);
    free(keys);
    return (NSArray *)array;
}

@end

@implementation NSMutableDictionary

+ (instancetype)dictionaryWithCapacity:(NSUInteger)capacity {
    return (id)CFDictionaryCreateMutable(kCFAllocatorDefault, (CFIndex)capacity,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
}

+ (instancetype)dictionary {
    return [self dictionaryWithCapacity:0];
}

- (void)setObject:(id)object forKey:(id)key {
    CFDictionarySetValue((CFMutableDictionaryRef)self, (const void *)key,
                         (const void *)object);
}

- (void)setObject:(id)object forKeyedSubscript:(id)key {
    [self setObject:object forKey:key];
}

- (void)removeObjectForKey:(id)key {
    CFDictionaryRemoveValue((CFMutableDictionaryRef)self, (const void *)key);
}

@end

#if DEPLOYMENT_RUNTIME_OBJC
__attribute__((constructor))
static void __NSCFDictionaryBridgeInit(void) {
    _CFRuntimeBridgeClasses(CFDictionaryGetTypeID(), "NSDictionary");
}
#endif
