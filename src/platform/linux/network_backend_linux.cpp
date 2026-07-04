#include "network_backend_linux.h"

#include "../../console_log.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <NetworkManager.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace wifigd {

namespace {

constexpr int kScanTimeoutSec = 15;
constexpr int kConnectTimeoutSec = 30;
constexpr int kConnectPollIntervalMs = 200;
constexpr const char *kWifiGdConnectionId = "WifiGD Connection";

godot::String gbytes_to_ssid(GBytes *ssid_bytes) {
	if (ssid_bytes == nullptr) {
		return godot::String();
	}
	gsize len = 0;
	const guint8 *data = static_cast<const guint8 *>(g_bytes_get_data(ssid_bytes, &len));
	if (data == nullptr || len == 0) {
		return godot::String();
	}
	return godot::String::utf8(reinterpret_cast<const char *>(data), (int64_t)len);
}

godot::String bssid_to_string(const char *bssid) {
	return bssid != nullptr ? godot::String::utf8(bssid) : godot::String();
}

void frequency_to_band_and_channel(guint32 frequency_mhz, int &channel, godot::String &band) {
	channel = 0;
	band = "";

	if (frequency_mhz == 0) {
		return;
	}

	if (frequency_mhz >= 5925) {
		band = "6";
	} else if (frequency_mhz >= 4900) {
		band = "5";
	} else {
		band = "2.4";
	}

	if (frequency_mhz >= 2412 && frequency_mhz <= 2484) {
		if (frequency_mhz == 2484) {
			channel = 14;
		} else {
			channel = (int)((frequency_mhz - 2412) / 5) + 1;
		}
	} else if (frequency_mhz >= 5170 && frequency_mhz <= 5825) {
		channel = (int)((frequency_mhz - 5000) / 5);
	} else if (frequency_mhz >= 5955 && frequency_mhz <= 7115) {
		channel = (int)((frequency_mhz - 5950) / 5);
	}
}

godot::String security_type_from_ap(NMAccessPoint *ap) {
	if (ap == nullptr) {
		return "unknown";
	}

	const NM80211ApFlags flags = nm_access_point_get_flags(ap);
	const NM80211ApSecurityFlags wpa_flags = nm_access_point_get_wpa_flags(ap);
	const NM80211ApSecurityFlags rsn_flags = nm_access_point_get_rsn_flags(ap);

	if ((flags & NM_802_11_AP_FLAGS_PRIVACY) == 0) {
		return "open";
	}

	if (rsn_flags & (NM_802_11_AP_SEC_KEY_MGMT_SAE)) {
		return "wpa3";
	}

	if (rsn_flags & (NM_802_11_AP_SEC_KEY_MGMT_PSK)) {
		return "wpa2";
	}

	if (wpa_flags & (NM_802_11_AP_SEC_KEY_MGMT_PSK)) {
		return "wpa";
	}

	if (flags & NM_802_11_AP_FLAGS_WPS) {
		return "wps";
	}

	return "secured";
}

bool is_ap_secured(NMAccessPoint *ap) {
	const godot::String security = security_type_from_ap(ap);
	return security != "open";
}

ConnectionState map_device_state(NMDeviceState state) {
	switch (state) {
		case NM_DEVICE_STATE_ACTIVATED:
			return ConnectionState::CONNECTED;
		case NM_DEVICE_STATE_PREPARE:
		case NM_DEVICE_STATE_CONFIG:
		case NM_DEVICE_STATE_NEED_AUTH:
		case NM_DEVICE_STATE_IP_CONFIG:
		case NM_DEVICE_STATE_IP_CHECK:
		case NM_DEVICE_STATE_SECONDARIES:
			return ConnectionState::CONNECTING;
		case NM_DEVICE_STATE_FAILED:
			return ConnectionState::FAILED;
		default:
			return ConnectionState::DISCONNECTED;
	}
}

godot::String describe_nm_error(GError *err) {
	if (err == nullptr) {
		return godot::String();
	}

	const godot::String message = godot::String::utf8(err->message);
	if (g_strstr_len(err->message, -1, "org.freedesktop.NetworkManager.PermissionDenied") != nullptr ||
			g_strstr_len(err->message, -1, "Not authorized") != nullptr ||
			err->code == G_DBUS_ERROR_ACCESS_DENIED) {
		return "Permission denied. Approve the system dialog or configure polkit for your user.";
	}
	if (g_strstr_len(err->message, -1, "Secrets were required") != nullptr ||
			g_strstr_len(err->message, -1, "802-11-wireless-security") != nullptr) {
		return "Authentication failed. Check the password.";
	}
	if (g_strstr_len(err->message, -1, "No network with SSID") != nullptr) {
		return "Network not found. Scan again and retry.";
	}
	return message;
}

void log_gerror(const godot::String &operation, GError *err) {
	if (err == nullptr) {
		return;
	}
	log_to_console("[WifiGD] " + operation + ": " + godot::String::utf8(err->message));
	if (err->code != 0) {
		log_to_console("  GError domain=" + godot::String::num_int64((int64_t)err->domain) +
				" code=" + godot::String::num_int64((int64_t)err->code));
	}
}

struct AsyncWaitContext {
	GMainContext *context = nullptr;
	GMainLoop *loop = nullptr;
	GError *error = nullptr;
	bool success = false;
	NMActiveConnection *active = nullptr;
};

// libnm async D-Bus calls must run on a thread with an acquired GMainContext.
struct NmThreadSession {
	GMainContext *context = nullptr;
	NMClient *client = nullptr;
	GError *init_error = nullptr;

