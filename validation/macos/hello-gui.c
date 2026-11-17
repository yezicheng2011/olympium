/* Independent Cocoa smoke application. No Apple SDK headers or assets. */
typedef void *id;
typedef void *SEL;
typedef void *Class;
typedef struct { double x, y; } Point;
typedef struct { double width, height; } Size;
typedef struct { Point origin; Size size; } Rect;
extern Class objc_getClass(const char *);
extern SEL sel_registerName(const char *);
extern Class objc_allocateClassPair(Class, const char *, unsigned long);
extern void objc_registerClassPair(Class);
extern signed char class_addMethod(Class, SEL, void (*)(void), const char *);
extern void objc_msgSend(void);
extern int printf(const char *, ...);
extern int fflush(void *);

static id window, label;
static id send0(id object, const char *name) {
    return ((id (*)(id, SEL))objc_msgSend)(object, sel_registerName(name));
}
static void object_arg(id object, const char *name, id value) {
    ((void (*)(id, SEL, id))objc_msgSend)(object, sel_registerName(name), value);
}
static void number_arg(id object, const char *name, long value) {
    ((void (*)(id, SEL, long))objc_msgSend)(object, sel_registerName(name), value);
}
static id string(const char *value) {
    return ((id (*)(id, SEL, const char *))objc_msgSend)(
        objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), value);
}
static id control(const char *name, Rect frame) {
    return ((id (*)(id, SEL, Rect))objc_msgSend)(
        send0(objc_getClass(name), "alloc"), sel_registerName("initWithFrame:"), frame);
}
static void clicked(id self, SEL command, id sender) {
    (void)self; (void)command; (void)sender;
    object_arg(label, "setStringValue:", string("Cocoa button event verified"));
    object_arg(window, "setTitle:", string("Olympium Mac GUI - verified"));
}
int main(void) {
    printf("Cocoa validation: main entered\n");
    fflush(0);
    if (!objc_getClass("NSApplication") || !objc_getClass("NSWindow")) {
        printf("Cocoa validation: required AppKit classes unavailable\n");
        return 2;
    }
    id pool = send0(send0(objc_getClass("NSAutoreleasePool"), "alloc"), "init");
    id app = send0(objc_getClass("NSApplication"), "sharedApplication");
    printf("Cocoa validation: NSApplication=%p\n", app);
    fflush(0);
    number_arg(app, "setActivationPolicy:", 0);
    Rect frame = {{100, 100}, {460, 220}};
    window = ((id (*)(id, SEL, Rect, unsigned long, unsigned long, signed char))objc_msgSend)(
        send0(objc_getClass("NSWindow"), "alloc"),
        sel_registerName("initWithContentRect:styleMask:backing:defer:"), frame, 15, 2, 0);
    if (!app || !window) {
        printf("Cocoa validation: application/window initialization failed\n");
        return 3;
    }
    object_arg(window, "setTitle:", string("Olympium Mac GUI"));
    label = control("NSTextField", (Rect){{30, 140}, {400, 30}});
    object_arg(label, "setStringValue:", string("Real Mach-O application using Cocoa"));
    number_arg(label, "setEditable:", 0);
    number_arg(label, "setBezeled:", 0);
    number_arg(label, "setDrawsBackground:", 0);
    object_arg(send0(window, "contentView"), "addSubview:", label);
    Class klass = objc_allocateClassPair(objc_getClass("NSObject"), "OlympiumValidationController", 0);
    class_addMethod(klass, sel_registerName("didClick:"), (void (*)(void))clicked, "v@:@");
    objc_registerClassPair(klass);
    id controller = send0(send0(klass, "alloc"), "init");
    id button = control("NSButton", (Rect){{120, 60}, {220, 40}});
    object_arg(button, "setTitle:", string("Verify Cocoa input"));
    object_arg(button, "setTarget:", controller);
    object_arg(button, "setAction:", sel_registerName("didClick:"));
    object_arg(send0(window, "contentView"), "addSubview:", button);
    send0(window, "center");
    object_arg(window, "makeKeyAndOrderFront:", 0);
    number_arg(app, "activateIgnoringOtherApps:", 1);
    printf("Cocoa validation: entering application event loop\n");
    fflush(0);
    send0(app, "run");
    printf("Cocoa validation: application event loop returned\n");
    send0(pool, "drain");
    return 0;
}
