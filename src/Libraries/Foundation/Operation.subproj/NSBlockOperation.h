/*
 * NSOperation / NSBlockOperation - the minimum openjdk's launcher needs.
 *
 * libjli.dylib sends exactly +blockOperationWithBlock: and -start, and it uses
 * the pair to hop work onto the main thread before starting the VM.
 */
#import <Foundation/NSObject.h>

@interface NSOperation : NSObject
- (void)start;
- (void)main;
@property (readonly, getter=isFinished) BOOL finished;
@property (readonly, getter=isExecuting) BOOL executing;
@property (readonly, getter=isCancelled) BOOL cancelled;
- (void)cancel;
@end

@interface NSBlockOperation : NSOperation {
    id _block;
}
+ (instancetype)blockOperationWithBlock:(void (^)(void))block;
- (void)addExecutionBlock:(void (^)(void))block;
@end
