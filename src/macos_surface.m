#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

void *flecs_create_metal_layer(void *ns_window) {
  NSWindow *window = (NSWindow *)ns_window;
  NSView *view = [window contentView];
  [view setWantsLayer:YES];
  CAMetalLayer *layer = [CAMetalLayer layer];
  [view setLayer:layer];
  return layer;
}
