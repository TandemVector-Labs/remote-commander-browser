#include "commander_companion.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
int passed = 0, failed = 0;
void Check(bool result, const char* name) {
  if (result) ++passed; else ++failed;
  std::cout << (result ? "PASS " : "FAIL ") << name << '\n';
}
std::string Replace(std::string source, const std::string& before, const std::string& after) {
  const size_t where = source.find(before);
  if (where == std::string::npos) { std::cerr << "TEST_ANCHOR_MISSING\n"; std::exit(2); }
  source.replace(where, before.size(), after); return source;
}
const std::string kBinding = R"({"appId":"app-canonical","accountId":"account-test","profileId":"profile-test","workflowId":"workflow-test","projectRoot":"/workspace/project-test","chatUrl":"https://chatgpt.com/c/chat-test"})";
std::string MonitorSnapshot() {
  return R"({"schema":1,"kind":"COMMANDER_LIVE_MONITOR","observedAtEpochMs":1000,"expiresAtEpochMs":11000,"identity":{"deviceName":"linux-host","profile":"default","version":"0.10.19","configSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","port":48832,"platform":"linux"},"gui":{"enabled":true,"available":true,"uncertain":false,"backend":"gnome-mutter-remote-desktop","sessionType":"wayland","reason":null,"capabilities":{"screenshot":true,"mouse":true,"keyboard":true,"focus":true}},"browser":{"enabled":true,"available":true,"uncertain":false,"backend":"chromium-cdp","reason":null},"workflows":{"enabled":true,"engineEnabled":true,"runCount":2},"extensions":{"count":2,"items":[{"id":"project-execution-brain","version":"1.3.1"},{"id":"remote-commander-linux","version":"1.3.4"}]},"operations":{"active":0,"lockedKeys":0}})";
}

std::string AutomationSnapshot() {
  return R"({"schema":1,"kind":"COMMANDER_AUTOMATION_MONITOR","observedAtEpochMs":1000,"expiresAtEpochMs":11000,"configSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","automation":{"runnerConfigured":true,"automaticExecution":false,"localModel":"PROPOSAL_ONLY","workCredits":"DENY_IN_CORE","creditUsage":"UNVERIFIED"}})";
}

