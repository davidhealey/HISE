/*  ===========================================================================
*
*   Network connectivity detection for macOS / iOS.
*   Based on code from SquarePine Modules (https://gitlab.com/squarepine/squarepinemodules)
*   Used under the MIT licence.
*
*   This file is compiled as Objective-C++ via hi_network_connectivity.mm.
*
*   ===========================================================================
*/

#if JUCE_MAC || JUCE_IOS

// Reachability.mm must be included here rather than compiled separately so
// it shares the same Objective-C++ translation unit.
#include "Reachability.mm"

namespace hise {

NetworkConnectivityChecker::NetworkType getCurrentSystemNetworkType()
{
    Reachability* reachability = [Reachability reachabilityForInternetConnection];
    NetworkStatus status = [reachability currentReachabilityStatus];

    if (status == NotReachable)          return NetworkConnectivityChecker::NetworkType::none;
    else if (status == ReachableViaWiFi) return NetworkConnectivityChecker::NetworkType::wifi;
    else if (status == ReachableViaWWAN) return NetworkConnectivityChecker::NetworkType::mobile;

    return NetworkConnectivityChecker::NetworkType::wired;
}

double getCurrentSystemRSSI()
{
    // Apple does not expose RSSI through public APIs:
    // https://forums.developer.apple.com/thread/67932
    return 1.0;
}

} // namespace hise

#endif // JUCE_MAC || JUCE_IOS
