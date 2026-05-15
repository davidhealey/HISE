/*
  Reachability.mm — converted from Apple's Reachability sample (plain .m → .mm).

  Original sample code Copyright (C) 2010 Apple Inc. All Rights Reserved.
  Adapted for use as Objective-C++ (.mm) so it can coexist with C++ translation units.

  No C++-specific changes were made to the logic; the rename to .mm is sufficient.
*/

#if JUCE_MAC || JUCE_IOS

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <netinet/in.h>

typedef enum : NSInteger
{
    NotReachable = 0,
    ReachableViaWiFi,
    ReachableViaWWAN
} NetworkStatus;

#define kReachabilityChangedNotification @"kNetworkReachabilityChangedNotification"

@interface Reachability : NSObject

+ (instancetype)reachabilityWithHostName:(NSString*)hostName;
+ (instancetype)reachabilityWithAddress:(const struct sockaddr*)hostAddress;
+ (instancetype)reachabilityForInternetConnection;

- (BOOL)startNotifier;
- (void)stopNotifier;

- (NetworkStatus)currentReachabilityStatus;

@property (nonatomic, assign) SCNetworkReachabilityRef reachabilityRef;

@end

static void ReachabilityCallback (SCNetworkReachabilityRef target,
                                  SCNetworkReachabilityFlags flags,
                                  void* info)
{
    #pragma unused (target, flags)
    NSCAssert (info != NULL, @"info was NULL in ReachabilityCallback");
    NSCAssert ([(__bridge NSObject*) info isKindOfClass: [Reachability class]],
               @"info was the wrong class in ReachabilityCallback");

    Reachability* noteObject = (__bridge Reachability*) info;
    [[NSNotificationCenter defaultCenter] postNotificationName: kReachabilityChangedNotification
                                                        object: noteObject];
}

@implementation Reachability

+ (instancetype)reachabilityWithHostName:(NSString*)hostName
{
    Reachability* returnValue = NULL;
    SCNetworkReachabilityRef reachability =
        SCNetworkReachabilityCreateWithName (NULL, [hostName UTF8String]);

    if (reachability != NULL)
    {
        returnValue = [[self alloc] init];
        if (returnValue != NULL)
            returnValue.reachabilityRef = reachability;
        else
            CFRelease (reachability);
    }

    return returnValue;
}

+ (instancetype)reachabilityWithAddress:(const struct sockaddr*)hostAddress
{
    SCNetworkReachabilityRef reachability =
        SCNetworkReachabilityCreateWithAddress (kCFAllocatorDefault, hostAddress);

    Reachability* returnValue = NULL;
    if (reachability != NULL)
    {
        returnValue = [[self alloc] init];
        if (returnValue != NULL)
            returnValue.reachabilityRef = reachability;
        else
            CFRelease (reachability);
    }

    return returnValue;
}

+ (instancetype)reachabilityForInternetConnection
{
    struct sockaddr_in zeroAddress;
    bzero (&zeroAddress, sizeof (zeroAddress));
    zeroAddress.sin_len    = sizeof (zeroAddress);
    zeroAddress.sin_family = AF_INET;

    return [self reachabilityWithAddress: (const struct sockaddr*) &zeroAddress];
}

- (void)dealloc
{
    [self stopNotifier];
    if (_reachabilityRef != NULL)
    {
        CFRelease (_reachabilityRef);
        _reachabilityRef = NULL;
    }
}

- (BOOL)startNotifier
{
    SCNetworkReachabilityContext context = { 0, (__bridge void*) self, NULL, NULL, NULL };

    if (SCNetworkReachabilitySetCallback (_reachabilityRef, ReachabilityCallback, &context))
        if (SCNetworkReachabilityScheduleWithRunLoop (_reachabilityRef,
                                                     CFRunLoopGetCurrent(),
                                                     kCFRunLoopDefaultMode))
            return YES;

    return NO;
}

- (void)stopNotifier
{
    if (_reachabilityRef != NULL)
        SCNetworkReachabilityUnscheduleFromRunLoop (_reachabilityRef,
                                                   CFRunLoopGetCurrent(),
                                                   kCFRunLoopDefaultMode);
}

- (NetworkStatus)networkStatusForFlags:(SCNetworkReachabilityFlags)flags
{
    if ((flags & kSCNetworkReachabilityFlagsReachable) == 0)
        return NotReachable;

    NetworkStatus returnValue = NotReachable;

    if ((flags & kSCNetworkReachabilityFlagsConnectionRequired) == 0)
        returnValue = ReachableViaWiFi;

    if ((flags & kSCNetworkReachabilityFlagsConnectionOnDemand) != 0
        || (flags & kSCNetworkReachabilityFlagsConnectionOnTraffic) != 0)
    {
        if ((flags & kSCNetworkReachabilityFlagsInterventionRequired) == 0)
            returnValue = ReachableViaWiFi;
    }

   #if JUCE_IOS
    if ((flags & kSCNetworkReachabilityFlagsIsWWAN) == kSCNetworkReachabilityFlagsIsWWAN)
        returnValue = ReachableViaWWAN;
   #endif

    return returnValue;
}

- (NetworkStatus)currentReachabilityStatus
{
    NSCAssert (_reachabilityRef != NULL, @"currentNetworkStatus called with NULL SCNetworkReachabilityRef");

    SCNetworkReachabilityFlags flags;
    if (SCNetworkReachabilityGetFlags (_reachabilityRef, &flags))
        return [self networkStatusForFlags: flags];

    return NotReachable;
}

@end

#endif // JUCE_MAC || JUCE_IOS
