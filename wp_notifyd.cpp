#include <fmt/core.h>
#include <libnotify/notify.h>
#include <spdlog/spdlog.h>
#include <wp/wp.h>

class Wireplumber {
  public:
    Wireplumber() {
        spdlog::debug("Initialize Wireplumber");
        wp_init(WP_INIT_PIPEWIRE);

        _wp_core = wp_core_new(NULL, NULL);
        _wp_plugins = g_ptr_array_new_with_free_func(g_object_unref);
        _wp_object_manager = wp_object_manager_new();

        wp_manager_declare_interest();
        wp_load_plugins();
        wp_connect();

        g_signal_connect_swapped(_wp_object_manager, "installed", (GCallback)on_object_manager_installed, this);
        // activate plugins, when all are active calls wp_core_install_object_manager
        activate_plugins();
    }
    ~Wireplumber() {
        g_object_unref(_wp_object_manager);
        g_ptr_array_free(_wp_plugins, TRUE);
        wp_core_disconnect(_wp_core);
        g_object_unref(_wp_core);
    }

  private:
    void wp_manager_declare_interest() {
        spdlog::debug("Declare interest in sinks and sources");
        wp_object_manager_add_interest(_wp_object_manager, WP_TYPE_NODE, WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class",
                                       "=s", "Audio/Sink", NULL);
    }
    void wp_load_plugins() {
        spdlog::debug("Loading wp defaults and mixer plugin");
        g_autoptr(GError) error = NULL;

        if (!wp_core_load_component(_wp_core, "libwireplumber-module-default-nodes-api", "module", NULL, &error)) {
            spdlog::error(error->message);
            throw std::runtime_error(error->message);
        }

        if (!wp_core_load_component(_wp_core, "libwireplumber-module-mixer-api", "module", NULL, &error)) {
            spdlog::error(error->message);
            throw std::runtime_error(error->message);
        }

        WpPlugin* defaults = wp_plugin_find(_wp_core, "default-nodes-api");
        g_ptr_array_add(_wp_plugins, defaults);
        WpPlugin* mixer = wp_plugin_find(_wp_core, "mixer-api");
        /* cubic scaling */
        g_object_set(G_OBJECT(mixer), "scale", 1, NULL);
        g_ptr_array_add(_wp_plugins, mixer);
    }
    void wp_connect() {
        spdlog::debug("Connecting Wireplumber core to PipeWire...");

        if (!wp_core_connect(_wp_core)) {
            spdlog::error("Could not connect to PipeWire");
            throw std::runtime_error("Could not connect to PipeWire\n");
        }

        spdlog::debug("Connected!");
    }
    void activate_plugins() {
        spdlog::debug("Activating plugins");
        for (guint i = 0; i < _wp_plugins->len; i++) {
            WpPlugin* plugin = static_cast<WpPlugin*>(g_ptr_array_index(_wp_plugins, i));
            wp_object_activate(WP_OBJECT(plugin), WP_PLUGIN_FEATURE_ENABLED, NULL,
                               (GAsyncReadyCallback)on_plugin_activated, this);
        }
    }
    /* Must be static to be used as callback */
    static void on_object_manager_installed(Wireplumber* self) {
        spdlog::debug("Callback: on_object_manager_installed");
    }
    static void on_plugin_activated(WpObject* plugin, GAsyncResult* result, Wireplumber* self) {
        auto plugin_name = wp_plugin_get_name(WP_PLUGIN(plugin));
        spdlog::debug("Callback: on_plugin_activated: {}", plugin_name);

        g_autoptr(GError) error = NULL;
        if (!wp_object_activate_finish(plugin, result, &error)) {
            spdlog::error("Error activating plugin {}: {}", plugin_name, error->message);
            throw std::runtime_error(error->message);
        }

        if (++self->plugins_activated == self->_wp_plugins->len) {
            spdlog::debug("Installing object manager in wp core");
            wp_core_install_object_manager(self->_wp_core, self->_wp_object_manager);
        }
    }

  private:
    WpCore* _wp_core;
    GPtrArray* _wp_plugins;
    WpObjectManager* _wp_object_manager;
    guint plugins_activated = 0;
};

int main(int argc, char** argv) {
    if (argc != 1) {
        fmt::format("{} takes no arguments.\n", argv[0]);
        return 1;
    }
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Set log level to debug");

    Wireplumber wp;

    GMainContext* context= g_main_context_default();
    GMainLoop* loop = g_main_loop_new(context, false);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    g_main_context_unref(context);

    return 0;
}