	NmThreadSession() {
		context = g_main_context_new();
		if (context == nullptr) {
			return;
		}
		if (!g_main_context_acquire(context)) {
			g_main_context_unref(context);
			context = nullptr;
			return;
		}
		g_main_context_push_thread_default(context);
		client = nm_client_new(nullptr, &init_error);
	}

	bool is_ready() const {
		return context != nullptr && client != nullptr;
	}

	~NmThreadSession() {
		if (client != nullptr) {
			g_object_unref(client);
			client = nullptr;
		}
		if (context != nullptr) {
			g_main_context_pop_thread_default(context);
			g_main_context_release(context);
			g_main_context_unref(context);
			context = nullptr;
		}
		if (init_error != nullptr) {
			g_error_free(init_error);
			init_error = nullptr;
		}
	}
};

void activate_ready_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
	AsyncWaitContext *ctx = static_cast<AsyncWaitContext *>(user_data);
	NMClient *client = NM_CLIENT(source);
	GVariant *out_result = nullptr;
	ctx->active = nm_client_add_and_activate_connection2_finish(client, result, &out_result, &ctx->error);
	if (out_result != nullptr) {
		g_variant_unref(out_result);
	}
	ctx->success = ctx->active != nullptr && ctx->error == nullptr;
	if (ctx->loop != nullptr) {
		g_main_loop_quit(ctx->loop);
	}
}

void activate_existing_ready_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
	AsyncWaitContext *ctx = static_cast<AsyncWaitContext *>(user_data);
	NMClient *client = NM_CLIENT(source);
	ctx->active = nm_client_activate_connection_finish(client, result, &ctx->error);
	ctx->success = ctx->active != nullptr && ctx->error == nullptr;
	if (ctx->loop != nullptr) {
		g_main_loop_quit(ctx->loop);
	}
}

bool delete_remote_connection(NMRemoteConnection *remote) {
	if (remote == nullptr) {
		return true;
	}

	GError *error = nullptr;
	const gboolean ok = nm_remote_connection_delete(remote, nullptr, &error);
	if (!ok) {
		log_gerror("Delete connection profile", error);
		if (error != nullptr) {
			g_error_free(error);
		}
		return false;
	}
	return true;
}

bool connection_id_is_wifigd_legacy(const char *id) {
	return id != nullptr && g_strcmp0(id, kWifiGdConnectionId) == 0;
}

