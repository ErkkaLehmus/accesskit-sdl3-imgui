/*
 * A simple test to create a SDL3 window with GUI labels and buttons,
 * making the widgets accessible to screen readers
 *
 * MIT license. Feel free to copy, modify, and use the code the way you need.
 */

//AccessKit is for desktop OSes, but also mentions Android.
//Personally, for desktop projects I'd prefer to have main() and the loop
//but for this test I chose to go with callbacks for two reasons;
//1. it is easier to convert from callback version to a plain version
//2. I read the callback way is more portable, so in case there is a way
// to integrate with accessibility on iPhone and other such platforms,
// then maybe just go with callbacks right from the beginning.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

#include "accesskit.h"

#include <string>

#include "imgui_internal.h"


//still, because I'm lazy, I mix having static global variables
//instead of packing it all into the appstate
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static std::string labeltext = "use a screen reader, mouse or TAB and SPACE";

//we need a variable to help keep the GUI and AccessKit in sync
static bool labelUpdated = true;

//more helper variables
static int appleCounter = 0;
static int orangeCounter = 0;
static float uiScale = 1.0f;

#define WINDOW_WIDTH 711
#define WINDOW_HEIGHT 400
static const ImVec2 WINDOW_SIZE = {WINDOW_WIDTH, WINDOW_HEIGHT};
constexpr char WINDOW_TITLE[] = "Test AccessKit with SDL3 and ImGui";
constexpr char CAPTION_BUTTON_1[] = "Apples";
constexpr char CAPTION_BUTTON_2[] = "Oranges";

//OK I want ImGui but with a single tool window which fill the entire app window
static constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoDecoration;


//AccessKit logic starts here

//first some structs to store data
struct window_state {
    accesskit_node_id focus;
    const char *announcement;
    SDL_Mutex *mutex;
};

struct action_handler_state {
    Uint32 event_type;
    Uint32 window_id;
};

//then numbers and helpers to help build the nodes for AccessKit
const accesskit_node_id WINDOW_ID = 0;
const accesskit_node_id BUTTON_1_ID = 1;
const accesskit_node_id BUTTON_2_ID = 2;
const accesskit_node_id LABEL_ID = 3;
const accesskit_node_id ANNOUNCEMENT_ID = 4;
//#define INITIAL_FOCUS LABEL_ID;
const accesskit_node_id TAB_ORDER[] = {BUTTON_1_ID,BUTTON_2_ID,LABEL_ID};
const int TAB_MAX = 3;
int currentFocus = 0;

constexpr int padding = 32;
constexpr int buttonHeight = 80;
accesskit_rect LABEL_RECT = {20.0, 20.0, 80.0, 80.0};
const accesskit_rect BUTTON_1_RECT = {
    20.0, LABEL_RECT.y1 + padding * 2, 400.0, LABEL_RECT.y1 + padding * 2 + buttonHeight
};
const accesskit_rect BUTTON_2_RECT = {
    20.0, BUTTON_1_RECT.y1 + padding, 400.0, BUTTON_1_RECT.y1 + padding + buttonHeight
};

accesskit_node_id getNextFocus() {
    currentFocus++;
    if (currentFocus >= TAB_MAX) currentFocus = 0;
    return TAB_ORDER[currentFocus];
}


ImVec2 BUTTON_1_POSITION = {static_cast<float>(BUTTON_1_RECT.x0), static_cast<float>(BUTTON_1_RECT.y0)};
ImVec2 BUTTON_1_SIZE = {
    static_cast<float>(BUTTON_1_RECT.x1 - BUTTON_1_RECT.x0), static_cast<float>(BUTTON_1_RECT.y1 - BUTTON_1_RECT.y0)
};
ImVec2 BUTTON_2_POSITION = {static_cast<float>(BUTTON_2_RECT.x0), static_cast<float>(BUTTON_2_RECT.y0)};
ImVec2 BUTTON_2_SIZE = {
    static_cast<float>(BUTTON_2_RECT.x1 - BUTTON_2_RECT.x0), static_cast<float>(BUTTON_2_RECT.y1 - BUTTON_2_RECT.y0)
};
ImVec2 LABEL_POSITION = {static_cast<float>(LABEL_RECT.x0), static_cast<float>(LABEL_RECT.y0)};
//ImVec2 LABEL_SIZE= {static_cast<float>(LABEL_RECT.x1-BUTTON_2_RECT.x0),static_cast<float>(LABEL_RECT.y1-BUTTON_2_RECT.y0)};

