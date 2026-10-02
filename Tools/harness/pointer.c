// Minimal version-1 client for wlr-virtual-pointer-unstable-v1.
// Protocol: https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/blob/master/unstable/wlr-virtual-pointer-unstable-v1.xml
#include <wayland-client.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const struct wl_message requests[] = {
    {"motion", "uff", NULL}, {"motion_absolute", "uuuu", NULL},
    {"button", "uuu", NULL}, {"axis", "uuf", NULL}, {"frame", "", NULL},
    {"axis_source", "u", NULL}, {"axis_stop", "uu", NULL},
    {"axis_discrete", "uufi", NULL}, {"destroy", "", NULL}
};
static const struct wl_interface pointer_interface = {
    "zwlr_virtual_pointer_v1", 1, 9, requests, 0, NULL
};
static const struct wl_interface *create_types[] = {&wl_seat_interface, &pointer_interface};
static const struct wl_message manager_requests[] = {
    {"create_virtual_pointer", "?on", create_types}, {"destroy", "", NULL}
};
static const struct wl_interface manager_interface = {
    "zwlr_virtual_pointer_manager_v1", 1, 2, manager_requests, 0, NULL
};
static struct wl_proxy *manager;
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, manager_interface.name))
        manager = wl_registry_bind(registry, name, &manager_interface, 1);
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener listener = {global, removed};
static uint32_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void frame(struct wl_display *display, struct wl_proxy *pointer) {
    wl_proxy_marshal_flags(pointer, 4, NULL, 1, 0);
    if (wl_display_roundtrip(display) < 0) { perror("wayland roundtrip"); exit(1); }
}
static void button(struct wl_display *display, struct wl_proxy *pointer, unsigned code, unsigned pressed) {
    wl_proxy_marshal_flags(pointer, 2, NULL, 1, 0, now_ms(), code, pressed);
    frame(display, pointer);
}
int main(int argc, char **argv) {
    if (argc < 3 || (strcmp(argv[1], "click") && strcmp(argv[1], "scroll") && strcmp(argv[1], "drag") && strcmp(argv[1], "move"))) {
        fputs("Usage: pointer click 272|273 | scroll signed-steps | drag dx dy | move dx dy\n", stderr); return 2;
    }
    if ((!strcmp(argv[1], "drag") || !strcmp(argv[1], "move")) && argc != 4) return 2;
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) { perror("wayland connect"); return 1; }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, NULL); wl_display_roundtrip(display);
    if (!manager) { fputs("Compositor lacks virtual pointer protocol\n", stderr); return 1; }
    struct wl_proxy *pointer = wl_proxy_marshal_flags(manager, 0, &pointer_interface, 1, 0, NULL, NULL);
    // Warped input needs a motion event; free motion must preserve its own delta.
    if (strcmp(argv[1], "move")) {
        wl_proxy_marshal_flags(pointer, 0, NULL, 1, 0, now_ms(), wl_fixed_from_int(1), wl_fixed_from_int(1));
        frame(display, pointer); usleep(100000);
    }
    if (!strcmp(argv[1], "click")) {
        button(display, pointer, atoi(argv[2]), 1); usleep(100000);
        button(display, pointer, atoi(argv[2]), 0);
    } else if (!strcmp(argv[1], "scroll")) {
        int steps = atoi(argv[2]), direction = steps < 0 ? -1 : 1;
        for (int i = 0; i < abs(steps); ++i) {
            wl_proxy_marshal_flags(pointer, 5, NULL, 1, 0, 0u);
            wl_proxy_marshal_flags(pointer, 7, NULL, 1, 0, now_ms(), 0u, wl_fixed_from_int(15 * direction), direction);
            frame(display, pointer); usleep(100000);
        }
    } else {
        const int dragging = !strcmp(argv[1], "drag");
        if (dragging) { button(display, pointer, BTN_MIDDLE, 1); usleep(100000); }
        for (int i = 0; i < 10; ++i) {
            wl_proxy_marshal_flags(pointer, 0, NULL, 1, 0, now_ms(), wl_fixed_from_double(atof(argv[2]) / 10.), wl_fixed_from_double(atof(argv[3]) / 10.));
            frame(display, pointer); usleep(40000);
        }
        if (dragging) button(display, pointer, BTN_MIDDLE, 0);
    }
    wl_proxy_marshal_flags(pointer, 8, NULL, 1, WL_MARSHAL_FLAG_DESTROY);
    wl_proxy_marshal_flags(manager, 1, NULL, 1, WL_MARSHAL_FLAG_DESTROY);
    wl_registry_destroy(registry); wl_display_roundtrip(display); wl_display_disconnect(display);
    return 0;
}