void delete_wifigd_saved_connections(NMClient *client) {
	if (client == nullptr) {
		return;
	}

	// Remove legacy profiles created before we switched to temporary/in-memory IDs.
	for (int attempt = 0; attempt < 8; attempt++) {
		NMRemoteConnection *remote = nm_client_get_connection_by_id(client, kWifiGdConnectionId);
		if (remote == nullptr) {
			break;
		}
		if (!delete_remote_connection(remote)) {
			g_object_unref(remote);
			break;
		}
		g_object_unref(remote);
	}

	const GPtrArray *connections = nm_client_get_connections(client);
	if (connections == nullptr) {
		return;
	}

	for (guint i = 0; i < connections->len; i++) {
		NMRemoteConnection *remote = NM_REMOTE_CONNECTION(g_ptr_array_index(connections, i));
		if (remote == nullptr) {
			continue;
		}

		NMSettingConnection *setting = nm_connection_get_setting_connection(NM_CONNECTION(remote));
		if (setting == nullptr) {
			continue;
		}

		const char *id = nm_setting_connection_get_id(setting);
		if (!connection_id_is_wifigd_legacy(id)) {
			continue;
		}

		delete_remote_connection(remote);
	}
}

bool should_delete_connection_profile(NMRemoteConnection *remote) {
	if (remote == nullptr) {
		return false;
	}

	NMSettingConnection *setting = nm_connection_get_setting_connection(NM_CONNECTION(remote));
	if (setting == nullptr) {
		return false;
	}

	const char *id = nm_setting_connection_get_id(setting);
	return connection_id_is_wifigd_legacy(id);
}

void pump_main_context(GMainContext *context) {
	if (context == nullptr) {
		return;
	}
	while (g_main_context_pending(context)) {
		g_main_context_iteration(context, FALSE);
	}
}

bool wait_for_device_disconnected(GMainContext *context, NMDevice *device, int timeout_sec) {
	const gint64 deadline = g_get_monotonic_time() + (gint64)timeout_sec * G_TIME_SPAN_SECOND;
	while (g_get_monotonic_time() < deadline) {
		pump_main_context(context);

		const NMDeviceState state = nm_device_get_state(device);
		if (state == NM_DEVICE_STATE_DISCONNECTED || state == NM_DEVICE_STATE_UNAVAILABLE) {
			return true;
		}
		if (nm_device_get_active_connection(device) == nullptr && state != NM_DEVICE_STATE_ACTIVATED) {
			return true;
		}
		g_usleep((gulong)kConnectPollIntervalMs * 1000);
	}

	pump_main_context(context);
	return nm_device_get_active_connection(device) == nullptr &&
			nm_device_get_state(device) != NM_DEVICE_STATE_ACTIVATED;
}

bool deactivate_wifi_connection(NMClient *client, GMainContext *context, NMDevice *device) {
	if (client == nullptr || device == nullptr) {
		return false;
	}

	NMActiveConnection *active = nm_device_get_active_connection(device);
	if (active == nullptr) {
		return true;
	}

	NMRemoteConnection *remote = nm_active_connection_get_connection(active);
	if (remote != nullptr) {
		g_object_ref(remote);
	}

	GError *error = nullptr;
	gboolean ok = nm_client_deactivate_connection(client, active, nullptr, &error);
	if (!ok) {
		log_gerror("Deactivate Wi-Fi connection", error);
		if (error != nullptr) {
			g_error_free(error);
		}

		error = nullptr;
		ok = nm_device_disconnect(device, nullptr, &error);
		if (!ok) {
			log_gerror("Disconnect Wi-Fi device", error);
			if (error != nullptr) {
				g_error_free(error);
			}
			if (remote != nullptr) {
				g_object_unref(remote);
			}
			return false;
		}
	}

	if (remote != nullptr) {
		if (should_delete_connection_profile(remote)) {
			delete_remote_connection(remote);
		}
		g_object_unref(remote);
	}

	return wait_for_device_disconnected(context, device, kConnectTimeoutSec);
}

bool run_main_loop_until(GMainContext *context, GMainLoop *loop, int timeout_sec) {
	const gint64 deadline = g_get_monotonic_time() + (gint64)timeout_sec * G_TIME_SPAN_SECOND;
	while (g_main_loop_is_running(loop)) {
		if (!g_main_context_iteration(context, TRUE)) {
			break;
		}
		if (g_get_monotonic_time() >= deadline) {
			g_main_loop_quit(loop);
			return false;
		}
	}
	return true;
}