const Sint32 SET_FOCUS_MSG = 0;
const Sint32 DO_DEFAULT_ACTION_MSG = 1;

//and then the code

#if defined(__ANDROID__)
extern void android_request_accessibility_update(void);
#endif

#if ((defined(__linux__) || defined(__DragonFly__) || defined(__FreeBSD__) || \
      defined(__NetBSD__) || defined(__OpenBSD__)) &&                         \
     !defined(__ANDROID__))
#define UNIX
#endif

#if defined(__ANDROID__)
/* Android-specific globals for marshaling updates to UI thread */
static accesskit_tree_update_factory g_pending_update_factory = NULL;
static void *g_pending_update_userdata = NULL;
static SDL_mutex *g_update_mutex = NULL;
#endif

accesskit_node *build_button(accesskit_node_id id, const char *label) {
    accesskit_rect rect;
    if (id == BUTTON_1_ID) {
        rect = BUTTON_1_RECT;
    } else {
        rect = BUTTON_2_RECT;
    }

    accesskit_node *node = accesskit_node_new(ACCESSKIT_ROLE_BUTTON);
    accesskit_node_set_bounds(node, rect);
    accesskit_node_set_label(node, label);
    accesskit_node_add_action(node, ACCESSKIT_ACTION_FOCUS);
    accesskit_node_add_action(node, ACCESSKIT_ACTION_CLICK);
    return node;
}

accesskit_node *build_label(const char *label) {
    accesskit_rect rect = LABEL_RECT;
    accesskit_node *node = accesskit_node_new(ACCESSKIT_ROLE_LABEL);
    accesskit_node_set_bounds(node, rect);
    accesskit_node_set_label(node, label);
    accesskit_node_add_action(node, ACCESSKIT_ACTION_FOCUS);
    return node;
}

accesskit_node *build_announcement(const char *text) {
    accesskit_node *node = accesskit_node_new(ACCESSKIT_ROLE_LABEL);
    accesskit_node_set_value(node, text);
    accesskit_node_set_live(node, ACCESSKIT_LIVE_POLITE);
    return node;
}

struct accesskit_sdl_adapter {
#if defined(__APPLE__)
    accesskit_macos_subclassing_adapter *adapter;
#elif defined(UNIX)
    accesskit_unix_adapter *adapter;
#elif defined(_WIN32)
    accesskit_windows_subclassing_adapter *adapter;
#elif defined(__ANDROID__)
    /* On Android, the adapter is owned by the UI thread (JNI side).
       This struct only exists for API compatibility. */
    int dummy;
#endif
};

//We need these to persist from a callback to another
//and for some reason I decided to put these in appstate
//instead of having everything as global variables
//so feel free to rework the way you feel comfortable with
struct app_state {
    struct window_state state;
    struct accesskit_sdl_adapter adapter;
    struct action_handler_state ah_state;
    Uint32 user_event;
    Uint32 window_id;
};

void accesskit_sdl_adapter_init(
    struct accesskit_sdl_adapter *adapter, SDL_Window *window,
    accesskit_activation_handler_callback activation_handler,
    void *activation_handler_userdata,
    accesskit_action_handler_callback action_handler,
    void *action_handler_userdata,
    accesskit_deactivation_handler_callback deactivation_handler,
    void *deactivation_handler_userdata) {
#if defined(__APPLE__)
    void *nswindow = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
                                            NULL);
    if (nswindow) {
        accesskit_macos_add_focus_forwarder_to_window_class("SDL3Window");
        adapter->adapter = accesskit_macos_subclassing_adapter_for_window(
            (void *) nswindow, activation_handler,
            activation_handler_userdata, action_handler, action_handler_userdata);
    }
