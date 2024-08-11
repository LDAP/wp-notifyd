#include <algorithm>
#include <fmt/core.h>
#include <glib-unix.h>
#include <libnotify/notify.h>
#include <spdlog/spdlog.h>
#include <string>
#include <wp/wp.h>

#define NOTIFICATION_TIMEOUT_MS 1500

template <typename T> class MediaClass {
  public:
    virtual const gchar* get_media_class_name() const = 0;
    virtual const char* get_icon(gdouble volume, gboolean mute) const = 0;
    virtual const char* get_notification_title() const = 0;

    static T* get_instance() {
        if (!instance) {
            instance = new T();
        }
        return instance;
    }

  private:
    inline static T* instance = nullptr;
};

class MediaClassSink : public MediaClass<MediaClassSink> {
  public:
    const gchar* get_media_class_name() const {
        return "Audio/Sink";
    }

    const char* get_icon(gdouble volume, gboolean mute) const {
        if (mute || volume == 0.) {
            return "audio-volume-muted";
        }
        const char* icons[3] = {"audio-volume-low", "audio-volume-medium", "audio-volume-high"};
        return icons[std::clamp((std::size_t)(volume * 3), (std::size_t)0., (std::size_t)2.)];
    }

    const char* get_notification_title() const {
        return "Output Device";
    }
};

class MediaClassSource : public MediaClass<MediaClassSource> {
  public:
    const gchar* get_media_class_name() const {
        return "Audio/Source";
    }

    const char* get_icon(gdouble volume, gboolean mute) const {
        return "audio-input-microphone";
    }

    const char* get_notification_title() const {
        return "Input Device";
    }
};

class Wireplumber {
    template <typename NodeMediaClass = MediaClassSink> class Node {
      public:
        Node(Wireplumber* wp) : wp(wp) {
            notify = notify_notification_new("", "", NULL);
            notify_notification_set_timeout(notify, NOTIFICATION_TIMEOUT_MS);
            notify_notification_set_hint_int32(notify, "transient", 1);
        }

        ~Node() {
            g_free(name);
            g_object_unref(notify);
        }

        void set_id(uint32_t new_id) {
            if (new_id > 0 && new_id <= G_MAXUINT32 && new_id != id) {
                spdlog::debug("Node({}): Update id {} -> {}", id, id, new_id);
                id = new_id;
                update_state();
            }
        }

        void update_state() {
            spdlog::debug("Node({}): Update state", id);
            bool state_changed = false;

            g_autoptr(WpNode) node = static_cast<WpNode*>(wp_object_manager_lookup(
                wp->_wp_object_manager, WP_TYPE_NODE, WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, NULL));
            if (!node) {
                spdlog::debug("Node({}): wp manager did not find any node with this id", id);
                return;
            }

            // Update name
            const gchar* nick = wp_pipewire_object_get_property(WP_PIPEWIRE_OBJECT(node), "node.nick");
            const gchar* desc = wp_pipewire_object_get_property(WP_PIPEWIRE_OBJECT(node), "node.description");
            const gchar* new_name = nick ? nick : desc;
            if (g_strcmp0(name, new_name) != 0) {
                g_free(name);
                name = g_strdup(new_name);
                state_changed = true;
                spdlog::debug("Node({}): Updated name to {}", id, name);
            }

            // Update volume and mute
            g_autoptr(GVariant) variant = nullptr;
            g_signal_emit_by_name(wp->_wp_plugin_mixer, "get-volume", id, &variant);
            if (!variant) {
                auto err = fmt::format("Node({}): could not get volume.", id);
                spdlog::error("{}", err);
                throw std::runtime_error(err);
            }
            gdouble new_volume;
            gboolean new_mute;
            g_variant_lookup(variant, "volume", "d", &new_volume);
            g_variant_lookup(variant, "mute", "b", &new_mute);

            if (volume != new_volume) {
                volume = new_volume;
                spdlog::debug("Node({}): Updated volume {}", id, volume);
                state_changed = true;
            }
            if (mute != new_mute) {
                mute = new_mute;
                spdlog::debug("Node({}): Updated mute {}", id, mute);
                state_changed = true;
            }

            // Show notification if state changed
            if (state_changed && state_valid) {
                show_notification();
            }

            state_valid = true;
        }

        void show_notification() {
            notify_notification_update(notify, NodeMediaClass::get_instance()->get_notification_title(), name,
                                       NodeMediaClass::get_instance()->get_icon(volume, mute));
            notify_notification_set_hint_int32(notify, "value", !mute * (gint)(volume * 100));
            notify_notification_show(notify, NULL);
        }

        uint32_t get_id() {
            return id;
        }

      private:
        Wireplumber* wp;
        NotifyNotification* notify;
        uint32_t id = 0;
        bool state_valid = false;
        gchar* name = nullptr;
        gboolean mute = false;
        gdouble volume = 0;
    };

