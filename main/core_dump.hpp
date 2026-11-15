#pragma once

namespace core_dump {

// Whether flash holds an intact core dump, fixed for the whole boot. Reads the
// whole dump to verify its checksum.
bool stored();

}  // namespace core_dump