NMDeviceWifi *resolve_wifi_device_on(NMClient *nm_client, const godot::String &adapter_id, godot::String &error_out) {
	if (nm_client == nullptr) {
		error_out = "NetworkManager is not available.";
		return nullptr;
	}

	const godot::CharString adapter_utf8 = adapter_id.utf8();
	const char *iface_filter = adapter_utf8.length() > 0 ? adapter_utf8.get_data() : nullptr;

	if (iface_filter != nullptr) {
		NMDevice *device = nm_client_get_device_by_iface(nm_client, iface_filter);
		if (device == nullptr) {
			error_out = godot::vformat("Wi-Fi adapter '%s' was not found.", adapter_id);
			return nullptr;
		}
		if (!NM_IS_DEVICE_WIFI(device)) {
			error_out = godot::vformat("Adapter '%s' is not a Wi-Fi device.", adapter_id);
			return nullptr;
		}
		return NM_DEVICE_WIFI(device);
	}

	const GPtrArray *devices = nm_client_get_devices(nm_client);
	if (devices == nullptr) {
		error_out = "No network devices found.";
		return nullptr;
	}

	for (guint i = 0; i < devices->len; i++) {
		NMDevice *device = static_cast<NMDevice *>(g_ptr_array_index(devices, i));
		if (device != nullptr && NM_IS_DEVICE_WIFI(device)) {
			return NM_DEVICE_WIFI(device);
		}
	}

	error_out = "No Wi-Fi adapter found.";
	return nullptr;
}

bool ssid_matches_gbytes(const godot::String &ssid, GBytes *ssid_bytes) {
	return !ssid.is_empty() && ssid == gbytes_to_ssid(ssid_bytes);
}

NMRemoteConnection *find_saved_connection_by_ssid(NMClient *nm_client, const godot::String &ssid) {
	if (nm_client == nullptr || ssid.is_empty()) {
		return nullptr;
	}

	const GPtrArray *connections = nm_client_get_connections(nm_client);
	if (connections == nullptr) {
		return nullptr;
	}

	for (guint i = 0; i < connections->len; i++) {
		NMRemoteConnection *remote = NM_REMOTE_CONNECTION(g_ptr_array_index(connections, i));
		if (remote == nullptr) {
			continue;
		}

		NMSettingWireless *wireless = nm_connection_get_setting_wireless(NM_CONNECTION(remote));
		if (wireless == nullptr) {
			continue;
		}

		if (ssid_matches_gbytes(ssid, nm_setting_wireless_get_ssid(wireless))) {
			return NM_REMOTE_CONNECTION(g_object_ref(remote));
		}
	}

	return nullptr;
}

bool wait_for_device_activated(GMainContext *context, NMDevice *device, int timeout_sec) {
	const gint64 deadline = g_get_monotonic_time() + (gint64)timeout_sec * G_TIME_SPAN_SECOND;
	while (g_get_monotonic_time() < deadline) {
		pump_main_context(context);

		const NMDeviceState state = nm_device_get_state(device);
		if (state == NM_DEVICE_STATE_ACTIVATED) {
			return true;
		}
		if (state == NM_DEVICE_STATE_FAILED) {
			return false;
		}
		g_usleep((gulong)kConnectPollIntervalMs * 1000);
	}
	return false;
}

bool finish_activate_attempt(AsyncWaitContext &ctx, NMDevice *device, godot::String &error_out) {
	if (!ctx.success) {
		log_gerror("Connect to Wi-Fi", ctx.error);
		error_out = ctx.error != nullptr ? describe_nm_error(ctx.error) : "Connection timed out.";
		if (ctx.active != nullptr) {
			g_object_unref(ctx.active);
			ctx.active = nullptr;
		}
		if (ctx.error != nullptr) {
			g_error_free(ctx.error);
			ctx.error = nullptr;
		}
		return false;
	}

	if (ctx.error != nullptr) {
		g_error_free(ctx.error);
		ctx.error = nullptr;
	}

	if (!wait_for_device_activated(ctx.context, device, kConnectTimeoutSec)) {
		if (ctx.active != nullptr) {
			g_object_unref(ctx.active);
			ctx.active = nullptr;
		}
		error_out = "Connection timed out before the network became active.";
		return false;
	}

	if (ctx.active != nullptr) {
		g_object_unref(ctx.active);
		ctx.active = nullptr;
	}
	return true;
}

