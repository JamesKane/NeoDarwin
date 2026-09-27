// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: C header included by XNU's kern_exec.c and necp_client.c; Apple's CodeSignature private headers are not published.
//
// Entitlement keys XNU checks by name. The values are the publicly documented
// BrowserEngineKit entitlement identifiers.
#ifndef ND_CODESIGNATURE_ENTITLEMENTS_H
#define ND_CODESIGNATURE_ENTITLEMENTS_H

#define kCSWebBrowserHostEntitlement       "com.apple.developer.web-browser-engine.host"
#define kCSWebBrowserGPUEntitlement        "com.apple.developer.web-browser-engine.rendering"
#define kCSWebBrowserNetworkEntitlement    "com.apple.developer.web-browser-engine.networking"
#define kCSWebBrowserWebContentEntitlement "com.apple.developer.web-browser-engine.webcontent"

#endif