  public:
    Wireplumber() : _default_sink(this), _default_source(this) {
        spdlog::debug("Initialize Wireplumber");
        wp_init(WP_INIT_PIPEWIRE);

        _wp_core = wp_core_new(nullptr, nullptr, nullptr);
        _wp_object_manager = wp_object_manager_new();

        wp_manager_declare_interest();
        wp_connect();
        g_signal_connect_swapped(_wp_object_manager, "installed", (GCallback)on_object_manager_installed, this);
        wp_load_plugins();
    }
    ~Wireplumber() {
        g_object_unref(_wp_object_manager);
        g_object_unref(_wp_plugin_mixer);
        g_object_unref(_wp_plugin_defaults);
        wp_core_disconnect(_wp_core);
        g_object_unref(_wp_core);
    }

  private:
    void wp_manager_declare_interest() {
        spdlog::debug("Declare interest in sinks and sources");
        wp_object_manager_add_interest(_wp_object_manager, WP_TYPE_NODE, WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class",
                                       "=s", MediaClassSink::get_instance()->get_media_class_name(), NULL);
        wp_object_manager_add_interest(_wp_object_manager, WP_TYPE_NODE, WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class",
                                       "=s", MediaClassSource::get_instance()->get_media_class_name(), NULL);
    }
    void wp_load_plugins() {
        spdlog::debug("Loading wp defaults and mixer plugin");
        g_autoptr(GError) error = NULL;

        wp_core_load_component(_wp_core, "libwireplumber-module-default-nodes-api", "module", NULL, "default-nodes-api",
                               NULL, (GAsyncReadyCallback)on_default_nodes_api_loaded, this);
    }
    static void on_default_nodes_api_loaded(WpObject* p, GAsyncResult* res, Wireplumber* self) {
        spdlog::debug("loading default node api");

        gboolean success = FALSE;
        g_autoptr(GError) error = nullptr;
        success = wp_core_load_component_finish(self->_wp_core, res, &error);

        if (success == FALSE) {
            spdlog::error(error->message);
            throw std::runtime_error(error->message);
        }

        self->_wp_plugin_defaults = wp_plugin_find(self->_wp_core, "default-nodes-api");

        wp_core_load_component(self->_wp_core, "libwireplumber-module-mixer-api", "module", NULL, "mixer-api", NULL,
                               (GAsyncReadyCallback)on_mixer_api_loaded, self);
    }
    static void on_mixer_api_loaded(WpObject* p, GAsyncResult* res, Wireplumber* self) {
        spdlog::debug("loading mixer api");

        gboolean success = FALSE;
        g_autoptr(GError) error = nullptr;
        success = wp_core_load_component_finish(self->_wp_core, res, &error);

        if (success == FALSE) {
            spdlog::error(error->message);
            throw std::runtime_error(error->message);
        }

        self->_wp_plugin_mixer = wp_plugin_find(self->_wp_core, "mixer-api");
        /* cubic scaling */
        g_object_set(G_OBJECT(self->_wp_plugin_mixer), "scale", 1, NULL);

        // activate plugins, when all are active calls wp_core_install_object_manager
        self->activate_plugins();
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
        _on_plugin_activated_pending = 2;
        wp_object_activate(WP_OBJECT(_wp_plugin_defaults), WP_PLUGIN_FEATURE_ENABLED, NULL,
                           (GAsyncReadyCallback)on_plugin_activated, this);
        wp_object_activate(WP_OBJECT(_wp_plugin_mixer), WP_PLUGIN_FEATURE_ENABLED, NULL,
                           (GAsyncReadyCallback)on_plugin_activated, this);
    }
    static void on_plugin_activated(WpObject* plugin, GAsyncResult* result, Wireplumber* self) {
        auto plugin_name = wp_plugin_get_name(WP_PLUGIN(plugin));
        spdlog::debug("Callback: on_plugin_activated: {}", plugin_name);

        g_autoptr(GError) error = NULL;
        if (!wp_object_activate_finish(plugin, result, &error)) {
            spdlog::error("Error activating plugin {}: {}", plugin_name, error->message);
            throw std::runtime_error(error->message);
        }

        if (--self->_on_plugin_activated_pending == 0) {
            spdlog::debug("Installing object manager in wp core");
            wp_core_install_object_manager(self->_wp_core, self->_wp_object_manager);
        }
    }
    /* Must be static to be used as callback */
    static void on_object_manager_installed(Wireplumber* self) {
        spdlog::debug("Callback: on_object_manager_installed");
        g_signal_connect_swapped(self->_wp_plugin_mixer, "changed", (GCallback)on_mixer_changed, self);
        g_signal_connect_swapped(self->_wp_plugin_defaults, "changed", (GCallback)on_defaults_changed, self);

        // get initial default nodes.
        on_defaults_changed(self);
    }
    static void on_defaults_changed(Wireplumber* self) {
        uint32_t new_sink_id, new_source_id;
        g_signal_emit_by_name(self->_wp_plugin_defaults, "get-default-node",
                              MediaClassSink::get_instance()->get_media_class_name(), &new_sink_id);
        g_signal_emit_by_name(self->_wp_plugin_defaults, "get-default-node",
                              MediaClassSource::get_instance()->get_media_class_name(), &new_source_id);
        self->_default_sink.set_id(new_sink_id);
        self->_default_source.set_id(new_source_id);
    }
    static void on_mixer_changed(Wireplumber* self, uint32_t id) {
        if (id == self->_default_sink.get_id()) {
            self->_default_sink.update_state();
        }
        if (id == self->_default_source.get_id()) {
            self->_default_source.update_state();
        }
    }