godot::String ip_config_address(NMIPConfig *ip_config) {
	if (ip_config == nullptr) {
		return godot::String();
	}

	const GPtrArray *addresses = nm_ip_config_get_addresses(ip_config);
	if (addresses == nullptr || addresses->len == 0) {
		return godot::String();
	}

	for (guint i = 0; i < addresses->len; i++) {
		NMIPAddress *addr = static_cast<NMIPAddress *>(g_ptr_array_index(addresses, i));
		if (addr == nullptr) {
			continue;
		}
		const int family = nm_ip_address_get_family(addr);
		if (family == AF_INET) {
			return godot::String::utf8(nm_ip_address_get_address(addr));
		}
	}
	return godot::String();
}

godot::String ip_config_gateway(NMIPConfig *ip_config) {
	if (ip_config == nullptr) {
		return godot::String();
	}
	const char *gateway = nm_ip_config_get_gateway(ip_config);
	return gateway != nullptr ? godot::String::utf8(gateway) : godot::String();
}

godot::String ip_config_dns(NMIPConfig *ip_config) {
	if (ip_config == nullptr) {
		return godot::String();
	}
	const char *const *dns = nm_ip_config_get_nameservers(ip_config);
	if (dns == nullptr || dns[0] == nullptr) {
		return godot::String();
	}
	return godot::String::utf8(dns[0]);
}

void fill_ip_info_from_device(NMDevice *device, godot::String &ip, godot::String &gateway, godot::String &dns) {
	NMIPConfig *ip4 = nm_device_get_ip4_config(device);
	ip = ip_config_address(ip4);
	gateway = ip_config_gateway(ip4);
	dns = ip_config_dns(ip4);
}

godot::String active_ssid_for_device(NMDevice *device) {
	NMActiveConnection *active = nm_device_get_active_connection(device);
	if (active == nullptr) {
		return godot::String();
	}

	NMRemoteConnection *remote = nm_active_connection_get_connection(active);
	if (remote == nullptr) {
		return godot::String();
	}

	NMSettingWireless *wireless = nm_connection_get_setting_wireless(NM_CONNECTION(remote));
	if (wireless == nullptr) {
		return godot::String();
	}

	return gbytes_to_ssid(nm_setting_wireless_get_ssid(wireless));
}

WifiNetwork access_point_to_network(NMAccessPoint *ap, NMDeviceWifi *device, NMAccessPoint *active_ap) {
	WifiNetwork network;
	if (ap == nullptr || device == nullptr) {
		return network;
	}

	network.ssid = gbytes_to_ssid(nm_access_point_get_ssid(ap));
	network.bssid = bssid_to_string(nm_access_point_get_bssid(ap));
	network.signal_strength = nm_access_point_get_strength(ap);
	network.frequency_mhz = (int)nm_access_point_get_frequency(ap);
	frequency_to_band_and_channel((guint32)network.frequency_mhz, network.channel, network.band);
	network.security_type = security_type_from_ap(ap);
	network.is_secured = is_ap_secured(ap);
	network.adapter_id = godot::String::utf8(nm_device_get_iface(NM_DEVICE(device)));

	if (active_ap != nullptr) {
		const char *active_bssid = nm_access_point_get_bssid(active_ap);
		const char *ap_bssid = nm_access_point_get_bssid(ap);
		if (active_bssid != nullptr && ap_bssid != nullptr && g_strcmp0(active_bssid, ap_bssid) == 0) {
			network.is_connected = true;
			network.security_type = "connected";
		}
	}

	return network;
}

} // namespace

NetworkBackendLinux::NetworkBackendLinux() {
	GError *error = nullptr;
	client = nm_client_new(nullptr, &error);
	if (client == nullptr) {
		if (error != nullptr) {
			log_gerror("NetworkManager init", error);
			last_error = "NetworkManager is not available.";
			g_error_free(error);
		} else {
			last_error = "NetworkManager is not available.";
		}
	}
}

NetworkBackendLinux::~NetworkBackendLinux() {
	if (client != nullptr) {
		g_object_unref(client);
		client = nullptr;
	}
}

void NetworkBackendLinux::set_error(const godot::String &message) {
	last_error = message;
}

bool NetworkBackendLinux::ensure_nm_available() {
	if (client == nullptr) {
		if (last_error.is_empty()) {
			last_error = "NetworkManager is not available.";
		}
		return false;
	}
	return true;
}

