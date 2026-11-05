#pragma once

#include <cstdint>

namespace LHTransactions {

enum class Isolation {
  // every table a transaction read must be unchanged at commit (default)
  Serializable,
  // only write-write conflicts abort (first committer wins)
  Snapshot
};

// Protocol settings (lvconfig.conf: ISOLATION, MARKER_LEASE_MS, ...)
struct ProtocolOptions {
  Isolation isolation = Isolation::Serializable;
  // a marker reserves its version for this long (object-store time)
  uint32_t marker_lease_ms = 30000;
  // waiters disregard an expired marker only after this additional time
  uint32_t marker_grace_ms = 100;
  // validation rounds with redo before a transaction aborts
  uint32_t max_redos = 3;
  // LV[Read]: repair the global log after repeated validation failures
  bool global_log_repair = false;
  uint32_t global_log_repair_after = 3;
  // LV[Read]: check for commits newer than the global log at begin
  bool global_log_check_at_begin = false;
  // a commit of a version that is a multiple of the checkpoint interval
  // writes a Delta checkpoint (like Spark)
  bool checkpoints = true;
  // 0: the table property delta.checkpointInterval (default 10)
  uint32_t checkpoint_interval = 0;
};

// process-wide defaults for new managers
void set_default_protocol_options(const ProtocolOptions& options);
ProtocolOptions default_protocol_options();

}  // namespace LHTransactions
