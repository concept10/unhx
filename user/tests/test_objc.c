/*
 * user/tests/test_objc.c — Phase 4 milestone: ObjC runtime + Foundation test
 *
 * Verifies:
 *   1. ObjC runtime init (sel, class, msg_send, NSObject)
 *   2. Foundation init (NSString, NSArray, NSDictionary, NSNumber)
 *   3. NSAutoreleasePool drain
 *   4. Message dispatch and method cache
 *
 * Expected output:
 *   [objc] runtime init OK
 *   [objc] NSObject alloc/init OK
 *   [objc] NSString OK: Hello, UNHOX!
 *   [objc] NSArray OK: count=3
 *   [objc] NSDictionary OK: found=Hello, UNHOX!
 *   [objc] NSNumber OK: 42
 *   [objc] autorelease pool drain OK
 *   [objc] ALL TESTS PASSED
 */

#include "objc/runtime.h"
#include "Foundation.h"
#include "stdio.h"
#include "string.h"

static int failures = 0;

#define TEST(name, cond) do { \
    if (!(cond)) { \
        printf("[objc] FAIL: %s\n", name); \
        failures++; \
    } \
} while(0)

int main(void)
{
    /* 1. Initialize runtime + Foundation */
    objc_runtime_init();
    foundation_init();
    printf("[objc] runtime init OK\n");

    /* 2. NSObject alloc/init */
    {
        Class nsobject_cls = objc_getClass("NSObject");
        TEST("NSObject class exists", nsobject_cls != Nil);

        SEL alloc_sel = sel_registerName("alloc");
        SEL init_sel  = sel_registerName("init");

        IMP alloc_imp = objc_msg_lookup((id)nsobject_cls, alloc_sel);
        id obj = alloc_imp((id)nsobject_cls, alloc_sel);
        TEST("NSObject alloc", obj != nil);

        IMP init_imp = objc_msg_lookup(obj, init_sel);
        obj = init_imp(obj, init_sel);
        TEST("NSObject init", obj != nil);

        /* Check class method */
        SEL class_sel = sel_registerName("class");
        IMP class_imp = objc_msg_lookup(obj, class_sel);
        Class got_cls = (Class)class_imp(obj, class_sel);
        TEST("NSObject class method", got_cls == nsobject_cls);

        printf("[objc] NSObject alloc/init OK\n");

        SEL release_sel = sel_registerName("release");
        IMP rel_imp = objc_msg_lookup(obj, release_sel);
        rel_imp(obj, release_sel);
    }

    /* 3. NSString */
    {
        Class str_cls = objc_getClass("NSString");
        TEST("NSString class exists", str_cls != Nil);

        SEL alloc_sel = sel_registerName("alloc");
        SEL init_sel  = sel_registerName("initWithCString:");

        IMP alloc_imp = objc_msg_lookup((id)str_cls, alloc_sel);
        id str = alloc_imp((id)str_cls, alloc_sel);
        TEST("NSString alloc", str != nil);

        IMP init_imp = objc_msg_lookup(str, init_sel);
        str = init_imp(str, init_sel, "Hello, UNHOX!");
        TEST("NSString initWithCString", str != nil);

        SEL utf8_sel = sel_registerName("UTF8String");
        IMP utf8_imp = objc_msg_lookup(str, utf8_sel);
        const char *cstr = (const char *)utf8_imp(str, utf8_sel);
        TEST("NSString UTF8String", cstr != 0 && strcmp(cstr, "Hello, UNHOX!") == 0);

        SEL len_sel = sel_registerName("length");
        IMP len_imp = objc_msg_lookup(str, len_sel);
        size_t len = (size_t)len_imp(str, len_sel);
        TEST("NSString length", len == 13);

        printf("[objc] NSString OK: %s\n", cstr);

        SEL release_sel = sel_registerName("release");
        IMP rel_imp = objc_msg_lookup(str, release_sel);
        rel_imp(str, release_sel);
    }

    /* 4. NSArray */
    {
        Class arr_cls = objc_getClass("NSArray");
        TEST("NSArray class exists", arr_cls != Nil);

        SEL alloc_sel = sel_registerName("alloc");
        SEL init_sel  = sel_registerName("init");

        IMP alloc_imp = objc_msg_lookup((id)arr_cls, alloc_sel);
        id arr = alloc_imp((id)arr_cls, alloc_sel);
        IMP init_imp = objc_msg_lookup(arr, init_sel);
        arr = init_imp(arr, init_sel);
        TEST("NSArray init", arr != nil);

        /* Add 3 NSString objects */
        Class str_cls = objc_getClass("NSString");
        SEL str_alloc = sel_registerName("alloc");
        SEL str_init  = sel_registerName("initWithCString:");
        SEL add_sel   = sel_registerName("addObject:");

        const char *items[] = { "one", "two", "three" };
        for (int i = 0; i < 3; i++) {
            IMP sa = objc_msg_lookup((id)str_cls, str_alloc);
            id s = sa((id)str_cls, str_alloc);
            IMP si = objc_msg_lookup(s, str_init);
            s = si(s, str_init, items[i]);

            IMP add_imp = objc_msg_lookup(arr, add_sel);
            add_imp(arr, add_sel, s);

            /* Release our reference (array retained it) */
            SEL rel = sel_registerName("release");
            IMP ri = objc_msg_lookup(s, rel);
            ri(s, rel);
        }

        SEL count_sel = sel_registerName("count");
        IMP count_imp = objc_msg_lookup(arr, count_sel);
        uint32_t count = (uint32_t)(uintptr_t)count_imp(arr, count_sel);
        TEST("NSArray count", count == 3);

        printf("[objc] NSArray OK: count=%u\n", count);

        SEL release_sel = sel_registerName("release");
        IMP rel_imp = objc_msg_lookup(arr, release_sel);
        rel_imp(arr, release_sel);
    }

    /* 5. NSDictionary */
    {
        Class dict_cls = objc_getClass("NSDictionary");
        TEST("NSDictionary class exists", dict_cls != Nil);

        SEL alloc_sel = sel_registerName("alloc");
        SEL init_sel  = sel_registerName("init");

        IMP alloc_imp = objc_msg_lookup((id)dict_cls, alloc_sel);
        id dict = alloc_imp((id)dict_cls, alloc_sel);
        IMP init_imp = objc_msg_lookup(dict, init_sel);
        dict = init_imp(dict, init_sel);
        TEST("NSDictionary init", dict != nil);

        /* Create key and value strings */
        Class str_cls = objc_getClass("NSString");
        SEL sa = sel_registerName("alloc");
        SEL si = sel_registerName("initWithCString:");

        IMP str_alloc = objc_msg_lookup((id)str_cls, sa);
        id key = str_alloc((id)str_cls, sa);
        IMP str_init = objc_msg_lookup(key, si);
        key = str_init(key, si, "greeting");

        id val = str_alloc((id)str_cls, sa);
        str_init = objc_msg_lookup(val, si);
        val = str_init(val, si, "Hello, UNHOX!");

        /* Set and get */
        SEL set_sel = sel_registerName("setObject:forKey:");
        IMP set_imp = objc_msg_lookup(dict, set_sel);
        set_imp(dict, set_sel, val, key);

        SEL get_sel = sel_registerName("objectForKey:");
        IMP get_imp = objc_msg_lookup(dict, get_sel);
        id got = get_imp(dict, get_sel, key);
        TEST("NSDictionary objectForKey", got != nil);

        SEL utf8_sel = sel_registerName("UTF8String");
        IMP utf8_imp = objc_msg_lookup(got, utf8_sel);
        const char *got_str = (const char *)utf8_imp(got, utf8_sel);
        TEST("NSDictionary value", got_str != 0 && strcmp(got_str, "Hello, UNHOX!") == 0);

        printf("[objc] NSDictionary OK: found=%s\n", got_str);

        SEL rel = sel_registerName("release");
        IMP ri = objc_msg_lookup(key, rel); ri(key, rel);
        ri = objc_msg_lookup(val, rel); ri(val, rel);
        ri = objc_msg_lookup(dict, rel); ri(dict, rel);
    }

    /* 6. NSNumber */
    {
        Class num_cls = objc_getClass("NSNumber");
        TEST("NSNumber class exists", num_cls != Nil);

        SEL numint_sel = sel_registerName("numberWithInt:");
        IMP numint_imp = objc_msg_lookup((id)num_cls, numint_sel);
        id num = numint_imp((id)num_cls, numint_sel, (long)42);
        TEST("NSNumber numberWithInt", num != nil);

        SEL intval_sel = sel_registerName("intValue");
        IMP intval_imp = objc_msg_lookup(num, intval_sel);
        long val = (long)intval_imp(num, intval_sel);
        TEST("NSNumber intValue", val == 42);

        printf("[objc] NSNumber OK: %ld\n", val);

        SEL rel = sel_registerName("release");
        IMP ri = objc_msg_lookup(num, rel); ri(num, rel);
    }

    /* 7. Autorelease pool */
    {
        Class pool_cls = objc_getClass("NSAutoreleasePool");
        TEST("NSAutoreleasePool class exists", pool_cls != Nil);

        SEL alloc_sel = sel_registerName("alloc");
        SEL init_sel  = sel_registerName("init");
        SEL drain_sel = sel_registerName("drain");

        IMP alloc_imp = objc_msg_lookup((id)pool_cls, alloc_sel);
        id pool = alloc_imp((id)pool_cls, alloc_sel);
        IMP init_imp = objc_msg_lookup(pool, init_sel);
        pool = init_imp(pool, init_sel);
        TEST("NSAutoreleasePool init", pool != nil);

        /* Create a string and autorelease it */
        Class str_cls = objc_getClass("NSString");
        SEL sa = sel_registerName("alloc");
        SEL si = sel_registerName("initWithCString:");
        SEL ar = sel_registerName("autorelease");

        IMP str_alloc = objc_msg_lookup((id)str_cls, sa);
        id str = str_alloc((id)str_cls, sa);
        IMP str_init = objc_msg_lookup(str, si);
        str = str_init(str, si, "autoreleased");

        IMP ar_imp = objc_msg_lookup(str, ar);
        ar_imp(str, ar);

        /* Drain the pool */
        IMP drain_imp = objc_msg_lookup(pool, drain_sel);
        drain_imp(pool, drain_sel);

        printf("[objc] autorelease pool drain OK\n");
    }

    /* Summary */
    if (failures == 0)
        printf("[objc] ALL TESTS PASSED\n");
    else
        printf("[objc] %d TESTS FAILED\n", failures);

    return failures;
}