NMDeviceWifi *NetworkBackendLinux::resolve_wifi_device(const godot::String &adapter_id) {
	if (!ensure_nm_available()) {
		return nullptr;
	}

	const godot::CharString adapter_utf8 = adapter_id.utf8();
	const char *iface_filter = adapter_utf8.length() > 0 ? adapter_utf8.get_data() : nullptr;

	if (iface_filter != nullptr) {
		NMDevice *device = nm_client_get_device_by_iface(client, iface_filter);
		if (device == nullptr) {
			set_error(godot::vformat("Wi-Fi adapter '%s' was not found.", adapter_id));
			return nullptr;
		}
		if (!NM_IS_DEVICE_WIFI(device)) {
			set_error(godot::vformat("Adapter '%s' is not a Wi-Fi device.", adapter_id));
			return nullptr;
		}
		return NM_DEVICE_WIFI(device);
	}

	const GPtrArray *devices = nm_client_get_devices(client);
	if (devices == nullptr) {
		set_error("No network devices found.");
		return nullptr;
	}

	for (guint i = 0; i < devices->len; i++) {
		NMDevice *device = static_cast<NMDevice *>(g_ptr_array_index(devices, i));
		if (device != nullptr && NM_IS_DEVICE_WIFI(device)) {
			return NM_DEVICE_WIFI(device);
		}
	}

	set_error("No Wi-Fi adapter found.");
	return nullptr;
}

bool NetworkBackendLinux::is_wifi_enabled() {
	if (!ensure_nm_available()) {
		return false;
	}
	last_error = "";
	return nm_client_wireless_get_enabled(client) != FALSE;
}

bool NetworkBackendLinux::set_wifi_enabled(bool enabled) {
	if (!ensure_nm_available()) {
		return false;
	}

	nm_client_wireless_set_enabled(client, enabled ? TRUE : FALSE);

	for (int attempt = 0; attempt < 10; attempt++) {
		if (nm_client_wireless_get_enabled(client) == (enabled ? TRUE : FALSE)) {
			last_error = "";
			return true;
		}
		g_usleep(100 * 1000);
	}

	set_error("Permission denied. Approve the system dialog or configure polkit for your user.");
	return false;
}

std::vector<NetworkAdapter> NetworkBackendLinux::get_network_adapters() {
	std::vector<NetworkAdapter> adapters;
	if (!ensure_nm_available()) {
		return adapters;
	}

	const GPtrArray *devices = nm_client_get_devices(client);
	if (devices == nullptr) {
		set_error("No network devices found.");
		return adapters;
	}

	last_error = "";
	for (guint i = 0; i < devices->len; i++) {
		NMDevice *device = static_cast<NMDevice *>(g_ptr_array_index(devices, i));
		if (device == nullptr) {
			continue;
		}

		NetworkAdapter adapter;
		adapter.id = godot::String::utf8(nm_device_get_iface(device));
		adapter.name = adapter.id;

		const char *product = nm_device_get_product(device);
		const char *driver = nm_device_get_driver(device);
		if (product != nullptr && product[0] != '\0') {
			adapter.description = godot::String::utf8(product);
		} else if (driver != nullptr) {
			adapter.description = godot::String::utf8(driver);
		}

		const char *hw_addr = nm_device_get_hw_address(device);
		if (hw_addr != nullptr) {
			adapter.mac_address = godot::String::utf8(hw_addr);
		}

		fill_ip_info_from_device(device, adapter.ip_address, adapter.gateway, adapter.dns_primary);

		const NMDeviceState state = nm_device_get_state(device);
		adapter.is_up = state != NM_DEVICE_STATE_UNMANAGED && state != NM_DEVICE_STATE_UNAVAILABLE;
		adapter.is_connected = state == NM_DEVICE_STATE_ACTIVATED;

		if (NM_IS_DEVICE_WIFI(device)) {
			adapter.type = AdapterType::WIFI;
		} else if (NM_IS_DEVICE_ETHERNET(device)) {
			adapter.type = AdapterType::ETHERNET;
		} else {
			adapter.type = AdapterType::OTHER;
		}

		adapters.push_back(adapter);
	}

	if (adapters.empty()) {
		set_error("No network adapters found.");
	}

	return adapters;
}

