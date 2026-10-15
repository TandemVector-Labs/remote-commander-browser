#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// A native, read-only observation contract. No browser JavaScript, network,
// command execution, account cookies, or credentials are accepted by this API.
namespace companion {
constexpr size_t kMaximumSnapshotBytes = 65536;
constexpr size_t kMaximumNativeBindingBytes = 32768;
constexpr size_t kMaximumMonitorBytes = 65536;
constexpr uint64_t kMaximumLifetimeMs = 300000;

struct Binding {
  std::string app_id, account_id, profile_id, workflow_id, project_root, chat_url;
};

struct Observation {
  bool valid = false;
  bool blocked = false;
  std::string error = "SNAPSHOT_MISSING";
  uint64_t observed_at = 0, expires_at = 0, route_generation = 0, revision = 0;
  std::string app_id, profile, device_name, version, commit, config_sha256;
  std::string backend_state, tool_catalog_sha256, chat_state, conversation_url;
  std::string chat_app_id, project_state, project_id, project_root, next_action;
  std::string evidence_sha256, reconciliation_state;
  std::vector<std::string> backend_tools, chat_tools, skills, plugins, reasons;
  Binding binding;
};

struct NativeBinding {
  bool valid = false;
  std::string error = "NATIVE_BINDING_MISSING";
  Binding binding;
};

struct Monitor {
  bool valid = false;
  bool blocked = false;
  std::string error = "MONITOR_MISSING";
  uint64_t observed_at = 0, expires_at = 0, port = 0;
  uint64_t workflow_runs = 0, extension_count = 0, active_operations = 0, locked_keys = 0;
  std::string device_name, profile, version, config_sha256, platform;
  bool gui_enabled = false, gui_available = false, gui_uncertain = false;
  std::string gui_backend, gui_session_type, gui_reason;
  bool gui_screenshot = false, gui_mouse = false, gui_keyboard = false, gui_focus = false;
  bool browser_enabled = false, browser_available = false, browser_uncertain = false;
  std::string browser_backend, browser_reason;
  bool workflows_enabled = false, workflow_engine_enabled = false;
  std::vector<std::string> extensions;
};

struct Automation {
  bool valid = false, blocked = false;
  std::string error = "AUTOMATION_MISSING";
  uint64_t observed_at = 0, expires_at = 0;
  std::string config_sha256, local_model, work_credits, credit_usage;
  bool runner_configured = false, automatic_execution = false;
};

struct DisplayRow { std::string text, detail; };

// Pure deterministic parser/validation. Duplicate keys (including escaped
// aliases), unknown keys, invalid UTF-8, excessive depth/nodes and size fail shut.
Observation ParseObservation(const std::string& json);
Monitor ParseMonitor(const std::string& json);
Automation ParseAutomation(const std::string& json);
NativeBinding ParseNativeBinding(const std::string& json);
std::string CatalogSha256(std::vector<std::string> names);
bool IsCanonicalChatUrl(const std::string& url);
bool IsCanonicalProjectRoot(const std::string& root);
bool SameProjectRoot(const std::string& a, const std::string& b);
bool IsFresh(const Observation& observation, uint64_t now_ms);
bool IsFresh(const Monitor& monitor, uint64_t now_ms);
bool IsFresh(const Automation& automation, uint64_t now_ms);
bool ImmutableSnapshotName(const std::string& name, uint64_t& observed_at);
std::vector<DisplayRow> DescribeMonitor(const Monitor& monitor, uint64_t now_ms);
std::vector<DisplayRow> DescribeAutomation(const Automation& automation, const Monitor& monitor, uint64_t now_ms);
std::vector<DisplayRow> Describe(const Observation& observation,
                                 const NativeBinding& expected,
                                 const std::string& active_url,
                                 uint64_t now_ms);

// Linux bounded immutable-candidate reads, newest lexicographic candidate only;
// legacy fixed file only when zero immutable names exist. Same-owner private
// regular files, no links, no symlink path components, identity recheck.
Observation ReadObservation(const std::string& profile_root);
Monitor ReadMonitor(const std::string& profile_root);
Automation ReadAutomation(const std::string& profile_root);
NativeBinding ReadNativeBinding(const std::string& profile_root);
std::string ProfileRoot();
uint64_t NowEpochMs();
}  // namespace companion
