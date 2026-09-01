/*
 * performSelectorOnMainThread:, over libdispatch.
 *
 * Cocoa's version enqueues onto the main thread's run loop. OpenOSX has
 * libdispatch, and dispatch_get_main_queue() targets the main thread, so it is
 * the right primitive here and avoids needing a run loop to be running.
 *
 * Three cases, and the middle one is the one that matters:
 *
 *   already on the main thread   call directly. Going through dispatch_sync on
 *                                the main queue from the main thread deadlocks.
 *   wait == YES                  dispatch_sync: the contract is that the
 *                                selector has run by the time this returns.
 *   wait == NO                   dispatch_async.
 *
 * The object and argument are retained across an async hop and released after,
 * because the caller is entitled to release them the moment this returns.
 */
#import <Foundation/NSObjectMainThread.h>
#import <dispatch/dispatch.h>
#import <objc/message.h>
#import <pthread.h>

@implementation NSObject (OpenOSXMainThread)

- (void)performSelectorOnMainThread:(SEL)aSelector
                         withObject:(id)arg
                      waitUntilDone:(BOOL)wait
{
    if (!aSelector) {
        return;
    }

    void (*send)(id, SEL, id) = (void (*)(id, SEL, id))objc_msgSend;

    if (pthread_main_np()) {
        send(self, aSelector, arg);
        return;
    }

    if (wait) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            send(self, aSelector, arg);
        });
        return;
    }

    id target = [self retain];
    id argument = [arg retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        send(target, aSelector, argument);
        [argument release];
        [target release];
    });
}

@end
