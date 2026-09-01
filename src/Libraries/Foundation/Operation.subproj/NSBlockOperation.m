/*
 * NSOperation / NSBlockOperation.
 *
 * Deliberately synchronous: -start runs the block on the calling thread and
 * returns when it is done. That is a legal NSOperation - the class contract
 * allows a non-concurrent operation whose -start does the work directly, and
 * NSOperation's own default -start behaves exactly this way when the operation
 * is not marked concurrent.
 *
 * It is NOT a queue. There is no NSOperationQueue here, so nothing can enqueue
 * one of these and expect it to run elsewhere. The single caller that matters -
 * openjdk's libjli - creates one and calls -start on it directly, which this
 * serves correctly. Anything wanting real concurrency should get a real
 * implementation rather than a shell that silently runs on the wrong thread.
 */
#import <Foundation/NSBlockOperation.h>
#import <objc/runtime.h>

typedef void (^OpenOSXVoidBlock)(void);

@implementation NSOperation {
    BOOL _finished;
    BOOL _executing;
    BOOL _cancelled;
}

- (void)start
{
    if (_cancelled) {
        _finished = YES;
        return;
    }
    _executing = YES;
    [self main];
    _executing = NO;
    _finished = YES;
}

- (void)main
{
    /* Subclass responsibility; the base operation does nothing. */
}

- (void)cancel            { _cancelled = YES; }
- (BOOL)isFinished        { return _finished; }
- (BOOL)isExecuting       { return _executing; }
- (BOOL)isCancelled       { return _cancelled; }

@end

@implementation NSBlockOperation

+ (instancetype)blockOperationWithBlock:(OpenOSXVoidBlock)block
{
    NSBlockOperation *op = [[[self alloc] init] autorelease];
    if (op && block) {
        op->_block = [block copy];
    }
    return op;
}

- (void)addExecutionBlock:(OpenOSXVoidBlock)block
{
    /* Only the last block is kept: this class exists for a single caller that
     * adds none. Storing a list nobody reads would be more code and the same
     * behaviour. */
    if (block) {
        id old = _block;
        _block = [block copy];
        [old release];
    }
}

- (void)main
{
    if (_block) {
        ((OpenOSXVoidBlock)_block)();
    }
}

- (void)dealloc
{
    [_block release];
    [super dealloc];
}

@end