std::string UnknownSnapshot() {
  return R"({"schema":1,"kind":"COMMANDER_COMPANION_OBSERVATION","observedAtEpochMs":1000,"expiresAtEpochMs":2000,"producer":{"id":"remote-commander-companion","version":"1"},"identity":{"appId":null,"profile":"profile-test","deviceName":"renamed-device","version":"0.10.4","commit":null,"configSha256":null,"routeGeneration":null},"backend":{"state":"CONFIRMED","toolNames":["system_status","file_read"],"toolCatalogSha256":"5bb07faadcdc7512789e644acaedcdf9440cf633cc6437f9a774bce5767bf38a"},"currentChat":{"state":"UNKNOWN","conversationUrl":null,"appId":null,"toolNames":[],"skills":[],"plugins":[]},"project":{"state":"UNKNOWN","projectId":"project-test","root":"/workspace/project-test","revision":1,"nextAction":"AWAIT_AUTHORITATIVE_CHAT_OBSERVATION","evidenceSha256":null},"binding":{"appId":null,"accountId":null,"profileId":null,"workflowId":null,"projectRoot":null,"chatUrl":null},"reconciliation":{"state":"UNPROVEN","reasonCodes":["CHAT_OBSERVATION_MISSING"],"actionAllowed":false}})";
}
std::string BoundSnapshot() {
  std::string s = UnknownSnapshot();
  s = Replace(s, R"("identity":{"appId":null)", R"("identity":{"appId":"app-canonical")");
  s = Replace(s, R"("currentChat":{"state":"UNKNOWN","conversationUrl":null,"appId":null,"toolNames":[],"skills":[],"plugins":[]})",
    R"("currentChat":{"state":"CONFIRMED","conversationUrl":"https://chatgpt.com/c/chat-test","appId":"app-canonical","toolNames":["file_read"],"skills":["skill/read"],"plugins":["plugin@app"]})");
  s = Replace(s, R"("project":{"state":"UNKNOWN","projectId":"project-test")", R"("project":{"state":"BOUND","projectId":"workflow-test")");
  s = Replace(s, R"("evidenceSha256":null)", R"("evidenceSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")");
  s = Replace(s, R"({"appId":null,"accountId":null,"profileId":null,"workflowId":null,"projectRoot":null,"chatUrl":null})", kBinding);
  return Replace(s, R"("state":"UNPROVEN","reasonCodes":["CHAT_OBSERVATION_MISSING"])", R"("state":"BOUND","reasonCodes":[])");
}
bool Has(const std::vector<companion::DisplayRow>& rows, const std::string& text) {
  return std::any_of(rows.begin(), rows.end(), [&text](const companion::DisplayRow& row) {
    return row.text.find(text) != std::string::npos || row.detail.find(text) != std::string::npos;
  });
}
std::string ImmutableName(uint64_t time, unsigned serial = 0) {
  const std::string stamp = std::to_string(time), suffix = std::to_string(serial);
  return "commander-companion-" + std::string(16 - stamp.size(), '0') + stamp +
    "-00000000-0000-4000-8000-" + std::string(12 - suffix.size(), '0') + suffix + ".json";
}
std::string AtTime(const std::string& fixture, uint64_t observed, uint64_t expires) {
  return Replace(Replace(fixture, R"("observedAtEpochMs":1000)", "\"observedAtEpochMs\":" + std::to_string(observed)),
    R"("expiresAtEpochMs":2000)", "\"expiresAtEpochMs\":" + std::to_string(expires));
}
#if defined(__linux__)
void Write(const std::string& file, const std::string& bytes) {
  std::ofstream stream(file, std::ios::binary | std::ios::trunc); stream << bytes; stream.close();
  if (!stream.good() || ::chmod(file.c_str(), 0600) != 0) { std::cerr << "TEST_FILE_WRITE_FAILED\n"; std::exit(2); }
}
void FileGuards(const std::string& fixture) {
  char path[] = "/tmp/cgwa-companion-contract.XXXXXX";
  const char* made = ::mkdtemp(path);
  if (!made) { std::cerr << "TEST_PROFILE_CREATE_FAILED\n"; std::exit(2); }
  const std::string root(made), file = root + "/commander-companion.json";
  Check(companion::ReadObservation(root).error == "SNAPSHOT_MISSING", "missing snapshot fails closed");
  Write(file, fixture);
  Check(companion::ReadObservation(root).valid, "private regular same-owner file accepted");
  ::chmod(file.c_str(), 0644);
  Check(!companion::ReadObservation(root).valid, "group/world readable snapshot rejected");
  ::chmod(file.c_str(), 0600);
  const std::string linked = root + "/hardlink";
  if (::link(file.c_str(), linked.c_str()) != 0) { std::cerr << "TEST_LINK_FAILED\n"; std::exit(2); }
  Check(!companion::ReadObservation(root).valid, "hard-linked snapshot rejected");
  ::unlink(linked.c_str());
  const std::string target = root + "/target";
  ::rename(file.c_str(), target.c_str());
  if (::symlink("target", file.c_str()) != 0) { std::cerr << "TEST_SYMLINK_FAILED\n"; std::exit(2); }
  Check(!companion::ReadObservation(root).valid, "snapshot symlink rejected");
  ::unlink(file.c_str()); ::rename(target.c_str(), file.c_str());
  const std::string alias = root + "-alias";
  if (::symlink(root.c_str(), alias.c_str()) != 0) { std::cerr << "TEST_PROFILE_ALIAS_FAILED\n"; std::exit(2); }
  Check(!companion::ReadObservation(alias).valid, "profile root symlink rejected");
  ::unlink(alias.c_str());
  ::chmod(root.c_str(), 0777);
  Check(!companion::ReadObservation(root).valid, "other-writable profile rejected");
  ::chmod(root.c_str(), 0700);
  ::chmod(root.c_str(), 0750);
  Check(!companion::ReadObservation(root).valid, "group-readable profile rejected");
  ::chmod(root.c_str(), 0700);
  Write(file, std::string(companion::kMaximumSnapshotBytes + 1, ' '));
  Check(!companion::ReadObservation(root).valid, "oversize file rejected before parsing");
  ::unlink(file.c_str());
  if (::mkfifo(file.c_str(), 0600) != 0) { std::cerr << "TEST_FIFO_FAILED\n"; std::exit(2); }
  Check(!companion::ReadObservation(root).valid, "FIFO rejected without blocking");
  ::unlink(file.c_str());
  const std::string monitor_file = root + "/commander-monitor.json";
  Write(monitor_file, MonitorSnapshot());
  Check(companion::ReadMonitor(root).valid, "private live monitor accepted");
  ::chmod(monitor_file.c_str(), 0644);
  Check(!companion::ReadMonitor(root).valid && companion::ReadMonitor(root).blocked, "world-readable live monitor rejected");
  ::chmod(monitor_file.c_str(), 0600);
  ::unlink(monitor_file.c_str());
  const std::string auto_file = root + "/commander-automation.json";
  Check(companion::ReadAutomation(root).error == "AUTOMATION_MISSING",
        "optional automation monitor missing is explicit");
  Write(auto_file, AutomationSnapshot());
  Check(companion::ReadAutomation(root).valid, "private automation monitor accepted");
  ::chmod(auto_file.c_str(), 0644);
  Check(!companion::ReadAutomation(root).valid &&
        companion::ReadAutomation(root).blocked, "world-readable automation rejected");
  ::chmod(auto_file.c_str(), 0600);
  ::unlink(auto_file.c_str());
  const std::string binding_file = root + "/commander-binding.json";
  Write(binding_file, kBinding);
  Check(companion::ReadNativeBinding(root).valid, "independent private six-field binding accepted");
  ::unlink(binding_file.c_str());

  Write(file, AtTime(fixture, 1000, 10000));
  const std::string older = root + "/" + ImmutableName(2000), newest = root + "/" + ImmutableName(3000);
  Write(older, AtTime(fixture, 2000, 10000));
  Check(companion::ReadObservation(root).observed_at == 2000, "immutable observation supersedes legacy fixed snapshot");
  Write(newest, "broken JSON");
  Check(!companion::ReadObservation(root).valid && companion::ReadObservation(root).blocked, "invalid newest immutable never falls back to older or legacy");
  Write(newest, AtTime(fixture, 3000, 3001));
  const auto expired = companion::ReadObservation(root);
  Check(expired.valid && expired.observed_at == 3000 && !companion::IsFresh(expired, 3500), "expired newest immutable never falls back to still-fresh older snapshot");
  Write(newest, AtTime(fixture, 2999, 10000));
  Check(companion::ReadObservation(root).error == "SNAPSHOT_FILENAME_TIMESTAMP_MISMATCH", "immutable filename and payload timestamps must agree");
  ::unlink(newest.c_str());
  if (::symlink("commander-companion.json", newest.c_str()) != 0) { std::cerr << "TEST_NEWEST_SYMLINK_FAILED\n"; std::exit(2); }
  Check(companion::ReadObservation(root).blocked, "newest immutable symlink stops rather than being skipped");
  ::unlink(newest.c_str());
  Write(newest, AtTime(fixture, 3000, 10000));
  ::chmod(newest.c_str(), 0644);
  Check(companion::ReadObservation(root).blocked, "newest immutable privacy violation stops rather than falling back");
  ::chmod(newest.c_str(), 0600);
  if (::link(newest.c_str(), linked.c_str()) != 0) { std::cerr << "TEST_NEWEST_LINK_FAILED\n"; std::exit(2); }
  Check(companion::ReadObservation(root).blocked, "in-flight immutable publication nlink2 is safely blocked");
  ::unlink(linked.c_str());
  Check(companion::ReadObservation(root).valid, "completed immutable publication nlink1 can be read on next refresh");
  ::unlink(newest.c_str());
  if (::mkdir(newest.c_str(), 0700) != 0) { std::cerr << "TEST_NEWEST_DIRECTORY_FAILED\n"; std::exit(2); }
  Check(companion::ReadObservation(root).blocked, "recognized newest directory is rejected not ignored");
  ::rmdir(newest.c_str());
  const std::string malformed = root + "/commander-companion-0000000000009000-UPPERCASE-invalid.json";
  Write(malformed, "ignored");
  Check(companion::ReadObservation(root).observed_at == 2000, "unrecognized filenames ignored without overriding newest candidate");
  ::unlink(malformed.c_str()); ::unlink(older.c_str());
  for (unsigned i = 0; i < 32; ++i) Write(root + "/" + ImmutableName(5000 + i, i), AtTime(fixture, 5000 + i, 10000));
  Check(companion::ReadObservation(root).observed_at == 5031, "at most32 immutable candidates deterministic lexicographic latest accepted");
  Write(root + "/" + ImmutableName(5032, 32), AtTime(fixture, 5032, 10000));
  Check(companion::ReadObservation(root).error == "SNAPSHOT_CANDIDATE_LIMIT", "33 immutable candidates stop without old snapshot fallback");
  for (unsigned i = 0; i <= 32; ++i) ::unlink((root + "/" + ImmutableName(5000 + i, i)).c_str());
  for (unsigned i = 0; i < 4096; ++i) Write(root + "/unrelated-" + std::to_string(i), "ignored");
  Check(companion::ReadObservation(root).error == "SNAPSHOT_DIRECTORY_ENTRY_LIMIT", "unrelated directory entries also have bounded scan cost");
  for (unsigned i = 0; i < 4096; ++i) ::unlink((root + "/unrelated-" + std::to_string(i)).c_str());
  Check(companion::ReadObservation(root).observed_at == 1000, "zero immutable names explicitly permits private legacy snapshot");
  ::unlink(file.c_str());
  if (::rmdir(root.c_str()) != 0) { std::cerr << "TEST_PROFILE_CLEANUP_FAILED\n"; std::exit(2); }
}
#endif
}  // namespace

