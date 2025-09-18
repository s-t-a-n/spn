#pragma once

namespace spn::di::phase {

// convenient tags for use in the DI system for phased execution

struct backend_init {};
struct register_settings {};
struct load_settings {};
struct configure {};
struct init {};
struct runtime {};

} // namespace spn::di::phase