#elif defined(UNIX)
    adapter->adapter = accesskit_unix_adapter_new(
        activation_handler, activation_handler_userdata, action_handler,
        action_handler_userdata, deactivation_handler,
        deactivation_handler_userdata);
#elif defined(_WIN32)
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    adapter->adapter = accesskit_windows_subclassing_adapter_new(
        wmInfo.info.win.window, activation_handler, activation_handler_userdata,
        action_handler, action_handler_userdata);
#elif defined(__ANDROID__)
    (void) adapter;
    (void) window;
    (void) activation_handler;
    (void) activation_handler_userdata;
    (void) action_handler;
    (void) action_handler_userdata;
    (void) deactivation_handler;
    (void) deactivation_handler_userdata;
    /* On Android, the adapter is owned by the UI thread (JNI side).
       Nothing to do here. */
#endif
}

void accesskit_sdl_adapter_destroy(struct accesskit_sdl_adapter *adapter) {
#if defined(__APPLE__)
    if (adapter->adapter != NULL) {
        accesskit_macos_subclassing_adapter_free(adapter->adapter);
    }
#elif defined(UNIX)
    if (adapter->adapter != NULL) {
        accesskit_unix_adapter_free(adapter->adapter);
    }
#elif defined(_WIN32)
    if (adapter->adapter != NULL) {
        accesskit_windows_subclassing_adapter_free(adapter->adapter);
    }
#elif defined(__ANDROID__)
    /* On Android, the adapter is owned by the UI thread (JNI side).
       Nothing to do here. */
    (void) adapter;
#endif
}

void accesskit_sdl_adapter_update_if_active(
    struct accesskit_sdl_adapter *adapter,
    accesskit_tree_update_factory update_factory,
    void *update_factory_userdata) {
#if defined(__APPLE__)
    accesskit_macos_queued_events *events =
            accesskit_macos_subclassing_adapter_update_if_active(
                adapter->adapter, update_factory, update_factory_userdata);
    if (events != NULL) {
        accesskit_macos_queued_events_raise(events);
    }
#elif defined(UNIX)
    accesskit_unix_adapter_update_if_active(adapter->adapter, update_factory,
                                            update_factory_userdata);
#elif defined(_WIN32)
    accesskit_windows_queued_events *events =
            accesskit_windows_subclassing_adapter_update_if_active(
                adapter->adapter, update_factory, update_factory_userdata);
    if (events != NULL) {
        accesskit_windows_queued_events_raise(events);
    }
#elif defined(__ANDROID__)
    (void) adapter;
    SDL_LockMutex(g_update_mutex);
    g_pending_update_factory = update_factory;
    g_pending_update_userdata = update_factory_userdata;
    SDL_UnlockMutex(g_update_mutex);

    android_request_accessibility_update();
#endif
}

void accesskit_sdl_adapter_update_window_focus_state(
    struct accesskit_sdl_adapter *adapter, bool is_focused) {
#if defined(__APPLE__)
    accesskit_macos_queued_events *events =
            accesskit_macos_subclassing_adapter_update_view_focus_state(
                adapter->adapter, is_focused);
    if (events != NULL) {
        accesskit_macos_queued_events_raise(events);
    }
#elif defined(UNIX)
    accesskit_unix_adapter_update_window_focus_state(adapter->adapter,
                                                     is_focused);
#elif defined(__ANDROID__)
    /* On Android, focus is handled by the system */
    (void) adapter;
    (void) is_focused;
#endif
    /* On Windows, the subclassing adapter takes care of this. */
}

void accesskit_sdl_adapter_update_root_window_bounds(
    struct accesskit_sdl_adapter *adapter, SDL_Window *window) {
#if defined(UNIX)
    int x, y, width, height;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &width, &height);
    int top, left, bottom, right;
    SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
    accesskit_rect outer_bounds = {
        x - left, y - top, x + width + right,
        y + height + bottom
    };
    accesskit_rect inner_bounds = {x, y, x + width, y + height};
    accesskit_unix_adapter_set_root_window_bounds(adapter->adapter, outer_bounds,
                                                  inner_bounds);
