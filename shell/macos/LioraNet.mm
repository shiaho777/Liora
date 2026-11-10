#import "LioraNet.h"

#include <signal.h>

@implementation LioraNetService {
  NSTask* _task;
  NSString* _binary;
  NSString* _configPath;
  NSString* _config;
  NSFileHandle* _logFile;
}

+ (NSString*)logPath {
  NSString* base = [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) firstObject];
  NSString* directory = [base stringByAppendingPathComponent:@"Logs/Liora"];
  [[NSFileManager defaultManager] createDirectoryAtPath:directory
                            withIntermediateDirectories:YES
                                             attributes:nil
                                                  error:nil];
  return [directory stringByAppendingPathComponent:@"liora-net.log"];
}

+ (NSString*)locateBinary {
  NSFileManager* files = [NSFileManager defaultManager];

  NSString* override = NSProcessInfo.processInfo.environment[@"LIORA_NET_BIN"];
  if (override.length && [files isExecutableFileAtPath:override]) return override;

  NSString* bundled = [[NSBundle mainBundle] pathForResource:@"liora-net" ofType:nil];
  if (bundled && [files isExecutableFileAtPath:bundled]) return bundled;

#ifdef LIORA_NET_DEV_BIN
  NSString* development = @(LIORA_NET_DEV_BIN);
  if ([files isExecutableFileAtPath:development]) return development;
#endif
  return nil;
}

- (BOOL)startWithConfig:(NSString*)configJSON error:(NSString**)error {
  [self stop];

  if (!_binary) {
    _binary = [LioraNetService locateBinary];
    if (!_binary) {
      if (error) *error = @"找不到 liora-net 可执行文件";
      return NO;
    }
  }

  // One config file per launch; the child reads it once at startup.
  NSString* directory = NSTemporaryDirectory();
  _configPath = [directory stringByAppendingPathComponent:@"liora-net.json"];
  NSError* writeError = nil;
  if (![configJSON writeToFile:_configPath atomically:YES encoding:NSUTF8StringEncoding error:&writeError]) {
    if (error) *error = writeError.localizedDescription ?: @"无法写入 liora-net 配置";
    return NO;
  }

  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:_binary];
  task.arguments = @[ @"--config", _configPath, @"--doh-port", @"0", @"--proxy-port", @"0" ];

  // Keep a log file next to the app logs; it makes DNS problems diagnosable.
  NSString* logPath = [LioraNetService logPath];
  [[NSFileManager defaultManager] createFileAtPath:logPath contents:nil attributes:nil];
  _logFile = [NSFileHandle fileHandleForWritingAtPath:logPath];
  [_logFile seekToEndOfFile];

  NSPipe* stdoutPipe = [NSPipe pipe];
  NSPipe* stderrPipe = [NSPipe pipe];
  task.standardOutput = stdoutPipe;
  task.standardError = stderrPipe;
  task.standardInput = [NSFileHandle fileHandleWithNullDevice];

  NSError* launchError = nil;
  if (![task launchAndReturnError:&launchError]) {
    if (error) *error = launchError.localizedDescription ?: @"无法启动 liora-net";
    return NO;
  }

  // The first stdout line is the state document; read it with a bounded wait.
  dispatch_semaphore_t ready = dispatch_semaphore_create(0);
  __block NSMutableData* buffer = [NSMutableData data];
  __block NSString* line = nil;

  NSFileHandle* stdoutHandle = stdoutPipe.fileHandleForReading;
  stdoutHandle.readabilityHandler = ^(NSFileHandle* handle) {
    NSData* chunk = handle.availableData;
    if (chunk.length == 0) return;
    [buffer appendData:chunk];
    if (line) return;
    NSRange newline = [buffer rangeOfData:[@"\n" dataUsingEncoding:NSUTF8StringEncoding]
                                  options:0
                                    range:NSMakeRange(0, buffer.length)];
    if (newline.location == NSNotFound) return;
    line = [[NSString alloc] initWithData:[buffer subdataWithRange:NSMakeRange(0, newline.location)]
                                 encoding:NSUTF8StringEncoding];
    dispatch_semaphore_signal(ready);
  };

  NSFileHandle* stderrHandle = stderrPipe.fileHandleForReading;
  stderrHandle.readabilityHandler = ^(NSFileHandle* handle) {
    NSData* chunk = handle.availableData;
    if (chunk.length == 0) return;
    [self->_logFile writeData:chunk];
    NSString* text = [[NSString alloc] initWithData:chunk encoding:NSUTF8StringEncoding];
    if (text.length) {
      NSLog(@"[liora-net] %@", [text stringByTrimmingCharactersInSet:
                                    [NSCharacterSet whitespaceAndNewlineCharacterSet]]);
    }
  };

  long timedOut = dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC));
  stdoutHandle.readabilityHandler = nil;

  if (timedOut != 0 || line.length == 0) {
    [task terminate];
    if (error) *error = @"liora-net 没有报告状态";
    return NO;
  }

  NSData* data = [line dataUsingEncoding:NSUTF8StringEncoding];
  NSDictionary* state = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
  if (![state isKindOfClass:[NSDictionary class]]) {
    [task terminate];
    if (error) *error = @"liora-net 的状态无法解析";
    return NO;
  }

  _dohPort = [state[@"dohPort"] intValue];
  _proxyPort = [state[@"proxyPort"] intValue];
  _diagPort = [state[@"diagPort"] intValue];
  _spki = [state[@"spki"] copy];
  _runningConfig = [configJSON copy];
  _task = task;
  _config = [configJSON copy];

  if (_dohPort <= 0) {
    [self stop];
    if (error) *error = @"liora-net 未能绑定 DoH 端口";
    return NO;
  }
  return YES;
}

- (void)stop {
  if (!_task) return;
  if (_task.isRunning) {
    [_task terminate];
    // Give it a moment to close listeners, then make sure.
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
    while (_task.isRunning && deadline.timeIntervalSinceNow > 0) {
      [NSThread sleepForTimeInterval:0.02];
    }
    if (_task.isRunning) kill(_task.processIdentifier, SIGKILL);
  }
  _task = nil;
  _dohPort = 0;
  _proxyPort = 0;
  _diagPort = 0;
  _spki = nil;
  _runningConfig = nil;
  [_logFile closeFile];
  _logFile = nil;
}

- (void)dealloc {
  [self stop];
}

@end