  private:
    WpCore* _wp_core = nullptr;
    WpObjectManager* _wp_object_manager = nullptr;
    guint _on_plugin_activated_pending = 0;
    WpPlugin* _wp_plugin_defaults = nullptr;
    WpPlugin* _wp_plugin_mixer = nullptr;

    Node<MediaClassSink> _default_sink;
    Node<MediaClassSource> _default_source;
};

Wireplumber* wp;

static void quit_main_loop(GMainLoop* loop) {
    spdlog::info("Caught SIGINT/SIGTERM, shutting down normally.");
    delete wp;
    g_main_loop_quit(loop);
}

void setup_shutdown_signals(GMainLoop* loop) {
    g_unix_signal_add(SIGINT, (GSourceFunc)quit_main_loop, loop);
    g_unix_signal_add(SIGTERM, (GSourceFunc)quit_main_loop, loop);
}

int main(int argc, char** argv) {
    if (argc != 1) {
        fmt::print("{} takes no arguments.\n", argv[0]);
        return 1;
    }

#ifndef DEBUG
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Set log level to debug");
#endif

    notify_init("wp_notifyd");
    wp = new Wireplumber();

    GMainContext* context = g_main_context_default();
    GMainLoop* loop = g_main_loop_new(context, false);

    setup_shutdown_signals(loop);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    g_main_context_unref(context);
    notify_uninit();

    return 0;
}