#elif defined(__ANDROID__)
    /* On Android, bounds are managed by the system */
    (void) adapter;
    (void) window;
#endif
}

void window_state_init(struct window_state *state) {
    state->focus = TAB_ORDER[currentFocus];
    state->announcement = NULL;
    state->mutex = SDL_CreateMutex();
}

void window_state_destroy(struct window_state *state) {
    SDL_DestroyMutex(state->mutex);
}

void window_state_lock(struct window_state *state) {
    SDL_LockMutex(state->mutex);
}

void window_state_unlock(struct window_state *state) {
    SDL_UnlockMutex(state->mutex);
}

accesskit_node *window_state_build_root(const struct window_state *state) {
    accesskit_node *node = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);

    accesskit_node_push_child(node, LABEL_ID);
    accesskit_node_push_child(node, BUTTON_1_ID);
    accesskit_node_push_child(node, BUTTON_2_ID);

    if (state->announcement != NULL) {
        accesskit_node_push_child(node, ANNOUNCEMENT_ID);
    }
    accesskit_node_set_label(node, WINDOW_TITLE);
    return node;
}

//OK, here we tell the AccessKit what buttons and labels we have,
//and where they are.
//So, essentially, we build the UI twice, first for AccessKit,
//then the corresponding GUI.
//An improved version could have something like an abstract UI element tree.
//And functions to build both the GUI and AccessKit node tree using the abstract tree.
//So that changes in the abstract tree would be reflected both in the GUI
//and the AccessKit.
//This example here is rather clumsy, for to add or change elements in the UI
//you need to make the corresponding changes at two different places.
accesskit_tree_update *window_state_build_initial_tree(
    const struct window_state *state) {
    accesskit_node *root = window_state_build_root(state);
    accesskit_node *button_1 = build_button(BUTTON_1_ID, CAPTION_BUTTON_1);
    accesskit_node *button_2 = build_button(BUTTON_2_ID, CAPTION_BUTTON_2);
    accesskit_node *label = build_label(labeltext.c_str());
    accesskit_tree_update *result = accesskit_tree_update_with_capacity_and_focus(
        (state->announcement != NULL) ? 5 : 4, state->focus);
    accesskit_tree *tree = accesskit_tree_new(WINDOW_ID);
    accesskit_tree_update_set_tree(result, tree);
    accesskit_tree_update_push_node(result, WINDOW_ID, root);
    accesskit_tree_update_push_node(result, BUTTON_1_ID, button_1);
    accesskit_tree_update_push_node(result, BUTTON_2_ID, button_2);
    accesskit_tree_update_push_node(result, LABEL_ID, label);
    if (state->announcement != NULL) {
        accesskit_node *announcement = build_announcement(state->announcement);
        accesskit_tree_update_push_node(result, ANNOUNCEMENT_ID, announcement);
    }
    return result;
}

accesskit_tree_update *build_tree_update_for_button_press(void *userdata) {
    struct window_state *state = static_cast<struct window_state *>(userdata);
    accesskit_node *announcement = build_announcement(state->announcement);
    accesskit_node *root = window_state_build_root(state);
    accesskit_tree_update *update =
            accesskit_tree_update_with_capacity_and_focus(2, state->focus);
    accesskit_tree_update_push_node(update, ANNOUNCEMENT_ID, announcement);
    accesskit_tree_update_push_node(update, WINDOW_ID, root);
    return update;
}

accesskit_tree_update *build_tree_update_for_label_update(void *userdata) {
    struct window_state *state = static_cast<struct window_state *>(userdata);
    accesskit_node *label = build_label(labeltext.c_str());
    accesskit_node *root = window_state_build_root(state);
    accesskit_tree_update *update =
            accesskit_tree_update_with_capacity_and_focus(2, state->focus);
    accesskit_tree_update_push_node(update, WINDOW_ID, root);
    accesskit_tree_update_push_node(update, LABEL_ID, label);
    return update;
}