ConnectivityInfo NetworkBackendLinux::get_connectivity_info() {
	ConnectivityInfo info;
	if (!ensure_nm_available()) {
		return info;
	}

	NMDeviceWifi *wifi = resolve_wifi_device("");
	if (wifi == nullptr) {
		return info;
	}

	NMDevice *device = NM_DEVICE(wifi);
	const NMDeviceState state = nm_device_get_state(device);
	info.state = map_device_state(state);
	info.is_wifi_connected = state == NM_DEVICE_STATE_ACTIVATED;
	info.connected_ssid = active_ssid_for_device(device);
	fill_ip_info_from_device(device, info.local_ip, info.gateway, info.dns_primary);
	info.has_internet = info.is_wifi_connected && !info.local_ip.is_empty();
	last_error = "";
	return info;
}

std::vector<WifiNetwork> NetworkBackendLinux::scan_wifi_networks(const godot::String &adapter_id) {
	std::vector<WifiNetwork> networks;
	if (!ensure_nm_available()) {
		return networks;
	}

	NMDeviceWifi *wifi = resolve_wifi_device(adapter_id);
	if (wifi == nullptr) {
		return networks;
	}

	if (!nm_client_wireless_get_enabled(client)) {
		set_error("Wi-Fi radio is disabled.");
		return networks;
	}

	GError *scan_error = nullptr;
	const gboolean scan_ok = nm_device_wifi_request_scan(wifi, nullptr, &scan_error);
	if (!scan_ok) {
		log_gerror("Wi-Fi scan", scan_error);
		set_error(scan_error != nullptr ? describe_nm_error(scan_error) : "Wi-Fi scan failed.");
		if (scan_error != nullptr) {
			g_error_free(scan_error);
		}
		return networks;
	}

	g_usleep(1500 * 1000);

	const GPtrArray *access_points = nm_device_wifi_get_access_points(wifi);
	NMAccessPoint *active_ap = nm_device_wifi_get_active_access_point(wifi);
	if (access_points == nullptr || access_points->len == 0) {
		set_error("No Wi-Fi networks found.");
		return networks;
	}

	last_error = "";
	networks.reserve(access_points->len);
	for (guint i = 0; i < access_points->len; i++) {
		NMAccessPoint *ap = static_cast<NMAccessPoint *>(g_ptr_array_index(access_points, i));
		WifiNetwork network = access_point_to_network(ap, wifi, active_ap);
		if (network.ssid.is_empty()) {
			continue;
		}
		networks.push_back(network);
	}

	std::sort(networks.begin(), networks.end(), [](const WifiNetwork &a, const WifiNetwork &b) {
		return a.signal_strength > b.signal_strength;
	});

	if (networks.empty()) {
		set_error("No Wi-Fi networks found.");
	}

	return networks;
}

