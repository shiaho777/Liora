#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Runs the liora-net helper as a child process and exposes the ports it chose
// plus the SPKI of its local DoH certificate.
@interface LioraNetService : NSObject

// Search order: LIORA_NET_BIN, the app bundle, the development build.
+ (nullable NSString*)locateBinary;

- (BOOL)startWithConfig:(NSString*)configJSON error:(NSString* _Nullable * _Nullable)error;
- (void)stop;

@property(nonatomic, readonly) int dohPort;
@property(nonatomic, readonly) int proxyPort;
@property(nonatomic, readonly, nullable, copy) NSString* spki;
@property(nonatomic, readonly, nullable, copy) NSString* runningConfig;

@end

NS_ASSUME_NONNULL_END
