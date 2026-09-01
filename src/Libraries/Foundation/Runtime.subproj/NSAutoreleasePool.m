/*
 * NSAutoreleasePool.
 *
 * The real work is all in libobjc: objc_autoreleasePoolPush returns an opaque
 * token and objc_autoreleasePoolPop unwinds to it. This class is the ObjC-object
 * face of that pair, which is what code written before ARC expects to allocate.
 *
 * -drain and -release are the same operation here, as they are on macOS outside
 * of garbage collection: both pop the pool. -dealloc deliberately does NOT pop
 * again; popping a token twice corrupts the runtime's pool stack, and a
 * double-drain is a mistake that should be inert rather than fatal.
 */
#import <Foundation/NSAutoreleasePool.h>
#import <objc/runtime.h>
#import <objc/message.h>

void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *token);
id objc_autorelease(id obj);

@implementation NSAutoreleasePool

- (instancetype)init
{
    self = [super init];
    if (self) {
        _token = objc_autoreleasePoolPush();
    }
    return self;
}

- (void)drain
{
    if (_token) {
        void *t = _token;
        /* Clear first: popping can release objects that message this pool. */
        _token = NULL;
        objc_autoreleasePoolPop(t);
    }
}

- (void)release
{
    [self drain];
}

- (void)dealloc
{
    /* Do not pop here. If -drain or -release already ran, the token is spent,
     * and popping a spent token unwinds someone else's pool. */
    [super dealloc];
}

- (void)addObject:(id)object
{
    objc_autorelease(object);
}

+ (void)addObject:(id)object
{
    objc_autorelease(object);
}

/* Pools are not retained in the usual sense - they are scope markers. */
- (instancetype)retain { return self; }
- (NSUInteger)retainCount { return 1; }
- (instancetype)autorelease { return self; }

@end