bool NetworkBackendLinux::connect_to_wifi(
		const godot::String &ssid,
		const godot::String &password,
		const godot::String &adapter_id) {
	if (ssid.is_empty()) {
		set_error("SSID is required.");
		return false;
	}

	// Fast path: already on the requested network (avoids needless disconnect/reconnect).
	{
		godot::String quick_error;
		NmThreadSession quick_session;
		if (quick_session.is_ready()) {
			NMDeviceWifi *wifi = resolve_wifi_device_on(quick_session.client, adapter_id, quick_error);
			if (wifi != nullptr) {
				NMDevice *device = NM_DEVICE(wifi);
				if (nm_device_get_state(device) == NM_DEVICE_STATE_ACTIVATED &&
						active_ssid_for_device(device) == ssid) {
					last_error = "";
					return true;
				}
			}
		}
	}

	NmThreadSession session;
	if (!session.is_ready()) {
		log_gerror("NetworkManager init", session.init_error);
		set_error("NetworkManager is not available.");
		return false;
	}

	godot::String resolve_error;
	NMDeviceWifi *wifi = resolve_wifi_device_on(session.client, adapter_id, resolve_error);
	if (wifi == nullptr) {
		set_error(resolve_error);
		return false;
	}

	NMDevice *device = NM_DEVICE(wifi);
	AsyncWaitContext ctx;
	ctx.context = session.context;
	ctx.loop = g_main_loop_new(session.context, FALSE);

	NMRemoteConnection *saved = find_saved_connection_by_ssid(session.client, ssid);
	if (saved != nullptr) {
		nm_client_activate_connection_async(
				session.client,
				NM_CONNECTION(saved),
				device,
				nullptr,
				nullptr,
				activate_existing_ready_cb,
				&ctx);
		run_main_loop_until(session.context, ctx.loop, kConnectTimeoutSec);
		g_object_unref(saved);
	} else {
		const godot::CharString ssid_utf8 = ssid.utf8();
		const godot::CharString password_utf8 = password.utf8();

		NMConnection *connection = nm_simple_connection_new();

		NMSettingConnection *setting_connection = NM_SETTING_CONNECTION(nm_setting_connection_new());
		g_object_set(
				G_OBJECT(setting_connection),
				NM_SETTING_CONNECTION_ID,
				ssid_utf8.get_data(),
				NM_SETTING_CONNECTION_TYPE,
				"802-11-wireless",
				NM_SETTING_CONNECTION_AUTOCONNECT,
				FALSE,
				nullptr);
		nm_connection_add_setting(connection, NM_SETTING(setting_connection));

		GBytes *ssid_bytes = g_bytes_new(ssid_utf8.get_data(), ssid_utf8.length());
		NMSettingWireless *setting_wireless = NM_SETTING_WIRELESS(nm_setting_wireless_new());
		g_object_set(
				G_OBJECT(setting_wireless),
				NM_SETTING_WIRELESS_SSID,
				ssid_bytes,
				NM_SETTING_WIRELESS_MODE,
				"infrastructure",
				nullptr);
		g_bytes_unref(ssid_bytes);
		nm_connection_add_setting(connection, NM_SETTING(setting_wireless));

		NMSettingIP4Config *setting_ip4 = NM_SETTING_IP4_CONFIG(nm_setting_ip4_config_new());
		g_object_set(G_OBJECT(setting_ip4), NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO, nullptr);
		nm_connection_add_setting(connection, NM_SETTING(setting_ip4));

		NMSettingIP6Config *setting_ip6 = NM_SETTING_IP6_CONFIG(nm_setting_ip6_config_new());
		g_object_set(G_OBJECT(setting_ip6), NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_AUTO, nullptr);
		nm_connection_add_setting(connection, NM_SETTING(setting_ip6));

		if (!password.is_empty()) {
			NMSettingWirelessSecurity *setting_security =
					NM_SETTING_WIRELESS_SECURITY(nm_setting_wireless_security_new());
			g_object_set(
					G_OBJECT(setting_security),
					NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
					"wpa-psk",
					NM_SETTING_WIRELESS_SECURITY_PSK,
					password_utf8.get_data(),
					nullptr);
			nm_connection_add_setting(connection, NM_SETTING(setting_security));
		}

		// Temporary profiles are removed automatically when deactivated (no "WifiGD Connection" left behind).
		GVariant *options = g_variant_new_parsed("@a{sv} { 'persist': <'temporary'> }");
		nm_client_add_and_activate_connection2(
				session.client,
				connection,
				device,
				nullptr,
				options,
				nullptr,
				activate_ready_cb,
				&ctx);
		g_variant_unref(options);
		run_main_loop_until(session.context, ctx.loop, kConnectTimeoutSec);
		g_object_unref(connection);
	}

	g_main_loop_unref(ctx.loop);
	ctx.loop = nullptr;

	godot::String connect_error;
	if (!finish_activate_attempt(ctx, device, connect_error)) {
		set_error(connect_error);
		return false;
	}

	last_error = "";
	return true;
}

bool NetworkBackendLinux::disconnect_from_wifi(const godot::String &adapter_id) {
	NmThreadSession session;
	if (!session.is_ready()) {
		log_gerror("NetworkManager init", session.init_error);
		set_error("NetworkManager is not available.");
		return false;
	}

	godot::String resolve_error;
	NMDeviceWifi *wifi = resolve_wifi_device_on(session.client, adapter_id, resolve_error);
	if (wifi == nullptr) {
		set_error(resolve_error);
		return false;
	}

	if (!deactivate_wifi_connection(session.client, session.context, NM_DEVICE(wifi))) {
		set_error("Failed to disconnect from Wi-Fi.");
		return false;
	}

	delete_wifigd_saved_connections(session.client);
	last_error = "";
	return true;
}

godot::String NetworkBackendLinux::get_last_error() const {
	return last_error;
}

} // namespace wifigd