void window_state_press_button(struct window_state *state,
                               struct accesskit_sdl_adapter *adapter,
                               accesskit_node_id id) {
    if (id == BUTTON_1_ID) {
        appleCounter++;
        if (appleCounter < 2)
            labeltext = "Oh yes, an apple! That is good.";
        else labeltext = std::to_string(appleCounter)+" apples!";
    } else {
        orangeCounter++;
        if (orangeCounter < 2)
            labeltext = "Oranges, it is!";
        else labeltext = "You have picked "+std::to_string(orangeCounter)+" oranges!";
    }
    state->announcement = labeltext.c_str();
    accesskit_sdl_adapter_update_if_active(
        adapter, build_tree_update_for_button_press, state);
}

accesskit_tree_update *build_tree_update_for_focus_update(void *userdata) {
    //struct window_state *state = userdata;
    struct window_state *state = static_cast<struct window_state *>(userdata);
    accesskit_tree_update *update =
            accesskit_tree_update_with_focus(state->focus);
    return update;
}


void window_state_set_focus(struct window_state *state,
                            struct accesskit_sdl_adapter *adapter,
                            accesskit_node_id focus) {
    state->focus = focus;
    accesskit_sdl_adapter_update_if_active(
        adapter, build_tree_update_for_focus_update, state);
}


void do_action(accesskit_action_request *request, void *userdata) {
    struct action_handler_state *state = static_cast<struct action_handler_state *>(userdata);
    SDL_Event event;
    SDL_zero(event);
    event.type = state->event_type;
    event.user.windowID = state->window_id;
    event.user.data1 = (void *) ((uintptr_t) (request->target_node));

    if (request->action == ACCESSKIT_ACTION_FOCUS) {
        event.user.code = SET_FOCUS_MSG;
        SDL_PushEvent(&event);
    } else if (request->action == ACCESSKIT_ACTION_CLICK) {
        //printf("click detected!\n");
        event.user.code = DO_DEFAULT_ACTION_MSG;
        SDL_PushEvent(&event);
    }
    accesskit_action_request_free(request);
}

accesskit_tree_update *build_initial_tree(void *userdata) {
    //struct window_state *state = userdata;
    auto *state = static_cast<struct window_state *>(userdata);
    window_state_lock(state);
    accesskit_tree_update *update = window_state_build_initial_tree(state);
    window_state_unlock(state);
    return update;
}

void deactivate_accessibility(void *userdata) {
    /* There's nothing in the state that depends on whether the adapter
       is active, so there's nothing to do here. */
}

#if defined(__ANDROID__)
/* On Android, we need global state accessible from JNI.
   The adapter is owned by the UI thread (JNI side), not here. */
static struct window_state *g_window_state = NULL;
static struct action_handler_state *g_action_handler_state = NULL;

void *get_window_state(void) { return g_window_state; }
void *get_action_handler_state(void) { return g_action_handler_state; }

/* Called from JNI on UI thread to get and clear the pending update */
accesskit_tree_update *get_pending_update(void) {
    SDL_LockMutex(g_update_mutex);
    accesskit_tree_update *update = NULL;
    if (g_pending_update_factory != NULL) {
        update = g_pending_update_factory(g_pending_update_userdata);
        g_pending_update_factory = NULL;
        g_pending_update_userdata = NULL;
    }
    SDL_UnlockMutex(g_update_mutex);
    return update;
}

/* SDL uses SDL_main on Android */
#define main SDL_main
#endif