int main(int argc, char** argv) {
  const std::string unknown = UnknownSnapshot(), bound = BoundSnapshot();
  const auto u = companion::ParseObservation(unknown), b = companion::ParseObservation(bound);
  const auto native = companion::ParseNativeBinding(kBinding);
  const auto monitor = companion::ParseMonitor(MonitorSnapshot());
  Check(monitor.valid && monitor.gui_available && monitor.gui_mouse && monitor.extension_count == 2, "live monitor schema accepted");
  Check(companion::IsFresh(monitor,1000) && !companion::IsFresh(monitor,11000), "live monitor freshness boundary enforced");
  Check(Has(companion::DescribeMonitor(monitor,1500),"CONNECTED") && Has(companion::DescribeMonitor(monitor,1500),"GUI control: READY"), "live monitor describes connected Core and GUI readiness");
  const auto uncertain_gui = companion::ParseMonitor(Replace(MonitorSnapshot(), R"("uncertain":false,"backend":"gnome-mutter-remote-desktop")", R"("uncertain":true,"backend":"gnome-mutter-remote-desktop")"));
  Check(uncertain_gui.valid && Has(companion::DescribeMonitor(uncertain_gui,1500),"GUI control: UNCERTAIN"), "uncertain GUI never renders READY");
  const auto uncertain_browser = companion::ParseMonitor(Replace(MonitorSnapshot(), R"("browser":{"enabled":true,"available":true,"uncertain":false)", R"("browser":{"enabled":true,"available":true,"uncertain":true)"));
  Check(uncertain_browser.valid && Has(companion::DescribeMonitor(uncertain_browser,1500),"Background browser: UNCERTAIN"), "uncertain browser never renders READY");
  Check(!companion::ParseMonitor(Replace(MonitorSnapshot(), R"("active":0)", R"("active":0,"command":"forbidden")")).valid, "monitor operations schema rejects action fields");
  Check(!companion::ParseMonitor(Replace(MonitorSnapshot(), R"("count":2)", R"("count":3)")).valid, "monitor extension count must match bounded items");
  const auto automation = companion::ParseAutomation(AutomationSnapshot());
  Check(automation.valid && automation.runner_configured && !automation.automatic_execution,
    "private automation contract accepts only configuration proof");
  Check(companion::IsFresh(automation,1000) && !companion::IsFresh(automation,11000),
    "private automation TTL exact boundary");
  Check(Has(companion::DescribeAutomation(automation,monitor,1500),"Work credits: DENY_IN_CORE") &&
        Has(companion::DescribeAutomation(automation,monitor,1500),"Automation: MANUAL_ONLY"),
        "browser displays local policy without claiming automatic execution");
  const auto wrong_sha=companion::ParseAutomation(Replace(AutomationSnapshot(),
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  Check(wrong_sha.valid && Has(companion::DescribeAutomation(wrong_sha,monitor,1500),"UNVERIFIED"),
        "mismatched device config never displays accepted automation");
  Check(!companion::ParseAutomation(Replace(AutomationSnapshot(),
    R"("creditUsage":"UNVERIFIED")",R"("creditUsage":"ZERO")")).valid,
    "billing zero claim cannot be injected into companion");
  Check(!companion::ParseAutomation(Replace(AutomationSnapshot(),
    R"("automaticExecution":false)",R"("automaticExecution":true,"command":"OPEN_WORK")")).valid,
    "unknown execution or action keys rejected");
  Check(!companion::ParseAutomation(Replace(AutomationSnapshot(),
    R"("runnerConfigured":true,"automaticExecution":false)",
    R"("runnerConfigured":false,"automaticExecution":true)")).valid,
    "automatic execution without configured runner fails shut");

  Check(u.valid && u.chat_state == "UNKNOWN" && u.project_id == "project-test", "unknown chat preserves known workflow metadata without tool claims");
  Check(b.valid && native.valid, "complete observed binding accepted");
  Check(companion::CatalogSha256({}) == "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945", "SHA256 empty catalog known vector");
  Check(companion::CatalogSha256({"b", "a"}) == "0473ef2dc0d324ab659d3580c1134e9d812035905c4781fdd6d529b0c6860e13", "SHA256 sorted catalog known vector");
  Check(companion::CatalogSha256({"tool_" + std::string(120, 'b'), "tool_" + std::string(120, 'a')}) ==
    "f4d4df0b58d355f3eb94158e835a3a628682443083cd6d8399cdabc34b8f319d", "SHA256 multi-block catalog known vector");
  Check(!companion::ParseObservation(Replace(unknown, R"("schema":1)", R"("schema":1,"schema":1)")).valid, "duplicate root key rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("schema":1)", R"("schema":1,"\u0073chema":1)")).valid, "escaped duplicate root key rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("version":"1"})", R"("version":"1","version":"1"})")).valid, "duplicate nested key rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("schema":1)", R"("schema":1,"command":"ignored")")).valid, "unknown root key rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("actionAllowed":false)", R"("actionAllowed":true)")).valid, "action permission cannot be enabled");
  Check(!companion::ParseObservation(Replace(unknown, R"("observedAtEpochMs":1000)", R"("observedAtEpochMs":1000.0)")).valid, "float timestamp rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("observedAtEpochMs":1000)", R"("observedAtEpochMs":9007199254740992)")).valid, "unsafe integer timestamp rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("expiresAtEpochMs":2000)", R"("expiresAtEpochMs":999)")).valid, "negative lifetime rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("expiresAtEpochMs":2000)", R"("expiresAtEpochMs":301001)")).valid, "lifetime beyond five minutes rejected");
  Check(!companion::IsFresh(u, 999) && companion::IsFresh(u, 1000) && !companion::IsFresh(u, 2000), "future and expiry boundary fail closed");
  Check(!companion::ParseObservation(Replace(unknown, R"("skills":[])", R"("skills":["fabricated"])")).valid, "unknown chat cannot claim skill exposure");
  Check(!companion::ParseObservation(Replace(unknown, R"(["system_status","file_read"])", R"(["file_read","file_read"])")).valid, "duplicate tools rejected");
  Check(!companion::ParseObservation(Replace(unknown, "5bb07faadcdc7512789e644acaedcdf9440cf633cc6437f9a774bce5767bf38a", std::string(64, '0'))).valid, "catalog content hash mismatch rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("deviceName":"renamed-device")", R"("deviceName":"bad\u202Ealias")")).valid, "bidi control identity rejected");
  Check(!companion::ParseObservation(Replace(unknown, "renamed-device", std::string(1, static_cast<char>(0xFF)))).valid, "invalid UTF8 rejected");
  Check(!companion::ParseObservation(Replace(unknown, R"("deviceName":"renamed-device")", R"("deviceName":"\uD800")")).valid, "unpaired surrogate rejected");
  Check(!companion::ParseObservation(std::string(65537, ' ')).valid, "parser byte cap enforced");
  std::string deep = "null"; for (int i = 0; i < 14; ++i) deep = "[" + deep + "]";
  Check(!companion::ParseObservation(deep).valid, "parser recursion cap enforced");
  std::string many = "[0"; for (int i = 0; i < 4100; ++i) many += ",0"; many += ']';
  Check(!companion::ParseObservation(many).valid, "parser node cap enforced");
  Check(!companion::ParseNativeBinding(Replace(kBinding, R"("appId":"app-canonical")", R"("appId":null)")).valid, "incomplete independent binding stays unproven");
  Check(!companion::ParseNativeBinding(Replace(kBinding, R"("appId":"app-canonical")", R"("schema":1,"appId":"app-canonical")")).valid, "native binding schema closed to six keys");
  Check(companion::IsCanonicalChatUrl("https://chatgpt.com/g/g-test/c/chat-test") && !companion::IsCanonicalChatUrl("https://chatgpt.com.evil/c/chat-test") &&
    !companion::IsCanonicalChatUrl("https://chatgpt.com/c/chat-test?x=1") && !companion::IsCanonicalChatUrl("https://chatgpt.com/c/%2e%2e"), "chat origin, query and encoded aliases rejected");
  Check(companion::IsCanonicalProjectRoot("C:\\Projects\\test") && companion::SameProjectRoot("C:\\Projects\\test", "c:\\projects\\TEST") &&
    !companion::SameProjectRoot("/workspace/Project", "/workspace/project"), "remote Windows roots compared separately from local POSIX roots");
  Check(!companion::IsCanonicalProjectRoot("/") && !companion::IsCanonicalProjectRoot("/workspace/../project") &&
    !companion::IsCanonicalProjectRoot("C:\\Projects\\test.") && !companion::IsCanonicalProjectRoot("\\\\host\\share"), "root-only traversal and Windows aliases rejected");
  Check(Has(companion::Describe(b, native, "https://chatgpt.com/c/chat-test", 1500), "BOUND ("), "exact native active chat admits observed binding");
  Check(!Has(companion::Describe(b, {}, "https://chatgpt.com/c/chat-test", 1500), "BOUND (") &&
    Has(companion::Describe(b, {}, "https://chatgpt.com/c/chat-test", 1500), "NATIVE_BINDING_MISSING"), "snapshot alone cannot confirm native session");
  Check(Has(companion::Describe(b, native, "https://chatgpt.com/c/other-chat", 1500), "STOP"), "active chat mismatch stops native context");
  Check(!Has(companion::Describe(b, native, "", 1500), "BOUND (") &&
    Has(companion::Describe(b, native, "", 1500), "ACTIVE_CHAT_UNPROVEN"), "navigation or login URL stays unknown instead of using cached binding");
  const auto wrong = companion::ParseNativeBinding(Replace(kBinding, "profile-test", "profile-other"));
  Check(Has(companion::Describe(b, wrong, "https://chatgpt.com/c/chat-test", 1500), "NATIVE_BINDING_MISMATCH"), "independent profile mismatch stops context");
  Check(Has(companion::Describe(u, native, "https://chatgpt.com/c/chat-test", 1500), "UNKNOWN"), "backend catalog never becomes current chat exposure");
  Check(!Has(companion::Describe(b, native, "https://chatgpt.com/c/chat-test", 2000), "BOUND (") &&
    Has(companion::Describe(b, native, "https://chatgpt.com/c/chat-test", 2000), "SNAPSHOT_EXPIRED"), "expired display drops previous confirmations");
  Check(!companion::ParseObservation(Replace(bound, R"("state":"BOUND","reasonCodes":[])", R"("state":"UNPROVEN","reasonCodes":[])")).valid, "bound project cannot outlive reconciliation");
  Check(!companion::ParseObservation(Replace(bound, R"("identity":{"appId":"app-canonical")", R"("identity":{"appId":null)")).valid, "missing observed AppID cannot inherit claimed binding");
  Check(!companion::ParseObservation(Replace(bound, R"("projectId":"workflow-test")", R"("projectId":"other-workflow")")).valid, "wrong project ID cannot inherit workflow binding");
  Check(companion::ParseObservation(Replace(unknown, "renamed-device", "دستگاه Saeed PC")).valid, "mutable device alias may contain spaces and Persian without becoming identity");
  std::string long_alias; for (int i = 0; i < 100; ++i) long_alias += "س";
  Check(companion::ParseObservation(Replace(unknown, "renamed-device", long_alias)).valid, "Unicode display alias cap counts characters not UTF8 bytes");
  std::string unicode_root = "/"; for (int i = 0; i < 3000; ++i) unicode_root += "س";
  Check(companion::IsCanonicalProjectRoot(unicode_root), "remote Unicode project root cap matches JS UTF16 units");
  Check(companion::ParseObservation(Replace(bound, R"("evidenceSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")", R"("evidenceSha256":null)")).valid, "binding confirmation does not invent checkpoint evidence");
  Check(!companion::ParseObservation(Replace(unknown, R"("backend":{"state":"CONFIRMED")", R"("backend":{"state":"UNAVAILABLE")")).valid, "unavailable backend cannot retain claimed tool catalog");
  Check(!companion::ParseObservation(Replace(unknown, R"("state":"UNPROVEN","reasonCodes":["CHAT_OBSERVATION_MISSING"])", R"("state":"STOP","reasonCodes":["UNCERTAIN_OPERATION"])")).valid, "STOP reconciliation requires project STOP");
  std::string conflicting = Replace(unknown, R"({"appId":null,"accountId":null,"profileId":null,"workflowId":null,"projectRoot":null,"chatUrl":null})", kBinding);
  Check(!companion::ParseObservation(conflicting).valid, "known mismatched project metadata cannot remain UNPROVEN instead of STOP");
  const auto missing_binding = companion::ParseObservation(Replace(unknown, R"("projectId":"project-test")", R"("projectId":"workflow-test")"));
  const auto missing_rows = companion::Describe(missing_binding, native, "https://chatgpt.com/c/chat-test", 1500);
  Check(!Has(missing_rows, "STOP") && Has(missing_rows, "ACTIVE_CHAT_UNPROVEN"), "missing observation binding is unknown not a fabricated conflict");
  Check(!companion::ParseObservation(Replace(unknown, "renamed-device", "sk-" + std::string(24, 'a'))).valid, "credential-like device alias rejected before native display");
  uint64_t filename_time = 0;
  Check(companion::ImmutableSnapshotName(ImmutableName(1900000000000), filename_time) && filename_time == 1900000000000ULL,
    "strict padded immutable filename recognizes observed time");
  Check(!companion::ImmutableSnapshotName(Replace(ImmutableName(1000), "4000-8000", "4000-800A"), filename_time) &&
    !companion::ImmutableSnapshotName("commander-companion.json", filename_time) &&
    !companion::ImmutableSnapshotName(ImmutableName(1000) + ".tmp", filename_time), "uppercase UUID legacy and temporary files are not immutable candidates");
