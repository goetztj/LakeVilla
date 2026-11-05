#pragma once
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

#include "Connectors/Connector.hpp"
#include "Helpers/Helper.hpp"

namespace LHTransactions {

// A reserved log version (lease). The marker object in the log carries a
// deadline in object-store time; after it (plus a grace period) waiting
// transactions may take the version over. All overwrites of the marker are
// conditional on its ETag, so a late commit of the holder and a takeover
// can never both succeed.
struct Marker {
  std::string marker_path;

  // ETag of the marker object ("" if unknown: unconditional writes)
  std::string etag;

  // end of the lease in store time (ms since the epoch; 0 = no lease)
  int64_t deadline_ms = 0;

  Marker(std::string&& marker_path,
         StorageConnector::Connector* connector_ptr, std::string etag = "",
         int64_t deadline_ms = 0);

  // releases a reserved version that was neither committed nor released
  ~Marker();

  void setInvalid();

  // true while the reserved version has not been committed or released
  bool is_valid() const { return this->valid; }

  // replaces the marker with an empty commit (conditional on the ETag);
  // true if the version is no longer reserved by this marker afterwards
  // (released, or already taken over by another transaction)
  bool release();

  // the empty commit that releases (or takes over) a version
  static std::string empty_commit(const std::string& mode);

 private:
  bool valid;

  StorageConnector::Connector* connector_ptr;
};
};  // namespace LHTransactions
