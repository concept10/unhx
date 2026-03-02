/*
 * servers/bootstrap/registry.c — Service name → port registry for UNHOX
 *
 * The registry maps service names (strings) to port handles (ipc_port *,
 * carried as uint64_t to avoid a kernel header dependency in the .h).
 *
 * In a real system the bootstrap server would receive registration and
 * lookup requests as Mach messages on its bootstrap port and update this
 * registry in response.  For UNHOX Phase 2, the message dispatch happens
 * in bootstrap.c; this file is the backing store.
 *
 * Reference: OSF MK bootstrap/bootstrap_defs.h for the message format.
 */

#include "bootstrap.h"

/* -------------------------------------------------------------------------
 * Minimal string helpers (freestanding — no libc)
 * ------------------------------------------------------------------------- */

static int bs_strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void bs_strncpy(char *dest, const char *src, int n)
{
    int i;
    for (i = 0; i < n - 1 && src[i]; i++)
        dest[i] = src[i];
    dest[i] = '\0';
}

/* -------------------------------------------------------------------------
 * Registry data structure
 * ------------------------------------------------------------------------- */

struct service_entry {
    char        name[BOOTSTRAP_NAME_MAX];
    uint64_t    port;       /* ipc_port * carried as opaque integer */
    int         active;     /* 1 = registered and active */
    int         checked_in; /* 1 = server has claimed this slot */
};

static struct service_entry registry[BOOTSTRAP_MAX_SERVICES];
static int registry_count = 0;

/* -------------------------------------------------------------------------
 * Implementation
 * ------------------------------------------------------------------------- */

void bootstrap_init(void)
{
    for (int i = 0; i < BOOTSTRAP_MAX_SERVICES; i++) {
        registry[i].active     = 0;
        registry[i].checked_in = 0;
        registry[i].port       = 0;
        registry[i].name[0]    = '\0';
    }
    registry_count = 0;
}

int bootstrap_register(const char *name, uint64_t port)
{
    if (!name)
        return BOOTSTRAP_NOT_PRIVILEGED;

    /* Check for duplicate name */
    for (int i = 0; i < BOOTSTRAP_MAX_SERVICES; i++) {
        if (registry[i].active && bs_strcmp(registry[i].name, name) == 0)
            return BOOTSTRAP_NAME_IN_USE;
    }

    /* Find a free slot */
    if (registry_count >= BOOTSTRAP_MAX_SERVICES)
        return BOOTSTRAP_NO_MEMORY;

    for (int i = 0; i < BOOTSTRAP_MAX_SERVICES; i++) {
        if (!registry[i].active) {
            bs_strncpy(registry[i].name, name, BOOTSTRAP_NAME_MAX);
            registry[i].port       = port;
            registry[i].active     = 1;
            registry[i].checked_in = 1;
            registry_count++;
            return BOOTSTRAP_SUCCESS;
        }
    }

    return BOOTSTRAP_NO_MEMORY;
}

int bootstrap_lookup(const char *name, uint64_t *out_port)
{
    if (!name || !out_port)
        return BOOTSTRAP_UNKNOWN_SERVICE;

    for (int i = 0; i < BOOTSTRAP_MAX_SERVICES; i++) {
        if (registry[i].active && bs_strcmp(registry[i].name, name) == 0) {
            *out_port = registry[i].port;
            return BOOTSTRAP_SUCCESS;
        }
    }

    return BOOTSTRAP_UNKNOWN_SERVICE;
}

int bootstrap_checkin(const char *name, uint64_t *out_port)
{
    if (!name || !out_port)
        return BOOTSTRAP_UNKNOWN_SERVICE;

    for (int i = 0; i < BOOTSTRAP_MAX_SERVICES; i++) {
        if (registry[i].active &&
            !registry[i].checked_in &&
            bs_strcmp(registry[i].name, name) == 0)
        {
            registry[i].checked_in = 1;
            *out_port = registry[i].port;
            return BOOTSTRAP_SUCCESS;
        }
    }

    return BOOTSTRAP_UNKNOWN_SERVICE;
}
