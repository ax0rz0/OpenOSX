/*
 * -performSelectorOnMainThread:withObject:waitUntilDone: as a category on
 * NSObject. openjdk's libjli sends it to hop VM startup onto the main thread.
 */
#import <Foundation/NSObject.h>

@interface NSObject (OpenOSXMainThread)
- (void)performSelectorOnMainThread:(SEL)aSelector
                         withObject:(id)arg
                      waitUntilDone:(BOOL)wait;
@end
