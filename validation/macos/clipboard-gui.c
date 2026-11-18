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
extern id const NSPasteboardTypeString;
extern int printf(const char *, ...);
extern int fflush(void *);
extern char *getenv(const char *);
extern unsigned int sleep(unsigned int);
#define TRACE(message) do { printf("Cocoa validation: %s\n", message); fflush(0); } while (0)

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
static void copied(id self, SEL command, id sender) {
    (void)self; (void)command; (void)sender;
    id board = send0(objc_getClass("NSPasteboard"), "generalPasteboard");
    id type = NSPasteboardTypeString;
    id types = ((id (*)(id, SEL, id))objc_msgSend)(
        objc_getClass("NSArray"), sel_registerName("arrayWithObject:"), type);
    ((long (*)(id, SEL, id, id))objc_msgSend)(board, sel_registerName("declareTypes:owner:"), types, 0);
    signed char ok = ((signed char (*)(id, SEL, id, id))objc_msgSend)(
        board, sel_registerName("setString:forType:"), string("Olympium Cocoa copy"), type);
    object_arg(window, "setTitle:", string(ok ? "Cocoa clipboard copied" : "Copy failed"));
    printf("copy=%d\n", ok); fflush(0);
}
static void pasted(id self, SEL command, id sender) {
    (void)self; (void)command; (void)sender;
    id board = send0(objc_getClass("NSPasteboard"), "generalPasteboard");
    id value = ((id (*)(id, SEL, id))objc_msgSend)(
        board, sel_registerName("stringForType:"), NSPasteboardTypeString);
    const char *text = ((const char *(*)(id, SEL))objc_msgSend)(value, sel_registerName("UTF8String"));
    object_arg(window, "setTitle:", value ? value : string("Paste failed"));
    printf("paste=%s\n", text ? text : "(nil)"); fflush(0);
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
    TRACE("NSWindow created");
    object_arg(window, "setTitle:", string("Olympium Cocoa Clipboard"));
    TRACE("window title set");
    label = control("NSTextField", (Rect){{30, 140}, {400, 30}});
    TRACE("text field created");
    object_arg(label, "setStringValue:", string("Real Mach-O application using Cocoa"));
    number_arg(label, "setEditable:", 0);
    number_arg(label, "setBezeled:", 0);
    number_arg(label, "setDrawsBackground:", 0);
    object_arg(send0(window, "contentView"), "addSubview:", label);
    TRACE("text field attached");
    Class klass = objc_allocateClassPair(objc_getClass("NSObject"), "OlympiumValidationController", 0);
    class_addMethod(klass, sel_registerName("didCopy:"), (void (*)(void))copied, "v@:@");
    class_addMethod(klass, sel_registerName("didPaste:"), (void (*)(void))pasted, "v@:@");
    objc_registerClassPair(klass);
    id controller = send0(send0(klass, "alloc"), "init");
    id button = control("NSButton", (Rect){{35, 60}, {180, 40}});
    TRACE("button created");
    object_arg(button, "setTitle:", string("Copy from Cocoa"));
    object_arg(button, "setTarget:", controller);
    object_arg(button, "setAction:", sel_registerName("didCopy:"));
    object_arg(send0(window, "contentView"), "addSubview:", button);
    id paste = control("NSButton", (Rect){{245, 60}, {180, 40}});
    object_arg(paste, "setTitle:", string("Read host clipboard"));
    object_arg(paste, "setTarget:", controller);
    object_arg(paste, "setAction:", sel_registerName("didPaste:"));
    object_arg(send0(window, "contentView"), "addSubview:", paste);
    send0(window, "center");
    TRACE("window centered");
    if (getenv("OLYMPIUM_COCOA_DIAGNOSTIC_PAUSE")) {
        TRACE("diagnostic pause before first display");
        sleep(20);
    }
    object_arg(window, "makeKeyAndOrderFront:", 0);
    TRACE("window ordered front");
    number_arg(app, "activateIgnoringOtherApps:", 1);
    TRACE("application activated");
    printf("Cocoa validation: entering application event loop\n");
    fflush(0);
    send0(app, "run");
    printf("Cocoa validation: application event loop returned\n");
    send0(pool, "drain");
    return 0;
}
