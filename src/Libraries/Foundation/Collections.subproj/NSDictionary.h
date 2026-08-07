/*
 * Copyright (C) 2026, Samuel Zormeister.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef NSDictionary_h
#define NSDictionary_h

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSArray.h>

@class NSURL, NSError, NSString;

@interface NSDictionary : NSObject

+ (instancetype)dictionary;

/* The plist readers. -contentsOfURL: is what NSProcessInfo-free code uses to
 * read SystemVersion.plist and friends; both go through CFPropertyList. */
+ (nullable instancetype)dictionaryWithContentsOfURL:(NSURL *)url
                                               error:(NSError **)error;
+ (nullable instancetype)dictionaryWithContentsOfFile:(NSString *)path;

- (NSUInteger)count;
- (nullable id)objectForKey:(id)key;
- (nullable id)objectForKeyedSubscript:(id)key;
- (NSArray *)allKeys;

@end

@interface NSMutableDictionary : NSDictionary

+ (instancetype)dictionaryWithCapacity:(NSUInteger)capacity;

- (void)setObject:(id)object forKey:(id)key;
- (void)setObject:(id)object forKeyedSubscript:(id)key;
- (void)removeObjectForKey:(id)key;

@end

#endif /* NSDictionary_h */