#if defined(__linux__)
  FileGuards(unknown);
#elif defined(_WIN32)
  const auto windows_monitor = companion::ReadMonitor(companion::ProfileRoot());
  Check(!windows_monitor.valid && windows_monitor.error != "WINDOWS_PRIVATE_FILE_GUARD_UNAVAILABLE", "Windows live monitor uses the native ACL guard");
  const auto windows_observation = companion::ReadObservation(companion::ProfileRoot());
  Check(!windows_observation.valid &&
        windows_observation.error != "WINDOWS_PRIVATE_FILE_GUARD_UNAVAILABLE",
        "Windows companion observation uses the native ACL guard");
  const auto windows_binding = companion::ReadNativeBinding(companion::ProfileRoot());
  Check(!windows_binding.valid &&
        windows_binding.error != "WINDOWS_PRIVATE_FILE_GUARD_UNAVAILABLE",
        "Windows native binding uses the native ACL guard");
#else
  std::cout << "UNPROVEN private-file guards on unsupported runtime\n";
#endif
  if (argc == 3 && std::string(argv[1]) == "--fixture") {
    std::ifstream fixture(argv[2], std::ios::binary);
    std::string text;
    char chunk[4096];
    while (fixture && text.size() <= companion::kMaximumSnapshotBytes) {
      fixture.read(chunk, sizeof(chunk)); text.append(chunk, static_cast<size_t>(fixture.gcount()));
    }
    const auto emitted = companion::ParseObservation(text);
    Check(emitted.valid && emitted.chat_state == "UNKNOWN" && emitted.app_id.empty(), "real Commander emitted fixture stays unknown without host evidence");
  } else if (argc != 1) { std::cerr << "Usage: companion-contract-test [--fixture file]\n"; return 2; }
  std::cout << "COMPANION_CONTRACT passed=" << passed << " failed=" << failed << '\n';
  return failed ? 1 : 0;
}