//AccessKit logic ends here

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    SDL_SetAppMetadata(WINDOW_TITLE, "1.0", "com.example.hello.SDL3.accesskit.imgui");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    float mainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    //printf("main_scale: %f\n", mainScale);

    int scaledWidth = static_cast<int>(WINDOW_WIDTH * mainScale);
    int scaledHeight = static_cast<int>(WINDOW_HEIGHT * mainScale);
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer(WINDOW_TITLE, scaledWidth, scaledHeight, window_flags, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(renderer, 1);

    //not sure how to correctly handle this.
    //apparently different backends might handle hdpi displays slightly differently?
    //for more info see : https://allyourfaultforever.com/posts/hidpi-imgui-linux/

    float displayScale = SDL_GetWindowDisplayScale(window);
    //printf("display_scale: %f\n", displayScale);

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.BackendFlags = ImGuiBackendFlags_None | ImGuiBackendFlags_RendererHasVtxOffset;

    // Setup Dear ImGui style
    //ImGui::StyleColorsDark();
    ImGui::StyleColorsLight();

    // Setup scaling
    // Unsure if it is better to use mainScale or displayScale here ?!

    uiScale = displayScale;

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(uiScale);
    style.FontScaleDpi = uiScale;

    BUTTON_1_POSITION.x /= uiScale;
    BUTTON_1_POSITION.y /= uiScale;
    BUTTON_1_SIZE.x /= uiScale;
    BUTTON_1_SIZE.y /= uiScale;

    BUTTON_2_POSITION.x /= uiScale;
    BUTTON_2_POSITION.y /= uiScale;
    BUTTON_2_SIZE.x /= uiScale;
    BUTTON_2_SIZE.y /= uiScale;

    LABEL_POSITION.x /= uiScale;
    LABEL_POSITION.y /= uiScale;

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Init AccessKit

    Uint32 window_id = SDL_GetWindowID(window);
    Uint32 user_event = SDL_RegisterEvents(1);
    if (user_event == (Uint32) -1) {
        fprintf(stderr, "Couldn't register user event: (%s)\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    //printf("Registered user event: %u\n", user_event);

    /* Allocate app state on the heap so it persists across callbacks */

    struct app_state *as = (struct app_state *) SDL_malloc(sizeof(struct app_state));
    if (!as) {
        SDL_Log("Couldn't allocate app state");
        return SDL_APP_FAILURE;
    }

    //no idea if ah_state needs to be a separate struct in appstate
    //but I will keep it for now, since I don't know if accesskit has a reason
    //to have action_handler_userdata separate
    as->user_event = user_event;
    as->window_id = window_id;
    as->ah_state.event_type = user_event;
    as->ah_state.window_id = window_id;
    window_state_init(&as->state);

    accesskit_sdl_adapter_init(&as->adapter, window, build_initial_tree, &as->state,
                               do_action, &as->ah_state,
                               deactivate_accessibility, &as->state);

    *appstate = as;

    return SDL_APP_CONTINUE; /* carry on with the program! */
}

void handleButtonClick(void *appstate) {
    struct app_state *as = static_cast<struct app_state *>(appstate);
    window_state_lock(&as->state);
    accesskit_node_id id = as->state.focus;
    window_state_press_button(&as->state, &as->adapter, id);
    window_state_unlock(&as->state);
    labelUpdated = true;
}

void handleLabelUpdated(void *appstate) {
    struct app_state *as = static_cast<struct app_state *>(appstate);
    window_state_lock(&as->state);
    accesskit_sdl_adapter_update_if_active(
        &as->adapter, build_tree_update_for_label_update, &as->state);
    window_state_unlock(&as->state);
    labelUpdated = false;
}


/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    struct app_state *as = static_cast<struct app_state *>(appstate);
    const Uint32 window_id = as->ah_state.window_id;

    //pass on the event for ImGui
    ImGui_ImplSDL3_ProcessEvent(event);

    if (event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST &&
        event->window.windowID == window_id) {
        switch (event->type) {
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                accesskit_sdl_adapter_update_window_focus_state(&as->adapter, true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                accesskit_sdl_adapter_update_window_focus_state(&as->adapter, false);
                break;
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_SHOWN:
                accesskit_sdl_adapter_update_root_window_bounds(&as->adapter, window);
                break;
        }
    } else if (event->type == SDL_EVENT_KEY_DOWN && event->key.windowID == window_id) {

        //here we have hotkeys for navigating the UI,
        //this is independent of the screen reader navigation
        switch (event->key.key) {
            case SDLK_TAB: {
                window_state_lock(&as->state);
                accesskit_node_id new_focus = getNextFocus();
                window_state_set_focus(&as->state, &as->adapter, new_focus);
                window_state_unlock(&as->state);
                break;
            }
            case SDLK_SPACE: {
                handleButtonClick(appstate);
                break;
            }
        }
    } else

        //here we handle custom events sent by the AccessKit stuff
        ////so this will be focus changes and button clicks by the screen reader
        if (event->type == as->user_event && event->user.windowID == window_id) {
            accesskit_node_id target =
                    (accesskit_node_id) ((uintptr_t) (event->user.data1));

            if (target == BUTTON_1_ID || target == BUTTON_2_ID || target == LABEL_ID) {
                window_state_lock(&as->state);
                if (event->user.code == SET_FOCUS_MSG) {
                    //change focus
                    window_state_set_focus(&as->state, &as->adapter, target);
                } else if (event->user.code == DO_DEFAULT_ACTION_MSG) {
                    //click a button
                    //window_state_set_focus(&as->state, &as->adapter, target);
                    handleButtonClick(appstate);
                }
                window_state_unlock(&as->state);
            }
        }

    return SDL_APP_CONTINUE;
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate) {
    struct app_state *as = static_cast<struct app_state *>(appstate);
    const Uint32 window_id = as->ah_state.window_id;

    const ImGuiIO &io = ImGui::GetIO();

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    //For this test we try to get the GUI element positions and sizes
    //based on values we had for the AccessKit UI tree

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(WINDOW_SIZE);
    ImGui::Begin("Hello, world!", nullptr, flags); // Create a window called "Hello, world!" and append into it.

    ImGui::SetCursorPos(LABEL_POSITION);

    accesskit_node_id focused = as->state.focus;

    //we highlight the currently focused element, also when it is not because of mouse hover
    //but because of navigating with TAB, or with whatever hotkeys the screen reader has
    if (focused == LABEL_ID) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
    ImGui::Text("%s", labeltext.c_str());
    if (labelUpdated) {
        //ImGui does not have a concept of setting the text element size
        //also, when the text changes, the size changes
        //so we need to notify the AccessKit to keep the changes in sync
        ImVec2 corner2 = ImGui::GetItemRectMax();
        LABEL_RECT.x1 = corner2.x * uiScale;
        handleLabelUpdated(appstate);
    }
    if (focused == LABEL_ID) ImGui::PopStyleColor(1);

    ImGui::SetCursorPos(BUTTON_1_POSITION);
    if (focused == BUTTON_1_ID)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);

    if (ImGui::Button(CAPTION_BUTTON_1, BUTTON_1_SIZE)) {
        window_state_set_focus(&as->state, &as->adapter, BUTTON_1_ID);
        handleButtonClick(appstate);
    }
    if (focused == BUTTON_1_ID) ImGui::PopStyleColor(1);

    ImGui::SetCursorPos(BUTTON_2_POSITION);
    if (focused == BUTTON_2_ID)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);

    if (ImGui::Button(CAPTION_BUTTON_2, BUTTON_2_SIZE)) {
        window_state_set_focus(&as->state, &as->adapter, BUTTON_2_ID);
        handleButtonClick(appstate);
    }
    if (focused == BUTTON_2_ID) ImGui::PopStyleColor(1);

    ImGui::End();

    // Render the UI
    ImGui::Render();

    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE); /* black, full alpha */
    SDL_RenderClear(renderer); /* start with a blank canvas. */

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer); /* put it all on the screen! */

    return SDL_APP_CONTINUE; /* carry on with the program! */
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    struct app_state *as = static_cast<struct app_state *>(appstate);
    if (as) {
        accesskit_sdl_adapter_destroy(&as->adapter);
        window_state_destroy(&as->state);
        //SDL_free(&as->ah_state);
        SDL_free(as);
    }
    /* SDL will clean up the window/renderer for us. */
}
