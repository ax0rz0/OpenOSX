/*
 * Copyright (C) 2026, Samuel Zormeister.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef NSArray_h
#define NSArray_h

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>

@interface NSArray : NSObject

+ (instancetype)array;
+ (instancetype)arrayWithObjects:(const id *)objects count:(NSUInteger)count;

- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
- (id)objectAtIndexedSubscript:(NSUInteger)index;
- (BOOL)containsObject:(id)object;

@end

@interface NSMutableArray : NSArray

+ (instancetype)arrayWithCapacity:(NSUInteger)capacity;

- (void)addObject:(id)object;
- (void)removeObjectAtIndex:(NSUInteger)index;
- (void)removeAllObjects;

@end

#endif /* NSArray_h */
