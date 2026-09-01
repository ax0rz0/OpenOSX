/*
 * NSAutoreleasePool - a thin shell over the objc runtime's pool API.
 *
 * Needed because openjdk's libjli.dylib references _OBJC_CLASS_$_NSAutoreleasePool
 * at load time and sends it -init and -drain.
 */
#import <Foundation/NSObject.h>

@interface NSAutoreleasePool : NSObject {
    void *_token;
}
- (instancetype)init;
- (void)drain;
- (void)addObject:(id)object;
+ (void)addObject:(id)object;
@end
